// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

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

namespace bfs = boost::filesystem;

namespace weiler {

using namespace ui;

namespace {
/// Wares, jobs and buildings of disabled addons (and internal goods) are not shown
template<typename T>
bool unused(const GameWorldBase& w, T value, std::function<bool(T)> (*make)(const GlobalGameSettings&))
{
    static const GlobalGameSettings* cached = nullptr;
    static std::function<bool(T)> fn;
    if(cached != &w.getGGS())
    {
        cached = &w.getGGS();
        fn = make(*cached);
    }
    return fn(value);
}
} // namespace

bool hiddenWare(const GameWorldBase& w, GoodType gt)
{
    return gt == GoodType::Nothing || gt == GoodType::WaterEmpty || gt == GoodType::ShieldVikings
           || gt == GoodType::ShieldAfricans || gt == GoodType::ShieldJapanese
           || unused<GoodType>(w, gt, [](const GlobalGameSettings& g) { return makeIsUnusedWare(g); });
}
bool hiddenJob(const GameWorldBase& w, Job j)
{
    return unused<Job>(w, j, [](const GlobalGameSettings& g) { return makeIsUnusedJob(g); });
}
bool hiddenBuilding(const GameWorldBase& w, BuildingType bt)
{
    return unused<BuildingType>(w, bt, [](const GlobalGameSettings& g) { return makeIsUnusedBuilding(g); });
}

const ISpriteSource* src(ITexture* t)
{
    return dynamic_cast<const ISpriteSource*>(t);
}

namespace {
std::string savePath(const std::string& name)
{
    return (RTTRCONFIG.expandPath(s25::folders::save) / (name + ".sav")).string();
}
} // namespace

Rect2 Hud::Impl::sheetHeader(const Rect2& r, const std::string& title, Icon ic)
{
    const auto& th = ui.theme;
    const float pad = app.dp(th.space.xl) - app.dp(4);
    const float top = landscape() ? app.dp(6) : app.dp(14);
    const float h = app.dp(58);
    float tx = r.x + pad;
    if(ic != Icon::None)
    {
        const float d = app.dp(38);
        const float cx = tx + d / 2, cy = r.y + top + h / 2;
        app.batch().circle(cx, cy, d / 2, th.accentSoft);
        ui.icon(ic, cx, cy, app.dp(20), th.accent);
        tx += d + app.dp(12);
    }
    const float fs = app.dp(th.type.title);
    ui.heading(tx, r.y + top + (h - fs * 1.3f) / 2, title, fs, th.text);
    const float bs = app.dp(44);
    const Icon closeIcon = sheetStack.empty() ? Icon::Close : Icon::Back;
    if(ui.iconButton(id("sheet.close"), {r.right() - bs - app.dp(10), r.y + top + (h - bs) / 2, bs, bs}, closeIcon,
                     false, 0, Style::Flat))
        closeSheet();
    const float ly = r.y + top + h + app.dp(2);
    app.batch().roundRectV({r.x + pad, ly, r.w - 2 * pad, 1}, 0, th.line, th.line);
    return {r.x + pad, ly + app.dp(12), r.w - 2 * pad, r.bottom() - ly - app.dp(12) - app.dp(th.space.lg)};
}

void Hud::Impl::drawSheet()
{
    if(sheet != SheetKind::None)
    {
        Rect2 r = sheetRect();
        const float t = Ui::ease(std::min(1.f, static_cast<float>((app.time() - sheetTime) / ui.theme.motion.normal)));
        if(landscape())
            r.x += (1 - t) * (r.w + app.dp(24));
        else
            r.y += (1 - t) * (r.h + app.dp(24));
        ui.panel(r, app.dp(ui.theme.radius.xl), ui.theme.glassStrong);
        if(!landscape())
            app.batch().roundRect({r.x + r.w / 2 - app.dp(18), r.y + app.dp(7), app.dp(36), app.dp(4)}, app.dp(2),
                                  ui.theme.text.withAlpha(0.22f));
        const SheetKind drawn = sheet;
        fitReq = 0;
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
            case SheetKind::Ship: sheetShip(r); break;
            case SheetKind::None: break;
        }
        if(sheet == drawn)
            fitH = fitReq <= 0 ? 0 : fitH <= 0 ? fitReq : fitH + (fitReq - fitH) * 0.3f;
    }
    else if(app.time() - sheetCloseTime < ui.theme.motion.normal)
    {
        // Closing: the empty panel slides out (its content may refer to objects that are gone)
        const float t = Ui::ease(static_cast<float>((app.time() - sheetCloseTime) / ui.theme.motion.normal));
        Rect2 r = sheetCloseRect;
        if(landscape())
            r.x += t * (r.w + app.dp(24));
        else
            r.y += t * (r.h + app.dp(24));
        ui.panel(r, app.dp(ui.theme.radius.xl), ui.theme.glassStrong, 1.f - 0.5f * t);
    }
    if(confirmAction)
    {
        const float W = static_cast<float>(app.width()), H = static_cast<float>(app.height());
        const float t = std::min(1.f, static_cast<float>((app.time() - confirmTime) / ui.theme.motion.normal));
        app.batch().quad(0, 0, 0, W, H, 0, 0, 1, 1, ui.theme.scrim);
        ui.block({0, 0, W, H});
        const float w = std::min(W - app.dp(40), app.dp(420));
        const float fs = app.dp(ui.theme.type.label);
        const auto lines = app.font().wrap(confirmText, fs, w - app.dp(48));
        const float h = app.dp(120) + static_cast<float>(lines.size()) * fs * 1.3f;
        const float sc = 0.94f + 0.06f * Ui::easeBack(t);
        const Rect2 r{(W - w * sc) / 2, (H - h * sc) / 2, w * sc, h * sc};
        ui.panel(r, app.dp(ui.theme.radius.xl), ui.theme.glassStrong);
        ui.textWrapped(r.x + app.dp(24), r.y + app.dp(26), r.w - app.dp(48), confirmText, fs, ui.theme.text);
        const float bh = app.dp(50), bw = (r.w - app.dp(60)) / 2;
        if(ui.button(id("confirm.no"), {r.x + app.dp(24), r.bottom() - bh - app.dp(22), bw, bh}, "Abbrechen"))
            confirmAction = nullptr;
        if(ui.button(id("confirm.yes"), {r.right() - bw - app.dp(24), r.bottom() - bh - app.dp(22), bw, bh}, "OK",
                     Icon::None, Style::Primary))
        {
            auto fn = std::move(confirmAction);
            confirmAction = nullptr;
            fn();
        }
    }
}

