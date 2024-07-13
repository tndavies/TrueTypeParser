#include "ttfparser.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>

uint8_t* LoadFile(const char* file_path) 
{
	// Load font file into memory.
	std::ifstream FileHandle(file_path, std::ios::binary);
	if (!FileHandle.is_open()) {
		return nullptr;
	}

	FileHandle.seekg(0, FileHandle.end);
	const auto FileByteSize = FileHandle.tellg();
	FileHandle.seekg(0, FileHandle.beg);

	char* Data = static_cast<char*>(malloc(FileByteSize));
	FileHandle.read(Data, FileByteSize);
	FileHandle.close();
	assert(Data);

	return (uint8_t*)Data;
}

uint8_t get_u8(void* stream) {
	uint8_t* ptrDataField = static_cast<uint8_t*>(stream);
	uint8_t Bytes = *ptrDataField;
	return Bytes;
}
int8_t get_s8(void* stream) {
	int8_t* ptrDataField = static_cast<int8_t*>(stream);
	int8_t Bytes = *ptrDataField;
	return Bytes;
}
uint16_t get_u16(void* stream) {
	uint16_t* ptrDataField = static_cast<uint16_t*>(stream);
	uint16_t Bytes = *ptrDataField;

#ifndef BIG_ENDIAN
	uint16_t Val0 = (Bytes & 0xff) << 8;
	uint16_t Val1 = (Bytes & 0xff00) >> 8;

	Bytes = Val0 | Val1;
#endif

	return Bytes;
}
int16_t get_s16(void* stream) {
	int16_t* ptrDataField = static_cast<int16_t*>(stream);
	int16_t Bytes = *ptrDataField;

#ifndef BIG_ENDIAN
	int16_t Val0 = (Bytes & 0xff) << 8;
	int16_t Val1 = (Bytes & 0xff00) >> 8;

	Bytes = Val0 | Val1;
#endif

	return Bytes;
}
uint32_t get_u32(void* stream) {
	uint32_t* ptrDataField = static_cast<uint32_t*>(stream);
	uint32_t Bytes = *ptrDataField;

#ifndef BIG_ENDIAN
	uint32_t Val0 = (Bytes & 0xff) << 24;
	uint32_t Val1 = (Bytes & 0xff00) << 8;
	uint32_t Val2 = (Bytes & 0xff0000) >> 8;
	uint32_t Val3 = (Bytes & 0xff000000) >> 24;

	Bytes = Val0 | Val1 | Val2 | Val3;
#endif

	return Bytes;
}
int32_t get_s32(void* stream) {
	int32_t* ptrDataField = static_cast<int32_t*>(stream);
	int32_t Bytes = *ptrDataField;

#ifndef BIG_ENDIAN
	int32_t Val0 = (Bytes & 0xff) << 24;
	int32_t Val1 = (Bytes & 0xff00) << 8;
	int32_t Val2 = (Bytes & 0xff0000) >> 8;
	int32_t Val3 = (Bytes & 0xff000000) >> 24;

	Bytes = Val0 | Val1 | Val2 | Val3;
#endif

	return Bytes;
}
uint64_t get_u64(void* stream) {
	uint64_t* ptrDataField = static_cast<uint64_t*>(stream);
	uint64_t Bytes = *ptrDataField;

#ifndef BIG_ENDIAN
	uint64_t Val0 = (Bytes & 0xff) << 56;
	uint64_t Val1 = (Bytes & 0xff00) << 40;
	uint64_t Val2 = (Bytes & 0xff0000) >> 24;
	uint64_t Val3 = (Bytes & 0xff000000) << 8;
	uint64_t Val4 = (Bytes & 0xff00000000) >> 8;
	uint64_t Val5 = (Bytes & 0xff0000000000) >> 24;
	uint64_t Val6 = (Bytes & 0xff000000000000) >> 40;
	uint64_t Val7 = (Bytes & 0xff00000000000000) >> 56;

	Bytes = Val0 | Val1 | Val2 | Val3 | Val4 | Val5 | Val6 | Val7;
#endif

	return Bytes;
}
int64_t get_s64(void* stream) {
	int64_t* ptrDataField = static_cast<int64_t*>(stream);
	int64_t Bytes = *ptrDataField;

#ifndef BIG_ENDIAN
	int64_t Val0 = (Bytes & 0xff) << 56;
	int64_t Val1 = (Bytes & 0xff00) << 40;
	int64_t Val2 = (Bytes & 0xff0000) >> 24;
	int64_t Val3 = (Bytes & 0xff000000) << 8;
	int64_t Val4 = (Bytes & 0xff00000000) >> 8;
	int64_t Val5 = (Bytes & 0xff0000000000) >> 24;
	int64_t Val6 = (Bytes & 0xff000000000000) >> 40;
	int64_t Val7 = (Bytes & 0xff00000000000000) >> 56;

	Bytes = Val0 | Val1 | Val2 | Val3 | Val4 | Val5 | Val6 | Val7;
#endif

	return Bytes;
}

