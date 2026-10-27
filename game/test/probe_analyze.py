#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - read back a g18probe capture: which memory word of the player state / entity state
# the engine puts into which wire field (SPEC-202). The probe encodes target/word/pattern in the
# game time of every frame (code = 1000 + 200*target + 2*word + sub).
# Implements: SPEC-202 (decoding only)

import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402


def main(cap):
    sess, *_ = sofnet.decode_capture(cap)
    playernum = (sess.serverdata or {}).get("playernum", 0)
    own = playernum + 1
    seen = defaultdict(set)
    for f in sess.frames_seen:
        pass
    # re-run to get per-frame states with game times
    sess2 = sofnet.Session()
    import json
    recs = [json.loads(l) for l in open(cap)]
    times = {}
    for fs in sess.frames_seen:
        times[fs["frame"]] = fs["time"]
    for frame, (ents, ps) in sorted(sess.frames.items()):
        pass
    # frames dict keeps only the last 32: decode again keeping everything
    allframes = {}
    orig_svc23 = sofnet.Session.svc_23

    def svc_23(self, m, ctx):
        orig_svc23(self, m, ctx)
        fr = ctx["frame"]
        allframes[fr] = (self.frames[fr], self.frames_seen[-1]["time"])
    sofnet.Session.svc_23 = svc_23
    sess, *_ = sofnet.decode_capture(cap)
    out = []
    for fr, ((ents, ps), t) in sorted(allframes.items()):
        code = round(t)
        if code < 1000 or abs(t - code) > 0.01:
            continue
        code -= 1000
        target, rest = divmod(code, 200)
        word, sub = divmod(rest, 2)
        if target == 0:
            for _, name, fmt in sofnet.PS_FIELDS:
                v = ps[name]
                if any(v):
                    seen[(target, word, sub)].add("%s=%s" % (name, v))
        else:
            e = ents.get(own)
            base = sess.baselines.get(own) or sofnet.EMPTY_ENT
            if e:
                for k, v in e.items():
                    if k.startswith("_") or k == "number":
                        continue
                    if v != sofnet.EMPTY_ENT.get(k):
                        seen[(target, word, sub)].add("%s=%s" % (k, v))
    for key in sorted(seen):
        t, w, s = key
        print("%s word %2d (+0x%02x) %s: %s" % ("ps" if t == 0 else "es", w, 4 * w,
                                               "float" if s else "int  ", "; ".join(sorted(seen[key]))))


if __name__ == "__main__":
    main(sys.argv[1])
