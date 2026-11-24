// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DrawPoint.h"
#include "ITexture.h"
#include "ogl/SpriteRenderer.h"
#include "libsiedler2/ArchivItem_BitmapBase.h"

/// Base class for drawable bitmaps. Weiler: textures are managed by the frontend's ISpriteRenderer.
class glArchivItem_BitmapBase : public virtual libsiedler2::ArchivItem_BitmapBase, public ITexture, public ISpriteSource
{
public:
    glArchivItem_BitmapBase() = default;
    glArchivItem_BitmapBase(const glArchivItem_BitmapBase& item);
    ~glArchivItem_BitmapBase() override = default;

    /// Drop cached texture data (e.g. after the pixels changed)
    void deleteTexture();
    void setInterpolateTexture(bool interpolate) { interpolateTexture_ = interpolate; }
    bool isInterpolated() const { return interpolateTexture_; }

    /// Return the "Null point"
    DrawPoint getOrigin() const override { return DrawPoint(nx_, ny_); }
    Extent getSize() const override { return Extent(getWidth(), getHeight()); }

    Extent getSourceSize() const override { return getSize(); }

protected:
    /// Make sure a palette is set for paletted images
    void ensurePalette() const;
    /// Common draw implementation (handles the compatibility rules for empty sizes)
    void drawImpl(Rect dstArea, Rect srcArea, unsigned color, unsigned playerColor);

private:
    bool interpolateTexture_ = true;
};
