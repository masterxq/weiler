// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Ui.h"
#include "App.h"
#include "ogl/SpriteRenderer.h"
#include <algorithm>
#include <cmath>

namespace weiler::ui {

Ui::Ui(App& app) : app_(app) {}

float Ui::dp(float v) const
{
    return app_.dp(v);
}

gfx::Batch& Ui::batch()
{
    return app_.batch();
}

double Ui::time() const
{
    return app_.time();
}

bool Ui::hit(float x, float y) const
{
    for(const Block& b : lastBlocks_)
    {
        if(b.r.contains(x, y))
            return true;
    }
    return false;
}

bool Ui::onTop(Id i) const
{
    for(auto it = lastBlocks_.rbegin(); it != lastBlocks_.rend(); ++it)
    {
        if(it->r.contains(pressX_, pressY_))
            return it->owner == i;
    }
    return true;
}

bool Ui::pointer(const PointerEvent& ev)
{
    switch(ev.type)
    {
        case PointerEvent::Type::Down:
            if(captured_ || !hit(ev.x, ev.y))
                return false;
            captured_ = true;
            dragging_ = false;
            longFired_ = false;
            pressX_ = curX_ = ev.x;
            pressY_ = curY_ = ev.y;
            pressTime_ = app_.time();
            dragDY_ = 0;
            return true;
        case PointerEvent::Type::Move:
            if(!captured_)
                return false;
            dragDY_ += ev.y - curY_;
            curX_ = ev.x;
            curY_ = ev.y;
            if(std::hypot(curX_ - pressX_, curY_ - pressY_) > dp(10))
                dragging_ = true;
            return true;
        case PointerEvent::Type::Up:
        case PointerEvent::Type::Cancel:
            if(!captured_)
                return false;
            captured_ = false;
            released_ = ev.type == PointerEvent::Type::Up;
            relX_ = ev.x;
            relY_ = ev.y;
            return true;
    }
    return false;
}

bool Ui::wheel(float x, float y, float delta)
{
    if(!hit(x, y))
        return false;
    wheelDelta_ += delta;
    wheelX_ = x;
    wheelY_ = y;
    return true;
}

void Ui::begin()
{
    blocks_.clear();
    targets_.clear();
}

void Ui::tag(const std::string& label, const Rect2& r)
{
    if(!app_.testMode() || label.empty())
        return;
    // Only the visible part inside a scroll view counts
    if(!scrollStack_.empty())
    {
        const Rect2& v = scroll_[scrollStack_.back()].view;
        if(r.y + r.h / 2 < v.y || r.y + r.h / 2 > v.bottom())
            return;
    }
    targets_.emplace_back(label, r);
}

bool Ui::findTarget(const std::string& pattern, Rect2& r, int nth) const
{
    const bool prefix = !pattern.empty() && pattern.back() == '*';
    const std::string p = prefix ? pattern.substr(0, pattern.size() - 1) : pattern;
    for(const auto& t : lastTargets_)
    {
        if((prefix ? t.first.rfind(p, 0) == 0 : t.first == p) && nth-- == 0)
        {
            r = t.second;
            return true;
        }
    }
    return false;
}

std::vector<std::string> Ui::targetLabels() const
{
    std::vector<std::string> res;
    for(const auto& t : lastTargets_)
        res.push_back(t.first);
    return res;
}

void Ui::end()
{
    lastBlocks_ = blocks_;
    lastTargets_.swap(targets_);
    released_ = false;
    dragDY_ = 0;
    wheelDelta_ = 0;
    if(!captured_)
        activeSlider_ = 0;
}

void Ui::panel(const Rect2& r, float radius, Color c)
{
    if(radius < 0)
        radius = dp(18);
    if(c.a < 0)
        c = theme.panel;
    auto& b = batch();
    b.shadow({r.x, r.y + dp(4), r.w, r.h}, radius, dp(18), theme.shadow);
    b.roundRect(r, radius, c);
    b.roundRect(r, radius, theme.line, 1.f, dp(1));
    block(r);
}

void Ui::text(float x, float y, const std::string& s, float size, Color c, gfx::Align align, bool bold)
{
    (bold ? app_.fontBold() : app_.font()).draw(batch(), x, y, s, size, c, align);
}

float Ui::textWidth(const std::string& s, float size, bool bold)
{
    return (bold ? app_.fontBold() : app_.font()).measure(s, size);
}

float Ui::textWrapped(float x, float y, float w, const std::string& s, float size, Color c, gfx::Align align)
{
    return app_.font().drawWrapped(batch(), x, y, w, s, size, c, align);
}

void Ui::icon(Icon i, float cx, float cy, float size, Color c)
{
    app_.icons().draw(batch(), i, cx, cy, size, c);
}

void Ui::sprite(const ISpriteSource* src, const Rect2& r, unsigned playerColor, float alpha)
{
    if(!src)
        return;
    const Extent s = src->getSourceSize();
    if(s.x == 0 || s.y == 0)
        return;
    const float scale = std::min(r.w / static_cast<float>(s.x), r.h / static_cast<float>(s.y));
    const float w = static_cast<float>(s.x) * scale, h = static_cast<float>(s.y) * scale;
    app_.atlas().drawSource(*src, {r.x + (r.w - w) / 2, r.y + (r.h - h) / 2, w, h}, Color{1, 1, 1, alpha}, playerColor);
}

bool Ui::pressed(Id, const Rect2& r) const
{
    return captured_ && !dragging_ && r.contains(pressX_, pressY_) && r.contains(curX_, curY_);
}

bool Ui::area(Id i, const Rect2& r)
{
    // Inside a scroll view only the visible part is clickable
    if(!scrollStack_.empty())
    {
        const Rect2& v = scroll_[scrollStack_.back()].view;
        const float y0 = std::max(r.y, v.y), y1 = std::min(r.bottom(), v.bottom());
        if(y1 > y0)
            block({r.x, y0, r.w, y1 - y0}, i);
        if(!v.contains(pressX_, pressY_))
            return false;
    } else
        block(r, i);
    if(released_ && !onTop(i))
        return false;
    return released_ && !dragging_ && r.contains(pressX_, pressY_) && r.contains(relX_, relY_);
}

bool Ui::longPress(Id, const Rect2& r)
{
    if(captured_ && !dragging_ && !longFired_ && r.contains(pressX_, pressY_) && app_.time() - pressTime_ > 0.5)
    {
        longFired_ = true;
        dragging_ = true; // no click afterwards
        return true;
    }
    return false;
}

bool Ui::button(Id i, const Rect2& r, const std::string& label, Icon ic, Style st, bool enabled)
{
    auto& b = batch();
    const bool down = enabled && pressed(i, r);
    Color bg = st == Style::Primary ? theme.accent : st == Style::Danger ? theme.danger : theme.raised;
    Color fg = st == Style::Primary ? theme.accentText : theme.text;
    if(st == Style::Flat)
        bg = Color{1, 1, 1, 0.0f};
    if(!enabled)
    {
        bg = bg.withAlpha(bg.a * 0.4f);
        fg = theme.textDim.withAlpha(0.6f);
    }
    if(down)
        bg = Color{std::min(1.f, bg.r + 0.08f), std::min(1.f, bg.g + 0.08f), std::min(1.f, bg.b + 0.08f),
                   std::max(bg.a, 0.25f)};
    const float radius = std::min(dp(14), r.h / 2);
    b.roundRect(r, radius, bg);
    if(st == Style::Normal)
        b.roundRect(r, radius, theme.line, 1.f, dp(1));
    const float fs = std::min(dp(17), r.h * 0.38f);
    const float is = std::min(dp(24), r.h * 0.5f);
    if(ic != Icon::None && label.empty())
        icon(ic, r.x + r.w / 2, r.y + r.h / 2, is, fg);
    else if(ic != Icon::None)
    {
        const float tw = textWidth(label, fs, true);
        const float total = is + dp(8) + tw;
        const float x0 = r.x + std::max(dp(12), (r.w - total) / 2);
        icon(ic, x0 + is / 2, r.y + r.h / 2, is, fg);
        text(x0 + is + dp(8), r.y + (r.h - fs * 1.3f) / 2, label, fs, fg, gfx::Align::Left, true);
    } else
        text(r.x + r.w / 2, r.y + (r.h - fs * 1.3f) / 2, label, fs, fg, gfx::Align::Center, true);
    tag(label.empty() ? std::string("icon:") + iconName(ic) : label, r);
    return enabled && area(i, r);
}

bool Ui::iconButton(Id i, const Rect2& r, Icon ic, bool active, int badge, Style st)
{
    auto& b = batch();
    const bool down = pressed(i, r);
    const float radius = std::min(r.w, r.h) / 2;
    Color bg = active ? theme.accent : theme.panel;
    if(st == Style::Flat && !active)
        bg = Color{0, 0, 0, 0};
    if(down)
        bg = active ? theme.accent.withAlpha(0.8f) : theme.raised;
    if(st != Style::Flat || active || down)
    {
        if(st != Style::Flat)
            b.shadow({r.x, r.y + dp(2), r.w, r.h}, radius, dp(10), theme.shadow.withAlpha(0.3f));
        b.roundRect(r, radius, bg);
    }
    icon(ic, r.x + r.w / 2, r.y + r.h / 2, std::min(r.w, r.h) * 0.5f, active ? theme.accentText : theme.text);
    if(badge > 0)
    {
        const float br = dp(9);
        const float bx = r.right() - br * 0.9f, by = r.y + br * 0.9f;
        b.circle(bx, by, br, theme.danger);
        const std::string t = badge > 99 ? "99" : std::to_string(badge);
        text(bx, by - dp(7), t, dp(11), Color::rgb(0xFFFFFF), gfx::Align::Center, true);
    }
    tag(std::string("icon:") + iconName(ic), r);
    return area(i, r);
}

bool Ui::slider(Id i, const Rect2& r, float& value, float lo, float hi, float step)
{
    auto& b = batch();
    block(r);
    const float trackH = dp(6);
    const float knob = dp(11);
    const float x0 = r.x + knob, x1 = r.right() - knob;
    const float cy = r.y + r.h / 2;
    bool changed = false;
    if(captured_ && r.contains(pressX_, pressY_) && (activeSlider_ == 0 || activeSlider_ == i))
    {
        activeSlider_ = i;
        dragging_ = true;
        float t = std::clamp((curX_ - x0) / std::max(1.f, x1 - x0), 0.f, 1.f);
        float v = lo + t * (hi - lo);
        if(step > 0)
            v = std::round(v / step) * step;
        if(v != value)
        {
            value = v;
            changed = true;
        }
    }
    const float t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.f, 1.f) : 0.f;
    b.roundRect({x0, cy - trackH / 2, x1 - x0, trackH}, trackH / 2, Color::rgb(0xFFFFFF, 0.12f));
    b.roundRect({x0, cy - trackH / 2, (x1 - x0) * t, trackH}, trackH / 2, theme.accent);
    b.circle(x0 + (x1 - x0) * t, cy, knob, theme.text);
    return changed;
}

