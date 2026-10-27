# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-412 test harness (screenshot comparison)
#
# Pairs <dir>/<a>_<name>.png with <dir>/<b>_<name>.png, writes side-by-side images
# <dir>/cmp_<name>.png (original left, OpenMerc right, absolute difference x4 below) and prints the
# mean absolute difference per channel and the share of pixels that differ by more than 24.
#
# usage: compare.py <dir> [a_prefix=orig_basic] [b_prefix=om_basic]
import glob
import os
import sys

from PIL import Image, ImageChops


def main():
    d = sys.argv[1]
    a = sys.argv[2] if len(sys.argv) > 2 else 'orig_basic'
    b = sys.argv[3] if len(sys.argv) > 3 else 'om_basic'
    for fa in sorted(glob.glob(os.path.join(d, a + '_*.png'))):
        name = os.path.basename(fa)[len(a) + 1:-4]
        fb = os.path.join(d, b + '_' + name + '.png')
        if not os.path.exists(fb):
            continue
        ia = Image.open(fa).convert('RGB')
        ib = Image.open(fb).convert('RGB')
        if ia.size != ib.size:
            print(name, 'size differs', ia.size, ib.size)
            continue
        diff = ImageChops.difference(ia, ib)
        w, h = ia.size
        hist = diff.convert('L').histogram()
        total = w * h
        big = sum(hist[25:]) / total
        stat = [sum(i * c for i, c in enumerate(diff.getchannel(k).histogram())) / total for k in range(3)]
        out = Image.new('RGB', (w * 2, h * 2))
        out.paste(ia, (0, 0))
        out.paste(ib, (w, 0))
        out.paste(diff.point(lambda v: min(255, v * 4)), (w // 2, h))
        out.save(os.path.join(d, 'cmp_' + name + '.png'))
        print('%-12s mean |diff| R %.1f G %.1f B %.1f, pixels > 24: %.1f%%' % (name, stat[0], stat[1], stat[2], big * 100))


if __name__ == '__main__':
    main()
