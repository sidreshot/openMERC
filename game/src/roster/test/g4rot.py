#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - decoder of the 32-bit packed rotation of Ghoul instance transforms (SPEC-014 §12 = SPEC-201 B.6),
# used to compare turned parts (tank turret/guns, helicopter gun and crew heads) between captures: the image of the
# model's X axis (row 0 of the row-vector matrix) as yaw/pitch in degrees.
# Implements: SPEC-014 §12-§13 (decoder side)
#
# usage: g4rot.py <hex word> ...
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402

_TABLE = None


def table():
    global _TABLE
    if _TABLE is None:
        axes = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]
        _TABLE = axes + [tuple(v) for v in sofnet.load_dirs()[0]]
    return _TABLE


def _norm(v):
    n = math.sqrt(sum(x * x for x in v))
    return tuple(x / n for x in v) if n > 1e-10 else (0.0, 0.0, 0.0)


def _cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def decode(w):
    """3x3 matrix in row-vector layout: m[r][c]; columns are a, d, e (SPEC-014 §12 decoder)."""
    t = table()[w & 255]
    c = tuple(((w >> s) & 15) * 0.018666666 - 0.14 for s in (8, 12, 16))
    a = _norm(tuple(t[i] + c[i] for i in range(3)))
    p = _cross(t, (1, 0, 0))
    for ax in ((0, 1, 0), (0, 0, 1)):
        q = _cross(t, ax)
        if math.sqrt(sum(x * x for x in q)) > math.sqrt(sum(x * x for x in p)):
            p = q
    b = _norm(_cross(p, a))
    cc = _norm(_cross(a, b))
    th = ((w >> 20) & 2047) * 2 * math.pi / 2047
    d = _norm(tuple(b[i] * math.cos(th) + cc[i] * math.sin(th) for i in range(3)))
    e = _cross(a, d)
    if w >> 31:
        e = tuple(-x for x in e)
    e = _norm(e)
    return [[a[r], d[r], e[r]] for r in range(3)]


def forward(w):
    """yaw, pitch (degrees) of the image of the model's X axis (row 0)."""
    m = decode(w)
    x = m[0]
    return math.degrees(math.atan2(x[1], x[0])), math.degrees(math.atan2(x[2], math.hypot(x[0], x[1])))


if __name__ == "__main__":
    for h in sys.argv[1:]:
        w = int(h, 16)
        y, p = forward(w)
        print("%08x  X -> yaw %7.2f pitch %7.2f  matrix %s" % (w, y, p, [[round(v, 3) for v in r] for r in decode(w)]))
