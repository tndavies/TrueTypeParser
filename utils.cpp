#include "utils.h"
#include <fstream>
#include <assert.h>
#include <iostream>
#include <functional>
#include <stack> 

float em2raster(const float value, const float upem) {
	const float pts = 12.0f;
	const float dpi = 96.0f;

	return (value / upem) * pts * dpi;
}

void em2rasterPt(fPoint& pt, float upem) {
	const float pts = 12.0f;
	const float dpi = 96.0f;

	pt.x = em2raster(pt.x, upem);
	pt.y = em2raster(pt.y, upem);
}

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

void edgeTable::add_edge(fPoint p0, fPoint p1) {
	edges.emplace_back(p0, p1);
}

void edgeTable::add_bezier(fPoint p0, fPoint ctrl, fPoint p1) {
	const float kTolerance = 1.0f; // @todo: what value makes sense for this?

	std::stack<Bezier> stack;

	stack.emplace(p0, ctrl, p1);

	while (!stack.empty()) {
		const Bezier curr = stack.top(); // copy off stack top.

		stack.pop();

		// See: https://en.wikipedia.org/wiki/Distance_from_a_point_to_a_line.
		const auto [x0, y0] = curr.ctrl;
		const auto [x1, y1] = curr.p0;
		const auto [x2, y2] = curr.p1;

		const float k = (y2 - y1) * x0 - (x2 - x1) * y0 + x2 * y1 - y2 * x1;
		const float m = std::powf(y2 - y1, 2) + std::powf(x2 - x1, 2);
		assert(m != 0.0f);

		const float dist = std::fabsf(k) / std::sqrtf(m);

		// 
		if (dist <= kTolerance) {
			edges.emplace_back(curr.p0, curr.p1);
			continue;
		}

		// @todo: The divide gets rounded here, do not in font units!
		auto m0x = 0.5f * (curr.p0.x + curr.ctrl.x);
		auto m0y = 0.5f * (curr.p0.y + curr.ctrl.y);
		fPoint m0(m0x, m0y);

		auto m2x = 0.5f * (curr.ctrl.x + curr.p1.x);
		auto m2y = 0.5f * (curr.ctrl.y + curr.p1.y);
		fPoint m2(m2x, m2y);

		auto m1x = 0.5f * (m0.x + m2.x);
		auto m1y = 0.5f * (m0.y + m2.y);
		fPoint m1(m1x, m1y);

		stack.emplace(curr.p0, m0, m1);
		stack.emplace(m1, m2, curr.p1);
	}

}
