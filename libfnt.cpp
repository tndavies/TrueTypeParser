#include <assert.h> // @todo: replace asserts with proper error codes.
#include <fstream>

#include "libfnt.h"
#include "raster.h"

using namespace libfnt;

library::library(const std::string& fontFilePath) 
{
	// Load font file into memory.
	std::ifstream file_handle(fontFilePath, std::ios::binary);
	assert(file_handle.is_open());

	file_handle.seekg(0, file_handle.end);
	const auto file_size = file_handle.tellg();
	file_handle.seekg(0, file_handle.beg);

	void* font = std::malloc(file_size);
	file_handle.read((char*)font, file_size);
	file_handle.close();
	
	assert(font);

	//
	m_parser = new ttf_parser((char*)font);
	m_parser->init(); // @todo: check for error.
}

OutlineData library::load_glyph(const char_code c)
{
	return m_parser->load_outline(c);
}

const RasterTarget* library::render_glyph(const OutlineData &outlineInfo, const float ptSize, const render_options flags)
{
	return rasterise_outline(outlineInfo, m_parser->m_upem);
}
