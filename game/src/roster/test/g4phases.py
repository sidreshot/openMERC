#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - motion phases of one entity in two captures, by server frame number: runs of frames in which the
# entity moves (M), only turns (T) or stands still (S), with their first/last frame and the end position. Used to
# compare helicopter and vehicle routines (legs, turns, hovers, drives) of the original and OpenMerc frame by frame.
# Implements: SPEC-731 (black-box comparison of routine timing)
#
# usage: g4phases.py <capA.jsonl> <capB.jsonl> <entity A> [entity B]
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g4ents  # noqa: E402


def series(path, num):
    """frame -> (origin, angles) rounded to 0.1"""
    s = g4ents.resolve(path)
    out = {}
    for x in s.timeline:
        e = x["ents"].get(num)
        if e:
            out[x["frame"]] = (tuple(round(v, 1) for v in e["origin"]), tuple(round(v, 1) for v in e["angles"]))
    return out


def phases(ser):
    fr = sorted(ser)
    if not fr:
        return []
    res = []
    cur = None
    start = fr[0]
    for i in range(1, len(fr)):
        moving = ser[fr[i]][0] != ser[fr[i - 1]][0]
        turning = ser[fr[i]][1] != ser[fr[i - 1]][1]
        st = "M" if moving else ("T" if turning else "S")
        if st != cur:
            if cur:
                res.append((cur, start, fr[i - 1], ser[fr[i - 1]]))
            cur = st
            start = fr[i - 1]
    res.append((cur, start, fr[-1], ser[fr[-1]]))
    return res


def main(argv):
    a = series(argv[0], int(argv[2]))
    b = series(argv[1], int(argv[3] if len(argv) > 3 else argv[2]))
    pa, pb = phases(a), phases(b)

    def f(p):
        return "%s %4d-%4d (%3d) %s" % (p[0], p[1], p[2], p[2] - p[1], p[3][0]) if p else "-"
    for i in range(max(len(pa), len(pb))):
        print("%-60s | %s" % (f(pa[i] if i < len(pa) else None), f(pb[i] if i < len(pb) else None)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
