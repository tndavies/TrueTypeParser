#pragma once

// Depends:
#include <unordered_map>
#include <string>

#include "stream.h"
#include "outline.h"
#include "encodings.h"

#define XSelect [](Contour& c) -> auto & { return c.xs; }
#define YSelect [](Contour& c) -> auto & { return c.ys; }
#define OnCurve(x) (x & 1)
#define RepeatBit 3
#define XShort 1
#define YShort 2
#define XDual 4
#define YDual 5

//

struct Parser {
    Parser(const void* pFontData);

    void RegisterTables();

    void ChooseEncoder();

    Stream GetTable(const std::string& pTag) const;

    void LoadGlobalMetrics();

    Outline LoadGlyph(const size_t pCharCode);

    //

    std::unordered_map<std::string, uint32_t> tables;
    const BasicUnicodeEncoder* encoder;
    const uint8_t* fontData;
    uint16_t upem;
};