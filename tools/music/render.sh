#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Render the Weiler music: MIDI (pieces.py) -> fluidsynth with the FluidR3_GM soundfont -> Ogg Vorbis in data/music.
# Needs: python3, fluidsynth, fluid-soundfont-gm, ffmpeg (with libvorbis).
#   tools/music/render.sh [work folder]
set -e
here=$(cd "$(dirname "$0")" && pwd)
out="$here/../../data/music"
work=${1:-$(mktemp -d)}
sf=/usr/share/sounds/sf2/FluidR3_GM.sf2
mkdir -p "$work" "$out"
python3 "$here/pieces.py" "$work"
for mid in "$work"/*.mid; do
    name=$(basename "$mid" .mid)
    fluidsynth -ni -q -r 44100 -g 0.6 -R 1 -C 0 \
        -o synth.reverb.room-size=0.72 -o synth.reverb.damp=0.35 -o synth.reverb.width=0.9 -o synth.reverb.level=0.75 \
        -F "$work/$name.wav" "$sf" "$mid"
    # Same loudness for all pieces (calm: -20 LUFS), gentle fade at the very end
    len=$(ffprobe -v error -show_entries format=duration -of csv=p=0 "$work/$name.wav")
    fade=$(python3 -c "print(max(0.0, $len - 3.0))")
    ffmpeg -v error -y -i "$work/$name.wav" \
        -af "loudnorm=I=-20:TP=-2:LRA=11,afade=t=out:st=$fade:d=3,aresample=44100" \
        -c:a libvorbis -q:a 2 "$out/$name.ogg"
    echo "$name.ogg"
done
