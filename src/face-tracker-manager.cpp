#include <obs-module.h>
#include <util/platform.h>
#include <algorithm>
#include <utility>
#include "plugin-macros.generated.h"
#include "face-tracker-manager.hpp"
#include "face-detector-dlib-hog.h"
#include "face-detector-dlib-cnn.h"
#ifdef HAVE_YUNET
#include "face-detector-yunet.h"
#endif
#ifdef HAVE_SCRFD
#include "face-detector-scrfd.h"
#endif
#include "face-tracker-dlib.h"
#include "texture-object.h"
#include "helper.hpp"

// #define debug_track(fmt, ...) blog(LOG_INFO, fmt, __VA_ARGS__)
// #define debug_detect(fmt, ...) blog(LOG_INFO, fmt, __VA_ARGS__)
#define debug_track(fmt, ...)
#define debug_detect(fmt, ...)
#define debug_track_thread(fmt, ...) // blog(LOG_INFO, fmt, __VA_ARGS__)

#define DIR_DLIB_HOG "dlib_hog_model"
#define DIR_DLIB_CNN "dlib_cnn_model"
#define DIR_DLIB_LANDMARK "dlib_face_landmark_model"
#define DIR_YUNET "yunet_model"
#define DIR_SCRFD "scrfd_model"

static constexpr size_t max_idle_trackers = 2;

face_tracker_manager::face_tracker_manager()
{
	upsize_l = upsize_r = upsize_t = upsize_b = 0.0f;
	scale = 0.0f;
	tracking_threshold = 1e-2f;
	landmark_detection_data = NULL;
	crop_cur.x0 = crop_cur.x1 = crop_cur.y0 = crop_cur.y1 = 0.0f;
	tick_cnt = detect_tick = 0;
	next_detection_ns = 0;
	next_tracking_ns = 0;
	target_lost_since_ns = 0;
	selected_target_valid = false;
	selected_target = rect_s{0, 0, 0, 0, 0.0f};
	detector_in_progress = false;
	reset_requested.store(false, std::memory_order_relaxed);
	target_selection = target_selection_sticky;
	detector_interval_ms = 2000;
	detector_interval_lost_ms = 250;
	tracker_interval_ms = 50;
	target_stick_ms = 1500;
	detector_gpu_device = 0;
	yunet_score_threshold = 0.6f;
	yunet_nms_threshold = 0.3f;
	yunet_max_input_size = 320;
	scrfd_score_threshold = 0.5f;
	scrfd_nms_threshold = 0.4f;
	scrfd_input_size = 640;
	scrfd_use_cuda = true;
	scrfd_model_variant = scrfd_model_2_5g;
	detect = NULL;
}

face_tracker_manager::~face_tracker_manager()
{
	std::lock_guard<std::mutex> guard(state_mutex);
	for (auto &t : trackers_idlepool) {
		if (t.tracker) {
			t.tracker->stop();
			delete t.tracker;
			t.tracker = NULL;
		}
	}
	for (auto &t : trackers) {
		if (t.tracker) {
			t.tracker->stop();
			delete t.tracker;
			t.tracker = NULL;
		}
	}
	if (detect) {
		detect->stop();
		delete detect;
	}
	bfree(landmark_detection_data);
}

inline void face_tracker_manager::retire_tracker(int ix)
{
	debug_track_thread("%p retire_tracker(%d %p)", this, ix, trackers[ix].tracker);
	trackers_idlepool.push_back(trackers[ix]);
	trackers[ix].tracker->request_suspend();
	trackers.erase(trackers.begin() + ix);
	while (trackers_idlepool.size() > max_idle_trackers) {
		auto &idle = trackers_idlepool.back();
		if (idle.tracker) {
			idle.tracker->stop();
			delete idle.tracker;
		}
		trackers_idlepool.pop_back();
	}
}

inline bool face_tracker_manager::is_low_confident(const tracker_inst_s &t, float th1)
{
	if (t.att * t.rect.score <= th1)
		return true;

	if (t.att * t.rect.score <= tracking_threshold * t.score_first)
		return true;

	return false;
}

void face_tracker_manager::remove_duplicated_tracker()
{
	for (size_t i = 0; i < trackers.size(); i++) {
		if (trackers[i].state != tracker_inst_s::tracker_state_available)
			continue;

		rect_s r = trackers[i].rect;
		float a0 = rect_area(r);
		if (a0 <= 0.0f) {
			retire_tracker((int)i);
			i--;
			continue;
		}
		float a_overlap_sum = 0.0f;
		bool to_remove = false;
		for (size_t j = i + 1; j < trackers.size() && !to_remove; j++) {
			if (trackers[j].state != tracker_inst_s::tracker_state_available)
				continue;
			float a = (float)common_area(r, trackers[j].rect);
			a_overlap_sum += a;
			if (a * 10 > a0 && a_overlap_sum * 2 > a0)
				to_remove = true;
		}

		if (to_remove) {
			retire_tracker(i);
			i--;
		}
	}
}

