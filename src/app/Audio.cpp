// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Audio.h"
#include "MusicStream.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace weiler {

namespace {
constexpr float PI2 = 6.28318531f;

void writeLE(FILE* f, uint32_t v, int bytes)
{
    for(int i = 0; i < bytes; ++i)
        std::fputc(static_cast<int>((v >> (8 * i)) & 0xFF), f);
}

void writeWavHeader(FILE* f, size_t frames)
{
    const uint32_t data = static_cast<uint32_t>(frames * 4);
    std::fseek(f, 0, SEEK_SET);
    std::fwrite("RIFF", 1, 4, f);
    writeLE(f, 36 + data, 4);
    std::fwrite("WAVEfmt ", 1, 8, f);
    writeLE(f, 16, 4);
    writeLE(f, 1, 2); // PCM
    writeLE(f, 2, 2);
    writeLE(f, Audio::RATE, 4);
    writeLE(f, Audio::RATE * 4, 4);
    writeLE(f, 4, 2);
    writeLE(f, 16, 2);
    std::fwrite("data", 1, 4, f);
    writeLE(f, data, 4);
    std::fseek(f, 0, SEEK_END);
}

/// Soft limiter: linear up to 0.8, then smoothly towards 1
float soft(float x)
{
    const float a = std::abs(x);
    if(a <= 0.8f)
        return x;
    const float y = 0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f);
    return x < 0 ? -y : y;
}
} // namespace

Audio::Audio()
{
    makeUiSounds();
}

Audio::~Audio()
{
    AUDIODRIVER.setBackend(nullptr);
    if(stream_)
        SDL_DestroyAudioStream(stream_);
    if(wav_)
    {
        writeWavHeader(wav_, wavFrames_);
        std::fclose(wav_);
    }
}

bool Audio::init()
{
    spec_.format = SDL_AUDIO_F32;
    spec_.channels = 2;
    spec_.freq = RATE;
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

bool Audio::initFile(const std::string& path)
{
    spec_.format = SDL_AUDIO_F32;
    spec_.channels = 2;
    spec_.freq = RATE;
    wav_ = std::fopen(path.c_str(), "wb");
    if(!wav_)
        return false;
    writeWavHeader(wav_, 0);
    AUDIODRIVER.setBackend(this);
    return true;
}

void Audio::setSuspended(bool suspended)
{
    if(!stream_)
        return;
    if(suspended)
        SDL_PauseAudioStreamDevice(stream_);
    else
        SDL_ResumeAudioStreamDevice(stream_);
}

void Audio::pump(double seconds)
{
    if(!wav_)
        return;
    wavCarry_ += seconds * RATE;
    const int frames = static_cast<int>(wavCarry_);
    wavCarry_ -= frames;
    if(frames <= 0)
        return;
    mixBuf_.assign(static_cast<size_t>(frames) * 2, 0.f);
    mix(mixBuf_.data(), frames);
    std::vector<int16_t> pcm(mixBuf_.size());
    for(size_t i = 0; i < pcm.size(); ++i)
        pcm[i] = static_cast<int16_t>(std::lround(std::clamp(mixBuf_[i], -1.f, 1.f) * 32767.f));
    std::fwrite(pcm.data(), sizeof(int16_t), pcm.size(), wav_);
    wavFrames_ += static_cast<size_t>(frames);
}

std::shared_ptr<void> Audio::loadSound(const std::vector<char>& data, const std::string& extension, SoundType type)
{
    if(type != SoundType::Effect || extension != ".wav" || !active())
        return nullptr; // The original music is not used
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

int Audio::addVoice(std::shared_ptr<Sound> snd, float volume, bool loop, Bus bus)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if(voices_.size() >= 32)
        voices_.erase(voices_.begin());
    Voice v;
    v.id = nextId_++;
    if(nextId_ > 1000000)
        nextId_ = 1;
    v.sound = std::move(snd);
    v.volume = volume;
    v.loop = loop;
    v.bus = bus;
    voices_.push_back(std::move(v));
    return voices_.back().id;
}

int Audio::playEffect(const std::shared_ptr<void>& sound, uint8_t volume, bool loop)
{
    if(!sound)
        return -1;
    return addVoice(std::static_pointer_cast<Sound>(sound), volume / 255.f, loop, Bus::Effects);
}

void Audio::playUi(UiSound s, float gain)
{
    if(!active())
        return;
    addVoice(ui_[static_cast<int>(s)], gain, false, Bus::Ui);
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

void Audio::setVolume(Bus bus, float v)
{
    std::lock_guard<std::mutex> lock(mutex_);
    volumes_[static_cast<int>(bus)] = std::clamp(v, 0.f, 1.f);
}

void Audio::stopAll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.bus == Bus::Effects; }),
                  voices_.end());
}

