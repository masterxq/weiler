// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Music.h"
#include "Audio.h"
#include "MusicStream.h"
#include <algorithm>
#include <iostream>

namespace weiler {

Music::Music(Audio& audio) : audio_(audio), rng_(std::random_device{}()) {}

void Music::setTracks(Context c, std::vector<std::string> files)
{
    std::sort(files.begin(), files.end());
    lists_[c] = std::move(files);
    if(c == ctx_)
        order_.clear();
}

void Music::play(Context c)
{
    if(c == ctx_)
        return;
    if(deck_ >= 0)
        audio_.fadeOutMusic(deck_, 2.5f);
    deck_ = -1;
    current_.clear();
    ctx_ = c;
    order_.clear();
    // The new piece fades in while the old one fades out
    gap_ = 0.4;
}

void Music::skip()
{
    if(deck_ >= 0)
        audio_.fadeOutMusic(deck_, 1.5f);
    deck_ = -1;
    current_.clear();
    gap_ = 1.2;
}

void Music::startNext(float fadeIn)
{
    const auto& list = lists_[ctx_];
    if(list.empty())
        return;
    if(order_.empty() || pos_ >= order_.size())
    {
        order_.resize(list.size());
        for(size_t i = 0; i < list.size(); ++i)
            order_[i] = i;
        std::shuffle(order_.begin(), order_.end(), rng_);
        // Never the same piece twice in a row
        if(order_.size() > 1 && list[order_.front()] == last_)
            std::swap(order_.front(), order_.back());
        pos_ = 0;
    }
    const std::string& file = list[order_[pos_++]];
    auto stream = MusicStream::open(file);
    if(!stream)
    {
        std::cerr << "Music: cannot open " << file << std::endl;
        gap_ = 5;
        return;
    }
    deck_ = audio_.startMusic(std::move(stream), fadeIn);
    current_ = last_ = file;
}

void Music::update(double dt)
{
    if(ctx_ == Context::None || !audio_.active())
        return;
    if(deck_ >= 0)
    {
        const double left = audio_.musicRemaining(deck_);
        if(left > 0)
            return;
        // Piece over: a quiet pause before the next one (longer in the game)
        deck_ = -1;
        current_.clear();
        std::uniform_real_distribution<double> d(ctx_ == Context::Game ? 8.0 : 2.0, ctx_ == Context::Game ? 25.0 : 4.0);
        gap_ = d(rng_);
        return;
    }
    gap_ -= dt;
    if(gap_ <= 0)
        startNext(ctx_ == Context::Menu ? 3.f : 2.5f);
}

} // namespace weiler
