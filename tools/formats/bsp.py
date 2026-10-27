#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-101
"""Reader for IBSP version 46 maps (SPEC-101).

The lump directory has 22 entries. Lumps 0-18 keep the Quake II numbering and
meaning (qfiles.h, BSPVERSION 38); faces and leafs have larger records; lumps
19-21 are new.

usage:
  bsp.py info <map.bsp>                         lump table and counts
  bsp.py validate <dir>                         parse + cross-check every .bsp under <dir>
  bsp.py obj <map.bsp> <out.obj> [texroot]      world model as OBJ (+ .mtl, UVs if texroot has the .m32 files)
  bsp.py topdown <map.bsp> <out.png> [texroot] [mode] [units_per_px]
                                                orthographic view from +Z; mode = tex | light | lit | flat
  bsp.py atlas <map.bsp> <out.png> [scale]      every face's first-style lightmap packed into one image
"""
import math
import os
import struct
import sys
from collections import namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m32  # noqa: E402  (SPEC-100 reader, same directory)

IDENT = b"IBSP"
VERSION = 46
NUM_LUMPS = 22

LUMP_ENTITIES = 0
LUMP_PLANES = 1
LUMP_VERTEXES = 2
LUMP_VISIBILITY = 3
LUMP_NODES = 4
LUMP_TEXINFO = 5
LUMP_FACES = 6
LUMP_LIGHTING = 7
LUMP_LEAFS = 8
LUMP_LEAFFACES = 9
LUMP_LEAFBRUSHES = 10
LUMP_EDGES = 11
LUMP_SURFEDGES = 12
LUMP_MODELS = 13
LUMP_BRUSHES = 14
LUMP_BRUSHSIDES = 15
LUMP_POP = 16
LUMP_AREAS = 17
LUMP_AREAPORTALS = 18
LUMP_FACEPOLYS = 19   # new in v46: sub-polygons of faces
LUMP_LIGHTS = 20      # new in v46: light source list
LUMP_VOLUMES = 21     # new in v46: volume parameter records (fog-like)

LUMP_NAMES = ["entities", "planes", "vertexes", "visibility", "nodes", "texinfo", "faces",
              "lighting", "leafs", "leaffaces", "leafbrushes", "edges", "surfedges", "models",
              "brushes", "brushsides", "pop", "areas", "areaportals", "facepolys", "lights", "volumes"]

SURF_LIGHT, SURF_SKY, SURF_WARP, SURF_NODRAW, SURF_HINT, SURF_SKIP = 0x1, 0x4, 0x8, 0x80, 0x100, 0x200
SURF_TRANS33, SURF_TRANS66, SURF_ALPHA = 0x10, 0x20, 0x800
CONTENTS_VOLUME = 0x100   # leaf/brush contents bit; such leafs always carry a volume index

Plane = namedtuple("Plane", "normal dist type")
Node = namedtuple("Node", "planenum children mins maxs firstface numfaces")
TexInfo = namedtuple("TexInfo", "vecs flags value texture nexttexinfo")
Face = namedtuple("Face", "planenum side firstedge numedges texinfo volume firstpoly numpolys "
                          "styles lightofs lm_width lm_height lm_scale_s lm_scale_t lm_mins lm_extents")
Leaf = namedtuple("Leaf", "contents cluster area volume mins maxs firstleafface numleaffaces "
                          "firstleafbrush numleafbrushes")
Model = namedtuple("Model", "mins maxs origin headnode firstface numfaces")
Brush = namedtuple("Brush", "firstside numsides contents")
BrushSide = namedtuple("BrushSide", "planenum texinfo")
Area = namedtuple("Area", "numareaportals firstareaportal")
AreaPortal = namedtuple("AreaPortal", "portalnum otherarea")
FacePoly = namedtuple("FacePoly", "firstedge numedges")
Light = namedtuple("Light", "origin color intensity type unknown")
Volume = namedtuple("Volume", "kind f1 f2 f3 f4 f5 rgba i7 f8 f9 f10 reserved")

