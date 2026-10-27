#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-120 (preview helper)
"""Tiny software rasteriser for checking decoded Ghoul (.ghb) geometry.

usage:
  ghb_render.py <file.ghb> <out.png> [--frame N] [--frames a,b,c] [--skin mat=texname ...] [--gpm file.gpm]
                [--az DEG] [--el DEG] [--size PX] [--obj out.obj]

Textures are looked up next to the model (<name>.tga / <name>.ifl first line),
then in ghoul/comskin.  Z is up (Quake convention)."""
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ghb  # noqa: E402


def find_texture(model_path, name):
    """Resolve a material/skin name to an image file (case-insensitive)."""
    d = os.path.dirname(model_path)
    roots = [d]
    p = d
    while os.path.basename(p).lower() != 'ghoul' and os.path.dirname(p) != p:
        p = os.path.dirname(p)
    if os.path.basename(p).lower() == 'ghoul':
        roots.append(os.path.join(p, 'comskin'))
    for root in roots:
        try:
            listing = {f.lower(): f for f in os.listdir(root)}
        except OSError:
            continue
        for ext in ('.tga', '.png'):
            f = listing.get(name.lower() + ext)
            if f:
                return os.path.join(root, f)
        f = listing.get(name.lower() + '.ifl')
        if f:
            first = open(os.path.join(root, f), 'rb').read().decode('latin-1').split()
            if first:
                q = listing.get(first[0].lower())
                if q:
                    return os.path.join(root, q)
    return None


def load_tex(path):
    if not path:
        return None
    try:
        return np.asarray(Image.open(path).convert('RGB'), np.float32) / 255.0
    except Exception:
        return None


def rot(az, el):
    a, e = math.radians(az), math.radians(el)
    # camera looks along -forward; build world->camera basis (x right, y up, z towards viewer)
    fwd = np.array([math.cos(e) * math.cos(a), math.cos(e) * math.sin(a), math.sin(e)])
    up = np.array([0.0, 0.0, 1.0])
    right = np.cross(up, fwd)
    right /= np.linalg.norm(right)
    up2 = np.cross(fwd, right)
    return np.stack([right, up2, fwd])


def render(tris, uvs, mats, textures, size=512, az=30, el=20, bbox=None, bg=(40, 44, 52)):
    """tris (T,3,3), uvs (T,3,2), mats (T,) -> PIL image (orthographic)."""
    R = rot(az, el)
    P = tris.reshape(-1, 3) @ R.T
    lo = P.min(0) if bbox is None else bbox[0]
    hi = P.max(0) if bbox is None else bbox[1]
    span = max(hi[0] - lo[0], hi[1] - lo[1]) * 1.08 + 1e-6
    cx, cy = (lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2
    sx = (P[:, 0] - cx) / span * size + size / 2
    sy = size / 2 - (P[:, 1] - cy) / span * size
    S = np.stack([sx, sy, P[:, 2]], 1).reshape(-1, 3, 3)
    img = np.empty((size, size, 3), np.float32)
    img[:] = np.array(bg, np.float32) / 255
    zb = np.full((size, size), -np.inf, np.float32)
    light = np.array([0.4, 0.3, 0.86])
    light /= np.linalg.norm(light)
    W = tris.reshape(-1, 3, 3)
    for t in range(len(S)):
        (x0, y0, z0), (x1, y1, z1), (x2, y2, z2) = S[t]
        den = (y1 - y2) * (x0 - x2) + (x2 - x1) * (y0 - y2)
        if abs(den) < 1e-9:
            continue
        n = np.cross(W[t, 1] - W[t, 0], W[t, 2] - W[t, 0]) @ R.T
        ln = np.linalg.norm(n)
        shade = 0.35 + 0.65 * abs(n @ light) / ln if ln > 0 else 0.6
        xa, xb = int(max(0, math.floor(min(x0, x1, x2)))), int(min(size - 1, math.ceil(max(x0, x1, x2))))
        ya, yb = int(max(0, math.floor(min(y0, y1, y2)))), int(min(size - 1, math.ceil(max(y0, y1, y2))))
        if xa > xb or ya > yb:
            continue
        gx, gy = np.meshgrid(np.arange(xa, xb + 1) + 0.5, np.arange(ya, yb + 1) + 0.5)
        l0 = ((y1 - y2) * (gx - x2) + (x2 - x1) * (gy - y2)) / den
        l1 = ((y2 - y0) * (gx - x2) + (x0 - x2) * (gy - y2)) / den
        l2 = 1 - l0 - l1
        inside = (l0 >= -1e-4) & (l1 >= -1e-4) & (l2 >= -1e-4)
        if not inside.any():
            continue
        z = l0 * z0 + l1 * z1 + l2 * z2
        sub = zb[ya:yb + 1, xa:xb + 1]
        upd = inside & (z > sub)
        if not upd.any():
            continue
        tex = textures.get(int(mats[t]))
        if tex is not None:
            u = l0 * uvs[t, 0, 0] + l1 * uvs[t, 1, 0] + l2 * uvs[t, 2, 0]
            v = l0 * uvs[t, 0, 1] + l1 * uvs[t, 1, 1] + l2 * uvs[t, 2, 1]
            h, w = tex.shape[:2]
            tx = np.clip((np.mod(u, 1.0) * w).astype(int), 0, w - 1)
            ty = np.clip((np.mod(v, 1.0) * h).astype(int), 0, h - 1)
            col = tex[ty, tx] * shade
        else:
            base = np.array([0.75, 0.72, 0.65])
            col = np.broadcast_to(base * shade, gx.shape + (3,))
        sub[upd] = z[upd]
        img[ya:yb + 1, xa:xb + 1][upd] = col[upd]
    return Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8))


