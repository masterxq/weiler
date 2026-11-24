// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GameScreen.h"
#include "Perf.h"
#include "App.h"
#include "Music.h"
#include "Cheats.h"
#include "Game.h"
#include "GamePlayer.h"
#include "Hud.h"
#include "nodeObjs/noShip.h"
#include "buildings/nobHarborBuilding.h"
#include "factories/BuildingFactory.h"
#include "helpers/EnumRange.h"
#include "postSystem/PostManager.h"
#include "addons/const_addons.h"
#include "gameData/MapConsts.h"
#include "gameData/TerrainDesc.h"
#include "gameData/WorldDescription.h"
#include <iostream>
#include <map>
#include "gameTypes/BuildingQuality.h"
#include "gameData/BuildingProperties.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobUsual.h"
#include "buildings/noBuildingSite.h"
#include "Names.h"
#include "Loader.h"
#include "RttrConfig.h"
#include "ViewState.h"
#include "files.h"
#include "gameData/GameLoader.h"
#include "ogl/glAllocator.h"
#include "libsiedler2/libsiedler2.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include "gameData/GameConsts.h"
#include "s25util/Log.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <cmath>

namespace bfs = boost::filesystem;

namespace weiler {

bool ensureStartFilesLoaded()
{
    static bool loaded = false;
    if(!loaded)
    {
        libsiedler2::setAllocator(new GlAllocator());
        LOADER.initResourceFolders();
        loaded = LOADER.loadFilesAtStart();
    }
    return loaded;
}

GameScreen::GameScreen(App& app, std::unique_ptr<Session> session)
    : Screen(app), session_(std::move(session)), view_(app.batch(), app.atlas())
{
    GameLoader loader(LOADER, session_->gamePtr());
    if(!loader.load())
        throw std::runtime_error("Could not load the graphics of the game");
    app.atlas().preloadGameSprites();
    GameWorld& world = session_->world();
    gwv_ = std::make_unique<GameWorldViewer>(session_->getPlayerId(), world);
    // Post messages of the local player (notifications, missions)
    if(!world.getPostMgr().getPostBox(session_->getPlayerId()))
        world.getPostMgr().addPostBox(session_->getPlayerId());
    cheats_ = std::make_unique<Cheats>(world, *session_);
    session_->setGameInterface(this);
    view_.cam.screenW = app.width();
    view_.cam.screenH = app.height();
    view_.cam.zoom = std::clamp(app.uiScale(), 1.f, 6.f);
    view_.setViewer(gwv_.get());
    minimap_.init(*gwv_);
    const MapPoint hq = world.getPlayer(session_->getPlayerId()).getHQPos();
    view_.centerOn(hq.isValid() ? hq : MapPoint(world.getSize().x / 2, world.getSize().y / 2));
    VIEWSTATE.playerId = session_->getPlayerId();
    VIEWSTATE.onDestroyBuilding = [this](MapPoint pt) { session_->DestroyBuilding(pt); };
    VIEWSTATE.onToggleHumanAIPlayer = [this] { session_->toggleLocalAI(); };
    hud_ = std::make_unique<Hud>(*this, app_);
    session_->setLuaMessageHandler([this](const LuaMessage& msg) { hud_->showLuaMessage(msg); });
    lastAutosave_ = app.time();
    app.music().play(Music::Context::Game);
}

GameScreen::~GameScreen()
{
    VIEWSTATE.onDestroyBuilding = nullptr;
    VIEWSTATE.onToggleHumanAIPlayer = nullptr;
    if(session_)
        session_->setGameInterface(nullptr);
    if(gwv_)
        gwv_->setTerrainObserver(nullptr);
}

unsigned GameScreen::localPlayer() const
{
    return session_->getPlayerId();
}

void GameScreen::setSpeedFactor(unsigned f)
{
    speedFactor_ = std::clamp(f, 1u, 8u);
}

void GameScreen::setPaused(bool p)
{
    paused_ = p;
    VIEWSTATE.paused = p;
}

void GameScreen::jumpTo(MapPoint pt)
{
    view_.centerOn(pt);
}

void GameScreen::runGame(double dt)
{
    const auto baseLen = SPEED_GF_LENGTHS[session_->game().ggs_.speed];
    const double gfLen = std::chrono::duration<double>(baseLen).count() / speedFactor_;
    if(!paused_ && !session_->isFinished())
    {
        accum_ += dt;
        // Never more than 50 ms of simulation work per frame; drop time if we cannot keep up
        const auto start = std::chrono::steady_clock::now();
        perf::Scope timeSim(perf::Phase::Sim);
        while(accum_ >= gfLen)
        {
            const auto gfStart = std::chrono::steady_clock::now();
            session_->runGF();
            perf::gameFrame(
              std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gfStart).count());
            accum_ -= gfLen;
            if(std::chrono::steady_clock::now() - start > std::chrono::milliseconds(50))
            {
                accum_ = std::min(accum_, gfLen);
                break;
            }
        }
    }
    VIEWSTATE.gfLength = std::chrono::milliseconds(std::max<long>(1, std::lround(gfLen * 1000)));
    VIEWSTATE.frameTime = std::chrono::milliseconds(std::lround(accum_ * 1000));
    // Global animations (trees, flags) follow the clock; the test mode uses its own clock (reproducible pictures)
    const auto now = app_.testMode() ? ViewState::Clock::time_point(std::chrono::milliseconds(app_.timeMs())) :
                                       ViewState::Clock::now();
    VIEWSTATE.lastTime = now - VIEWSTATE.frameTime;
    VIEWSTATE.currentGF = session_->getGF();
}

