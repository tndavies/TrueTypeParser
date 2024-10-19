#include <assert.h>
#include <fstream>

#include "libfnt.h"

library::library(const std::string& pFontPath)
{
	// Load font file into memory.
	std::ifstream file_handle(pFontPath, std::ios::binary);
	assert(file_handle.is_open());

	file_handle.seekg(0, file_handle.end);
	const auto file_size = file_handle.tellg();
	file_handle.seekg(0, file_handle.beg);

	void* font = std::malloc(file_size);
	file_handle.read((char*)font, file_size);
	file_handle.close();

	assert(font);

	//

	parser = new Parser(font);
}

GlyphDescription library::LoadGlyph(const size_t pCharCode)
{
	const GlyphID glyphID = parser->encoder->GetGlyphID(pCharCode);
	return parser->LoadGlyph(glyphID);
}

const RasterTarget* library::RenderGlyph(const GlyphDescription& pGlyphDesc, const float pPointSize)
{
	return RenderOutline(pGlyphDesc, parser->upem);
}
