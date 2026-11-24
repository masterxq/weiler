// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "glArchivItem_Bitmap.h"
#include "Point.h"
#include "RTTR_Assert.h"
#include "libsiedler2/PixelBufferBGRA.h"
#include <algorithm>

glArchivItem_Bitmap::glArchivItem_Bitmap() = default;

glArchivItem_Bitmap::glArchivItem_Bitmap(const glArchivItem_Bitmap& item)
    : ArchivItem_BitmapBase(item), baseArchivItem_Bitmap(item), glArchivItem_BitmapBase(item)
{}

void glArchivItem_Bitmap::draw(Rect dstArea, Rect srcArea, unsigned color /*= COLOR_WHITE*/)
{
    RTTR_Assert(getBobType() != libsiedler2::BobType::BitmapPlayer);
    drawImpl(dstArea, srcArea, color, 0);
}

void glArchivItem_Bitmap::drawFull(const Rect& destArea, unsigned color)
{
    draw(destArea, Rect(Position(0, 0), getSize()), color);
}

void glArchivItem_Bitmap::drawFull(const DrawPoint& dstPos, unsigned color)
{
    drawFull(Rect(dstPos, getSize()), color);
}

void glArchivItem_Bitmap::drawPart(const Rect& destArea, const DrawPoint& offset, unsigned color)
{
    draw(destArea, Rect(offset, destArea.getSize()), color);
}

void glArchivItem_Bitmap::drawPart(const Rect& destArea, unsigned color /*= COLOR_WHITE*/)
{
    drawPart(destArea, DrawPoint::all(0), color);
}

void glArchivItem_Bitmap::drawPercent(const DrawPoint& dstPos, unsigned percent, unsigned color /*= COLOR_WHITE*/)
{
    RTTR_Assert(percent <= 100);
    if(percent == 0u)
        return;
    unsigned drawnHeight = getHeight() * std::min(100u, percent) / 100;
    DrawPoint offset(0, getHeight() - drawnHeight);
    drawPart(Rect(dstPos + offset, getWidth(), drawnHeight), offset, color);
}

void glArchivItem_Bitmap::renderSource(libsiedler2::PixelBufferBGRA& buffer) const
{
    ensurePalette();
    print(buffer);
}
