// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "glArchivItem_Bitmap_Player.h"
#include "Loader.h"
#include "Point.h"
#include "libsiedler2/PixelBufferBGRA.h"

void glArchivItem_Bitmap_Player::drawFull(const Rect& destArea, unsigned color, unsigned player_color)
{
    draw(destArea, Rect(Position::all(0), getSize()), color, player_color);
}

void glArchivItem_Bitmap_Player::drawFull(const DrawPoint& dst, unsigned color, unsigned player_color)
{
    drawFull(Rect(dst, getSize()), color, player_color);
}

void glArchivItem_Bitmap_Player::drawFull(const Position& dstPos, unsigned color)
{
    drawFull(dstPos, color, COLOR_WHITE);
}

void glArchivItem_Bitmap_Player::drawForPlayer(const DrawPoint& dst, unsigned playerColor)
{
    drawFull(dst, COLOR_WHITE, playerColor);
}

void glArchivItem_Bitmap_Player::draw(Rect dstArea, Rect srcArea, unsigned color /*= COLOR_WHITE*/)
{
    draw(dstArea, srcArea, color, COLOR_WHITE);
}

void glArchivItem_Bitmap_Player::draw(Rect dstArea, Rect srcArea, unsigned color, unsigned player_color)
{
    drawImpl(dstArea, srcArea, color, player_color);
}

void glArchivItem_Bitmap_Player::renderSource(libsiedler2::PixelBufferBGRA& buffer) const
{
    // Special palette (blue player colors are gray) so the player color can be applied by the renderer
    const libsiedler2::ArchivItem_Palette* palette = LOADER.getPaletteN("colors");
    print(buffer, palette, 128, 0, 0, 0, 0, 0, 0, false);
    print(buffer, palette, 128, getWidth(), 0, 0, 0, 0, 0, true);
}
