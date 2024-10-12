#include <functional>
#include <algorithm>
#include <iostream>
#include <assert.h>
#include <string>
#include <stack>

#include "parser.h"
#include "utils.h"
#include "stream.h"

struct contour
{
	std::vector<uint8_t> flags;
	std::vector<int16_t> xs;
	std::vector<int16_t> ys;

	auto total_points() const { return flags.size(); }
};

#define OnCurve(x) (x & 1)
#define XSelect [](contour &c) -> auto & { return c.xs; }
#define YSelect [](contour &c) -> auto & { return c.ys; }
#define RepeatBit 3
#define XShort 1
#define YShort 2
#define XDual 4
#define YDual 5

using namespace libfnt;

// ================================================================= //

ttf_parser::ttf_parser(char* fontMemory)
	: m_font(fontMemory), m_mapper(nullptr), m_upem(0) {}

void ttf_parser::init()
{
	register_tables();
	select_charmap();
	get_metrics();
}

void ttf_parser::register_tables()
{
	Stream ttfFile(m_font);
	ttfFile.Skip(4); // skip to table-count
	uint16_t tableCount = ttfFile.GetField<uint16_t>();
	ttfFile.Skip(6); // skip to 1st table-descriptor

	for (size_t i = 0; i < tableCount; i++) {
		std::string tag;
		for (size_t k = 0; k < 4; k++) {
			uint8_t letter = ttfFile.GetField<uint8_t>();
			tag += std::tolower(letter);
		}

		ttfFile.Skip(4); // skip checksum
		uint32_t tableOffset = ttfFile.GetField<uint32_t>();
		m_tables.insert(std::make_pair(tag, tableOffset));

		ttfFile.Skip(4); // skip table length
	}
}

void ttf_parser::select_charmap()
{
	Stream cmap = GetTable("cmap");
	const uint8_t* cmapTop = (const uint8_t*)cmap.get();

	cmap.SkipField<uint16_t>(); // skip version number
	uint16_t encodingCount = cmap.GetField<uint16_t>();

	for (size_t k = 0; k < encodingCount; k++) {
		cmap.Skip(4); // skip to encoding table offset
		uint32_t encodingTableOffset = cmap.GetField<uint32_t>();

		Stream encodingTable(cmapTop + encodingTableOffset);
		uint16_t encodingFormat = encodingTable.GetField<uint16_t>();

		if (!encodingFormat) {
			const uint8_t* glyphLookupTable = (const uint8_t*)encodingTable.get() + 4;
			m_mapper = new Format0_Mapper(glyphLookupTable);
			break;
		}
	}

	assert(m_mapper);
}

Stream
ttf_parser::GetTable(const std::string& tag) const
{
	return Stream(m_font + m_tables.at(tag));
}

void ttf_parser::get_metrics()
{
	Stream head = GetTable("head");
	head.Skip(18);
	m_upem = head.GetField<uint16_t>();
}

void unpack_flags(
	Stream& dataStream,
	std::vector<contour>& contours,
	std::vector<uint16_t>& endPoints)
{
	const auto kRepeatMask = (1 << RepeatBit); // @todo: make constexpr?

	size_t prev_total_pts = 0;
	for (const auto& idx : endPoints) {
		contours.emplace_back();
		auto& flags = contours.back().flags;

		const auto contour_pt_count = (idx + 1) - prev_total_pts;

		while (flags.size() < contour_pt_count) {
			uint8_t flag = *dataStream;
			auto repeat_count = (flag & kRepeatMask) ? *dataStream : 0;
			flags.insert(flags.end(), 1 + repeat_count, flag);
		}

		prev_total_pts += contour_pt_count;
	}
}

void unpack_axis(
	Stream& dataStream,
	std::vector<contour>& contours,
	const std::function<std::vector<int16_t>& (contour&)>& selectBuffer,
	const size_t kShortBit,
	const size_t kDualBit)
{
	// todo: use constexpr to make bitmask at compile time?
	auto kShortMask = (1 << kShortBit);
	auto kDualMask = (1 << kDualBit);

	size_t ref_val = 0;
	for (auto& contour : contours) {
		auto& buff = selectBuffer(contour);
		buff.emplace_back(ref_val);

		for (const auto& flg : contour.flags) {
			bool dual_bit = flg & kDualMask;

			if (flg & kShortMask) {
				auto delta = *dataStream;
				auto val = buff.back() + delta * (dual_bit ? 1 : -1);
				buff.push_back(val);
			}
			else {
				if (dual_bit) {
					buff.push_back(buff.back());
				}
				else {
					auto delta = dataStream.GetField<int16_t>();
					auto val = buff.back() + delta;
					buff.push_back(val);
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
			case 2:
			{
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
			}
			break;

			case 3:
			{
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
			}
			break;
			}
		}
	}

	return et;
}

OutlineData
ttf_parser::LoadGlyph(size_t c)
{
	auto glyphID = m_mapper->get_glyph_idx(c);

	//

	Stream head = GetTable("head");
	head.Skip(50); // skip to locaFormat field 
	bool locaLongFormat = (bool)head.GetField<int16_t>();

	Stream loca = GetTable("loca");
	size_t bytesPerElement = locaLongFormat ? 4 : 2;
	loca.Skip(bytesPerElement * glyphID); // jump to array element for glyph.
	uint32_t arrayValue = locaLongFormat ? loca.GetField<uint32_t>() : loca.GetField<uint16_t>();
	uint32_t glyphOffset = locaLongFormat ? arrayValue : 2 * arrayValue;

	Stream glyf = GetTable("glyf");
	glyf.Skip(glyphOffset);

	// Process outline data

	auto contourCount = glyf.GetField<int16_t>();

	assert(contourCount > 0); // We don't support compund glyphs yet.

	auto xMin = glyf.GetField<int16_t>();
	auto yMin = glyf.GetField<int16_t>();
	auto xMax = glyf.GetField<int16_t>();
	auto yMax = glyf.GetField<int16_t>();
	BoundingBox bb(xMin, yMin, xMax, yMax);

	std::vector<uint16_t> contourEndPts(contourCount);
	for (size_t k = 0; k < contourCount; ++k) {
		contourEndPts[k] = glyf.GetField<uint16_t>();
	}

	auto instructionCount = glyf.GetField<uint16_t>();
	glyf.Skip(instructionCount);

	std::vector<contour> contours;
	unpack_flags(glyf, contours, contourEndPts);
	unpack_axis(glyf, contours, XSelect, XShort, XDual);
	unpack_axis(glyf, contours, YSelect, YShort, YDual);
	const auto* et = generate_meshes(contours);

	//

	return OutlineData(et, bb);
}