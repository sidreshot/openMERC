#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - list the server messages of selected packets of a capture (cap.jsonl from game/test/sofclient.py),
# with a hex dump of the bytes where decoding stopped.
# Implements: SPEC-200 (netchan header), SPEC-201 (message ids)
#
# usage: capsvc.py cap.jsonl [--seq 27] [--first N]

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cap")
    ap.add_argument("--seq", type=int, action="append", default=[])
    ap.add_argument("--first", type=int, default=0, help="list the first N sequenced server packets")
    a = ap.parse_args(argv)
    sess = sofnet.Session()
    shown = 0
    for line in open(a.cap):
        r = json.loads(line)
        data = bytes.fromhex(r["hex"])
        if r["dir"] != "s2c":
            continue
        if data[:4] == b"\xff\xff\xff\xff":
            txt = data[4:].split(b"\n")[0]
            if txt.startswith(b"client_connect"):
                sess.new_connection()
            continue
        m = sofnet.Msg(data)
        w1, w2 = m.ulong(), m.ulong()
        seq = w1 & 0x7FFFFFFF
        ctx = {"t": r["t"], "seq": seq, "dir": "s2c"}
        err = None
        try:
            names = sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001
            err = ex
            names = [s[0] for s in ctx.get("spans", [])]
        want = seq in a.seq or (a.first and shown < a.first) or (err and not a.seq)
        if want:
            shown += 1
            print("seq %d t %.2f len %d rel %d: %s%s" % (seq, r["t"], len(data), w1 >> 31, " ".join(names),
                                                       "  ERROR %s" % err if err else ""))
            for name, s, e in ctx.get("spans", []):
                if name in ("svc_restart_predn", "svc_rebuild_pred_inv", "svc_stufftext", "svc_print"):
                    print("   %s @%d..%d %s" % (name, s, e, data[s:min(e, s + 24)].hex()))
            if err:
                p = ctx.get("spans", [(None, 0, 8)])[-1][2] if ctx.get("spans") else 8
                print("   stop @%d: %s" % (p, data[p:p + 48].hex()))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
