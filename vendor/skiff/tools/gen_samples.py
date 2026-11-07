#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the SAMPLER engine's sample sets from WAV files into a C header.

Samples are stored as IMA ADPCM (4 bit) at their native rate; loop points
and roots come from the WAV 'smpl' chunk and the file name (C3 = MIDI 60
naming), single-cycle waves get their root from rate / loop length.
The ADPCM state at the loop start is stored so loops restart exactly.

Libraries:
  cc0        assets/samples-cc0/ (tools/fetch_cc0.py, Versilian Studios, CC0):
             PIANO, FLUTE, SAX (set 1, once TRANH, and set 4, once PERC, are aliases of PIANO)
SAMPLE's factory presets end before set 4 (SMP_PERC_SLOT); every sample set and SET / USR
index stays in its original place.
A retired set keeps its index as an alias: the original's name and zones (no data), and a
preset equal to the original's (SMP_SET_ORIG), so old projects and presets play it and
browsing skips it (params.c enum_orig, ui.c preset_orig).
Set 4 was PERC, the GM-mapped drum kit (Felucca's generated drums and CC0 hand percussion,
~66 KB of flash): retired, the DRUM engine plays the same GM map. A sound that selected it
loads as DRUM with its default kit (core.h drum_from_perc); what that cannot reach (GRAIN's
SRC, a value sent live) plays the alias, PIANO.

The SLICE engine's built-in loops (eng_slice.c) are encoded here from assets/breaks/ (Skiff phase 9:
made with the YuE2 music model, see ATTRIBUTION.txt there; before, BREAK was arranged from
Felucca's generated drums): BREAK, one bar at 120 BPM, and FUNK, two beats at 86.33 BPM. They
are stored after every set (their offsets do not move) and are NOT SAMPLE sets (the SET list,
its presets and the USR1-3 numbers stay as they were). Each one's slice table (decoder states
on a 128-point grid, AUTO slices where the firmware's own detector finds the hits) is written
with it: SLC_BREAK_INIT, SLC_FUNK_INIT.

