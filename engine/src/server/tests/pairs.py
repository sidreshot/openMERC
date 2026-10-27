#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - side-by-side pictures of two sweep runs (sweep.ps1): for every map that both runs captured, one
# PNG per capture name with the first run left and the second right, for looking at them together.
# Implements: test harness for SPEC-001 / SPEC-003 (black-box comparison, no game code)
#
# usage: pairs.py <prefixA> <prefixB> <outdir> [--maps kos1,kos2] [--shots m1,m2]
import argparse
import sys
from pathlib import Path

from PIL import Image, ImageDraw

DATA = Path(r"C:\openmerc-data\i1b")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("out")
    ap.add_argument("--maps", default="")
    ap.add_argument("--shots", default="m0,m1,m2")
    a = ap.parse_args(argv)
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    maps = [m for m in a.maps.split(",") if m] or sorted(
        p.name[len(a.a) + 1:] for p in DATA.glob(a.a + "_*") if p.is_dir())
    n = 0
    for m in maps:
        for s in a.shots.split(","):
            pa, pb = DATA / ("%s_%s" % (a.a, m)) / (s + ".png"), DATA / ("%s_%s" % (a.b, m)) / (s + ".png")
            if not (pa.exists() and pb.exists()):
                continue
            ia, ib = Image.open(pa).convert("RGB"), Image.open(pb).convert("RGB")
            w, h = max(ia.width, ib.width), max(ia.height, ib.height)
            im = Image.new("RGB", (w * 2 + 4, h + 16), (40, 40, 40))
            im.paste(ia, (0, 16))
            im.paste(ib, (w + 4, 16))
            d = ImageDraw.Draw(im)
            d.text((4, 2), "%s %s: %s" % (m, s, a.a), fill=(255, 255, 0))
            d.text((w + 8, 2), a.b, fill=(255, 255, 0))
            im.save(out / ("%s_%s.png" % (m, s)))
            n += 1
    print("%d pairs in %s" % (n, out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
