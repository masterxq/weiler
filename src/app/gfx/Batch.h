// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Gl.h"
#include <vector>

namespace weiler::gfx {

struct Rect2
{
    float x = 0, y = 0, w = 0, h = 0;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
    Rect2 inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
};

/// Unified 2D batch renderer: textured sprites, glyphs and anti aliased shapes (signed distance fields).
/// Coordinates are framebuffer pixels after applying the current transform (scale + translation).
class Batch
{
public:
    enum Kind : int
    {
        KindTexture = 0,
        KindGlyph = 1,
        KindShape = 2,
        /// Rounded rectangle filled with the blurred backdrop, tinted (frosted glass)
        KindGlass = 3,
        /// Sprite from the texture array of the sprite atlas (layer in the vertex)
        KindAtlas = 4,
        /// Atlas sprite with its player color layer in one pass
        KindAtlasPlayer = 5
    };

    Batch() = default;
    ~Batch();
    void init();
    /// Start a frame with the given framebuffer size
    void begin(int fbWidth, int fbHeight);
    void flush();

    void setTransform(float scale, float tx, float ty)
    {
        scale_ = scale;
        tx_ = tx;
        ty_ = ty;
    }
    void resetTransform() { setTransform(1.f, 0.f, 0.f); }
    float scale() const { return scale_; }
    /// Restrict drawing to a rectangle in framebuffer pixels (w <= 0 disables)
    void setClip(const Rect2& clip);

    void quad(GLuint tex, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c,
              Kind kind = KindTexture);
    /// Sprite from the atlas texture array (see setAtlas); never breaks a batch of shapes or other atlas sprites
    void atlasQuad(float layer, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, Color c);
    /// Atlas sprite plus its player layer (du to the right, colored with player) in one quad
    void atlasQuadPlayer(float layer, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                         float du, Color c, Color player);
    /// Texture array used by atlasQuad
    void setAtlas(GLuint arrayTex) { atlasTex_ = arrayTex; }
    /// Filled (stroke = 0) or outlined rounded rectangle. softness widens the anti aliasing (for shadows).
    void roundRect(const Rect2& r, float radius, Color c, float softness = 1.f, float stroke = 0.f);
    /// Rounded rectangle with a vertical gradient from top to bottom color
    void roundRectV(const Rect2& r, float radius, Color top, Color bottom, float softness = 1.f, float stroke = 0.f);
    /// Frosted glass: blurred copy of what was drawn so far, tinted with tint (alpha = tint strength).
    /// The backdrop is captured once per frame on first use.
    void glass(const Rect2& r, float radius, Color tint, float opacity = 1.f);
    /// Copy of the last captured backdrop (world without interface) at a quarter of the screen size, RGBA top-down.
    /// False if nothing was captured yet.
    bool readBackdrop(std::vector<uint8_t>& rgba, int& w, int& h);
    /// Quality of the glass: 2 = blur captured every frame, 1 = blur reused for a few frames (moved along with the
    /// camera), 0 = no blur (tinted glass over a dark ground)
    void setGlassQuality(int q) { glassQuality_ = q; }
    /// Camera of the world drawn this frame (lets a reused blur follow pans); call after begin()
    void setWorldCamera(float x, float y, float zoom)
    {
        cam_[0] = x, cam_[1] = y, cam_[2] = zoom;
        hasCam_ = true;
    }
    void circle(float cx, float cy, float radius, Color c, float stroke = 0.f, float softness = 1.f);
    /// Line with round caps
    void line(float x0, float y0, float x1, float y1, float width, Color c);
    /// Shadow below a rounded rectangle; covered = part hidden by an opaque rectangle drawn later (not drawn)
    void shadow(const Rect2& r, float radius, float blur, Color c, const Rect2* covered = nullptr);
    /// Rectangle left out of a shape quad (local pixel coordinates relative to the shape center, before scaling)
    struct Hole
    {
        float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    };

    int width() const { return fbW_; }
    int height() const { return fbH_; }

private:
    struct Vertex
    {
        float x, y, u, v;
        uint32_t color;
        float hx, hy, radius, soft;
        float stroke, kind;
    };
    void push(GLuint tex, const Vertex (&v)[4]);
    void setAttribs(size_t offset);
    void shapeQuad(float cx, float cy, float hx, float hy, float ux, float uy, float extent, float radius, Color c,
                   float softness, float stroke, Color c2 = {0, 0, 0, -1}, float kind = 2.f, const Hole* hole = nullptr);
    void captureBackdrop();
    void prepareBackdrop();
    void initBlur();

    GLuint program_ = 0, vao_ = 0, vbo_ = 0, ibo_ = 0, whiteTex_ = 0, glassGround_ = 0;
    GLint uScreen_ = -1, uBackdrop_ = -1, uOverdraw_ = -1, uBackdropShift_ = -1;
    // Backdrop blur: chain of half size render targets, the last one is blurred in two passes
    struct Target
    {
        GLuint tex = 0, fbo = 0;
        int w = 0, h = 0;
    };
    Target down_[3], blurTmp_;
    GLuint blurProgram_ = 0, blurVao_ = 0;
    GLint uBlurDir_ = -1, uBlurTex_ = -1;
    bool backdropValid_ = false, backdropPrepared_ = false;
    /// Reuse of the blurred backdrop: frame counter, frame/camera/size of the last capture
    unsigned frameNo_ = 0, captureFrame_ = 0;
    int glassQuality_ = 1;
    float cam_[3] = {0, 0, 1}, capCam_[3] = {0, 0, 1};
    bool hasCam_ = false, capHasCam_ = false;
    int capW_ = 0, capH_ = 0;
    float shiftX_ = 0, shiftY_ = 0;
    Rect2 clip_;
    std::vector<Vertex> vertices_;
    GLuint curTex_ = 0;
    GLuint atlasTex_ = 0;
    size_t vboOffset_ = 0;
    int fbW_ = 1, fbH_ = 1;
    float scale_ = 1.f, tx_ = 0.f, ty_ = 0.f;
};

} // namespace weiler::gfx