int Audio::startMusic(std::unique_ptr<MusicStream> stream, float fadeIn)
{
    if(!stream)
        return -1;
    std::lock_guard<std::mutex> lock(mutex_);
    Deck d;
    d.id = nextDeck_++;
    d.stream = std::move(stream);
    d.gain = fadeIn > 0 ? 0.f : 1.f;
    d.target = 1;
    d.step = fadeIn > 0 ? 1.f / (fadeIn * RATE) : 1.f;
    decks_.push_back(std::move(d));
    return decks_.back().id;
}

void Audio::fadeOutMusic(int deck, float seconds)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(Deck& d : decks_)
    {
        if(d.id == deck || deck < 0)
        {
            d.target = 0;
            d.step = seconds > 0 ? 1.f / (seconds * RATE) : 1.f;
        }
    }
}

double Audio::musicRemaining(int deck)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for(const Deck& d : decks_)
    {
        if(d.id == deck)
            return d.ended ? 0.0 : d.stream->duration() - d.stream->position();
    }
    return -1;
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
    // Volume changes are smoothed per buffer (no clicks when a slider moves)
    for(int b = 0; b < static_cast<int>(Bus::Count); ++b)
        smoothVol_[b] += (volumes_[b] - smoothVol_[b]) * 0.5f;
    for(Voice& v : voices_)
    {
        const auto& s = v.sound->samples;
        // Perceived loudness: square law
        const float bv = smoothVol_[static_cast<int>(v.bus)];
        // The original effects are loud and sharp next to the music: -3 dB trim
        const float trim = v.bus == Bus::Effects ? 0.7f : 1.f;
        const float vol = v.volume * bv * bv * trim;
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
    // Music decks with fades
    const float mv = smoothVol_[static_cast<int>(Bus::Music)];
    const float musicGain = mv * mv;
    deckBuf_.resize(static_cast<size_t>(frames) * 2);
    for(Deck& d : decks_)
    {
        const int got = d.ended ? 0 : d.stream->read(deckBuf_.data(), frames);
        if(got < frames)
            d.ended = true;
        for(int i = 0; i < got; ++i)
        {
            if(d.gain < d.target)
                d.gain = std::min(d.target, d.gain + d.step);
            else if(d.gain > d.target)
                d.gain = std::max(d.target, d.gain - d.step);
            // Equal power curve
            const float g = std::sin(d.gain * 1.5707963f) * musicGain;
            out[i * 2] += deckBuf_[static_cast<size_t>(i) * 2] * g;
            out[i * 2 + 1] += deckBuf_[static_cast<size_t>(i) * 2 + 1] * g;
        }
    }
    decks_.erase(std::remove_if(decks_.begin(), decks_.end(),
                                [](const Deck& d) { return d.ended || (d.target <= 0 && d.gain <= 0); }),
                 decks_.end());
    for(int i = 0; i < frames * 2; ++i)
        out[i] = soft(out[i]);
}

