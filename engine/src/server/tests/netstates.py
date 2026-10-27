#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - print the entity states / player states a capture carries, for black-box
# comparison of the OpenMerc server with the original one (decoding by tools/net/sofnet.py).
# Implements: SPEC-202 (reader side only, through sofnet)
#
# usage: netstates.py <capture.jsonl> [--ents 1,2,3] [--ps] [--baselines] [--frames N]

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402


def fmt_ent(e):
    keys = ["modelindex", "modelindex2", "frame", "skinnum", "effects", "renderfx", "origin",
            "angles", "yawofs", "sound", "sounddata", "events", "solid", "fx"]
    out = []
    for k in keys:
        if k in e:
            v = e[k]
            if k == "solid" and isinstance(v, int) and v not in (0, 31):
                v = "%08x(x%d y%d zd%d zu%d)" % (v, v & 255, (v >> 8) & 255, (v >> 16) & 255,
                                                 ((v >> 24) & 255) - 128)
            out.append("%s=%s" % (k, v))
    return " ".join(out)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--ents", default="")
    ap.add_argument("--ps", action="store_true")
    ap.add_argument("--baselines", action="store_true")
    ap.add_argument("--frames", type=int, default=0, help="stop after N frames")
    a = ap.parse_args(argv)
    want = set(int(x) for x in a.ents.split(",") if x)
    last = {}
    lastps = [None]
    count = [0]
    orig23 = sofnet.Session.svc_23

    def svc_23(self, m, ctx):
        orig23(self, m, ctx)
        frame = ctx["frame"]
        ents, ps = self.frames[frame]
        count[0] += 1
        if a.frames and count[0] > a.frames:
            return
        if a.ps:
            key = tuple((k, ps[k]) for k in sorted(ps) if not k.startswith("_"))
            if key != lastps[0]:
                vo = ps["viewoffset"]
                print("f%d ps pm_type=%s origin=%s vel=%s flags=%s viewofs=(%.2f %.2f %.2f) va=%s "
                      "pmextra=%s fov=%s stats=%s vweap=%s" % (
                          frame, ps["pm_type"], ps["origin"], ps["velocity"], ps["pm_flags"],
                          vo[0] * 0.25, vo[1] * 0.25, vo[2] * 0.2 + 14.6, ps["viewangles"],
                          ps["pmextra"], ps["fov"], ps["stats"], ps["vweap"]))
                lastps[0] = key
        for num in sorted(ents):
            if want and num not in want:
                continue
            e = ents[num]
            s = fmt_ent(e)
            if last.get(num) != s:
                print("f%d ent %d %s" % (frame, num, s))
                last[num] = s

    sofnet.Session.svc_23 = svc_23
    sess, *_ = sofnet.decode_capture(a.capture)
    if a.baselines:
        for num in sorted(sess.baselines):
            print("baseline %d %s" % (num, fmt_ent(sess.baselines[num])))


if __name__ == "__main__":
    main(sys.argv[1:])
