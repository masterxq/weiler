// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "App.h"
#include "Settings.h"
#include "Session.h"
#include "game/GameScreen.h"
#include "menu/Menus.h"
#include "s25util/colors.h"
#include <boost/filesystem.hpp>
#include <iostream>

namespace bfs = boost::filesystem;

namespace weiler {

extern std::string gQuickMap;
extern unsigned gQuickAIs;
extern unsigned gQuickSeed;
extern unsigned gQuickWarmupGF;
extern bool gQuickAutoplay;
extern std::string gQuickLoad;
extern std::string gQuickLevel;

NewGameSetup makeSkirmishSetup(const bfs::path& map, unsigned numAIs, unsigned seed, AI::Level level, Nation nation)
{
    NewGameSetup setup;
    setup.map = map;
    setup.seed = seed;
    PlayerInfo human;
    human.ps = PlayerState::Occupied;
    human.name = "Du";
    human.nation = nation;
    human.color = PLAYER_COLORS[0];
    human.team = Team::None;
    setup.players.push_back(human);
    for(unsigned i = 0; i < numAIs; ++i)
    {
        PlayerInfo pi;
        pi.ps = PlayerState::AI;
        pi.aiInfo = AI::Info(AI::Type::Default, level);
        pi.name = "KI " + std::to_string(i + 1);
        pi.nation = Nation((i + 1) % 4);
        pi.color = PLAYER_COLORS[(i + 1) % PLAYER_COLORS.size()];
        pi.team = Team::None;
        setup.players.push_back(pi);
    }
    setup.ggs.objective = GameObjective::None;
    return setup;
}

std::unique_ptr<Screen> createStartScreen(App& app)
{
    if(!app.paths().hasGameData())
        return createMissingDataScreen(app);
    app.paths().apply();
    if(!ensureStartFilesLoaded())
        return createMissingDataScreen(app);
    try
    {
        if(!gQuickLoad.empty())
        {
            auto session = Session::loadGame(gQuickLoad, gQuickSeed);
            if(gQuickAutoplay)
                session->toggleLocalAI();
            return createGameScreen(app, std::move(session));
        }
        if(!gQuickMap.empty())
        {
            bfs::path map(gQuickMap);
            if(!map.is_absolute() && !bfs::exists(map))
                map = app.paths().gameData / map;
            auto session =
              Session::newGame(makeSkirmishSetup(map, gQuickAIs, gQuickSeed,
                                                 gQuickLevel == "easy" ? AI::Level::Easy :
                                                 gQuickLevel == "hard" ? AI::Level::Hard :
                                                                         AI::Level::Medium,
                                                 Nation::Romans));
            if(gQuickAutoplay)
                session->toggleLocalAI();
            for(unsigned i = 0; i < gQuickWarmupGF; ++i)
                session->runGF();
            if(gQuickAutoplay)
                session->toggleLocalAI();
            return createGameScreen(app, std::move(session));
        }
    } catch(const std::exception& e)
    {
        std::cerr << "Could not start game: " << e.what() << std::endl;
    }
    // First start: the short introduction on a practice map
    if(gQuickMap.empty() && gQuickLoad.empty() && !app.settings().getBool("tutorial.done"))
        startTutorialGame(app, true);
    return createMainMenu(app);
}

} // namespace weiler
