// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "drivers/AudioDriverWrapper.h"
#include "libsiedler2/ArchivItem_Sound.h"
#include <boost/interprocess/streams/vectorstream.hpp>

SoundHandle AudioDriverWrapper::load(const libsiedler2::ArchivItem_Sound& soundArchiv, const std::string& extension,
                                     SoundType type)
{
    if(!backend_)
        return {};
    boost::interprocess::basic_ovectorstream<std::vector<char>> tmp;
    if(soundArchiv.write(tmp) != 0)
        return {};
    auto data = backend_->loadSound(tmp.vector(), extension, type);
    if(!data)
        return {};
    return SoundHandle(std::move(data), type);
}

SoundHandle AudioDriverWrapper::loadEffect(const libsiedler2::ArchivItem_Sound& soundArchiv,
                                           const std::string& extension)
{
    return load(soundArchiv, extension, SoundType::Effect);
}

SoundHandle AudioDriverWrapper::loadMusic(const libsiedler2::ArchivItem_Sound& soundArchiv,
                                          const std::string& extension)
{
    return load(soundArchiv, extension, SoundType::Music);
}

EffectPlayId AudioDriverWrapper::playEffect(const SoundHandle& sound, uint8_t volume, bool loop)
{
    if(!backend_ || !sound)
        return EffectPlayId::Invalid;
    const int id = backend_->playEffect(sound.getData(), volume, loop);
    return id < 0 ? EffectPlayId::Invalid : static_cast<EffectPlayId>(id);
}

void AudioDriverWrapper::stopEffect(EffectPlayId& playId)
{
    if(backend_ && playId != EffectPlayId::Invalid)
        backend_->stopEffect(static_cast<int>(playId));
    playId = EffectPlayId::Invalid;
}

void AudioDriverWrapper::playMusic(const SoundHandle& sound, int repeats)
{
    if(backend_ && sound)
        backend_->playMusic(sound.getData(), repeats);
}

void AudioDriverWrapper::stopMusic()
{
    if(backend_)
        backend_->stopMusic();
}

bool AudioDriverWrapper::isEffectPlaying(EffectPlayId playId)
{
    return backend_ && playId != EffectPlayId::Invalid && backend_->isEffectPlaying(static_cast<int>(playId));
}

void AudioDriverWrapper::changeVolume(EffectPlayId playId, uint8_t volume)
{
    if(backend_ && playId != EffectPlayId::Invalid)
        backend_->changeVolume(static_cast<int>(playId), volume);
}
