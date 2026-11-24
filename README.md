# Weiler

Weiler plays the classic settlement building game of 1996 from the data of its *Gold Edition*, by the original
rules, with a new interface made for touch screens. It is built for Linux phones (arm64, OpenGL ES 3) and works
just as well on a desktop with mouse and keyboard. Single player against computer opponents: free games on the
original maps, the Roman and the world campaign, saving and loading with several slots and an autosave.

The game rules, the AI and the file formats come from [Return to the Roots](https://www.rttr.info); the interface,
renderer, music and interface sounds are new (see `THIRD_PARTY.md`). This project was developed with the help of
AI tools.

## Game data

**Weiler needs the data of the Gold Edition**, which is not included. Copy the game folder of your installation or
CD (the folder that contains `DATA` and `GFX`) to

    ~/.local/share/weiler/S2

or start Weiler with `--data <folder>` or the environment variable `WEILER_DATA=<folder>`. Without data Weiler shows
a short guide instead of the game. The music of the original CD can be imported in the settings from your own CD
image (`.ccd`/`.img` or `.cue`/`.bin` next to the game data); it stays on your device.

## Building

Debian packages: `cmake ninja-build g++ libboost-dev libboost-filesystem-dev libboost-iostreams-dev
libboost-locale-dev libboost-nowide-dev libboost-program-options-dev liblua5.1-0-dev libbz2-dev libsdl3-dev
libfreetype-dev libharfbuzz-dev libgles-dev libegl-dev libvorbis-dev gettext`.

    cmake -S . -B build -G Ninja
    ninja -C build
    ./build/src/weiler

Options: `--data <folder>`, `--fullscreen`, `--size 1080x2400`, `--scale <factor>` (interface size).
On arm64 the build is tuned for Cortex-A76/A55; `-DWEILER_ARM_TUNING=OFF` builds for any ARMv8 device.

Tests need the game data: `cmake -S . -B build -DWEILER_TEST_DATA=<folder>`, then `ctest --test-dir build`
(simulation, savegames, damaged files and the interface scripts in `tests/ui`, which run offscreen).

## Playing

The world fills the screen. At the top: menu, game time, speed, pause and messages. On the right edge: building
sites, roads, minimap and a jump button (newest event first, then headquarters and warehouses). Menus open as a sheet from the bottom (portrait) or from the
side (landscape).

| Action | Touch | Mouse and keyboard |
|--------|-------|--------------------|
| Move the map | drag with one finger | drag with the left button |
| Zoom | pinch, double tap zooms in | wheel |
| Back (close a menu, cancel) | tap with two fingers | right click, Esc |
| Build | tap a building site: ring with the buildings that fit there, recently used first | click |
| Road | drag from a flag: the road follows the finger, flags are placed on the way | drag from a flag |
| Building, flag, ship | tap it | click |
| Attack | tap an enemy military building, choose the soldiers | click |
| Information about a place | long press | |
| Messages | tap a message to jump to its place; the post office filters by kind and deletes | N |
| Pause, speed (1x to 8x) | buttons at the top | Space, + and - |
| Building sites, minimap, headquarters | buttons on the right | B, M, H |
| Building names | settings | P |
| Quick save, quick load | menu | F5, F9 |

New buildings are connected to the road network automatically (can be switched off in the settings). Military
buildings limit their garrison per rank and exchange their soldiers; the economy sheets have default buttons as in
the original. The victory condition of a free game and the own share of the land are shown in the post office. The
introduction in the main menu shows the gestures step by step.

## License

GPL-2.0-or-later, see `LICENSE`. Fonts: SIL Open Font License; music and soundfont: see `THIRD_PARTY.md`.
