// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DataPaths.h"
#include "RttrConfig.h"
#include "files.h"
#include "s25util/Log.h"
#include "s25util/System.h"
#include <boost/filesystem.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <deque>
#include <fstream>

namespace bfs = boost::filesystem;

// Installed layout, relative to the folder of the program (set by CMake)
#ifndef WEILER_APPDATA_REL
#    define WEILER_APPDATA_REL "../share/weiler"
#endif
#ifndef WEILER_INSTALL_DATA_DIR
#    define WEILER_INSTALL_DATA_DIR ""
#endif
// Development builds only (WEILER_SOURCE_PATHS): data in the source and build tree
#ifndef WEILER_SOURCE_DATA_DIR
#    define WEILER_SOURCE_DATA_DIR ""
#endif
#ifndef WEILER_FONT_DIR
#    define WEILER_FONT_DIR ""
#endif
#ifndef WEILER_MUSIC_DIR
#    define WEILER_MUSIC_DIR ""
#endif
#ifndef WEILER_LANG_DIR
#    define WEILER_LANG_DIR ""
#endif
#ifndef WEILER_GAMEDATA_LIST
#    define WEILER_GAMEDATA_LIST ""
#endif

namespace weiler {
namespace {
/// Files that identify the folder of the Gold Edition (also marked with "!" in data/gamedata.list)
constexpr std::array<const char*, 6> requiredFiles = {"DATA/RESOURCE.DAT",      "DATA/IO/IO.DAT",
                                                      "DATA/BOBS/JOBS.BOB",     "DATA/SOUNDDAT/SOUND.LST",
                                                      "GFX/PALETTE/PAL5.BBM",   "DATA/MBOB/ROM_Z.LST"};

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

bfs::path exeDir()
{
    const bfs::path exe = System::getExecutablePath();
    return exe.empty() ? bfs::path() : exe.parent_path();
}

bool iequals(const std::string& a, const std::string& b)
{
    return a.size() == b.size()
           && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
                  return std::toupper(x) == std::toupper(y);
              });
}

/// Case-insensitive glob match ('*' and '?')
bool globMatch(const char* pat, const char* s)
{
    if(*pat == '\0')
        return *s == '\0';
    if(*pat == '*')
        return globMatch(pat + 1, s) || (*s && globMatch(pat, s + 1));
    if(*s && (*pat == '?' || std::toupper(static_cast<unsigned char>(*pat)) == std::toupper(static_cast<unsigned char>(*s))))
        return globMatch(pat + 1, s + 1);
    return false;
}

/// Find the entry of dir named like name in any case (exact name first)
bfs::path childAnyCase(const bfs::path& dir, const std::string& name)
{
    boost::system::error_code ec;
    if(bfs::exists(dir / name, ec))
        return dir / name;
    for(bfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
    {
        if(iequals(it->path().filename().string(), name))
            return it->path();
    }
    return {};
}

/// Resolve a relative path like "DATA/IO/IO.DAT" below dir in any case
bfs::path resolveAnyCase(const bfs::path& dir, const std::string& rel)
{
    bfs::path cur = dir;
    for(const auto& part : bfs::path(rel))
    {
        cur = childAnyCase(cur, part.string());
        if(cur.empty())
            return {};
    }
    return cur;
}

bool isEmptyDir(const bfs::path& dir)
{
    boost::system::error_code ec;
    return bfs::is_directory(dir, ec) && bfs::directory_iterator(dir, ec) == bfs::directory_iterator();
}

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

bfs::path firstExisting(std::initializer_list<bfs::path> dirs, const char* probe)
{
    boost::system::error_code ec;
    for(const auto& d : dirs)
    {
        if(!d.empty() && bfs::exists(d / probe, ec))
            return d;
    }
    return {};
}

void findAppData(DataPaths& p)
{
    // 1. $WEILER_APPDATA, 2. installed layout next to the program (also an unpacked package), 3. the configured
    // install folder, 4. the source tree (development builds)
    std::vector<bfs::path> candidates;
    if(!envPath("WEILER_APPDATA").empty())
        candidates.push_back(envPath("WEILER_APPDATA"));
    const bfs::path exe = exeDir();
    if(!exe.empty())
        candidates.push_back((exe / WEILER_APPDATA_REL).lexically_normal());
    if(*WEILER_INSTALL_DATA_DIR)
        candidates.emplace_back(WEILER_INSTALL_DATA_DIR);
    if(*WEILER_SOURCE_DATA_DIR)
        candidates.emplace_back(WEILER_SOURCE_DATA_DIR);
    p.appData = candidates.empty() ? bfs::path() : candidates.back();
    boost::system::error_code ec;
    for(const auto& c : candidates)
    {
        if(bfs::exists(c / "RTTR" / "gamedata", ec))
        {
            p.appData = c;
            break;
        }
    }
    p.fontDir = firstExisting({p.appData / "fonts", bfs::path(WEILER_FONT_DIR)}, "Lato-Regular.ttf");
    p.musicDir = firstExisting({p.appData / "music", bfs::path(WEILER_MUSIC_DIR)}, ".");
    p.langDir = firstExisting({p.appData / "languages", bfs::path(WEILER_LANG_DIR)}, "rttr-de.mo");
    p.gameFileList = p.appData / "gamedata.list";
    if(!bfs::exists(p.gameFileList, ec) && *WEILER_GAMEDATA_LIST)
        p.gameFileList = WEILER_GAMEDATA_LIST;
}
} // namespace

