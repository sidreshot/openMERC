# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-410 (fog regions) test helper: prints the region records (lump 21) of a SoF map with
# the meaning SPEC-410 gives them, and how many faces/leafs/boundary faces reference each region.
# usage: fogregions.py <map.bsp>
import struct
import sys


def main():
    data = open(sys.argv[1], 'rb').read()
    ident, version = struct.unpack_from('<4si', data, 0)
    lumps = [struct.unpack_from('<ii', data, 8 + 8 * i) for i in range(22)]
    ofs, ln = lumps[21]
    regions = []
    for i in range(ln // 76):
        r = struct.unpack_from('<ifffff4Bifff', data, ofs + 76 * i)
        regions.append(r)
        kind, opacity, amp, start, rng, unused20, cr, cg, cb, ca, flags, speed, farwall, unused40 = r
        print('region %d: kind %d opacity %.2f amp %.1f start %.1f range %.1f colour %d %d %d %d flags %d '
              'speed %.2f farwall %.0f' % (i, kind, opacity, amp, start, rng, cr, cg, cb, ca, flags, speed, farwall))
    # faces
    fofs, fln = lumps[6]
    tofs, tln = lumps[5]
    counts = {}
    bcounts = {}
    for f in range(fln // 44):
        planenum, side, firstedge, numedges, texinfo, volume = struct.unpack_from('<Hhihhh', data, fofs + 44 * f)
        if volume >= 0:
            counts[volume] = counts.get(volume, 0) + 1
            tflags = struct.unpack_from('<i', data, tofs + 76 * texinfo + 32)[0]
            if tflags & 0x10000:
                bcounts[volume] = bcounts.get(volume, 0) + 1
    lofs, lln = lumps[8]
    lcounts = {}
    for l in range(lln // 32):
        vol = struct.unpack_from('<h', data, lofs + 32 * l + 8)[0]
        if vol >= 0:
            lcounts[vol] = lcounts.get(vol, 0) + 1
    for i in range(len(regions)):
        print('region %d: %d faces (%d boundary), %d leafs' % (i, counts.get(i, 0), bcounts.get(i, 0), lcounts.get(i, 0)))


if __name__ == '__main__':
    main()