void Hud::Impl::sheetGameMenu(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Menü", Icon::Menu);
    Grid g{c, 3, app.dp(landscape() ? 80 : 88), app.dp(10)};
    if(ui.button(id("gm.resume"), g.next(), "Weiter", Icon::Play, Style::TilePrimary))
        closeSheet();
    if(ui.button(id("gm.save"), g.next(), "Speichern", Icon::Save, Style::Tile))
        openSheet(SheetKind::Save, MapPoint::invalid(), true);
    if(ui.button(id("gm.load"), g.next(), "Laden", Icon::Folder, Style::Tile))
        openSheet(SheetKind::Load, MapPoint::invalid(), true);
    if(ui.button(id("gm.eco"), g.next(), "Wirtschaft", Icon::Sliders, Style::Tile))
        openSheet(SheetKind::Economy, MapPoint::invalid(), true);
    if(ui.button(id("gm.stats"), g.next(), "Statistik", Icon::Chart, Style::Tile))
        openSheet(SheetKind::Stats, MapPoint::invalid(), true);
    if(ui.button(id("gm.msg"), g.next(), "Nachrichten", Icon::Mail, Style::Tile))
        openSheet(SheetKind::Messages, MapPoint::invalid(), true);
    if(ui.button(id("gm.settings"), g.next(), "Einstellungen", Icon::Gear, Style::Tile))
        openSheet(SheetKind::Settings, MapPoint::invalid(), true);
    if(ui.button(id("gm.help"), g.next(), "Hilfe", Icon::Help, Style::Tile))
        openSheet(SheetKind::Help, MapPoint::invalid(), true);
    if(!missionText.empty() && ui.button(id("gm.mission"), g.next(), "Auftrag", Icon::Info, Style::Tile))
        openSheet(SheetKind::Mission, MapPoint::invalid(), true);
    if(ui.button(id("gm.quit"), g.next(), "Hauptmenü", Icon::Exit, Style::Tile))
    {
        confirm("Partie verlassen? Der Stand wird als Autosave gesichert.", [this] {
            game.saveTo(savePath("autosave"));
            App& a = app;
            a.post([&a] { a.setScreen(createMainMenu(a)); });
        });
    }
    fit(r, g.area.y + static_cast<float>((g.n + 2) / 3) * (g.h + g.gap));
}

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
    // Fixed order: by category, then roughly by the order of the production chains
    static const BuildingType order[] = {
      BuildingType::Woodcutter,  BuildingType::Forester,      BuildingType::Quarry,       BuildingType::Sawmill,
      BuildingType::CoalMine,    BuildingType::IronMine,      BuildingType::GoldMine,     BuildingType::GraniteMine,
      BuildingType::Charburner,  BuildingType::Fishery,       BuildingType::Hunter,       BuildingType::Well,
      BuildingType::Farm,        BuildingType::Mill,          BuildingType::Bakery,       BuildingType::PigFarm,
      BuildingType::Slaughterhouse, BuildingType::Brewery,    BuildingType::Vineyard,     BuildingType::Winery,
      BuildingType::Ironsmelter, BuildingType::Armory,        BuildingType::Metalworks,   BuildingType::Mint,
      BuildingType::DonkeyBreeder, BuildingType::Storehouse,  BuildingType::Shipyard,     BuildingType::HarborBuilding,
      BuildingType::Skinner,     BuildingType::Tannery,       BuildingType::LeatherWorks, BuildingType::Temple,
      BuildingType::Barracks,    BuildingType::Guardhouse,    BuildingType::Watchtower,   BuildingType::Fortress,
      BuildingType::LookoutTower, BuildingType::Catapult};
    auto rank = [](BuildingType bt) {
        const auto* it = std::find(std::begin(order), std::end(order), bt);
        return static_cast<int>(it - std::begin(order));
    };
    auto all = fittingBuildings(*this, sheetPt);
    std::stable_sort(all.begin(), all.end(), [&](BuildingType a, BuildingType b) {
        return std::make_pair(category(a), rank(a)) < std::make_pair(category(b), rank(b));
    });
    const Rect2 view{c.x, c.y + app.dp(50), c.w, c.bottom() - c.y - app.dp(50)};
    float y0 = ui.beginScroll(id("build.scroll"), view);
    const float cell = app.dp(82);
    const int cols = std::max(1, static_cast<int>(view.w / cell));
    const float cw = view.w / static_cast<float>(cols);
    int n = 0;
    const Nation nation = player().nation;
    for(BuildingType bt : all)
    {
        if(sheetTab != 0 && category(bt) != sheetTab)
            continue;
        const Rect2 cr{view.x + static_cast<float>(n % cols) * cw, y0 + static_cast<float>(n / cols) * app.dp(100),
                       cw, app.dp(96)};
        ++n;
        if(ui.pressed(id("build", static_cast<uint64_t>(bt)), cr))
            app.batch().roundRect(cr.inset(app.dp(3)), app.dp(14), ui.theme.raised);
        buildingIcon(nation, bt, {cr.x + app.dp(8), cr.y + app.dp(6), cr.w - app.dp(16), app.dp(56)});
        ui.textWrapped(cr.x + app.dp(2), cr.y + app.dp(66), cr.w - app.dp(4), buildingName(bt), app.dp(11.5f),
                       ui.theme.text, gfx::Align::Center);
        ui.tag(std::string("build:") + buildingName(bt), cr);
        if(ui.area(id("build", static_cast<uint64_t>(bt)), cr))
        {
            game.session().SetBuildingSite(sheetPt, bt);
            noteRecent(bt);
            if(app.settings().getBool("build.autoconnect", true) && !tutorial)
            {
                pendingConnect = world().getNeighbour(sheetPt, Direction::SouthEast);
                pendingConnectTime = app.time();
            }
            if(tutorial)
                tutorial->onBuild(bt);
            closeSheet();
            break;
        }
    }
    ui.endScroll(static_cast<float>((n + cols - 1) / cols) * app.dp(100));
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
    buildingIcon(bld->getNation(), bt, {c.x, c.y, picW, picW * 0.9f});
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
            const bool expl = harbor->isExplorationExpeditionActive();
            if(ui.button(id("bld.expl"), {c.x, by, c.w, bh}, expl ? "Erkundung abbrechen" : "Erkundungsfahrt vorbereiten",
                         Icon::Eye))
                s.StartStopExplorationExpedition(sheetPt, !expl);
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
            if(hiddenWare(world(), gt))
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
        if(!dynamic_cast<const nobBaseWarehouse*>(obj))
            fit(r, dr.bottom());
    }
}

