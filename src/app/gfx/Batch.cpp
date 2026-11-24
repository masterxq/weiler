// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Batch.h"
#include "Perf.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace weiler::gfx {

namespace {
constexpr unsigned MAX_QUADS = 16384;
/// Streaming vertex buffer (several frames of typical vertex data)
constexpr size_t VBO_BYTES = size_t(8) << 20;

const char* VS = R"(#version 300 es
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUv;
layout(location = 2) in vec4 aColor;
layout(location = 3) in vec4 aShape;
layout(location = 4) in vec2 aExtra;
uniform vec2 uScreen;
out vec2 vUv;
out vec4 vColor;
out vec4 vShape;
out vec2 vExtra;
void main()
{
    vUv = aUv;
    vColor = vec4(aColor.rgb * aColor.a, aColor.a);
    vShape = aShape;
    vExtra = aExtra;
    gl_Position = vec4(aPos.x / uScreen.x * 2.0 - 1.0, 1.0 - aPos.y / uScreen.y * 2.0, 0.0, 1.0);
}
)";

const char* FS = R"(#version 300 es
precision highp float;
uniform sampler2D uTex;
uniform sampler2D uBackdrop;
uniform mediump sampler2DArray uAtlas;
uniform vec2 uScreen;
uniform float uOverdraw;
uniform vec2 uBackdropShift;
in vec2 vUv;
in vec4 vColor;
in vec4 vShape;
in vec2 vExtra;
out vec4 fragColor;
float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + vec2(r);
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}
void main()
{
    int kind = int(vExtra.y + 0.5);
    if(kind == 4)
        fragColor = texture(uAtlas, vec3(vUv, vExtra.x)) * vColor;
    else if(kind == 5)
    {
        // Sprite with its player color layer (stored to the right of the base image) in one pass:
        // the same result as drawing the base and then the player layer on top
        vec4 base = texture(uAtlas, vec3(vUv, vExtra.x)) * vColor;
        vec4 pc = vec4(vShape.rgb * vShape.a, vShape.a);
        vec4 pl = texture(uAtlas, vec3(vUv.x + (vExtra.y - 5.0) * 2.0, vUv.y, vExtra.x)) * pc;
        fragColor = pl + base * (1.0 - pl.a);
    }
    else if(kind == 0)
        fragColor = texture(uTex, vUv) * vColor;
    else if(kind == 1)
        fragColor = vColor * texture(uTex, vUv).r;
    else if(kind == 3)
    {
        // Frosted glass: blurred backdrop, slightly desaturated and darkened, tinted, with a little grain
        float d = sdRoundBox(vUv, vShape.xy, vShape.z);
        float a = clamp(0.5 - d / max(vShape.w, 0.001), 0.0, 1.0);
        vec3 b = texture(uBackdrop, (gl_FragCoord.xy + uBackdropShift) / uScreen).rgb;
        float l = dot(b, vec3(0.299, 0.587, 0.114));
        b = mix(vec3(l), b, 0.6) * 0.85;
        float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
        vec3 col = b * (1.0 - vColor.a) + vColor.rgb + vec3(n * 0.012);
        fragColor = vec4(col, 1.0) * (a * vExtra.x);
    }
    else
    {
        float d = sdRoundBox(vUv, vShape.xy, vShape.z);
        float soft = max(vShape.w, 0.001);
        float a;
        if(vExtra.x > 0.0)
            a = clamp(0.5 - (abs(d + vExtra.x * 0.5) - vExtra.x * 0.5) / soft, 0.0, 1.0);
        else
            a = clamp(0.5 - d / soft, 0.0, 1.0);
        fragColor = vColor * a;
    }
    if(uOverdraw > 0.0)
        fragColor = vec4(uOverdraw, 0.0, 0.0, 0.0);
}
)";
const char* BLUR_VS = R"(#version 300 es
out vec2 vUv;
void main()
{
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// 9 tap gaussian with linear sampling (5 fetches)
const char* BLUR_FS = R"(#version 300 es
precision mediump float;
uniform sampler2D uTex;
uniform vec2 uDir;
in vec2 vUv;
out vec4 fragColor;
void main()
{
    vec3 c = texture(uTex, vUv).rgb * 0.2270270;
    c += texture(uTex, vUv + uDir * 1.3846154).rgb * 0.3162162;
    c += texture(uTex, vUv - uDir * 1.3846154).rgb * 0.3162162;
    c += texture(uTex, vUv + uDir * 3.2307692).rgb * 0.0702703;
    c += texture(uTex, vUv - uDir * 3.2307692).rgb * 0.0702703;
    fragColor = vec4(c, 1.0);
}
)";
} // namespace

