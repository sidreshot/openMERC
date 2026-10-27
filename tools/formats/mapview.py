#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-141, SPEC-143 (viewers only)
"""Top-down outline of a map's world model, used as a backdrop for overlays.

Draws every upward-facing world face (normal z > 0.3) as a grey polygon whose
brightness follows its height, painter-sorted from low to high. Returns a Pillow
image and a world->pixel transform. Only geometry from bsp.py (SPEC-101) is used.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsp  # noqa: E402


class TopDown:
    def __init__(self, b, units_per_px=4.0, margin=32, extra_points=()):
        from PIL import Image, ImageDraw
        m = b.models[0]
        polys = []
        for fi in range(m.firstface, m.firstface + m.numfaces):
            f = b.faces[fi]
            ti = b.texinfo[f.texinfo]
            if ti.flags & (bsp.SURF_SKY | bsp.SURF_NODRAW | bsp.SURF_HINT | bsp.SURF_SKIP):
                continue
            n = b.face_normal(f)
            if n[2] <= 0.3:
                continue
            pts = b.face_points(f)
            polys.append((sum(p[2] for p in pts) / len(pts), pts))
        xs = [p[0] for _, ps in polys for p in ps] + [p[0] for p in extra_points]
        ys = [p[1] for _, ps in polys for p in ps] + [p[1] for p in extra_points]
        zs = [z for z, _ in polys] or [0.0]
        self.upp = units_per_px
        self.x0 = min(xs) - margin
        self.y1 = max(ys) + margin
        W = int((max(xs) + margin - self.x0) / units_per_px) + 1
        H = int((self.y1 - (min(ys) - margin)) / units_per_px) + 1
        self.zmin, self.zmax = min(zs), max(zs)
        self.img = Image.new("RGB", (W, H), (16, 16, 20))
        dr = ImageDraw.Draw(self.img)
        span = max(1.0, self.zmax - self.zmin)
        for z, pts in sorted(polys, key=lambda t: t[0]):
            g = int(60 + 110 * (z - self.zmin) / span)
            dr.polygon([self.xy(p) for p in pts], fill=(g, g, g))

    def xy(self, p):
        return ((p[0] - self.x0) / self.upp, (self.y1 - p[1]) / self.upp)

    def draw(self):
        from PIL import ImageDraw
        return ImageDraw.Draw(self.img)
