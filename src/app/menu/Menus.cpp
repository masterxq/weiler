// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Menus.h"
#include "App.h"
#include "MenuBackdrop.h"
#include "Audio.h"
#include "Music.h"
#include "CdImport.h"
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

/// Background of all menu screens: the drifting map (or a gradient without game data)
void drawBackground(App& app)
{
    app.menuBackdrop().draw();
}

class MenuScreen : public Screen
{
public:
    explicit MenuScreen(App& app) : Screen(app), ui_(app.ui()) { app.music().play(Music::Context::Menu); }
    void update(double dt) override { app_.menuBackdrop().update(dt); }
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
    void title(const std::string& t)
    {
        const float y = app_.portrait() ? app_.dp(80) : app_.dp(30);
        ui_.heading(static_cast<float>(app_.width()) / 2, y, t, app_.dp(ui_.theme.type.headline), ui_.theme.text,
                    gfx::Align::Center);
    }
    /// Title of a sub screen, centered on the height of the back button
    void screenTitle(const std::string& t)
    {
        const float fs = app_.dp(ui_.theme.type.title + 2);
        ui_.heading(static_cast<float>(app_.width()) / 2, app_.dp(14) + (app_.dp(48) - fs * 1.3f) / 2, t, fs,
                    ui_.theme.text, gfx::Align::Center);
    }
    bool backButton()
    {
        const float s = app_.dp(48);
        return ui_.iconButton(id("menu.back"), {app_.dp(14), app_.dp(14), s, s}, Icon::Back);
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
            app.releaseMenuBackdrop();
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
        screenTitle("Neues Spiel");
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
            const bool rowHit = ui_.row(id("map", i), row, sel);
            const Color fg = ui_.theme.text;
            ui_.text(row.x + app_.dp(14), row.y + app_.dp(7), me.name, app_.dp(16), fg, gfx::Align::Left, true);
            static const char* worlds[] = {"Grünland", "Ödland", "Winterwelt"};
            ui_.text(row.x + app_.dp(14), row.y + app_.dp(29),
                     std::to_string(me.w) + "×" + std::to_string(me.h) + " · " + std::to_string(me.players)
                       + " Spieler · " + worlds[std::min(2u, me.gfx)],
                     app_.dp(12.5f), ui_.theme.textDim);
            if(rowHit)
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
                app.releaseMenuBackdrop();
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
        screenTitle("Kampagne");
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
            const bool rowHit = ui_.row(id("camp.m", i), row, hl);
            ui_.text(row.x + app_.dp(18), row.y + app_.dp(7), "Kapitel " + std::to_string(i + 1), app_.dp(12.5f),
                     hl ? ui_.theme.accent : ui_.theme.textDim);
            ui_.text(row.x + app_.dp(18), row.y + app_.dp(24), c.missionTitles[i], app_.dp(16), ui_.theme.text,
                     gfx::Align::Left, true);
            ui_.icon(Icon::Play, row.right() - app_.dp(26), row.y + row.h / 2, app_.dp(18),
                     hl ? ui_.theme.accent : ui_.theme.textFaint);
            ui_.tag("Kapitel " + std::to_string(i + 1), row);
            if(rowHit)
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
                app.releaseMenuBackdrop();
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
        screenTitle("Spielstand laden");
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
            const bool rowHit = ui_.row(id("lf", i), row);
            std::string name = files[i].stem().string();
            if(name == "autosave")
                name = "Automatisch gesichert";
            else if(name == "quicksave")
                name = "Schnellspeicher";
            else if(name.rfind("slot", 0) == 0)
                name = "Platz " + name.substr(4);
            float tx = row.x + app_.dp(48);
            bfs::path png = files[i];
            png.replace_extension(".png");
            if(const gfx::Texture* thumb = app_.imageTexture(png.string()))
            {
                const float th = row.h - app_.dp(12);
                ui_.picture(*thumb, {row.x + app_.dp(10), row.y + app_.dp(6), th * 1.5f, th});
                tx = row.x + app_.dp(20) + th * 1.5f;
            } else
                ui_.icon(Icon::Folder, row.x + app_.dp(24), row.y + row.h / 2, app_.dp(22), ui_.theme.accent);
            ui_.text(tx, row.y + app_.dp(8), name, app_.dp(16), ui_.theme.text, gfx::Align::Left, true);
            ui_.text(tx, row.y + app_.dp(30), saveDescription(files[i]), app_.dp(13), ui_.theme.textDim);
            ui_.tag(name, row);
            if(rowHit)
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
        screenTitle("Einstellungen");
        const Rect2 col = column(app_.dp(76));
        // The panel takes the content height of the last frame; scrolls on short screens
        const float maxH = static_cast<float>(app_.height()) - col.y - app_.dp(16);
        const Rect2 p{col.x, col.y, col.w, std::min(maxH, contentH_ > 0 ? contentH_ + app_.dp(36) : maxH)};
        ui_.panel(p, app_.dp(ui_.theme.radius.xl));
        const Rect2 c = p.inset(app_.dp(18));
        float y = ui_.beginScroll(id("set.menu"), c);
        const float y0 = y;
        y += drawAudioSettings(app_, c.x, y, c.w - app_.dp(6));
        y += ui_.textWrapped(c.x, y, c.w - app_.dp(6), "Spieldaten: " + app_.paths().gameData.string(), app_.dp(12),
                             ui_.theme.textFaint);
        contentH_ = y - y0;
        ui_.endScroll(contentH_);
    }
    bool onBack() override
    {
        app_.settings().save();
        app_.setScreen(createMainMenu(app_));
        return true;
    }

private:
    float contentH_ = 0;
};

