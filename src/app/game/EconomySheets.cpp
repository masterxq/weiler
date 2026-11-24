// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Sheets of the economy: statistics, settings (distribution, transport, tools, military) and inventory

#include "Audio.h"
#include "Game.h"
#include "GamePlayer.h"
#include "HudImpl.h"
#include "Loader.h"
#include "AddonHelperFunctions.h"
#include "GlobalGameSettings.h"
#include "ogl/glArchivItem_Bitmap.h"
#include "Names.h"
#include "RttrConfig.h"
#include "Settings.h"
#include "Tutorial.h"
#include "buildings/noBuildingSite.h"
#include "buildings/nobBaseWarehouse.h"
#include "buildings/nobHarborBuilding.h"
#include "buildings/nobMilitary.h"
#include "buildings/nobShipYard.h"
#include "buildings/nobUsual.h"
#include "drivers/AudioDriverWrapper.h"
#include "figures/nofPassiveSoldier.h"
#include "files.h"
#include "menu/Menus.h"
#include "nodeObjs/noShip.h"
#include "postSystem/PostBox.h"
#include "postSystem/PostManager.h"
#include "postSystem/PostMsg.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "gameTypes/SettingsTypes.h"
#include "gameTypes/ShipDirection.h"
#include "gameData/BuildingProperties.h"
#include "gameTypes/VisualSettings.h"
#include "gameData/BuildingConsts.h"
#include "gameData/MilitaryConsts.h"
#include "gameData/SettingTypeConv.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>

