// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "App.h"
#include <boost/program_options.hpp>
#include <iostream>

namespace po = boost::program_options;

namespace weiler {
// Options for starting directly into a game (development and tests)
std::string gQuickMap;
unsigned gQuickAIs = 2;
unsigned gQuickSeed = 1;
unsigned gQuickWarmupGF = 0;
std::string gQuickLevel = "medium";
bool gQuickAutoplay = false;
std::string gQuickLoad;
} // namespace weiler

int main(int argc, char** argv)
{
    weiler::AppOptions opts;
    std::string size;
    po::options_description desc("Weiler options");
    desc.add_options()("help,h", "Show help")("data", po::value(&opts.dataDir),
                                                "Folder with the original game data (contains DATA and GFX)")(
      "size", po::value(&size), "Window size WxH")("scale", po::value(&opts.uiScale), "UI scale factor")(
      "fullscreen", po::bool_switch(&opts.fullscreen), "Fullscreen")(
      "script", po::value(&opts.script), "Run a test script (synthetic input, screenshots)")(
      "map", po::value(&weiler::gQuickMap), "Start a game on this map directly")(
      "ais", po::value(&weiler::gQuickAIs), "Number of AI opponents for --map")(
      "seed", po::value(&weiler::gQuickSeed), "Random seed for --map")(
      "warmup", po::value(&weiler::gQuickWarmupGF), "Simulate this many game frames before showing --map")(
      "load", po::value(&weiler::gQuickLoad), "Load a savegame directly")(
      "ai-level", po::value(&weiler::gQuickLevel), "AI strength for --map: easy, medium or hard")(
      "autoplay", po::bool_switch(&weiler::gQuickAutoplay), "Let an AI play for the local player (during --warmup; whole game with --load)");
    po::variables_map vm;
    try
    {
        po::store(po::parse_command_line(argc, argv, desc), vm);
        po::notify(vm);
    } catch(const std::exception& e)
    {
        std::cerr << e.what() << "\n" << desc << std::endl;
        return 2;
    }
    if(vm.count("help"))
    {
        std::cout << desc << std::endl;
        return 0;
    }
    if(!size.empty())
    {
        const auto x = size.find('x');
        if(x != std::string::npos)
        {
            opts.width = std::stoi(size.substr(0, x));
            opts.height = std::stoi(size.substr(x + 1));
        }
    }
    weiler::App app(opts);
    return app.run();
}
