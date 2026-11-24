// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "App.h"
#include "Audio.h"
#include "Loader.h"
#include "Settings.h"
#include "TestScript.h"
#include "ViewState.h"
#include "ui/Ui.h"
#include "s25util/Log.h"
#include <boost/filesystem.hpp>
#include <chrono>
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
}

App::~App()
{
    screen_.reset();
    nextScreen_.reset();
    ui_.reset();
    audio_.reset();
    atlas_.reset();
    font_.reset();
    fontBold_.reset();
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
    if(!initVideo())
        return 1;
    batch_.init();
    atlas_ = std::make_unique<gfx::SpriteAtlas>(batch_);
    icons_.init(96);
    ui_ = std::make_unique<ui::Ui>(*this);
    font_ = std::make_unique<gfx::Font>();
    fontBold_ = std::make_unique<gfx::Font>();
    std::vector<bfs::path> fontDirs = {paths_.appData / "fonts", bfs::path(WEILER_FONT_DIR)};
    for(const auto& dir : fontDirs)
    {
        if(bfs::exists(dir / "Lato-Regular.ttf"))
        {
            font_->load((dir / "Lato-Regular.ttf").string());
            fontBold_->load((dir / "Lato-Bold.ttf").string());
            break;
        }
    }
    atlas_->textDrawer = [this](float x, float y, const std::string& text, gfx::Color c) {
        font_->draw(batch_, x, y - 8, text, 9, c, gfx::Align::Center);
    };
    audio_ = std::make_unique<Audio>();
    if(!testMode_)
        audio_->init();
    settings_ = std::make_unique<Settings>(paths_.userData / "settings.ini");
    audio_->setEffectVolume(settings_->getFloat("volume.effects", 0.8f));
    audio_->setMusicVolume(settings_->getFloat("volume.music", 0.6f));

    if(testMode_)
    {
        script_ = std::make_unique<TestScript>(*this);
        if(!script_->load(opts_.script))
            return 2;
    }

    extern std::unique_ptr<Screen> createStartScreen(App & app);
    screen_ = createStartScreen(*this);

    using Clock = std::chrono::steady_clock;
    auto last = Clock::now();
    double fpsTime = 0;
    unsigned fpsFrames = 0;
    while(running_)
    {
        double dt;
        if(testMode_)
            dt = 1.0 / 60.0;
        else
        {
            const auto now = Clock::now();
            dt = std::min(0.25, std::chrono::duration<double>(now - last).count());
            last = now;
        }
        if(script_ && !script_->step())
            running_ = false;
        SDL_Event ev;
        while(SDL_PollEvent(&ev))
            handleEvent(ev);
        frame(dt);
        fpsTime += dt;
        ++fpsFrames;
        if(fpsTime >= 0.5)
        {
            fps_ = static_cast<float>(fpsFrames / fpsTime);
            fpsTime = 0;
            fpsFrames = 0;
        }
    }
    if(screen_)
        screen_->onSuspend();
    settings_->save();
    return script_ ? script_->exitCode() : 0;
}

void App::frame(double dt)
{
    const auto workStart = std::chrono::steady_clock::now();
    time_ += dt;
    if(nextScreen_)
    {
        screen_ = std::move(nextScreen_);
    }
    if(screen_)
        screen_->update(dt);
    batch_.begin(fbW_, fbH_);
    glClearColor(0.07f, 0.075f, 0.08f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);
    ui_->begin();
    if(screen_)
        screen_->draw();
    ui_->end();
    batch_.flush();
    // Work time of this frame; the GPU part is only included when measuring (glFinish stalls the pipeline)
    if(perfLeft_ > 0)
        glFinish();
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - workStart).count();
    frameMs_ = static_cast<float>(frameMs_ * 0.9 + ms * 0.1);
    if(perfLeft_ > 0)
    {
        perfSum_ += ms;
        perfMax_ = std::max(perfMax_, ms);
        if(--perfLeft_ == 0)
            std::cout << "[perf] " << perfFrames_ << " frames, " << fbW_ << "x" << fbH_ << ": avg "
                      << perfSum_ / perfFrames_ << " ms, max " << perfMax_ << " ms (" << 1000.0 / (perfSum_ / perfFrames_)
                      << " fps possible)" << std::endl;
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

void App::setScreen(std::unique_ptr<Screen> screen)
{
    nextScreen_ = std::move(screen);
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
    switch(ev.type)
    {
        case SDL_EVENT_QUIT: running_ = false; break;
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED: updateSize(); break;
        case SDL_EVENT_DID_ENTER_BACKGROUND:
        case SDL_EVENT_WINDOW_HIDDEN:
            if(screen_)
                screen_->onSuspend();
            settings_->save();
            break;
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