void Hud::Impl::sheetFlag(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Flagge", Icon::Flag);
    fit(r, c.y);
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
        const bool rowHit = ui.row(id("msg", k), row);
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
            if(rowHit)
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
        const bool rowHit = ui.row(id("slot", i), row);
        float tx = row.x + app.dp(50);
        const gfx::Texture* thumb = exists ? app.imageTexture(bfs::path(path).replace_extension(".png").string()) : nullptr;
        if(thumb)
        {
            const float th = row.h - app.dp(12);
            ui.picture(*thumb, {row.x + app.dp(10), row.y + app.dp(6), th * 1.5f, th});
            tx = row.x + app.dp(20) + th * 1.5f;
        } else
            ui.icon(save ? Icon::Save : Icon::Folder, row.x + app.dp(26), row.y + row.h / 2, app.dp(22), ui.theme.accent);
        ui.text(tx, row.y + app.dp(8), slots[i].second, app.dp(16), ui.theme.text, gfx::Align::Left, true);
        ui.text(tx, row.y + app.dp(29), exists ? saveDescription(path) : "leer", app.dp(13), ui.theme.textDim);
        ui.tag(slots[i].second, row);
        if(rowHit)
        {
            if(save)
            {
                auto doSave = [this, path] {
                    toast(game.saveTo(path) ? "Gespeichert" : "Speichern fehlgeschlagen");
                    sheetStack.clear();
                    closeSheet();
                };
                if(exists)
                    confirm("Spielstand überschreiben?", doSave);
                else
                    doSave();
            } else
            {
                game.loadFrom(path);
            }
        }
        y += rowH;
    }
    ui.endScroll(y - y0);
}

