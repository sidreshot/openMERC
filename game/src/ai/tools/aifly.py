#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G2 - flythrough route of a level for aiclient.py: the character spawn points of the map's entity lump
# (tools/formats/bsp.py on the extracted maps), visited in greedy nearest-neighbour order from the player start,
# each as a waypoint 48 units above the character; dormant characters (spawnflag 16, never woken by proximity) and
# the player/Hawk doubles of cinematics are left out unless --all. Prints the --route string (and with --list the
# characters in visiting order).
# Implements: SPEC-700 (black-box verification harness)
#
# usage: aifly.py <map> [--r 160] [--dwell 4] [--all] [--list] [--limit N]

import argparse
import math
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "tools" / "formats"))
import bsp  # noqa: E402

MAPS = Path(r"C:\openmerc-data\extract\pak0\maps")
DOUBLES = ("m_x_mcharacter", "m_x_mmerc")


def vec(s):
    return tuple(float(v) for v in s.split())


def characters(mapname, everything):
    b = bsp.load(str(MAPS / (mapname + ".bsp")))
    start = None
    chars = []
    for e in b.entities():
        c = e.get("classname", "")
        if c == "info_player_start" and start is None:
            start = vec(e["origin"])
        if not c.startswith("m_") or "origin" not in e:
            continue
        flags = int(e.get("spawnflags", "0"))
        if not everything and (flags & 16 or c in DOUBLES):
            continue
        chars.append((c, vec(e["origin"]), flags, e.get("targetname", "")))
    return start, chars


def tour(start, chars):
    left = list(chars)
    pos = start
    order = []
    while left:
        nxt = min(left, key=lambda ch: math.dist(pos, ch[1]))
        left.remove(nxt)
        order.append(nxt)
        pos = nxt[1]
    return order


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("map")
    ap.add_argument("--r", type=float, default=160)
    ap.add_argument("--dwell", type=float, default=4)
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--limit", type=int, default=0)
    a = ap.parse_args(argv)
    start, chars = characters(a.map, a.all)
    order = tour(start, chars)
    if a.limit:
        order = order[:a.limit]
    if a.list:
        pos = start
        total = 0.0
        for i, (c, o, flags, tn) in enumerate(order):
            total += math.dist(pos, o)
            pos = o
            print("%3d %-20s %-18s flags %-7d at %6.0f %6.0f %6.0f  path %6.0f" % (i + 1, c, tn, flags, o[0], o[1],
                                                                                     o[2], total))
    print(";".join("%g %g %g %g %g" % (o[0], o[1], o[2] + 48, a.r, a.dwell) for _, o, _, _ in order))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
