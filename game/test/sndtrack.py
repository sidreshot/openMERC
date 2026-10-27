#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - timeline of the sounds (svc_sound) and temp entities a client received: capture time, sound name
# (resolved through the capture's own configstrings) and the entity / temp-entity kind.
# Implements: (test tooling; SPEC-201 svc_sound / svc_temp_entity through tools/net/sofnet.py)
#
# usage: sndtrack.py <capture dir or cap.jsonl> [--grep text] [--te]

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402

NOW = [0.0]
EVENTS = []


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cap")
    ap.add_argument("--grep", default="")
    ap.add_argument("--te", action="store_true", help="temp entities too")
    a = ap.parse_args(argv)
    p = Path(a.cap)
    if p.is_dir():
        p = p / "cap.jsonl"
    parse = sofnet.Session.parse_svc
    snd, te = sofnet.Session.svc_10, sofnet.Session.svc_1

    def parse2(self, m, ctx):
        NOW[0] = ctx["t"]
        return parse(self, m, ctx)

    def snd2(self, m, ctx):
        st = m.p
        snd(self, m, ctx)
        d = m.d[st:m.p]
        fl, idx = d[0], d[1] + (256 if d[0] & 0x20 else 0)
        k = 2 + bool(fl & 1) + bool(fl & 2) + bool(fl & 0x10)
        ent = (int.from_bytes(d[k:k + 2], "little") >> 3) if fl & 8 else -1
        name = self.cs.get(291 + idx, b"#%d" % idx)
        EVENTS.append((NOW[0], "sound", name.decode("latin-1") if isinstance(name, bytes) else str(name), ent))

    def te2(self, m, ctx):
        st = m.p
        te(self, m, ctx)
        EVENTS.append((NOW[0], "te", str(m.d[st]), m.d[st + 1:m.p].hex()))

    sofnet.Session.parse_svc, sofnet.Session.svc_10, sofnet.Session.svc_1 = parse2, snd2, te2
    sofnet.decode_capture(str(p))
    for t, kind, what, extra in EVENTS:
        if kind == "te" and not a.te:
            continue
        if a.grep and a.grep.lower() not in what.lower():
            continue
        print("%8.2f %-5s %-40s %s" % (t, kind, what, extra))


if __name__ == "__main__":
    main(sys.argv[1:])
