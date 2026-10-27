# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-410 test harness (finds stock map faces for the I4c black-box views)
#
# usage: bspscan.py <dir with maps/*.bsp> [kind]
#   kind = flow | waves | warp | inline-detail | fog | detail-inline | env-warp
# Prints, per map, the faces of the requested kind with texture, flags, value, model number and centre.
# detail-inline: faces of inline models whose .m32 names a second-pass texture (read from the stock paks);
# env-warp: liquid faces whose .m32 second-pass texture is an environment map (kind bit 0x8).
import glob
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..', '..', '..', 'tools', 'formats'))
import bsp  # noqa: E402
import m32  # noqa: E402
import vfs  # noqa: E402

_vfs = None
_m32 = {}


def texture(name):
    """Parsed textures/<name>.m32 from the stock paks, or None."""
    global _vfs
    if _vfs is None:
        _vfs = vfs.PakVFS()
    key = name.lower()
    if key not in _m32:
        try:
            _m32[key] = m32.parse(_vfs.read('textures/%s.m32' % key), strict=False)
        except (FileNotFoundError, ValueError):
            _m32[key] = None
    return _m32[key]


def second_pass(name):
    """(detail name, kind of the detail image) of a texture, or None."""
    t = texture(name)
    if not t or not t.detail_name:
        return None
    d = texture(t.detail_name)
    return t.detail_name, (d.kind if d else -1)


def face_model(b):
    owner = {}
    for mi, m in enumerate(b.models):
        for fi in range(m.firstface, m.firstface + m.numfaces):
            owner[fi] = mi
    return owner


def centre(b, f):
    pts = b.face_points(f)
    return tuple(round(sum(p[k] for p in pts) / len(pts)) for k in range(3))


def main():
    root = sys.argv[1]
    kind = sys.argv[2] if len(sys.argv) > 2 else 'flow'
    for path in sorted(glob.glob(os.path.join(root, 'maps', '*.bsp'))):
        b = bsp.load(path)
        owner = face_model(b)
        hits = {}
        for fi, f in enumerate(b.faces):
            t = b.texinfo[f.texinfo]
            fl = t.flags
            ok = False
            if kind == 'flow':
                ok = bool(fl & 0x40)
            elif kind == 'waves':
                ok = bool(fl & 0x2000)
            elif kind == 'warp':
                ok = bool(fl & 0x8)
            elif kind == 'inline-detail':
                ok = owner.get(fi, 0) > 0
            elif kind == 'fog':
                ok = f.volume >= 0
            elif kind == 'detail-inline':
                ok = owner.get(fi, 0) > 0 and not (fl & 0x400) and second_pass(t.texture) is not None
            elif kind == 'env-warp':
                sp = second_pass(t.texture) if fl & 0x8 else None
                ok = sp is not None and sp[1] >= 0 and bool(sp[1] & 0x8)
            if not ok:
                continue
            key = (t.texture, fl, t.value, owner.get(fi, 0))
            hits.setdefault(key, []).append(fi)
        if hits:
            print(os.path.basename(path))
            for (tex, fl, val, mod), faces in sorted(hits.items()):
                sp = second_pass(tex) if kind in ('detail-inline', 'env-warp') else None
                print('   %-32s flags %08x value %08x model %3d faces %4d  e.g. face %d at %s%s' % (
                    tex, fl, val & 0xFFFFFFFF, mod, len(faces), faces[0], centre(b, b.faces[faces[0]]),
                    ('  second pass %s kind %d' % sp) if sp else ''))


if __name__ == '__main__':
    main()
