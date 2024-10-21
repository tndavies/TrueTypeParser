#include <functional>
#include <algorithm>
#include <assert.h>

#include "stream.h"
#include "parser.h"
#include "raster.h"

//

Parser::Parser(const void *pFontData)
	: fontData((const uint8_t *)pFontData), encoder(nullptr), upem(0)
{
	RegisterTables();
	ChooseEncoder();
	LoadGlobalMetrics();
}

void Parser::RegisterTables()
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

void Parser::ChooseEncoder()
{
	Stream cmap = GetTable("cmap");
	const uint8_t *cmapTop = (const uint8_t *)cmap.get();

	cmap.SkipField<uint16_t>(); // skip version number
	const uint16_t encodingCount = cmap.GetField<uint16_t>();

	for (size_t k = 0; k < encodingCount; k++) {
		cmap.Skip(4); // skip to encoding table offset
		const uint32_t encodingTableOffset = cmap.GetField<uint32_t>();

		Stream encodingTable(cmapTop + encodingTableOffset);
		const void *encodingTableTop = encodingTable.get();

		const uint16_t encodingFormat = encodingTable.GetField<uint16_t>();
		if (encodingFormat == 4) {
			encoder = new BasicUnicodeEncoder(encodingTableTop);
			break;
		}
	}

	assert(encoder);
}

Stream
Parser::GetTable(const std::string &pTag) const
{
	return Stream(fontData + tables.at(pTag));
}

void Parser::LoadGlobalMetrics()
{
	Stream head = GetTable("head");
	head.Skip(18);
	upem = head.GetField<uint16_t>();
}

void UnpackFlags(
	Stream &dataStream,
	std::vector<Contour> &contours,
	const std::vector<uint16_t> &endPoints)
{
	constexpr auto kRepeatMask = (1 << RepeatBit);

	size_t prevTotalPts = 0;
	for (const auto &idx : endPoints) {
		contours.emplace_back();
		auto &flags = contours.back().flags;

		const auto contourPtCount = (idx + 1) - prevTotalPts;

		while (flags.size() < contourPtCount) {
			const uint8_t flag = *dataStream;
			const auto repeatCount = (flag & kRepeatMask) ? *dataStream : 0;
			flags.insert(flags.end(), 1 + repeatCount, flag);
		}

		prevTotalPts += contourPtCount;
	}
}

