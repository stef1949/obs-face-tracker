#include <obs-module.h>
#include <util/platform.h>

#include "face-detector-scrfd.h"
#include "helper.hpp"
#include "texture-object.h"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
constexpr uint64_t error_retry_ns = 5ULL * 1000ULL * 1000ULL * 1000ULL;
constexpr size_t max_candidates = 5000;

#ifdef _WIN32
// The packaged cuDNN 9.24 CUDA 12 runtime supports Turing and newer GPUs
// (compute capability 7.5+), and CUDA 12 requires a CUDA 12-capable driver.
constexpr int minimum_cuda_driver_version = 12000;
constexpr int minimum_compute_capability = 75;

std::vector<std::pair<int, std::string>> enumerate_supported_cuda_devices()
{
	std::vector<std::pair<int, std::string>> devices;
	HMODULE driver = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
	if (!driver)
		return devices;

	using cu_init_fn = int(WINAPI *)(unsigned int);
	using cu_driver_get_version_fn = int(WINAPI *)(int *);
	using cu_device_get_count_fn = int(WINAPI *)(int *);
	using cu_device_get_fn = int(WINAPI *)(int *, int);
	using cu_device_get_name_fn = int(WINAPI *)(char *, int, int);
	using cu_device_compute_capability_fn = int(WINAPI *)(int *, int *, int);

	auto cu_init = reinterpret_cast<cu_init_fn>(GetProcAddress(driver, "cuInit"));
	auto cu_driver_get_version =
		reinterpret_cast<cu_driver_get_version_fn>(GetProcAddress(driver, "cuDriverGetVersion"));
	auto cu_device_get_count = reinterpret_cast<cu_device_get_count_fn>(GetProcAddress(driver, "cuDeviceGetCount"));
	auto cu_device_get = reinterpret_cast<cu_device_get_fn>(GetProcAddress(driver, "cuDeviceGet"));
	auto cu_device_get_name = reinterpret_cast<cu_device_get_name_fn>(GetProcAddress(driver, "cuDeviceGetName"));
	auto cu_device_compute_capability =
		reinterpret_cast<cu_device_compute_capability_fn>(GetProcAddress(driver, "cuDeviceComputeCapability"));

	int driver_version = 0;
	int device_count = 0;
	bool initialized = cu_init && cu_driver_get_version && cu_device_get_count && cu_device_get &&
			   cu_device_get_name && cu_device_compute_capability && cu_init(0) == 0 &&
			   cu_driver_get_version(&driver_version) == 0 &&
			   driver_version >= minimum_cuda_driver_version && cu_device_get_count(&device_count) == 0;
	if (initialized) {
		for (int ordinal = 0; ordinal < device_count; ordinal++) {
			int device = 0;
			int major = 0;
			int minor = 0;
			std::array<char, 256> name = {};
			if (cu_device_get(&device, ordinal) != 0 ||
			    cu_device_compute_capability(&major, &minor, device) != 0 ||
			    major * 10 + minor < minimum_compute_capability ||
			    cu_device_get_name(name.data(), (int)name.size(), device) != 0)
				continue;
			devices.emplace_back(ordinal, name.data());
		}
	}

	FreeLibrary(driver);
	return devices;
}
#endif

struct candidate_s
{
	rect_s rect;
	float score;
};

Ort::SessionOptions make_session_options()
{
	Ort::SessionOptions options;
	options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
	options.SetIntraOpNumThreads(1);
	options.SetInterOpNumThreads(1);
	options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
	return options;
}

const float *validated_tensor(const Ort::Value &value, size_t count, int channels)
{
	if (!value.IsTensor())
		throw std::runtime_error("SCRFD returned a non-tensor output");
	auto info = value.GetTensorTypeAndShapeInfo();
	if (info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
		throw std::runtime_error("SCRFD returned a non-float output");
	auto shape = info.GetShape();
	bool valid_shape = (shape.size() == 2 && shape[0] == (int64_t)count && shape[1] == channels) ||
			   (shape.size() == 3 && shape[0] == 1 && shape[1] == (int64_t)count && shape[2] == channels);
	if (!valid_shape || info.GetElementCount() != count * (size_t)channels)
		throw std::runtime_error("SCRFD output shape does not match the input dimensions");
	return value.GetTensorData<float>();
}

float interpolate(float p00, float p10, float p01, float p11, float tx, float ty)
{
	float top = p00 + (p10 - p00) * tx;
	float bottom = p01 + (p11 - p01) * tx;
	return top + (bottom - top) * ty;
}
}

