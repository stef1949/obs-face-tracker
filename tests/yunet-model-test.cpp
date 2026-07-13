#include <onnxruntime_cxx_api.h>

#include <array>
#include <filesystem>
#include <iostream>
#include <vector>

int main(int argc, char **argv)
{
	if (argc != 2) {
		std::cerr << "usage: yunet-model-test <model.onnx>\n";
		return 2;
	}

	try {
		Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "yunet-model-test");
		Ort::SessionOptions options;
		options.SetIntraOpNumThreads(1);
		options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
		Ort::Session session(env, std::filesystem::path(argv[1]).c_str(), options);

		constexpr int width = 320;
		constexpr int height = 320;
		std::vector<float> input((size_t)3 * width * height, 0.0f);
		std::array<int64_t, 4> input_shape = {1, 3, height, width};
		auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
		auto tensor = Ort::Value::CreateTensor<float>(memory_info, input.data(), input.size(),
							      input_shape.data(), input_shape.size());
		const char *input_names[] = {"input"};
		constexpr std::array<const char *, 12> output_names = {
			"cls_8",  "cls_16",  "cls_32",  "obj_8", "obj_16", "obj_32",
			"bbox_8", "bbox_16", "bbox_32", "kps_8", "kps_16", "kps_32",
		};
		auto outputs = session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names.data(),
					   output_names.size());
		if (outputs.size() != output_names.size()) {
			std::cerr << "expected 12 outputs, received " << outputs.size() << '\n';
			return 1;
		}
		for (const Ort::Value &output : outputs) {
			if (!output.IsTensor() || output.GetTensorTypeAndShapeInfo().GetElementType() !=
							  ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
				std::cerr << "YuNet returned an invalid output tensor\n";
				return 1;
			}
		}
		std::cout << "YuNet model loaded with ONNX Runtime " << OrtGetApiBase()->GetVersionString() << '\n';
		return 0;
	} catch (const std::exception &e) {
		std::cerr << e.what() << '\n';
		return 1;
	}
}
