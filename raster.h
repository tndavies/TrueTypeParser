#pragma once

#include <vector>
#include "outline.h"

//

struct Edge {
    Edge(fPoint p0, fPoint p1);

    void transform(const float pUpem, float pXmin, float pYmin);

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
        : p0(p0), ctrl(ctrl), p1(p1) {}
};

struct EdgeTable {
    std::vector<Edge> edges;

    void addEdge(const fPoint& p0, const fPoint& p1);
    void addBezier(const fPoint& p0, const fPoint& ctrl, const fPoint& p1);
};

struct RasterTarget {
    RasterTarget(const size_t pWidth, const size_t pHeight);

    void store(const size_t pX, const size_t pY, const uint8_t pCol);

    uint8_t fetch(const size_t pX, const size_t pY) const;

    //

    size_t width, height;
    void* memory_;
};

const RasterTarget* RenderOutline(const Outline& pOutline, const float pUpem);
float em2raster(const float value, const float upem);
void em2rasterPt(fPoint& pt, const float upem);