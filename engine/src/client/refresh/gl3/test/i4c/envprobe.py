# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-410 test harness (black-box probe of the environment-map texture coordinates)
#
# usage: envprobe.py install <game dir> <base texture> <env texture> [link] [checker|gradient|grey|black|white] [flatenv] [alpha170]
#        envprobe.py remove <game dir>
#        envprobe.py decode <capture.png> [x0 y0 x1 y1]
# install: writes loose copies of two stock textures into <game dir>/user/textures (SPEC-300: loose files in the
# user directory override the paks), keeping their headers: the base texture becomes opaque white, the
# environment map encodes its own texture coordinates, R = 100 s, G = 100 t, B = 100 at every mip level. The
# environment pass (blend destination colour x source colour, twice) then shows 200 s, 200 t, 200 scaled by the
# light on white, so s = R / B and t = G / B per pixel. remove: deletes the loose copies again.
# decode: prints s, t statistics (and a coarse grid) of a capture.
import os
import shutil
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', '..', '..', '..', '..', '..', '..', 'tools', 'formats'))
import m32  # noqa: E402
import vfs  # noqa: E402

MARK = 'omprobe'


def patched(data, fill):
    m = m32.parse(data, strict=False)
    out = bytearray(data)
    for lv in range(m.num_mips):
        w, h, o = m.widths[lv], m.heights[lv], m.offsets[lv]
        for y in range(h):
            for x in range(w):
                out[o + 4 * (y * w + x):o + 4 * (y * w + x) + 4] = bytes(fill(x, y, w, h))
    return bytes(out)


def install(game, base, env, link=False, checker=False, flatenv=False, alpha=255):
    """link: also make <env> the second-pass texture of <base> (header field at 0x2DC); checker: the base
    texture is a white / dark grey checkerboard of 8 x 8 cells instead of plain white."""
    v = vfs.PakVFS(os.path.join(game, 'base'))
    root = os.path.join(game, 'user', 'textures')
    if checker in ('grey', 'black', 'white'):
        val = {'grey': 128, 'black': 0, 'white': 255}[checker]
        basefill = lambda x, y, w, h: (val, val, val, 255)
    elif checker == 'gradient':
        basefill = lambda x, y, w, h: (round(100 * (x + 0.5) / w), round(100 * (y + 0.5) / h), 100, alpha)
    elif checker:
        basefill = lambda x, y, w, h: (255, 255, 255, 255) if ((8 * x // w) + (8 * y // h)) % 2 else (64, 64, 64, 255)
    else:
        basefill = lambda x, y, w, h: (255, 255, 255, 255)
    if flatenv:
        envfill = lambda x, y, w, h: (50, 50, 50, 255)
    else:
        envfill = lambda x, y, w, h: (round(100 * (x + 0.5) / w), round(100 * (y + 0.5) / h), 100, 255)
    for name, fill in ((base, basefill), (env, envfill)):
        path = os.path.join(root, *(name.lower() + '.m32').split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        data = bytearray(patched(v.read('textures/%s.m32' % name), fill))
        if link and name == base:
            data[0x2DC:0x2DC + m32.NAME_LEN] = env.encode('latin-1').ljust(m32.NAME_LEN, b'\0')
        with open(path, 'wb') as f:
            f.write(bytes(data))
        print('wrote', path)
    with open(os.path.join(root, MARK), 'w') as f:
        f.write('loose probe textures of envprobe.py; delete this directory\n')


def remove(game):
    root = os.path.join(game, 'user', 'textures')
    if os.path.exists(os.path.join(root, MARK)):
        shutil.rmtree(root)
        print('removed', root)


def decode(png, box=None):
    import numpy as np
    from PIL import Image
    a = np.asarray(Image.open(png).convert('RGB')).astype(float)
    if box:
        a = a[box[1]:box[3], box[0]:box[2]]
    b = a[..., 2]
    ok = b > 40
    s = np.where(ok, a[..., 0] / np.maximum(b, 1), np.nan)
    t = np.where(ok, a[..., 1] / np.maximum(b, 1), np.nan)
    print('pixels used %d of %d' % (ok.sum(), ok.size))
    print('s min %.3f max %.3f   t min %.3f max %.3f' % (np.nanmin(s), np.nanmax(s), np.nanmin(t), np.nanmax(t)))
    h, w = s.shape
    for gy in range(0, h, max(1, h // 8)):
        print(' '.join('%4.2f,%4.2f' % (s[gy, gx], t[gy, gx]) if ok[gy, gx] else '   -,-   '
                       for gx in range(0, w, max(1, w // 8))))


def main():
    cmd = sys.argv[1]
    if cmd == 'install':
        install(sys.argv[2], sys.argv[3], sys.argv[4], 'link' in sys.argv[5:],
                next((a for a in sys.argv[5:] if a in ('gradient', 'grey', 'black', 'white')), 'checker' in sys.argv[5:]),
                'flatenv' in sys.argv[5:],
                170 if 'alpha170' in sys.argv[5:] else 255)
    elif cmd == 'remove':
        remove(sys.argv[2])
    elif cmd == 'decode':
        decode(sys.argv[2], [int(x) for x in sys.argv[3:7]] if len(sys.argv) > 6 else None)


if __name__ == '__main__':
    main()
