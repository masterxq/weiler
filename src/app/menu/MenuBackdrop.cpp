// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MenuBackdrop.h"
#include "App.h"
#include "Game.h"
#include "GamePlayer.h"
#include "Menus.h"
#include "Session.h"
#include "ViewState.h"
#include "game/GameScreen.h"
#include "gfx/WorldView.h"
#include "ui/Ui.h"
#include "gameData/GameConsts.h"
#include "gameData/GameLoader.h"
#include "Loader.h"
#include "world/GameWorld.h"
#include "world/GameWorldViewer.h"
#include <boost/filesystem.hpp>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iostream>

namespace bfs = boost::filesystem;

namespace weiler {

using gfx::Color;

MenuBackdrop::MenuBackdrop(App& app) : app_(app) {}

MenuBackdrop::~MenuBackdrop()
{
    if(gwv_)
        gwv_->setTerrainObserver(nullptr);
    view_.reset();
    gwv_.reset();
    session_.reset();
}

void MenuBackdrop::load()
{
    tried_ = true;
    if(!app_.paths().hasGameData() || !ensureStartFilesLoaded())
        return;
    // A few green original maps with villages, water and mountains; the first one found is used, varied by day
    static const char* const candidates[] = {"DATA/MAPS4/KARTE01.WLD", "DATA/MAPS/MISS205.WLD", "DATA/MAPS4/KARTE05.WLD",
                                             "DATA/MAPS3/AKARTE01.WLD"};
    std::vector<bfs::path> maps;
    for(const char* c : candidates)
    {
        const bfs::path p = app_.paths().gameData / c;
        if(bfs::exists(p))
            maps.push_back(p);
    }
    if(maps.empty())
        return;
    const auto day = static_cast<unsigned>(std::time(nullptr) / 86400);
    const bfs::path map = maps[app_.testMode() ? 0 : day % maps.size()];
    try
    {
        // All players are AI; everything is visible
        NewGameSetup setup = makeSkirmishSetup(map, 3, 7, AI::Level::Medium, Nation::Romans);
        setup.ggs.exploration = Exploration::Disabled;
        auto session = Session::newGame(setup);
        GameLoader loader(LOADER, session->gamePtr());
        if(!loader.load())
            return;
        app_.atlas().preloadGameSprites();
        session->toggleLocalAI();
        session_ = std::move(session);
    } catch(const std::exception& e)
    {
        std::cerr << "Menu background: " << e.what() << std::endl;
        session_.reset();
        return;
    }
    GameWorld& world = session_->world();
    gwv_ = std::make_unique<GameWorldViewer>(session_->getPlayerId(), world);
    view_ = std::make_unique<gfx::WorldView>(app_.batch(), app_.atlas());
    view_->cam.screenW = app_.width();
    view_->cam.screenH = app_.height();
    view_->cam.zoom = std::max(1.f, app_.uiScale() * 0.9f);
    view_->setViewer(gwv_.get());
    const MapPoint hq = world.getPlayer(session_->getPlayerId()).getHQPos();
    view_->centerOn(hq.isValid() ? hq : MapPoint(world.getSize().x / 2, world.getSize().y / 2));
    VIEWSTATE.playerId = session_->getPlayerId();
    loadedAt_ = app_.time();
}

void MenuBackdrop::update(double dt)
{
    ++frames_;
    // Load after the first frames so that the menu appears at once
    if(!tried_ && frames_ > 2)
        load();
    if(!session_)
        return;
    t_ += dt;
    // Simulation at normal speed (the villages grow while the menu is open)
    const double gfLen = std::chrono::duration<double>(SPEED_GF_LENGTHS[session_->game().ggs_.speed]).count();
    accum_ += std::min(dt, 0.25);
    const auto start = std::chrono::steady_clock::now();
    while(accum_ >= gfLen)
    {
        session_->runGF();
        accum_ -= gfLen;
        if(std::chrono::steady_clock::now() - start > std::chrono::milliseconds(20))
        {
            accum_ = 0;
            break;
        }
    }
    VIEWSTATE.paused = false;
    VIEWSTATE.gfLength = std::chrono::milliseconds(std::max<long>(1, std::lround(gfLen * 1000)));
    VIEWSTATE.frameTime = std::chrono::milliseconds(std::lround(accum_ * 1000));
    VIEWSTATE.lastTime = ViewState::Clock::now() - VIEWSTATE.frameTime;
    VIEWSTATE.currentGF = session_->getGF();
    // Camera: slow drift whose direction turns gently (the map wraps around)
    auto& cam = view_->cam;
    cam.screenW = app_.width();
    cam.screenH = app_.height();
    const float heading = static_cast<float>(0.6 + 0.9 * std::sin(t_ * 0.021) + 0.35 * std::sin(t_ * 0.047));
    const float speed = 9.f; // world pixels per second
    view_->moveBy(std::cos(heading) * speed * static_cast<float>(dt) * cam.zoom,
                  std::sin(heading) * speed * 0.6f * static_cast<float>(dt) * cam.zoom);
}

void MenuBackdrop::drawGradient(float alpha)
{
    auto& b = app_.batch();
    const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
    const auto& th = app_.ui().theme;
    b.roundRectV({0, 0, W, H * 0.6f}, 0, Color::rgb(0x16181C, alpha), Color::rgb(0x2A2219, alpha), 0.f);
    b.roundRectV({0, H * 0.6f - 1, W, H * 0.4f + 1}, 0, Color::rgb(0x2A2219, alpha), th.bg.withAlpha(alpha), 0.f);
    // Low warm light
    b.circle(W * 0.7f, H * 0.42f, std::max(W, H) * 0.45f, th.accent.withAlpha(0.07f * alpha), 0,
             std::max(W, H) * 0.4f);
}

void MenuBackdrop::draw()
{
    auto& b = app_.batch();
    const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
    const auto& th = app_.ui().theme;
    float fade = 0;
    if(session_)
    {
        fade = std::min(1.f, static_cast<float>((app_.time() - loadedAt_) / 1.2));
        view_->draw(app_.timeMs());
        b.resetTransform();
        b.setClip({});
    }
    if(fade < 1)
        drawGradient(1.f - fade);
    if(fade <= 0)
        return;
    // Warm evening grade and darkening for legible text: darker towards top and bottom, soft vignette
    b.quad(0, 0, 0, W, H, 0, 0, 1, 1, Color::rgb(0x1E1408, 0.28f * fade));
    const bool portrait = app_.portrait();
    b.roundRectV({0, 0, W, H * 0.45f}, 0, Color::rgb(0x0B0A09, 0.62f * fade), Color::rgb(0x0B0A09, 0.10f * fade), 0.f);
    b.roundRectV({0, H * 0.45f, W, H * 0.55f}, 0, Color::rgb(0x0B0A09, 0.10f * fade),
                 Color::rgb(0x0B0A09, (portrait ? 0.82f : 0.6f) * fade), 0.f);
    const float diag = std::sqrt(W * W + H * H);
    const float vw = diag * 0.35f;
    b.circle(W / 2, H / 2, diag / 2 + vw * 0.2f, Color::rgb(0x050505, 0.55f * fade), vw, vw * 0.8f);
    (void)th;
}

} // namespace weiler
