# Copyright (C) 2026 Weiler contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""The Weiler music: own compositions (menu theme and five calm pieces for the game).

    python3 pieces.py <output folder>      writes one MIDI file per piece

Render them with render.sh (fluidsynth + FluidR3_GM soundfont, then Ogg Vorbis).
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from score import Song, arpeggio, bassline, drums, pads, third_below  # noqa: E402

# General MIDI programs
GUITAR, DULCIMER, VIOLIN, CELLO, CONTRABASS, PIZZ, HARP = 24, 15, 40, 42, 43, 45, 46
STRINGS, SLOW_STRINGS, CHOIR, HORN, OBOE, BASSOON, CLARINET = 48, 49, 52, 60, 68, 70, 71
FLUTE, RECORDER, PANFLUTE = 73, 74, 75

MAJOR = {0, 2, 4, 5, 7, 9, 11}


def scale(root, mode=MAJOR):
    return {(root + i) % 12 for i in mode}


# ---------------------------------------------------------------------------------------------------------------
def morgenrot():
    """Menu theme: D dorian, 3/4, harp and recorder, strings and choir in the middle part"""
    s = Song(66, 3, seed=11)
    harp = s.track("Harp", 0, HARP, volume=92, reverb=85, pan=48)
    rec = s.track("Recorder", 1, RECORDER, volume=96, reverb=80, pan=70)
    flute = s.track("Flute", 2, FLUTE, volume=88, reverb=85, pan=60)
    cello = s.track("Cello", 3, CELLO, volume=80, reverb=75, pan=40)
    pad = s.track("Strings", 4, SLOW_STRINGS, volume=70, reverb=90, pan=80)
    choir = s.track("Choir", 5, CHOIR, volume=58, reverb=100, pan=64)
    B = 3
    A1 = ["Dm", "G", "Dm", "C", "F", "C", "G", "Dm"]
    A2 = ["Dm", "G", "F", "C", "Dm", "Am", "G", "Dm"]
    BB = ["F", "C", "G", "Dm", "F", "C", "Em", "Am"]
    m1 = "d5/1 e5/1 f5/1 g5/2 f5/0.5 e5/0.5 d5/1 a4/1 d5/1 e5/2 c5/1 f5/1 g5/1 a5/1 g5/1.5 e5/0.5 c5/1 d5/1 b4/1 d5/1 d5/3"
    m2 = "a5/1 g5/1 f5/1 g5/1 b5/0.5 a5/0.5 g5/1 f5/1.5 e5/0.5 f5/1 e5/2 c5/1 d5/1 f5/1 a5/1 e5/2 c5/1 d5/1 b4/1 g4/1 a4/1 d5/2"
    mb = "c6/2 a5/1 g5/3 b5/1.5 a5/0.5 g5/1 a5/3 c6/1 d6/1 c6/1 g5/2 e5/1 g5/1 e5/1 b4/1 c5/1.5 b4/0.5 a4/1"
    harp_pat = [0, 1, 2, 3, 4, 3]
    t = 0
    # Intro
    arpeggio(harp, t, ["Dm", "G", "Dm", "G"], B, harp_pat, 0.5, center=62, vel=58)
    bassline(cello, t, ["Dm", "G", "Dm", "G"], B, [(0, 3, "r")], center=45, vel=52)
    t += 4 * B
    # A1, A2: recorder
    for chords, mel, with_pad in ((A1, m1, False), (A2, m2, True)):
        arpeggio(harp, t, chords, B, harp_pat, 0.5, center=62, vel=56)
        bassline(cello, t, chords, B, [(0, 3, "r")], center=45, vel=56)
        if with_pad:
            pads(pad, t, chords, B, center=62, vel=44)
        rec.melody(t, mel, vel=78)
        t += 8 * B
    # B: flute, strings and choir
    arpeggio(harp, t, BB, B, [0, 2, 4], 1, center=60, vel=50)
    bassline(cello, t, BB, B, [(0, 3, "r")], center=45, vel=60)
    pads(pad, t, BB, B, center=64, vel=52)
    pads(choir, t, BB, B, center=67, vel=40)
    flute.melody(t, mb, vel=76)
    t += 8 * B
    # A1 again: recorder with flute in thirds; A2 to the end
    arpeggio(harp, t, A1, B, harp_pat, 0.5, center=62, vel=56)
    bassline(cello, t, A1, B, [(0, 3, "r")], center=45, vel=56)
    pads(pad, t, A1, B, center=62, vel=40)
    rec.melody(t, m1, vel=78)
    flute.melody(t, third_below(m1, scale(0)), vel=58)
    t += 8 * B
    arpeggio(harp, t, A2, B, harp_pat, 0.5, center=62, vel=54)
    bassline(cello, t, A2, B, [(0, 3, "r")], center=45, vel=54)
    pads(pad, t, A2, B, center=62, vel=42)
    rec.melody(t, m2, vel=74)
    s.ritardando(t + 5 * B, 3 * B, 52)
    t += 8 * B
    # Ending: open D chord, harp rolls up
    arpeggio(harp, t, ["Dm"], 6, [0, 1, 2, 3, 4, 5], 0.5, center=62, vel=50, legato=8)
    cello.note(t, 6, 38, 50)
    pad.note(t, 6, 62, 36)
    pad.note(t, 6, 69, 34)
    s.end = t + 6
    return s


