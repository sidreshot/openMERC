#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - black-box check of the material sound precache: for each captured map, compares the client
# sound-list entries (SPEC-401) that the original registered as sound configstrings with the entries predicted
# from the surface materials of the map's texinfo (SPEC-401 surface table: footsteps, impact, landing), and
# prints the material-flag string next to the entity materials.
# Implements: (test tooling for SPEC-760 B1.3/B1.5, no spec of its own)
#
# usage: matsounds.py <suite tag> [--maps m1,m2]

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
sys.path.insert(0, str(HERE.parents[1] / "tools" / "formats"))
import capdiff  # noqa: E402
import vfs  # noqa: E402
import bsp  # noqa: E402

DATA = Path(r"C:\openmerc-data\g18")
FOOT = {"metal": 2, "snow": 6, "water": 10, "grass": 14, "gravel": 18, "sand": 22, "wood": 26, "stone": 30}
IMPACT = {"stone": [37], "water": [38], "wood": [39], "grass": [40], "gravel": [41], "sand": [42], "snow": [43],
          "metal": [79, 80, 81]}
LAND = {"grass": 71, "gravel": 72, "metal": 73, "sand": 74, "snow": 75, "stone": 76, "water": 77, "wood": 78}


def mat_class(m):
    """(footsteps, impact, landing) group names of a surface material id (SPEC-401 table)."""
    if m == 0 or 17 <= m <= 24 or m in (33, 34):
        return "stone", "stone", "stone"
    if m == 1 or 28 <= m <= 32 or m in (37, 38, 39, 41, 42):
        return "metal", "metal", "metal"
    if 2 <= m <= 5:
        return "sand", "sand", "sand"
    if 6 <= m <= 8:
        return "gravel", "gravel", "gravel"
    if m == 9:
        return "snow", "snow", "snow"
    if 10 <= m <= 13 or m in (27, 36):
        return "water", "water", "water"
    if 14 <= m <= 16 or m == 40:
        return "wood", "wood", "wood"
    if m in (25, 26):
        return "grass", "grass", "grass"
    if m == 35:
        return None, "stone", "stone"
    return None, None, None


def predicted(mats):
    out = set()
    for m in mats:
        f, i, l = mat_class(m)
        if f:
            out.update(range(FOOT[f], FOOT[f] + 4))
        if i:
            out.update(IMPACT[i])
        if l:
            out.add(LAND[l])
    return out


def soundlist():
    src = (HERE.parent / "src" / "props" / "prop_core.cpp").read_text()
    body = src[src.index("s_soundlist[114]"):]
    body = body[body.index("{") + 1:body.index("};")]
    items = re.findall(r'"([^"]*)"|NULL', body)
    names = []
    for m in re.finditer(r'"([^"]*)"|NULL', body):
        names.append(m.group(1))
    return [n.lower() if n else None for n in names]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("--maps")
    a = ap.parse_args(argv)
    sl = soundlist()
    idx = {n: i for i, n in enumerate(sl) if n}
    v = vfs.PakVFS(base=r"D:\Games\SoF-oracle\base")
    dirs = sorted(DATA.glob("%s_orig_*" % a.tag))
    for d in dirs:
        mp = d.name[len(a.tag) + 6:].replace("dm_", "dm/", 1)
        if a.maps and mp not in a.maps.split(","):
            continue
        try:
            s, _ = capdiff.load(d)
        except Exception as ex:  # noqa: BLE001
            print(mp, "no capture", ex)
            continue
        snd = {capdiff.text(s.cs[i]).lower() for i in s.cs if 291 <= i < 647}
        got = {idx[n] for n in snd if n in idx}
        b = bsp.BSP(v.read("maps/%s.bsp" % mp))
        mats = Counter((t.flags >> 24) & 0xFF for t in b.texinfo)
        pred = predicted(mats)
        foot_land_imp = set(range(2, 34)) | set(range(37, 44)) | set(range(71, 82))
        g = got & foot_land_imp
        ents = re.findall(r"\{([^}]*)\}", b.entities_text)
        emat = Counter()
        for e in ents:
            kv = dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', e))
            if "material" in kv:
                emat[(kv.get("classname"), kv["material"])] += 1
        flags = capdiff.text(s.cs.get(16, b"?"))
        print("%-16s flags %s  missing %s  extra %s  other %s" % (
            mp, flags, sorted(pred - g - {77}), sorted(g - pred), sorted(got - foot_land_imp)))
        print("%16s mats %s ents %s" % ("", dict(sorted(mats.items())), dict(emat)))


if __name__ == "__main__":
    main(sys.argv[1:])