FACE_FMT = "<HhihhhHiH4BHi4B2h2h"
LEAF_FMT = "<ihhh3h3h4HH"
VOLUME_FMT = "<i5f4Bi3f8i"
assert struct.calcsize(FACE_FMT) == 44
assert struct.calcsize(LEAF_FMT) == 32
assert struct.calcsize(VOLUME_FMT) == 76


class BSPError(ValueError):
    pass


def _records(raw, fmt, lumpname):
    size = struct.calcsize(fmt)
    if len(raw) % size:
        raise BSPError("%s lump length %d not a multiple of %d" % (lumpname, len(raw), size))
    return list(struct.iter_unpack(fmt, raw))


class BSP:
    def __init__(self, data):
        if len(data) < 8 + 8 * NUM_LUMPS:
            raise BSPError("file too short")
        ident, version = struct.unpack_from("<4si", data, 0)
        if ident != IDENT:
            raise BSPError("bad ident %r" % ident)
        if version != VERSION:
            raise BSPError("version %d != %d" % (version, VERSION))
        self.data = data
        self.lumps = [struct.unpack_from("<ii", data, 8 + 8 * i) for i in range(NUM_LUMPS)]
        for i, (o, l) in enumerate(self.lumps):
            if o < 0 or l < 0 or o + l > len(data):
                raise BSPError("lump %d (%s) out of file bounds" % (i, LUMP_NAMES[i]))
        L = self.raw
        self.entities_text = L(LUMP_ENTITIES).split(b"\0", 1)[0].decode("latin-1")
        self.planes = [Plane(r[0:3], r[3], r[4]) for r in _records(L(LUMP_PLANES), "<4fi", "planes")]
        self.vertexes = _records(L(LUMP_VERTEXES), "<3f", "vertexes")
        self.visibility = L(LUMP_VISIBILITY)
        self.nodes = [Node(r[0], r[1:3], r[3:6], r[6:9], r[9], r[10]) for r in _records(L(LUMP_NODES), "<3i6h2H", "nodes")]
        self.texinfo = [TexInfo((r[0:4], r[4:8]), r[8] & 0xFFFFFFFF, r[9], r[10].split(b"\0", 1)[0].decode("latin-1"), r[11])
                        for r in _records(L(LUMP_TEXINFO), "<8f2i32si", "texinfo")]
        self.faces = [Face(r[0], r[1], r[2], r[3], r[4], r[5], r[7], r[8], r[9:13], r[14], r[15], r[16], r[17], r[18],
                           r[19:21], r[21:23]) for r in _records(L(LUMP_FACES), FACE_FMT, "faces")]
        self.lighting = L(LUMP_LIGHTING)
        self.leafs = [Leaf(r[0], r[1], r[2], r[3], r[4:7], r[7:10], r[10], r[11], r[12], r[13])
                      for r in _records(L(LUMP_LEAFS), LEAF_FMT, "leafs")]
        self.leaffaces = [r[0] for r in _records(L(LUMP_LEAFFACES), "<H", "leaffaces")]
        self.leafbrushes = [r[0] for r in _records(L(LUMP_LEAFBRUSHES), "<H", "leafbrushes")]
        self.edges = _records(L(LUMP_EDGES), "<2H", "edges")
        self.surfedges = [r[0] for r in _records(L(LUMP_SURFEDGES), "<i", "surfedges")]
        self.models = [Model(r[0:3], r[3:6], r[6:9], r[9], r[10], r[11]) for r in _records(L(LUMP_MODELS), "<9f3i", "models")]
        self.brushes = [Brush(*r) for r in _records(L(LUMP_BRUSHES), "<3i", "brushes")]
        self.brushsides = [BrushSide(*r) for r in _records(L(LUMP_BRUSHSIDES), "<Hh", "brushsides")]
        self.pop = L(LUMP_POP)
        self.areas = [Area(*r) for r in _records(L(LUMP_AREAS), "<2i", "areas")]
        self.areaportals = [AreaPortal(*r) for r in _records(L(LUMP_AREAPORTALS), "<2i", "areaportals")]
        self.facepolys = [FacePoly(*r) for r in _records(L(LUMP_FACEPOLYS), "<2i", "facepolys")]
        self.lights = [Light(r[0:3], r[3:6], r[6], r[7], r[8]) for r in _records(L(LUMP_LIGHTS), "<7f2i", "lights")]
        self.volumes = [Volume(r[0], r[1], r[2], r[3], r[4], r[5], r[6:10], r[10], r[11], r[12], r[13], r[14:22])
                        for r in _records(L(LUMP_VOLUMES), VOLUME_FMT, "volumes")]

    def raw(self, i):
        o, l = self.lumps[i]
        return self.data[o:o + l]

    # ---- geometry -------------------------------------------------------
    def edge_vertex(self, surfedge_index):
        e = self.surfedges[surfedge_index]
        return self.edges[e][0] if e >= 0 else self.edges[-e][1]

    def face_vertex_indices(self, face):
        return [self.edge_vertex(face.firstedge + k) for k in range(face.numedges)]

    def face_points(self, face):
        return [self.vertexes[i] for i in self.face_vertex_indices(face)]

    def face_normal(self, face):
        n = self.planes[face.planenum].normal
        return (-n[0], -n[1], -n[2]) if face.side else n

    def face_num_styles(self, face):
        if face.lm_width == 0 or face.lm_height == 0:
            return 0
        return sum(1 for s in face.styles if s != 255)

    def lightmap(self, face, style_slot=0):
        """(width, height, RGB bytes) of one style block of a face, or None."""
        if style_slot >= self.face_num_styles(face):
            return None
        n = face.lm_width * face.lm_height * 3
        o = face.lightofs + style_slot * n
        return face.lm_width, face.lm_height, self.lighting[o:o + n]

    def entities(self):
        import re
        out = []
        for blk in re.findall(r"\{([^{}]*)\}", self.entities_text):
            out.append(dict(re.findall(r'"([^"]*)"\s*"([^"]*)"', blk)))
        return out

    def num_clusters(self):
        return struct.unpack_from("<i", self.visibility, 0)[0] if len(self.visibility) >= 4 else 0


