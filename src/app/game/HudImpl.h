// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

// Internal state of the in-game interface, shared by Hud.cpp, Sheets.cpp and Tutorial.cpp

#include "App.h"
#include "GameScreen.h"
#include "Hud.h"
#include "ui/Ui.h"
#include "gameTypes/BuildingType.h"
#include "gameTypes/Direction.h"
#include "gameTypes/MapCoordinates.h"
#include "gameTypes/GoodTypes.h"
#include "gameTypes/JobTypes.h"
#include "gameTypes/VisualSettings.h"
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GamePlayer;
class GameWorldBase;
class ITexture;
class ISpriteSource;
class noBase;
class PostBox;

namespace weiler {

class Tutorial;

enum class SheetKind
{
    None,
    GameMenu,
    Build,
    Building,
    Flag,
    Messages,
    Stats,
    Economy,
    Distribution,
    Transport,
    Tools,
    Military,
    Inventory,
    Save,
    Load,
    Settings,
    Attack,
    Mission,
    GameOver,
    Help,
    Ship
};

struct RingItem
{
    enum class Kind
    {
        Building,
        Flag,
        Road,
        WaterRoad,
        Geologist,
        Scout,
        DestroyFlag,
        DestroyRoad,
        UpgradeRoad,
        Connect,
        More
    } kind;
    BuildingType bt = BuildingType::Headquarters;
    std::string label;
};

/// Grid of big menu buttons
struct Grid
{
    ui::Rect2 area;
    int cols;
    float h, gap;
    int n = 0;
    ui::Rect2 next()
    {
        const float w = (area.w - gap * static_cast<float>(cols - 1)) / static_cast<float>(cols);
        const int c = n % cols, r = n / cols;
        ++n;
        return {area.x + static_cast<float>(c) * (w + gap), area.y + static_cast<float>(r) * (h + gap), w, h};
    }
};

/// Wares, jobs and buildings of disabled addons (and internal goods) are not shown
bool hiddenWare(const GameWorldBase& w, GoodType gt);
bool hiddenJob(const GameWorldBase& w, Job j);
bool hiddenBuilding(const GameWorldBase& w, BuildingType bt);
/// Sprite source of a texture of the core (nullptr if it is none)
const ISpriteSource* src(ITexture* t);

struct Hud::Impl
{
    Impl(GameScreen& g, App& a);
    ~Impl();

    GameScreen& game;
    App& app;
    ui::Ui& ui;

    // ---- Gestures on the world
    struct Finger
    {
        float x, y, x0, y0;
        double t0;
    };
    std::map<uint64_t, Finger> fingers;
    float velX = 0, velY = 0;
    float pinchDist0 = 0, pinchZoom0 = 1;
    bool gestureMoved = false, gestureMulti = false, longPressDone = false;
    uint64_t primaryId = 0;
    double lastTapTime = -10;
    float lastTapX = 0, lastTapY = 0;
    double lastInput = 0;
    float mouseX = -1, mouseY = -1;

    // ---- Road building
    struct Road
    {
        bool active = false;
        bool water = false;
        bool fingerDrawing = false;
        MapPoint start, point;
        std::vector<Direction> route;
    } road;

    // ---- Overlays
    bool showBQ = false;
    /// Names and productivity of own buildings (collected while the world is drawn, drawn on top)
    bool showNames = false;
    struct NameLabel
    {
        std::string text;
        PointF worldPos;
        int productivity; // -1 = none
    };
    std::vector<NameLabel> nameLabels;
    void drawNameLabels();
    bool showMinimap = false;
    bool hasSelection = false;
    MapPoint selected;
    struct Ring
    {
        bool open = false;
        MapPoint pt;
        float sx = 0, sy = 0;
        double openTime = 0;
        std::vector<RingItem> items;
    } ring;

    // ---- Sheets (bottom sheet in portrait, side panel in landscape)
    SheetKind sheet = SheetKind::None;
    /// Content height requested by the current sheet (portrait: the sheet shrinks to fit), -1 = not known yet
    float fitReq = 0, fitH = -1;
    /// The current sheet holds a long list (stores): taller in portrait
    bool tallSheet = false;
    bool wideSheet = false; // landscape: wider side panel (charts)
    std::vector<SheetKind> sheetStack;
    MapPoint sheetPt;
    double sheetTime = 0;
    /// Closing animation: the panel of the closed sheet slides out
    double sheetCloseTime = -100;
    ui::Rect2 sheetCloseRect;
    int sheetTab = 0;
    int msgFilter = 0; // messages sheet: all, military, economy, geologist
    int statsType = 0, statsTime = 0;
    std::string missionTitle, missionText;
    /// Portrait or picture of the mission message from the game data (file, index; empty = none)
    std::string missionImageFile;
    int missionImageIdx = -1;
    bool missionPaused = false;
    std::string gameOverText;
    bool gameOverWin = false;
    std::string confirmText;
    std::function<void()> confirmAction;
    double confirmTime = 0;
    unsigned attackSoldiers = 1;
    unsigned shipId = 0;
    bool attackStrong = true;
    std::vector<int> buildCategoryFilter;

