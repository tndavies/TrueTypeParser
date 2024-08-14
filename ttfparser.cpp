#include "ttfparser.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include "utils.h"
#include <array>
#include <algorithm>

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
	std::vector<Point> points;
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


/////////////////////////////////////////////////////////////////////////////////////

typedef int16_t FontUnit;

struct Edge {
	FontUnit x0, y0;
	FontUnit x1, y1;

	Edge(FontUnit x0, FontUnit y0, FontUnit x1, FontUnit y1)
		: x0(x0), y0(y0), x1(x1), y1(y1)
	{}
};

std::vector<Edge> build_edge_table(std::vector<Point> points) {
	std::vector<Edge> edge_table;

	for (size_t k = 1; k < points.size(); k++) {
		const auto& pt1 = points[k];
		const auto& pt0 = points[k - 1];

		if (pt0.yc == pt1.yc) continue; // skip horizontal edge.

		edge_table.emplace_back(pt0.xc, pt0.yc, pt1.xc, pt1.yc);
	}

	assert(edge_table.size());

	return edge_table;
}

Bitmap TTFParser::allocate_bitmap(const Outline_Descriptor& outline) {
	size_t bitmap_width = std::ceil(toPixels(outline.x_extent, m_UnitsPerEM));
	size_t bitmap_height = std::ceil(toPixels(outline.y_extent, m_UnitsPerEM));

	size_t bytes = bitmap_width * bitmap_height;
	void* memory = malloc(bytes);
	assert(memory);

	std::memset(memory, 0, bytes);

	return Bitmap(memory, bitmap_width, bitmap_height);
}

void fill_span(float x0, float x1, float y, const Bitmap& bitmap) {
	// @todo: how should we cast from float here?
	for (int px = (int)x0; px <= (int)x1; px++) {
		PutPixel(px, y, 0xff, bitmap.memory, bitmap.width);
	}
}

Bitmap TTFParser::RasterizeGlyph(int codepoint) {
	// Extract outline information
	auto outline_desc = ExtractOutline(codepoint);

	// Construct edge table. 
	auto edge_table = build_edge_table(outline_desc.points);

	// Allocate bitmap memory.
	Bitmap bitmap = allocate_bitmap(outline_desc);

	// Rasterise outline.
	for (float scanline = 0.0f; scanline < bitmap.height; scanline++)
	{
		// find scanline intersections.
		std::vector<float> hit_list;
		for (const auto& e : edge_table) {
			auto yMin = toPixels(std::min(e.y0, e.y1), m_UnitsPerEM);
			auto yMax = toPixels(std::max(e.y0, e.y1), m_UnitsPerEM);
			
			auto x0 = toPixels(e.x0, m_UnitsPerEM);
			auto y1 = toPixels(e.y1, m_UnitsPerEM);
			auto x1 = toPixels(e.x1, m_UnitsPerEM);
			auto y0 = toPixels(e.y0, m_UnitsPerEM);

			if (scanline >= yMin && scanline <= yMax) {
				if (e.x0 != e.x1) { // slanted line
					float gradient = float(y1 - y0) / (x1 - x0);
					float intercept = y0 - gradient * x0;
					hit_list.push_back( (scanline - intercept) / gradient );
				}
				else { // vertical line
					hit_list.push_back(x0);
				}
			}
		}

		// fill in the predetermined spans.
		assert(hit_list.size() % 2 == 0);

		std::sort(hit_list.begin(), hit_list.end());

		for (size_t k = 1; k < hit_list.size(); k+=2) {
			fill_span(hit_list[k - 1], hit_list[k], scanline, bitmap);
		}
	}

	return bitmap;
}
