// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "HudImpl.h"
#include "Audio.h"
#include "GamePlayer.h"
#include "GlobalGameSettings.h"
#include "Loader.h"
#include "ogl/glArchivItem_Bitmap.h"
#include "buildings/nobBaseWarehouse.h"
#include "Names.h"
#include "Settings.h"
#include "Tutorial.h"
#include "menu/Menus.h"
#include "addons/const_addons.h"
#include "buildings/noBaseBuilding.h"
#include "buildings/nobBaseMilitary.h"
#include "buildings/nobUsual.h"
#include "buildings/noBuildingSite.h"
#include "nodeObjs/noFlag.h"
#include "nodeObjs/noShip.h"
#include "pathfinding/FindPathForRoad.h"
#include "postSystem/PostBox.h"
#include "postSystem/PostManager.h"
#include "postSystem/PostMsg.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "gameTypes/BuildingQuality.h"
#include "gameData/BuildingConsts.h"
#include "gameData/BuildingProperties.h"
#include "gameData/MapConsts.h"
#include <algorithm>
#include <iostream>
#include <cmath>

namespace weiler {

using namespace ui;

namespace {
bool isBuildingObj(const noBase* obj)
{
    if(!obj)
        return false;
    const NodalObjectType t = obj->getType();
    return t == NodalObjectType::Building || t == NodalObjectType::Buildingsite;
}

bool addonAllows(const GlobalGameSettings& ggs, BuildingType bt)
{
    switch(bt)
    {
        case BuildingType::Charburner: return ggs.isEnabled(AddonId::CHARBURNER);
        case BuildingType::Vineyard:
        case BuildingType::Winery:
        case BuildingType::Temple: return ggs.isEnabled(AddonId::WINE);
        case BuildingType::Skinner:
        case BuildingType::Tannery:
        case BuildingType::LeatherWorks: return ggs.isEnabled(AddonId::LEATHER);
        case BuildingType::Headquarters:
        case BuildingType::Nothing9: return false;
        default: return true;
    }
}
} // namespace

/// Buildings that can be placed at pt by the local player
std::vector<BuildingType> fittingBuildings(const Hud::Impl& h, MapPoint pt)
{
    std::vector<BuildingType> fitting;
    const unsigned me = h.game.localPlayer();
    const BuildingQuality bq = h.game.viewer().getBQ(pt);
    const bool milAllowed = !h.world().isMilitaryBuildingNearNode(pt, static_cast<unsigned char>(me));
    for(unsigned i = 0; i < helpers::NumEnumValues_v<BuildingType>; ++i)
    {
        const auto bt = static_cast<BuildingType>(i);
        if(!addonAllows(h.world().getGGS(), bt) || !h.player().isBuildingEnabled(bt))
            continue;
        if(!canUseBq(bq, BUILDING_SIZE[bt]))
            continue;
        if(BuildingProperties::isMilitary(bt) && !milAllowed)
            continue;
        fitting.push_back(bt);
    }
    return fitting;
}

// ---------------------------------------------------------------------------------------------------------------
Hud::Impl::Impl(GameScreen& g, App& a) : game(g), app(a), ui(a.ui())
{
    lastInput = app.time();
    showNames = app.settings().getBool("show.names");
}

Hud::Impl::~Impl()
{
    if(observedBox)
        observedBox->observeNewMsg(nullptr);
}

const GameWorldBase& Hud::Impl::world() const
{
    return game.viewer().getWorld();
}

const GamePlayer& Hud::Impl::player() const
{
    return world().getPlayer(game.localPlayer());
}

float Hud::Impl::alpha() const
{
    const double idle = app.time() - lastInput;
    if(sheet != SheetKind::None || ring.open || road.active || idle < 6)
        return 1.f;
    return static_cast<float>(std::max(0.45, 1.0 - (idle - 6) * 0.5));
}

Rect2 Hud::Impl::sheetRect() const
{
    const float W = static_cast<float>(app.width()), H = static_cast<float>(app.height());
    const float m = app.dp(10);
    if(landscape())
    {
        const float w = std::min(std::max(W * 0.42f, app.dp(360)), W - 2 * m);
        return {W - w - m, m, w, H - 2 * m};
    }
    float h = std::min(H * 0.58f, std::max(app.dp(420), H * 0.5f));
    if(fitH > 0)
        h = std::min(h, std::max(app.dp(150), fitH));
    return {m, H - h - m, W - 2 * m, h};
}

void Hud::Impl::openSheet(SheetKind k, MapPoint pt, bool push)
{
    if(push && sheet != SheetKind::None)
        sheetStack.push_back(sheet);
    else if(!push)
        sheetStack.clear();
    if(sheet == SheetKind::None)
    {
        sheetTime = app.time();
        if(k != SheetKind::None)
            app.audio().playUi(UiSound::Open);
    }
    if(sheet != k)
        fitH = -1;
    sheet = k;
    sheetPt = pt;
    sheetTab = 0;
    ring.open = false;
    if(tutorial)
        tutorial->onSheet(k);
}

void Hud::Impl::closeSheet()
{
    if(!sheetStack.empty())
    {
        sheet = sheetStack.back();
        sheetStack.pop_back();
        fitH = -1;
        return;
    }
    if(sheet == SheetKind::Mission && missionPaused)
    {
        game.setPaused(false);
        missionPaused = false;
    }
    if(sheet != SheetKind::None)
        app.audio().playUi(UiSound::Close);
    sheet = SheetKind::None;
}

void Hud::Impl::toast(const std::string& text, bool hasPos, MapPoint pos)
{
    // The same message again (e.g. repeated attacks) replaces the older one instead of stacking up
    toasts.erase(std::remove_if(toasts.begin(), toasts.end(), [&](const Toast& t) { return t.text == text; }),
                 toasts.end());
    toasts.push_back({text, app.time(), hasPos, pos});
    if(app.testMode())
        std::cout << "[toast] " << text << std::endl;
    while(toasts.size() > 3)
        toasts.pop_front();
}

void Hud::Impl::confirm(const std::string& text, std::function<void()> action)
{
    confirmText = text;
    confirmAction = std::move(action);
    confirmTime = app.time();
}

void Hud::Impl::noteRecent(BuildingType bt)
{
    std::vector<int> recent = app.settings().getList("recent.buildings");
    recent.erase(std::remove(recent.begin(), recent.end(), static_cast<int>(bt)), recent.end());
    recent.insert(recent.begin(), static_cast<int>(bt));
    if(recent.size() > 12)
        recent.resize(12);
    app.settings().setList("recent.buildings", recent);
}

// ---------------------------------------------------------------------------------------------------------------
// World interaction

void Hud::Impl::onTap(float x, float y)
{
    if(ring.open)
    {
        ring.open = false;
        return;
    }
    if(confirmAction)
        return;
    auto& view = game.view();
    MapPoint pt = view.pickNode(x, y);
    auto& gwv = game.viewer();
    const unsigned me = game.localPlayer();

    if(road.active)
    {
        if(pt == road.point)
            return;
        // Tap on the planned route removes the rest of it
        MapPoint cur = road.start;
        for(size_t i = 0; i < road.route.size(); ++i)
        {
            if(cur == pt)
            {
                truncateRoadAt(pt);
                return;
            }
            cur = world().getNeighbour(cur, road.route[i]);
        }
        if(extendRoadTo(pt))
        {
            const noBase* obj = world().getNO(road.point);
            if(obj && obj->getType() == NodalObjectType::Flag)
                buildRoad();
        } else
            toast("Hier kann keine Straße entlang führen");
        return;
    }

    // A ship next to a building (docked at a harbor) only wins when the building itself was not tapped
    if(!isBuildingObj(world().getNO(pt)) && tapShip(pt))
        return;
    // Buildings are tall: if the tapped node has no building, look at the nodes below for one
    if(!isBuildingObj(world().getNO(pt)))
    {
        for(Direction d1 : {Direction::SouthWest, Direction::SouthEast})
        {
            const MapPoint p1 = world().getNeighbour(pt, d1);
            if(isBuildingObj(world().getNO(p1)) && gwv.getVisibility(p1) == Visibility::Visible)
            {
                pt = p1;
                break;
            }
        }
    }
    selected = pt;
    hasSelection = true;
    if(gwv.getVisibility(pt) != Visibility::Visible)
        return;
    const noBase* obj = world().getNO(pt);
    if(isBuildingObj(obj))
    {
        const auto* bld = static_cast<const noBaseBuilding*>(obj);
        if(bld->getPlayer() == me)
        {
            openSheet(SheetKind::Building, pt);
            return;
        }
        const auto* mil = dynamic_cast<const nobBaseMilitary*>(obj);
        if(mil && mil->isAttackable(me))
        {
            attackSoldiers = std::max(1u, gwv.getNumSoldiersForAttack(pt));
            openSheet(SheetKind::Attack, pt);
            return;
        }
        toast(std::string(buildingName(bld->getBuildingType())) + " von " + world().getPlayer(bld->getPlayer()).name
              + (mil ? " · zurzeit nicht angreifbar" : ""));
        return;
    }
    openRingAt(pt);
}

bool Hud::Impl::tapShip(MapPoint pt)
{
    std::vector<MapPoint> pts{pt};
    for(const auto dir : helpers::EnumRange<Direction>{})
        pts.push_back(world().getNeighbour(pt, dir));
    for(const MapPoint p : pts)
    {
        if(game.viewer().getVisibility(p) != Visibility::Visible)
            continue;
        for(const noBase& fig : world().getFigures(p))
        {
            if(fig.getGOT() != GO_Type::Ship)
                continue;
            const auto& ship = static_cast<const noShip&>(fig);
            if(ship.getPlayerId() != game.localPlayer())
                continue;
            shipId = player().getShipID(ship);
            openSheet(SheetKind::Ship);
            return true;
        }
    }
    return false;
}

void Hud::Impl::onDoubleTap(float x, float y)
{
    // The first tap may have opened a ring or a sheet for the node: the double tap only zooms
    ring.open = false;
    if(sheet == SheetKind::Building || sheet == SheetKind::Attack || sheet == SheetKind::Ship)
        closeSheet();
    auto& view = game.view();
    view.zoomAt(x, y, std::min(8.f, view.cam.zoom * 1.6f));
}

void Hud::Impl::onLongPress(float x, float y)
{
    const MapPoint pt = game.view().pickNode(x, y);
    auto& gwv = game.viewer();
    if(gwv.getVisibility(pt) == Visibility::Invisible)
    {
        toast("Unerforschtes Gebiet");
        return;
    }
    std::string info;
    const MapNode& node = world().getNode(pt);
    const noBase* obj = world().getNO(pt);
    if(isBuildingObj(obj))
        info = buildingName(static_cast<const noBaseBuilding*>(obj)->getBuildingType());
    else if(obj && obj->getType() == NodalObjectType::Flag)
        info = "Flagge";
    else if(obj && obj->getType() == NodalObjectType::Tree)
        info = "Baum";
    else if(obj && obj->getType() == NodalObjectType::Granite)
        info = "Granit";
    else
        info = "Gelände";
    const BuildingQuality bq = gwv.getBQ(pt);
    static const char* bqNames[] = {"nicht bebaubar", "Flagge", "kleines Gebäude", "mittleres Gebäude",
                                    "großes Gebäude", "Mine",   "Hafen"};
    info += " · " + std::string(bqNames[std::min<size_t>(static_cast<size_t>(bq), 6)]);
    if(node.owner)
        info += " · Gebiet von " + world().getPlayer(node.owner - 1).name;
    info += " · Höhe " + std::to_string(node.altitude);
    toast(info);
}

void Hud::Impl::openRingAt(MapPoint pt)
{
    auto& gwv = game.viewer();
    const unsigned me = game.localPlayer();
    ring.items.clear();
    const noBase* obj = world().getNO(pt);
    const bool isFlag = obj && obj->getType() == NodalObjectType::Flag;
    if(isFlag)
    {
        if(static_cast<const noFlag*>(obj)->getPlayer() != me)
            return;
        ring.items.push_back({RingItem::Kind::Road, BuildingType::Headquarters, "Straße"});
        ring.items.push_back({RingItem::Kind::Connect, BuildingType::Headquarters, "Verbinden"});
        bool nearWater = false;
        for(const auto dir : helpers::EnumRange<Direction>{})
            nearWater |= world().isWaterPoint(world().getNeighbour(pt, dir));
        if(nearWater)
            ring.items.push_back({RingItem::Kind::WaterRoad, BuildingType::Headquarters, "Wasserweg"});
        ring.items.push_back({RingItem::Kind::Geologist, BuildingType::Headquarters, "Geologe"});
        ring.items.push_back({RingItem::Kind::Scout, BuildingType::Headquarters, "Späher"});
        ring.items.push_back({RingItem::Kind::DestroyFlag, BuildingType::Headquarters, "Abreißen"});
    } else
    {
        const BuildingQuality bq = gwv.getBQ(pt);
        if(bq == BuildingQuality::Nothing)
        {
            if(gwv.isOnRoad(pt) && world().getNode(pt).owner == me + 1)
            {
                ring.items.push_back({RingItem::Kind::DestroyRoad, BuildingType::Headquarters, "Abreißen"});
                ring.items.push_back({RingItem::Kind::UpgradeRoad, BuildingType::Headquarters, "Eselstraße"});
            } else
                return;
        } else
        {
            ring.items.push_back({RingItem::Kind::Flag, BuildingType::Headquarters, "Flagge"});
            if(bq != BuildingQuality::Flag)
            {
                const std::vector<BuildingType> fitting = fittingBuildings(*this, pt);
                // Recently used first, then the rest in a sensible default order
                std::vector<BuildingType> order;
                for(int r : app.settings().getList("recent.buildings"))
                {
                    const auto bt = static_cast<BuildingType>(r);
                    if(std::find(fitting.begin(), fitting.end(), bt) != fitting.end())
                        order.push_back(bt);
                }
                static const BuildingType defaults[] = {
                  BuildingType::Woodcutter, BuildingType::Forester,    BuildingType::Quarry,  BuildingType::Sawmill,
                  BuildingType::Barracks,   BuildingType::Guardhouse,  BuildingType::Fishery, BuildingType::Hunter,
                  BuildingType::Well,       BuildingType::Farm,        BuildingType::Mill,    BuildingType::Bakery,
                  BuildingType::CoalMine,   BuildingType::IronMine,    BuildingType::GoldMine,
                  BuildingType::GraniteMine, BuildingType::HarborBuilding};
                for(BuildingType bt : defaults)
                {
                    if(std::find(fitting.begin(), fitting.end(), bt) != fitting.end()
                       && std::find(order.begin(), order.end(), bt) == order.end())
                        order.push_back(bt);
                }
                for(BuildingType bt : fitting)
                {
                    if(std::find(order.begin(), order.end(), bt) == order.end())
                        order.push_back(bt);
                }
                const size_t maxDirect = 7;
                const bool needMore = order.size() > maxDirect;
                for(size_t i = 0; i < order.size() && i < (needMore ? maxDirect - 1 : maxDirect); ++i)
                    ring.items.push_back({RingItem::Kind::Building, order[i], buildingName(order[i])});
                if(needMore)
                    ring.items.push_back({RingItem::Kind::More, BuildingType::Headquarters, "Alle"});
            }
        }
    }
    if(ring.items.empty())
        return;
    ring.open = true;
    ring.pt = pt;
    ring.openTime = app.time();
    const PointF sp = game.view().worldToScreen(game.view().worldPosNear(pt));
    const float R = app.dp(104);
    // Keep the ring on screen (on very short screens it is simply centered)
    auto keepIn = [](float v, float lo, float hi) { return lo > hi ? (lo + hi) / 2 : std::clamp(v, lo, hi); };
    ring.sx = keepIn(sp.x, R + app.dp(40), static_cast<float>(app.width()) - R - app.dp(40));
    ring.sy = keepIn(sp.y, R + app.dp(70), static_cast<float>(app.height()) - R - app.dp(50));
    if(tutorial)
        tutorial->onRingOpened(isFlag);
}

void Hud::Impl::activateRingItem(const RingItem& item)
{
    Session& s = game.session();
    const MapPoint pt = ring.pt;
    ring.open = false;
    hasSelection = false;
    switch(item.kind)
    {
        case RingItem::Kind::Building:
            s.SetBuildingSite(pt, item.bt);
            app.audio().playUi(UiSound::Build);
            noteRecent(item.bt);
            if(app.settings().getBool("build.autoconnect", true) && !tutorial)
            {
                pendingConnect = world().getNeighbour(pt, Direction::SouthEast);
                pendingConnectTime = app.time();
            }
            if(tutorial)
                tutorial->onBuild(item.bt);
            break;
        case RingItem::Kind::Flag: s.SetFlag(pt); break;
        case RingItem::Kind::Road:
            startRoad(pt, false);
            toast("Ziehe die Straße oder tippe die Wegpunkte an");
            break;
        case RingItem::Kind::WaterRoad: startRoad(pt, true); break;
        case RingItem::Kind::Connect:
            if(!autoConnect(pt))
                toast("Keine Verbindung gefunden. Ziehe die Straße selbst.");
            break;
        case RingItem::Kind::Geologist: s.CallSpecialist(pt, Job::Geologist); break;
        case RingItem::Kind::Scout: s.CallSpecialist(pt, Job::Scout); break;
        case RingItem::Kind::DestroyFlag:
            confirm("Flagge und angeschlossene Straßen abreißen?", [&s, pt] { s.DestroyFlag(pt); });
            break;
        case RingItem::Kind::DestroyRoad:
        case RingItem::Kind::UpgradeRoad:
        {
            // Find the road direction at this point
            for(const auto dir : helpers::EnumRange<Direction>{})
            {
                if(world().getPointRoad(pt, dir) != PointRoad::None)
                {
                    if(item.kind == RingItem::Kind::DestroyRoad)
                        s.DestroyRoad(pt, dir);
                    else
                        s.UpgradeRoad(pt, dir);
                    break;
                }
            }
            break;
        }
        case RingItem::Kind::More: openSheet(SheetKind::Build, pt); break;
    }
}

bool Hud::Impl::startRoad(MapPoint flag, bool water)
{
    if(road.active)
        cancelRoad();
    road = Road{};
    road.active = true;
    road.water = water;
    road.start = road.point = flag;
    ring.open = false;
    return true;
}

bool Hud::Impl::extendRoadTo(MapPoint pt)
{
    auto& gwv = game.viewer();
    if(!gwv.isPlayerTerritory(pt) && !road.water)
        return false;
    // A road ends at the first flag it reaches
    if(road.point != road.start)
    {
        const noBase* endObj = world().getNO(road.point);
        if(endObj && endObj->getType() == NodalObjectType::Flag)
            return false;
    }
    // The path finder does not check the goal node itself
    const noBase* obj = world().getNO(pt);
    const bool ownFlag = obj && obj->getType() == NodalObjectType::Flag
                         && static_cast<const noFlag*>(obj)->getPlayer() == game.localPlayer();
    if(!ownFlag && !gwv.isRoadAvailable(road.water, pt))
        return false;
    std::vector<Direction> path = findPathForRoad(gwv, road.point, pt, road.water, 100);
    if(path.empty())
        return false;
    for(const auto dir : path)
    {
        gwv.setVisiblePointRoad(road.point, dir, road.water ? PointRoad::Boat : PointRoad::Normal);
        gwv.recalcBQForRoad(road.point);
        road.point = world().getNeighbour(road.point, dir);
    }
    gwv.recalcBQForRoad(road.point);
    road.route.insert(road.route.end(), path.begin(), path.end());
    return true;
}

void Hud::Impl::truncateRoadAt(MapPoint pt)
{
    auto& gwv = game.viewer();
    MapPoint cur = road.start;
    size_t idx = 0;
    for(; idx < road.route.size() && cur != pt; ++idx)
        cur = world().getNeighbour(cur, road.route[idx]);
    if(cur != pt)
        return;
    const std::vector<Direction> rest(road.route.begin() + static_cast<ptrdiff_t>(idx), road.route.end());
    gwv.removeVisualRoad(pt, rest);
    road.route.resize(idx);
    road.point = pt;
}

void Hud::Impl::buildRoad()
{
    if(!road.active)
        return;
    if(road.route.empty())
    {
        cancelRoad();
        return;
    }
    Session& s = game.session();
    s.BuildRoad(road.start, road.water, road.route);
    // Flags along the road (every second node), executed after the road is built
    MapPoint cur = road.start;
    for(size_t i = 0; i + 1 < road.route.size(); ++i)
    {
        cur = world().getNeighbour(cur, road.route[i]);
        if(i % 2 == 1)
            s.SetFlag(cur);
    }
    road.active = false;
    road.fingerDrawing = false;
    if(tutorial)
        tutorial->onRoadBuilt();
}

void Hud::Impl::buildingIcon(Nation nation, BuildingType bt, const Rect2& r, float alpha)
{
    if(const auto* icon = LOADER.getNationIcon(nation, bt))
        ui.sprite(icon, r, 0, alpha);
    else
        ui.icon(Icon::House, r.x + r.w / 2, r.y + r.h / 2, std::min(r.w, r.h) * 0.7f, ui.theme.text.withAlpha(alpha));
}

bool Hud::Impl::autoConnect(MapPoint flag)
{
    const noBase* obj = world().getNO(flag);
    const unsigned me = game.localPlayer();
    if(!obj || obj->getType() != NodalObjectType::Flag || static_cast<const noFlag*>(obj)->getPlayer() != me)
        return false;
    auto& gwv = game.viewer();
    // Candidates: own flags with a road or a warehouse, nearest first
    struct Cand
    {
        unsigned dist;
        MapPoint pt;
    };
    std::vector<Cand> cands;
    const int R = 20;
    for(int dy = -R; dy <= R; ++dy)
    {
        for(int dx = -R; dx <= R; ++dx)
        {
            const MapPoint pt = world().makeMapPoint(Position(flag.x + dx, flag.y + dy));
            if(pt == flag)
                continue;
            const auto* f = world().getSpecObj<noFlag>(pt);
            if(!f || f->getPlayer() != me)
                continue;
            bool connected = false;
            for(const auto dir : helpers::EnumRange<Direction>{})
            {
                if(dir != Direction::NorthWest && f->getRoute(dir))
                    connected = true;
            }
            const auto* bld = world().getSpecObj<noBaseBuilding>(world().getNeighbour(pt, Direction::NorthWest));
            if(bld && BuildingProperties::isWareHouse(bld->getBuildingType()))
                connected = true;
            if(connected)
                cands.push_back({world().calcDistance(flag, pt), pt});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.dist < b.dist; });
    std::vector<Direction> best;
    for(const Cand& c : cands)
    {
        if(!best.empty() && c.dist >= best.size())
            break;
        const unsigned maxLen = best.empty() ? c.dist * 2 + 6 : static_cast<unsigned>(best.size()) - 1;
        std::vector<Direction> path = findPathForRoad(gwv, flag, c.pt, false, maxLen);
        if(!path.empty() && (best.empty() || path.size() < best.size()))
            best = std::move(path);
    }
    if(best.empty())
        return false;
    startRoad(flag, false);
    for(const auto dir : best)
    {
        gwv.setVisiblePointRoad(road.point, dir, PointRoad::Normal);
        gwv.recalcBQForRoad(road.point);
        road.point = world().getNeighbour(road.point, dir);
    }
    gwv.recalcBQForRoad(road.point);
    road.route = best;
    buildRoad();
    return true;
}

void Hud::Impl::cancelRoad()
{
    if(!road.active)
        return;
    game.viewer().removeVisualRoad(road.start, road.route);
    road.active = false;
    road.fingerDrawing = false;
    road.route.clear();
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing

void Hud::Impl::drawWorldOverlay(MapPoint pt, PointF pos)
{
    auto& b = app.batch();
    auto& gwv = game.viewer();
    const float a = 1.f;
    if(showNames)
    {
        const noBase* obj = world().getNO(pt);
        if(isBuildingObj(obj) && static_cast<const noBaseBuilding*>(obj)->getPlayer() == game.localPlayer())
        {
            const auto* bld = static_cast<const noBaseBuilding*>(obj);
            int prod = -1;
            std::string text = buildingName(bld->getBuildingType());
            if(obj->getType() == NodalObjectType::Buildingsite)
            {
                prod = static_cast<int>(static_cast<const noBuildingSite*>(obj)->getBuildProgress());
                text = "Baustelle";
            } else if(const auto* usual = dynamic_cast<const nobUsual*>(obj))
                prod = usual->hasWorker() ? static_cast<int>(usual->getProductivity()) : -1;
            nameLabels.push_back({text, pos, prod});
        }
    }
    if(showBQ || road.active)
    {
        const BuildingQuality bq = gwv.getBQ(pt);
        if(bq != BuildingQuality::Nothing && gwv.getVisibility(pt) == Visibility::Visible)
        {
            const Color fill = ui.theme.accent.withAlpha(0.9f * a);
            const Color dark = Color::rgb(0x000000, 0.45f * a);
            switch(bq)
            {
                case BuildingQuality::Flag:
                    b.circle(pos.x, pos.y - 2, 3.2f, dark);
                    b.circle(pos.x, pos.y - 2, 2.2f, fill);
                    break;
                case BuildingQuality::Hut:
                case BuildingQuality::House:
                case BuildingQuality::Castle:
                {
                    const float sz = bq == BuildingQuality::Hut ? 9 : (bq == BuildingQuality::House ? 12 : 15);
                    b.circle(pos.x, pos.y - sz / 2, sz * 0.62f, dark);
                    app.icons().draw(b, Icon::House, pos.x, pos.y - sz / 2, sz, fill);
                    break;
                }
                case BuildingQuality::Mine:
                    b.circle(pos.x, pos.y - 6, 7.5f, dark);
                    app.icons().draw(b, Icon::Pick, pos.x, pos.y - 6, 12, fill);
                    break;
                case BuildingQuality::Harbor:
                    b.circle(pos.x, pos.y - 7, 9, dark);
                    app.icons().draw(b, Icon::Ship, pos.x, pos.y - 7, 14, fill);
                    break;
                default: break;
            }
        }
    }
    if(road.active)
    {
        if(pt == road.point)
        {
            b.circle(pos.x, pos.y, 7, ui.theme.accent, 2.f);
        } else
        {
            for(const auto dir : helpers::EnumRange<Direction>{})
            {
                if(world().getNeighbour(road.point, dir) == pt && gwv.isRoadAvailable(road.water, pt)
                   && (road.water || gwv.isPlayerTerritory(pt)))
                    b.circle(pos.x, pos.y, 4, Color::rgb(0xFFFFFF, 0.85f));
            }
        }
    }
    if(hasSelection && pt == selected && !road.active && sheet != SheetKind::None)
        b.circle(pos.x, pos.y, 6, Color::rgb(0xFFFFFF, 0.9f), 1.5f);
    if(ring.open && pt == ring.pt)
        b.circle(pos.x, pos.y, 6, ui.theme.accent, 2.f);
}

void Hud::Impl::drawNameLabels()
{
    auto& b = app.batch();
    auto& view = game.view();
    // Small pills below the buildings; hidden when zoomed out far (would only be clutter)
    if(view.cam.zoom < app.uiScale() * 0.6f)
        return;
    const float fs = app.dp(10.5f);
    for(const NameLabel& l : nameLabels)
    {
        const PointF sp = view.worldToScreen(l.worldPos);
        const float tw = ui.textWidth(l.text, fs, true);
        const float barW = l.productivity >= 0 ? app.dp(28) : 0;
        const float w = tw + app.dp(12) + (barW > 0 ? barW + app.dp(6) : 0), h = fs * 1.5f;
        const Rect2 r{sp.x - w / 2, sp.y + app.dp(4), w, h};
        b.roundRect(r, h / 2, ui.theme.panelSolid.withAlpha(0.72f));
        b.roundRect(r, h / 2, ui.theme.line, 1.f, app.dp(1));
        ui.text(r.x + app.dp(6), r.y + (h - fs * 1.3f) / 2, l.text, fs, ui.theme.text, gfx::Align::Left, true);
        if(barW > 0)
        {
            const Rect2 bar{r.right() - barW - app.dp(6), r.y + h / 2 - app.dp(2.5f), barW, app.dp(5)};
            const float f = std::clamp(static_cast<float>(l.productivity) / 100.f, 0.f, 1.f);
            b.roundRect(bar, app.dp(2.5f), ui.theme.text.withAlpha(0.15f));
            b.roundRect({bar.x, bar.y, bar.w * f, bar.h}, app.dp(2.5f),
                        l.productivity >= 60 ? ui.theme.ok : l.productivity >= 30 ? ui.theme.accent : ui.theme.danger);
        }
    }
}

void Hud::Impl::drawRing()
{
    if(!ring.open)
        return;
    auto& b = app.batch();
    const auto& th = ui.theme;
    const float age = static_cast<float>(app.time() - ring.openTime);
    const float t = Ui::ease(std::min(1.f, age / th.motion.normal));
    const size_t n = ring.items.size();
    const float R0 = app.dp(n > 6 ? 112 : 100);
    const float btn = app.dp(60);
    // Soft dark halo behind the ring keeps it readable on any terrain
    b.circle(ring.sx, ring.sy, (R0 + btn * 0.8f) * (0.7f + 0.3f * t), th.shadow.withAlpha(0.55f * t), 0, app.dp(46));
    const Nation nation = player().nation;
    for(size_t i = 0; i < n; ++i)
    {
        const RingItem& item = ring.items[i];
        // Items spring out one after another
        const float ti = std::clamp((age - 0.018f * static_cast<float>(i)) / th.motion.slow, 0.f, 1.f);
        const float k = Ui::easeBack(ti);
        const float R = R0 * (0.35f + 0.65f * k);
        const float alphaI = std::min(1.f, ti * 2.5f);
        const float ang = -1.5708f + 6.2832f * static_cast<float>(i) / static_cast<float>(n);
        const float cx = ring.sx + std::cos(ang) * R, cy = ring.sy + std::sin(ang) * R;
        const Rect2 hitR{cx - btn / 2, cy - btn / 2, btn, btn};
        const float p = ui.anim(id("ringp", i), ui.pressed(id("ring", i), hitR) ? 1.f : 0.f, th.motion.fast);
        const float bs = btn * (0.6f + 0.4f * std::min(k, 1.08f)) * (1.f - 0.07f * p);
        const Rect2 r{cx - bs / 2, cy - bs / 2, bs, bs};
        ui.elevate(r, bs / 2, th.low, alphaI);
        b.glass(r, bs / 2, th.glassStrong, alphaI);
        b.roundRectV(r, bs / 2, th.highlight.withAlpha(th.highlight.a * alphaI), th.line.withAlpha(0.03f * alphaI), 1.f,
                     app.dp(1));
        if(p > 0.01f)
            b.circle(cx, cy, bs / 2, th.surfacePressed.withAlpha(th.surfacePressed.a * p));
        if(item.kind == RingItem::Kind::Building)
            buildingIcon(nation, item.bt, r.inset(bs * 0.15f), alphaI);
        else
        {
            Icon ic = Icon::Info;
            switch(item.kind)
            {
                case RingItem::Kind::Flag: ic = Icon::Flag; break;
                case RingItem::Kind::Road: ic = Icon::Road; break;
                case RingItem::Kind::Connect: ic = Icon::Target; break;
                case RingItem::Kind::WaterRoad: ic = Icon::Ship; break;
                case RingItem::Kind::Geologist: ic = Icon::Pick; break;
                case RingItem::Kind::Scout: ic = Icon::Eye; break;
                case RingItem::Kind::DestroyFlag:
                case RingItem::Kind::DestroyRoad: ic = Icon::Trash; break;
                case RingItem::Kind::UpgradeRoad: ic = Icon::Up; break;
                case RingItem::Kind::More: ic = Icon::Grid; break;
                default: break;
            }
            const bool destroy = item.kind == RingItem::Kind::DestroyFlag || item.kind == RingItem::Kind::DestroyRoad;
            ui.icon(ic, cx, cy, bs * 0.42f, (destroy ? th.danger : th.text).withAlpha(alphaI));
        }
        // Label
        const float fs = app.dp(11.5f);
        const float tw = ui.textWidth(item.label, fs, true);
        const Rect2 lr{cx - tw / 2 - app.dp(7), cy + btn / 2 + app.dp(3), tw + app.dp(14), fs * 1.5f};
        b.roundRect(lr, lr.h / 2, th.panelSolid.withAlpha(0.82f * alphaI));
        ui.text(cx, lr.y + (lr.h - fs * 1.3f) / 2, item.label, fs, th.text.withAlpha(alphaI), gfx::Align::Center, true);
        ui.tag("ring:" + item.label, hitR);
        if(ui.area(id("ring", i), hitR))
        {
            activateRingItem(item);
            return;
        }
    }
    // Center: close
    const float cb = app.dp(42) * (0.5f + 0.5f * t);
    if(ui.iconButton(id("ringclose"), {ring.sx - cb / 2, ring.sy - cb / 2, cb, cb}, Icon::Close))
    {
        // A quick second tap on the node (the ring opened under the finger) is a double tap: zoom there
        if(app.time() - ring.openTime < 0.35)
        {
            const PointF sp = game.view().worldToScreen(game.view().worldPosNear(ring.pt));
            onDoubleTap(sp.x, sp.y);
        }
        ring.open = false;
    }
}

void Hud::Impl::drawTopBar()
{
    const float a = alpha();
    const float m = app.dp(10);
    const float s = app.dp(48);
    const float top = m;
    auto& b = app.batch();
    (void)b;
    if(ui.iconButton(id("menu"), {m, top, s, s}, Icon::Menu))
        openSheet(sheet == SheetKind::GameMenu ? SheetKind::None : SheetKind::GameMenu);
    // Clock / speed pill
    const std::string clock = game.session().formatGFTime(game.session().getGF());
    const std::string speed = game.paused() ? "Pause" : std::to_string(game.speedFactor()) + "×";
    // In landscape the side panel takes the right edge
    const float W = landscape() && sheet != SheetKind::None ? sheetRect().x : static_cast<float>(app.width());
    const float pillW = app.dp(116) + s * 3;
    const Rect2 pill{W - m - pillW, top, pillW, s};
    ui.panel(pill, s / 2, ui.theme.glass, a);
    ui.text(pill.x + app.dp(20), pill.y + app.dp(6), clock, app.dp(16), ui.theme.text.withAlpha(a), gfx::Align::Left,
            true);
    ui.text(pill.x + app.dp(20), pill.y + app.dp(26), speed, app.dp(12),
            (game.paused() ? ui.theme.accent : ui.theme.textDim).withAlpha(a), gfx::Align::Left, game.paused());
    float bx = pill.right() - s * 3;
    if(ui.iconButton(id("speed"), {bx, top, s, s}, Icon::Fast, game.speedFactor() > 1, 0, Style::Flat))
        game.setSpeedFactor(game.speedFactor() >= 8 ? 1 : game.speedFactor() * 2);
    bx += s;
    if(ui.iconButton(id("pause"), {bx, top, s, s}, game.paused() ? Icon::Play : Icon::Pause, game.paused(), 0,
                     Style::Flat))
        game.setPaused(!game.paused());
    bx += s;
    if(ui.iconButton(id("mail"), {bx, top, s, s}, Icon::Mail, false, std::max(0, msgBadge), Style::Flat))
    {
        openSheet(SheetKind::Messages);
        msgBadge = 0;
    }
}

void Hud::Impl::drawTools()
{
    const float s = app.dp(52);
    const float m = app.dp(10);
    const float W = static_cast<float>(app.width()), H = static_cast<float>(app.height());
    float x = W - m - s;
    float y = landscape() ? H / 2 - s * 2 - app.dp(12) : H * 0.30f;
    const float gap = app.dp(10);
    if(landscape() && sheet != SheetKind::None)
        x = sheetRect().x - m - s;
    else if(!landscape() && sheet != SheetKind::None)
    {
        // Keep the column above the bottom sheet
        y = std::min(y, sheetRect().y - m - 4 * s - 3 * gap);
        if(y < app.dp(68))
            return;
    }
    if(ui.iconButton(id("tool.bq"), {x, y, s, s}, Icon::Grid, showBQ))
    {
        showBQ = !showBQ;
        if(tutorial)
            tutorial->onBQToggled(showBQ);
    }
    y += s + gap;
    if(ui.iconButton(id("tool.road"), {x, y, s, s}, Icon::Road, road.active))
    {
        if(road.active)
            cancelRoad();
        else
        {
            const noBase* obj = hasSelection ? world().getNO(selected) : nullptr;
            if(obj && obj->getType() == NodalObjectType::Flag
               && static_cast<const noFlag*>(obj)->getPlayer() == game.localPlayer())
                startRoad(selected, false);
            else
                toast("Ziehe mit dem Finger von einer Flagge aus, um eine Straße zu bauen");
        }
    }
    y += s + gap;
    if(ui.iconButton(id("tool.map"), {x, y, s, s}, Icon::Map, showMinimap))
        showMinimap = !showMinimap;
    y += s + gap;
    if(ui.iconButton(id("tool.home"), {x, y, s, s}, Icon::Home))
    {
        // Cycle: headquarters / warehouses / last message
        std::vector<MapPoint> targets;
        for(const auto* wh : player().getBuildingRegister().getStorehouses())
            targets.push_back(wh->getPos());
        if(hasLastMessagePos)
            targets.push_back(lastMessagePos);
        if(targets.empty())
            targets.push_back(player().getHQPos());
        jumpIndex = (jumpIndex + 1) % targets.size();
        game.jumpTo(targets[jumpIndex]);
    }
}

void Hud::Impl::drawRoadBar()
{
    if(!road.active)
        return;
    const float W = static_cast<float>(app.width()), H = static_cast<float>(app.height());
    const float h = app.dp(64), w = std::min(W - app.dp(20), app.dp(420));
    const Rect2 r{(W - w) / 2, H - h - app.dp(16), w, h};
    ui.panel(r, h / 2);
    const float bs = h - app.dp(12);
    ui.text(r.x + app.dp(22), r.y + app.dp(10), road.water ? "Wasserweg" : "Straße", app.dp(17), ui.theme.text,
            gfx::Align::Left, true);
    ui.text(r.x + app.dp(22), r.y + app.dp(33), std::to_string(road.route.size()) + " Abschnitte", app.dp(13),
            ui.theme.textDim);
    if(ui.iconButton(id("road.cancel"), {r.right() - bs * 2 - app.dp(16), r.y + app.dp(6), bs, bs}, Icon::Close))
        cancelRoad();
    if(ui.iconButton(id("road.ok"), {r.right() - bs - app.dp(6), r.y + app.dp(6), bs, bs}, Icon::Check, true))
        buildRoad();
}

void Hud::Impl::drawMinimapPanel()
{
    if(!showMinimap)
        return;
    const float m = app.dp(10);
    const float W = static_cast<float>(app.width());
    // Left of the tool column, below the top bar
    const float right = (landscape() && sheet != SheetKind::None ? sheetRect().x : W) - m - app.dp(52) - m;
    const float w = std::min(right - m, app.dp(landscape() ? 300 : 340));
    const float h = std::min(w * 0.62f, static_cast<float>(app.height()) - app.dp(86));
    const Rect2 panelR{right - w, m + app.dp(58), w, h};
    ui.panel(panelR, app.dp(ui.theme.radius.lg));
    const Rect2 inner = panelR.inset(app.dp(8));
    auto& b = app.batch();
    const Rect2 mr = game.minimap().draw(b, inner);
    // View rectangle
    auto& view = game.view();
    const float sx = mr.w / view.mapWidthPx(), sy = mr.h / view.mapHeightPx();
    const Rect2 vr{mr.x + view.cam.x * sx, mr.y + view.cam.y * sy, view.cam.viewW() * sx, view.cam.viewH() * sy};
    app.batch().setClip(mr);
    b.roundRect(vr, app.dp(2), Color::rgb(0xFFFFFF, 0.9f), 1.f, app.dp(1.5f));
    app.batch().setClip({});
    if(ui.area(id("minimap"), mr) || (ui.hasCapture() && ui.pressed(id("minimap"), mr)))
    {
        // Jump: use the press position
    }
    if(minimapTap)
    {
        minimapTap = false;
        if(mr.contains(minimapTapX, minimapTapY))
        {
            const float fx = (minimapTapX - mr.x) / mr.w, fy = (minimapTapY - mr.y) / mr.h;
            view.cam.x = fx * view.mapWidthPx() - view.cam.viewW() / 2;
            view.cam.y = fy * view.mapHeightPx() - view.cam.viewH() / 2;
            view.normalize();
        }
    }
    minimapRect = mr;
}

void Hud::Impl::drawToasts()
{
    const double now = app.time();
    while(!toasts.empty() && now - toasts.front().time > 5)
        toasts.pop_front();
    const float W = static_cast<float>(app.width());
    float y = (sheet != SheetKind::None && !landscape()) ? sheetRect().y - app.dp(12) :
                                                           static_cast<float>(app.height()) - app.dp(road.active ? 96 : 24);
    const float fs = app.dp(14);
    for(auto it = toasts.rbegin(); it != toasts.rend(); ++it)
    {
        const float age = static_cast<float>(now - it->time);
        const float a = std::min(1.f, age / 0.15f) * std::min(1.f, (5.f - age) / 0.5f);
        const float maxW = std::min(W - app.dp(40), app.dp(520));
        const auto lines = app.font().wrap(it->text, fs, maxW - app.dp(28));
        const float h = static_cast<float>(lines.size()) * fs * 1.3f + app.dp(18);
        float w = 0;
        for(const auto& l : lines)
            w = std::max(w, ui.textWidth(l, fs));
        w += app.dp(28);
        y -= h;
        const Rect2 r{(W - w) / 2, y, w, h};
        ui.panel(r, app.dp(ui.theme.radius.lg), ui.theme.glassStrong, a);
        float ty = r.y + app.dp(9);
        for(const auto& l : lines)
        {
            ui.text(r.x + app.dp(14), ty, l, fs, ui.theme.text.withAlpha(a));
            ty += fs * 1.3f;
        }
        if(it->hasPos && ui.area(id("toast", static_cast<uint64_t>(it->time * 1000)), r))
            game.jumpTo(it->pos);
        y -= app.dp(8);
    }
}

void Hud::Impl::checkMessages()
{
    PostBox* box = world().getPostMgr().getPostBox(game.localPlayer());
    if(!box || observedBox == box)
        return;
    observedBox = box;
    box->observeNewMsg([this](const PostMsg& msg, unsigned) {
        const bool hasPos = msg.getPos().isValid();
        toast(msg.getText(), hasPos, msg.getPos());
        app.audio().playUi(UiSound::Notify, 0.8f);
        if(hasPos)
        {
            lastMessagePos = msg.getPos();
            hasLastMessagePos = true;
        }
        ++msgBadge;
    });
}

// ---------------------------------------------------------------------------------------------------------------
// Hud

Hud::Hud(GameScreen& game, App& app) : d(std::make_unique<Impl>(game, app))
{
    game.view().nodeOverlay = [this](MapPoint pt, PointF pos) { d->drawWorldOverlay(pt, pos); };
}

Hud::~Hud() = default;

void Hud::startTutorial(bool firstStart)
{
    d->tutorial = std::make_unique<Tutorial>(*d);
    d->tutorial->setFirstStart(firstStart);
}

void Hud::showLuaMessage(const LuaMessage& msg)
{
    d->missionTitle = msg.title;
    d->missionText = reflowText(msg.text);
    d->missionImageFile.clear();
    d->missionImageIdx = -1;
    if(msg.isMissionStatement && msg.helpImage != 0)
    {
        d->missionImageFile = "io";
        d->missionImageIdx = static_cast<int>(msg.helpImage);
    } else if(!msg.iconFile.empty() && msg.iconIdx >= 0)
    {
        d->missionImageFile = msg.iconFile;
        d->missionImageIdx = msg.iconIdx;
    }
    d->missionPaused = msg.pause && !d->game.paused();
    if(d->missionPaused)
        d->game.setPaused(true);
    d->openSheet(SheetKind::Mission);
}

void Hud::update(double dt)
{
    auto& view = d->game.view();
    if(d->fingers.empty() && (std::abs(d->velX) > 1 || std::abs(d->velY) > 1))
    {
        view.moveBy(d->velX * static_cast<float>(dt), d->velY * static_cast<float>(dt));
        const float decay = std::exp(-5.f * static_cast<float>(dt));
        d->velX *= decay;
        d->velY *= decay;
    }
    // Long press on the world
    if(d->fingers.size() == 1 && !d->gestureMoved && !d->longPressDone && !d->road.fingerDrawing)
    {
        const auto& f = d->fingers.begin()->second;
        if(d->app.time() - f.t0 > 0.55)
        {
            d->longPressDone = true;
            d->onLongPress(f.x, f.y);
        }
    }
    d->checkMessages();
    if(d->pendingConnect.isValid())
    {
        const noBase* obj = d->world().getNO(d->pendingConnect);
        if(obj && obj->getType() == NodalObjectType::Flag)
        {
            const auto* f = static_cast<const noFlag*>(obj);
            bool hasRoad = false;
            for(const auto dir : helpers::EnumRange<Direction>{})
                hasRoad |= dir != Direction::NorthWest && f->getRoute(dir);
            if(!hasRoad && !d->road.active && !d->autoConnect(d->pendingConnect))
                d->toast("Keine Straßenverbindung gefunden. Ziehe sie von der Flagge aus.", true, d->pendingConnect);
            d->pendingConnect = MapPoint::invalid();
        } else if(d->app.time() - d->pendingConnectTime > 3)
            d->pendingConnect = MapPoint::invalid();
    }
    if(d->tutorial)
        d->tutorial->update();
    // Only draw node overlays when needed (saves time)
    const bool needOverlay = d->showBQ || d->showNames || d->road.active || (d->hasSelection && d->sheet != SheetKind::None) || d->ring.open;
    if(needOverlay && !view.nodeOverlay)
        view.nodeOverlay = [this](MapPoint pt, PointF pos) { d->drawWorldOverlay(pt, pos); };
    else if(!needOverlay && view.nodeOverlay)
        view.nodeOverlay = nullptr;
}

void Hud::draw()
{
    Impl& s = *d;
    s.drawNameLabels();
    s.nameLabels.clear();
    s.drawTopBar();
    s.drawTools();
    s.drawMinimapPanel();
    s.drawRoadBar();
    s.drawSheet();
    s.drawRing();
    s.drawToasts();
    if(s.tutorial)
        s.tutorial->draw();
    // FPS counter
    if(s.app.settings().getBool("show.fps"))
    {
        char buf[48];
        std::snprintf(buf, sizeof(buf), "%d fps · %.1f ms", static_cast<int>(std::lround(s.app.fps())),
                      static_cast<double>(s.app.frameMs()));
        s.ui.text(s.app.dp(12), s.app.dp(64), buf, s.app.dp(11), s.ui.theme.textDim.withAlpha(0.9f));
    }
}

void Hud::onPointer(const PointerEvent& ev)
{
    Impl& s = *d;
    s.lastInput = s.app.time();
    auto& view = s.game.view();
    if(ev.isMouse)
    {
        s.mouseX = ev.x;
        s.mouseY = ev.y;
        if(ev.type == PointerEvent::Type::Move && ev.button == 0 && s.fingers.empty())
            return;
    }
    // Minimap taps are handled here because the minimap is drawn after the event
    if(ev.type == PointerEvent::Type::Up && s.showMinimap && s.minimapRect.contains(ev.x, ev.y)
       && s.minimapRect.contains(s.minimapDownX, s.minimapDownY))
    {
        s.minimapTap = true;
        s.minimapTapX = ev.x;
        s.minimapTapY = ev.y;
    }
    if(ev.type == PointerEvent::Type::Down)
    {
        s.minimapDownX = ev.x;
        s.minimapDownY = ev.y;
    }
    if(s.ui.pointer(ev))
        return;
    switch(ev.type)
    {
        case PointerEvent::Type::Down:
        {
            s.fingers[ev.id] = {ev.x, ev.y, ev.x, ev.y, s.app.time()};
            s.velX = s.velY = 0;
            if(s.fingers.size() == 1)
            {
                s.gestureMoved = false;
                s.gestureMulti = false;
                s.longPressDone = false;
                s.primaryId = ev.id;
                // Road drawing starts on the current road end or on an own flag
                const MapPoint pt = view.pickNode(ev.x, ev.y);
                const PointF sp = view.worldToScreen(view.worldPosNear(pt));
                const bool nearNode = std::hypot(sp.x - ev.x, sp.y - ev.y) < s.app.dp(30);
                const noBase* obj = s.world().getNO(pt);
                if(nearNode && s.road.active && pt == s.road.point)
                    s.road.fingerDrawing = true;
                else if(nearNode && !s.road.active && !s.ring.open && obj && obj->getType() == NodalObjectType::Flag
                        && static_cast<const noFlag*>(obj)->getPlayer() == s.game.localPlayer())
                {
                    s.startRoad(pt, false);
                    s.road.fingerDrawing = true;
                    s.roadFromDrag = true;
                }
            } else
            {
                s.gestureMulti = true;
                if(s.road.fingerDrawing)
                {
                    s.road.fingerDrawing = false;
                    if(s.roadFromDrag && s.road.route.empty())
                        s.cancelRoad();
                }
                auto it = s.fingers.begin();
                const auto& a = it->second;
                const auto& c = (++it)->second;
                s.pinchDist0 = std::hypot(a.x - c.x, a.y - c.y);
                s.pinchZoom0 = view.cam.zoom;
            }
            break;
        }
        case PointerEvent::Type::Move:
        {
            auto it = s.fingers.find(ev.id);
            if(it == s.fingers.end())
                break;
            const float dx = ev.x - it->second.x, dy = ev.y - it->second.y;
            it->second.x = ev.x;
            it->second.y = ev.y;
            if(std::hypot(ev.x - it->second.x0, ev.y - it->second.y0) > s.app.dp(10))
                s.gestureMoved = true;
            if(s.road.fingerDrawing)
            {
                const MapPoint pt = view.pickNode(ev.x, ev.y);
                if(pt != s.road.point)
                {
                    MapPoint cur = s.road.start;
                    bool onRoute = false;
                    for(size_t i = 0; i < s.road.route.size(); ++i)
                    {
                        if(cur == pt)
                        {
                            onRoute = true;
                            break;
                        }
                        cur = s.world().getNeighbour(cur, s.road.route[i]);
                    }
                    if(onRoute)
                        s.truncateRoadAt(pt);
                    else
                        s.extendRoadTo(pt);
                }
                break;
            }
            if(s.fingers.size() == 1 && !s.gestureMulti)
            {
                if(s.gestureMoved)
                {
                    view.moveBy(dx, dy);
                    s.velX = dx * 60.f;
                    s.velY = dy * 60.f;
                    if(s.tutorial)
                        s.tutorial->onPan();
                }
            } else if(s.fingers.size() == 2)
            {
                auto f = s.fingers.begin();
                const auto& a = f->second;
                const auto& c = (++f)->second;
                const float dist = std::hypot(a.x - c.x, a.y - c.y);
                if(s.pinchDist0 > 1)
                {
                    const float z = std::clamp(s.pinchZoom0 * dist / s.pinchDist0, 0.3f, 8.f);
                    if(std::abs(z - view.cam.zoom) > 0.001f && s.tutorial)
                        s.tutorial->onZoom();
                    view.zoomAt((a.x + c.x) / 2, (a.y + c.y) / 2, z);
                    view.moveBy(dx / 2, dy / 2);
                }
            }
            break;
        }
        case PointerEvent::Type::Up:
        case PointerEvent::Type::Cancel:
        {
            auto it = s.fingers.find(ev.id);
            if(it == s.fingers.end())
                break;
            const Impl::Finger f = it->second;
            s.fingers.erase(it);
            if(s.road.fingerDrawing && ev.id == s.primaryId)
            {
                s.road.fingerDrawing = false;
                if(s.road.route.empty())
                {
                    // Just a tap on the flag: open its menu instead
                    s.cancelRoad();
                    if(s.roadFromDrag && !s.gestureMoved)
                        s.onTap(f.x, f.y);
                } else
                {
                    const noBase* obj = s.world().getNO(s.road.point);
                    if(obj && obj->getType() == NodalObjectType::Flag && s.road.point != s.road.start)
                        s.buildRoad();
                }
                s.roadFromDrag = false;
                break;
            }
            if(!s.fingers.empty())
            {
                s.velX = s.velY = 0;
                break;
            }
            if(ev.type == PointerEvent::Type::Up && !s.gestureMoved && !s.gestureMulti && !s.longPressDone)
            {
                const double now = s.app.time();
                if(now - s.lastTapTime < 0.3 && std::hypot(ev.x - s.lastTapX, ev.y - s.lastTapY) < s.app.dp(30))
                {
                    s.onDoubleTap(ev.x, ev.y);
                    s.lastTapTime = -10;
                } else
                {
                    s.onTap(ev.x, ev.y);
                    s.lastTapTime = now;
                    s.lastTapX = ev.x;
                    s.lastTapY = ev.y;
                }
            }
            break;
        }
    }
}

void Hud::onWheel(float x, float y, float delta)
{
    d->lastInput = d->app.time();
    if(d->ui.wheel(x, y, delta))
        return;
    auto& view = d->game.view();
    view.zoomAt(x, y, std::clamp(view.cam.zoom * std::pow(1.15f, delta), 0.3f, 8.f));
    if(d->tutorial)
        d->tutorial->onZoom();
}

void Hud::onKey(SDL_Keycode key, bool down)
{
    if(!down)
        return;
    Impl& s = *d;
    s.lastInput = s.app.time();
    switch(key)
    {
        case SDLK_SPACE: s.game.setPaused(!s.game.paused()); break;
        case SDLK_PLUS:
        case SDLK_KP_PLUS:
        case SDLK_EQUALS: s.game.setSpeedFactor(s.game.speedFactor() * 2); break;
        case SDLK_MINUS:
        case SDLK_KP_MINUS: s.game.setSpeedFactor(s.game.speedFactor() / 2); break;
        case SDLK_F5: s.toast(s.game.quickSave() ? "Schnellgespeichert" : "Speichern fehlgeschlagen"); break;
        case SDLK_F9:
            if(!s.game.quickLoad())
                s.toast("Kein Schnellspeicherstand vorhanden");
            break;
        case SDLK_B: s.showBQ = !s.showBQ; break;
        case SDLK_P:
            s.showNames = !s.showNames;
            s.app.settings().setBool("show.names", s.showNames);
            break;
        case SDLK_M: s.showMinimap = !s.showMinimap; break;
        case SDLK_H: s.game.jumpTo(s.player().getHQPos()); break;
        case SDLK_N: s.openSheet(SheetKind::Messages); break;
        default: break;
    }
}

bool Hud::onBack()
{
    Impl& s = *d;
    s.lastInput = s.app.time();
    if(s.tutorial)
        s.tutorial->onBack();
    if(s.confirmAction)
    {
        s.confirmAction = nullptr;
        return true;
    }
    if(s.ring.open)
    {
        s.ring.open = false;
        return true;
    }
    if(s.road.active)
    {
        s.cancelRoad();
        return true;
    }
    if(s.sheet != SheetKind::None)
    {
        s.closeSheet();
        return true;
    }
    if(s.showMinimap)
    {
        s.showMinimap = false;
        return true;
    }
    if(s.hasSelection)
    {
        s.hasSelection = false;
        return true;
    }
    s.openSheet(SheetKind::GameMenu);
    return true;
}

void Hud::notifyDefeated(unsigned playerId)
{
    if(playerId == d->game.localPlayer())
    {
        d->gameOverText = "Dein Reich ist gefallen.";
        d->gameOverWin = false;
        d->openSheet(SheetKind::GameOver);
    } else
        d->toast(d->world().getPlayer(playerId).name + " wurde besiegt");
}

void Hud::notifyWinner(unsigned playerId, bool team)
{
    const bool me = playerId == d->game.localPlayer()
                    || (team && d->world().getPlayer(playerId).isAlly(d->game.localPlayer()));
    d->gameOverText = me ? "Das Land gehört dir." : d->world().getPlayer(playerId).name + " hat gewonnen.";
    d->gameOverWin = me;
    d->openSheet(SheetKind::GameOver);
}

void Hud::flagDestroyed(MapPoint pt)
{
    if(d->road.active && (d->road.start == pt || d->road.point == pt))
        d->cancelRoad();
    if(d->ring.open && d->ring.pt == pt)
        d->ring.open = false;
}

void Hud::startRoad(MapPoint start, bool water)
{
    d->startRoad(start, water);
}

void Hud::cancelRoad()
{
    d->cancelRoad();
}

} // namespace weiler
