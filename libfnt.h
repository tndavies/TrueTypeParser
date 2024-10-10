#pragma once

#include <cstdint>
#include <string>
#include "parser.h"

namespace libfnt 
{
	typedef uint32_t char_code;

	enum class render_options {
		Default
	};

	struct library {
		ttf_parser* m_parser;

		library(const std::string& fontFilePath);

		OutlineData load_glyph(const char_code c);

		const RasterTarget* render_glyph(const OutlineData &outlineInfo, const float ptSize, 
			const render_options flags = render_options::Default);
	};
}