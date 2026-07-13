#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <graphics/vec2.h>
#include <graphics/graphics.h>
#include <climits>
#include <cstdlib>
#include <string>
#include "plugin-macros.generated.h"
#include "libvisca-thread.hpp"
#include "libvisca.h"

#define debug(...) // blog(LOG_INFO, __VA_ARGS__)

#define TH_FAIL 4

libvisca_thread::libvisca_thread()
{
	debug("libvisca_thread::libvisca_thread");
	iface = NULL;
	camera = NULL;
	data = NULL;
	data_changed.store(false, std::memory_order_relaxed);
	preset_changed.store(false, std::memory_order_relaxed);
	pan_rsvd.store(0, std::memory_order_relaxed);
	tilt_rsvd.store(0, std::memory_order_relaxed);
	zoom_rsvd.store(0, std::memory_order_relaxed);
	preset_rsvd.store(0, std::memory_order_relaxed);
	zoom_got.store(0, std::memory_order_relaxed);
	pthread_mutex_init(&mutex, 0);

	add_ref(); // release inside thread_main
	pthread_t thread;
	int err = pthread_create(&thread, NULL, libvisca_thread::thread_main, (void *)this);
	if (err) {
		blog(LOG_ERROR, "libvisca_thread: pthread_create failed (%d)", err);
		release();
	} else {
		pthread_detach(thread);
	}
}

libvisca_thread::~libvisca_thread()
{
	if (iface) {
		VISCA_close(iface);
		bfree(iface);
	}
	if (camera)
		bfree(camera);
	if (data)
		obs_data_release(data);
	pthread_mutex_destroy(&mutex);
}

void libvisca_thread::thread_connect()
{
	pthread_mutex_lock(&mutex);
	data_changed.store(false, std::memory_order_release);
	std::string address = data ? obs_data_get_string(data, "address") : "";
	int port = data ? (int)obs_data_get_int(data, "port") : 0;
	pthread_mutex_unlock(&mutex);
	if (iface) {
		VISCA_close(iface);
		bfree(iface);
		iface = NULL;
	}
	if (address.empty() || port <= 0)
		return;

	auto *iface_new = (struct _VISCA_interface *)bzalloc(sizeof(struct _VISCA_interface));
	debug("libvisca_thread::thread_connect connecting to address=%s port=%d...", address.c_str(), port);
	if (VISCA_open_tcp(iface_new, address.c_str(), port) != VISCA_SUCCESS) {
		blog(LOG_ERROR, "failed to connect %s:%d", address.c_str(), port);
		bfree(iface_new);
		iface_new = NULL;
	}
	debug("libvisca_thread::thread_connect connected.");
	if (!iface_new)
		return;

	iface = iface_new;
	if (!camera)
		camera = (VISCACamera_t *)bzalloc(sizeof(VISCACamera_t));
	debug("libvisca_thread::thread_connect sending VISCA_clear...");
	camera->address = 1;
	VISCA_clear(iface, camera);
	debug("libvisca_thread::thread_connect exiting successfully");
}

void *libvisca_thread::thread_main(void *data)
{
	auto *visca = (libvisca_thread *)data;

	// add_ref() was called just before creating this thread.

	visca->thread_loop();

	visca->release();

	return NULL;
}

static inline bool send_pantilt(struct _VISCA_interface *iface, struct _VISCA_camera *camera, int pan, int tilt,
				int retry = 0)
{
	debug("send_pantilt moving pan=%d tilt=%d", pan, tilt);
	int pan_a = abs(pan);
	int tilt_a = abs(tilt);
	if (pan_a > 127)
		pan_a = 127;
	if (tilt_a > 127)
		tilt_a = 127;

	uint32_t res = VISCA_SUCCESS;
	if (tilt < 0 && pan < 0) // 1=up, 1=left
		res = VISCA_set_pantilt_upleft(iface, camera, pan_a, tilt_a);
	else if (tilt < 0 && pan == 0) // 1=up
		res = VISCA_set_pantilt_up(iface, camera, pan_a, tilt_a);
	else if (tilt < 0 && pan > 0) // 1=up, 2=right
		res = VISCA_set_pantilt_upright(iface, camera, pan_a, tilt_a);
	else if (tilt == 0 && pan < 0) // 1=left
		res = VISCA_set_pantilt_left(iface, camera, pan_a, tilt_a);
	else if (tilt == 0 && pan == 0)
		res = VISCA_set_pantilt_stop(iface, camera, pan_a, tilt_a);
	else if (tilt == 0 && pan > 0) // 2=right
		res = VISCA_set_pantilt_right(iface, camera, pan_a, tilt_a);
	else if (tilt > 0 && pan < 0) // 2=down, 1=left
		res = VISCA_set_pantilt_downleft(iface, camera, pan_a, tilt_a);
	else if (tilt > 0 && pan == 0) // 2=down
		res = VISCA_set_pantilt_down(iface, camera, pan_a, tilt_a);
	else if (tilt > 0 && pan > 0)
		res = VISCA_set_pantilt_downright(iface, camera, pan_a, tilt_a);

	if (res != VISCA_SUCCESS)
		return false;

	if (iface->type == VISCA_RESPONSE_ERROR && retry < 3) {
		blog(LOG_INFO, "send_pantilt(%d, %d): retrying (%d)", pan, tilt, retry);
		return send_pantilt(iface, camera, pan, tilt, retry + 1);
	}

	return true;
}