// ------------------------------------------------------------------------------------------------------------------
/// Word mark: emblem (two gables under a low sun, in a ring) above "WEILER" in spaced serif capitals
void drawWordmark(App& app, float cx, float top, float scale, float alpha)
{
    auto& b = app.batch();
    auto& ui = app.ui();
    const auto& th = ui.theme;
    const float u = app.dp(1) * scale;
    const Color gold = th.accent.withAlpha(alpha);
    const Color ivory = th.text.withAlpha(alpha);
    // Emblem
    const float er = 30 * u, ecy = top + er;
    b.circle(cx, ecy, er, gold, 1.6f * u);
    b.circle(cx + 9 * u, ecy - 9 * u, 4.2f * u, gold);
    const float w = 2.2f * u;
    const float gy = ecy + 13 * u; // ground line
    b.line(cx - 21 * u, gy, cx + 21 * u, gy, w * 0.8f, gold);
    // Larger gable on the left, smaller one overlapping on the right
    b.line(cx - 17 * u, gy - 7 * u, cx - 6 * u, gy - 19 * u, w, gold);
    b.line(cx - 6 * u, gy - 19 * u, cx + 5 * u, gy - 7 * u, w, gold);
    b.line(cx - 14 * u, gy - 10 * u, cx - 14 * u, gy, w, gold);
    b.line(cx + 2 * u, gy - 10 * u, cx + 2 * u, gy, w, gold);
    b.line(cx + 1 * u, gy - 4.5f * u, cx + 9 * u, gy - 13 * u, w, gold);
    b.line(cx + 9 * u, gy - 13 * u, cx + 17 * u, gy - 4.5f * u, w, gold);
    b.line(cx + 15 * u, gy - 7 * u, cx + 15 * u, gy, w, gold);
    // Door
    b.roundRect({cx - 7.5f * u, gy - 7 * u, 3 * u, 7 * u}, 1.2f * u, gold);
    // Letters with wide tracking
    const std::string word = "WEILER";
    const float fs = 50 * u;
    const float track = fs * 0.16f;
    auto& font = app.fontDisplay();
    std::vector<float> widths;
    float total = 0;
    for(char c : word)
    {
        widths.push_back(font.measure(std::string(1, c), fs));
        total += widths.back();
    }
    total += track * static_cast<float>(word.size() - 1);
    float x = cx - total / 2;
    const float ty = ecy + er + 12 * u;
    for(size_t i = 0; i < word.size(); ++i)
    {
        font.draw(b, x, ty + 2 * u, std::string(1, word[i]), fs, Color::rgb(0x000000, 0.35f * alpha));
        font.draw(b, x, ty, std::string(1, word[i]), fs, ivory);
        x += widths[i] + track;
    }
    // Tagline between two fine rules
    const float ly = ty + fs * 1.3f + 6 * u;
    const std::string tag = "Siedeln · Bauen · Handeln";
    const float tfs = 13.5f * u;
    const float tw = ui.textWidth(tag, tfs);
    ui.text(cx, ly, tag, tfs, th.textDim.withAlpha(alpha), gfx::Align::Center);
    const float rl = 26 * u, gap = 12 * u, ry = ly + tfs * 0.68f;
    b.line(cx - tw / 2 - gap - rl, ry, cx - tw / 2 - gap, ry, 1.f * u, gold.withAlpha(0.6f * alpha));
    b.line(cx + tw / 2 + gap, ry, cx + tw / 2 + gap + rl, ry, 1.f * u, gold.withAlpha(0.6f * alpha));
}