struct face_detector_scrfd::private_s
{
	std::shared_ptr<texture_object> tex;
	std::vector<rect_s> rects;
	std::vector<float> input;
	std::string model_filename;
	std::string loaded_model_filename;
	Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "obs-face-tracker-scrfd"};
	std::unique_ptr<Ort::Session> session;
	std::string input_name;
	std::vector<std::string> output_names;
	std::vector<const char *> output_name_ptrs;
	std::vector<int> strides;
	float score_threshold = 0.5f;
	float nms_threshold = 0.4f;
	int requested_input_size = 640;
	int model_input_width = 0;
	int model_input_height = 0;
	int anchors = 0;
	int crop_l = 0;
	int crop_r = 0;
	int crop_t = 0;
	int crop_b = 0;
	int gpu_device = 0;
	int loaded_gpu_device = -1;
	bool use_cuda = false;
	bool loaded_use_cuda = false;
	bool active_cuda = false;
	uint64_t retry_after_ns = 0;
	std::string failed_model_filename;
};

face_detector_scrfd::face_detector_scrfd()
{
	p = new private_s;
}

face_detector_scrfd::~face_detector_scrfd()
{
	stop();
	delete p;
}

std::vector<std::pair<int, std::string>> face_detector_scrfd::get_cuda_devices()
{
#if defined(_WIN32) && defined(HAVE_ONNXRUNTIME_CUDA)
	static const std::vector<std::pair<int, std::string>> devices = enumerate_supported_cuda_devices();
	return devices;
#elif defined(HAVE_ONNXRUNTIME_CUDA)
	// The self-contained installer is Windows-only. Preserve the previous
	// default-device behaviour for custom CUDA builds on other platforms.
	return {{0, "Default NVIDIA GPU"}};
#else
	return {};
#endif
}

void face_detector_scrfd::set_texture(const std::shared_ptr<texture_object> &tex, int crop_l, int crop_r, int crop_t,
				      int crop_b)
{
	p->tex = tex;
	p->crop_l = crop_l;
	p->crop_r = crop_r;
	p->crop_t = crop_t;
	p->crop_b = crop_b;
}

void face_detector_scrfd::set_model(const char *filename)
{
	std::string model_filename = filename ? filename : "";
	if (p->model_filename != model_filename) {
		p->model_filename = std::move(model_filename);
		p->retry_after_ns = 0;
		p->failed_model_filename.clear();
	}
}

void face_detector_scrfd::set_config(float score_threshold, float nms_threshold, int input_size, bool use_cuda,
				     int gpu_device)
{
	p->score_threshold = std::clamp(score_threshold, 0.01f, 0.99f);
	p->nms_threshold = std::clamp(nms_threshold, 0.01f, 0.99f);
	p->requested_input_size = std::clamp((input_size / 32) * 32, 160, 1280);
#ifdef HAVE_ONNXRUNTIME_CUDA
	p->use_cuda = use_cuda;
#else
	(void)use_cuda;
	p->use_cuda = false;
#endif
	p->gpu_device = std::max(gpu_device, 0);
}

void face_detector_scrfd::get_faces(std::vector<rect_s> &rects)
{
	rects = p->rects;
}

