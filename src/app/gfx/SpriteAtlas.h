// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Batch.h"
#include "ogl/SpriteRenderer.h"
#include <functional>
#include <memory>
#include <vector>

namespace weiler::gfx {

/// Texture atlas for the sprites of the original data. Implements the drawing interface of the game objects.
class SpriteAtlas : public ISpriteRenderer
{
public:
    explicit SpriteAtlas(Batch& batch);
    ~SpriteAtlas() override;

    void drawSprite(const ISpriteSource& src, const Rect& dst, const Rect& srcRect, unsigned color,
                    unsigned playerColor) override;
    void drawRect(const Rect& rect, unsigned color) override;
    void drawLine(DrawPoint pt1, DrawPoint pt2, unsigned width, unsigned color) override;
    void drawText(DrawPoint pt, const std::string& text, unsigned color) override;
    void releaseSource(const ISpriteSource& src) override;

    /// Draw a whole source to a rectangle in current batch coordinates (used by the user interface)
    void drawSource(const ISpriteSource& src, const Rect2& dst, Color c = {}, unsigned playerColor = 0);

    /// Optional text drawer for drawText
    std::function<void(float, float, const std::string&, Color)> textDrawer;

    unsigned numPages() const { return static_cast<unsigned>(pages_.size()); }
    unsigned numSprites() const { return numSprites_; }

private:
    struct Entry
    {
        uint16_t page = 0;
        int x = 0, y = 0, w = 0, h = 0;
        bool player = false;
        bool valid = false;
    };
    struct Page
    {
        Texture tex;
        int shelfY = 0, shelfH = 0, cursorX = 0;
    };
    Entry* lookup(const ISpriteSource& src);
    bool allocate(int w, int h, uint16_t& page, int& x, int& y);

    Batch& batch_;
    std::vector<Page> pages_;
    std::vector<Entry> entries_;
    std::vector<uint32_t> freeHandles_;
    unsigned numSprites_ = 0;
};

} // namespace weiler::gfx
