#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G2 - what a client sees of the characters: decodes a headless-client capture (game/test/sofclient.py,
# udpproxy JSON lines) with tools/net/sofnet.py and resolves, per server frame and entity, the origin, angles and
# the Ghoul instance's current sequence (entity frame field = instance id, SPEC-202; instance state deltas and
# object sequence lists, SPEC-013). Used to compare the original game module with OpenMerc's (aidiff.py).
# Implements: SPEC-700, SPEC-711 (black-box observation), SPEC-013 / SPEC-202 (decoding)
#
# usage: aicap.py <cap.jsonl> [--json out.json] [--ents] [--seq]
#   --seq   print every sequence change per entity (time, entity, sequence)
#   --ents  print a per-entity summary (first/last seen, distance moved, sequences used)

import argparse
import json
import math
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "tools" / "net"))
import sofnet  # noqa: E402

# SPEC-013 section 3: default state of a new instance (only the fields used here)
DEFAULT = {"f1": 1, "g2": 4, "fA": 1, "fB": 0, "g5": 0, "g6": 0, "g7": 0, "g8": (0, 0), "g9": 0.0}


class Tracker(sofnet.Session):
    """sofnet session that also resolves ghoul instance states per frame."""

    def __init__(self):
        super().__init__()
        self.inst_states = defaultdict(dict)   # iid -> frame -> state
        self.inst_obj = {}
        self.timeline = []                      # (t, frame, gtime, {num: entity-dict}, {iid: state})
        self._pending_ghoul = []
        self._cur_frame = None
        self.sounds = []                        # (game time, entity, channel, sound name)

    def new_connection(self):
        super().new_connection()
        self.inst_states = defaultdict(dict)
        self.inst_obj = {}

    def svc_27(self, m, ctx):
        n = m.short()
        data = m.take(n)
        try:
            rec = self.ghoul.read_unreliable(data)
        except Exception as ex:  # noqa: BLE001
            self.note("ghoul unreliable decode failure", "%s: %s" % (type(ex).__name__, ex))
            return
        frame = ctx.get("frame")
        if frame is None:
            return
        for iid, oid, base, st in rec["updates"]:
            obj = self.ghoul.objects.get(oid)
            if base == 0:
                ref = dict(DEFAULT)
                if obj is not None:
                    ref.update(obj.get("baseline", {}))
            else:
                ref = self.inst_states[iid].get(frame - base)
                if ref is None:
                    ref = dict(DEFAULT)
                    self.note("ghoul base frame missing", "inst %d frame %d base %d" % (iid, frame, base))
            new = dict(ref)
            new.update(st)
            new["_oid"] = oid
            self.inst_states[iid][frame] = new
            self.inst_obj[iid] = oid
            for k in [k for k in self.inst_states[iid] if k < frame - 40]:
                del self.inst_states[iid][k]

    def svc_10(self, m, ctx):
        """svc_sound (SPEC-201): kept as (game time, entity, channel, sound name) for --sounds"""
        p = m.p
        super().svc_10(m, ctx)
        m2 = sofnet.Msg(m.d[p:m.p])
        fl = m2.byte()
        idx = m2.byte()
        if fl & 0x20:
            idx += 256
        for bit in (0x01, 0x02, 0x10):
            if fl & bit:
                m2.byte()
        ent = chan = -1
        if fl & 0x08:
            ec = m2.short()
            ent, chan = ec >> 3, ec & 7
        t = self.frames_seen[-1]["time"] if self.frames_seen else 0.0
        name = self.cs.get(291 + idx, "#%d" % idx)
        self.sounds.append((t, ent, chan, name.decode("latin-1") if isinstance(name, bytes) else name))

    def svc_5(self, m, ctx):
        """svc_effect (SPEC-201): effects on an entity kept as (game time, entity, 'effect', name) for --sounds"""
        p = m.p
        super().svc_5(m, ctx)
        m2 = sofnet.Msg(m.d[p:m.p])
        eff, fl = m2.byte(), m2.byte()
        ent = -1
        if not (fl & 0x01) and (fl & 0x06):
            ent = m2.short()
        if ent >= 0:
            t = self.frames_seen[-1]["time"] if self.frames_seen else 0.0
            name = self.cs.get(647 + eff, b"?")
            self.sounds.append((t, ent, -1, "effect " + (name.decode("latin-1") if isinstance(name, bytes) else name)))

    def svc_23(self, m, ctx):
        super().svc_23(m, ctx)
        frame = ctx["frame"]
        ents, ps = self.frames[frame]
        self.timeline.append({"t": ctx["t"], "frame": frame, "time": self.frames_seen[-1]["time"],
                              "ents": ents, "ps": ps})

    def seq_name(self, iid, frame):
        """name of the current sequence of instance iid as of frame (latest state at or before frame)."""
        states = self.inst_states.get(iid)
        if not states:
            return None
        ks = [k for k in states if k <= frame]
        if not ks:
            return None
        st = states[max(ks)]
        obj = self.ghoul.objects.get(st.get("_oid"))
        sid = st.get("g6", 0)
        if obj is None or sid <= 0 or sid > len(obj["lists"][0]):
            return None
        si = obj["lists"][0][sid - 1]
        s = self.ghoul.strings[si] if si < len(self.ghoul.strings) else b"?"
        return s.decode("latin-1")


