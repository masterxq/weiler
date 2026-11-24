# Third-party components

## Return to the Roots (frozen copy in `third_party/rttr/`)

Source: https://github.com/Return-To-The-Roots/s25client and its submodules. Exact commits are listed in
`third_party/rttr/COMMITS`.

| Part | Path | License |
|------|------|---------|
| Game simulation, AI, savegames, replays, Lua missions (s25main) | `third_party/rttr/libs/s25main` | GPL-2.0-or-later |
| Game data descriptions (libGamedata), configuration, common helpers | `third_party/rttr/libs/{libGamedata,rttrConfig,common}` | GPL-2.0-or-later |
| libsiedler2 (file formats of the original game) | `third_party/rttr/external/libsiedler2` | GPL-2.0-or-later |
| libutil, libendian, mygettext | `third_party/rttr/external/{libutil,libendian,mygettext}` | GPL-2.0-or-later (single files BSL-1.0 / BSD-3-Clause / CC0-1.0, see SPDX headers) |
| kaguya (Lua binding, header only) | `third_party/rttr/external/kaguya` | BSL-1.0 |
| RTTR data: world descriptions, campaigns, additional graphics for addon buildings | `third_party/rttr/data/RTTR` | GPL-2.0-or-later (see `.reuse/dep5` of the upstream repository) |

License texts: `third_party/rttr/LICENSE`, `third_party/rttr/LICENSES/`.

Changes against upstream:
- The complete user interface of RTTR was removed (desktops, ingame windows, controls, window manager, fonts,
  video/audio driver layer, lobby and network code, menu graphics).
- Couplings of the game objects to the removed parts were replaced by narrow interfaces, all marked with
  "Weiler" in the source: `ViewState.h` (frame timing and local player for drawing), `ogl/SpriteRenderer.h`
  (drawing of sprites, implemented by the Weiler renderer), `drivers/AudioDriverWrapper.h` (sound backend),
  `ILocalGameState.h` (Lua message boxes), `world/GameWorldViewer.h` (terrain change observer),
  `GlobalGameSettings` (addon configuration passed in instead of read from the RTTR settings).
- Sound conversion only corrects the sample rate in the header (no libsamplerate).
- `RttrConfig` no longer changes the working directory.

The game loop in `src/engine/Session.cpp` is adapted from RTTR (`extras/ai-battle`, `network/GameClient*`).
