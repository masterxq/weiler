// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Hud.h"
#include "App.h"
#include "GameScreen.h"
#include "world/GameWorldViewer.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace weiler {

struct Hud::Impl
{
    GameScreen& game;
    App& app;
    struct Finger
    {
        float x, y, x0, y0;
    };
    std::map<uint64_t, Finger> fingers;
    float velX = 0, velY = 0;
    bool dragging = false;
    float pinchDist0 = 0, pinchZoom0 = 1;
    Impl(GameScreen& g, App& a) : game(g), app(a) {}
};

Hud::Hud(GameScreen& game, App& app) : d(std::make_unique<Impl>(game, app)) {}
Hud::~Hud() = default;

void Hud::update(double dt)
{
    if(d->fingers.empty() && (std::abs(d->velX) > 1 || std::abs(d->velY) > 1))
    {
        d->game.view().moveBy(d->velX * static_cast<float>(dt), d->velY * static_cast<float>(dt));
        const float decay = std::exp(-4.f * static_cast<float>(dt));
        d->velX *= decay;
        d->velY *= decay;
    }
}

void Hud::draw()
{
    auto& b = d->app.batch();
    const std::string fps = std::to_string(static_cast<int>(std::lround(d->app.fps()))) + " fps";
    d->app.font().draw(b, d->app.dp(8), d->app.dp(8), fps, d->app.dp(14), gfx::Color::rgb(0xFFFFFF));
    const float s = d->app.dp(140);
    d->game.minimap().draw(b, {d->app.width() - s - d->app.dp(8), d->app.dp(8), s, s * 0.6f});
}

void Hud::onPointer(const PointerEvent& ev)
{
    auto& view = d->game.view();
    if(ev.isMouse && ev.type == PointerEvent::Type::Move && ev.button == 0)
        return;
    switch(ev.type)
    {
        case PointerEvent::Type::Down:
            d->fingers[ev.id] = {ev.x, ev.y, ev.x, ev.y};
            d->velX = d->velY = 0;
            if(d->fingers.size() == 2)
            {
                auto it = d->fingers.begin();
                const auto& a = it->second;
                const auto& c = (++it)->second;
                d->pinchDist0 = std::hypot(a.x - c.x, a.y - c.y);
                d->pinchZoom0 = view.cam.zoom;
            }
            break;
        case PointerEvent::Type::Move:
        {
            auto it = d->fingers.find(ev.id);
            if(it == d->fingers.end())
                break;
            const float dx = ev.x - it->second.x, dy = ev.y - it->second.y;
            it->second.x = ev.x;
            it->second.y = ev.y;
            if(d->fingers.size() == 1)
            {
                view.moveBy(dx, dy);
                d->velX = dx * 60.f;
                d->velY = dy * 60.f;
            } else if(d->fingers.size() == 2)
            {
                auto f = d->fingers.begin();
                const auto& a = f->second;
                const auto& c = (++f)->second;
                const float dist = std::hypot(a.x - c.x, a.y - c.y);
                if(d->pinchDist0 > 1)
                {
                    const float z = std::clamp(d->pinchZoom0 * dist / d->pinchDist0, 0.25f, 8.f);
                    view.zoomAt((a.x + c.x) / 2, (a.y + c.y) / 2, z);
                    view.moveBy(dx / 2, dy / 2);
                }
            }
            break;
        }
        case PointerEvent::Type::Up:
        case PointerEvent::Type::Cancel:
            d->fingers.erase(ev.id);
            if(!d->fingers.empty())
                d->velX = d->velY = 0;
            break;
    }
}

void Hud::onWheel(float x, float y, float delta)
{
    auto& view = d->game.view();
    view.zoomAt(x, y, std::clamp(view.cam.zoom * std::pow(1.15f, delta), 0.25f, 8.f));
}

void Hud::onKey(SDL_Keycode key, bool down)
{
    if(!down)
        return;
    if(key == SDLK_SPACE)
        d->game.setPaused(!d->game.paused());
    else if(key == SDLK_PLUS || key == SDLK_KP_PLUS)
        d->game.setSpeedFactor(d->game.speedFactor() * 2);
    else if(key == SDLK_MINUS || key == SDLK_KP_MINUS)
        d->game.setSpeedFactor(d->game.speedFactor() / 2);
    else if(key == SDLK_F5)
        d->game.quickSave();
    else if(key == SDLK_F9)
        d->game.quickLoad();
}

bool Hud::onBack()
{
    return false;
}

void Hud::notifyDefeated(unsigned) {}
void Hud::notifyWinner(unsigned, bool) {}
void Hud::flagDestroyed(MapPoint) {}
void Hud::startRoad(MapPoint, bool) {}
void Hud::cancelRoad() {}

} // namespace weiler
