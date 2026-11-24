// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SpriteAtlas.h"
#include "libsiedler2/PixelBufferBGRA.h"
#include <algorithm>
#include <cmath>

namespace weiler::gfx {

namespace {
constexpr int PAGE_SIZE = 2048;
constexpr int PAD = 1;
} // namespace

SpriteAtlas::SpriteAtlas(Batch& batch) : batch_(batch)
{
    entries_.emplace_back(); // handle 0 = invalid
    setSpriteRenderer(this);
}

SpriteAtlas::~SpriteAtlas()
{
    if(getSpriteRenderer() == this)
        setSpriteRenderer(nullptr);
}

bool SpriteAtlas::allocate(int w, int h, uint16_t& page, int& x, int& y)
{
    w += 2 * PAD;
    h += 2 * PAD;
    if(w > PAGE_SIZE || h > PAGE_SIZE)
        return false;
    for(size_t i = 0; i <= pages_.size(); ++i)
    {
        if(i == pages_.size())
        {
            pages_.emplace_back();
            std::vector<uint8_t> zero(static_cast<size_t>(PAGE_SIZE) * PAGE_SIZE * 4, 0);
            pages_.back().tex.create(PAGE_SIZE, PAGE_SIZE, false, true, zero.data());
        }
        Page& p = pages_[i];
        if(p.cursorX + w > PAGE_SIZE)
        {
            p.shelfY += p.shelfH;
            p.shelfH = 0;
            p.cursorX = 0;
        }
        if(p.shelfY + h > PAGE_SIZE)
            continue;
        x = p.cursorX + PAD;
        y = p.shelfY + PAD;
        p.cursorX += w;
        p.shelfH = std::max(p.shelfH, h);
        page = static_cast<uint16_t>(i);
        return true;
    }
    return false;
}

SpriteAtlas::Entry* SpriteAtlas::lookup(const ISpriteSource& src)
{
    if(src.rendererHandle && src.rendererHandle < entries_.size() && entries_[src.rendererHandle].valid)
        return &entries_[src.rendererHandle];
    const Extent size = src.getSourceSize();
    if(size.x == 0 || size.y == 0)
        return nullptr;
    const bool player = src.hasPlayerLayer();
    const int bufW = static_cast<int>(size.x) * (player ? 2 : 1);
    const int bufH = static_cast<int>(size.y);
    libsiedler2::PixelBufferBGRA buffer(bufW, bufH);
    src.renderSource(buffer);
    // Convert BGRA to premultiplied RGBA
    std::vector<uint8_t> rgba(static_cast<size_t>(bufW) * bufH * 4);
    const uint8_t* in = buffer.getPixelPtr();
    for(size_t i = 0; i < static_cast<size_t>(bufW) * bufH; ++i)
    {
        const unsigned a = in[i * 4 + 3];
        rgba[i * 4 + 0] = static_cast<uint8_t>(in[i * 4 + 2] * a / 255);
        rgba[i * 4 + 1] = static_cast<uint8_t>(in[i * 4 + 1] * a / 255);
        rgba[i * 4 + 2] = static_cast<uint8_t>(in[i * 4 + 0] * a / 255);
        rgba[i * 4 + 3] = static_cast<uint8_t>(a);
    }
    Entry e;
    if(!allocate(bufW, bufH, e.page, e.x, e.y))
        return nullptr;
    pages_[e.page].tex.upload(e.x, e.y, bufW, bufH, rgba.data());
    e.w = static_cast<int>(size.x);
    e.h = bufH;
    e.player = player;
    e.valid = true;
    uint32_t handle;
    if(!freeHandles_.empty())
    {
        handle = freeHandles_.back();
        freeHandles_.pop_back();
        entries_[handle] = e;
    } else
    {
        handle = static_cast<uint32_t>(entries_.size());
        entries_.push_back(e);
    }
    src.rendererHandle = handle;
    ++numSprites_;
    return &entries_[handle];
}

void SpriteAtlas::releaseSource(const ISpriteSource& src)
{
    // Atlas space is not reclaimed (sprites of the original data are loaded once)
    if(src.rendererHandle && src.rendererHandle < entries_.size() && entries_[src.rendererHandle].valid)
    {
        entries_[src.rendererHandle].valid = false;
        freeHandles_.push_back(src.rendererHandle);
        --numSprites_;
    }
    src.rendererHandle = 0;
}

void SpriteAtlas::drawSprite(const ISpriteSource& src, const Rect& dst, const Rect& srcRect, unsigned color,
                             unsigned playerColor)
{
    const Entry* e = lookup(src);
    if(!e)
        return;
    const Page& page = pages_[e->page];
    const float inv = 1.f / PAGE_SIZE;
    const float u0 = (e->x + srcRect.left) * inv, u1 = (e->x + srcRect.right) * inv;
    const float v0 = (e->y + srcRect.top) * inv, v1 = (e->y + srcRect.bottom) * inv;
    const auto x0 = static_cast<float>(dst.left), y0 = static_cast<float>(dst.top);
    const auto x1 = static_cast<float>(dst.right), y1 = static_cast<float>(dst.bottom);
    batch_.quad(page.tex.id(), x0, y0, x1, y1, u0, v0, u1, v1, Color::fromARGB(color));
    if(e->player && playerColor)
    {
        const float du = e->w * inv;
        batch_.quad(page.tex.id(), x0, y0, x1, y1, u0 + du, v0, u1 + du, v1, Color::fromARGB(playerColor));
    }
}

void SpriteAtlas::drawSource(const ISpriteSource& src, const Rect2& dst, Color c, unsigned playerColor)
{
    const Extent size = src.getSourceSize();
    const Entry* e = lookup(src);
    if(!e)
        return;
    const Page& page = pages_[e->page];
    const float inv = 1.f / PAGE_SIZE;
    const float u0 = e->x * inv, u1 = (e->x + static_cast<float>(size.x)) * inv;
    const float v0 = e->y * inv, v1 = (e->y + static_cast<float>(size.y)) * inv;
    batch_.quad(page.tex.id(), dst.x, dst.y, dst.right(), dst.bottom(), u0, v0, u1, v1, c);
    if(e->player && playerColor)
    {
        const float du = e->w * inv;
        batch_.quad(page.tex.id(), dst.x, dst.y, dst.right(), dst.bottom(), u0 + du, v0, u1 + du, v1,
                    Color::fromARGB(playerColor));
    }
}

void SpriteAtlas::drawRect(const Rect& rect, unsigned color)
{
    batch_.quad(0, static_cast<float>(rect.left), static_cast<float>(rect.top), static_cast<float>(rect.right),
                static_cast<float>(rect.bottom), 0, 0, 1, 1, Color::fromARGB(color));
}

void SpriteAtlas::drawLine(DrawPoint pt1, DrawPoint pt2, unsigned width, unsigned color)
{
    batch_.line(static_cast<float>(pt1.x), static_cast<float>(pt1.y), static_cast<float>(pt2.x),
                static_cast<float>(pt2.y), static_cast<float>(width), Color::fromARGB(color));
}

void SpriteAtlas::drawText(DrawPoint pt, const std::string& text, unsigned color)
{
    if(textDrawer)
        textDrawer(static_cast<float>(pt.x), static_cast<float>(pt.y), text, Color::fromARGB(color));
}

} // namespace weiler::gfx
