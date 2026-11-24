// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

class GameWorldViewer;

namespace weiler {

class App;
class Session;
namespace gfx {
class WorldView;
}

/// Atmospheric background of the menus: a real map of the game data, played by AI players, seen by a slowly
/// drifting camera with a warm, darkened grade. Falls back to a plain gradient without game data.
/// Owned by the App so that the camera keeps moving between menu screens. Must be released before a game starts.
class MenuBackdrop
{
public:
    explicit MenuBackdrop(App& app);
    ~MenuBackdrop();
    void update(double dt);
    void draw();

private:
    void load();
    void drawGradient(float alpha);
    App& app_;
    std::unique_ptr<Session> session_;
    std::unique_ptr<GameWorldViewer> gwv_;
    std::unique_ptr<gfx::WorldView> view_;
    bool tried_ = false;
    int frames_ = 0;
    double loadedAt_ = -1, accum_ = 0, t_ = 0;
};

} // namespace weiler