Batch::~Batch()
{
    if(program_)
    {
        glDeleteProgram(program_);
        glDeleteBuffers(1, &vbo_);
        glDeleteBuffers(1, &ibo_);
        glDeleteVertexArrays(1, &vao_);
        glDeleteTextures(1, &whiteTex_);
        glDeleteTextures(1, &glassGround_);
    }
    for(Target* t : {&down_[0], &down_[1], &down_[2], &blurTmp_})
    {
        if(t->fbo)
        {
            glDeleteFramebuffers(1, &t->fbo);
            glDeleteTextures(1, &t->tex);
        }
    }
    if(blurProgram_)
    {
        glDeleteProgram(blurProgram_);
        glDeleteVertexArrays(1, &blurVao_);
    }
}

void Batch::init()
{
    program_ = createProgram(VS, FS);
    uScreen_ = glGetUniformLocation(program_, "uScreen");
    uBackdrop_ = glGetUniformLocation(program_, "uBackdrop");
    uOverdraw_ = glGetUniformLocation(program_, "uOverdraw");
    uBackdropShift_ = glGetUniformLocation(program_, "uBackdropShift");
    glUseProgram(program_);
    glUniform1i(glGetUniformLocation(program_, "uTex"), 0);
    glUniform1i(uBackdrop_, 1);
    glUniform1i(glGetUniformLocation(program_, "uAtlas"), 2);
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, VBO_BYTES, nullptr, GL_STREAM_DRAW);
    std::vector<uint16_t> idx;
    // 16 bit indices: draw in chunks of at most 16384 vertices
    for(unsigned q = 0; q < 4096; ++q)
    {
        const uint16_t b = static_cast<uint16_t>(q * 4);
        for(uint16_t i : {0, 1, 2, 2, 1, 3})
            idx.push_back(static_cast<uint16_t>(b + i));
    }
    glGenBuffers(1, &ibo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(uint16_t), idx.data(), GL_STATIC_DRAW);
    for(GLuint a = 0; a < 5; ++a)
        glEnableVertexAttribArray(a);
    setAttribs(0);
    glBindVertexArray(0);
    const uint32_t white = 0xFFFFFFFF;
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    vertices_.reserve(MAX_QUADS * 4);
    // Ground of the glass without blur: a dark warm tone close to the average blurred world
    const uint32_t ground = 0xFF2A3238u; // ABGR: r 0x38, g 0x32, b 0x2A
    glGenTextures(1, &glassGround_);
    glBindTexture(GL_TEXTURE_2D, glassGround_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &ground);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
}

void Batch::begin(int fbWidth, int fbHeight)
{
    fbW_ = fbWidth;
    fbH_ = fbHeight;
    backdropValid_ = false;
    backdropPrepared_ = false;
    hasCam_ = false;
    ++frameNo_;
    clip_ = {};
    resetTransform();
    glViewport(0, 0, fbW_, fbH_);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Batch::setClip(const Rect2& clip)
{
    flush();
    clip_ = clip;
    if(clip.w <= 0 || clip.h <= 0)
    {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(clip.x), static_cast<GLint>(fbH_ - clip.y - clip.h), static_cast<GLsizei>(clip.w),
              static_cast<GLsizei>(clip.h));
}

void Batch::setAttribs(size_t offset)
{
    const auto stride = static_cast<GLsizei>(sizeof(Vertex));
    auto at = [offset](size_t member) { return reinterpret_cast<void*>(offset + member); };
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex, x)));
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex, u)));
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, at(offsetof(Vertex, color)));
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex, hx)));
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, at(offsetof(Vertex, stroke)));
}