inline void face_tracker_manager::attenuate_tracker()
{
	for (size_t i = 0; i < trackers.size(); i++) {
		if (trackers[i].state != tracker_inst_s::tracker_state_available)
			continue;
		struct tracker_inst_s &t = trackers[i];

		float a1 = rect_area(t.rect);
		if (a1 <= 0.0f) {
			t.att = 0.0f;
			continue;
		}
		float amax = a1 * 0.1f;
		for (size_t j = 0; j < detect_rects.size(); j++) {
			rect_s r = detect_rects[j];
			float a = (float)common_area(r, t.rect);
			if (a > amax)
				amax = a;
		}

		t.att *= powf(amax / a1, 0.1f); // if no faces, remove the tracker
	}

	float score_max = 1e-17f;
	for (size_t i = 0; i < trackers.size(); i++) {
		if (trackers[i].state == tracker_inst_s::tracker_state_available) {
			float s = trackers[i].att * trackers[i].rect.score;
			if (s > score_max)
				score_max = s;
		}
	}

	for (size_t i = 0; i < trackers.size(); i++) {
		if (trackers[i].state != tracker_inst_s::tracker_state_available)
			continue;
		if (!is_low_confident(trackers[i], 1e-2f * score_max))
			continue;

		retire_tracker(i);
		i--;
	}
}

inline void face_tracker_manager::copy_detector_to_tracker()
{
	size_t i_tracker;
	for (i_tracker = 0; i_tracker < trackers.size(); i_tracker++)
		if (trackers[i_tracker].tick_cnt == detect_tick &&
		    trackers[i_tracker].state == tracker_inst_s::tracker_state_e::tracker_state_reset_texture)
			break;
	if (i_tracker >= trackers.size())
		return;

	if (detect_rects.size() <= 0) {
		retire_tracker(i_tracker);
		return;
	}

	struct tracker_inst_s &t = trackers[i_tracker];

	struct rect_s r = detect_rects[select_rect(detect_rects)];
	int w = r.x1 - r.x0;
	int h = r.y1 - r.y0;
	r.x0 -= w * upsize_l;
	r.x1 += w * upsize_r;
	r.y0 -= h * upsize_t;
	r.y1 += h * upsize_b;
	t.tracker->set_position(r); // TODO: consider how to track two or more faces.
	t.tracker->set_upsize_info(rectf_s{upsize_l, upsize_t, upsize_r, upsize_b});
	t.tracker->start();
	t.state = tracker_inst_s::tracker_state_constructing;
}

inline void face_tracker_manager::stage_to_detector(const std::shared_ptr<texture_object> &cvtex)
{
	if (!detect || detect->trylock())
		return;

	// get previous results
	if (detector_in_progress) {
		detect->get_faces(detect_rects);
		for (size_t i = 0; i < detect_rects.size(); i++)
			debug_detect("stage_to_detector: detect_rects %d %d %d %d %d %f", i, detect_rects[i].x0,
				     detect_rects[i].y0, detect_rects[i].x1, detect_rects[i].y1, detect_rects[i].score);
		attenuate_tracker();
		copy_detector_to_tracker();
		detector_in_progress = false;
	}

	uint64_t now = os_gettime_ns();
	if (now < next_detection_ns) {
		detect->unlock();
		return;
	}

	if (cvtex) {
		detect->set_texture(cvtex, detector_crop_l, detector_crop_r, detector_crop_t, detector_crop_b);
		if (detector_engine == engine_dlib_hog) {
			if (auto *d = dynamic_cast<face_detector_dlib_hog *>(detect))
				d->set_model(detector_dlib_hog_model.c_str());
		} else if (detector_engine == engine_dlib_cnn) {
			if (auto *d = dynamic_cast<face_detector_dlib_cnn *>(detect)) {
				d->set_model(detector_dlib_cnn_model.c_str());
				d->set_gpu_device(detector_gpu_device);
			}
		}
#ifdef HAVE_YUNET
		else if (detector_engine == engine_yunet) {
			if (auto *d = dynamic_cast<face_detector_yunet *>(detect)) {
				d->set_model(detector_yunet_model.c_str());
				d->set_config(yunet_score_threshold, yunet_nms_threshold, yunet_max_input_size);
			}
		}
#endif
#ifdef HAVE_SCRFD
		else if (detector_engine == engine_scrfd) {
			if (auto *d = dynamic_cast<face_detector_scrfd *>(detect)) {
				d->set_model(detector_scrfd_model.c_str());
				d->set_config(scrfd_score_threshold, scrfd_nms_threshold, scrfd_input_size,
					      scrfd_use_cuda, detector_gpu_device);
			}
		}
#endif
		detect->signal();
		detector_in_progress = true;
		detect_tick = tick_cnt;
		int interval_ms = tracker_rects.empty() ? detector_interval_lost_ms : detector_interval_ms;
		next_detection_ns = now + (uint64_t)std::max(interval_ms, 50) * 1000000ULL;

		struct tracker_inst_s t;
		t.rect = rect_s{0, 0, 0, 0, 0.0f};
		t.crop_rect = rectf_s{0.0f, 0.0f, 0.0f, 0.0f};
		t.att = 0.0f;
		t.score_first = 0.0f;
		if (trackers_idlepool.size() > 0) {
			t.tracker = trackers_idlepool[0].tracker;
			trackers_idlepool[0].tracker = NULL;
			trackers_idlepool.pop_front();
		} else {
			debug_track_thread(
				"%p No available idle tracker, creating new tracker thread. There are %d existing thread.",
				this, trackers.size());
			t.tracker = new face_tracker_dlib();
			for (size_t i = 0; i < trackers.size(); i++) {
				debug_track_thread("%p existing tracker[%d]: state=%d", this, i,
						   (int)trackers[i].state);
			}
		}
		t.crop_tracker = crop_cur;
		t.state = tracker_inst_s::tracker_state_e::tracker_state_reset_texture;
		t.tick_cnt = tick_cnt;
		t.tracker->set_texture(cvtex);
		t.tracker->set_landmark_detection(landmark_detection_data);
		if (!landmark_detection_data)
			t.landmark.clear();
		trackers.push_back(t);
	}

	detect->unlock();
}

