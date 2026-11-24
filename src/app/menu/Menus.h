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
NewGameSetup makeSkirmishSetup(const boost::filesystem::path& map, unsigned numAIs, unsigned seed, AI::Level level,
                               Nation nation);

} // namespace weiler
