# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-311 check (test tool, no game code)
# Compares the blocks of two .dm2 files: prints which of the first N blocks are byte-identical and, for the
# others, the lengths and the first differing offset.
# usage: python -I dm2cmp.py <a.dm2> <b.dm2> [N]
import struct
import sys


def blocks(p):
    d = open(p, 'rb').read()
    o, r = 0, []
    while o + 4 <= len(d):
        (n,) = struct.unpack('<i', d[o:o + 4])
        o += 4
        if n == -1:
            break
        r.append(d[o:o + n])
        o += n
    return r


def main():
    a, b = blocks(sys.argv[1]), blocks(sys.argv[2])
    n = int(sys.argv[3]) if len(sys.argv) > 3 else min(len(a), len(b))
    print('blocks: %d and %d' % (len(a), len(b)))
    same = [i for i in range(n) if a[i] == b[i]]
    print('identical among the first %d: %d' % (n, len(same)))
    for i in range(n):
        if a[i] != b[i]:
            x, y = a[i], b[i]
            k = next((j for j in range(min(len(x), len(y))) if x[j] != y[j]), min(len(x), len(y)))
            print('  block %d: len %d / %d, first difference at byte %d: %s / %s' % (
                i, len(x), len(y), k, x[k:k + 8].hex(), y[k:k + 8].hex()))


if __name__ == '__main__':
    main()
