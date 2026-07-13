#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

struct obs_data;
typedef struct obs_data obs_data_t;

#ifdef _WIN32
#define DEBUG_DATA_PATH_FILTER "TSV Files (*.tsv);;Data Files (*.dat);;All Files (*.*)"
#else
#define DEBUG_DATA_PATH_FILTER "Data Files (*.dat);;TSV Files (*.tsv);;All Files (*.*)"
#endif

#define CALLDATA_FIXED_DECL(cd, size)       \
	calldata_t cd;                      \
	uint8_t calldata_##cd##_stack[128]; \
	calldata_init_fixed(&cd, calldata_##cd##_stack, sizeof(calldata_##cd##_stack));

struct pointf_s
{
	float x;
	float y;
};

struct rect_s
{
	int x0;
	int y0;
	int x1;
	int y1;
	float score;
};

struct rectf_s
{
	float x0;
	float y0;
	float x1;
	float y1;
};

enum rect_selection_policy {
	rect_selection_sticky = 0,
	rect_selection_largest = 1,
	rect_selection_center = 2,
	rect_selection_first = 3,
};

static inline float rect_area(const rect_s &rect)
{
	float width = (float)(rect.x1 - rect.x0);
	float height = (float)(rect.y1 - rect.y0);
	return width > 0.0f && height > 0.0f ? width * height : 0.0f;
}

static inline float rect_center_distance_sq(const rect_s &rect, float x, float y)
{
	float dx = (rect.x0 + rect.x1) * 0.5f - x;
	float dy = (rect.y0 + rect.y1) * 0.5f - y;
	return dx * dx + dy * dy;
}

static inline int select_gpu_device(int requested, int device_count)
{
	if (device_count <= 0)
		return -1;
	if (requested < 0 || requested >= device_count)
		return 0;
	return requested;
}

static inline uint64_t interval_deadline_ns(uint64_t now_ns, int interval_ms)
{
	return now_ns + (uint64_t)(interval_ms > 0 ? interval_ms : 1) * 1000000ULL;
}

static inline size_t select_rect_index(const std::vector<rect_s> &rects, enum rect_selection_policy policy,
				       bool reference_valid, const rect_s &reference, const rectf_s &frame)
{
	if (rects.empty() || policy == rect_selection_first)
		return 0;

	size_t best = 0;
	if (policy == rect_selection_largest || (policy == rect_selection_sticky && !reference_valid)) {
		float best_area = rect_area(rects[0]);
		for (size_t i = 1; i < rects.size(); i++) {
			float area = rect_area(rects[i]);
			if (area > best_area) {
				best = i;
				best_area = area;
			}
		}
		return best;
	}

	float x = (frame.x0 + frame.x1) * 0.5f;
	float y = (frame.y0 + frame.y1) * 0.5f;
	if (policy == rect_selection_sticky) {
		x = (reference.x0 + reference.x1) * 0.5f;
		y = (reference.y0 + reference.y1) * 0.5f;
	}
	float best_distance = rect_center_distance_sq(rects[0], x, y);
	for (size_t i = 1; i < rects.size(); i++) {
		float distance = rect_center_distance_sq(rects[i], x, y);
		if (distance < best_distance) {
			best = i;
			best_distance = distance;
		}
	}
	return best;
}

struct f3
{
	float v[3];

	f3(const f3 &a) { *this = a; }
	f3(float a, float b, float c)
	{
		v[0] = a;
		v[1] = b;
		v[2] = c;
	}
	f3(const rect_s &a)
	{
		v[0] = (float)(a.x0 + a.x1) * 0.5f;
		v[1] = (float)(a.y0 + a.y1) * 0.5f;
		v[2] = sqrtf((float)(a.x1 - a.x0) * (float)(a.y1 - a.y0));
	}
	f3(const rectf_s &a)
	{
		v[0] = (a.x0 + a.x1) * 0.5f;
		v[1] = (a.y0 + a.y1) * 0.5f;
		v[2] = sqrtf((a.x1 - a.x0) * (a.y1 - a.y0));
	}
	f3 operator+(const f3 &a) { return f3(v[0] + a.v[0], v[1] + a.v[1], v[2] + a.v[2]); }
	f3 operator-(const f3 &a) { return f3(v[0] - a.v[0], v[1] - a.v[1], v[2] - a.v[2]); }
	f3 operator*(float a) { return f3(v[0] * a, v[1] * a, v[2] * a); }
	f3 &operator+=(const f3 &a) { return *this = f3(v[0] + a.v[0], v[1] + a.v[1], v[2] + a.v[2]); }
	f3 &operator=(const f3 &a)
	{
		v[0] = a.v[0];
		v[1] = a.v[1];
		v[2] = a.v[2];
		return *this;
	}

	f3 hp(const f3 &a) const { return f3(v[0] * a.v[0], v[1] * a.v[1], v[2] * a.v[2]); }
};

static inline bool isnan(const f3 &a)
{
	return std::isnan(a.v[0]) || std::isnan(a.v[1]) || std::isnan(a.v[2]);
}

static inline int get_width(const rect_s &r)
{
	return r.x1 - r.x0;
}
static inline int get_height(const rect_s &r)
{
	return r.y1 - r.y0;
}
static inline float get_width(const rectf_s &r)
{
	return r.x1 - r.x0;
}
static inline float get_height(const rectf_s &r)
{
	return r.y1 - r.y0;
}

static inline int common_length(int a0, int a1, int b0, int b1)
{
	// assumes a0 < a1, b0 < b1
	// if (a1 <= b0) return 0; // a0 < a1 < b0 < b1
	if (a0 <= b0 && b0 <= a1 && a1 <= b1)
		return a1 - b0; // a0 < b0 < a1 < b1
	if (a0 <= b0 && b1 <= a1)
		return b1 - b0; // a0 < b0 < b1 < a1
	if (b0 <= a0 && a1 <= b1)
		return a1 - a0; // b0 < a0 < a1 < b1
	if (b0 <= a0 && a0 <= b1 && a0 <= b1)
		return b1 - a0; // b0 < a0 < b1 < a1
	// if (b1 <= a0) return 0; // b0 < b1 < a0 < a1
	return 0;
}

static inline int common_area(const rect_s &a, const rect_s &b)
{
	return common_length(a.x0, a.x1, b.x0, b.x1) * common_length(a.y0, a.y1, b.y0, b.y1);
}

static inline float rect_iou(const rect_s &a, const rect_s &b)
{
	float intersection = (float)common_area(a, b);
	float union_area = rect_area(a) + rect_area(b) - intersection;
	return union_area > 0.0f ? intersection / union_area : 0.0f;
}

template<typename T> static inline bool samesign(const T &a, const T &b)
{
	if (a > 0 && b > 0)
		return true;
	if (a < 0 && b < 0)
		return true;
	return false;
}

static inline float sqf(float x)
{
	return x * x;
}

/*
 * Apply a symmetric soft deadband with hysteresis.  Once an axis settles
 * inside the deadband it remains held until the error clears the nonlinear
 * band (or half the deadband when no nonlinear band is configured).  This
 * prevents detector noise near the boundary from repeatedly reversing the
 * controller output.
 */
static inline float apply_control_deadband(float value, float deadband, float nonlinear, bool &settled)
{
	deadband = std::max(deadband, 0.0f);
	nonlinear = std::max(nonlinear, 0.0f);
	const float magnitude = std::abs(value);
	const float release = deadband + std::max(nonlinear, deadband * 0.5f);

	if (settled) {
		if (magnitude <= release)
			return 0.0f;
		settled = false;
	} else if (magnitude <= deadband) {
		settled = true;
		return 0.0f;
	}

	if (nonlinear > 0.0f && magnitude < deadband + nonlinear) {
		const float softened = sqf(magnitude - deadband) / (2.0f * nonlinear);
		return std::copysign(softened, value);
	}

	const float adjusted = magnitude - deadband - nonlinear * 0.5f;
	return std::copysign(std::max(adjusted, 0.0f), value);
}

static inline rectf_s f3_to_rectf(const f3 &u, float w, float h)
{
	const float srwh = sqrtf(w * h);
	const float s2h = h / srwh;
	const float s2w = w / srwh;
	rectf_s r;
	r.x0 = u.v[0] - s2w * u.v[2] * 0.5f;
	r.x1 = u.v[0] + s2w * u.v[2] * 0.5f;
	r.y0 = u.v[1] - s2h * u.v[2] * 0.5f;
	r.y1 = u.v[1] + s2h * u.v[2] * 0.5f;
	return r;
}

void draw_rect_upsize(rect_s r, float upsize_l = 0.0f, float upsize_r = 0.0f, float upsize_t = 0.0f,
		      float upsize_b = 0.0f);
void draw_landmark(const std::vector<pointf_s> &landmark);
float landmark_area(const std::vector<pointf_s> &landmark);
pointf_s landmark_center(const std::vector<pointf_s> &landmark);

inline double from_dB(double x)
{
	return exp(x * (M_LN10 / 20));
}

void debug_data_open(FILE **dest, char **last_name, obs_data_t *settings, const char *name);

#ifdef _WIN32
bool preload_cudnn_runtime_libraries();
#endif
