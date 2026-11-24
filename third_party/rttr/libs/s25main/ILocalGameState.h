// Copyright (C) 2005 - 2026 Settlers Freaks (sf-team at siedler25.org)
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

/// Help images of mission statements (index into io.dat, IM_NONE = none)
enum class MissionHelpImage : unsigned
{
    IM_NONE,
    IM_SWORDSMAN = 234,
    IM_READER,
    IM_RIDER,
    IM_AVATAR1 = 239,
    IM_AVATAR2,
    IM_AVATAR3,
    IM_AVATAR4,
    IM_AVATAR5,
    IM_AVATAR6,
    IM_AVATAR7,
    IM_AVATAR8,
    IM_AVATAR9,
    IM_AVATAR10,
    IM_AVATAR11,
    IM_AVATAR12,
};

/// Weiler: message box or mission statement requested by a Lua script
struct LuaMessage
{
    std::string title, text;
    bool isError = false;
    /// Optional icon (resource file + index), -1 = none
    std::string iconFile;
    int iconIdx = -1;
    /// Optional icon position (-1 = default)
    int iconX = -1, iconY = -1;
    /// Mission statement (help image index in helpImage, may pause the game)
    bool isMissionStatement = false;
    bool pause = false;
    unsigned helpImage = 0;
};

class ILocalGameState
{
public:
    /// Get the local player id
    virtual unsigned getPlayerId() const = 0;
    /// Return true if the local player is the host
    virtual bool isHost() const = 0;
    /// Convert a number of GameFrames into real time (HH:MM:SS or MM:SS if hours = 0)
    virtual std::string formatGFTime(unsigned numGFs) const = 0;
    /// Send a chat message to the local player
    virtual void systemChat(const std::string& text) = 0;
    /// Show a message requested by a Lua script (default: ignore)
    virtual void showLuaMessage(const LuaMessage& /*msg*/) const {}
};

/// For headless contexts: acts as the host playing as player 0 and discards all output.
struct NullLocalGameState : ILocalGameState
{
    unsigned getPlayerId() const override { return 0; }
    bool isHost() const override { return true; }
    std::string formatGFTime(unsigned) const override { return ""; }
    void systemChat(const std::string&) override {}
};
