// Copyright (C) 2005 - 2021 Settlers Freaks <sf-team at siedler25.org>
// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "convertSounds.h"
#include "helpers/mathFuncs.h"
#include <libsiedler2/Archiv.h>
#include <libsiedler2/ArchivItem_Sound_Wave.h>
#include <libsiedler2/loadMapping.h>
#include <s25util/StringConversion.h>
#include <algorithm>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <vector>

// Weiler: instead of resampling (libsamplerate) only the sample rate in the header is corrected.
// The audio backend resamples to the output rate.
void convertSounds(libsiedler2::Archiv& sounds, const boost::filesystem::path& scriptPath)
{
    libsiedler2::loadMapping(scriptPath, [&sounds](unsigned idx, const std::string& sFrequency) {
        const auto frequency = s25util::fromStringClassic<unsigned>(sFrequency);
        auto* sound = dynamic_cast<libsiedler2::ArchivItem_Sound_Wave*>(sounds[idx]);
        if(!sound)
            throw std::runtime_error("No wave sound at index " + std::to_string(idx));
        auto header = sound->getHeader();
        if(header.numChannels != 1)
            throw std::runtime_error("Unexpected number of channels for item " + std::to_string(idx));
        header.samplesPerSec = frequency;
        header.bytesPerSec = header.samplesPerSec * header.frameSize;
        sound->setHeader(header);
    });
}
