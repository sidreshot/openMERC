#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - runs the original and OpenMerc's gamex86.dll on a list of maps (runtest.py) and tabulates the
# capdiff.py difference counts per map, server exits, crash logs and placeholder classnames.
# Implements: (test tooling, no spec)
#
# usage: mapsuite.py --dll <om dll> [--maps dm|sp|all|m1,m2,...] [--seconds 5] [--force-orig] [--tag suite]
#                    [--script "..."]
# Output: <openmerc-data>/g18/<tag>/summary.json and summary.txt; per-map runs in <tag>_orig_<map>/<tag>_om_<map>.

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import capdiff  # noqa: E402
import runtest  # noqa: E402

DM_MAPS = ("airportdm factorydm gercon1 gerctb1 gerctf1 irqcon1 irqctb1 irqctb2 irqctb3 irqctf1 irqdesertdm irqdm1 "
           "irqdm2 jpncon1 jpnctb1 jpnctf1 jpndm2 koscon1 koscon2 kosctb1 kosdm1 labdm nycctb1 nycctb2 nycctf1 nycdm1 "
           "nycdm2 nycdm3 nycdm4 nycdm5 plantdm raven sibcon1 sibctb1 sibctb2 sibctf1 sudcon1 sudctb1 sudctf1 sudctf2 "
           "suddm1 suddm2 suddm3").split()
SP_MAPS = ("arm1 arm2 arm3 ger1 ger2 ger3 ger4 irq1a irq1b irq2a irq2b irq3a irq3b jpn1 jpn2 jpn3 kos1 kos2 kos3 "
           "nyc1 nyc2 nyc3 sib1 sib2 sib3 sud1 sud2 sud3 trn1 tsr1 tsr2 tut1").split()


def run(dll, mp, tag, seconds, script, port, dm):
    args = [sys.executable, str(HERE / "runtest.py"), "--dll", dll, "--map", mp, "--tag", tag, "--seconds",
            str(seconds), "--port", str(port), "--dm", str(dm)]
    if script:
        args += ["--script", script]
    if not dm:
        args += ["--sp"]  # single player on a dedicated server needs G6b's proxy (runtest.py --sp)
    r = subprocess.run(args, capture_output=True, text=True, timeout=seconds + 120)
    out = r.stdout + r.stderr
    return {"exited": "EXITED early" in out, "client": out.strip().splitlines()[0] if out.strip() else ""}


def placeholders(log):
    if not log.exists():
        return []
    return sorted(set(re.findall(r"G18 placeholder: (\S+)", log.read_text(encoding="latin-1", errors="replace"))))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", required=True)
    ap.add_argument("--maps", default="dm")
    ap.add_argument("--seconds", type=float, default=5)
    ap.add_argument("--force-orig", action="store_true")
    ap.add_argument("--tag", default="suite")
    ap.add_argument("--script", default="1:move 200 0 0;2.5:move 0 0 0;3:buttons 1;3.5:buttons 0")
    ap.add_argument("--port", type=int, default=29450)  # G18 port base (testing notes "Network ports")
    a = ap.parse_args(argv)
    if a.maps == "dm":
        maps = ["dm/" + m for m in DM_MAPS]
    elif a.maps == "sp":
        maps = SP_MAPS
    elif a.maps == "all":
        maps = ["dm/" + m for m in DM_MAPS] + SP_MAPS
    else:
        maps = a.maps.split(",")
    outdir = runtest.DATA / a.tag
    outdir.mkdir(parents=True, exist_ok=True)
    summary = {}
    for mp in maps:
        name = mp.replace("/", "_")
        dm = 1 if mp.startswith("dm/") else 0
        to, tb = "%s_orig_%s" % (a.tag, name), "%s_om_%s" % (a.tag, name)
        res = {}
        if a.force_orig or not (runtest.DATA / to / "cap.jsonl").exists():
            res["orig"] = run("orig", mp, to, a.seconds, a.script, a.port, dm)
        res["om"] = run(a.dll, mp, tb, a.seconds, a.script, a.port, dm)
        crash = runtest.DATA / tb / "g18crash.log"
        res["crash"] = crash.read_text()[:400] if crash.exists() else ""
        res["placeholders"] = placeholders(runtest.DATA / tb / "sof.log")
        try:
            sa, _ = capdiff.load(runtest.DATA / to)
            sb, _ = capdiff.load(runtest.DATA / tb)
            rep = capdiff.Report(False)
            capdiff.compare(sa, sb, rep)
            res["diff"] = rep.summary()
            res["counts"] = {"cs": [len(sa.cs), len(sb.cs)], "baselines": [len(sa.baselines), len(sb.baselines)],
                             "frames": [len(sa.frames_seen), len(sb.frames_seen)]}
        except Exception as ex:  # noqa: BLE001
            res["diff"] = {"error": "%s: %s" % (type(ex).__name__, ex)}
        summary[mp] = res
        d = res["diff"]
        line = "%-18s %s" % (mp, " ".join("%s=%s" % (k.split(" (")[0].split(";")[0][:22], v) for k, v in d.items()))
        if res["om"]["exited"]:
            line += "  OM EXITED"
        print(line, flush=True)
        (outdir / "summary.json").write_text(json.dumps(summary, indent=1))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
