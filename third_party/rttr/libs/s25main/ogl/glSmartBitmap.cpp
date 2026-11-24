// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "glSmartBitmap.h"
#include "Loader.h"
#include "helpers/mathFuncs.h"
#include "ogl/glBitmapItem.h"
#include "libsiedler2/ArchivItem_Bitmap.h"
#include "libsiedler2/ArchivItem_Bitmap_Player.h"
#include "libsiedler2/PixelBufferBGRA.h"
#include "s25util/colors.h"
#include <cmath>
#include <limits>


glSmartBitmap::glSmartBitmap() : origin_(0, 0), size_(0, 0), hasPlayer(false) {}

glSmartBitmap::~glSmartBitmap()
{
    reset();
}

void glSmartBitmap::reset()
{
    if(rendererHandle && getSpriteRenderer())
        getSpriteRenderer()->releaseSource(*this);
    rendererHandle = 0;

    for(glBitmapItem& bmpItem : items)
    {
        if(bmpItem.isOwning_)
            delete bmpItem.bmp;
    }
    items.clear();
}

Extent glSmartBitmap::getRequiredTexSize() const
{
    Extent texSize(size_);
    if(hasPlayer)
        texSize.x *= 2;
    return texSize;
}

void glSmartBitmap::add(libsiedler2::ArchivItem_Bitmap_Player* bmp, int nyOffset)
{
    if(!bmp)
        return;
    items.push_back(glBitmapItem(bmp, nyOffset));
    calcDimensions();
}

void glSmartBitmap::add(libsiedler2::baseArchivItem_Bitmap* bmp)
{
    if(!bmp)
        return;
    items.push_back(glBitmapItem(bmp));
    calcDimensions();
}

void glSmartBitmap::add(std::unique_ptr<libsiedler2::baseArchivItem_Bitmap> bmp)
{
    if(!bmp)
        return;
    items.push_back(glBitmapItem(bmp.release(), false, true));
    calcDimensions();
}

void glSmartBitmap::addShadow(libsiedler2::baseArchivItem_Bitmap* bmp)
{
    if(!bmp)
        return;
    items.push_back(glBitmapItem(bmp, true));
    calcDimensions();
}

void glSmartBitmap::calcDimensions()
{
    if(items.empty())
    {
        origin_ = Position(0, 0);
        size_ = Extent(0, 0);
        return;
    }

    origin_.x = origin_.y = std::numeric_limits<int>::min();
    Position maxPos = origin_;

    hasPlayer = false;

    for(const glBitmapItem& bmpItem : items)
    {
        if(bmpItem.type == glBitmapItemType::PlayerBitmap)
            hasPlayer = true;

        origin_ = elMax(origin_, bmpItem.origin);
        maxPos = elMax(maxPos, bmpItem.size - bmpItem.origin);
    }

    size_ = Extent(origin_ + maxPos);
}