void Hud::Impl::sheetSettings(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, "Einstellungen", Icon::Gear);
    float y = ui.beginScroll(id("set.scroll"), c);
    const float y0 = y;
    auto& st = app.settings();
    y += drawAudioSettings(app, c.x, y, c.w - app.dp(8));
    y += drawGraphicsSettings(app, c.x, y, c.w - app.dp(8));
    bool birds = AUDIODRIVER.birdsEnabled;
    if(ui.toggle(id("set.birds"), {c.x, y, c.w, app.dp(48)}, birds, "Vogelgezwitscher"))
    {
        AUDIODRIVER.birdsEnabled = birds;
        st.setBool("sound.birds", birds);
    }
    y += app.dp(56);
    if(ui.toggle(id("set.names"), {c.x, y, c.w, app.dp(48)}, showNames, "Gebäudenamen anzeigen"))
        st.setBool("show.names", showNames);
    y += app.dp(56);
    bool autoRoad = st.getBool("build.autoconnect", true);
    if(ui.toggle(id("set.autoroad"), {c.x, y, c.w, app.dp(48)}, autoRoad, "Neue Gebäude anschließen"))
        st.setBool("build.autoconnect", autoRoad);
    y += app.dp(56);
    bool fps = st.getBool("show.fps");
    if(ui.toggle(id("set.fps"), {c.x, y, c.w, app.dp(48)}, fps, "Bildrate anzeigen"))
        st.setBool("show.fps", fps);
    y += app.dp(56);
    if(ui.button(id("set.tut"), {c.x, y, c.w, app.dp(52)}, "Einführung starten", Icon::Help))
    {
        closeSheet();
        tutorial = std::make_unique<Tutorial>(*this);
    }
    ui.endScroll(y + app.dp(52) - y0);
    fit(r, c.y + y + app.dp(52) - y0);
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
        fit(r, y + app.dp(30));
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
        toast(attackSoldiers == 1 ? "1 Soldat greift an" : std::to_string(attackSoldiers) + " Soldaten greifen an");
        closeSheet();
    }
    fit(r, y + app.dp(56));
}

void Hud::Impl::sheetMission(const Rect2& r)
{
    const Rect2 c = sheetHeader(r, missionTitle.empty() ? "Mission" : missionTitle, Icon::Info);
    // Portrait of the narrator (from the game data) in a frame beside the text
    const ISpriteSource* img = nullptr;
    if(!missionImageFile.empty())
        img = src(LOADER.getTextureN(ResourceId::make(missionImageFile), static_cast<unsigned>(missionImageIdx)));
    float tx = c.x;
    if(img)
    {
        const float pw = app.dp(landscape() ? 84 : 92);
        const Rect2 frame{c.x, c.y + app.dp(2), pw, pw * 1.18f};
        app.batch().roundRect(frame, app.dp(ui.theme.radius.md), Color::rgb(0x000000, 0.28f));
        ui.sprite(img, frame.inset(app.dp(6)));
        app.batch().roundRect(frame, app.dp(ui.theme.radius.md), ui.theme.accent.withAlpha(0.55f), 1.f, app.dp(1.5f));
        tx = frame.right() + app.dp(16);
    }
    const Rect2 view{tx, c.y, c.right() - tx, c.h - app.dp(64)};
    float y = ui.beginScroll(id("mission.scroll"), view);
    const float h = ui.textWrapped(view.x, y, view.w - app.dp(6), missionText, app.dp(ui.theme.type.body),
                                   ui.theme.text);
    ui.endScroll(h);
    if(ui.button(id("mission.ok"), {c.x, c.bottom() - app.dp(54), c.w, app.dp(54)}, "OK", Icon::Check, Style::Primary))
        closeSheet();
}

