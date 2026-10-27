#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - per Ghoul entity, what the client sees at a moment of a capture: model, current sequence, skin
# overrides, the bolt-on tree (child models on named bolts, recursively) and part flips; and a side-by-side
# comparison of two captures (original game module vs OpenMerc) for equal entity numbers.
# Builds on G2's aicap.Tracker; the bolt-on map and the skin overrides are add/remove deltas (SPEC-013 §3).
# Implements: SPEC-013 (reader side), SPEC-202 (entity frame = Ghoul instance id); checks for SPEC-730/731/732
#
# usage: g4ents.py ents <cap.jsonl> [--time T] [--class TEXT]
#        g4ents.py diff <capA.jsonl> <capB.jsonl> [--time T] [--class TEXT] [--byorigin]
#        g4ents.py multiset <capA.jsonl> <capB.jsonl> [--time T] [--class TEXT]   trees as a multiset
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "tools" / "net"))
sys.path.insert(0, str(REPO / "game" / "src" / "ai" / "tools"))
import sofnet  # noqa: E402
import aicap  # noqa: E402


def read_state_masked(self, b, obj):
    """sofnet.GhoulState.read_state, also keeping the part-flag XOR mask of group 12 (SPEC-013 §3) as g12m."""
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
        mask = []
        while len(mask) < n:
            if b.bit() == 0:
                for _ in range(min(4, n - len(mask))):
                    mask.append(b.bit())
            else:
                v = b.bit()
                c = b.bits(5)
                mask += [v] * (c + 6)
        st["g12"] = sum(mask)
        st["g12m"] = mask[:n]
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


sofnet.GhoulState.read_state = read_state_masked


def apply_parts(state, st, nparts):
    cur = state.get("_parts")
    if cur is None or len(cur) != nparts:
        cur = [1] * nparts
    if "g12m" in st:
        cur = [c ^ m for c, m in zip(cur, st["g12m"] + [0] * (nparts - len(st["g12m"])))]
    state["_parts"] = cur


def apply_maps(state, st):
    if "g13" in st:
        add, rem = st["g13"]
        for child, bolt in rem:
            state["_bolts"].pop(child, None)
        for child, bolt in add:
            state["_bolts"][child] = bolt
    if "g14" in st:
        add, rem = st["g14"]
        for mat, part in rem:
            state["_skins"].pop((mat, part), None)
        for mat, part, skin in add:
            state["_skins"][(mat, part)] = skin


class Resolver(aicap.Tracker):
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
            ref = None
            if base:
                ref = self.inst_states[iid].get(frame - base)
            if ref is None:
                ref = dict(aicap.DEFAULT)
                ref["_bolts"], ref["_skins"] = {}, {}
                if obj is not None:
                    bl = obj.get("baseline", {})
                    ref.update({k: v for k, v in bl.items() if k not in ("g13", "g14", "g12m")})
                    apply_maps(ref, bl)
                    apply_parts(ref, bl, len(obj["lists"][1]))
            new = dict(ref)
            new["_bolts"] = dict(ref.get("_bolts", {}))
            new["_skins"] = dict(ref.get("_skins", {}))
            new.update({k: v for k, v in st.items() if k not in ("g13", "g14", "g12m")})
            apply_maps(new, st)
            if "g11" in st and "rot" in st["g11"]:
                new["_rot"] = st["g11"]["rot"]
            if obj is not None:
                new["_parts"] = list(ref.get("_parts") or [1] * len(obj["lists"][1]))
                apply_parts(new, st, len(obj["lists"][1]))
            new["_oid"] = oid
            self.inst_states[iid][frame] = new
            self.inst_obj[iid] = oid
            for k in [k for k in self.inst_states[iid] if k < frame - 4000]:
                del self.inst_states[iid][k]


def resolve(path):
    sess = Resolver()
    for line in open(path, encoding="utf-8"):
        r = json.loads(line)
        if r["dir"] != "s2c":
            continue
        data = bytes.fromhex(r["hex"])
        if data[:4] == b"\xff\xff\xff\xff":
            if data[4:].startswith(b"client_connect"):
                sess.new_connection()
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


