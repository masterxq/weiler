// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <map>
#include <random>
#include <string>
#include <vector>

namespace weiler {

class Audio;

/// Music playlists: menu theme and game pieces (shuffled, with quiet pauses between the pieces so that long
/// sessions do not tire). Switching the context cross fades.
class Music
{
public:
    enum class Context
    {
        None,
        Menu,
        Game
    };
    explicit Music(Audio& audio);
    void setTracks(Context c, std::vector<std::string> files);
    const std::vector<std::string>& tracks(Context c) { return lists_[c]; }
    /// Switch to the playlist of a context (cross fade)
    void play(Context c);
    /// Main thread, every frame
    void update(double dt);
    /// Fade out the current piece and continue with the next one
    void skip();
    /// File name of the piece playing (empty if none)
    const std::string& current() const { return current_; }

private:
    void startNext(float fadeIn);
    Audio& audio_;
    Context ctx_ = Context::None;
    std::map<Context, std::vector<std::string>> lists_;
    std::vector<size_t> order_;
    size_t pos_ = 0;
    int deck_ = -1;
    double gap_ = 0;
    std::string current_, last_;
    std::mt19937 rng_;
};

} // namespace weiler
