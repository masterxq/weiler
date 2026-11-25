// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "notifications/notifications.h"
#include "gameTypes/MapCoordinates.h"

class noFigure;

/// Weiler (diagnosis): a figure that is no soldier stepped into the territory of another player
struct FigureNote
{
    ENABLE_NOTIFICATION(FigureNote);

    FigureNote(const noFigure& figure, unsigned player, const MapPoint& pos, unsigned territoryOwner)
        : figure(figure), player(player), pos(pos), territoryOwner(territoryOwner)
    {}

    const noFigure& figure;
    /// Owner of the figure
    const unsigned player;
    const MapPoint pos;
    /// Player whose territory was entered
    const unsigned territoryOwner;
};
