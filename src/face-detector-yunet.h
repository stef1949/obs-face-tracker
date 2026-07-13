#pragma once

#include "face-detector-base.h"

#include <memory>
#include <string>

class face_detector_yunet : public face_detector_base {
	struct private_s;
	private_s *p;
	void detect_main() override;

public:
	face_detector_yunet();
	~face_detector_yunet() override;

	void set_texture(const std::shared_ptr<texture_object> &, int crop_l, int crop_r, int crop_t,
			 int crop_b) override;
	void get_faces(std::vector<rect_s> &) override;
	void set_model(const char *filename);
	void set_config(float score_threshold, float nms_threshold, int max_input_size);
};
