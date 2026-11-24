// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Batch.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace weiler::gfx {

enum class Align
{
    Left,
    Center,
    Right
};

/// Text rendering with FreeType (rasterization) and HarfBuzz (shaping), glyphs cached in an R8 atlas.
class Font
{
public:
    Font();
    ~Font();
    bool load(const std::string& path);

    /// Draw text; y is the top of the line box. Returns the width.
    float draw(Batch& batch, float x, float y, const std::string& text, float sizePx, Color c,
               Align align = Align::Left);
    float measure(const std::string& text, float sizePx);
    float lineHeight(float sizePx) const { return sizePx * 1.3f; }
    /// Split text into lines no wider than maxWidth (explicit \n respected)
    std::vector<std::string> wrap(const std::string& text, float sizePx, float maxWidth);
    /// Draw wrapped text, returns the used height
    float drawWrapped(Batch& batch, float x, float y, float maxWidth, const std::string& text, float sizePx, Color c,
                      Align align = Align::Left);

private:
    struct Glyph
    {
        int x = 0, y = 0, w = 0, h = 0;
        int left = 0, top = 0;
        bool valid = false;
    };
    struct Shaped
    {
        unsigned glyph;
        float xOff, yOff, advance;
    };
    struct Size;
    Size& size(int px);
    const Glyph& glyph(Size& s, unsigned glyphIndex);
    void shape(const std::string& text, Size& s, std::vector<Shaped>& out);

    struct Impl;
    std::unique_ptr<Impl> impl_;
    Texture atlas_;
    int cursorX_ = 0, cursorY_ = 0, rowH_ = 0;
};

} // namespace weiler::gfx