def load(path):
    with open(path, "rb") as f:
        return BSP(f.read())


# ---- validation ---------------------------------------------------------
def validate(b):
    """Return a list of SPEC-101 violations (empty list = conforming)."""
    p = []
    nv, ne, nse, npl = len(b.vertexes), len(b.edges), len(b.surfedges), len(b.planes)
    nf, nti, nl, nn = len(b.faces), len(b.texinfo), len(b.leafs), len(b.nodes)
    nvol, npoly = len(b.volumes), len(b.facepolys)
    # lumps must not overlap
    spans = sorted((o, o + l, i) for i, (o, l) in enumerate(b.lumps) if l)
    for (a0, a1, ai), (b0, b1, bi) in zip(spans, spans[1:]):
        if b0 < a1:
            p.append("lumps %d and %d overlap" % (ai, bi))
    for i, pl in enumerate(b.planes):
        ln = math.sqrt(sum(c * c for c in pl.normal))
        if abs(ln - 1) > 1e-3 or not 0 <= pl.type <= 5:
            p.append("plane %d bad normal/type" % i)
            break
    for i, (a, c) in enumerate(b.edges):
        if a >= nv or c >= nv:
            p.append("edge %d vertex out of range" % i)
            break
    for i, e in enumerate(b.surfedges):
        if abs(e) >= ne:
            p.append("surfedge %d out of range" % i)
            break
    for i, t in enumerate(b.texinfo):
        if not (t.nexttexinfo == -1 or 0 <= t.nexttexinfo < nti):
            p.append("texinfo %d next out of range" % i)
    light_len = len(b.lighting)
    for i, f in enumerate(b.faces):
        if f.planenum >= npl or f.texinfo >= nti or f.texinfo < 0 or f.numedges < 3 \
                or f.firstedge < 0 or f.firstedge + f.numedges > nse or f.side not in (0, 1):
            p.append("face %d basic index out of range" % i)
            continue
        if (f.volume < 0) != (f.firstpoly < 0):
            p.append("face %d volume/facepoly presence mismatch" % i)
        if f.volume >= nvol:
            p.append("face %d volume index %d >= %d" % (i, f.volume, nvol))
        if f.firstpoly >= 0 and f.firstpoly + f.numpolys > npoly:
            p.append("face %d facepolys out of range" % i)
        if f.lm_scale_s and f.lm_scale_t:
            if f.lm_width != f.lm_extents[0] // (16 * f.lm_scale_s) + 1 or f.lm_height != f.lm_extents[1] // (16 * f.lm_scale_t) + 1:
                p.append("face %d lightmap size rule broken" % i)
            if f.styles[0] != 0:
                p.append("face %d first style not 0" % i)
        elif f.lm_width or f.lm_height or any(f.styles):
            p.append("face %d unlit face has lightmap data" % i)
        ns = b.face_num_styles(f)
        if ns and (f.lightofs < 0 or f.lightofs + ns * f.lm_width * f.lm_height * 3 > light_len):
            p.append("face %d lightmap outside lighting lump" % i)
    for i, fp in enumerate(b.facepolys):
        if fp.firstedge < 0 or fp.firstedge + fp.numedges > nse or fp.numedges < 3:
            p.append("facepoly %d out of range" % i)
            break
    nclusters = b.num_clusters()
    for i, n in enumerate(b.nodes):
        if n.planenum >= npl or n.firstface + n.numfaces > nf:
            p.append("node %d out of range" % i)
            break
        for c in n.children:
            if (c >= 0 and c >= nn) or (c < 0 and -(c + 1) >= nl):
                p.append("node %d child out of range" % i)
    for i, l in enumerate(b.leafs):
        if l.firstleafface + l.numleaffaces > len(b.leaffaces) or l.firstleafbrush + l.numleafbrushes > len(b.leafbrushes):
            p.append("leaf %d list out of range" % i)
        if l.cluster >= nclusters or l.area >= len(b.areas):
            p.append("leaf %d cluster/area out of range" % i)
        if i and l.volume >= 0 and l.volume >= nvol:
            p.append("leaf %d volume index out of range" % i)
        if i and (l.contents & CONTENTS_VOLUME) and l.volume < 0:
            p.append("leaf %d has volume contents but no volume index" % i)
    if any(x >= nf for x in b.leaffaces):
        p.append("leafface out of range")
    if any(x >= len(b.brushes) for x in b.leafbrushes):
        p.append("leafbrush out of range")
    for i, m in enumerate(b.models):
        if m.firstface + m.numfaces > nf or m.headnode >= nn:
            p.append("model %d out of range" % i)
    for i, br in enumerate(b.brushes):
        if br.firstside + br.numsides > len(b.brushsides):
            p.append("brush %d sides out of range" % i)
            break
    for i, s in enumerate(b.brushsides):
        if s.planenum >= npl or not (s.texinfo == -1 or 0 <= s.texinfo < nti):
            p.append("brushside %d out of range" % i)
            break
    for i, a in enumerate(b.areas):
        if a.firstareaportal + a.numareaportals > len(b.areaportals):
            p.append("area %d portals out of range" % i)
    for i, ap in enumerate(b.areaportals):
        if ap.otherarea >= len(b.areas):
            p.append("areaportal %d out of range" % i)
    for i, lt in enumerate(b.lights):
        if lt.type not in (0, 1, 2, 3):
            p.append("light %d type %d" % (i, lt.type))
            break
    # visibility: Quake II layout, run-length rows decode to exactly ceil(n/8) bytes
    v = b.visibility
    if nclusters:
        offs = struct.unpack_from("<%di" % (2 * nclusters), v, 4)
        rowlen = (nclusters + 7) // 8
        for o in offs:
            if not 4 + 8 * nclusters <= o < len(v):
                p.append("vis offset out of range")
                break
            out, j = 0, o
            while out < rowlen and j < len(v):
                if v[j]:
                    out, j = out + 1, j + 1
                else:
                    out, j = out + v[j + 1], j + 2
            if out != rowlen:
                p.append("vis row length %d != %d" % (out, rowlen))
                break
    return p