def gpm_setup(gpm_path):
    """Skins and hidden parts for a player model (.gpm), see SPEC-121."""
    import ghoul_text as gt
    g = gt.parse_gpm(open(gpm_path, 'rb').read().decode('latin-1'))
    root = os.path.dirname(os.path.dirname(os.path.abspath(gpm_path)))      # .../ghoul
    mdir = os.path.join(root, *g['dir'].split('/'))
    skins = {s['material'].lower(): s['skin'] for s in g['skins']}
    hide = set()
    poff = gt._find(g['set'] + '_poff.gsq', [mdir])
    if poff:
        hide |= {x.lower() for x in gt.resolve_gsq(poff, [os.path.join(root, 'comskin')])}
    for p in g['parts']:
        if p['on']:
            hide.discard(p['part'].lower())
        else:
            hide.add(p['part'].lower())
    return g, skins, hide


def model_tris(path, frame=0, skins=None, hide=None):
    b = open(path, 'rb').read()
    m = ghb.parse(b)
    mesh = ghb.build_mesh(m, b, frame, strict=False)
    hide = set(hide or ())
    for x in m['meshes']:                       # per-frame mesh visibility from the file
        v = x['visible']
        if (v['const'] and not v['value']) or (not v['const'] and not v['values'][frame]):
            hide.add(x['name'].lower())
    if hide:
        names = [x['name'].lower() for x in m['meshes']]
        keep = [k for k, f in enumerate(mesh['faces']) if names[f[1]] not in hide]
        idx = np.array([[3 * k, 3 * k + 1, 3 * k + 2] for k in keep], int).reshape(-1)
        mesh = {'pos': mesh['pos'][idx], 'uv': mesh['uv'][idx],
                'faces': [mesh['faces'][k] for k in keep], 'skipped': mesh['skipped']}
    T = mesh['pos'].reshape(-1, 3, 3) if len(mesh['faces']) else np.zeros((0, 3, 3))
    UV = mesh['uv'].reshape(-1, 3, 2) if len(mesh['faces']) else np.zeros((0, 3, 2))
    mats = np.array([f[0] for f in mesh['faces']], int)
    textures = {}
    for i, mt in enumerate(m['materials']):
        name = (skins or {}).get(mt['name'].lower()) or mt['slots'][0]['texture'] or mt['name']
        textures[i] = load_tex(find_texture(path, name))
    return m, mesh, T, UV, mats, textures


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    path, out = argv[0], argv[1]
    opts = {'--frame': '0', '--az': '30', '--el': '20', '--size': '512'}
    skins = {}
    hide = set()
    i = 2
    frames = None
    while i < len(argv):
        if argv[i] == '--gpm':
            _, sk, hd = gpm_setup(argv[i + 1])
            skins.update(sk)
            hide |= hd
        elif argv[i] == '--skin':
            k, v = argv[i + 1].split('=', 1)
            skins[k.lower()] = v
        elif argv[i] == '--frames':
            frames = [int(x) for x in argv[i + 1].split(',')]
        else:
            opts[argv[i]] = argv[i + 1]
        i += 2
    size = int(opts['--size'])
    if frames:
        tiles = []
        allT = []
        data = []
        for f in frames:
            m, mesh, T, UV, mats, tex = model_tris(path, f, skins, hide)
            data.append((f, T, UV, mats, tex))
            allT.append(T.reshape(-1, 3))
        R = rot(float(opts['--az']), float(opts['--el']))
        P = np.concatenate(allT) @ R.T
        bb = (P.min(0), P.max(0))
        for f, T, UV, mats, tex in data:
            im = render(T, UV, mats, tex, size, float(opts['--az']), float(opts['--el']), bb)
            ImageDraw.Draw(im).text((6, 6), 'frame %d' % f, fill=(255, 255, 255))
            tiles.append(im)
        sheet = Image.new('RGB', (size * len(tiles), size))
        for k, im in enumerate(tiles):
            sheet.paste(im, (k * size, 0))
        sheet.save(out)
    else:
        m, mesh, T, UV, mats, tex = model_tris(path, int(opts['--frame']), skins, hide)
        views = [(float(opts['--az']), float(opts['--el'])), (float(opts['--az']) + 120, 15),
                 (float(opts['--az']) + 240, 60)]
        sheet = Image.new('RGB', (size * 3, size))
        for k, (az, el) in enumerate(views):
            im = render(T, UV, mats, tex, size, az, el)
            ImageDraw.Draw(im).text((6, 6), 'az %d el %d' % (az, el), fill=(255, 255, 255))
            sheet.paste(im, (k * size, 0))
        sheet.save(out)
        if '--obj' in opts:          # also export exactly what was rendered
            ghb.write_obj(m, mesh, opts['--obj'])
        print('%s: %d triangles, %d skipped, textures %s' % (
            os.path.basename(path), len(T), mesh['skipped'],
            {m['materials'][k]['name']: (v is not None) for k, v in tex.items()}))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
