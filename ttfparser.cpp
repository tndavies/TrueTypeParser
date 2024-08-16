#include "ttfparser.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include "utils.h"
#include <array>
#include <algorithm>
#include <list>

//std::vector<Point> debug_pts = {
//	{400,200, true},
//	{900,600, true},
//	{200,800, true},
//	{400, 200, true}
//};
//Outline_Descriptor outline_desc = { .points = debug_pts, .x_extent = 1000, .y_extent = 1000 };

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

std::vector<Edge> build_edge_table(std::vector<Point> points) {
	std::vector<Edge> edge_table;

	for (size_t k = 1; k < points.size(); k++) {
		const auto& pt1 = points[k];
		const auto& pt0 = points[k - 1];

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

///////////////////////////////////////////////////////////////////////////////////

const Edge* get_shared_edge(size_t kCurrEdge, const Point& sharedVertex, 
	const std::vector<Edge>& edge_table) 
{
	const auto lastIndex = edge_table.size() - 1;
	const Edge* next = &edge_table[(kCurrEdge != lastIndex ? kCurrEdge + 1 : 0)];
	const Edge* prev = &edge_table[(kCurrEdge ? kCurrEdge - 1 : lastIndex)];

	const Edge* sharedEdge = nullptr;

	if (next->p0 == sharedVertex || next->p1 == sharedVertex)
		sharedEdge = next;
	else if (prev->p0 == sharedVertex || prev->p1 == sharedVertex)
		sharedEdge = prev;
	
	assert(sharedEdge);

	return sharedEdge;
}

void TTFParser::find_intersections(size_t kCurrEdge, float scl, 
	std::vector<float>& hits, std::vector<Edge>& edge_table) 
{
	// Get current edge & classify its vertices.
	Edge& edge = edge_table[kCurrEdge];
	
	if (edge.p0.yc == edge.p1.yc) return; // Skip horizontal edges.

	const auto classify = edge.p0.yc > edge.p1.yc;
	const auto ptApex = classify ? edge.p0 : edge.p1;
	const auto ptBase = classify ? edge.p1 : edge.p0;

	// If the scanline doesn't pass through the edge's vertices,
	// then calculate the intersection point and return.
	const auto apexX = toPixels(ptApex.xc, m_UnitsPerEM);
	const auto apexY = toPixels(ptApex.yc, m_UnitsPerEM);
	const auto baseX = toPixels(ptBase.xc, m_UnitsPerEM);
	const auto baseY = toPixels(ptBase.yc, m_UnitsPerEM);
	if (scl > baseY && scl < apexY) {
		if (ptApex.xc != ptBase.xc) { // Slanted edge.
			const float gradient = (apexY - baseY) / (apexX - baseX);
			const float intercept = apexY - gradient * apexX; // Could use ptBase here too, doesn't matter.
			hits.push_back((scl - intercept) / gradient);
		}
		else { // Vertical edge.
			hits.push_back(toPixels(ptApex.xc, m_UnitsPerEM));
		}
		
		return;
	}

	// If the scanline does pass through a vertex of the edge..
	if (scl == baseY || scl == apexY) {
		// Find which of the edge's vertices are intersecting with the scanline, and not, 
		// and also find the free vertex of the edge that shares the vertex that the scanline
		// is going through.
		const auto intersectingVertex = (scl == baseY) ? ptBase : ptApex;
		const auto freeVertex = (scl == baseY) ? ptApex : ptBase;

		auto sharedEdge = get_shared_edge(kCurrEdge, intersectingVertex, edge_table);
		const auto freeVertex_Shared = (sharedEdge->p0 == intersectingVertex) ? sharedEdge->p1 : sharedEdge->p0;
		
		// Find the intersection point of the scanline with the edge.
		const float ix = toPixels(intersectingVertex.xc, m_UnitsPerEM);
		hits.push_back(ix);

		if (sharedEdge->p0.yc != sharedEdge->p1.yc) { // Shared edge isn't horizontal.
			const auto freeY = toPixels(freeVertex.yc, m_UnitsPerEM);
			const auto freeSharedY = toPixels(freeVertex_Shared.yc, m_UnitsPerEM);
			const float theta = (freeY - scl) * (freeSharedY - scl);
			
			if (theta > 0.0f) { // Edges are on same side of scanline.
				hits.push_back(ix);
			}
		}
	}
}

Bitmap TTFParser::RasterizeGlyph(int codepoint) {

	// Extract outline information
	auto outline_desc = ExtractOutline(']');

	// Construct edge table. 
	auto edge_table = build_edge_table(outline_desc.points);

	// Allocate bitmap memory.
	Bitmap bitmap = allocate_bitmap(outline_desc);

	// @TODO: How to handle case at scl=pixels(1724)
	// for codepoint=']'. We get 3 intersections, and
	// the rules don't address this case.

	// Rasterise outline.
	for (float scanline = 0.0f; scanline < bitmap.height; scanline++) 
	{
		std::vector<float> hit_list;
		for (size_t k = 0; k < edge_table.size(); k++) {
			find_intersections(k, scanline, hit_list, edge_table);
		}

		assert(hit_list.size() % 2 == 0);

		std::sort(hit_list.begin(), hit_list.end());
		for (size_t k = 1; k < hit_list.size(); k+=2) {
			fill_span(hit_list[k - 1], hit_list[k], scanline, bitmap);
		}
	}

	return bitmap;
}

//Edge& e = edge_table.at(kCurrEdge);

	//if (e.p0.yc == e.p1.yc) continue; // skip horizontal edge.

	//if (e.is_dirty) {
	//	e.is_dirty = false;
	//	continue;
	//}

	//const auto vertex_max = e.p0.yc > e.p1.yc ? e.p0 : e.p1;
	//const auto vertex_min = e.p0.yc < e.p1.yc ? e.p0 : e.p1;

	//const auto yMin = toPixels(vertex_min.yc, m_UnitsPerEM);
	//const auto yMax = toPixels(vertex_max.yc, m_UnitsPerEM);

	//const auto x0 = toPixels(e.p0.xc, m_UnitsPerEM);
	//const auto y0 = toPixels(e.p0.yc, m_UnitsPerEM);

	//const auto y1 = toPixels(e.p1.yc, m_UnitsPerEM);
	//const auto x1 = toPixels(e.p1.xc, m_UnitsPerEM);

	//if (scanline > yMin && scanline < yMax) {
	//	float ix = x0;

	//	if (e.p0.xc != e.p1.xc) { // slanted line
	//		const float gradient = float(y1 - y0) / (x1 - x0);
	//		const float intercept = y0 - gradient * x0;
	//		ix = (scanline - intercept) / gradient;
	//	}

	//	hit_list.push_back(ix);
	//}
	//else if (scanline == yMin || scanline == yMax) {
	//	// classify the points for this edge.
	//	const Point* ptI, * ptO = nullptr;
	//	if (scanline == yMin) {
	//		ptI = &vertex_min;
	//		ptO = &vertex_max;
	//	}
	//	else {
	//		ptI = &vertex_max;
	//		ptO = &vertex_min;
	//	}

	//	// don't need to handle anything if shared edge
	//	// is horizontal!
	//	size_t j = k + 1;
	//	if (k == 0 && scanline == toPixels(outline_desc.points[0].yc, m_UnitsPerEM)) {
	//		j = edge_table.size() - 1;
	//	}
	//	Edge& se = edge_table.at(j);
	//	if (se.p0.yc == se.p1.yc) {
	//		const auto ix = toPixels(ptI->xc, m_UnitsPerEM);
	//		hit_list.push_back(ix);
	//	}
	//	else {
	//		const auto se_y0 = toPixels(se.p0.yc, m_UnitsPerEM);
	//		const auto pt1_EM = (scanline == se_y0) ? se.p1 : se.p0; // next edge

	//		const auto pt0 = toPixels(ptO->yc, m_UnitsPerEM);
	//		const auto pt1 = toPixels(pt1_EM.yc, m_UnitsPerEM);

	//		const float theta = (pt0 - scanline) * (pt1 - scanline);
	//		const auto ix = toPixels(ptI->xc, m_UnitsPerEM);
	//		if (theta > 0.0f) hit_list.push_back(ix);
	//		hit_list.push_back(ix);
	//		se.is_dirty = true;
	//	}
	//}