# ---------------------------------------------------------------------------------------------------------------
def feldweg():
    """G major, 6/8 lilt: guitar picking, flute melody, clarinet in the middle part"""
    s = Song(100, 3, seed=21)
    gtr = s.track("Guitar", 0, GUITAR, volume=96, reverb=60, pan=44)
    flute = s.track("Flute", 1, FLUTE, volume=92, reverb=70, pan=74)
    clar = s.track("Clarinet", 2, CLARINET, volume=86, reverb=70, pan=58)
    bass = s.track("Pizzicato", 3, PIZZ, volume=86, reverb=55, pan=50)
    pad = s.track("Strings", 4, SLOW_STRINGS, volume=64, reverb=85, pan=80)
    B = 3
    A1 = ["G", "C", "G", "D", "Em", "C", "D", "G"]
    A2 = ["G", "Em", "C", "D", "G", "C", "D", "G"]
    BB = ["Em", "Bm", "C", "G", "Am", "Em", "C", "D"]
    m1 = ("d5/1 b4/0.5 d5/1 g5/0.5 e5/1 c5/0.5 e5/1 g5/0.5 d5/1.5 b4/1.5 a4/1 f#4/0.5 a4/1 d5/0.5 "
          "g5/1 f#5/0.5 e5/1 b4/0.5 c5/1 e5/0.5 g5/1 e5/0.5 f#5/1 e5/0.5 d5/1 a4/0.5 b4/1 a4/0.5 g4/1.5")
    m2 = ("b4/1 c5/0.5 d5/1 b4/0.5 e5/1.5 g5/1.5 g5/1 e5/0.5 c5/1 e5/0.5 d5/1.5 a4/1.5 "
          "d5/1 e5/0.5 d5/1 b4/0.5 c5/1 e5/0.5 a5/1 g5/0.5 f#5/1 e5/0.5 d5/1 e5/0.5 g5/3")
    mb = ("b4/1 a4/0.5 g4/1 e4/0.5 f#4/1.5 d4/1.5 e4/1 g4/0.5 c5/1 b4/0.5 b4/1 a4/0.5 b4/1.5 "
          "c5/1 b4/0.5 a4/1 e4/0.5 g4/1.5 b4/1.5 c5/1 d5/0.5 e5/1 c5/0.5 d5/3")
    pick = [0, 2, 3, 1, 4, 2]
    brhythm = [(0, 1.4, "r"), (1.5, 1.4, "5")]
    scale_g = scale(7)

    def section(t, chords, mel_track, mel, vel=80, harmony=None, strings=False):
        arpeggio(gtr, t, chords, B, pick, 0.5, center=59, vel=58)
        bassline(bass, t, chords, B, brhythm, center=43, vel=64)
        if strings:
            pads(pad, t, chords, B, center=62, vel=42)
        if mel_track:
            mel_track.melody(t, mel, vel=vel)
        if harmony:
            harmony[0].melody(t, third_below(mel, scale_g), vel=harmony[1])
        return t + len(chords) * B

    t = 0
    arpeggio(gtr, t, ["G", "G"], B, pick, 0.5, center=59, vel=56)
    t += 2 * B
    t = section(t, A1, flute, m1)
    t = section(t, A2, flute, m2)
    t = section(t, BB, clar, mb, vel=76, strings=True)
    t = section(t, A1, flute, m1, harmony=(clar, 56), strings=True)
    t = section(t, A2, flute, m2)
    t = section(t, BB, flute, mb, vel=70, strings=True)
    t = section(t, A1, clar, m1, vel=72)
    s.ritardando(t, 4 * B, 78)
    t = section(t, A2[:4], flute, "b4/1 c5/0.5 d5/1 b4/0.5 e5/1.5 g5/1.5 g5/1 e5/0.5 c5/1 e5/0.5 d5/1.5 a4/1.5")
    arpeggio(gtr, t, ["G"], 6, [0, 1, 2, 3, 4], 0.5, center=59, vel=52, legato=8)
    bass.note(t, 3, 43, 60)
    flute.note(t, 5, 79, 64)
    s.end = t + 6
    return s


