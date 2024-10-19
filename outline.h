#pragma once

#include <vector>
#include "base.h"

//

struct EdgeTable; // defined in raster.h

struct Point {
    float x, y;

    //

    Point(const float pX, const float pY)
        : x(pX), y(pY)
    {
    }

    Point() : x(0), y(0) {}
};

using fPoint = Point; // todo: remove

struct Contour {
    std::vector<uint8_t> flags;
    std::vector<s16> xs;
    std::vector<s16> ys;

    //

    auto getTotalPtCount() const { return flags.size(); }
};

struct BoundingBox {
    float xMin, yMin;
    float xMax, yMax;

    //

    BoundingBox(const float pXmin, const float pYmin, const float pXmax, const float pYmax)
        : xMin(pXmin), yMin(pYmin), xMax(pXmax), yMax(pYmax)
    {
    }
};

struct Outline {
    Outline(const EdgeTable* pEdgeTable, const BoundingBox& pBB)
        : et(pEdgeTable), bb(pBB)
    {
    }

    //

    const EdgeTable* et;
    BoundingBox bb;
};

struct GlyphMesh {
    GlyphMesh() = default;

    GlyphMesh(const std::vector<Contour>& pContours)
        : contours(pContours)
    {
    }

    void AddMesh(const GlyphMesh& pMesh)
    {
        for (const auto& c : pMesh.contours) {
            contours.emplace_back(c);
        }
    }

    //

    std::vector<Contour> contours;
};

struct GlyphDescription {
    GlyphDescription(const GlyphMesh& pMesh, const BoundingBox& pBB)
        : mesh(pMesh), bb(pBB)
    {
    }

    //

    GlyphMesh mesh;
    BoundingBox bb;
};