// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Screen.h"
#include "gameTypes/MapCoordinates.h"
#include <map>
#include <memory>

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

    void notifyDefeated(unsigned playerId);
    void notifyWinner(unsigned playerId, bool team);
    void flagDestroyed(MapPoint pt);
    void startRoad(MapPoint start, bool water);
    void cancelRoad();
    void startTutorial();
    void showLuaMessage(const LuaMessage& msg);

    struct Impl;

private:
    std::unique_ptr<Impl> d;
};

} // namespace weiler