void Batch::flush()
{
    if(vertices_.empty())
        return;
    glUseProgram(program_);
    glUniform2f(uScreen_, static_cast<float>(fbW_), static_cast<float>(fbH_));
    glUniform1f(uOverdraw_, gOverdraw);
    glUniform2f(uBackdropShift_, shiftX_, shiftY_);
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D_ARRAY, atlasTex_);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, backdropValid_ ? down_[2].tex : glassGround_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, curTex_ ? curTex_ : whiteTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    // Streaming without stalls: append to the buffer unsynchronized, orphan it only when it is full
    const size_t total = vertices_.size();
    for(size_t start = 0; start < total; start += 4096 * 4)
    {
        const size_t count = std::min<size_t>(4096 * 4, total - start);
        const size_t bytes = count * sizeof(Vertex);
        if(vboOffset_ + bytes > VBO_BYTES)
        {
            glBufferData(GL_ARRAY_BUFFER, VBO_BYTES, nullptr, GL_STREAM_DRAW);
            vboOffset_ = 0;
        }
        void* dst = glMapBufferRange(GL_ARRAY_BUFFER, static_cast<GLintptr>(vboOffset_), static_cast<GLsizeiptr>(bytes),
                                     GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT | GL_MAP_INVALIDATE_RANGE_BIT);
        if(dst)
        {
            std::memcpy(dst, vertices_.data() + start, bytes);
            glUnmapBuffer(GL_ARRAY_BUFFER);
        } else
            glBufferSubData(GL_ARRAY_BUFFER, static_cast<GLintptr>(vboOffset_), static_cast<GLsizeiptr>(bytes),
                            vertices_.data() + start);
        setAttribs(vboOffset_);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count / 4 * 6), GL_UNSIGNED_SHORT, nullptr);
        vboOffset_ += (bytes + 255) & ~size_t(255);
        perf::drawCall();
    }
    glBindVertexArray(0);
    vertices_.clear();
}

void Batch::push(GLuint tex, const Vertex (&v)[4])
{
    if(tex != curTex_ || vertices_.size() + 4 > MAX_QUADS * 4)
    {
        flush();
        curTex_ = tex;
    }
    vertices_.insert(vertices_.end(), std::begin(v), std::end(v));
}

void Batch::quad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c,
                 Kind kind)
{
    x0 = x0 * scale_ + tx_;
    x1 = x1 * scale_ + tx_;
    y0 = y0 * scale_ + ty_;
    y1 = y1 * scale_ + ty_;
    if(x1 < 0 || y1 < 0 || x0 > fbW_ || y0 > fbH_)
        return;
    const uint32_t col = c.packed();
    const float k = static_cast<float>(kind);
    const Vertex v[4] = {{x0, y0, u0, v0, col, 0, 0, 0, 0, 0, k},
                         {x1, y0, u1, v0, col, 0, 0, 0, 0, 0, k},
                         {x0, y1, u0, v1, col, 0, 0, 0, 0, 0, k},
                         {x1, y1, u1, v1, col, 0, 0, 0, 0, 0, k}};
    push(tex, v);
}

void Batch::atlasQuad(float layer, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                      Color c)
{
    x0 = x0 * scale_ + tx_;
    x1 = x1 * scale_ + tx_;
    y0 = y0 * scale_ + ty_;
    y1 = y1 * scale_ + ty_;
    if(x1 < 0 || y1 < 0 || x0 > fbW_ || y0 > fbH_)
        return;
    const uint32_t col = c.packed();
    const float k = static_cast<float>(KindAtlas);
    const Vertex v[4] = {{x0, y0, u0, v0, col, 0, 0, 0, 0, layer, k},
                         {x1, y0, u1, v0, col, 0, 0, 0, 0, layer, k},
                         {x0, y1, u0, v1, col, 0, 0, 0, 0, layer, k},
                         {x1, y1, u1, v1, col, 0, 0, 0, 0, layer, k}};
    push(0, v);
}

