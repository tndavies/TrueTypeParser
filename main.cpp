#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "libfnt.h"

int main(int argc, char* argv[])
{
	library lib("arial.ttf");

	auto glyph = lib.LoadGlyph('@');

	auto bmp = lib.RenderGlyph(glyph, 12.0f);

	stbi_write_bmp("example.bmp", bmp->width, bmp->height, 1, bmp->memory_);

	return 0;
}