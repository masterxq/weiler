// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Text.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <hb-ft.h>
#include <hb.h>
#include <cmath>
#include <algorithm>
#include <unordered_map>

namespace weiler::gfx {

namespace {
constexpr int ATLAS = 2048;
}

struct Font::Size
{
    FT_Face face = nullptr;
    hb_font_t* hbFont = nullptr;
    std::unordered_map<unsigned, Glyph> glyphs;
    float ascender = 0;
};

struct Font::Impl
{
    FT_Library lib = nullptr;
    std::string path;
    std::map<int, Size> sizes;
    hb_buffer_t* buf = nullptr;
};

Font::Font() : impl_(std::make_unique<Impl>()) {}

Font::~Font()
{
    for(auto& [px, s] : impl_->sizes)
    {
        hb_font_destroy(s.hbFont);
        FT_Done_Face(s.face);
    }
    if(impl_->buf)
        hb_buffer_destroy(impl_->buf);
    if(impl_->lib)
        FT_Done_FreeType(impl_->lib);
}

bool Font::load(const std::string& path)
{
    if(FT_Init_FreeType(&impl_->lib))
        return false;
    FT_Face test;
    if(FT_New_Face(impl_->lib, path.c_str(), 0, &test))
        return false;
    FT_Done_Face(test);
    impl_->path = path;
    impl_->buf = hb_buffer_create();
    std::vector<uint8_t> zero(static_cast<size_t>(ATLAS) * ATLAS, 0);
    atlas_.create(ATLAS, ATLAS, true, true, zero.data());
    return true;
}

Font::Size& Font::size(int px)
{
    auto it = impl_->sizes.find(px);
    if(it != impl_->sizes.end())
        return it->second;
    Size& s = impl_->sizes[px];
    FT_New_Face(impl_->lib, impl_->path.c_str(), 0, &s.face);
    FT_Set_Pixel_Sizes(s.face, 0, static_cast<FT_UInt>(px));
    s.hbFont = hb_ft_font_create_referenced(s.face);
    s.ascender = static_cast<float>(s.face->size->metrics.ascender) / 64.f;
    return s;
}

const Font::Glyph& Font::glyph(Size& s, unsigned glyphIndex)
{
    auto it = s.glyphs.find(glyphIndex);
    if(it != s.glyphs.end())
        return it->second;
    Glyph& g = s.glyphs[glyphIndex];
    if(FT_Load_Glyph(s.face, glyphIndex, FT_LOAD_DEFAULT) || FT_Render_Glyph(s.face->glyph, FT_RENDER_MODE_NORMAL))
        return g;
    const FT_Bitmap& bmp = s.face->glyph->bitmap;
    g.w = static_cast<int>(bmp.width);
    g.h = static_cast<int>(bmp.rows);
    g.left = s.face->glyph->bitmap_left;
    g.top = s.face->glyph->bitmap_top;
    if(g.w > 0 && g.h > 0)
    {
        if(cursorX_ + g.w + 2 > ATLAS)
        {
            cursorX_ = 0;
            cursorY_ += rowH_ + 2;
            rowH_ = 0;
        }
        if(cursorY_ + g.h + 2 > ATLAS)
        {
            // Atlas full: start over (rare, only with many sizes)
            for(auto& [px, sz] : impl_->sizes)
                sz.glyphs.clear();
            cursorX_ = cursorY_ = rowH_ = 0;
            return glyph(s, glyphIndex);
        }
        std::vector<uint8_t> pixels(static_cast<size_t>(g.w) * g.h);
        for(int y = 0; y < g.h; ++y)
            std::copy_n(bmp.buffer + y * bmp.pitch, g.w, pixels.begin() + static_cast<ptrdiff_t>(y) * g.w);
        g.x = cursorX_ + 1;
        g.y = cursorY_ + 1;
        atlas_.upload(g.x, g.y, g.w, g.h, pixels.data(), true);
        cursorX_ += g.w + 2;
        rowH_ = std::max(rowH_, g.h);
    }
    g.valid = true;
    return g;
}

void Font::shape(const std::string& text, Size& s, std::vector<Shaped>& out)
{
    out.clear();
    hb_buffer_t* buf = impl_->buf;
    hb_buffer_reset(buf);
    hb_buffer_add_utf8(buf, text.c_str(), static_cast<int>(text.size()), 0, static_cast<int>(text.size()));
    hb_buffer_guess_segment_properties(buf);
    hb_shape(s.hbFont, buf, nullptr, 0);
    unsigned n = 0;
    hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buf, &n);
    hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buf, &n);
    for(unsigned i = 0; i < n; ++i)
        out.push_back({info[i].codepoint, pos[i].x_offset / 64.f, pos[i].y_offset / 64.f, pos[i].x_advance / 64.f});
}