bool Ui::toggle(Id i, const Rect2& r, bool& value, const std::string& label)
{
    auto& b = batch();
    const float h = dp(30), w = dp(52);
    const float fs = dp(16);
    text(r.x, r.y + (r.h - fs * 1.3f) / 2, label, fs, theme.text);
    const Rect2 sw{r.right() - w, r.y + (r.h - h) / 2, w, h};
    b.roundRect(sw, h / 2, value ? theme.accent : Color::rgb(0xFFFFFF, 0.15f));
    b.circle(value ? sw.right() - h / 2 : sw.x + h / 2, sw.y + h / 2, h / 2 - dp(3), theme.text);
    tag(label, r);
    if(area(i, r))
    {
        value = !value;
        return true;
    }
    return false;
}

bool Ui::segmented(Id i, const Rect2& r, const std::vector<std::string>& options, int& selected)
{
    auto& b = batch();
    const float radius = std::min(dp(12), r.h / 2);
    b.roundRect(r, radius, Color::rgb(0xFFFFFF, 0.06f));
    const float w = r.w / static_cast<float>(std::max<size_t>(1, options.size()));
    bool changed = false;
    // One font size for all cells, small enough for the widest label
    float fs = std::min(dp(15), r.h * 0.38f);
    for(const auto& o : options)
    {
        const float tw = textWidth(o, fs, true);
        if(tw > w - dp(10))
            fs *= (w - dp(10)) / tw;
    }
    for(size_t k = 0; k < options.size(); ++k)
    {
        const Rect2 cell{r.x + w * static_cast<float>(k), r.y, w, r.h};
        const bool sel = static_cast<int>(k) == selected;
        if(sel)
            b.roundRect(cell.inset(dp(3)), radius - dp(2), theme.accent);
        text(cell.x + w / 2, r.y + (r.h - fs * 1.3f) / 2, options[k], fs, sel ? theme.accentText : theme.text,
             gfx::Align::Center, true);
        tag(options[k], cell);
        if(area(i + k + 1, cell) && !sel)
        {
            selected = static_cast<int>(k);
            changed = true;
        }
    }
    return changed;
}

