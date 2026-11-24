// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/filesystem/path.hpp>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace weiler {

/// Reads the audio tracks of the user's original CD image (CloneCD .ccd/.img or .cue/.bin) and stores them as
/// Ogg Vorbis files in the user folder (music/cd/trackNN.ogg). Runs in a background thread. Nothing of it is part
/// of Weiler itself.
class CdImport
{
public:
    ~CdImport();
    /// Look for a CD image near the game data (the folder, its parent and grandparent and their direct sub folders)
    static boost::filesystem::path findImage(const boost::filesystem::path& gameData);
    /// Start the import; false if the image has no audio tracks
    bool start(const boost::filesystem::path& image, const boost::filesystem::path& outDir);
    bool running() const { return running_; }
    /// 0..1
    float progress() const { return progress_; }
    /// Number of tracks written by the last finished import (once; then 0)
    int takeWritten() { return running_ ? 0 : written_.exchange(0); }
    const std::string& error() const { return error_; }

private:
    struct Track
    {
        boost::filesystem::path file;
        uint64_t begin, end; // byte offsets in file
    };
    static std::vector<Track> parse(const boost::filesystem::path& image);
    void run(std::vector<Track> tracks, boost::filesystem::path outDir);
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<float> progress_{0};
    std::atomic<int> written_{0};
    std::string error_;
};

} // namespace weiler