namespace weiler {

using namespace ui;

void Hud::Impl::sheetStats(const Rect2& r)
{
    tallSheet = true;
    wideSheet = true;
    const Rect2 c = sheetHeader(r, "Statistik", Icon::Chart);
    static const std::vector<std::string> tabs = {"Vergleich", "Waren", "Gebäude", "Militär"};
    ui.segmented(id("stats.tab"), {c.x, c.y, c.w, app.dp(40)}, tabs, sheetTab);
    const Rect2 body{c.x, c.y + app.dp(50), c.w, c.h - app.dp(50)};
    auto& b = app.batch();
    if(sheetTab == 0)
    {
        static const std::vector<std::string> types = {"Land", "Gebäude", "Einwohner", "Militär", "Gold", "Produktivität"};
        static const std::vector<std::string> types1(types.begin(), types.begin() + 3), types2(types.begin() + 3, types.end());
        static const StatisticType typeIds[] = {StatisticType::Country,  StatisticType::Buildings,
                                                StatisticType::Inhabitants, StatisticType::Military,
                                                StatisticType::Gold,     StatisticType::Productivity};
        static const std::vector<std::string> times = {"15 min", "1 h", "4 h", "16 h"};
        float y = body.y;
        if(landscape())
        {
            // Wide side panel: all kinds in one row, the chart gets the height
            ui.segmented(id("stats.type"), {body.x, y, body.w, app.dp(36)}, types, statsType);
            y += app.dp(42);
        } else
        {
            int sel1 = statsType < 3 ? statsType : -1, sel2 = statsType >= 3 ? statsType - 3 : -1;
            if(ui.segmented(id("stats.type1"), {body.x, y, body.w, app.dp(36)}, types1, sel1))
                statsType = sel1;
            if(ui.segmented(id("stats.type2"), {body.x, y + app.dp(40), body.w, app.dp(36)}, types2, sel2))
                statsType = sel2 + 3;
            y += app.dp(86);
        }
        ui.segmented(id("stats.time"), {body.x, y, body.w, app.dp(34)}, times, statsTime);
        y += app.dp(42);
        const Rect2 chart{body.x, y, body.w, std::max(app.dp(60), body.bottom() - y)};
        b.roundRect(chart, app.dp(10), Color::rgb(0xFFFFFF, 0.04f));
        // Top strip: largest value left, legend right; the curves below it
        const Rect2 plot{chart.x + app.dp(10), chart.y + app.dp(30), chart.w - app.dp(20), chart.h - app.dp(40)};
        const StatisticType type = typeIds[std::clamp(statsType, 0, 5)];
        const auto time = static_cast<StatisticTime>(std::clamp(statsTime, 0, 3));
        uint32_t maxV = 1;
        for(unsigned p = 0; p < world().getNumPlayers(); ++p)
        {
            const GamePlayer& pl = world().getPlayer(p);
            if(!pl.isUsed())
                continue;
            for(uint32_t v : pl.getStatistic(time).data[type])
                maxV = std::max(maxV, v);
        }
        b.roundRect({plot.x, plot.y, plot.w, 1}, 0, Color::rgb(0xFFFFFF, 0.08f));
        b.roundRect({plot.x, plot.bottom(), plot.w, 1}, 0, Color::rgb(0xFFFFFF, 0.08f));
        float legendX = chart.right() - app.dp(10);
        for(unsigned p = world().getNumPlayers(); p-- > 0;)
        {
            const GamePlayer& pl = world().getPlayer(p);
            if(!pl.isUsed())
                continue;
            const auto& st = pl.getStatistic(time);
            const Color col = Color::fromARGB(pl.color | 0xFF000000);
            float px = 0, py = 0;
            for(unsigned i = 0; i < NUM_STAT_STEPS; ++i)
            {
                const unsigned idx = (st.currentIndex + 1 + i) % NUM_STAT_STEPS;
                const float x = plot.x + plot.w * static_cast<float>(i) / (NUM_STAT_STEPS - 1);
                const float y = plot.bottom() - plot.h * static_cast<float>(st.data[type][idx]) / static_cast<float>(maxV);
                if(i > 0)
                    b.line(px, py, x, y, app.dp(2.5f), col);
                px = x;
                py = y;
            }
            const float nw = ui.textWidth(pl.name, app.dp(13));
            legendX -= nw;
            ui.text(legendX, chart.y + app.dp(7), pl.name, app.dp(13), ui.theme.text);
            b.circle(legendX - app.dp(10), chart.y + app.dp(16), app.dp(5), col);
            legendX -= app.dp(28);
        }
        ui.text(chart.x + app.dp(10), chart.y + app.dp(7), std::to_string(maxV), app.dp(12), ui.theme.textDim);
    } else if(sheetTab == 1)
    {
        // Ware statistics as in the original: produced amounts of 14 groups over time, several at once
        struct Group
        {
            GoodType icon;
            const char* name;
        };
        static const Group groups[NUM_STAT_MERCHANDISE_TYPES] = {
          {GoodType::Wood, "Holz"},      {GoodType::Boards, "Bretter"}, {GoodType::Stones, "Steine"},
          {GoodType::Bread, "Nahrung"},  {GoodType::Water, "Wasser"},   {GoodType::Beer, "Bier"},
          {GoodType::Coal, "Kohle"},     {GoodType::IronOre, "Eisenerz"}, {GoodType::Gold, "Gold"},
          {GoodType::Iron, "Eisen"},     {GoodType::Coins, "Münzen"},   {GoodType::Hammer, "Werkzeuge"},
          {GoodType::Sword, "Waffen"},   {GoodType::Boat, "Boote"}};
        static const uint32_t colors[NUM_STAT_MERCHANDISE_TYPES] = {
          0xE8A33C, 0xC9783E, 0xA9A9A9, 0x7FC97F, 0x5DADE2, 0xF4D03F, 0x8E9EB8,
          0xB9770E, 0xFFD700, 0xD5DBDB, 0xF39C12, 0xAF7AC5, 0xE74C3C, 0x48C9B0};
        const bool wide = landscape();
        const unsigned perRow = wide ? NUM_STAT_MERCHANDISE_TYPES : NUM_STAT_MERCHANDISE_TYPES / 2;
        const float chipGap = app.dp(4);
        const float chipW = (body.w - chipGap * static_cast<float>(perRow - 1)) / static_cast<float>(perRow);
        const float chipH = app.dp(wide ? 38 : 44);
        float y = body.y;
        for(unsigned g = 0; g < NUM_STAT_MERCHANDISE_TYPES; ++g)
        {
            const Rect2 chip{body.x + static_cast<float>(g % perRow) * (chipW + chipGap),
                             y + static_cast<float>(g / perRow) * (chipH + chipGap), chipW, chipH};
            const bool on = (statsWares >> g) & 1u;
            const Color col = Color::rgb(colors[g]);
            b.roundRect(chip, app.dp(8), on ? col.withAlpha(0.28f) : Color::rgb(0xFFFFFF, 0.05f));
            if(on)
                b.roundRect({chip.x + app.dp(6), chip.bottom() - app.dp(5), chip.w - app.dp(12), app.dp(3)}, app.dp(1.5f),
                            col);
            const float is = std::min(chip.w, chip.h) - app.dp(12);
            ui.sprite(src(LOADER.getWareTex(groups[g].icon)), {chip.x + (chip.w - is) / 2, chip.y + app.dp(4), is, is}, 0,
                      on ? 1.f : 0.55f);
            ui.tag(groups[g].name, chip);
            if(ui.area(id("stats.ware", g), chip))
                statsWares ^= 1u << g;
        }
        y += static_cast<float>((NUM_STAT_MERCHANDISE_TYPES + perRow - 1) / perRow) * (chipH + chipGap) + app.dp(4);
        static const std::vector<std::string> times = {"15 min", "1 h", "4 h", "16 h"};
        ui.segmented(id("stats.time"), {body.x, y, body.w, app.dp(34)}, times, statsTime);
        y += app.dp(42);
        const Rect2 chart{body.x, y, body.w, std::max(app.dp(60), body.bottom() - y)};
        b.roundRect(chart, app.dp(10), Color::rgb(0xFFFFFF, 0.04f));
        const Rect2 plot{chart.x + app.dp(10), chart.y + app.dp(30), chart.w - app.dp(20), chart.h - app.dp(40)};
        const auto& st = player().getStatistic(static_cast<StatisticTime>(std::clamp(statsTime, 0, 3)));
        unsigned maxV = 1;
        for(unsigned g = 0; g < NUM_STAT_MERCHANDISE_TYPES; ++g)
        {
            if((statsWares >> g) & 1u)
            {
                for(unsigned i = 0; i < NUM_STAT_STEPS; ++i)
                    maxV = std::max<unsigned>(maxV, st.merchandiseData[g][i]);
            }
        }
        b.roundRect({plot.x, plot.y, plot.w, 1}, 0, Color::rgb(0xFFFFFF, 0.08f));
        b.roundRect({plot.x, plot.bottom(), plot.w, 1}, 0, Color::rgb(0xFFFFFF, 0.08f));
        std::string names;
        for(unsigned g = 0; g < NUM_STAT_MERCHANDISE_TYPES; ++g)
        {
            if(!((statsWares >> g) & 1u))
                continue;
            names += (names.empty() ? "" : ", ") + std::string(groups[g].name);
            float px = 0, py = 0;
            for(unsigned i = 0; i < NUM_STAT_STEPS; ++i)
            {
                const unsigned idx = (st.currentIndex + 1 + i) % NUM_STAT_STEPS;
                const float x = plot.x + plot.w * static_cast<float>(i) / (NUM_STAT_STEPS - 1);
                const float yv =
                  plot.bottom() - plot.h * static_cast<float>(st.merchandiseData[g][idx]) / static_cast<float>(maxV);
                if(i > 0)
                    b.line(px, py, x, yv, app.dp(2.5f), Color::rgb(colors[g]));
                px = x;
                py = yv;
            }
        }
        ui.text(chart.x + app.dp(10), chart.y + app.dp(7), std::to_string(maxV), app.dp(12), ui.theme.textDim);
        std::string head = names.empty() ? std::string("Warengruppen oben antippen") : "Erzeugt: " + names;
        if(ui.textWidth(head, app.dp(12)) > chart.w - app.dp(80))
            head = "Erzeugt: " + std::to_string(std::count(names.begin(), names.end(), ',') + 1) + " Gruppen";
        ui.text(chart.right() - app.dp(10), chart.y + app.dp(7), head, app.dp(12), ui.theme.textDim, gfx::Align::Right);
    } else if(sheetTab == 2 || sheetTab == 3)
    {
        float y = ui.beginScroll(id("stats.scroll", static_cast<uint64_t>(sheetTab)), body);
        const float y0 = y;
        const float rowH = app.dp(40);
        if(sheetTab == 2)
        {
            const BuildingCount bc = player().getBuildingRegister().getBuildingNums();
            for(unsigned i = 0; i < helpers::NumEnumValues_v<BuildingType>; ++i)
            {
                const auto bt = static_cast<BuildingType>(i);
                if(bc.buildings[bt] == 0 && bc.buildingSites[bt] == 0)
                    continue;
                buildingIcon(player().nation, bt, {body.x, y + app.dp(2), app.dp(36), app.dp(36)});
                ui.text(body.x + app.dp(46), y + app.dp(10), buildingName(bt), app.dp(14), ui.theme.text);
                std::string cnt = std::to_string(bc.buildings[bt]);
                if(bc.buildingSites[bt])
                    cnt += " (+" + std::to_string(bc.buildingSites[bt]) + ")";
                ui.text(body.right() - app.dp(8), y + app.dp(10), cnt, app.dp(14), ui.theme.textDim, gfx::Align::Right);
                y += rowH;
            }
        } else
        {
            const Inventory& inv = player().getInventory();
            static const Job ranks[] = {Job::Private, Job::PrivateFirstClass, Job::Sergeant, Job::Officer, Job::General};
            for(Job j : ranks)
            {
                ui.sprite(src(LOADER.getJobTex(j)), {body.x, y + app.dp(2), app.dp(36), app.dp(36)});
                ui.text(body.x + app.dp(46), y + app.dp(10), jobName(j), app.dp(14), ui.theme.text);
                ui.text(body.right() - app.dp(8), y + app.dp(10), std::to_string(inv[j]), app.dp(14), ui.theme.textDim,
                        gfx::Align::Right);
                y += rowH;
            }
            ui.text(body.x, y + app.dp(10),
                    "Militärgebäude: " + std::to_string(player().getBuildingRegister().getMilitaryBuildings().size()),
                    app.dp(14), ui.theme.text);
            y += rowH;
        }
        ui.endScroll(y - y0);
    }
}

void Hud::Impl::sheetEconomy(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Wirtschaft", Icon::Sliders);
    Grid g{c, 1, app.dp(58), app.dp(10)};
    editValid = false;
    if(ui.button(id("eco.dist"), g.next(), "Warenverteilung", Icon::Sliders, Style::Nav))
        openSheet(SheetKind::Distribution, MapPoint::invalid(), true);
    if(ui.button(id("eco.trans"), g.next(), "Transportreihenfolge", Icon::List, Style::Nav))
        openSheet(SheetKind::Transport, MapPoint::invalid(), true);
    if(ui.button(id("eco.tools"), g.next(), "Werkzeugproduktion", Icon::Tools, Style::Nav))
        openSheet(SheetKind::Tools, MapPoint::invalid(), true);
    if(ui.button(id("eco.mil"), g.next(), "Militär", Icon::Shield, Style::Nav))
        openSheet(SheetKind::Military, MapPoint::invalid(), true);
    if(ui.button(id("eco.inv"), g.next(), "Inventar", Icon::Boxes, Style::Nav))
        openSheet(SheetKind::Inventory, MapPoint::invalid(), true);
    fit(r, g.area.y + static_cast<float>(g.n) * (g.h + g.gap));
}

bool Hud::Impl::defaultsButton(float x, float& y, float w)
{
    const bool hit = ui.button(id("defaults", static_cast<uint64_t>(sheet)), {x, y + app.dp(10), w, app.dp(48)},
                               "Standardwerte");
    y += app.dp(66);
    return hit;
}

void Hud::Impl::ensureEdit()
{
    if(!editValid)
    {
        player().fillVisualSettings(edit);
        editValid = true;
    }
}

void Hud::Impl::sheetDistribution(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Warenverteilung", Icon::Sliders);
    ensureEdit();
    float y = ui.beginScroll(id("dist.scroll"), c);
    const float y0 = y;
    GoodType lastGood = GoodType::Nothing;
    for(unsigned i = 0; i < distributionMap.size(); ++i)
    {
        const GoodType gt = std::get<0>(distributionMap[i]);
        const BuildingType bt = std::get<1>(distributionMap[i]);
        if(hiddenBuilding(world(), bt))
            continue;
        if(gt != lastGood)
        {
            y += app.dp(6);
            ui.sprite(src(LOADER.getWareTex(gt)), {c.x, y, app.dp(28), app.dp(28)});
            ui.text(c.x + app.dp(36), y + app.dp(4), wareName(gt), app.dp(16), ui.theme.accent, gfx::Align::Left, true);
            y += app.dp(34);
            lastGood = gt;
        }
        ui.text(c.x + app.dp(8), y + app.dp(10), buildingName(bt), app.dp(14), ui.theme.text);
        float v = edit.distribution[i];
        const float sx = c.x + std::min(app.dp(160), c.w * 0.45f);
        if(ui.slider(id("dist", i), {sx, y, c.right() - sx, app.dp(40)}, v, 0, 10, 1))
        {
            edit.distribution[i] = static_cast<uint8_t>(v);
            game.session().ChangeDistribution(edit.distribution);
        }
        y += app.dp(42);
    }
    if(defaultsButton(c.x, y, c.w - app.dp(8)))
    {
        edit.distribution = GamePlayer::getStandardVisualSettings().distribution;
        game.session().ChangeDistribution(edit.distribution);
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetTransport(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Transportreihenfolge", Icon::List);
    ensureEdit();
    float y = ui.beginScroll(id("trans.scroll"), c);
    const float y0 = y;
    const float rowH = app.dp(52);
    for(unsigned i = 0; i < edit.transport_order.size(); ++i)
    {
        const uint8_t group = edit.transport_order[i];
        // Wares of this group
        std::string names;
        GoodType first = GoodType::Nothing;
        for(const auto gt : helpers::enumRange<GoodType>())
        {
            if(gt == GoodType::Nothing || STD_TRANSPORT_PRIO[gt] != group)
                continue;
            if(first == GoodType::Nothing)
                first = gt;
            const std::string n = wareName(gt);
            if(names.find(n) == std::string::npos)
                names += (names.empty() ? "" : ", ") + n;
        }
        ui.text(c.x, y + app.dp(14), std::to_string(i + 1) + ".", app.dp(14), ui.theme.textDim);
        if(first != GoodType::Nothing)
            ui.sprite(src(LOADER.getWareTex(first)), {c.x + app.dp(26), y + app.dp(8), app.dp(34), app.dp(34)});
        const float bs = app.dp(44);
        const float textW = c.w - app.dp(70) - bs * 2 - app.dp(8);
        // Long groups ("Schwert, Schild, Rüstung") take two smaller lines instead of being cut
        if(ui.textWidth(names, app.dp(14)) <= textW)
            ui.text(c.x + app.dp(68), y + app.dp(14), names, app.dp(14), ui.theme.text);
        else
        {
            const auto lines = app.font().wrap(names, app.dp(12), textW);
            float ly = y + app.dp(lines.size() > 1 ? 7 : 15);
            for(size_t k = 0; k < lines.size() && k < 2; ++k, ly += app.dp(16))
                ui.text(c.x + app.dp(68), ly, lines[k], app.dp(12), ui.theme.text);
        }
        if(ui.button(id("trans.up", i), {c.right() - bs * 2 - app.dp(6), y + app.dp(4), bs, bs}, "", Icon::Up,
                     Style::Normal, i > 0))
        {
            std::swap(edit.transport_order[i], edit.transport_order[i - 1]);
            game.session().ChangeTransport(edit.transport_order);
        }
        if(ui.button(id("trans.down", i), {c.right() - bs, y + app.dp(4), bs, bs}, "", Icon::Down, Style::Normal,
                     i + 1 < edit.transport_order.size()))
        {
            std::swap(edit.transport_order[i], edit.transport_order[i + 1]);
            game.session().ChangeTransport(edit.transport_order);
        }
        y += rowH;
    }
    if(defaultsButton(c.x, y, c.w - app.dp(8)))
    {
        edit.transport_order = GamePlayer::getStandardVisualSettings().transport_order;
        game.session().ChangeTransport(edit.transport_order);
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetTools(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Werkzeugproduktion", Icon::Tools);
    ensureEdit();
    static const GoodType toolWares[] = {GoodType::Tongs,      GoodType::Hammer, GoodType::Axe,     GoodType::Saw,
                                         GoodType::PickAxe,    GoodType::Shovel, GoodType::Crucible, GoodType::RodAndLine,
                                         GoodType::Scythe,     GoodType::Cleaver, GoodType::Rollingpin, GoodType::Bow};
    float y = ui.beginScroll(id("tools.scroll"), c);
    const float y0 = y;
    unsigned i = 0;
    for(const auto tool : helpers::enumRange<Tool>())
    {
        const GoodType gt = toolWares[i];
        ui.sprite(src(LOADER.getWareTex(gt)), {c.x, y + app.dp(5), app.dp(32), app.dp(32)});
        ui.text(c.x + app.dp(40), y + app.dp(11), wareName(gt), app.dp(14), ui.theme.text);
        float v = edit.tools_settings[tool];
        const float sx = c.x + std::min(app.dp(160), c.w * 0.42f);
        if(ui.slider(id("tool", i), {sx, y, c.right() - sx, app.dp(42)}, v, 0, 10, 1))
        {
            edit.tools_settings[tool] = static_cast<uint8_t>(v);
            game.session().ChangeTools(edit.tools_settings);
        }
        y += app.dp(46);
        ++i;
    }
    // As in the original: defaults, or no tools at all
    const float half = (c.w - app.dp(8) - app.dp(10)) / 2, by = y;
    if(defaultsButton(c.x, y, half))
    {
        edit.tools_settings = GamePlayer::getStandardToolSettings();
        game.session().ChangeTools(edit.tools_settings);
    }
    if(ui.button(id("tools.zero"), {c.x + half + app.dp(10), by + app.dp(10), half, app.dp(48)}, "Alle auf 0"))
    {
        edit.tools_settings = ToolSettings{};
        game.session().ChangeTools(edit.tools_settings);
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetMilitary(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Militär", Icon::Shield);
    ensureEdit();
    static const char* labels[] = {"Rekrutierung",          "Stärke der Verteidiger", "Anzahl Verteidiger",
                                   "Anzahl Angreifer",       "Besatzung Hinterland",   "Besatzung Landesmitte",
                                   "Besatzung Hafennähe",    "Besatzung Grenze"};
    float y = ui.beginScroll(id("mil.scroll"), c);
    const float y0 = y;
    for(unsigned i = 0; i < edit.military_settings.size(); ++i)
    {
        ui.text(c.x, y + app.dp(4), labels[i], app.dp(14), ui.theme.text);
        float v = edit.military_settings[i];
        if(ui.slider(id("mil", i), {c.x, y + app.dp(24), c.w, app.dp(40)}, v, 0,
                     static_cast<float>(MILITARY_SETTINGS_SCALE[i]), 1))
        {
            edit.military_settings[i] = static_cast<uint8_t>(v);
            game.session().ChangeMilitary(edit.military_settings);
        }
        y += app.dp(70);
    }
    if(defaultsButton(c.x, y, c.w - app.dp(8)))
    {
        edit.military_settings = GamePlayer::getStandardMilitarySettings();
        game.session().ChangeMilitary(edit.military_settings);
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetInventory(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Inventar", Icon::Boxes);
    static const std::vector<std::string> tabs = {"Waren", "Leute"};
    ui.segmented(id("inv.tab"), {c.x, c.y, c.w, app.dp(40)}, tabs, sheetTab);
    const Rect2 view{c.x, c.y + app.dp(50), c.w, c.h - app.dp(50)};
    const Inventory& inv = player().getInventory();
    float y = ui.beginScroll(id("inv.scroll", static_cast<uint64_t>(sheetTab)), view);
    const float y0 = y;
    const float cell = app.dp(78);
    const int cols = std::max(1, static_cast<int>(view.w / cell));
    const float cw = view.w / static_cast<float>(cols);
    int n = 0;
    auto cellAt = [&](int k) {
        return Rect2{view.x + static_cast<float>(k % cols) * cw, y + static_cast<float>(k / cols) * app.dp(84), cw,
                     app.dp(80)};
    };
    if(sheetTab == 0)
    {
        for(const auto gt : helpers::enumRange<GoodType>())
        {
            if(hiddenWare(world(), gt))
                continue;
            const Rect2 cr = cellAt(n++);
            ui.sprite(src(LOADER.getWareTex(gt)), {cr.x + cr.w / 2 - app.dp(18), cr.y + app.dp(4), app.dp(36), app.dp(36)});
            ui.text(cr.x + cr.w / 2, cr.y + app.dp(42), std::to_string(inv[gt]), app.dp(15), ui.theme.text,
                    gfx::Align::Center, true);
            ui.text(cr.x + cr.w / 2, cr.y + app.dp(60), wareName(gt), app.dp(10.5f), ui.theme.textDim, gfx::Align::Center);
        }
    } else
    {
        for(const auto j : helpers::enumRange<Job>())
        {
            if(j == Job::BoatCarrier || hiddenJob(world(), j))
                continue;
            const Rect2 cr = cellAt(n++);
            ui.sprite(src(LOADER.getJobTex(j)), {cr.x + cr.w / 2 - app.dp(18), cr.y + app.dp(4), app.dp(36), app.dp(36)});
            ui.text(cr.x + cr.w / 2, cr.y + app.dp(42), std::to_string(inv[j]), app.dp(15), ui.theme.text,
                    gfx::Align::Center, true);
            ui.text(cr.x + cr.w / 2, cr.y + app.dp(60), jobName(j), app.dp(10.5f), ui.theme.textDim, gfx::Align::Center);
        }
    }
    ui.endScroll(static_cast<float>((n + cols - 1) / cols) * app.dp(84) + (y - y0));
}


} // namespace weiler