void GameScreen::update(double dt)
{
    // Result of a savegame written in the background
    if(saving_.valid() && saving_.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
    {
        if(!saving_.get())
            hud_->toast("Speichern fehlgeschlagen");
        saving_ = {};
    }
    view_.cam.screenW = app_.width();
    view_.cam.screenH = app_.height();
    runGame(dt);
    hud_->update(dt);
    minimap_.update(4);
    // Autosave every 5 minutes of real time
    if(app_.time() - lastAutosave_ > 300)
    {
        lastAutosave_ = app_.time();
        saveTo((RTTRCONFIG.expandPath(s25::folders::save) / "autosave.sav").string());
    }
}

void GameScreen::draw()
{
    {
        perf::Scope timeWorld(perf::Phase::World);
        view_.draw(app_.timeMs());
    }
    hud_->draw();
}

void GameScreen::onPointer(const PointerEvent& ev)
{
    hud_->onPointer(ev);
}

void GameScreen::onWheel(float x, float y, float delta)
{
    hud_->onWheel(x, y, delta);
}

void GameScreen::onKey(SDL_Keycode key, bool down)
{
    hud_->onKey(key, down);
}

bool GameScreen::onBack()
{
    return hud_->onBack();
}

void GameScreen::onSuspend()
{
    saveTo((RTTRCONFIG.expandPath(s25::folders::save) / "autosave.sav").string());
}

bool GameScreen::saveTo(const std::string& path)
{
    // The game state is copied now; compressing and writing run on a worker thread (no hitch at autosave)
    const auto t0 = std::chrono::steady_clock::now();
    bfs::create_directories(bfs::path(path).parent_path());
    saving_ = session_->saveInBackground(path);
    const auto t1 = std::chrono::steady_clock::now();
    // Picture for the savegame lists (encoded on a worker thread as well)
    app_.saveThumbnail(bfs::path(path).replace_extension(".png").string());
    const auto t2 = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return static_cast<long>(std::chrono::duration<double, std::milli>(b - a).count()); };
    std::cout << "[time] save: snapshot " << ms(t0, t1) << " ms, picture " << ms(t1, t2) << " ms on the main thread"
              << std::endl;
    return true;
}

void GameScreen::startTutorial(bool firstStart)
{
    hud_->startTutorial(firstStart);
}

bool GameScreen::quickSave()
{
    return saveTo((RTTRCONFIG.expandPath(s25::folders::save) / "quicksave.sav").string());
}

bool GameScreen::quickLoad()
{
    const bfs::path p = RTTRCONFIG.expandPath(s25::folders::save) / "quicksave.sav";
    if(!bfs::exists(p))
        return false;
    App& app = app_;
    app.post([&app, p] {
        try
        {
            app.setScreen(createGameScreen(app, Session::loadGame(p, 1)));
        } catch(const std::exception& e)
        {
            LOG.write("Quickload failed: %1%\n") % e.what();
        }
    });
    return true;
}

