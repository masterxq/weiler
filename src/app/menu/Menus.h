// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Session.h"
#include <boost/filesystem/path.hpp>
#include <memory>

namespace weiler {

class App;
class Screen;
class Settings;

/// Game data missing or not reachable: choose a folder (portal dialog or own folder browser), search it, remember it
std::unique_ptr<Screen> createDataSetupScreen(App& app);
/// Word mark (emblem above "WEILER"), top = upper edge, scale 1 = about 160 dp high
void drawWordmark(App& app, float cx, float top, float scale, float alpha);
std::unique_ptr<Screen> createMainMenu(App& app);
/// First screen: missing data notice, a game given on the command line, the introduction or the main menu
std::unique_ptr<Screen> createStartScreen(App& app);
void startTutorialGame(App& app, bool firstStart = false);
/// Undo the hand wrapping of original texts (single line breaks), keep paragraphs
std::string reflowText(const std::string& text);
/// Volume sliders and music choice (menu settings and in-game settings sheet); returns the height used
float drawAudioSettings(App& app, float x, float y, float w);
/// Graphics settings (glass effect, frame rate); returns the height used
float drawGraphicsSettings(App& app, float x, float y, float w);
/// Second line of a savegame entry: date, map and played time (cached per file version)
std::string saveDescription(const boost::filesystem::path& path);
/// Campaign progress (missions counted from 0): a mission is open when it is the first one or its predecessor is done
void markMissionDone(Settings& settings, const std::string& campaignId, size_t mission);
bool missionDone(const Settings& settings, const std::string& campaignId, size_t mission);
bool missionOpen(const Settings& settings, const std::string& campaignId, size_t mission);
NewGameSetup makeSkirmishSetup(const boost::filesystem::path& map, unsigned numAIs, unsigned seed, AI::Level level,
                               Nation nation);

} // namespace weiler