# ---- texture lookup -----------------------------------------------------
class TextureSource:
    """Finds textures/<name>.m32 under one or more roots (case-insensitive)."""

    def __init__(self, roots):
        self.index = {}
        for root in roots:
            for dp, _, fn in os.walk(root):
                for f in fn:
                    if f.lower().endswith(".m32"):
                        full = os.path.join(dp, f)
                        rel = os.path.relpath(full, root).replace(os.sep, "/").lower()
                        if rel.startswith("textures/"):
                            self.index.setdefault(rel[9:-4], full)
                        else:
                            # roots that are pak extraction folders: <root>/<pak>/textures/...
                            parts = rel.split("/", 1)
                            if len(parts) == 2 and parts[1].startswith("textures/"):
                                self.index.setdefault(parts[1][9:-4], full)
        self.cache = {}

    def get(self, name):
        key = name.lower()
        if key not in self.cache:
            path = self.index.get(key)
            self.cache[key] = None if path is None else m32.load(path, strict=False)
        return self.cache[key]


# ---- exports ------------------------------------------------------------
def export_obj(b, out_obj, textures=None, model=0):
    m = b.models[model]
    mtl = os.path.splitext(out_obj)[0] + ".mtl"
    used = []
    with open(out_obj, "w", encoding="latin-1") as o:
        o.write("# OpenMerc bsp.py export (SPEC-101), model %d\nmtllib %s\n" % (model, os.path.basename(mtl)))
        vi = 1   # next vertex / texcoord index (1-based, one vt per v)
        ni = 0   # normals: one per face
        by_tex = {}
        for fi in range(m.firstface, m.firstface + m.numfaces):
            by_tex.setdefault(b.texinfo[b.faces[fi].texinfo].texture, []).append(fi)
        for tex in sorted(by_tex):
            used.append(tex)
            o.write("usemtl %s\n" % tex.replace(" ", "_"))
            for fi in by_tex[tex]:
                f = b.faces[fi]
                ti = b.texinfo[f.texinfo]
                pts = b.face_points(f)
                img = textures.get(ti.texture) if textures else None
                w, h = (img.width, img.height) if img else (64, 64)
                for x, y, z in pts:
                    o.write("v %.3f %.3f %.3f\n" % (x, y, z))
                for x, y, z in pts:
                    s = x * ti.vecs[0][0] + y * ti.vecs[0][1] + z * ti.vecs[0][2] + ti.vecs[0][3]
                    t = x * ti.vecs[1][0] + y * ti.vecs[1][1] + z * ti.vecs[1][2] + ti.vecs[1][3]
                    o.write("vt %.5f %.5f\n" % (s / w, 1.0 - t / h))
                o.write("vn %.4f %.4f %.4f\n" % b.face_normal(f))
                ni += 1
                o.write("f %s\n" % " ".join("%d/%d/%d" % (vi + j, vi + j, ni) for j in range(len(pts))))
                vi += len(pts)
    with open(mtl, "w", encoding="latin-1") as o:
        for tex in used:
            o.write("newmtl %s\nKd 1 1 1\nmap_Kd textures/%s.png\n\n" % (tex.replace(" ", "_"), tex))
    return len(used)