/// Height of the word mark at scale 1 in dp
constexpr float WORDMARK_H = 60 + 12 + 50 * 1.3f + 6 + 20;

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
        const auto& th = ui_.theme;
        const float t = std::min(1.f, static_cast<float>(app_.time() - shownAt_) / th.motion.slow);
        const float e = Ui::ease(t);
        const int n = canContinue ? 6 : 5;
        float bh = app_.dp(56), gap = app_.dp(10), primH = app_.dp(60);
        int cols = 1;
        Rect2 col;
        if(app_.portrait())
        {
            const float scale = std::min(1.2f, (W / app_.dp(1)) / 330.f);
            drawWordmark(app_, W / 2, H * 0.13f - app_.dp(10) * (1 - e), scale, e);
            const float total = primH + gap * 1.6f + (bh + gap) * static_cast<float>(n - 1) + app_.dp(50);
            const float w = std::min(W - app_.dp(40), app_.dp(440));
            col = {(W - w) / 2, H - total - app_.dp(36), w, total};
        } else
        {
            // Word mark on the left, buttons on the right; buttons shrink on short screens
            const float scale = std::clamp((H / app_.dp(1)) / 420.f, 0.7f, 1.2f);
            drawWordmark(app_, W * 0.3f, H / 2 - WORDMARK_H * app_.dp(1) * scale / 2, scale, e);
            // Short screens: the navigation cards form a grid of two columns
            gap = app_.dp(10);
            cols = 2;
            const int rows = (n - 1 + 1) / 2;
            const float total = primH + gap * 1.6f + (bh + gap) * static_cast<float>(rows) + app_.dp(50);
            const float w = std::min(W * 0.46f, app_.dp(460));
            col = {W * 0.72f - w / 2, (H - total) / 2, w, total};
        }
        // Column slides up into place
        col.y += app_.dp(24) * (1 - e);
        float y = col.y;
        const bool first = !canContinue;
        if(ui_.button(id(first ? "mm.new" : "mm.cont"), {col.x, y, col.w, primH}, first ? "Neues Spiel" : "Fortsetzen",
                      first ? Icon::Flag : Icon::Play, Style::Primary))
        {
            if(first)
                app_.setScreen(std::make_unique<NewGameScreen>(app_));
            else
                startLoaded(app_, autosave);
        }
        y += primH + gap * 1.6f;
        int k = 0;
        const float cw = (col.w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        auto nav = [&](const char* key, const std::string& label, Icon ic) {
            const Rect2 r{col.x + static_cast<float>(k % cols) * (cw + gap), y, cw, bh};
            if(++k % cols == 0)
                y += bh + gap;
            // Cards directly over the world get a glass base
            app_.batch().glass(r, std::min(app_.dp(th.radius.md), bh / 2), th.glass);
            return ui_.button(id(key), r, label, ic, Style::Nav);
        };
        if(!first && nav("mm.new", "Neues Spiel", Icon::Flag))
            app_.setScreen(std::make_unique<NewGameScreen>(app_));
        if(nav("mm.camp", "Kampagne", Icon::Trophy))
            app_.setScreen(std::make_unique<CampaignScreen>(app_));
        if(nav("mm.tut", "Einführung", Icon::Help))
            startTutorialGame(app_);
        if(nav("mm.load", "Laden", Icon::Folder))
            app_.setScreen(std::make_unique<LoadScreen>(app_));
        if(nav("mm.set", "Einstellungen", Icon::Gear))
            app_.setScreen(std::make_unique<SettingsScreen>(app_));
        if(k % cols != 0)
            y += bh + gap;
        const float qw = app_.dp(150);
        if(ui_.button(id("mm.quit"), {col.x + (col.w - qw) / 2, y + app_.dp(2), qw, app_.dp(44)}, "Beenden", Icon::Exit,
                      Style::Flat))
            app_.quit();
        ui_.text(app_.portrait() ? W / 2 : W * 0.3f, H - app_.dp(26), "Benötigt die Daten der Gold Edition · GPL",
                 app_.dp(11), th.textFaint.withAlpha(e), gfx::Align::Center);
    }
    bool onBack() override
    {
        app_.quit();
        return true;
    }

