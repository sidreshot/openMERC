# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: test tool only (SPEC-310 13 / SPEC-411 screenshot comparison, no game code).
# Mean absolute difference (0-255, all channels) of two captures inside a box, and the per-channel mean of
# each image there (shows a colour cast).
#   python -I regcmp.py <orig.png> <om.png> [x0 y0 x1 y1]
import sys
from PIL import Image, ImageChops, ImageStat


def main():
    a = Image.open(sys.argv[1]).convert('RGB')
    b = Image.open(sys.argv[2]).convert('RGB')
    box = tuple(int(v) for v in sys.argv[3:7]) if len(sys.argv) >= 7 else (0, 0, a.size[0], a.size[1])
    ca, cb = a.crop(box), b.crop(box)
    d = ImageStat.Stat(ImageChops.difference(ca, cb)).mean
    print('box %s diff %.2f  orig rgb %s  om rgb %s' % (box, sum(d) / 3,
          ' '.join('%.1f' % v for v in ImageStat.Stat(ca).mean),
          ' '.join('%.1f' % v for v in ImageStat.Stat(cb).mean)))


if __name__ == '__main__':
    main()