void Batch::atlasQuadPlayer(float layer, float x0, float y0, float x1, float y1, float u0, float v0, float u1,
                            float v1, float du, Color c, Color player)
{
    x0 = x0 * scale_ + tx_;
    x1 = x1 * scale_ + tx_;
    y0 = y0 * scale_ + ty_;
    y1 = y1 * scale_ + ty_;
    if(x1 < 0 || y1 < 0 || x0 > fbW_ || y0 > fbH_)
        return;
    const uint32_t col = c.packed();
    // Kind 5, the offset of the player layer in the fraction (constant across the quad, exact in float)
    const float k = static_cast<float>(KindAtlasPlayer) + du * 0.5f;
    const float pr = player.r, pg = player.g, pb = player.b, pa = player.a;
    const Vertex v[4] = {{x0, y0, u0, v0, col, pr, pg, pb, pa, layer, k},
                         {x1, y0, u1, v0, col, pr, pg, pb, pa, layer, k},
                         {x0, y1, u0, v1, col, pr, pg, pb, pa, layer, k},
                         {x1, y1, u1, v1, col, pr, pg, pb, pa, layer, k}};
    push(0, v);
}

void Batch::shapeQuad(float cx, float cy, float hx, float hy, float ux, float uy, float extent, float radius, Color c,
                      float softness, float stroke, Color c2, float kind, const Hole* hole)
{
    // Quad around the shape, enlarged by extent (anti aliasing / shadow blur), axis (ux,uy) = local x direction
    cx = cx * scale_ + tx_;
    cy = cy * scale_ + ty_;
    const float ex = hx + extent, ey = hy + extent;
    const float vx = -uy, vy = ux;
    // Vertical gradient: extrapolate the colors to the enlarged quad so that top/bottom of the shape match
    Color colTop = c, colBottom = c;
    auto lerp = [](Color a, Color b, float t) {
        return Color{a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    };
    if(c2.a >= 0)
    {
        const float k = hy > 0 ? extent / (2 * hy) : 0;
        auto sat = [](Color x) {
            auto c01 = [](float v) { return std::min(1.f, std::max(0.f, v)); };
            return Color{c01(x.r), c01(x.g), c01(x.b), c01(x.a)};
        };
        colTop = sat(lerp(c, c2, -k));
        colBottom = sat(lerp(c, c2, 1 + k));
    }
    const uint32_t top = colTop.packed(), bottom = colBottom.packed();
    auto mk = [&](float lx, float ly) {
        // Inner vertices (holes): the color the full quad would interpolate there (premultiplied interpolation)
        uint32_t col = top;
        if(ly >= ey)
            col = bottom;
        else if(ly > -ey && top != bottom)
        {
            const float t = (ly + ey) / (2 * ey);
            const Color pt{colTop.r * colTop.a, colTop.g * colTop.a, colTop.b * colTop.a, colTop.a};
            const Color pb{colBottom.r * colBottom.a, colBottom.g * colBottom.a, colBottom.b * colBottom.a, colBottom.a};
            const Color p = lerp(pt, pb, t);
            col = (p.a > 0 ? Color{p.r / p.a, p.g / p.a, p.b / p.a, p.a} : Color{0, 0, 0, 0}).packed();
        }
        return Vertex{cx + ux * lx + vx * ly, cy + uy * lx + vy * ly, lx, ly, col, hx, hy, radius, softness, stroke, kind};
    };
    auto rect = [&](float x0, float y0, float x1, float y1) {
        if(x1 <= x0 || y1 <= y0)
            return;
        const Vertex v[4] = {mk(x0, y0), mk(x1, y0), mk(x0, y1), mk(x1, y1)};
        push(0, v);
    };
    // A hole that is invisible anyway (inside a stroke, below an opaque panel) is left out: saves fill rate
    if(hole)
    {
        const float hx0 = std::max(hole->x0 * scale_, -ex), hx1 = std::min(hole->x1 * scale_, ex);
        const float hy0 = std::max(hole->y0 * scale_, -ey), hy1 = std::min(hole->y1 * scale_, ey);
        if(hx1 - hx0 > 8 && hy1 - hy0 > 8)
        {
            rect(-ex, -ey, ex, hy0);
            rect(-ex, hy1, ex, ey);
            rect(-ex, hy0, hx0, hy1);
            rect(hx1, hy0, ex, hy1);
            return;
        }
    }
    rect(-ex, -ey, ex, ey);
}

namespace {
/// Inside of a stroked rounded rectangle that the stroke does not touch (local coordinates, unscaled)
bool strokeHole(const Rect2& r, float radius, float softness, float stroke, Batch::Hole& h)
{
    if(stroke <= 0)
        return false;
    const float in = std::max(radius, stroke) + softness + 2;
    h = {-r.w / 2 + in, -r.h / 2 + in, r.w / 2 - in, r.h / 2 - in};
    return h.x1 > h.x0 && h.y1 > h.y0;
}
} // namespace

void Batch::roundRect(const Rect2& r, float radius, Color c, float softness, float stroke)
{
    const float s = scale_;
    Hole h;
    const bool hole = strokeHole(r, radius, softness / s, stroke, h);
    shapeQuad(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 * s, r.h / 2 * s, 1, 0, softness + 1, radius * s, c, softness,
              stroke * s, {0, 0, 0, -1}, 2.f, hole ? &h : nullptr);
}

void Batch::roundRectV(const Rect2& r, float radius, Color top, Color bottom, float softness, float stroke)
{
    const float s = scale_;
    Hole h;
    const bool hole = strokeHole(r, radius, softness / s, stroke, h);
    shapeQuad(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 * s, r.h / 2 * s, 1, 0, softness + 1, radius * s, top, softness,
              stroke * s, bottom, 2.f, hole ? &h : nullptr);
}

void Batch::glass(const Rect2& r, float radius, Color tint, float opacity)
{
    if(!backdropPrepared_)
        prepareBackdrop();
    const float s = scale_;
    shapeQuad(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 * s, r.h / 2 * s, 1, 0, 2, radius * s, tint, 1.f, opacity,
              {0, 0, 0, -1}, static_cast<float>(KindGlass));
}

void Batch::prepareBackdrop()
{
    flush();
    backdropPrepared_ = true;
    backdropValid_ = true;
    shiftX_ = shiftY_ = 0;
    if(glassQuality_ <= 0)
    {
        // No blur: the glass shows a dark ground (1x1 texture)
        backdropValid_ = false;
        return;
    }
    // Reuse the blur of a recent frame when the view did not change much: saves the copy of the frame (splits the
    // render pass on tiling GPUs) and the blur passes. Pans move the old picture along.
    constexpr unsigned REUSE_FRAMES = 4;
    bool reuse = glassQuality_ == 1 && captureFrame_ && frameNo_ - captureFrame_ < REUSE_FRAMES && capW_ == fbW_
                 && capH_ == fbH_ && hasCam_ == capHasCam_;
    if(reuse && hasCam_)
    {
        const float zoom = cam_[2];
        shiftX_ = (cam_[0] - capCam_[0]) * zoom;
        shiftY_ = -(cam_[1] - capCam_[1]) * zoom;
        reuse = cam_[2] == capCam_[2] && std::abs(shiftX_) < fbW_ / 8.f && std::abs(shiftY_) < fbH_ / 8.f;
    }
    if(reuse)
        return;
    shiftX_ = shiftY_ = 0;
    captureBackdrop();
    captureFrame_ = frameNo_;
    capW_ = fbW_;
    capH_ = fbH_;
    capHasCam_ = hasCam_;
    std::copy(std::begin(cam_), std::end(cam_), std::begin(capCam_));
}

void Batch::initBlur()
{
    if(!blurProgram_)
    {
        blurProgram_ = createProgram(BLUR_VS, BLUR_FS);
        uBlurDir_ = glGetUniformLocation(blurProgram_, "uDir");
        uBlurTex_ = glGetUniformLocation(blurProgram_, "uTex");
        glGenVertexArrays(1, &blurVao_);
    }
    auto make = [](Target& t, int w, int h) {
        if(t.w == w && t.h == h && t.fbo)
            return;
        if(!t.fbo)
        {
            glGenTextures(1, &t.tex);
            glGenFramebuffers(1, &t.fbo);
        }
        t.w = w;
        t.h = h;
        glBindTexture(GL_TEXTURE_2D, t.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    };
    int w = fbW_, h = fbH_;
    for(Target& t : down_)
    {
        w = std::max(1, (w + 1) / 2);
        h = std::max(1, (h + 1) / 2);
        make(t, w, h);
    }
    make(blurTmp_, down_[2].w, down_[2].h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Batch::captureBackdrop()
{
    flush();
    initBlur();
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    // Downsample in steps of two (each step averages 2x2 pixels) to 1/8 size
    GLuint src = 0;
    int sw = fbW_, sh = fbH_;
    for(int i = 0; i < 3; ++i)
    {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, src);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, down_[i].fbo);
        glBlitFramebuffer(0, 0, sw, sh, 0, 0, down_[i].w, down_[i].h, GL_COLOR_BUFFER_BIT, GL_LINEAR);
        src = down_[i].fbo;
        sw = down_[i].w;
        sh = down_[i].h;
    }
    // Two separable gaussian passes, twice
    glUseProgram(blurProgram_);
    glUniform1i(uBlurTex_, 0);
    glBindVertexArray(blurVao_);
    glActiveTexture(GL_TEXTURE0);
    glViewport(0, 0, sw, sh);
    const Target& t = down_[2];
    for(int k = 0; k < 2; ++k)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, blurTmp_.fbo);
        glBindTexture(GL_TEXTURE_2D, t.tex);
        glUniform2f(uBlurDir_, 1.f / static_cast<float>(sw), 0.f);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glBindTexture(GL_TEXTURE_2D, blurTmp_.tex);
        glUniform2f(uBlurDir_, 0.f, 1.f / static_cast<float>(sh));
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }
    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, fbW_, fbH_);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    if(clip_.w > 0 && clip_.h > 0)
        glEnable(GL_SCISSOR_TEST);
}

