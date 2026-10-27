#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - entity / player states and Ghoul stream of a client demo (.dm2, SPEC-311), decoded by
# tools/net/sofnet.py, for black-box comparison of single-player sessions of the original engine and OpenMerc
# (the client demo is the only server->client stream of a local game).
# Implements: SPEC-311 / SPEC-202 (reader side only, through sofnet)
#
# usage: demostates.py <demo.dm2> [--ents 1,2] [--ps] [--frames N] [--first] [--sizes]
#   --first : only the entity list of the first frame (number, model, renderfx, origin)
#   --sizes : per-frame byte counts of the frame message parts (where the bytes go)

import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402
from netstates import fmt_ent  # noqa: E402


def blocks(path):
    d = Path(path).read_bytes()
    p = 0
    while p + 4 <= len(d):
        (n,) = struct.unpack_from("<i", d, p)
        p += 4
        if n == -1:
            return
        yield d[p:p + n]
        p += n


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("demo")
    ap.add_argument("--ents", default="")
    ap.add_argument("--ps", action="store_true")
    ap.add_argument("--frames", type=int, default=0)
    ap.add_argument("--first", action="store_true")
    ap.add_argument("--sizes", action="store_true")
    ap.add_argument("--at", type=int, default=-1, help="only the entity list of this frame number")
    a = ap.parse_args(argv)
    want = set(int(x) for x in a.ents.split(",") if x)
    last, lastps, count = {}, [None], [0]
    orig23 = sofnet.Session.svc_23

    def svc_23(self, m, ctx):
        start = m.p
        orig23(self, m, ctx)
        frame = ctx["frame"]
        ents, ps = self.frames[frame]
        count[0] += 1
        if a.frames and count[0] > a.frames:
            return
        if a.sizes:
            print("f%d frame message %d bytes, %d entities" % (frame, m.p - start, len(ents)))
        if a.at >= 0:
            if frame == a.at:
                for num in sorted(ents):
                    print("f%d ent %d %s" % (frame, num, fmt_ent(ents[num])))
            return
        if a.first:
            if count[0] == 1:
                for num in sorted(ents):
                    print("f%d ent %d %s" % (frame, num, fmt_ent(ents[num])))
            return
        if a.ps:
            key = tuple((k, str(ps[k])) for k in sorted(ps) if not k.startswith("_"))
            if key != lastps[0]:
                print("f%d ps %s" % (frame, " ".join("%s=%s" % kv for kv in key)))
                lastps[0] = key
        for num in sorted(ents):
            if want and num not in want:
                continue
            s = fmt_ent(ents[num])
            if last.get(num) != s:
                print("f%d ent %d %s" % (frame, num, s))
                last[num] = s
        for num in sorted(set(last) - set(ents)):
            if not want or num in want:
                print("f%d ent %d removed" % (frame, num))
                del last[num]

    sofnet.Session.svc_23 = svc_23
    sess = sofnet.Session()
    for i, b in enumerate(blocks(a.demo)):
        m = sofnet.Msg(b)
        try:
            sess.parse_svc(m, {"t": i, "seq": i, "dir": "s2c"})
        except Exception as ex:  # noqa: BLE001
            print("block %d: %s: %s" % (i, type(ex).__name__, ex))
    print("== %s: %d frames; ghoul: %s" % (a.demo, count[0], dict(sess.ghoul_stats)))


if __name__ == "__main__":
    main(sys.argv[1:])
