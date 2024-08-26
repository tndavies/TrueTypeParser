#include "ttfparser.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include "utils.h"
#include <array>
#include <algorithm>
#include <list>
#include <stack>

TTFParser::TTFParser(const char* file_path) 
	: m_FilePath(file_path), m_Data(nullptr), m_Encoding(nullptr)
{
	m_Data = LoadFile(m_FilePath.c_str());

	check_supported();
	
	register_tables();
	
	// query em coordinate space info.
	auto head = get_table("head");
	m_UnitsPerEM = get_u16(head + 18);
	std::cout << "UnitsPerEM: " << m_UnitsPerEM << std::endl;

	select_encoding_scheme();
}

void TTFParser::check_supported()
{
    auto OutlineType = get_u32(m_Data);
	assert(OutlineType == 0x00010000 || OutlineType == 0x74727565); // only support ttf outlines.
}

void TTFParser::register_tables() 
{
	auto TableCount = get_u16(m_Data + 4);
	for (size_t tbl_idx = 0; tbl_idx < TableCount; tbl_idx++)
	{
		auto Metadata = (m_Data + 12) + (16 * tbl_idx);

		std::string tag;
		for (size_t k = 0; k < 4; k++)
			tag += std::tolower(Metadata[k]);

		auto offset = get_u32(Metadata + 8);

		m_Tables[tag] = offset;
	}
}

void TTFParser::select_encoding_scheme() 
{
	auto* cmap = get_table("cmap");
	auto EncodingCount = get_u16(cmap + 2);
	for (size_t k = 0; k < EncodingCount; k++)
	{
		uint8_t* EncodingMetatable = cmap + 4 + 8 * k;
		uint8_t* format_table = cmap + get_u32(EncodingMetatable + 4);
		auto format = get_u16(format_table);

		switch (format) {
		case 0:
			m_Encoding = new CodepointMap_Format0(format_table + 6);
			break;
		}
	}
	assert(m_Encoding);
}

std::vector<int16_t>* parse_coordinate_data(const std::vector<uint8_t>& flags, 
	uint8_t** stream_handle, size_t shortbit_idx, size_t infobit_idx) 
{
	std::vector<int16_t>* buff = new std::vector<int16_t>{ 0 };

	for (auto flag : flags) {
		bool is_short = flag & (1 << shortbit_idx);
		bool info_bit = flag & (1 << infobit_idx);

		if (is_short) {
			uint8_t delta = **stream_handle;
			(*stream_handle)++;

			int16_t cval = buff->back() + delta * (info_bit ? 1 : -1);
			buff->push_back(cval);
		}
		else {
			if (info_bit) {
				buff->push_back(buff->back());
			}
			else {
				auto delta = get_s16(*stream_handle);
				*stream_handle += 2;

				int16_t cval = buff->back() + delta;
				buff->push_back(cval);
			}
		}
	}

	buff->erase(buff->begin()); // remove the reference point (0,0).

	return buff;
}

Outline_Descriptor TTFParser::extract_outline(int cp) 
{
	auto glyph_index = m_Encoding->get_glyph_index(cp);

	// Find the location of the outline data.
	uint8_t* outline = nullptr;

	auto head = get_table("head");
	auto loca = get_table("loca");
	auto glyf = get_table("glyf");

	auto loca_format = get_s16(head + 50);
	assert(loca_format == 0 || loca_format == 1);
	if (loca_format == 1) {
		auto* offset = reinterpret_cast<uint32_t*>(loca) + glyph_index;
		outline = glyf + get_u32(offset);
	}
	else {
		auto* offset = reinterpret_cast<uint16_t*>(loca) + glyph_index;
		outline = glyf + 2 * get_u16(offset);
	}

	// Parse the point data.
	auto contour_count = get_s16(outline);
	assert(contour_count > 0); // only support simple outline atm!

	auto xMin = get_s16(outline + 2); 
	auto yMin = get_s16(outline + 4); 
	auto xMax = get_s16(outline + 6); 
	auto yMax = get_s16(outline + 8); 

	auto x_extent = xMax - xMin;
	auto y_extent = yMax - yMin;

	uint8_t* contour_data = outline + 10;
	auto* point_map_end = (uint16_t*)contour_data + (contour_count - 1);
	auto point_count = get_u16(point_map_end) + 1;
	uint8_t* hprog = (uint8_t*)(point_map_end + 1);
	uint8_t* stream = hprog + 2 + get_u16(hprog);

	// decompress flags array.
	std::vector<uint8_t> flags;
	while (flags.size() < point_count) {
		uint8_t flag = *stream++;

		size_t repeat_count = 1;
		if (flag & (1 << 3)) {
			repeat_count += *stream++;
		}

		for (size_t n = 0; n < repeat_count; n++) {
			flags.push_back(flag);
		}
	}

	std::vector<int16_t>* xc_buff = parse_coordinate_data(flags, &stream, 1, 4);
	std::vector<int16_t>* yc_buff = parse_coordinate_data(flags, &stream, 2, 5);

	// Build points list.
	std::vector<libfnt_point> points;
	for (size_t k = 0; k < flags.size(); k++) {
		points.emplace_back(xc_buff->at(k) - xMin, yc_buff->at(k) - yMin, flags[k] & 1);
	}
	points.push_back(points.front());

	return {points, x_extent, y_extent};
}

