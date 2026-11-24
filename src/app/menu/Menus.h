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

std::unique_ptr<Screen> createMissingDataScreen(App& app);
std::unique_ptr<Screen> createMainMenu(App& app);
void startTutorialGame(App& app, bool firstStart = false);
/// Undo the hand wrapping of original texts (single line breaks), keep paragraphs
std::string reflowText(const std::string& text);
/// Second line of a savegame entry: date, map and played time (cached per file version)
std::string saveDescription(const boost::filesystem::path& path);
NewGameSetup makeSkirmishSetup(const boost::filesystem::path& map, unsigned numAIs, unsigned seed, AI::Level level,
                               Nation nation);

} // namespace weiler