void Hud::Impl::sheetGameOver(const Rect2& r)
{
    const auto& th = ui.theme;
    auto& b = app.batch();
    const float pad = app.dp(th.space.xl);
    const Rect2 c{r.x + pad, r.y + app.dp(26), r.w - 2 * pad, r.h - app.dp(40)};
    // Emblem: ring with trophy (victory) or shield (defeat), with a soft glow that breathes slowly
    const float t = Ui::ease(std::min(1.f, static_cast<float>((app.time() - sheetTime) / th.motion.slow)));
    const float glow = 0.5f + 0.5f * std::sin(static_cast<float>(app.time()) * 1.6f);
    const Color tone = gameOverWin ? th.accent : th.danger;
    const float er = app.dp(38) * (0.7f + 0.3f * Ui::easeBack(t));
    const float cx = c.x + c.w / 2, cy = c.y + app.dp(44);
    b.circle(cx, cy, er * 1.9f, tone.withAlpha(0.10f + 0.06f * glow), 0, er);
    b.circle(cx, cy, er, tone.withAlpha(0.16f));
    b.circle(cx, cy, er, tone, app.dp(2));
    ui.icon(gameOverWin ? Icon::Trophy : Icon::Shield, cx, cy, er * 1.05f, tone);
    float y = cy + app.dp(52);
    const float hs = app.dp(th.type.headline + 4);
    ui.heading(cx, y, gameOverWin ? "Sieg" : "Niederlage", hs, th.text, gfx::Align::Center);
    y += hs * 1.3f + app.dp(2);
    y += ui.textWrapped(c.x, y, c.w, gameOverText, app.dp(th.type.body), th.textDim, gfx::Align::Center) + app.dp(18);
    // Three key figures of the own realm
    const auto& reg = player().getBuildingRegister();
    unsigned buildings = static_cast<unsigned>(reg.getMilitaryBuildings().size());
    buildings += static_cast<unsigned>(reg.getStorehouses().size());
    for(const auto bt : helpers::enumRange<BuildingType>())
    {
        if(BuildingProperties::isUsual(bt))
            buildings += static_cast<unsigned>(reg.getBuildings(bt).size());
    }
    const Inventory& inv = player().getInventory();
    unsigned soldiers = 0;
    for(Job j : {Job::Private, Job::PrivateFirstClass, Job::Sergeant, Job::Officer, Job::General})
        soldiers += inv[j];
    const std::pair<std::string, std::string> stats[] = {
      {game.session().formatGFTime(game.session().getGF()), "Spielzeit"},
      {std::to_string(buildings), "Gebäude"},
      {std::to_string(soldiers), "Soldaten"}};
    const float gap = app.dp(10), sw = (c.w - 2 * gap) / 3, sh = app.dp(70);
    for(int i = 0; i < 3; ++i)
    {
        const Rect2 sr{c.x + static_cast<float>(i) * (sw + gap), y, sw, sh};
        b.roundRect(sr, app.dp(th.radius.md), th.surface);
        ui.text(sr.x + sw / 2, sr.y + app.dp(12), stats[i].first, app.dp(th.type.title - 2), th.text, gfx::Align::Center,
                true);
        ui.text(sr.x + sw / 2, sr.y + app.dp(44), stats[i].second, app.dp(th.type.caption), th.textDim,
                gfx::Align::Center);
    }
    y += sh + app.dp(20);
    const float bh = app.dp(th.size.button), bw = (c.w - gap) / 2;
    if(ui.button(id("go.cont"), {c.x, y, bw, bh}, "Weiterspielen", Icon::Play))
        closeSheet();
    if(ui.button(id("go.menu"), {c.x + bw + gap, y, bw, bh}, "Hauptmenü", Icon::Exit, Style::Primary))
    {
        App& a = app;
        a.post([&a] { a.setScreen(createMainMenu(a)); });
    }
    fit(r, y + bh + app.dp(8));
}

