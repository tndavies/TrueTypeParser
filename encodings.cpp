#include <assert.h>
#include <vector>
#include "encodings.h"

//

BasicUnicodeEncoder::BasicUnicodeEncoder(const void *pEncodingTable)
    : stream_(pEncodingTable)
{
    const u16 encodingFormat = stream_.GetField<u16>();
    assert(encodingFormat == 4);

    stream_.Skip(4); // Skip to segment-count.

    const u16 segmentCount = stream_.GetField<u16>() / 2;

    stream_.Skip(6); // Skip endCode array.

    // Locate the arrays that define the segments' properties.
    Stream &endCodes = stream_;
    Stream startCodes = (u16 *)endCodes.get() + segmentCount + 1;
    Stream idDeltas = (u16 *)startCodes.get() + segmentCount;
    const u16 *idRangeOffsets = (const u16 *)idDeltas.get() + segmentCount;

    for (size_t k = 0; k < segmentCount; ++k) {
        segments_.emplace_back(startCodes.GetField<u16>(), endCodes.GetField<u16>(),
            idDeltas.GetField<u16>(), idRangeOffsets);

        ++idRangeOffsets;
    }

    for (size_t k = 0; k < segmentCount; ++k) {
        glyphIndexArray_.emplace_back(stream_.GetField<u16>());
    }
}

GlyphID
BasicUnicodeEncoder::GetGlyphID(const CharCode pCharCode) const
{
    // Determine which segment the char-code lies within.
    const Segment *segment = nullptr;
    for (size_t k = 0; k < segments_.size(); ++k) {
        const Segment *seg = &segments_[k];

        if (pCharCode >= seg->startCharCode && pCharCode <= seg->endCharCode) {
            segment = seg;
            break;
        }
    }

    assert(segment); // We don't have a mapping for this char-code.

    //

    GlyphID glyphID = 0; // default to the null glyph.

    const u16 idRangeOffset = Stream::GetField<u16>(segment->idRangeOffsetPtr);
    if (idRangeOffset) {
        const u8 *glyphIdPtr = (const u8 *)segment->idRangeOffsetPtr + (idRangeOffset + 2 * (pCharCode - segment->startCharCode));
        glyphID = Stream::GetField<u16>(glyphIdPtr);
    }
    else {
        glyphID = (segment->idDelta + pCharCode) & 0xffff;
    }

    return glyphID;
}
