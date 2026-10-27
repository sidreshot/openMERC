#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - registration suite: every single-player map is started with the original game module and with
# OpenMerc's (g4sp.py, single player through spshim), the client stays a few seconds, and the Ghoul model objects
# the client received (model file, sequence count, registered skins: SPEC-730 §3-§4 level models and skin
# registration, accessory and weapon bolt-on objects of every character on the map, helicopter/vehicle models) are
# compared as multisets (g4cap.py). Results: <openmerc-data>/g4/<prefix>_{orig,om}_<map>/ and a summary.
# Implements: SPEC-730 §3-§4, SPEC-731 §4 / Part B §2, SPEC-732 (black-box checks)
#
# usage: g4suite.py [--maps all|m1,m2] [--prefix reg] [--seconds 3] [--force-orig] [--only orig|om]
#                   [--set "cvar value"]...   (e.g. --set "fs_createpak 1": every random accessory is attached)
import argparse
import io
import json
import sys
from contextlib import redirect_stdout
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import g4sp  # noqa: E402
import g4cap  # noqa: E402

SP_MAPS = ("tut1 tsr1 tsr2 trn1 kos1 kos2 kos3 sib1 sib2 sib3 irq1a irq2a irq3a nyc1 nyc2 nyc3 sud1 sud2 sud3 "
           "jpn1 jpn2 jpn3 irq1b irq2b irq3b ger1 ger2 ger3 ger4 arm1 arm2 arm3").split()


def run(dll, m, tag, seconds, sets=()):
    cap = g4sp.DATA / tag / "cap.jsonl"
    extra = []
    for s in sets:
        extra += ["--set", s]
    rc = g4sp.main(["--dll", dll, "--map", m, "--sp", "--tag", tag, "--seconds", str(seconds)] + extra)
    g4sp.kill_own()
    return rc == 0 and cap.exists()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--maps", default="all")
    ap.add_argument("--prefix", default="reg")
    ap.add_argument("--seconds", type=float, default=3)
    ap.add_argument("--force-orig", action="store_true")
    ap.add_argument("--only", default="")
    ap.add_argument("--set", action="append", default=[])
    a = ap.parse_args(argv)
    maps = SP_MAPS if a.maps == "all" else a.maps.split(",")
    summary = {}
    for m in maps:
        to, tb = "%s_orig_%s" % (a.prefix, m), "%s_om_%s" % (a.prefix, m)
        if a.only != "om" and (a.force_orig or not (g4sp.DATA / to / "cap.jsonl").exists()):
            run("orig", m, to, a.seconds, a.set)
        if a.only != "orig":
            run("om", m, tb, a.seconds, a.set)
        ca, cb = g4sp.DATA / to / "cap.jsonl", g4sp.DATA / tb / "cap.jsonl"
        if not (ca.exists() and cb.exists()):
            summary[m] = "missing capture"
            print(m, "missing capture", flush=True)
            continue
        buf = io.StringIO()
        with redirect_stdout(buf):
            rc = g4cap.cmd_diff(str(ca), str(cb))
        text = buf.getvalue()
        (g4sp.DATA / tb / "objdiff.txt").write_text(text, encoding="utf-8")
        summary[m] = text.splitlines()[0] if text else "?"
        print(m, summary[m], "(identical)" if rc == 0 else "", flush=True)
    (g4sp.DATA / ("%s_summary.json" % a.prefix)).write_text(json.dumps(summary, indent=1), encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