std::vector<bfs::path> DataPaths::packageDataCandidates()
{
    if(!envPath("WEILER_PACKAGE_DATA").empty())
        return {envPath("WEILER_PACKAGE_DATA")};
    std::vector<bfs::path> c;
    const bfs::path exe = exeDir();
    if(!exe.empty())
        c.push_back((exe / WEILER_APPDATA_REL / ".." / "weiler-data" / "S2").lexically_normal());
    c.emplace_back("/usr/share/games/weiler-data/S2");
    return c;
}

bfs::path DataPaths::userGameData()
{
    return xdgDataHome() / "weiler" / "S2";
}

DataPaths DataPaths::detect(const std::string& gameDataArg, const std::string& rememberedArg)
{
    DataPaths p;
    findAppData(p);
    p.userData = xdgDataHome() / "weiler";
    if(!envPath("WEILER_USERDATA").empty())
        p.userData = envPath("WEILER_USERDATA");

    if(!rememberedArg.empty())
    {
        p.rememberedData = rememberedArg;
        boost::system::error_code ec;
        if(isGameData(p.rememberedData))
            p.remembered = Remembered::Ok;
        else if(!bfs::exists(p.rememberedData, ec) || isEmptyDir(p.rememberedData))
            p.remembered = Remembered::Unreachable;
        else
            p.remembered = Remembered::Invalid;
    }
    // An explicit folder is used even if it is not valid (the reason is shown)
    if(!gameDataArg.empty())
    {
        p.gameData = gameDataArg;
        p.source = Source::Argument;
        return p;
    }
    if(!envPath("WEILER_DATA").empty())
    {
        p.gameData = envPath("WEILER_DATA");
        p.source = Source::Environment;
        return p;
    }
    if(p.remembered == Remembered::Ok)
    {
        p.gameData = p.rememberedData;
        p.source = Source::Remembered;
        return p;
    }
    for(const auto& c : packageDataCandidates())
    {
        if(isGameData(c))
        {
            p.gameData = c;
            p.source = Source::Package;
            return p;
        }
    }
    p.gameData = userGameData();
    p.source = isGameData(p.gameData) ? Source::User : Source::None;
    return p;
}

bool DataPaths::isGameData(const bfs::path& dir)
{
    boost::system::error_code ec;
    return std::all_of(requiredFiles.begin(), requiredFiles.end(),
                       [&](const char* f) { return bfs::is_regular_file(dir / f, ec); });
}

bool DataPaths::isGameDataAnyCase(const bfs::path& dir)
{
    return std::all_of(requiredFiles.begin(), requiredFiles.end(),
                       [&](const char* f) { return !resolveAnyCase(dir, f).empty(); });
}

bfs::path DataPaths::findGameData(const bfs::path& root, const std::atomic<bool>* cancel)
{
    // Breadth first: the shallowest match wins. Hidden folders and links to folders are skipped (no loops).
    constexpr unsigned maxDepth = 10;
    constexpr size_t maxFolders = 200000;
    std::deque<std::pair<bfs::path, unsigned>> queue{{root, 0}};
    size_t visited = 0;
    while(!queue.empty() && ++visited < maxFolders)
    {
        if(cancel && *cancel)
            return {};
        auto [dir, depth] = queue.front();
        queue.pop_front();
        if(!childAnyCase(dir, "DATA").empty() && isGameDataAnyCase(dir))
            return dir;
        if(depth >= maxDepth)
            continue;
        boost::system::error_code ec;
        std::vector<bfs::path> subs;
        for(bfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string name = it->path().filename().string();
            boost::system::error_code ec2;
            if(name.empty() || name[0] == '.' || bfs::is_symlink(it->symlink_status(ec2))
               || !bfs::is_directory(it->status(ec2)))
                continue;
            if(depth == 0 && dir == "/" && (name == "proc" || name == "sys" || name == "dev" || name == "run"))
                continue;
            subs.push_back(it->path());
        }
        std::sort(subs.begin(), subs.end());
        for(auto& s : subs)
            queue.emplace_back(std::move(s), depth + 1);
    }
    return {};
}

bool DataPaths::copyGameData(const bfs::path& src, const bfs::path& dst, std::string& error) const
{
    std::ifstream list(gameFileList.string());
    if(!list)
    {
        error = "list of the game files not found: " + gameFileList.string();
        return false;
    }
    boost::system::error_code ec;
    std::string line;
    unsigned copied = 0;
    while(std::getline(list, line))
    {
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t\r") + 1);
        if(line.empty() || line[0] == '#')
            continue;
        const bool required = line[0] == '!';
        if(line[0] == '!' || line[0] == '?')
            line.erase(0, 1);
        const bfs::path rel(line);
        const bfs::path dir = rel.has_parent_path() ? resolveAnyCase(src, rel.parent_path().string()) : src;
        unsigned matched = 0;
        if(!dir.empty())
        {
            const std::string pattern = rel.filename().string();
            for(bfs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec))
            {
                const std::string name = it->path().filename().string();
                if(!bfs::is_regular_file(it->path(), ec) || !globMatch(pattern.c_str(), name.c_str()))
                    continue;
                const bfs::path target = dst / upper(rel.parent_path().string()) / upper(name);
                bfs::create_directories(target.parent_path(), ec);
                bfs::copy_file(it->path(), target, bfs::copy_options::overwrite_existing, ec);
                if(ec)
                {
                    error = "cannot write " + target.string() + ": " + ec.message();
                    return false;
                }
                ++matched;
            }
        }
        if(required && !matched)
        {
            error = "file " + line + " is missing in " + src.string();
            return false;
        }
        copied += matched;
    }
    if(!copied || !isGameData(dst))
    {
        error = "no game data in " + src.string();
        return false;
    }
    return true;
}

std::string DataPaths::missingGameDataReason() const
{
    if(!bfs::is_directory(gameData))
        return "folder " + gameData.string() + " does not exist";
    for(const char* f : requiredFiles)
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
