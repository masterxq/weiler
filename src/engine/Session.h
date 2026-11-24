// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "AsyncChecksum.h"
#include "GameCommand.h"
#include "GlobalGameSettings.h"
#include "ILocalGameState.h"
#include "PlayerInfo.h"
#include "factories/GameCommandFactory.h"
#include <boost/filesystem/path.hpp>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class Game;
class GameWorld;
class AIPlayer;
class GameInterface;

namespace weiler {

/// Settings for a new game
struct NewGameSetup
{
    boost::filesystem::path map;
    /// Optional Lua script (missions)
    boost::filesystem::path lua;
    std::vector<PlayerInfo> players;
    GlobalGameSettings ggs;
    unsigned seed = 1;
    std::string title;
};

/// A running single player game: the core simulation plus the AI players and the local player's commands.
/// Weiler replacement for the GameClient/GameServer pair of RTTR (no network).
class Session : public ILocalGameState, public GameCommandFactory
{
public:
    static std::unique_ptr<Session> newGame(const NewGameSetup& setup);
    /// Load a savegame. Throws std::runtime_error on failure.
    static std::unique_ptr<Session> loadGame(const boost::filesystem::path& path, unsigned seed);
    ~Session() override;

    /// Number of game frames between two AI command rounds
    static constexpr unsigned NWF_LENGTH = 4;

    /// Run one game frame
    void runGF();
    /// Current game frame
    unsigned getGF() const;
    bool isFinished() const;

    bool save(const boost::filesystem::path& path) const;
    AsyncChecksum checksum() const;

    Game& game() { return *game_; }
    const std::shared_ptr<Game>& gamePtr() const { return game_; }
    const Game& game() const { return *game_; }
    GameWorld& world();
    const GameWorld& world() const;
    const std::string& title() const { return title_; }

    /// Frontend callbacks
    void setGameInterface(GameInterface* gi);
    std::function<void(const LuaMessage&)> onLuaMessage;
    std::function<void(const std::string&)> onSystemChat;

    /// Let the AI play for the local player (toggle)
    void toggleLocalAI();

    // ILocalGameState
    unsigned getPlayerId() const override { return localPlayer_; }
    bool isHost() const override { return true; }
    std::string formatGFTime(unsigned numGFs) const override;
    void systemChat(const std::string& text) override;
    void showLuaMessage(const LuaMessage& msg) const override;

protected:
    // GameCommandFactory: commands of the local player, executed before the next GF
    bool addGC(gc::GameCommandPtr gc) override;

private:
    Session(std::shared_ptr<Game> game, unsigned localPlayer, std::string title);
    void createAIs();

    std::shared_ptr<Game> game_;
    unsigned localPlayer_;
    std::string title_;
    std::vector<gc::GameCommandPtr> localCommands_;
    /// AI commands collected at the last NWF, executed at the next one
    std::vector<std::pair<unsigned, std::vector<gc::GameCommandPtr>>> pendingAICommands_;
    std::unique_ptr<AIPlayer> localAI_;
};

} // namespace weiler
