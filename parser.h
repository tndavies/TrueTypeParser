#pragma once

#include <unordered_map>
#include <assert.h>
#include <string>
#include "utils.h"

namespace libfnt
{
	class Format0_Mapper
	{
		public:
			Format0_Mapper(char* gids)
				: m_GlyphIDs(gids) {}

			int get_glyph_idx(const char_code c) {
				assert(c <= 255);
				return m_GlyphIDs[c];
			}

		private:
			char* m_GlyphIDs;
	};

	struct ttf_parser {
		ttf_parser(char* fontMemory);

		void init();

		bool is_supported(); // @todo: mark const.
		
		void register_tables();

		void select_charmap();

		void get_metrics();

		char* get_table(const std::string& tag) const;

		OutlineData load_outline(size_t c);

		char* m_font; // @todo: mark const.
	
		std::unordered_map<std::string, uint32_t> m_tables;

		Format0_Mapper* m_mapper;

		uint16_t m_upem;
	};





}