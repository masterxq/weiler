// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Batch.h"
#include "SpriteAtlas.h"
#include "TerrainLayer.h"
#include <functional>

class GameWorldViewer;

namespace weiler::gfx {

/// Draws the game world: terrain, roads, objects, figures, borders and fog of war.
/// Object drawing order adapted from Return to the Roots (world/GameWorldView.cpp), GPL-2.0-or-later.
class WorldView
{
public:
    WorldView(Batch& batch, SpriteAtlas& atlas);
    void setViewer(GameWorldViewer* gwv);
    GameWorldViewer* viewer() const { return gwv_; }
    TerrainLayer& terrain() { return terrain_; }

    Camera cam;

    /// Called for every visible node after the objects of its row (screen position, world transform active)
    std::function<void(MapPoint, PointF)> nodeOverlay;

    void draw(unsigned timeMs);

    /// Screen pixel -> nearest map node
    MapPoint pickNode(float sx, float sy) const;
    /// World pixel of a node, nearest copy to the current view center
    PointF worldPosNear(MapPoint pt) const;
    PointF worldToScreen(PointF w) const { return {(w.x - cam.x) * cam.zoom, (w.y - cam.y) * cam.zoom}; }
    PointF screenToWorld(float sx, float sy) const { return {sx / cam.zoom + cam.x, sy / cam.zoom + cam.y}; }
    void centerOn(MapPoint pt);
    /// Zoom around a screen point
    void zoomAt(float sx, float sy, float newZoom);
    void moveBy(float dxScreen, float dyScreen);
    /// Keep the camera inside one map period
    void normalize();
    float mapWidthPx() const;
    float mapHeightPx() const;

private:
    Batch& batch_;
    SpriteAtlas& atlas_;
    TerrainLayer terrain_;
    GameWorldViewer* gwv_ = nullptr;
};

} // namespace weiler::gfx
