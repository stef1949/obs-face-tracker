#pragma once

#include <util/threading.h>
#include <atomic>
// #include "libvisca.h"
#include "ptz-backend.hpp"

class libvisca_thread : public ptz_backend {
	pthread_mutex_t mutex;
	struct _VISCA_interface *iface;
	struct _VISCA_camera *camera;
	struct obs_data *data;
	std::atomic_bool data_changed;
	std::atomic_bool preset_changed;
	std::atomic_long pan_rsvd, tilt_rsvd, zoom_rsvd;
	std::atomic_int preset_rsvd;
	std::atomic_long zoom_got;

	static void *thread_main(void *);
	void thread_connect();
	void thread_loop();
	float raw2zoomfactor(int);

public:
	libvisca_thread();
	~libvisca_thread() override;

	void set_config(struct obs_data *data) override; // and attempt to connect

	void set_pantilt_speed(int pan, int tilt) override
	{
		pan_rsvd.store(pan, std::memory_order_release);
		tilt_rsvd.store(tilt, std::memory_order_release);
	}
	void set_zoom_speed(int zoom) override { zoom_rsvd.store(zoom, std::memory_order_release); }
	void recall_preset(int preset) override
	{
		preset_rsvd.store(preset, std::memory_order_release);
		preset_changed.store(true, std::memory_order_release);
	}
	float get_zoom() override { return raw2zoomfactor(zoom_got.load(std::memory_order_acquire)); }

	inline static bool check_data(obs_data_t *data)
	{
		const char *address = obs_data_get_string(data, "address");
		if (!address || !*address)
			return false;
		if (obs_data_get_int(data, "port") <= 0)
			return false;
		return true;
	}
	static bool ptz_type_modified(obs_properties_t *group_output, obs_data_t *settings);
};
