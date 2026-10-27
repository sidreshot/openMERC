#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - timeline of what the test client saw in one capture: player-state origin/velocity/view angles,
# stats and weapon fields per frame (only when something changed), plus prints. Two captures side by side with
# --other: the frames are matched by time since the first frame.
# Implements: (test tooling, no spec)
#
# usage: captrack.py <capture dir or cap.jsonl> [--other <capture>] [--fields origin,velocity,stats,...]

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402

DEFAULT_FIELDS = "pm_type,origin,velocity,pm_flags,viewangles,viewoffset,vweap,wcountA,wcountB,wseq,stats,fov,blend"


class Tracker(sofnet.Session):
    """Session that keeps the player state of every frame (the base class keeps only the last 32)."""

    def __init__(self, *args, **kw):
        super().__init__(*args, **kw)
        self.timeline = []

    def svc_23(self, m, ctx):
        super().svc_23(m, ctx)
        f = self.frames_seen[-1]
        ents, ps = self.frames[f["frame"]]
        self.timeline.append((ctx["t"], f["frame"], ps, ents.get(1)))


def load(path):
    p = Path(path)
    if p.is_dir():
        p = p / "cap.jsonl"
    orig = sofnet.Session
    sofnet.Session = Tracker
    try:
        sess = sofnet.decode_capture(str(p))[0]
    finally:
        sofnet.Session = orig
    return sess


def fmt(ps, fields, e1):
    out = []
    for f in fields:
        if f == "origin":
            v = tuple(round(x / 8.0, 1) for x in ps["origin"])
        elif f == "ent1":
            v = None if e1 is None else (e1["frame"], hex(e1["renderfx"]), e1["events"])
        else:
            v = ps.get(f)
        out.append("%s=%s" % (f, v))
    return " ".join(out)


def rows(sess, fields):
    res = []
    last = None
    t0 = sess.timeline[0][0] if sess.timeline else 0
    for t, frame, ps, e1 in sess.timeline:
        s = fmt(ps, fields, e1)
        if s != last:
            res.append((t - t0, frame, s))
            last = s
    return res


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cap")
    ap.add_argument("--other")
    ap.add_argument("--fields", default=DEFAULT_FIELDS)
    a = ap.parse_args(argv)
    fields = a.fields.split(",")
    sa = load(a.cap)
    ra = rows(sa, fields)
    if not a.other:
        for t, frame, s in ra:
            print("%6.2f %5d %s" % (t, frame, s))
        return
    sb = load(a.other)
    rb = rows(sb, fields)
    merged = sorted([(t, "A", f, s) for t, f, s in ra] + [(t, "B", f, s) for t, f, s in rb])
    for t, side, frame, s in merged:
        print("%6.2f %s %5d %s" % (t, side, frame, s))


if __name__ == "__main__":
    main(sys.argv[1:])