float toPixels(int16_t em, float upem, float pts = 12.0f, float dpi = 96.0f) 
{
	return (em / upem) * pts * dpi;
}

void PutPixel(int x, int y, char val, void* pixels, int stride)
{
	uint8_t* pixel = (uint8_t*)pixels + y * stride + x;
	*pixel = val;
}

libfnt_edge::libfnt_edge(libfnt_point p0, libfnt_point p1, float upem)
	: active(false), is_vertical(true), m(0), c(0), sclx(0) {

	// classify the points into min & max.
	if (p0.y > p1.y) {
		xmax = p0.x;
		ymax = p0.y;

		xmin = p1.x;
		ymin = p1.y;
	}
	else {
		xmax = p1.x;
		ymax = p1.y;

		xmin = p0.x;
		ymin = p0.y;
	}

	// scale points into bitmap space.
	ymax = toPixels(ymax, upem);
	ymin = toPixels(ymin, upem);
	xmax = toPixels(xmax, upem);
	xmin = toPixels(xmin, upem);

	// calculate gradient & intercept (for non-vertical edges).
	if (p0.x != p1.x) {
		m = (ymax - ymin) / (xmax - xmin);
		c = ymin - m * xmin;
		is_vertical = false;
	}
}

/////////////////////////////////////////////////////////////////////////////////////

struct libfnt_bezier {
	libfnt_point p0, ctrl, p1;

	libfnt_bezier(const libfnt_point& p0_, const libfnt_point& ctrl_, const libfnt_point& p1_)
		: p0(p0_), ctrl(ctrl_), p1(p1_) {}
};

enum class libfnt_quality_level {
	Restricted	= 0,
	Low			= 1, 
	Medium		= 2, 
	High		= 3
};

void subdivide_qbezier(const libfnt_bezier& b, float upem, 
	std::vector<libfnt_edge>& edge_buff, const libfnt_quality_level quality)
{
	// For the lowest quality setting, we simple approximate the bezier
	// curve by a line connecting its endpoints, performing no
	// subdivisions, to save compute time.
	if (quality == libfnt_quality_level::Restricted) {
		edge_buff.emplace_back(b.p0, b.p1, upem);
		return;
	}

	const float tolerance = 100.0f / std::powf(10.0f, (float)quality);

	// @todo: When we have small tolerances, we tend to subdivide the curves so many times
	// that their control points converge to the same point, as we currently store the coordinates
	// as int16_t in libfnt_point. This then causes m=0, and the resulting distance inf.
	// Without the assert here, this causes an infinte loop where we keep subdiving as the if fails,
	// and our stack grows indefinetly!

	std::stack<libfnt_bezier> stack;

	stack.push(b);

	while (!stack.empty()) {
		const libfnt_bezier curr = stack.top(); // copy off stack top.

		stack.pop();
		
		// See: https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line.
		const auto [x0,y0, o_] = curr.ctrl;
		const auto [x1,y1, t_] = curr.p0;
		const auto [x2,y2, tt_] = curr.p1;

		const float k = (y2 - y1) * x0 - (x2 - x1) * y0 + x2 * y1 - y2 * x1;
		const float m = std::powf(y2 - y1, 2) + std::powf(x2 - x1, 2);
		assert(m != 0.0f);
		
		const float dist = std::fabsf(k) / std::sqrtf(m);

		// 
		if (dist <= tolerance) {
			edge_buff.emplace_back(curr.p0, curr.p1, upem);
			continue;
		}
	
		//
		libfnt_point m0 = (curr.p0 + curr.ctrl) / 2.0f;
		libfnt_point m2 = (curr.ctrl + curr.p1) / 2.0f;
		libfnt_point m1 = (m0 + m2) / 2.0f;

		stack.emplace(curr.p0, m0, m1);
		stack.emplace(m1, m2, curr.p1);
	}
}

