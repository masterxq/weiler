// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "drivers/AudioDriverWrapper.h"
#include <SDL3/SDL_audio.h>
#include <mutex>
#include <vector>

namespace weiler {

/// Sound effect mixer on top of SDL audio streams. Implements the audio backend of the game core.
class Audio : public IAudioBackend
{
public:
    Audio();
    ~Audio() override;
    bool init();

    std::shared_ptr<void> loadSound(const std::vector<char>& data, const std::string& extension,
                                    SoundType type) override;
    int playEffect(const std::shared_ptr<void>& sound, uint8_t volume, bool loop) override;
    void stopEffect(int playId) override;
    bool isEffectPlaying(int playId) override;
    void changeVolume(int playId, uint8_t volume) override;
    void playMusic(const std::shared_ptr<void>& sound, int repeats) override;
    void stopMusic() override;

    /// 0..1
    void setEffectVolume(float v) { effectVolume_ = v; }
    float effectVolume() const { return effectVolume_; }
    void setMusicVolume(float v) { musicVolume_ = v; }
    float musicVolume() const { return musicVolume_; }
    void stopAll();

private:
    struct Sound
    {
        std::vector<float> samples; // stereo interleaved at the output rate
    };
    struct Voice
    {
        int id;
        std::shared_ptr<Sound> sound;
        size_t pos = 0;
        float volume = 1;
        bool loop = false;
    };
    static void SDLCALL callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);

    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioSpec spec_{};
    std::mutex mutex_;
    std::vector<Voice> voices_;
    std::vector<float> mixBuf_;
    int nextId_ = 1;
    float effectVolume_ = 0.8f, musicVolume_ = 0.6f;
};

} // namespace weiler
