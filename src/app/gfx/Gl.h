// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <GLES3/gl3.h>
#include <cstdint>
#include <string>
#include <utility>

namespace weiler::gfx {

/// Compile and link a program; throws std::runtime_error with the log on failure
GLuint createProgram(const char* vertexSrc, const char* fragmentSrc);

/// Owning wrapper for a GL texture
class Texture
{
public:
    Texture() = default;
    ~Texture() { reset(); }
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& o) noexcept : id_(std::exchange(o.id_, 0)), w_(o.w_), h_(o.h_) {}
    Texture& operator=(Texture&& o) noexcept
    {
        if(this != &o)
        {
            reset();
            id_ = std::exchange(o.id_, 0);
            w_ = o.w_;
            h_ = o.h_;
        }
        return *this;
    }

    /// Create an RGBA8 (or R8 if singleChannel) texture
    void create(int w, int h, bool singleChannel = false, bool linear = true, const void* pixels = nullptr);
    void upload(int x, int y, int w, int h, const void* pixels, bool singleChannel = false);
    void setFilter(bool linear);
    void reset();
    GLuint id() const { return id_; }
    int width() const { return w_; }
    int height() const { return h_; }
    explicit operator bool() const { return id_ != 0; }

private:
    GLuint id_ = 0;
    int w_ = 0, h_ = 0;
};

/// Premultiplied RGBA color as floats
struct Color
{
    float r = 1, g = 1, b = 1, a = 1;
    static Color fromARGB(uint32_t argb)
    {
        return {((argb >> 16) & 0xFF) / 255.f, ((argb >> 8) & 0xFF) / 255.f, (argb & 0xFF) / 255.f,
                ((argb >> 24) & 0xFF) / 255.f};
    }
    static constexpr Color rgb(uint32_t rgb, float a = 1.f)
    {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, a};
    }
    Color withAlpha(float na) const { return {r, g, b, na}; }
    Color operator*(float f) const { return {r * f, g * f, b * f, a * f}; }
    /// Pack as RGBA8 (non premultiplied, the shader premultiplies)
    uint32_t packed() const;
};

} // namespace weiler::gfx
