#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - static clip census of the stock single-player maps (black box: reads only the game data .bsp files).
# Lists per map the world-hull brushes that block the player but are drawn invisible: CONTENTS_WINDOW brushes
# without CONTENTS_SOLID whose sides use trigger / clip / nodraw-style textures (the tsr1 TV-booth "ghost
# ledge" class), player-clip brushes near breakable glass, and the breakable / glass entities of the map.
#   xclip_scan.py [<maps dir>] [--json out.jsonl] [--near 96] [--fix none|old|new]
# --fix applies the engine's map-load glass-clip fix first (xglassfix.py: old = 0a17ab2, new = fixes-1027), so the
# census shows what the player meets in game; the "strip" column counts the brushes the fix changed.
# Implements: black-box test harness (no game code)
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "formats"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bsp  # noqa: E402

CONTENTS_SOLID, CONTENTS_WINDOW, CONTENTS_PLAYERCLIP = 1, 2, 0x10000
INVIS = re.compile(r"trigger|clip|nodraw|skip|hint|origin", re.I)
# a WINDOW brush drawn with a clip texture is a designer's player clip (intended); trigger / hint / skip / nodraw
# textures are compiler or logic helpers that are not expected to stop the player
SUSP = re.compile(r"trigger|hint|skip|nodraw|origin", re.I)
GLASSY = re.compile(r"glass|window|breakable", re.I)


def world_brushes(b):
    seen, stack = set(), [b.models[0].headnode]
    leaf_seen = set()
    while stack:
        n = stack.pop()
        if n < 0:
            li = -(n + 1)
            if li in leaf_seen:
                continue
            leaf_seen.add(li)
            lf = b.leafs[li]
            for i in range(lf.firstleafbrush, lf.firstleafbrush + lf.numleafbrushes):
                seen.add(b.leafbrushes[i])
            continue
        stack.extend(b.nodes[n].children)
    return seen


def model_brushes(b, mi):
    out, stack, ls = set(), [b.models[mi].headnode], set()
    while stack:
        n = stack.pop()
        if n < 0:
            li = -(n + 1)
            if li in ls:
                continue
            ls.add(li)
            lf = b.leafs[li]
            for i in range(lf.firstleafbrush, lf.firstleafbrush + lf.numleafbrushes):
                out.add(b.leafbrushes[i])
            continue
        stack.extend(b.nodes[n].children)
    return out


def aabb(b, bi):
    br = b.brushes[bi]
    lo, hi = [-1e9] * 3, [1e9] * 3
    for si in range(br.firstside, br.firstside + br.numsides):
        pl = b.planes[b.brushsides[si].planenum]
        nx = pl.normal
        for ax in range(3):
            o = [nx[(ax + 1) % 3], nx[(ax + 2) % 3]]
            if abs(o[0]) < 1e-3 and abs(o[1]) < 1e-3:
                if nx[ax] > 0.999:
                    hi[ax] = min(hi[ax], pl.dist)
                elif nx[ax] < -0.999:
                    lo[ax] = max(lo[ax], -pl.dist)
    return lo, hi


def textures(b, bi):
    br = b.brushes[bi]
    out = set()
    for si in range(br.firstside, br.firstside + br.numsides):
        ti = b.brushsides[si].texinfo
        if 0 <= ti < len(b.texinfo):
            out.add(b.texinfo[ti].texture)
    return out


def near(a, c, d):
    return all(a[0][k] - d <= c[1][k] and c[0][k] - d <= a[1][k] for k in range(3))


def fix_strips(b, rule):
    """brush indices whose blocking contents the engine's glass-clip fix clears under the given rule."""
    if rule == "none":
        return set()
    import xglassfix
    trig, clip, _ = xglassfix.run(b, rule)
    return {x["brush"] for x in trig + clip if x.get("strip", True)}


def scan(path, dist, rule="none"):
    b = bsp.load(str(path))
    stripped = fix_strips(b, rule)
    ents = b.entities()
    world = world_brushes(b)
    sub = set()
    glass_boxes = []
    cls = Counter()
    for e in ents:
        cn = e.get("classname", "")
        m = e.get("model", "")
        if cn.startswith("func_") or GLASSY.search(cn):
            cls[cn] += 1
        if m.startswith("*"):
            mi = int(m[1:])
            if 0 < mi < len(b.models):
                mb = model_brushes(b, mi)
                sub |= mb
                if GLASSY.search(cn) or any(GLASSY.search(t) for bi in mb for t in textures(b, bi)):
                    mo = b.models[mi]
                    glass_boxes.append((cn, m, (list(mo.mins), list(mo.maxs))))
    ghost, pclip_near = [], []
    for bi in world - sub:
        if bi in stripped:
            continue
        c = b.brushes[bi].contents
        tex = textures(b, bi)
        if (c & CONTENTS_WINDOW) and not (c & CONTENTS_SOLID) and tex and all(INVIS.search(t) for t in tex):
            lo, hi = aabb(b, bi)
            ghost.append({"brush": bi, "contents": hex(c), "tex": sorted(tex), "mins": [round(x) for x in lo],
                          "maxs": [round(x) for x in hi],
                          "near_glass": [g[1] for g in glass_boxes if near((lo, hi), g[2], dist)]})
        elif c & CONTENTS_PLAYERCLIP and glass_boxes:
            lo, hi = aabb(b, bi)
            ng = [g[1] for g in glass_boxes if near((lo, hi), g[2], dist)]
            if ng:
                pclip_near.append({"brush": bi, "contents": hex(c), "mins": [round(x) for x in lo],
                                   "maxs": [round(x) for x in hi], "near_glass": ng})
    return {"map": path.stem, "fix": rule, "strip": sorted(stripped), "glass_models": len(glass_boxes), "func_classes": dict(cls.most_common()),
            "ghost_window_brushes": len(ghost), "ghost_near_glass": sum(1 for g in ghost if g["near_glass"]),
            "ghost_suspect": sum(1 for g in ghost if any(SUSP.search(t) for t in g["tex"])),
            "playerclip_near_glass": len(pclip_near), "ghost": ghost[:40], "pclip": pclip_near[:40]}


def main(argv):
    d = Path(argv[1]) if len(argv) > 1 and not argv[1].startswith("--") else \
        Path(r"C:\openmerc-data\extract\pak0\maps")
    out = argv[argv.index("--json") + 1] if "--json" in argv else None
    dist = float(argv[argv.index("--near") + 1]) if "--near" in argv else 96.0
    rule = argv[argv.index("--fix") + 1] if "--fix" in argv else "none"
    rows = []
    for p in sorted(d.glob("*.bsp")):
        try:
            rows.append(scan(p, dist, rule))
        except Exception as ex:  # a broken map is a finding too
            rows.append({"map": p.stem, "error": repr(ex)})
    rows.sort(key=lambda r: (-r.get("ghost_suspect", 0), -r.get("ghost_near_glass", 0), -r.get("ghost_window_brushes", 0), r["map"]))
    print("fix=%s" % rule)
    print("%-8s %6s %6s %6s %6s %6s %6s" % ("map", "glass", "ghost", "suspct", "g~gl", "pc~gl", "strip"))
    for r in rows:
        if "error" in r:
            print("%-8s ERROR %s" % (r["map"], r["error"]))
            continue
        print("%-8s %6d %6d %6d %6d %6d %6d" % (r["map"], r["glass_models"], r["ghost_window_brushes"], r["ghost_suspect"],
                                            r["ghost_near_glass"], r["playerclip_near_glass"], len(r["strip"])))
    if out:
        with open(out, "w", encoding="utf-8") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main(sys.argv)
