# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-411 test harness (screenshot comparison restricted to Ghoul pixels)
#
# Every shot <name> of the scripts is taken twice in the same paused frame: <name> normally and
# <name>_nog with ghl_no_render 1. The pixels that differ between the two (in either renderer) are the
# pixels of Ghoul models and their shadows. For each pair <dir>/<a>_<name>.png / <dir>/<b>_<name>.png
# this prints the mean |difference| over those pixels, the share of them differing by more than 24, and
# writes <dir>/gcmp_<name>.png: original | OpenMerc | |difference| x4, cropped to the Ghoul pixels' box.
#
# usage: ghoulcmp.py <dir> <a_prefix> <b_prefix> [names,comma,separated]
import glob
import os
import sys

from PIL import Image, ImageChops


def load(path):
    return Image.open(path).convert('RGB')


def mask_of(img, nog):
    d = ImageChops.difference(img, nog).convert('L')
    return d.point(lambda v: 255 if v > 8 else 0)


def main():
    d, a, b = sys.argv[1:4]
    only = sys.argv[4].split(',') if len(sys.argv) > 4 else None
    for fa in sorted(glob.glob(os.path.join(d, a + '_*.png')), key=os.path.getmtime):
        name = os.path.basename(fa)[len(a) + 1:-4]
        if name.endswith('_nog') or (only and name not in only):
            continue
        fb = os.path.join(d, b + '_' + name + '.png')
        fan = os.path.join(d, a + '_' + name + '_nog.png')
        fbn = os.path.join(d, b + '_' + name + '_nog.png')
        if not all(os.path.exists(f) for f in (fb, fan, fbn)):
            continue
        ia, ib = load(fa), load(fb)
        m = ImageChops.lighter(mask_of(ia, load(fan)), mask_of(ib, load(fbn)))
        hist = m.histogram()
        n = hist[255]
        if not n:
            print('%-12s no Ghoul pixels' % name)
            continue
        diff = ImageChops.difference(ia, ib).convert('L')
        dm = ImageChops.multiply(diff, m)  # m is 0 or 255
        dh = dm.histogram()
        mean = sum(i * c for i, c in enumerate(dh)) / n
        big = sum(dh[25:]) / n
        print('%-12s ghoul pixels %6d  mean |diff| %5.2f  > 24: %5.1f%%' % (name, n, mean, big * 100))
        box = m.getbbox()
        x0, y0, x1, y1 = box
        x0, y0, x1, y1 = max(0, x0 - 8), max(0, y0 - 8), min(ia.size[0], x1 + 8), min(ia.size[1], y1 + 8)
        ca, cb = ia.crop((x0, y0, x1, y1)), ib.crop((x0, y0, x1, y1))
        cd = ImageChops.difference(ca, cb).point(lambda v: min(255, v * 4))
        s = max(1, min(4, 400 // max(1, y1 - y0)))
        w, h = (x1 - x0) * s, (y1 - y0) * s
        out = Image.new('RGB', (3 * w + 8, h), (40, 40, 40))
        out.paste(ca.resize((w, h), Image.NEAREST), (0, 0))
        out.paste(cb.resize((w, h), Image.NEAREST), (w + 4, 0))
        out.paste(cd.resize((w, h), Image.NEAREST), (2 * w + 8, 0))
        out.save(os.path.join(d, 'gcmp_' + name + '.png'))


if __name__ == '__main__':
    main()
