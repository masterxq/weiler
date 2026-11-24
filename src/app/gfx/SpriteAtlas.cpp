// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "SpriteAtlas.h"
#include "Loader.h"
#include "libsiedler2/Archiv.h"
#include "libsiedler2/ArchivItem.h"
#include "Perf.h"
#include "libsiedler2/PixelBufferBGRA.h"
#include <algorithm>
#include <atomic>
#ifdef __GLIBC__
#    include <malloc.h>
#endif
#include <chrono>
#include <iostream>
#include <cmath>
#include <cstring>
#include <thread>
#include <type_traits>

namespace weiler::gfx {

namespace {
constexpr int PAD = 1;
/// Handles carry a generation in the upper bits: sources of an earlier game never hit entries of a new one
constexpr unsigned GEN_SHIFT = 24;
constexpr uint32_t INDEX_MASK = (1u << GEN_SHIFT) - 1;

template<class T, class F>
void visitSprites(T& x, F& f)
{
    if constexpr(std::is_same_v<T, glSmartBitmap>)
        f(x);
    else if constexpr(std::is_same_v<T, Loader::FightSprites>)
    {
        visitSprites(x.attacking, f);
        visitSprites(x.defending, f);
        visitSprites(x.hit, f);
    } else if constexpr(std::is_same_v<T, Loader::BuildingSprites>)
    {
        visitSprites(x.building, f);
        visitSprites(x.skeleton, f);
        visitSprites(x.door, f);
    } else
    {
        for(auto& e : x)
            visitSprites(e, f);
    }
}
} // namespace

SpriteAtlas::SpriteAtlas(Batch& batch) : batch_(batch)
{
    entries_.emplace_back(); // index 0 = invalid
    setSpriteRenderer(this);
    GLint maxSize = 2048;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxSize);
    // 2048 is supported everywhere and keeps the unused rest of the last layer small
    pageSize_ = std::min(2048, static_cast<int>(maxSize));
}

SpriteAtlas::~SpriteAtlas()
{
    if(getSpriteRenderer() == this)
        setSpriteRenderer(nullptr);
    if(tex_)
    {
        glDeleteTextures(1, &tex_);
        Texture::account(-static_cast<long long>(layers_) * pageSize_ * pageSize_ * 4);
    }
}

void SpriteAtlas::ensureLayers(unsigned n)
{
    if(n <= layers_)
        return;
    if(layers_ > 0)
        n = std::max(n, layers_ + 1);
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex);
    glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_RGBA8, pageSize_, pageSize_, static_cast<GLsizei>(n));
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Copy the old layers and clear the new ones (transparent borders around the sprites)
    GLint oldFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &oldFbo);
    const GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    for(unsigned l = 0; l < n; ++l)
    {
        if(l < layers_)
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex_, 0, static_cast<GLint>(l));
            glCopyTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(l), 0, 0, pageSize_, pageSize_);
        } else
        {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, tex, 0, static_cast<GLint>(l));
            const GLfloat zero[4] = {0, 0, 0, 0};
            glClearBufferfv(GL_COLOR, 0, zero);
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(oldFbo));
    glDeleteFramebuffers(1, &fbo);
    if(scissor)
        glEnable(GL_SCISSOR_TEST);
    if(tex_)
    {
        batch_.flush();
        glDeleteTextures(1, &tex_);
    }
    Texture::account(static_cast<long long>(n - layers_) * pageSize_ * pageSize_ * 4);
    tex_ = tex;
    layers_ = n;
    batch_.setAtlas(tex_);
}

bool SpriteAtlas::allocate(int w, int h, uint16_t& page, int& x, int& y)
{
    w += 2 * PAD;
    h += 2 * PAD;
    if(w > pageSize_ || h > pageSize_)
        return false;
    for(size_t i = 0; i <= pages_.size(); ++i)
    {
        if(i == pages_.size())
            pages_.emplace_back();
        Page& p = pages_[i];
        // Best fitting shelf: enough room and not much taller than needed
        Shelf* best = nullptr;
        for(Shelf& s : p.shelves)
        {
            if(s.h >= h && s.h <= h + h / 2 + 4 && s.x + w <= pageSize_ && (!best || s.h < best->h))
                best = &s;
        }
        if(!best && p.used + h <= pageSize_)
        {
            p.shelves.push_back({p.used, h, 0});
            p.used += h;
            best = &p.shelves.back();
        }
        if(!best)
            continue;
        x = best->x + PAD;
        y = best->y + PAD;
        best->x += w;
        page = static_cast<uint16_t>(i);
        return true;
    }
    return false;
}