void glSmartBitmap::drawTo(libsiedler2::PixelBufferBGRA& buffer, const Extent& bufOffset) const
{
    libsiedler2::ArchivItem_Palette* p_colors = LOADER.getPaletteN("colors");
    libsiedler2::ArchivItem_Palette* p_5 = LOADER.getPaletteN("pal5");

    for(const glBitmapItem& bmpItem : items)
    {
        if((bmpItem.size.x == 0) || (bmpItem.size.y == 0))
            continue;

        DrawPoint offset = origin_ - bmpItem.origin;

        if(bmpItem.type == glBitmapItemType::ShadowBitmap)
        {
            libsiedler2::PixelBufferBGRA tmp(size_.x, size_.y);

            dynamic_cast<libsiedler2::baseArchivItem_Bitmap*>(bmpItem.bmp) //-V522
              ->print(tmp, p_5, offset.x, offset.y, bmpItem.pos.x, bmpItem.pos.y, bmpItem.size.x, bmpItem.size.y);

            unsigned tmpIdx = 0;
            for(unsigned y = 0; y < size_.y; ++y)
            {
                unsigned idx = buffer.calcIdx(bufOffset.x, bufOffset.y + y);
                for(unsigned x = 0; x < size_.x; ++x)
                {
                    if(tmp.get(tmpIdx).getAlpha() != 0x00 && buffer.get(idx).getAlpha() == 0x00)
                        buffer.set(idx, libsiedler2::ColorBGRA(0, 0, 0, 0x40));
                    idx++;
                    tmpIdx++;
                }
            }
        } else if(!hasPlayer)
        {
            // No player bitmap -> Just (over)write the data
            RTTR_Assert(bmpItem.type == glBitmapItemType::Normal);
            dynamic_cast<libsiedler2::baseArchivItem_Bitmap*>(bmpItem.bmp)
              ->print(buffer, p_5, offset.x + bufOffset.x, offset.y + bufOffset.y, bmpItem.pos.x, bmpItem.pos.y,
                      bmpItem.size.x, bmpItem.size.y);
        } else
        {
            // There is a player bitmap -> First write to temp buffer
            libsiedler2::PixelBufferBGRA tmp(size_.x, size_.y);
            if(bmpItem.type == glBitmapItemType::Normal)
            {
                dynamic_cast<libsiedler2::baseArchivItem_Bitmap*>(bmpItem.bmp)
                  ->print(tmp, p_5, offset.x, offset.y, bmpItem.pos.x, bmpItem.pos.y, bmpItem.size.x, bmpItem.size.y);
            } else
            {
                dynamic_cast<libsiedler2::ArchivItem_Bitmap_Player*>(bmpItem.bmp)
                  ->print(tmp, p_colors, 128, offset.x, offset.y, bmpItem.pos.x, bmpItem.pos.y, bmpItem.size.x,
                          bmpItem.size.y, false);
            }
            // Now copy temp buffer to real buffer, but we need to reset all player colors that would be overwritten
            // so it looks like, the first bitmap is fully drawn (including player colors) and then the next
            // overwrites it
            unsigned tmpIdx = 0;
            for(unsigned y = 0; y < size_.y; ++y)
            {
                unsigned idx = buffer.calcIdx(bufOffset.x + offset.x, bufOffset.y + y);
                tmpIdx += offset.x;
                for(unsigned x = offset.x; x < size_.x; x++)
                {
                    // Check for non-transparent pixels
                    if(tmp.get(tmpIdx).getAlpha())
                    {
                        // Copy to buffer
                        buffer.set(idx, tmp.get(tmpIdx));
                        // Reset player color to transparent
                        buffer.set(idx + size_.x, libsiedler2::ColorBGRA());
                    }
                    ++idx;
                    ++tmpIdx;
                }
            }
            // Finally write the player color part if it has one
            if(bmpItem.type == glBitmapItemType::PlayerBitmap)
            {
                dynamic_cast<libsiedler2::ArchivItem_Bitmap_Player*>(bmpItem.bmp)
                  ->print(buffer, p_colors, 128, offset.x + size_.x + bufOffset.x, offset.y + bufOffset.y,
                          bmpItem.pos.x, bmpItem.pos.y, bmpItem.size.x, bmpItem.size.y, true);
            }
        }
    }
}

void glSmartBitmap::draw(Rect dstArea, Rect srcArea, unsigned color /*= 0xFFFFFFFF*/)
{
    drawRect(dstArea, srcArea, color);
}

void glSmartBitmap::draw(DrawPoint drawPt, unsigned color, unsigned player_color)
{
    drawPercent(drawPt, 100, color, player_color);
}

void glSmartBitmap::drawPercent(DrawPoint drawPt, unsigned percent, unsigned color, unsigned player_color)
{
    // nothing to draw?
    if(!percent)
        return;
    RTTR_Assert(percent <= 100);
    const float partDrawn = percent / 100.f;
    auto startY = int(std::round(size_.y * (1 - partDrawn)));
    auto height = size_.y - startY;
    Rect dstArea(drawPt.x, drawPt.y + startY, size_.x, height);
    Rect srcArea(0, startY, size_.x, height);
    drawRect(dstArea, srcArea, color, player_color);
}

void glSmartBitmap::drawRect(Rect dstArea, Rect srcArea, unsigned color /*= 0xFFFFFFFF*/, unsigned player_color /*= 0*/)
{
    ISpriteRenderer* renderer = getSpriteRenderer();
    if(!renderer || items.empty() || size_.x == 0 || size_.y == 0)
        return;
    dstArea.move(-origin_);
    renderer->drawSprite(*this, dstArea, srcArea, color, hasPlayer ? player_color : 0);
}

void glSmartBitmap::renderSource(libsiedler2::PixelBufferBGRA& buffer) const
{
    drawTo(buffer);
}
