#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - offline replay of the engine's map-load glass-clip fix (collision.c CMod_FixTriggerWindowBrushes)
# on the stock .bsp files: lists every brush the fix changes, with its owner (world / entity), textures, true
# vertex bounds and the glass brush it was matched against.  --rule old|new selects the heuristic: old = 0a17ab2
# (axial bounds, +-1e9 on slanted brushes, any glass, 2 u overlap), new = fixes-1027 (true bounds, func_breakable
# panes only, world brushes >= 50% inside the pane, pane with open floor on both sides).
#   xglassfix.py [<maps dir>] [--rule old|new] [--maps tsr1,ger3] [--json out.jsonl] [-v]
# Implements: black-box test harness (no game code)
import itertools
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "formats"))
import bsp  # noqa: E402

SOLID, WINDOW, PCLIP, MCLIP = 1, 2, 0x10000, 0x20000
BIG = 1e9


def tex(b, bi):
    br = b.brushes[bi]
    out = []
    for si in range(br.firstside, br.firstside + br.numsides):
        ti = b.brushsides[si].texinfo
        out.append(b.texinfo[ti].texture if 0 <= ti < len(b.texinfo) else "")
    return out


def has(b, bi, fold):
    return any(fold in t.lower() for t in tex(b, bi))


def axial_aabb(b, bi):
    """The engine's original bounds: only axial planes count, other axes stay +-1e9."""
    br = b.brushes[bi]
    lo, hi = [-BIG] * 3, [BIG] * 3
    for si in range(br.firstside, br.firstside + br.numsides):
        pl = b.planes[b.brushsides[si].planenum]
        n = pl.normal
        for k in range(3):
            k1, k2 = (k + 1) % 3, (k + 2) % 3
            if abs(n[k1]) < 1e-3 and abs(n[k2]) < 1e-3:
                if n[k] > 0.999:
                    hi[k] = min(hi[k], pl.dist)
                elif n[k] < -0.999:
                    lo[k] = max(lo[k], -pl.dist)
    return lo, hi


