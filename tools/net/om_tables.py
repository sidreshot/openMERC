#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - the engine's generated fixed tables, Python twin of engine/src/common/header/om_geodesic.h.
# No table data is stored: the 250 directions are the vertices of a frequency-5 geodesic icosphere without its
# poles (docs/impl/LEGAL-tables.md); the Huffman weights are OpenMerc's own character classes.
# Implements: SPEC-201 'dir' (generated contents), SPEC-014 section 13 (axes + directions)
#
# usage: om_tables.py [--compare-private]   print a summary; optionally compare with docs/private (local only)
import math
import struct
import sys


def _slerp(a, b, t):
    d = max(-1.0, min(1.0, a[0] * b[0] + a[1] * b[1] + a[2] * b[2]))
    w = math.acos(d)
    if w < 1e-12:
        return tuple(a)
    s = math.sin(w)
    k1, k2 = math.sin((1.0 - t) * w), math.sin(t * w)
    return tuple((k1 * x + k2 * y) / s for x, y in zip(a, b))


def build_dirs():
    z, r = 1 / math.sqrt(5), 2 / math.sqrt(5)
    north, south = (0.0, 0.0, 1.0), (0.0, 0.0, -1.0)
    up = [(r * math.cos(math.radians(72 * k)), r * math.sin(math.radians(72 * k)), z) for k in range(5)]
    dn = [(r * math.cos(math.radians(72 * k + 36)), r * math.sin(math.radians(72 * k + 36)), -z) for k in range(5)]
    faces = [(north, up[k], up[(k + 1) % 5]) for k in range(5)]
    faces += [(up[(k + 1) % 5], dn[k], dn[(k + 1) % 5]) for k in (4, 0, 1, 2, 3)]
    faces += [(dn[k], up[(k + 1) % 5], up[k]) for k in range(5)]
    faces += [(south, dn[(k + 1) % 5], dn[k]) for k in range(5)]
    pts = [north, south]
    for a, b, c in faces:
        for i in range(6):
            e1, e2 = _slerp(a, b, i / 5), _slerp(a, c, i / 5)
            for j in range(i + 1):
                p = _slerp(e1, e2, j / i) if i else a
                if not any(all(abs(p[n] - q[n]) < 1e-6 for n in range(3)) for q in pts):
                    pts.append(p)
    assert len(pts) == 252
    raw = b"".join(struct.pack("<3f", *p) for p in pts[2:])
    vecs = [struct.unpack_from("<3f", raw, 12 * i) for i in range(250)]
    return vecs, raw


def huff_weight(c):
    if ord("a") <= c <= ord("z"):
        return 100.0 if chr(c) in "eaotinsrlhdc" else 50.0
    if c == ord("_"):
        return 90.0
    if c in (ord("/"), ord(".")):
        return 30.0
    if ord("0") <= c <= ord("9"):
        return 20.0
    if ord("A") <= c <= ord("Z"):
        return 10.0
    if c in (ord(" "), ord("-")):
        return 5.0
    return 1.0


HUFF_WEIGHTS = [huff_weight(c) for c in range(256)]

if __name__ == "__main__":
    vecs, raw = build_dirs()
    print("250 directions, %d bytes; weights: %d classes" % (len(raw), len(set(HUFF_WEIGHTS))))
    if "--compare-private" in sys.argv:
        sys.path.insert(0, __import__("os").path.dirname(__file__))
        import sofnet
        if sofnet.TABLES != "original":
            print("docs/private not present")
        else:
            err = max(max(abs(a - b) for a, b in zip(u, v)) for u, v in zip(vecs, sofnet.DIRS))
            print("max difference to the original's table: %.3g" % err)