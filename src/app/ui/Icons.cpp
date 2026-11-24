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
        prims.push_back({Prim::Disk, cut, cx, cy, 0, 0, r});
        return *this;
    }
    Shape& ring(float cx, float cy, float r, float w)
    {
        disk(cx, cy, r + w / 2);
        return disk(cx, cy, r - w / 2, true);
    }
    Shape& seg(float x0, float y0, float x1, float y1, float w, bool cut = false)
    {
        prims.push_back({Prim::Seg, cut, x0, y0, x1, y1, 0, w});
        return *this;
    }
    Shape& path(std::initializer_list<P> pts, float w)
    {
        const std::vector<P> v(pts);
        for(size_t i = 1; i < v.size(); ++i)
            seg(v[i - 1].x, v[i - 1].y, v[i].x, v[i].y, w);
        return *this;
    }
    Shape& rrect(float x, float y, float w, float h, float r, bool cut = false)
    {
        prims.push_back({Prim::RRect, cut, x, y, w, h, r});
        return *this;
    }
    Shape& poly(std::vector<P> pts, bool cut = false)
    {
        Prim p{Prim::Poly, cut};
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

Shape design(Icon icon)
{
    Shape s;
    switch(icon)
    {
        case Icon::None:
        case Icon::Count: break;
        case Icon::Menu: s.seg(5, 7, 19, 7, 2.2f).seg(5, 12, 19, 12, 2.2f).seg(5, 17, 19, 17, 2.2f); break;
        case Icon::Close: s.seg(6, 6, 18, 18, 2.4f).seg(18, 6, 6, 18, 2.4f); break;
        case Icon::Back: s.path({{14, 5}, {7, 12}, {14, 19}}, 2.6f); break;
        case Icon::Pause: s.rrect(6.5f, 5, 4, 14, 1).rrect(13.5f, 5, 4, 14, 1); break;
        case Icon::Play: s.poly({{7, 4.5f}, {19.5f, 12}, {7, 19.5f}}); break;
        case Icon::Fast: s.poly({{3, 6}, {12, 12}, {3, 18}}).poly({{12, 6}, {21, 12}, {12, 18}}); break;
        case Icon::Road:
            s.seg(4, 20, 9, 4, 2).seg(20, 20, 15, 4, 2).seg(12, 16.5f, 12, 19, 1.8f).seg(12, 10.5f, 12, 13, 1.8f).seg(
              12, 5, 12, 7, 1.8f);
            break;
        case Icon::Flag: s.seg(7, 3.5f, 7, 21, 2).poly({{8, 4}, {19.5f, 7.5f}, {8, 11.5f}}); break;
        case Icon::Build:
            s.seg(15, 9, 5.5f, 18.5f, 2.8f).poly({{10, 5.5f}, {14, 2.5f}, {20, 8.5f}, {17, 11.5f}});
            break;
        case Icon::House:
            s.poly({{2.5f, 11.5f}, {12, 3}, {21.5f, 11.5f}}).rrect(5.5f, 10.5f, 13, 10.5f, 1).rrect(10.3f, 14.5f, 3.4f,
                                                                                                 7, 0.6f, true);
            break;
        case Icon::Grid:
            for(int y = 0; y < 3; ++y)
                for(int x = 0; x < 3; ++x)
                    s.disk(6 + 6.f * x, 6 + 6.f * y, 1.9f);
            break;
        case Icon::Map:
            s.poly({{3, 6}, {9, 4}, {15, 6}, {21, 4}, {21, 18}, {15, 20}, {9, 18}, {3, 20}})
              .seg(9, 4, 9, 18, 1.2f, true)
              .seg(15, 6, 15, 20, 1.2f, true);
            break;
        case Icon::Home:
            s.rrect(4.5f, 9, 15, 12, 1)
              .rrect(4.5f, 5, 3.5f, 5, 0.5f)
              .rrect(10.25f, 5, 3.5f, 5, 0.5f)
              .rrect(16, 5, 3.5f, 5, 0.5f)
              .rrect(10, 14.5f, 4, 7, 1.5f, true);
            break;
        case Icon::Store:
            s.rrect(3.5f, 5.5f, 17, 15, 2)
              .rrect(5.5f, 7.5f, 13, 11, 1, true)
              .seg(6.5f, 8.5f, 17.5f, 17.5f, 1.8f)
              .seg(17.5f, 8.5f, 6.5f, 17.5f, 1.8f);
            break;
        case Icon::Mail:
            s.rrect(3, 5.5f, 18, 13, 2).rrect(4.6f, 7.1f, 14.8f, 9.8f, 1, true).path({{4.5f, 7.5f}, {12, 13}, {19.5f, 7.5f}},
                                                                                      1.7f);
            break;
        case Icon::Chart: s.rrect(4, 13, 4, 7, 0.8f).rrect(10, 8, 4, 12, 0.8f).rrect(16, 4, 4, 16, 0.8f); break;
        case Icon::Gear:
            for(int i = 0; i < 8; ++i)
            {
                const float a = static_cast<float>(i) * 3.14159f / 4;
                s.seg(12 + std::cos(a) * 6, 12 + std::sin(a) * 6, 12 + std::cos(a) * 9.5f, 12 + std::sin(a) * 9.5f,
                      3.2f);
            }
            s.disk(12, 12, 7.2f).disk(12, 12, 2.8f, true);
            break;
        case Icon::Save:
            s.rrect(4, 4, 16, 16, 2).rrect(7.5f, 4, 8, 5.5f, 0.5f, true).rrect(7, 12.5f, 10, 7.5f, 1, true);
            break;
        case Icon::Folder: s.poly({{3, 6}, {9, 6}, {11, 8.5f}, {21, 8.5f}, {21, 19}, {3, 19}}); break;
        case Icon::Sword:
            s.seg(7, 17, 19, 5, 2.6f).seg(5, 13, 11, 19, 2.2f).seg(6, 18, 3.5f, 20.5f, 2.8f);
            break;
        case Icon::Shield: s.poly({{12, 2.5f}, {20, 5.5f}, {19, 13.5f}, {12, 21.5f}, {5, 13.5f}, {4, 5.5f}}); break;
        case Icon::Tools:
            s.seg(5.5f, 18.5f, 14, 10, 3.2f).disk(16, 8, 5).disk(16, 8, 2.6f, true).seg(17, 7, 21.5f, 2.5f, 3.6f, true);
            break;
        case Icon::Sliders:
            s.seg(4, 7, 20, 7, 1.6f)
              .seg(4, 12, 20, 12, 1.6f)
              .seg(4, 17, 20, 17, 1.6f)
              .disk(9, 7, 2.6f)
              .disk(15, 12, 2.6f)
              .disk(11, 17, 2.6f);
            break;
        case Icon::List:
            s.disk(5, 7, 1.6f)
              .disk(5, 12, 1.6f)
              .disk(5, 17, 1.6f)
              .seg(9, 7, 20, 7, 2)
              .seg(9, 12, 20, 12, 2)
              .seg(9, 17, 20, 17, 2);
            break;
        case Icon::Boxes: s.rrect(3, 12.5f, 8.5f, 8, 1).rrect(12.5f, 12.5f, 8.5f, 8, 1).rrect(7.75f, 3.5f, 8.5f, 8, 1); break;
        case Icon::Pick: s.seg(12, 8, 12, 21, 2.4f).poly({{3, 9.5f}, {12, 3.5f}, {21, 9.5f}, {12, 7}}); break;
        case Icon::Eye:
        {
            std::vector<P> lens;
            for(int i = 0; i <= 16; ++i)
            {
                const float t = static_cast<float>(i) / 16 * 3.14159f;
                lens.push_back({12 - 10 * std::cos(t), 12 - 6.5f * std::sin(t)});
            }
            for(int i = 1; i < 16; ++i)
            {
                const float t = static_cast<float>(i) / 16 * 3.14159f;
                lens.push_back({12 + 10 * std::cos(t), 12 + 6.5f * std::sin(t)});
            }
            s.poly(lens).disk(12, 12, 4, true).disk(12, 12, 2.2f);
            break;
        }
        case Icon::Ship:
            s.poly({{2.5f, 14}, {21.5f, 14}, {18, 19.5f}, {6, 19.5f}})
              .poly({{12.8f, 3}, {19, 12.5f}, {12.8f, 12.5f}})
              .poly({{11.2f, 5}, {11.2f, 12.5f}, {6, 12.5f}})
              .seg(12, 3, 12, 14, 1.4f);
            break;
        case Icon::Trash:
            s.rrect(6, 8, 12, 13, 1.5f)
              .rrect(4, 5, 16, 2.4f, 1)
              .rrect(9.5f, 2.8f, 5, 3, 1)
              .seg(10, 11, 10, 18, 1.4f, true)
              .seg(14, 11, 14, 18, 1.4f, true);
            break;
        case Icon::Up: s.poly({{12, 4}, {19.5f, 12.5f}, {4.5f, 12.5f}}).rrect(9.5f, 11.5f, 5, 8.5f, 0.6f); break;
        case Icon::Down: s.poly({{12, 20}, {19.5f, 11.5f}, {4.5f, 11.5f}}).rrect(9.5f, 4, 5, 8.5f, 0.6f); break;
        case Icon::Check: s.path({{4.5f, 12.5f}, {9.5f, 17.5f}, {19.5f, 6.5f}}, 2.8f); break;
        case Icon::Plus: s.seg(12, 5, 12, 19, 2.6f).seg(5, 12, 19, 12, 2.6f); break;
        case Icon::Minus: s.seg(5, 12, 19, 12, 2.6f); break;
        case Icon::Info: s.ring(12, 12, 9, 2).disk(12, 7.5f, 1.5f).seg(12, 11, 12, 17, 2.2f); break;
        case Icon::Help:
            s.ring(12, 12, 9.5f, 2)
              .path({{9, 9.5f}, {9.8f, 7.4f}, {12, 6.6f}, {14.2f, 7.4f}, {15, 9.3f}, {14, 11.2f}, {12, 12.3f}, {12, 14}},
                    2)
              .disk(12, 17, 1.4f);
            break;
        case Icon::People:
            s.disk(12, 7, 3.8f).poly({{4.5f, 21}, {5.5f, 15}, {9, 12.5f}, {15, 12.5f}, {18.5f, 15}, {19.5f, 21}});
            break;
        case Icon::Power:
            s.ring(12, 13, 7.2f, 2.3f).rrect(8.8f, 2, 6.4f, 9, 1, true).seg(12, 3.5f, 12, 12, 2.3f);
            break;
        case Icon::Target:
            s.ring(12, 12, 7, 2)
              .seg(12, 2, 12, 7, 2)
              .seg(12, 17, 12, 22, 2)
              .seg(2, 12, 7, 12, 2)
              .seg(17, 12, 22, 12, 2)
              .disk(12, 12, 1.8f);
            break;
        case Icon::Coin: s.disk(12, 12, 9).disk(12, 12, 6.6f, true).disk(12, 12, 4.6f); break;
        case Icon::Hand: s.rrect(10, 3, 4.2f, 13, 2.1f).rrect(6.5f, 10.5f, 11, 10.5f, 3.5f); break;
        case Icon::Trophy:
            s.poly({{6, 3.5f}, {18, 3.5f}, {17, 10.5f}, {12, 14}, {7, 10.5f}})
              .seg(12, 13, 12, 18.5f, 2.2f)
              .rrect(7.5f, 18, 9, 2.8f, 1)
              .ring(5.5f, 7.5f, 2.6f, 1.6f)
              .ring(18.5f, 7.5f, 2.6f, 1.6f);
            break;
        case Icon::Exit:
            s.rrect(3.5f, 3, 10, 18, 1.5f)
              .rrect(5.5f, 5, 6, 14, 0.5f, true)
              .seg(10, 12, 20, 12, 2.2f)
              .poly({{16.5f, 7.5f}, {21.5f, 12}, {16.5f, 16.5f}});
            break;
    }
    return s;
}
} // namespace

const char* iconName(Icon icon)
{
    static const char* const names[] = {"None", "Menu", "Close", "Back", "Pause", "Play", "Fast", "Road", "Flag", "Build", "House", "Grid", "Map", "Home", "Store", "Mail", "Chart", "Gear", "Save", "Folder", "Sword", "Shield", "Tools", "Sliders", "List", "Boxes", "Pick", "Eye", "Ship", "Trash", "Up", "Down", "Check", "Plus", "Minus", "Info", "Help", "People", "Power", "Target", "Coin", "Hand", "Trophy", "Exit"};
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