void SpriteAtlas::decode(Decoded& d)
{
    const Extent size = d.src->getSourceSize();
    d.player = d.src->hasPlayerLayer();
    d.baseW = static_cast<int>(size.x);
    d.w = static_cast<int>(size.x) * (d.player ? 2 : 1);
    d.h = static_cast<int>(size.y);
    if(d.w == 0 || d.h == 0)
        return;
    libsiedler2::PixelBufferBGRA buffer(d.w, d.h);
    d.src->renderSource(buffer);
    // Convert BGRA to premultiplied RGBA
    const size_t n = static_cast<size_t>(d.w) * d.h;
    d.rgba.resize(n * 4);
    const uint8_t* in = buffer.getPixelPtr();
    uint8_t* out = d.rgba.data();
    for(size_t i = 0; i < n; ++i)
    {
        const unsigned a = in[i * 4 + 3];
        out[i * 4 + 0] = static_cast<uint8_t>(in[i * 4 + 2] * a / 255);
        out[i * 4 + 1] = static_cast<uint8_t>(in[i * 4 + 1] * a / 255);
        out[i * 4 + 2] = static_cast<uint8_t>(in[i * 4 + 0] * a / 255);
        out[i * 4 + 3] = static_cast<uint8_t>(a);
    }
}

void SpriteAtlas::addEntry(const Decoded& d, const Entry& e)
{
    uint32_t index;
    if(!freeHandles_.empty())
    {
        index = freeHandles_.back();
        freeHandles_.pop_back();
        entries_[index] = e;
    } else
    {
        index = static_cast<uint32_t>(entries_.size());
        entries_.push_back(e);
    }
    d.src->rendererHandle = index | (generation_ << GEN_SHIFT);
    ++numSprites_;
}

SpriteAtlas::Entry* SpriteAtlas::find(const ISpriteSource& src)
{
    const uint32_t h = src.rendererHandle;
    if(!h || (h >> GEN_SHIFT) != generation_)
        return nullptr;
    const uint32_t index = h & INDEX_MASK;
    if(index < entries_.size() && entries_[index].valid)
        return &entries_[index];
    return nullptr;
}

SpriteAtlas::Entry* SpriteAtlas::lookup(const ISpriteSource& src)
{
    if(Entry* e = find(src))
        return e->ready ? e : nullptr;
    const Extent size = src.getSourceSize();
    if(size.x == 0 || size.y == 0)
        return nullptr;
    // Not preloaded: decode now, upload at the start of the next frame. Changing the texture while this frame's
    // draw calls still use it would split the render pass (tilers) or stall; the sprite is missing for one frame.
    perf::Scope timeAtlas(perf::Phase::Atlas);
    Pending p;
    p.d.src = &src;
    decode(p.d);
    if(!allocate(p.d.w, p.d.h, p.e.page, p.e.x, p.e.y))
        return nullptr;
    p.e.w = p.d.baseW;
    p.e.h = p.d.h;
    p.e.player = p.d.player;
    p.e.valid = true;
    p.e.ready = false;
    addEntry(p.d, p.e);
    p.index = src.rendererHandle & INDEX_MASK;
    pending_.push_back(std::move(p));
    ++lateSprites_;
    return nullptr;
}

void SpriteAtlas::beginFrame()
{
    if(pending_.empty())
        return;
    perf::Scope timeAtlas(perf::Phase::Atlas);
    ensureLayers(static_cast<unsigned>(pages_.size()));
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for(const Pending& p : pending_)
    {
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, p.e.x, p.e.y, p.e.page, p.d.w, p.d.h, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        p.d.rgba.data());
        Entry& e = entries_[p.index];
        if(e.valid && e.page == p.e.page && e.x == p.e.x && e.y == p.e.y)
            e.ready = true;
    }
    pending_.clear();
}

void SpriteAtlas::reset()
{
    batch_.flush();
    pending_.clear();
    entries_.assign(1, Entry{});
    freeHandles_.clear();
    pages_.clear();
    numSprites_ = 0;
    lateSprites_ = 0;
    generation_ = (generation_ + 1) & 0xFF;
    if(generation_ == 0)
        generation_ = 1;
    // The texture array is kept and overwritten (same size in most cases)
}

