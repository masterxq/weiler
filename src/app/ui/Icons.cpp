// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Icons.h"
#include <iterator>
#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace weiler::ui {

namespace {
struct P
{
    float x, y;
};
/// Shape primitive in a 24x24 design grid
struct Prim
{
    enum Kind
    {
        Disk,
        Seg,
        RRect,
        Poly
    } kind;
    bool cut;
    float a = 0, b = 0, c = 0, d = 0, r = 0, w = 0;
    std::vector<P> pts;
};

class Shape
{
public:
    Shape& disk(float cx, float cy, float r, bool cut = false)
    {
        prims.push_back({Prim::Disk, cut, cx, cy, 0, 0, r, 0, {}});
        return *this;
    }
    Shape& ring(float cx, float cy, float r, float w)
    {
        disk(cx, cy, r + w / 2);
        return disk(cx, cy, r - w / 2, true);
    }
    Shape& seg(float x0, float y0, float x1, float y1, float w, bool cut = false)
    {
        prims.push_back({Prim::Seg, cut, x0, y0, x1, y1, 0, w, {}});
        return *this;
    }
    Shape& path(std::initializer_list<P> pts, float w)
    {
        const std::vector<P> v(pts);
        for(size_t i = 1; i < v.size(); ++i)
            seg(v[i - 1].x, v[i - 1].y, v[i].x, v[i].y, w);
        return *this;
    }
    /// Closed outline through the points
    Shape& loop(std::initializer_list<P> pts, float w)
    {
        const std::vector<P> v(pts);
        for(size_t i = 0; i < v.size(); ++i)
            seg(v[i].x, v[i].y, v[(i + 1) % v.size()].x, v[(i + 1) % v.size()].y, w);
        return *this;
    }
    /// Arc (angles in degrees, 0 = right, clockwise in screen coordinates)
    Shape& arc(float cx, float cy, float r, float a0, float a1, float w)
    {
        const int n = 24;
        for(int i = 0; i < n; ++i)
        {
            const float t0 = (a0 + (a1 - a0) * static_cast<float>(i) / n) * 3.14159265f / 180;
            const float t1 = (a0 + (a1 - a0) * static_cast<float>(i + 1) / n) * 3.14159265f / 180;
            seg(cx + std::cos(t0) * r, cy + std::sin(t0) * r, cx + std::cos(t1) * r, cy + std::sin(t1) * r, w);
        }
        return *this;
    }
    /// Outline of a rounded rectangle (stroke centered on the edge)
    Shape& rrectLine(float x, float y, float w, float h, float r, float sw)
    {
        rrect(x - sw / 2, y - sw / 2, w + sw, h + sw, r + sw / 2);
        return rrect(x + sw / 2, y + sw / 2, w - sw, h - sw, std::max(0.f, r - sw / 2), true);
    }
    Shape& rrect(float x, float y, float w, float h, float r, bool cut = false)
    {
        prims.push_back({Prim::RRect, cut, x, y, w, h, r, 0, {}});
        return *this;
    }
    Shape& poly(std::vector<P> pts, bool cut = false)
    {
        Prim p{Prim::Poly, cut, 0, 0, 0, 0, 0, 0, {}};
        p.pts = std::move(pts);
        prims.push_back(std::move(p));
        return *this;
    }

    bool inside(float x, float y) const
    {
        bool in = false;
        for(const Prim& p : prims)
        {
            if(p.cut ? !in : in)
                continue;
            if(test(p, x, y))
                in = !p.cut;
        }
        return in;
    }