def true_aabb(b, bi):
    """Bounds of the brush polytope: intersect every plane triple, keep points inside all planes."""
    br = b.brushes[bi]
    pls = [b.planes[b.brushsides[si].planenum] for si in range(br.firstside, br.firstside + br.numsides)]
    pts = []
    for p1, p2, p3 in itertools.combinations(pls, 3):
        a, c, d = p1.normal, p2.normal, p3.normal
        cr = lambda u, v: (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
        dot = lambda u, v: u[0] * v[0] + u[1] * v[1] + u[2] * v[2]
        cd = cr(c, d)
        den = dot(a, cd)
        if abs(den) < 1e-6:
            continue
        da, dc = cr(d, a), cr(a, c)
        x = [(p1.dist * cd[k] + p2.dist * da[k] + p3.dist * dc[k]) / den for k in range(3)]
        if all(dot(pl.normal, x) - pl.dist <= 0.05 for pl in pls):
            pts.append(x)
    if not pts:
        return None
    return [min(p[k] for p in pts) for k in range(3)], [max(p[k] for p in pts) for k in range(3)]


def overlap(a, c, pad):
    return all(a[1][k] + pad >= c[0][k] and c[1][k] + pad >= a[0][k] for k in range(3))


def owners(b):
    """brush index -> owner label ('world' or 'classname *N targetname')."""
    own = {}
    ents = b.entities()
    lab = {0: "world"}
    for e in ents:
        m = e.get("model", "")
        if m.startswith("*"):
            lab[int(m[1:])] = ("%s %s %s" % (e.get("classname", "?"), m, e.get("targetname", ""))).strip()
    for mi, mo in enumerate(b.models):
        stack, ls = [mo.headnode], set()
        while stack:
            n = stack.pop()
            if n < 0:
                li = -(n + 1)
                if li in ls:
                    continue
                ls.add(li)
                lf = b.leafs[li]
                for i in range(lf.firstleafbrush, lf.firstleafbrush + lf.numleafbrushes):
                    own.setdefault(b.leafbrushes[i], lab.get(mi, "model*%d" % mi))
                continue
            stack.extend(b.nodes[n].children)
    return own


BREAKABLE = ("func_breakable",)
INSIDE, SIDE, DROP = 0.5, 24.0, 64.0
MASK = SOLID | PCLIP | WINDOW


def inside_fraction(a, g):
    """part of box a inside box g; 0 unless every axis overlaps by >= 1 unit (CMod_InsideFraction)."""
    vol = ov = 1.0
    for k in range(3):
        lo, hi = max(a[0][k], g[0][k]), min(a[1][k], g[1][k])
        if hi - lo < 1.0:
            return 0.0
        ov *= hi - lo
        vol *= max(a[1][k] - a[0][k], 1.0)
    return ov / vol


def leaf_of(b, p):
    n = b.models[0].headnode
    while n >= 0:
        nd = b.nodes[n]
        pl = b.planes[nd.planenum]
        d = sum(pl.normal[k] * p[k] for k in range(3)) - pl.dist
        n = nd.children[0] if d >= 0 else nd.children[1]
    return b.leafs[-(n + 1)]


def point_blocked(b, p, skip=()):
    """a world brush with contents in MASK contains p (what a zero-size CM_BoxTrace start test sees;
    the leaf contents are coarser and include brushes that only share the leaf).  skip: brushes the fix
    would strip anyway."""
    lf = leaf_of(b, p)
    for i in range(lf.firstleafbrush, lf.firstleafbrush + lf.numleafbrushes):
        bi = b.leafbrushes[i]
        br = b.brushes[bi]
        if not (br.contents & MASK) or bi in skip:
            continue
        if all(sum(b.planes[b.brushsides[si].planenum].normal[k] * p[k] for k in range(3))
               - b.planes[b.brushsides[si].planenum].dist <= 0.0
               for si in range(br.firstside, br.firstside + br.numsides)):
            return bi
    return None


def sides_open(b, g, skip=()):
    """CMod_GlassSidesOpen: 24 u out of both faces of the pane, free space with ground <= 64 u below its bottom."""
    lo, hi = g
    t = 0 if (hi[0] - lo[0]) <= (hi[1] - lo[1]) else 1
    u = 1 - t
    why = []
    for side in (0, 1):
        p = [0.0, 0.0, 0.0]
        p[u] = 0.5 * (lo[u] + hi[u])
        p[t] = hi[t] + SIDE if side else lo[t] - SIDE
        p[2] = lo[2] + 8.0
        bl = point_blocked(b, p, skip)
        if bl is not None:
            return False, "side %d blocked at %s by #%d" % (side, [round(v) for v in p], bl)
        z, hit = p[2], None
        while z >= lo[2] - DROP:
            if point_blocked(b, (p[0], p[1], z), skip) is not None:
                hit = z
                break
            z -= 1.0
        if hit is None:
            return False, "side %d drop >%d at %s" % (side, DROP, [round(v) for v in p])
        why.append("side %d floor %d" % (side, round(hit)))
    return True, ", ".join(why)


def run_new(b):
    """the engine rule since fixes-1027 (collision.c CMod_FixTriggerWindowBrushes)."""
    own = owners(b)
    glass = []
    for bi, br in enumerate(b.brushes):
        o = own.get(bi, "orphan")
        if (br.contents & WINDOW) and o.split(" ")[0].startswith(BREAKABLE) and has(b, bi, "glass"):
            tb = true_aabb(b, bi)
            if tb:
                glass.append([bi, tb, o, None])
    trig, clip = [], []
    if not glass:
        return trig, clip, 0
    for bi, br in enumerate(b.brushes):
        c = br.contents
        if own.get(bi) != "world" or (c & SOLID):
            continue
        if (c & WINDOW) and not has(b, bi, "glass") and has(b, bi, "trigger"):
            kind = "trig"
        elif c & PCLIP:
            kind = "clip"
        else:
            continue
        tb = true_aabb(b, bi)
        if not tb:
            continue
        for g in glass:
            f = inside_fraction(tb, g[1])
            if f < INSIDE:
                continue
            if g[3] is None:
                g[3] = sides_open(b, g[1])
            rec = {"brush": bi, "owner": "world", "contents": hex(c), "tex": sorted(set(tex(b, bi))),
                   "bounds": [[round(v) for v in x] for x in tb], "glass": g[0], "glass_owner": g[2],
                   "glass_bounds": [[round(v) for v in x] for x in g[1]], "inside": round(f, 2),
                   "strip": g[3][0], "why": g[3][1]}
            (trig if kind == "trig" else clip).append(rec)
            break
    return trig, clip, len(glass)


def run(b, rule):
    if rule == "new":
        return run_new(b)
    own = owners(b)
    glass, trig, clip = [], [], []
    for bi, br in enumerate(b.brushes):
        c = br.contents
        if not (c & WINDOW):
            continue
        if has(b, bi, "glass"):
            o = own.get(bi, "orphan")
            if rule == "old":
                glass.append((bi, axial_aabb(b, bi), o))
            else:
                # new: only glass that can go away (a breakable entity), with its true bounds
                if o.split(" ")[0].startswith(BREAKABLE):
                    glass.append((bi, true_aabb(b, bi), o))
            continue
        if c & SOLID or not has(b, bi, "trigger"):
            continue
        o = own.get(bi, "orphan")
        if rule == "new" and o != "world":
            continue  # entity brushes: not part of the world hull; leave them as authored
        trig.append({"brush": bi, "owner": o, "contents": hex(c), "tex": sorted(set(tex(b, bi))),
                     "bounds": [[round(v) for v in x] for x in (true_aabb(b, bi) or ([0] * 3, [0] * 3))]})
    for bi, br in enumerate(b.brushes):
        c = br.contents
        if not (c & PCLIP) or (c & SOLID):
            continue
        box = axial_aabb(b, bi) if rule == "old" else true_aabb(b, bi)
        if box is None:
            continue
        for gi, gbox, go in glass:
            if gbox is None:
                continue
            if overlap(box, gbox, 2.0):
                tb = true_aabb(b, bi)
                clip.append({"brush": bi, "owner": own.get(bi, "orphan"), "contents": hex(c),
                             "tex": sorted(set(tex(b, bi))),
                             "bounds": [[round(v) for v in x] for x in tb] if tb else None,
                             "glass": gi, "glass_owner": go,
                             "glass_bounds": [[round(v) for v in x] for x in (true_aabb(b, gi) or gbox)]})
                break
    return trig, clip, len(glass)


def main(argv):
    d = Path(argv[1]) if len(argv) > 1 and not argv[1].startswith("-") else \
        Path(r"C:\openmerc-data\extract\pak0\maps")
    rule = argv[argv.index("--rule") + 1] if "--rule" in argv else "old"
    only = set(argv[argv.index("--maps") + 1].split(",")) if "--maps" in argv else None
    out = open(argv[argv.index("--json") + 1], "w", encoding="utf-8") if "--json" in argv else None
    verbose = "-v" in argv
    tot = [0, 0, 0]
    for p in sorted(d.glob("*.bsp")):
        if only and p.stem not in only:
            continue
        b = bsp.load(str(p))
        trig, clip, ng = run(b, rule)
        if rule == "new":
            kept = [x for x in trig + clip if not x["strip"]]
            trig = [x for x in trig if x["strip"]]
            clip = [x for x in clip if x["strip"]]
            if verbose:
                for x in kept:
                    print("   kept  #%d %s %s %s inside=%.2f pane #%d %s: %s" % (x["brush"], x["contents"], x["tex"],
                          x["bounds"], x["inside"], x["glass"], x["glass_owner"], x["why"]))
        tw = sum(1 for t in trig if t["owner"] == "world")
        tot[0] += len(trig); tot[1] += tw; tot[2] += len(clip)
        print("%-6s rule=%s trigWINDOW %3d (world %d) clipSeal %2d glass %3d" % (p.stem, rule, len(trig), tw, len(clip), ng))
        if verbose:
            for t in trig:
                if t["owner"] == "world":
                    print("   trig  #%d %s %s %s %s" % (t["brush"], t["contents"], t["tex"], t["bounds"],
                          ("inside=%.2f pane #%d %s: %s" % (t["inside"], t["glass"], t["glass_owner"], t["why"]))
                          if "inside" in t else ""))
            for c in clip:
                print("   clip  #%d %s %s %s owner=%s <- glass #%d %s %s" % (c["brush"], c["contents"], c["tex"], c["bounds"],
                      c["owner"], c["glass"], c["glass_owner"], c["glass_bounds"]))
        if out:
            out.write(json.dumps({"map": p.stem, "rule": rule, "trig": trig, "clip": clip, "glass": ng}) + "\n")
    print("TOTAL rule=%s trigWINDOW %d (world %d) clipSeal %d" % (rule, *tot))


if __name__ == "__main__":
    main(sys.argv)
