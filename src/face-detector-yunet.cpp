#include <obs-module.h>
#include <util/platform.h>

#include "face-detector-yunet.h"
#include "helper.hpp"
#include "texture-object.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
constexpr std::array<int, 3> strides = {8, 16, 32};
constexpr uint64_t error_retry_ns = 5ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr std::array<const char *, 12> output_names = {
	"cls_8",  "cls_16",  "cls_32",  "obj_8", "obj_16", "obj_32",
	"bbox_8", "bbox_16", "bbox_32", "kps_8", "kps_16", "kps_32",
};

struct candidate_s
{
	rect_s rect;
	float score;
};

}

struct face_detector_yunet::private_s
{
	std::shared_ptr<texture_object> tex;
	std::vector<rect_s> rects;
	std::string model_filename;
	std::string loaded_model_filename;
	Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "obs-face-tracker-yunet"};
	Ort::SessionOptions session_options;
	std::unique_ptr<Ort::Session> session;
	float score_threshold = 0.6f;
	float nms_threshold = 0.3f;
	int max_input_size = 320;
	int logged_width = 0;
	int logged_height = 0;
	int crop_l = 0;
	int crop_r = 0;
	int crop_t = 0;
	int crop_b = 0;
	bool runtime_logged = false;
	uint64_t retry_after_ns = 0;
	std::string failed_model_filename;

	private_s()
	{
		session_options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
		session_options.SetIntraOpNumThreads(1);
		session_options.SetInterOpNumThreads(1);
		session_options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	}
};

face_detector_yunet::face_detector_yunet()
{
	p = new private_s;
}

face_detector_yunet::~face_detector_yunet()
{
	stop();
	delete p;
}

void face_detector_yunet::set_texture(const std::shared_ptr<texture_object> &tex, int crop_l, int crop_r, int crop_t,
				      int crop_b)
{
	p->tex = tex;
	p->crop_l = crop_l;
	p->crop_r = crop_r;
	p->crop_t = crop_t;
	p->crop_b = crop_b;
}

void face_detector_yunet::set_model(const char *filename)
{
	std::string model_filename = filename ? filename : "";
	if (p->model_filename != model_filename) {
		p->model_filename = std::move(model_filename);
		p->retry_after_ns = 0;
		p->failed_model_filename.clear();
	}
}

void face_detector_yunet::set_config(float score_threshold, float nms_threshold, int max_input_size)
{
	p->score_threshold = std::clamp(score_threshold, 0.01f, 0.99f);
	p->nms_threshold = std::clamp(nms_threshold, 0.01f, 0.99f);
	p->max_input_size = std::clamp(max_input_size, 160, 1280);
}

void face_detector_yunet::get_faces(std::vector<rect_s> &rects)
{
	rects = p->rects;
}