    std::vector<Prim> prims;

private:
    static bool test(const Prim& p, float x, float y)
    {
        switch(p.kind)
        {
            case Prim::Disk: return (x - p.a) * (x - p.a) + (y - p.b) * (y - p.b) <= p.r * p.r;
            case Prim::Seg:
            {
                const float dx = p.c - p.a, dy = p.d - p.b;
                const float len2 = dx * dx + dy * dy;
                float t = len2 > 0 ? ((x - p.a) * dx + (y - p.b) * dy) / len2 : 0;
                t = std::clamp(t, 0.f, 1.f);
                const float px = p.a + t * dx - x, py = p.b + t * dy - y;
                return px * px + py * py <= p.w * p.w / 4;
            }
            case Prim::RRect:
            {
                const float cx = p.a + p.c / 2, cy = p.b + p.d / 2;
                const float qx = std::abs(x - cx) - p.c / 2 + p.r, qy = std::abs(y - cy) - p.d / 2 + p.r;
                const float out = std::hypot(std::max(qx, 0.f), std::max(qy, 0.f));
                return std::min(std::max(qx, qy), 0.f) + out - p.r <= 0;
            }
            case Prim::Poly:
            {
                bool c = false;
                for(size_t i = 0, j = p.pts.size() - 1; i < p.pts.size(); j = i++)
                {
                    const P& a = p.pts[i];
                    const P& b = p.pts[j];
                    if(((a.y > y) != (b.y > y)) && (x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x))
                        c = !c;
                }
                return c;
            }
        }
        return false;
    }
};

/// One line icon set: 24 unit grid, stroke 1.9, round caps and joins, small solid dots only as accents
Shape design(Icon icon)
{
    constexpr float W = 1.9f;
    Shape s;
    switch(icon)
    {
        case Icon::None:
        case Icon::Count: break;
        case Icon::Menu: s.seg(5, 7, 19, 7, W).seg(5, 12, 19, 12, W).seg(5, 17, 19, 17, W); break;
        case Icon::Close: s.seg(6.5f, 6.5f, 17.5f, 17.5f, W).seg(17.5f, 6.5f, 6.5f, 17.5f, W); break;
        case Icon::Back: s.path({{14.5f, 5.5f}, {8, 12}, {14.5f, 18.5f}}, W * 1.1f); break;
        case Icon::Next: s.path({{9.5f, 5.5f}, {16, 12}, {9.5f, 18.5f}}, W * 1.1f); break;
        case Icon::Pause: s.seg(9, 6, 9, 18, W * 1.2f).seg(15, 6, 15, 18, W * 1.2f); break;
        case Icon::Play: s.loop({{8, 5.5f}, {18.5f, 12}, {8, 18.5f}}, W); break;
        case Icon::Fast: s.loop({{4, 7}, {11.5f, 12}, {4, 17}}, W).loop({{12.5f, 7}, {20, 12}, {12.5f, 17}}, W); break;
        case Icon::Road:
            s.seg(5, 20, 9.5f, 4, W).seg(19, 20, 14.5f, 4, W).seg(12, 16.5f, 12, 19, W).seg(12, 10.5f, 12, 13, W).seg(
              12, 5, 12, 7, W);
            break;
        case Icon::Flag:
            s.seg(6.5f, 3.5f, 6.5f, 21, W).loop({{6.5f, 4.5f}, {18, 4.5f}, {15, 8.75f}, {18, 13}, {6.5f, 13}}, W);
            break;
        case Icon::Build:
            s.loop({{10.5f, 7.5f}, {14.5f, 3.5f}, {20.5f, 9.5f}, {16.5f, 13.5f}}, W).seg(5, 19, 12.5f, 11.5f, W * 1.2f);
            break;
        case Icon::House:
            s.path({{3.5f, 11.5f}, {12, 4}, {20.5f, 11.5f}}, W)
              .path({{6, 10}, {6, 20}, {18, 20}, {18, 10}}, W)
              .path({{10.25f, 20}, {10.25f, 15}, {13.75f, 15}, {13.75f, 20}}, W);
            break;
        case Icon::Grid:
            for(int y = 0; y < 3; ++y)
                for(int x = 0; x < 3; ++x)
                    s.disk(6 + 6.f * x, 6 + 6.f * y, 1.7f);
            break;
        case Icon::Map:
            s.loop({{3.5f, 6}, {9, 4}, {15, 6}, {20.5f, 4}, {20.5f, 18}, {15, 20}, {9, 18}, {3.5f, 20}}, W)
              .seg(9, 4, 9, 18, W)
              .seg(15, 6, 15, 20, W);
            break;
        case Icon::Home:
            s.loop({{4.5f, 20.5f}, {4.5f, 5.5f}, {8, 5.5f}, {8, 8.5f}, {10.25f, 8.5f}, {10.25f, 5.5f}, {13.75f, 5.5f},
                    {13.75f, 8.5f}, {16, 8.5f}, {16, 5.5f}, {19.5f, 5.5f}, {19.5f, 20.5f}},
                   W)
              .path({{10, 20.5f}, {10, 16.5f}, {12, 14.5f}, {14, 16.5f}, {14, 20.5f}}, W);
            break;
        case Icon::Store:
            s.rrectLine(4.5f, 5.5f, 15, 14, 2, W).seg(4.5f, 10, 19.5f, 10, W).seg(10, 13.5f, 14, 13.5f, W);
            break;
        case Icon::Mail: s.rrectLine(3.5f, 5.5f, 17, 13, 2, W).path({{4.5f, 7}, {12, 12.5f}, {19.5f, 7}}, W); break;
        case Icon::Chart:
            s.seg(6, 19.5f, 6, 14, W * 1.3f).seg(12, 19.5f, 12, 9, W * 1.3f).seg(18, 19.5f, 18, 4.5f, W * 1.3f);
            break;
        case Icon::Gear:
            for(int i = 0; i < 8; ++i)
            {
                const float a = static_cast<float>(i) * 3.14159f / 4;
                s.seg(12 + std::cos(a) * 7, 12 + std::sin(a) * 7, 12 + std::cos(a) * 9.6f, 12 + std::sin(a) * 9.6f,
                      2.9f);
            }
            s.ring(12, 12, 6.3f, W).ring(12, 12, 2.4f, W);
            break;
        case Icon::Save:
            s.rrectLine(4, 4, 16, 16, 2.2f, W)
              .path({{8, 4}, {8, 9}, {15, 9}, {15, 4}}, W)
              .path({{7.5f, 20}, {7.5f, 14}, {16.5f, 14}, {16.5f, 20}}, W);
            break;
        case Icon::Folder: s.loop({{3.5f, 6.5f}, {9, 6.5f}, {11, 8.5f}, {20.5f, 8.5f}, {20.5f, 18.5f}, {3.5f, 18.5f}}, W); break;
        case Icon::Sword:
            s.path({{6.5f, 17.5f}, {17.5f, 6.5f}, {19.5f, 4.5f}}, W)
              .seg(5, 13.5f, 10.5f, 19, W)
              .seg(6, 18, 4, 20, W * 1.3f);
            break;
        case Icon::Shield: s.loop({{12, 3}, {19.5f, 5.8f}, {19, 12.5f}, {12, 21}, {5, 12.5f}, {4.5f, 5.8f}}, W); break;
        case Icon::Tools:
            s.ring(15.8f, 8.2f, 3.8f, W).seg(16.6f, 7.4f, 21.5f, 2.5f, 3.2f, true).seg(5.5f, 18.5f, 13, 11, W * 1.3f);
            break;
        case Icon::Sliders:
            s.seg(4, 7, 20, 7, W).seg(4, 12, 20, 12, W).seg(4, 17, 20, 17, W);
            for(const P k : {P{9, 7}, P{15, 12}, P{10, 17}})
                s.disk(k.x, k.y, 2.4f + W / 2 + 0.6f, true).ring(k.x, k.y, 2.4f, W);
            break;
        case Icon::List:
            s.disk(5, 7, 1.4f)
              .disk(5, 12, 1.4f)
              .disk(5, 17, 1.4f)
              .seg(9, 7, 20, 7, W)
              .seg(9, 12, 20, 12, W)
              .seg(9, 17, 20, 17, W);
            break;
        case Icon::Boxes:
            s.rrectLine(3.5f, 12.5f, 7.5f, 7.5f, 1.5f, W)
              .rrectLine(13, 12.5f, 7.5f, 7.5f, 1.5f, W)
              .rrectLine(8.25f, 3.5f, 7.5f, 7.5f, 1.5f, W);
            break;
        case Icon::Pick:
            s.seg(12, 8, 12, 21, W).path({{3.5f, 10}, {7, 6.5f}, {12, 5}, {17, 6.5f}, {20.5f, 10}}, W);
            break;
        case Icon::Eye:
            s.path({{2.5f, 12}, {5, 8.6f}, {8.5f, 6.4f}, {12, 5.8f}, {15.5f, 6.4f}, {19, 8.6f}, {21.5f, 12}}, W)
              .path({{2.5f, 12}, {5, 15.4f}, {8.5f, 17.6f}, {12, 18.2f}, {15.5f, 17.6f}, {19, 15.4f}, {21.5f, 12}}, W)
              .ring(12, 12, 3, W);
            break;
        case Icon::Ship:
            s.loop({{3, 14}, {21, 14}, {18, 19.5f}, {6, 19.5f}}, W)
              .seg(12, 3.5f, 12, 14, W)
              .loop({{14.5f, 5}, {19, 11.5f}, {14.5f, 11.5f}}, W);
            break;
        case Icon::Trash:
            s.seg(4, 6.5f, 20, 6.5f, W)
              .path({{9.5f, 6.5f}, {9.5f, 4}, {14.5f, 4}, {14.5f, 6.5f}}, W)
              .path({{6, 6.5f}, {7, 20}, {17, 20}, {18, 6.5f}}, W)
              .seg(10.5f, 10, 10.5f, 16.5f, W)
              .seg(13.5f, 10, 13.5f, 16.5f, W);
            break;
        case Icon::Up: s.seg(12, 19.5f, 12, 5, W).path({{6, 11}, {12, 5}, {18, 11}}, W); break;
        case Icon::Down: s.seg(12, 4.5f, 12, 19, W).path({{6, 13}, {12, 19}, {18, 13}}, W); break;
        case Icon::Check: s.path({{5, 12.5f}, {9.8f, 17.3f}, {19, 7}}, W * 1.2f); break;
        case Icon::Plus: s.seg(12, 5, 12, 19, W * 1.15f).seg(5, 12, 19, 12, W * 1.15f); break;
        case Icon::Minus: s.seg(5, 12, 19, 12, W * 1.15f); break;
        case Icon::Info: s.ring(12, 12, 9, W).disk(12, 7.8f, 1.3f).seg(12, 11, 12, 16.5f, W); break;
        case Icon::Help:
            s.ring(12, 12, 9, W)
              .path({{9.2f, 9.6f}, {9.9f, 7.7f}, {12, 6.9f}, {14.1f, 7.7f}, {14.8f, 9.5f}, {13.9f, 11.2f}, {12, 12.4f},
                     {12, 13.8f}},
                    W)
              .disk(12, 17, 1.3f);
            break;
        case Icon::People:
            s.ring(12, 8, 3.6f, W).path({{5, 20}, {5.8f, 16.5f}, {8.5f, 14}, {15.5f, 14}, {18.2f, 16.5f}, {19, 20}}, W);
            break;
        case Icon::Power: s.arc(12, 13, 7.5f, -50, 230, W).seg(12, 3.5f, 12, 11.5f, W); break;
        case Icon::Target:
            s.ring(12, 12, 7, W)
              .seg(12, 2.5f, 12, 6.5f, W)
              .seg(12, 17.5f, 12, 21.5f, W)
              .seg(2.5f, 12, 6.5f, 12, W)
              .seg(17.5f, 12, 21.5f, 12, W)
              .disk(12, 12, 1.6f);
            break;
        case Icon::Coin: s.ring(12, 12, 8.5f, W).ring(12, 12, 4.4f, W); break;
        case Icon::Hand:
            s.seg(8.5f, 11, 8.5f, 6.5f, W)
              .seg(12, 10.5f, 12, 4.5f, W)
              .seg(15.5f, 11, 15.5f, 6, W)
              .path({{6, 10.5f}, {6, 15}, {9, 20}, {16, 20}, {18, 15}, {19.5f, 10.5f}}, W);
            break;
        case Icon::Trophy:
            s.loop({{7, 4}, {17, 4}, {16.5f, 10}, {12, 13.5f}, {7.5f, 10}}, W)
              .seg(12, 13.5f, 12, 18.5f, W)
              .seg(8, 19.5f, 16, 19.5f, W)
              .path({{7, 5.5f}, {4.5f, 6}, {4.5f, 8.5f}, {7.3f, 10.2f}}, W)
              .path({{17, 5.5f}, {19.5f, 6}, {19.5f, 8.5f}, {16.7f, 10.2f}}, W);
            break;
        case Icon::Exit:
            s.path({{13, 4}, {5, 4}, {5, 20}, {13, 20}}, W)
              .seg(10, 12, 20, 12, W)
              .path({{16.5f, 8.5f}, {20, 12}, {16.5f, 15.5f}}, W);
            break;
    }
    return s;
}
} // namespace

