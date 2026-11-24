# Weiler

Weiler is a game engine that plays the original data of the *Gold Edition* of the classic settlement building game
from 1996 by its original rules, with a completely new, touch friendly user interface. It is made for Linux phones
(arm64, OpenGL ES 3) and works just as well on a desktop with mouse and keyboard.

The game rules, the AI and the file parsers come from [Return to the Roots](https://www.rttr.info)
(see `THIRD_PARTY.md`). The user interface, renderer, music and interface sounds are new. The music
(`data/music`) consists of own pieces; their score is `tools/music/pieces.py`, rendered with `tools/music/render.sh`
(fluidsynth and the FluidR3_GM soundfont). In the settings the music of the original CD can be imported from your
own CD image (`.ccd`/`.img` or `.cue`/`.bin` next to the game data); it stays on your device.

**Weiler needs the data of the Gold Edition**, which is not included. Copy the game folder (the one containing
`DATA` and `GFX`) to `~/.local/share/weiler/S2`, or point the game to it with `--data <folder>` or the
environment variable `WEILER_DATA`.

## Building

Dependencies (Debian): `cmake ninja-build g++ libboost-dev libboost-filesystem-dev libboost-iostreams-dev
libboost-locale-dev libboost-nowide-dev libboost-program-options-dev liblua5.1-0-dev libbz2-dev libsdl3-dev
libfreetype-dev libharfbuzz-dev libgles-dev libegl-dev libvorbis-dev gettext`.

    cmake -S . -B build -G Ninja
    ninja -C build
    ./build/src/weiler --data /path/to/S2

Tests (need the game data): `cmake -S . -B build -DWEILER_TEST_DATA=/path/to/S2 && ctest --test-dir build`.

`weiler-headless` runs AI-only games without user interface, e.g.
`weiler-headless --data S2 --map DATA/MAPS4/KARTE01.WLD --ais 3 --minutes 30`.

## License

GPL-2.0-or-later, see `LICENSE`. This project was developed with the help of AI tools.