# ---------------------------------------------------------------------------------------------------------------
def muehlbach():
    """A dorian, 4/4: dulcimer ostinato like a turning mill wheel, oboe melody, strings in the middle part"""
    s = Song(80, 4, seed=31)
    dulc = s.track("Dulcimer", 0, DULCIMER, volume=80, reverb=70, pan=42)
    oboe = s.track("Oboe", 1, OBOE, volume=90, reverb=72, pan=72)
    flute = s.track("Flute", 2, FLUTE, volume=84, reverb=78, pan=60)
    bsn = s.track("Bassoon", 3, BASSOON, volume=82, reverb=60, pan=52)
    strings = s.track("Strings", 4, STRINGS, volume=76, reverb=86, pan=84)
    harp = s.track("Harp", 5, HARP, volume=80, reverb=80, pan=36)
    B = 4
    A1 = ["Am", "G", "Am", "D", "Am", "G", "Em", "Am"]
    A2 = ["C", "G", "D", "Am", "C", "G", "Em", "Am"]
    BB = ["D", "Am", "G", "Am", "C", "G", "Em", "Am"]
    m1 = ("e5/1.5 d5/0.5 c5/1 b4/1 d5/1.5 c5/0.5 b4/2 c5/1 d5/1 e5/1 a5/1 f#5/2 e5/1 d5/1 "
          "e5/1.5 c5/0.5 a4/2 b4/1 d5/1 g5/1 f#5/0.5 e5/0.5 e5/2 d5/1 b4/1 a4/4")
    m2 = ("g5/1 e5/1 c5/1 e5/1 d5/2 b4/2 a4/1 d5/1 f#5/1 a5/1 g5/1 e5/1 c5/2 "
          "e5/1 g5/1 c6/1 b5/1 a5/1 g5/1 d5/2 e5/1.5 d5/0.5 b4/2 a4/4")
    mb = "a4/2 f#4/2 e4/2 a4/2 b4/2 d5/2 c5/3 b4/1 g4/2 c5/2 b4/2 d5/1 c5/1 b4/2 g4/2 a4/4"
    wheel = [0, 2, 4, 5, 6, 5, 4, 2]
    scale_a = scale(7)

    def section(t, chords, mel_track, mel, vel=80, harmony=None, middle=False):
        if middle:
            arpeggio(harp, t, chords, B, [0, 2, 3, 4], 1, center=57, vel=52)
            pads(strings, t, chords, B, center=60, vel=46, swell=False)
            strings.melody(t, mel, vel=vel)
        else:
            arpeggio(dulc, t, chords, B, wheel, 0.5, center=60, vel=54)
            mel_track.melody(t, mel, vel=vel)
        bassline(bsn, t, chords, B, [(0, 1.8, "r"), (2, 1.8, "5")], center=45, vel=58)
        if harmony:
            harmony[0].melody(t, third_below(mel, scale_a), vel=harmony[1])
        return t + len(chords) * B

    t = 0
    arpeggio(dulc, t, ["Am", "Am"], B, wheel, 0.5, center=60, vel=52)
    t += 2 * B
    t = section(t, A1, oboe, m1)
    t = section(t, A2, oboe, m2)
    t = section(t, BB, None, mb, vel=62, middle=True)
    t = section(t, A1, flute, m1, vel=76)
    t = section(t, A2, oboe, m2, harmony=(flute, 54))
    t = section(t, BB, None, mb, vel=58, middle=True)
    s.ritardando(t + 4 * B, 4 * B, 64)
    t = section(t, A1, oboe, m1, vel=74)
    arpeggio(dulc, t, ["Am"], 6, [0, 2, 4, 6], 0.5, center=60, vel=48, legato=8)
    bsn.note(t, 4, 45, 54)
    s.end = t + 6
    return s


