// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "GameInterface.h"
#include "Screen.h"
#include "Session.h"
#include "gfx/Minimap.h"
#include "gfx/WorldView.h"
#include <future>
#include <memory>

class GameWorldViewer;

namespace weiler {

class Hud;

/// The running game: world view, input gestures, game loop with pause and fast forward.
class GameScreen : public Screen, public GameInterface
{
public:
    GameScreen(App& app, std::unique_ptr<Session> session);
    ~GameScreen() override;

    void update(double dt) override;
    void draw() override;
    const char* name() const override { return "game"; }
    bool still() const override { return paused_; }
    void onPointer(const PointerEvent& ev) override;
    void onWheel(float x, float y, float delta) override;
    void onKey(SDL_Keycode key, bool down) override;
    bool onBack() override;
    void onSuspend() override;
    bool testPoint(const std::string& spec, float& x, float& y) override;
    bool testCommand(const std::string& cmd, const std::vector<std::string>& args) override;

    // Access for the HUD
    Session& session() { return *session_; }
    GameWorldViewer& viewer() { return *gwv_; }
    gfx::WorldView& view() { return view_; }
    gfx::Minimap& minimap() { return minimap_; }
    unsigned speedFactor() const { return speedFactor_; }
    void setSpeedFactor(unsigned f);
    bool paused() const { return paused_; }
    void setPaused(bool p);
    unsigned localPlayer() const;
    void jumpTo(MapPoint pt);
    bool quickSave();
    bool quickLoad();
    bool saveTo(const std::string& path);
    void startTutorial(bool firstStart = false);

    // GameInterface
    void GI_PlayerDefeated(unsigned playerId) override;
    void GI_UpdateMinimap(MapPoint pt) override;
    void GI_FlagDestroyed(MapPoint pt) override;
    void GI_TreatyOfAllianceChanged(unsigned playerId) override;
    void GI_UpdateMapVisibility() override;
    void GI_Winner(unsigned playerId) override;
    void GI_TeamWinner(unsigned playerId) override;
    void GI_StartRoadBuilding(MapPoint startPt, bool waterRoad) override;
    void GI_CancelRoadBuilding() override;
    void GI_BuildRoad() override;
    Cheats& GI_GetCheats() override;

private:
    void runGame(double dt);
    void updateViewState();

    std::unique_ptr<Session> session_;
    std::unique_ptr<GameWorldViewer> gwv_;
    std::unique_ptr<Cheats> cheats_;
    gfx::WorldView view_;
    gfx::Minimap minimap_;
    std::unique_ptr<Hud> hud_;
    double accum_ = 0;
    unsigned speedFactor_ = 1;
    bool paused_ = false;
    /// Savegame being written in the background
    std::shared_future<bool> saving_;
    double lastAutosave_ = 0;
    MapPoint testFound_ = MapPoint::invalid();
    friend class Hud;
};

/// Create a game screen for a new or loaded game; shows an error message on failure
std::unique_ptr<Screen> createGameScreen(App& app, std::unique_ptr<Session> session);
/// Load the files of the original data needed before any game (palettes, fonts of the core, ...)
bool ensureStartFilesLoaded();

} // namespace weiler
