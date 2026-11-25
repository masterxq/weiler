// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

// weiler-headless: runs a game without user interface (AI players only) for tests and benchmarks.

#include "DataPaths.h"
#include "Session.h"
#include "GamePlayer.h"
#include "SerializedGameData.h"
#include "buildings/nobBaseWarehouse.h"
#include "figures/noFigure.h"
#include "world/GameWorld.h"
#include "game/Names.h"
#include "gameData/GameConsts.h"
#include "gameData/MaxPlayers.h"
#include "s25util/Log.h"
#include "s25util/colors.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/program_options.hpp>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <unordered_map>
#include <optional>
#include <random>
#include <vector>

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

/// Load damaged copies of a savegame or map: every copy has to be rejected with an error or play a few game frames
template<typename StartFn>
int runDamageTests(const std::string& file, bool isSave, unsigned count, const DataPaths& paths, StartFn start)
{
    bfs::path src(file);
    if(!isSave && !src.is_absolute() && !bfs::exists(src))
        src = paths.gameData / src;
    bfs::ifstream in(src, std::ios::binary);
    const std::vector<char> original((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if(original.empty())
    {
        std::cerr << "Cannot read " << src << std::endl;
        return 2;
    }
    const bfs::path damaged = bfs::path(paths.userData) / ("damaged" + src.extension().string());
    unsigned rejected = 0, played = 0;
    for(unsigned i = 0; i < count; ++i)
    {
        std::minstd_rand rng(i + 1);
        std::vector<char> data = original;
        if(i % 5 == 4)
            data.resize(rng() % data.size()); // truncated
        else
        {
            // Some bytes changed, mostly in the header and the first blocks
            const unsigned n = 1 + rng() % 4;
            const size_t range = i % 2 ? data.size() : std::min<size_t>(data.size(), 4096);
            for(unsigned j = 0; j < n; ++j)
                data[rng() % range] = static_cast<char>(rng());
        }
        {
            bfs::ofstream out(damaged, std::ios::binary | std::ios::trunc);
            out.write(data.data(), static_cast<std::streamsize>(data.size()));
        }
        try
        {
            auto session = isSave ? start(damaged.string(), std::string()) : start(std::string(), damaged.string());
            for(unsigned gf = 0; gf < 100 && !session->isFinished(); ++gf)
                session->runGF();
            ++played;
        } catch(const std::exception&)
        {
            ++rejected;
        }
    }
    bfs::remove(damaged);
    std::cout << "damage test " << src.filename().string() << ": " << count << " copies, " << rejected
              << " rejected, " << played << " played" << std::endl;
    return 0;
}

/// Watches figures that enter the territory of another player: who they belong to, what they do and where they
/// came from (diagnosis of workers leaving for foreign land)
class ForeignWatch
{
public:
    explicit ForeignWatch(bool verbose) : verbose_(verbose) {}
    void scan(const Session& s)
    {
        const GameWorld& w = s.world();
        const MapExtent size = w.getSize();
        for(MapCoord y = 0; y < size.y; ++y)
        {
            for(MapCoord x = 0; x < size.x; ++x)
            {
                const MapPoint pt(x, y);
                const unsigned owner = w.getNode(pt).owner;
                for(const noBase& obj : w.getFigures(pt))
                {
                    const auto* fig = dynamic_cast<const noFigure*>(&obj);
                    if(!fig)
                        continue;
                    auto [it, isNew] = seen_.try_emplace(fig->getObjId());
                    Seen& info = it->second;
                    if(isNew)
                    {
                        info.firstGF = s.getGF();
                        info.firstPos = pt;
                        const auto* wh = w.getSpecObj<nobBaseWarehouse>(pt);
                        info.fromWarehouse = wh && wh->getPlayer() == fig->getPlayer();
                    }
                    if(info.reported || owner == fig->getPlayer() + 1u || fig->isSoldier())
                        continue;
                    info.reported = true;
                    const char* state = fig->isWandering()      ? "wandert" :
                                        fig->isDoingJobWorks()  ? "arbeitet" :
                                        fig->isWalkingOnRoad()  ? "auf Strasse" :
                                                                  "anderes";
                    std::string goal = "-";
                    if(const noRoadNode* g = fig->getGoal())
                    {
                        goal = "(" + std::to_string(g->getPos().x) + "," + std::to_string(g->getPos().y) + ") von P"
                               + std::to_string(g->getPlayer());
                        if(const auto* bld = dynamic_cast<const noBaseBuilding*>(g))
                            goal = std::string(buildingName(bld->getBuildingType())) + " " + goal;
                    }
                    const std::string key = std::string(owner ? "fremd / " : "niemand / ") + jobName(fig->getJobType()) + " / " + state + " / "
                                            + (info.fromWarehouse ? "aus Lager" : "von unterwegs");
                    ++counts_[key];
                    if(verbose_ || counts_[key] <= 3)
                        std::cout << "[fremd] GF " << s.getGF() << " P" << unsigned(fig->getPlayer()) << " "
                                  << jobName(fig->getJobType()) << " (GOT " << rttr::enumCast(fig->getGOT())
                                  << ") " << state << " bei (" << pt.x << "," << pt.y << ") " << (owner ? "Gebiet P" + std::to_string(owner - 1) : std::string("Niemandsland"))
                                  << ", Ziel " << goal << ", zuerst GF " << info.firstGF << " bei ("
                                  << info.firstPos.x << "," << info.firstPos.y << ")"
                                  << (info.fromWarehouse ? " im Lager" : "") << std::endl;
                }
            }
        }
    }
    void summary() const
    {
        for(const auto& [key, n] : counts_)
            std::cout << "[fremd-summe] " << key << ": " << n << std::endl;
    }

private:
    struct Seen
    {
        unsigned firstGF = 0;
        MapPoint firstPos;
        bool fromWarehouse = false, reported = false;
    };
    bool verbose_;
    std::unordered_map<unsigned, Seen> seen_;
    std::map<std::string, unsigned> counts_;
};

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
    unsigned numAIs = 2, seed = 1, minutes = 30, extraGFs = 0, reportEvery = 0, damageTests = 0;
    bool printHash = false, foreign = false, foreignAll = false;
    std::vector<std::string> sendOut;
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
      "hash", po::bool_switch(&printHash), "Print a hash of the complete game state at the end")(
      "foreign", po::bool_switch(&foreign),
      "Report figures (no soldiers) outside their own territory: job, state, goal, origin")(
      "foreign-all", po::bool_switch(&foreignAll), "With --foreign: print every figure, not only 3 per kind")(
      "send-out", po::value(&sendOut)->multitoken(),
      "Set 'send out' for these jobs (enum numbers) in the headquarters of player 0 at the start")(
      "damage-test", po::value(&damageTests),
      "Robustness test: load N damaged copies of the --load or --map file (must fail or play, never crash)");
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

    const std::optional<unsigned> loadSeed =
      vm.count("seed") && !vm["seed"].defaulted() ? std::optional<unsigned>(seed) : std::nullopt;
    auto startSession = [&](const std::string& load, const std::string& mapFile) {
        if(!load.empty())
            return Session::loadGame(load, loadSeed);
        NewGameSetup setup;
        bfs::path mapPath(mapFile);
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
        return Session::newGame(setup);
    };

    if(damageTests)
        return runDamageTests(loadPath.empty() ? map : loadPath, !loadPath.empty(), damageTests, paths, startSession);

    std::unique_ptr<Session> session;
    try
    {
        session = startSession(loadPath, map);
    } catch(const std::exception& e)
    {
        std::cerr << "Could not start game: " << e.what() << std::endl;
        return 4;
    }

    const unsigned gfPerMinute = static_cast<unsigned>(
      std::chrono::minutes(1) / std::chrono::milliseconds(SPEED_GF_LENGTHS[GameSpeed::Normal]));
    const unsigned numGFs = vm.count("gfs") ? extraGFs : minutes * gfPerMinute;
    const unsigned targetGF = session->getGF() + numGFs;
    for(const std::string& job : sendOut)
    {
        // Same command as the store sheet of the interface, executed for player 0
        const GamePlayer& p0 = session->world().getPlayer(0);
        session->SetInventorySetting(p0.getHQPos(), static_cast<Job>(std::stoi(job)),
                                     InventorySetting(EInventorySetting::Send));
    }
    ForeignWatch watch(foreignAll);
    const auto start = std::chrono::steady_clock::now();
    while(session->getGF() < targetGF && !session->isFinished())
    {
        session->runGF();
        if(foreign && session->getGF() % 10 == 0)
            watch.scan(*session);
        if(reportEvery && session->getGF() % (reportEvery * gfPerMinute) == 0)
            printStatus(*session);
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    printStatus(*session);
    if(foreign)
        watch.summary();
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