void SpriteAtlas::preload(const std::vector<const ISpriteSource*>& sources)
{
    const auto start = std::chrono::steady_clock::now();
    std::vector<Decoded> todo;
    todo.reserve(sources.size());
    for(const ISpriteSource* s : sources)
    {
        if(s && !find(*s))
        {
            const Extent size = s->getSourceSize();
            if(size.x && size.y)
                todo.emplace_back().src = s;
        }
    }
    // The same source may appear twice
    std::sort(todo.begin(), todo.end(), [](const Decoded& a, const Decoded& b) { return a.src < b.src; });
    todo.erase(std::unique(todo.begin(), todo.end(), [](const Decoded& a, const Decoded& b) { return a.src == b.src; }),
               todo.end());
    // Decode on all cores (reading the archive data only)
    const unsigned numThreads = std::clamp(std::thread::hardware_concurrency(), 1u, 8u);
    std::atomic<size_t> next{0};
    auto work = [&] {
        for(size_t i = next++; i < todo.size(); i = next++)
        {
            // An exception must not leave the thread (std::terminate); the sprite stays empty
            try
            {
                decode(todo[i]);
            } catch(const std::exception&)
            {
                todo[i].rgba.clear();
            }
        }
    };
    std::vector<std::thread> threads;
    for(unsigned t = 1; t < numThreads; ++t)
        threads.emplace_back(work);
    work();
    for(auto& t : threads)
        t.join();
    const auto decoded = std::chrono::steady_clock::now();
    // Pack the tallest first (shelves fill well), keep the order of equal heights stable
    std::vector<size_t> order(todo.size());
    for(size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return todo[a].h != todo[b].h ? todo[a].h > todo[b].h : todo[a].w > todo[b].w;
    });
    const size_t firstNewPage = pages_.size();
    std::vector<Entry> placed(todo.size());
    for(size_t i : order)
    {
        const Decoded& d = todo[i];
        if(d.rgba.empty() || !allocate(d.w, d.h, placed[i].page, placed[i].x, placed[i].y))
            continue;
        placed[i].w = d.baseW;
        placed[i].h = d.h;
        placed[i].player = d.player;
        placed[i].valid = true;
    }
    batch_.flush();
    ensureLayers(static_cast<unsigned>(pages_.size()));
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    // New pages are composed on the CPU and uploaded in one go, sprites in older pages one by one
    const size_t ps = static_cast<size_t>(pageSize_);
    std::vector<uint8_t> page;
    for(size_t p = firstNewPage; p < pages_.size(); ++p)
    {
        page.assign(ps * pages_[p].used * 4, 0);
        for(size_t i = 0; i < todo.size(); ++i)
        {
            const Entry& e = placed[i];
            if(!e.valid || e.page != p)
                continue;
            const Decoded& d = todo[i];
            for(int row = 0; row < d.h; ++row)
                std::memcpy(&page[((e.y + row) * ps + e.x) * 4], &d.rgba[static_cast<size_t>(row) * d.w * 4],
                            static_cast<size_t>(d.w) * 4);
        }
        if(pages_[p].used > 0)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(p), pageSize_, pages_[p].used, 1, GL_RGBA,
                            GL_UNSIGNED_BYTE, page.data());
    }
    for(size_t i = 0; i < todo.size(); ++i)
    {
        const Entry& e = placed[i];
        if(!e.valid)
            continue;
        if(e.page < firstNewPage)
            glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, e.x, e.y, e.page, todo[i].w, todo[i].h, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                            todo[i].rgba.data());
        addEntry(todo[i], e);
    }
    lateSprites_ = 0;
    todo.clear();
    todo.shrink_to_fit();
    page.clear();
    page.shrink_to_fit();
#ifdef __GLIBC__
    // The decoding threads left freed memory in their malloc arenas: give it back to the system
    malloc_trim(0);
#endif
    const auto end = std::chrono::steady_clock::now();
    auto ms = [](auto a, auto b) { return static_cast<long>(std::chrono::duration<double, std::milli>(b - a).count()); };
    std::cout << "[time] atlas preload: " << todo.size() << " sprites, decode " << ms(start, decoded) << " ms ("
              << numThreads << " threads), pack+upload " << ms(decoded, end) << " ms, " << describe() << std::endl;
}