void UnpackAxis(
	Stream &dataStream,
	std::vector<Contour> &contours,
	const std::function<std::vector<int16_t> &(Contour &)> &selectBuffer,
	const size_t kShortBit,
	const size_t kDualBit)
{
	const auto kShortMask = (1 << kShortBit);
	const auto kDualMask = (1 << kDualBit);

	size_t refVal = 0;
	for (auto &c : contours) {
		auto &buff = selectBuffer(c);
		buff.emplace_back(refVal);

		for (const auto &flg : c.flags) {
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

float
InterpretF2DOT14(const u16 bits)
{
	// The F2DOT14 datatype represents a signed real value
	// within 16-bits. The first 14-bits store the
	// fractional value, and the top 2-bits store the
	// integer value, using twos complement representation.
	// See: https://learn.microsoft.com/en-us/typography/opentype/spec/otff

	s8 integerVal = (bits & 0xC000) >> 14; // shift integer-bits to bottom.
	if (bits & 0x8000) { // if msb is set, then value is -ve.
		// minus one, and negate the integer-bits, yielding the +ve
		// version of the value.
		constexpr u8 integerBitsMask = 1 << 15;
		const u8 absVal = ~(integerVal - 1) & integerBitsMask;
		integerVal = -absVal; // store back as signed value.
	}

	// Compute the fractional value from the fraction-bits, using the
	// formula (fraction-bits integer value) / 2^14.
	const float fractionalVal = (bits & 0x3FFF) / (float)16384;

	//

	return integerVal + fractionalVal;
}

const GlyphMesh
Parser::LoadSimpleGlyph(Stream glyf, const int16_t pContourCount)
{
	std::vector<uint16_t> contourEndPts(pContourCount);
	for (size_t k = 0; k < pContourCount; ++k) {
		contourEndPts[k] = glyf.GetField<uint16_t>();
	}

	const uint16_t instructionCount = glyf.GetField<uint16_t>();
	glyf.Skip(instructionCount);

	std::vector<Contour> contours;
	UnpackFlags(glyf, contours, contourEndPts);
	UnpackAxis(glyf, contours, XSelect, XShort, XDual);
	UnpackAxis(glyf, contours, YSelect, YShort, YDual);

	return GlyphMesh(contours); // @todo: avoid copy of contour data into GlyphMesh struct?
}

const GlyphMesh
Parser::LoadCompoundGlyph(Stream pData)
{
	// @todo: "In a variable font, the offset vector can be modified by deltas in the 'gvar' table; 
	// see Point numbers and processing for composite glyphs in the 'gvar' chapter for details."

	GlyphMesh compoundMesh;

	bool hasNextComponent = true;
	while (hasNextComponent) {
		const u16 flags = pData.GetField<u16>();
		const u16 componentGlyphID = pData.GetField<u16>();
		GlyphDescription subGlyph = LoadGlyph(componentGlyphID); // @todo: use maxp table to avoid stack recursion. 

		// Load arguments 1 and 2.
		const bool isWide = flags & argWidthMask;
		const bool isSigned = flags & argTypeMask;

		// Load the transform components.
		float a = 1.0f; // a-d (transform matrix).
		float b = 0.0f;
		float c = 0.0f;
		float d = 1.0f;
		float e = 0.0f; // e-f (translation)
		float f = 0.0f;

		// offsets ...
		if (isWide && isSigned) {
			e = pData.GetField<s16>();	// x-delta
			f = pData.GetField<s16>();	// y-delta
		}
		else if (!isWide && isSigned) {
			e = pData.GetField<s8>();	// x-delta
			f = pData.GetField<s8>();	// y-delta
		}
		else { // point-aligments.
			assert(0);
		}

		if (flags & singleScaleMask) {
			const float scale = InterpretF2DOT14(pData.GetField<u16>());
			a = scale;
			d = scale;
		}
		else if (flags & doubleScaleMask) {
			a = InterpretF2DOT14(pData.GetField<u16>());
			d = InterpretF2DOT14(pData.GetField<u16>());
		}
		else if (flags & transformMask) {
			a = InterpretF2DOT14(pData.GetField<u16>());
			b = InterpretF2DOT14(pData.GetField<u16>());
			c = InterpretF2DOT14(pData.GetField<u16>());
			d = InterpretF2DOT14(pData.GetField<u16>());
		}

		// if instructed, scale the subglyph's offsets by the transform.
		float m = 1.0f, n = 1.0f;
		if (!(flags & UNSCALED_COMPONENT_OFFSET)) {
			m = std::max(std::abs(a), std::abs(b));
			n = std::max(std::abs(c), std::abs(d));

			constexpr float threshold = 33 / (float)65536;
			if ((std::abs(std::abs(a) - std::abs(c)) <= threshold)) {
				m *= 2;
			}
			if ((std::abs(std::abs(b) - std::abs(d)) <= threshold)) {
				n *= 2;
			}
		}

		// Transform the child's control points. 
		for (auto &con : subGlyph.mesh.contours) {
			for (size_t k = 0; k < con.getTotalPtCount(); ++k) {
				s16 x = con.xs[k], y = con.ys[k];
				con.xs[k] = (s16)std::roundf(a*x + c*y + m*e);
				con.ys[k] = (s16)std::roundf(b*x + d*y + n*f);
			}
		}

		// 
		compoundMesh.AddMesh(subGlyph.mesh);

		//
		hasNextComponent = flags & nextCompMask;
	}

	return compoundMesh;
}

GlyphDescription
Parser::LoadGlyph(const GlyphID pGlyphID)
{
	Stream head = GetTable("head");
	head.Skip(50); // skip to locaFormat field
	const bool locaLongFormat = (bool)head.GetField<int16_t>();

	Stream loca = GetTable("loca");
	const size_t bytesPerElement = locaLongFormat ? 4 : 2;
	loca.Skip(bytesPerElement * pGlyphID); // jump to array element for glyph.
	uint32_t glyphOffset = locaLongFormat ? loca.GetField<uint32_t>() : loca.GetField<uint16_t>();
	if (!locaLongFormat) {
		glyphOffset *= 2;
	}

	Stream glyf = GetTable("glyf");
	glyf.Skip(glyphOffset);

	//

	const int16_t contourCount = glyf.GetField<int16_t>();

	const int16_t xMin = glyf.GetField<int16_t>();
	const int16_t yMin = glyf.GetField<int16_t>();
	const int16_t xMax = glyf.GetField<int16_t>();
	const int16_t yMax = glyf.GetField<int16_t>();
	BoundingBox bb(xMin, yMin, xMax, yMax);

	const GlyphMesh mesh = (contourCount < 0) ? LoadCompoundGlyph(glyf)
		: LoadSimpleGlyph(glyf, contourCount);

	//

	return GlyphDescription(mesh, bb);
}