const char* iconName(Icon icon)
{
    static const char* const names[] = {"None", "Menu", "Close", "Back", "Pause", "Play", "Fast", "Road", "Flag", "Build", "House", "Grid", "Map", "Home", "Store", "Mail", "Chart", "Gear", "Save", "Folder", "Sword", "Shield", "Tools", "Sliders", "List", "Boxes", "Pick", "Eye", "Ship", "Trash", "Up", "Down", "Check", "Plus", "Minus", "Info", "Help", "People", "Power", "Target", "Coin", "Hand", "Trophy", "Exit", "Next"};
    static_assert(std::size(names) == static_cast<size_t>(Icon::Count));
    const auto i = static_cast<size_t>(icon);
    return i < std::size(names) ? names[i] : "?";
}

void IconAtlas::init(int cellPx)
{
    cell_ = cellPx;
    const int n = static_cast<int>(Icon::Count);
    const int rows = (n + cols_ - 1) / cols_;
    std::vector<uint8_t> pixels(static_cast<size_t>(cols_ * cell_) * rows * cell_, 0);
    const int ss = 4;
    for(int i = 1; i < n; ++i)
    {
        const Shape shape = design(static_cast<Icon>(i));
        const int ox = (i % cols_) * cell_, oy = (i / cols_) * cell_;
        const float scale = 24.f / static_cast<float>(cell_);
        for(int y = 0; y < cell_; ++y)
        {
            for(int x = 0; x < cell_; ++x)
            {
                int cov = 0;
                for(int sy = 0; sy < ss; ++sy)
                    for(int sx = 0; sx < ss; ++sx)
                        cov += shape.inside((static_cast<float>(x) + (sx + 0.5f) / ss) * scale,
                                            (static_cast<float>(y) + (sy + 0.5f) / ss) * scale);
                pixels[static_cast<size_t>(oy + y) * cols_ * cell_ + ox + x] =
                  static_cast<uint8_t>(cov * 255 / (ss * ss));
            }
        }
    }
    tex_.create(cols_ * cell_, rows * cell_, true, true, pixels.data());
    // Mipmaps keep the thin strokes smooth at small sizes
    glBindTexture(GL_TEXTURE_2D, tex_.id());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
}

void IconAtlas::draw(gfx::Batch& batch, Icon icon, float cx, float cy, float size, gfx::Color c) const
{
    if(icon == Icon::None || !tex_)
        return;
    const int i = static_cast<int>(icon);
    const float tw = static_cast<float>(tex_.width()), th = static_cast<float>(tex_.height());
    const float u0 = static_cast<float>((i % cols_) * cell_) / tw, v0 = static_cast<float>((i / cols_) * cell_) / th;
    const float u1 = u0 + static_cast<float>(cell_) / tw, v1 = v0 + static_cast<float>(cell_) / th;
    batch.quad(tex_.id(), cx - size / 2, cy - size / 2, cx + size / 2, cy + size / 2, u0, v0, u1, v1, c,
               gfx::Batch::KindGlyph);
}

} // namespace weiler::ui
