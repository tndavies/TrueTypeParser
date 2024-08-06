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

Outline TTFParser::parse_outline(int cp) {
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

	auto width = xMax - xMin;
	auto height = yMax - yMin;

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

	std::vector<Point> points;
	for (size_t k = 0; k < flags.size(); k++) {
		points.emplace_back(xc_buff->at(k), yc_buff->at(k), flags[k] & 1);
	}
	points.push_back(points.front());

	// Build edge list.
	std::vector<Line> edges;
	for (size_t k = 0; k < points.size(); k++) {
		auto& pt = points[k];
		assert(pt.on_curve);

		pt.xc -= xMin;
		pt.yc -= yMin;

		if (k == 0) continue;

		edges.emplace_back(points[k - 1], pt, m_UnitsPerEM);
	}

	return {edges, width, height};
}

float toPixels(int16_t em, float upem, float pts = 12.0f, float dpi = 96.0f) {
	return (em / upem) * pts * dpi;
}

void PutPixel(int x, int y, char val, void* pixels, int stride) 
{
	 uint8_t* pixel = (uint8_t*)pixels + y*stride + x;
	 *pixel = val;
} 

#include <glm/glm.hpp>

template <typename T>
bool InRange(T x, T x0, T x1) {
	std::array<T, 2> range = { x0, x1 };
	std::sort(range.begin(), range.end());
	
	return x >= range[0] && x <= range[1];
}

RenderResult TTFParser::Rasterize(int cp)
{
	std::cout << "Codepoint: " << cp << std::endl;
	auto outline = parse_outline(cp);

	// Allocate bitmap.
	size_t bmpWidth = std::ceil(toPixels(outline.width, m_UnitsPerEM));
	size_t bmpHeight = std::ceil(toPixels(outline.height, m_UnitsPerEM));
	char* pixels = (char*)malloc(bmpWidth * bmpHeight);
	memset(pixels, 0x00, bmpWidth * bmpHeight);
	std::cout << "Bitmap: " << bmpWidth << "x" << bmpHeight << " pixels" << std::endl;

	// Rasterize.
	for (float scl = 0.0f; scl <= bmpHeight; scl++)
	{
		std::vector<float> ixs;

		for (const auto& edge : outline.edges) {
			edge.getIntersections(scl, ixs);
		}

		std::sort(ixs.begin(), ixs.end());

		for (size_t k = 0; k < ixs.size(); k++) {
			PutPixel(ixs[k], scl, 0xff, pixels, bmpWidth);
		}
	}

    return { pixels, bmpWidth, bmpHeight};
}

void Line::getIntersections(float scanline, std::vector<float>& ixs) const
{
	switch (m_Type) {
	case Type::Horizontal:
	{
		if (scanline == m_P0.yc) {
			ixs.push_back(m_P0.xc);
			ixs.push_back(m_P1.xc);
		}
	}
	break;

	case Type::Vertical:
	{
		if (InRange<int16_t>(scanline, m_P0.yc, m_P1.yc)) {
			ixs.push_back(m_P0.xc);
		}
	}
	break;

	case Type::Slanted:
	{
		float ix = (scanline - m_Intercept) / m_Gradient;
		if (InRange<int16_t>(ix, m_P0.xc, m_P1.xc)) {
			ixs.push_back(ix);
		}
	}
	break;
	}
}

void Line::Categorize(float upem) {
	// Convert coordinates from em-space -> bitmap-space.
	Point p0 = m_P0, p1 = m_P1;
	m_P0.xc = toPixels(m_P0.xc, upem);
	m_P0.yc = toPixels(m_P0.yc, upem);
	m_P1.xc = toPixels(m_P1.xc, upem);
	m_P1.yc = toPixels(m_P1.yc, upem);

	// Categorise line type (done in em-space to avoid fp precision errors).
	if (p0.xc == p1.xc) {
		m_Type = Type::Vertical;
	}
	else if (p0.yc == p1.yc) {
		m_Type = Type::Horizontal;
	}
	else {
		m_Type = Type::Slanted;

		float x0 = static_cast<float>(m_P0.xc);
		float x1 = static_cast<float>(m_P1.xc);
		float y0 = static_cast<float>(m_P0.yc);
		float y1 = static_cast<float>(m_P1.yc);

		m_Gradient = (y1 - y0) / (x1 - x0);
		m_Intercept = y0 - m_Gradient * x0;
	}
}