# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-801 black-box checks (test harness, no game code)
# Side-by-side pairs (original | OpenMerc) of the menu captures and a per-menu difference figure.
# usage: python pairs.py <shotdir> <tagA> <tagB> [outtag]
import os
import sys

from PIL import Image, ImageChops, ImageDraw


def main():
    d, a, b = sys.argv[1], sys.argv[2], sys.argv[3]
    out = sys.argv[4] if len(sys.argv) > 4 else "pair"
    names = sorted(f[len(a) + 1:-4] for f in os.listdir(d) if f.startswith(a + "_") and f.endswith(".png"))
    rows = []
    for n in names:
        pa = os.path.join(d, "%s_%s.png" % (a, n))
        pb = os.path.join(d, "%s_%s.png" % (b, n))
        if not os.path.exists(pb):
            rows.append((n, None, None))
            continue
        ia = Image.open(pa).convert("RGB")
        ib = Image.open(pb).convert("RGB")
        if ia.size != ib.size:
            ib = ib.resize(ia.size)
        diff = ImageChops.difference(ia, ib)
        hist = diff.convert("L").histogram()
        total = sum(hist)
        mean = sum(i * c for i, c in enumerate(hist)) / total
        same = hist[0] / total
        w, h = ia.size
        pair = Image.new("RGB", (w * 2 + 8, h + 20), (40, 40, 40))
        pair.paste(ia, (0, 20))
        pair.paste(ib, (w + 8, 20))
        dr = ImageDraw.Draw(pair)
        dr.text((4, 4), "%s: %s" % (a, n), fill=(255, 255, 0))
        dr.text((w + 12, 4), "%s   mean |diff| %.1f, identical %.1f%%" % (b, mean, same * 100), fill=(255, 255, 0))
        pair.save(os.path.join(d, "%s_%s.png" % (out, n)))
        rows.append((n, mean, same))
    for n, mean, same in rows:
        if mean is None:
            print("%-12s  (no %s capture)" % (n, b))
        else:
            print("%-12s  mean |diff| %6.2f   identical pixels %5.1f%%" % (n, mean, same * 100))


if __name__ == "__main__":
    main()
