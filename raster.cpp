#include "raster.h"

#include <algorithm>
#include <assert.h>
#include <vector>
#include <stack>
#include <cmath>

using namespace libfnt;

// Note: Pass outline by-value to avoid mutation of original struct.
const RasterTarget* libfnt::rasterise_outline(const OutlineData &ot, float upem)
{
	// Transform all edges in the edge-table into raster space.
	auto raster_table = *ot.et;
	
	for (auto& e : raster_table.edges) {
		e.transform(upem, ot.bounding_box.xMin, ot.bounding_box.yMin);
	}

	// Allocate bitmap memory.
	const auto xExtent = em2raster(ot.bounding_box.xMax - ot.bounding_box.xMin, upem);
	const auto yExtent = em2raster(ot.bounding_box.yMax - ot.bounding_box.yMin, upem);

	const size_t img_width = std::ceil(xExtent);
	const size_t img_height = std::ceil(yExtent);
	RasterTarget* target = new RasterTarget(img_width, img_height);

	// Rasterise outline.
	const float kScanlineDelta = 1.0f;

	for (float scanline = 0.5f; scanline < target->height; scanline += kScanlineDelta) {
		std::vector<float> crossings; // @perf: set capacity to avoid allocs?

		for (auto &e : raster_table.edges) {
			if (e.is_active) {
				if (e.apex.y<= scanline) {
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
			const auto xs = crossings[k];
			const auto xe = crossings[k + 1];

			for (int x = xs; x <= xe; x++) {
				target->store(x, (int)scanline, 0xff);
			}

		}
	}

	return target;
}