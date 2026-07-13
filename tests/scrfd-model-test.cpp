#include <onnxruntime_cxx_api.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

int main(int argc, char **argv)
{
	if (argc != 2 && argc != 4) {
		std::cerr << "usage: scrfd-model-test <model.onnx> [--cuda <device>]\n";
		return 2;
	}
	bool use_cuda = argc == 4 && std::string(argv[2]) == "--cuda";
	if (argc == 4 && !use_cuda) {
		std::cerr << "expected --cuda <device>\n";
		return 2;
	}

	try {
		Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "scrfd-model-test");
		Ort::SessionOptions options;
		options.SetIntraOpNumThreads(1);
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		if (use_cuda) {
			Ort::CUDAProviderOptions cuda_options;
			cuda_options.Update({{"device_id", argv[3]}, {"cudnn_conv_algo_search", "HEURISTIC"}});
			options.AppendExecutionProvider_CUDA_V2(*cuda_options);
		}
		Ort::Session session(env, std::filesystem::path(argv[1]).c_str(), options);
		if (session.GetInputCount() != 1) {
			std::cerr << "expected one SCRFD input\n";
			return 1;
		}

		Ort::AllocatorWithDefaultOptions allocator;
		auto input_name_value = session.GetInputNameAllocated(0, allocator);
		std::string input_name(input_name_value.get());
		auto input_info = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
		auto model_shape = input_info.GetShape();
		if (input_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || model_shape.size() != 4) {
			std::cerr << "invalid SCRFD input tensor\n";
			return 1;
		}
		int height = model_shape[2] > 0 ? (int)model_shape[2] : 640;
		int width = model_shape[3] > 0 ? (int)model_shape[3] : 640;
		std::vector<float> input((size_t)3 * (size_t)width * (size_t)height, -127.5f / 128.0f);
		std::array<int64_t, 4> input_shape = {1, 3, height, width};
		auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		auto tensor = Ort::Value::CreateTensor<float>(memory_info, input.data(), input.size(),
							      input_shape.data(), input_shape.size());

		size_t output_count = session.GetOutputCount();
		if (output_count != 6 && output_count != 9 && output_count != 10 && output_count != 15) {
			std::cerr << "unexpected SCRFD output count: " << output_count << '\n';
			return 1;
		}
		std::vector<std::string> output_names;
		std::vector<const char *> output_name_ptrs;
		output_names.reserve(output_count);
		output_name_ptrs.reserve(output_count);
		for (size_t i = 0; i < output_count; i++) {
			auto value = session.GetOutputNameAllocated(i, allocator);
			output_names.emplace_back(value.get());
		}
		for (const std::string &name : output_names)
			output_name_ptrs.push_back(name.c_str());
		const char *input_names[] = {input_name.c_str()};
		auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_name_ptrs.data(),
					   output_name_ptrs.size());

		size_t levels = output_count == 6 || output_count == 9 ? 3 : 5;
		int anchors = levels == 3 ? 2 : 1;
		constexpr std::array<int, 5> strides = {8, 16, 32, 64, 128};
		for (size_t level = 0; level < levels; level++) {
			size_t count =
				(size_t)(height / strides[level]) * (size_t)(width / strides[level]) * (size_t)anchors;
			for (size_t group = 0; group < output_count / levels; group++) {
				const auto &output = outputs[level + group * levels];
				int channels = group == 0 ? 1 : (group == 1 ? 4 : 10);
				auto info = output.GetTensorTypeAndShapeInfo();
				if (!output.IsTensor() ||
				    info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
				    info.GetElementCount() != count * (size_t)channels) {
					std::cerr << "invalid SCRFD output tensor at index " << level + group * levels
						  << '\n';
					return 1;
				}
			}
		}

		std::cout << "SCRFD model loaded with ONNX Runtime " << OrtGetApiBase()->GetVersionString()
			  << ", input " << width << 'x' << height << ", outputs " << output_count << ", provider "
			  << (use_cuda ? "CUDA" : "CPU") << '\n';
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
