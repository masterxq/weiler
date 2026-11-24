// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "MusicStream.h"
#include "Audio.h"
#include <vorbis/vorbisfile.h>
#include <algorithm>

namespace weiler {

struct MusicStream::Impl
{
    OggVorbis_File vf{};
    bool open = false;
};

std::unique_ptr<MusicStream> MusicStream::open(const std::string& path)
{
    std::unique_ptr<MusicStream> s(new MusicStream());
    s->impl_ = std::make_unique<Impl>();
    if(ov_fopen(path.c_str(), &s->impl_->vf) != 0)
        return nullptr;
    s->impl_->open = true;
    const vorbis_info* vi = ov_info(&s->impl_->vf, -1);
    if(!vi || vi->channels < 1)
        return nullptr;
    s->channels_ = vi->channels;
    s->rate_ = static_cast<int>(vi->rate);
    s->duration_ = ov_time_total(&s->impl_->vf, -1);
    return s;
}

MusicStream::~MusicStream()
{
    if(impl_ && impl_->open)
        ov_clear(&impl_->vf);
}

bool MusicStream::decode()
{
    // Keep the last frame for interpolation
    std::vector<float> keep;
    if(src_.size() >= 2)
        keep.assign(src_.end() - 2, src_.end());
    src_.swap(keep);
    srcPos_ = 0;
    float** pcm = nullptr;
    int section = 0;
    const long n = ov_read_float(&impl_->vf, &pcm, 4096, &section);
    if(n <= 0)
        return false;
    const size_t base = src_.size();
    src_.resize(base + static_cast<size_t>(n) * 2);
    for(long i = 0; i < n; ++i)
    {
        const float l = pcm[0][i];
        const float r = channels_ > 1 ? pcm[1][i] : l;
        src_[base + static_cast<size_t>(i) * 2] = l;
        src_[base + static_cast<size_t>(i) * 2 + 1] = r;
    }
    return true;
}

int MusicStream::read(float* out, int frames)
{
    const double step = static_cast<double>(rate_) / Audio::RATE;
    int done = 0;
    while(done < frames)
    {
        // Need two source frames (srcPos_ and srcPos_ + 1) for linear interpolation
        if(srcPos_ + 3 >= src_.size())
        {
            if(!decode())
                break;
            continue;
        }
        const float f = static_cast<float>(frac_);
        out[done * 2] = src_[srcPos_] + (src_[srcPos_ + 2] - src_[srcPos_]) * f;
        out[done * 2 + 1] = src_[srcPos_ + 1] + (src_[srcPos_ + 3] - src_[srcPos_ + 1]) * f;
        ++done;
        frac_ += step;
        while(frac_ >= 1.0)
        {
            frac_ -= 1.0;
            srcPos_ += 2;
        }
    }
    position_ += static_cast<double>(done) / Audio::RATE;
    return done;
}

} // namespace weiler
