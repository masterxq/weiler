// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gfx/Batch.h"

namespace weiler::ui {

/// Own vector icons (rasterized at runtime, drawn as coverage masks)
enum class Icon
{
    None,
    Menu,
    Close,
    Back,
    Pause,
    Play,
    Fast,
    Road,
    Flag,
    Build,
    House,
    Grid,
    Map,
    Home,
    Store,
    Mail,
    Chart,
    Gear,
    Save,
    Folder,
    Sword,
    Shield,
    Tools,
    Sliders,
    List,
    Boxes,
    Pick,
    Eye,
    Ship,
    Trash,
    Up,
    Down,
    Check,
    Plus,
    Minus,
    Info,
    Help,
    People,
    Power,
    Target,
    Coin,
    Hand,
    Trophy,
    Exit,
    Next,
    Music,
    Sound,
    Touch,
    Count
};

/// Name of the icon (test scripts)
const char* iconName(Icon icon);

class IconAtlas
{
public:
    void init(int cellPx = 96);
    /// Draw centered at (cx, cy) with the given edge length in pixels
    void draw(gfx::Batch& batch, Icon icon, float cx, float cy, float size, gfx::Color c) const;

private:
    gfx::Texture tex_;
    int cell_ = 96, cols_ = 8;
};

} // namespace weiler::ui
