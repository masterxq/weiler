// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Icons.h"
#include "Screen.h"
#include "gfx/Batch.h"
#include "gfx/Text.h"
#include <cstdint>
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

/// Colors and metrics of the user interface (one place for the look)
struct Theme
{
    Color bg = Color::rgb(0x121417);
    Color panel = Color::rgb(0x1B1E23, 0.86f);
    Color panelSolid = Color::rgb(0x1B1E23);
    Color raised = Color::rgb(0x2A2E35, 0.92f);
    Color line = Color::rgb(0xFFFFFF, 0.08f);
    Color text = Color::rgb(0xF2EEE8);
    Color textDim = Color::rgb(0xA8A198);
    Color accent = Color::rgb(0xE8A33D);
    Color accentText = Color::rgb(0x1A1410);
    Color danger = Color::rgb(0xE0604E);
    Color ok = Color::rgb(0x7CC47F);
    Color shadow = Color::rgb(0x000000, 0.45f);
};

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
    Flat
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
    void begin();
    void end();

    float dp(float v) const;
    /// Minimum touch target
    float touch() const { return dp(48); }
    gfx::Batch& batch();
    App& app() { return app_; }

    // Drawing
    void block(const Rect2& r, Id owner = 0) { blocks_.push_back({r, owner}); }
    void panel(const Rect2& r, float radius = -1, Color c = {0, 0, 0, -1});
    void text(float x, float y, const std::string& s, float size, Color c, gfx::Align align = gfx::Align::Left,
              bool bold = false);
    float textWidth(const std::string& s, float size, bool bold = false);
    float textWrapped(float x, float y, float w, const std::string& s, float size, Color c,
                      gfx::Align align = gfx::Align::Left);
    void icon(Icon i, float cx, float cy, float size, Color c);
    /// Draw a sprite of the game data fitted into r
    void sprite(const ISpriteSource* src, const Rect2& r, unsigned playerColor = 0, float alpha = 1);

    // Widgets (return true when activated)
    bool button(Id i, const Rect2& r, const std::string& label, Icon ic = Icon::None, Style st = Style::Normal,
                bool enabled = true);
    bool iconButton(Id i, const Rect2& r, Icon ic, bool active = false, int badge = 0, Style st = Style::Normal);
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
    /// Ease a 0..1 value
    static float ease(float t) { return 1.f - (1.f - t) * (1.f - t) * (1.f - t); }

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
};

} // namespace weiler::ui
