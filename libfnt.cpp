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

Outline library::LoadGlyph(const size_t pCharCode)
{
	return parser->LoadGlyph(pCharCode);
}

const RasterTarget* library::RenderGlyph(const Outline& pOutline, const float pPointSize)
{
	return RenderOutline(pOutline, parser->upem);
}
