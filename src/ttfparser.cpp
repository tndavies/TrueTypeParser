#include "ttfparser.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include "utils.h"
#include <array>
#include <algorithm>
#include <list>

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

void TTFParser::register_tables() {
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

void TTFParser::select_encoding_scheme() {
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

Outline_Descriptor TTFParser::ExtractOutline(int cp) {
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

float toPixels(int16_t em, float upem, float pts = 12.0f, float dpi = 96.0f) {
	return (em / upem) * pts * dpi;
}

void PutPixel(int x, int y, char val, void* pixels, int stride)
{
	uint8_t* pixel = (uint8_t*)pixels + y * stride + x;
	*pixel = val;
}

libfnt_edge::libfnt_edge(libfnt_point p0, libfnt_point p1, float upem)
	: active(false), is_vertical(true) {

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

std::vector<libfnt_edge> build_edge_table(std::vector<libfnt_point> points, float upem) {
	std::vector<libfnt_edge> edge_table;

	for (size_t k = 1; k < points.size(); k++) {
		const auto& pt1 = points[k];
		const auto& pt0 = points[k - 1];

		if (pt0.y == pt1.y) continue; // Skip horizontal edges.

		libfnt_edge edge (pt0, pt1, upem);
		edge_table.push_back(edge);
	}

	assert(edge_table.size());

	//auto sorting_func = [](const Edge& lhs, const Edge& rhs) {
	//	return lhs.apex.yc > rhs.apex.yc;
	//};

	//std::sort(edge_table.begin(), edge_table.end(), sorting_func);

	return edge_table;
}

Bitmap TTFParser::allocate_bitmap(const Outline_Descriptor& outline) {
	size_t bitmap_width = std::ceil(toPixels(outline.x_extent, m_UnitsPerEM));
	size_t bitmap_height = std::ceil(toPixels(outline.y_extent, m_UnitsPerEM));

	size_t bytes = bitmap_width * bitmap_height;
	void* memory = std::malloc(bytes);
	assert(memory);

	std::memset(memory, 0, bytes);

	return Bitmap(memory, bitmap_width, bitmap_height);
}

void create_active_edge(libfnt_edge& edge, float scanline) {
	edge.sclx = edge.is_vertical ? edge.xmin : (scanline - edge.c) / edge.m;
	edge.active = true;
}

void update_active_edge_ix(libfnt_edge& edge, float scanline, float scl_delta) {
	if(!edge.is_vertical) { // intersection only changes for non-vertical edges.
		edge.sclx += scl_delta / edge.m;
	}
}

Bitmap TTFParser::RasterizeGlyph(int codepoint) {
	// Extract outline information
	auto outline_desc = ExtractOutline(codepoint);

	// Construct edge table. 
	auto edges = build_edge_table(outline_desc.points, m_UnitsPerEM);

	// Allocate bitmap memory.
	Bitmap bitmap = allocate_bitmap(outline_desc);

	// Rasterise outline.
	for(size_t bitmap_row = 0; bitmap_row < bitmap.height; bitmap_row++) {
		const float scanline = bitmap_row + 0.5f;

		std::vector<float> crossings;

		for(auto& e: edges){
			if(e.active) {
				if(e.ymax <= scanline) {
					// edge shouldn't be active anymore.
					e.active = false;
				} else {
					// edge is still active so update its intersection point.
					update_active_edge_ix(e, scanline, 1);
					crossings.push_back(e.sclx);
				}
			}
			else {
				// note: the scanline can never be on ymax in this
				// codepath as the edge would be activated from it
				// passing ymin.
				if(scanline >= e.ymin && scanline < e.ymax){
					create_active_edge(e, scanline);
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
				PutPixel(x, bitmap_row, 0xff, bitmap.memory, bitmap.width);
		}
	}

	return bitmap;
}
