#pragma once
#include <string>
#include <unordered_map>
#include <assert.h>

struct RenderResult {
	void* bitmap;
	size_t width;
	size_t height;
};

struct Point {
	Point(int16_t t_xc, int16_t t_yc, bool t_oc)
		: xc(t_xc), yc(t_yc), on_curve(t_oc) {}

	int16_t xc, yc;
	bool on_curve;
};


class Line {
public:
	// note: coordinates should be in font-unit space.
	Line(Point p0, Point p1, float upem)
		: m_P0(p0), m_P1(p1), m_Intercept(0), m_Gradient(0)
	{
		assert(!((p0.xc == p1.xc) && (p0.yc == p1.yc))); // Ensure p0 != p1

		this->Categorize(upem);
	}

	void getIntersections(float scanline, std::vector<float>& ixs) const;

private:
	void Categorize(float upem);

private:
	Point m_P0, m_P1;
	float m_Intercept, m_Gradient;

	enum class Type {
		Vertical, Horizontal, Slanted
	} m_Type;
};

struct Outline {
	std::vector<Line> edges;
	int width, height;
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

	RenderResult Rasterize(int cp);

	void check_supported();
	void register_tables();
	void select_encoding_scheme();
	Outline parse_outline(int cp);

private:
	std::unordered_map<std::string, uint32_t> m_Tables;
	ICodepointMap* m_Encoding;
	std::string m_FilePath;
	uint8_t* m_Data;
	int m_UnitsPerEM;
};