inline bool face_tracker_manager::stage_surface_to_tracker(struct tracker_inst_s &t,
							   const std::shared_ptr<texture_object> &cvtex)
{
	if (!cvtex)
		return false;
	t.tracker->set_texture(cvtex);
	t.crop_tracker = crop_cur;
	return true;
}

inline void face_tracker_manager::stage_to_trackers(const std::shared_ptr<texture_object> &cvtex)
{
	bool have_new_tracker = false;
	for (size_t i = 0; i < trackers.size(); i++) {
		struct tracker_inst_s &t = trackers[i];
		if (t.state == tracker_inst_s::tracker_state_constructing) {
			if (!t.tracker->trylock()) {
				if (stage_surface_to_tracker(t, cvtex))
					t.tracker->signal();
				t.tracker->unlock();
				t.state = tracker_inst_s::tracker_state_first_track;
			}
		} else if (t.state == tracker_inst_s::tracker_state_first_track) {
			if (!t.tracker->trylock()) {
				bool ret = t.tracker->get_face(t.rect);
				t.crop_rect = t.crop_tracker;
				debug_track("tracker_state_first_track %p %d %d %d %d %f", t.tracker, t.rect.x0,
					    t.rect.y0, t.rect.x1, t.rect.y1, t.rect.score);
				t.att = 1.0f;
				t.score_first = t.rect.score;
				if (!ret || !landmark_detection_data || !t.tracker->get_landmark(t.landmark))
					t.landmark.resize(0);
				if (stage_surface_to_tracker(t, cvtex))
					t.tracker->signal();
				t.tracker->unlock();
				if (ret) {
					t.state = tracker_inst_s::tracker_state_available;
					have_new_tracker = true;
				}
			}
		} else if (t.state == tracker_inst_s::tracker_state_available) {
			if (!t.tracker->trylock()) {
				bool ret = t.tracker->get_face(t.rect);
				t.crop_rect = t.crop_tracker;
				debug_track("tracker_state_available %p %d %d %d %d %f landmark=%d", t.tracker,
					    t.rect.x0, t.rect.y0, t.rect.x1, t.rect.y1, t.rect.score,
					    t.landmark.size());
				if (!ret || !landmark_detection_data || !t.tracker->get_landmark(t.landmark))
					t.landmark.resize(0);
				if (stage_surface_to_tracker(t, cvtex))
					t.tracker->signal();
				t.tracker->unlock();
			}
		}
	}

	if (have_new_tracker)
		remove_duplicated_tracker();
}

size_t face_tracker_manager::select_rect(const std::vector<rect_s> &rects) const
{
	return select_rect_index(rects, (enum rect_selection_policy)target_selection, selected_target_valid,
				 selected_target, crop_cur);
}

void face_tracker_manager::update_tracker_rects()
{
	size_t n = 0;
	for (size_t i = 0; i < trackers.size(); i++) {
		if (trackers[i].state != face_tracker_manager::tracker_inst_s::tracker_state_available)
			continue;

		float score = trackers[i].rect.score * trackers[i].att;

		if (score <= 0.0f || isnan(score))
			continue;

		if (tracker_rects.size() <= n)
			tracker_rects.resize(n + 1);
		auto &r = tracker_rects[n++];

		r.rect = trackers[i].rect;
		r.rect.score = score;
		r.crop_rect = trackers[i].crop_rect;
		r.landmark = trackers[i].landmark;
	}

	if (tracker_rects.size() > n)
		tracker_rects.resize(n);

	uint64_t now = os_gettime_ns();
	if (tracker_rects.empty()) {
		if (!target_lost_since_ns) {
			target_lost_since_ns = now;
			next_detection_ns = 0;
		}
		if (now - target_lost_since_ns > (uint64_t)std::max(target_stick_ms, 0) * 1000000ULL)
			selected_target_valid = false;
		return;
	}

	target_lost_since_ns = 0;
	std::vector<rect_s> rects;
	rects.reserve(tracker_rects.size());
	for (const auto &tracker : tracker_rects)
		rects.push_back(tracker.rect);
	size_t selected = select_rect(rects);
	tracker_rect_s target = std::move(tracker_rects[selected]);
	tracker_rects.clear();
	tracker_rects.push_back(std::move(target));
	selected_target = tracker_rects[0].rect;
	selected_target_valid = true;
}