void Hud::Impl::sheetShip(const Rect2& r)
{
    const noShip* ship = player().getShipByID(shipId);
    if(!ship)
    {
        closeSheet();
        return;
    }
    const Rect2 c = sheetHeader(r, "Schiff " + ship->getName(), Icon::Ship);
    float y = c.y;
    std::string state = "Wartet im Hafen";
    if(ship->isWaitingForExpeditionInstructions())
        state = "Expedition wartet auf Befehle";
    else if(ship->isOnExpedition())
        state = "Auf Expedition";
    else if(ship->isOnExplorationExpedition())
        state = "Auf Erkundungsfahrt";
    else if(ship->isOnAttackMission())
        state = "Bringt Soldaten zum Angriff";
    else if(ship->isLoading())
        state = "Wird beladen";
    else if(ship->isUnloading())
        state = "Wird entladen";
    else if(!ship->isIdling())
        state = "Unterwegs";
    ui.text(c.x, y, state, app.dp(16), ui.theme.text);
    y += app.dp(32);
    if(ship->isWaitingForExpeditionInstructions())
    {
        Session& s = game.session();
        const float bh = app.dp(52);
        if(ui.button(id("ship.colony"), {c.x, y, c.w, bh}, "Kolonie gründen", Icon::Flag, Style::Primary,
                     ship->isAbleToFoundColony()))
        {
            s.foundColony(shipId);
            closeSheet();
            return;
        }
        y += bh + app.dp(10);
        ui.text(c.x, y, "Weiterfahren nach", app.dp(13), ui.theme.textDim);
        y += app.dp(22);
        // Compass rose: six directions like the hexagonal map
        static const struct
        {
            ShipDirection dir;
            const char* label;
            int col, row;
        } dirs[] = {{ShipDirection::NorthWest, "Nordwest", 0, 0}, {ShipDirection::North, "Nord", 1, 0},
                    {ShipDirection::NorthEast, "Nordost", 2, 0},   {ShipDirection::SouthWest, "Südwest", 0, 1},
                    {ShipDirection::South, "Süd", 1, 1},           {ShipDirection::SouthEast, "Südost", 2, 1}};
        const float gap = app.dp(8), bw = (c.w - 2 * gap) / 3;
        for(const auto& d : dirs)
        {
            const Rect2 br{c.x + static_cast<float>(d.col) * (bw + gap), y + static_cast<float>(d.row) * (bh + gap), bw,
                           bh};
            // Only directions with a free harbor spot on the way can be chosen
            const bool reachable = static_cast<bool>(
              world().getNextFreeHarborPoint(ship->getPos(), ship->getCurrentHarbor(), d.dir, ship->getPlayerId()));
            if(ui.button(id("ship.dir", static_cast<uint64_t>(d.dir)), br, d.label, Icon::None, Style::Normal,
                         reachable))
            {
                s.travelToNextSpot(d.dir, shipId);
                closeSheet();
                return;
            }
        }
        y += 2 * (bh + gap) + app.dp(4);
        if(ui.button(id("ship.cancel"), {c.x, y, c.w, bh}, "Expedition abbrechen", Icon::Close, Style::Danger))
        {
            s.CancelExpedition(shipId);
            closeSheet();
            return;
        }
        y += bh;
    }
    fit(r, y);
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
      {"Straßen", "Von einer Flagge aus ziehen, der Weg folgt dem Finger. Flaggen werden automatisch gesetzt. "
                  "Neue Gebäude werden mit der nächsten Flagge verbunden (abschaltbar), im Flaggenmenü "
                  "verbindet „Verbinden“ eine Flagge auf kürzestem Weg."},
      {"Angriff", "Feindliches Militärgebäude antippen, Anzahl der Soldaten wählen, Angreifen."},
      {"Schiffe", "Eigenes Schiff antippen: Expeditionen steuern, Kolonie gründen."},
      {"Gebäude", "Antippen öffnet Details: Produktion, Soldaten, Lager, Abriss."},
      {"Infos", "Lange drücken zeigt, was an einer Stelle ist."},
      {"Tastatur", "Leertaste Pause, +/- Tempo, F5/F9 Schnellspeichern/-laden, B Bauplätze, M Karte, "
                   "H Hauptquartier, N Nachrichten, P Gebäudenamen."},
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