// GameInterface
void GameScreen::GI_PlayerDefeated(unsigned playerId)
{
    hud_->notifyDefeated(playerId);
}
void GameScreen::GI_UpdateMinimap(MapPoint) {}
void GameScreen::GI_FlagDestroyed(MapPoint pt)
{
    hud_->flagDestroyed(pt);
}
void GameScreen::GI_TreatyOfAllianceChanged(unsigned) {}
void GameScreen::GI_UpdateMapVisibility()
{
    view_.terrain().allChanged();
    minimap_.invalidateAll();
}
void GameScreen::GI_Winner(unsigned playerId)
{
    hud_->notifyWinner(playerId, false);
}
void GameScreen::GI_TeamWinner(unsigned playerId)
{
    hud_->notifyWinner(playerId, true);
}
void GameScreen::GI_StartRoadBuilding(MapPoint startPt, bool waterRoad)
{
    hud_->startRoad(startPt, waterRoad);
}
void GameScreen::GI_CancelRoadBuilding()
{
    hud_->cancelRoad();
}
void GameScreen::GI_BuildRoad() {}
Cheats& GameScreen::GI_GetCheats()
{
    return *cheats_;
}

std::unique_ptr<Screen> createGameScreen(App& app, std::unique_ptr<Session> session)
{
    return std::make_unique<GameScreen>(app, std::move(session));
}


// ---------------------------------------------------------------------------------------------------------------
// Test scripts

namespace {
bool parsePair(const std::string& s, int& a, int& b)
{
    const auto c = s.find(',');
    if(c == std::string::npos)
        return false;
    a = std::stoi(s.substr(0, c));
    b = std::stoi(s.substr(c + 1));
    return true;
}
std::string underscored(std::string s)
{
    std::replace(s.begin(), s.end(), ' ', '_');
    return s;
}
} // namespace

bool GameScreen::testPoint(const std::string& spec, float& x, float& y)
{
    const auto colon = spec.find(':');
    const std::string kind = spec.substr(0, colon), arg = spec.substr(colon + 1);
    const GameWorld& world = session_->world();
    const GamePlayer& me = world.getPlayer(localPlayer());
    MapPoint pt = MapPoint::invalid();
    int dx = 0, dy = 0;
    if(kind == "node")
    {
        int px, py;
        if(!parsePair(arg, px, py))
            return false;
        pt = MapPoint(static_cast<MapCoord>(px), static_cast<MapCoord>(py));
    } else if(kind == "hq" || kind == "found")
    {
        pt = kind == "hq" ? me.getHQPos() : testFound_;
        if(!arg.empty() && !parsePair(arg, dx, dy))
            return false;
    } else if(kind == "ship")
    {
        if(me.getShips().empty())
            return false;
        pt = me.getShips().front()->getPos();
    } else if(kind == "hqflag" || kind == "foundflag")
    {
        const MapPoint base = kind == "hqflag" ? me.getHQPos() : testFound_;
        if(!base.isValid())
            return false;
        pt = world.getNeighbour(base, Direction::SouthEast);
    } else if(kind == "bld")
    {
        // bld:<German name, spaces as _>[#n]
        std::string name = arg;
        int nth = 0;
        const auto hash = name.rfind('#');
        if(hash != std::string::npos)
        {
            nth = std::stoi(name.substr(hash + 1));
            name.resize(hash);
        }
        const auto& reg = me.getBuildingRegister();
        std::vector<MapPoint> pts;
        for(const auto* site : reg.getBuildingSites())
        {
            if(underscored(buildingName(site->getBuildingType())) == name)
                pts.push_back(site->getPos());
        }
        for(unsigned i = 0; i < helpers::NumEnumValues_v<BuildingType>; ++i)
        {
            const auto bt = static_cast<BuildingType>(i);
            if(underscored(buildingName(bt)) != name)
                continue;
            if(BuildingProperties::isMilitary(bt))
            {
                for(const auto* b : reg.getMilitaryBuildings())
                {
                    if(b->getBuildingType() == bt)
                        pts.push_back(b->getPos());
                }
            } else if(BuildingProperties::isWareHouse(bt))
            {
                for(const auto* b : reg.getStorehouses())
                {
                    if(b->getBuildingType() == bt)
                        pts.push_back(b->getPos());
                }
            } else
            {
                for(const auto* b : reg.getBuildings(bt))
                    pts.push_back(b->getPos());
            }
        }
        if(nth >= static_cast<int>(pts.size()))
            return false;
        pt = pts[static_cast<size_t>(nth)];
    } else
        return false;
    if(!pt.isValid())
        return false;
    PointF p = view_.worldToScreen(view_.worldPosNear(pt));
    // Offsets in nodes (approximate, screen space)
    p.x += static_cast<float>(dx) * TR_W * view_.cam.zoom;
    p.y += static_cast<float>(dy) * TR_H * view_.cam.zoom;
    x = p.x;
    y = p.y;
    return x >= 0 && y >= 0 && x < static_cast<float>(app_.width()) && y < static_cast<float>(app_.height());
}