SLICE's SRC PIANO (added in 1.0.4 after BREAK and USR1-3) is the PIANO set's middle C zone itself:
no data of its own, only its slice table (one AUTO slice, the note's attack): SLC_PIANO_INIT.
Without the CC0 library (no PIANO set) it has no material (SLICE plays a sine there).

The header is cached (build/gen_samples.cache) under a hash of every
input file, this script, sampleio.py and the Python version, so unchanged
inputs skip the slow pitch detection.
"""
import hashlib
import math
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
BREAKS = SRC / "assets" / "breaks"           # (Skiff phase 9) SLICE's BREAK and FUNK
TR = 22050                                   # stored sample rate

# CC0 library: set -> kind ("oneshot" decaying, "sus" looped sustain, "kit" one sample per key)
# Built-in sets (piano, flute, sax); the other slots of the 8 are for the user (USR1-3, loaded
# from the web editor). TRANH was removed (66 KB of flash); its index 1 stays, an alias of PIANO
# ("alias": the set named). PERC (the GM kit) was removed (66 KB); its index 4 stays, an alias of
# PIANO too (PERC_SLOT; its sounds load as the DRUM engine)
CC0_SETS = [("PIANO", "oneshot"), ("PIANO", "alias"), ("FLUTE", "sus"), ("SAX", "sus"), ("PIANO", "alias")]
PERC_SLOT = 4                                # core.h SMP_SET_PERC: SAMPLE's factory presets end before it
MEASURED_TUNING = ()                         # sets whose recordings are not at A440 (was TRANH, ~+35 ct)

KIT_BASE = 53                     # F3, the lowest FM-1 key

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

BREAK_BPM, BREAK_STEPS = 120, 16          # SLICE's BREAK: one bar of 16ths (assets/breaks/break.wav)
SLC_PIANO_NOTE = 60                          # SLICE's SRC PIANO: the PIANO zone of this root (middle C)
SLC_GRID, SLC_AUTO, SLC_HOP = 128, 32, 32   # eng_slice.c slc_src_t, its AUTO detector's hop

ENV = {"wave":(5, 80, 100, 50), "kit": (0, 127, 127, 60), "multi": (0, 85, 0, 75),
       "oneshot": (0, 127, 127, 70), "sus": (12, 80, 120, 60)}
SET_TAG = {"PIANO": "PT_KEYS", "FLUTE": "PT_LEAD", "SAX": "PT_LEAD"}   # (Skiff phase 9) core.h PT_*: PRESETS' LIST
SET_VARIANTS = [   # (Skiff phase 9) SAMPLE's presets after PERC's slot: (name, e, env, mono, fx, tag[, more]), apply9.py
    ("LOFI PIANO", [0, 0, 110, 1, 30, 0, 30, 0], [0, 127, 127, 70], 0, [40, 10, 20, 40], "PT_KEYS", [("P_DTYPE", 3), ("P_LRATE", 40), ("P_LD_PIT", 8)]),
    ("MUSIC BOX", [0, 24, 0, 0, 110, 0, 0, 0], [0, 90, 0, 80], 0, [0, 30, 40, 60], "PT_PLUCK"),
    ("FLUTE PAD", [2, 0, 0, 1, 90, 0, 0, 0], [80, 90, 120, 100], 0, [0, 60, 25, 90], "PT_PAD"),
    ("DARK SAX", [3, -12, 0, 1, 26, 0, 48, 0], [22, 80, 120, 55], 1, [0, 15, 20, 60], "PT_LEAD", [("P_LD_PIT", 8), ("P_LRATE", 84), ("P_LFADE", 100)]),
    ("KOTO", [0, 12, 0, 0, 120, 0, 22, 0], [0, 72, 0, 50], 0, [0, 0, 25, 30], "PT_PLUCK", [("P_ED_PIT", 2)]),
]

# PIANO's notes cut to 0.75 s (saves flash), faded out over the last SET_FADE s; (Skiff 1.3) FLUTE's and SAX's to 0.68 s:
# their sustain loops (from 0.40 s) half as long, 0.28 s (18 KB of flash for 1.3's features)
SET_KEEP = {"PIANO": 0.75, "FLUTE": 0.68, "SAX": 0.68}
SET_FADE = {"PIANO": 0.15}

_wavs = {}


def cos_fade(x, fade):
    """fade the last `fade` samples of x out on a raised cosine (no corner at either end)"""
    n = len(x)
    for i in range(fade):
        x[n - fade + i] *= 0.5 + 0.5 * math.cos(math.pi * (i + 1) / fade)


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def cc0_entries(setname, kind):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((CC0 / setname).glob("*.wav"))
    out = []
    if kind != "kit":
        # octave convention of this set (C4 = 60 or C3 = 60): YIN votes, the names give the notes
        votes = {1: 0, 2: 0}
        for p in files:
            nn = name_note(p.name)
            if not nn:
                continue
            sr, x = wav(p)
            det = hz_to_midi(detect_hz(x[onset(x):], sr))
            for o in (1, 2):
                if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                    votes[o] += 1
        conv = 2 if votes[2] >= votes[1] else 1
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        if kind == "kit":
            root = KIT_BASE + k
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = SET_KEEP.get(setname) or {"oneshot": 1.0, "sus": 0.95, "kit": 0.6}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "sus":                            # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            if setname in SET_FADE:                  # a shortened set: a long raised-cosine fade-out
                cos_fade(x, int(SET_FADE[setname] * TR))
            else:
                fade = int(0.08 * TR)
                for i in range(fade):
                    x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


def ima_steps(data, n):
    """an IMA ADPCM decoder (from predictor 0 / index 0, as eng_slice.c's) over n samples of data: its
    (predictor, index) before each sample, and after the last; a predictor after a sample is that sample"""
    pred, idx, out = 0, 0, [(0, 0)]
    for k in range(n):
        code = (data[k >> 1] >> (4 * (k & 1))) & 15
        step = sio.IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + sio.IMA_IDX[code & 7]))
        out.append((pred, idx))
    return out


def ima_states(data, positions):
    """IMA ADPCM decoder state before sample p for each p, packed as eng_slice.c reads it:
    (predictor & 0xFFFF) | index << 16 (segment 0)"""
    st = ima_steps(data, max(positions, default=0))
    return [(st[p][0] & 0xFFFF) | st[p][1] << 16 for p in positions]


def slc_onsets(x):
    """eng_slice.c slc_scan's AUTO slices of decoded samples x at TR, in the same integer steps (so the table
    written here is what the firmware finds): per hop of SLC_HOP the peak of the first difference and the
    plain peak; an onset is a hop where either rises 1.75x over its follower, above 1/12 (1/8) of its largest
    value, at least 50 ms after the last one; the slice starts one hop before the hop that rose"""
    gap = ((int(round(TR / 44100 * 65536)) >> 4) * 44100 >> 12) // 20
    prev = pkd = pkr = 0
    for v in x:
        pkd, pkr, prev = max(pkd, abs(v - prev)), max(pkr, abs(v)), v
    thd, thr = pkd // 12, pkr // 8
    out, prev, envd, envr, last, h0 = [0], 0, 0, 0, 0, 0
    for hs in range(0, len(x), SLC_HOP):
        hd = hr = 0
        for v in x[hs:hs + SLC_HOP]:
            hd, hr, prev = max(hd, abs(v - prev)), max(hr, abs(v)), v
        if hs and ((hd > thd and hd * 4 > envd * 7) or (hr > thr and hr * 4 > envr * 7)) and h0 >= last + gap \
                and len(out) < SLC_AUTO:
            out.append(h0)
            last = h0
        envd = hd if hd > envd else envd - envd // 16
        envr = hr if hr > envr else envr - envr // 64
        h0 = hs
    return out


class Builder:
    def __init__(self):
        self.zones, self.sets, self.blob, self.kinds = [], [], bytearray(), {}
        self.alias = {}                         # set index -> the set index it aliases
        self.brk = None
        self.fnk = None
        self.pno = None

    def slice_loop(self, name):
        """a built-in SLICE loop (assets/breaks/NAME.wav), after every set; its slice table: decoder states at
        k * len / SLC_GRID, the AUTO slices slc_scan finds (eng_slice.c, which scans a user slot at boot)"""
        sr, x = read_any_wav(BREAKS / f"{name}.wav")
        x = resample(x, sr, TR)
        pk = peak(x)
        x = [int(v * 30000 / pk) for v in x]
        off, _ = self.add(x, 0)
        n = len(x)
        data = self.blob[off:off + (n + 1) // 2]
        grid = ima_states(data, [k * n // SLC_GRID for k in range(SLC_GRID)])
        hits = slc_onsets([p for p, _ in ima_steps(data, n)[1:]])
        return dict(off=off, n=n, grid=grid, apos=hits, ast=ima_states(data, hits))

    def slice_piano(self):
        """SLICE's PIANO: the PIANO set's SLC_PIANO_NOTE zone (its data, no copy); its slice table: decoder states
        at k * len / SLC_GRID, one AUTO slice at 0 (a single note has one attack)"""
        z = next((z for name, z0, nz in self.sets if name == "PIANO"
                  for z in self.zones[z0:z0 + nz] if z["root16"] == SLC_PIANO_NOTE * 16), None)
        if not z:
            return
        off, n = z["off"], z["n"]
        data = self.blob[off:off + (n + 1) // 2]
        grid = ima_states(data, [k * n // SLC_GRID for k in range(SLC_GRID)])
        self.pno = dict(off=off, n=n, grid=grid, apos=[0], ast=ima_states(data, [0]))

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        self.sets.append((name, len(self.zones), len(entries)))
        self.zones += entries
        self.kinds[name] = kind

    def alias_set(self, name):
        """a retired set's index: the zones of the earlier set `name` (no data of its own)"""
        orig = next(i for i, (n, _, _) in enumerate(self.sets) if n == name)
        self.alias[len(self.sets)] = orig
        self.sets.append(self.sets[orig])

    def cc0_set(self, name, kind):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=TR,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=KIT_BASE + k if kind == "kit" else None))
        self.add_set(name, kind, entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        L.append(f"#define SMP_NSETS {max(1, len(sets))}")
        named = sets or [("NONE", 0, 0)]
        perc = PERC_SLOT if len(named) > PERC_SLOT else len(named)
        # (Skiff phase 9) the variations follow PERC's slot, only with the CC0 sets their SET values name (PERC's
        # alias then is a preset among them, hidden as the other alias)
        var = SET_VARIANTS if len(named) == PERC_SLOT + 1 else []
        npre = len(named) + len(var) if var else perc
        L.append(f"#define SMP_NPRESETS {npre}")
        if len(named) > PERC_SLOT:
            L.append(f"#define SMP_PERC_SLOT {PERC_SLOT}   /* once PERC (the GM kit): an alias, its sounds load as DRUM */")
        L.append("/* a preset per set (an alias: its original's, hidden by its name; PERC's past SMP_NPRESETS unless")
        L.append(" * variations follow, ui.c apply_preset_to loads it as DRUM), (Skiff phase 9) then the variations */")
        L.append("static const preset_t SMP_PRESET_TABLE[] = {")
        for i, (name, _, _) in enumerate(named):
            k = self.kinds.get(name, "wave")
            a, d, s_, r = ENV[k]
            loop = 0 if k == "kit" else 1
            si = self.alias.get(i, i)                   # an alias: the original's preset (hidden by its name)
            L.append(f'    {{"{name}", {{{si}, 0, 0, {loop}, 127, 0, 0, 0}}, {{{a}, {d}, {s_}, {r}}}, 0, 0, '
                     f'TAG({SET_TAG.get(name, "PT_NONE")})}},')
        for name, e, env, mono, fx, tag, *more in var:  # (more: [(id, value)], as the engines' MORE)
            mo = ", ".join(f"{{{i}, {v}}}" for i, v in more[0]) if more else ""
            L.append(f'    {{"{name}", {{{", ".join(map(str, e))}}}, {{{", ".join(map(str, env))}}}, 0, {mono}, '
                     f'FX({", ".join(map(str, fx))}), ' + (f"MORE({mo}), " if mo else "") + f'TAG({tag})}},')
        L.append("};")
        orig = [self.alias.get(i, i) for i in range(len(named))]
        L.append("/* SET i plays SMP_SET_ORIG[i]: != i for a retired set kept as an alias (its preset i too) */")
        L.append("static const uint8_t SMP_SET_ORIG[] = {" + ", ".join(map(str, orig)) + "};")
        L.append(f"#define SMP_NALIAS {sum(1 for i, o in enumerate(orig) if o != i and i < npre)}   /* among the presets */")
        names = ", ".join(f'"{n}"' for n, _, _ in named)
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
        L += self.break_header()
        L += self.funk_header()
        L += self.piano_header()
        return "\n".join(L) + "\n"

    @staticmethod
    def slc_init(name, b):
        """an eng_slice.c slc_src_t initializer (len, rate, nseg, nauto, seg[], grid[], apos[], ast[])"""
        rate = int(round(TR / 44100 * 65536))

        def lst(v, k):
            v = list(v) + [0] * (k - len(v))
            return ", \\\n    ".join(", ".join(map(str, v[i:i + 12])) for i in range(0, len(v), 12))
        return [f"#define {name} {{{b['n']}, {rate}, 1, {len(b['apos'])}, {{{{{b['off']}, 0, {b['n']}}}}}, {{ \\",
                "    " + lst(b["grid"], SLC_GRID) + "}, { \\", "    " + lst(b["apos"], SLC_AUTO) + "}, { \\",
                "    " + lst(b["ast"], SLC_AUTO) + "}}"]

    def break_header(self):
        """SLC_BREAK_INIT: SLICE's BREAK ({0}: none)"""
        b = self.brk
        if not b:
            return ["#define SLC_BREAK_INIT {0}"]
        return ["", f"/* SLICE's BREAK: one bar of {BREAK_STEPS} steps at {BREAK_BPM} BPM, {len(b['apos'])} hits */",
                f"#define SLC_BREAK_BPM {BREAK_BPM}", f"#define SLC_BREAK_STEPS {BREAK_STEPS}"] + \
            self.slc_init("SLC_BREAK_INIT", b)

    def funk_header(self):
        """SLC_FUNK_INIT: SLICE's FUNK (Skiff phase 9; {0}: none)"""
        b = self.fnk
        if not b:
            return ["#define SLC_FUNK_INIT {0}"]
        return ["", f"/* SLICE's FUNK: two beats at 86.33 BPM, {len(b['apos'])} hits */"] + self.slc_init("SLC_FUNK_INIT", b)

    def piano_header(self):
        """SLC_PIANO_INIT: SLICE's PIANO, the PIANO zone of middle C ({0}: none, no CC0 library)"""
        b = self.pno
        if not b:
            return ["#define SLC_PIANO_INIT {0}"]
        return ["", f"/* SLICE's PIANO: the PIANO zone of MIDI {SLC_PIANO_NOTE} ({b['n']} samples at SMP_DATA[{b['off']}]) */",
                f"#define SLC_PIANO_NOTE {SLC_PIANO_NOTE}"] + self.slc_init("SLC_PIANO_INIT", b)

    def summary(self):
        brk = f", SLICE BREAK {self.brk['n']} + FUNK {self.fnk['n']} samples" if self.brk else ""
        brk += f", SLICE PIANO (shared) {self.pno['n']} samples" if self.pno else ""
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM{brk}"


def slice_on():
    """the SLICE engine is built (src/core.h: on unless FELUCCA_SLICE=0)"""
    return os.environ.get("FELUCCA_SLICE", "1") != "0"


def input_key(have_cc0):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], have_cc0, slice_on())).encode())   # sum() differs across versions
    files = sorted(BREAKS.glob("*.wav"))
    if have_cc0:
        files += sorted(CC0.glob("*/*.wav"))
    for p in files:
        h.update(str(p.relative_to(SRC)).encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main(out):
    have_cc0 = CC0.exists() and any(CC0.glob("*/*.wav"))
    if not have_cc0:
        print("samples: CC0 library not fetched (tools/fetch_cc0.py) - no SAMPLE sets")
    key = input_key(have_cc0)
    try:
        ck, summary, text = CACHE.read_text().split("\n", 2)
        if ck == key:
            Path(out).write_text(text)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    if have_cc0:
        for name, kind in CC0_SETS:
            if kind == "alias":
                b.alias_set(name)
            else:
                b.cc0_set(name, kind)
    if slice_on():                                  # SLICE's loops: only when that engine is built
        b.brk = b.slice_loop("break")               # last: the sets' offsets stay as they were
        b.fnk = b.slice_loop("funk")
        b.slice_piano()                             # (no data of its own: the PIANO set's)
    text = b.header()
    Path(out).write_text(text)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}")
    print(b.summary())


if __name__ == "__main__":
    main(sys.argv[1])
