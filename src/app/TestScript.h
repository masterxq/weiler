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
///   down x y | move x y [frames] | up x y (one finger held across commands) |
///   text <string> | shot <file.png> | resize w h | rename <from> <to> | log <text> | quit
/// Extra commands: waitfor <point> [frames] | tapfirst <point>... | tapopt <point>... | labels | perf [frames] [label] |
///   perfasync [frames] [label] (measure while the next commands run) | overdraw [heatmap.png] |
///   failshots <dir> | game <screen command> [args]
/// Game screen commands: ff <gf> | speed <1..8> | center hq|found|ship|mil|bld:<name>|x,y | zoom <z> | findbq <flag|small|medium|large|mine>
///   [from=hq|found|enemy] [min=<n>] [mil] [near=tree|granite|water|mountain] | findenemy | dump [mine] | node x,y |
///   expect connected|nothing found | gameover win|lose|team <mask> | mark (camera, own flags) |
///   expect moved|still|flags <delta>|flagsmin <n> (since mark) | expect road <parts>|noroad|cornermap on|off|layers <mask>
/// Instead of "x y" a symbolic point can be given: ui:<label>[#n] (button label, spaces as '_', trailing '*' for a
/// prefix, icon buttons as ui:icon:Name) or a point the current screen knows (game: node:x,y, hq:dx,dy,
/// bld:<name>[#n], mil: = own military building with the most soldiers, ...).
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
    float fingerX_ = 0, fingerY_ = 0; // last position of down/move
};

} // namespace weiler
