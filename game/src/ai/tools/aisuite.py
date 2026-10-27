#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G2 - single-player maps with characters, original game module vs OpenMerc's, same scripted client:
# runs airun.py for each map and module (original captures are reused unless --force-orig), then compares what the
# client saw of the characters with aidiff.py. Summary per map in <openmerc-data>/g2/<tag>/summary.json.
# Implements: SPEC-700, SPEC-701, SPEC-710, SPEC-711 (black-box verification harness)
#
# usage: aisuite.py --maps tsr1,tsr2,... --tag NAME [--script idle|walk|"<sofclient script>"] [--seconds 60]
#                   [--force-orig] [--only om] [--set "cvar v"] [--copy b --port 29462]

import argparse
import json
import re
import subprocess
import sys
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
DATA = Path(r"C:\openmerc-data\g2")
SCRIPTS = {
    "idle": "",
    # walk forward from 10 s after begin for the rest of the run (kos1: into the first enemies' fire)
    "walk2": "10:move 100 0 0",
}


def run(dll, m, tag, script, seconds, sets, copy="", port=0):
    cmd = [sys.executable, str(HERE / "airun.py"), "--dll", dll, "--map", m, "--sp", "--tag", tag,
           "--seconds", str(seconds)]
    if copy:
        cmd += ["--copy", copy]
    if port:
        cmd += ["--port", str(port)]
    if script:
        cmd += ["--script", script]
    for s in sets:
        cmd += ["--set", s]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=seconds + 200)
    return (r.stdout + r.stderr).strip().splitlines()[-1:] or [""]


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--script", default="idle")
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--force-orig", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--copy", default="", help="test copy suffix (airun.py --copy), for a second suite in parallel")
    ap.add_argument("--port", type=int, default=0)
    a = ap.parse_args(argv)
    script = SCRIPTS.get(a.script, a.script)
    skey = a.script if a.script in SCRIPTS else "s%08x" % zlib.crc32(a.script.encode("utf-8"))
    out = DATA / a.tag
    out.mkdir(parents=True, exist_ok=True)
    summary = {}
    for m in [x for x in a.maps.split(",") if x]:
        otag, ntag = "orig_%s_%s" % (m, skey), "%s_%s" % (a.tag, m)
        if a.only != "om" and (a.force_orig or not (DATA / otag / "cap.jsonl").exists()):
            print(m, "orig:", run("orig", m, otag, script, a.seconds, [], a.copy, a.port), flush=True)
        if a.only != "orig":
            print(m, "om:", run("om", m, ntag, script, a.seconds, a.set, a.copy, a.port), flush=True)
        oc, nc = DATA / otag / "cap.jsonl", DATA / ntag / "cap.jsonl"
        if not (oc.exists() and nc.exists()):
            summary[m] = {"error": "capture missing"}
            continue
        r = subprocess.run([sys.executable, str(HERE / "aidiff.py"), str(oc), str(nc), "--until", str(a.seconds)],
                           capture_output=True, text=True)
        (out / ("%s.diff.txt" % m)).write_text(r.stdout, encoding="utf-8")
        s = re.search(r"summary: (\d+) characters, mean sequence match ([\d.]+), mean position difference ([\d.na]+)",
                      r.stdout)
        crash = (DATA / ntag / "sof.log").read_text(encoding="latin-1", errors="replace") if (
            DATA / ntag / "sof.log").exists() else ""
        summary[m] = {"chars": int(s.group(1)) if s else 0, "seq_match": float(s.group(2)) if s else 0.0,
                      "pos_diff": s.group(3) if s else "-",
                      "om_errors": len(re.findall(r"(?i)\berror\b|exception|crash", crash))}
        print(m, summary[m], flush=True)
    (out / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
