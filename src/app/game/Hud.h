// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Screen.h"
#include "gameTypes/MapCoordinates.h"
#include <map>
#include <memory>
#include <string>

struct LuaMessage;

namespace weiler {

class App;
class GameScreen;

/// In-game user interface: gestures on the world, overlays, menus
class Hud
{
public:
    Hud(GameScreen& game, App& app);
    ~Hud();
    void update(double dt);
    void draw();
    void onPointer(const PointerEvent& ev);
    void onWheel(float x, float y, float delta);
    void onKey(SDL_Keycode key, bool down);
    bool onBack();
    /// Test mode checks of the interface state: "road <n>" (road being built with n parts), "noroad",
    /// "cornermap on|off", "layers <mask>"
    bool testExpect(const std::string& what, const std::string& arg);

    void notifyDefeated(unsigned playerId);
    /// Winner announced by the core: a player index, or with team = true a bit mask of the team's players
    void notifyWinner(unsigned playerOrMask, bool team);
    void flagDestroyed(MapPoint pt);
    /// Short message at the bottom
    void toast(const std::string& text);
    void startRoad(MapPoint start, bool water);
    void cancelRoad();
    void startTutorial(bool firstStart = false);
    void showLuaMessage(const LuaMessage& msg);

    struct Impl;

private:
    std::unique_ptr<Impl> d;
};

} // namespace weiler
