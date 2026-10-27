#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G5/F5 - data check of the stock .wrs waypoint files against the path rule of SPEC-741 14.4 as corrected by
# SPEC-AMEND-005 G5.7 (one-waypoint paths, juncture lists ordered by waypoint id). Spawn waypoints are taken from the
# file's dm/team1/team2 sections (the original's writer records the waypoint of each spawn point there; the game
# rebuilds the same table from the map). Same rules as game/src/bots/bot_waypoints.cpp BotWp_BuildPaths.
# Implements: SPEC-741 14.2, 14.4; SPEC-AMEND-005 G5.7, SPEC-AMEND-006 F5.1 (data check)
#
# usage: wrscheck.py <dir with .wrs files> [map ...]     (default dir: openmerc-data/g5/pak2|pak3/bot/wpfiles)

import re
import sys
from pathlib import Path

DATA = Path(r"C:\openmerc-data\g5")


def load(path):
    text = path.read_text(encoding="latin-1")
    lines = text.splitlines()
    spawns = set()
    wps = []
    i = 1
    while i < len(lines):
        ln = lines[i].strip()
        if ln.startswith("begin"):
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("end"):
                parts = lines[i].split()
                if len(parts) >= 2:
                    spawns.add(int(parts[1]))
                i += 1
            i += 1
            continue
        break
    for ln in lines[i:]:
        if ln.strip().startswith("$"):
            break
        m = re.match(r"\s*(\d+)\s+(\d+)\s+\((-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\)\s*(c\d+)?\s*(.*)$", ln)
        if not m:
            continue
        rest = m.group(7).split()
        if not rest:
            continue  # a line that ends before next_d2 is not added
        tag = int(m.group(6)[1:]) if m.group(6) else 0
        links = []
        nums = rest[1:]
        for k in range(0, len(nums) - 1, 2):
            if len(links) < 8:
                links.append((int(nums[k]) & 0x7FFF, float(nums[k + 1])))
        wps.append(dict(kind=int(m.group(2)), tag=tag, links=links))
    return wps, spawns


def build(wps, spawns):
    n = len(wps)
    links = lambda a, b: 1 <= a <= n and 1 <= b <= n and any(t == b for t, _ in wps[a - 1]["links"])
    spawn = lambda i: i in spawns
    path_of = {}
    paths = []
    i = 1
    while i <= n:
        if spawn(i):
            i += 1
            continue
        k = i
        while k + 1 <= n and not spawn(k + 1) and links(k, k + 1):
            k += 1
        if len(paths) < 255:
            for j in range(i, k + 1):
                path_of[j] = len(paths)
            paths.append((i, k))
        i = k + 1
    pj = [[] for _ in paths]

    def insert(p, e):
        v = pj[p]
        at = len(v)
        while at > 0 and v[at - 1][0] > e[0]:
            at -= 1
        v.insert(at, e)

    def add(p, node, q, onode):
        # SPEC-AMEND-006 F5.1: refused when the path holds a record with the same own-side and other-side waypoints
        v = pj[p]
        if any(o[0] == node and o[2] == onode for o in v):
            return
        v[:] = [o for o in v if not (o[0] == node and o[1] < 0)]
        insert(p, (node, q, onode))

    def dead(p, node, onode):
        if any(o[0] == node for o in pj[p]):
            return
        insert(p, (node, -1, onode))

    for p, (s, e) in enumerate(paths):
        for end in (s, e):
            best, bc = None, 0
            for t, c in wps[end - 1]["links"]:
                if t < 1 or t > n or c >= 16384.0:
                    continue
                if (t in (end - 1, end + 1) and links(end, t)) or spawn(t):
                    continue
                if best is None or c < bc:
                    best, bc = t, c
            if best is None:
                dead(p, end, 0)  # no candidate: dead end with other node 0 (black box, F5)
                continue
            q = path_of.get(best, -1)
            if q < 0:
                dead(p, end, best)
            else:
                add(p, end, q, best)
                if q != p:  # a loop juncture is added once (F5.1)
                    add(q, best, p, end)
    return paths, pj, path_of


def verify(pj):
    """SPEC-AMEND-006 F5.1: waypoint ids reported by bot_wp_verifypaths"""
    reported = []
    for v in pj:
        for i, a in enumerate(v):
            for k, b in enumerate(v):
                if i != k and a[0] == b[0] and a[0] not in reported:
                    reported.append(a[0])
    return reported


def main(argv):
    dirs = [Path(argv[0])] if argv and Path(argv[0]).is_dir() else [DATA / "pak3/bot/wpfiles", DATA / "pak2/bot/wpfiles"]
    names = [a for a in argv if not Path(a).is_dir()]
    seen = {}
    for d in dirs:
        for f in sorted(d.glob("*.wrs")):
            seen.setdefault(f.stem.lower(), f)
    total = 0
    for stem, f in sorted(seen.items()):
        if names and stem not in names:
            continue
        wps, spawns = load(f)
        paths, pj, path_of = build(wps, spawns)
        single = [s for s, e in paths if s == e]
        nojunc = sum(1 for v in pj if not v)
        dead = sum(1 for v in pj for o in v if o[1] < 0)
        real = sum(1 for v in pj for o in v if o[1] >= 0)
        inpath = len(path_of)
        nonspawn = sum(1 for i in range(1, len(wps) + 1) if i not in spawns)
        errs = verify(pj)
        total += 1
        print("%-12s %3d wps %2d spawn  %3d paths (%2d single: %s)  juncture entries %d real %d dead-end, %d paths "
              "without; verify errors %s; %s" % (stem, len(wps), len(spawns), len(paths), len(single),
                               ",".join(map(str, single[:12])) + ("..." if len(single) > 12 else ""), real, dead,
                               nojunc, errs or 0, "all non-spawn in a path" if inpath == nonspawn else
                               "%d non-spawn waypoints in no path" % (nonspawn - inpath)))
    print("%d files" % total)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
