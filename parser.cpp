#include <functional>
#include <algorithm>
#include <assert.h>

#include "stream.h"
#include "parser.h"
#include "raster.h"

// 

Parser::Parser(const void* pFontData)
	: fontData((const uint8_t*)pFontData), mapper(nullptr), upem(0)
{
	RegisterTables();
	SelectEncoding();
	LoadGlobalMetrics();
}


void
Parser::RegisterTables()
{
	Stream ttfFile(fontData);
	ttfFile.Skip(4); // skip to table-count
	uint16_t tableCount = ttfFile.GetField<uint16_t>();
	ttfFile.Skip(6); // skip to 1st table-descriptor

	for (size_t i = 0; i < tableCount; i++) {
		std::string tag;
		for (size_t k = 0; k < 4; k++) {
			const uint8_t letter = ttfFile.GetField<uint8_t>();
			tag += std::tolower(letter);
		}

		ttfFile.Skip(4); // skip checksum
		const uint32_t tableOffset = ttfFile.GetField<uint32_t>();
		tables.insert(std::make_pair(tag, tableOffset));

		ttfFile.Skip(4); // skip table length
	}
}

void
Parser::SelectEncoding()
{
	Stream cmap = GetTable("cmap");
	const uint8_t* cmapTop = (const uint8_t*)cmap.get();

	cmap.SkipField<uint16_t>(); // skip version number
	const uint16_t encodingCount = cmap.GetField<uint16_t>();

	for (size_t k = 0; k < encodingCount; k++) {
		cmap.Skip(4); // skip to encoding table offset
		const uint32_t encodingTableOffset = cmap.GetField<uint32_t>();

		Stream encodingTable(cmapTop + encodingTableOffset);
		const uint16_t encodingFormat = encodingTable.GetField<uint16_t>();

		if (!encodingFormat) {
			const uint8_t* glyphLookupTable = (const uint8_t*)encodingTable.get() + 4;
			mapper = new Format0_Mapper(glyphLookupTable);
			break;
		}
	}

	assert(mapper);
}

Stream
Parser::GetTable(const std::string& pTag) const
{
	return Stream(fontData + tables.at(pTag));
}

void
Parser::LoadGlobalMetrics()
{
	Stream head = GetTable("head");
	head.Skip(18);
	upem = head.GetField<uint16_t>();
}

void
UnpackFlags(
	Stream& dataStream,
	std::vector<Contour>& contours,
	const std::vector<uint16_t>& endPoints)
{
	constexpr auto kRepeatMask = (1 << RepeatBit);

	size_t prevTotalPts = 0;
	for (const auto& idx : endPoints) {
		contours.emplace_back();
		auto& flags = contours.back().flags;

		const auto contourPtCount = (idx + 1) - prevTotalPts;

		while (flags.size() < contourPtCount) {
			const uint8_t flag = *dataStream;
			const auto repeatCount = (flag & kRepeatMask) ? *dataStream : 0;
			flags.insert(flags.end(), 1 + repeatCount, flag);
		}

		prevTotalPts += contourPtCount;
	}
}

void
UnpackAxis(
	Stream& dataStream,
	std::vector<Contour>& contours,
	const std::function<std::vector<int16_t>& (Contour&)>& selectBuffer,
	const size_t kShortBit,
	const size_t kDualBit)
{
	const auto kShortMask = (1 << kShortBit);
	const auto kDualMask = (1 << kDualBit);

	size_t refVal = 0;
	for (auto& c : contours) {
		auto& buff = selectBuffer(c);
		buff.emplace_back(refVal);

		for (const auto& flg : c.flags) {
			const bool dual_bit = flg & kDualMask;

			if (flg & kShortMask) {
				const auto delta = *dataStream;
				const auto val = buff.back() + delta * (dual_bit ? 1 : -1);
				buff.push_back(val);
			}
			else {
				if (dual_bit) {
					buff.push_back(buff.back());
				}
				else {
					const auto delta = dataStream.GetField<int16_t>();
					const auto val = buff.back() + delta;
					buff.push_back(val);
				}
			}
		}

		buff.erase(buff.begin()); // remove the reference component.
		refVal = buff.back();
	}
}