# ---------------------------------------------------------------------------------------------------------------
def abendweide():
    """F major with a lydian touch, slow 3/4: strings, harp, clarinet; horn in the middle part"""
    s = Song(58, 3, seed=41)
    harp = s.track("Harp", 0, HARP, volume=86, reverb=88, pan=40)
    clar = s.track("Clarinet", 1, CLARINET, volume=92, reverb=80, pan=70)
    horn = s.track("Horn", 2, HORN, volume=78, reverb=90, pan=56)
    pad = s.track("Strings", 3, SLOW_STRINGS, volume=78, reverb=92, pan=82)
    cb = s.track("Contrabass", 4, CONTRABASS, volume=78, reverb=70, pan=50)
    flute = s.track("Flute", 5, FLUTE, volume=82, reverb=88, pan=64)
    B = 3
    A1 = ["F", "G/F", "F", "C", "Dm", "Bb", "C", "F"]
    A2 = ["Dm", "Am", "Bb", "F", "Gm", "C", "C", "F"]
    BB = ["Bb", "F", "C", "Dm", "Bb", "F", "Gm", "C"]
    m1 = "c5/2 a4/1 b4/2 d5/1 c5/1.5 a4/0.5 f4/1 g4/3 a4/1 c5/1 f5/1 d5/2 bb4/1 e5/2 g4/1 f4/3"
    m2 = "a4/2 d5/1 e5/2 c5/1 d5/1 f5/1 d5/1 c5/3 bb4/2 g4/1 c5/1 e5/1 g5/1 f5/2 e5/1 f5/3"
    mb = "f4/2 d4/1 c4/2 f4/1 e4/2 g4/1 f4/3 d4/1 f4/1 bb4/1 a4/2 f4/1 g4/1.5 a4/0.5 bb4/1 g4/3"

    def section(t, chords, mel_track, mel, vel=78, flowing=True):
        arpeggio(harp, t, chords, B, [0, 2, 3, 4, 3, 2] if flowing else [0, 3, 4], 0.5 if flowing else 1,
                 center=60, vel=48)
        pads(pad, t, chords, B, center=60, vel=46)
        bassline(cb, t, chords, B, [(0, 3, "r")], center=41, vel=56)
        mel_track.melody(t, mel, vel=vel, legato=0.97)
        return t + len(chords) * B

    t = 0
    arpeggio(harp, t, ["F", "G/F"], B, [0, 2, 3, 4, 3, 2], 0.5, center=60, vel=48)
    pads(pad, t, ["F", "G/F"], B, center=60, vel=38)
    t += 2 * B
    t = section(t, A1, clar, m1)
    t = section(t, A2, clar, m2)
    t = section(t, BB, horn, mb, vel=70, flowing=False)
    t = section(t, A1, flute, m1, vel=72)
    t = section(t, A2, clar, m2, vel=74)
    t = section(t, BB, horn, mb, vel=66, flowing=False)
    s.ritardando(t + 4 * B, 4 * B, 46)
    t = section(t, A1, clar, m1, vel=70)
    arpeggio(harp, t, ["F"], 6, [0, 1, 2, 3, 4, 5], 0.5, center=60, vel=44, legato=8)
    pad.note(t, 6, 53, 36)
    pad.note(t, 6, 60, 34)
    pad.note(t, 6, 69, 32)
    s.end = t + 6
    return s


