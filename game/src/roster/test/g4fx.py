#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - effect events (svc_effect, SPEC-201) a client received, per server frame, and burst statistics of
# one effect (e.g. the tank's machine-gun muzzle flash `weapons/othermz/machinegun`, the helicopter's
# `weapons/othermz/heli`): shots per burst and pauses between bursts, for comparing two captures.
# Implements: SPEC-201 (reader side of svc_effect), SPEC-731 Part A §13, Part B §11 (black-box checks)
#
# usage: g4fx.py list <cap.jsonl> [--filter TEXT]
#        g4fx.py bursts <cap.jsonl> <effect name> [--gap FRAMES]
#        g4fx.py te <cap.jsonl> [--types 44,18,...]       temp entities (SPEC-201) per frame with their fields
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[3] / "tools" / "net"))
import sofnet  # noqa: E402


class FxSession(sofnet.Session):
    def __init__(self):
        super().__init__()
        self.fx = []
        self.te = []

    def svc_1(self, m, ctx):
        t = m.byte()
        if t > 64:
            raise ValueError("temp entity type %d > 64" % t)
        self.te_count[t] += 1
        self.te.append((ctx.get("frame"), ctx.get("t"), t, sofnet.read_fmt(m, sofnet.TE_FMT[t])))

    def svc_5(self, m, ctx):
        eff, fl = m.byte(), m.byte()
        pos = None
        if fl & 0x01:
            pos = m.pos()
        elif fl & 0x02:
            m.short()
        elif fl & 0x04:
            m.short()
            if fl & 0x08:
                m.short()
            m.short()
        if fl & 0x80:
            ext = m.byte()
            if ext & 0x01: m.short()
            if ext & 0x02: m.byte()
            if ext & 0x04: m.pos()
            if ext & 0x08: m.pos()
            if ext & 0x10: m.pos()
            if ext & 0x20: m.pos()
            if ext & 0x40: m.short()
            if ext & 0x80: m.byte()
        name = self.cs.get(647 + eff, b"?").decode("latin-1")
        self.fx.append((ctx.get("frame"), ctx.get("t"), name, fl, pos))


def load(path):
    sess = FxSession()
    for line in open(path, encoding="utf-8"):
        r = json.loads(line)
        if r["dir"] != "s2c":
            continue
        data = bytes.fromhex(r["hex"])
        if data[:4] == b"\xff\xff\xff\xff":
            continue
        mm = sofnet.Msg(data)
        mm.ulong()
        mm.ulong()
        ctx = {"t": r["t"], "seq": 0, "dir": "s2c"}
        try:
            sess.parse_svc(mm, ctx)
        except Exception as ex:  # noqa: BLE001
            sess.note("parse error", str(ex))
    return sess


def frames_of(sess, name):
    """server frame numbers (or receive times when no frame is known) of each event of the effect"""
    return [fr if fr is not None else t for fr, t, n, fl, pos in sess.fx if n.lower() == name.lower()]


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        flt = argv[argv.index("--filter") + 1].lower() if "--filter" in argv else None
        sess = load(argv[1])
        for fr, t, n, fl, pos in sess.fx:
            if flt and flt not in n.lower():
                continue
            print("frame %s t %.2f %s flags %02x%s" % (fr, t, n, fl, "" if pos is None else " at %s" % (tuple(round(v, 1) for v in pos),)))
        return 0
    if len(argv) >= 2 and argv[0] == "te":
        types = [int(x) for x in argv[argv.index("--types") + 1].split(",")] if "--types" in argv else None
        sess = load(argv[1])
        for fr, t, ty, fields in sess.te:
            if types and ty not in types:
                continue
            print("frame %s t %.2f te %d %s" % (fr, t, ty, fields))
        return 0
    if len(argv) >= 3 and argv[0] == "bursts":
        gap = int(argv[argv.index("--gap") + 1]) if "--gap" in argv else 2
        sess = load(argv[1])
        fr = frames_of(sess, argv[2])
        bursts = []
        for f in fr:
            if bursts and f - bursts[-1][-1] <= gap:
                bursts[-1].append(f)
            else:
                bursts.append([f])
        print("%d events in %d bursts" % (len(fr), len(bursts)))
        for i, b in enumerate(bursts):
            pause = (b[0] - bursts[i - 1][-1]) if i else None
            print("  burst at frame %s: %d shots%s" % (b[0], len(b), "" if pause is None else ", %d frames after the previous" % pause))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
