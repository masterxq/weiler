// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Campaign.h"
#include "JoinPlayerInfo.h"
#include "RttrConfig.h"
#include "files.h"
#include "lua/CampaignDataLoader.h"
#include "lua/LuaInterfaceSettings.h"
#include "network/IGameLobbyController.h"
#include "s25util/colors.h"
#include "libsiedler2/Archiv.h"
#include "libsiedler2/ArchivItem_Map.h"
#include "libsiedler2/ArchivItem_Map_Header.h"
#include "libsiedler2/prototypen.h"
#include "s25util/utf8.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <stdexcept>

namespace bfs = boost::filesystem;

namespace weiler {

namespace {
bool readMapHeader(const bfs::path& path, std::string& name, unsigned& numPlayers)
{
    libsiedler2::Archiv a;
    if(!bfs::exists(path) || libsiedler2::loader::loadMAP(path, a, true) != 0 || !a.get(0))
        return false;
    const auto* map = dynamic_cast<const libsiedler2::ArchivItem_Map*>(a.get(0));
    if(!map)
        return false;
    name = s25util::ansiToUTF8(map->getHeader().getName());
    numPlayers = map->getHeader().getNumPlayers();
    return true;
}

/// The settings of the RTTR lobby, reduced to a single player game without network
class MissionLobby : public IGameLobbyController
{
public:
    MissionLobby(std::vector<JoinPlayerInfo>& players, GlobalGameSettings& ggs) : players_(players), ggs_(ggs) {}
    unsigned getMaxNumPlayers() const override { return static_cast<unsigned>(players_.size()); }
    JoinPlayerInfo& getJoinPlayer(unsigned playerIdx) override { return players_.at(playerIdx); }
    void closeSlot(unsigned playerIdx) override { players_.at(playerIdx).ps = PlayerState::Locked; }
    void setPlayerState(unsigned playerIdx, PlayerState state, const AI::Info& aiInfo) override
    {
        JoinPlayerInfo& p = players_.at(playerIdx);
        p.ps = state;
        p.aiInfo = aiInfo;
        if(state == PlayerState::AI && p.name.empty())
            p.name = "KI " + std::to_string(playerIdx);
    }
    void setName(unsigned playerIdx, const std::string& name) override { players_.at(playerIdx).name = name; }
    void setPortrait(unsigned playerIdx, unsigned portraitIndex) override
    {
        players_.at(playerIdx).portraitIndex = portraitIndex;
    }
    void setColor(unsigned playerIdx, unsigned newColor) override { players_.at(playerIdx).color = newColor; }
    void setTeam(unsigned playerIdx, Team newTeam) override { players_.at(playerIdx).team = newTeam; }
    void setNation(unsigned playerIdx, Nation newNation) override { players_.at(playerIdx).nation = newNation; }
    const GlobalGameSettings& getGGS() const override { return ggs_; }
    void changeGlobalGameSettings(const GlobalGameSettings& ggs) override { ggs_ = ggs; }

private:
    std::vector<JoinPlayerInfo>& players_;
    GlobalGameSettings& ggs_;
};
} // namespace

std::vector<Campaign> loadCampaigns()
{
    std::vector<Campaign> result;
    const bfs::path dir = RTTRCONFIG.expandPath(s25::folders::campaignsBuiltin);
    if(!bfs::is_directory(dir))
        return result;
    std::vector<bfs::path> folders;
    for(const auto& e : bfs::directory_iterator(dir))
    {
        if(bfs::is_directory(e.path()) && bfs::exists(e.path() / "campaign.lua"))
            folders.push_back(e.path());
    }
    std::sort(folders.begin(), folders.end());
    for(const auto& folder : folders)
    {
        Campaign c;
        c.id = folder.filename().string();
        CampaignDataLoader loader(c.desc, folder);
        if(!loader.load())
            continue;
        bool complete = c.desc.getNumMaps() > 0;
        for(size_t i = 0; i < c.desc.getNumMaps(); ++i)
        {
            std::string name;
            unsigned numPlayers = 0;
            complete &= readMapHeader(c.desc.getMapFilePath(i), name, numPlayers);
            c.missionTitles.push_back(name);
        }
        if(complete)
            result.push_back(std::move(c));
    }
    return result;
}

NewGameSetup makeMissionSetup(const Campaign& campaign, size_t mission, const std::string& playerName, unsigned seed)
{
    NewGameSetup setup;
    setup.map = campaign.desc.getMapFilePath(mission);
    setup.lua = campaign.desc.getLuaFilePath(mission);
    setup.seed = seed;
    std::string title;
    unsigned numPlayers = 0;
    if(!readMapHeader(setup.map, title, numPlayers) || numPlayers == 0)
        throw std::runtime_error("Mission map missing: " + setup.map.string());
    setup.title = title;

    // Lobby defaults: the human player in slot 0, all other slots open until the script decides
    std::vector<JoinPlayerInfo> players(numPlayers);
    for(unsigned i = 0; i < numPlayers; ++i)
    {
        JoinPlayerInfo& p = players[i];
        p.ps = i == 0 ? PlayerState::Occupied : PlayerState::Free;
        p.name = i == 0 ? playerName : std::string();
        p.nation = Nation::Romans;
        p.color = PLAYER_COLORS[i % PLAYER_COLORS.size()];
        p.team = Team::None;
        p.isHost = i == 0;
    }
    GlobalGameSettings ggs;
    if(bfs::exists(setup.lua))
    {
        MissionLobby lobby(players, ggs);
        NullLocalGameState localState;
        LuaInterfaceSettings lua(lobby, localState);
        if(!lua.loadScript(setup.lua))
            throw std::runtime_error("Mission script failed: " + setup.lua.string());
        if(lua.eventSettingsInit(true, false))
            lua.eventSettingsReady();
    } else
        setup.lua.clear();
    // Slots the script left open stay empty
    for(JoinPlayerInfo& p : players)
    {
        if(p.ps == PlayerState::Free)
            p.ps = PlayerState::Locked;
    }
    setup.players.assign(players.begin(), players.end());
    setup.ggs = ggs;
    return setup;
}

} // namespace weiler
