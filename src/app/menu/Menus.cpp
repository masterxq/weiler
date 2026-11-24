// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Menus.h"
#include "App.h"
#include "Audio.h"
#include "RttrConfig.h"
#include "Campaign.h"
#include "Screen.h"
#include "Settings.h"
#include "files.h"
#include "game/GameScreen.h"
#include "ui/Ui.h"
#include "libsiedler2/Archiv.h"
#include "libsiedler2/ArchivItem_Map.h"
#include "libsiedler2/ArchivItem_Map_Header.h"
#include "libsiedler2/prototypen.h"
#include "s25util/utf8.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <map>

namespace bfs = boost::filesystem;

namespace weiler {

using namespace ui;

namespace {

/// Own background: dusky gradient with soft hills
void drawBackground(App& app)
{
    auto& b = app.batch();
    const float W = static_cast<float>(app.width()), H = static_cast<float>(app.height());
    const int bands = 24;
    for(int i = 0; i < bands; ++i)
    {
        const float t = static_cast<float>(i) / bands;
        const Color c{0.07f + 0.10f * t, 0.075f + 0.06f * t, 0.09f + 0.02f * t, 1};
        b.quad(0, 0, H * t, W, H * (t + 1.f / bands) + 1, 0, 0, 1, 1, c);
    }
    const float tm = static_cast<float>(app.time());
    // Low evening sun, away from the title
    const float sunX = W * (app.portrait() ? 0.82f : 0.86f), sunY = H * (app.portrait() ? 0.27f : 0.2f);
    b.circle(sunX, sunY, std::min(W, H) * 0.07f, Color::rgb(0xF2C27A, 0.9f), 0, app.dp(3));
    b.circle(sunX, sunY, std::min(W, H) * 0.22f, Color::rgb(0xE8A33D, 0.10f), 0, app.dp(120));
    const struct
    {
        float x, y, r;
        uint32_t col;
    } hills[] = {{0.15f, 1.02f, 0.55f, 0x2E3A2C}, {0.75f, 1.05f, 0.60f, 0x273226}, {0.45f, 1.18f, 0.62f, 0x1E271D},
                 {0.0f, 1.25f, 0.5f, 0x182018},   {0.95f, 1.28f, 0.55f, 0x151C15}};
    for(const auto& h : hills)
    {
        const float r = std::max(W, H) * h.r;
        b.circle(W * h.x + std::sin(tm * 0.05f + h.x * 6) * app.dp(6), H * h.y, r, Color::rgb(h.col), 0, app.dp(2));
    }
}

class MenuScreen : public Screen
{
public:
    explicit MenuScreen(App& app) : Screen(app), ui_(app.ui()) {}
    void update(double) override {}
    void onPointer(const PointerEvent& ev) override { ui_.pointer(ev); }
    void onWheel(float x, float y, float d) override { ui_.wheel(x, y, d); }

protected:
    /// Content column (centered, limited width)
    Rect2 column(float top) const
    {
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        const float w = std::min(W - app_.dp(32), app_.dp(460));
        return {(W - w) / 2, top, w, H - top - app_.dp(16)};
    }
    void title(const std::string& t, const std::string& sub = {})
    {
        const float y = app_.portrait() ? app_.dp(70) : app_.dp(28);
        ui_.text(static_cast<float>(app_.width()) / 2, y, t, app_.dp(46), ui_.theme.text, gfx::Align::Center, true);
        if(!sub.empty())
            ui_.text(static_cast<float>(app_.width()) / 2, y + app_.dp(62), sub, app_.dp(15), ui_.theme.textDim,
                     gfx::Align::Center);
    }
    bool backButton()
    {
        const float s = app_.dp(48);
        return ui_.iconButton(id("menu.back"), {app_.dp(12), app_.dp(12), s, s}, Icon::Back);
    }
    Ui& ui_;
};

std::string savePath(const std::string& stem)
{
    return (RTTRCONFIG.expandPath(s25::folders::save) / (stem + ".sav")).string();
}

void startLoaded(App& app, const std::string& path)
{
    app.post([&app, path] {
        try
        {
            app.setScreen(createGameScreen(app, Session::loadGame(path, static_cast<unsigned>(std::time(nullptr)))));
        } catch(const std::exception& e)
        {
            std::cerr << "Loading failed: " << e.what() << std::endl;
        }
    });
}

// ------------------------------------------------------------------------------------------------------------------
struct MapEntry
{
    bfs::path path;
    std::string name;
    unsigned w = 0, h = 0, players = 0, gfx = 0;
};

std::vector<MapEntry> scanMaps(const DataPaths& paths)
{
    std::vector<MapEntry> maps;
    const std::vector<bfs::path> dirs = {paths.gameData / "DATA" / "MAPS4", paths.gameData / "DATA" / "MAPS3",
                                         paths.gameData / "DATA" / "MAPS2", paths.gameData / "WORLDS",
                                         paths.userData / "WORLDS"};
    for(const auto& dir : dirs)
    {
        if(!bfs::is_directory(dir))
            continue;
        std::vector<bfs::path> files;
        for(const auto& e : bfs::directory_iterator(dir))
        {
            std::string ext = e.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
            if(ext == ".WLD" || ext == ".SWD")
                files.push_back(e.path());
        }
        std::sort(files.begin(), files.end());
        for(const auto& f : files)
        {
            libsiedler2::Archiv a;
            if(libsiedler2::loader::loadMAP(f, a, true) != 0 || !a.get(0))
                continue;
            const auto* map = dynamic_cast<const libsiedler2::ArchivItem_Map*>(a.get(0));
            if(!map)
                continue;
            const auto& h = map->getHeader();
            MapEntry m;
            m.path = f;
            m.name = s25util::ansiToUTF8(h.getName());
            m.w = h.getWidth();
            m.h = h.getHeight();
            m.players = h.getNumPlayers();
            m.gfx = h.getGfxSet();
            if(m.players >= 1)
                maps.push_back(m);
        }
    }
    return maps;
}

class NewGameScreen : public MenuScreen
{
public:
    explicit NewGameScreen(App& app) : MenuScreen(app), maps_(scanMaps(app.paths()))
    {
        auto& st = app.settings();
        selected_ = std::clamp(st.getInt("newgame.map", 0), 0, std::max(0, static_cast<int>(maps_.size()) - 1));
        ais_ = st.getInt("newgame.ais", 2);
        level_ = st.getInt("newgame.level", 1);
        wares_ = st.getInt("newgame.wares", 2);
        goal_ = st.getInt("newgame.goal", 0);
        nation_ = st.getInt("newgame.nation", 0);
    }
    void draw() override
    {
        drawBackground(app_);
        if(backButton())
        {
            app_.setScreen(createMainMenu(app_));
            return;
        }
        const bool land = !app_.portrait();
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        const float m = app_.dp(14);
        ui_.text(W / 2, app_.dp(18), "Neues Spiel", app_.dp(24), ui_.theme.text, gfx::Align::Center, true);
        Rect2 listR, optR;
        if(land)
        {
            listR = {m + app_.dp(56), app_.dp(70), (W - app_.dp(56)) * 0.48f, H - app_.dp(84)};
            optR = {listR.right() + m, app_.dp(70), W - listR.right() - 2 * m, H - app_.dp(84)};
        } else
        {
            listR = {m, app_.dp(70), W - 2 * m, (H - app_.dp(84)) * 0.38f};
            optR = {m, listR.bottom() + m, W - 2 * m, H - listR.bottom() - 2 * m};
        }
        // Map list
        ui_.panel(listR, app_.dp(20));
        const Rect2 view = listR.inset(app_.dp(8));
        float y = ui_.beginScroll(id("maps"), view);
        const float y0 = y;
        const float rowH = app_.dp(58);
        for(size_t i = 0; i < maps_.size(); ++i)
        {
            const MapEntry& me = maps_[i];
            const Rect2 row{view.x, y, view.w - app_.dp(6), rowH - app_.dp(4)};
            const bool sel = static_cast<int>(i) == selected_;
            if(sel || ui_.pressed(id("map", i), row))
                app_.batch().roundRect(row, app_.dp(12), sel ? ui_.theme.accent.withAlpha(0.9f) : ui_.theme.raised);
            const Color fg = sel ? ui_.theme.accentText : ui_.theme.text;
            ui_.text(row.x + app_.dp(14), row.y + app_.dp(7), me.name, app_.dp(16), fg, gfx::Align::Left, true);
            static const char* worlds[] = {"Grünland", "Ödland", "Winterwelt"};
            ui_.text(row.x + app_.dp(14), row.y + app_.dp(29),
                     std::to_string(me.w) + "×" + std::to_string(me.h) + " · " + std::to_string(me.players)
                       + " Spieler · " + worlds[std::min(2u, me.gfx)],
                     app_.dp(12.5f), sel ? fg : ui_.theme.textDim);
            if(ui_.area(id("map", i), row))
                selected_ = static_cast<int>(i);
            y += rowH;
        }
        ui_.endScroll(y - y0);
        // Options
        ui_.panel(optR, app_.dp(20));
        const Rect2 c = optR.inset(app_.dp(14));
        // Options scroll when the screen is short (landscape phones), the start button stays visible
        const Rect2 startR{c.x, c.bottom() - app_.dp(56), c.w, app_.dp(56)};
        const Rect2 optView{c.x, c.y, c.w, startR.y - app_.dp(10) - c.y};
        float oy = ui_.beginScroll(id("opts"), optView);
        const float oy0 = oy;
        const int maxAIs = maps_.empty() ? 0 : static_cast<int>(maps_[selected_].players) - 1;
        ais_ = std::clamp(ais_, 0, std::max(0, maxAIs));
        const float rh = app_.dp(44);
        ui_.stepper(id("opt.ais"), {c.x, oy, c.w, rh}, ais_, 0, std::max(0, maxAIs), "Gegner");
        oy += rh + app_.dp(10);
        ui_.text(c.x, oy, "Stärke der Gegner", app_.dp(13), ui_.theme.textDim);
        ui_.segmented(id("opt.level"), {c.x, oy + app_.dp(18), c.w, app_.dp(40)}, {"Leicht", "Mittel", "Schwer"}, level_);
        oy += app_.dp(66);
        ui_.text(c.x, oy, "Startwaren", app_.dp(13), ui_.theme.textDim);
        ui_.segmented(id("opt.wares"), {c.x, oy + app_.dp(18), c.w, app_.dp(40)}, {"Sehr wenig", "Wenig", "Normal", "Viel"},
                      wares_);
        oy += app_.dp(66);
        ui_.text(c.x, oy, "Ziel", app_.dp(13), ui_.theme.textDim);
        ui_.segmented(id("opt.goal"), {c.x, oy + app_.dp(18), c.w, app_.dp(40)}, {"Frei", "3/4 Land", "Alles"}, goal_);
        oy += app_.dp(66);
        ui_.text(c.x, oy, "Volk", app_.dp(13), ui_.theme.textDim);
        ui_.segmented(id("opt.nation"), {c.x, oy + app_.dp(18), c.w, app_.dp(40)}, {"Römer", "Wikinger", "Nubier", "Japaner"},
                      nation_);
        oy += app_.dp(66);
        ui_.endScroll(oy - oy0);
        if(ui_.button(id("opt.start"), startR, "Spiel starten", Icon::Play, Style::Primary, !maps_.empty()))
            start();
    }
    bool onBack() override
    {
        app_.setScreen(createMainMenu(app_));
        return true;
    }

private:
    void start()
    {
        auto& st = app_.settings();
        st.setInt("newgame.map", selected_);
        st.setInt("newgame.ais", ais_);
        st.setInt("newgame.level", level_);
        st.setInt("newgame.wares", wares_);
        st.setInt("newgame.goal", goal_);
        st.setInt("newgame.nation", nation_);
        st.save();
        const MapEntry me = maps_[selected_];
        static const AI::Level levels[] = {AI::Level::Easy, AI::Level::Medium, AI::Level::Hard};
        NewGameSetup setup = makeSkirmishSetup(me.path, static_cast<unsigned>(ais_),
                                               static_cast<unsigned>(std::time(nullptr)), levels[level_], Nation(nation_));
        setup.ggs.startWares = static_cast<StartWares>(wares_);
        setup.ggs.objective = goal_ == 0 ? GameObjective::None :
                              goal_ == 1 ? GameObjective::Conquer3_4 : GameObjective::TotalDomination;
        setup.title = me.name;
        App& app = app_;
        app.post([&app, setup] {
            try
            {
                app.setScreen(createGameScreen(app, Session::newGame(setup)));
            } catch(const std::exception& e)
            {
                std::cerr << "Could not start game: " << e.what() << std::endl;
            }
        });
    }
    std::vector<MapEntry> maps_;
    int selected_ = 0, ais_ = 2, level_ = 1, wares_ = 2, goal_ = 0, nation_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------
class CampaignScreen : public MenuScreen
{
public:
    explicit CampaignScreen(App& app) : MenuScreen(app), campaigns_(loadCampaigns())
    {
        sel_ = std::clamp(app.settings().getInt("campaign.selected", 0), 0,
                          std::max(0, static_cast<int>(campaigns_.size()) - 1));
    }
    void draw() override
    {
        drawBackground(app_);
        if(backButton())
        {
            app_.setScreen(createMainMenu(app_));
            return;
        }
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        const float m = app_.dp(14);
        ui_.text(W / 2, app_.dp(18), "Kampagne", app_.dp(24), ui_.theme.text, gfx::Align::Center, true);
        if(campaigns_.empty())
        {
            ui_.textWrapped(m, app_.dp(90), W - 2 * m, "Keine Kampagne gefunden (Karten der Gold Edition fehlen).",
                            app_.dp(16), ui_.theme.textDim, gfx::Align::Center);
            return;
        }
        float top = app_.dp(70);
        if(campaigns_.size() > 1)
        {
            std::vector<std::string> names;
            for(const auto& c : campaigns_)
                names.push_back(c.desc.name);
            const float w = std::min(W - 2 * m, app_.dp(520));
            if(ui_.segmented(id("camp.sel"), {(W - w) / 2, top, w, app_.dp(42)}, names, sel_))
                app_.settings().setInt("campaign.selected", sel_);
            top += app_.dp(54);
        }
        const Campaign& c = campaigns_[static_cast<size_t>(sel_)];
        Rect2 descR, listR;
        if(app_.portrait())
        {
            descR = {m, top, W - 2 * m, (H - top) * 0.36f};
            listR = {m, descR.bottom() + m, W - 2 * m, H - descR.bottom() - 2 * m};
        } else
        {
            listR = {m + app_.dp(56), top, (W - app_.dp(56)) * 0.46f, H - top - m};
            descR = {listR.right() + m, top, W - listR.right() - 2 * m, H - top - m};
        }
        // Story
        ui_.panel(descR, app_.dp(20));
        const Rect2 dv = descR.inset(app_.dp(16));
        float y = ui_.beginScroll(id("camp.desc", static_cast<uint64_t>(sel_)), dv);
        const float h = ui_.textWrapped(dv.x, y, dv.w - app_.dp(8), reflowText(c.desc.longDescription), app_.dp(15),
                                        ui_.theme.text);
        ui_.endScroll(h);
        // Chapters
        ui_.panel(listR, app_.dp(20));
        const Rect2 lv = listR.inset(app_.dp(8));
        y = ui_.beginScroll(id("camp.list", static_cast<uint64_t>(sel_)), lv);
        const float y0 = y;
        const int last = app_.settings().getInt("campaign." + c.id + ".last", 0);
        const float rowH = app_.dp(60);
        for(size_t i = 0; i < c.missionTitles.size(); ++i)
        {
            const Rect2 row{lv.x, y, lv.w - app_.dp(6), rowH - app_.dp(6)};
            const bool hl = static_cast<int>(i) == last;
            if(hl || ui_.pressed(id("camp.m", i), row))
                app_.batch().roundRect(row, app_.dp(12), hl ? ui_.theme.accent.withAlpha(0.9f) : ui_.theme.raised);
            const Color fg = hl ? ui_.theme.accentText : ui_.theme.text;
            ui_.text(row.x + app_.dp(14), row.y + app_.dp(7), "Kapitel " + std::to_string(i + 1), app_.dp(12.5f),
                     hl ? fg : ui_.theme.textDim);
            ui_.text(row.x + app_.dp(14), row.y + app_.dp(24), c.missionTitles[i], app_.dp(16), fg, gfx::Align::Left,
                     true);
            ui_.icon(Icon::Play, row.right() - app_.dp(26), row.y + row.h / 2, app_.dp(20), fg);
            ui_.tag("Kapitel " + std::to_string(i + 1), row);
            if(ui_.area(id("camp.m", i), row))
                start(c, i);
            y += rowH;
        }
        ui_.endScroll(y - y0);
    }
    bool onBack() override
    {
        app_.setScreen(createMainMenu(app_));
        return true;
    }

private:
    void start(const Campaign& c, size_t mission)
    {
        app_.settings().setInt("campaign." + c.id + ".last", static_cast<int>(mission));
        app_.settings().save();
        App& app = app_;
        const Campaign campaign = c;
        app.post([&app, campaign, mission] {
            try
            {
                NewGameSetup setup =
                  makeMissionSetup(campaign, mission, "Du", static_cast<unsigned>(std::time(nullptr)));
                app.setScreen(createGameScreen(app, Session::newGame(setup)));
            } catch(const std::exception& e)
            {
                std::cerr << "Could not start mission: " << e.what() << std::endl;
            }
        });
    }
    std::vector<Campaign> campaigns_;
    int sel_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------
class LoadScreen : public MenuScreen
{
public:
    using MenuScreen::MenuScreen;
    void draw() override
    {
        drawBackground(app_);
        if(backButton())
        {
            app_.setScreen(createMainMenu(app_));
            return;
        }
        ui_.text(static_cast<float>(app_.width()) / 2, app_.dp(18), "Spielstand laden", app_.dp(24), ui_.theme.text,
                 gfx::Align::Center, true);
        const Rect2 col = column(app_.dp(76));
        ui_.panel(col, app_.dp(20));
        const Rect2 view = col.inset(app_.dp(10));
        float y = ui_.beginScroll(id("load.list"), view);
        const float y0 = y;
        std::vector<bfs::path> files;
        const bfs::path dir = RTTRCONFIG.expandPath(s25::folders::save);
        if(bfs::is_directory(dir))
        {
            for(const auto& e : bfs::directory_iterator(dir))
            {
                if(e.path().extension() == ".sav")
                    files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end(),
                  [](const bfs::path& a, const bfs::path& b) { return bfs::last_write_time(a) > bfs::last_write_time(b); });
        if(files.empty())
            ui_.text(view.x + app_.dp(8), y + app_.dp(8), "Keine Spielstände vorhanden", app_.dp(16), ui_.theme.textDim);
        for(size_t i = 0; i < files.size(); ++i)
        {
            const Rect2 row{view.x, y, view.w - app_.dp(6), app_.dp(58)};
            app_.batch().roundRect(row, app_.dp(12),
                                   ui_.pressed(id("lf", i), row) ? ui_.theme.raised : Color::rgb(0xFFFFFF, 0.05f));
            std::string name = files[i].stem().string();
            if(name == "autosave")
                name = "Automatisch gesichert";
            else if(name == "quicksave")
                name = "Schnellspeicher";
            else if(name.rfind("slot", 0) == 0)
                name = "Platz " + name.substr(4);
            ui_.icon(Icon::Folder, row.x + app_.dp(24), row.y + row.h / 2, app_.dp(22), ui_.theme.accent);
            ui_.text(row.x + app_.dp(48), row.y + app_.dp(8), name, app_.dp(16), ui_.theme.text, gfx::Align::Left, true);
            ui_.text(row.x + app_.dp(48), row.y + app_.dp(30), saveDescription(files[i]), app_.dp(13),
                     ui_.theme.textDim);
            ui_.tag(name, row);
            if(ui_.area(id("lf", i), row))
                startLoaded(app_, files[i].string());
            y += app_.dp(64);
        }
        ui_.endScroll(y - y0);
    }
    bool onBack() override
    {
        app_.setScreen(createMainMenu(app_));
        return true;
    }
};

// ------------------------------------------------------------------------------------------------------------------
class SettingsScreen : public MenuScreen
{
public:
    using MenuScreen::MenuScreen;
    void draw() override
    {
        drawBackground(app_);
        if(backButton())
        {
            app_.settings().save();
            app_.setScreen(createMainMenu(app_));
            return;
        }
        ui_.text(static_cast<float>(app_.width()) / 2, app_.dp(18), "Einstellungen", app_.dp(24), ui_.theme.text,
                 gfx::Align::Center, true);
        const Rect2 col = column(app_.dp(76));
        const Rect2 p{col.x, col.y, col.w, app_.dp(300)};
        ui_.panel(p, app_.dp(20));
        const Rect2 c = p.inset(app_.dp(18));
        auto& st = app_.settings();
        float y = c.y;
        ui_.text(c.x, y, "Geräusche", app_.dp(15), ui_.theme.text);
        float ev = app_.audio().effectVolume();
        if(ui_.slider(id("s.fx"), {c.x, y + app_.dp(22), c.w, app_.dp(40)}, ev))
        {
            app_.audio().setEffectVolume(ev);
            st.setFloat("volume.effects", ev);
        }
        y += app_.dp(76);
        ui_.text(c.x, y, "Musik", app_.dp(15), ui_.theme.text);
        float mv = app_.audio().musicVolume();
        if(ui_.slider(id("s.mu"), {c.x, y + app_.dp(22), c.w, app_.dp(40)}, mv))
        {
            app_.audio().setMusicVolume(mv);
            st.setFloat("volume.music", mv);
        }
        y += app_.dp(76);
        ui_.textWrapped(c.x, y, c.w, "Spieldaten: " + app_.paths().gameData.string(), app_.dp(12), ui_.theme.textDim);
    }
    bool onBack() override
    {
        app_.settings().save();
        app_.setScreen(createMainMenu(app_));
        return true;
    }
};

// ------------------------------------------------------------------------------------------------------------------
class MainMenuScreen : public MenuScreen
{
public:
    using MenuScreen::MenuScreen;
    void draw() override
    {
        drawBackground(app_);
        const std::string autosave = savePath("autosave");
        const bool canContinue = bfs::exists(autosave);
        const float W = static_cast<float>(app_.width()), H = static_cast<float>(app_.height());
        Rect2 col;
        float bh = app_.dp(58), gap = app_.dp(12);
        if(app_.portrait())
        {
            title("Weiler", "Siedeln, bauen, handeln");
            col = column(H * 0.36f);
        } else
        {
            // Title on the left, buttons on the right; buttons shrink on short screens
            const float tx = W * 0.27f, ty = H / 2 - app_.dp(46);
            ui_.text(tx, ty, "Weiler", app_.dp(46), ui_.theme.text, gfx::Align::Center, true);
            ui_.text(tx, ty + app_.dp(62), "Siedeln, bauen, handeln", app_.dp(15), ui_.theme.textDim, gfx::Align::Center);
            const int n = canContinue ? 7 : 6;
            gap = app_.dp(10);
            bh = std::min(bh, (H - app_.dp(40) - gap * static_cast<float>(n - 1)) / static_cast<float>(n));
            const float w = std::min(W * 0.42f, app_.dp(420));
            const float total = bh * static_cast<float>(n) + gap * static_cast<float>(n - 1);
            col = {W * 0.73f - w / 2, (H - total) / 2 - app_.dp(8), w, total};
        }
        float y = col.y;
        auto btn = [&](const char* key, const std::string& label, Icon ic, Style st = Style::Normal) {
            const bool r = ui_.button(id(key), {col.x, y, col.w, bh}, label, ic, st);
            y += bh + gap;
            return r;
        };
        if(canContinue && btn("mm.cont", "Fortsetzen", Icon::Play, Style::Primary))
            startLoaded(app_, autosave);
        if(btn("mm.new", "Neues Spiel", Icon::Flag, canContinue ? Style::Normal : Style::Primary))
            app_.setScreen(std::make_unique<NewGameScreen>(app_));
        if(btn("mm.camp", "Kampagne", Icon::Trophy))
            app_.setScreen(std::make_unique<CampaignScreen>(app_));
        if(btn("mm.tut", "Einführung", Icon::Help))
            startTutorialGame(app_);
        if(btn("mm.load", "Laden", Icon::Folder))
            app_.setScreen(std::make_unique<LoadScreen>(app_));
        if(btn("mm.set", "Einstellungen", Icon::Gear))
            app_.setScreen(std::make_unique<SettingsScreen>(app_));
        if(btn("mm.quit", "Beenden", Icon::Exit))
            app_.quit();
        ui_.text(app_.portrait() ? W / 2 : W * 0.27f, H - app_.dp(28),
                 "Benötigt die Daten der Gold Edition · GPL", app_.dp(11), ui_.theme.textDim.withAlpha(0.7f),
                 gfx::Align::Center);
    }
    bool onBack() override
    {
        app_.quit();
        return true;
    }
};

class MessageScreen : public MenuScreen
{
public:
    MessageScreen(App& app, std::string title, std::string text)
        : MenuScreen(app), title_(std::move(title)), text_(std::move(text))
    {}
    void draw() override
    {
        drawBackground(app_);
        title(title_);
        const Rect2 col = column(app_.portrait() ? app_.dp(170) : app_.dp(110));
        const float h =
          static_cast<float>(app_.font().wrap(text_, app_.dp(16), col.w - app_.dp(40)).size()) * app_.dp(16) * 1.3f
          + app_.dp(110);
        const Rect2 p{col.x, col.y, col.w, h};
        ui_.panel(p, app_.dp(20));
        ui_.textWrapped(p.x + app_.dp(20), p.y + app_.dp(20), p.w - app_.dp(40), text_, app_.dp(16), ui_.theme.text);
        if(ui_.button(id("msg.quit"), {p.x + app_.dp(20), p.bottom() - app_.dp(70), p.w - app_.dp(40), app_.dp(52)},
                      "Beenden", Icon::Exit))
            app_.quit();
    }
    bool onBack() override
    {
        app_.quit();
        return true;
    }

private:
    std::string title_, text_;
};

} // namespace

void startTutorialGame(App& app, bool firstStart)
{
    // Small green map without opponents
    const auto maps = scanMaps(app.paths());
    const MapEntry* best = nullptr;
    for(const auto& m : maps)
    {
        if(m.gfx == 0 && (!best || m.w * m.h < best->w * best->h))
            best = &m;
    }
    if(!best && !maps.empty())
        best = &maps.front();
    if(!best)
        return;
    NewGameSetup setup = makeSkirmishSetup(best->path, 0, 1, AI::Level::Easy, Nation::Romans);
    setup.title = best->name;
    app.post([&app, setup, firstStart] {
        try
        {
            auto screen = std::make_unique<GameScreen>(app, Session::newGame(setup));
            screen->startTutorial(firstStart);
            app.setScreen(std::move(screen));
        } catch(const std::exception& e)
        {
            std::cerr << "Could not start tutorial: " << e.what() << std::endl;
        }
    });
}

std::unique_ptr<Screen> createMissingDataScreen(App& app)
{
    return std::make_unique<MessageScreen>(
      app, "Weiler",
      "Weiler benötigt die Daten der Gold Edition.\n\nKopiere den Spielordner (der DATA und GFX enthält) nach\n"
        + app.paths().gameData.string()
        + "\noder starte Weiler mit --data <Ordner> bzw. setze WEILER_DATA.\n\nGrund: "
        + app.paths().missingGameDataReason());
}

std::string reflowText(const std::string& text)
{
    // Texts of the original are wrapped by hand for fixed windows: single line breaks become spaces,
    // empty lines stay paragraph breaks
    std::string out;
    size_t i = 0;
    while(i < text.size())
    {
        if(text[i] == '\n')
        {
            size_t n = 0;
            while(i < text.size() && (text[i] == '\n' || text[i] == ' '))
                n += text[i++] == '\n';
            if(!out.empty() && i < text.size())
                out += n >= 2 ? "\n\n" : " ";
            continue;
        }
        out += text[i++];
    }
    while(!out.empty() && (out.back() == ' ' || out.back() == '\n'))
        out.pop_back();
    return out;
}

std::string saveDescription(const bfs::path& path)
{
    static std::map<std::string, std::pair<std::time_t, std::string>> cache;
    boost::system::error_code ec;
    const std::time_t t = bfs::last_write_time(path, ec);
    if(ec)
        return {};
    auto& entry = cache[path.string()];
    if(entry.first != t || entry.second.empty())
    {
        char buf[64];
        std::strftime(buf, sizeof(buf), "%d.%m. %H:%M", std::localtime(&t));
        const std::string info = Session::describeSave(path);
        entry = {t, info.empty() ? std::string(buf) : std::string(buf) + " · " + info};
    }
    return entry.second;
}

std::unique_ptr<Screen> createMainMenu(App& app)
{
    return std::make_unique<MainMenuScreen>(app);
}

} // namespace weiler
