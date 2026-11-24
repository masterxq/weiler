// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Weiler: replaces the RTTR audio driver layer by a narrow backend interface implemented by the frontend.

#include "drivers/SoundHandle.h"
#include "s25util/Singleton.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace libsiedler2 {
class ArchivItem_Sound;
}

enum class EffectPlayId : int32_t
{
    Invalid = -1
};

/// Implemented by the frontend (e.g. on top of SDL audio)
class IAudioBackend
{
public:
    virtual ~IAudioBackend() = default;
    /// Load a sound from file data (format given by extension, e.g. ".wav", ".mid"). Returns nullptr on failure.
    virtual std::shared_ptr<void> loadSound(const std::vector<char>& data, const std::string& extension,
                                            SoundType type) = 0;
    virtual int playEffect(const std::shared_ptr<void>& sound, uint8_t volume, bool loop) = 0;
    virtual void stopEffect(int playId) = 0;
    virtual bool isEffectPlaying(int playId) = 0;
    virtual void changeVolume(int playId, uint8_t volume) = 0;
    virtual void playMusic(const std::shared_ptr<void>& sound, int repeats) = 0;
    virtual void stopMusic() = 0;
};

class AudioDriverWrapper : public Singleton<AudioDriverWrapper>
{
public:
    /// Effects (and ambient bird sounds) can be switched off by the frontend
    bool effectsEnabled = true;
    bool birdsEnabled = true;

    void setBackend(IAudioBackend* backend) { backend_ = backend; }
    IAudioBackend* getBackend() const { return backend_; }

    SoundHandle loadEffect(const libsiedler2::ArchivItem_Sound& soundArchiv, const std::string& extension);
    SoundHandle loadMusic(const libsiedler2::ArchivItem_Sound& soundArchiv, const std::string& extension);
    EffectPlayId playEffect(const SoundHandle& sound, uint8_t volume, bool loop);
    /// Stops a sound and resets the id to "Invalid"
    void stopEffect(EffectPlayId& playId);
    void playMusic(const SoundHandle& sound, int repeats);
    void stopMusic();
    bool isEffectPlaying(EffectPlayId playId);
    void changeVolume(EffectPlayId playId, uint8_t volume);

private:
    SoundHandle load(const libsiedler2::ArchivItem_Sound& soundArchiv, const std::string& extension, SoundType type);
    IAudioBackend* backend_ = nullptr;
};

#define AUDIODRIVER AudioDriverWrapper::inst()
