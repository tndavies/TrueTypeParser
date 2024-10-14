#pragma  once

#include <vector>
#include "base.h"
#include "stream.h"

using CharCode = u32;
using GlyphID = u32;

class BasicUnicodeEncoder {
    public:

    BasicUnicodeEncoder(const void* pEncodingTable);

    GlyphID GetGlyphID(const CharCode pCharCode) const;

    //

    struct Segment {
        u16 startCharCode;
        u16 endCharCode;
        u16 idDelta;
        const u16* idRangeOffsetPtr;

        Segment(const u16 pStartCharCode, const u16 pEndCharCode, const u16 pIdDelta, const u16* pIdRangeOffset)
            : startCharCode(pStartCharCode), endCharCode(pEndCharCode), idDelta(pIdDelta), idRangeOffsetPtr(pIdRangeOffset)
        {
        }
    };

    Stream stream_;
    std::vector<Segment> segments_;
    std::vector<u16> glyphIndexArray_;
};