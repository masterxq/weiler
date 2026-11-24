// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Ui.h"
#include "App.h"
#include "ogl/SpriteRenderer.h"
#include <algorithm>
#include <cmath>
#include <iterator>

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
            scrollGesture_ = false;
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
    ++frame_;
    const double now = app_.time();
    dt_ = static_cast<float>(std::clamp(now - lastTime_, 0.0, 0.1));
    lastTime_ = now;
    // Forget animation states of elements not drawn for a while
    if(frame_ % 600 == 0)
    {
        for(auto it = anims_.begin(); it != anims_.end();)
            it = frame_ - it->second.frame > 600 ? anims_.erase(it) : std::next(it);
    }
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

float Ui::anim(Id i, float target, float duration)
{
    auto it = anims_.find(i);
    if(it == anims_.end())
    {
        anims_.emplace(i, AnimState{target, frame_});
        return target;
    }
    AnimState& st = it->second;
    if(st.frame != frame_)
    {
        // Exponential approach, frame rate independent; snaps when close
        const float k = duration > 0 ? 1.f - std::exp(-dt_ * 3.f / duration) : 1.f;
        st.value += (target - st.value) * k;
        if(std::abs(target - st.value) < 0.002f)
            st.value = target;
        st.frame = frame_;
    }
    return st.value;
}

float Ui::pressAnim(Id i, bool down)
{
    return anim(i ^ 0x5bd1e995u, down ? 1.f : 0.f, down ? theme.motion.fast * 0.5f : theme.motion.normal);
}

void Ui::activated(int kind)
{
    if(onActivate)
        onActivate(kind);
}

void Ui::elevate(const Rect2& r, float radius, const Theme::Elevation& e, float opacity)
{
    auto& b = batch();
    const float amb = dp(e.blur) * 1.6f;
    // The glass drawn on top is opaque when fully shown: the shadow below its inner part is invisible
    const Rect2 inner = r.inset(radius + dp(1));
    const Rect2* covered = opacity >= 0.999f && inner.w > 0 && inner.h > 0 ? &inner : nullptr;
    b.shadow({r.x - dp(2), r.y + dp(e.y) * 0.4f, r.w + dp(4), r.h + dp(2)}, radius + dp(4), amb,
             theme.shadow.withAlpha(e.alpha * 0.45f * opacity), covered);
    b.shadow({r.x + dp(1), r.y + dp(e.y), r.w - dp(2), r.h}, radius, dp(e.blur) * 0.6f,
             theme.shadow.withAlpha(e.alpha * opacity), covered);
}

void Ui::panel(const Rect2& r, float radius, Color tint, float opacity)
{
    if(radius < 0)
        radius = dp(theme.radius.xl);
    if(tint.a < 0)
        tint = theme.glass;
    auto& b = batch();
    elevate(r, radius, theme.mid, opacity);
    b.glass(r, radius, tint, opacity);
    // Edge: brighter on top (light from above), fading towards the bottom
    b.roundRectV(r, radius, theme.highlight.withAlpha(theme.highlight.a * opacity),
                 theme.line.withAlpha(theme.line.a * 0.5f * opacity), 1.f, dp(1));
    block(r);
}

void Ui::heading(float x, float y, const std::string& s, float size, Color c, gfx::Align align)
{
    app_.fontDisplay().draw(batch(), x, y, s, size, c, align);
}