bool Batch::readBackdrop(std::vector<uint8_t>& rgba, int& w, int& h)
{
    const Target& t = down_[1];
    if(!t.fbo)
        return false;
    flush();
    w = t.w;
    h = t.h;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    rgba.resize(px.size());
    const size_t row = static_cast<size_t>(w) * 4;
    for(int y = 0; y < h; ++y)
        std::copy_n(px.data() + (h - 1 - y) * row, row, rgba.data() + y * row);
    for(size_t i = 3; i < rgba.size(); i += 4)
        rgba[i] = 255;
    return true;
}

void Batch::shadow(const Rect2& r, float radius, float blur, Color c, const Rect2* covered)
{
    if(!covered)
    {
        roundRect(r, radius, c, blur);
        return;
    }
    // The part below an opaque rectangle is not drawn
    const float cx = r.x + r.w / 2, cy = r.y + r.h / 2;
    const Hole h{covered->x - cx, covered->y - cy, covered->right() - cx, covered->bottom() - cy};
    const float s = scale_;
    shapeQuad(cx, cy, r.w / 2 * s, r.h / 2 * s, 1, 0, blur + 1, radius * s, c, blur, 0.f, {0, 0, 0, -1}, 2.f, &h);
}

void Batch::circle(float cx, float cy, float radius, Color c, float stroke, float softness)
{
    const float rs = radius * scale_;
    shapeQuad(cx, cy, rs, rs, 1, 0, softness + 1, rs, c, softness, stroke * scale_);
}

void Batch::line(float x0, float y0, float x1, float y1, float width, Color c)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);
    const float ux = len > 0 ? dx / len : 1.f, uy = len > 0 ? dy / len : 0.f;
    const float hw = width / 2 * scale_;
    shapeQuad((x0 + x1) / 2, (y0 + y1) / 2, len / 2 * scale_ + hw, hw, ux, uy, 1.5f, hw, c, 1.f, 0.f);
}

} // namespace weiler::gfx
