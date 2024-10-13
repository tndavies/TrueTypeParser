#pragma once

#include <vector>

//

struct EdgeTable; // defined in raster.h

struct Point {
    float x, y;

    Point(const float pX, const float pY)
        : x(pX), y(pY) {}

    Point() : x(0), y(0) {}
};

using fPoint = Point; // todo: remove

struct Contour {
    std::vector<uint8_t> flags;
    std::vector<int16_t> xs;
    std::vector<int16_t> ys;

    auto getTotalPtCount() const { return flags.size(); }
};

struct BoundingBox {
    float xMin, yMin;
    float xMax, yMax;

    BoundingBox(const float pXmin, const float pYmin, const float pXmax, const float pYmax)
        : xMin(pXmin), yMin(pYmin), xMax(pXmax), yMax(pYmax) {}
};

struct Outline {
    Outline(const EdgeTable* pEdgeTable, const BoundingBox& pBB)
        : et(pEdgeTable), bb(pBB) {}

    //

    const EdgeTable* et;
    BoundingBox bb;
};