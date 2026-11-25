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
class MapInfo;
class Replay;
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
    /// Play back a recorded game (RTTR replay): the commands of all players come from the file, no AI runs.
    /// Throws std::runtime_error if the file cannot be read.
    static std::unique_ptr<Session> loadReplay(const boost::filesystem::path& path);
    ~Session() override;

    /// Record this game (start state and the commands of all players) as an RTTR replay in the given folder,
    /// file named by date and time. Only the newest `keep` recordings in the folder are kept. Possible only before
    /// the first game frame. Returns the file, empty if recording is not possible.
    boost::filesystem::path startRecording(const boost::filesystem::path& folder, unsigned keep = 5);
    bool isRecording() const;
    /// Write the current game frame as the end of the recording (before the program may be ended from outside)
    void flushRecording();
    /// The game is a playback of a recording
    bool isReplay() const;
    /// Last game frame of the recording (only for playback)
    unsigned replayEndGF() const;
    /// First game frame at which the playback differed from the recording (checksums), if any
    std::optional<unsigned> replayDesyncGF() const { return replayDesyncGF_; }

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
    /// Seed of the random generators at the start (for the recording)
    unsigned seed_ = 0;
    /// Start of the game (map or savegame), kept until the first game frame for a recording
    std::unique_ptr<MapInfo> startInfo_;
    /// Recording or playback
    std::unique_ptr<Replay> replay_;
    std::optional<unsigned> nextReplayGF_;
    std::optional<unsigned> replayDesyncGF_;
    /// AI commands collected at the last NWF, executed at the next one
    std::vector<std::pair<unsigned, std::vector<gc::GameCommandPtr>>> pendingAICommands_;
    std::unique_ptr<AIPlayer> localAI_;
};

} // namespace weiler