def render_topdown(b, out_png, textures=None, mode="lit", units_per_px=4.0, model=0):
    """Orthographic view from +Z of upward-facing faces of one model, z-buffered."""
    import numpy as np
    from PIL import Image, ImageDraw
    m = b.models[model]
    faces = []
    for fi in range(m.firstface, m.firstface + m.numfaces):
        f = b.faces[fi]
        ti = b.texinfo[f.texinfo]
        if ti.flags & (SURF_SKY | SURF_NODRAW | SURF_HINT | SURF_SKIP):
            continue
        n = b.face_normal(f)
        if n[2] <= 0.05:
            continue
        faces.append((fi, f, ti, n))
    allpts = [p for _, f, _, _ in faces for p in b.face_points(f)]
    xs = [p[0] for p in allpts]
    ys = [p[1] for p in allpts]
    x0, y1 = min(xs), max(ys)
    W = int((max(xs) - x0) / units_per_px) + 2
    H = int((y1 - min(ys)) / units_per_px) + 2
    img = np.zeros((H, W, 3), np.float32)
    zbuf = np.full((H, W), -1e9, np.float32)
    for fi, f, ti, n in faces:
        pts = b.face_points(f)
        poly = [((x - x0) / units_per_px, (y1 - y) / units_per_px) for x, y, _ in pts]
        px0 = max(0, int(math.floor(min(p[0] for p in poly))))
        py0 = max(0, int(math.floor(min(p[1] for p in poly))))
        px1 = min(W, int(math.ceil(max(p[0] for p in poly))) + 1)
        py1 = min(H, int(math.ceil(max(p[1] for p in poly))) + 1)
        if px1 <= px0 or py1 <= py0:
            continue
        mask_im = Image.new("L", (px1 - px0, py1 - py0), 0)
        ImageDraw.Draw(mask_im).polygon([(a - px0, c - py0) for a, c in poly], fill=255)
        mask = np.asarray(mask_im) > 0
        if not mask.any():
            continue
        yy, xx = np.nonzero(mask)
        wx = x0 + (xx + px0 + 0.5) * units_per_px
        wy = y1 - (yy + py0 + 0.5) * units_per_px
        pl = b.planes[f.planenum]
        nz = pl.normal[2]
        wz = (pl.dist - pl.normal[0] * wx - pl.normal[1] * wy) / nz
        gy, gx = yy + py0, xx + px0
        closer = wz > zbuf[gy, gx]
        if not closer.any():
            continue
        gy, gx, wx, wy, wz = gy[closer], gx[closer], wx[closer], wy[closer], wz[closer]
        s = wx * ti.vecs[0][0] + wy * ti.vecs[0][1] + wz * ti.vecs[0][2] + ti.vecs[0][3]
        t = wx * ti.vecs[1][0] + wy * ti.vecs[1][1] + wz * ti.vecs[1][2] + ti.vecs[1][3]
        col = np.ones((len(gx), 3), np.float32)
        if mode in ("tex", "lit", "flat"):
            tex = textures.get(ti.texture) if textures else None
            if tex is None:
                col *= 0.6
            elif mode == "flat":
                a = np.frombuffer(tex.mip(tex.num_mips - 1), np.uint8)[:3].astype(np.float32) / 255
                col *= a
            else:
                a = np.frombuffer(tex.mip(0), np.uint8).reshape(tex.height, tex.width, 4)
                u = np.floor(s).astype(np.int64) % tex.width
                v = np.floor(t).astype(np.int64) % tex.height
                col = a[v, u, :3].astype(np.float32) / 255
        if mode in ("light", "lit"):
            lm = b.lightmap(f, 0)
            if lm is not None:
                lw, lh, raw = lm
                L = np.frombuffer(raw, np.uint8).reshape(lh, lw, 3).astype(np.float32) / 255
                ls = (s - f.lm_mins[0]) / (16.0 * f.lm_scale_s)
                lt = (t - f.lm_mins[1]) / (16.0 * f.lm_scale_t)
                # bilinear sample between luxel centres (luxel k sits at mins + k*16*scale)
                ls = np.clip(ls, 0, lw - 1)
                lt = np.clip(lt, 0, lh - 1)
                i0 = np.floor(ls).astype(int)
                j0 = np.floor(lt).astype(int)
                i1 = np.minimum(i0 + 1, lw - 1)
                j1 = np.minimum(j0 + 1, lh - 1)
                fs, ft = (ls - i0)[:, None], (lt - j0)[:, None]
                lum = (L[j0, i0] * (1 - fs) * (1 - ft) + L[j0, i1] * fs * (1 - ft) +
                       L[j1, i0] * (1 - fs) * ft + L[j1, i1] * fs * ft)
                col = col * lum * (2.0 if mode == "lit" else 1.0)
        if mode == "flat":
            col = col * (0.4 + 0.6 * n[2])
        img[gy, gx] = np.clip(col, 0, 1)
        zbuf[gy, gx] = wz
    Image.fromarray((img * 255).astype(np.uint8), "RGB").save(out_png)
    return W, H


