// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "Session.h"
#include "gameData/CampaignDescription.h"
#include <string>
#include <vector>

namespace weiler {

/// A campaign of the RTTR data (Lua description) with the titles of its missions
struct Campaign
{
    /// Folder name, used as key for the progress
    std::string id;
    CampaignDescription desc;
    /// Mission titles from the map headers (empty if the map is missing)
    std::vector<std::string> missionTitles;
};

/// Campaigns whose maps exist in the game data
std::vector<Campaign> loadCampaigns();

/// Settings of a campaign mission: the mission script decides players, nations, addons and game settings
/// (like the RTTR lobby with LuaInterfaceSettings). Throws std::runtime_error on failure.
NewGameSetup makeMissionSetup(const Campaign& campaign, size_t mission, const std::string& playerName, unsigned seed);

} // namespace weiler
