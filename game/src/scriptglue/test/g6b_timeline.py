#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G6b - turn a headless-client capture (game/test/sofclient.py, udpproxy JSON lines) into the timeline a
# client sees of a scripted sequence: entity moves/rotations, entity spawns/removals, sounds, captions (string
# package prints), prints, configstrings set during play, stufftext, and the player-state fields scripts drive
# (remote camera view/origin/angles, field of view, cinematic freeze, music, pm_type). Everything is keyed by the
# server frame number, so two runs of the same map (original module vs OpenMerc) can be diffed line by line.
# Implements: SPEC-202 (player-state fields 18-20, 24, 27, 28), SPEC-201 (svc_sound, svc_sp_print), SPEC-750/751
#
# usage: g6b_timeline.py cap.jsonl [--out timeline.txt] [--from FRAME] [--to FRAME] [--noents] [--rel REGEX]
#                          [--drop REGEX]
#        g6b_timeline.py --diff A.txt B.txt     (frame-aligned comparison, summary of differences)

import argparse
import difflib
import json
import re
import sys
from collections import defaultdict
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "tools" / "net"))
import sofnet  # noqa: E402

PS_KEEP = ("pm_type", "delta_angles", "remoteview", "remoteorigin", "remoteangles", "fov", "cinfreeze", "music")


class TimelineSession(sofnet.Session):
    def __init__(self):
        super().__init__()
        self.events = defaultdict(list)   # frame -> [text]
        self.cur = -1
        self.pending = []                 # events of the packet before its svc_frame
        self.last_ps = None
        self.last_ents = {}
        self.begun = False

    def emit(self, text):
        self.pending.append(text)

    def flush(self, frame):
        if self.pending:
            self.events[frame].extend(self.pending)
            self.pending = []

    def cs_name(self, idx):
        v = self.cs.get(idx, b"?")
        return v.decode("latin-1") if isinstance(v, bytes) else str(v)

    # sounds (SPEC-201 svc_sound)
    def svc_10(self, m, ctx):
        fl = m.byte()
        idx = m.byte()
        if fl & 0x20:
            idx += 256
        vol = m.byte() if fl & 0x01 else 255
        att = m.byte() if fl & 0x02 else 64
        ofs = m.byte() if fl & 0x10 else 0
        ent = chan = -1
        if fl & 0x08:
            ec = m.short()
            ent, chan = ec >> 3, ec & 7
        if fl & 0x04:
            m.pos()
        self.emit("sound %s ent %d ch %d vol %d att %d ofs %d" % (self.cs_name(291 + idx), ent, chan, vol, att, ofs))

    def svc_11(self, m, ctx):
        lvl = m.byte()
        s = m.string().decode("latin-1", "replace").rstrip("\n")
        self.emit("print %d %s" % (lvl, s))

    def svc_13(self, m, ctx):
        s = m.string()
        self.stuff.append((ctx["t"], s))
        if self.begun:
            self.emit("stuff %s" % s.decode("latin-1", "replace").strip().replace("\n", " | "))

    def svc_15(self, m, ctx):
        before = dict(self.cs)
        super().svc_15(m, ctx)
        if self.begun:
            for k, v in self.cs.items():
                if before.get(k) != v:
                    self.emit("cs %d %s" % (k, v.decode("latin-1") if isinstance(v, bytes) else v))

    def svc_17(self, m, ctx):
        self.emit("center %s" % m.string().decode("latin-1", "replace"))

    def svc_34(self, m, ctx):
        self.emit("sp %04x" % m.short())

    def svc_36(self, m, ctx):
        sid = m.short()
        n = m.byte()
        m.take(n)
        self.emit("sp %04x data1 %d" % (sid, n))

    def svc_37(self, m, ctx):
        sid = m.short()
        n = m.short()
        m.take(n)
        self.emit("sp %04x data2 %d" % (sid, n))

    def svc_23(self, m, ctx):
        super().svc_23(m, ctx)
        frame = ctx["frame"]
        self.begun = True
        ents, ps = self.frames[frame]
        out = []
        if self.last_ps is not None:
            for k in PS_KEEP:
                if ps[k] != self.last_ps[k]:
                    out.append("ps %s %s" % (k, ps[k]))
        else:
            out.append("ps start " + " ".join("%s=%s" % (k, ps[k]) for k in PS_KEEP))
        self.last_ps = ps
        for num in sorted(set(ents) | set(self.last_ents)):
            if num <= 1:
                continue
            e, o = ents.get(num), self.last_ents.get(num)
            if e is None:
                out.append("ent %d removed" % num)
            elif o is None:
                out.append("ent %d appears model %d at %s ang %s" % (num, e["modelindex"], fmtv(e["origin"]),
                                                                     fmtv(e["angles"])))
            elif e["origin"] != o["origin"] or e["angles"] != o["angles"]:
                out.append("ent %d at %s ang %s" % (num, fmtv(e["origin"]), fmtv(e["angles"])))
        self.last_ents = ents
        self.pending = out + self.pending
        self.flush(frame)
        self.cur = frame


