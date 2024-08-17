#include <iostream>
#include "ttfparser.h"

#define __STDC_LIB_EXT1__
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

int main(int argc, char* argv[]) 
{
	//if (argc < 3) {
	//	std::cout << "Error: Invalid inputs!";
	//	return -1;
	//}

	TTFParser ttf(argv[1]);
	auto bitmap = ttf.RasterizeGlyph(0);

	auto x = stbi_write_bmp("letter.bmp", bitmap.width, bitmap.height, 1, bitmap.memory);
	assert(x);
}