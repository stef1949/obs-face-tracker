#pragma once

#include <deque>
#include <string>
#include <atomic>
#include <mutex>
#include "face-tracker-base.h"

class face_tracker_manager {
public:
	enum detector_engine_e {
		engine_dlib_hog = 0,
		engine_dlib_cnn = 1,
		engine_yunet = 2,
		engine_scrfd = 3,
		engine_uninitialized = -1,
	};
	enum scrfd_model_variant_e {
		scrfd_model_2_5g = 0,
		scrfd_model_10g = 1,
		scrfd_model_custom = 2,
	};
	enum target_selection_e {
		target_selection_sticky = 0,
		target_selection_largest = 1,
		target_selection_center = 2,
		target_selection_first = 3,
	};

	struct tracker_rect_s
	{
		rect_s rect;
		rectf_s crop_rect;
		std::vector<pointf_s> landmark;
	};

	struct tracker_inst_s
	{
		class face_tracker_base *tracker;
		rect_s rect;
		rectf_s crop_tracker; // crop corresponding to current processing image
		rectf_s crop_rect;    // crop corresponding to rect
		std::vector<pointf_s> landmark;
		float att;
		float score_first;
		enum tracker_state_e {
			tracker_state_init = 0,
			tracker_state_reset_texture, // texture has been set, position is not set.
			tracker_state_constructing, // texture and positions have been set, starting to construct correlation_tracker.
			tracker_state_first_track, // correlation_tracker has been prepared, running 1st tracking
			tracker_state_available,   // 1st tracking was done, `rect` is available, can accept next frame.
			tracker_state_ending,
		} state;
		int tick_cnt;
	};

public: // properties
	float upsize_l, upsize_r, upsize_t, upsize_b;
	std::atomic<float> scale;
	std::atomic_bool reset_requested;
	float tracking_threshold;
	enum detector_engine_e detector_engine = engine_uninitialized;
	std::string detector_dlib_hog_model;
	std::string detector_dlib_cnn_model;
	std::string detector_yunet_model;
	std::string detector_scrfd_model;
	enum scrfd_model_variant_e scrfd_model_variant;
	float yunet_score_threshold;
	float yunet_nms_threshold;
	int yunet_max_input_size;
	float scrfd_score_threshold;
	float scrfd_nms_threshold;
	int scrfd_input_size;
	bool scrfd_use_cuda;
	int detector_gpu_device;
	int detector_crop_l, detector_crop_r, detector_crop_t, detector_crop_b;
	enum target_selection_e target_selection;
	int detector_interval_ms;
	int detector_interval_lost_ms;
	int tracker_interval_ms;
	int target_stick_ms;
	char *landmark_detection_data;

public: // realtime status
	rectf_s crop_cur;
	int tick_cnt;

public: // results
	std::vector<rect_s> detect_rects;
	std::vector<tracker_rect_s> tracker_rects;

public: /* not sure they are necessary to be public */
	class face_detector_base *detect;
	int detect_tick;

	// TODO: Just have two pairs
	std::deque<struct tracker_inst_s> trackers;
	std::deque<struct tracker_inst_s> trackers_idlepool;

private:
	mutable std::mutex state_mutex;
	uint64_t next_detection_ns;
	uint64_t next_tracking_ns;
	uint64_t target_lost_since_ns;
	bool selected_target_valid;
	rect_s selected_target;
	bool detector_in_progress;

public:
	face_tracker_manager();
	virtual ~face_tracker_manager();
	void tick(float second);
	void post_render();
	bool frame_due() const;
	void update(obs_data_t *settings);
	static void get_properties(obs_properties_t *);
	static void get_defaults(obs_data_t *settings);

protected:
	virtual std::shared_ptr<texture_object> get_cvtex() = 0;

private:
	inline void retire_tracker(int ix);
	inline bool is_low_confident(const tracker_inst_s &t, float th1);
	void remove_duplicated_tracker();
	void attenuate_tracker();
	void copy_detector_to_tracker();
	void stage_to_detector(const std::shared_ptr<texture_object> &cvtex);
	bool stage_surface_to_tracker(struct tracker_inst_s &t, const std::shared_ptr<texture_object> &cvtex);
	void stage_to_trackers(const std::shared_ptr<texture_object> &cvtex);
	void update_tracker_rects();
	size_t select_rect(const std::vector<rect_s> &rects) const;
};
