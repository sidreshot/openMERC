#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 5.2, 5.3 (black-box comparison of music fades of two backends; no game code)
"""Fades after each relay event in two musicwin.py tables (fine windows, e.g. --win 0.05) of the same scenario.

usage: musicfade.py <a.win.txt> <b.win.txt> [--span 3]

For every relay event (time relative to each run's first_frame) and every piece whose left gain changes by more
than 0.05 within --span seconds after the event, the level before (median of the 0.3 s before the event), the level
after (median of the last 0.3 s of the span) and the times at which the change reaches 10 %, 50 % and 90 % are
reported for both runs (seconds after the event). The 10-90 % time of a linear fade is 0.8 of its length
(SPEC-040 fade lengths at 22050 Hz: 150 samples = 6.8 ms, 10000 = 0.454 s, 15000 = 0.680 s, 100000 = 4.535 s).
"""
import argparse

import numpy as np

from musiccmp import load


def crossings(rows, idx, e, span, step):
    t = np.array([r[0] for r in rows])
    g = np.array([r[1][idx][0] for r in rows])
    pre = g[(t >= e - 0.3) & (t < e)]
    post = g[(t >= e + span - 0.3) & (t < e + span)]
    if not len(pre) or not len(post):
        return None
    l0, l1 = float(np.median(pre)), float(np.median(post))
    if abs(l1 - l0) < 0.05:
        return l0, l1, None
    seg = (t >= e) & (t < e + span)
    ts, gs = t[seg], g[seg]
    out = []
    for q in (0.1, 0.5, 0.9):
        thr = l0 + q * (l1 - l0)
        hit = np.where((gs - thr) * np.sign(l1 - l0) >= 0)[0]
        out.append(float(ts[hit[0]] - e + step / 2) if len(hit) else None)
    return l0, l1, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--span", type=float, default=3.0)
    a = ap.parse_args()
    na, fa, ea, ra = load(a.a)
    nb, fb, eb, rb = load(a.b)
    step = ra[1][0] - ra[0][0]
    rel_a = [(t - fa, v) for t, v in ra]
    rel_b = [(t - fb, v) for t, v in rb]
    # events of A on A's relative axis; the same events of B (same order)
    evs_a = [(t - fa, d) for t, d in ea if "first_frame" not in d]
    evs_b = [(t - fb, d) for t, d in eb if "first_frame" not in d]
    print(f"# {a.a} (A) vs {a.b} (B); window {step:g} s; times in s after the relay event")
    print(f"# {'event':>8}  {'piece':22} {'A: from -> to':>16}  {'t10   t50   t90':>17}   {'B: from -> to':>16}  "
          f"{'t10   t50   t90':>17}")
    fmt = lambda x: f"{x:5.2f}" if x is not None else "  -  "
    for (ta, da), (tb, db) in zip(evs_a, evs_b):
        for i, n in enumerate(na):
            ca = crossings(rel_a, i, ta, a.span, step)
            cb = crossings(rel_b, i, tb, a.span, step)
            if not ca or not cb or (ca[2] is None and cb[2] is None):
                continue
            sa = " ".join(fmt(x) for x in (ca[2] or [None] * 3))
            sb = " ".join(fmt(x) for x in (cb[2] or [None] * 3))
            print(f"  {ta:8.2f}  {n[:22]:22} {ca[0]:7.4f}->{ca[1]:7.4f}  {sa}   {cb[0]:7.4f}->{cb[1]:7.4f}  {sb}"
                  f"   [{da[:60]}]")


if __name__ == "__main__":
    main()
