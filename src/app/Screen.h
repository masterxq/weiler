// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <SDL3/SDL_keycode.h>
#include <cstdint>

namespace weiler {

class App;

/// Pointer (finger or mouse) event in framebuffer pixels
struct PointerEvent
{
    enum class Type
    {
        Down,
        Move,
        Up,
        Cancel
    };
    Type type;
    /// Finger id or 0 for the mouse
    uint64_t id;
    float x, y;
    /// Mouse button (1 = left, 3 = right), 0 for touch
    int button = 0;
    bool isMouse = false;
};

/// A full screen state of the application (main menu, game, ...)
class Screen
{
public:
    explicit Screen(App& app) : app_(app) {}
    virtual ~Screen() = default;
    /// Advance time (seconds of real time)
    virtual void update(double dt) = 0;
    virtual void draw() = 0;
    virtual void onPointer(const PointerEvent& ev) = 0;
    virtual void onWheel(float x, float y, float delta) { (void)x, (void)y, (void)delta; }
    virtual void onKey(SDL_Keycode key, bool down) { (void)key, (void)down; }
    virtual void onTextInput(const char* text) { (void)text; }
    /// Back gesture (two finger tap, right click, Esc). Return false if nothing to go back to.
    virtual bool onBack() { return false; }
    /// The app is about to quit or to be suspended (autosave)
    virtual void onSuspend() {}

protected:
    App& app_;
};

} // namespace weiler
