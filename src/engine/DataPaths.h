// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/filesystem/path.hpp>
#include <string>

namespace weiler {

/// Locations of all data the game needs
struct DataPaths
{
    /// Original game data (folder containing DATA and GFX)
    boost::filesystem::path gameData;
    /// Weiler/RTTR data shipped with the program (contains RTTR/)
    boost::filesystem::path appData;
    /// Writable per-user folder (saves, settings, logs)
    boost::filesystem::path userData;

    /// Determine the paths. gameDataArg (command line) has priority, then $WEILER_DATA, then
    /// $XDG_DATA_HOME/weiler/S2 (or ~/.local/share/weiler/S2).
    static DataPaths detect(const std::string& gameDataArg = {});

    /// True if the original game data looks complete
    bool hasGameData() const;
    /// Human readable description of what is missing (empty if complete)
    std::string missingGameDataReason() const;

    /// Configure RttrConfig, logging and create the user folders
    void apply() const;
};

} // namespace weiler
