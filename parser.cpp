#include <assert.h>
#include "parser.h"
#include "utils.h"
#include <string>
#include <stack>
#include <algorithm>
#include <iostream>

#define OnCurve(x) (x & 1)

using namespace libfnt;

// ================================================================= //

ttf_parser::ttf_parser(char* fontMemory)
	: m_font(fontMemory), m_mapper(nullptr), m_upem(0) {}

void
ttf_parser::init()
{
	is_supported();
	register_tables();
	select_charmap();
	get_metrics();
}

bool
ttf_parser::is_supported()
{
	auto outline_type = get_u32(m_font);
	return (outline_type == 0x00010000 || outline_type == 0x74727565);
}

void
ttf_parser::register_tables()
{
	auto num_tables = get_u16(m_font + 4);

	for (size_t i = 0; i < num_tables; i++) {
		auto metadata = (m_font + 12) + (16 * i);

		std::string tag;
		for (size_t k = 0; k < 4; k++) {
			tag += std::tolower(metadata[k]);
		}

		auto offset = get_u32(metadata + 8);

		m_tables.insert(std::make_pair(tag, offset));
	}
}

void 
ttf_parser::select_charmap()
{
	auto* cmap = get_table("cmap");
	auto num_encodings = get_u16(cmap + 2);

	for (size_t k = 0; k < num_encodings; k++)
	{
		auto* encoding_metatable = cmap + 4 + 8 * k;
		auto* format_table = cmap + get_u32(encoding_metatable + 4);
		auto format = get_u16(format_table);

		switch (format) {
			case 0:
				m_mapper = new Format0_Mapper(format_table + 6);
				break;
		}
	}

	assert(m_mapper);
}

char*
ttf_parser::get_table(const std::string &tag) const
{
	return m_font + m_tables.at(tag);
}

void 
ttf_parser::get_metrics()
{
	auto head = get_table("head");

	m_upem = get_u16(head + 18);
}

template <size_t shortbit_idx, size_t infobit_idx>
std::vector<int16_t>* 
decompress_coordinates(const std::vector<uint8_t> &flags, uint8_t** stream_handle) 
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

void
gen_mesh(
	size_t ptidx0, size_t ptidx1, 
	std::vector<int16_t> *xs, 
	std::vector<int16_t> *ys,
	std::vector<uint8_t> &flags,
	edgeTable *et
) 
{
	std::vector<size_t> buff;
	buff.reserve(3);

	const auto num_pts = (ptidx1 - ptidx0) + 1;

	for(size_t i = 0; i <= num_pts; ++i) {
		const auto pt_idx = ptidx0 + (i % num_pts);
		buff.push_back(pt_idx);

		switch(buff.size()) {
			case 2: {
				const auto &slt0 = buff[0];
				const auto &slt1 = buff[1];

				if(OnCurve(flags[slt0]) && OnCurve(flags[slt1])) {
					fPoint p0 (xs->at(slt0), ys->at(slt0));
					fPoint p1 (xs->at(slt1), ys->at(slt1));

					if(p0.y != p1.y) { // skip horizontal edges
						et->add_edge(p0, p1); 
					}

					buff[0] = buff[1];
					buff.pop_back();
				}
			} break;

			case 3: {
				const auto &slt0 = buff[0];
				const auto &slt1 = buff[1];
				const auto &slt2 = buff[2];

				if(OnCurve(flags[slt0]) && !OnCurve(flags[slt1]) && OnCurve(flags[slt2])) {
					fPoint p0 (xs->at(slt0), ys->at(slt0));
					fPoint p1 (xs->at(slt1), ys->at(slt1));
					fPoint p2 (xs->at(slt2), ys->at(slt2));

					et->add_bezier(p0, p1, p2);

					buff[0] = buff[2];
					buff.pop_back();
					buff.pop_back();
				} 
			} break;
		}

	}

}

OutlineData 
ttf_parser::load_outline(const char_code c)
{
	// Find the location of the outline data.
	const auto glyph_index = m_mapper->get_glyph_idx(c);
	char* outline_data = nullptr;
	auto head = get_table("head");
	auto loca = get_table("loca");
	auto glyf = get_table("glyf");

	auto loca_format = get_s16(head + 50);
	assert(loca_format == 0 || loca_format == 1);

	if (loca_format == 1) {
		auto* offset = (uint32_t*)loca + glyph_index;
		outline_data = glyf + get_u32(offset);
	}
	else {
		auto* offset = (uint16_t*)loca + glyph_index;
		outline_data = glyf + 2 * get_u16(offset);
	}

	// Parse the point data.
	auto contour_count = get_s16(outline_data);
	assert(contour_count > 0); // only support simple outline atm!

	auto xMin = get_s16(outline_data + 2);
	auto yMin = get_s16(outline_data + 4);
	auto xMax = get_s16(outline_data + 6);
	auto yMax = get_s16(outline_data + 8);

	// Note: Unpack flags and decompress coordinate data. 

	// @perf: Avoid doing this effective copy, and
	// just lookup directly from the stored array.
	uint16_t* contour_data = (uint16_t*)(outline_data + 10);
	// uint16_t* contour_ends = new uint16_t[contour_count]; // @todo: free me!
	std::vector<uint16_t> contour_ends(contour_count);
	for (size_t k = 0; k < contour_count; k++) {
		contour_ends[k] = get_u16(contour_data + k);
	}

	size_t point_count = 1 + contour_ends[contour_count - 1];

	uint8_t* hprog = (uint8_t*)((uint16_t*)contour_data + contour_count);
	uint8_t* stream = hprog + 2 + get_u16(hprog);
	std::vector<uint8_t> flags;

	while (flags.size() < point_count) {
		uint8_t flag = *stream++;

		size_t repeat_count = 1;
		if (flag & (1 << 3)) {
			repeat_count += *stream++;
		}

		flags.insert(flags.end(), repeat_count, flag);
	}

	auto xdata = decompress_coordinates<1,4>(flags, &stream);
	auto ydata = decompress_coordinates<2,5>(flags, &stream);

	// Create any inferred points.
	for(size_t i = 0; i < flags.size() - 1; ++i) {
		if(!OnCurve(flags[i]) && !OnCurve(flags[i + 1])) {
			float x = (xdata->at(i) + xdata->at(i + 1)) / 2.0f;
			float y = (ydata->at(i) + ydata->at(i + 1)) / 2.0f;

			flags.insert(flags.begin() + i + 1, 0xff);
			xdata->insert(xdata->begin() + i + 1, x);
			ydata->insert(ydata->begin() + i + 1, y);
		}
	}

	// ContourCount: 2
	// ContourEnds[]: 5, 11
	// Points: (0,1,2,3,4,5), (6,7,8,9,10,11)
	
	//
	edgeTable *et = new edgeTable(); // glyph_mesh
	size_t idx0 = 0;

	for(size_t i = 0; i < contour_ends.size(); ++i) {
		const auto &idx1 = contour_ends[i];
		gen_mesh(idx0, idx1, xdata, ydata, flags, et);
		idx0 = idx1 + 1;
	}

	//

	return OutlineData(et, xMin, xMax, yMin, yMax);
}