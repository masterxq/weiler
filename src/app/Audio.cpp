// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Audio.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace weiler {

Audio::Audio() = default;

Audio::~Audio()
{
    AUDIODRIVER.setBackend(nullptr);
    if(stream_)
        SDL_DestroyAudioStream(stream_);
}

bool Audio::init()
{
    spec_.format = SDL_AUDIO_F32;
    spec_.channels = 2;
    spec_.freq = 44100;
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec_, &Audio::callback, this);
    if(!stream_)
    {
        SDL_Log("No audio: %s", SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(stream_);
    AUDIODRIVER.setBackend(this);
    return true;
}

std::shared_ptr<void> Audio::loadSound(const std::vector<char>& data, const std::string& extension, SoundType type)
{
    if(type != SoundType::Effect || extension != ".wav" || !stream_)
        return nullptr; // Music is played by the frontend itself
    SDL_IOStream* io = SDL_IOFromConstMem(data.data(), data.size());
    SDL_AudioSpec srcSpec;
    Uint8* buf = nullptr;
    Uint32 len = 0;
    if(!SDL_LoadWAV_IO(io, true, &srcSpec, &buf, &len))
        return nullptr;
    Uint8* dst = nullptr;
    int dstLen = 0;
    const bool ok = SDL_ConvertAudioSamples(&srcSpec, buf, static_cast<int>(len), &spec_, &dst, &dstLen);
    SDL_free(buf);
    if(!ok)
        return nullptr;
    auto snd = std::make_shared<Sound>();
    snd->samples.assign(reinterpret_cast<float*>(dst), reinterpret_cast<float*>(dst) + dstLen / sizeof(float));
    SDL_free(dst);
    return snd;
}

int Audio::playEffect(const std::shared_ptr<void>& sound, uint8_t volume, bool loop)
{
    if(!sound)
        return -1;
    std::lock_guard<std::mutex> lock(mutex_);
    if(voices_.size() >= 32)
        voices_.erase(voices_.begin());
    Voice v;
    v.id = nextId_++;
    if(nextId_ > 1000000)
        nextId_ = 1;
    v.sound = std::static_pointer_cast<Sound>(sound);
    v.volume = volume / 255.f;
    v.loop = loop;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

void Audio::stopEffect(int playId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [playId](const Voice& v) { return v.id == playId; }),
                  voices_.end());
}

bool Audio::isEffectPlaying(int playId)
{
    std::lock_guard<std::mutex> lock(mutex_);
    return std::any_of(voices_.begin(), voices_.end(), [playId](const Voice& v) { return v.id == playId; });
}

void Audio::changeVolume(int playId, uint8_t volume)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(Voice& v : voices_)
    {
        if(v.id == playId)
            v.volume = volume / 255.f;
    }
}

void Audio::playMusic(const std::shared_ptr<void>&, int) {}

void Audio::stopMusic() {}

void Audio::stopAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.clear();
}

void SDLCALL Audio::callback(void* userdata, SDL_AudioStream* stream, int additional, int /*total*/)
{
    auto* self = static_cast<Audio*>(userdata);
    const int frames = additional / static_cast<int>(sizeof(float) * 2);
    if(frames <= 0)
        return;
    self->mixBuf_.assign(static_cast<size_t>(frames) * 2, 0.f);
    self->mix(self->mixBuf_.data(), frames);
    SDL_PutAudioStreamData(stream, self->mixBuf_.data(), frames * 2 * static_cast<int>(sizeof(float)));
}

void Audio::mix(float* out, int frames)
{
    std::lock_guard<std::mutex> lock(mutex_);
    const float master = effectVolume_;
    for(Voice& v : voices_)
    {
        const auto& s = v.sound->samples;
        const float vol = v.volume * master;
        for(int i = 0; i < frames && !s.empty(); ++i)
        {
            if(v.pos + 1 >= s.size())
            {
                if(!v.loop)
                    break;
                v.pos = 0;
            }
            out[i * 2] += s[v.pos] * vol;
            out[i * 2 + 1] += s[v.pos + 1] * vol;
            v.pos += 2;
        }
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(),
                                 [](const Voice& v) { return !v.loop && v.pos + 1 >= v.sound->samples.size(); }),
                  voices_.end());
    for(int i = 0; i < frames * 2; ++i)
        out[i] = std::clamp(out[i], -1.f, 1.f);
}

} // namespace weiler