def fmtv(v):
    return "(%s)" % " ".join("%g" % round(x, 1) for x in v)


def build(cap):
    sess = TimelineSession()
    recs = [json.loads(l) for l in open(cap)]
    chan_in = 0
    for r in recs:
        if r["dir"] != "s2c":
            continue
        data = bytes.fromhex(r["hex"])
        if data[:4] == b"\xff\xff\xff\xff":
            if data[4:].startswith(b"client_connect"):
                sess.new_connection()
                chan_in = 0
            continue
        if len(data) < 8:
            continue
        m = sofnet.Msg(data)
        w1 = m.ulong()
        m.ulong()
        seq = w1 & 0x7FFFFFFF
        if seq <= chan_in:
            continue
        chan_in = seq
        ctx = {"t": r["t"], "seq": seq, "dir": "s2c"}
        try:
            sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001
            sess.pending.append("decode error %s" % ex)
        if "frame" not in ctx:
            sess.flush(sess.cur)
    return sess


def write(sess, out, lo, hi, noents, rel=None, drop=None):
    """rel: regex of the anchor event; frames are written relative to its first frame, configstring indexes are
    dropped (they depend on what else the module registered). drop: regex of events to leave out."""
    base = 0
    if rel:
        rx = re.compile(rel)
        hits = [f for f in sorted(sess.events) for ev in sess.events[f] if rx.search(ev)]
        base = hits[0] if hits else 0
    dx = re.compile(drop) if drop else None
    lines = []
    for f in sorted(sess.events):
        if f < lo or f > hi:
            continue
        for ev in sess.events[f]:
            if noents and ev.startswith("ent "):
                continue
            if dx and dx.search(ev):
                continue
            if rel:
                ev = re.sub(r"^cs \d+ ", "cs ", ev)
            lines.append("%6d %s" % (f - base, ev))
    text = "\n".join(lines) + "\n"
    if out:
        Path(out).write_text(text, encoding="utf-8")
    else:
        sys.stdout.write(text)
    return lines


def diff(a, b):
    la = Path(a).read_text(encoding="utf-8").splitlines()
    lb = Path(b).read_text(encoding="utf-8").splitlines()
    d = list(difflib.unified_diff(la, lb, a, b, n=1, lineterm=""))
    print("%d lines vs %d lines, %d diff lines" % (len(la), len(lb), len(d)))
    for line in d[:400]:
        print(line)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cap", nargs="?")
    ap.add_argument("--out")
    ap.add_argument("--from", dest="lo", type=int, default=-10**9)
    ap.add_argument("--to", dest="hi", type=int, default=10**9)
    ap.add_argument("--noents", action="store_true")
    ap.add_argument("--diff", nargs=2)
    ap.add_argument("--rel", help="regex of the anchor event: frames relative to it, configstring indexes dropped")
    ap.add_argument("--drop", help="regex of events to leave out")
    a = ap.parse_args(argv)
    if a.diff:
        diff(*a.diff)
        return 0
    sess = build(a.cap)
    lines = write(sess, a.out, a.lo, a.hi, a.noents, a.rel, a.drop)
    if a.out:
        print("%d timeline lines -> %s" % (len(lines), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
