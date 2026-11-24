// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <SDL3/SDL_events.h>
#include <deque>
#include <string>
#include <vector>

namespace weiler {

class App;

/// Plays a script of synthetic input (touch gestures, mouse, keys) and takes screenshots. Used for offscreen tests.
///
/// Commands (one per line, coordinates in pixels or percent of the screen, e.g. "50%"):
///   wait <frames> | sec <seconds> | tap x y | longpress x y | drag x0 y0 x1 y1 [frames] |
///   pinch cx cy d0 d1 [frames] | twotap x y | click x y [right] | wheel x y delta | key <name> |
///   text <string> | shot <file.png> | resize w h | log <text> | quit
/// Extra commands: waitfor <point> [frames] | labels | game <screen command> [args]
/// Instead of "x y" a symbolic point can be given: ui:<label>[#n] (button label, spaces as '_', trailing '*' for a
/// prefix, icon buttons as ui:icon:Name) or a point the current screen knows (game: node:x,y, hq:dx,dy, ...).
class TestScript
{
public:
    explicit TestScript(App& app);
    bool load(const std::string& path);
    /// Called once per frame before events are processed. Returns false when the script is done.
    bool step();
    /// Called after drawing (screenshots)
    void afterDraw();
    int exitCode() const { return exitCode_; }

private:
    float coord(const std::string& s, bool isY) const;
    /// Resolve a symbolic point; false if it does not exist (yet)
    bool resolve(const std::string& spec, float& x, float& y) const;
    /// Replace symbolic points by "x y"; false on failure
    bool expand(std::vector<std::string>& args);
    void finger(Uint32 type, Uint64 fingerId, float x, float y, int delayFrames);
    void schedule(int delayFrames, const SDL_Event& ev);
    bool execute(const std::string& line);

    App& app_;
    std::vector<std::string> lines_;
    size_t next_ = 0;
    int waitFrames_ = 0;
    std::vector<std::pair<int, SDL_Event>> scheduled_;
    std::string pendingShot_;
    int exitCode_ = 0;
    int frame_ = 0;
    void failShot();
    std::string waitFor_, failDir_;
    int failCount_ = 0;
    int waitForFrames_ = 0;
};

} // namespace weiler
