// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Drawing order adapted from Return to the Roots (libs/s25main/world/GameWorldView.cpp), GPL-2.0-or-later.

#include "WorldView.h"
#include "CatapultStone.h"
#include "FOWObjects.h"
#include "GamePlayer.h"
#include "Loader.h"
#include "helpers/EnumRange.h"
#include "nodeObjs/noBase.h"
#include "nodeObjs/noMovable.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "world/MapGeometry.h"
#include "gameTypes/FoWNode.h"
#include "gameData/MapConsts.h"
#include "s25util/colors.h"
#include <cmath>
#include <vector>

namespace weiler::gfx {

namespace {
struct BetweenRows
{
    noBase* obj;
    DrawPoint pos;
};
} // namespace

WorldView::WorldView(Batch& batch, SpriteAtlas& atlas) : batch_(batch), atlas_(atlas) {}

void WorldView::setViewer(GameWorldViewer* gwv)
{
    gwv_ = gwv;
    if(gwv_)
    {
        terrain_.init(*gwv_);
        gwv_->setTerrainObserver(&terrain_);
    }
}

float WorldView::mapWidthPx() const
{
    return gwv_ ? static_cast<float>(gwv_->getWorld().getSize().x * TR_W) : 1.f;
}

float WorldView::mapHeightPx() const
{
    return gwv_ ? static_cast<float>(gwv_->getWorld().getSize().y * TR_H) : 1.f;
}

void WorldView::normalize()
{
    const float w = mapWidthPx(), h = mapHeightPx();
    cam.x = std::fmod(cam.x, w);
    if(cam.x < 0)
        cam.x += w;
    cam.y = std::fmod(cam.y, h);
    if(cam.y < 0)
        cam.y += h;
}

void WorldView::moveBy(float dxScreen, float dyScreen)
{
    cam.x -= dxScreen / cam.zoom;
    cam.y -= dyScreen / cam.zoom;
    normalize();
}

void WorldView::zoomAt(float sx, float sy, float newZoom)
{
    const PointF w = screenToWorld(sx, sy);
    cam.zoom = newZoom;
    cam.x = w.x - sx / cam.zoom;
    cam.y = w.y - sy / cam.zoom;
    normalize();
}

void WorldView::centerOn(MapPoint pt)
{
    if(!gwv_)
        return;
    const PointF p(gwv_->getWorld().getNodePos(pt));
    cam.x = p.x - cam.viewW() / 2;
    cam.y = p.y - cam.viewH() / 2;
    normalize();
}

PointF WorldView::worldPosNear(MapPoint pt) const
{
    PointF p(gwv_->getWorld().getNodePos(pt));
    const float w = mapWidthPx(), h = mapHeightPx();
    const float cx = cam.x + cam.viewW() / 2, cy = cam.y + cam.viewH() / 2;
    p.x += std::round((cx - p.x) / w) * w;
    p.y += std::round((cy - p.y) / h) * h;
    return p;
}

MapPoint WorldView::pickNode(float sx, float sy) const
{
    const PointF w = screenToWorld(sx, sy);
    // Search the nodes around the estimated row/column (altitude shifts nodes up)
    const NodeRange r{static_cast<int>(std::floor(w.x / TR_W)) - 2, static_cast<int>(std::floor(w.y / TR_H)) - 2,
                      static_cast<int>(std::ceil(w.x / TR_W)) + 2,
                      static_cast<int>(std::ceil(w.y / TR_H)) + 2 + 60 * HEIGHT_FACTOR / TR_H};
    float best = 1e30f;
    MapPoint bestPt(0, 0);
    for(int y = r.y0; y <= r.y1; ++y)
    {
        for(int x = r.x0; x <= r.x1; ++x)
        {
            const PointF p = terrain_.nodePos(Position(x, y));
            const float d = (p.x - w.x) * (p.x - w.x) + (p.y - w.y) * (p.y - w.y);
            if(d < best)
            {
                best = d;
                bestPt = terrain_.toMapPoint(Position(x, y));
            }
        }
    }
    return bestPt;
}

void WorldView::draw(unsigned timeMs)
{
    if(!gwv_)
        return;
    batch_.flush();
    terrain_.draw(cam, timeMs);

    const GameWorldBase& world = gwv_->getWorld();
    batch_.setTransform(cam.zoom, -cam.x * cam.zoom, -cam.y * cam.zoom);
    const NodeRange r = terrain_.visibleRange(cam);
    std::vector<BetweenRows> between;
    for(int y = r.y0; y <= r.y1; ++y)
    {
        between.clear();
        for(int x = r.x0; x <= r.x1; ++x)
        {
            const Position p(x, y);
            const MapPoint pt = terrain_.toMapPoint(p);
            const PointF posF = terrain_.nodePos(p);
            const DrawPoint pos(Position::Truncate, posF);
            const Visibility vis = gwv_->getVisibility(pt);
            if(vis == Visibility::Invisible)
                continue;

            // Boundary stones
            {
                const bool isFoW = vis == Visibility::FogOfWar;
                const BoundaryStones& stones =
                  isFoW ? gwv_->getYoungestFOWNode(pt).boundary_stones : world.getNode(pt).boundary_stones;
                const unsigned char owner = stones[BorderStonePos::OnPoint];
                if(owner)
                {
                    const GamePlayer& pl = world.getPlayer(owner - 1);
                    unsigned playerColor = pl.color;
                    if(isFoW)
                        playerColor = calcPlayerFOWDrawColor(playerColor);
                    for(const auto bPos : helpers::EnumRange<BorderStonePos>{})
                    {
                        DrawPoint stonePos;
                        if(bPos == BorderStonePos::OnPoint)
                            stonePos = pos;
                        else if(stones[bPos])
                        {
                            const PointF nb = terrain_.nodePos(::getNeighbour(p, toDirection(bPos)));
                            stonePos = DrawPoint(Position::Truncate, (posF + nb) / 2.f);
                        } else
                            continue;
                        LOADER.boundary_stone_cache[pl.nation].draw(stonePos, isFoW ? FOW_DRAW_COLOR : COLOR_WHITE,
                                                                     playerColor);
                    }
                }
            }

            if(vis == Visibility::Visible)
            {
                if(noBase* obj = world.getNode(pt).obj)
                    obj->draw(pos);
                // Figures moving towards this point from below
                for(Direction dir : {Direction::NorthEast, Direction::NorthWest})
                {
                    const Position pBelow = ::getNeighbour(p, dir + 3u);
                    const MapPoint ptBelow = terrain_.toMapPoint(pBelow);
                    const DrawPoint figPos(Position::Truncate, terrain_.nodePos(pBelow));
                    for(noBase& fig : world.getFigures(ptBelow))
                    {
                        if(fig.isMoving() && static_cast<noMovable&>(fig).getCurMoveDir() == dir)
                            between.push_back({&fig, figPos});
                    }
                }
                for(noBase& fig : world.getFigures(pt))
                {
                    if(fig.isMoving())
                    {
                        const Direction d = static_cast<noMovable&>(fig).getCurMoveDir();
                        if(d == Direction::NorthEast || d == Direction::NorthWest)
                            continue;
                        between.push_back({&fig, pos});
                    } else if(fig.getGOT() == GO_Type::Ship)
                        between.push_back({&fig, pos});
                    else
                        fig.draw(pos);
                }
            } else if(const FOWObject* fow = gwv_->getYoungestFOWObject(pt))
                fow->draw(pos);

            if(nodeOverlay)
                nodeOverlay(pt, posF);
        }
        for(auto& b : between)
            b.obj->draw(b.pos);
    }
    // Catapult stones (they draw themselves relative to an offset)
    for(auto* stone : world.catapult_stones)
    {
        if(gwv_->getVisibility(stone->dest_building) == Visibility::Visible
           || gwv_->getVisibility(stone->dest_map) == Visibility::Visible)
            stone->draw(DrawPoint(0, 0));
    }
    batch_.flush();
    batch_.resetTransform();
}

} // namespace weiler::gfx
