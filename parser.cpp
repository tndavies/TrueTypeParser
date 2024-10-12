#include <assert.h>
#include "parser.h"
#include "utils.h"
#include <string>
#include <stack>
#include <algorithm>
#include <iostream>
#include <functional>

#define OnCurve(x) (x & 1)

struct contour {
	std::vector<uint8_t> flags;
	std::vector<int16_t> xs;
	std::vector<int16_t> ys;

	auto total_points() const { return flags.size(); }
};

#define XSelect [](contour &c) -> auto& { return c.xs; }
#define YSelect [](contour &c) -> auto& { return c.ys; }
#define RepeatBit 3
#define XShort	1
#define YShort	2
#define XDual	4
#define YDual	5

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

void unpack_flags(
	uint8_t*& dataStream,
	std::vector<contour>& contours,
	std::vector<uint16_t>& endPoints
)
{
	const auto kRepeatMask = (1 << RepeatBit); // @todo: make constexpr?

	size_t prev_total_pts = 0;
	for(const auto &idx : endPoints) {
		contours.emplace_back();
		auto &flags = contours.back().flags;

		const auto contour_pt_count = (idx + 1) - prev_total_pts;

		while (flags.size() < contour_pt_count) {
			const auto flag = *dataStream++;
			const auto repeat_count = (flag & kRepeatMask) ? *dataStream++ : 0;
			flags.insert(flags.end(), 1 + repeat_count, flag);
		}

		prev_total_pts += contour_pt_count;
	}
}

void 
unpack_axis(
	uint8_t*& dataStream,
	std::vector<contour>& contours,
	const std::function<std::vector<int16_t>& (contour&)>& selectBuffer,
	const size_t kShortBit,
	const size_t kDualBit)
{
	// todo: use constexpr to make bitmask at compile time?
	const auto kShortMask = (1 << kShortBit);
	const auto kDualMask = (1 << kDualBit);

	size_t ref_val = 0; 
	for (auto &contour : contours) {
		auto& buff = selectBuffer(contour);
		buff.emplace_back(ref_val);

		for (const auto& flg : contour.flags) {
			const bool dual_bit = flg & kDualMask;

			if (flg & kShortMask) {
				const auto delta = *dataStream++;
				const auto val = buff.back() + delta * (dual_bit ? 1 : -1);
				buff.push_back(val);
			}
			else {
				if (dual_bit) {
					buff.push_back(buff.back());
				}
				else {
					const auto delta = get_s16(dataStream);
					const auto val = buff.back() + delta;
					buff.push_back(val);
					dataStream += 2;
				}
			}
		}

		buff.erase(buff.begin()); // remove the reference component.
		ref_val = buff.back();
	}
}

edgeTable*
generate_meshes(std::vector<contour>& contours)
{
	auto* et = new edgeTable();

	for (auto& c : contours) {
		auto& flags = c.flags;
		auto& xs = c.xs;
		auto& ys = c.ys;

		assert(OnCurve(flags[0])); // Assume the 1st contour point is on-curve.

		// Create any inferred points.
		for (size_t i = 0; i < flags.size() - 1; ++i) {
			if (!OnCurve(flags[i]) && !OnCurve(flags[i + 1])) {
				float x = (xs.at(i) + xs.at(i + 1)) / 2.0f;
				float y = (ys.at(i) + ys.at(i + 1)) / 2.0f;

				flags.insert(flags.begin() + i + 1, 0xff);
				xs.insert(xs.begin() + i + 1, x);
				ys.insert(ys.begin() + i + 1, y);
			}
		}

		const auto& num_pts = flags.size();
		std::vector<size_t> buff;

		//
		for (size_t k = 0; k <= num_pts; ++k) {
			const auto idx = k % num_pts; // point index into the data buffers.
			buff.push_back(idx);

			switch (buff.size()) {
			case 2: {
				const auto& slt0 = buff[0];
				const auto& slt1 = buff[1];

				if (OnCurve(flags[slt0]) && OnCurve(flags[slt1])) {
					fPoint p0(xs.at(slt0), ys.at(slt0));
					fPoint p1(xs.at(slt1), ys.at(slt1));

					if (p0.y != p1.y) { // skip horizontal edges
						et->add_edge(p0, p1);
					}

					buff[0] = buff[1];
					buff.pop_back();
				}
			} break;

			case 3: {
				const auto& slt0 = buff[0];
				const auto& slt1 = buff[1];
				const auto& slt2 = buff[2];

				if (OnCurve(flags[slt0]) && !OnCurve(flags[slt1]) && OnCurve(flags[slt2])) {
					fPoint p0(xs.at(slt0), ys.at(slt0));
					fPoint p1(xs.at(slt1), ys.at(slt1));
					fPoint p2(xs.at(slt2), ys.at(slt2));

					et->add_bezier(p0, p1, p2);

					buff[0] = buff[2];
					buff.pop_back();
					buff.pop_back();
				}
			} break;
			}
		}
	}

	return et;
}

OutlineData 
ttf_parser::load_outline(size_t c)
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
	uint16_t* contour_data = (uint16_t*)(outline_data + 10);

	std::vector<uint16_t> contour_ends(contour_count);
	for (size_t k = 0; k < contour_count; k++) {
		contour_ends[k] = get_u16(contour_data + k);
	}

	size_t point_count = contour_ends.back() + 1;
	uint8_t* hprog = (uint8_t*)((uint16_t*)contour_data + contour_count);
	uint8_t* stream = hprog + 2 + get_u16(hprog);

	//

	std::vector<contour> contours;
	unpack_flags(stream, contours, contour_ends);
	unpack_axis(stream, contours, XSelect, XShort, XDual);
	unpack_axis(stream, contours, YSelect, YShort, YDual);
	const auto* et = generate_meshes(contours);

	//

	return OutlineData(et, xMin, xMax, yMin, yMax);
}