// Copyright (C) 2026 Weiler contributors
//
// SPDX-License-Identifier: GPL-2.0-or-later

#include "App.h"
#include "CdImport.h"
#include <chrono>
#include <boost/program_options.hpp>
#include <iostream>
#include <sstream>
#include <thread>

namespace po = boost::program_options;

namespace weiler {
// Options for starting directly into a game (development and tests)
std::string gQuickMap;
unsigned gQuickAIs = 2;
unsigned gQuickSeed = 1;
unsigned gQuickWarmupGF = 0;
std::string gQuickLevel = "medium";
std::string gQuickObjective = "none";
bool gQuickAutoplay = false;
std::string gQuickLoad;
} // namespace weiler

int main(int argc, char** argv)
{
    weiler::AppOptions opts;
    std::string size, cdImage, cdOut;
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
      "objective", po::value(&weiler::gQuickObjective), "Victory condition for --map: none, conquer (3/4 of the land) or all")(
      "autoplay", po::bool_switch(&weiler::gQuickAutoplay), "Let an AI play for the local player (during --warmup; whole game with --load)")(
      "cd-music", po::value(&cdImage), "Convert the audio tracks of a CD image (.ccd or .cue) to Ogg Vorbis and quit")(
      "cd-music-out", po::value(&cdOut), "Folder for the tracks of --cd-music (trackNN.ogg)");
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
    if(!cdImage.empty())
    {
        // Used by weiler-pack-data: the tracks of the user's CD go into the local package weiler-data
        weiler::CdImport import;
        if(cdOut.empty() || !import.start(cdImage, cdOut))
        {
            std::cerr << (cdOut.empty() ? "--cd-music-out is missing" : import.error()) << std::endl;
            return 1;
        }
        while(import.running())
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        const int written = import.takeWritten();
        if(!import.error().empty())
            std::cerr << import.error() << std::endl;
        std::cout << written << " tracks written to " << cdOut << std::endl;
        return written > 0 ? 0 : 1;
    }
    if(!size.empty())
    {
        int w = 0, h = 0;
        char sep = 0;
        std::istringstream in(size);
        if(!(in >> w >> sep >> h) || sep != 'x' || w < 64 || h < 64)
        {
            std::cerr << "Invalid --size '" << size << "', expected WxH, e.g. 1080x2400" << std::endl;
            return 2;
        }
        opts.width = w;
        opts.height = h;
    }
    weiler::App app(opts);
    return app.run();
}
