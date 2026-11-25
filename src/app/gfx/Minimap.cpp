// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Minimap.h"
#include "GamePlayer.h"
#include "nodeObjs/noBase.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "gameTypes/MapTypes.h"
#include "gameData/TerrainDesc.h"
#include "gameData/WorldDescription.h"
#include <algorithm>

namespace weiler::gfx {

namespace {
uint32_t rgba(unsigned r, unsigned g, unsigned b, unsigned a = 255)
{
    return r | (g << 8) | (b << 16) | (a << 24);
}
uint32_t mixColor(uint32_t argb, unsigned r, unsigned g, unsigned b, float t)
{
    const float ar = static_cast<float>((argb >> 16) & 0xFF), ag = static_cast<float>((argb >> 8) & 0xFF),
                ab = static_cast<float>(argb & 0xFF);
    return rgba(static_cast<unsigned>(r * (1 - t) + ar * t), static_cast<unsigned>(g * (1 - t) + ag * t),
                static_cast<unsigned>(b * (1 - t) + ab * t));
}
} // namespace

void Minimap::init(const GameWorldViewer& gwv)
{
    gwv_ = &gwv;
    size_ = gwv.getWorld().getSize();
    // Two pixels per node horizontally: nodes are twice as wide as high
    pixels_.assign(static_cast<size_t>(size_.x) * 2 * size_.y, 0);
    tex_.create(size_.x * 2, size_.y, false, true, nullptr);
    rowMin_.assign(size_.y, -1);
    rowMax_.assign(size_.y, -1);
    invalidateAll();
    update(size_.y);
}

void Minimap::setLayers(unsigned layers)
{
    if(layers == layers_)
        return;
    layers_ = layers;
    // Redraw at once (a toggle must show right away)
    if(gwv_)
    {
        nextRow_ = 0;
        update(size_.y);
    }
}

void Minimap::invalidateAll()
{
    nextRow_ = 0;
}

uint32_t Minimap::nodeColor(MapPoint pt) const
{
    const GameWorldBase& world = gwv_->getWorld();
    const Visibility vis = gwv_->getVisibility(pt);
    if(vis == Visibility::Invisible)
        return rgba(12, 13, 14);
    const MapNode& node = world.getNode(pt);
    const auto& desc = world.getDescription().get(node.t1);
    const bool water = desc.kind == TerrainKind::Water;
    const uint32_t terrain = (layers_ & (water ? Water : Terrain)) ? desc.minimapColor : 0x5A5C58;
    unsigned r = (terrain >> 16) & 0xFF, g = (terrain >> 8) & 0xFF, b = terrain & 0xFF;
    // Shading from the terrain (light from the west)
    const float shade = std::clamp(static_cast<float>(node.shadow) / 64.f, 0.55f, 1.25f);
    r = std::min(255u, static_cast<unsigned>(r * shade));
    g = std::min(255u, static_cast<unsigned>(g * shade));
    b = std::min(255u, static_cast<unsigned>(b * shade));
    uint32_t c = rgba(r, g, b);
    const unsigned owner = vis == Visibility::Visible ? node.owner : gwv_->getYoungestFOWNode(pt).owner;
    if(owner)
    {
        const unsigned pc = world.getPlayer(owner - 1).color;
        if(layers_ & Territory)
            c = mixColor(pc, r, g, b, 0.35f);
        const noBase* obj = vis == Visibility::Visible ? node.obj : nullptr;
        if(obj && (layers_ & Buildings))
        {
            const GO_Type t = obj->getGOT();
            if(t == GO_Type::NobUsual || t == GO_Type::NobMilitary || t == GO_Type::NobStorehouse
               || t == GO_Type::NobHarborbuilding || t == GO_Type::NobHq || t == GO_Type::Buildingsite)
                c = mixColor(pc, 255, 255, 255, 0.75f);
        }
        bool road = false;
        if(layers_ & Roads)
        {
            for(const RoadDir dir : helpers::EnumRange<RoadDir>{})
                road |= gwv_->getVisibleRoad(pt, dir, vis) != PointRoad::None;
        }
        if(road)
            c = rgba(214, 196, 160);
    }
    if(vis == Visibility::FogOfWar)
    {
        const unsigned rr = (c & 0xFF) / 2, gg = ((c >> 8) & 0xFF) / 2, bb = ((c >> 16) & 0xFF) / 2;
        c = rgba(rr, gg, bb);
    }
    return c;
}

void Minimap::update(unsigned rows)
{
    if(!gwv_)
        return;
    for(unsigned i = 0; i < rows; ++i)
    {
        const unsigned y = nextRow_;
        rowMin_[y] = rowMax_[y] = -1;
        for(unsigned x = 0; x < size_.x; ++x)
        {
            const MapPoint pt(static_cast<MapCoord>(x), static_cast<MapCoord>(y));
            if(gwv_->getVisibility(pt) != Visibility::Invisible)
            {
                if(rowMin_[y] < 0)
                    rowMin_[y] = static_cast<int>(x);
                rowMax_[y] = static_cast<int>(x);
            }
            const uint32_t c = nodeColor(pt);
            // Odd rows are shifted by half a node
            const size_t base = static_cast<size_t>(y) * size_.x * 2;
            const unsigned px = (x * 2 + (y & 1)) % (size_.x * 2);
            pixels_[base + px] = c;
            pixels_[base + (px + 1) % (size_.x * 2)] = c;
        }
        nextRow_ = (nextRow_ + 1) % size_.y;
        dirty_ = true;
    }
    if(dirty_)
    {
        tex_.upload(0, 0, size_.x * 2, size_.y, pixels_.data());
        dirty_ = false;
    }
}

Rect2 Minimap::exploredRegion() const
{
    const Rect2 all{0, 0, static_cast<float>(size_.x), static_cast<float>(size_.y)};
    int x0 = size_.x, x1 = -1, y0 = size_.y, y1 = -1;
    for(unsigned y = 0; y < size_.y; ++y)
    {
        if(rowMin_[y] < 0)
            continue;
        x0 = std::min(x0, rowMin_[y]);
        x1 = std::max(x1, rowMax_[y]);
        y0 = std::min(y0, static_cast<int>(y));
        y1 = static_cast<int>(y);
    }
    if(x1 < 0)
        return all;
    // Margin, minimum size; areas across the map edge (the map wraps around) give the whole width or height
    const float margin = 10, minSize = 48;
    auto span = [&](int lo, int hi, float size, float& outLo, float& outLen) {
        float len = static_cast<float>(hi - lo + 1) + 2 * margin;
        len = std::max(len, std::min(minSize, size));
        if(len > size * 0.8f)
        {
            outLo = 0;
            outLen = size;
            return;
        }
        const float center = static_cast<float>(lo + hi + 1) / 2;
        outLo = std::clamp(center - len / 2, 0.f, size - len);
        outLen = len;
    };
    Rect2 r;
    span(x0, x1, all.w, r.x, r.w);
    span(y0, y1, all.h, r.y, r.h);
    return r;
}

Rect2 Minimap::fit(const Rect2& area)
{
    shown_ = exploredRegion();
    const float aspect = shown_.w * 2 / shown_.h;
    float w = area.w, h = area.w / aspect;
    if(h > area.h)
    {
        h = area.h;
        w = h * aspect;
    }
    return {area.x + (area.w - w) / 2, area.y + (area.h - h) / 2, w, h};
}

Rect2 Minimap::draw(Batch& batch, const Rect2& area, float alpha)
{
    const Rect2 r = fit(area);
    const float u0 = shown_.x / static_cast<float>(size_.x), v0 = shown_.y / static_cast<float>(size_.y);
    const float u1 = shown_.right() / static_cast<float>(size_.x), v1 = shown_.bottom() / static_cast<float>(size_.y);
    batch.quad(tex_.id(), r.x, r.y, r.right(), r.bottom(), u0, v0, u1, v1, Color{1, 1, 1, alpha});
    return r;
}

} // namespace weiler::gfx