float Ui::headingWidth(const std::string& s, float size)
{
    return app_.fontDisplay().measure(s, size);
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

void Ui::picture(const gfx::Texture& tex, const Rect2& r)
{
    if(!tex || r.w <= 0 || r.h <= 0)
        return;
    const float ta = static_cast<float>(tex.width()) / static_cast<float>(tex.height()), ra = r.w / r.h;
    float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
    if(ta > ra)
    {
        const float f = ra / ta;
        u0 = (1 - f) / 2;
        u1 = u0 + f;
    } else
    {
        const float f = ta / ra;
        v0 = (1 - f) / 2;
        v1 = v0 + f;
    }
    batch().quad(tex.id(), r.x, r.y, r.right(), r.bottom(), u0, v0, u1, v1, Color{1, 1, 1, 1});
    batch().roundRect(r, dp(3), theme.highlight, 1.f, dp(1));
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
    const bool hitIt = released_ && !dragging_ && r.contains(pressX_, pressY_) && r.contains(relX_, relY_);
    if(hitIt)
        activated();
    return hitIt;
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

namespace {
Color mix(Color a, Color b, float t)
{
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}
Color darken(Color c, float f)
{
    return {c.r * f, c.g * f, c.b * f, c.a};
}
/// Shrink a rectangle around its center
Rect2 squeeze(const Rect2& r, float f)
{
    const float dx = r.w * (1 - f) / 2, dy = r.h * (1 - f) / 2;
    return {r.x + dx, r.y + dy, r.w - 2 * dx, r.h - 2 * dy};
}
} // namespace

bool Ui::button(Id i, const Rect2& rIn, const std::string& label, Icon ic, Style st, bool enabled)
{
    auto& b = batch();
    const float p = pressAnim(i, enabled && pressed(i, rIn));
    const Rect2 r = squeeze(rIn, 1.f - 0.035f * p);
    const float radius = std::min(dp(theme.radius.md), r.h / 2);
    const float dim = enabled ? 1.f : 0.4f;
    Color fg = theme.text;
    switch(st)
    {
        case Style::Primary:
        case Style::TilePrimary:
        case Style::Danger:
        {
            const Color top = st != Style::Danger ? theme.accent : theme.danger;
            const Color bottom = st != Style::Danger ? theme.accentDeep : darken(theme.danger, 0.85f);
            const float f = 1.f - 0.12f * p;
            if(enabled)
                b.shadow({r.x + dp(4), r.y + dp(5), r.w - dp(8), r.h}, radius, dp(16),
                         top.withAlpha(0.28f * (1 - p * 0.5f)));
            b.roundRectV(r, radius, darken(top, f).withAlpha(dim), darken(bottom, f).withAlpha(dim));
            b.roundRectV(r, radius, Color::rgb(0xFFFFFF, 0.28f * dim), Color::rgb(0xFFFFFF, 0.0f), 1.f, dp(1));
            fg = st != Style::Danger ? theme.accentText : Color::rgb(0xFFFFFF);
            break;
        }
        case Style::Normal:
        case Style::Tile:
        case Style::Nav:
            b.roundRect(r, radius, mix(theme.surface, theme.surfacePressed, p).withAlpha(
                                     mix(theme.surface, theme.surfacePressed, p).a * dim));
            b.roundRectV(r, radius, theme.highlight.withAlpha(0.10f * dim), theme.line.withAlpha(0.04f * dim), 1.f,
                         dp(1));
            break;
        case Style::Flat:
            if(p > 0.01f)
                b.roundRect(r, radius, theme.surfacePressed.withAlpha(theme.surfacePressed.a * p));
            break;
    }
    if(!enabled)
        fg = theme.textFaint;
    const bool plain = st == Style::Normal || st == Style::Flat || st == Style::Tile || st == Style::Nav;
    const Color iconColor = plain && enabled ? theme.accent : fg;
    if(st == Style::Tile || st == Style::TilePrimary)
    {
        const float is = std::min(dp(26), r.h * 0.34f);
        float fs = dp(theme.type.small);
        const float tw = textWidth(label, fs, true);
        if(tw > r.w - dp(12))
            fs *= (r.w - dp(12)) / tw;
        const float gap = dp(8);
        const float total = is + gap + fs * 1.2f;
        const float y0 = r.y + (r.h - total) / 2;
        icon(ic, r.x + r.w / 2, y0 + is / 2, is, iconColor);
        text(r.x + r.w / 2, y0 + is + gap - fs * 0.1f, label, fs, fg, gfx::Align::Center, true);
        if(enabled)
            tag(label, rIn);
        return enabled && area(i, rIn);
    }
    const float fs = std::min(dp(theme.type.label), r.h * 0.36f);
    const float is = std::min(dp(theme.size.icon), r.h * 0.46f);
    if(st == Style::Nav)
    {
        float x = r.x + dp(18);
        if(ic != Icon::None)
        {
            icon(ic, x + is / 2, r.y + r.h / 2, is, iconColor);
            x += is + dp(14);
        }
        // Narrow buttons (landscape menu grid): the label shrinks instead of running into the arrow; on narrow grids
        // the arrow goes for all buttons alike (it is only a hint)
        const float tw = textWidth(label, fs, true);
        const bool arrow = r.w >= dp(160) && tw * 0.75f <= r.right() - x - dp(38);
        const float room = r.right() - x - dp(arrow ? 38 : 12);
        const float lfs = tw > room ? fs * std::max(0.6f, room / tw) : fs;
        text(x, r.y + (r.h - lfs * 1.3f) / 2, label, lfs, fg, gfx::Align::Left, true);
        if(arrow)
            icon(Icon::Next, r.right() - dp(22), r.y + r.h / 2, dp(16), theme.textFaint);
        if(enabled)
            tag(label, rIn);
        return enabled && area(i, rIn);
    }
    if(ic != Icon::None && label.empty())
        icon(ic, r.x + r.w / 2, r.y + r.h / 2, is, fg);
    else if(ic != Icon::None)
    {
        // Narrow buttons: the label shrinks (down to 75 %)
        const float room = r.w - dp(28) - is - dp(10);
        const float tw0 = textWidth(label, fs, true);
        const float lfs = tw0 > room ? fs * std::max(0.75f, room / tw0) : fs;
        const float tw = tw0 * lfs / fs;
        const float total = is + dp(10) + tw;
        const float x0 = r.x + std::max(dp(8), (r.w - total) / 2);
        icon(ic, x0 + is / 2, r.y + r.h / 2, is, iconColor);
        text(x0 + is + dp(10), r.y + (r.h - lfs * 1.3f) / 2, label, lfs, fg, gfx::Align::Left, true);
    } else
    {
        const float tw = textWidth(label, fs, true);
        const float lfs = tw > r.w - dp(20) ? fs * std::max(0.75f, (r.w - dp(20)) / tw) : fs;
        text(r.x + r.w / 2, r.y + (r.h - lfs * 1.3f) / 2, label, lfs, fg, gfx::Align::Center, true);
    }
    // Disabled buttons are no touch targets for test scripts
    if(enabled)
        tag(label.empty() ? std::string("icon:") + iconName(ic) : label, rIn);
    return enabled && area(i, rIn);
}

bool Ui::iconButton(Id i, const Rect2& rIn, Icon ic, bool active, int badge, Style st)
{
    auto& b = batch();
    const float p = pressAnim(i, pressed(i, rIn));
    const float act = anim(i ^ 0xac71u, active ? 1.f : 0.f, theme.motion.normal);
    const Rect2 r = squeeze(rIn, 1.f - 0.06f * p);
    const float radius = std::min(r.w, r.h) / 2;
    if(st != Style::Flat)
    {
        elevate(r, radius, theme.low);
        b.glass(r, radius, theme.glass);
        b.roundRectV(r, radius, theme.highlight, theme.line.withAlpha(0.03f), 1.f, dp(1));
    }
    if(act > 0.01f)
        b.roundRectV(squeeze(r, 0.86f + 0.14f * act), radius, theme.accent.withAlpha(act),
                     theme.accentDeep.withAlpha(act));
    if(p > 0.01f)
        b.roundRect(r, radius, theme.surfacePressed.withAlpha(theme.surfacePressed.a * p));
    icon(ic, r.x + r.w / 2, r.y + r.h / 2, std::min(r.w, r.h) * 0.46f, mix(theme.text, theme.accentText, act));
    if(badge > 0)
    {
        const float br = dp(9);
        const float bx = rIn.right() - br * 0.9f, by = rIn.y + br * 0.9f;
        b.circle(bx, by, br + dp(2), Color::rgb(0x000000, 0.35f), 0, dp(3));
        b.circle(bx, by, br, theme.danger);
        const std::string t = badge > 99 ? "99" : std::to_string(badge);
        text(bx, by - dp(7), t, dp(11), Color::rgb(0xFFFFFF), gfx::Align::Center, true);
    }
    tag(std::string("icon:") + iconName(ic), rIn);
    return area(i, rIn);
}

bool Ui::row(Id i, const Rect2& r, bool selected)
{
    auto& b = batch();
    const float p = pressAnim(i, pressed(i, r));
    const float sel = anim(i ^ 0x5e1u, selected ? 1.f : 0.f, theme.motion.normal);
    const float radius = dp(theme.radius.md);
    b.roundRect(r, radius, mix(theme.surface.withAlpha(0.035f), theme.surfacePressed, p));
    if(sel > 0.01f)
    {
        b.roundRect(r, radius, theme.accentSoft.withAlpha(theme.accentSoft.a * sel));
        b.roundRect(r, radius, theme.accent.withAlpha(0.75f * sel), 1.f, dp(1.5f));
        b.roundRect({r.x + dp(6), r.y + r.h * 0.25f, dp(3), r.h * 0.5f}, dp(1.5f), theme.accent.withAlpha(sel));
    }
    return area(i, r);
}

bool Ui::slider(Id i, const Rect2& r, float& value, float lo, float hi, float step)
{
    auto& b = batch();
    block(r);
    const float trackH = dp(6);
    const float knob = dp(12);
    const float x0 = r.x + knob, x1 = r.right() - knob;
    const float cy = r.y + r.h / 2;
    bool changed = false;
    // In a list a slider only takes a sideways drag or a tap, so that scrolling over it changes nothing
    const bool inList = !scrollStack_.empty();
    const float dx = std::abs(curX_ - pressX_), dy = std::abs(curY_ - pressY_);
    const bool sideways = !scrollGesture_ && dx > dp(8) && dx > dy;
    const bool pressedHere = r.contains(pressX_, pressY_);
    const bool active = captured_ && pressedHere
                        && (activeSlider_ == i || (activeSlider_ == 0 && (!inList || sideways)));
    const bool tapped = inList && released_ && !dragging_ && pressedHere && r.contains(relX_, relY_);
    if(active || tapped)
    {
        activeSlider_ = active ? i : activeSlider_;
        dragging_ = dragging_ || active;
        float t = std::clamp(((active ? curX_ : relX_) - x0) / std::max(1.f, x1 - x0), 0.f, 1.f);
        float v = lo + t * (hi - lo);
        if(step > 0)
            v = std::round(v / step) * step;
        if(v != value)
        {
            value = v;
            changed = true;
        }
    }
    const float grow = anim(i ^ 0x511du, active ? 1.f : 0.f, theme.motion.fast);
    const float t = hi > lo ? std::clamp((value - lo) / (hi - lo), 0.f, 1.f) : 0.f;
    b.roundRect({x0, cy - trackH / 2, x1 - x0, trackH}, trackH / 2, Color::rgb(0xFFFFFF, 0.10f));
    b.roundRectV({x0, cy - trackH / 2, (x1 - x0) * t, trackH}, trackH / 2, theme.accent, theme.accentDeep);
    const float kx = x0 + (x1 - x0) * t;
    if(grow > 0.01f)
        b.circle(kx, cy, knob * (1.f + 0.9f * grow), theme.accent.withAlpha(0.18f * grow));
    b.circle(kx, cy + dp(1.5f), knob, Color::rgb(0x000000, 0.35f), 0, dp(4));
    b.circle(kx, cy, knob, theme.text);
    return changed;
}

bool Ui::toggle(Id i, const Rect2& r, bool& value, const std::string& label)
{
    auto& b = batch();
    const float h = dp(30), w = dp(52);
    const float fs = dp(theme.type.label);
    const float on = anim(i ^ 0x7091u, value ? 1.f : 0.f, theme.motion.normal);
    text(r.x, r.y + (r.h - fs * 1.3f) / 2, label, fs, theme.text);
    const Rect2 sw{r.right() - w, r.y + (r.h - h) / 2, w, h};
    b.roundRect(sw, h / 2, Color::rgb(0xFFFFFF, 0.12f));
    if(on > 0.01f)
        b.roundRectV(sw, h / 2, theme.accent.withAlpha(on), theme.accentDeep.withAlpha(on));
    const float kx = sw.x + h / 2 + (sw.w - h) * on;
    b.circle(kx, sw.y + h / 2 + dp(1), h / 2 - dp(3), Color::rgb(0x000000, 0.3f), 0, dp(3));
    b.circle(kx, sw.y + h / 2, h / 2 - dp(3), theme.text);
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
    const float radius = std::min(dp(theme.radius.md), r.h / 2);
    b.roundRect(r, radius, Color::rgb(0x000000, 0.22f));
    b.roundRect(r, radius, theme.line, 1.f, dp(1));
    const float w = r.w / static_cast<float>(std::max<size_t>(1, options.size()));
    bool changed = false;
    // One font size for all cells, small enough for the widest label
    float fs = std::min(dp(theme.type.body), r.h * 0.38f);
    for(const auto& o : options)
    {
        const float tw = textWidth(o, fs, true);
        if(tw > w - dp(10))
            fs *= (w - dp(10)) / tw;
    }
    // Selection pill slides between the cells
    const float pos = anim(i ^ 0x5e9u, static_cast<float>(selected), theme.motion.normal);
    if(selected >= 0)
    {
        const Rect2 pill = Rect2{r.x + w * pos, r.y, w, r.h}.inset(dp(3));
        b.shadow({pill.x, pill.y + dp(2), pill.w, pill.h}, radius - dp(2), dp(8), theme.shadow.withAlpha(0.3f));
        b.roundRectV(pill, radius - dp(2), theme.accent, theme.accentDeep);
    }
    for(size_t k = 0; k < options.size(); ++k)
    {
        const Rect2 cell{r.x + w * static_cast<float>(k), r.y, w, r.h};
        const float near = std::clamp(1.f - std::abs(pos - static_cast<float>(k)), 0.f, 1.f);
        const float p = pressAnim(i + k + 1, pressed(i + k + 1, cell));
        if(p > 0.01f)
            b.roundRect(cell.inset(dp(3)), radius - dp(2), theme.surfacePressed.withAlpha(theme.surfacePressed.a * p));
        text(cell.x + w / 2, r.y + (r.h - fs * 1.3f) / 2, options[k], fs, mix(theme.textDim, theme.accentText, near),
             gfx::Align::Center, true);
        tag(options[k], cell);
        if(area(i + k + 1, cell) && static_cast<int>(k) != selected)
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
    // A press scrolls once it moves mostly vertically and no slider has taken it
    if(captured_ && dragging_ && view.contains(pressX_, pressY_) && activeSlider_ == 0
       && (scrollGesture_ || std::abs(curY_ - pressY_) >= std::abs(curX_ - pressX_)))
    {
        scrollGesture_ = true;
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
        batch().roundRect({s.view.right() - dp(4), y, dp(3), h}, dp(1.5f), theme.text.withAlpha(0.22f));
    }
}

} // namespace weiler::ui
