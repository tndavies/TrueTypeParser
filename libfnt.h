#pragma once

#include <string>

#include "outline.h" 
#include "parser.h" 
#include "raster.h" 

//

struct library {
    library(const std::string& pFontFilePath);

    GlyphDescription LoadGlyph(const size_t pCharCode);

    const RasterTarget* RenderGlyph(const GlyphDescription& pGlyphDesc, const float pPointSize);

    //

    Parser* parser;
};