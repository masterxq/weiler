// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "GameScreen.h"
#include "App.h"
#include "Cheats.h"
#include "Game.h"
#include "GamePlayer.h"
#include "Hud.h"
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
    GameWorld& world = session_->world();
    gwv_ = std::make_unique<GameWorldViewer>(session_->getPlayerId(), world);
    cheats_ = std::make_unique<Cheats>(world, *session_);
    session_->setGameInterface(this);
    view_.cam.screenW = app.width();
    view_.cam.screenH = app.height();
    view_.cam.zoom = std::clamp(app.uiScale() * 1.5f, 1.f, 6.f);
    view_.setViewer(gwv_.get());
    minimap_.init(*gwv_);
    const MapPoint hq = world.getPlayer(session_->getPlayerId()).getHQPos();
    view_.centerOn(hq.isValid() ? hq : MapPoint(world.getSize().x / 2, world.getSize().y / 2));
    VIEWSTATE.playerId = session_->getPlayerId();
    VIEWSTATE.onDestroyBuilding = [this](MapPoint pt) { session_->DestroyBuilding(pt); };
    VIEWSTATE.onToggleHumanAIPlayer = [this] { session_->toggleLocalAI(); };
    hud_ = std::make_unique<Hud>(*this, app_);
    session_->onLuaMessage = [this](const LuaMessage& msg) { hud_->showLuaMessage(msg); };
    lastAutosave_ = app.time();
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
        while(accum_ >= gfLen)
        {
            session_->runGF();
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
    VIEWSTATE.lastTime = ViewState::Clock::now() - VIEWSTATE.frameTime;
    VIEWSTATE.currentGF = session_->getGF();
}

void GameScreen::update(double dt)
{
    view_.cam.screenW = app_.width();
    view_.cam.screenH = app_.height();
    runGame(dt);
    hud_->update(dt);
    minimap_.update(4);
    // Autosave every 5 minutes of real time
    if(!app_.testMode() && app_.time() - lastAutosave_ > 300)
    {
        lastAutosave_ = app_.time();
        saveTo((RTTRCONFIG.expandPath(s25::folders::save) / "autosave.sav").string());
    }
}

void GameScreen::draw()
{
    view_.draw(app_.timeMs());
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
    if(!app_.testMode())
        saveTo((RTTRCONFIG.expandPath(s25::folders::save) / "autosave.sav").string());
}

bool GameScreen::saveTo(const std::string& path)
{
    bfs::create_directories(bfs::path(path).parent_path());
    return session_->save(path);
}

void GameScreen::startTutorial()
{
    hud_->startTutorial();
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

} // namespace weiler
