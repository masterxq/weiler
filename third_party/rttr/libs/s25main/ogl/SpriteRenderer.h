// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Weiler: narrow drawing interface replacing the former OpenGL 2 code of the bitmap items.
// Game objects keep their Draw methods; all their sprite draws end up here. The frontend implements the
// renderer (texture atlases, batching, player colors in the shader). Without a renderer nothing is drawn.

#include "DrawPoint.h"
#include "Point.h"
#include "Rect.h"
#include <cstdint>
#include <string>

namespace libsiedler2 {
class PixelBufferBGRA;
}

/// Something that can be rasterized into a texture.
/// The image has an optional player color layer of the same size: a gray mask that is multiplied with the
/// player color and drawn over the base image.
class ISpriteSource
{
public:
    ISpriteSource() = default;
    ISpriteSource(const ISpriteSource&) : rendererHandle(0) {}
    ISpriteSource& operator=(const ISpriteSource&) { return *this; }
    virtual ~ISpriteSource();

    /// Size of the base image
    virtual Extent getSourceSize() const = 0;
    virtual bool hasPlayerLayer() const = 0;
    /// Write the base image at (0,0) and, if present, the player layer at (getSourceSize().x, 0).
    /// The buffer is at least (2*w or w, h) large and initialized transparent.
    virtual void renderSource(libsiedler2::PixelBufferBGRA& buffer) const = 0;

    /// Handle of the renderer cache (0 = not yet uploaded)
    mutable uint32_t rendererHandle = 0;
};

class ISpriteRenderer
{
public:
    virtual ~ISpriteRenderer() = default;
    /// Draw part of a sprite. dst is in view coordinates (already including the origin offset),
    /// src in pixels of the base image. color modulates the base image (ARGB), playerColor the player layer
    /// (0 = do not draw the player layer).
    virtual void drawSprite(const ISpriteSource& src, const Rect& dst, const Rect& srcRect, unsigned color,
                            unsigned playerColor) = 0;
    virtual void drawRect(const Rect& rect, unsigned color) = 0;
    virtual void drawLine(DrawPoint pt1, DrawPoint pt2, unsigned width, unsigned color) = 0;
    /// Small text centered horizontally at pt (view coordinates)
    virtual void drawText(DrawPoint pt, const std::string& text, unsigned color) = 0;
    /// The source is destroyed or changed, drop cached data
    virtual void releaseSource(const ISpriteSource& src) = 0;
};

/// Currently active renderer (may be nullptr)
ISpriteRenderer* getSpriteRenderer();
void setSpriteRenderer(ISpriteRenderer* renderer);
