#pragma once

#include <unordered_map>
#include <assert.h>
#include <string>

#include "stream.h"
#include "utils.h"

namespace libfnt
{
	class Format0_Mapper {
	public:
		Format0_Mapper(const uint8_t* gids)
			: m_GlyphIDs(gids) {}

		int get_glyph_idx(size_t c) {
			assert(c <= 255);
			return m_GlyphIDs[c];
		}

	private:
		const uint8_t* m_GlyphIDs;
	};

	struct ttf_parser {
		ttf_parser(char* fontMemory);

		void init();
		void register_tables();
		void select_charmap();
		void get_metrics();

		OutlineData LoadGlyph(size_t c);
		Stream GetTable(const std::string& tag) const;
		
		std::unordered_map<std::string, uint32_t> m_tables;
		Format0_Mapper* m_mapper;
		const char* m_font;
		uint16_t m_upem;
	};
}