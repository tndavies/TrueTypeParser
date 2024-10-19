#pragma once

// Depends:
#include <unordered_map>
#include <string>

#include "stream.h"
#include "outline.h"
#include "encodings.h"

#define XSelect [](Contour& c) -> auto & { return c.xs; }
#define YSelect [](Contour& c) -> auto & { return c.ys; }
#define RepeatBit 3
#define XShort 1
#define YShort 2
#define XDual 4
#define YDual 5

#define argWidthMask 	 (1 << 0)
#define argTypeMask 	 (1 << 1)
#define singleScaleMask  (1 << 3)
#define doubleScaleMask  (1 << 6)
#define transformMask 	 (1 << 7)
#define nextCompMask     (1 << 5)
//

struct Parser {
    Parser(const void* pFontData);

    void RegisterTables();

    void ChooseEncoder();

    Stream GetTable(const std::string& pTag) const;

    void LoadGlobalMetrics();

    GlyphDescription LoadGlyph(const GlyphID pGlyphID);

    const GlyphMesh LoadCompoundGlyph(Stream pData); // @todo: private
    const GlyphMesh LoadSimpleGlyph(Stream glyf, const int16_t pContourCount); // @todo: private

    //

    std::unordered_map<std::string, uint32_t> tables;
    const BasicUnicodeEncoder* encoder;
    const uint8_t* fontData;
    uint16_t upem;
};