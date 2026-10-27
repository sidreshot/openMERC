#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - compare two protocol-33 captures (original server vs OpenMerc server) of the same
# scripted client session: out-of-band texts, signon commands, serverdata, configstrings, baselines,
# Ghoul reliable stream, message counts and the first full frame. Decoding by tools/net/sofnet.py.
# Implements: SPEC-200..203 (comparison of observable behaviour)
#
# usage: capcompare.py <reference.jsonl> <candidate.jsonl> [--frames N]

import argparse
import json
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402


class Rec:
    def __init__(self, path, nframes):
        self.first_frames = []
        self.stuff = []
        self.ghoul_rel = []
        self.ghoul_unrel = []
        orig23 = sofnet.Session.svc_23
        orig13 = sofnet.Session.svc_13
        orig26 = sofnet.Session.svc_26
        orig27 = sofnet.Session.svc_27
        me = self

        def svc_23(sess, m, ctx):
            orig23(sess, m, ctx)
            if len(me.first_frames) < nframes:
                ents, ps = sess.frames[ctx["frame"]]
                me.first_frames.append((ctx["frame"], {k: dict(v) for k, v in ents.items()}, dict(ps)))

        def svc_13(sess, m, ctx):
            p = m.p
            orig13(sess, m, ctx)
            me.stuff.append(bytes(m.d[p:m.p]))

        def svc_26(sess, m, ctx):
            p = m.p
            orig26(sess, m, ctx)
            me.ghoul_rel.append(m.p - p)

        def svc_27(sess, m, ctx):
            p = m.p
            orig27(sess, m, ctx)
            me.ghoul_unrel.append(m.p - p)

        sofnet.Session.svc_23, sofnet.Session.svc_13 = svc_23, svc_13
        sofnet.Session.svc_26, sofnet.Session.svc_27 = svc_26, svc_27
        try:
            self.sess, self.chans, self.totals, self.oob, self.bad, self.qports = \
                sofnet.decode_capture(path)
        finally:
            sofnet.Session.svc_23, sofnet.Session.svc_13 = orig23, orig13
            sofnet.Session.svc_26, sofnet.Session.svc_27 = orig26, orig27
        self.recs = [json.loads(l) for l in open(path)]


def strip(e):
    return {k: v for k, v in e.items() if not k.startswith("_")}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("cand")
    ap.add_argument("--frames", type=int, default=3)
    ap.add_argument("--maxlist", type=int, default=40)
    a = ap.parse_args(argv)
    R, C = Rec(a.ref, a.frames), Rec(a.cand, a.frames)
    n = a.maxlist

    print("== decode errors: ref %d, cand %d" % (len(R.bad), len(C.bad)))
    for b in C.bad[:10]:
        print("   cand", b)

    print("== out-of-band (s2c)")
    ro = [t for _, d, t in R.oob if d == "s2c"]
    co = [t for _, d, t in C.oob if d == "s2c"]
    for x, y in zip(ro, co):
        tag = "same" if x == y else "DIFF"
        if tag == "DIFF" and x.split()[0] == y.split()[0] == b"challenge":
            tag = "same kind (challenge value)"
        print("  %-28s ref %r\n  %-28s cand %r" % (tag, x[:120], "", y[:120]))

    print("== stufftexts: ref %d, cand %d" % (len(R.stuff), len(C.stuff)))
    for x, y in zip(R.stuff, C.stuff):
        if x != y:
            print("  ref %r  cand %r" % (x[:60], y[:60]))

    print("== serverdata: ref %r\n                cand %r" % (R.sess.serverdata, C.sess.serverdata))

    print("== configstrings: ref %d, cand %d" % (len(R.sess.cs), len(C.sess.cs)))
    keys = sorted(set(R.sess.cs) | set(C.sess.cs))
    diffs = [(k, R.sess.cs.get(k), C.sess.cs.get(k)) for k in keys if R.sess.cs.get(k) != C.sess.cs.get(k)]
    ghoulfile = [d for d in diffs if 1497 <= d[0] < 3545]
    other = [d for d in diffs if not (1497 <= d[0] < 3545)]
    print("   differing: %d (ghoul-file range %d, other %d)" % (len(diffs), len(ghoulfile), len(other)))
    for k, x, y in other[:n]:
        print("   cs %4d ref %r cand %r" % (k, x, y))

    print("== baselines: ref %d, cand %d" % (len(R.sess.baselines), len(C.sess.baselines)))
    for k in sorted(set(R.sess.baselines) | set(C.sess.baselines)):
        x, y = R.sess.baselines.get(k), C.sess.baselines.get(k)
        x, y = strip(x) if x else None, strip(y) if y else None
        if x != y:
            print("   baseline %d\n     ref  %s\n     cand %s" % (k, x, y))

    print("== ghoul reliable chunks: ref %d (%d bytes), cand %d (%d bytes)" % (
        len(R.ghoul_rel), sum(R.ghoul_rel), len(C.ghoul_rel), sum(C.ghoul_rel)))
    print("== ghoul unreliable msgs: ref %d (%d bytes), cand %d (%d bytes)" % (
        len(R.ghoul_unrel), sum(R.ghoul_unrel), len(C.ghoul_unrel), sum(C.ghoul_unrel)))

    print("== message counts (svc)")
    for k in sorted(set(R.sess.svc_count) | set(C.sess.svc_count)):
        x, y = R.sess.svc_count.get(k, 0), C.sess.svc_count.get(k, 0)
        print("   %-26s ref %6d cand %6d%s" % (k, x, y, "" if x == y else "   *"))
    print("== temp entities: ref %s\n                   cand %s" % (
        dict(sorted(R.sess.te_count.items())), dict(sorted(C.sess.te_count.items()))))

    print("== first frames")
    for (rf, re, rp), (cf, ce, cp) in zip(R.first_frames, C.first_frames):
        print("  frame ref %d cand %d: entities ref %s cand %s" % (rf, cf, sorted(re), sorted(ce)))
        for k in sorted(set(rp) | set(cp)):
            if k.startswith("_"):
                continue
            if rp.get(k) != cp.get(k):
                print("    ps %-12s ref %s cand %s" % (k, rp.get(k), cp.get(k)))
        for num in sorted(set(re) & set(ce)):
            x, y = strip(re[num]), strip(ce[num])
            for f in sorted(set(x) | set(y)):
                if x.get(f) != y.get(f):
                    print("    ent %3d %-11s ref %s cand %s" % (num, f, x.get(f), y.get(f)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