EdgeTable*
GenerateMeshes(std::vector<Contour>& contours)
{
	EdgeTable* et = new EdgeTable();

	for (auto& c : contours) {
		auto& flags = c.flags;
		auto& xs = c.xs;
		auto& ys = c.ys;

		assert(OnCurve(flags[0])); // Assume the 1st contour point is on-curve.

		// Create any inferred points.
		for (size_t i = 0; i < flags.size() - 1; ++i) {
			if (!OnCurve(flags[i]) && !OnCurve(flags[i + 1])) {
				const float x = (xs.at(i) + xs.at(i + 1)) / 2.0f;
				const float y = (ys.at(i) + ys.at(i + 1)) / 2.0f;

				flags.insert(flags.begin() + i + 1, 0xff);
				xs.insert(xs.begin() + i + 1, x);
				ys.insert(ys.begin() + i + 1, y);
			}
		}

		const auto& ptCount = flags.size();
		std::vector<size_t> buff;

		//
		for (size_t k = 0; k <= ptCount; ++k) {
			const auto idx = k % ptCount; // point index into the data buffers.
			buff.push_back(idx);

			switch (buff.size()) {
				case 2:
				{
					const auto& slt0 = buff[0];
					const auto& slt1 = buff[1];

					if (OnCurve(flags[slt0]) && OnCurve(flags[slt1])) {
						const fPoint p0(xs.at(slt0), ys.at(slt0));
						const fPoint p1(xs.at(slt1), ys.at(slt1));

						if (p0.y != p1.y) { // skip horizontal edges
							et->addEdge(p0, p1);
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
						const fPoint p0(xs.at(slt0), ys.at(slt0));
						const fPoint p1(xs.at(slt1), ys.at(slt1));
						const fPoint p2(xs.at(slt2), ys.at(slt2));

						et->addBezier(p0, p1, p2);

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

Outline
Parser::LoadGlyph(const size_t pCharCode)
{
	auto glyphID = mapper->get_glyph_idx(pCharCode);

	//

	Stream head = GetTable("head");
	head.Skip(50); // skip to locaFormat field 
	const bool locaLongFormat = (bool)head.GetField<int16_t>();

	Stream loca = GetTable("loca");
	const size_t bytesPerElement = locaLongFormat ? 4 : 2;
	loca.Skip(bytesPerElement * glyphID); // jump to array element for glyph.
	uint32_t glyphOffset = locaLongFormat ? loca.GetField<uint32_t>() : loca.GetField<uint16_t>();
	if (!locaLongFormat) glyphOffset *= 2;

	Stream glyf = GetTable("glyf");
	glyf.Skip(glyphOffset);

	// Process outline data

	const int16_t contourCount = glyf.GetField<int16_t>();

	assert(contourCount > 0); // We don't support compund glyphs yet.

	const int16_t xMin = glyf.GetField<int16_t>();
	const int16_t yMin = glyf.GetField<int16_t>();
	const int16_t xMax = glyf.GetField<int16_t>();
	const int16_t yMax = glyf.GetField<int16_t>();
	BoundingBox bb(xMin, yMin, xMax, yMax);

	std::vector<uint16_t> contourEndPts(contourCount);
	for (size_t k = 0; k < contourCount; ++k) {
		contourEndPts[k] = glyf.GetField<uint16_t>();
	}

	const uint16_t instructionCount = glyf.GetField<uint16_t>();
	glyf.Skip(instructionCount);

	std::vector<Contour> contours;
	UnpackFlags(glyf, contours, contourEndPts);
	UnpackAxis(glyf, contours, XSelect, XShort, XDual);
	UnpackAxis(glyf, contours, YSelect, YShort, YDual);
	const EdgeTable* et = GenerateMeshes(contours);

	//

	return Outline(et, bb);
}