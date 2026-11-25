// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later
//
// Game loop structure adapted from Return to the Roots (extras/ai-battle/HeadlessGame.cpp,
// libs/s25main/network/GameClient*.cpp), GPL-2.0-or-later.

#include "Session.h"
#include "EventManager.h"
#include "Game.h"
#include "GamePlayer.h"
#include "GameSetup.h"
#include "Replay.h"
#include "ReplayPlayback.h"
#include "Savegame.h"
#include "ViewState.h"
#include "ai/AIPlayer.h"
#include "ai/random.h"
#include "factories/AIFactory.h"
#include "random/Random.h"
#include "world/GameWorld.h"
#include "gameTypes/MapInfo.h"
#include "network/PlayerGameCommands.h"
#include "gameData/GameConsts.h"
#include "s25util/Log.h"
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
#include <algorithm>
#include <ctime>
#include <mutex>
#include <stdexcept>

namespace bfs = boost::filesystem;

namespace weiler {

namespace {
unsigned findLocalPlayer(const std::vector<PlayerInfo>& players)
{
    for(unsigned i = 0; i < players.size(); ++i)
    {
        if(players[i].isHuman())
            return i;
    }
    return 0;
}

void seedRandom(unsigned seed)
{
    RANDOM.init(seed);
    AI::getRandomGenerator().seed(seed);
}
} // namespace

Session::Session(std::shared_ptr<Game> game, unsigned localPlayer, std::string title)
    : game_(std::move(game)), localPlayer_(localPlayer), title_(std::move(title))
{}

Session::~Session()
{
    if(isRecording())
    {
        replay_->updateLastGF(getGF());
        replay_->stopRecording();
    }
    // AIs reference the world, destroy them first
    localAI_.reset();
    if(game_)
        game_->aiPlayers_.clear();
}

std::unique_ptr<Session> Session::newGame(const NewGameSetup& setup)
{
    seedRandom(setup.seed);
    auto game = std::make_shared<Game>(setup.ggs, 0u, setup.players);
    std::unique_ptr<Session> session(
      new Session(std::move(game), findLocalPlayer(setup.players),
                  setup.title.empty() ? setup.map.stem().string() : setup.title));
    auto mapInfo = std::make_unique<MapInfo>();
    mapInfo->type = MapType::OldMap;
    mapInfo->filepath = setup.map;
    mapInfo->luaFilepath = setup.lua;
    mapInfo->title = session->title_;
    setupGameWorld(*session->game_, *mapInfo, *session, nullptr);
    session->createAIs();
    session->game_->start(false);
    session->seed_ = setup.seed;
    session->startInfo_ = std::move(mapInfo);
    return session;
}

std::unique_ptr<Savegame> Session::readSave(const bfs::path& path)
{
    waitForSaves();
    auto save = std::make_unique<Savegame>();
    bool ok = false;
    try
    {
        ok = save->load(path, SaveGameDataToLoad::All);
    } catch(const std::exception& e)
    {
        throw std::runtime_error("Could not load savegame " + path.string() + ": " + e.what());
    }
    if(!ok)
        throw std::runtime_error("Could not load savegame " + path.string());
    return save;
}

std::unique_ptr<Session> Session::loadGame(const bfs::path& path, std::optional<unsigned> seed)
{
    return loadGame(readSave(path), path, seed);
}

std::unique_ptr<Session> Session::loadGame(std::unique_ptr<Savegame> save, const bfs::path& path,
                                           std::optional<unsigned> seed)
{
    // Savegames do not contain the state of the random generators: derive a seed from the saved game
    if(!seed)
    {
        uint32_t h = 2166136261u;
        auto mix = [&h](uint32_t v) {
            for(int i = 0; i < 4; ++i, v >>= 8)
                h = (h ^ (v & 0xFF)) * 16777619u;
        };
        mix(save->start_gf);
        mix(save->sgd.getLength());
        for(const char c : save->getMapName())
            mix(static_cast<unsigned char>(c));
        seed = h;
    }
    seedRandom(*seed);
    std::vector<PlayerInfo> players;
    for(unsigned i = 0; i < save->getNumPlayers(); ++i)
        players.emplace_back(save->getPlayer(i));
    auto game = std::make_shared<Game>(save->ggs, save->start_gf, players);
    std::unique_ptr<Session> session(new Session(std::move(game), findLocalPlayer(players), save->getMapName()));
    auto mapInfo = std::make_unique<MapInfo>();
    mapInfo->type = MapType::Savegame;
    mapInfo->filepath = path;
    mapInfo->title = save->getMapName();
    mapInfo->savegame = std::move(save);
    setupGameWorld(*session->game_, *mapInfo, *session, nullptr);
    session->createAIs();
    session->game_->start(true);
    session->seed_ = *seed;
    session->startInfo_ = std::move(mapInfo);
    return session;
}

std::unique_ptr<Session> Session::loadReplay(const bfs::path& path)
{
    auto replay = std::make_unique<Replay>();
    auto mapInfo = std::make_unique<MapInfo>();
    if(!replay->loadHeader(path) || !replay->loadGameData(*mapInfo))
        throw std::runtime_error("Could not load replay " + path.string() + ": " + replay->getLastErrorMsg());
    seedRandom(replay->getSeed());
    std::vector<PlayerInfo> players;
    for(unsigned i = 0; i < replay->getNumPlayers(); ++i)
        players.emplace_back(replay->getPlayer(i));
    const unsigned startGF = mapInfo->savegame ? mapInfo->savegame->start_gf : 0;
    auto game = std::make_shared<Game>(replay->ggs, startGF, players);
    std::unique_ptr<Session> session(new Session(std::move(game), findLocalPlayer(players), replay->getMapName()));
    try
    {
        setupGameWorld(*session->game_, *mapInfo, *session, replay.get());
    } catch(const std::exception& e)
    {
        throw std::runtime_error("Could not start replay " + path.string() + ": " + e.what());
    }
    // No AI: the commands of the AI players were recorded as well
    session->game_->start(mapInfo->savegame != nullptr);
    session->seed_ = replay->getSeed();
    session->nextReplayGF_ = replay->readGF();
    session->replay_ = std::move(replay);
    return session;
}

namespace {
/// Recordings in a folder, oldest first (names are date and time)
std::vector<bfs::path> listRecordings(const bfs::path& folder)
{
    std::vector<bfs::path> files;
    boost::system::error_code ec;
    for(bfs::directory_iterator it(folder, ec), end; !ec && it != end; it.increment(ec))
    {
        if(it->path().extension() == ".rpl")
            files.push_back(it->path());
    }
    std::sort(files.begin(), files.end());
    return files;
}
} // namespace

bfs::path Session::startRecording(const bfs::path& folder, unsigned keep)
{
    if(!startInfo_ || replay_)
        return {};
    std::unique_ptr<MapInfo> info = std::move(startInfo_);
    boost::system::error_code ec;
    bfs::create_directories(folder, ec);
    // Only the newest recordings are kept (including the new one)
    std::vector<bfs::path> old = listRecordings(folder);
    for(size_t i = 0; i + std::max(keep, 1u) <= old.size(); ++i)
        bfs::remove(old[i], ec);
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
    bfs::path file = folder / (std::string(stamp) + ".rpl");
    for(unsigned n = 2; bfs::exists(file); ++n)
        file = folder / (std::string(stamp) + "_" + std::to_string(n) + ".rpl"); // sorts after the first one
    try
    {
        if(info->type == MapType::OldMap)
        {
            // The map and script themselves go into the recording (the replay must not depend on the files)
            if(!info->mapData.compressFromFile(info->filepath, &info->mapChecksum))
                return {};
            if(!info->luaFilepath.empty() && !info->luaData.compressFromFile(info->luaFilepath, &info->luaChecksum))
                return {};
        }
        auto replay = std::make_unique<Replay>();
        for(const auto& player : game_->world_.getPlayers())
            replay->addPlayer(player);
        replay->ggs = game_->ggs_;
        if(!replay->startRecording(file, *info, seed_))
        {
            LOG.write("Could not record the game in %1%: %2%\n") % file % replay->getLastErrorMsg();
            return {};
        }
        replay_ = std::move(replay);
    } catch(const std::exception& e)
    {
        LOG.write("Could not record the game in %1%: %2%\n") % file % e.what();
        return {};
    }
    return file;
}

void Session::flushRecording()
{
    if(isRecording())
        replay_->updateLastGF(getGF());
}

bool Session::isRecording() const
{
    return replay_ && replay_->isRecording();
}

bool Session::isReplay() const
{
    return replay_ && !replay_->isRecording();
}

unsigned Session::replayEndGF() const
{
    return replay_ ? replay_->getLastGF() : 0;
}

void Session::createAIs()
{
    GameWorld& w = game_->world_;
    for(unsigned id = 0; id < w.getNumPlayers(); ++id)
    {
        const GamePlayer& player = w.getPlayer(id);
        if(player.ps == PlayerState::AI)
            game_->addAIPlayer(AIFactory::create(player.aiInfo, id, w));
    }
}

GameWorld& Session::world()
{
    return game_->world_;
}

const GameWorld& Session::world() const
{
    return game_->world_;
}

bool Session::hasScript() const
{
    return game_->world_.hasLua();
}

unsigned Session::getGF() const
{
    return game_->em_->getCurrentGF();
}

bool Session::isFinished() const
{
    return game_->isGameFinished();
}

void Session::setGameInterface(GameInterface* gi)
{
    game_->world_.setGameInterface(gi);
}

bool Session::addGC(gc::GameCommandPtr gc)
{
    localCommands_.push_back(std::move(gc));
    return true;
}

void Session::toggleLocalAI()
{
    if(localAI_)
        localAI_.reset();
    else
        localAI_ = AIFactory::create(AI::Info(AI::Type::Default, AI::Level::Medium), localPlayer_, world());
}

void Session::runGF()
{
    GameWorld& w = game_->world_;
    const unsigned gf = getGF();
    const bool isNWF = gf % NWF_LENGTH == 0;
    // The start state is only needed for a recording that begins before the first frame
    startInfo_.reset();

    if(isReplay())
    {
        // Playback: the recorded commands of all players, no AI and no local input
        localCommands_.clear();
        try
        {
            const ReplayCommandsResult result = executeReplayCommands(*replay_, nextReplayGF_, *game_, nullptr);
            if(result.desync && !replayDesyncGF_)
                replayDesyncGF_ = result.desync->gf;
        } catch(const std::exception& e)
        {
            // Incomplete end of a recording (program was killed while writing)
            LOG.write("Replay ends at GF %1%: %2%\n") % gf % e.what();
            nextReplayGF_.reset();
        }
        game_->runGF();
        VIEWSTATE.currentGF = getGF();
        return;
    }

    // Recording: every command batch with the checksum of the state before the first command of this frame
    // (like RTTR, so that a playback can detect a difference)
    std::optional<AsyncChecksum> checksum;
    const auto record = [&](unsigned player, const std::vector<gc::GameCommandPtr>& batch) {
        if(!isRecording() || batch.empty())
            return;
        if(!checksum)
            checksum = AsyncChecksum::create(*game_);
        replay_->addGameCommand(gf, static_cast<uint8_t>(player), PlayerGameCommands(*checksum, batch));
    };
    if(isRecording() && gf % 100 == 0)
        replay_->updateLastGF(gf);

    // Commands of the local player are executed directly at the next GF
    std::vector<gc::GameCommandPtr> cmds;
    std::swap(cmds, localCommands_);
    record(localPlayer_, cmds);
    for(const auto& cmd : cmds)
        cmd->execute(w, localPlayer_);

    if(isNWF)
    {
        // AI commands collected at the last NWF (like the network round trip of RTTR)
        for(const auto& playerCmds : pendingAICommands_)
        {
            record(playerCmds.first, playerCmds.second);
            for(const auto& cmd : playerCmds.second)
                cmd->execute(w, playerCmds.first);
        }
        pendingAICommands_.clear();
        for(AIPlayer& ai : game_->aiPlayers_)
        {
            pendingAICommands_.emplace_back(ai.getPlayerId(), ai.fetchGameCommands());
            ai.getAIInterface().fetchChatMessages();
        }
        if(localAI_)
            pendingAICommands_.emplace_back(localPlayer_, localAI_->fetchGameCommands());
    }

    for(AIPlayer& ai : game_->aiPlayers_)
        ai.runGF(gf, isNWF);
    if(localAI_)
        localAI_->runGF(gf, isNWF);

    game_->runGF();
    VIEWSTATE.currentGF = getGF();
}

namespace {
std::mutex gSavesMutex;
std::vector<std::shared_future<bool>> gSaves;

bool writeSave(Savegame& save, const bfs::path& path, const std::string& title)
{
    try
    {
        const bfs::path tmp = path.string() + ".tmp";
        if(!save.save(tmp, title))
            return false;
        bfs::rename(tmp, path);
        return true;
    } catch(const std::exception& e)
    {
        LOG.write("Error during saving: %1%\n") % e.what();
        return false;
    }
}
} // namespace

std::unique_ptr<Savegame> Session::snapshot() const
{
    auto save = std::make_unique<Savegame>();
    for(const auto& player : game_->world_.getPlayers())
        save->addPlayer(player);
    save->ggs = game_->ggs_;
    save->start_gf = getGF();
    save->sgd.makeSnapshot(*game_);
    return save;
}

bool Session::save(const bfs::path& path) const
{
    try
    {
        auto save = snapshot();
        return writeSave(*save, path, title_);
    } catch(const std::exception& e)
    {
        LOG.write("Error during saving: %1%\n") % e.what();
        return false;
    }
}

void Session::waitForSaves()
{
    std::vector<std::shared_future<bool>> saves;
    {
        std::lock_guard<std::mutex> lock(gSavesMutex);
        saves.swap(gSaves);
    }
    for(auto& f : saves)
        f.wait();
}

std::shared_future<bool> Session::saveInBackground(const bfs::path& path) const
{
    std::shared_ptr<Savegame> save;
    try
    {
        save = snapshot();
    } catch(const std::exception& e)
    {
        LOG.write("Error during saving: %1%\n") % e.what();
        std::promise<bool> failed;
        failed.set_value(false);
        return failed.get_future().share();
    }
    // The same file is never written twice at the same time
    waitForSaves();
    std::shared_future<bool> f =
      std::async(std::launch::async, [save, path, title = title_] { return writeSave(*save, path, title); }).share();
    std::lock_guard<std::mutex> lock(gSavesMutex);
    gSaves.push_back(f);
    return f;
}

AsyncChecksum Session::checksum() const
{
    return AsyncChecksum::create(*game_);
}

std::string Session::describeSave(const bfs::path& path)
{
    Savegame save;
    try
    {
        if(!save.load(path, SaveGameDataToLoad::HeaderAndSettings))
            return {};
    } catch(const std::exception&)
    {
        return {};
    }
    const auto gfLen = std::chrono::duration_cast<std::chrono::milliseconds>(SPEED_GF_LENGTHS[save.ggs.speed]);
    const unsigned sec = static_cast<unsigned>(static_cast<uint64_t>(save.start_gf) * gfLen.count() / 1000);
    return (boost::format("%s · %u:%02u h") % save.getMapName() % (sec / 3600) % (sec / 60 % 60)).str();
}

std::string Session::formatGFTime(unsigned numGFs) const
{
    return VIEWSTATE.formatGFTime(numGFs);
}

void Session::systemChat(const std::string& text)
{
    if(onSystemChat)
        onSystemChat(text);
}

void Session::showLuaMessage(const LuaMessage& msg) const
{
    if(onLuaMessage_)
        onLuaMessage_(msg);
    else
        pendingLuaMessages_.push_back(msg);
}

void Session::campaignChapterCompleted(const std::string& campaignUid, unsigned chapter)
{
    if(onChapterCompleted)
        onChapterCompleted(campaignUid, chapter);
}

void Session::setLuaMessageHandler(std::function<void(const LuaMessage&)> handler)
{
    onLuaMessage_ = std::move(handler);
    if(!onLuaMessage_)
        return;
    std::vector<LuaMessage> pending;
    std::swap(pending, pendingLuaMessages_);
    for(const LuaMessage& msg : pending)
        onLuaMessage_(msg);
}

} // namespace weiler
