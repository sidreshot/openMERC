#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-400 / SPEC-401 black-box check (no game code)
#
# Compares what an original server sent to the OpenMerc client (udpproxy capture, decoded independently
# by tools/net/sofnet.py from the specs) with what the client's effects system handled (its log with
# om_fxtrace 1): temp entities per type, svc_effect and culled events must agree one to one. Also lists
# what the effects system produced (decals, sounds, puffs, effects by name).
#
#   python fxtrace_compare.py <capture.jsonl> <client sof.log>
import ast
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

SOFNET = Path(__file__).resolve().parents[5] / "tools" / "net" / "sofnet.py"


def decode(capture):
    out = subprocess.run([sys.executable, "-I", str(SOFNET), "decode", capture], capture_output=True,
                         text=True, encoding="latin-1").stdout
    te, svc, parsed = {}, {}, ""
    for line in out.splitlines():
        if line.startswith("temp entities: "):
            te = ast.literal_eval(line[len("temp entities: "):])
        elif line.startswith("svc counts: "):
            svc = ast.literal_eval(line[len("svc counts: "):])
        elif line.startswith("sequenced bytes fully parsed"):
            parsed = line
    return te, svc, parsed


def client(log):
    te, other, produced = Counter(), Counter(), Counter()
    effects = Counter()
    for line in open(log, encoding="latin-1"):
        m = re.match(r"svc_temp_entity (\d+)", line)
        if m:
            te[int(m.group(1))] += 1
            continue
        if line.startswith("svc_effect "):
            other["svc_effect"] += 1
            continue
        if line.startswith("culledEvent "):
            other["svc_culledEvent"] += 1
            continue
        m = re.match(r"event (\d+)", line)
        if m:
            other["entity event %s" % m.group(1)] += 1
            continue
        m = re.match(r"\s+effect '([^']*)' started", line)
        if m:
            effects[m.group(1)] += 1
            continue
        m = re.match(r"\s+(decal|sound|light|shake|dust puffs|concrete puffs|impact chunks|glass shards|"
                     r"soda cans|spark shower|blowing paper|lightning)\b", line)
        if m:
            produced[m.group(1)] += 1
    return te, other, produced, effects


def main():
    if len(sys.argv) < 3:
        print(__doc__ or "usage: fxtrace_compare.py <capture.jsonl> <client sof.log>")
        return 2
    te_cap, svc_cap, parsed = decode(sys.argv[1])
    te_cl, other_cl, produced, effects = client(sys.argv[2])
    print(parsed)
    print("%-26s %8s %8s  %s" % ("message", "captured", "handled", ""))
    bad = 0
    for t in sorted(set(te_cap) | set(te_cl)):
        a, b = te_cap.get(t, 0), te_cl.get(t, 0)
        bad += a != b
        print("%-26s %8d %8d  %s" % ("svc_temp_entity %d" % t, a, b, "OK" if a == b else "DIFF"))
    for k in ("svc_effect", "svc_culledEvent"):
        a, b = svc_cap.get(k, 0), other_cl.get(k, 0)
        bad += a != b
        print("%-26s %8d %8d  %s" % (k, a, b, "OK" if a == b else "DIFF"))
    print("entity events handled: %s" % dict(sorted((k, v) for k, v in other_cl.items() if k.startswith("entity"))))
    print("effects started by name/slot: %s" % dict(effects.most_common()))
    print("produced: %s" % dict(produced.most_common()))
    print("RESULT: %s" % ("all messages accounted for" if not bad else "%d differences" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
