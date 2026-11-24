// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Icons.h"
#include "Theme.h"
#include "Screen.h"
#include "gfx/Batch.h"
#include "gfx/Text.h"
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ISpriteSource;

namespace weiler {
class App;
}

namespace weiler::ui {

using gfx::Color;
using gfx::Rect2;

using Id = uint64_t;
constexpr Id id(std::string_view s, uint64_t n = 0)
{
    uint64_t h = 1469598103934665603ull ^ (n * 0x9E3779B97F4A7C15ull);
    for(char c : s)
        h = (h ^ static_cast<uint8_t>(c)) * 1099511628211ull;
    return h;
}

enum class Style
{
    Normal,
    Primary,
    Danger,
    Flat,
    /// Square tile: icon above a short label (menus)
    Tile,
    TilePrimary,
    /// Navigation row: icon and label left aligned, chevron on the right
    Nav
};

/// Immediate mode widgets for touch and mouse
class Ui
{
public:
    explicit Ui(App& app);
    Theme theme;

    /// Feed pointer events; returns true if the UI handles the pointer (pressed on a UI element)
    bool pointer(const PointerEvent& ev);
    bool wheel(float x, float y, float delta);
    /// True if a UI element was drawn at that point in the last frame
    bool hit(float x, float y) const;
    bool hasCapture() const { return captured_; }
    /// Where the current or last press started
    float pressX() const { return pressX_; }
    float pressY() const { return pressY_; }
    void begin();
    void end();

    float dp(float v) const;
    /// Minimum touch target
    float touch() const { return dp(48); }
    gfx::Batch& batch();
    App& app() { return app_; }

    // Drawing
    void block(const Rect2& r, Id owner = 0) { blocks_.push_back({r, owner}); }
    /// Frosted glass panel with soft shadow and edge highlight (tint alpha < 0: theme glass)
    void panel(const Rect2& r, float radius = -1, Color tint = {0, 0, 0, -1}, float opacity = 1.f);
    /// Layered soft shadow (ambient + key light)
    void elevate(const Rect2& r, float radius, const Theme::Elevation& e, float opacity = 1.f);
    /// Title text in the serif display font
    void heading(float x, float y, const std::string& s, float size, Color c, gfx::Align align = gfx::Align::Left);
    float headingWidth(const std::string& s, float size);
    void text(float x, float y, const std::string& s, float size, Color c, gfx::Align align = gfx::Align::Left,
              bool bold = false);
    float textWidth(const std::string& s, float size, bool bold = false);
    float textWrapped(float x, float y, float w, const std::string& s, float size, Color c,
                      gfx::Align align = gfx::Align::Left);
    void icon(Icon i, float cx, float cy, float size, Color c);
    /// Picture filling r (centered crop) with a fine frame
    void picture(const gfx::Texture& tex, const Rect2& r);
    /// Draw a sprite of the game data fitted into r
    void sprite(const ISpriteSource* src, const Rect2& r, unsigned playerColor = 0, float alpha = 1);

    // Widgets (return true when activated)
    bool button(Id i, const Rect2& r, const std::string& label, Icon ic = Icon::None, Style st = Style::Normal,
                bool enabled = true);
    bool iconButton(Id i, const Rect2& r, Icon ic, bool active = false, int badge = 0, Style st = Style::Normal);
    /// Row of a list: background with press and selection state, returns true when tapped
    bool row(Id i, const Rect2& r, bool selected = false);
    /// Clickable area without drawing (rows of lists)
    bool area(Id i, const Rect2& r);
    bool pressed(Id i, const Rect2& r) const;
    /// Long press on an area (fires once)
    bool longPress(Id i, const Rect2& r);
    bool slider(Id i, const Rect2& r, float& value, float lo = 0, float hi = 1, float step = 0);
    bool toggle(Id i, const Rect2& r, bool& value, const std::string& label);
    /// Segmented choice; returns true if changed
    bool segmented(Id i, const Rect2& r, const std::vector<std::string>& options, int& selected);
    /// Stepper - [value] +
    bool stepper(Id i, const Rect2& r, int& value, int lo, int hi, const std::string& label);

    /// Vertical scroll region: call begin, draw content with the returned y offset, end with the content height
    float beginScroll(Id i, const Rect2& view);
    void endScroll(float contentHeight);

    /// Time in seconds (animations)
    double time() const;

    /// Test mode: named touch targets of the last frame (buttons by label, icon buttons as "icon:Name")
    void tag(const std::string& label, const Rect2& r);
    /// Finds the n-th target whose label equals (or, with a trailing '*', starts with) the pattern
    bool findTarget(const std::string& pattern, Rect2& r, int nth = 0) const;
    std::vector<std::string> targetLabels() const;
    /// Ease a 0..1 value (cubic out)
    static float ease(float t) { return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }
    /// Ease with a small overshoot (springy appearance)
    static float easeBack(float t)
    {
        const float c1 = 1.5f, c3 = c1 + 1;
        t -= 1;
        return 1 + c3 * t * t * t + c1 * t * t;
    }
    /// Value of an element property that follows target smoothly (time constant ~ duration/3).
    /// The first call returns the target directly.
    float anim(Id i, float target, float duration);
    /// Called when a widget was activated (UI sound)
    std::function<void(int)> onActivate;

private:
    App& app_;
    struct Block
    {
        Rect2 r;
        Id owner;
    };
    std::vector<Block> blocks_, lastBlocks_;
    /// True if no other element covered the press position in the last frame
    bool onTop(Id i) const;
    std::vector<std::pair<std::string, Rect2>> targets_, lastTargets_;
    bool captured_ = false, dragging_ = false, released_ = false, longFired_ = false;
    float pressX_ = 0, pressY_ = 0, curX_ = 0, curY_ = 0, relX_ = 0, relY_ = 0;
    double pressTime_ = 0;
    float dragDY_ = 0;
    struct ScrollState
    {
        float y = 0, vel = 0, content = 0;
        Rect2 view;
    };
    std::unordered_map<Id, ScrollState> scroll_;
    std::vector<Id> scrollStack_;
    float wheelDelta_ = 0, wheelX_ = 0, wheelY_ = 0;
    Id activeSlider_ = 0;
    bool scrollGesture_ = false; // the current press scrolls a list (sliders in it stay untouched)
    struct AnimState
    {
        float value;
        unsigned frame;
    };
    std::unordered_map<Id, AnimState> anims_;
    unsigned frame_ = 0;
    double lastTime_ = 0;
    float dt_ = 0;
    /// Pressed state of an element, animated (0..1)
    float pressAnim(Id i, bool down);
    void activated(int kind = 0);
};

} // namespace weiler::ui
