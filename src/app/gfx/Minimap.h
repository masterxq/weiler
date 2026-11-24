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
    /// Draw the explored part of the map into rect; returns the used rectangle (aspect kept)
    Rect2 draw(Batch& batch, const Rect2& area, float alpha = 1.f);
    /// Part of the map shown by the last draw, in nodes: the explored area with a margin (the whole map once most
    /// of it is explored)
    const Rect2& shownNodes() const { return shown_; }
    const Texture& texture() const { return tex_; }

private:
    uint32_t nodeColor(MapPoint pt) const;
    const GameWorldViewer* gwv_ = nullptr;
    MapExtent size_;
    Texture tex_;
    std::vector<uint32_t> pixels_;
    unsigned nextRow_ = 0;
    /// Explored columns per row (-1 = nothing explored)
    std::vector<int> rowMin_, rowMax_;
    Rect2 shown_;
    Rect2 exploredRegion() const;
    bool dirty_ = false;
};

} // namespace weiler::gfx
