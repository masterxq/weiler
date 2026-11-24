// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Gl.h"
#include <algorithm>
#include <stdexcept>

namespace weiler::gfx {

namespace {
GLuint compile(GLenum type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok)
    {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        glDeleteShader(s);
        throw std::runtime_error(std::string("Shader compile error: ") + log);
    }
    return s;
}
} // namespace

GLuint createProgram(const char* vertexSrc, const char* fragmentSrc)
{
    GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc);
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok)
    {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        glDeleteProgram(p);
        throw std::runtime_error(std::string("Program link error: ") + log);
    }
    return p;
}

void Texture::create(int w, int h, bool singleChannel, bool linear, const void* pixels)
{
    reset();
    w_ = w;
    h_ = h;
    glGenTextures(1, &id_);
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    if(singleChannel)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, pixels);
    else
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    setFilter(linear);
}

void Texture::setFilter(bool linear)
{
    glBindTexture(GL_TEXTURE_2D, id_);
    const GLint f = linear ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
}

void Texture::upload(int x, int y, int w, int h, const void* pixels, bool singleChannel)
{
    glBindTexture(GL_TEXTURE_2D, id_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, w, h, singleChannel ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, pixels);
}

void Texture::reset()
{
    if(id_)
        glDeleteTextures(1, &id_);
    id_ = 0;
}

uint32_t Color::packed() const
{
    auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
    return c(r) | (c(g) << 8) | (c(b) << 16) | (c(a) << 24);
}

} // namespace weiler::gfx
