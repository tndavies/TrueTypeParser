#include "utils.h"
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