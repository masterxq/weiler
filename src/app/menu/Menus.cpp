// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Menus.h"
#include "App.h"
#include "Screen.h"

namespace weiler {

namespace {
class MessageScreen : public Screen
{
public:
    MessageScreen(App& app, std::string title, std::string text) : Screen(app), title_(std::move(title)), text_(std::move(text)) {}
    void update(double) override {}
    void draw() override
    {
        auto& b = app_.batch();
        const float pad = app_.dp(24);
        app_.fontBold().draw(b, pad, pad * 2, title_, app_.dp(28), gfx::Color::rgb(0xF2EEE8));
        app_.font().drawWrapped(b, pad, pad * 2 + app_.dp(48), app_.width() - 2 * pad, text_, app_.dp(16),
                                gfx::Color::rgb(0xC9C3BA));
    }
    void onPointer(const PointerEvent&) override {}
    bool onBack() override
    {
        app_.quit();
        return true;
    }

private:
    std::string title_, text_;
};
} // namespace

std::unique_ptr<Screen> createMissingDataScreen(App& app)
{
    return std::make_unique<MessageScreen>(
      app, "Weiler",
      "Weiler needs the data of the Gold Edition.\n\nCopy the game folder (the one that contains DATA and GFX) to\n"
        + app.paths().gameData.string()
        + "\nor start Weiler with --data <folder> or set WEILER_DATA.\n\nProblem: " + app.paths().missingGameDataReason());
}

std::unique_ptr<Screen> createMainMenu(App& app)
{
    return std::make_unique<MessageScreen>(app, "Weiler", "Main menu follows.");
}

} // namespace weiler
