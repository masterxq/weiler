// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

// weiler-headless: runs a game without user interface (AI players only) for tests and benchmarks.

#include "DataPaths.h"
#include "Session.h"
#include "GamePlayer.h"
#include "SerializedGameData.h"
#include "world/GameWorld.h"
#include "gameData/GameConsts.h"
#include "gameData/MaxPlayers.h"
#include "s25util/Log.h"
#include "s25util/colors.h"
#include <boost/filesystem.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>

namespace bfs = boost::filesystem;
namespace po = boost::program_options;
using namespace weiler;

namespace {
/// Hash of the complete serialized game state (much stronger than AsyncChecksum)
uint64_t stateHash(const Session& session)
{
    SerializedGameData sgd;
    sgd.makeSnapshot(const_cast<Game&>(session.game()));
    uint64_t h = 1469598103934665603ull;
    const auto* data = sgd.getData();
    for(unsigned i = 0; i < sgd.getLength(); ++i)
    {
        h ^= data[i];
        h *= 1099511628211ull;
    }
    return h;
}

void printStatus(const Session& s)
{
    const auto& w = s.world();
    std::cout << "GF " << s.getGF() << " (" << s.formatGFTime(s.getGF()) << ")";
    for(unsigned i = 0; i < w.getNumPlayers(); ++i)
    {
        const GamePlayer& p = w.getPlayer(i);
        if(!p.isUsed())
            continue;
        std::cout << " | P" << i << " land=" << p.getStatisticCurrentValue(StatisticType::Country)
                  << " bld=" << p.getStatisticCurrentValue(StatisticType::Buildings)
                  << " mil=" << p.getStatisticCurrentValue(StatisticType::Military)
                  << (p.isDefeated() ? " (defeated)" : "");
    }
    std::cout << std::endl;
}
} // namespace

int main(int argc, char** argv)
{
    std::string dataDir, map, loadPath, savePath;
    unsigned numAIs = 2, seed = 1, minutes = 30, extraGFs = 0, reportEvery = 0;
    bool printHash = false;
    po::options_description desc("weiler-headless options");
    desc.add_options()("help,h", "Show help")("data", po::value(&dataDir), "Folder with the original game data")(
      "map", po::value(&map), "Map file (relative paths are looked up in the game data)")(
      "ais", po::value(&numAIs)->default_value(2), "Number of AI players for a new game")(
      "seed", po::value(&seed)->default_value(1), "Random seed (--load: derived from the savegame if not given)")(
      "minutes", po::value(&minutes)->default_value(30), "Game minutes to simulate (normal speed)")(
      "gfs", po::value(&extraGFs), "Game frames to simulate (overrides --minutes)")(
      "load", po::value(&loadPath), "Load a savegame instead of starting a new game")(
      "save", po::value(&savePath), "Write a savegame at the end")(
      "report", po::value(&reportEvery)->default_value(0), "Print status every N game minutes")(
      "hash", po::bool_switch(&printHash), "Print a hash of the complete game state at the end");
    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch(const std::exception& e)
    {
        std::cerr << e.what() << "\n" << desc << std::endl;
        return 2;
    }
    if(vm.count("help") || (map.empty() && loadPath.empty()))
    {
        std::cout << desc << std::endl;
        return vm.count("help") ? 0 : 2;
    }

    const DataPaths paths = DataPaths::detect(dataDir);
    if(!paths.hasGameData())
    {
        std::cerr << "Game data not usable: " << paths.missingGameDataReason() << std::endl;
        return 3;
    }
    paths.apply();

    std::unique_ptr<Session> session;
    try
    {
        if(!loadPath.empty())
            session = Session::loadGame(loadPath, vm.count("seed") && !vm["seed"].defaulted() ?
                                                    std::optional<unsigned>(seed) :
                                                    std::nullopt);
        else
        {
            NewGameSetup setup;
            bfs::path mapPath(map);
            if(!mapPath.is_absolute() && !bfs::exists(mapPath))
                mapPath = paths.gameData / mapPath;
            setup.map = mapPath;
            setup.seed = seed;
            setup.ggs.objective = GameObjective::TotalDomination;
            for(unsigned i = 0; i < numAIs && i < MAX_PLAYERS; ++i)
            {
                PlayerInfo pi;
                pi.ps = PlayerState::AI;
                pi.aiInfo = AI::Info(AI::Type::Default, AI::Level::Hard);
                pi.name = "AI " + std::to_string(i + 1);
                pi.nation = Nation(i % 4);
                pi.color = PLAYER_COLORS[i % PLAYER_COLORS.size()];
                pi.team = Team::None;
                setup.players.push_back(pi);
            }
            session = Session::newGame(setup);
        }
    } catch(const std::exception& e)
    {
        std::cerr << "Could not start game: " << e.what() << std::endl;
        return 4;
    }

    const unsigned gfPerMinute = static_cast<unsigned>(
      std::chrono::minutes(1) / std::chrono::milliseconds(SPEED_GF_LENGTHS[GameSpeed::Normal]));
    const unsigned numGFs = vm.count("gfs") ? extraGFs : minutes * gfPerMinute;
    const unsigned targetGF = session->getGF() + numGFs;
    const auto start = std::chrono::steady_clock::now();
    while(session->getGF() < targetGF && !session->isFinished())
    {
        session->runGF();
        if(reportEvery && session->getGF() % (reportEvery * gfPerMinute) == 0)
            printStatus(*session);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printStatus(*session);
    std::cout << "Simulated " << numGFs << " GF in " << std::fixed << std::setprecision(1) << secs << " s ("
              << std::setprecision(0) << numGFs / std::max(secs, 1e-3) << " GF/s)" << std::endl;
    std::cout << "checksum " << session->checksum() << std::endl;
    if(printHash)
        std::cout << "statehash " << std::hex << stateHash(*session) << std::dec << std::endl;
    if(!savePath.empty())
    {
        if(!session->save(savePath))
        {
            std::cerr << "Saving failed" << std::endl;
            return 5;
        }
        std::cout << "saved " << savePath << std::endl;
    }
    return 0;
}
