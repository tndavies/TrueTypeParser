#include "ttfparser.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include "utils.h"

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

	return {points, width, height, xMin, yMin};
}

float TTFParser::toPixels(int16_t em, float pts, float dpi) {
	return (em / (float)m_UnitsPerEM) * pts * dpi;
}

void PutPixel(int x, int y, char val, void* pixels, int stride) 
{
	 uint8_t* pixel = (uint8_t*)pixels + y*stride + x;
	 *pixel = val;
} 

struct Edge {
	virtual bool intersects(int hline) const = 0;
	virtual std::string repr() const = 0;
};

class Line : public Edge {
public:
	Line(Point p0, Point p1)
		: m_Start(p0), m_End(p1) {}

	bool intersects(int hline) const override 
	{
		return false;
	}

	std::string repr() const override 
	{
		return "line";
	}

private:
	Point m_Start, m_End;
};

class Bezier : public Edge {
public:
	Bezier(Point p0, Point p1, Point p2)
		: m_Start(p0), m_Control(p1), m_End(p2) {}

	bool intersects(int hline) const override 
	{
		return false;
	}

	std::string repr() const override 
	{
		return "bezier";
	}

private:
	Point m_Start, m_Control, m_End;
};


RenderResult TTFParser::Rasterize(int cp)
{
	std::cout << "Codepoint: " << cp << std::endl;
	auto outline = parse_outline(cp);

	// allocate bitmap memory.
	size_t bmpWidth = std::ceil(toPixels(outline.width));
	size_t bmpHeight = std::ceil(toPixels(outline.height));
	char* pixels = (char*)malloc(bmpWidth * bmpHeight);
	memset(pixels, 0x00, bmpWidth*bmpHeight);
	std::cout << "Bitmap: " << bmpWidth << "x" << bmpHeight << " pixels" << std::endl;

	// Build edge list.
	std::vector<Edge*> edge_list;

	std::cout << "\nPoint Data: \n" << std::endl;
	uint8_t bits = 0, j = 0;
	for(size_t k = 0; k < outline.points.size(); k++) {
		auto& pt = outline.points[k];
		std::cout << (pt.on_curve ? "on" : "off") << std::endl;

		pt.xc = toPixels(pt.xc - outline.xMin);
		pt.yc = toPixels(pt.yc - outline.yMin);
		PutPixel(pt.xc, pt.yc, 0xff, pixels, bmpWidth);

		bits |= pt.on_curve ? (1 << j++) : 0;
		switch(bits) {
			case 0b011: // on, on
			{
				Line* edge = new Line(outline.points[k-1], pt);
				edge_list.push_back(edge);

				bits = 0;
				j = 0;
			}
			break;

			case 0b101: // on, off, on
			{
				Bezier* edge = new Bezier(outline.points[k-2], outline.points[k-1], pt);
				edge_list.push_back(edge);

				bits = 0;
				j = 0;
			}
			break;

			case 0b1001: // on, off, off, on --> // on, off, on, off, on
			{
				float ix = 0.5f*(outline.points[k-2].xc + outline.points[k-1].xc); 
				float iy = 0.5f*(outline.points[k-2].yc + outline.points[k-1].yc); 
				Point ipt(ix, iy, NULL);

				Bezier* edge1 = new Bezier(outline.points[k-3], outline.points[k-2], ipt); 
				Bezier* edge2 = new Bezier(ipt, outline.points[k-1], pt); 
			
				edge_list.push_back(edge1);
				edge_list.push_back(edge2);
				
				bits = 0;
				j = 0;
			}
			break;
		}
	}

	// Scanline rasterizer.
	const int scl = 20.0f;
	for(int x = 0; x <= bmpWidth; x++) PutPixel(x, scl, 0xff, pixels, bmpWidth); // debug line
	
	// debug output.
	std::cout << "Edges:\n";
	for(const auto& edge : edge_list) {
		std::cout << edge->repr() << std::endl;
	}

    return {pixels, bmpWidth, bmpHeight};
}
