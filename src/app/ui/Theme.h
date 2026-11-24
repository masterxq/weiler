// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "gfx/Gl.h"

namespace weiler::ui {

using gfx::Color;

/// Design tokens of the user interface: the one place for colors, spacing, radii, elevation, type and motion.
/// Lengths are density independent pixels (multiply with Ui::dp), durations are seconds.
///
/// Look: dark frosted glass over the world (blurred backdrop, warm tint), soft layered shadows, generous radii,
/// one warm honey accent, serif display type for titles and Lato for everything else.
struct Theme
{
    // ---- Colors
    /// Screen background behind menus without a world
    Color bg = Color::rgb(0x0F1012);
    /// Tint laid over the blurred backdrop of glass panels (alpha = tint strength)
    Color glass = Color::rgb(0x16130F, 0.66f);
    /// Stronger tint for large sheets and dialogs (more contrast for dense content)
    Color glassStrong = Color::rgb(0x14110D, 0.80f);
    /// Opaque panel color (fallback, dialogs over dialogs)
    Color panelSolid = Color::rgb(0x1D1A16);
    /// Same as glass, kept for content that draws its own panel
    Color panel = Color::rgb(0x1D1A16, 0.88f);
    /// Raised element on glass (buttons, list rows) and its pressed state
    Color surface = Color::rgb(0xFFF4E2, 0.065f);
    Color surfacePressed = Color::rgb(0xFFF4E2, 0.14f);
    Color raised = Color::rgb(0xFFF4E2, 0.10f);
    /// Hairlines and borders
    Color line = Color::rgb(0xFFF4E2, 0.09f);
    /// Highlight on the upper edge of glass (light from above)
    Color highlight = Color::rgb(0xFFF8EC, 0.16f);
    Color text = Color::rgb(0xF5EFE6);
    Color textDim = Color::rgb(0xB9AFA2);
    Color textFaint = Color::rgb(0x857C70);
    Color accent = Color::rgb(0xECAA4C);
    /// Lower end of the accent gradient
    Color accentDeep = Color::rgb(0xD48D31);
    Color accentText = Color::rgb(0x241809);
    Color accentSoft = Color::rgb(0xECAA4C, 0.16f);
    Color danger = Color::rgb(0xE26B52);
    Color ok = Color::rgb(0x8EC489);
    Color info = Color::rgb(0x7EAAD8);
    Color shadow = Color::rgb(0x000000, 0.42f);
    /// Dimming behind modal dialogs
    Color scrim = Color::rgb(0x07080A, 0.55f);

    // ---- Spacing scale
    struct
    {
        float xxs = 2, xs = 4, sm = 8, md = 12, lg = 16, xl = 24, xxl = 32, xxxl = 48;
    } space;

    // ---- Corner radii
    struct
    {
        float sm = 8, md = 14, lg = 20, xl = 26;
    } radius;

    // ---- Elevation: y offset and blur of the key shadow; a wide ambient shadow is added by Ui
    struct Elevation
    {
        float y, blur, alpha;
    };
    Elevation low{2, 8, 0.28f}, mid{6, 22, 0.36f}, high{14, 44, 0.46f};

    // ---- Type scale (font size in dp; titles use the serif display font)
    struct
    {
        float caption = 12, small = 13.5f, body = 15.5f, label = 16.5f, title = 22, headline = 30, display = 64;
    } type;

    // ---- Sizes of interactive elements
    struct
    {
        /// Minimum touch target (~9 mm at the phone's density with scale 3)
        float touch = 48, button = 54, icon = 22, fab = 52, row = 58;
    } size;

    // ---- Motion
    struct
    {
        float fast = 0.12f, normal = 0.24f, slow = 0.40f, ambient = 1.2f;
    } motion;
};

} // namespace weiler::ui
