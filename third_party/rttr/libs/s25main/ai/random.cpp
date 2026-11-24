// Copyright (C) 2005 - 2025 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "ai/random.h"

namespace AI {

std::minstd_rand& getRandomGenerator()
{
    // Weiler: seeded from the game seed when a game starts or is loaded (reproducible single player games)
    static std::minstd_rand rng;
    return rng;
}

} // namespace AI
