#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - aggregate the capdiff details of a mapsuite run: every difference line (configstring region sets,
# entities, player state, message kinds, temp entities, Ghoul strings) with the number of maps it occurs on.
# Implements: (test tooling, no spec)
#
# usage: suiteagg.py <suite tag> [--section configstrings] [--maps m1,m2] [--min 1]

import argparse
import re
import sys
from collections import defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import capdiff  # noqa: E402

DATA = Path(r"C:\openmerc-data\g18")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("--section", default="", help="only sections whose name contains this text")
    ap.add_argument("--maps", default="")
    ap.add_argument("--min", type=int, default=1)
    ap.add_argument("--skip-index", action="store_true", default=True)
    a = ap.parse_args(argv)
    runs = sorted(DATA.glob(a.tag + "_om_*"))
    seen = defaultdict(list)
    nmaps = 0
    for rb in runs:
        name = rb.name[len(a.tag) + 4:]
        if a.maps and name not in a.maps.split(","):
            continue
        ra = DATA / (a.tag + "_orig_" + name)
        if not (ra / "cap.jsonl").exists() or not (rb / "cap.jsonl").exists():
            continue
        try:
            sa, _ = capdiff.load(ra)
            sb, _ = capdiff.load(rb)
            rep = capdiff.Report(True)
            capdiff.compare(sa, sb, rep)
        except Exception as ex:  # noqa: BLE001
            print("%s: %s" % (name, ex))
            continue
        nmaps += 1
        for sec, _, lines in rep.sections:
            if sec == "configstrings by index" and a.skip_index:
                continue
            if a.section and a.section not in sec:
                continue
            short = re.sub(r" \(.*", "", sec.split(";")[0])
            for l in lines:
                # entity lines carry numbers that differ per map; keep them as they are
                seen[(short, l)].append(name)
    print("%d maps" % nmaps)
    for (sec, l), maps in sorted(seen.items(), key=lambda kv: (kv[0][0], -len(kv[1]), kv[0][1])):
        if len(maps) >= a.min:
            print("%3d  %-22s %s%s" % (len(maps), sec[:22], l[:150],
                                       "" if len(maps) > 3 else "   [" + ",".join(maps) + "]"))


if __name__ == "__main__":
    main(sys.argv[1:])
