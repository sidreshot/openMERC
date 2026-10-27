#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 5 (black-box comparison of two backends' music; no game code)
"""Compare the musicwin.py tables of two runs of the same scenario (e.g. Defsnd and OMsnd).

usage: musiccmp.py <a.win.txt> <b.win.txt> [--settle 1.5] [--on 0.05]

Both tables must be made with --proxy (the relay events give each capture's time axis): times are taken relative to
each run's `first_frame` event. For every window of run A, the window of run B at the same relative time is compared:
- which pieces are playing (gain > --on on the left side) - windows where the sets differ are listed;
- the gain difference per piece in steady windows (not within --settle seconds after a relay event, when fades and
  the backends' different frame timing make windows incomparable): median and maximum |dL|, |dR|.
"""
import argparse
import re


def load(path):
    events, first, rows, names = [], None, [], None
    for line in open(path, encoding="utf-8", errors="replace"):
        if line.startswith("# event at"):
            t = float(line.split()[3])
            if "first_frame" in line:
                first = t
            events.append((t, line.split(":", 1)[1].strip()))
        elif line.startswith("#   time_s"):
            names = re.findall(r"(\S+): gain L", line)
        elif not line.startswith("#") and line.strip():
            f = line.split()
            t = float(f[0])
            vals = []
            for i in range(len(names)):
                gl, gr = float(f[1 + 3 * i]), float(f[2 + 3 * i])
                vals.append((gl, gr))
            rows.append((t, vals))
    if first is None:
        raise SystemExit(f"{path}: no first_frame event (make the table with --proxy)")
    return names, first, events, rows


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--settle", type=float, default=1.5)
    ap.add_argument("--on", type=float, default=0.05)
    a = ap.parse_args()
    na, fa, ea, ra = load(a.a)
    nb, fb, eb, rb = load(a.b)
    if na != nb:
        raise SystemExit("different pieces")
    step = ra[1][0] - ra[0][0]
    bt = {round((t - fb) / step): v for t, v in rb}
    evs = sorted({round(t - fa, 3) for t, _ in ea} | {round(t - fb, 3) for t, _ in eb})
    diffs = {n: [] for n in na}
    mism = []
    for t, va in ra:
        rel = t - fa
        vb = bt.get(round(rel / step))
        if vb is None:
            continue
        on_a = {n for n, (gl, _) in zip(na, va) if gl > a.on}
        on_b = {n for n, (gl, _) in zip(nb, vb) if gl > a.on}
        settling = any(0 <= rel - e < a.settle or 0 <= rel + step - e < a.settle for e in evs)
        if on_a != on_b and not settling:
            mism.append((rel, sorted(on_a), sorted(on_b)))
        if not settling:
            for n, (al, ar), (bl, br) in zip(na, va, vb):
                if al > a.on or bl > a.on:
                    diffs[n].append((abs(al - bl), abs(ar - br), al, bl))
    print(f"# {a.a} vs {a.b}: windows {step:g} s, times relative to first_frame, settle {a.settle} s after events")
    for n in na:
        d = diffs[n]
        if not d:
            print(f"{n:24} never playing")
            continue
        dl = sorted(x[0] for x in d)
        dr = sorted(x[1] for x in d)
        la = sorted(x[2] for x in d)
        lb = sorted(x[3] for x in d)
        print(f"{n:24} {len(d):4} steady windows: median level A {la[len(la) // 2]:.4f} B {lb[len(lb) // 2]:.4f}; "
              f"|dL| median {dl[len(dl) // 2]:.4f} max {dl[-1]:.4f}; |dR| median {dr[len(dr) // 2]:.4f} max {dr[-1]:.4f}")
    print(f"windows where the playing pieces differ (outside settling): {len(mism)}")
    for rel, x, y in mism[:20]:
        print(f"  {rel:8.2f} s: A {x}  B {y}")


if __name__ == "__main__":
    main()
