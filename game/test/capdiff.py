#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - compares what a client received from two server runs (runtest.py captures): configstrings by
# region, entity baselines, the entities and player state of the last frame, server message counts and prints.
# Implements: (test tooling for SPEC-001/SPEC-203 black-box comparison, no spec of its own)
#
# usage: capdiff.py <capture A (cap.jsonl or its directory)> <capture B> [--all] [--json out.json]
# Prints a summary with difference counts per section; --all lists every difference instead of the first 12.

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402

# configstring regions (SPEC-203; game/src/sof_game.h)
REGIONS = [(0, "general"), (16, "materials"), (31, "maxclients/misc"), (35, "models"), (291, "sounds"),
           (647, "effects"), (903, "images"), (1159, "lights"), (1415, "playerskins"), (1447, "dmicons"),
           (1463, "stringpackages"), (1493, "welcome"), (1497, "ghoulfiles"), (3545, "controlpoints"),
           (3555, "end")]


def region(i):
    name = "?"
    for start, n in REGIONS:
        if i >= start:
            name = n
    return name


def load(p):
    p = Path(p)
    if p.is_dir():
        p = p / "cap.jsonl"
    sess, chans, totals, oob, bad, qports = sofnet.decode_capture(str(p))
    return sess, bad


def text(b):
    return b.decode("latin-1", "replace") if isinstance(b, bytes) else str(b)


def model_name(sess, idx):
    if not idx:
        return "-"
    if idx == 255:
        return "<player>"
    s = sess.cs.get(35 + idx)
    return text(s) if s is not None else "#%d" % idx


def sound_name(sess, idx):
    if not idx:
        return "-"
    s = sess.cs.get(291 + idx)
    return text(s) if s is not None else "#%d" % idx


def effect_name(sess, idx):
    if not idx:
        return "-"
    v = sess.cs.get(647 + idx)
    return text(v) if v is not None else "#%d" % idx


