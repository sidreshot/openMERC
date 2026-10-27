#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-400 / SPEC-412 black-box comparison (no game code)
#
# Summarises effect scenes handed to the renderer, per sprite: the original client's renderer traces (capture
# shim ref_omcap, `echo OMTRACE <name>`, every quad of every chain) and the OpenMerc client's scene dumps
# (cvar om_fxdump, same line format). For each sprite: frames it appears in, chains per frame, quads per chain,
# geometry (billboard: the two half-dimensions; beam: half width and segment length), colour, alpha, flags and
# the spread of the quad origins along x, y, z. Optionally only frames whose client time lies in a window.
#
#   python scenestats.py <trace or dump dir> [--sprite sub] [--tmin t --tmax t]
import argparse
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path

RF = re.compile(r"RenderFrame .*?time ([-\d.e]+)")
CHAIN = re.compile(r"^  chain (\d+) count (\d+) weight (\S+) first (\S+) flags ([0-9a-f]+)")
Q = re.compile(r"^    q (\d+) (\S+) flags ([0-9a-f]+) org\(([-\d.e]+) ([-\d.e]+) ([-\d.e]+)\) "
               r"v\(([-\d.e]+) ([-\d.e]+) ([-\d.e]+)\) f18 ([-\d.e]+) f1c ([-\d.e]+) rgba\((\d+) (\d+) (\d+) (\d+)\)")
AQ = re.compile(r"^  quad (\d+) (\S+) flags ([0-9a-f]+) org\(([-\d.e]+) ([-\d.e]+) ([-\d.e]+)\) "
                r"v\(([-\d.e]+) ([-\d.e]+) ([-\d.e]+)\) f18 ([-\d.e]+) f1c ([-\d.e]+) rgba\((\d+) (\d+) (\d+) (\d+)\)")


def summary(vals):
    if not vals:
        return "-"
    vals = sorted(vals)
    return "%.2f [%.2f %.2f %.2f] %.2f" % (vals[0], vals[len(vals) // 4], vals[len(vals) // 2],
                                           vals[3 * len(vals) // 4], vals[-1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dir")
    ap.add_argument("--sprite", default="")
    ap.add_argument("--tmin", type=float, default=-1e9)
    ap.add_argument("--tmax", type=float, default=1e9)
    a = ap.parse_args()
    frames = 0
    per = defaultdict(lambda: defaultdict(list))
    for f in sorted(Path(a.dir).glob("*.txt")):
        txt = open(f, encoding="latin-1").read().splitlines()
        t = None
        for line in txt:
            m = RF.search(line)
            if m:
                t = float(m.group(1))
                break
        if t is None or not (a.tmin <= t <= a.tmax):
            continue
        frames += 1
        seen = defaultdict(int)
        chain_sprite = None
        for line in txt:
            m = CHAIN.match(line)
            if m:
                chain_sprite = m.group(4)
                if a.sprite in chain_sprite:
                    per[chain_sprite]["quads per chain"].append(int(m.group(2)))
                    seen[chain_sprite] += 1
                continue
            m = Q.match(line) or AQ.match(line)
            if not m:
                continue
            spr = m.group(2)
            if a.sprite not in spr:
                continue
            kind = "chain" if line.startswith("    q ") else "alone"
            if kind == "alone":
                seen[spr + " (alone)"] += 0
            d = per[spr if kind == "chain" else spr + " (alone)"]
            flags = int(m.group(3), 16)
            o = [float(m.group(i)) for i in (4, 5, 6)]
            v = [float(m.group(i)) for i in (7, 8, 9)]
            f18 = float(m.group(10))
            rgba = [int(m.group(i)) for i in (12, 13, 14, 15)]
            d["flags"].append(flags)
            d["x"].append(o[0])
            d["y"].append(o[1])
            d["z"].append(o[2])
            if flags & 0x2:
                d["dim1 (height)"].append(v[0])
                d["dim2 (width)"].append(v[1])
            elif flags & 0x4:
                d["beam half width"].append(f18)
                d["beam length"].append(math.dist(o, v))
            for c, n in zip(rgba, "rgba"):
                d[n].append(c)
        for s, n in seen.items():
            per[s]["chains per frame"].append(n)
    print("%d frame(s) from %s" % (frames, a.dir))
    for spr in sorted(per):
        d = per[spr]
        nq = len(d["r"])
        print("%s: %d quad(s)" % (spr, nq))
        fl = defaultdict(int)
        for x in d["flags"]:
            fl[x] += 1
        print("  flags " + ", ".join("%04x x%d" % (k, v) for k, v in sorted(fl.items())))
        for key in ("chains per frame", "quads per chain", "dim1 (height)", "dim2 (width)", "beam half width",
                    "beam length", "r", "g", "b", "a", "x", "y", "z"):
            if d[key]:
                print("  %-16s %s" % (key, summary(d[key])))


if __name__ == "__main__":
    main()
