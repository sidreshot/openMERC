#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G3 - what a client sees of damage and gore: decodes a headless-client capture (game/test/sofclient.py)
# with tools/net/sofnet.py (through G2's aicap.Tracker for the Ghoul instance states) and lists, in time order,
# the named effects (svc_effect), sounds, temp entities and prints, the player's health / damage direction, and for
# every character instance the parts switched on/off and the skin overrides added (SPEC-013 groups 12 and 14:
# wound meshes, wound skins, caps) and its sequences.
# Implements: SPEC-720, SPEC-721, SPEC-722 (black-box observation); SPEC-013 / SPEC-201 (decoding)
#
# usage: g3cap.py <cap.jsonl> [--from T] [--to T] [--ent N] [--summary]
import argparse
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "tools" / "net"))
sys.path.insert(0, str(REPO / "game" / "src" / "ai" / "tools"))
import sofnet  # noqa: E402
import aicap  # noqa: E402

EVENTS = []      # (game time, kind, text)


def GT(sess, ctx):
    return sess.frames_seen[-1]["time"] if sess.frames_seen else 0.0
CUR = {"t": 0.0}


def s(sess, idx):
    v = sess.cs.get(idx)
    return v.decode("latin-1") if isinstance(v, bytes) else (v or "#%d" % idx)


# --- wrap the svc handlers to record what they parse ---------------------------------------------------------
_svc1, _svc5, _svc10, _svc11, _svc23 = (sofnet.Session.svc_1, sofnet.Session.svc_5, sofnet.Session.svc_10,
                                         sofnet.Session.svc_11, aicap.Tracker.svc_23)


def svc_1(self, m, ctx):
    p = m.p
    t = m.data[p] if hasattr(m, "data") else None
    _svc1(self, m, ctx)
    extra = ""
    raw = m.data[p:m.p]
    if t in (3, 6, 7) and len(raw) >= 8:
        import struct
        kind = raw[1]
        x, y, z = struct.unpack("<hhh", raw[2:8])
        extra = " impact kind %d at (%.0f %.0f %.0f)" % (kind, x / 8, y / 8, z / 8)
        if kind in (2, 3) and len(raw) >= 16:
            extra += " ent/mat " + raw[8:].hex()
    elif t == 18 and len(raw) >= 15:
        import struct
        x, y, z = struct.unpack("<hhh", raw[1:7])
        extra = " at (%.0f %.0f %.0f) dir %d count %d size %d chunk %d ext %d %d %d mat %d" % (
            x / 8, y / 8, z / 8, raw[7], raw[8], raw[9], raw[10], raw[11], raw[12], raw[13], raw[14])
    EVENTS.append((GT(self, ctx), "te", "TE %s%s" % (t if t is not None else "?", extra)))


def svc_5(self, m, ctx):
    p = m.p
    _svc5(self, m, ctx)
    raw = m.data[p:m.p] if hasattr(m, "data") else b""
    eff, fl = (raw[0], raw[1]) if len(raw) >= 2 else (0, 0)
    where = ""
    if fl & 0x02 and not fl & 0x01:
        where = " ent %d" % (raw[2] | raw[3] << 8)
    elif fl & 0x04 and not fl & 0x03:
        ent = raw[2] | raw[3] << 8
        iid = (raw[4] | raw[5] << 8) if fl & 0x08 else None
        bolt = (raw[6] | raw[7] << 8) if fl & 0x08 else (raw[4] | raw[5] << 8)
        where = " ent %d bolt %d %s" % (ent, bolt, bolt_name(self, ent, bolt, iid))
    EVENTS.append((GT(self, ctx), "effect", "%s%s" % (s(self, 647 + eff), where)))


def bolt_name(sess, ent, bolt, iid=None):
    """name of part/bolt id `bolt` (SPEC-013 list 2 = parts) of the entity's current Ghoul instance"""
    try:
        if iid is None:
            fr = max(sess.frames)
            iid = sess.frames[fr][0][ent].get("frame")
        obj = sess.ghoul.objects.get(sess.inst_obj.get(iid))
        return name_of(sess, obj, 1, bolt) if obj else "?"
    except Exception:  # noqa: BLE001
        return "?"


def svc_10(self, m, ctx):
    p = m.p
    _svc10(self, m, ctx)
    raw = m.data[p:m.p] if hasattr(m, "data") else b""
    if len(raw) >= 2:
        fl, idx = raw[0], raw[1]
        if fl & 0x20:
            idx += 256
        ent = ""
        k = 2 + (1 if fl & 1 else 0) + (1 if fl & 2 else 0) + (1 if fl & 0x10 else 0)
        if fl & 0x08 and len(raw) >= k + 2:
            ec = raw[k] | raw[k + 1] << 8
            ent = " ent %d ch %d" % (ec >> 3, ec & 7)
        EVENTS.append((GT(self, ctx), "sound", "%s%s" % (s(self, 291 + idx), ent)))


