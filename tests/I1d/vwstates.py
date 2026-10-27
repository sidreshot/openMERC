#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1d test aid: the view weapon in a client demo (.dm2, SPEC-311), decoded with tools/net/sofnet.py -
# per frame: frame number, game time, player-state field 1 (view-weapon instance id and weapon type), the clip /
# reserve counters and the weapon-state sequence, every svc 27 record of that instance (sequence id, and the
# times A - frame time and S - A in 1/120 s, SPEC-013 group 8), temp entities (type), svc 28/29/30.
#   python -I vwstates.py <demo.dm2> [--all]       (--all: also frames without a change)
# Implements: SPEC-013 (reader side, through sofnet), SPEC-202 (player-state field 1)
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402


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
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args(argv)
    sess = sofnet.Session()
    state = {"frame": None, "time": 0.0, "vid": 0, "line": [], "last_ps": None}
    orig23, orig27, orig1 = sofnet.Session.svc_23, sofnet.Session.svc_27, sofnet.Session.svc_1
    orig28, orig29, orig30 = sofnet.Session.svc_28, sofnet.Session.svc_29, sofnet.Session.svc_30

    def flush():
        if state["frame"] is not None and (state["line"] or a.all):
            print("f%d t%.3f vw %d %s" % (state["frame"], state["time"], state["vid"], " ".join(state["line"])))
        state["line"] = []

    def svc_23(self, m, ctx):
        flush()
        orig23(self, m, ctx)
        frame = ctx["frame"]
        ents, ps = self.frames[frame]
        fs = self.frames_seen[-1]
        state["frame"], state["time"] = frame, fs["time"]
        vw = ps["vweap"]
        state["vid"] = vw[0]
        key = (vw, ps["wcountA"], ps["wcountB"], ps["wseq"], ps["resync"], ps["inhibit"])
        if key != state["last_ps"]:
            state["line"].append("ps(vweap %s clip %s res %s wseq %s resync %s inhibit %s)" % (
                vw, ps["wcountA"][0], ps["wcountB"][0], ps["wseq"][0], ps["resync"][0], ps["inhibit"][0]))
            state["last_ps"] = key

    last_unrel = {}
    orig_ru = sofnet.GhoulState.read_unreliable

    def read_unreliable(self, data):
        rec = orig_ru(self, data)
        last_unrel["rec"] = rec
        return rec

    sofnet.GhoulState.read_unreliable = read_unreliable

    def svc_27(self, m, ctx):
        last_unrel.pop("rec", None)
        orig27(self, m, ctx)
        rec = last_unrel.get("rec")
        if not rec:
            state["line"].append("svc27-undecoded")
            return
        for iid, oid, base, st in rec["updates"]:
            if iid != state["vid"]:
                continue
            parts = ["base-%d" % base]
            if "g6" in st:
                parts.append("seq %d" % st["g6"])
            if "g7" in st:
                parts.append("mode %d" % st["g7"])
            if "g8" in st:
                t1, t2 = st["g8"]
                parts.append("A-t %d/120 S-A %d/120" % (t1, t2))
            if "g13" in st:
                parts.append("bolt %s" % (st["g13"],))
            state["line"].append("vw27(%s)" % " ".join(parts))

    def svc_1(self, m, ctx):
        p0 = m.p
        t = m.byte()
        m.p = p0
        orig1(self, m, ctx)
        state["line"].append("te%d" % t)

    def svc_28(self, m, ctx):
        p0 = m.p
        orig28(self, m, ctx)
        state["line"].append("svc28(%s)" % bytes(m.d[p0:m.p]).hex())

    def svc_29(self, m, ctx):
        p0 = m.p
        orig29(self, m, ctx)
        state["line"].append("svc29")

    def svc_30(self, m, ctx):
        orig30(self, m, ctx)
        state["line"].append("svc30")

    sofnet.Session.svc_23 = svc_23
    sofnet.Session.svc_27 = svc_27
    sofnet.Session.svc_1 = svc_1
    sofnet.Session.svc_28 = svc_28
    sofnet.Session.svc_29 = svc_29
    sofnet.Session.svc_30 = svc_30
    for i, b in enumerate(blocks(a.demo)):
        m = sofnet.Msg(b)
        try:
            sess.parse_svc(m, {"t": i, "seq": i, "dir": "s2c"})
        except Exception as ex:  # noqa: BLE001
            print("block %d: %s: %s" % (i, type(ex).__name__, ex))
    flush()


if __name__ == "__main__":
    main(sys.argv[1:])
