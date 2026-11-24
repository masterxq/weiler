// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Tutorial.h"
#include "Settings.h"
#include <array>

namespace weiler {

using namespace ui;

namespace {
struct Step
{
    const char* title;
    const char* text;
};
constexpr std::array<Step, 8> STEPS = {{
  {"Willkommen", "Diese kurze Einführung zeigt die Bedienung. Tippe auf Weiter oder überspringe sie."},
  {"Verschieben", "Ziehe mit einem Finger über die Landschaft, um dich umzusehen."},
  {"Zoomen", "Ziehe zwei Finger auseinander oder zusammen. Am PC geht das mit dem Mausrad."},
  {"Bauplätze", "Tippe rechts auf das Punkte-Symbol. Die Karte zeigt dann, wo Gebäude passen."},
  {"Bauen", "Tippe auf einen freien Bauplatz und wähle im Kreismenü ein Gebäude, zum Beispiel den Holzfäller."},
  {"Straßen", "Lege den Finger auf die Flagge vor deinem neuen Gebäude und ziehe zu einer anderen Flagge. "
              "Flaggen entlang der Straße setzt Weiler selbst."},
  {"Menü und Zurück", "Öffne das Menü oben links. Ein Tipp mit zwei Fingern schließt es wieder "
                      "(am PC Rechtsklick oder Esc)."},
  {"Fertig", "Mehr steckt in den Gebäuden: einfach antippen. Die Einführung findest du jederzeit im Menü "
             "unter Hilfe."},
}};
} // namespace

Tutorial::Tutorial(Hud::Impl& hud) : hud_(hud)
{
    stepTime_ = hud_.app.time();
}

void Tutorial::next()
{
    ++step_;
    stepTime_ = hud_.app.time();
    if(step_ >= STEPS.size())
        finish();
}

void Tutorial::finish()
{
    hud_.app.settings().setBool("tutorial.done", true);
    hud_.app.settings().save();
    hud_.app.post([&h = hud_] { h.tutorial.reset(); });
}

void Tutorial::update() {}

void Tutorial::onPan()
{
    if(step_ == 1 && (panAmount_ += 1) > 25)
        next();
}

void Tutorial::onZoom()
{
    if(step_ == 2 && (zoomAmount_ += 1) > 6)
        next();
}

void Tutorial::onBQToggled(bool on)
{
    if(step_ == 3 && on)
        next();
}

void Tutorial::onRingOpened(bool) {}

void Tutorial::onBuild(BuildingType)
{
    if(step_ == 4)
        next();
}

void Tutorial::onRoadBuilt()
{
    if(step_ == 5)
        next();
}

void Tutorial::onSheet(SheetKind k)
{
    if(step_ == 6 && k == SheetKind::GameMenu)
        menuOpened_ = true;
}

void Tutorial::onBack()
{
    if(step_ == 6 && menuOpened_)
        hud_.app.post([this] { next(); });
}

void Tutorial::draw()
{
    if(step_ >= STEPS.size())
        return;
    Ui& ui = hud_.ui;
    App& app = hud_.app;
    const float W = static_cast<float>(app.width());
    const float w = std::min(W - app.dp(20), app.dp(440));
    const float fs = app.dp(15);
    const auto lines = app.font().wrap(STEPS[step_].text, fs, w - app.dp(36));
    const float h = app.dp(62) + static_cast<float>(lines.size()) * fs * 1.3f + app.dp(56);
    const float y = app.dp(68);
    const float t = Ui::ease(std::min(1.f, static_cast<float>((app.time() - stepTime_) / 0.25)));
    const Rect2 r{(W - w) / 2, y - (1 - t) * app.dp(12), w, h};
    ui.panel(r, app.dp(20), ui.theme.panelSolid.withAlpha(0.94f));
    // Progress dots
    for(size_t i = 0; i < STEPS.size(); ++i)
        app.batch().circle(r.x + app.dp(22) + static_cast<float>(i) * app.dp(14), r.y + app.dp(20), app.dp(4),
                           i <= step_ ? ui.theme.accent : Color::rgb(0xFFFFFF, 0.2f));
    ui.text(r.x + app.dp(18), r.y + app.dp(30), STEPS[step_].title, app.dp(19), ui.theme.text, gfx::Align::Left, true);
    float ty = r.y + app.dp(58);
    for(const auto& l : lines)
    {
        ui.text(r.x + app.dp(18), ty, l, fs, ui.theme.textDim);
        ty += fs * 1.3f;
    }
    const float bh = app.dp(44);
    const float by = r.bottom() - bh - app.dp(12);
    const bool manual = step_ == 0 || step_ + 1 == STEPS.size();
    if(manual)
    {
        if(ui.button(id("tut.next"), {r.right() - app.dp(140), by, app.dp(124), bh},
                     step_ + 1 == STEPS.size() ? "Los geht's" : "Weiter", Icon::None, Style::Primary))
            next();
    }
    if(step_ + 1 < STEPS.size())
    {
        if(ui.button(id("tut.skip"), {r.x + app.dp(12), by, app.dp(140), bh}, "Überspringen", Icon::None, Style::Flat))
            finish();
    }
}

} // namespace weiler