void face_tracker_manager::tick(float second)
{
	std::lock_guard<std::mutex> guard(state_mutex);
	(void)second;
	if (reset_requested.exchange(false, std::memory_order_acq_rel)) {
		for (auto &tracker : trackers)
			tracker.att = 0.0f;
		detect_rects.clear();
		selected_target_valid = false;
		target_lost_since_ns = 0;
		next_detection_ns = 0;
		next_tracking_ns = 0;
	}

	tick_cnt += 1;

	update_tracker_rects();
}

void face_tracker_manager::post_render()
{
	std::lock_guard<std::mutex> guard(state_mutex);
	uint64_t now = os_gettime_ns();
	if (now < next_tracking_ns)
		return;
	next_tracking_ns = interval_deadline_ns(now, tracker_interval_ms);

	// Harvest completed work before deciding whether another CPU frame is
	// needed. This avoids GPU readback while the detector is busy or sleeping.
	stage_to_detector(nullptr);
	stage_to_trackers(nullptr);
	bool tracker_needs_frame = std::any_of(trackers.begin(), trackers.end(), [](const tracker_inst_s &tracker) {
		return tracker.state == tracker_inst_s::tracker_state_first_track ||
		       tracker.state == tracker_inst_s::tracker_state_available;
	});
	bool detector_needs_frame = detect && !detector_in_progress && now >= next_detection_ns;
	if (!tracker_needs_frame && !detector_needs_frame)
		return;

	// Capturing a filter frame requires a GPU-to-CPU transfer. Capture once and
	// share the immutable frame between the detector and every active tracker.
	auto cvtex = get_cvtex();
	if (!cvtex) {
		next_tracking_ns = 0;
		return;
	}
	stage_to_detector(cvtex);
	stage_to_trackers(cvtex);
}

bool face_tracker_manager::frame_due() const
{
	std::lock_guard<std::mutex> guard(state_mutex);
	return os_gettime_ns() >= next_tracking_ns;
}

static void update_detector(face_tracker_manager *ftm, enum face_tracker_manager::detector_engine_e detector_engine)
{
	if (ftm->detect) {
		ftm->detect->stop();
		delete ftm->detect;
		ftm->detect = NULL;
	}

	switch (detector_engine) {
	case face_tracker_manager::engine_dlib_hog:
		ftm->detect = new face_detector_dlib_hog();
		break;
	case face_tracker_manager::engine_dlib_cnn:
		ftm->detect = new face_detector_dlib_cnn();
		break;
#ifdef HAVE_YUNET
	case face_tracker_manager::engine_yunet:
		ftm->detect = new face_detector_yunet();
		break;
#endif
#ifdef HAVE_SCRFD
	case face_tracker_manager::engine_scrfd:
		ftm->detect = new face_detector_scrfd();
		break;
#endif
	default:
		blog(LOG_ERROR, "unknown detector_engine %d", (int)detector_engine);
	}

	ftm->detector_engine = detector_engine;

	if (ftm->detect)
		ftm->detect->start();
}

