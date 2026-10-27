# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-310 / SPEC-311 screenshot comparison (test tool, no game code)
#
# Pairs original and OpenMerc captures of the same demo frames: writes <out>/pair_<n>.png (original left,
# OpenMerc middle, amplified absolute difference right) and prints the mean absolute difference over the
# whole frame and over the HUD regions of SPEC-310 section 2 at 640x480 (item panel, weapon panel,
# health bar, rank / score).
# usage: python -I compare.py <outdir> <orig.png> <om.png> [<orig.png> <om.png> ...]
import sys
from PIL import Image, ImageChops, ImageStat

REGIONS = {
    'frame': (0, 0, 640, 480),
    'item_panel': (0, 385, 130, 480),
    'weapon_panel': (510, 385, 640, 480),
    'health': (240, 428, 400, 480),
    'rank': (560, 0, 640, 70),
    'centre': (220, 160, 420, 320),
}


def mad(a, b, box):
    d = ImageChops.difference(a.crop(box), b.crop(box)).convert('L')
    return ImageStat.Stat(d).mean[0]


def main():
    out = sys.argv[1]
    files = sys.argv[2:]
    for k in range(0, len(files), 2):
        a = Image.open(files[k]).convert('RGB').resize((640, 480))
        b = Image.open(files[k + 1]).convert('RGB').resize((640, 480))
        diff = ImageChops.difference(a, b).point(lambda v: min(255, v * 4))
        pair = Image.new('RGB', (1920, 480))
        pair.paste(a, (0, 0))
        pair.paste(b, (640, 0))
        pair.paste(diff, (1280, 0))
        name = '%s/pair_%d.png' % (out, k // 2 + 1)
        pair.save(name)
        stats = ' '.join('%s=%.1f' % (r, mad(a, b, box)) for r, box in REGIONS.items())
        print('%s  %s | %s' % (name, files[k].split('\\')[-1].split('/')[-1], stats))


if __name__ == '__main__':
    main()
