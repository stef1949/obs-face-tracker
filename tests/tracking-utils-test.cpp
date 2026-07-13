#include "helper.hpp"
#include <cstdlib>
#include <iostream>

static void expect(bool condition, const char *message)
{
	if (!condition) {
		std::cerr << "FAILED: " << message << '\n';
		std::exit(1);
	}
}

int main()
{
	std::vector<rect_s> faces = {
		{10, 10, 30, 30, 1.0f},
		{40, 40, 90, 90, 1.0f},
		{130, 10, 150, 30, 1.0f},
	};
	rectf_s frame{0.0f, 0.0f, 160.0f, 100.0f};
	rect_s previous{125, 5, 155, 35, 1.0f};

	expect(select_rect_index(faces, rect_selection_first, false, {}, frame) == 0, "first policy");
	expect(select_rect_index(faces, rect_selection_largest, false, {}, frame) == 1, "largest policy");
	expect(select_rect_index(faces, rect_selection_center, false, {}, frame) == 1, "center policy");
	expect(select_rect_index(faces, rect_selection_sticky, true, previous, frame) == 2, "sticky policy");
	expect(select_rect_index(faces, rect_selection_sticky, false, {}, frame) == 1,
	       "sticky policy falls back to largest");
	expect(common_area(faces[0], rect_s{20, 20, 40, 40, 1.0f}) == 100, "intersection area");
	expect(std::abs(rect_iou(faces[0], rect_s{20, 20, 40, 40, 1.0f}) - (100.0f / 700.0f)) < 1e-6f,
	       "intersection over union");
	expect(rect_iou(faces[0], faces[2]) == 0.0f, "non-overlapping intersection over union");
	expect(rect_area(rect_s{10, 10, 10, 30, 1.0f}) == 0.0f, "zero-width rectangle has no area");
	expect(rect_area(rect_s{30, 30, 10, 10, 1.0f}) == 0.0f, "inverted rectangle has no area");
	expect(select_rect_index({}, rect_selection_first, false, {}, frame) == 0, "empty input");
	expect(select_gpu_device(1, 3) == 1, "requested GPU device");
	expect(select_gpu_device(-1, 3) == 0, "automatic GPU device");
	expect(select_gpu_device(8, 3) == 0, "invalid GPU falls back to device zero");
	expect(select_gpu_device(0, 0) == -1, "no GPU available");
	expect(interval_deadline_ns(1000000ULL, 50) == 51000000ULL, "tracking interval deadline");
	expect(interval_deadline_ns(1000000ULL, 0) == 2000000ULL, "tracking interval minimum");

	bool settled = false;
	expect(apply_control_deadband(4.0f, 5.0f, 3.0f, settled) == 0.0f && settled,
	       "deadband enters settled state");
	expect(apply_control_deadband(7.0f, 5.0f, 3.0f, settled) == 0.0f && settled,
	       "deadband hysteresis holds through detector noise");
	expect(apply_control_deadband(9.0f, 5.0f, 3.0f, settled) > 0.0f && !settled,
	       "deadband releases after hysteresis threshold");
	bool positive_settled = false;
	bool negative_settled = false;
	float positive = apply_control_deadband(6.0f, 5.0f, 3.0f, positive_settled);
	float negative = apply_control_deadband(-6.0f, 5.0f, 3.0f, negative_settled);
	expect(std::abs(positive + negative) < 1e-6f, "soft deadband is symmetric");

	return 0;
}