void SpriteAtlas::preloadGameSprites()
{
    reset();
    std::vector<const ISpriteSource*> sources;
    sources.reserve(32768);
    auto add = [&](const glSmartBitmap& bmp) {
        if(!bmp.empty())
            sources.push_back(&bmp);
    };
    Loader& l = LOADER;
    // Decoding threads look up these palettes (std::map access): make sure they exist before
    l.getPaletteN("colors");
    l.getPaletteN("pal5");
    visitSprites(l.animal_cache, add);
    visitSprites(l.building_cache, add);
    visitSprites(l.flag_cache, add);
    visitSprites(l.tree_cache, add);
    visitSprites(l.bob_jobs_cache, add);
    visitSprites(l.fat_carrier_cache, add);
    visitSprites(l.granite_cache, add);
    visitSprites(l.grainfield_cache, add);
    visitSprites(l.carrier_cache, add);
    visitSprites(l.boundary_stone_cache, add);
    visitSprites(l.boat_cache, add);
    visitSprites(l.donkey_cache, add);
    visitSprites(l.gateway_cache, add);
    visitSprites(l.fight_cache, add);
    // Map objects (trees, stones, wares, fire, ...). The images of the nations are not needed: the buildings are
    // drawn from the caches above, the few remaining ones (doors, fire) come on first use.
    auto addArchive = [&](const libsiedler2::Archiv* archive) {
        if(!archive)
            return;
        for(unsigned i = 0; i < archive->size(); ++i)
        {
            if(const auto* src = dynamic_cast<const ISpriteSource*>(archive->get(i)))
                sources.push_back(src);
        }
    };
    addArchive(l.getMapArchive());
    // Work animations of the figures and the ships
    addArchive(&l.getArchive("rom_bobs"));
    addArchive(&l.getArchive("boot_z"));

    preload(sources);
}

void SpriteAtlas::releaseSource(const ISpriteSource& src)
{
    // Atlas space is not reclaimed (the atlas is rebuilt for every game)
    if(Entry* e = find(src))
    {
        e->valid = false;
        freeHandles_.push_back(src.rendererHandle & INDEX_MASK);
        --numSprites_;
    }
    src.rendererHandle = 0;
}

std::string SpriteAtlas::describe() const
{
    size_t used = 0;
    for(const Page& p : pages_)
        used += static_cast<size_t>(p.used);
    const double fill = pages_.empty() ? 0 : 100.0 * static_cast<double>(used) / (pages_.size() * pageSize_);
    return "atlas " + std::to_string(layers_) + " layers of " + std::to_string(pageSize_) + "^2 ("
           + std::to_string(static_cast<int>(fill)) + "% rows used), " + std::to_string(numSprites_) + " sprites, "
           + std::to_string(lateSprites_) + " late";
}

void SpriteAtlas::drawSprite(const ISpriteSource& src, const Rect& dst, const Rect& srcRect, unsigned color,
                             unsigned playerColor)
{
    const Entry* e = lookup(src);
    if(!e)
        return;
    const float inv = 1.f / static_cast<float>(pageSize_);
    const float u0 = static_cast<float>(e->x + srcRect.left) * inv, u1 = static_cast<float>(e->x + srcRect.right) * inv;
    const float v0 = static_cast<float>(e->y + srcRect.top) * inv, v1 = static_cast<float>(e->y + srcRect.bottom) * inv;
    const auto x0 = static_cast<float>(dst.left), y0 = static_cast<float>(dst.top);
    const auto x1 = static_cast<float>(dst.right), y1 = static_cast<float>(dst.bottom);
    const auto layer = static_cast<float>(e->page);
    if(e->player && playerColor)
        batch_.atlasQuadPlayer(layer, x0, y0, x1, y1, u0, v0, u1, v1, static_cast<float>(e->w) * inv,
                               Color::fromARGB(color), Color::fromARGB(playerColor));
    else
        batch_.atlasQuad(layer, x0, y0, x1, y1, u0, v0, u1, v1, Color::fromARGB(color));
}

void SpriteAtlas::drawSource(const ISpriteSource& src, const Rect2& dst, Color c, unsigned playerColor)
{
    const Extent size = src.getSourceSize();
    const Entry* e = lookup(src);
    if(!e)
        return;
    const float inv = 1.f / static_cast<float>(pageSize_);
    const float u0 = static_cast<float>(e->x) * inv, u1 = (static_cast<float>(e->x) + static_cast<float>(size.x)) * inv;
    const float v0 = static_cast<float>(e->y) * inv, v1 = (static_cast<float>(e->y) + static_cast<float>(size.y)) * inv;
    const auto layer = static_cast<float>(e->page);
    if(e->player && playerColor)
        batch_.atlasQuadPlayer(layer, dst.x, dst.y, dst.right(), dst.bottom(), u0, v0, u1, v1,
                               static_cast<float>(e->w) * inv, c, Color::fromARGB(playerColor));
    else
        batch_.atlasQuad(layer, dst.x, dst.y, dst.right(), dst.bottom(), u0, v0, u1, v1, c);
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
