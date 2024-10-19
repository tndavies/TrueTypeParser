#pragma once

#include <vector>
#include "outline.h"

#define OnCurve(x) (x & 1)

//

struct Edge {
    Edge(const fPoint& p0, const fPoint& p1, const BoundingBox& pBB, const float upem);

    //

    fPoint apex, base;
    bool is_active;
    bool is_vertical;
    float m, c;
    float sclx;
};

struct Bezier {
    fPoint p0, ctrl, p1;

    Bezier(fPoint p0, fPoint ctrl, fPoint p1)
        : p0(p0), ctrl(ctrl), p1(p1)
    {
    }
};

class EdgeTable {
    /* === Methods === */
public:
    EdgeTable(const GlyphDescription& pGlyphDesc, const float pRasterUpem)
        : m_glyphDesc(pGlyphDesc), m_upem(pRasterUpem)
    {
        Generate(m_glyphDesc.mesh);
    }

private:
    void Generate(GlyphMesh& pMesh);
    void AddEdge(const fPoint& p0, const fPoint& p1);
    void AddBezier(const fPoint& p0, const fPoint& ctrl, const fPoint& p1);

    /* === Variables === */
public:
    std::vector<Edge> edges;

private:
    GlyphDescription m_glyphDesc;
    float m_upem;
};

struct RasterTarget {
    RasterTarget(const size_t pWidth, const size_t pHeight);

    void store(const size_t pX, const size_t pY, const uint8_t pCol);

    uint8_t fetch(const size_t pX, const size_t pY) const;

    //

    size_t width, height;
    void* memory_;
};

const RasterTarget* RenderOutline(const GlyphDescription& pGlyphDesc, const float pUpem);
float DesignToRaster(const float value, const float upem);