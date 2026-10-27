#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - summarise the Ghoul objects (model files, registered skins) and Ghoul instance states (skin
# overrides, bolt-ons, hidden parts) a client received in a capture of game/test/sofclient.py, for comparing
# the original game module with OpenMerc's on the same map.
# Implements: SPEC-013 (reader side of the object definitions and instance states), SPEC-730/731/732 checks
#
# usage: g4cap.py objects <cap.jsonl> [--filter TEXT]
#        g4cap.py diff <capA.jsonl> <capB.jsonl>      multiset comparison of (model, skin set) objects
#        g4cap.py order <capA.jsonl> <capB.jsonl> [--filter TEXT]  objects equal as sets whose skin registration
#                                                     order differs (the order gives the skin indices on the wire)
import sys
import collections
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402


def load(path):
    sess, chans, totals, oob, bad, q = sofnet.decode_capture(path)
    S = sess.ghoul.strings

    def s(i):
        i &= 0xFFFF
        return S[i].decode("latin-1") if i < len(S) else "?%d" % i
    objs = {}
    for oid, o in sorted(sess.ghoul.objects.items()):
        l = o["lists"]
        mats = [s(x) for x in l[3]]
        cur = None
        skins = []
        for e, v in l[4]:
            if e is not None:
                e &= 0xFFFF
                cur = mats[e - 1] if 0 < e <= len(mats) else "mat%d" % e
            skins.append("%s=%s" % (cur, s(v)))
        model = s(l[0][0]) if l[0] else "-"
        objs[oid] = {"model": model, "nseq": len(l[0]), "nparts": len(l[1]), "mats": mats, "skins": skins,
                     "parts": [s(x) for x in l[1]]}
    return sess, objs, bad


def cmd_objects(path, flt=None):
    sess, objs, bad = load(path)
    for oid, o in objs.items():
        if flt and flt.lower() not in o["model"].lower():
            continue
        print("%4d %-45s seq %3d parts %3d skins %s" % (oid, o["model"], o["nseq"], o["nparts"],
                                                         " ".join(o["skins"])))
    if bad:
        print("decode problems:", len(bad))


def key(o):
    return (o["model"], o["nseq"], tuple(sorted(o["skins"])))


def cmd_diff(a, b):
    _, oa, _ = load(a)
    _, ob, _ = load(b)
    ca = collections.Counter(key(o) for o in oa.values())
    cb = collections.Counter(key(o) for o in ob.values())
    only_a = ca - cb
    only_b = cb - ca
    print("objects: A %d, B %d, identical (model, sequences, skin set) %d" % (
        sum(ca.values()), sum(cb.values()), sum((ca & cb).values())))
    for k, n in sorted(only_a.items()):
        print("  only A x%d: %s seq %d skins %s" % (n, k[0], k[1], " ".join(k[2])))
    for k, n in sorted(only_b.items()):
        print("  only B x%d: %s seq %d skins %s" % (n, k[0], k[1], " ".join(k[2])))
    return 0 if not only_a and not only_b else 1


def cmd_order(a, b, flt=None):
    _, oa, _ = load(a)
    _, ob, _ = load(b)
    pool = {}
    for o in ob.values():
        pool.setdefault(key(o), []).append(o)
    n = bad = 0
    for o in oa.values():
        if flt and flt.lower() not in o["model"].lower():
            continue
        c = pool.get(key(o))
        if not c:
            continue
        p = c.pop(0)
        n += 1
        if p["skins"] != o["skins"]:
            bad += 1
            print("%s" % o["model"])
            print("  A: %s" % " ".join(o["skins"]))
            print("  B: %s" % " ".join(p["skins"]))
    print("objects equal as sets: %d, registration order differs: %d" % (n, bad))
    return 0 if not bad else 1


if __name__ == "__main__":
    if len(sys.argv) >= 3 and sys.argv[1] == "objects":
        cmd_objects(sys.argv[2], sys.argv[4] if len(sys.argv) > 4 and sys.argv[3] == "--filter" else None)
    elif len(sys.argv) >= 4 and sys.argv[1] == "order":
        sys.exit(cmd_order(sys.argv[2], sys.argv[3], sys.argv[5] if len(sys.argv) > 5 and sys.argv[4] == "--filter" else None))
    elif len(sys.argv) == 4 and sys.argv[1] == "diff":
        sys.exit(cmd_diff(sys.argv[2], sys.argv[3]))
    else:
        print(__doc__ if __doc__ else "usage: g4cap.py objects|diff ...")
        sys.exit(2)
