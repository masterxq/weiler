// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Batch.h"
#include <cmath>

namespace weiler::gfx {

namespace {
constexpr unsigned MAX_QUADS = 16384;

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
    if(kind == 0)
        fragColor = texture(uTex, vUv) * vColor;
    else if(kind == 1)
        fragColor = vColor * texture(uTex, vUv).r;
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
    }
}

void Batch::init()
{
    program_ = createProgram(VS, FS);
    uScreen_ = glGetUniformLocation(program_, "uScreen");
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    glGenBuffers(1, &vbo_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, MAX_QUADS * 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
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
    const auto stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, u)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, hx)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(offsetof(Vertex, stroke)));
    glBindVertexArray(0);
    const uint32_t white = 0xFFFFFFFF;
    glGenTextures(1, &whiteTex_);
    glBindTexture(GL_TEXTURE_2D, whiteTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    vertices_.reserve(MAX_QUADS * 4);
}

void Batch::begin(int fbWidth, int fbHeight)
{
    fbW_ = fbWidth;
    fbH_ = fbHeight;
    drawCalls_ = 0;
    resetTransform();
    glViewport(0, 0, fbW_, fbH_);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}

void Batch::setClip(const Rect2& clip)
{
    flush();
    if(clip.w <= 0 || clip.h <= 0)
    {
        glDisable(GL_SCISSOR_TEST);
        return;
    }
    glEnable(GL_SCISSOR_TEST);
    glScissor(static_cast<GLint>(clip.x), static_cast<GLint>(fbH_ - clip.y - clip.h), static_cast<GLsizei>(clip.w),
              static_cast<GLsizei>(clip.h));
}

void Batch::flush()
{
    if(vertices_.empty())
        return;
    glUseProgram(program_);
    glUniform2f(uScreen_, static_cast<float>(fbW_), static_cast<float>(fbH_));
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, curTex_ ? curTex_ : whiteTex_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    const size_t total = vertices_.size();
    for(size_t start = 0; start < total; start += 4096 * 4)
    {
        const size_t count = std::min<size_t>(4096 * 4, total - start);
        glBufferData(GL_ARRAY_BUFFER, MAX_QUADS * 4 * sizeof(Vertex), nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, count * sizeof(Vertex), vertices_.data() + start);
        glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count / 4 * 6), GL_UNSIGNED_SHORT, nullptr);
        ++drawCalls_;
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

void Batch::shapeQuad(float cx, float cy, float hx, float hy, float ux, float uy, float extent, float radius, Color c,
                      float softness, float stroke)
{
    // Quad around the shape, enlarged by extent (anti aliasing / shadow blur), axis (ux,uy) = local x direction
    cx = cx * scale_ + tx_;
    cy = cy * scale_ + ty_;
    const float ex = hx + extent, ey = hy + extent;
    const float vx = -uy, vy = ux;
    const uint32_t col = c.packed();
    auto mk = [&](float lx, float ly) {
        return Vertex{cx + ux * lx + vx * ly, cy + uy * lx + vy * ly, lx, ly, col, hx, hy, radius, softness,
                      stroke, static_cast<float>(KindShape)};
    };
    const Vertex v[4] = {mk(-ex, -ey), mk(ex, -ey), mk(-ex, ey), mk(ex, ey)};
    push(0, v);
}

void Batch::roundRect(const Rect2& r, float radius, Color c, float softness, float stroke)
{
    const float s = scale_;
    shapeQuad(r.x + r.w / 2, r.y + r.h / 2, r.w / 2 * s, r.h / 2 * s, 1, 0, softness + 1, radius * s, c, softness,
              stroke * s);
}

void Batch::shadow(const Rect2& r, float radius, float blur, Color c)
{
    roundRect(r, radius, c, blur);
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