def svc_11(self, m, ctx):
    _svc11(self, m, ctx)
    lvl, txt = ctx["prints"][-1]
    EVENTS.append((GT(self, ctx), "print", txt.decode("latin-1", "replace").strip() if isinstance(txt, bytes) else txt))


LAST_PS = {}
SEEN = set()


def svc_23(self, m, ctx):
    _svc23(self, m, ctx)
    fr = ctx.get("frame")
    if fr in self.frames:
        ents, ps = self.frames[fr]
        st = ps["stats"]
        key = (st[1], st[15])
        if LAST_PS.get("k") != key:
            o = ps["origin"]
            EVENTS.append((GT(self, ctx), "ps", "health %d damagedir %#x at (%.0f %.0f %.0f) view %s" % (
                st[1], st[15], o[0] / 8, o[1] / 8, o[2] / 8, tuple(round(a * 360 / 65536) for a in ps["viewangles"]))))
            LAST_PS["k"] = key
        for num, e in ents.items():
            if num not in SEEN and e.get("renderfx", 0) & 0x400000:
                SEEN.add(num)
                EVENTS.append((GT(self, ctx), "new", "ent %d at (%.0f %.0f %.0f) yaw %.0f" % (
                    num, e["origin"][0], e["origin"][1], e["origin"][2], e["angles"][1])))


sofnet.Session.svc_1 = svc_1
sofnet.Session.svc_5 = svc_5
sofnet.Session.svc_10 = svc_10
sofnet.Session.svc_11 = svc_11
aicap.Tracker.svc_23 = svc_23

# Msg keeps its buffer as .b (sofnet); expose it as .data for the wrappers
_init = sofnet.Msg.__init__


def _msg_init(self, data, pos=0):
    _init(self, data, pos)
    self.data = data


sofnet.Msg.__init__ = _msg_init

# --- Ghoul instance states: part toggles (group 12), bolt-ons (group 13), skin overrides (group 14) by name ------
# A recording copy of sofnet.GhoulState.read_state (SPEC-013 section 4): same bit consumption, but group 12 also keeps
# the indices of the toggled parts ("g12i"), so the part state of every instance can be followed.
def read_state(self, b, obj):
    w0, w1, w2, w3 = obj["w"]
    st = {}
    st["f1"] = b.bit()
    if b.bit(): st["g2"] = b.bits(4)
    st["fA"] = b.bit()
    st["fB"] = b.bit()
    if w1 > 0 and b.bit(): st["g5"] = b.bits(w1)
    if w0 > 0 and b.bit(): st["g6"] = b.bits(w0)
    if b.bit(): st["g7"] = b.bits(2)
    if b.bit(): st["g8"] = (b.gtime(), b.gtime())
    if st["fA"] and b.bit():
        st["g9"] = 1e30 if b.bit() else b.float32()
    if b.bit():
        st["g10"] = [b.bits(8) if b.bit() else None for _ in range(4)]
    if b.bit():
        t = {}
        if b.bit(): t["rot"] = b.bits(32)
        if b.bit(): t["scale"] = b.float32()
        if b.bit():
            tr = []
            for ax in range(3):
                if b.bit():
                    tr.append(b.float32() if b.bit() == 0 else None)
                    if tr[-1] is None:
                        tr[-1] = b.bits(11) / 16.0 - 64
                else:
                    tr.append(None)
            t["trans"] = tr
        st["g11"] = t
    if b.bit():
        n = self.array_len(obj)
        k, idx = 0, []
        while k < n:
            if b.bit() == 0:
                for _ in range(min(4, n - k)):
                    if b.bit():
                        idx.append(k)
                    k += 1
            else:
                v = b.bit()
                c = b.bits(5)
                if v:
                    idx.extend(range(k, k + c + 6))
                k += c + 6
        if k != n:
            st["g12_overrun"] = (k, n)
        st["g12"] = len(idx)
        st["g12i"] = idx
    if b.bit():
        xs, ys = [], []
        while True:
            key = b.gid()
            if key == 0: break
            xs.append((key, b.bits(w1)))
        while True:
            key = b.gid()
            if key == 0: break
            ys.append((key, b.bits(w1)))
        st["g13"] = (xs, ys)
    if b.bit():
        add, rem = [], []
        while b.bit():
            add.append((b.bits(w2), b.bits(w1), b.bits(w3)))
        while b.bit():
            rem.append((b.bits(w2), b.bits(w1)))
        st["g14"] = (add, rem)
    return st


sofnet.GhoulState.read_state = read_state