void TTFParser::check_supported() {
	auto OutlineType = get_u32(m_Data);
	assert(OutlineType == 0x00010000 || OutlineType == 0x74727565); // only support ttf outlines.
}

void TTFParser::register_tables() {
	auto TableCount = get_u16(m_Data + 4);
	for (size_t tbl_idx = 0; tbl_idx < TableCount; tbl_idx++)
	{
		auto Metadata = (m_Data + 12) + (16 * tbl_idx);

		std::string tag;
		for (size_t k = 0; k < 4; k++)
			tag += std::tolower(Metadata[k]);

		auto offset = get_u32(Metadata + 8);

		m_Tables[tag] = offset;
	}
}

void TTFParser::select_encoding_scheme() {
	auto* cmap = get_table("cmap");
	auto EncodingCount = get_u16(cmap + 2);
	for (size_t k = 0; k < EncodingCount; k++)
	{
		uint8_t* EncodingMetatable = cmap + 4 + 8 * k;
		uint8_t* format_table = cmap + get_u32(EncodingMetatable + 4);
		auto format = get_u16(format_table);

		switch (format) {
		case 0:
			m_Encoding = new CodepointMap_Format0(format_table + 6);
			break;
		}
	}
	assert(m_Encoding);
}

std::vector<int16_t>* parse_coordinate_data(const std::vector<uint8_t>& flags, 
	uint8_t** stream_handle, size_t shortbit_idx, size_t infobit_idx) 
{
	std::vector<int16_t>* buff = new std::vector<int16_t>{ 0 };

	for (auto flag : flags) {
		bool is_short = flag & (1 << shortbit_idx);
		bool info_bit = flag & (1 << infobit_idx);

		if (is_short) {
			uint8_t delta = **stream_handle;
			(*stream_handle)++;

			int16_t cval = buff->back() + delta * (info_bit ? 1 : -1);
			buff->push_back(cval);
		}
		else {
			if (info_bit) {
				buff->push_back(buff->back());
			}
			else {
				auto delta = get_s16(*stream_handle);
				*stream_handle += 2;

				int16_t cval = buff->back() + delta;
				buff->push_back(cval);
			}
		}
	}

	buff->erase(buff->begin()); // remove the reference point (0,0).

	return buff;
}

void TTFParser::parse_outline(int cp) {
	auto glyph_index = m_Encoding->get_glyph_index(cp);

	// Find the location of the outline data.
	uint8_t* outline = nullptr;

	auto head = get_table("head");
	auto loca = get_table("loca");
	auto glyf = get_table("glyf");

	auto loca_format = get_s16(head + 50);
	assert(loca_format == 0 || loca_format == 1);
	if (loca_format == 1) {
		auto* offset = reinterpret_cast<uint32_t*>(loca) + glyph_index;
		outline = glyf + get_u32(offset);
	}
	else {
		auto* offset = reinterpret_cast<uint16_t*>(loca) + glyph_index;
		outline = glyf + 2 * get_u16(offset);
	}

	// Parse the point data.
	auto contour_count = get_s16(outline);
	assert(contour_count > 0); // only support simple outline atm!

	uint8_t* contour_data = outline + 10;
	auto* point_map_end = (uint16_t*)contour_data + (contour_count - 1);
	auto point_count = get_u16(point_map_end) + 1;
	uint8_t* hprog = (uint8_t*)(point_map_end + 1);
	uint8_t* stream = hprog + 2 + get_u16(hprog);

	// decompress flags array.
	std::vector<uint8_t> flags;
	while (flags.size() < point_count) {
		uint8_t flag = *stream++;

		size_t repeat_count = 1;
		if (flag & (1 << 3)) {
			repeat_count += *stream++;
		}

		for (size_t n = 0; n < repeat_count; n++) {
			flags.push_back(flag);
		}
	}

	std::vector<int16_t>* xc_buff = parse_coordinate_data(flags, &stream, 1, 4);
	std::vector<int16_t>* yc_buff = parse_coordinate_data(flags, &stream, 2, 5);

	std::vector<Point> points;
	for (size_t k = 0; k < flags.size(); k++) {
		points.emplace_back(xc_buff->at(k), yc_buff->at(k), flags[k] & 1);
	}

	int x = 100;
}

TTFParser::TTFParser(const char* file_path) 
	: m_FilePath(file_path), m_Data(nullptr), m_Encoding(nullptr)
{
	m_Data = LoadFile(m_FilePath.c_str());

	check_supported();
	
	register_tables();
	
	select_encoding_scheme();

	parse_outline('A');
}