bool Ui::stepper(Id i, const Rect2& r, int& value, int lo, int hi, const std::string& label)
{
    const float bs = r.h;
    const float fs = dp(16);
    text(r.x, r.y + (r.h - fs * 1.3f) / 2, label, fs, theme.text);
    bool changed = false;
    const Rect2 minus{r.right() - bs * 2.6f, r.y, bs, bs}, plus{r.right() - bs, r.y, bs, bs};
    if(button(i + 1, minus, "", Icon::Minus, Style::Normal, value > lo))
    {
        --value;
        changed = true;
    }
    text(r.right() - bs * 1.3f, r.y + (r.h - fs * 1.3f) / 2, std::to_string(value), fs, theme.text, gfx::Align::Center,
         true);
    if(button(i + 2, plus, "", Icon::Plus, Style::Normal, value < hi))
    {
        ++value;
        changed = true;
    }
    return changed;
}

float Ui::beginScroll(Id i, const Rect2& view)
{
    ScrollState& s = scroll_[i];
    s.view = view;
    scrollStack_.push_back(i);
    block(view);
    if(captured_ && dragging_ && view.contains(pressX_, pressY_) && activeSlider_ == 0)
    {
        s.y -= dragDY_;
        s.vel = -dragDY_ * 60.f;
    } else if(!captured_)
    {
        s.y += s.vel / 60.f;
        s.vel *= 0.92f;
        if(std::abs(s.vel) < 1)
            s.vel = 0;
    }
    if(wheelDelta_ != 0 && view.contains(wheelX_, wheelY_))
        s.y -= wheelDelta_ * dp(60);
    s.y = std::clamp(s.y, 0.f, std::max(0.f, s.content - view.h));
    batch().setClip(view);
    return view.y - s.y;
}

void Ui::endScroll(float contentHeight)
{
    if(scrollStack_.empty())
        return;
    ScrollState& s = scroll_[scrollStack_.back()];
    s.content = contentHeight;
    scrollStack_.pop_back();
    batch().setClip({});
    // Scroll indicator
    if(contentHeight > s.view.h + 1)
    {
        const float h = s.view.h * s.view.h / contentHeight;
        const float y = s.view.y + (s.view.h - h) * (s.y / std::max(1.f, contentHeight - s.view.h));
        batch().roundRect({s.view.right() - dp(4), y, dp(3), h}, dp(1.5f), Color::rgb(0xFFFFFF, 0.25f));
    }
}

} // namespace weiler::ui
