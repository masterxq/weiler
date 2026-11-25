# Weiler

Weiler plays the classic settlement building game of 1996 from the data of its *Gold Edition*, by the original
rules, with a new interface made for touch screens. It is built for Linux phones (arm64, OpenGL ES 3) and works
just as well on a desktop with mouse and keyboard. Single player against computer opponents: free games on the
original maps, the Roman and the world campaign, saving and loading with several slots and an autosave.

The game rules, the AI and the file formats come from [Return to the Roots](https://www.rttr.info); the interface,
renderer, music and interface sounds are new (see `THIRD_PARTY.md`). This project was developed with the help of
AI tools.

## Installation

Debian packages for arm64 and amd64 (built against Debian testing) are published in the APT repository
`codeberg.org/moblix`:

    sudo curl -o /etc/apt/keyrings/forgejo-moblix.asc https://codeberg.org/api/packages/moblix/debian/repository.key
    echo "deb [signed-by=/etc/apt/keyrings/forgejo-moblix.asc] https://codeberg.org/api/packages/moblix/debian testing main" \
        | sudo tee /etc/apt/sources.list.d/moblix.list
    sudo apt update
    sudo apt install weiler

The package contains the game (`/usr/games/weiler`), its own data (`/usr/share/games/weiler`), the desktop entry and
`weiler-pack-data`. Snapshot builds of the `main` branch get versions like `0.1.0+git20261125.120000.gabcdef1`.

## Game data

**Weiler needs the data of the Gold Edition**, which is not included. There are three ways to provide it:

- **Choose the folder in the game.** Without data Weiler asks for a folder (folder dialog of the desktop, or its own
  touch folder browser), searches it with all sub folders for the game folder (the one that contains `DATA` and
  `GFX`) and remembers it. If the remembered folder is on a memory card or USB stick that is not inserted, Weiler
  shows the path and waits until you retry or choose another folder.
- **As a package for all users:** `weiler-pack-data` builds the local package `weiler-data` from the game folder or
  a CD image (`.iso`, `.ccd`/`.img`, `.cue`/`.bin`; needs `bchunk` and `7zip`) and installs it to
  `/usr/share/games/weiler-data`. Without options it asks for everything:

      weiler-pack-data
      weiler-pack-data --source /media/cdrom --install
      weiler-pack-data --source ~/S2GOLD.ccd --music --install   # with the music of the CD

  Only the files Weiler loads go into the package (`data/gamedata.list`). It is for your own computer; the game data
  must not be passed on.
- **By hand:** copy the game folder to `~/.local/share/weiler/S2`, or start Weiler with `--data <folder>` or
  `WEILER_DATA=<folder>`.

Weiler looks in this order: `--data`, `WEILER_DATA`, the remembered folder, the package `weiler-data`,
`~/.local/share/weiler/S2`. The music of the original CD can also be imported in the settings from a CD image next to
the game data; it stays on your device.

## Building

Debian packages: `cmake ninja-build g++ libboost-filesystem-dev libboost-iostreams-dev libboost-locale-dev
libboost-nowide-dev libboost-program-options-dev liblua5.1-0-dev libbz2-dev libsdl3-dev libfreetype-dev
libharfbuzz-dev libgles-dev libegl-dev libvorbis-dev gettext`.

    cmake -S . -B build -G Ninja
    ninja -C build
    ./build/src/weiler

A development build finds its data in the source tree. `ninja -C build install` installs to the prefix
(`-DCMAKE_INSTALL_PREFIX`, default `/usr/local`); `-DWEILER_INSTALL_BINDIR=games
-DWEILER_INSTALL_DATADIR=share/games/weiler -DWEILER_SOURCE_PATHS=OFF` gives the Debian layout. The installed program
finds its data relative to itself (`../share/...`), so an unpacked package runs from any folder.

The Debian package: `dpkg-buildpackage -b -us -uc` (`debian/`, needs `debhelper`). The workflow
`.github/workflows/deb.yml` builds it for arm64 and amd64 in Debian testing, runs the tests that need no game data
and lintian, and publishes builds of `main` to the APT repository (secret `CODEBERG_TOKEN`).

Options: `--data <folder>`, `--fullscreen`, `--size 1080x2400`, `--scale <factor>` (interface size), `--help`.
On arm64 a development build is tuned for Cortex-A76/A55; `-DWEILER_ARM_TUNING=OFF` builds for any ARMv8 device (the
Debian package does).

Tests: `ctest --test-dir build`. Most tests need the game data: `cmake -S . -B build -DWEILER_TEST_DATA=<folder>`
(simulation, savegames, damaged files, the interface scripts in `tests/ui`, which run offscreen, and the game data
dialog). `pack_data` checks `weiler-pack-data` with a fake game folder and runs without data.

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
| Road | drag from a flag: the road follows the finger; where the drag ends a flag is set (back onto the start flag cancels). The bar below builds the road plainly or with flags along it. Tap the end of the road: set a flag there and go on, build, cancel | drag from a flag |
| Building, flag, ship | tap it | click |
| Attack | tap an enemy military building, choose the soldiers | click |
| Information about a place | long press | |
| Messages | tap a message to jump to its place; the post office filters by kind and deletes | N |
| Pause, speed (1x to 8x) | buttons at the top | Space, + and - |
| Building sites, map, headquarters | buttons on the right; the map follows the finger, its layers (terrain, water, territory, buildings, roads) and a small map in the bottom left corner are switched below it | B, M, H |
| Building names | settings | P |
| Quick save, quick load | menu | F5, F9 |

New buildings are connected to the road network automatically (can be switched off in the settings). Building sites
show their size by shape: hut, house with chimney, castle (also as a badge in the build menu). Stores, the inventory,
the tool production and the statistics show everything as a grid of symbols with numbers; a tap on one shows its
name and, in stores, its setting (store, send out, stop). Settlers are only sent out of a store when another store
takes them. Military
buildings limit their garrison per rank and exchange their soldiers; the economy sheets have default buttons as in
the original. The victory condition of a free game and the own share of the land are shown in the post office. The
introduction in the main menu shows the gestures step by step.

Every game is recorded (the commands of all players) in `REPLAYS/` of the user data folder (`~/.local/share/weiler`
or `WEILER_USERDATA`); the newest 5 recordings are kept. `weiler-headless --replay <file>` plays one back without
interface, e.g. with `--foreign` to list figures outside their territory. Own figures (no soldiers) that step into
another player's territory are written to the log in `LOGS/` with job, state, origin, goal and reason.

## License

GPL-2.0-or-later, see `LICENSE`. Fonts: SIL Open Font License; music and soundfont: see `THIRD_PARTY.md`.