def decode(path):
    sess = Tracker()
    recs = [json.loads(line) for line in open(path, encoding="utf-8")]
    t0 = None
    for r in recs:
        if r["dir"] != "s2c":
            continue
        data = bytes.fromhex(r["hex"])
        if data[:4] == b"\xff\xff\xff\xff":
            txt = data[4:]
            if txt.startswith(b"client_connect"):
                sess.new_connection()
            continue
        m = sofnet.Msg(data)
        m.ulong()
        m.ulong()
        ctx = {"t": r["t"], "seq": 0, "dir": "s2c"}
        try:
            sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001
            sess.note("parse error", str(ex))
        # the ghoul unreliable message follows the frame in the same packet; resolve sequences after parsing
        if "frame" in ctx and sess.timeline and sess.timeline[-1]["frame"] == ctx["frame"]:
            row = sess.timeline[-1]
            seqs = {}
            for num, e in row["ents"].items():
                if e.get("renderfx", 0) & 0x400000:
                    seqs[num] = sess.seq_name(e.get("frame", 0), ctx["frame"])
            row["seqs"] = seqs
    return sess


def model_of(sess, iid):
    oid = sess.inst_obj.get(iid)
    obj = sess.ghoul.objects.get(oid)
    if obj is None or not obj["lists"][0]:
        return None
    si = obj["lists"][0][0]
    s = sess.ghoul.strings[si].decode("latin-1") if si < len(sess.ghoul.strings) else "?"
    return s.rsplit("/", 1)[0]


def summarise(sess, only_chars=True):
    """per entity: first/last time, start/end origin, path length, sequence runs"""
    out = {}
    t_begin = None
    for row in sess.timeline:
        if t_begin is None:
            t_begin = row["time"]
        for num, e in row["ents"].items():
            seq = row.get("seqs", {}).get(num)
            d = out.get(num)
            if d is None:
                d = out[num] = {"num": num, "first": row["time"], "last": row["time"], "o0": e["origin"],
                                "o": e["origin"], "a": e["angles"], "path": 0.0, "seqs": [], "inst": e.get("frame"),
                                "renderfx": e.get("renderfx", 0), "modelindex": e.get("modelindex", 0)}
            d["path"] += math.dist(d["o"], e["origin"])
            d["o"] = e["origin"]
            d["a"] = e["angles"]
            d["last"] = row["time"]
            if seq and (not d["seqs"] or d["seqs"][-1][1] != seq):
                d["seqs"].append((round(row["time"], 2), seq))
    if only_chars:
        out = {k: v for k, v in out.items() if any("ENEMY/" in s[1].upper() for s in v["seqs"])}
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--json")
    ap.add_argument("--seq", action="store_true")
    ap.add_argument("--ents", action="store_true")
    ap.add_argument("--all", action="store_true", help="all ghoul entities, not only characters")
    ap.add_argument("--track", type=int, action="append", default=[], help="print this entity every --step s")
    ap.add_argument("--step", type=float, default=0.5)
    ap.add_argument("--sounds", type=int, action="append", default=[],
                    help="print the sounds started on this entity (svc_sound)")
    a = ap.parse_args(argv)
    sess = decode(a.capture)
    summ = summarise(sess, not a.all)
    if a.ents or not (a.seq or a.json or a.track or a.sounds):
        for num in sorted(summ):
            d = summ[num]
            print("ent %4d inst %4s %-22s seen %6.1f-%6.1f moved %7.1f origin (%s) -> (%s) yaw %.0f, %d seq changes"
                  % (num, d["inst"], model_of(sess, d["inst"]) or "-", d["first"], d["last"], d["path"],
                     " ".join("%.0f" % x for x in d["o0"]), " ".join("%.0f" % x for x in d["o"]), d["a"][1],
                     len(d["seqs"])))
    if a.seq:
        for num in sorted(summ):
            for t, s in summ[num]["seqs"]:
                print("%7.2f ent %4d %s" % (t, num, s.rsplit("/", 1)[-1]))
    for num in a.track:
        last = -1e9
        for row in sess.timeline:
            e = row["ents"].get(num)
            if not e or row["time"] - last < a.step - 1e-3:
                continue
            last = row["time"]
            seq = (row.get("seqs", {}).get(num) or "-").rsplit("/", 1)[-1]
            print("%7.2f ent %4d (%7.1f %7.1f %6.1f) yaw %6.1f %s" % (row["time"], num, e["origin"][0], e["origin"][1],
                                                                  e["origin"][2], e["angles"][1], seq))
    for num in a.sounds:
        for t, ent, chan, name in sess.sounds:
            if ent == num:
                print("%7.2f ent %4d chan %d sound %s" % (t, ent, chan, name))
    if a.json:
        Path(a.json).write_text(json.dumps({str(k): v for k, v in summ.items()}, indent=1), encoding="utf-8")
    for k, v in sess.findings.items():
        print("note: %s: %d (%s)" % (k, len(v), v[0]), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
