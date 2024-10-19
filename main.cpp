#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "libfnt.h"

int main(int argc, char* argv[])
{
	library lib("./fonts/TestFont.ttf");

	//const uint32_t charCode = 0x06E9; // This glyph crashes the renderer!

	auto glyph = lib.LoadGlyph(2);

	auto bmp = lib.RenderGlyph(glyph, 12.0f);

	stbi_write_bmp("example.bmp", bmp->width, bmp->height, 1, bmp->memory_);

	// @todo: write program to render all Unicode-BMP glyphs into
	// a single atlas, and then we can optimise the library's speed.

	return 0;
}