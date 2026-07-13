#pragma once

#include "face-detector-base.h"

#include <memory>

class face_detector_scrfd : public face_detector_base {
	struct private_s;
	private_s *p;
	void detect_main() override;

public:
	face_detector_scrfd();
	~face_detector_scrfd() override;

	void set_texture(const std::shared_ptr<texture_object> &, int crop_l, int crop_r, int crop_t,
			 int crop_b) override;
	void get_faces(std::vector<rect_s> &) override;
	void set_model(const char *filename);
	void set_config(float score_threshold, float nms_threshold, int input_size, bool use_cuda, int gpu_device);
};