# ---------------------------------------------------------------------------------------------------------------
def hirtenlied():
    """D mixolydian, 4/4: drone, recorder and pan flute, a soft frame drum"""
    s = Song(92, 4, seed=51)
    drone = s.track("Drone", 0, BASSOON, volume=64, reverb=80, pan=50)
    drone2 = s.track("Drone strings", 1, SLOW_STRINGS, volume=56, reverb=90, pan=78)
    rec = s.track("Recorder", 2, RECORDER, volume=94, reverb=72, pan=66)
    pan = s.track("Pan flute", 3, PANFLUTE, volume=84, reverb=80, pan=58)
    oboe = s.track("Oboe", 4, OBOE, volume=86, reverb=76, pan=70)
    harp = s.track("Harp", 5, HARP, volume=80, reverb=80, pan=40)
    drum = s.track("Drum", 9, 0, volume=74, reverb=60, pan=60)
    B = 4
    A1 = ["D", "C", "D", "G", "D", "C", "G", "D"]
    A2 = ["G", "D", "C", "D", "G", "C", "Am", "D"]
    BB = ["Em", "C", "G", "D", "Em", "Am", "C", "D"]
    m1 = ("d5/0.5 e5/0.5 f#5/1 a5/1 f#5/1 g5/1 e5/1 c5/1 e5/1 f#5/1.5 e5/0.5 d5/1 a4/1 b4/1 c5/1 d5/2 "
          "a5/1 f#5/0.5 g5/0.5 a5/1 d6/1 c6/1 b5/0.5 a5/0.5 g5/1 e5/1 d5/1 g5/1 f#5/1 e5/1 d5/4")
    m2 = ("g5/1 b5/1 a5/1 g5/1 f#5/2 d5/2 e5/1 g5/1 c6/1 a5/1 a5/2 f#5/1 d5/1 "
          "b5/1 a5/0.5 g5/0.5 d5/2 e5/1 c5/1 e5/1 g5/1 e5/1.5 d5/0.5 c5/1 a4/1 d5/4")
    mb = "b4/2 e5/2 g5/1.5 e5/0.5 c5/2 d5/1 b4/1 g4/2 a4/2 f#4/1 a4/1 g4/1 b4/1 e5/2 c5/1 e5/1 a5/2 g5/2 e5/2 f#5/2 d5/2"
    beat = [(0, 41, 6), (1.5, 41, -8), (2, 41, 0), (3, 54, -14), (3.5, 54, -20)]

    def drone_on(t, bars, vel=46):
        drone.note(t, bars * B, 38, vel)
        drone.note(t, bars * B, 45, vel - 6)
        drone2.note(t, bars * B, 50, vel - 8)
        drone2.note(t, bars * B, 57, vel - 10)
        drone2.swell(t, bars * B, 70, 110, 16)

    t = 0
    drone_on(t, 2)
    drums(drum, t + B, 1, B, beat, vel=42)
    t += 2 * B
    for chords, track, mel, vel, drum_vel in ((A1, rec, m1, 80, 46), (A2, rec, m2, 80, 48)):
        drone_on(t, 8)
        drums(drum, t, 8, B, beat, vel=drum_vel)
        track.melody(t, mel, vel=vel)
        t += 8 * B
    # B: no drone, harp chords, oboe
    arpeggio(harp, t, BB, B, [0, 2, 3, 4, 2, 3, 4, 5], 0.5, center=60, vel=50)
    oboe.melody(t, mb, vel=72)
    t += 8 * B
    drone_on(t, 8)
    drums(drum, t, 8, B, beat, vel=44)
    pan.melody(t, m1, vel=74)
    rec.melody(t, m1, vel=56, transpose=-12)
    t += 8 * B
    drone_on(t, 8)
    drums(drum, t, 8, B, beat, vel=48)
    rec.melody(t, m2, vel=80)
    t += 8 * B
    arpeggio(harp, t, BB, B, [0, 2, 3, 4, 2, 3, 4, 5], 0.5, center=60, vel=48)
    pan.melody(t, mb, vel=66, transpose=12)
    oboe.melody(t, mb, vel=58)
    t += 8 * B
    drone_on(t, 10)
    drums(drum, t, 6, B, beat, vel=42)
    s.ritardando(t + 5 * B, 3 * B, 74)
    rec.melody(t, m1, vel=76)
    t += 8 * B
    pan.note(t, 6, 74, 60)
    s.end = t + 8
    return s


