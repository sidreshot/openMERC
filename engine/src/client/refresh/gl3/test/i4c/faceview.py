# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-410 test harness (eye positions for the capture shim's view override, om_capview)
#
# usage: faceview.py <map.bsp> <face> [distance=160] [lift=0]
#        faceview.py <map.bsp> <face> @dx,dy,dz
#        faceview.py <map.bsp> <face> auto      (first candidate eye with a clear line of sight)
# Prints "x y z pitch yaw roll" for an eye `distance` units in front of the face's centre (along its normal,
# raised by `lift`), or at centre + (dx, dy, dz), looking at the centre (Quake angles: positive pitch looks
# down).
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..', '..', '..', 'tools', 'formats'))
import bsp  # noqa: E402


def leaf_at(b, p):
    n = 0
    while n >= 0:
        nd = b.nodes[n]
        pl = b.planes[nd.planenum]
        n = nd.children[0] if sum(pl.normal[k] * p[k] for k in range(3)) - pl.dist >= 0 else nd.children[1]
    return b.leafs[-1 - n]


def clear(b, eye, c):
    # no solid leaf on the way from the eye to 4 units in front of the face centre
    d = [c[k] - eye[k] for k in range(3)]
    ln = math.sqrt(sum(x * x for x in d))
    steps = max(2, int(ln / 4))
    for i in range(steps):
        t = i / steps * (ln - 4) / ln
        if leaf_at(b, [eye[k] + d[k] * t for k in range(3)]).contents & 1:
            return False
    return True


def auto(b, c, n):
    up = (0.0, 0.0, 1.0)
    horiz = abs(n[2]) > 0.9
    for d in (160, 120, 90, 60, 40):
        cands = []
        if horiz:
            for ax in ((-1, 0), (1, 0), (0, -1), (0, 1)):
                cands.append([c[0] + ax[0] * d, c[1] + ax[1] * d, c[2] + n[2] * d * 0.8])
        else:
            for lift in (0.6, 0.3, 0.0):
                cands.append([c[k] + n[k] * d + up[k] * d * lift for k in range(3)])
        for e in cands:
            if clear(b, e, c):
                return e
    return None


def main():
    b = bsp.load(sys.argv[1])
    f = b.faces[int(sys.argv[2])]
    pts = b.face_points(f)
    c = [sum(p[k] for p in pts) / len(pts) for k in range(3)]
    n = b.face_normal(f)
    if len(sys.argv) > 3 and sys.argv[3] == 'auto':
        eye = auto(b, c, n)
        if eye is None:
            sys.stderr.write('no clear eye found\n')
            sys.exit(1)
    elif len(sys.argv) > 3 and sys.argv[3].startswith('@'):
        off = [float(x) for x in sys.argv[3][1:].split(',')]
        eye = [c[k] + off[k] for k in range(3)]
    else:
        dist = float(sys.argv[3]) if len(sys.argv) > 3 else 160.0
        lift = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
        eye = [c[k] + n[k] * dist for k in range(3)]
        eye[2] += lift
    d = [c[k] - eye[k] for k in range(3)]
    yaw = math.degrees(math.atan2(d[1], d[0]))
    pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
    t = b.texinfo[f.texinfo]
    sys.stderr.write('face %d %s flags %08x value %08x centre (%.0f %.0f %.0f) normal (%.2f %.2f %.2f)\n' % (
        int(sys.argv[2]), t.texture, t.flags, t.value & 0xFFFFFFFF, c[0], c[1], c[2], n[0], n[1], n[2]))
    print('%.1f %.1f %.1f %.1f %.1f 0' % (eye[0], eye[1], eye[2], pitch, yaw))


if __name__ == '__main__':
    main()
