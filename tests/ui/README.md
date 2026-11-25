# Interface test scripts

Scripts for the built-in test mode (`weiler --script <file>`): synthetic touch input, screenshots and checks.
`ctest -L ui` runs them offscreen (see `tests/CMakeLists.txt`, screenshots in the build folder). By hand: from a
working folder that contains a `shots/` directory, with a separate user data folder:

    mkdir -p work/shots/long && cd work
    WEILER_USERDATA=$PWD/userdata SDL_VIDEO_DRIVER=offscreen weiler --data <S2 folder> --size 1080x2400 --scale 3 \
        --script ../tests/ui/sheets.txt --map DATA/MAPS4/KARTE01.WLD --ais 1

| Script | Start | Checks |
|--------|-------|--------|
| `tut.txt` | first start (empty user data) | introduction steps |
| `sheets.txt`, `misc.txt`, `build.txt` | `--map DATA/MAPS4/KARTE01.WLD --ais 1` | ring, road, sheets, pause, speed, back gesture |
| `taps.txt` | as above | taps of frequent actions (build and connect, stop production, tear down a site) |
| `save.txt` | as above | save slots, quick save/load |
| `road.txt` | as above | quick swipe from a flag pans, hold and drag builds a road without flags between, with flags from the road bar, choices at the road end, drag back cancels, donkey road and tear down from the middle of a road, road stays open with "build on release: never" |
| `map.txt` | as above, `--size 2400x1080` | dialog over the map wins, map follows a held finger, layers, small corner map |
| `grids.txt` | as above, portrait and landscape | store, inventory, tools, statistics as grids; building size marks and badges |
| `camp.txt` | main menu | campaign missions |
| `land.txt` | main menu, `--size 2400x1080` or `1920x1080` | landscape layout |
| `attack.txt` | `--map DATA/MAPS3/AKARTE01.WLD --ais 1 --warmup 60000 --autoplay` | attack sheet, garrison (troop limits, exchange) |
| `goal.txt` | `--map DATA/MAPS3/AKARTE01.WLD --ais 1 --warmup 60000 --autoplay --objective conquer` | victory condition in the post office, team victory |
| `perf.txt`, `names.txt` | `--map ... --warmup 36000 --autoplay` | frame time (`[perf]` lines), name labels |
| `datasetup1..3.txt` | no game data (`DataSetup.cmake`: own home folder, data linked in) | missing data, folder dialog and own browser, remembered folder, removable medium gone and back, lower case names |
| `record.txt` | `--map DATA/MAPS4/KARTE01.WLD --ais 1` (`RecordReplay.cmake`) | the game is recorded in `REPLAYS/`, the log names the file, `weiler-headless --replay` plays it back equal |
| `genlong.py` | writes a long game script: `python3 genlong.py long.txt` | economy, military, attack |

Test hooks of the game screen: `game seafaring` places a harbor with wares and a ship next to it (ship and expedition
sheets without a long game, e.g. on `DATA/MAPS/MISS203.WLD`), `game gameover win|lose|team <mask>` shows the end of game sheet (team: bit mask of the winning players),
`game expect connected|nothing found` checks the building placed at `found:` (its flag has a road / it is gone),
`game findroad` puts `found:` on the nearest own road node without flag, `game expect roadat|noroadat|donkeyat found` checks
the road through it, `game setting <key> <int>` changes a setting. `drag x0 y0 x1 y1 [frames] [hold]` rests `hold` frames before moving.
With `WEILER_AUDIO_WAV=<file.wav>` the test mode writes the audio mix (music, effects, interface sounds) into a WAV
file instead of playing it.

Measurements: `perf <frames> [label]` measures the next frames and prints `[perf]` (frame time mean and percentiles,
simulation/world/interface time, GPU wait, draw calls, game frame times, memory, atlas); `perfasync` measures while
the following commands run (gestures); `overdraw [heatmap.png]` prints the shaded fragments per pixel of the next
frame; `game speed <1..8>` sets the game speed. Load times are printed as `[time]` lines. Frame pacing (sleeping
between frames) is off in the test mode unless `WEILER_TEST_PACING=1` is set.

Test hooks of the game data screen: `game dialog <folder>|cancel|fail` delivers the result of the folder dialog (the
test mode opens no real dialog; `fail` opens the own folder browser), `game expect missing|unreachable|browse|busy|idle`
checks its state; `rename <from> <to>` moves a folder (a removable medium that disappears or comes back).

Finger steps `down x y`, `move x y [frames]`, `up x y` hold one finger across commands (checks while holding).
`game mark` remembers the camera and the number of own flags; `game expect moved|still|flags <n>|flagsmin <n>`
compares with it; `game expect road <parts>|noroad|cornermap on|off|layers <mask>` checks the interface state.

Failures are printed as `[script] FAIL`, the exit code is 4 then. Commands are listed in `src/app/TestScript.h`.
