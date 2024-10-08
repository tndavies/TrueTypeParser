#pragma once
#include <stdint.h>
#include <vector>
#include <assert.h>

uint8_t get_u8(void* stream);
uint16_t get_u16(void* stream);
uint32_t get_u32(void* stream);
uint64_t get_u64(void* stream);

int8_t get_s8(void* stream);
int16_t get_s16(void* stream);
int32_t get_s32(void* stream);
int64_t get_s64(void* stream);

typedef uint32_t char_code;

template <typename T>
struct Point {
	T x, y;

	Point() : x(0), y(0) {}

	Point(T x, T y) 
		: x(x), y(y) {} 
};

using fPoint = Point<float>;

float em2raster(const float value, const float upem);
void em2rasterPt(fPoint& pt, float upem);

struct Edge {
	Edge(fPoint p0, fPoint p1) 
		: is_active(false), m(0), c(0), sclx(0) 
	{
		// classify the points into min & max.
		if (p0.y > p1.y) {
			apex.x = p0.x;
			apex.y = p0.y;

			base.x = p1.x;
			base.y = p1.y;
		}
		else {
			apex.x = p1.x;
			apex.y = p1.y;

			base.x = p0.x;
			base.y = p0.y;
		}

		is_vertical = (apex.x == base.x);
	}

	void transform(const float upem, float xmin, float ymin) {
		// translate the outline into the 1st quadrant.
		apex.x -= xmin;
		base.x -= xmin;
		
		apex.y -= ymin;
		base.y -= ymin;

		// scale points into bitmap space.
		em2rasterPt(apex, upem);
		em2rasterPt(base, upem);

		// calculate gradient & intercept (for non-vertical edges).
		if (!is_vertical) {
			m = (apex.y - base.y) / (apex.x - base.x);
			c = base.y - m * base.x;
		}
	}

	fPoint apex, base;

	bool is_active;
	bool is_vertical;

	float m, c;
	float sclx;

};

struct Bezier {
	fPoint p0, ctrl, p1;

	Bezier(fPoint p0, fPoint ctrl, fPoint p1)
		: p0(p0), ctrl(ctrl), p1(p1) {}
};

struct edgeTable
{
	std::vector<Edge> edges;

	void add_edge(fPoint p0, fPoint p1);
	void add_bezier(fPoint p0, fPoint ctrl, fPoint p1);

};

struct OutlineData {
	float xMin, yMin, xMax, yMax;
	const edgeTable *et;

	OutlineData(const edgeTable *et, float xMin, float xMax, float yMin, float yMax)
		: et(et), xMin(xMin), yMin(yMin), xMax(xMax), yMax(yMax) {}
};

struct RasterTarget {
	void* m_memory;
	size_t width, height;

	RasterTarget(size_t Width, size_t Height) 
	: width(Width), height(Height), m_memory(nullptr)
	{
		const auto img_size = width * height;
		m_memory = std::malloc(img_size);
		assert(m_memory);
		std::memset(m_memory, 0, img_size);
	}

	void store(const size_t x, const size_t y, const char colour) {
		((char*)m_memory)[x + y * width] = colour;
	}

	uint8_t fetch(const size_t x, const size_t y) const {
		return ((uint8_t*)m_memory)[y * width + x];
	}
};