float Font::measure(const std::string& text, float sizePx)
{
    if(!impl_->lib || text.empty())
        return 0;
    Size& s = size(std::max(1, static_cast<int>(std::lround(sizePx))));
    std::vector<Shaped> shaped;
    shape(text, s, shaped);
    float w = 0;
    for(const auto& g : shaped)
        w += g.advance;
    return w;
}

float Font::draw(Batch& batch, float x, float y, const std::string& text, float sizePx, Color c, Align align)
{
    if(!impl_->lib || text.empty())
        return 0;
    // Rasterize at the final pixel size (the batch scale is applied to the size)
    const float bs = batch.scale();
    const int px = std::max(1, static_cast<int>(std::lround(sizePx * bs)));
    Size& s = size(px);
    std::vector<Shaped> shaped;
    shape(text, s, shaped);
    float width = 0;
    for(const auto& g : shaped)
        width += g.advance;
    const float inv = 1.f / bs;
    float penX = x * bs;
    if(align == Align::Center)
        penX -= width / 2;
    else if(align == Align::Right)
        penX -= width;
    // Baseline: center the ascender in the line box
    const float lineH = lineHeight(static_cast<float>(px));
    const float baseline = std::round(y * bs + (lineH - static_cast<float>(px)) / 2 + s.ascender);
    const float invA = 1.f / ATLAS;
    for(const auto& sg : shaped)
    {
        const Glyph& g = glyph(s, sg.glyph);
        if(g.valid && g.w > 0)
        {
            const float gx = std::round(penX + sg.xOff) + static_cast<float>(g.left);
            const float gy = baseline - sg.yOff - static_cast<float>(g.top);
            batch.quad(atlas_.id(), gx * inv, gy * inv, (gx + static_cast<float>(g.w)) * inv,
                       (gy + static_cast<float>(g.h)) * inv, g.x * invA, g.y * invA, (g.x + g.w) * invA,
                       (g.y + g.h) * invA, c, Batch::KindGlyph);
        }
        penX += sg.advance;
    }
    return width * inv;
}

std::vector<std::string> Font::wrap(const std::string& text, float sizePx, float maxWidth)
{
    std::vector<std::string> lines;
    size_t start = 0;
    while(start <= text.size())
    {
        size_t nl = text.find('\n', start);
        const std::string para = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
        std::string line;
        size_t p = 0;
        while(p < para.size())
        {
            size_t sp = para.find(' ', p);
            const std::string word = para.substr(p, sp == std::string::npos ? std::string::npos : sp - p);
            const std::string candidate = line.empty() ? word : line + " " + word;
            if(!line.empty() && measure(candidate, sizePx) > maxWidth)
            {
                lines.push_back(line);
                line = word;
            } else
                line = candidate;
            if(sp == std::string::npos)
                break;
            p = sp + 1;
        }
        lines.push_back(line);
        if(nl == std::string::npos)
            break;
        start = nl + 1;
    }
    return lines;
}

float Font::drawWrapped(Batch& batch, float x, float y, float maxWidth, const std::string& text, float sizePx,
                        Color c, Align align)
{
    const auto lines = wrap(text, sizePx, maxWidth);
    float cy = y;
    const float ax = align == Align::Center ? x + maxWidth / 2 : (align == Align::Right ? x + maxWidth : x);
    for(const auto& l : lines)
    {
        draw(batch, ax, cy, l, sizePx, c, align);
        cy += lineHeight(sizePx);
    }
    return cy - y;
}

} // namespace weiler::gfx