def svc_27(self, m, ctx):
    """aicap.Tracker.svc_27 with the part-off set ("poff", part ids) and bolt-on map ("bmap", child -> bolt id) of
    every instance resolved per frame (XOR / add-remove against the base state, SPEC-013 groups 12 and 13)."""
    from collections import defaultdict as _dd  # noqa: F401
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
            ref = dict(aicap.DEFAULT)
            if obj is not None:
                bl = obj.get("baseline", {})
                ref.update(bl)
                ref["poff"] = frozenset(i + 1 for i in bl.get("g12i", []))
                ref["bmap"] = dict(bl.get("g13", ([], []))[0])
        else:
            ref = self.inst_states[iid].get(frame - base)
            if ref is None:
                ref = dict(aicap.DEFAULT)
                self.note("ghoul base frame missing", "inst %d frame %d base %d" % (iid, frame, base))
        new = dict(ref)
        new.update(st)
        poff = set(ref.get("poff", frozenset()))
        for i in st.get("g12i", []):
            poff ^= {i + 1}
        new["poff"] = frozenset(poff)
        bmap = dict(ref.get("bmap", {}))
        if "g13" in st:
            for c, bo in st["g13"][1]:
                bmap.pop(c, None)
            for c, bo in st["g13"][0]:
                bmap[c] = bo
        new["bmap"] = bmap
        new["_oid"] = oid
        self.inst_states[iid][frame] = new
        self.inst_obj[iid] = oid
        prev = PARTSTATE.get(iid)
        t = GT(self, ctx)
        if prev is not None and (prev[3] != oid or base == 0):
            prev = None  # instance id reused by a new instance
        if obj is not None and prev is None:
            nparts = len(obj["lists"][1])
            off = sorted(new["poff"])
            if len(off) > nparts // 2:
                desc = "shown:" + "".join(" " + name_of(self, obj, 1, i) for i in range(1, nparts + 1)
                                          if i not in new["poff"])
            else:
                desc = "hidden:" + "".join(" " + name_of(self, obj, 1, i) for i in off)
            seq = name_of(self, obj, 0, 1).rsplit("/", 2)
            EVENTS.append((t, "newinst", "inst %d (%s) %s%s" % (iid, "/".join(seq[:-1]), desc, "".join(
                " bolt %d@%s" % (c, name_of(self, obj, 1, b)) for c, b in bmap.items()))))
        elif obj is not None:
            on = sorted(prev[0] - new["poff"])
            off = sorted(new["poff"] - prev[0])
            if on or off:
                EVENTS.append((t, "parts", "inst %d%s" % (iid, "".join(
                    [" +" + name_of(self, obj, 1, i) for i in on] + [" -" + name_of(self, obj, 1, i) for i in off]))))
            added = [c for c in bmap if c not in prev[1]]
            gone = [c for c in prev[1] if c not in bmap]
            if added or gone:
                txt = ["+%d@%s" % (c, name_of(self, obj, 1, bmap[c])) for c in added] + ["-%d" % c for c in gone]
                EVENTS.append((t, "bolts", "inst %d %s" % (iid, " ".join(txt))))
        if new.get("f1") != (prev[2] if prev else 1):
            EVENTS.append((t, "inst", "inst %d %s" % (iid, "on" if new.get("f1") else "off")))
        PARTSTATE[iid] = (new["poff"], bmap, new.get("f1"), oid)
        for k in [k for k in self.inst_states[iid] if k < frame - 40]:
            del self.inst_states[iid][k]


PARTSTATE = {}
aicap.Tracker.svc_27 = svc_27


def name_of(sess, obj, li, idx):
    try:
        si = obj["lists"][li][idx - 1]  # Ghoul ids are 1-based
        if isinstance(si, tuple):
            si = si[1]
        return sess.ghoul.strings[si].decode("latin-1")
    except Exception:  # noqa: BLE001
        return "#%d" % idx


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--from", dest="t0", type=float, default=0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args(argv)
    sess = aicap.decode(a.capture)
    # skin overrides per instance from the decoded unreliable updates are kept by aicap as states; collect g14 adds
    skins = []
    for iid, frames in sess.inst_states.items():
        last = None
        for fr in sorted(frames):
            stt = frames[fr]
            g14 = stt.get("g14")
            if g14 and g14 != last:
                oid = sess.inst_obj.get(iid)
                obj = sess.ghoul.objects.get(oid)
                if obj:
                    adds = ["%s:%s" % (name_of(sess, obj, 3, mt), name_of(sess, obj, 4, sk)) for (mt, pt, sk) in g14[0]]
                    if adds:
                        skins.append((fr, iid, adds))
                last = g14
    ev = sorted(EVENTS, key=lambda e: e[0])
    t_first = 0.0
    if a.summary:
        c = Counter((k, txt.split(" ent")[0]) for (t, k, txt) in ev if a.t0 <= t - t_first <= a.t1 and k != "ps")
        for (k, txt), n in sorted(c.items()):
            print("%-7s %4d  %s" % (k, n, txt))
    else:
        for (t, k, txt) in ev:
            if a.t0 <= t - t_first <= a.t1:
                print("%8.2f %-7s %s" % (t - t_first, k, txt))
    for fr, iid, adds in skins[:200]:
        print("frame %d inst %d skins+ %s" % (fr, iid, " ".join(adds)))
    for num, d in aicap.summarise(sess).items():
        print("ent %d inst %s seqs %s" % (num, d["inst"], " ".join("%s@%.1f" % (q[1].rsplit("/", 1)[-1], q[0])
                                                                   for q in d["seqs"])))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
