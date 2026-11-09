#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""skymatch.py - a new SoF sky box from a CC0 equirectangular HDR panorama, graded to look like the map's own sky.

The colour and brightness of the original sky are taken per elevation above the horizon (the mean and spread of
each channel in 1-degree bands); the panorama's own bands are moved onto them (a Reinhard transfer per band). So the
new sky keeps what the level designers chose - how dark it is, its tint, the black band the map's hills melt into -
and only the detail (clouds, stars) comes from the panorama, at a higher resolution. Nothing of the original sky is
copied: only its 90 x 3 means and spreads steer the grade.

usage:
  skymatch.py preview <orig_sides.png> <orig_top.png> <out.png> <pano.hdr>...   one view per panorama + the original
  skymatch.py build <orig_sides.png> <orig_top.png> <pano.hdr> <outdir> <name> <face> [yaw]
      writes <name>_rt/bk/lf/ft/up.jpg (the bottom face stays "none")
Needs numpy and opencv-python.
"""
import os
import sys

import cv2
import numpy as np

SKYTEXORDER = [0, 2, 1, 3, 4, 5]
ST_TO_VEC = [[3, -1, 2], [-3, 1, 2], [1, 3, 2], [-1, -3, 2], [-2, -1, 3], [2, -1, -3]]
VEC_TO_ST = [[-2, 3, 1], [2, 3, -1], [1, 3, 2], [-1, 3, -2], [-2, -1, 3], [-2, 1, -3]]


def face_dirs(line, n):
    axis = SKYTEXORDER[line]
    u = (np.arange(n) + 0.5) / n
    s = 2 * u[None, :] - 1 + 0 * u[:, None]
    t = 1 - 2 * u[:, None] + 0 * u[None, :]
    b = [s, t, np.ones_like(s)]
    v = [(-b[-k - 1] if k < 0 else b[k - 1]) for k in ST_TO_VEC[axis]]
    v = np.stack(v, -1)
    return v / np.linalg.norm(v, axis=-1, keepdims=True)


def remap_flat(img, x, y, border):
    """cv2.remap of flat coordinate lists (its maps must stay below 32767 in each dimension)"""
    n = x.size
    w = 1024
    rows = (n + w - 1) // w
    xp = np.zeros(rows * w, np.float32)
    yp = np.zeros(rows * w, np.float32)
    xp[:n] = x.ravel()
    yp[:n] = y.ravel()
    r = cv2.remap(img, xp.reshape(rows, w), yp.reshape(rows, w), cv2.INTER_LINEAR, borderMode=border)
    return r.reshape(rows * w, -1)[:n]


def cube_sample(faces, d):
    """faces: list of 6 float images (line order rt bk lf ft up dn, None = black); d: (..., 3) directions"""
    out = np.zeros(d.shape[:-1] + (3,), np.float32)
    ad = np.abs(d)
    axis = np.where((ad[..., 0] >= ad[..., 1]) & (ad[..., 0] >= ad[..., 2]), np.where(d[..., 0] > 0, 0, 1),
                    np.where(ad[..., 1] >= ad[..., 2], np.where(d[..., 1] > 0, 2, 3), np.where(d[..., 2] > 0, 4, 5)))
    for a in range(6):
        img = faces[SKYTEXORDER[a]]
        m = axis == a
        if img is None or not m.any():
            continue
        dd = d[m]

        def comp(k):
            return -dd[:, -k - 1] if k < 0 else dd[:, k - 1]
        dv = comp(VEC_TO_ST[a][2])
        s = comp(VEC_TO_ST[a][0]) / dv
        t = comp(VEC_TO_ST[a][1]) / dv
        h, w = img.shape[:2]
        x = ((s + 1) / 2) * w - 0.5
        y = (1 - (t + 1) / 2) * h - 0.5
        out[m] = remap_flat(img, x, y, cv2.BORDER_REPLICATE)
    return out


def load_rgb(path):
    im = cv2.imread(path, cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32) / 255.0
    return im


def orig_faces(sides, top):
    s, t = load_rgb(sides), load_rgb(top)
    return [s, s, s, s, t, None]


def pano_sampler(path, yaw=0.0):
    hdr = cv2.imread(path, cv2.IMREAD_ANYDEPTH | cv2.IMREAD_COLOR)[:, :, ::-1].astype(np.float32)
    H, W = hdr.shape[:2]

    def sample(d):
        lon = np.arctan2(d[..., 1], d[..., 0]) + np.radians(yaw)
        lat = np.arcsin(np.clip(d[..., 2], -1, 1))
        x = ((0.5 - lon / (2 * np.pi)) % 1.0) * W - 0.5
        y = (0.5 - lat / np.pi) * H - 0.5
        sh = d.shape[:-1]
        r = remap_flat(hdr, x, y, cv2.BORDER_WRAP)
        return r.reshape(sh + (3,))
    return sample


def sphere_dirs(nel=91, naz=720):
    """directions on bands 0..90 degrees of elevation (1-degree steps), naz per band"""
    el = np.radians(np.arange(nel))[:, None]
    az = (np.arange(naz) + 0.5) / naz * 2 * np.pi
    return np.stack([np.cos(el) * np.cos(az), np.cos(el) * np.sin(az), np.sin(el) + 0 * az], -1)


def band_stats(img):
    m = img.mean(axis=1)
    s = img.std(axis=1)
    # smooth over a few degrees (the original's bands are noisy)
    k = np.ones(5) / 5
    m = np.stack([np.convolve(np.pad(m[:, c], 2, mode='edge'), k, 'valid') for c in range(3)], -1)
    s = np.stack([np.convolve(np.pad(s[:, c], 2, mode='edge'), k, 'valid') for c in range(3)], -1)
    return m, s


def tonemap(x):
    # display encoding of the HDR, its upper sky median at mid grey
    return np.clip(x, 0, None) ** (1 / 2.2)


def grader(orig, pano):
    d = sphere_dirs()
    to, _ = cube_sample(orig, d), None
    tm, ts = band_stats(to)
    raw = pano(d)
    expo = 0.18 / max(float(np.median(raw.mean(axis=-1))), 1e-6)
    sm, ss = band_stats(tonemap(raw * expo))

    def grade(dirs):
        el = np.degrees(np.arcsin(np.clip(dirs[..., 2], -1, 1)))
        i = np.clip(el, 0, 90)
        i0 = np.floor(i).astype(int)
        i1 = np.minimum(i0 + 1, 90)
        f = (i - i0)[..., None]

        def lerp(a):
            return a[i0] * (1 - f) + a[i1] * f
        x = tonemap(pano(dirs) * expo)
        y = (x - lerp(sm)) / np.maximum(lerp(ss), 1e-4) * lerp(ts) + lerp(tm)
        y = np.where((el < 0)[..., None], 0.0, y)  # under the horizon: black, as SoF's
        return np.clip(y, 0, 1)
    return grade


def view_dirs(w, h, yaw, pitch, fov):
    fx = np.tan(np.radians(fov) / 2)
    xs = (np.arange(w) + 0.5) / w * 2 - 1
    ys = 1 - (np.arange(h) + 0.5) / h * 2
    X, Y = np.meshgrid(xs * fx, ys * fx * h / w)
    d = np.stack([np.ones_like(X), -X, Y], -1)
    cp, sp = np.cos(np.radians(pitch)), np.sin(np.radians(pitch))
    d = np.stack([d[..., 0] * cp - d[..., 2] * sp, d[..., 1], d[..., 0] * sp + d[..., 2] * cp], -1)
    cy, sy = np.cos(np.radians(yaw)), np.sin(np.radians(yaw))
    d = np.stack([d[..., 0] * cy - d[..., 1] * sy, d[..., 0] * sy + d[..., 1] * cy, d[..., 2]], -1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


def label(img, text):
    im = (np.clip(img, 0, 1) * 255).astype(np.uint8)[:, :, ::-1].copy()
    cv2.putText(im, text, (8, 22), cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 255), 1, cv2.LINE_AA)
    return im


def preview(sides, top, out, panos):
    orig = orig_faces(sides, top)
    vd = view_dirs(640, 280, 0, 18, 100)
    gain = 4.0  # the views are brightened alike so they can be compared on a screen (the game shows them darker)
    tiles = [label(cube_sample(orig, vd) * gain, 'ORIGINAL x%g' % gain)]
    for p in panos:
        g = grader(orig, pano_sampler(p))
        tiles.append(label(g(vd) * gain, os.path.basename(p).split('.')[0]))
    cols = 2
    rows = (len(tiles) + cols - 1) // cols
    sheet = np.zeros((rows * 280, cols * 640, 3), np.uint8)
    for i, t in enumerate(tiles):
        sheet[(i // cols) * 280:(i // cols + 1) * 280, (i % cols) * 640:(i % cols + 1) * 640] = t
    cv2.imwrite(out, sheet)


def build(sides, top, pano, outdir, name, n, yaw=0.0):
    orig = orig_faces(sides, top)
    g = grader(orig, pano_sampler(pano, yaw))
    os.makedirs(outdir, exist_ok=True)
    for line, suf in enumerate(['rt', 'bk', 'lf', 'ft', 'up']):
        y = g(face_dirs(line, n))
        im = np.clip(y * 255 + np.random.uniform(-0.5, 0.5, y.shape), 0, 255).astype(np.uint8)
        cv2.imwrite(os.path.join(outdir, '%s_%s.jpg' % (name, suf)), im[:, :, ::-1], [cv2.IMWRITE_JPEG_QUALITY, 95])


if __name__ == '__main__':
    if sys.argv[1] == 'preview':
        preview(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5:])
    elif sys.argv[1] == 'build':
        build(sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5], sys.argv[6], int(sys.argv[7]),
              float(sys.argv[8]) if len(sys.argv) > 8 else 0.0)
