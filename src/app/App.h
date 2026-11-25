// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "DataPaths.h"
#include "Screen.h"
#include "Perf.h"
#include "gfx/Batch.h"
#include "gfx/SpriteAtlas.h"
#include "gfx/Text.h"
#include "ui/Icons.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>

namespace weiler {

class Audio;
class CdImport;
class MenuBackdrop;
class Music;
class Settings;
class TestScript;
namespace ui {
class Ui;
}

struct AppOptions
{
    std::string dataDir;
    /// Window size (0 = default)
    int width = 0, height = 0;
    /// UI scale override (0 = from display)
    float uiScale = 0;
    std::string script;
    bool fullscreen = false;
};

class App
{
public:
    explicit App(AppOptions opts);
    ~App();
    int run();

    // Services for the screens
    gfx::Batch& batch() { return batch_; }
    gfx::SpriteAtlas& atlas() { return *atlas_; }
    gfx::Font& font() { return *font_; }
    gfx::Font& fontBold() { return *fontBold_; }
    /// Serif display font (titles, word mark)
    gfx::Font& fontDisplay() { return *fontDisplay_; }
    Audio& audio() { return *audio_; }
    Music& music() { return *music_; }
    /// Import of the original CD music (created on first use)
    CdImport& cdImport();
    /// Collect the music files (own pieces, imported CD tracks) according to the settings
    void scanMusic();
    /// Folder with the tracks of the original CD (imported, else from the package weiler-data)
    boost::filesystem::path cdMusicDir() const;
    ui::IconAtlas& icons() { return icons_; }
    ui::Ui& ui() { return *ui_; }
    Settings& settings() { return *settings_; }
    const DataPaths& paths() const { return paths_; }
    /// Use the game data in dir (chosen by the user); remember: keep it in the settings for the next start
    void setGameData(const boost::filesystem::path& dir, bool remember);
    /// Density independent pixel -> framebuffer pixel
    float dp(float v) const { return v * uiScale_; }
    float uiScale() const { return uiScale_; }
    int width() const { return fbW_; }
    int height() const { return fbH_; }
    bool portrait() const { return fbH_ >= fbW_; }
    /// Milliseconds since start (deterministic in test mode)
    unsigned timeMs() const { return static_cast<unsigned>(time_ * 1000.0); }
    double time() const { return time_; }
    bool testMode() const { return testMode_; }
    float fps() const { return fps_; }
    /// Average work time per frame in milliseconds (update, draw and GPU until the frame is done)
    float frameMs() const { return frameMs_; }
    /// Test scripts: measure the next frames (work time incl. glFinish) and print the result
    void startPerf(int frames, std::string label = {})
    {
        // Measure from an idle GPU (queued frames of the past would count into the first frame)
        glFinish();
        perf_.start(frames, std::move(label));
    }
    /// Test scripts: draw the next frame in overdraw mode and print the number of fragments per pixel
    void measureOverdraw(std::string heatmapPng = {})
    {
        overdrawFrames_ = 1;
        overdrawPng_ = std::move(heatmapPng);
    }

    /// Apply the graphics settings (glass quality, frame rate)
    void applyGraphicsSettings();

    /// Replace the current screen at the end of the frame
    void setScreen(std::unique_ptr<Screen> screen);
    /// Destroy the current screen right away and drop the other posted functions (only from a posted function)
    void closeScreen();
    /// Short message over every screen (errors outside of a game)
    void notify(std::string text);
    void quit() { running_ = false; }
    /// Small picture of the world (without interface) for savegames, written as PNG
    bool saveThumbnail(const std::string& pngPath);
    /// Texture of a PNG file (cached; reloaded when the file changes), nullptr if missing
    const gfx::Texture* imageTexture(const std::string& pngPath);
    /// Write a screenshot of the next presented frame
    bool saveScreenshot(const std::string& path);
    void startTextInput(bool on);

    Screen* screen() { return screen_.get(); }
    /// Background of the menus (created on first use)
    MenuBackdrop& menuBackdrop();
    /// Free the background world (before a game session is created)
    void releaseMenuBackdrop();

    /// Run a function on the main thread at the end of the frame
    void post(std::function<void()> fn) { posted_.push_back(std::move(fn)); }

private:
    bool initVideo();
    void handleEvent(const SDL_Event& ev);
    void pointer(PointerEvent ev);
    /// One frame; workStart = begin of the frame incl. event handling (measurements)
    void frame(double dt, std::chrono::steady_clock::time_point workStart);
    void updateSize();

    AppOptions opts_;
    DataPaths paths_;
    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    gfx::Batch batch_;
    std::unique_ptr<gfx::SpriteAtlas> atlas_;
    std::unique_ptr<gfx::Font> font_, fontBold_, fontDisplay_;
    std::unique_ptr<Audio> audio_;
    std::unique_ptr<Music> music_;
    std::unique_ptr<CdImport> cdImport_;
    ui::IconAtlas icons_;
    std::unique_ptr<ui::Ui> ui_;
    std::unique_ptr<Settings> settings_;
    std::unique_ptr<Screen> screen_, nextScreen_;
    std::unique_ptr<MenuBackdrop> backdrop_;
    struct CachedImage
    {
        gfx::Texture tex;
        long long stamp = 0;
    };
    std::map<std::string, CachedImage> images_;
    std::future<bool> thumbnail_;
    std::unique_ptr<TestScript> script_;
    std::deque<std::function<void()>> posted_;
    std::string notice_;
    double noticeTime_ = -100;
    void drawNotice();
    int fbW_ = 1, fbH_ = 1;
    float uiScale_ = 1;
    double time_ = 0;
    bool running_ = true;
    bool testMode_ = false;
    float fps_ = 0, frameMs_ = 0;
    perf::Recorder perf_;
    /// Frame pacing: 0 = 30 fps, 1 = display rate, 2 = display rate after input, 30 fps when idle
    int fpsMode_ = 2;
    float refreshRate_ = 60.f;
    double lastInput_ = 0;
    /// Window not visible (background, minimized, occluded): no drawing
    bool hidden_ = false;
    bool pacedTest_ = false;
    void setHidden(bool hidden);
    /// Seconds to wait after a frame (frame pacing); 0 = as fast as the swap interval allows
    double frameInterval() const;
    int overdrawFrames_ = 0;
    std::string overdrawPng_;
    bool screenTimed_ = false;
    void reportOverdraw();
    // Two finger tap detection
    struct Touch
    {
        float x0, y0, x, y;
        double start;
    };
    std::map<uint64_t, Touch> touches_;
    unsigned maxTouches_ = 0;
    double multiStart_ = 0;
    bool multiMoved_ = false;
    friend class TestScript;
};

} // namespace weiler
