# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-411 test harness (screenshot comparison)
#
# Contact sheet of screenshot pairs: for every <dir>/<a>_<name>.png with a matching <dir>/<b>_<name>.png,
# one row with the crop box of both (original left, OpenMerc right), scaled, labelled with the name and
# the mean absolute difference of the crop. Writes <dir>/<out>.png.
#
# usage: sheet.py <dir> <a_prefix> <b_prefix> <out> [x0 y0 x1 y1] [scale] [names,comma,separated]
import glob
import os
import sys

from PIL import Image, ImageChops, ImageDraw


def main():
    d, a, b, out = sys.argv[1:5]
    box = tuple(int(v) for v in sys.argv[5:9]) if len(sys.argv) > 8 else None
    scale = float(sys.argv[9]) if len(sys.argv) > 9 else 1.0
    only = sys.argv[10].split(',') if len(sys.argv) > 10 else None
    rows = []
    for fa in sorted(glob.glob(os.path.join(d, a + '_*.png')), key=os.path.getmtime):
        name = os.path.basename(fa)[len(a) + 1:-4]
        if only and name not in only:
            continue
        fb = os.path.join(d, b + '_' + name + '.png')
        if not os.path.exists(fb):
            continue
        ia = Image.open(fa).convert('RGB')
        ib = Image.open(fb).convert('RGB')
        if box:
            ia, ib = ia.crop(box), ib.crop(box)
        diff = ImageChops.difference(ia, ib).convert('L')
        mean = sum(i * c for i, c in enumerate(diff.histogram())) / (ia.size[0] * ia.size[1])
        w, h = int(ia.size[0] * scale), int(ia.size[1] * scale)
        rows.append((name, mean, ia.resize((w, h), Image.NEAREST), ib.resize((w, h), Image.NEAREST)))
        print('%-14s mean |diff| %.1f' % (name, mean))
    if not rows:
        return
    w, h = rows[0][2].size
    sheet = Image.new('RGB', (2 * w + 4, len(rows) * (h + 14)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, (name, mean, x, y) in enumerate(rows):
        top = i * (h + 14)
        dr.text((2, top), '%s  (orig | OpenMerc)  mean diff %.1f' % (name, mean), fill=(255, 255, 0))
        sheet.paste(x, (0, top + 14))
        sheet.paste(y, (w + 4, top + 14))
    sheet.save(os.path.join(d, out + '.png'))


if __name__ == '__main__':
    main()
