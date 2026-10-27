#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - Ghoul sequence timeline of the player models a client sees: for every instance of an object whose
# sequence list holds `meso_player` / `fem_play` sequences, the sequence index of each Ghoul state update (field
# "g6", an index into the object's first list = its sequence files) is printed by name with the capture time.
# Used to compare the third-person locomotion sequences of the original and OpenMerc (SPEC-AMEND-005 G18.26); the
# playback speed index (state field 2, SPEC-013) is printed when an update carries it.
# Implements: (test tooling; reads SPEC-013/SPEC-201 Ghoul stream through tools/net/sofnet.py)
#
# usage: seqtrack.py <capture dir or cap.jsonl> [--all]

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
    ap.add_argument("--all", action="store_true", help="every update, not only changes")
    a = ap.parse_args(argv)
    p = Path(a.cap)
    if p.is_dir():
        p = p / "cap.jsonl"
    parse, ru = sofnet.Session.parse_svc, sofnet.GhoulState.read_unreliable

    def parse2(self, m, ctx):
        NOW[0] = ctx["t"]
        return parse(self, m, ctx)

    def ru2(self, data):
        r = ru(self, data)
        for iid, oid, _base, st in r["updates"]:
            if "g6" in st or "g2" in st:
                EVENTS.append((NOW[0], iid, oid, st.get("g6"), self, st.get("g2")))
        return r

    sofnet.Session.parse_svc, sofnet.GhoulState.read_unreliable = parse2, ru2
    sofnet.decode_capture(str(p))
    last = {}
    cur = {}
    for t, iid, oid, seq, g, spd in EVENTS:
        if seq is None:
            seq = cur.get(iid)
            if seq is None:
                continue
        cur[iid] = seq
        obj = g.objects.get(oid)
        if not obj:
            continue
        names = obj["lists"][0]
        if not any(b"MESO_PLAYER" in g.strings[i] or b"FEM_PLAY" in g.strings[i]
                   for i in names[:1] if i < len(g.strings)):
            continue
        name = g.strings[names[seq]].decode("latin-1") if seq < len(names) and names[seq] < len(g.strings) else seq
        name = str(name).rsplit("/", 1)[-1].replace(".GHL", "").lower()
        key = (name, spd)
        if a.all or last.get(iid) != key:
            print("%8.2f inst %3d %s%s" % (t, iid, name, "" if spd is None else "  speed %d" % spd))
            last[iid] = key


if __name__ == "__main__":
    main(sys.argv[1:])