def lightmap_atlas(b, out_png, scale=2, width=1024):
    from PIL import Image
    blocks = []
    for fi, f in enumerate(b.faces):
        lm = b.lightmap(f, 0)
        if lm:
            blocks.append((lm[1], lm[0], fi, lm[2]))
    blocks.sort(key=lambda x: (-x[0], -x[1]))
    x = y = shelf = 0
    places = []
    for h, w, fi, raw in blocks:
        if x + w + 1 > width:
            x, y, shelf = 0, y + shelf + 1, 0
        places.append((x, y, w, h, raw))
        x += w + 1
        shelf = max(shelf, h)
    atlas = Image.new("RGB", (width, y + shelf + 1), (24, 24, 32))
    for x, y, w, h, raw in places:
        atlas.paste(Image.frombytes("RGB", (w, h), raw), (x, y))
    if scale != 1:
        atlas = atlas.resize((atlas.width * scale, atlas.height * scale), Image.NEAREST)
    atlas.save(out_png)
    return len(places)


# ---- CLI -----------------------------------------------------------------
def info(b):
    counts = {
        LUMP_PLANES: len(b.planes), LUMP_VERTEXES: len(b.vertexes), LUMP_NODES: len(b.nodes),
        LUMP_TEXINFO: len(b.texinfo), LUMP_FACES: len(b.faces), LUMP_LEAFS: len(b.leafs),
        LUMP_LEAFFACES: len(b.leaffaces), LUMP_LEAFBRUSHES: len(b.leafbrushes), LUMP_EDGES: len(b.edges),
        LUMP_SURFEDGES: len(b.surfedges), LUMP_MODELS: len(b.models), LUMP_BRUSHES: len(b.brushes),
        LUMP_BRUSHSIDES: len(b.brushsides), LUMP_AREAS: len(b.areas), LUMP_AREAPORTALS: len(b.areaportals),
        LUMP_FACEPOLYS: len(b.facepolys), LUMP_LIGHTS: len(b.lights), LUMP_VOLUMES: len(b.volumes)}
    for i, (o, l) in enumerate(b.lumps):
        print("%2d %-12s ofs %9d len %9d %s" % (i, LUMP_NAMES[i], o, l, ("count %d" % counts[i]) if i in counts else ""))
    print("clusters %d, entities %d" % (b.num_clusters(), len(b.entities())))


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "info":
        info(load(argv[1]))
    elif cmd == "validate":
        ok = bad = 0
        tot = {"faces": 0, "lights": 0, "volumes": 0, "facepolys": 0}
        for dp, _, fn in os.walk(argv[1]):
            for f in sorted(fn):
                if not f.lower().endswith(".bsp"):
                    continue
                path = os.path.join(dp, f)
                try:
                    b = load(path)
                    probs = validate(b)
                except (BSPError, struct.error, IndexError) as e:
                    probs = [str(e)]
                    b = None
                if probs:
                    bad += 1
                    print("FAIL %s: %s" % (path, "; ".join(probs[:5])))
                else:
                    ok += 1
                    tot["faces"] += len(b.faces)
                    tot["lights"] += len(b.lights)
                    tot["volumes"] += len(b.volumes)
                    tot["facepolys"] += len(b.facepolys)
        print("parsed %d ok, %d failed; totals %s" % (ok, bad, tot))
        return 0 if bad == 0 else 1
    elif cmd == "obj":
        tex = TextureSource([argv[3]]) if len(argv) > 3 else None
        n = export_obj(load(argv[1]), argv[2], tex)
        print("wrote %s (%d materials)" % (argv[2], n))
    elif cmd == "topdown":
        tex = TextureSource([argv[3]]) if len(argv) > 3 and argv[3] != "-" else None
        mode = argv[4] if len(argv) > 4 else "lit"
        upp = float(argv[5]) if len(argv) > 5 else 4.0
        print("wrote %s %s" % (argv[2], render_topdown(load(argv[1]), argv[2], tex, mode, upp)))
    elif cmd == "atlas":
        sc = int(argv[3]) if len(argv) > 3 else 2
        print("wrote %s with %d lightmaps" % (argv[2], lightmap_atlas(load(argv[1]), argv[2], sc)))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
