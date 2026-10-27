#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - list the messages of a client demo (.dm2) with prints, string-package prints and stuffed text.
# Implements: SPEC-311 (demo file layout: int32 length + message, -1 ends), SPEC-201 (message ids)
#
# usage: demoprints.py FILE.dm2 [--all]

import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402


def blocks(path):
    raw = open(path, "rb").read()
    p = 0
    while p + 4 <= len(raw):
        n = struct.unpack_from("<i", raw, p)[0]
        p += 4
        if n < 0 or p + n > len(raw):
            break
        yield raw[p:p + n]
        p += n


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("demo")
    ap.add_argument("--all", action="store_true")
    a = ap.parse_args(argv)
    sess = sofnet.Session()
    kinds = Counter()
    for i, b in enumerate(blocks(a.demo)):
        m = sofnet.Msg(b)
        ctx = {"t": i, "seq": i, "dir": "s2c"}
        try:
            names = sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001
            names = [s[0] for s in ctx.get("spans", [])] + ["ERROR %s" % ex]
        kinds.update(names)
        for lvl, s in ctx.get("prints", []):
            print("block %d print [%s] %r" % (i, lvl, s))
        for name, s, e in ctx.get("spans", []):
            if name in ("svc_sp_print", "svc_stufftext", "svc_print", "svc_restart_predn", "svc_rebuild_pred_inv"):
                print("block %d %s %s" % (i, name, b[s:min(e, s + 32)].hex()))
        if a.all:
            print("block %d: %s" % (i, " ".join(names)))
    print(dict(kinds))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
