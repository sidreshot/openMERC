#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - compares two client demos (.dm2, SPEC-311) of the same level frame by frame: for every server
# frame present in both, the set of entities and their transmitted fields (decoded by tools/net/sofnet.py), the
# player state, and the Ghoul stream totals. Prints a summary and the first differences per field, so a
# single-player session of the original engine can be compared with the OpenMerc server running the same game
# module. Positions are compared with a tolerance (--tol units).
# Implements: SPEC-311 / SPEC-202 (reader side, through sofnet); black-box comparison, no game code
#
# usage: demodiff.py <a.dm2> <b.dm2> [--tol 1.0] [--show 12] [--ignore frame,angles]
import argparse
import math
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import sofnet  # noqa: E402
from demostates import blocks  # noqa: E402

FIELDS = ["modelindex", "modelindex2", "frame", "skinnum", "effects", "renderfx", "origin", "angles", "sound",
          "sounddata", "solid", "fx"]


def load(path):
    frames = {}
    sess = sofnet.Session()
    orig23 = sofnet.Session.svc_23

    def svc_23(self, m, ctx):
        orig23(self, m, ctx)
        f = ctx["frame"]
        ents, ps = self.frames[f]
        frames[f] = ({n: dict(e) for n, e in ents.items()}, dict(ps))

    sofnet.Session.svc_23 = svc_23
    try:
        for i, b in enumerate(blocks(path)):
            sess.parse_svc(sofnet.Msg(b), {"t": i, "seq": i, "dir": "s2c"})
    finally:
        sofnet.Session.svc_23 = orig23
    return frames, sess


def close(a, b, tol):
    if isinstance(a, (tuple, list)) and isinstance(b, (tuple, list)) and len(a) == len(b):
        return all(close(x, y, tol) for x, y in zip(a, b))
    if isinstance(a, float) or isinstance(b, float):
        return abs(float(a) - float(b)) <= tol
    return a == b


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tol", type=float, default=1.0)
    ap.add_argument("--show", type=int, default=12)
    ap.add_argument("--ignore", default="")
    a = ap.parse_args(argv)
    ignore = set(x for x in a.ignore.split(",") if x)
    fa, sa = load(a.a)
    fb, sb = load(a.b)
    common = sorted(set(fa) & set(fb))
    print("frames: A %d (%s..%s), B %d (%s..%s), common %d" % (
        len(fa), min(fa, default="-"), max(fa, default="-"), len(fb), min(fb, default="-"), max(fb, default="-"),
        len(common)))
    print("svc A: %s" % dict(sa.svc_count))
    print("svc B: %s" % dict(sb.svc_count))
    only_a, only_b = Counter(), Counter()
    diffs = defaultdict(list)
    diffcount = Counter()
    for f in common:
        ea, pa = fa[f]
        eb, pb = fb[f]
        for n in set(ea) - set(eb):
            only_a[n] += 1
        for n in set(eb) - set(ea):
            only_b[n] += 1
        for n in set(ea) & set(eb):
            for k in FIELDS:
                if k in ignore or k not in ea[n] or k not in eb[n]:
                    continue
                if not close(ea[n][k], eb[n][k], a.tol):
                    diffcount[k] += 1
                    if len(diffs[k]) < a.show:
                        diffs[k].append("f%d ent %d: A %s  B %s" % (f, n, ea[n][k], eb[n][k]))
        for k in sorted(set(pa) & set(pb)):
            if k.startswith("_") or k in ignore:
                continue
            if not close(pa[k], pb[k], a.tol):
                diffcount["ps." + k] += 1
                if len(diffs["ps." + k]) < a.show:
                    diffs["ps." + k].append("f%d: A %s  B %s" % (f, pa[k], pb[k]))
    if only_a:
        print("entities only in A (frames): %s" % dict(sorted(only_a.items())[:40]))
    if only_b:
        print("entities only in B (frames): %s" % dict(sorted(only_b.items())[:40]))
    if not diffcount:
        print("no field differences in common entities")
    for k, c in diffcount.most_common():
        print("%s: %d differences" % (k, c))
        for line in diffs[k]:
            print("   " + line)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
