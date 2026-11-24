// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace weiler::perf {

/// Parts of a frame that are timed separately (CPU time, milliseconds)
enum class Phase
{
    /// Game simulation (game frames, AI)
    Sim,
    /// Building and submitting the world (terrain, objects, figures)
    World,
    /// Creating sprites in the texture atlas (part of World or UI)
    Atlas,
    /// Building the terrain geometry (part of World)
    Terrain,
    Count
};

/// True while a measurement window is open (timing calls are cheap no-ops otherwise)
bool active();
/// Add time to a phase of the current frame
void add(Phase phase, double ms);
/// One game frame took ms (simulation statistics)
void gameFrame(double ms);
/// Count a draw call of the current frame
void drawCall();

/// Scoped timer for a phase
class Scope
{
public:
    explicit Scope(Phase p) : phase_(p), on_(active())
    {
        if(on_)
            start_ = std::chrono::steady_clock::now();
    }
    ~Scope()
    {
        if(on_)
            add(phase_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count());
    }

private:
    Phase phase_;
    bool on_;
    std::chrono::steady_clock::time_point start_;
};

/// Measurement window over a number of frames, driven by the app
class Recorder
{
public:
    void start(int frames, std::string label);
    bool running() const { return left_ > 0; }
    /// Frame finished: total work time incl. GPU (glFinish), update and draw CPU parts, time waiting for the GPU
    void frameDone(double totalMs, double updateMs, double drawMs, double gpuWaitMs, int fbW, int fbH);

private:
    struct Sample
    {
        double total, update, draw, gpu, sim, world, atlas, terrain;
        unsigned draws;
    };
    void report(int fbW, int fbH);
    std::vector<Sample> samples_;
    std::string label_;
    int left_ = 0;
};

/// Resident memory of the process in MiB (current and peak)
void memoryMiB(double& rss, double& peak);

/// Extra text for the measurement report (atlas sizes etc.)
extern std::function<std::string()> extraInfo;

/// Milliseconds since process start (first call of this function happens in main)
double sinceStart();

} // namespace weiler::perf
