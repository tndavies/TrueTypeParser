#pragma once

// Depends:
#include <unordered_map>
#include <string>

#include "stream.h"
#include "outline.h"

#define XSelect [](Contour& c) -> auto & { return c.xs; }
#define YSelect [](Contour& c) -> auto & { return c.ys; }
#define OnCurve(x) (x & 1)
#define RepeatBit 3
#define XShort 1
#define YShort 2
#define XDual 4
#define YDual 5

//

class Format0_Mapper {
    public:
    Format0_Mapper(const uint8_t* gids)
        : m_GlyphIDs(gids) {}

    int get_glyph_idx(size_t c) {
        assert(c <= 255);
        return m_GlyphIDs[c];
    }

    private:
    const uint8_t* m_GlyphIDs;
};

struct Parser {
    Parser(const void* pFontData);

    void RegisterTables();

    void SelectEncoding();

    Stream GetTable(const std::string& pTag) const;

    void LoadGlobalMetrics();

    Outline LoadGlyph(const size_t pCharCode);

    //

    std::unordered_map<std::string, uint32_t> tables;
    Format0_Mapper* mapper;
    const uint8_t* fontData;
    uint16_t upem;
};