    // ---- Notifications
    struct Toast
    {
        std::string text;
        double time;
        bool hasPos;
        MapPoint pos;
    };
    std::deque<Toast> toasts;
    PostBox* observedBox = nullptr;
    MapPoint lastMessagePos;
    bool hasLastMessagePos = false;
    bool lastMessageFresh = false; // not yet visited with the home button
    unsigned jumpIndex = 0;
    int msgBadge = 0;
    bool roadFromDrag = false;
    // Minimap panel taps
    ui::Rect2 minimapRect;
    bool minimapTap = false;
    float minimapTapX = 0, minimapTapY = 0, minimapDownX = -1, minimapDownY = -1;

    std::unique_ptr<Tutorial> tutorial;

    // ---- Helpers
    const GameWorldBase& world() const;
    const GamePlayer& player() const;
    float alpha() const;
    bool landscape() const { return !app.portrait(); }
    ui::Rect2 sheetRect() const;
    /// Called by a sheet with the bottom of its content (sheets without call use the full height)
    void fit(const ui::Rect2& r, float bottom) { fitReq = bottom - r.y + app.dp(18); }
    void openSheet(SheetKind k, MapPoint pt = MapPoint::invalid(), bool push = false);
    void closeSheet();
    void toast(const std::string& text, bool hasPos = false, MapPoint pos = MapPoint::invalid());
    void confirm(const std::string& text, std::function<void()> action);
    void noteRecent(BuildingType bt);

    // World interaction (Hud.cpp)
    void onTap(float x, float y);
    void onDoubleTap(float x, float y);
    void onLongPress(float x, float y);
    void openRingAt(MapPoint pt);
    void activateRingItem(const RingItem& item);
    bool startRoad(MapPoint flag, bool water);
    /// Build the shortest road from an own flag to the nearest flag of the road network; false if none found
    bool autoConnect(MapPoint flag);
    /// Flag waiting for its building site command (auto connect once it exists)
    MapPoint pendingConnect = MapPoint::invalid();
    double pendingConnectTime = 0;
    bool extendRoadTo(MapPoint pt);
    void truncateRoadAt(MapPoint pt);
    void buildRoad();
    void cancelRoad();
    void drawWorldOverlay(MapPoint pt, PointF pos);
    void drawRing();
    void drawTopBar();
    void drawTools();
    void drawRoadBar();
    void drawMinimapPanel();
    void drawToasts();
    void checkMessages();

    // Sheets (Sheets.cpp)
    void drawSheet();
    void sheetGameMenu(const ui::Rect2& r);
    void sheetBuild(const ui::Rect2& r);
    void sheetBuilding(const ui::Rect2& r);
    void sheetFlag(const ui::Rect2& r);
    void sheetMessages(const ui::Rect2& r);
    void sheetStats(const ui::Rect2& r);
    /// Edited copy of the economy settings (commands take effect one game frame later); refreshed when the
    /// economy menu is shown
    VisualSettings edit;
    bool editValid = false;
    void ensureEdit();
    /// "Standardwerte" button at the end of a settings list (moves y below it)
    bool defaultsButton(float x, float& y, float w);
    void sheetEconomy(const ui::Rect2& r);
    void sheetDistribution(const ui::Rect2& r);
    void sheetTransport(const ui::Rect2& r);
    void sheetTools(const ui::Rect2& r);
    void sheetMilitary(const ui::Rect2& r);
    void sheetInventory(const ui::Rect2& r);
    void sheetSaveLoad(const ui::Rect2& r, bool save);
    void sheetSettings(const ui::Rect2& r);
    void sheetAttack(const ui::Rect2& r);
    void sheetMission(const ui::Rect2& r);
    void sheetGameOver(const ui::Rect2& r);
    void sheetHelp(const ui::Rect2& r);
    void sheetShip(const ui::Rect2& r);
    /// Own ship at or next to pt (ships float between nodes); opens its sheet
    bool tapShip(MapPoint pt);
    /// Icon of a building type from the game data (own symbol if the data has none)
    void buildingIcon(Nation nation, BuildingType bt, const ui::Rect2& r, float alpha = 1);
    /// Title row with close button; returns the remaining content rect
    ui::Rect2 sheetHeader(const ui::Rect2& r, const std::string& title, ui::Icon icon = ui::Icon::None);
};

/// Buildings that the local player can place at pt
std::vector<BuildingType> fittingBuildings(const Hud::Impl& h, MapPoint pt);

} // namespace weiler