void face_detector_scrfd::detect_main()
{
	auto tex = std::move(p->tex);
	p->rects.clear();
	if (!tex || p->model_filename.empty())
		return;
	if (p->failed_model_filename == p->model_filename && os_gettime_ns() < p->retry_after_ns)
		return;

	try {
		bool session_config_changed = p->loaded_use_cuda != p->use_cuda ||
					      p->loaded_gpu_device != p->gpu_device;
		if (!p->session || p->loaded_model_filename != p->model_filename || session_config_changed) {
			p->session.reset();
			p->active_cuda = false;
			if (p->use_cuda) {
#ifdef HAVE_ONNXRUNTIME_CUDA
				try {
#ifdef _WIN32
					if (!preload_cudnn_runtime_libraries())
						throw std::runtime_error("cuDNN runtime libraries could not be loaded");
#endif
					auto options = make_session_options();
					Ort::CUDAProviderOptions cuda_options;
					cuda_options.Update({
						{"device_id", std::to_string(p->gpu_device)},
						{"cudnn_conv_algo_search", "HEURISTIC"},
						{"do_copy_in_default_stream", "1"},
						{"use_tf32", "1"},
					});
					options.AppendExecutionProvider_CUDA_V2(*cuda_options);
					p->session = std::make_unique<Ort::Session>(
						p->env, std::filesystem::path(p->model_filename).c_str(), options);
					p->active_cuda = true;
				} catch (const std::exception &e) {
					blog(LOG_WARNING, "SCRFD CUDA provider unavailable on GPU %d; using CPU: %s",
					     p->gpu_device, e.what());
				}
#endif
			}
			if (!p->session) {
				auto options = make_session_options();
				p->session = std::make_unique<Ort::Session>(
					p->env, std::filesystem::path(p->model_filename).c_str(), options);
			}

			if (p->session->GetInputCount() != 1)
				throw std::runtime_error("SCRFD model must have exactly one input");
			Ort::AllocatorWithDefaultOptions allocator;
			auto input_name = p->session->GetInputNameAllocated(0, allocator);
			p->input_name = input_name.get();
			auto input_info = p->session->GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
			if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT)
				throw std::runtime_error("SCRFD model input must be float32");
			auto input_shape = input_info.GetShape();
			if (input_shape.size() != 4 || (input_shape[0] > 0 && input_shape[0] != 1) ||
			    (input_shape[1] > 0 && input_shape[1] != 3))
				throw std::runtime_error("SCRFD model input must have shape [1,3,H,W]");
			p->model_input_height = input_shape[2] > 0 ? (int)input_shape[2] : 0;
			p->model_input_width = input_shape[3] > 0 ? (int)input_shape[3] : 0;

			size_t output_count = p->session->GetOutputCount();
			if (output_count == 6 || output_count == 9) {
				p->strides = {8, 16, 32};
				p->anchors = 2;
			} else if (output_count == 10 || output_count == 15) {
				p->strides = {8, 16, 32, 64, 128};
				p->anchors = 1;
			} else {
				throw std::runtime_error("SCRFD model must expose 6, 9, 10, or 15 outputs");
			}
			p->output_names.clear();
			p->output_name_ptrs.clear();
			p->output_names.reserve(output_count);
			p->output_name_ptrs.reserve(output_count);
			for (size_t i = 0; i < output_count; i++) {
				auto output_name = p->session->GetOutputNameAllocated(i, allocator);
				p->output_names.emplace_back(output_name.get());
			}
			for (const std::string &name : p->output_names)
				p->output_name_ptrs.push_back(name.c_str());

			p->loaded_model_filename = p->model_filename;
			p->loaded_use_cuda = p->use_cuda;
			p->loaded_gpu_device = p->gpu_device;
			p->failed_model_filename.clear();
			p->retry_after_ns = 0;
			blog(LOG_INFO, "SCRFD loaded model '%s'", p->model_filename.c_str());
			blog(LOG_INFO, "SCRFD using ONNX Runtime %s %s provider%s", OrtGetApiBase()->GetVersionString(),
			     p->active_cuda ? "CUDA" : "CPU",
			     p->active_cuda ? (" on GPU " + std::to_string(p->gpu_device)).c_str() : "");
		}

		auto img = tex->get_dlib_rgb_image();
		if (!img)
			return;

		int x0 = std::max((int)(p->crop_l / tex->scale), 0);
		int y0 = std::max((int)(p->crop_t / tex->scale), 0);
		int x1 = std::min((int)img->nc() - (int)(p->crop_r / tex->scale), (int)img->nc());
		int y1 = std::min((int)img->nr() - (int)(p->crop_b / tex->scale), (int)img->nr());
		int source_width = x1 - x0;
		int source_height = y1 - y0;
		if (source_width < 32 || source_height < 32)
			return;

		int input_width = p->model_input_width > 0 ? p->model_input_width : p->requested_input_size;
		int input_height = p->model_input_height > 0 ? p->model_input_height : p->requested_input_size;
		if (input_width < 32 || input_height < 32 || input_width % 32 || input_height % 32)
			throw std::runtime_error("SCRFD input dimensions must be positive multiples of 32");
		float resize_scale =
			std::min((float)input_width / (float)source_width, (float)input_height / (float)source_height);
		int resized_width = std::clamp((int)std::lround(source_width * resize_scale), 1, input_width);
		int resized_height = std::clamp((int)std::lround(source_height * resize_scale), 1, input_height);
		float inverse_scale_x = (float)source_width / (float)resized_width;
		float inverse_scale_y = (float)source_height / (float)resized_height;

		constexpr float padding_value = -127.5f / 128.0f;
		size_t plane_size = (size_t)input_width * (size_t)input_height;
		p->input.assign(plane_size * 3, padding_value);
		for (int y = 0; y < resized_height; y++) {
			float source_y = std::clamp(((float)y + 0.5f) * inverse_scale_y - 0.5f, 0.0f,
						    (float)source_height - 1.0f);
			int sy0 = (int)source_y;
			int sy1 = std::min(sy0 + 1, source_height - 1);
			float ty = source_y - (float)sy0;
			for (int x = 0; x < resized_width; x++) {
				float source_x = std::clamp(((float)x + 0.5f) * inverse_scale_x - 0.5f, 0.0f,
							    (float)source_width - 1.0f);
				int sx0 = (int)source_x;
				int sx1 = std::min(sx0 + 1, source_width - 1);
				float tx = source_x - (float)sx0;
				const auto &a = (*img)(sy0 + y0, sx0 + x0);
				const auto &b = (*img)(sy0 + y0, sx1 + x0);
				const auto &c = (*img)(sy1 + y0, sx0 + x0);
				const auto &d = (*img)(sy1 + y0, sx1 + x0);
				size_t index = (size_t)y * (size_t)input_width + (size_t)x;
				p->input[index] = (interpolate(a.red, b.red, c.red, d.red, tx, ty) - 127.5f) / 128.0f;
				p->input[plane_size + index] =
					(interpolate(a.green, b.green, c.green, d.green, tx, ty) - 127.5f) / 128.0f;
				p->input[plane_size * 2 + index] =
					(interpolate(a.blue, b.blue, c.blue, d.blue, tx, ty) - 127.5f) / 128.0f;
			}
		}

		std::array<int64_t, 4> input_shape = {1, 3, input_height, input_width};
		auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		auto input_tensor = Ort::Value::CreateTensor<float>(memory_info, p->input.data(), p->input.size(),
								    input_shape.data(), input_shape.size());
		const char *input_names[] = {p->input_name.c_str()};
		auto outputs = p->session->Run(Ort::RunOptions{nullptr}, input_names, &input_tensor, 1,
					       p->output_name_ptrs.data(), p->output_name_ptrs.size());
		if (outputs.size() != p->output_name_ptrs.size())
			throw std::runtime_error("SCRFD returned an unexpected number of outputs");

		size_t levels = p->strides.size();
		bool has_landmarks = outputs.size() == levels * 3;
		std::vector<candidate_s> candidates;
		for (size_t level = 0; level < levels; level++) {
			int stride = p->strides[level];
			int rows = input_height / stride;
			int cols = input_width / stride;
			size_t count = (size_t)rows * (size_t)cols * (size_t)p->anchors;
			const float *scores = validated_tensor(outputs[level], count, 1);
			const float *bbox = validated_tensor(outputs[level + levels], count, 4);
			if (has_landmarks)
				(void)validated_tensor(outputs[level + levels * 2], count, 10);

			for (size_t index = 0; index < count; index++) {
				float score = scores[index];
				if (!std::isfinite(score) || score < p->score_threshold)
					continue;
				const float *distance = bbox + index * 4;
				if (!std::isfinite(distance[0]) || !std::isfinite(distance[1]) ||
				    !std::isfinite(distance[2]) || !std::isfinite(distance[3]))
					continue;
				size_t cell = index / (size_t)p->anchors;
				float center_x = (float)(cell % (size_t)cols) * (float)stride;
				float center_y = (float)(cell / (size_t)cols) * (float)stride;
				float left = std::clamp(center_x - distance[0] * stride, 0.0f, (float)resized_width);
				float top = std::clamp(center_y - distance[1] * stride, 0.0f, (float)resized_height);
				float right = std::clamp(center_x + distance[2] * stride, 0.0f, (float)resized_width);
				float bottom = std::clamp(center_y + distance[3] * stride, 0.0f, (float)resized_height);
				if (right - left < 2.0f || bottom - top < 2.0f)
					continue;
				candidate_s candidate;
				candidate.rect.x0 = (int)std::lround((left * inverse_scale_x + x0) * tex->scale);
				candidate.rect.y0 = (int)std::lround((top * inverse_scale_y + y0) * tex->scale);
				candidate.rect.x1 = (int)std::lround((right * inverse_scale_x + x0) * tex->scale);
				candidate.rect.y1 = (int)std::lround((bottom * inverse_scale_y + y0) * tex->scale);
				candidate.rect.score = score;
				candidate.score = score;
				candidates.push_back(candidate);
			}
		}

		std::sort(candidates.begin(), candidates.end(),
			  [](const candidate_s &a, const candidate_s &b) { return a.score > b.score; });
		if (candidates.size() > max_candidates)
			candidates.resize(max_candidates);
		for (const candidate_s &candidate : candidates) {
			bool suppressed = std::any_of(p->rects.begin(), p->rects.end(), [&](const rect_s &selected) {
				return rect_iou(candidate.rect, selected) > p->nms_threshold;
			});
			if (!suppressed)
				p->rects.push_back(candidate.rect);
		}
	} catch (const std::exception &e) {
		blog(LOG_ERROR, "SCRFD detection failed: %s", e.what());
		p->failed_model_filename = p->model_filename;
		p->retry_after_ns = os_gettime_ns() + error_retry_ns;
		p->session.reset();
		p->loaded_model_filename.clear();
		p->rects.clear();
	}
}
