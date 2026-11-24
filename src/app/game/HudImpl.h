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
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class GamePlayer;
class GameWorldBase;
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
    Help
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
        More
    } kind;
    BuildingType bt = BuildingType::Headquarters;
    std::string label;
};

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
    std::vector<SheetKind> sheetStack;
    MapPoint sheetPt;
    double sheetTime = 0;
    int sheetTab = 0;
    int statsType = 0, statsTime = 0;
    std::string missionTitle, missionText;
    bool missionPaused = false;
    std::string gameOverText;
    std::string confirmText;
    std::function<void()> confirmAction;
    unsigned attackSoldiers = 1;
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
    /// Title row with close button; returns the remaining content rect
    ui::Rect2 sheetHeader(const ui::Rect2& r, const std::string& title, ui::Icon icon = ui::Icon::None);
};

} // namespace weiler
