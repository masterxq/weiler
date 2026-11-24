// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Perf.h"
#include "gfx/Gl.h"
#include <algorithm>
#if defined(__GLIBC__)
#include <malloc.h>
#endif
#include <array>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>

namespace weiler::perf {

namespace {
bool gActive = false;
std::array<double, static_cast<size_t>(Phase::Count)> gPhase{};
unsigned gDraws = 0, gTerrainRebuilds = 0;
std::vector<double> gGameFrames;
const auto gStart = std::chrono::steady_clock::now();

double percentile(std::vector<double> v, double p)
{
    if(v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    const size_t i = std::min(v.size() - 1, static_cast<size_t>(p * static_cast<double>(v.size() - 1) + 0.5));
    return v[i];
}
double mean(const std::vector<double>& v)
{
    return v.empty() ? 0 : std::accumulate(v.begin(), v.end(), 0.0) / static_cast<double>(v.size());
}
} // namespace

bool active()
{
    return gActive;
}

void add(Phase phase, double ms)
{
    gPhase[static_cast<size_t>(phase)] += ms;
}

void gameFrame(double ms)
{
    if(gActive)
        gGameFrames.push_back(ms);
}

void drawCall()
{
    ++gDraws;
}

void terrainRebuild()
{
    ++gTerrainRebuilds;
}

double sinceStart()
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - gStart).count();
}

double heapMiB()
{
#if defined(__GLIBC__)
    return static_cast<double>(mallinfo2().uordblks) / (1024.0 * 1024.0);
#else
    return 0;
#endif
}

void memoryMiB(double& rss, double& peak)
{
    rss = peak = 0;
    std::ifstream f("/proc/self/status");
    std::string line;
    while(std::getline(f, line))
    {
        long kb = 0;
        if(std::sscanf(line.c_str(), "VmRSS: %ld kB", &kb) == 1)
            rss = static_cast<double>(kb) / 1024.0;
        else if(std::sscanf(line.c_str(), "VmHWM: %ld kB", &kb) == 1)
            peak = static_cast<double>(kb) / 1024.0;
    }
}

void Recorder::start(int frames, std::string label)
{
    left_ = frames;
    label_ = std::move(label);
    samples_.clear();
    samples_.reserve(static_cast<size_t>(frames));
    gGameFrames.clear();
    gPhase.fill(0);
    gDraws = 0;
    gTerrainRebuilds = 0;
    gActive = true;
    // The frame that starts the measurement contains the wait for the GPU (glFinish): not counted
    skipFirst_ = true;
}

void Recorder::frameDone(double totalMs, double updateMs, double drawMs, double gpuWaitMs, int fbW, int fbH)
{
    if(left_ <= 0)
        return;
    if(skipFirst_)
    {
        skipFirst_ = false;
        gPhase.fill(0);
        gDraws = 0;
        gGameFrames.clear();
        return;
    }
    samples_.push_back({totalMs, updateMs, drawMs, gpuWaitMs, gPhase[static_cast<size_t>(Phase::Sim)],
                        gPhase[static_cast<size_t>(Phase::World)], gPhase[static_cast<size_t>(Phase::Atlas)],
                        gPhase[static_cast<size_t>(Phase::Terrain)], gDraws});
    gPhase.fill(0);
    gDraws = 0;
    if(--left_ == 0)
    {
        gActive = false;
        report(fbW, fbH);
    }
}

void Recorder::report(int fbW, int fbH)
{
    auto col = [&](double Sample::*m) {
        std::vector<double> v;
        v.reserve(samples_.size());
        for(const Sample& s : samples_)
            v.push_back(s.*m);
        return v;
    };
    const std::vector<double> total = col(&Sample::total);
    const double avg = mean(total);
    const auto outliers = std::count_if(total.begin(), total.end(), [&](double t) { return t > 2 * avg + 2; });
    const auto over16 = std::count_if(total.begin(), total.end(), [](double t) { return t > 16.7; });
    double draws = 0;
    for(const Sample& s : samples_)
        draws += s.draws;
    draws /= std::max<size_t>(1, samples_.size());
    double rss, peak;
    memoryMiB(rss, peak);
    const std::vector<double> sim = col(&Sample::sim), world = col(&Sample::world), upd = col(&Sample::update),
                              drw = col(&Sample::draw), gpu = col(&Sample::gpu),
                              atlas = col(&Sample::atlas), terrain = col(&Sample::terrain);
    std::ostringstream o;
    o << std::fixed << std::setprecision(2);
    o << "[perf] " << label_ << (label_.empty() ? "" : " ") << samples_.size() << " frames " << fbW << "x" << fbH
      << ": avg " << avg << " p50 " << percentile(total, 0.5) << " p95 " << percentile(total, 0.95) << " p99 "
      << percentile(total, 0.99) << " max " << percentile(total, 1.0) << " ms, outliers(>2x) " << outliers
      << ", >16.7ms " << over16 << " | sim " << mean(sim) << " world " << mean(world) << " (terrain " << mean(terrain) << ", atlas " << mean(atlas) << ") ui "
      << mean(drw) - mean(world) << " update-other " << mean(upd) - mean(sim) << " gpu-wait " << mean(gpu)
      << " ms | draws " << std::setprecision(0) << draws << ", terrain rebuilds " << gTerrainRebuilds;
    if(!gGameFrames.empty())
        o << std::setprecision(3) << " | GF " << gGameFrames.size() << " x avg " << mean(gGameFrames) << " p99 "
          << percentile(gGameFrames, 0.99) << " max " << percentile(gGameFrames, 1.0) << " ms";
    o << std::setprecision(1) << " | rss " << rss << " MiB (peak " << peak << ", heap " << heapMiB() << "), textures "
      << static_cast<double>(gfx::Texture::totalBytes()) / (1024.0 * 1024.0) << " MiB";
    if(extraInfo)
        o << ", " << extraInfo();
    std::cout << o.str() << std::endl;
    // The worst frames with their parts (finding the cause of hitches)
    std::vector<size_t> idx(samples_.size());
    std::iota(idx.begin(), idx.end(), 0);
    std::sort(idx.begin(), idx.end(), [&](size_t a, size_t b) { return samples_[a].total > samples_[b].total; });
    for(size_t k = 0; k < std::min<size_t>(3, idx.size()); ++k)
    {
        const Sample& s = samples_[idx[k]];
        if(s.total <= 2 * avg + 2)
            break;
        std::cout << std::fixed << std::setprecision(1) << "[perf]   frame " << idx[k] << ": " << s.total
                  << " ms (sim " << s.sim << ", other update " << s.update - s.sim << ", world " << s.world
                  << " (terrain " << s.terrain << ", atlas " << s.atlas << "), ui " << s.draw - s.world << ", gpu-wait " << s.gpu << ")" << std::endl;
    }
    samples_.clear();
}

std::function<std::string()> extraInfo;

} // namespace weiler::perf
