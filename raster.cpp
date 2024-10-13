#include <algorithm>
#include <assert.h>
#include <stack>
#include <cmath>

#include "raster.h"

//

Edge::Edge(fPoint p0, fPoint p1)
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

void
Edge::transform(const float upem, float xmin, float ymin)
{
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

//

void
EdgeTable::addEdge(const fPoint& p0, const fPoint& p1)
{
	edges.emplace_back(p0, p1);
}

void
EdgeTable::addBezier(const fPoint& p0, const fPoint& ctrl, const fPoint& p1)
{
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

//

RasterTarget::RasterTarget(const size_t width, const size_t height)
	: width(width), height(height), memory_(nullptr)
{
	const auto imgSize = width * height;
	memory_ = std::malloc(imgSize);
	assert(memory_);
	std::memset(memory_, 0, imgSize);
}

void
RasterTarget::store(const size_t x, const size_t y, const uint8_t colour) {
	((char*)memory_)[x + y * width] = colour;
}

uint8_t
RasterTarget::fetch(const size_t x, const size_t y) const {
	return ((uint8_t*)memory_)[y * width + x];
}

//

float
em2raster(const float value, const float upem) {
	const float pts = 12.0f;
	const float dpi = 96.0f;

	return (value / upem) * pts * dpi;
}

void
em2rasterPt(fPoint& pt, const float upem) {
	const float pts = 12.0f;
	const float dpi = 96.0f;

	pt.x = em2raster(pt.x, upem);
	pt.y = em2raster(pt.y, upem);
}

//

const RasterTarget*
RenderOutline(const Outline& ot, const float upem)
{
	// Transform all edges in the edge-table into raster space.
	auto raster_table = *ot.et;

	for (auto& e : raster_table.edges) {
		e.transform(upem, ot.bb.xMin, ot.bb.yMin);
	}

	// Allocate bitmap memory.
	const auto xExtent = em2raster(ot.bb.xMax - ot.bb.xMin, upem);
	const auto yExtent = em2raster(ot.bb.yMax - ot.bb.yMin, upem);

	const size_t img_width = std::ceil(xExtent);
	const size_t img_height = std::ceil(yExtent);
	RasterTarget* target = new RasterTarget(img_width, img_height);

	// Rasterise outline.
	const float kScanlineDelta = 1.0f;

	for (float scanline = 0.5f; scanline < target->height; scanline += kScanlineDelta) {
		std::vector<float> crossings; // @perf: set capacity to avoid allocs?

		for (auto& e : raster_table.edges) {
			if (e.is_active) {
				if (e.apex.y <= scanline) {
					// edge shouldn't be active anymore.
					e.is_active = false;
				}
				else {
					// edge is still active so update its intersection point.
					if (!e.is_vertical) { // intersection only changes for non-vertical edges.
						e.sclx += kScanlineDelta / e.m;
					}
					crossings.push_back(e.sclx);
				}
			}
			else {
				// Note: the scanline can never be on ymax in this
				// codepath as the edge would be activated from it
				// passing ymin.
				if (scanline >= e.base.y && scanline < e.apex.y) {
					// create new active edge.
					e.is_active = true;
					e.sclx = e.is_vertical ? e.base.x : (scanline - e.c) / e.m;
					crossings.push_back(e.sclx);
				}
			}
		}

		assert(crossings.size() % 2 == 0);

		std::sort(crossings.begin(), crossings.end());

		for (size_t k = 0; k < crossings.size(); k += 2) {
			const float xs = crossings[k];
			const float xe = crossings[k + 1];

			for (int x = xs; x <= xe; x++) {
				target->store(x, (int)scanline, 0xff);
			}

		}
	}

	return target;
}
