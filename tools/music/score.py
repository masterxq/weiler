# Copyright (C) 2026 Weiler contributors
#
# SPDX-License-Identifier: GPL-2.0-or-later
"""Small notation and MIDI toolkit for the Weiler music (no external modules).

Times are in quarter beats. Melodies are written as text: "d5/1 e5/0.5 r/0.5 c4+e4+g4/2", pitch names with
octave (c4 = middle C = 60), '#'/'b' accidentals, 'r' = rest, '+' joins chord tones, '/' duration in beats.
"""

import random
import struct

TPQ = 480
NOTE = {"c": 0, "d": 2, "e": 4, "f": 5, "g": 7, "a": 9, "b": 11}


def pitch(name):
    """'f#4' -> 66"""
    pc = NOTE[name[0]]
    i = 1
    while i < len(name) and name[i] in "#b":
        pc += 1 if name[i] == "#" else -1
        i += 1
    return pc + 12 * (int(name[i:]) + 1)


def parse(text):
    """Melody text -> list of (pitches or None, duration); chords as 'c4+e4+g4/2'"""
    out = []
    for tok in text.split():
        notes, dur = tok.rsplit("/", 1)
        dur = float(dur)
        out.append((None if notes == "r" else [pitch(n) for n in notes.split("+")], dur))
    return out


def chord_tones(sym):
    """Chord symbol -> (root pitch class, list of pitch classes, bass pitch class). 'Dm', 'G/F', 'Asus4', 'Cmaj7'"""
    bass = None
    if "/" in sym:
        sym, b = sym.split("/")
        bass = NOTE[b[0].lower()] + (1 if "#" in b else -1 if b[1:2] == "b" else 0)
    root = NOTE[sym[0].lower()]
    rest = sym[1:]
    if rest[:1] in ("#", "b"):
        root += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    root %= 12
    if rest.startswith("m") and not rest.startswith("maj"):
        ivs = [0, 3, 7]
        rest = rest[1:]
    elif rest.startswith("dim"):
        ivs = [0, 3, 6]
        rest = rest[3:]
    else:
        ivs = [0, 4, 7]
    if rest == "sus4":
        ivs = [0, 5, 7]
    elif rest == "sus2":
        ivs = [0, 2, 7]
    elif rest == "7":
        ivs = ivs + [10]
    elif rest == "maj7":
        ivs = ivs + [11]
    elif rest == "add9":
        ivs = ivs + [14]
    pcs = [(root + i) % 12 for i in ivs]
    return root, pcs, root if bass is None else bass % 12


def nearest(pc, around):
    """Pitch with pitch class pc nearest to the pitch 'around'"""
    p = around - ((around - pc) % 12)
    return p + 12 if around - p > 6 else p


def voicing(sym, center, n=3):
    """Close voicing of n chord tones around center"""
    _, pcs, _ = chord_tones(sym)
    ps = sorted({nearest(pc, center) for pc in pcs})
    while len(ps) < n:
        ps.append(ps[len(ps) - len(pcs)] + 12 if len(ps) >= len(pcs) else ps[-1] + 12)
    return sorted(ps)[:n]


class Track:
    def __init__(self, song, name, channel, program, volume=100, reverb=70, chorus=0, pan=64):
        self.song, self.name, self.channel, self.program = song, name, channel, program
        self.events = []  # (tick, order, bytes)
        self.cc(0, 7, volume)
        self.cc(0, 10, pan)
        self.cc(0, 91, reverb)
        self.cc(0, 93, chorus)
        self.cc(0, 11, 127)
        if channel != 9:
            self.events.append((0, 1, bytes([0xC0 | channel, program])))

    def tick(self, beat):
        return max(0, int(round(beat * TPQ)))

    def cc(self, beat, num, val):
        self.events.append((self.tick(beat), 0, bytes([0xB0 | self.channel, num, max(0, min(127, int(val)))])))

    def note(self, beat, dur, p, vel):
        rnd = self.song.rnd
        beat += rnd.uniform(-0.012, 0.012) if beat > 0 else 0
        vel = max(1, min(127, int(vel + rnd.uniform(-5, 5))))
        on, off = self.tick(beat), self.tick(beat + dur)
        if off <= on:
            off = on + 1
        self.events.append((on, 2, bytes([0x90 | self.channel, p, vel])))
        self.events.append((off, 0, bytes([0x80 | self.channel, p, 0])))

    def melody(self, start, text, vel=80, legato=0.92, transpose=0, accent=True):
        """Play a melody text; returns the end beat. Long notes slightly softer at their start, phrase peaks louder."""
        t = start
        for ps, dur in parse(text):
            if ps:
                v = vel
                if accent and abs((t - start) % 4) < 0.01:
                    v += 4
                for p in ps:
                    self.note(t, dur * legato, p + transpose, v)
            t += dur
        return t

    def swell(self, start, dur, lo=70, hi=110, steps=16):
        """Expression swell up and down over dur beats"""
        for i in range(steps + 1):
            x = i / steps
            v = lo + (hi - lo) * (1 - abs(2 * x - 1))
            self.cc(start + dur * x, 11, v)


