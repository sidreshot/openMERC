# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-412 test harness (screenshot comparison across directories)
#
# Like ../compare.py, but the two sets may live in different directories and the side-by-side images go to a
# third one (so another package's capture directory is only read):
#   compare2.py <dirA> <prefixA> <dirB> <prefixB> <outdir> [outprefix=cmp] [names...]
# Prints, per name, the mean absolute difference per channel (0-255) and the share of pixels differing by
# more than 24; writes <outdir>/<outprefix>_<name>.png = A | B on top, |A - B| x 4 below.
import glob
import os
import sys

from PIL import Image, ImageChops


def compare(fa, fb, out):
    ia = Image.open(fa).convert('RGB')
    ib = Image.open(fb).convert('RGB')
    if ia.size != ib.size:
        return None
    diff = ImageChops.difference(ia, ib)
    w, h = ia.size
    total = w * h
    big = sum(diff.convert('L').histogram()[25:]) / total
    stat = [sum(i * c for i, c in enumerate(diff.getchannel(k).histogram())) / total for k in range(3)]
    if out:
        img = Image.new('RGB', (w * 2, h * 2))
        img.paste(ia, (0, 0))
        img.paste(ib, (w, 0))
        img.paste(diff.point(lambda v: min(255, v * 4)), (w // 2, h))
        img.save(out)
    return stat, big


def main():
    da, pa, db, pb, out = sys.argv[1:6]
    op = sys.argv[6] if len(sys.argv) > 6 else 'cmp'
    names = sys.argv[7:]
    os.makedirs(out, exist_ok=True)
    if not names:
        names = [os.path.basename(f)[len(pa) + 1:-4] for f in sorted(glob.glob(os.path.join(da, pa + '_*.png')))]
    tot = [0.0, 0.0, 0.0]
    n = 0
    for name in names:
        fa = os.path.join(da, '%s_%s.png' % (pa, name))
        fb = os.path.join(db, '%s_%s.png' % (pb, name))
        if not (os.path.exists(fa) and os.path.exists(fb)):
            continue
        r = compare(fa, fb, os.path.join(out, '%s_%s.png' % (op, name)))
        if r is None:
            print('%-12s size differs' % name)
            continue
        stat, big = r
        tot = [t + s for t, s in zip(tot, stat)]
        n += 1
        print('%-12s mean |diff| R %.1f G %.1f B %.1f, pixels > 24: %.1f%%' % (name, stat[0], stat[1], stat[2], big * 100))
    if n:
        print('%-12s mean |diff| R %.1f G %.1f B %.1f over %d pairs' % ('ALL', tot[0] / n, tot[1] / n, tot[2] / n, n))


if __name__ == '__main__':
    main()