static inline bool send_zoom(struct _VISCA_interface *iface, struct _VISCA_camera *camera, int zoom, int retry = 0)
{
	debug("send_zoom moving zoom=%d", zoom);
	uint32_t res;
	int zoom_a = std::abs(zoom);
	if (zoom_a > 7)
		zoom_a = 7;
	// zoom>0 : wide
	if (zoom > 0)
		res = VISCA_set_zoom_wide_speed(iface, camera, zoom_a);
	else if (zoom < 0)
		res = VISCA_set_zoom_tele_speed(iface, camera, zoom_a);
	else
		res = VISCA_set_zoom_stop(iface, camera);

	if (res != VISCA_SUCCESS)
		return false;

	if (iface->type == VISCA_RESPONSE_ERROR && retry < 3) {
		blog(LOG_INFO, "send_zoom(%d): retrying (%d)", zoom, retry);
		return send_zoom(iface, camera, zoom, retry + 1);
	}

	return true;
}

void libvisca_thread::thread_loop()
{
	int pan_prev = INT_MIN, tilt_prev = INT_MIN, zoom_prev = INT_MIN;
	int n_fail = 0;

	while (get_ref() > 1) {
		if (data_changed.load(std::memory_order_acquire) || n_fail > TH_FAIL) {
			thread_connect();
			pan_prev = INT_MIN;
			tilt_prev = INT_MIN;
			zoom_prev = INT_MIN;
		}
		if (!iface) {
			os_sleep_ms(50);
			continue;
		}
		int pan = (int)pan_rsvd.load(std::memory_order_acquire);
		int tilt = (int)tilt_rsvd.load(std::memory_order_acquire);
		int zoom = (int)zoom_rsvd.load(std::memory_order_acquire);
		bool ptz_changed = false;
		if (pan != pan_prev || tilt != tilt_prev) {
			if (send_pantilt(iface, camera, pan, tilt)) {
				pan_prev = pan;
				tilt_prev = tilt;
				ptz_changed = true;
				n_fail = 0;
			} else {
				n_fail++;
			}
		}
		if (zoom != zoom_prev) {
			if (send_zoom(iface, camera, zoom)) {
				zoom_prev = zoom;
				ptz_changed = true;
				n_fail = 0;
			} else {
				n_fail++;
			}
		}

		if (preset_changed.exchange(false, std::memory_order_acq_rel)) {
			os_sleep_ms(48);
			int preset = preset_rsvd.load(std::memory_order_acquire);
			debug("libvisca_thread::thread_loop recall preset=%d", preset);
			VISCA_memory_recall(iface, camera, preset);
			os_sleep_ms(48);
		}

		if (zoom != 0) {
			uint16_t zoom_cur = 0;
			if (VISCA_get_zoom_value(iface, camera, &zoom_cur) == VISCA_SUCCESS) {
				if (zoom_cur != zoom_got.load(std::memory_order_relaxed)) {
					debug("libvisca_thread::thread_loop got zoom=%d", (int)zoom_cur);
				}
				zoom_got.store((long)zoom_cur, std::memory_order_release);
			}
		}

		if (!ptz_changed)
			os_sleep_ms(50);
	}
}

void libvisca_thread::set_config(struct obs_data *data_)
{
	pthread_mutex_lock(&mutex);

	std::string address_old = data ? obs_data_get_string(data, "address") : "";
	int port_old = data ? (int)obs_data_get_int(data, "port") : 0;
	std::string address_new = data_ ? obs_data_get_string(data_, "address") : "";
	int port_new = data_ ? (int)obs_data_get_int(data_, "port") : 0;

	obs_data_addref(data_);
	if (data) {
		obs_data_release(data);
		if (address_old != address_new)
			data_changed.store(true, std::memory_order_release);
		if (port_old != port_new)
			data_changed.store(true, std::memory_order_release);
	} else
		data_changed.store(true, std::memory_order_release);
	data = data_;

	pthread_mutex_unlock(&mutex);
}

float libvisca_thread::raw2zoomfactor(int zoom)
{
	// TODO: configurable
	return expf((float)zoom * (logf(20.0f) / 16384.f));
}

bool libvisca_thread::ptz_type_modified(obs_properties_t *pp, obs_data_t *settings)
{
	(void)settings;
	if (obs_properties_get(pp, "ptz.visca-over-tcp.address"))
		return false;

	obs_properties_add_text(pp, "ptz.visca-over-tcp.address", obs_module_text("IP address"), OBS_TEXT_DEFAULT);
	obs_properties_add_int(pp, "ptz.visca-over-tcp.port", obs_module_text("Port"), 1, 65535, 1);
	return true;
}