# ---------------------------------------------------------------------------------------------------------------
def marktfrieden():
    """C major, 4/4, a little livelier: guitar and pizzicato, violin and flute, light tambourine"""
    s = Song(96, 4, seed=61)
    gtr = s.track("Guitar", 0, GUITAR, volume=92, reverb=58, pan=40)
    vln = s.track("Violin", 1, VIOLIN, volume=84, reverb=74, pan=72)
    flute = s.track("Flute", 2, FLUTE, volume=88, reverb=72, pan=60)
    pizz = s.track("Pizzicato", 3, PIZZ, volume=84, reverb=60, pan=54)
    bsn = s.track("Bassoon", 4, BASSOON, volume=76, reverb=60, pan=48)
    drum = s.track("Tambourine", 9, 0, volume=60, reverb=55, pan=80)
    B = 4
    A1 = ["C", "Am", "F", "G", "C", "Am", "Dm", "G"]
    A2 = ["C", "Am", "F", "G", "C", "F", "G", "C"]
    BB = ["Am", "Em", "F", "C", "Dm", "Am", "Dm", "G"]
    m1 = ("e5/1 g5/1 c6/1 g5/1 a5/1.5 g5/0.5 e5/2 f5/1 a5/1 c6/1 a5/1 g5/1.5 f5/0.5 d5/2 "
          "e5/0.5 f5/0.5 g5/1 e5/1 c5/1 c5/1 e5/1 a5/2 f5/1 e5/1 d5/1 f5/1 d5/2 g4/2")
    m2 = ("e5/1 g5/1 c6/1 e6/1 c6/1.5 b5/0.5 a5/2 a5/1 c6/1 f5/1 a5/1 g5/2 d5/2 "
          "e5/1 c5/1 g5/1 e5/1 f5/1 a5/1 c6/1 a5/1 g5/1 f5/1 d5/1 b4/1 c5/4")
    mb = ("c6/2 b5/1 a5/1 g5/2 e5/2 f5/1 g5/1 a5/1 c6/1 g5/3 e5/1 "
          "f5/1.5 e5/0.5 d5/1 f5/1 e5/2 c5/2 d5/1 f5/1 a5/1 f5/1 g5/2 b4/1 d5/1")
    strum = [0, 2, 3, 4, None, 3, 2, 4]
    tamb = [(1, 54, -6), (3, 54, -2), (3.5, 54, -16)]
    scale_c = scale(0)

    def section(t, chords, mel_track, mel, vel=78, harmony=None, perc=True):
        arpeggio(gtr, t, chords, B, strum, 0.5, center=60, vel=54, legato=1.2)
        bassline(pizz, t, chords, B, [(0, 0.8, "r"), (1, 0.8, "5"), (2, 0.8, "8"), (3, 0.8, "5")], center=43, vel=58)
        bassline(bsn, t, chords, B, [(0, 1.8, "r"), (2, 1.8, "r")], center=36, vel=46)
        if perc:
            drums(drum, t, len(chords), B, tamb, vel=38)
        mel_track.melody(t, mel, vel=vel, legato=0.88)
        if harmony:
            harmony[0].melody(t, third_below(mel, scale_c), vel=harmony[1], legato=0.88)
        return t + len(chords) * B

    t = 0
    arpeggio(gtr, t, ["C", "G"], B, strum, 0.5, center=60, vel=52, legato=1.2)
    t += 2 * B
    t = section(t, A1, vln, m1)
    t = section(t, A2, vln, m2)
    t = section(t, BB, flute, mb, vel=72, perc=False)
    t = section(t, A1, flute, m1, vel=74, harmony=(vln, 52))
    t = section(t, A2, vln, m2, vel=76)
    t = section(t, BB, vln, mb, vel=68, perc=False)
    s.ritardando(t + 4 * B, 4 * B, 80)
    t = section(t, A2, flute, m2, vel=72)
    arpeggio(gtr, t, ["C"], 6, [0, 1, 2, 3, 4], 0.5, center=60, vel=48, legato=8)
    pizz.note(t, 1, 36, 54)
    s.end = t + 6
    return s


PIECES = {
    "menu_morgenrot": morgenrot,
    "game_feldweg": feldweg,
    "game_muehlbach": muehlbach,
    "game_abendweide": abendweide,
    "game_hirtenlied": hirtenlied,
    "game_marktfrieden": marktfrieden,
}

if __name__ == "__main__":
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, fn in PIECES.items():
        song = fn()
        song.write(os.path.join(out, name + ".mid"))
        print(f"{name}: {song.end:.0f} beats")
