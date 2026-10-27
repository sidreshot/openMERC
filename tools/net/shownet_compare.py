#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - compare sofnet.py's message split of a capture with the original client's own
# `cl_shownet 2` console log (black-box cross-check of message boundaries).
# Implements: SPEC-201
#
# usage: shownet_compare.py <capture.jsonl> <client sof.log> [first log line]

import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sofnet  # noqa: E402


def log_packets(path, first_line=1):
    pk, cur = [], []
    for n, line in enumerate(open(path, encoding="latin-1"), 1):
        if n < first_line:
            continue
        m = re.match(r"^\s*(\d+):(svc_\w+|TOTAL PACKET)\s*$", line)
        if not m:
            continue
        ln, name = int(m.group(1)), m.group(2)
        if name == "TOTAL PACKET":
            pk.append((ln, cur))
            cur = []
        else:
            cur.append((ln, name))
    return pk


def my_packets(cap):
    import json
    out = []
    sess = sofnet.Session()
    for line in open(cap):
        r = json.loads(line)
        if r["dir"] != "s2c":
            continue
        data = bytes.fromhex(r["hex"])
        if data[:4] == b"\xff\xff\xff\xff":
            if data[4:].startswith(b"client_connect"):
                sess.new_connection()
            continue
        if len(data) == 8:
            continue  # header only: the client prints nothing for an empty payload
        ctx = {"t": r["t"], "seq": 0}
        m = sofnet.Msg(data, 8)
        names = sess.parse_svc(m, ctx)
        spans = ctx["spans"]
        out.append((len(data), [(b - a, n) for n, a, b in spans]))
    return out


def main(argv):
    cap, log = argv[0], argv[1]
    first = int(argv[2]) if len(argv) > 2 else 1
    lp = log_packets(log, first)
    mp = my_packets(cap)
    print("client log packets: %d, decoder packets with payload: %d" % (len(lp), len(mp)))
    same_names = same_total = 0
    shown = 0
    for i, (a, b) in enumerate(zip(lp, mp)):
        an = [n for _, n in a[1] if n not in ("svc_playerinfo", "svc_packetentities")]
        bn = [n for _, n in b[1]]
        if an == bn:
            same_names += 1
        elif shown < 5:
            shown += 1
            print("packet %d names differ:\n  log %s\n  dec %s" % (i, an, bn))
        if a[0] == b[0]:
            same_total += 1
        elif shown < 10:
            shown += 1
            print("packet %d total differs: log %d decoder %d" % (i, a[0], b[0]))
    n = min(len(lp), len(mp))
    print("same message names: %d/%d; same total length: %d/%d" % (same_names, n, same_total, n))
    # per-message lengths for messages outside frames
    eq = tot = 0
    diffs = {}
    for a, b in zip(lp, mp):
        al = [(l, nm) for l, nm in a[1] if nm not in ("svc_playerinfo", "svc_packetentities")]
        for (l1, n1), (l2, n2) in zip(al, b[1]):
            if n1 != n2:
                break
            tot += 1
            if l1 == l2:
                eq += 1
            else:
                diffs.setdefault(n1, set()).add(l1 - l2)
    print("per-message length equal: %d/%d; differences by message (log - decoder): %s" % (
        eq, tot, {k: sorted(v) for k, v in diffs.items()}))


if __name__ == "__main__":
    main(sys.argv[1:])
