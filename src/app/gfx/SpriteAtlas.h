// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Batch.h"
#include "ogl/SpriteRenderer.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace weiler::gfx {

/// Texture atlas for the sprites of the original data. Implements the drawing interface of the game objects.
/// All pages are layers of one texture array, so sprites of different pages never break a draw call.
/// The sprites of a game are created in one go when the game is loaded (preload, decoded on all cores);
/// sprites that were not preloaded are added on first use.
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

    /// Create the given sprites now (skips those that already exist). Decoding runs on several threads.
    void preload(const std::vector<const ISpriteSource*>& sources);
    /// Preload all sprites of a game (call after the game graphics are loaded): caches of the loader (figures,
    /// buildings, trees, ...), map objects, work animations and ships. Forgets the sprites of an earlier game.
    void preloadGameSprites();

    /// Upload the sprites that were requested during the last frame (call before drawing)
    void beginFrame();

    /// Optional text drawer for drawText
    std::function<void(float, float, const std::string&, Color)> textDrawer;

    unsigned numPages() const { return layers_; }
    unsigned numSprites() const { return numSprites_; }
    /// Sprites created on first use since the last preload (should stay near 0 during a game)
    unsigned lateSprites() const { return lateSprites_; }
    /// Short description for measurements (pages, sprites, fill)
    std::string describe() const;

private:
    struct Entry
    {
        uint16_t page = 0;
        int x = 0, y = 0, w = 0, h = 0;
        bool player = false;
        bool valid = false;
        bool ready = true; ///< Uploaded
    };
    struct Shelf
    {
        int y = 0, h = 0, x = 0;
    };
    struct Page
    {
        std::vector<Shelf> shelves;
        int used = 0; ///< Height used by shelves
    };
    struct Decoded
    {
        const ISpriteSource* src = nullptr;
        int w = 0, h = 0; ///< Buffer size (twice the width with player layer)
        int baseW = 0;
        bool player = false;
        std::vector<uint8_t> rgba;
    };
    struct Pending
    {
        Decoded d;
        Entry e;
        uint32_t index = 0;
    };
    Entry* find(const ISpriteSource& src);
    Entry* lookup(const ISpriteSource& src);
    /// Forget all sprites (new game: the loader caches are refilled)
    void reset();
    static void decode(Decoded& d);
    bool allocate(int w, int h, uint16_t& page, int& x, int& y);
    /// Make sure the texture array has at least n layers (keeps the content)
    void ensureLayers(unsigned n);
    void addEntry(const Decoded& d, const Entry& e);

    Batch& batch_;
    GLuint tex_ = 0;
    int pageSize_ = 2048;
    unsigned layers_ = 0;
    std::vector<Page> pages_;
    std::vector<Entry> entries_;
    std::vector<uint32_t> freeHandles_;
    unsigned numSprites_ = 0, lateSprites_ = 0;
    uint32_t generation_ = 1;
    std::vector<Pending> pending_;
};

} // namespace weiler::gfx
