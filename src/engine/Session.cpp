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
#include "Savegame.h"
#include "ViewState.h"
#include "ai/AIPlayer.h"
#include "ai/random.h"
#include "factories/AIFactory.h"
#include "random/Random.h"
#include "world/GameWorld.h"
#include "gameTypes/MapInfo.h"
#include "gameData/GameConsts.h"
#include "s25util/Log.h"
#include <boost/filesystem.hpp>
#include <boost/format.hpp>
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
    MapInfo mapInfo;
    mapInfo.type = MapType::OldMap;
    mapInfo.filepath = setup.map;
    mapInfo.luaFilepath = setup.lua;
    mapInfo.title = session->title_;
    setupGameWorld(*session->game_, mapInfo, *session, nullptr);
    session->createAIs();
    session->game_->start(false);
    return session;
}

std::unique_ptr<Session> Session::loadGame(const bfs::path& path, unsigned seed)
{
    auto save = std::make_unique<Savegame>();
    if(!save->load(path, SaveGameDataToLoad::All))
        throw std::runtime_error("Could not load savegame " + path.string());
    seedRandom(seed);
    std::vector<PlayerInfo> players;
    for(unsigned i = 0; i < save->getNumPlayers(); ++i)
        players.emplace_back(save->getPlayer(i));
    auto game = std::make_shared<Game>(save->ggs, save->start_gf, players);
    std::unique_ptr<Session> session(new Session(std::move(game), findLocalPlayer(players), save->getMapName()));
    MapInfo mapInfo;
    mapInfo.type = MapType::Savegame;
    mapInfo.filepath = path;
    mapInfo.title = save->getMapName();
    mapInfo.savegame = std::move(save);
    setupGameWorld(*session->game_, mapInfo, *session, nullptr);
    session->createAIs();
    session->game_->start(true);
    return session;
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

    // Commands of the local player are executed directly at the next GF
    std::vector<gc::GameCommandPtr> cmds;
    std::swap(cmds, localCommands_);
    for(const auto& cmd : cmds)
        cmd->execute(w, localPlayer_);

    if(isNWF)
    {
        // AI commands collected at the last NWF (like the network round trip of RTTR)
        for(const auto& playerCmds : pendingAICommands_)
        {
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

bool Session::save(const bfs::path& path) const
{
    Savegame save;
    for(const auto& player : game_->world_.getPlayers())
        save.addPlayer(player);
    save.ggs = game_->ggs_;
    save.start_gf = getGF();
    try
    {
        save.sgd.makeSnapshot(*game_);
        const bfs::path tmp = path.string() + ".tmp";
        if(!save.save(tmp, title_))
            return false;
        bfs::rename(tmp, path);
        return true;
    } catch(const std::exception& e)
    {
        LOG.write("Error during saving: %1%\n") % e.what();
        return false;
    }
}

AsyncChecksum Session::checksum() const
{
    return AsyncChecksum::create(*game_);
}

std::string Session::describeSave(const bfs::path& path)
{
    Savegame save;
    if(!save.load(path, SaveGameDataToLoad::HeaderAndSettings))
        return {};
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