class Song:
    def __init__(self, bpm, beats_per_bar, seed=1):
        self.bpm = bpm
        self.bar = beats_per_bar
        self.tracks = []
        self.tempos = [(0, bpm)]
        self.rnd = random.Random(seed)
        self.end = 0

    def track(self, *a, **k):
        t = Track(self, *a, **k)
        self.tracks.append(t)
        return t

    def tempo(self, beat, bpm):
        self.tempos.append((beat, bpm))

    def ritardando(self, start, dur, to_bpm, steps=8):
        for i in range(1, steps + 1):
            self.tempo(start + dur * i / steps, self.bpm + (to_bpm - self.bpm) * i / steps)

    def write(self, path):
        def vlq(n):
            b = [n & 0x7F]
            n >>= 7
            while n:
                b.append((n & 0x7F) | 0x80)
                n >>= 7
            return bytes(reversed(b))

        def chunk(events):
            data = bytearray()
            last = 0
            for tick, _, msg in sorted(events, key=lambda e: (e[0], e[1])):
                data += vlq(tick - last) + msg
                last = tick
            data += b"\x00\xff\x2f\x00"
            return b"MTrk" + struct.pack(">I", len(data)) + bytes(data)

        meta = []
        for beat, bpm in self.tempos:
            us = int(60_000_000 / bpm)
            meta.append((int(beat * TPQ), 0, b"\xff\x51\x03" + us.to_bytes(3, "big")))
        num = self.bar if self.bar == int(self.bar) else int(self.bar * 2)
        den = 2 if self.bar == int(self.bar) else 3
        meta.append((0, 0, bytes([0xFF, 0x58, 4, num, den, 24, 8])))
        # Silence at the end so that the reverb can ring out
        meta.append((int((self.end + 4) * TPQ), 0, b"\xff\x01\x03end"))
        with open(path, "wb") as f:
            f.write(b"MThd" + struct.pack(">IHHH", 6, 1, len(self.tracks) + 1, TPQ))
            f.write(chunk(meta))
            for t in self.tracks:
                f.write(chunk(t.events))


# ---- Accompaniment patterns ------------------------------------------------------------------------------------

def arpeggio(track, start, chords, bar, pattern, step, center=55, vel=60, legato=1.3):
    """Broken chords: pattern = indices into an extended voicing (0 = bass), one chord per bar"""
    t = start
    for sym in chords:
        _, pcs, bass = chord_tones(sym)
        low = nearest(bass, center - 12)
        tones = [low] + voicing(sym, center, 3) + [p + 12 for p in voicing(sym, center, 3)]
        n = int(round(bar / step))
        for i in range(n):
            idx = pattern[i % len(pattern)]
            if idx is None:
                continue
            v = vel + (8 if i == 0 else 0) - (4 if i % 2 else 0)
            track.note(t + i * step, step * legato, tones[idx % len(tones)], v)
        t += bar
    return t


def pads(track, start, chords, bar, center=60, vel=50, n=3, swell=True):
    t = start
    for sym in chords:
        for p in voicing(sym, center, n):
            track.note(t, bar * 0.98, p, vel)
        if swell:
            track.swell(t, bar, 80, 112, 8)
        t += bar
    return t


def bassline(track, start, chords, bar, rhythm, center=40, vel=70, fifths=True, legato=0.9):
    """Bass on chord roots; rhythm = list of (offset, duration, 'r'|'5'|'8')"""
    t = start
    for sym in chords:
        _, _, bass = chord_tones(sym)
        root = nearest(bass, center)
        for off, dur, what in rhythm:
            p = root + (7 if what == "5" and fifths else 12 if what == "8" else 0)
            track.note(t + off, dur * legato, p, vel)
        t += bar
    return t


def third_below(text, scale):
    """Harmony voice: each note a diatonic third below (scale = pitch classes of the key)"""
    out = []
    for tok in text.split():
        notes, dur = tok.rsplit("/", 1)
        if notes == "r" or "+" in notes:
            out.append(tok)
            continue
        p = pitch(notes)
        steps, q = 0, p
        while steps < 2:
            q -= 1
            if q % 12 in scale:
                steps += 1
        name = ["c", "c#", "d", "eb", "e", "f", "f#", "g", "ab", "a", "bb", "b"][q % 12] + str(q // 12 - 1)
        out.append(f"{name}/{dur}")
    return " ".join(out)


def drums(track, start, bars, bar, pattern, vel=50):
    """pattern: list of (offset, note, velocity offset) per bar on channel 10"""
    for b in range(bars):
        for off, n, dv in pattern:
            track.note(start + b * bar + off, 0.2, n, vel + dv)
    return start + bars * bar
