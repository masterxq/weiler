// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Batch.h"
#include "gameTypes/MapCoordinates.h"
#include <vector>

class GameWorldViewer;

namespace weiler::gfx {

/// Overview map: terrain colors, territories, buildings, roads and fog of war, updated a few rows per frame.
class Minimap
{
public:
    void init(const GameWorldViewer& gwv);
    /// Update a part of the image (call every frame)
    void update(unsigned rows = 8);
    void invalidateAll();
    /// Draw into rect; returns the used rectangle (aspect kept)
    Rect2 draw(Batch& batch, const Rect2& area, float alpha = 1.f);
    /// Rectangle -> map position, viewRect: visible area in world pixels
    static Rect2 fit(const Rect2& area, MapExtent size);
    const Texture& texture() const { return tex_; }

private:
    uint32_t nodeColor(MapPoint pt) const;
    const GameWorldViewer* gwv_ = nullptr;
    MapExtent size_;
    Texture tex_;
    std::vector<uint32_t> pixels_;
    unsigned nextRow_ = 0;
    bool dirty_ = false;
};

} // namespace weiler::gfx
