#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - origin/angles of one entity over time in two captures (original vs OpenMerc), sampled by game
# time, with the per-sample distance; used for the helicopter and tank paths (SPEC-731).
# Implements: SPEC-731 (black-box comparison of flight/drive paths)
#
# usage: g4track.py <capA.jsonl> <capB.jsonl> <entity number | model name> [step seconds]
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g4ents  # noqa: E402


def track(path, num):
    sess = g4ents.resolve(path)
    out = []
    for x in sess.timeline:
        e = x["ents"].get(num)
        if e:
            out.append((x["time"], tuple(e["origin"]), tuple(e["angles"])))
    return out


def at(tr, t):
    best = None
    for s in tr:
        if s[0] <= t + 1e-6:
            best = s
    return best


def find(path, model):
    # the first entity showing the model at any time (the model may leave the view before the capture ends)
    sess = g4ents.resolve(path)
    times = [x["time"] for x in sess.timeline]
    for t in times[::10] + times[-1:]:
        for num, d in g4ents.describe(sess, t).items():
            if model.lower() in d["model"].lower():
                return num
    return -1


def main(a, b, num, step=1.0):
    if not str(num).lstrip("-").isdigit():
        na, nb = find(a, num), find(b, num)
        print("entity of model %s: A %d, B %d" % (num, na, nb))
        ta, tb = track(a, na), track(b, nb)
    else:
        ta, tb = track(a, int(num)), track(b, int(num))
    if not ta or not tb:
        print("entity %s missing (A %d samples, B %d samples)" % (num, len(ta), len(tb)))
        return 1
    t0 = max(ta[0][0], tb[0][0])
    t1 = min(ta[-1][0], tb[-1][0])
    worst = 0
    t = t0
    while t <= t1 + 1e-6:
        sa, sb = at(ta, t), at(tb, t)
        d = math.dist(sa[1], sb[1])
        da = max(abs(((sa[2][i] - sb[2][i]) + 180) % 360 - 180) for i in range(3))
        worst = max(worst, d)
        print("t %6.1f  A %-28s B %-28s  dist %7.2f  angles A %-22s B %-22s max dAngle %.1f" % (
            t, tuple(round(v, 1) for v in sa[1]), tuple(round(v, 1) for v in sb[1]), d,
            tuple(round(v, 1) for v in sa[2]), tuple(round(v, 1) for v in sb[2]), da))
        t += step
    print("largest distance %.2f units" % worst)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], sys.argv[3], float(sys.argv[4]) if len(sys.argv) > 4 else 1.0))