bool GameScreen::testCommand(const std::string& cmd, const std::vector<std::string>& args)
{
    GameWorld& world = session_->world();
    const unsigned meId = localPlayer();
    const GamePlayer& me = world.getPlayer(meId);
    auto arg = [&](size_t i) { return i < args.size() ? args[i] : std::string(); };
    if(cmd == "ff")
    {
        // Simulate game frames as fast as possible
        const unsigned n = static_cast<unsigned>(std::stoul(arg(0)));
        for(unsigned i = 0; i < n && !session_->isFinished(); ++i)
            session_->runGF();
        VIEWSTATE.currentGF = session_->getGF();
        return true;
    }
    if(cmd == "center")
    {
        if(arg(0) == "hq")
            jumpTo(me.getHQPos());
        else if(arg(0) == "found" && testFound_.isValid())
            jumpTo(testFound_);
        else if(arg(0) == "ship")
        {
            if(me.getShips().empty())
                return false;
            std::cout << "[script] ships " << me.getShips().size() << std::endl;
            jumpTo(me.getShips().front()->getPos());
        }
        else
        {
            int px, py;
            if(!parsePair(arg(0), px, py))
                return false;
            jumpTo(MapPoint(static_cast<MapCoord>(px), static_cast<MapCoord>(py)));
        }
        return true;
    }
    if(cmd == "speed")
    {
        setSpeedFactor(static_cast<unsigned>(std::stoul(arg(0))));
        return true;
    }
    if(cmd == "zoom")
    {
        view_.zoomAt(app_.width() / 2.f, app_.height() / 2.f, std::stof(arg(0)));
        return true;
    }
    if(cmd == "findbq")
    {
        // Nearest visible own node (from the HQ or "found") with at least the given building quality
        static const std::pair<const char*, BuildingQuality> names[] = {{"flag", BuildingQuality::Flag},
                                                                        {"small", BuildingQuality::Hut},
                                                                        {"medium", BuildingQuality::House},
                                                                        {"large", BuildingQuality::Castle},
                                                                        {"mine", BuildingQuality::Mine},
                                                                        {"harbor", BuildingQuality::Harbor}};
        BuildingQuality want = BuildingQuality::Hut;
        for(const auto& n : names)
        {
            if(arg(0) == n.first)
                want = n.second;
        }
        // Options: from=hq|found|enemy min=<n> mil near=tree|granite|water
        std::map<std::string, std::string> opt;
        for(size_t i = 1; i < args.size(); ++i)
        {
            const auto eq = args[i].find('=');
            opt[args[i].substr(0, eq)] = eq == std::string::npos ? "1" : args[i].substr(eq + 1);
        }
        MapPoint from = opt["from"] == "found" && testFound_.isValid() ? testFound_ : me.getHQPos();
        const bool towardEnemy = opt["from"] == "enemy";
        if(towardEnemy)
        {
            // Nearest enemy warehouse: the node closest to it is searched
            unsigned best = 100000;
            for(unsigned p = 0; p < world.getNumPlayers(); ++p)
            {
                if(p == meId || world.getPlayer(p).isDefeated())
                    continue;
                for(const auto* wh : world.getPlayer(p).getBuildingRegister().getStorehouses())
                {
                    const unsigned d = world.calcDistance(me.getHQPos(), wh->getPos());
                    if(d < best)
                    {
                        best = d;
                        from = wh->getPos();
                    }
                }
            }
        }
        const unsigned minDist = opt["min"].empty() ? 2u : static_cast<unsigned>(std::stoul(opt["min"]));
        const bool needMil = !opt["mil"].empty();
        const std::string near = opt["near"];
        auto resourcesNear = [&](MapPoint pt) {
            unsigned n = 0;
            for(const MapPoint p : world.getPointsInRadius(pt, 4))
            {
                const noBase* o = world.getNO(p);
                if(near == "tree" && o && o->getType() == NodalObjectType::Tree)
                    ++n;
                else if(near == "granite" && o && o->getType() == NodalObjectType::Granite)
                    n += 2;
                else if(near == "water" && world.isWaterPoint(p))
                    ++n;
                else if(near == "mountain")
                {
                    const auto& desc = world.getDescription();
                    if(desc.get(world.getNode(p).t1).kind == TerrainKind::Mountain)
                        ++n;
                }
            }
            return n;
        };
        MapPoint best = MapPoint::invalid();
        unsigned bestDist = 1000;
        for(unsigned yy = 0; yy < world.getHeight(); ++yy)
        {
            for(unsigned xx = 0; xx < world.getWidth(); ++xx)
            {
                const MapPoint pt(static_cast<MapCoord>(xx), static_cast<MapCoord>(yy));
                const BuildingQuality bq = gwv_->getBQ(pt);
                const bool ok = want == BuildingQuality::Mine ? bq == BuildingQuality::Mine :
                                want == BuildingQuality::Flag ? bq != BuildingQuality::Nothing :
                                                                (bq != BuildingQuality::Mine && canUseBq(bq, want));
                if(!ok || gwv_->getVisibility(pt) != Visibility::Visible || world.getNode(pt).owner != meId + 1)
                    continue;
                if(needMil && world.isMilitaryBuildingNearNode(pt, static_cast<unsigned char>(meId)))
                    continue;
                if(!near.empty() && resourcesNear(pt) < 4)
                    continue;
                const unsigned d = world.calcDistance(from, pt);
                if(d >= minDist && d < bestDist)
                {
                    best = pt;
                    bestDist = d;
                }
            }
        }
        if(!best.isValid())
            return false;
        testFound_ = best;
        jumpTo(best);
        std::cout << "[script] found " << best.x << "," << best.y << " dist " << bestDist << std::endl;
        return true;
    }
    if(cmd == "findenemy")
    {
        // Nearest attackable enemy military building
        MapPoint best = MapPoint::invalid();
        unsigned bestDist = 100000;
        for(unsigned p = 0; p < world.getNumPlayers(); ++p)
        {
            if(p == meId || world.getPlayer(p).isDefeated())
                continue;
            for(const auto* b : world.getPlayer(p).getBuildingRegister().getMilitaryBuildings())
            {
                const unsigned d = world.calcDistance(me.getHQPos(), b->getPos());
                if(d < bestDist && b->isAttackable(meId))
                {
                    best = b->getPos();
                    bestDist = d;
                }
            }
            for(const auto* b : world.getPlayer(p).getBuildingRegister().getStorehouses())
            {
                const unsigned d = world.calcDistance(me.getHQPos(), b->getPos());
                if(d < bestDist && b->isAttackable(meId))
                {
                    best = b->getPos();
                    bestDist = d;
                }
            }
        }
        if(!best.isValid())
        {
            std::cout << "[script] no attackable enemy; peaceful " << world.getGGS().getSelection(AddonId::PEACEFULMODE);
            for(unsigned p = 0; p < world.getNumPlayers(); ++p)
                std::cout << " p" << p << " attackable " << world.getPlayer(p).isAttackable(meId);
            std::cout << std::endl;
            return false;
        }
        testFound_ = best;
        jumpTo(best);
        std::cout << "[script] enemy " << best.x << "," << best.y << " soldiers "
                  << gwv_->getNumSoldiersForAttack(best) << std::endl;
        return true;
    }
    if(cmd == "gameover")
    {
        // Test: show the end of game sheet (win or lose)
        if(arg(0) == "lose")
            hud_->notifyDefeated(localPlayer());
        else
            hud_->notifyWinner(localPlayer(), false);
        return true;
    }
    if(cmd == "seafaring")
    {
        // Test: finished harbor at the harbor spot nearest to the HQ with wares for an expedition, a ship at its
        // coast. Used to test the ship sheet without playing an hour.
        HarborId best;
        unsigned bestDist = 100000;
        for(unsigned i = 1; i <= world.getNumHarborPoints(); ++i)
        {
            const HarborId h(i);
            const MapPoint hp = world.getHarborPoint(h);
            const unsigned d = world.calcDistance(me.getHQPos(), hp);
            if(d < bestDist && world.getNO(hp)->getType() != NodalObjectType::Building)
            {
                bestDist = d;
                best = h;
            }
        }
        if(!best)
            return false;
        const MapPoint hp = world.getHarborPoint(best);
        world.destroyNO(hp, false);
        auto* harbor = static_cast<nobHarborBuilding*>(
          BuildingFactory::createBuilding(world, BuildingType::HarborBuilding, hp, static_cast<unsigned char>(meId),
                                          me.nation));
        if(!harbor)
            return false;
        GoodCounts goods;
        goods[GoodType::Boards] = 40;
        goods[GoodType::Stones] = 40;
        harbor->addToInventory(goods, true);
        PeopleCounts people;
        people[Job::Builder] = 4;
        people[Job::Scout] = 4;
        people[Job::Helper] = 10;
        harbor->addToInventory(people, true);
        for(const auto dir : helpers::EnumRange<Direction>{})
        {
            const SeaId sea = world.getSeaId(best, dir);
            if(!sea)
                continue;
            const MapPoint coast = world.getCoastalPoint(best, sea);
            if(!coast.isValid())
                continue;
            auto& ship = world.addFigure(coast, std::make_unique<noShip>(coast, static_cast<unsigned char>(meId)));
            world.getPlayer(meId).registerShip(ship);
            break;
        }
        testFound_ = hp;
        std::cout << "[script] harbor at " << hp.x << "," << hp.y << ", ships " << me.getNumShips() << std::endl;
        return true;
    }
    if(cmd == "node")
    {
        int px, py;
        if(!parsePair(arg(0), px, py))
            return false;
        const MapPoint pt(static_cast<MapCoord>(px), static_cast<MapCoord>(py));
        const noBase* obj = world.getNO(pt);
        std::cout << "[script] node " << px << "," << py << " obj " << (obj ? static_cast<int>(obj->getType()) : -1)
                  << " bq " << static_cast<int>(gwv_->getBQ(pt)) << " owner " << int(world.getNode(pt).owner)
                  << " roads";
        for(const auto dir : helpers::EnumRange<Direction>{})
            std::cout << " " << static_cast<int>(world.getPointRoad(pt, dir));
        std::cout << std::endl;
        return true;
    }
    if(cmd == "dump")
    {
        std::cout << "[script] GF " << session_->getGF() << " time " << session_->formatGFTime(session_->getGF())
                  << std::endl;
        for(unsigned p = 0; p < world.getNumPlayers(); ++p)
        {
            const GamePlayer& pl = world.getPlayer(p);
            const auto& reg = pl.getBuildingRegister();
            unsigned numBld = 0;
            for(unsigned i = 0; i < helpers::NumEnumValues_v<BuildingType>; ++i)
            {
                const auto bt = static_cast<BuildingType>(i);
                if(!BuildingProperties::isMilitary(bt) && !BuildingProperties::isWareHouse(bt))
                    numBld += static_cast<unsigned>(reg.getBuildings(bt).size());
            }
            std::cout << "[script] player " << p << (p == meId ? " (me)" : "") << (pl.isDefeated() ? " defeated" : "")
                      << ": military " << reg.getMilitaryBuildings().size() << ", buildings " << numBld
                      << ", sites " << reg.getBuildingSites().size() << ", stores " << reg.getStorehouses().size()
                      << ", soldiers " << pl.getInventory().people[Job::Private] << std::endl;
        }
        if(arg(0) == "mine")
        {
            const auto& reg = me.getBuildingRegister();
            for(unsigned i = 0; i < helpers::NumEnumValues_v<BuildingType>; ++i)
            {
                const auto bt = static_cast<BuildingType>(i);
                const size_t n = reg.getBuildings(bt).size();
                if(n)
                    std::cout << "[script]   " << buildingName(bt) << " " << n << std::endl;
            }
            const auto& inv = me.getInventory();
            std::cout << "[script]   wood " << inv.goods[GoodType::Wood] << " boards " << inv.goods[GoodType::Boards]
                      << " stones " << inv.goods[GoodType::Stones] << " food "
                      << inv.goods[GoodType::Fish] + inv.goods[GoodType::Bread] + inv.goods[GoodType::Meat]
                      << " coal " << inv.goods[GoodType::Coal] << " iron " << inv.goods[GoodType::Iron]
                      << " swords " << inv.goods[GoodType::Sword] << std::endl;
        }
        return true;
    }
    return false;
}

} // namespace weiler
