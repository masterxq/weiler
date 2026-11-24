// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "drivers/AudioDriverWrapper.h"
#include <SDL3/SDL_audio.h>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace weiler {

class MusicStream;

/// Mixer channels with own volume
enum class Bus
{
    Effects,
    Ui,
    Music,
    Count
};

/// Short sounds of the user interface (synthesized at start, no files)
enum class UiSound
{
    Tap,
    Toggle,
    Open,
    Close,
    Build,
    Notify,
    Error,
    Count
};

/// Audio mixer on top of an SDL audio stream: sound effects of the game core, interface sounds and streamed music
/// with cross fades. Implements the audio backend of the game core. In test mode the mix can be written into a WAV
/// file instead of a device (pump() advances it by the frame time).
class Audio : public IAudioBackend
{
public:
    static constexpr int RATE = 44100;
    Audio();
    ~Audio() override;
    /// Open the default playback device
    bool init();
    /// Mix into a 16 bit stereo WAV file instead of a device (tests)
    bool initFile(const std::string& path);
    /// File mode: mix the given amount of time
    void pump(double seconds);
    bool active() const { return stream_ != nullptr || wav_ != nullptr; }
    /// Stop the device while the app is in the background (no mixing, no wake ups)
    void setSuspended(bool suspended);

    std::shared_ptr<void> loadSound(const std::vector<char>& data, const std::string& extension,
                                    SoundType type) override;
    int playEffect(const std::shared_ptr<void>& sound, uint8_t volume, bool loop) override;
    void stopEffect(int playId) override;
    bool isEffectPlaying(int playId) override;
    void changeVolume(int playId, uint8_t volume) override;
    /// The original music (XMIDI) is not played; Weiler has its own music (see Music)
    void playMusic(const std::shared_ptr<void>& sound, int repeats) override;
    void stopMusic() override;

    /// Volume of a bus, 0..1
    void setVolume(Bus bus, float v);
    float volume(Bus bus) const { return volumes_[static_cast<int>(bus)]; }
    void playUi(UiSound s, float gain = 1.f);
    void stopAll();

    // ---- Music decks (used by Music)
    /// Start a stream, fading in over fadeIn seconds; returns the deck id
    int startMusic(std::unique_ptr<MusicStream> stream, float fadeIn);
    /// Fade a deck out and drop it afterwards
    void fadeOutMusic(int deck, float seconds);
    /// Seconds left in the stream of a deck; < 0 if the deck is gone
    double musicRemaining(int deck);

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
        Bus bus = Bus::Effects;
    };
    struct Deck
    {
        int id;
        std::unique_ptr<MusicStream> stream;
        float gain = 0, target = 1, step = 0;
        bool ended = false;
    };
    static void SDLCALL callback(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void mix(float* out, int frames);
    void makeUiSounds();
    int addVoice(std::shared_ptr<Sound> snd, float volume, bool loop, Bus bus);

    SDL_AudioStream* stream_ = nullptr;
    SDL_AudioSpec spec_{};
    std::mutex mutex_;
    std::vector<Voice> voices_;
    std::vector<Deck> decks_;
    std::vector<float> mixBuf_, deckBuf_;
    int nextId_ = 1, nextDeck_ = 1;
    float volumes_[static_cast<int>(Bus::Count)] = {0.8f, 0.6f, 0.6f};
    float smoothVol_[static_cast<int>(Bus::Count)] = {0.8f, 0.6f, 0.6f};
    std::shared_ptr<Sound> ui_[static_cast<int>(UiSound::Count)];
    // WAV output (tests)
    FILE* wav_ = nullptr;
    size_t wavFrames_ = 0;
    double wavCarry_ = 0;
};

} // namespace weiler