def describe(sess, when=None):
    S = sess.ghoul.strings

    def s(i):
        i &= 0xFFFF
        return S[i].decode("latin-1") if i < len(S) else "?%d" % i
    if not sess.timeline:
        return {}
    tl = sess.timeline
    entry = tl[-1]
    if when is not None:
        cand = [x for x in tl if x["time"] <= when]
        entry = cand[-1] if cand else tl[0]
    frame = entry["frame"]

    def state(iid):
        st = sess.inst_states.get(iid)
        if not st:
            return None
        ks = [k for k in st if k <= frame]
        return st[max(ks)] if ks else None

    def inst_desc(iid, depth=0):
        st = state(iid)
        if st is None or depth > 6:
            return {"model": "?", "skins": [], "bolts": [], "seq": None, "hidden": None}
        obj = sess.ghoul.objects.get(st.get("_oid"))
        if obj is None:
            return {"model": "?obj", "skins": [], "bolts": [], "seq": None, "hidden": None}
        lists = obj["lists"]
        mats = [s(x) for x in lists[3]]
        parts = [s(x) for x in lists[1]]
        skins = [s(v) for e, v in lists[4]]
        model = s(lists[0][0]).split("/")[-1] if lists[0] else "-"
        sk = []
        for (mat, part), skin in st.get("_skins", {}).items():
            mname = mats[mat - 1] if 0 < mat <= len(mats) else str(mat)
            sname = skins[skin - 1] if 0 < skin <= len(skins) else str(skin)
            pname = "" if not part else "@" + (parts[part - 1] if part <= len(parts) else str(part))
            sk.append("%s=%s%s" % (mname, sname, pname))
        bolts = []
        for child, bolt in st.get("_bolts", {}).items():
            bolts.append((parts[bolt - 1] if 0 < bolt <= len(parts) else str(bolt), inst_desc(child, depth + 1)))
        sid = st.get("g6", 0)
        seq = s(lists[0][sid - 1]).split("/")[-1] if 0 < sid <= len(lists[0]) else None
        flags = st.get("_parts") or [1] * len(parts)
        hidden = sorted(parts[i] for i in range(min(len(parts), len(flags))) if not flags[i])
        return {"model": model, "skins": sorted(sk), "bolts": sorted(bolts, key=lambda b: (b[0], b[1]["model"])),
                "seq": seq, "mode": st.get("g7", 0), "hidden": hidden, "rot": st.get("_rot")}
    out = {}
    for num, e in sorted(entry["ents"].items()):
        if not (e.get("renderfx", 0) & 0x400000):
            continue
        iid = e.get("frame", 0)
        if iid:
            d = inst_desc(iid)
        else:
            d = {"model": "(no instance)", "skins": [], "bolts": [], "seq": None, "hidden": None}
        d["origin"] = tuple(round(x) for x in e["origin"])
        d["angles"] = tuple(round(x, 1) for x in e["angles"])
        out[num] = d
    return out


def fmt(d, ind=""):
    rot = d.get("rot")
    lines = ["%s%s seq=%s/%s%s skins: %s" % (ind, d["model"], d.get("seq"), d.get("mode"),
                                            "" if rot is None else " rot=%08x" % rot, " ".join(d["skins"]))]
    if d.get("hidden"):
        lines.append("%s  hidden parts: %s" % (ind, " ".join(d["hidden"])))
    for bolt, cd in d["bolts"]:
        lines.append("%s  @%s:" % (ind, bolt))
        lines += fmt(cd, ind + "    ")
    return lines


def strip(d):
    return {"model": d["model"], "skins": d["skins"], "hidden": d.get("hidden"),
            "bolts": [(b, strip(c)) for b, c in d["bolts"]]}


def main(argv):
    when = float(argv[argv.index("--time") + 1]) if "--time" in argv else None
    cls = argv[argv.index("--class") + 1].lower() if "--class" in argv else None
    if argv[0] == "ents":
        for num, d in describe(resolve(argv[1]), when).items():
            if cls and cls not in d["model"].lower():
                continue
            print("ent %d origin %s angles %s" % (num, d["origin"], d["angles"]))
            print("\n".join(fmt(d, "  ")))
        return 0
    if argv[0] == "multiset":
        # trees as a multiset (entity numbers and positions ignored): identical trees, then the unmatched ones
        da = describe(resolve(argv[1]), when)
        db = describe(resolve(argv[2]), when)
        fa = [x for x in da.values() if not cls or cls in x["model"].lower()]
        fb = [y for y in db.values() if not cls or cls in y["model"].lower()]
        rest = list(fb)
        same = 0
        left = []
        for x in fa:
            for i, y in enumerate(rest):
                if strip(x) == strip(y):
                    same += 1
                    del rest[i]
                    break
            else:
                left.append(x)
        for x in left:
            print("only A:")
            print("\n".join(fmt(x, "   ")))
        for y in rest:
            print("only B:")
            print("\n".join(fmt(y, "   ")))
        print("identical trees: %d (A %d, B %d)" % (same, len(fa), len(fb)))
        return 0
    if argv[0] == "diff":
        da = describe(resolve(argv[1]), when)
        db = describe(resolve(argv[2]), when)
        same = total = 0
        pairs = {}
        if "--byorigin" in argv:
            # pair entities by model and origin (entity numbers may be shifted by other packages' entities)
            used = set()
            for na, x in sorted(da.items()):
                for nb, y in sorted(db.items()):
                    if nb not in used and y["model"] == x["model"] and                             max(abs(a - b) for a, b in zip(x["origin"], y["origin"])) <= 2:
                        pairs[na] = nb
                        used.add(nb)
                        break
            db = {na: db[nb] for na, nb in pairs.items()} | {("B%d" % nb): y for nb, y in db.items() if nb not in used}
            da = {na: x for na, x in da.items()}
        for num in sorted(set(da) | set(db), key=str):
            x, y = da.get(num), db.get(num)
            if cls and not any(cls in z["model"].lower() for z in (x, y) if z):
                continue
            total += 1
            if x and y and strip(x) == strip(y):
                same += 1
                continue
            print("== ent %s" % num)
            print("  A:\n" + ("\n".join(fmt(x, "     ")) if x else "     (none)"))
            print("  B:\n" + ("\n".join(fmt(y, "     ")) if y else "     (none)"))
        print("entities with identical model, skins and bolt-on tree: %d of %d" % (same, total))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
