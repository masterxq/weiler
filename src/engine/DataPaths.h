// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <boost/filesystem/path.hpp>
#include <atomic>
#include <string>
#include <vector>

namespace weiler {

/// Locations of all data the game needs
struct DataPaths
{
    /// Where the original game data was found
    enum class Source
    {
        Argument,    // --data
        Environment, // $WEILER_DATA
        Remembered,  // folder chosen earlier (settings)
        Package,     // package weiler-data (weiler-pack-data)
        User,        // $XDG_DATA_HOME/weiler/S2
        Chosen,      // chosen in the running program
        None         // nothing usable found
    };
    /// State of the remembered folder
    enum class Remembered
    {
        None,        // nothing remembered
        Ok,          // valid
        Unreachable, // the folder is gone or an empty mount point (removable medium not inserted)
        Invalid      // the folder exists but holds no game data (to be forgotten)
    };

    /// Original game data (folder containing DATA and GFX)
    boost::filesystem::path gameData;
    Source source = Source::None;
    /// The remembered folder and its state
    boost::filesystem::path rememberedData;
    Remembered remembered = Remembered::None;
    /// Weiler/RTTR data shipped with the program (contains RTTR/)
    boost::filesystem::path appData;
    /// Fonts, music, translations of the core and the list of the game files Weiler loads (gamedata.list)
    boost::filesystem::path fontDir, musicDir, langDir, gameFileList;
    /// Writable per-user folder (saves, settings, logs)
    boost::filesystem::path userData;

    /// Determine the paths. The game data is searched in this order: gameDataArg (command line), $WEILER_DATA,
    /// the remembered folder, the package weiler-data, $XDG_DATA_HOME/weiler/S2 (or ~/.local/share/weiler/S2).
    static DataPaths detect(const std::string& gameDataArg = {}, const std::string& rememberedArg = {});

    /// Folders of the package weiler-data (made by weiler-pack-data), first match wins
    static std::vector<boost::filesystem::path> packageDataCandidates();
    /// Default folder for the game data of the user
    static boost::filesystem::path userGameData();

    /// True if dir holds the original game data with the exact (upper case) file names
    static bool isGameData(const boost::filesystem::path& dir);
    /// True if dir holds the original game data, ignoring the case of the file names
    static bool isGameDataAnyCase(const boost::filesystem::path& dir);
    /// Search root and its sub folders (case-insensitive, links to folders are not followed) for the game data.
    /// Returns an empty path if nothing was found or cancel became true.
    static boost::filesystem::path findGameData(const boost::filesystem::path& root,
                                                const std::atomic<bool>* cancel = nullptr);
    /// Copy the game files Weiler loads (gameFileList) from src (any case) to dst (upper case names)
    bool copyGameData(const boost::filesystem::path& src, const boost::filesystem::path& dst,
                      std::string& error) const;

    /// True if the original game data looks complete
    bool hasGameData() const;
    /// Human readable description of what is missing (empty if complete)
    std::string missingGameDataReason() const;

    /// Configure RttrConfig, logging and create the user folders
    void apply() const;
};

} // namespace weiler