void face_tracker_manager::update(obs_data_t *settings)
{
	std::lock_guard<std::mutex> guard(state_mutex);
	upsize_l = obs_data_get_double(settings, "upsize_l");
	upsize_r = obs_data_get_double(settings, "upsize_r");
	upsize_t = obs_data_get_double(settings, "upsize_t");
	upsize_b = obs_data_get_double(settings, "upsize_b");
	scale.store((float)obs_data_get_double(settings, "scale"), std::memory_order_release);
	auto _detector_engine = (enum detector_engine_e)obs_data_get_int(settings, "detector_engine");
#ifndef HAVE_YUNET
	if (_detector_engine == engine_yunet)
		_detector_engine = engine_dlib_cnn;
#endif
#ifndef HAVE_SCRFD
	if (_detector_engine == engine_scrfd)
		_detector_engine = engine_dlib_cnn;
#endif
	if (_detector_engine != engine_dlib_hog && _detector_engine != engine_dlib_cnn
#ifdef HAVE_YUNET
	    && _detector_engine != engine_yunet
#endif
#ifdef HAVE_SCRFD
	    && _detector_engine != engine_scrfd
#endif
	) {
		blog(LOG_WARNING, "invalid detector engine %d; falling back to dlib HOG", (int)_detector_engine);
		_detector_engine = engine_dlib_hog;
	}
	if (_detector_engine != detector_engine) {
		update_detector(this, _detector_engine);
		detector_in_progress = false;
		detect_rects.clear();
		next_detection_ns = 0;
		next_tracking_ns = 0;
		for (size_t i = trackers.size(); i-- > 0;) {
			if (trackers[i].state == tracker_inst_s::tracker_state_reset_texture)
				retire_tracker((int)i);
		}
	}
	detector_dlib_hog_model = obs_data_get_string(settings, "detector_dlib_hog_model");
	detector_dlib_cnn_model = obs_data_get_string(settings, "detector_dlib_cnn_model");
	detector_yunet_model = obs_data_get_string(settings, "detector_yunet_model");
	const std::string legacy_scrfd_model = obs_data_get_string(settings, "detector_scrfd_model");
	const std::string packaged_scrfd_2_5g = obs_data_get_string(settings, "detector_scrfd_2_5g_model");
	const std::string packaged_scrfd_10g = obs_data_get_string(settings, "detector_scrfd_10g_model");
	int scrfd_variant = (int)obs_data_get_int(settings, "scrfd_model_variant");
	if (!obs_data_has_user_value(settings, "scrfd_model_variant") &&
	    obs_data_has_user_value(settings, "detector_scrfd_model") &&
	    legacy_scrfd_model != packaged_scrfd_2_5g) {
		scrfd_variant = (int)scrfd_model_custom;
		obs_data_set_int(settings, "scrfd_model_variant", scrfd_variant);
	}
	if (scrfd_variant < (int)scrfd_model_2_5g || scrfd_variant > (int)scrfd_model_custom)
		scrfd_variant = (int)scrfd_model_2_5g;
	bool activating_scrfd_10g = scrfd_model_variant != scrfd_model_10g &&
				       scrfd_variant == (int)scrfd_model_10g;
	scrfd_model_variant = (enum scrfd_model_variant_e)scrfd_variant;
	if (activating_scrfd_10g) {
		blog(LOG_WARNING,
		     "SCRFD 10G selected: expect substantially higher GPU usage and OBS rendering latency than 2.5G");
	}
	detector_scrfd_model = packaged_scrfd_2_5g;
	if (scrfd_model_variant == scrfd_model_10g)
		detector_scrfd_model = packaged_scrfd_10g;
	else if (scrfd_model_variant == scrfd_model_custom)
		detector_scrfd_model = legacy_scrfd_model;
	yunet_score_threshold = std::clamp((float)obs_data_get_double(settings, "yunet_score_threshold"), 0.01f, 0.99f);
	yunet_nms_threshold = std::clamp((float)obs_data_get_double(settings, "yunet_nms_threshold"), 0.01f, 0.99f);
	yunet_max_input_size = std::clamp((int)obs_data_get_int(settings, "yunet_max_input_size"), 160, 1280);
	scrfd_score_threshold = std::clamp((float)obs_data_get_double(settings, "scrfd_score_threshold"), 0.01f, 0.99f);
	scrfd_nms_threshold = std::clamp((float)obs_data_get_double(settings, "scrfd_nms_threshold"), 0.01f, 0.99f);
	scrfd_input_size = std::clamp((int)obs_data_get_int(settings, "scrfd_input_size"), 160, 1280);
	scrfd_use_cuda = obs_data_get_bool(settings, "scrfd_use_cuda");
	detector_gpu_device = (int)obs_data_get_int(settings, "detector_gpu_device");
	detector_crop_l = obs_data_get_int(settings, "detector_crop_l");
	detector_crop_r = obs_data_get_int(settings, "detector_crop_r");
	detector_crop_t = obs_data_get_int(settings, "detector_crop_t");
	detector_crop_b = obs_data_get_int(settings, "detector_crop_b");
	int selection = (int)obs_data_get_int(settings, "target_selection");
	if (selection < (int)target_selection_sticky || selection > (int)target_selection_first)
		selection = (int)target_selection_sticky;
	target_selection = (enum target_selection_e)selection;
	detector_interval_ms = std::clamp((int)obs_data_get_int(settings, "detector_interval_ms"), 100, 10000);
	detector_interval_lost_ms = std::clamp((int)obs_data_get_int(settings, "detector_interval_lost_ms"), 50, 5000);
	tracker_interval_ms = std::clamp((int)obs_data_get_int(settings, "tracker_interval_ms"), 16, 250);
	target_stick_ms = std::clamp((int)obs_data_get_int(settings, "target_stick_ms"), 0, 10000);
	bool landmark_detection = obs_data_get_bool(settings, "landmark_detection");
	bfree(landmark_detection_data);
	landmark_detection_data = NULL;
	if (landmark_detection)
		landmark_detection_data = bstrdup(obs_data_get_string(settings, "landmark_detection_data"));
	if (obs_data_get_bool(settings, "tracking_th_en"))
		tracking_threshold = from_dB(obs_data_get_double(settings, "tracking_th_dB"));
	else
		tracking_threshold = 0.0;
}

static bool tracking_th_en_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	bool tracking_th_en = obs_data_get_bool(settings, "tracking_th_en");
	obs_property_t *tracking_th_dB = obs_properties_get(props, "tracking_th_dB");
	obs_property_set_visible(tracking_th_dB, tracking_th_en);
	return true;
}

