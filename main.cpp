#include <iostream>
#include <string>
#include "ttfparser.h"
#include <array>
#include <algorithm> 

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

struct point {
	float x, y;
};

void fill_polygon(uint8_t* image, size_t img_width, size_t img_height, const std::vector<point>& points);

int main(int argc, char* argv[]) 
{
	if (argc < 2) {
		std::cout << "Error: No Truetype font file was specified (.ttf).";
		return -1;
	}

	// TTFParser ttf(argv[1]);

	const size_t dim_size = 600;
	uint8_t* image = new uint8_t[dim_size * dim_size * 3];

	std::vector<point> points;
	points.push_back({ 0,0});
	points.push_back({ 350,300});
	points.push_back({ 350,10});

	fill_polygon(image, dim_size, dim_size, points);

	stbi_flip_vertically_on_write(true);
	auto success = stbi_write_bmp("glyph.bmp", dim_size, dim_size, 3, image);
	assert(success);

	return 0;
}

void set_pixel(uint8_t* image, size_t img_width, size_t img_height, size_t x, size_t y, 
	uint8_t r, uint8_t g, uint8_t b) 
{
	uint8_t* pixel = &image[(3*img_width) * y + (x*3)];
	*pixel++ = r;
	*pixel++ = g;
	*pixel = b;
}

void fill_polygon(uint8_t* image, size_t img_width, size_t img_height, const std::vector<point>& points)
{
	memset(image, 0x10, img_width * img_height * 3);

	// find vertical extent of the polygon.
	std::vector<float> point_ys;
	for (auto& pt : points) point_ys.push_back(pt.y);
	std::sort(point_ys.begin(), point_ys.end());
	const float y_min = point_ys.front();
	const float y_max = point_ys.back();

	// construct edge list
	struct edge {
		edge(point p0, point p1) {
			this->p0 = p0;
			this->p1 = p1;

			grad = (p1.y - p0.y) / (p1.x - p0.x);
			c = p0.y - grad * p0.x;
		}

		point p0;
		point p1;
		float c;
		float grad;
	};

	std::vector<edge> edge_list;
	for (size_t k = 0; k < points.size(); k++) {
		auto p0 = points.at(k);
		
		size_t pt1_idx = k + 1;
		if (k == points.size() - 1) {
			pt1_idx = 0;
		}
		auto p1 = points.at(pt1_idx);

		edge_list.emplace_back(p0, p1);
	}

	// intersect scanline with edges, sort x_vals in increasing order
	for (float sl = y_min; sl <= y_max; sl += 0.1f)
	{
		std::vector<float> intersections;

		for (const auto& edge : edge_list) {
			float isx = (sl - edge.c) / edge.grad;

			bool include = false;
			if (edge.p0.x < edge.p1.x) {
				if (isx >= edge.p0.x && isx <= edge.p1.x) {
					include = true;
				}
			}
			else {
				if (isx >= edge.p1.x && isx <= edge.p0.x) {
					include = true;
				}
			}

			if (include) {
				intersections.push_back(isx);
			}
		}

		std::sort(intersections.begin(), intersections.end());

		// fill spans.
		for (size_t k = 0; k < intersections.size(); k++) 
		{
			if (k == intersections.size() - 1)
				break;

			auto x0 = intersections.at(k);
			auto x1 = intersections.at(k + 1);

			for (float x = x0; x <= x1; x += 0.1f) {
				set_pixel(image, img_width, img_height, x, sl, 255, 255, 255);
			}
		}
	}
	
	//for (auto pt : points) {
	//	set_pixel(image, img_width, img_height, pt.x, pt.y, 0xff, 0xff, 0);
	//}

	// fill in pairs of x_points, accounting for special case.
}
