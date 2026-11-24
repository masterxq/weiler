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
| German translation of the core texts (from the `languages` submodule) | `third_party/rttr/data/RTTR/languages/rttr-de.po` | GPL-2.0-or-later |

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
- The override graphics of RTTR (`assets/overrides`) are not included; the loader skips the missing folder and
  geologist water signs fall back to the single sign of the original data.
- Of the RTTR graphics only `assets/base/colors.act` (player color palette) is included. Not included: the addon
  graphics (charburner, wine, leather, catapult), the Babylonian nation, `io_new`, `map_new` and `fireworks`.
  The loader loads addon graphics only for enabled addons (Weiler keeps these addons disabled), no longer loads
  `io_new`/`map_new`, and military buildings near harbors show the original middle distance flag.
- `RttrConfig` no longer changes the working directory.
- Loading savegames: military buildings sort their soldiers only after all objects are read
  (`nobMilitary::finishLoading`), the soldiers may still be under construction before.
- Damaged files are reported as errors instead of assertions or buffer overreads: `BinaryFile::readShortString` and
  `readLongString` (libutil), `Serializer::popBool` (libutil), block headers in `ArchivItem_Map.cpp` (libsiedler2).
- `Loader`: accessors for the nation archives (preloading of the sprite atlas); the carrier cache uses an empty
  archive for the addon graphics that are not loaded.
- `ai/random.cpp`: the AI generator starts without `random_device`, it is seeded from the game seed.
- Lua: `rttr:SetCampaignChapterCompleted(campaign, chapter)` reports a completed campaign chapter to the frontend
  (`ILocalGameState`); the Roman mission scripts (`data/RTTR/campaigns/roman/MISS2*.lua`) call it at their final
  event.
- `lua/LuaInterfaceBase.cpp`: translated texts of the Lua scripts pass `gettext` once more, so that
  `data/lang/weiler-de.po` (merged before `rttr-de.po` at build time) can adapt the German mission texts to the
  informal address of the Weiler interface.
- `data/RTTR/campaigns/world/campaign.lua`: German spelling of the campaign name ("Weltkampagne").

The game loop in `src/engine/Session.cpp` is adapted from RTTR (`extras/ai-battle`, `network/GameClient*`).

## Lato (font)

Files: `data/fonts/Lato-Regular.ttf`, `data/fonts/Lato-Bold.ttf` (version 2.015, taken from the Debian package
`fonts-lato`, upstream http://www.latofonts.com).

Copyright (c) 2010-2011, Łukasz Dziedzic (as stated by Debian and `OFL-Lato.txt`). License: SIL Open Font License 1.1 (OFL-1.1),
full text in `data/fonts/OFL-Lato.txt`.

## Noto Serif Display (font)

File: `data/fonts/NotoSerifDisplay-SemiBold.ttf`, taken unchanged from the Debian package `fonts-noto-core`
(upstream https://github.com/notofonts). Used for titles and the "Weiler" word mark.

Copyright 2010, 2012-2020 Google Inc. / Google LLC (as stated by Debian). License: SIL Open Font License 1.1
(OFL-1.1), full text in `data/fonts/OFL-Noto.txt`.

## Music and FluidR3_GM (soundfont)

The pieces in `data/music/*.ogg` are own compositions of this project (GPL-2.0-or-later like the code). Their score
is `tools/music/pieces.py` (notation helpers in `tools/music/score.py`); `tools/music/render.sh` renders them with
fluidsynth and the General MIDI soundfont FluidR3_GM (Debian package `fluid-soundfont-gm`, file
`/usr/share/sounds/sf2/FluidR3_GM.sf2`) and encodes them as Ogg Vorbis. The soundfont itself is not part of this
repository; the rendered audio contains its instrument samples.

FluidR3_GM: Copyright 2000-2002, 2008 Frank Wen, 2008 Toby Smithe. License: MIT (as stated by Debian):

    Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
    documentation files (the "Software"), to deal in the Software without restriction, including without limitation
    the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and
    to permit persons to whom the Software is furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in all copies or substantial portions of
    the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
    THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
    SOFTWARE.

The interface sounds are synthesized at runtime (`src/app/Audio.cpp`), no sample files.