private:
    double shownAt_ = app_.time();
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
            app.releaseMenuBackdrop();
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

float drawAudioSettings(App& app, float x, float y, float w)
{
    auto& ui = app.ui();
    const auto& th = ui.theme;
    auto& st = app.settings();
    const float y0 = y;
    struct Row
    {
        Bus bus;
        const char* label;
        const char* key;
        Icon icon;
    };
    static const Row rows[] = {{Bus::Music, "Musik", "volume.music", Icon::Music},
                               {Bus::Effects, "Geräusche im Spiel", "volume.effects", Icon::Sound},
                               {Bus::Ui, "Klänge der Bedienung", "volume.ui", Icon::Touch}};
    for(const Row& r : rows)
    {
        float v = app.audio().volume(r.bus);
        ui.icon(r.icon, x + app.dp(11), y + app.dp(11), app.dp(20), th.accent);
        ui.text(x + app.dp(32), y, r.label, app.dp(th.type.body), th.text);
        ui.text(x + w, y, std::to_string(static_cast<int>(std::lround(v * 100))) + " %", app.dp(th.type.small),
                th.textDim, gfx::Align::Right);
        if(ui.slider(id(r.key), {x, y + app.dp(24), w, app.dp(40)}, v, 0, 1, 0.05f))
        {
            app.audio().setVolume(r.bus, v);
            st.setFloat(r.key, v);
            if(r.bus == Bus::Ui)
                app.audio().playUi(UiSound::Tap);
        }
        y += app.dp(74);
    }
    // Music source; the CD tracks have to be imported once from the user's CD image
    ui.text(x, y, "Musikauswahl", app.dp(th.type.small), th.textDim);
    int source = st.getInt("music.source", 0);
    if(ui.segmented(id("music.source"), {x, y + app.dp(20), w, app.dp(42)}, {"Weiler", "Beides", "Original-CD"},
                    source))
    {
        st.setInt("music.source", source);
        app.scanMusic();
        app.music().skip();
    }
    y += app.dp(72);
    // Import of the CD tracks (stays on this device, never part of Weiler)
    auto& cd = app.cdImport();
    const bfs::path cdDir = app.paths().userData / "music" / "cd";
    static std::string cdNote;
    if(cd.running())
    {
        ui.text(x, y, "CD wird eingelesen …", app.dp(th.type.small), th.textDim);
        const Rect2 bar{x, y + app.dp(24), w, app.dp(6)};
        app.batch().roundRect(bar, bar.h / 2, th.text.withAlpha(0.1f));
        app.batch().roundRectV({bar.x, bar.y, bar.w * cd.progress(), bar.h}, bar.h / 2, th.accent, th.accentDeep);
        y += app.dp(44);
    } else
    {
        size_t have = 0;
        boost::system::error_code ec;
        for(const auto& e : bfs::directory_iterator(cdDir, ec))
            have += e.path().extension() == ".ogg";
        if(const int n = cd.takeWritten())
        {
            app.scanMusic();
            cdNote = std::to_string(n) + " Stücke der CD eingelesen.";
        }
        if(ui.button(id("cd.import"), {x, y, w, app.dp(48)},
                     have ? "Original-CD erneut einlesen" : "Musik der Original-CD einlesen", Icon::Music))
        {
            const bfs::path img = CdImport::findImage(app.paths().gameData);
            if(img.empty())
                cdNote = "Kein CD-Abbild (.ccd oder .cue) bei den Spieldaten gefunden.";
            else if(!cd.start(img, cdDir))
                cdNote = cd.error();
            else
                cdNote.clear();
        }
        y += app.dp(56);
        const std::string note = !cdNote.empty() ? cdNote :
                                 have ? std::to_string(have) + " Stücke der CD vorhanden (nur auf diesem Gerät)." :
                                        "Liest die Audiospuren deines CD-Abbilds ein; sie bleiben auf diesem Gerät.";
        y += ui.textWrapped(x, y, w, note, app.dp(th.type.caption), th.textFaint) + app.dp(8);
    }
    return y - y0;
}

std::unique_ptr<Screen> createMainMenu(App& app)
{
    return std::make_unique<MainMenuScreen>(app);
}

} // namespace weiler