def fx_names(sess, fx):
    # 4 effect slots of (effect index, bolt, scale)
    return tuple((effect_name(sess, fx[3 * i]), fx[3 * i + 1], fx[3 * i + 2]) for i in range(len(fx) // 3)
                 if fx[3 * i] or fx[3 * i + 1] or fx[3 * i + 2])


def ent_key(sess, e):
    o = tuple(round(x) for x in e["origin"])
    return (model_name(sess, e["modelindex"]), o)


def ent_desc(sess, e):
    return ("model %s origin %s angles %s skin %d frame %d effects %#x renderfx %#x solid %#x sound %s fx %s" % (
        model_name(sess, e["modelindex"]), tuple(round(x, 1) for x in e["origin"]),
        tuple(round(x, 1) for x in e["angles"]), e["skinnum"], e["frame"], e["effects"], e["renderfx"],
        e["solid"], sound_name(sess, e["sound"]), e["fx"]))


FIELDS = ["origin", "angles", "modelindex", "modelindex2", "frame", "skinnum", "effects", "renderfx", "solid",
          "sound", "sounddata", "fx"]


def ent_field_diffs(sa, a, sb, b):
    d = []
    for f in FIELDS:
        va, vb = a[f], b[f]
        # for Ghoul-drawn entities (renderfx 0x400000) `frame` carries the engine's Ghoul instance number, which
        # depends only on the order the Ghoul objects were created in
        if f == "frame" and (a["renderfx"] & 0x400000) and (b["renderfx"] & 0x400000):
            continue
        if f == "modelindex":
            va, vb = model_name(sa, va), model_name(sb, vb)
        elif f == "sound":
            va, vb = sound_name(sa, va), sound_name(sb, vb)
        elif f == "fx":
            va, vb = fx_names(sa, va), fx_names(sb, vb)
        elif f in ("origin", "angles"):
            va, vb = tuple(round(x, 1) for x in va), tuple(round(x, 1) for x in vb)
        if va != vb:
            d.append("%s %s / %s" % (f, va, vb))
    return d


def match_ents(sa, ea, sb, eb):
    """Pairs (na, nb) of entities with equal model and rounded origin, then by equal model and number order;
    leftovers are returned as unmatched."""
    pairs = []
    left_a = dict(ea)
    left_b = dict(eb)
    for na in sorted(list(left_a)):
        ka = ent_key(sa, left_a[na])
        for nb in sorted(left_b):
            if ent_key(sb, left_b[nb]) == ka:
                pairs.append((na, nb))
                del left_a[na], left_b[nb]
                break
    for na in sorted(list(left_a)):
        ma = model_name(sa, left_a[na]["modelindex"])
        for nb in sorted(left_b):
            if model_name(sb, left_b[nb]["modelindex"]) == ma:
                pairs.append((na, nb))
                del left_a[na], left_b[nb]
                break
    return pairs, left_a, left_b


def compare_ents(rep, title, sa, ea, sb, eb):
    rep.section(title)
    pairs, la, lb = match_ents(sa, ea, sb, eb)
    shifts = Counter(nb - na for na, nb in pairs)
    if shifts:
        rep.cur[0] += "; number shift B-A %s" % dict(shifts.most_common(3))
    for na, nb in pairs:
        d = ent_field_diffs(sa, ea[na], sb, eb[nb])
        if d:
            rep.diff("ent %d/%d: %s" % (na, nb, "; ".join(d)))
    for n, e in sorted(la.items()):
        rep.diff("ent %d only in A: %s" % (n, ent_desc(sa, e)))
    for n, e in sorted(lb.items()):
        rep.diff("ent %d only in B: %s" % (n, ent_desc(sb, e)))


class Report:
    def __init__(self, show_all):
        self.show_all = show_all
        self.sections = []
        self.cur = None

    def section(self, name):
        self.cur = [name, 0, []]
        self.sections.append(self.cur)

    def diff(self, line):
        self.cur[1] += 1
        self.cur[2].append(line)

    def print(self):
        for name, n, lines in self.sections:
            print("== %s: %d difference%s" % (name, n, "" if n == 1 else "s"))
            for l in lines if self.show_all else lines[:12]:
                print("   " + l)
            if not self.show_all and len(lines) > 12:
                print("   ... %d more" % (len(lines) - 12))

    def summary(self):
        return {name: n for name, n, _ in self.sections}


def compare(sa, sb, rep):
    # configstrings: by index, and per region as a multiset (precache order may differ)
    rep.section("configstrings by index")
    for i in sorted(set(sa.cs) | set(sb.cs)):
        a, b = sa.cs.get(i), sb.cs.get(i)
        if a != b:
            rep.diff("%4d %-14s %r / %r" % (i, region(i), None if a is None else text(a)[:70],
                                          None if b is None else text(b)[:70]))
    rep.section("configstrings by region (as sets)")
    by = {}
    for side, s in (("a", sa), ("b", sb)):
        for i, v in s.cs.items():
            by.setdefault(region(i), {"a": Counter(), "b": Counter()})[side][text(v)] += 1
    for r, d in by.items():
        only_a = d["a"] - d["b"]
        only_b = d["b"] - d["a"]
        for v in sorted(only_a):
            rep.diff("%-14s only in A: %r" % (r, v[:90]))
        for v in sorted(only_b):
            rep.diff("%-14s only in B: %r" % (r, v[:90]))

    # baselines and last-frame entities: matched by content (model name + rounded origin), because entity
    # numbers shift when one side creates more entities before the map entities (see docs/impl/G18.md)
    compare_ents(rep, "baselines", sa, sa.baselines, sb, sb.baselines)
    fa, fb = max(sa.frames), max(sb.frames)
    ea, psa = sa.frames[fa]
    eb, psb = sb.frames[fb]
    compare_ents(rep, "last frame entities (A frame %d: %d, B frame %d: %d)" % (fa, len(ea), fb, len(eb)),
                 sa, ea, sb, eb)

    # player state of the last frame
    rep.section("last frame player state")
    skip = {"_flags", "_present", "_smask"}
    for k in psa:
        if k in skip:
            continue
        if k == "vweap" and psa[k][1:] == psb.get(k, ())[1:]:
            continue  # first value = Ghoul instance number of the view weapon (creation order)
        if psa[k] != psb.get(k):
            rep.diff("%s %s / %s" % (k, psa[k], psb.get(k)))

    # server message kinds
    rep.section("server message counts (kinds present in only one run, or 0 vs n)")
    for k in sorted(set(sa.svc_count) | set(sb.svc_count)):
        a, b = sa.svc_count.get(k, 0), sb.svc_count.get(k, 0)
        if (a == 0) != (b == 0):
            rep.diff("%s %d / %d" % (k, a, b))
    rep.section("temp entity kinds")
    for k in sorted(set(sa.te_count) | set(sb.te_count)):
        if sa.te_count.get(k, 0) != sb.te_count.get(k, 0):
            rep.diff("te %s %d / %d" % (k, sa.te_count.get(k, 0), sb.te_count.get(k, 0)))

    # ghoul strings / objects
    rep.section("ghoul strings")
    ga = Counter(text(s) for s in sa.ghoul.strings.values()) if isinstance(sa.ghoul.strings, dict) else Counter()
    gb = Counter(text(s) for s in sb.ghoul.strings.values()) if isinstance(sb.ghoul.strings, dict) else Counter()
    for v in sorted(ga - gb):
        rep.diff("only in A: %r" % v)
    for v in sorted(gb - ga):
        rep.diff("only in B: %r" % v)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--json")
    a = ap.parse_args(argv)
    sa, bada = load(a.a)
    sb, badb = load(a.b)
    print("A %s: %d configstrings, %d baselines, %d frames, %d decode problems" % (
        a.a, len(sa.cs), len(sa.baselines), len(sa.frames_seen), len(bada)))
    print("B %s: %d configstrings, %d baselines, %d frames, %d decode problems" % (
        a.b, len(sb.cs), len(sb.baselines), len(sb.frames_seen), len(badb)))
    rep = Report(a.all)
    compare(sa, sb, rep)
    rep.print()
    if a.json:
        Path(a.json).write_text(json.dumps(rep.summary(), indent=1))


if __name__ == "__main__":
    main(sys.argv[1:])
