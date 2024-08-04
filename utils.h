#pragma once
#include <stdint.h>

uint8_t* LoadFile(const char* file_path);

uint8_t get_u8(void* stream);
uint16_t get_u16(void* stream);
uint32_t get_u32(void* stream);
uint64_t get_u64(void* stream);

int8_t get_s8(void* stream);
int16_t get_s16(void* stream);
int32_t get_s32(void* stream);
int64_t get_s64(void* stream);



