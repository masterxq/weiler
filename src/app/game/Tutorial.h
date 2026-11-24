// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "HudImpl.h"

namespace weiler {

/// Interactive introduction to the gestures and the basic controls (overlay card with steps)
class Tutorial
{
public:
    explicit Tutorial(Hud::Impl& hud);
    void update();
    void draw();
    /// Animated ghost fingers for the gesture steps
    void drawGesture();

    // Events from the HUD
    void onPan();
    void onZoom();
    void onBQToggled(bool on);
    void onRingOpened(bool isFlag);
    void onBuild(BuildingType bt);
    void onRoadBuilt();
    void onSheet(SheetKind k);
    void onBack();

private:
    void next();
    void finish();
    Hud::Impl& hud_;
    bool firstStart_ = false;

public:
    void setFirstStart(bool on) { firstStart_ = on; }

private:
    unsigned step_ = 0;
    float panAmount_ = 0;
    float zoomAmount_ = 0;
    double stepTime_ = 0;
    bool menuOpened_ = false;
};

} // namespace weiler
