// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ViewState.h"
#include "GameEvent.h"
#include "helpers/mathFuncs.h"
#include "gameData/GameConsts.h"
#include <boost/format.hpp>

unsigned ViewState::getTickCount() const
{
    static const auto start = Clock::now();
    return static_cast<unsigned>(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
}

unsigned ViewState::getGlobalAnimation(const unsigned short max, const unsigned char factor_numerator,
                                       const unsigned char factor_denumerator, const unsigned offset) const
{
    // Unit for animations is 630ms. An animation runs fully in (num / denom) multiples of 630ms
    const unsigned unit = 630 /*ms*/ * factor_numerator / factor_denumerator;
    const auto currenttime = static_cast<unsigned>(
      std::chrono::duration_cast<std::chrono::milliseconds>((lastTime + frameTime).time_since_epoch()).count());
    return ((currenttime % unit) * max / unit + offset) % max;
}

template<typename T>
static T doInterpolate(const ViewState& vs, const T x1, const T x2, const GameEvent& ev)
{
    const std::chrono::milliseconds elapsedTime = (vs.currentGF - ev.startGF) * vs.gfLength + vs.frameTime;
    const auto duration = ev.length * vs.gfLength;
    return helpers::interpolate(x1, x2, elapsedTime, duration);
}

unsigned ViewState::interpolate(const unsigned maxVal, const GameEvent* ev) const
{
    return doInterpolate(*this, 0u, maxVal, *ev);
}

int ViewState::interpolate(const int x1, const int x2, const GameEvent* ev) const
{
    return doInterpolate(*this, x1, x2, *ev);
}

std::string ViewState::formatGFTime(const unsigned gf) const
{
    const auto totalSec = static_cast<unsigned>(
      std::chrono::duration_cast<std::chrono::seconds>(gf * SPEED_GF_LENGTHS[GameSpeed::Normal]).count());
    const unsigned h = totalSec / 3600, m = (totalSec / 60) % 60, s = totalSec % 60;
    if(h)
        return (boost::format("%02u:%02u:%02u") % h % m % s).str();
    return (boost::format("%02u:%02u") % m % s).str();
}
