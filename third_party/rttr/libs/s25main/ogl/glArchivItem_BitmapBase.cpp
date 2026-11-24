// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "glArchivItem_BitmapBase.h"
#include "Loader.h"
#include "RTTR_Assert.h"

glArchivItem_BitmapBase::glArchivItem_BitmapBase(const glArchivItem_BitmapBase& item)
    : ArchivItem_BitmapBase(item), ITexture(item), ISpriteSource(item), interpolateTexture_(item.interpolateTexture_)
{}

void glArchivItem_BitmapBase::deleteTexture()
{
    if(rendererHandle && getSpriteRenderer())
        getSpriteRenderer()->releaseSource(*this);
    rendererHandle = 0;
}

void glArchivItem_BitmapBase::ensurePalette() const
{
    auto& self = const_cast<glArchivItem_BitmapBase&>(*this);
    if(!getPalette() && getFormat() == libsiedler2::TextureFormat::Paletted)
        self.setPaletteCopy(*LOADER.getPaletteN("pal5"));
}

void glArchivItem_BitmapBase::drawImpl(Rect dstArea, Rect srcArea, unsigned color, unsigned playerColor)
{
    ISpriteRenderer* renderer = getSpriteRenderer();
    if(!renderer || getWidth() == 0 || getHeight() == 0)
        return;
    // Compatibility only!
    Extent srcSize = srcArea.getSize();
    if(srcSize.x == 0)
        srcSize.x = getWidth();
    if(srcSize.y == 0)
        srcSize.y = getHeight();
    srcArea.setSize(srcSize);
    Extent dstSize = dstArea.getSize();
    if(dstSize.x == 0)
        dstSize.x = srcSize.x;
    if(dstSize.y == 0)
        dstSize.y = srcSize.y;
    dstArea.setSize(dstSize);
    dstArea.move(-getOrigin());
    renderer->drawSprite(*this, dstArea, srcArea, color, playerColor);
}
