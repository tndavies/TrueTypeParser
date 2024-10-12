#include "utils.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include <stack> 

float em2raster(const float value, const float upem) {
	const float pts = 12.0f;
	const float dpi = 96.0f;

	return (value / upem) * pts * dpi;
}

void em2rasterPt(fPoint& pt, float upem) {
	const float pts = 12.0f;
	const float dpi = 96.0f;

	pt.x = em2raster(pt.x, upem);
	pt.y = em2raster(pt.y, upem);
}

uint8_t* LoadFile(const char* file_path)
{
	// Load font file into memory.
	std::ifstream FileHandle(file_path, std::ios::binary);
	if (!FileHandle.is_open()) {
		return nullptr;
	}

	FileHandle.seekg(0, FileHandle.end);
	const auto FileByteSize = FileHandle.tellg();
	FileHandle.seekg(0, FileHandle.beg);

	char* Data = static_cast<char*>(malloc(FileByteSize));
	FileHandle.read(Data, FileByteSize);
	FileHandle.close();
	assert(Data);

	return (uint8_t*)Data;
}

void edgeTable::add_edge(fPoint p0, fPoint p1) {
	edges.emplace_back(p0, p1);
}

void edgeTable::add_bezier(fPoint p0, fPoint ctrl, fPoint p1) {
	const float kTolerance = 1.0f; // @todo: what value makes sense for this?

	std::stack<Bezier> stack;

	stack.emplace(p0, ctrl, p1);

	while (!stack.empty()) {
		const Bezier curr = stack.top(); // copy off stack top.

		stack.pop();

		// See: https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line.
		const auto [x0, y0] = curr.ctrl;
		const auto [x1, y1] = curr.p0;
		const auto [x2, y2] = curr.p1;

		const float k = (y2 - y1) * x0 - (x2 - x1) * y0 + x2 * y1 - y2 * x1;
		const float m = std::powf(y2 - y1, 2) + std::powf(x2 - x1, 2);
		assert(m != 0.0f);

		const float dist = std::fabsf(k) / std::sqrtf(m);

		// 
		if (dist <= kTolerance) {
			edges.emplace_back(curr.p0, curr.p1);
			continue;
		}

		// @todo: The divide gets rounded here, do not in font units!
		auto m0x = 0.5f * (curr.p0.x + curr.ctrl.x);
		auto m0y = 0.5f * (curr.p0.y + curr.ctrl.y);
		fPoint m0(m0x, m0y);

		auto m2x = 0.5f * (curr.ctrl.x + curr.p1.x);
		auto m2y = 0.5f * (curr.ctrl.y + curr.p1.y);
		fPoint m2(m2x, m2y);

		auto m1x = 0.5f * (m0.x + m2.x);
		auto m1y = 0.5f * (m0.y + m2.y);
		fPoint m1(m1x, m1y);

		stack.emplace(curr.p0, m0, m1);
		stack.emplace(m1, m2, curr.p1);
	}

}
