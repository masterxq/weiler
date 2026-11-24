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
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Game;
class Savegame;
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
    /// Load a savegame. Throws std::runtime_error on failure. Without a seed the random generators are seeded from
    /// the saved game state, so loading the same savegame always continues the same way.
    static std::unique_ptr<Session> loadGame(const boost::filesystem::path& path,
                                             std::optional<unsigned> seed = std::nullopt);
    /// Read a savegame completely without creating a game (throws std::runtime_error on failure); loadGame(save)
    /// creates the game from it. Allows checking a file before the running game is closed.
    static std::unique_ptr<Savegame> readSave(const boost::filesystem::path& path);
    static std::unique_ptr<Session> loadGame(std::unique_ptr<Savegame> save, const boost::filesystem::path& path,
                                             std::optional<unsigned> seed = std::nullopt);
    /// Short description of a savegame ("map · played time"), empty if unreadable
    static std::string describeSave(const boost::filesystem::path& path);
    ~Session() override;

    /// Number of game frames between two AI command rounds
    static constexpr unsigned NWF_LENGTH = 4;

    /// Run one game frame
    void runGF();
    /// Current game frame
    unsigned getGF() const;
    bool isFinished() const;

    bool save(const boost::filesystem::path& path) const;
    /// Save with the slow part (compression, writing) on a worker thread: the game state is copied now, the
    /// game can continue right away. The future tells whether writing worked.
    std::shared_future<bool> saveInBackground(const boost::filesystem::path& path) const;
    /// Wait until all savegames written in the background are complete (before loading or quitting)
    static void waitForSaves();
    AsyncChecksum checksum() const;

    Game& game() { return *game_; }
    const std::shared_ptr<Game>& gamePtr() const { return game_; }
    const Game& game() const { return *game_; }
    GameWorld& world();
    const GameWorld& world() const;
    const std::string& title() const { return title_; }
    /// Missions run a Lua script, free games do not
    bool hasScript() const;

    /// Frontend callbacks
    void setGameInterface(GameInterface* gi);
    std::function<void(const std::string&)> onSystemChat;
    /// A campaign script reports a completed chapter (1 = first mission)
    std::function<void(const std::string& campaignId, unsigned chapter)> onChapterCompleted;
    /// Receiver of Lua message boxes; messages sent before (e.g. at mission start) are delivered now
    void setLuaMessageHandler(std::function<void(const LuaMessage&)> handler);

    /// Let the AI play for the local player (toggle)
    void toggleLocalAI();

    // ILocalGameState
    unsigned getPlayerId() const override { return localPlayer_; }
    bool isHost() const override { return true; }
    std::string formatGFTime(unsigned numGFs) const override;
    void systemChat(const std::string& text) override;
    void showLuaMessage(const LuaMessage& msg) const override;
    void campaignChapterCompleted(const std::string& campaignUid, unsigned chapter) override;

protected:
    // GameCommandFactory: commands of the local player, executed before the next GF
    bool addGC(gc::GameCommandPtr gc) override;

private:
    std::function<void(const LuaMessage&)> onLuaMessage_;
    mutable std::vector<LuaMessage> pendingLuaMessages_;
    Session(std::shared_ptr<Game> game, unsigned localPlayer, std::string title);
    void createAIs();
    /// Copy of the complete game state for a savegame
    std::unique_ptr<Savegame> snapshot() const;

    std::shared_ptr<Game> game_;
    unsigned localPlayer_;
    std::string title_;
    std::vector<gc::GameCommandPtr> localCommands_;
    /// AI commands collected at the last NWF, executed at the next one
    std::vector<std::pair<unsigned, std::vector<gc::GameCommandPtr>>> pendingAICommands_;
    std::unique_ptr<AIPlayer> localAI_;
};

} // namespace weiler