#ifdef HAVE_SCRFD
static bool scrfd_model_variant_modified(obs_properties_t *props, obs_property_t *, obs_data_t *settings)
{
	int variant = (int)obs_data_get_int(settings, "scrfd_model_variant");
	obs_property_t *warning = obs_properties_get(props, "scrfd_10g_warning");
	obs_property_t *custom_model = obs_properties_get(props, "detector_scrfd_model");
	if (warning)
		obs_property_set_visible(warning, variant == (int)face_tracker_manager::scrfd_model_10g);
	if (custom_model)
		obs_property_set_visible(custom_model, variant == (int)face_tracker_manager::scrfd_model_custom);
	return true;
}
#endif

void face_tracker_manager::get_properties(obs_properties_t *pp)
{
	obs_property_t *p;
	std::string data_path = obs_get_module_data_path(obs_current_module());

	{
		obs_properties_t *group = obs_properties_create();
		p = obs_properties_add_list(group, "detector_engine", obs_module_text("Detector"), OBS_COMBO_TYPE_LIST,
					    OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("Detector.dlib.hog"), (int)engine_dlib_hog);
		obs_property_list_add_int(p, obs_module_text("Detector.dlib.cnn"), (int)engine_dlib_cnn);
#ifdef HAVE_YUNET
		obs_property_list_add_int(p, obs_module_text("Detector.yunet"), (int)engine_yunet);
#endif
#ifdef HAVE_SCRFD
		obs_property_list_add_int(p, obs_module_text("Detector.scrfd"), (int)engine_scrfd);
#endif

		p = obs_properties_add_list(group, "detector_gpu_device", obs_module_text("GPU.Device"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		std::vector<std::pair<int, std::string>> gpu_devices;
		auto dlib_gpu_devices = face_detector_dlib_cnn::get_gpu_device_names();
		for (size_t i = 0; i < dlib_gpu_devices.size(); i++)
			gpu_devices.emplace_back((int)i, std::move(dlib_gpu_devices[i]));
#if defined(HAVE_SCRFD) && defined(HAVE_ONNXRUNTIME_CUDA)
		if (gpu_devices.empty())
			gpu_devices = face_detector_scrfd::get_cuda_devices();
#endif
		if (gpu_devices.empty()) {
			obs_property_list_add_int(p, obs_module_text("GPU.Unavailable"), -1);
			obs_property_set_enabled(p, false);
		} else {
			for (const auto &[ordinal, name] : gpu_devices) {
				std::string label = std::to_string(ordinal) + ": " + name;
				obs_property_list_add_int(p, label.c_str(), (long long)ordinal);
			}
		}

#ifdef HAVE_SCRFD
		p = obs_properties_add_list(group, "scrfd_model_variant", obs_module_text("SCRFD.ModelVariant"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("SCRFD.Model2.5G"), (int)scrfd_model_2_5g);
		obs_property_list_add_int(p, obs_module_text("SCRFD.Model10G"), (int)scrfd_model_10g);
		obs_property_list_add_int(p, obs_module_text("SCRFD.ModelCustom"), (int)scrfd_model_custom);
		obs_property_set_long_description(p, obs_module_text("SCRFD.10GWarning"));
		obs_property_set_modified_callback(p, scrfd_model_variant_modified);

		p = obs_properties_add_text(group, "scrfd_10g_warning", obs_module_text("SCRFD.10GWarning"),
					    OBS_TEXT_INFO);
		obs_property_text_set_info_type(p, OBS_TEXT_INFO_WARNING);
		obs_property_set_visible(p, false);

		p = obs_properties_add_path(group, "detector_scrfd_model", obs_module_text("SCRFD.CustomModel"),
					    OBS_PATH_FILE,
					    "ONNX Models (*.onnx);;"
					    "All Files (*.*)",
					    (data_path + "/" DIR_SCRFD).c_str());
		obs_property_set_visible(p, false);
#ifdef HAVE_ONNXRUNTIME_CUDA
		p = obs_properties_add_bool(group, "scrfd_use_cuda", obs_module_text("SCRFD.UseCUDA"));
		if (gpu_devices.empty()) {
			obs_property_set_enabled(p, false);
			obs_property_set_long_description(p, obs_module_text("SCRFD.CUDAUnavailable"));
		}
#endif
		obs_properties_add_float_slider(group, "scrfd_score_threshold", obs_module_text("SCRFD.ScoreThreshold"),
						0.01, 0.99, 0.01);
		obs_properties_add_float_slider(group, "scrfd_nms_threshold", obs_module_text("SCRFD.NmsThreshold"),
						0.01, 0.99, 0.01);
		obs_properties_add_int(group, "scrfd_input_size", obs_module_text("SCRFD.InputSize"), 160, 1280, 32);
#endif
#ifdef HAVE_YUNET
		obs_properties_add_float_slider(group, "yunet_score_threshold", obs_module_text("YuNet.ScoreThreshold"),
						0.01, 0.99, 0.01);
		obs_properties_add_float_slider(group, "yunet_nms_threshold", obs_module_text("YuNet.NmsThreshold"),
						0.01, 0.99, 0.01);
		obs_properties_add_int(group, "yunet_max_input_size", obs_module_text("YuNet.MaxInputSize"), 160, 1280,
				       32);
		obs_properties_add_path(group, "detector_yunet_model", obs_module_text("YuNet.Model"), OBS_PATH_FILE,
					"ONNX Models (*.onnx);;"
					"All Files (*.*)",
					(data_path + "/" DIR_YUNET).c_str());
#endif

		obs_properties_add_path(group, "detector_dlib_hog_model", obs_module_text("Dlib HOG model"),
					OBS_PATH_FILE,
					"Data Files (*.dat);;"
					"All Files (*.*)",
					(data_path + "/" DIR_DLIB_HOG).c_str());
		obs_properties_add_path(group, "detector_dlib_cnn_model", obs_module_text("Dlib CNN model"),
					OBS_PATH_FILE,
					"Data Files (*.dat);;"
					"All Files (*.*)",
					(data_path + "/" DIR_DLIB_CNN).c_str());
		obs_properties_add_group(pp, "detector_settings", obs_module_text("Group.Detector"), OBS_GROUP_NORMAL,
					 group);
	}

	{
		obs_properties_t *group = obs_properties_create();
		p = obs_properties_add_list(group, "target_selection", obs_module_text("TargetSelection"),
					    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_list_add_int(p, obs_module_text("TargetSelection.Sticky"), (int)target_selection_sticky);
		obs_property_list_add_int(p, obs_module_text("TargetSelection.Largest"), (int)target_selection_largest);
		obs_property_list_add_int(p, obs_module_text("TargetSelection.Center"), (int)target_selection_center);
		obs_property_list_add_int(p, obs_module_text("TargetSelection.First"), (int)target_selection_first);
		p = obs_properties_add_int(group, "target_stick_ms", obs_module_text("TargetSelection.Hold"), 0, 10000,
					   100);
		obs_property_int_set_suffix(p, " ms");
		p = obs_properties_add_bool(group, "tracking_th_en", obs_module_text("Set tracking threshold"));
		obs_property_set_modified_callback(p, tracking_th_en_modified);
		p = obs_properties_add_float(group, "tracking_th_dB", obs_module_text("Tracking threshold"), -120.0,
					     -20.0, 5.0);
		obs_property_float_set_suffix(p, " dB");
		obs_properties_add_group(pp, "subject_tracking", obs_module_text("Group.SubjectTracking"),
					 OBS_GROUP_NORMAL, group);
	}

	{
		obs_properties_t *group = obs_properties_create();
		obs_properties_add_float(group, "scale", obs_module_text("Scale image"), 1.0, 16.0, 1.0);
		obs_properties_add_float(group, "upsize_l", obs_module_text("Left"), -0.4, 4.0, 0.2);
		obs_properties_add_float(group, "upsize_r", obs_module_text("Right"), -0.4, 4.0, 0.2);
		obs_properties_add_float(group, "upsize_t", obs_module_text("Top"), -0.4, 4.0, 0.2);
		obs_properties_add_float(group, "upsize_b", obs_module_text("Bottom"), -0.4, 4.0, 0.2);
		obs_properties_add_int(group, "detector_crop_l", obs_module_text("Crop left for detector"), 0, 1920, 1);
		obs_properties_add_int(group, "detector_crop_r", obs_module_text("Crop right for detector"), 0, 1920,
				       1);
		obs_properties_add_int(group, "detector_crop_t", obs_module_text("Crop top for detector"), 0, 1080, 1);
		obs_properties_add_int(group, "detector_crop_b", obs_module_text("Crop bottom for detector"), 0, 1080,
				       1);
		obs_properties_add_group(pp, "detection_area", obs_module_text("Group.DetectionArea"), OBS_GROUP_NORMAL,
					 group);
	}

	{
		obs_properties_t *group = obs_properties_create();
		p = obs_properties_add_int(group, "detector_interval_ms", obs_module_text("DetectionInterval.Tracking"),
					   100, 10000, 50);
		obs_property_int_set_suffix(p, " ms");
		p = obs_properties_add_int(group, "detector_interval_lost_ms",
					   obs_module_text("DetectionInterval.Searching"), 50, 5000, 50);
		obs_property_int_set_suffix(p, " ms");
		p = obs_properties_add_int(group, "tracker_interval_ms", obs_module_text("TrackingInterval"), 16, 250,
					   1);
		obs_property_int_set_suffix(p, " ms");
		obs_properties_add_group(pp, "timing_performance", obs_module_text("Group.TimingPerformance"),
					 OBS_GROUP_NORMAL, group);
	}

	{
		obs_properties_t *group = obs_properties_create();
		obs_properties_add_bool(group, "landmark_detection", obs_module_text("Enable landmark detection"));
		p = obs_properties_add_path(group, "landmark_detection_data",
					    obs_module_text("Landmark detection data"), OBS_PATH_FILE,
					    "Data Files (*.dat);;"
					    "All Files (*.*)",
					    (data_path + "/" DIR_DLIB_LANDMARK).c_str());
		obs_property_set_long_description(
			p, obs_module_text("You can get the shape_predictor_68_face_landmarks.dat file from: "
					   "http://dlib.net/files/shape_predictor_68_face_landmarks.dat.bz2"));
		obs_properties_add_group(pp, "landmarks", obs_module_text("Group.Landmarks"), OBS_GROUP_NORMAL, group);
	}
}

void face_tracker_manager::get_defaults(obs_data_t *settings)
{
	obs_data_set_default_double(settings, "upsize_l", 0.2);
	obs_data_set_default_double(settings, "upsize_r", 0.2);
	obs_data_set_default_double(settings, "upsize_t", 0.3);
	obs_data_set_default_double(settings, "upsize_b", 0.1);
	obs_data_set_default_double(settings, "scale", 2.0);
	obs_data_set_default_bool(settings, "tracking_th_en", true);
	obs_data_set_default_double(settings, "tracking_th_dB", -80.0);
	obs_data_set_default_int(settings, "target_selection", (int)target_selection_sticky);
	obs_data_set_default_int(settings, "detector_interval_ms", 2000);
	obs_data_set_default_int(settings, "detector_interval_lost_ms", 250);
	obs_data_set_default_int(settings, "tracker_interval_ms", 50);
	obs_data_set_default_int(settings, "target_stick_ms", 1500);
	bool cuda_available = false;
#if defined(HAVE_SCRFD) && defined(HAVE_ONNXRUNTIME_CUDA)
	auto cuda_devices = face_detector_scrfd::get_cuda_devices();
	cuda_available = !cuda_devices.empty();
	obs_data_set_default_int(settings, "detector_gpu_device", cuda_available ? cuda_devices.front().first : 0);
#else
	obs_data_set_default_int(settings, "detector_gpu_device", 0);
#endif
	obs_data_set_default_double(settings, "yunet_score_threshold", 0.6);
	obs_data_set_default_double(settings, "yunet_nms_threshold", 0.3);
	obs_data_set_default_int(settings, "yunet_max_input_size", 320);
	obs_data_set_default_double(settings, "scrfd_score_threshold", 0.5);
	obs_data_set_default_double(settings, "scrfd_nms_threshold", 0.4);
	obs_data_set_default_int(settings, "scrfd_input_size", 640);
	obs_data_set_default_int(settings, "scrfd_model_variant", (int)scrfd_model_2_5g);
#ifdef HAVE_ONNXRUNTIME_CUDA
	obs_data_set_default_bool(settings, "scrfd_use_cuda", cuda_available);
#else
	obs_data_set_default_bool(settings, "scrfd_use_cuda", false);
#endif
#ifdef HAVE_YUNET
	obs_data_set_default_int(settings, "detector_engine", cuda_available ? (int)engine_scrfd : (int)engine_yunet);
#elif defined(HAVE_SCRFD)
	obs_data_set_default_int(settings, "detector_engine", (int)engine_scrfd);
#endif

	if (char *f = obs_module_file(DIR_DLIB_HOG "/frontal_face_detector.dat")) {
		obs_data_set_default_string(settings, "detector_dlib_hog_model", f);
		bfree(f);
	} else {
		blog(LOG_ERROR, "frontal_face_detector.dat is not found in the data directory.");
	}

	if (char *f = obs_module_file(DIR_DLIB_CNN "/mmod_human_face_detector.dat")) {
		obs_data_set_default_string(settings, "detector_dlib_cnn_model", f);
		bfree(f);
	} else {
		blog(LOG_ERROR, "mmod_human_face_detector.dat is not found in the data directory.");
	}

#ifdef HAVE_YUNET
	if (char *f = obs_module_file(DIR_YUNET "/face_detection_yunet_2026may.onnx")) {
		obs_data_set_default_string(settings, "detector_yunet_model", f);
		bfree(f);
	} else {
		blog(LOG_ERROR, "face_detection_yunet_2026may.onnx is not found in the data directory.");
	}
#endif

#ifdef HAVE_SCRFD
	if (char *f = obs_module_file(DIR_SCRFD "/scrfd_2.5g_bnkps.onnx")) {
		obs_data_set_default_string(settings, "detector_scrfd_2_5g_model", f);
		obs_data_set_default_string(settings, "detector_scrfd_model", f);
		bfree(f);
	} else {
		blog(LOG_WARNING, "scrfd_2.5g_bnkps.onnx is not found; select an SCRFD ONNX model in properties.");
	}
	if (char *f = obs_module_file(DIR_SCRFD "/scrfd_10g_bnkps.onnx")) {
		obs_data_set_default_string(settings, "detector_scrfd_10g_model", f);
		bfree(f);
	} else {
		blog(LOG_WARNING, "scrfd_10g_bnkps.onnx is not found; the SCRFD 10G preset is unavailable.");
	}
#endif

	if (char *f = obs_module_file(DIR_DLIB_LANDMARK "/shape_predictor_5_face_landmarks.dat")) {
		obs_data_set_default_string(settings, "landmark_detection_data", f);
		bfree(f);
	} else {
		blog(LOG_ERROR, "shape_predictor_5_face_landmarks.dat is not found in the data directory.");
	}
}
