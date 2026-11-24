// Copyright (C) 2005 - 2021 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MusicItem.h"
#include "drivers/AudioDriverWrapper.h"

void MusicItem::play(int repeats)
{
    AUDIODRIVER.playMusic(getSoundHandle(), repeats);
}
