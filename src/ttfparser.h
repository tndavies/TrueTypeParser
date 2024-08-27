#pragma once
#include <string>
#include <unordered_map>
#include <assert.h>
#include <vector>

struct libfnt_point {	
	libfnt_point(int16_t t_xc, int16_t t_yc, bool t_oc)
		: x(t_xc), y(t_yc), on_curve(t_oc) {}

	int16_t x, y;
	bool on_curve;

	libfnt_point operator+(const libfnt_point& p) const
	{
		return libfnt_point(this->x + p.x, this->y + p.y, NULL);
	}

	libfnt_point operator/(float scalar)
	{
		return libfnt_point(this->x / scalar, this->y / scalar, NULL);
	}
};

struct libfnt_edge {
	libfnt_edge(libfnt_point p0, libfnt_point p1, float upem);

	bool is_vertical, active;
	float ymax, ymin;
	float xmax, xmin;
	float m, c, sclx;
};

struct libfnt_contour {
	std::vector<libfnt_point> points;
};

struct Bitmap {
	void* memory;
	size_t width, height;

	Bitmap(void* mem, size_t w, size_t h)
		: memory(mem), width(w), height(h)
	{}
};

struct Outline_Descriptor {
	std::vector<libfnt_contour> contours;
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

	Bitmap rasterize(int cp);

	void check_supported();
	void register_tables();
	void select_encoding_scheme();
	Outline_Descriptor extract_outline(int cp);

	Bitmap allocate_bitmap(const Outline_Descriptor& outline);

private:
	std::unordered_map<std::string, uint32_t> m_Tables;
	ICodepointMap* m_Encoding;
	std::string m_FilePath;
	uint8_t* m_Data;
	int m_UnitsPerEM;
};