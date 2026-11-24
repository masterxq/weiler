// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "App.h"
#include "Session.h"
#include "menu/MenuBackdrop.h"
#include "Music.h"
#include "CdImport.h"
#include "Audio.h"
#include "Loader.h"
#include "Settings.h"
#include "TestScript.h"
#include "menu/Menus.h"
#include "ViewState.h"
#include "ui/Ui.h"
#include "s25util/Log.h"
#include "mygettext/mygettext.h"
#include <boost/filesystem.hpp>
#include <array>
#include <chrono>
#include <clocale>
#include <cstdlib>
#include <iostream>
#include <cmath>
#include <stdexcept>
#include <vector>

#ifndef WEILER_FONT_DIR
#    define WEILER_FONT_DIR ""
#endif

namespace bfs = boost::filesystem;

namespace weiler {

App::App(AppOptions opts) : opts_(std::move(opts)), paths_(DataPaths::detect(opts_.dataDir))
{
    testMode_ = !opts_.script.empty();
    pacedTest_ = std::getenv("WEILER_TEST_PACING") != nullptr;
}

App::~App()
{
    screen_.reset();
    nextScreen_.reset();
    backdrop_.reset();
    images_.clear();
    ui_.reset();
    cdImport_.reset();
    music_.reset();
    audio_.reset();
    atlas_.reset();
    font_.reset();
    fontBold_.reset();
    fontDisplay_.reset();
    if(gl_)
        SDL_GL_DestroyContext(gl_);
    if(window_)
        SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool App::initVideo()
{
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "Portrait LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
    SDL_SetHint(SDL_HINT_APP_ID, "weiler");
    if(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
    {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    SDL_InitSubSystem(SDL_INIT_AUDIO);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
    int w = opts_.width, h = opts_.height;
    if(w <= 0 || h <= 0)
    {
        w = 1280;
        h = 800;
    }
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
    if(!testMode_)
        flags |= SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if(opts_.fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow("Weiler", w, h, flags);
    if(!window_)
    {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    gl_ = SDL_GL_CreateContext(window_);
    if(!gl_)
    {
        SDL_Log("No OpenGL ES 3.0 context: %s", SDL_GetError());
        return false;
    }
    SDL_GL_MakeCurrent(window_, gl_);
    SDL_GL_SetSwapInterval(testMode_ ? 0 : 1);
    std::cout << "GL: " << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << " / "
              << reinterpret_cast<const char*>(glGetString(GL_VERSION)) << std::endl;
    updateSize();
    return true;
}

void App::updateSize()
{
    SDL_GetWindowSizeInPixels(window_, &fbW_, &fbH_);
    if(const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window_)))
    {
        if(mode->refresh_rate > 0)
            refreshRate_ = mode->refresh_rate;
    }
    if(opts_.uiScale > 0)
        uiScale_ = opts_.uiScale;
    else
    {
        float s = SDL_GetWindowDisplayScale(window_);
        if(const char* env = SDL_getenv("WEILER_UI_SCALE"))
            s = static_cast<float>(SDL_atof(env));
        uiScale_ = s > 0.1f ? s : 1.f;
    }
}

int App::run()
{
#ifdef WEILER_LANG_DIR
    // Texts of the core (post messages, missions) in German
    mygettext::setlocale(LC_ALL, "de");
    mygettext::bind_textdomain_codeset("rttr", "UTF-8");
    mygettext::bindtextdomain("rttr", WEILER_LANG_DIR);
    mygettext::textdomain("rttr");
#endif
    if(!initVideo())
        return 1;
    batch_.init();
    atlas_ = std::make_unique<gfx::SpriteAtlas>(batch_);
    perf::extraInfo = [this] { return atlas_->describe(); };
    icons_.init(96);
    ui_ = std::make_unique<ui::Ui>(*this);
    font_ = std::make_unique<gfx::Font>();
    fontBold_ = std::make_unique<gfx::Font>();
    fontDisplay_ = std::make_unique<gfx::Font>();
    std::vector<bfs::path> fontDirs = {paths_.appData / "fonts", bfs::path(WEILER_FONT_DIR)};
    for(const auto& dir : fontDirs)
    {
        if(bfs::exists(dir / "Lato-Regular.ttf"))
        {
            font_->load((dir / "Lato-Regular.ttf").string());
            fontBold_->load((dir / "Lato-Bold.ttf").string());
            if(!fontDisplay_->load((dir / "NotoSerifDisplay-SemiBold.ttf").string()))
                fontDisplay_->load((dir / "Lato-Bold.ttf").string());
            break;
        }
    }
    atlas_->textDrawer = [this](float x, float y, const std::string& text, gfx::Color c) {
        font_->draw(batch_, x, y - 8, text, 9, c, gfx::Align::Center);
    };
    audio_ = std::make_unique<Audio>();
    // Test mode: no device; the mix can be written into a file for analysis
    if(!testMode_)
        audio_->init();
    else if(const char* wav = std::getenv("WEILER_AUDIO_WAV"))
        audio_->initFile(wav);
    settings_ = std::make_unique<Settings>(paths_.userData / "settings.ini");
    audio_->setVolume(Bus::Effects, settings_->getFloat("volume.effects", 0.8f));
    audio_->setVolume(Bus::Music, settings_->getFloat("volume.music", 0.6f));
    audio_->setVolume(Bus::Ui, settings_->getFloat("volume.ui", 0.6f));
    applyGraphicsSettings();
    music_ = std::make_unique<Music>(*audio_);
    scanMusic();
    ui_->onActivate = [this](int kind) { audio_->playUi(kind == 1 ? UiSound::Toggle : UiSound::Tap); };

    if(testMode_)
    {
        script_ = std::make_unique<TestScript>(*this);
        if(!script_->load(opts_.script))
            return 2;
    }

    screen_ = createStartScreen(*this);

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    double fpsTime = 0;
    unsigned fpsFrames = 0;
    while(running_)
    {
        SDL_Event ev;
        // In the background: no drawing, no simulation; sleep until something happens
        while(hidden_ && running_ && !testMode_ && SDL_WaitEvent(&ev))
        {
            handleEvent(ev);
            last = Clock::now();
        }
        const auto frameStart = Clock::now();
        double dt;
        if(testMode_)
            dt = 1.0 / 60.0;
        else
        {
            dt = std::min(0.25, std::chrono::duration<double>(frameStart - last).count());
            last = frameStart;
        }
        if(script_ && !script_->step())
            running_ = false;
        while(SDL_PollEvent(&ev))
            handleEvent(ev);
        frame(dt, frameStart);
        fpsTime += dt;
        ++fpsFrames;
        if(fpsTime >= 0.5)
        {
            fps_ = static_cast<float>(fpsFrames / fpsTime);
            fpsTime = 0;
            fpsFrames = 0;
        }
        // Frame pacing: sleep (never spin) for the rest of the frame interval (test mode: only on request, for
        // measuring the power saving)
        if(!testMode_ || pacedTest_)
        {
            const double rest = frameInterval() - std::chrono::duration<double>(Clock::now() - frameStart).count();
            if(rest > 0.0005)
                SDL_DelayNS(static_cast<Uint64>(rest * 1e9));
        }
    }
    if(screen_)
        screen_->onSuspend();
    settings_->save();
    Session::waitForSaves();
    if(thumbnail_.valid())
        thumbnail_.wait();
    return script_ ? script_->exitCode() : 0;
}

void App::applyGraphicsSettings()
{
    batch_.setGlassQuality(std::clamp(settings_->getInt("gfx.glass", 1), 0, 2));
    fpsMode_ = std::clamp(settings_->getInt("gfx.fps", 2), 0, 2);
}

double App::frameInterval() const
{
    const double display = 1.0 / std::max(20.f, refreshRate_);
    const bool recentInput = time_ - lastInput_ < 2.0;
    double interval = 0;
    if(fpsMode_ == 0 || (fpsMode_ == 2 && !recentInput))
        interval = 1.0 / 30.0;
    // Paused game without input: nothing moves, a few frames per second are enough (toasts, clock)
    if(screen_ && screen_->still() && !recentInput)
        interval = 1.0 / 10.0;
    // Without a working swap interval (vsync) never draw faster than the display
    return std::max(interval, display * 0.9);
}

void App::setHidden(bool hidden)
{
    if(hidden == hidden_)
        return;
    hidden_ = hidden;
    if(audio_)
        audio_->setSuspended(hidden);
    if(hidden)
    {
        if(screen_)
            screen_->onSuspend();
        settings_->save();
    }
}

void App::frame(double dt, std::chrono::steady_clock::time_point workStart)
{
    using Clock = std::chrono::steady_clock;
    auto msSince = [](Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); };
    time_ += dt;
    if(nextScreen_)
    {
        screen_ = std::move(nextScreen_);
        screenTimed_ = false;
    }
    if(screen_)
        screen_->update(dt);
    music_->update(dt);
    audio_->pump(dt);
    const double updateMs = msSince(workStart);
    const auto drawStart = Clock::now();
    atlas_->beginFrame();
    const bool overdraw = overdrawFrames_ > 0;
    gfx::gOverdraw = overdraw ? 1.f / 255.f : 0.f;
    batch_.begin(fbW_, fbH_);
    if(overdraw)
        glClearColor(0.f, 0.f, 0.f, 0.f);
    else
        glClearColor(0.07f, 0.075f, 0.08f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ui_->begin();
    if(screen_)
        screen_->draw();
    drawNotice();
    ui_->end();
    batch_.flush();
    const double drawMs = msSince(drawStart);
    if(overdraw)
    {
        reportOverdraw();
        gfx::gOverdraw = 0.f;
        --overdrawFrames_;
    }
    // Work time of this frame; the GPU part is only included when measuring (glFinish stalls the pipeline)
    double gpuMs = 0;
    if(perf_.running())
    {
        const auto finishStart = Clock::now();
        glFinish();
        gpuMs = msSince(finishStart);
    }
    const double ms = msSince(workStart);
    frameMs_ = static_cast<float>(frameMs_ * 0.9 + ms * 0.1);
    perf_.frameDone(ms, updateMs, drawMs, gpuMs, fbW_, fbH_);
    // Load times: first frame of every screen
    if(screen_ && !screenTimed_)
    {
        screenTimed_ = true;
        double rss, peak;
        perf::memoryMiB(rss, peak);
        std::cout << "[time] first frame of " << screen_->name() << " after " << static_cast<long>(perf::sinceStart())
                  << " ms, rss " << static_cast<long>(rss) << " MiB, textures "
                  << gfx::Texture::totalBytes() / (1024 * 1024) << " MiB" << std::endl;
    }
    if(script_)
        script_->afterDraw();
    SDL_GL_SwapWindow(window_);
    while(!posted_.empty())
    {
        auto fn = std::move(posted_.front());
        posted_.pop_front();
        fn();
    }
}

void App::reportOverdraw()
{
    std::vector<uint8_t> px(static_cast<size_t>(fbW_) * fbH_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbW_, fbH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    uint64_t sum = 0;
    std::array<uint64_t, 6> hist{};
    unsigned maxV = 0;
    for(size_t i = 0; i < px.size(); i += 4)
    {
        const unsigned v = px[i];
        sum += v;
        maxV = std::max(maxV, v);
        ++hist[std::min<size_t>(v, hist.size() - 1)];
    }
    const double n = static_cast<double>(fbW_) * fbH_;
    std::cout << "[overdraw] " << fbW_ << "x" << fbH_ << ": " << static_cast<double>(sum) / n
              << " fragments per pixel, max " << maxV << ", share of pixels with 0/1/2/3/4/5+ layers:";
    for(uint64_t h : hist)
        std::cout << " " << static_cast<int>(100.0 * static_cast<double>(h) / n + 0.5) << "%";
    std::cout << std::endl;
    if(overdrawPng_.empty())
        return;
    // Heat map: black = 0, then blue, green, yellow, red, white for 1..5+ layers
    static const uint8_t ramp[6][3] = {{0, 0, 0}, {30, 60, 200}, {40, 170, 60}, {230, 210, 40}, {220, 60, 30},
                                       {255, 255, 255}};
    std::vector<uint8_t> img(px.size());
    for(int y = 0; y < fbH_; ++y)
    {
        for(int x = 0; x < fbW_; ++x)
        {
            const size_t src = (static_cast<size_t>(fbH_ - 1 - y) * fbW_ + x) * 4;
            const size_t dst = (static_cast<size_t>(y) * fbW_ + x) * 4;
            const auto* c = ramp[std::min<unsigned>(px[src], 5)];
            img[dst] = c[0];
            img[dst + 1] = c[1];
            img[dst + 2] = c[2];
            img[dst + 3] = 255;
        }
    }
    if(SDL_Surface* surf = SDL_CreateSurfaceFrom(fbW_, fbH_, SDL_PIXELFORMAT_ABGR8888, img.data(), fbW_ * 4))
    {
        SDL_SavePNG(surf, overdrawPng_.c_str());
        SDL_DestroySurface(surf);
    }
}

CdImport& App::cdImport()
{
    if(!cdImport_)
        cdImport_ = std::make_unique<CdImport>();
    return *cdImport_;
}

void App::scanMusic()
{
    auto collect = [](const bfs::path& dir, const std::string& prefix) {
        std::vector<std::string> files;
        boost::system::error_code ec;
        if(!bfs::is_directory(dir, ec))
            return files;
        for(const auto& e : bfs::directory_iterator(dir, ec))
        {
            const std::string name = e.path().filename().string();
            if(e.path().extension() == ".ogg" && name.rfind(prefix, 0) == 0)
                files.push_back(e.path().string());
        }
        return files;
    };
    bfs::path dir = paths_.appData / "music";
    if(!bfs::exists(dir))
        dir = WEILER_MUSIC_DIR;
    music_->setTracks(Music::Context::Menu, collect(dir, "menu"));
    // Source: 0 = own pieces, 1 = own and CD, 2 = CD only (imported tracks of the original CD)
    const int source = settings_->getInt("music.source", 0);
    std::vector<std::string> game;
    if(source != 2)
        game = collect(dir, "game");
    if(source != 0)
    {
        auto cd = collect(paths_.userData / "music" / "cd", "track");
        game.insert(game.end(), cd.begin(), cd.end());
    }
    if(game.empty())
        game = collect(dir, "game");
    music_->setTracks(Music::Context::Game, std::move(game));
}

MenuBackdrop& App::menuBackdrop()
{
    if(!backdrop_)
        backdrop_ = std::make_unique<MenuBackdrop>(*this);
    return *backdrop_;
}

void App::releaseMenuBackdrop()
{
    backdrop_.reset();
}

void App::setScreen(std::unique_ptr<Screen> screen)
{
    nextScreen_ = std::move(screen);
}

void App::closeScreen()
{
    // Functions posted by the old screen may refer to it
    posted_.clear();
    screen_.reset();
    nextScreen_.reset();
}

void App::notify(std::string text)
{
    notice_ = std::move(text);
    noticeTime_ = time_;
}

void App::drawNotice()
{
    const float age = static_cast<float>(time_ - noticeTime_);
    if(notice_.empty() || age > 6)
        return;
    auto& ui = *ui_;
    const float a = std::min(1.f, age / 0.15f) * std::min(1.f, (6.f - age) / 0.5f);
    const float fs = dp(14), W = static_cast<float>(fbW_);
    const float maxW = std::min(W - dp(40), dp(520));
    const auto lines = font_->wrap(notice_, fs, maxW - dp(28));
    float w = 0;
    for(const auto& l : lines)
        w = std::max(w, ui.textWidth(l, fs));
    w += dp(28);
    const float h = static_cast<float>(lines.size()) * fs * 1.3f + dp(18);
    const ui::Rect2 r{(W - w) / 2, static_cast<float>(fbH_) - h - dp(40), w, h};
    ui.panel(r, dp(ui.theme.radius.lg), ui.theme.glassStrong, a);
    float ty = r.y + dp(9);
    for(const auto& l : lines)
    {
        ui.text(r.x + dp(14), ty, l, fs, ui.theme.text.withAlpha(a));
        ty += fs * 1.3f;
    }
}

bool App::saveThumbnail(const std::string& pngPath)
{
    auto px = std::make_shared<std::vector<uint8_t>>();
    int w = 0, h = 0;
    if(!batch_.readBackdrop(*px, w, h))
        return false;
    // PNG encoding takes tens of milliseconds: on a worker thread, written next to the file and renamed
    if(thumbnail_.valid())
        thumbnail_.wait();
    thumbnail_ = std::async(std::launch::async, [px, w, h, pngPath] {
        SDL_Surface* surf = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_ABGR8888, px->data(), w * 4);
        if(!surf)
            return false;
        const std::string tmp = pngPath + ".tmp";
        bool ok = SDL_SavePNG(surf, tmp.c_str());
        SDL_DestroySurface(surf);
        boost::system::error_code ec;
        if(ok)
            bfs::rename(tmp, pngPath, ec);
        return ok && !ec;
    });
    return true;
}

const gfx::Texture* App::imageTexture(const std::string& pngPath)
{
    boost::system::error_code ec;
    const auto t = bfs::last_write_time(pngPath, ec);
    if(ec)
        return nullptr;
    const long long stamp = static_cast<long long>(t);
    auto it = images_.find(pngPath);
    if(it != images_.end() && it->second.stamp == stamp)
        return &it->second.tex;
    SDL_Surface* loaded = SDL_LoadPNG(pngPath.c_str());
    if(!loaded)
        return nullptr;
    SDL_Surface* surf = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_ABGR8888);
    SDL_DestroySurface(loaded);
    if(!surf)
        return nullptr;
    CachedImage& img = images_[pngPath];
    img.stamp = stamp;
    img.tex.create(surf->w, surf->h, false, true, surf->pixels);
    SDL_DestroySurface(surf);
    return &img.tex;
}

bool App::saveScreenshot(const std::string& path)
{
    std::vector<uint8_t> px(static_cast<size_t>(fbW_) * fbH_ * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, fbW_, fbH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    std::vector<uint8_t> flipped(px.size());
    const size_t row = static_cast<size_t>(fbW_) * 4;
    for(int y = 0; y < fbH_; ++y)
        std::copy_n(px.data() + (fbH_ - 1 - y) * row, row, flipped.data() + y * row);
    for(size_t i = 3; i < flipped.size(); i += 4)
        flipped[i] = 255;
    SDL_Surface* surf = SDL_CreateSurfaceFrom(fbW_, fbH_, SDL_PIXELFORMAT_ABGR8888, flipped.data(), fbW_ * 4);
    if(!surf)
        return false;
    const bool ok = SDL_SavePNG(surf, path.c_str());
    SDL_DestroySurface(surf);
    return ok;
}

void App::startTextInput(bool on)
{
    if(on)
        SDL_StartTextInput(window_);
    else
        SDL_StopTextInput(window_);
}

void App::pointer(PointerEvent ev)
{
    // Two finger tap = back
    if(!ev.isMouse)
    {
        if(ev.type == PointerEvent::Type::Down)
        {
            if(touches_.empty())
            {
                maxTouches_ = 0;
                multiMoved_ = false;
                multiStart_ = time_;
            }
            touches_[ev.id] = Touch{ev.x, ev.y, ev.x, ev.y, time_};
            maxTouches_ = std::max<unsigned>(maxTouches_, static_cast<unsigned>(touches_.size()));
        } else if(ev.type == PointerEvent::Type::Move)
        {
            auto it = touches_.find(ev.id);
            if(it != touches_.end())
            {
                it->second.x = ev.x;
                it->second.y = ev.y;
                if(std::hypot(ev.x - it->second.x0, ev.y - it->second.y0) > dp(14))
                    multiMoved_ = true;
            }
        }
    }
    // Releasing a multi finger gesture never clicks anything
    const bool released = ev.type == PointerEvent::Type::Up;
    if(!ev.isMouse && ev.type == PointerEvent::Type::Up && maxTouches_ >= 2)
        ev.type = PointerEvent::Type::Cancel;
    if(screen_)
        screen_->onPointer(ev);
    if(!ev.isMouse && (ev.type == PointerEvent::Type::Up || ev.type == PointerEvent::Type::Cancel))
    {
        touches_.erase(ev.id);
        if(touches_.empty() && maxTouches_ == 2 && !multiMoved_ && time_ - multiStart_ < 0.4 && released)
        {
            if(screen_)
                screen_->onBack();
        }
    }
}

void App::handleEvent(const SDL_Event& ev)
{
    if((ev.type >= SDL_EVENT_KEY_DOWN && ev.type < SDL_EVENT_JOYSTICK_AXIS_MOTION)
       || (ev.type >= SDL_EVENT_FINGER_DOWN && ev.type <= SDL_EVENT_FINGER_CANCELED))
        lastInput_ = time_;
    switch(ev.type)
    {
        case SDL_EVENT_QUIT: running_ = false; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
        case SDL_EVENT_WINDOW_DISPLAY_CHANGED: updateSize(); break;
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_WINDOW_HIDDEN:
        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_OCCLUDED: setHidden(true); break;
        case SDL_EVENT_DID_ENTER_FOREGROUND:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_EXPOSED: setHidden(false); break;
        case SDL_EVENT_FINGER_DOWN:
        case SDL_EVENT_FINGER_MOTION:
        case SDL_EVENT_FINGER_UP:
        case SDL_EVENT_FINGER_CANCELED:
        {
            PointerEvent pe;
            pe.type = ev.type == SDL_EVENT_FINGER_DOWN   ? PointerEvent::Type::Down :
                      ev.type == SDL_EVENT_FINGER_MOTION ? PointerEvent::Type::Move :
                      ev.type == SDL_EVENT_FINGER_UP     ? PointerEvent::Type::Up :
                                                           PointerEvent::Type::Cancel;
            pe.id = ev.tfinger.fingerID + 1;
            pe.x = ev.tfinger.x * static_cast<float>(fbW_);
            pe.y = ev.tfinger.y * static_cast<float>(fbH_);
            pointer(pe);
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        {
            if(ev.button.which == SDL_TOUCH_MOUSEID)
                break;
            const float sx = static_cast<float>(fbW_) / std::max(1, [this] {
                int w, h;
                SDL_GetWindowSize(window_, &w, &h);
                return w;
            }());
            if(ev.button.button == SDL_BUTTON_RIGHT)
            {
                if(ev.type == SDL_EVENT_MOUSE_BUTTON_UP && screen_)
                    screen_->onBack();
                break;
            }
            PointerEvent pe;
            pe.type = ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PointerEvent::Type::Down : PointerEvent::Type::Up;
            pe.id = 0;
            pe.x = ev.button.x * sx;
            pe.y = ev.button.y * sx;
            pe.button = ev.button.button;
            pe.isMouse = true;
            pointer(pe);
            break;
        }
        case SDL_EVENT_MOUSE_MOTION:
        {
            if(ev.motion.which == SDL_TOUCH_MOUSEID)
                break;
            int w, h;
            SDL_GetWindowSize(window_, &w, &h);
            const float sx = static_cast<float>(fbW_) / std::max(1, w);
            PointerEvent pe;
            pe.type = PointerEvent::Type::Move;
            pe.id = 0;
            pe.x = ev.motion.x * sx;
            pe.y = ev.motion.y * sx;
            pe.button = (ev.motion.state & SDL_BUTTON_LMASK) ? 1 : 0;
            pe.isMouse = true;
            pointer(pe);
            break;
        }
        case SDL_EVENT_MOUSE_WHEEL:
        {
            int w, h;
            SDL_GetWindowSize(window_, &w, &h);
            const float sx = static_cast<float>(fbW_) / std::max(1, w);
            if(screen_)
                screen_->onWheel(ev.wheel.mouse_x * sx, ev.wheel.mouse_y * sx, ev.wheel.y);
            break;
        }
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if(ev.type == SDL_EVENT_KEY_DOWN && (ev.key.key == SDLK_ESCAPE || ev.key.key == SDLK_AC_BACK))
            {
                if(screen_)
                    screen_->onBack();
                break;
            }
            if(screen_)
                screen_->onKey(ev.key.key, ev.type == SDL_EVENT_KEY_DOWN);
            break;
        case SDL_EVENT_TEXT_INPUT:
            if(screen_)
                screen_->onTextInput(ev.text.text);
            break;
        default: break;
    }
}

} // namespace weiler
