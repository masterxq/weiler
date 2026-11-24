// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "TestScript.h"
#include "App.h"
#include "Screen.h"
#include "ui/Ui.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <fstream>
#include <iostream>
#include <sstream>

namespace weiler {

namespace {
constexpr SDL_TouchID TEST_TOUCH = 77;
}

TestScript::TestScript(App& app) : app_(app) {}

bool TestScript::load(const std::string& path)
{
    std::ifstream in(path);
    if(!in)
    {
        std::cerr << "Cannot open script " << path << std::endl;
        return false;
    }
    std::string line;
    while(std::getline(in, line))
    {
        const auto first = line.find_first_not_of(" \t");
        if(first == std::string::npos || line[first] == '#')
            continue;
        lines_.push_back(line.substr(first));
    }
    return true;
}

float TestScript::coord(const std::string& s, bool isY) const
{
    if(!s.empty() && s.back() == '%')
        return std::stof(s.substr(0, s.size() - 1)) / 100.f * static_cast<float>(isY ? app_.height() : app_.width());
    return std::stof(s);
}

void TestScript::failShot()
{
    if(failDir_.empty())
        return;
    pendingShot_ = failDir_ + "/fail_" + std::to_string(++failCount_) + ".png";
    std::cerr << "[script] fail screenshot " << pendingShot_ << std::endl;
}

bool TestScript::resolve(const std::string& spec, float& x, float& y) const
{
    if(spec.rfind("ui:", 0) == 0)
    {
        std::string label = spec.substr(3);
        int nth = 0;
        const auto hash = label.rfind('#');
        if(hash != std::string::npos && hash + 1 < label.size())
        {
            nth = std::stoi(label.substr(hash + 1));
            label.resize(hash);
        }
        for(char& c : label)
        {
            if(c == '_')
                c = ' ';
        }
        gfx::Rect2 r;
        if(!app_.ui().findTarget(label, r, nth))
            return false;
        x = r.x + r.w / 2;
        y = r.y + r.h / 2;
        return true;
    }
    return app_.screen() && app_.screen()->testPoint(spec, x, y);
}

bool TestScript::expand(std::vector<std::string>& args)
{
    std::vector<std::string> out;
    for(const auto& a : args)
    {
        if(a.find(':') == std::string::npos || a.back() == '%')
        {
            out.push_back(a);
            continue;
        }
        float x, y;
        if(!resolve(a, x, y))
        {
            failShot();
            std::cerr << "[script] FAIL: point not found: " << a << std::endl;
            return false;
        }
        out.push_back(std::to_string(x));
        out.push_back(std::to_string(y));
    }
    args = std::move(out);
    return true;
}

void TestScript::schedule(int delayFrames, const SDL_Event& ev)
{
    scheduled_.emplace_back(frame_ + delayFrames, ev);
}

void TestScript::finger(Uint32 type, Uint64 fingerId, float x, float y, int delayFrames)
{
    SDL_Event ev{};
    ev.type = type;
    ev.tfinger.touchID = TEST_TOUCH;
    ev.tfinger.fingerID = fingerId;
    ev.tfinger.x = x / static_cast<float>(app_.width());
    ev.tfinger.y = y / static_cast<float>(app_.height());
    ev.tfinger.pressure = 1.f;
    schedule(delayFrames, ev);
}

bool TestScript::execute(const std::string& line)
{
    std::istringstream ss(line);
    std::string cmd;
    ss >> cmd;
    std::vector<std::string> a;
    std::string tok;
    while(ss >> tok)
        a.push_back(tok);
    auto num = [&](size_t i, float def) { return i < a.size() ? std::stof(a[i]) : def; };
    static const char* const pointCmds[] = {"tap", "longpress", "drag", "pinch", "twotap", "click", "wheel"};
    if(std::find(std::begin(pointCmds), std::end(pointCmds), cmd) != std::end(pointCmds) && !expand(a))
    {
        exitCode_ = 4;
        return true;
    }
    try
    {
        if(cmd == "failshots")
            failDir_ = a.at(0);
        else if(cmd == "tapfirst" || cmd == "tapopt")
        {
            // Tap the first symbolic point that exists (tapopt: do nothing if none exists)
            for(const auto& spec : a)
            {
                float x, y;
                if(resolve(spec, x, y))
                {
                    finger(SDL_EVENT_FINGER_DOWN, 1, x, y, 0);
                    finger(SDL_EVENT_FINGER_UP, 1, x, y, 3);
                    waitFrames_ = 5;
                    return true;
                }
            }
            if(cmd == "tapfirst")
            {
                failShot();
                std::cerr << "[script] FAIL: none found: " << line << std::endl;
                exitCode_ = 4;
            }
        } else if(cmd == "waitfor")
        {
            waitFor_ = a.at(0);
            waitForFrames_ = static_cast<int>(num(1, 600));
        } else if(cmd == "perf")
        {
            app_.startPerf(static_cast<int>(num(0, 120)));
            waitFrames_ = static_cast<int>(num(0, 120)) + 1;
        } else if(cmd == "labels")
        {
            std::cout << "[script] labels:";
            for(const auto& l : app_.ui().targetLabels())
                std::cout << " [" << l << "]";
            std::cout << std::endl;
        } else if(cmd == "game")
        {
            if(a.empty() || !app_.screen()
               || !app_.screen()->testCommand(a[0], std::vector<std::string>(a.begin() + 1, a.end())))
            {
                failShot();
                std::cerr << "[script] FAIL: " << line << std::endl;
                exitCode_ = 4;
            }
            waitFrames_ = 1;
        } else if(cmd == "wait")
            waitFrames_ = static_cast<int>(num(0, 1));
        else if(cmd == "sec")
            waitFrames_ = static_cast<int>(num(0, 1) * 60);
        else if(cmd == "tap" || cmd == "longpress")
        {
            const float x = coord(a.at(0), false), y = coord(a.at(1), true);
            const int hold = cmd == "tap" ? 3 : 50;
            finger(SDL_EVENT_FINGER_DOWN, 1, x, y, 0);
            finger(SDL_EVENT_FINGER_UP, 1, x, y, hold);
            waitFrames_ = hold + 2;
        } else if(cmd == "drag")
        {
            const float x0 = coord(a.at(0), false), y0 = coord(a.at(1), true);
            const float x1 = coord(a.at(2), false), y1 = coord(a.at(3), true);
            const int frames = static_cast<int>(num(4, 20));
            finger(SDL_EVENT_FINGER_DOWN, 1, x0, y0, 0);
            for(int i = 1; i <= frames; ++i)
            {
                const float t = static_cast<float>(i) / frames;
                finger(SDL_EVENT_FINGER_MOTION, 1, x0 + (x1 - x0) * t, y0 + (y1 - y0) * t, i);
            }
            finger(SDL_EVENT_FINGER_UP, 1, x1, y1, frames + 1);
            waitFrames_ = frames + 3;
        } else if(cmd == "pinch")
        {
            const float cx = coord(a.at(0), false), cy = coord(a.at(1), true);
            const float d0 = std::stof(a.at(2)), d1 = std::stof(a.at(3));
            const int frames = static_cast<int>(num(4, 20));
            finger(SDL_EVENT_FINGER_DOWN, 1, cx - d0 / 2, cy, 0);
            finger(SDL_EVENT_FINGER_DOWN, 2, cx + d0 / 2, cy, 1);
            for(int i = 1; i <= frames; ++i)
            {
                const float d = d0 + (d1 - d0) * static_cast<float>(i) / frames;
                finger(SDL_EVENT_FINGER_MOTION, 1, cx - d / 2, cy, i + 1);
                finger(SDL_EVENT_FINGER_MOTION, 2, cx + d / 2, cy, i + 1);
            }
            finger(SDL_EVENT_FINGER_UP, 1, cx - d1 / 2, cy, frames + 2);
            finger(SDL_EVENT_FINGER_UP, 2, cx + d1 / 2, cy, frames + 2);
            waitFrames_ = frames + 4;
        } else if(cmd == "twotap")
        {
            const float x = coord(a.at(0), false), y = coord(a.at(1), true);
            finger(SDL_EVENT_FINGER_DOWN, 1, x - 40, y, 0);
            finger(SDL_EVENT_FINGER_DOWN, 2, x + 40, y, 1);
            finger(SDL_EVENT_FINGER_UP, 1, x - 40, y, 5);
            finger(SDL_EVENT_FINGER_UP, 2, x + 40, y, 6);
            waitFrames_ = 8;
        } else if(cmd == "click")
        {
            const float x = coord(a.at(0), false), y = coord(a.at(1), true);
            const Uint8 button = (a.size() > 2 && a[2] == "right") ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.x = x;
            ev.motion.y = y;
            schedule(0, ev);
            ev = SDL_Event{};
            ev.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            ev.button.button = button;
            ev.button.x = x;
            ev.button.y = y;
            ev.button.down = true;
            schedule(1, ev);
            ev.type = SDL_EVENT_MOUSE_BUTTON_UP;
            ev.button.down = false;
            schedule(3, ev);
            waitFrames_ = 5;
        } else if(cmd == "wheel")
        {
            SDL_Event ev{};
            ev.type = SDL_EVENT_MOUSE_WHEEL;
            ev.wheel.mouse_x = coord(a.at(0), false);
            ev.wheel.mouse_y = coord(a.at(1), true);
            ev.wheel.y = std::stof(a.at(2));
            schedule(0, ev);
            waitFrames_ = 2;
        } else if(cmd == "key")
        {
            SDL_Event ev{};
            ev.type = SDL_EVENT_KEY_DOWN;
            ev.key.key = SDL_GetKeyFromName(a.at(0).c_str());
            ev.key.down = true;
            schedule(0, ev);
            ev.type = SDL_EVENT_KEY_UP;
            ev.key.down = false;
            schedule(2, ev);
            waitFrames_ = 4;
        } else if(cmd == "text")
        {
            const std::string text = line.substr(line.find(' ') + 1);
            app_.post([this, text] {
                SDL_Event ev{};
                ev.type = SDL_EVENT_TEXT_INPUT;
                ev.text.text = SDL_strdup(text.c_str());
                SDL_PushEvent(&ev);
            });
            waitFrames_ = 2;
        } else if(cmd == "shot")
        {
            pendingShot_ = a.at(0);
            waitFrames_ = 1;
        } else if(cmd == "resize")
        {
            SDL_SetWindowSize(SDL_GL_GetCurrentWindow(), static_cast<int>(num(0, 1080)), static_cast<int>(num(1, 2400)));
            waitFrames_ = 3;
        } else if(cmd == "log")
            std::cout << "[script] " << line.substr(4) << std::endl;
        else if(cmd == "quit")
        {
            exitCode_ = static_cast<int>(num(0, 0));
            return false;
        } else
            std::cerr << "[script] unknown command: " << line << std::endl;
    } catch(const std::exception& e)
    {
        std::cerr << "[script] bad command '" << line << "': " << e.what() << std::endl;
        exitCode_ = 3;
        return false;
    }
    return true;
}

bool TestScript::step()
{
    ++frame_;
    for(auto it = scheduled_.begin(); it != scheduled_.end();)
    {
        if(it->first <= frame_)
        {
            SDL_Event ev = it->second;
            SDL_PushEvent(&ev);
            it = scheduled_.erase(it);
        } else
            ++it;
    }
    if(waitFrames_ > 0)
    {
        --waitFrames_;
        return true;
    }
    if(!waitFor_.empty())
    {
        float x, y;
        if(!resolve(waitFor_, x, y))
        {
            if(--waitForFrames_ > 0)
                return true;
            failShot();
            std::cerr << "[script] FAIL: timeout waiting for " << waitFor_ << std::endl;
            exitCode_ = 4;
        }
        waitFor_.clear();
    }
    while(next_ < lines_.size() && waitFrames_ == 0)
    {
        if(!execute(lines_[next_++]))
            return false;
    }
    return next_ < lines_.size() || waitFrames_ > 0 || !scheduled_.empty();
}

void TestScript::afterDraw()
{
    if(pendingShot_.empty())
        return;
    if(!app_.saveScreenshot(pendingShot_))
        std::cerr << "[script] screenshot failed: " << SDL_GetError() << std::endl;
    else
        std::cout << "[script] screenshot " << pendingShot_ << std::endl;
    pendingShot_.clear();
}

} // namespace weiler
