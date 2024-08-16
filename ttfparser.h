#pragma once
#include <string>
#include <unordered_map>
#include <assert.h>
#include <vector>

typedef int16_t FontUnit;

struct Point {
	Point(int16_t t_xc, int16_t t_yc, bool t_oc)
		: xc(t_xc), yc(t_yc), on_curve(t_oc) {}

	int16_t xc, yc;
	bool on_curve;

	bool operator==(const Point& other) const {
		return this->xc == other.xc && this->yc == other.yc;
	}

	bool operator!=(const Point& other) const {
		return !(*this == other);
	}
};

struct Edge {
	Point p0, p1;

	Edge(FontUnit x0, FontUnit y0, FontUnit x1, FontUnit y1)
		: p0(x0, y0, true), p1(x1, y1, true)
	{}
};

struct Bitmap {
	void* memory;
	size_t width, height;

	Bitmap(void* mem, size_t w, size_t h)
		: memory(mem), width(w), height(h)
	{}
};

struct Outline_Descriptor {
	std::vector<Point> points;
	int x_extent, y_extent;
};

struct ICodepointMap 
{
	virtual int get_glyph_index(const int cp) = 0;
};

class CodepointMap_Format0 : public ICodepointMap 
{
public:
	CodepointMap_Format0(uint8_t* gids)
		: m_GlyphIDs(gids) {}

	int get_glyph_index(int cp) override {
		assert(cp <= 255);
		return m_GlyphIDs[cp];
	}

private:
	uint8_t* m_GlyphIDs;
};

class TTFParser {
public:
	TTFParser(const char* file_path);

	uint8_t* get_table(const std::string& tag) const { 
		return m_Data + m_Tables.at(tag);
	}

	Bitmap RasterizeGlyph(int cp);

	void check_supported();
	void register_tables();
	void select_encoding_scheme();
	Outline_Descriptor ExtractOutline(int cp);

	Bitmap allocate_bitmap(const Outline_Descriptor& outline);

	void find_intersections(size_t kCurrEdge, float scl, std::vector<float>& hits, std::vector<Edge>& edge_table);

private:
	std::unordered_map<std::string, uint32_t> m_Tables;
	ICodepointMap* m_Encoding;
	std::string m_FilePath;
	uint8_t* m_Data;
	int m_UnitsPerEM;
};