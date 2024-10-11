#include <unordered_map>
#include "libfnt.h"
#include "utils.h"

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char* argv[])
{
	libfnt::library lib("arial.ttf");

	auto glyph = lib.load_glyph('9');

	auto bmp = lib.render_glyph(glyph, 12.0f);

	stbi_write_bmp("example.bmp", bmp->width, bmp->height, 1, bmp->m_memory);

	return 0;
}