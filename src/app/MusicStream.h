// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace weiler {

/// Streaming decoder for Ogg Vorbis files; delivers stereo float frames at Audio::RATE
class MusicStream
{
public:
    static std::unique_ptr<MusicStream> open(const std::string& path);
    ~MusicStream();
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;
    /// Read up to frames stereo frames; returns the number read (less at the end of the stream)
    int read(float* out, int frames);
    /// Length and position in seconds
    double duration() const { return duration_; }
    double position() const { return position_; }

private:
    MusicStream() = default;
    /// Decode the next block into src_; false at the end
    bool decode();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    int channels_ = 2, rate_ = 44100;
    double duration_ = 0, position_ = 0;
    /// Decoded frames (stereo) not yet consumed; frac = resampling position
    std::vector<float> src_;
    size_t srcPos_ = 0;
    double frac_ = 0;
};

} // namespace weiler
