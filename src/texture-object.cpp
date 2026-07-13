#include <obs-module.h>
#include <util/platform.h>
#include <util/threading.h>
#include <util/bmem.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <dlib/array2d/array2d_kernel.h>
#include "plugin-macros.generated.h"
#include "texture-object.h"

static std::atomic_uint32_t formats_found{0};

static bool mark_format_seen(enum video_format format)
{
	uint32_t value = (uint32_t)format;
	if (value >= 32)
		return false;
	uint32_t bit = 1U << value;
	return !(formats_found.fetch_or(bit, std::memory_order_relaxed) & bit);
}

struct texture_object_private_s
{
	struct obs_source_frame *obs_frame = NULL;
	int scale = 0;
	mutable std::mutex rgb_mutex;
	mutable std::shared_ptr<const dlib::matrix<dlib::rgb_pixel>> rgb_image;
};

texture_object::texture_object()
{
	data = new texture_object_private_s;
	data->obs_frame = NULL;
	tick = 0;
	scale = 1.0f;
}

texture_object::~texture_object()
{
	obs_source_frame_destroy(data->obs_frame);
	delete data;
}

static void obsframe2dlib_bgrx(dlib::matrix<dlib::rgb_pixel> &img, const struct obs_source_frame *frame, int scale,
			       int size = 4)
{
	const int nr = img.nr();
	const int nc = img.nc();
	const int inc = size * scale;
	for (int i = 0; i < nr; i++) {
		uint8_t *line = frame->data[0] + frame->linesize[0] * scale * i;
		for (int j = 0, js = 0; j < nc; j++, js += inc) {
			img(i, j).red = line[js + 2];
			img(i, j).green = line[js + 1];
			img(i, j).blue = line[js + 0];
		}
	}
}

static void obsframe2dlib_rgbx(dlib::matrix<dlib::rgb_pixel> &img, const struct obs_source_frame *frame, int scale)
{
	const int nr = img.nr();
	const int nc = img.nc();
	for (int i = 0; i < nr; i++) {
		uint8_t *line = frame->data[0] + frame->linesize[0] * scale * i;
		for (int j = 0, js = 0; j < nc; j++, js += 4 * scale) {
			img(i, j).red = line[js + 0];
			img(i, j).green = line[js + 1];
			img(i, j).blue = line[js + 2];
		}
	}
}

static bool need_allocate_frame(const struct obs_source_frame *dst, const struct obs_source_frame *src)
{
	if (!dst)
		return true;

	if (dst->format != src->format)
		return true;

	if (dst->width != src->width || dst->height != src->height)
		return true;

	return false;
}

static int packed_rgb_bytes_per_pixel(enum video_format format)
{
	switch (format) {
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_BGRA:
	case VIDEO_FORMAT_RGBA:
		return 4;
	case VIDEO_FORMAT_BGR3:
		return 3;
	default:
		return 0;
	}
}

static void copy_downscaled_packed_rgb(struct obs_source_frame *dst, const struct obs_source_frame *src, int scale,
				       int bytes_per_pixel)
{
	for (uint32_t y = 0; y < dst->height; y++) {
		const uint8_t *src_line = src->data[0] + src->linesize[0] * y * scale;
		uint8_t *dst_line = dst->data[0] + dst->linesize[0] * y;
		for (uint32_t x = 0; x < dst->width; x++)
			memcpy(dst_line + x * bytes_per_pixel, src_line + x * scale * bytes_per_pixel,
			       (size_t)bytes_per_pixel);
	}
}

void texture_object::set_texture_obsframe(const struct obs_source_frame *frame, int scale)
{
	std::lock_guard<std::mutex> lock(data->rgb_mutex);
	if (!frame || !frame->width || !frame->height)
		return;
	int safe_scale = std::max(scale, 1);
	int bytes_per_pixel = packed_rgb_bytes_per_pixel(frame->format);
	struct obs_source_frame target = {};
	target.format = frame->format;
	target.width = bytes_per_pixel && safe_scale > 1 ? std::max(frame->width / safe_scale, 1U) : frame->width;
	target.height = bytes_per_pixel && safe_scale > 1 ? std::max(frame->height / safe_scale, 1U) : frame->height;
	if (need_allocate_frame(data->obs_frame, &target)) {
		obs_source_frame_destroy(data->obs_frame);
		data->obs_frame = obs_source_frame_create(target.format, target.width, target.height);
	}
	if (!data->obs_frame)
		return;

	if (bytes_per_pixel && safe_scale > 1) {
		copy_downscaled_packed_rgb(data->obs_frame, frame, safe_scale, bytes_per_pixel);
		data->scale = 1;
	} else {
		obs_source_frame_copy(data->obs_frame, frame);
		data->scale = safe_scale;
	}
	data->rgb_image.reset();
}

std::shared_ptr<const dlib::matrix<dlib::rgb_pixel>> texture_object::get_dlib_rgb_image() const
{
	std::lock_guard<std::mutex> lock(data->rgb_mutex);
	if (data->rgb_image)
		return data->rgb_image;
	if (!data->obs_frame)
		return nullptr;

	const auto *frame = data->obs_frame;
	const int scale = std::max(data->scale, 1);
	if (!frame->data[0] || !frame->width || !frame->height)
		return nullptr;
	bool first_seen = mark_format_seen(frame->format);
	if (first_seen)
		blog(LOG_INFO, "received frame format=%d", frame->format);
	auto img = std::make_shared<dlib::matrix<dlib::rgb_pixel>>();
	img->set_size(frame->height / scale, frame->width / scale);
	switch (frame->format) {
	case VIDEO_FORMAT_BGRX:
	case VIDEO_FORMAT_BGRA:
		obsframe2dlib_bgrx(*img, frame, scale);
		break;
	case VIDEO_FORMAT_BGR3:
		obsframe2dlib_bgrx(*img, frame, scale, 3);
		break;
	case VIDEO_FORMAT_RGBA:
		obsframe2dlib_rgbx(*img, frame, scale);
		break;
	default:
		if (first_seen)
			blog(LOG_ERROR, "Frame format %d has to be RGB", (int)frame->format);
		return nullptr;
	}

	data->rgb_image = img;
	return data->rgb_image;
}
