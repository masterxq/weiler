// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Gl.h"
#include "world/GameWorldViewer.h"
#include "gameTypes/MapCoordinates.h"
#include <array>
#include <vector>

class GameWorldViewer;

namespace weiler::gfx {

/// Camera of the world view: world pixel at the top left corner and zoom (screen pixels per world pixel)
struct Camera
{
    float x = 0, y = 0;
    float zoom = 1;
    int screenW = 1, screenH = 1;
    float viewW() const { return screenW / zoom; }
    float viewH() const { return screenH / zoom; }
};

/// Range of node positions (unwrapped, may exceed the map) that are visible
struct NodeRange
{
    int x0, y0, x1, y1;
};

/// Terrain triangles with texture, shading, terrain edges and roads; drawn with an own shader.
/// Geometry follows the terrain rendering of Return to the Roots (TerrainRenderer.cpp), GPL-2.0-or-later.
class TerrainLayer : public GameWorldViewer::ITerrainObserver
{
public:
    TerrainLayer();
    ~TerrainLayer() override;
    void init(const GameWorldViewer& gwv);

    NodeRange visibleRange(const Camera& cam) const;
    /// Position of a (possibly unwrapped) node position in world pixels, including the altitude
    PointF nodePos(Position p) const;
    MapPoint toMapPoint(Position p) const;

    void draw(const Camera& cam, unsigned timeMs);

    void altitudeChanged(MapPoint pt) override;
    void visibilityChanged(MapPoint pt) override;
    void allChanged() override;

private:
    struct Vtx
    {
        float x, y, u, v, b;
    };
    struct Tex
    {
        std::vector<Texture> frames;
        std::array<PointF, 3> rsu{}, usd{};
    };
    void updateColor(MapPoint pt);
    float color(Position p) const;
    void addTri(std::vector<Vtx>& out, PointF p0, PointF p1, PointF p2, PointF t0, PointF t1, PointF t2, float b0,
                float b1, float b2);

    const GameWorldViewer* gwv_ = nullptr;
    MapExtent size_;
    std::vector<float> colors_;
    std::vector<Tex> terrainTex_;
    std::vector<Texture> edgeTex_;
    std::vector<Texture> roadTex_;
    std::vector<std::vector<Vtx>> buckets_;
    GLuint program_ = 0, vao_ = 0, vbo_ = 0;
    GLint uScreen_ = -1, uCam_ = -1;
    int maxAltitudePx_ = 0;
};

} // namespace weiler::gfx
