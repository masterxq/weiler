// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Audio.h"
#include "Game.h"
#include "GamePlayer.h"
#include "HudImpl.h"
#include "Loader.h"
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
#include "postSystem/PostBox.h"
#include "postSystem/PostManager.h"
#include "postSystem/PostMsg.h"
#include "world/GameWorldBase.h"
#include "world/GameWorldViewer.h"
#include "gameTypes/SettingsTypes.h"
#include "gameTypes/VisualSettings.h"
#include "gameData/BuildingConsts.h"
#include "gameData/MilitaryConsts.h"
#include "gameData/SettingTypeConv.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <cmath>
#include <ctime>

namespace bfs = boost::filesystem;

namespace weiler {

using namespace ui;

namespace {
const ISpriteSource* src(ITexture* t)
{
    return dynamic_cast<const ISpriteSource*>(t);
}

std::string savePath(const std::string& name)
{
    return (RTTRCONFIG.expandPath(s25::folders::save) / (name + ".sav")).string();
}

std::string fileTime(const std::string& path)
{
    boost::system::error_code ec;
    const std::time_t t = bfs::last_write_time(path, ec);
    if(ec)
        return {};
    char buf[64];
    std::strftime(buf, sizeof(buf), "%d.%m. %H:%M", std::localtime(&t));
    return buf;
}

// Edited copy of the economy settings (commands take effect one game frame later)
VisualSettings gEdit;
bool gEditValid = false;
} // namespace

Rect2 Hud::Impl::sheetHeader(const Rect2& r, const std::string& title, Icon ic)
{
    const float pad = app.dp(16);
    const float h = app.dp(56);
    float tx = r.x + pad;
    if(ic != Icon::None)
    {
        ui.icon(ic, tx + app.dp(12), r.y + h / 2, app.dp(24), ui.theme.accent);
        tx += app.dp(34);
    }
    ui.text(tx, r.y + (h - app.dp(21) * 1.3f) / 2, title, app.dp(21), ui.theme.text, gfx::Align::Left, true);
    const float bs = app.dp(44);
    const Icon closeIcon = sheetStack.empty() ? Icon::Close : Icon::Back;
    if(ui.iconButton(id("sheet.close"), {r.right() - bs - app.dp(8), r.y + (h - bs) / 2, bs, bs}, closeIcon, false, 0,
                     Style::Flat))
        closeSheet();
    app.batch().quad(0, r.x + pad, r.y + h, r.right() - pad, r.y + h + 1, 0, 0, 1, 1, ui.theme.line);
    return {r.x + pad, r.y + h + app.dp(8), r.w - 2 * pad, r.h - h - app.dp(16)};
}

void Hud::Impl::drawSheet()
{
    if(sheet != SheetKind::None)
    {
        Rect2 r = sheetRect();
        const float t = Ui::ease(std::min(1.f, static_cast<float>((app.time() - sheetTime) / 0.22)));
        if(landscape())
            r.x += (1 - t) * (r.w + app.dp(20));
        else
            r.y += (1 - t) * (r.h + app.dp(20));
        ui.panel(r, app.dp(22), ui.theme.panel.withAlpha(0.93f));
        if(!landscape())
            app.batch().roundRect({r.x + r.w / 2 - app.dp(20), r.y + app.dp(6), app.dp(40), app.dp(4)}, app.dp(2),
                                  Color::rgb(0xFFFFFF, 0.25f));
        switch(sheet)
        {
            case SheetKind::GameMenu: sheetGameMenu(r); break;
            case SheetKind::Build: sheetBuild(r); break;
            case SheetKind::Building: sheetBuilding(r); break;
            case SheetKind::Flag: sheetFlag(r); break;
            case SheetKind::Messages: sheetMessages(r); break;
            case SheetKind::Stats: sheetStats(r); break;
            case SheetKind::Economy: sheetEconomy(r); break;
            case SheetKind::Distribution: sheetDistribution(r); break;
            case SheetKind::Transport: sheetTransport(r); break;
            case SheetKind::Tools: sheetTools(r); break;
            case SheetKind::Military: sheetMilitary(r); break;
            case SheetKind::Inventory: sheetInventory(r); break;
            case SheetKind::Save: sheetSaveLoad(r, true); break;
            case SheetKind::Load: sheetSaveLoad(r, false); break;
            case SheetKind::Settings: sheetSettings(r); break;
            case SheetKind::Attack: sheetAttack(r); break;
            case SheetKind::Mission: sheetMission(r); break;
            case SheetKind::GameOver: sheetGameOver(r); break;
            case SheetKind::Help: sheetHelp(r); break;
            case SheetKind::None: break;
        }
    }
    if(confirmAction)
    {
        const float W = static_cast<float>(app.width()), H = static_cast<float>(app.height());
        app.batch().quad(0, 0, 0, W, H, 0, 0, 1, 1, Color::rgb(0x000000, 0.45f));
        ui.block({0, 0, W, H});
        const float w = std::min(W - app.dp(40), app.dp(400));
        const float fs = app.dp(17);
        const auto lines = app.font().wrap(confirmText, fs, w - app.dp(40));
        const float h = app.dp(110) + static_cast<float>(lines.size()) * fs * 1.3f;
        const Rect2 r{(W - w) / 2, (H - h) / 2, w, h};
        ui.panel(r, app.dp(22), ui.theme.panelSolid);
        ui.textWrapped(r.x + app.dp(20), r.y + app.dp(22), w - app.dp(40), confirmText, fs, ui.theme.text);
        const float bh = app.dp(48), bw = (w - app.dp(52)) / 2;
        if(ui.button(id("confirm.no"), {r.x + app.dp(20), r.bottom() - bh - app.dp(18), bw, bh}, "Abbrechen"))
            confirmAction = nullptr;
        if(ui.button(id("confirm.yes"), {r.right() - bw - app.dp(20), r.bottom() - bh - app.dp(18), bw, bh}, "OK",
                     Icon::None, Style::Primary))
        {
            auto fn = std::move(confirmAction);
            confirmAction = nullptr;
            fn();
        }
    }
}

namespace {
/// Grid of big menu buttons
struct Grid
{
    Rect2 area;
    int cols;
    float h, gap;
    int n = 0;
    Rect2 next()
    {
        const float w = (area.w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const int c = n % cols, r = n / cols;
        ++n;
        return {area.x + static_cast<float>(c) * (w + gap), area.y + static_cast<float>(r) * (h + gap), w, h};
    }
};
} // namespace

void Hud::Impl::sheetGameMenu(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Menü", Icon::Menu);
    Grid g{c, 2, app.dp(64), app.dp(10)};
    if(ui.button(id("gm.resume"), g.next(), "Weiter", Icon::Play, Style::Primary))
        closeSheet();
    if(ui.button(id("gm.save"), g.next(), "Speichern", Icon::Save))
        openSheet(SheetKind::Save, MapPoint::invalid(), true);
    if(ui.button(id("gm.load"), g.next(), "Laden", Icon::Folder))
        openSheet(SheetKind::Load, MapPoint::invalid(), true);
    if(ui.button(id("gm.eco"), g.next(), "Wirtschaft", Icon::Sliders))
        openSheet(SheetKind::Economy, MapPoint::invalid(), true);
    if(ui.button(id("gm.stats"), g.next(), "Statistik", Icon::Chart))
        openSheet(SheetKind::Stats, MapPoint::invalid(), true);
    if(ui.button(id("gm.msg"), g.next(), "Nachrichten", Icon::Mail))
        openSheet(SheetKind::Messages, MapPoint::invalid(), true);
    if(ui.button(id("gm.settings"), g.next(), "Einstellungen", Icon::Gear))
        openSheet(SheetKind::Settings, MapPoint::invalid(), true);
    if(ui.button(id("gm.help"), g.next(), "Hilfe", Icon::Help))
        openSheet(SheetKind::Help, MapPoint::invalid(), true);
    if(ui.button(id("gm.quit"), g.next(), "Hauptmenü", Icon::Exit))
    {
        confirm("Partie verlassen? Der Stand wird als Autosave gesichert.", [this] {
            game.saveTo(savePath("autosave"));
            App& a = app;
            a.post([&a] { a.setScreen(createMainMenu(a)); });
        });
    }
}

std::vector<BuildingType> fittingBuildings(const Hud::Impl& h, MapPoint pt);

void Hud::Impl::sheetBuild(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Bauen", Icon::Build);
    static const std::vector<std::string> tabs = {"Alle", "Rohstoffe", "Nahrung", "Werkstatt", "Militär"};
    sheetTab = std::clamp(sheetTab, 0, 4);
    ui.segmented(id("build.tabs"), {c.x, c.y, c.w, app.dp(40)}, tabs, sheetTab);
    auto category = [](BuildingType bt) {
        switch(bt)
        {
            case BuildingType::Woodcutter:
            case BuildingType::Forester:
            case BuildingType::Quarry:
            case BuildingType::Sawmill:
            case BuildingType::GraniteMine:
            case BuildingType::CoalMine:
            case BuildingType::IronMine:
            case BuildingType::GoldMine:
            case BuildingType::Charburner: return 1;
            case BuildingType::Fishery:
            case BuildingType::Hunter:
            case BuildingType::Farm:
            case BuildingType::Mill:
            case BuildingType::Bakery:
            case BuildingType::PigFarm:
            case BuildingType::Slaughterhouse:
            case BuildingType::Well:
            case BuildingType::Brewery:
            case BuildingType::Vineyard:
            case BuildingType::Winery: return 2;
            case BuildingType::Barracks:
            case BuildingType::Guardhouse:
            case BuildingType::Watchtower:
            case BuildingType::Fortress:
            case BuildingType::LookoutTower:
            case BuildingType::Catapult: return 4;
            default: return 3;
        }
    };
    const auto all = fittingBuildings(*this, sheetPt);
    const Rect2 view{c.x, c.y + app.dp(50), c.w, c.bottom() - c.y - app.dp(50)};
    float y0 = ui.beginScroll(id("build.scroll"), view);
    const float cell = app.dp(96);
    const int cols = std::max(1, static_cast<int>(view.w / cell));
    const float cw = view.w / static_cast<float>(cols);
    int n = 0;
    const Nation nation = player().nation;
    for(BuildingType bt : all)
    {
        if(sheetTab != 0 && category(bt) != sheetTab)
            continue;
        const Rect2 cr{view.x + static_cast<float>(n % cols) * cw, y0 + static_cast<float>(n / cols) * app.dp(110),
                       cw, app.dp(104)};
        ++n;
        if(ui.pressed(id("build", static_cast<uint64_t>(bt)), cr))
            app.batch().roundRect(cr.inset(app.dp(3)), app.dp(14), ui.theme.raised);
        ui.sprite(LOADER.getNationIcon(nation, bt), {cr.x + app.dp(12), cr.y + app.dp(6), cr.w - app.dp(24), app.dp(62)});
        ui.textWrapped(cr.x + app.dp(4), cr.y + app.dp(72), cr.w - app.dp(8), buildingName(bt), app.dp(12),
                       ui.theme.text, gfx::Align::Center);
        if(ui.area(id("build", static_cast<uint64_t>(bt)), cr))
        {
            game.session().SetBuildingSite(sheetPt, bt);
            noteRecent(bt);
            if(tutorial)
                tutorial->onBuild(bt);
            closeSheet();
            break;
        }
    }
    ui.endScroll(static_cast<float>((n + cols - 1) / cols) * app.dp(110));
}

void Hud::Impl::sheetBuilding(const Rect2& r)
{
    const noBase* obj = world().getNO(sheetPt);
    if(!obj || (obj->getType() != NodalObjectType::Building && obj->getType() != NodalObjectType::Buildingsite))
    {
        closeSheet();
        return;
    }
    const auto* bld = static_cast<const noBaseBuilding*>(obj);
    if(bld->getPlayer() != game.localPlayer())
    {
        closeSheet();
        return;
    }
    Session& s = game.session();
    const BuildingType bt = bld->getBuildingType();
    const bool isSite = obj->getType() == NodalObjectType::Buildingsite;
    Rect2 c = sheetHeader(r, std::string(isSite ? "Baustelle: " : "") + buildingName(bt));
    // Picture
    const float picW = std::min(app.dp(120), c.w * 0.32f);
    ui.sprite(LOADER.getNationIcon(bld->getNation(), bt), {c.x, c.y, picW, picW * 0.9f});
    const float tx = c.x + picW + app.dp(16);
    const float tw = c.right() - tx;
    float y = c.y;
    const float fs = app.dp(15);
    auto line = [&](const std::string& t, Color col) {
        ui.text(tx, y, t, fs, col);
        y += fs * 1.45f;
    };
    auto bar = [&](float frac, Color col) {
        app.batch().roundRect({tx, y + app.dp(2), tw, app.dp(8)}, app.dp(4), Color::rgb(0xFFFFFF, 0.1f));
        app.batch().roundRect({tx, y + app.dp(2), tw * std::clamp(frac, 0.f, 1.f), app.dp(8)}, app.dp(4), col);
        y += app.dp(18);
    };
    const float bh = app.dp(52);
    float by = std::max(y + picW, c.y + picW + app.dp(10));
    if(isSite)
    {
        const auto* site = static_cast<const noBuildingSite*>(obj);
        const unsigned prog = site->getBuildProgress();
        line("Fortschritt " + std::to_string(prog) + " %", ui.theme.text);
        bar(static_cast<float>(prog) / 100.f, ui.theme.accent);
        line("Bretter " + std::to_string(site->getUsedBoards() + site->getBoards()) + " · Steine "
               + std::to_string(site->getUsedStones() + site->getStones()),
             ui.theme.textDim);
    } else if(const auto* usual = dynamic_cast<const nobUsual*>(obj))
    {
        if(!usual->hasWorker())
            line("Wartet auf " + std::string(jobName(*BLD_WORK_DESC[bt].job)), ui.theme.danger);
        else
        {
            const unsigned prod = usual->getProductivity();
            line("Produktivität " + std::to_string(prod) + " %", ui.theme.text);
            bar(static_cast<float>(prod) / 100.f, prod > 60 ? ui.theme.ok : ui.theme.accent);
        }
        const auto& wn = BLD_WORK_DESC[bt].waresNeeded;
        std::string wares;
        for(unsigned i = 0; i < wn.size(); ++i)
        {
            if(wn[i] == GoodType::Nothing)
                continue;
            wares += (wares.empty() ? "" : " · ") + std::string(wareName(wn[i])) + " "
                     + std::to_string(usual->getNumWares(i));
        }
        if(!wares.empty())
            line("Vorrat: " + wares, ui.theme.textDim);
        by = std::max(y, c.y + picW) + app.dp(10);
        bool on = !usual->isProductionDisabled();
        if(ui.toggle(id("bld.prod"), {c.x, by, c.w, bh}, on, "Produktion"))
            s.SetProductionEnabled(sheetPt, on);
        by += bh + app.dp(8);
        if(const auto* yard = dynamic_cast<const nobShipYard*>(obj))
        {
            int mode = yard->getMode() == nobShipYard::Mode::Boats ? 0 : 1;
            if(ui.segmented(id("bld.yard"), {c.x, by, c.w, app.dp(44)}, {"Boote", "Schiffe"}, mode))
                s.SetShipYardMode(sheetPt, mode == 1);
            by += app.dp(52);
        }
    } else if(const auto* mil = dynamic_cast<const nobMilitary*>(obj))
    {
        line("Soldaten " + std::to_string(mil->getNumTroops()) + " / " + std::to_string(mil->getMaxTroopsCt()),
             ui.theme.text);
        std::array<unsigned, NUM_SOLDIER_RANKS> ranks{};
        for(auto&& soldier : mil->getTroops())
            ++ranks[std::min<unsigned>(soldier.getRank(), NUM_SOLDIER_RANKS - 1)];
        std::string rs;
        for(unsigned i = 0; i < NUM_SOLDIER_RANKS; ++i)
            rs += (i ? " · " : "") + std::to_string(ranks[i]);
        line("Ränge: " + rs, ui.theme.textDim);
        line("Gold " + std::to_string(mil->getNumCoins()) + " / " + std::to_string(mil->getMaxCoinCt()),
             ui.theme.textDim);
        by = std::max(y, c.y + picW) + app.dp(10);
        bool gold = !mil->isGoldDisabled();
        if(ui.toggle(id("bld.gold"), {c.x, by, c.w, bh}, gold, "Goldmünzen annehmen"))
            s.SetCoinsAllowed(sheetPt, gold);
        by += bh + app.dp(8);
    } else if(const auto* wh = dynamic_cast<const nobBaseWarehouse*>(obj))
    {
        const Inventory& inv = wh->getInventory();
        unsigned wares = 0, people = 0;
        for(const auto gt : helpers::enumRange<GoodType>())
            wares += inv[gt];
        for(const auto j : helpers::enumRange<Job>())
            people += inv[j];
        line(std::to_string(wares) + " Waren · " + std::to_string(people) + " Bewohner", ui.theme.text);
        by = std::max(y, c.y + picW) + app.dp(10);
        if(const auto* harbor = dynamic_cast<const nobHarborBuilding*>(obj))
        {
            const bool exp = harbor->isExpeditionActive();
            if(ui.button(id("bld.exp"), {c.x, by, c.w, bh}, exp ? "Expedition abbrechen" : "Expedition vorbereiten",
                         Icon::Ship))
                s.StartStopExpedition(sheetPt, !exp);
            by += bh + app.dp(8);
        }
        // Ware list with store settings
        static const std::vector<std::string> modes = {"Normal", "Einlagern", "Auslagern", "Stopp"};
        const Rect2 view{c.x, by, c.w, c.bottom() - by - bh - app.dp(10)};
        float ly = ui.beginScroll(id("wh.scroll"), view);
        const float rowH = app.dp(48);
        unsigned row = 0;
        for(const auto gt : helpers::enumRange<GoodType>())
        {
            if(gt == GoodType::Nothing || gt == GoodType::WaterEmpty || gt == GoodType::ShieldVikings
               || gt == GoodType::ShieldAfricans || gt == GoodType::ShieldJapanese)
                continue;
            const Rect2 rr{view.x, ly + static_cast<float>(row) * rowH, view.w, rowH};
            ui.sprite(src(LOADER.getWareTex(gt)), {rr.x, rr.y + app.dp(8), app.dp(32), app.dp(32)});
            ui.text(rr.x + app.dp(42), rr.y + app.dp(13), wareName(gt), app.dp(14), ui.theme.text);
            ui.text(rr.x + app.dp(170), rr.y + app.dp(13), std::to_string(inv[gt]), app.dp(14), ui.theme.textDim);
            const InventorySetting st = wh->getInventorySetting(gt);
            int sel = st.isSet(EInventorySetting::Stop) ? 3 :
                      st.isSet(EInventorySetting::Send) ? 2 :
                      st.isSet(EInventorySetting::Collect) ? 1 : 0;
            const float sw = std::min(app.dp(220), rr.w - app.dp(220));
            if(sw > app.dp(120)
               && ui.segmented(id("wh.set", static_cast<uint64_t>(gt)), {rr.right() - sw, rr.y + app.dp(6), sw, app.dp(36)},
                               {"–", "Ein", "Aus", "Stop"}, sel))
            {
                InventorySetting ns;
                if(sel == 1)
                    ns = InventorySetting(EInventorySetting::Collect);
                else if(sel == 2)
                    ns = InventorySetting(EInventorySetting::Send);
                else if(sel == 3)
                    ns = InventorySetting(EInventorySetting::Stop);
                s.SetInventorySetting(sheetPt, gt, ns);
            }
            ++row;
        }
        ui.endScroll(static_cast<float>(row) * rowH);
        by = c.bottom() - bh;
    }
    if(bt != BuildingType::Headquarters)
    {
        const Rect2 dr{c.x, std::min(by, c.bottom() - bh), c.w, bh};
        if(ui.button(id("bld.destroy"), dr, "Abreißen", Icon::Trash, Style::Danger))
        {
            const MapPoint pt = sheetPt;
            confirm(std::string(buildingName(bt)) + " abreißen?", [this, pt] {
                game.session().DestroyBuilding(pt);
                closeSheet();
            });
        }
    }
}

void Hud::Impl::sheetFlag(const Rect2& r)
{
    sheetHeader(r, "Flagge", Icon::Flag);
}

void Hud::Impl::sheetMessages(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Nachrichten", Icon::Mail);
    PostBox* box = world().getPostMgr().getPostBox(game.localPlayer());
    const std::string goal = box ? box->getCurrentMissionGoal() : std::string();
    float top = c.y;
    if(!goal.empty())
    {
        top += ui.textWrapped(c.x, c.y, c.w, "Ziel: " + goal, app.dp(14), ui.theme.accent) + app.dp(8);
    }
    const Rect2 view{c.x, top, c.w, c.bottom() - top};
    float y = ui.beginScroll(id("msg.scroll"), view);
    const float y0 = y;
    const unsigned n = box ? box->getNumMsgs() : 0;
    if(n == 0)
        ui.text(c.x, y, "Keine Nachrichten", app.dp(15), ui.theme.textDim);
    for(unsigned k = 0; k < n; ++k)
    {
        const PostMsg* msg = box->getMsg(n - 1 - k);
        if(!msg)
            continue;
        const float fs = app.dp(14);
        const auto lines = app.font().wrap(msg->getText(), fs, view.w - app.dp(70));
        const float h = std::max(app.dp(56), static_cast<float>(lines.size()) * fs * 1.3f + app.dp(30));
        const Rect2 row{view.x, y, view.w, h - app.dp(6)};
        app.batch().roundRect(row, app.dp(12), ui.pressed(id("msg", k), row) ? ui.theme.raised : Color::rgb(0xFFFFFF, 0.04f));
        float ty = row.y + app.dp(8);
        for(const auto& l : lines)
        {
            ui.text(row.x + app.dp(12), ty, l, fs, ui.theme.text);
            ty += fs * 1.3f;
        }
        ui.text(row.x + app.dp(12), ty, game.session().formatGFTime(msg->getSendFrame()), app.dp(11), ui.theme.textDim);
        if(msg->getPos().isValid())
        {
            ui.icon(Icon::Target, row.right() - app.dp(28), row.y + row.h / 2, app.dp(22), ui.theme.accent);
            if(ui.area(id("msg", k), row))
            {
                game.jumpTo(msg->getPos());
                closeSheet();
                return;
            }
        }
        y += h;
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetStats(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Statistik", Icon::Chart);
    static const std::vector<std::string> tabs = {"Vergleich", "Waren", "Gebäude", "Militär"};
    ui.segmented(id("stats.tab"), {c.x, c.y, c.w, app.dp(40)}, tabs, sheetTab);
    const Rect2 body{c.x, c.y + app.dp(50), c.w, c.h - app.dp(50)};
    auto& b = app.batch();
    if(sheetTab == 0)
    {
        static const std::vector<std::string> types = {"Land", "Gebäude", "Leute", "Militär", "Gold", "Produktiv"};
        static const StatisticType typeIds[] = {StatisticType::Country,  StatisticType::Buildings,
                                                StatisticType::Inhabitants, StatisticType::Military,
                                                StatisticType::Gold,     StatisticType::Productivity};
        static const std::vector<std::string> times = {"15 min", "1 h", "4 h", "16 h"};
        ui.segmented(id("stats.type"), {body.x, body.y, body.w, app.dp(38)}, types, statsType);
        ui.segmented(id("stats.time"), {body.x, body.y + app.dp(46), body.w, app.dp(34)}, times, statsTime);
        const Rect2 chart{body.x, body.y + app.dp(92), body.w, body.h - app.dp(130)};
        b.roundRect(chart, app.dp(10), Color::rgb(0xFFFFFF, 0.04f));
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
        float legendX = chart.x;
        for(unsigned p = 0; p < world().getNumPlayers(); ++p)
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
                const float x = chart.x + app.dp(8) + (chart.w - app.dp(16)) * static_cast<float>(i) / (NUM_STAT_STEPS - 1);
                const float y = chart.bottom() - app.dp(8)
                                - (chart.h - app.dp(16)) * static_cast<float>(st.data[type][idx]) / static_cast<float>(maxV);
                if(i > 0)
                    b.line(px, py, x, y, app.dp(2.5f), col);
                px = x;
                py = y;
            }
            b.circle(legendX + app.dp(6), chart.bottom() + app.dp(18), app.dp(5), col);
            ui.text(legendX + app.dp(16), chart.bottom() + app.dp(9), pl.name, app.dp(13), ui.theme.text);
            legendX += ui.textWidth(pl.name, app.dp(13)) + app.dp(32);
        }
        ui.text(chart.x + app.dp(8), chart.y + app.dp(6), std::to_string(maxV), app.dp(12), ui.theme.textDim);
    } else if(sheetTab == 1 || sheetTab == 2 || sheetTab == 3)
    {
        float y = ui.beginScroll(id("stats.scroll", static_cast<uint64_t>(sheetTab)), body);
        const float y0 = y;
        const float rowH = app.dp(40);
        if(sheetTab == 1)
        {
            const Inventory& inv = player().getInventory();
            unsigned maxV = 1;
            for(const auto gt : helpers::enumRange<GoodType>())
                maxV = std::max(maxV, static_cast<unsigned>(inv[gt]));
            for(const auto gt : helpers::enumRange<GoodType>())
            {
                if(gt == GoodType::Nothing || gt == GoodType::WaterEmpty || gt == GoodType::ShieldVikings
                   || gt == GoodType::ShieldAfricans || gt == GoodType::ShieldJapanese)
                    continue;
                ui.sprite(src(LOADER.getWareTex(gt)), {body.x, y + app.dp(4), app.dp(30), app.dp(30)});
                ui.text(body.x + app.dp(40), y + app.dp(10), wareName(gt), app.dp(14), ui.theme.text);
                const float bx = body.x + app.dp(150), bw = body.w - app.dp(200);
                b.roundRect({bx, y + app.dp(15), bw * static_cast<float>(inv[gt]) / static_cast<float>(maxV), app.dp(10)},
                            app.dp(5), ui.theme.accent);
                ui.text(body.right() - app.dp(8), y + app.dp(10), std::to_string(inv[gt]), app.dp(14), ui.theme.textDim,
                        gfx::Align::Right);
                y += rowH;
            }
        } else if(sheetTab == 2)
        {
            const BuildingCount bc = player().getBuildingRegister().getBuildingNums();
            for(unsigned i = 0; i < helpers::NumEnumValues_v<BuildingType>; ++i)
            {
                const auto bt = static_cast<BuildingType>(i);
                if(bc.buildings[bt] == 0 && bc.buildingSites[bt] == 0)
                    continue;
                ui.sprite(LOADER.getNationIcon(player().nation, bt), {body.x, y + app.dp(2), app.dp(36), app.dp(36)});
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
    gEditValid = false;
    if(ui.button(id("eco.dist"), g.next(), "Warenverteilung", Icon::Sliders))
        openSheet(SheetKind::Distribution, MapPoint::invalid(), true);
    if(ui.button(id("eco.trans"), g.next(), "Transportreihenfolge", Icon::List))
        openSheet(SheetKind::Transport, MapPoint::invalid(), true);
    if(ui.button(id("eco.tools"), g.next(), "Werkzeugproduktion", Icon::Tools))
        openSheet(SheetKind::Tools, MapPoint::invalid(), true);
    if(ui.button(id("eco.mil"), g.next(), "Militär", Icon::Shield))
        openSheet(SheetKind::Military, MapPoint::invalid(), true);
    if(ui.button(id("eco.inv"), g.next(), "Inventar", Icon::Boxes))
        openSheet(SheetKind::Inventory, MapPoint::invalid(), true);
}

namespace {
void ensureEdit(const GamePlayer& p)
{
    if(!gEditValid)
    {
        p.fillVisualSettings(gEdit);
        gEditValid = true;
    }
}
} // namespace

void Hud::Impl::sheetDistribution(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Warenverteilung", Icon::Sliders);
    ensureEdit(player());
    float y = ui.beginScroll(id("dist.scroll"), c);
    const float y0 = y;
    GoodType lastGood = GoodType::Nothing;
    for(unsigned i = 0; i < distributionMap.size(); ++i)
    {
        const GoodType gt = std::get<0>(distributionMap[i]);
        const BuildingType bt = std::get<1>(distributionMap[i]);
        if(gt != lastGood)
        {
            y += app.dp(6);
            ui.sprite(src(LOADER.getWareTex(gt)), {c.x, y, app.dp(28), app.dp(28)});
            ui.text(c.x + app.dp(36), y + app.dp(4), wareName(gt), app.dp(16), ui.theme.accent, gfx::Align::Left, true);
            y += app.dp(34);
            lastGood = gt;
        }
        ui.text(c.x + app.dp(8), y + app.dp(10), buildingName(bt), app.dp(14), ui.theme.text);
        float v = gEdit.distribution[i];
        const float sx = c.x + std::min(app.dp(160), c.w * 0.45f);
        if(ui.slider(id("dist", i), {sx, y, c.right() - sx, app.dp(40)}, v, 0, 10, 1))
        {
            gEdit.distribution[i] = static_cast<uint8_t>(v);
            game.session().ChangeDistribution(gEdit.distribution);
        }
        y += app.dp(42);
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetTransport(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Transportreihenfolge", Icon::List);
    ensureEdit(player());
    float y = ui.beginScroll(id("trans.scroll"), c);
    const float y0 = y;
    const float rowH = app.dp(52);
    for(unsigned i = 0; i < gEdit.transport_order.size(); ++i)
    {
        const uint8_t group = gEdit.transport_order[i];
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
        std::string shown = names;
        while(shown.size() > 4 && ui.textWidth(shown, app.dp(14)) > textW)
            shown = shown.substr(0, shown.size() - 4) + "…";
        ui.text(c.x + app.dp(68), y + app.dp(14), shown, app.dp(14), ui.theme.text);
        if(ui.button(id("trans.up", i), {c.right() - bs * 2 - app.dp(6), y + app.dp(4), bs, bs}, "", Icon::Up,
                     Style::Normal, i > 0))
        {
            std::swap(gEdit.transport_order[i], gEdit.transport_order[i - 1]);
            game.session().ChangeTransport(gEdit.transport_order);
        }
        if(ui.button(id("trans.down", i), {c.right() - bs, y + app.dp(4), bs, bs}, "", Icon::Down, Style::Normal,
                     i + 1 < gEdit.transport_order.size()))
        {
            std::swap(gEdit.transport_order[i], gEdit.transport_order[i + 1]);
            game.session().ChangeTransport(gEdit.transport_order);
        }
        y += rowH;
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetTools(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Werkzeugproduktion", Icon::Tools);
    ensureEdit(player());
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
        float v = gEdit.tools_settings[tool];
        const float sx = c.x + std::min(app.dp(160), c.w * 0.42f);
        if(ui.slider(id("tool", i), {sx, y, c.right() - sx, app.dp(42)}, v, 0, 10, 1))
        {
            gEdit.tools_settings[tool] = static_cast<uint8_t>(v);
            game.session().ChangeTools(gEdit.tools_settings);
        }
        y += app.dp(46);
        ++i;
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetMilitary(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Militär", Icon::Shield);
    ensureEdit(player());
    static const char* labels[] = {"Rekrutierung",          "Stärke der Verteidiger", "Anzahl Verteidiger",
                                   "Anzahl Angreifer",       "Besatzung Hinterland",   "Besatzung Landesmitte",
                                   "Besatzung Hafennähe",    "Besatzung Grenze"};
    float y = ui.beginScroll(id("mil.scroll"), c);
    const float y0 = y;
    for(unsigned i = 0; i < gEdit.military_settings.size(); ++i)
    {
        ui.text(c.x, y + app.dp(4), labels[i], app.dp(14), ui.theme.text);
        float v = gEdit.military_settings[i];
        if(ui.slider(id("mil", i), {c.x, y + app.dp(24), c.w, app.dp(40)}, v, 0,
                     static_cast<float>(MILITARY_SETTINGS_SCALE[i]), 1))
        {
            gEdit.military_settings[i] = static_cast<uint8_t>(v);
            game.session().ChangeMilitary(gEdit.military_settings);
        }
        y += app.dp(70);
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
            if(gt == GoodType::Nothing || gt == GoodType::WaterEmpty || gt == GoodType::ShieldVikings
               || gt == GoodType::ShieldAfricans || gt == GoodType::ShieldJapanese)
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
            if(j == Job::BoatCarrier)
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

void Hud::Impl::sheetSaveLoad(const Rect2& r, bool save)
{
    const Rect2 c = sheetHeader(r, save ? "Speichern" : "Laden", save ? Icon::Save : Icon::Folder);
    std::vector<std::pair<std::string, std::string>> slots; // file stem, label
    if(!save)
    {
        slots.emplace_back("autosave", "Automatisch");
        slots.emplace_back("quicksave", "Schnellspeicher");
    }
    for(int i = 1; i <= 8; ++i)
        slots.emplace_back("slot" + std::to_string(i), "Platz " + std::to_string(i));
    float y = ui.beginScroll(id(save ? "save.scroll" : "load.scroll"), c);
    const float y0 = y;
    const float rowH = app.dp(62);
    for(size_t i = 0; i < slots.size(); ++i)
    {
        const std::string path = savePath(slots[i].first);
        const bool exists = bfs::exists(path);
        if(!save && !exists)
            continue;
        const Rect2 row{c.x, y, c.w, rowH - app.dp(8)};
        app.batch().roundRect(row, app.dp(12),
                              ui.pressed(id("slot", i), row) ? ui.theme.raised : Color::rgb(0xFFFFFF, 0.05f));
        ui.icon(save ? Icon::Save : Icon::Folder, row.x + app.dp(26), row.y + row.h / 2, app.dp(22), ui.theme.accent);
        ui.text(row.x + app.dp(50), row.y + app.dp(8), slots[i].second, app.dp(16), ui.theme.text, gfx::Align::Left, true);
        ui.text(row.x + app.dp(50), row.y + app.dp(29), exists ? fileTime(path) : "leer", app.dp(13), ui.theme.textDim);
        if(ui.area(id("slot", i), row))
        {
            if(save)
            {
                auto doSave = [this, path] {
                    toast(game.saveTo(path) ? "Gespeichert" : "Speichern fehlgeschlagen");
                    closeSheet();
                };
                if(exists)
                    confirm("Spielstand überschreiben?", doSave);
                else
                    doSave();
            } else
            {
                App& a = app;
                a.post([&a, path] {
                    try
                    {
                        a.setScreen(createGameScreen(a, Session::loadGame(path, 1)));
                    } catch(const std::exception&)
                    {}
                });
            }
        }
        y += rowH;
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetSettings(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Einstellungen", Icon::Gear);
    float y = c.y;
    auto& st = app.settings();
    ui.text(c.x, y, "Geräusche", app.dp(15), ui.theme.text);
    float ev = app.audio().effectVolume();
    if(ui.slider(id("set.fx"), {c.x, y + app.dp(22), c.w, app.dp(40)}, ev))
    {
        app.audio().setEffectVolume(ev);
        st.setFloat("volume.effects", ev);
    }
    y += app.dp(72);
    ui.text(c.x, y, "Musik", app.dp(15), ui.theme.text);
    float mv = app.audio().musicVolume();
    if(ui.slider(id("set.music"), {c.x, y + app.dp(22), c.w, app.dp(40)}, mv))
    {
        app.audio().setMusicVolume(mv);
        st.setFloat("volume.music", mv);
    }
    y += app.dp(72);
    bool birds = AUDIODRIVER.birdsEnabled;
    if(ui.toggle(id("set.birds"), {c.x, y, c.w, app.dp(48)}, birds, "Vogelgezwitscher"))
    {
        AUDIODRIVER.birdsEnabled = birds;
        st.setBool("sound.birds", birds);
    }
    y += app.dp(56);
    if(ui.button(id("set.tut"), {c.x, y, c.w, app.dp(52)}, "Einführung starten", Icon::Help))
    {
        closeSheet();
        tutorial = std::make_unique<Tutorial>(*this);
    }
}

void Hud::Impl::sheetAttack(const Rect2& r)
{
    const noBase* obj = world().getNO(sheetPt);
    const auto* mil = dynamic_cast<const nobBaseMilitary*>(obj);
    if(!mil)
    {
        closeSheet();
        return;
    }
    const Rect2 c = sheetHeader(r, "Angriff", Icon::Sword);
    float y = c.y;
    ui.text(c.x, y, std::string(buildingName(mil->getBuildingType())) + " von " + world().getPlayer(mil->getPlayer()).name,
            app.dp(16), ui.theme.text);
    y += app.dp(30);
    const unsigned avail = game.viewer().getNumSoldiersForAttack(sheetPt);
    if(avail == 0)
    {
        ui.textWrapped(c.x, y, c.w, "Keine Soldaten in Reichweite verfügbar.", app.dp(15), ui.theme.textDim);
        return;
    }
    int n = static_cast<int>(std::clamp<unsigned>(attackSoldiers, 1, avail));
    ui.stepper(id("atk.n"), {c.x, y, c.w, app.dp(48)}, n, 1, static_cast<int>(avail),
               "Soldaten (max. " + std::to_string(avail) + ")");
    attackSoldiers = static_cast<unsigned>(n);
    y += app.dp(58);
    int strong = attackStrong ? 0 : 1;
    if(ui.segmented(id("atk.str"), {c.x, y, c.w, app.dp(44)}, {"Starke zuerst", "Schwache zuerst"}, strong))
        attackStrong = strong == 0;
    y += app.dp(56);
    if(ui.button(id("atk.go"), {c.x, y, c.w, app.dp(56)}, "Angreifen", Icon::Sword, Style::Danger))
    {
        game.session().Attack(sheetPt, attackSoldiers, attackStrong);
        toast(std::to_string(attackSoldiers) + " Soldaten greifen an");
        closeSheet();
    }
}

void Hud::Impl::sheetMission(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, missionTitle.empty() ? "Mission" : missionTitle, Icon::Info);
    const Rect2 view{c.x, c.y, c.w, c.h - app.dp(64)};
    float y = ui.beginScroll(id("mission.scroll"), view);
    const float h = ui.textWrapped(c.x, y, c.w, missionText, app.dp(16), ui.theme.text);
    ui.endScroll(h);
    if(ui.button(id("mission.ok"), {c.x, c.bottom() - app.dp(54), c.w, app.dp(54)}, "OK", Icon::Check, Style::Primary))
        closeSheet();
}

void Hud::Impl::sheetGameOver(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Spielende", Icon::Trophy);
    ui.textWrapped(c.x, c.y + app.dp(10), c.w, gameOverText, app.dp(20), ui.theme.text);
    Grid g{{c.x, c.bottom() - app.dp(124), c.w, app.dp(124)}, 1, app.dp(54), app.dp(12)};
    if(ui.button(id("go.cont"), g.next(), "Weiterspielen", Icon::Play))
        closeSheet();
    if(ui.button(id("go.menu"), g.next(), "Hauptmenü", Icon::Exit, Style::Primary))
    {
        App& a = app;
        a.post([&a] { a.setScreen(createMainMenu(a)); });
    }
}

void Hud::Impl::sheetHelp(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Hilfe", Icon::Help);
    const Rect2 view{c.x, c.y, c.w, c.h - app.dp(64)};
    float y = ui.beginScroll(id("help.scroll"), view);
    const float y0 = y;
    static const char* items[][2] = {
      {"Verschieben", "Ein Finger ziehen (mit Schwung). Maus: linke Taste ziehen."},
      {"Zoomen", "Zwei Finger auseinander/zusammen, Doppeltipp vergrößert. Maus: Rad."},
      {"Zurück", "Tipp mit zwei Fingern, Rechtsklick oder Esc schließt Menüs und bricht Aktionen ab."},
      {"Bauen", "Bauplatz antippen: Kreismenü mit passenden Gebäuden, zuletzt benutzte zuerst."},
      {"Straßen", "Von einer Flagge aus ziehen, der Weg folgt dem Finger. Flaggen werden automatisch gesetzt."},
      {"Gebäude", "Antippen öffnet Details: Produktion, Soldaten, Lager, Abriss."},
      {"Infos", "Lange drücken zeigt, was an einer Stelle ist."},
      {"Tastatur", "Leertaste Pause, +/- Tempo, F5/F9 Schnellspeichern/-laden, B Bauplätze, M Karte, H Hauptquartier."},
    };
    for(const auto& it : items)
    {
        ui.text(c.x, y, it[0], app.dp(16), ui.theme.accent, gfx::Align::Left, true);
        y += app.dp(24);
        y += ui.textWrapped(c.x, y, c.w, it[1], app.dp(14), ui.theme.text) + app.dp(12);
    }
    ui.endScroll(y - y0);
    if(ui.button(id("help.tut"), {c.x, c.bottom() - app.dp(54), c.w, app.dp(54)}, "Einführung starten", Icon::Play,
                 Style::Primary))
    {
        sheetStack.clear();
        closeSheet();
        tutorial = std::make_unique<Tutorial>(*this);
    }
}

} // namespace weiler