/////////////////////////////////////////////////////////////////////////////////////

std::vector<libfnt_edge> build_edge_table(std::vector<libfnt_point> Points, float upem) 
{
	assert(Points[0].on_curve); // @todo: handle p1 being off-curve.
	
#ifdef Print_Seq
	std::cout << "Sequence: ";
	for (auto& p : Points) std::cout << (p.on_curve ? '1' : '0');
	std::cout << std::endl;
#endif

	size_t j = 0;
	std::vector<libfnt_edge> edge_table;

	// blowout sequence...
	// @speed: iterate backwards to avoid call to size()?
	while (j < Points.size() - 1) {

		const auto p0 = Points[j];
		const auto p1 = Points[j + 1];

		if (!p0.on_curve && !p1.on_curve) {
			// Note: the inferred point between two off-curve points
			// is an on-curve point that lies at the midpoint of the
			// connecting line.
			const float px = 0.5f * (p0.x + p1.x);
			const float py = 0.5f * (p0.y + p1.y);
			libfnt_point inferred_pt (px, py, true);
			
			const auto itr = Points.begin() + j + 1;
			Points.insert(itr, inferred_pt);
			++j;
		}

		++j;
	}

#ifdef Print_Seq
	std::cout << "Blown-out: ";
	for (auto& p : Points) std::cout << (p.on_curve ? '1' : '0');
	std::cout << std::endl;
#endif
	// @speed: bitfield & bit-op checks to increase cache bandwidth
	// and reduce branching?

	// generate table...
	size_t i = 0;
	std::vector<libfnt_point> buff;
	
	while (i < Points.size()) {
		buff.push_back(Points[i]);
		++i;
	
		if (buff.size() == 2 && buff[0].on_curve && buff[1].on_curve) {
			// add edge
			if (buff[0].y != buff[1].y) {
				libfnt_edge e(buff[0], buff[1], upem);
				edge_table.push_back(e);
			}

			const libfnt_point cached = buff[1];
			buff.clear();
			buff.push_back(cached);
		}
		// @speed: do we need to check if buff[2] is on curve, or is this guarenteed?
		else if (buff.size() == 3 && buff[0].on_curve && !buff[1].on_curve && buff[2].on_curve) {
			libfnt_bezier bc(buff[0], buff[1], buff[2]);

			subdivide_qbezier(bc, upem, edge_table, libfnt_quality_level::Medium);

			const libfnt_point cached = buff[2];
			buff.clear();
			buff.push_back(cached);
		}
	}

	return edge_table;
}

/////////////////////////////////////////////////////////////////////////////////////

Bitmap TTFParser::allocate_bitmap(const Outline_Descriptor& outline) {
	size_t bitmap_width = std::ceil(toPixels(outline.x_extent, m_UnitsPerEM));
	size_t bitmap_height = std::ceil(toPixels(outline.y_extent, m_UnitsPerEM));

	size_t bytes = bitmap_width * bitmap_height;
	void* memory = std::malloc(bytes);
	assert(memory);

	std::memset(memory, 0, bytes);

	return Bitmap(memory, bitmap_width, bitmap_height);
}

Bitmap TTFParser::rasterize(int Codepoint) 
{
	// Extract outline information
	auto outline_desc = extract_outline(Codepoint);

	// Construct edge table. 
	auto edges = build_edge_table(outline_desc.points, m_UnitsPerEM);

	// Allocate bitmap memory.
	Bitmap bitmap = allocate_bitmap(outline_desc);

	// Rasterise outline.
	const float kScanlineDelta = 1.0f;

	for(float scanline = 0.5f; scanline < bitmap.height; scanline += kScanlineDelta) {
		std::vector<float> crossings; // @perf: set capacity to avoid allocs?

		for(auto& e: edges){
			if(e.active) {
				if(e.ymax <= scanline) {
					// edge shouldn't be active anymore.
					e.active = false;
				} else {
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
				if(scanline >= e.ymin && scanline < e.ymax){
					// create new active edge.
					e.active = true;
					e.sclx = e.is_vertical ? e.xmin : (scanline - e.c) / e.m;
					crossings.push_back(e.sclx);
				}
			}
		}

		assert(crossings.size() % 2 == 0);
		std::sort(crossings.begin(), crossings.end());

		for (size_t k = 0; k < crossings.size(); k += 2) {
			const auto xs = crossings[k];
			const auto xe = crossings[k + 1];

			for (int x = xs; x <= xe; x++)
				PutPixel(x, (int)scanline, 0xff, bitmap.memory, bitmap.width);
		}
	}

	return bitmap;
}