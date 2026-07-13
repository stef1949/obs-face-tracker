#pragma once
#include <obs-module.h>
#include <util/threading.h>
#include <memory>
#include <vector>
#include <dlib/array2d/array2d_kernel.h>
#include "plugin-macros.generated.h"

class texture_object {
	struct texture_object_private_s *data;

public:
	texture_object();
	~texture_object();

	void set_texture_obsframe(const struct obs_source_frame *frame, int scale);
	std::shared_ptr<const dlib::matrix<dlib::rgb_pixel>> get_dlib_rgb_image() const;

public:
	int tick;
	float scale;
};
