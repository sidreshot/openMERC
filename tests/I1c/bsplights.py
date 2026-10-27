# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: test tool only (SPEC-101 lump 20 / SPEC-411 world-light records as the renderer hands them to the
# Ghoul system, SPEC-410 Model lights). Lists the world lights whose sphere contains a point, nearest first.
#   python -I bsplights.py <map.bsp> x y z
import math
import struct
import sys


def main():
    data = open(sys.argv[1], 'rb').read()
    p = [float(v) for v in sys.argv[2:5]]
    ofs, length = struct.unpack_from('<ii', data, 8 + 8 * 20)
    out = []
    for i in range(length // 36):
        r = struct.unpack_from('<3f3ffii', data, ofs + 36 * i)
        org, col, inten, typ, extra = r[0:3], r[3:6], r[6], r[7], r[8]
        if inten <= 0 or typ == 3:
            continue
        radius = math.sqrt(inten) / 2 if (typ == 0 or extra >= 2) else inten
        if radius > 1500:
            continue
        d = math.dist(org, p)
        if d <= radius:
            out.append((d, i, org, col, inten, typ, extra, radius))
    out.sort()
    for d, i, org, col, inten, typ, extra, radius in out:
        print('light %4d dist %7.1f radius %7.1f org (%.0f %.0f %.0f) colour (%.3f %.3f %.3f) intensity %g type %d extra %d'
              % (i, d, radius, org[0], org[1], org[2], col[0], col[1], col[2], inten, typ, extra))
    print('%d lights contain the point' % len(out))


if __name__ == '__main__':
    main()
