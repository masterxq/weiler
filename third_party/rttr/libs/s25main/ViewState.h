// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Weiler: narrow interface replacing the former GameClient coupling of the game objects.
// The frontend updates the frame timing each rendered frame; the simulation itself never depends on it
// (only drawing, interpolation and sound timing do).

#include "gameTypes/MapCoordinates.h"
#include "s25util/Singleton.h"
#include <chrono>
#include <functional>
#include <string>

class GameEvent;

class ViewState : public Singleton<ViewState>
{
public:
    using Clock = std::chrono::steady_clock;

    /// Local (human) player
    unsigned playerId = 0;
    bool paused = false;
    bool replayMode = false;
    bool replayFOWDisabled = false;
    /// Current game frame number (set by the session after each GF)
    unsigned currentGF = 0;
    /// Length of one GF at the current speed
    std::chrono::milliseconds gfLength{50};
    /// Time since the last GF, valid range [0, gfLength)
    std::chrono::milliseconds frameTime{0};
    /// Time of the last GF
    Clock::time_point lastTime = Clock::now();
    /// Called when a building of the local player should be destroyed (addon "demolish without resources")
    std::function<void(MapPoint)> onDestroyBuilding;
    /// Cheat: let an AI play for the local player (toggle)
    std::function<void()> onToggleHumanAIPlayer;

    unsigned getPlayerId() const { return playerId; }
    bool isPaused() const { return paused; }
    bool isReplayModeOn() const { return replayMode; }
    bool isReplayFOWDisabled() const { return replayMode && replayFOWDisabled; }
    std::chrono::milliseconds getGFLength() const { return gfLength; }
    std::chrono::milliseconds getFrameTime() const { return frameTime; }
    /// Milliseconds of a monotonic clock (for sound timing)
    unsigned getTickCount() const;

    unsigned getGlobalAnimation(unsigned short max, unsigned char factor_numerator, unsigned char factor_denumerator,
                                unsigned offset) const;
    unsigned interpolate(unsigned maxVal, const GameEvent* ev) const;
    int interpolate(int x1, int x2, const GameEvent* ev) const;
    /// Convert a number of GFs into real time at normal speed (HH:MM:SS or MM:SS)
    std::string formatGFTime(unsigned numGFs) const;
    void DestroyBuilding(MapPoint pt) const
    {
        if(onDestroyBuilding)
            onDestroyBuilding(pt);
    }
};

#define VIEWSTATE ViewState::inst()