void face_detector_yunet::detect_main()
{
	auto tex = std::move(p->tex);
	p->rects.clear();
	if (!tex || p->model_filename.empty())
		return;
	if (p->failed_model_filename == p->model_filename && os_gettime_ns() < p->retry_after_ns)
		return;

	try {
		if (!p->session || p->loaded_model_filename != p->model_filename) {
			p->session = std::make_unique<Ort::Session>(
				p->env, std::filesystem::path(p->model_filename).c_str(), p->session_options);
			p->loaded_model_filename = p->model_filename;
			p->failed_model_filename.clear();
			p->retry_after_ns = 0;
			blog(LOG_INFO, "YuNet loaded model '%s'", p->model_filename.c_str());
			if (!p->runtime_logged) {
				blog(LOG_INFO, "YuNet detector using ONNX Runtime %s CPU provider",
				     OrtGetApiBase()->GetVersionString());
				p->runtime_logged = true;
			}
		}

		auto img = tex->get_dlib_rgb_image();
		if (!img)
			return;

		int x0 = std::max((int)(p->crop_l / tex->scale), 0);
		int y0 = std::max((int)(p->crop_t / tex->scale), 0);
		int x1 = std::min((int)img->nc() - (int)(p->crop_r / tex->scale), (int)img->nc());
		int y1 = std::min((int)img->nr() - (int)(p->crop_b / tex->scale), (int)img->nr());
		int input_width = x1 - x0;
		int input_height = y1 - y0;
		if (input_width < 32 || input_height < 32)
			return;

		float inference_scale =
			std::min(1.0f, (float)p->max_input_size / (float)std::max(input_width, input_height));
		int inference_width = std::max((int)std::lround(input_width * inference_scale), 32);
		int inference_height = std::max((int)std::lround(input_height * inference_scale), 32);
		float inverse_scale_x = (float)input_width / (float)inference_width;
		float inverse_scale_y = (float)input_height / (float)inference_height;
		int padded_width = ((inference_width + 31) / 32) * 32;
		int padded_height = ((inference_height + 31) / 32) * 32;
		if (p->logged_width != padded_width || p->logged_height != padded_height) {
			blog(LOG_INFO, "YuNet inference size %dx%d for source region %dx%d", padded_width,
			     padded_height, input_width, input_height);
			p->logged_width = padded_width;
			p->logged_height = padded_height;
		}
		size_t plane_size = (size_t)padded_width * (size_t)padded_height;
		std::vector<float> input(plane_size * 3, 0.0f);
		for (int y = 0; y < inference_height; y++) {
			int source_y = std::min((int)(y * inverse_scale_y), input_height - 1);
			for (int x = 0; x < inference_width; x++) {
				int source_x = std::min((int)(x * inverse_scale_x), input_width - 1);
				const dlib::rgb_pixel &pixel = (*img)(source_y + y0, source_x + x0);
				size_t index = (size_t)y * (size_t)padded_width + (size_t)x;
				input[index] = (float)pixel.blue;
				input[plane_size + index] = (float)pixel.green;
				input[plane_size * 2 + index] = (float)pixel.red;
			}
		}

		std::array<int64_t, 4> input_shape = {1, 3, padded_height, padded_width};
		auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		Ort::Value input_tensor = Ort::Value::CreateTensor<float>(memory_info, input.data(), input.size(),
									  input_shape.data(), input_shape.size());
		const char *input_names[] = {"input"};
		auto outputs = p->session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
					       output_names.data(), output_names.size());
		if (outputs.size() != output_names.size())
			throw std::runtime_error("YuNet returned an unexpected number of outputs");

		auto tensor_data = [&](size_t output, size_t count, int channels) {
			if (!outputs[output].IsTensor())
				throw std::runtime_error("YuNet returned a non-tensor output");
			auto info = outputs[output].GetTensorTypeAndShapeInfo();
			if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
				throw std::runtime_error("YuNet returned a non-float output");
			auto shape = info.GetShape();
			if (shape.size() != 3 || shape[0] != 1 || shape[1] != (int64_t)count || shape[2] != channels)
				throw std::runtime_error("YuNet output shape does not match the input dimensions");
			return outputs[output].GetTensorData<float>();
		};

		std::vector<candidate_s> candidates;
		for (size_t level = 0; level < strides.size(); level++) {
			int stride = strides[level];
			int rows = padded_height / stride;
			int cols = padded_width / stride;
			size_t count = (size_t)rows * (size_t)cols;
			const float *cls = tensor_data(level, count, 1);
			const float *obj = tensor_data(level + 3, count, 1);
			const float *bbox = tensor_data(level + 6, count, 4);
			(void)tensor_data(level + 9, count, 10);

			for (int row = 0; row < rows; row++) {
				for (int col = 0; col < cols; col++) {
					size_t index = (size_t)row * (size_t)cols + (size_t)col;
					float score = std::sqrt(std::clamp(cls[index], 0.0f, 1.0f) *
								std::clamp(obj[index], 0.0f, 1.0f));
					if (!std::isfinite(score) || score < p->score_threshold)
						continue;
					if (!std::isfinite(bbox[index * 4]) || !std::isfinite(bbox[index * 4 + 1]) ||
					    !std::isfinite(bbox[index * 4 + 2]) || !std::isfinite(bbox[index * 4 + 3]))
						continue;
					float cx = ((float)col + bbox[index * 4]) * (float)stride;
					float cy = ((float)row + bbox[index * 4 + 1]) * (float)stride;
					float width = std::exp(bbox[index * 4 + 2]) * (float)stride;
					float height = std::exp(bbox[index * 4 + 3]) * (float)stride;
					if (!std::isfinite(width) || !std::isfinite(height))
						continue;
					float left = std::clamp(cx - width * 0.5f, 0.0f, (float)inference_width) *
						     inverse_scale_x;
					float top = std::clamp(cy - height * 0.5f, 0.0f, (float)inference_height) *
						    inverse_scale_y;
					float right = std::clamp(cx + width * 0.5f, 0.0f, (float)inference_width) *
						      inverse_scale_x;
					float bottom = std::clamp(cy + height * 0.5f, 0.0f, (float)inference_height) *
						       inverse_scale_y;
					if (right - left < 2.0f || bottom - top < 2.0f)
						continue;
					candidate_s candidate;
					candidate.rect.x0 = (int)std::lround((left + (float)x0) * tex->scale);
					candidate.rect.y0 = (int)std::lround((top + (float)y0) * tex->scale);
					candidate.rect.x1 = (int)std::lround((right + (float)x0) * tex->scale);
					candidate.rect.y1 = (int)std::lround((bottom + (float)y0) * tex->scale);
					candidate.rect.score = score;
					candidate.score = score;
					candidates.push_back(candidate);
				}
			}
		}

		std::sort(candidates.begin(), candidates.end(),
			  [](const candidate_s &a, const candidate_s &b) { return a.score > b.score; });
		if (candidates.size() > 5000)
			candidates.resize(5000);
		p->rects.clear();
		for (const candidate_s &candidate : candidates) {
			bool suppressed = std::any_of(p->rects.begin(), p->rects.end(), [&](const rect_s &selected) {
				return rect_iou(candidate.rect, selected) > p->nms_threshold;
			});
			if (!suppressed)
				p->rects.push_back(candidate.rect);
		}
	} catch (const std::exception &e) {
		blog(LOG_ERROR, "YuNet detection failed: %s", e.what());
		p->failed_model_filename = p->model_filename;
		p->retry_after_ns = os_gettime_ns() + error_retry_ns;
		p->session.reset();
		p->loaded_model_filename.clear();
		p->rects.clear();
	}
}