void Audio::makeUiSounds()
{
    // Small synthesizer: every sound is a sum of decaying partials plus optional filtered noise
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-1.f, 1.f);
    auto make = [&](float dur, auto&& fn) {
        auto snd = std::make_shared<Sound>();
        const int n = static_cast<int>(dur * RATE);
        snd->samples.resize(static_cast<size_t>(n) * 2);
        float lp = 0;
        for(int i = 0; i < n; ++i)
        {
            const float t = static_cast<float>(i) / RATE;
            const float v = fn(t, noise(rng), lp);
            // Short fade at the end (no click)
            const float tail = std::min(1.f, (dur - t) / 0.005f);
            snd->samples[static_cast<size_t>(i) * 2] = v * tail;
            snd->samples[static_cast<size_t>(i) * 2 + 1] = v * tail;
        }
        return snd;
    };
    auto tick = [](float t, float f, float decay) { return std::sin(PI2 * f * t) * std::exp(-t / decay); };
    // Tap: soft wooden tick
    ui_[static_cast<int>(UiSound::Tap)] = make(0.09f, [&](float t, float n, float& lp) {
        lp += (n - lp) * 0.35f;
        return 0.16f * tick(t, 1180, 0.016f) + 0.05f * tick(t, 2360, 0.007f) + 0.05f * lp * std::exp(-t / 0.002f);
    });
    // Toggle: two ticks rising
    ui_[static_cast<int>(UiSound::Toggle)] = make(0.14f, [&](float t, float, float&) {
        float v = 0.13f * tick(t, 990, 0.014f);
        if(t > 0.045f)
            v += 0.13f * tick(t - 0.045f, 1320, 0.016f);
        return v;
    });
    // Open: airy rising sweep
    ui_[static_cast<int>(UiSound::Open)] = make(0.22f, [&](float t, float n, float& lp) {
        const float k = 0.04f + 0.25f * (t / 0.22f);
        lp += (n - lp) * k;
        const float env = std::min(1.f, t / 0.04f) * std::exp(-t / 0.08f);
        return 0.10f * lp * env + 0.035f * std::sin(PI2 * (520 + 400 * t) * t) * env;
    });
    // Close: falling sweep
    ui_[static_cast<int>(UiSound::Close)] = make(0.2f, [&](float t, float n, float& lp) {
        const float k = 0.28f - 0.24f * (t / 0.2f);
        lp += (n - lp) * k;
        const float env = std::min(1.f, t / 0.03f) * std::exp(-t / 0.07f);
        return 0.09f * lp * env + 0.03f * std::sin(PI2 * (700 - 500 * t) * t) * env;
    });
    // Build: warm wooden knock
    ui_[static_cast<int>(UiSound::Build)] = make(0.35f, [&](float t, float n, float& lp) {
        lp += (n - lp) * 0.2f;
        return 0.22f * tick(t, 196, 0.09f) + 0.1f * tick(t, 392, 0.05f) + 0.05f * tick(t, 588, 0.03f)
               + 0.08f * lp * std::exp(-t / 0.006f);
    });
    // Notify: two soft bell tones
    ui_[static_cast<int>(UiSound::Notify)] = make(1.1f, [&](float t, float, float&) {
        auto bell = [&](float tt, float f) {
            if(tt < 0)
                return 0.f;
            const float att = std::min(1.f, tt / 0.004f);
            return att
                   * (std::sin(PI2 * f * tt) * std::exp(-tt / 0.45f) + 0.35f * std::sin(PI2 * f * 2.01f * tt) * std::exp(-tt / 0.2f)
                      + 0.15f * std::sin(PI2 * f * 2.76f * tt) * std::exp(-tt / 0.12f));
        };
        return 0.08f * bell(t, 880) + 0.07f * bell(t - 0.13f, 1318.5f);
    });
    // Error: two low muted ticks
    ui_[static_cast<int>(UiSound::Error)] = make(0.2f, [&](float t, float, float&) {
        float v = 0.15f * tick(t, 330, 0.03f);
        if(t > 0.09f)
            v += 0.15f * tick(t - 0.09f, 294, 0.035f);
        return v;
    });
}

} // namespace weiler
