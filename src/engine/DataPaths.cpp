// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DataPaths.h"
#include "RttrConfig.h"
#include "files.h"
#include "s25util/Log.h"
#include "s25util/System.h"
#include <boost/filesystem.hpp>
#include <array>
#include <cstdlib>

namespace bfs = boost::filesystem;

#ifndef WEILER_SOURCE_DATA_DIR
#    define WEILER_SOURCE_DATA_DIR ""
#endif
#ifndef WEILER_INSTALL_DATA_DIR
#    define WEILER_INSTALL_DATA_DIR ""
#endif

namespace weiler {
namespace {
bfs::path envPath(const char* name)
{
    const char* v = std::getenv(name);
    return (v && *v) ? bfs::path(v) : bfs::path();
}

bfs::path xdgDataHome()
{
    bfs::path p = envPath("XDG_DATA_HOME");
    if(p.empty())
        p = envPath("HOME") / ".local" / "share";
    return p;
}

bfs::path findAppData()
{
    // 1. Next to the executable (installed layout <prefix>/bin/weiler -> <prefix>/share/weiler)
    const bfs::path exe = System::getExecutablePath();
    std::vector<bfs::path> candidates;
    if(!exe.empty())
        candidates.push_back(exe.parent_path().parent_path() / "share" / "weiler");
    if(*WEILER_INSTALL_DATA_DIR)
        candidates.emplace_back(WEILER_INSTALL_DATA_DIR);
    // 2. Source tree (development builds)
    if(*WEILER_SOURCE_DATA_DIR)
        candidates.emplace_back(WEILER_SOURCE_DATA_DIR);
    for(const auto& c : candidates)
    {
        if(bfs::exists(c / "RTTR" / "gamedata"))
            return c;
    }
    return candidates.empty() ? bfs::path() : candidates.back();
}
} // namespace

DataPaths DataPaths::detect(const std::string& gameDataArg)
{
    DataPaths p;
    if(!gameDataArg.empty())
        p.gameData = gameDataArg;
    else if(!envPath("WEILER_DATA").empty())
        p.gameData = envPath("WEILER_DATA");
    else
        p.gameData = xdgDataHome() / "weiler" / "S2";
    p.appData = findAppData();
    p.userData = xdgDataHome() / "weiler";
    const bfs::path userOverride = envPath("WEILER_USERDATA");
    if(!userOverride.empty())
        p.userData = userOverride;
    return p;
}

std::string DataPaths::missingGameDataReason() const
{
    if(!bfs::is_directory(gameData))
        return "folder " + gameData.string() + " does not exist";
    static const std::array<const char*, 6> required = {"DATA/RESOURCE.DAT", "DATA/IO/IO.DAT", "DATA/BOBS/JOBS.BOB",
                                                        "DATA/SOUNDDAT/SOUND.LST", "GFX/PALETTE/PAL5.BBM",
                                                        "DATA/MBOB/ROM_Z.LST"};
    for(const char* f : required)
    {
        if(!bfs::exists(gameData / f))
            return std::string("file ") + f + " is missing in " + gameData.string();
    }
    if(!bfs::exists(appData / "RTTR" / "gamedata"))
        return "program data (RTTR/gamedata) not found in " + appData.string();
    return {};
}

bool DataPaths::hasGameData() const
{
    return missingGameDataReason().empty();
}

void DataPaths::apply() const
{
    RTTRCONFIG.init();
    RTTRCONFIG.overridePathMapping("GAME", gameData);
    RTTRCONFIG.overridePathMapping("DATA", appData);
    RTTRCONFIG.overridePathMapping("RTTR", appData / "RTTR");
    RTTRCONFIG.overridePathMapping("USERDATA", userData);
    for(const char* folder : {s25::folders::save, s25::folders::logs, s25::folders::screenshots})
        bfs::create_directories(RTTRCONFIG.expandPath(folder));
    LOG.setLogFilepath(RTTRCONFIG.expandPath(s25::folders::logs));
}

} // namespace weiler
