#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-120
"""Reader for Ghoul binary models (.ghb) -- the model/animation container used by
the Soldier of Fortune data files.  Everything here was derived from the data
files alone (see docs/specs/SPEC-120-ghoul-ghb.md for the layout, the evidence
and the list of UNKNOWN fields).

usage:
  ghb.py info <file.ghb>                 summary of one file
  ghb.py validate <dir> [--deep]         parse every .ghb below <dir> (--deep: also decode
                                         all vertex chunks and node tracks; ~2 min)
  ghb.py json <file.ghb> [out.json]      dump the parsed structure (no bulk data)
  ghb.py obj <file.ghb> <out.obj> [--frame N]
                                         export geometry for frame N (default 0)
  ghb.py normals <dir> <out.npy>         empirical normal-index table from the data
"""
import json
import os
import struct
import sys

import numpy as np

MAGIC = 0x198237FE
TAG = 0x033FB1A3
VERSION = 74
MARK_NODES = 0x05DC159C
MARK_EMPTY = 0x05DC159D       # always followed by a count of 0 in stock data
MARK_MESHES = 0x05DC159E
MARK_LIGHTS = 0x05DC159F


class GhbError(Exception):
    pass


class UnsupportedEncoding(GhbError):
    """Raised for compressed vertex records whose encoding is not yet known."""


# --------------------------------------------------------------------------- reader
class _R:
    def __init__(self, b, p=0):
        self.b = b
        self.p = p

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def u32(self):
        v = struct.unpack_from('<I', self.b, self.p)[0]
        self.p += 4
        return v

    def i32(self):
        v = struct.unpack_from('<i', self.b, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from('<f', self.b, self.p)[0]
        self.p += 4
        return v

    def fv(self, n):
        v = struct.unpack_from('<%df' % n, self.b, self.p)
        self.p += 4 * n
        return list(v)

    def raw(self, n):
        v = self.b[self.p:self.p + n]
        if len(v) != n:
            raise GhbError('truncated at 0x%x' % self.p)
        self.p += n
        return v

    def str(self):
        n = self.u32()
        if n > 4096:
            raise GhbError('implausible string length %d at 0x%x' % (n, self.p - 4))
        return self.raw(n).decode('latin-1')

    def expect(self, value, what):
        v = self.u32()
        if v != value:
            raise GhbError('expected %s 0x%08x at 0x%x, got 0x%08x' % (what, value, self.p - 4, v))


def _track(r, ncomp, kind='f'):
    """Animatable value: u8 is_constant; constant -> one value, else u32 n + n values."""
    const = r.u8()

    def one():
        if kind == 'f':
            return r.fv(ncomp) if ncomp > 1 else r.f32()
        if kind == 'mat':
            return {'m': r.fv(16), 'identity': r.u32()}
        raise ValueError(kind)
    if const:
        return {'const': True, 'value': one()}
    n = r.u32()
    return {'const': False, 'values': [one() for _ in range(n)]}


# ------------------------------------------------------------------- record parsers
def _material(r):
    m = {'name': r.str(), 'flags': r.raw(6).hex()}
    nslots = r.u32()
    m['slots'] = []
    for _ in range(nslots):
        s = {'texture': r.str(), 'u0': r.u32(), 'flags': r.raw(4).hex()}
        if r.u8():                                  # constant texture frame
            s['frame'] = {'const': True, 'value': r.u8()}
        else:                                       # per-model-frame image index (IFL frame)
            n = r.u32()
            s['frame'] = {'const': False, 'values': list(r.raw(n))}
        s['uv_matrix'] = _track(r, 0, 'mat')        # 4x4 texture-coordinate transform
        s['u1'] = r.u8()
        m['slots'].append(s)
    # six animatable properties: colour3, colour3, four scalars (names UNKNOWN)
    m['props'] = [_track(r, n) for n in (3, 3, 1, 1, 1, 1)]
    return m


def _node(r):
    d = {'name': r.str(), 'user': r.str()}
    if r.u8():
        d['visible'] = {'const': True, 'value': r.u8()}
    else:
        n = r.u32()
        words = r.raw(((n + 31) // 32) * 4)
        bits = np.unpackbits(np.frombuffer(words, np.uint8), bitorder='little')[:n]
        d['visible'] = {'const': False, 'values': bits.astype(int).tolist()}
    d['flags'] = r.raw(5).hex()
    d['pos_min'] = r.fv(3)
    d['pos_max'] = r.fv(3)
    n = r.u32()
    if n == 0:
        d['xform'] = _track(r, 0, 'mat')
    else:
        d['xform'] = {'compressed': True, 'frames': n, 'qscale': r.fv(3), 'qinv': r.fv(3),
                      'qofs': r.fv(3), 'data_offset': r.u32()}
    return d


def _geometry(r):
    g = {'u_m1': r.i32()}
    c = [r.u32() for _ in range(13)]
    g['counts'] = c
    # n_apos: positions that vary per frame (corner pos index >= 0)
    # n_anorm: per-frame normals stored as 1-byte indices (corner norm index >= 0)
    (g['u_c0'], g['n_meshes'], g['n_materials'], g['n_pos'], g['n_uv'], g['n_norm'],
     g['n_surf'], g['n_idx'], g['n_corner'], g['n_apos'], g['u_c10'], g['n_anorm'],
     g['u_c12']) = c
    g['offsets'] = [r.u32() for _ in range(9)]
    g['u_b'] = r.raw(2).hex()
    g['bbox'] = r.fv(6)
    g['vscale'] = r.fv(3)      # int16 vertex quantisation: pos = center + v * vinv
    g['vinv'] = r.fv(3)
    g['center'] = r.fv(3)
    g['u_q'] = r.fv(3)
    g['animated'] = r.u8()
    if g['animated']:
        g['n_apos2'] = r.u32()
        g['n_anorm2'] = r.u32()
        nf = r.u32()
        g['frame_chunk'] = [r.u32() for _ in range(nf)]
        g['vanim_size'] = r.u32()
        g['nodeanim_size'] = r.u32()
        nc = r.u32()
        g['chunks'] = []
        for _ in range(nc):
            first, count = r.u32(), r.u32()
            g['chunks'].append({'first': first, 'count': count, 'scale': r.fv(3), 'offset': r.fv(3),
                                'bits': list(r.raw(3)), 'pos_offset': r.u32(), 'norm_offset': r.u32()})
    g['static_size'] = r.u32()
    g['static_offset'] = r.u32()
    return g


def parse(b):
    """Parse the structured part ("part A") of a .ghb file."""
    r = _R(b)
    magic, size_a, size_b = struct.unpack_from('<3I', b, 0)
    if magic != MAGIC:
        raise GhbError('bad magic')
    if size_a + size_b != len(b):
        raise GhbError('size fields do not add up to file length')
    r.p = 12
    r.expect(TAG, 'tag')
    r.expect(VERSION, 'version')
    m = {'size_a': size_a, 'size_b': size_b, 'u0': r.u32(), 'source': r.str()}
    m['frames'] = r.u32()
    nseq = r.u32()
    m['sequences'] = []
    for _ in range(nseq):
        s = {'u_vec': r.fv(3), 'frame_ms': r.f32(), 'name': r.str()}
        ne = r.u32()
        s['events'] = [{'time_ms': r.f32(), 'u_int': r.i32(), 'type': r.str(), 'arg': r.str()}
                       for _ in range(ne)]
        s['first'] = r.u32()
        s['count'] = r.u32()
        m['sequences'].append(s)
    m['materials'] = [_material(r) for _ in range(r.u32())]
    m['u_b'] = r.raw(2).hex()
    m['bbox'] = r.fv(6)
    r.expect(MARK_NODES, 'node marker')
    m['nodes'] = [_node(r) for _ in range(r.u32())]
    r.expect(MARK_EMPTY, 'marker 9d')
    if r.u32() != 0:
        raise GhbError('non-empty 0x05DC159D section (never seen in stock data)')
    r.expect(MARK_MESHES, 'mesh marker')
    m['meshes'] = []
    for _ in range(r.u32()):
        d = _node(r)
        d['u_b'] = r.raw(2).hex()
        d['bbox'] = r.fv(6)
        m['meshes'].append(d)
    m['geometry'] = _geometry(r) if m['meshes'] else None
    r.expect(MARK_LIGHTS, 'light marker')
    m['lights'] = []
    for _ in range(r.u32()):
        d = _node(r)
        d['light_type'] = r.u32()
        d['u_lb'] = r.u8()
        d['props'] = [_track(r, n) for n in (3, 3, 1, 1, 1, 1)]
        m['lights'].append(d)
    if r.p != size_a:
        raise GhbError('part A ends at 0x%x, header says 0x%x' % (r.p, size_a))
    return m


# ------------------------------------------------------------------ static geometry
def static_geometry(m, b):
    """Return the static (unanimated) geometry tables of part B as numpy arrays."""
    g = m['geometry']
    if g is None:
        return None
    base = m['size_a'] + g['static_offset']
    S = b[base:base + g['static_size']]
    o = g['offsets']
    grp = np.frombuffer(S[:o[0]], '<u4').reshape(-1, 3)        # (material, first_surf, n_surf)
    out = {
        'groups': grp,
        'pos': np.frombuffer(S[o[0]:o[1]], '<f4').reshape(-1, 3),
        'uv': np.frombuffer(S[o[1]:o[2]], '<f4').reshape(-1, 2),
        'norm': np.frombuffer(S[o[2]:o[3]], '<f4').reshape(-1, 3),
        'surf': np.frombuffer(S[o[3]:o[4]], '<i2').reshape(-1, 3),  # (mesh, first_idx, n_idx)
        'idx': np.frombuffer(S[o[4]:o[5]], '<i2'),
        'corner': np.frombuffer(S[o[5]:o[6]], '<i2').reshape(-1, 3),  # (pos, norm, uv)
    }
    F = m['frames']
    if g['animated']:
        if not (o[6] == o[7] == o[8] == g['static_size']):
            raise GhbError('unexpected trailing static sections')
        out['frame_pos'] = None
        out['norm_idx'] = None
    else:
        if o[7] - o[6] != F * g['n_apos'] * 6 or o[8] != o[7] or g['static_size'] - o[8] != F * g['n_anorm']:
            raise GhbError('per-frame sections do not match counts')
        # vertex-major on disk (all frames of vertex 0, then vertex 1 ...); exposed as [frame, vertex, xyz]
        out['frame_pos'] = np.frombuffer(S[o[6]:o[7]], '<i2').reshape(g['n_apos'], F, 3).transpose(1, 0, 2)
        out['norm_idx'] = np.frombuffer(S[o[8]:], 'u1').reshape(g['n_anorm'], F)   # normal-major
    return out


def surface_triangles(idx):
    """Decode one surface's index list into corner-index triangles.
    Layout: runs of (signed count, |count| corner indices); negative = fan,
    positive = strip; a 0 count ends the runs; then u16 n + n indices of a
    plain triangle list."""
    tris = []
    i = 0
    n = len(idx)
    while i < n:
        c = int(idx[i])
        i += 1
        if c == 0:
            break
        k = abs(c)
        run = [int(x) for x in idx[i:i + k]]
        i += k
        if c < 0:
            for j in range(1, k - 1):
                tris.append((run[0], run[j], run[j + 1]))
        else:
            for j in range(k - 2):
                if j & 1:
                    tris.append((run[j + 1], run[j], run[j + 2]))
                else:
                    tris.append((run[j], run[j + 1], run[j + 2]))
    if i < n:
        k = int(idx[i])
        i += 1
        lst = [int(x) for x in idx[i:i + k]]
        i += k
        for j in range(0, len(lst) - 2, 3):
            tris.append((lst[j], lst[j + 1], lst[j + 2]))
    return tris


# ------------------------------------------------------------- vertex animation
class _Bits:
    """LSB-first bit reader over a byte string (bit i = bit i%8 of byte i//8)."""

    def __init__(self, data):
        self.d = bytes(data) + b'\0' * 8
        self.n = len(data) * 8
        self.p = 0

    def rd(self, n):
        if n == 0:
            return 0
        if self.p + n > self.n:
            raise GhbError('bit stream overrun')
        o = self.p >> 3
        v = (int.from_bytes(self.d[o:o + 5], 'little') >> (self.p & 7)) & ((1 << n) - 1)
        self.p += n
        return v


def chunk_bytes(m, b, ci):
    g = m['geometry']
    ch = g['chunks'][ci]
    base = m['size_a'] + g['nodeanim_size']
    nxt = g['chunks'][ci + 1]['pos_offset'] if ci + 1 < len(g['chunks']) else g['vanim_size']
    return b[base + ch['pos_offset']:base + ch['norm_offset']], b[base + ch['norm_offset']:base + nxt]


def _ref_bits(v):
    """Width of the reference-vertex field of a mode-1 record for vertex v (v >= 1)."""
    return 1 if v <= 1 else (v - 1).bit_length()


def decode_position_stream(data, nv, nf, bits, strict=True):
    """Decode one chunk position stream -> (q[nf, nv, 3] ints, ok[nv], end_bit)."""
    q = np.zeros((nf, nv, 3), np.int64)
    ok = np.zeros(nv, bool)
    br = _Bits(data)
    if nf == 1:                                   # raw positions, no record headers
        for v in range(nv):
            q[0, v] = [br.rd(bits[0]), br.rd(bits[1]), br.rd(bits[2])]
        ok[:] = True
        return q, ok, br.p
    for v in range(nv):
        mode = br.rd(2)
        if mode == 0:                             # absolute first frame + cumulative deltas
            w = [0, 0, 0]
            for a in range(3):
                q[0, v, a] = br.rd(bits[a])
                w[a] = br.rd(4)
            for f in range(1, nf):
                for a in range(3):
                    d = br.rd(w[a] + 1) - (1 << w[a]) if w[a] else 0
                    q[f, v, a] = q[f - 1, v, a] + d
        elif mode == 1:                           # relative to an earlier vertex's track
            r = br.rd(_ref_bits(v))
            if r >= v:
                raise GhbError('vertex %d references vertex %d' % (v, r))
            mbits = br.rd(4)
            w = [0, 0, 0]
            mn = [0, 0, 0]
            for a in range(3):
                w[a] = br.rd(4)
                if mbits:
                    x = br.rd(mbits + 1)          # sign-magnitude, sign = top bit
                    mag = x & ((1 << mbits) - 1)
                    mn[a] = -mag if x >> mbits else mag
            for f in range(nf):                   # all frames, frame 0 included
                for a in range(3):
                    q[f, v, a] = q[f, r, a] + mn[a] + br.rd(w[a])
        else:
            if strict:
                raise UnsupportedEncoding('vertex %d uses record mode %d (never seen in stock data)' % (v, mode))
            break
        ok[v] = True
    return q, ok, br.p


def decode_vertex_chunk(m, b, ci, strict=True):
    """Quantised animated vertex positions for one chunk: array (frames, n_apos, 3) of ints,
    and a per-vertex validity mask."""
    g = m['geometry']
    ch = g['chunks'][ci]
    pos, _ = chunk_bytes(m, b, ci)
    q, ok, _ = decode_position_stream(pos, g['n_apos'], ch['count'], ch['bits'], strict)
    return q, ok


def frame_positions(m, b, frame, strict=True, st=None):
    """Model-space per-frame vertex positions for a model frame (n_apos, 3) and validity mask."""
    g = m['geometry']
    if not g['animated']:
        st = st or static_geometry(m, b)
        v = st['frame_pos'][frame].astype(float)
        return np.array(g['center']) + v * np.array(g['vinv']), np.ones(g['n_apos'], bool)
    ci = g['frame_chunk'][frame]
    ch = g['chunks'][ci]
    q, ok = decode_vertex_chunk(m, b, ci, strict)
    p = np.array(ch['offset']) + q[frame - ch['first']] * np.array(ch['scale'])
    return p, ok


# ------------------------------------------------------------------ node tracks
def node_track_words(m, b, rec):
    """(frames, 2) array of the two u32 words per frame of a compressed node/mesh/light track."""
    x = rec['xform']
    o = m['size_a'] + x['data_offset']
    return np.frombuffer(b[o:o + 8 * x['frames']], '<u4').reshape(x['frames'], 2).astype(np.int64)


def node_positions(rec, words):
    """Word 1: x = bits 0-9, y = bits 10-19, z = bits 20-31; pos = qofs + q * qinv (model space)."""
    x = rec['xform']
    w = words[:, 1]
    q = np.stack([w & 1023, (w >> 10) & 1023, (w >> 20) & 4095], 1)
    return np.array(x['qofs']) + q * np.array(x['qinv'])


def node_rotation_fields(words):
    """Word 0 fields (SPEC-120; only partly understood): dir = bits 0-7 (index into the
    normal-direction table), n1..n3 = bits 8-11/12-15/16-19 as signed (value - 8) small
    corrections, theta = bits 20-30 in units of 2*pi/2048 (outer rotation about model X:
    R = Rx(-theta) * Q(bits 0-19), column vectors), bit 31 = 0 in stock data."""
    w = words[:, 0]
    return {'dir': w & 0xFF, 'n1': ((w >> 8) & 15) - 8, 'n2': ((w >> 12) & 15) - 8,
            'n3': ((w >> 16) & 15) - 8, 'theta': ((w >> 20) & 2047) * (2 * np.pi / 2048), 'bit31': w >> 31}


# ------------------------------------------------------------------------- export
def build_mesh(m, b, frame=0, strict=True):
    """Triangle soup for one frame: dict with positions, uvs, normals, faces (per material)."""
    st = static_geometry(m, b)
    if st is None:
        return None
    g = m['geometry']
    apos = None
    aok = None
    if g['n_apos']:
        apos, aok = frame_positions(m, b, frame, strict, st)
    faces = []
    for mat, fs, ns in st['groups']:
        for si in range(fs, fs + ns):
            mesh_i, i0, n = st['surf'][si]
            for t in surface_triangles(st['idx'][i0:i0 + n]):
                faces.append((int(mat), int(mesh_i), t))
    verts, uvs, out = [], [], []
    skipped = 0
    for mat, mesh_i, t in faces:
        tri = []
        good = True
        for ci in t:
            pi, ni, ti = (int(x) for x in st['corner'][ci])
            if pi < 0:
                p = st['pos'][~pi]
            else:
                if not aok[pi]:
                    good = False
                p = apos[pi]
            uv = st['uv'][~ti] if ti < 0 else (0.0, 0.0)
            tri.append((p, uv))
        if not good:
            skipped += 1
            continue
        k = len(verts)
        for p, uv in tri:
            verts.append(p)
            uvs.append(uv)
        out.append((mat, mesh_i, (k, k + 1, k + 2)))
    return {'pos': np.array(verts, float).reshape(-1, 3), 'uv': np.array(uvs, float).reshape(-1, 2),
            'faces': out, 'skipped': skipped}


def write_obj(m, mesh, path):
    mats = [x['name'] for x in m['materials']]
    with open(path, 'w') as f:
        f.write('# exported by openmerc tools/formats/ghb.py\n')
        for p in mesh['pos']:
            f.write('v %.5f %.5f %.5f\n' % tuple(p))
        for t in mesh['uv']:
            f.write('vt %.5f %.5f\n' % (t[0], 1.0 - t[1]))
        cur = None
        meshes = [x['name'] for x in m['meshes']]
        for mat, mi, (a, b_, c) in mesh['faces']:
            if (mat, mi) != cur:
                f.write('g %s\n' % (meshes[mi] if mi < len(meshes) else 'mesh%d' % mi))
                f.write('usemtl %s\n' % (mats[mat] if mat < len(mats) else 'mat%d' % mat))
                cur = (mat, mi)
            f.write('f %d/%d %d/%d %d/%d\n' % (a + 1, a + 1, b_ + 1, b_ + 1, c + 1, c + 1))


def estimate_normal_table(root):
    """Empirical estimate of the 256-entry normal-index table (not stored in the data):
    average outward face normal of every corner that uses an index, over all
    non-chunked models below root.  Returns (table[256,3], count[256], consistency[256])."""
    acc = np.zeros((256, 3))
    cnt = np.zeros(256)
    for p in _iter_files(root):
        b = open(p, 'rb').read()
        m = parse(b)
        g = m['geometry']
        if not g or g['animated'] or g['n_anorm'] == 0:
            continue
        st = static_geometry(m, b)
        tris = []
        for _, fs, ns in st['groups']:
            for si in range(fs, fs + ns):
                _, i0, n = st['surf'][si]
                tris += surface_triangles(st['idx'][i0:i0 + n])
        for f in range(m['frames']):
            ap = np.array(g['center']) + st['frame_pos'][f] * np.array(g['vinv']) if g['n_apos'] else None
            nacc = np.zeros((g['n_anorm'], 3))
            for t in tris:
                P = [st['pos'][~int(st['corner'][c][0])] if st['corner'][c][0] < 0 else ap[st['corner'][c][0]]
                     for c in t]
                fn = np.cross(P[1] - P[0], P[2] - P[0])
                for c in t:
                    ni = int(st['corner'][c][1])
                    if ni >= 0:
                        nacc[ni] += fn
            for ni in range(g['n_anorm']):
                ln = np.linalg.norm(nacc[ni])
                if ln > 1e-9:
                    k = int(st['norm_idx'][ni, f])
                    acc[k] += nacc[ni] / ln
                    cnt[k] += 1
    cons = np.linalg.norm(acc, axis=1) / np.maximum(cnt, 1)
    tab = acc / np.maximum(np.linalg.norm(acc, axis=1, keepdims=True), 1e-9)
    return tab, cnt, cons


# --------------------------------------------------------------------------- CLI
def _iter_files(root):
    for d, _, fs in os.walk(root):
        for n in fs:
            if n.lower().endswith('.ghb'):
                yield os.path.join(d, n)


def _summary(m):
    g = m['geometry']
    s = ['source   %s' % m['source'],
         'frames   %d in %d sequences' % (m['frames'], len(m['sequences'])),
         'materials %s' % [x['name'] for x in m['materials']],
         'nodes    %d  meshes %d  lights %d' % (len(m['nodes']), len(m['meshes']), len(m['lights']))]
    if g:
        s.append('geometry static pos %d uv %d norm %d surf %d idx %d corners %d; animated pos %d norm %d; '
                 'chunks %d' % (g['n_pos'], g['n_uv'], g['n_norm'], g['n_surf'], g['n_idx'], g['n_corner'],
                                g['n_apos'], g['n_anorm'], len(g.get('chunks', []))))
    return '\n'.join(s)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == 'info':
        b = open(argv[1], 'rb').read()
        print(_summary(parse(b)))
    elif cmd == 'validate':
        ok = bad = 0
        for p in sorted(_iter_files(argv[1])):
            try:
                b = open(p, 'rb').read()
                m = parse(b)
                static_geometry(m, b)
                if '--deep' in argv:      # decode every vertex chunk and node track too
                    g = m['geometry']
                    for ci in range(len(g['chunks']) if g and g['animated'] else 0):
                        pos, _ = chunk_bytes(m, b, ci)
                        ch = g['chunks'][ci]
                        _, _, end = decode_position_stream(pos, g['n_apos'], ch['count'], ch['bits'])
                        if len(pos) - (end + 7) // 8 != 4:
                            raise GhbError('chunk %d: stream does not end at the padding' % ci)
                    for rec in m['nodes'] + m['meshes'] + m['lights']:
                        if rec['xform'].get('compressed'):
                            P = node_positions(rec, node_track_words(m, b, rec))
                            tol = np.abs(rec['xform']['qinv']) + 1e-3
                            if (P < np.array(rec['pos_min']) - tol).any() or (P > np.array(rec['pos_max']) + tol).any():
                                raise GhbError('node %s: position outside its range' % rec['name'])
                ok += 1
            except Exception as e:      # report and continue
                bad += 1
                print('FAIL %s: %s' % (p, e))
        print('parsed %d, failed %d' % (ok, bad))
        return 1 if bad else 0
    elif cmd == 'json':
        m = parse(open(argv[1], 'rb').read())
        txt = json.dumps(m, indent=1)
        if len(argv) > 2:
            open(argv[2], 'w').write(txt)
        else:
            print(txt)
    elif cmd == 'normals':
        tab, cnt, cons = estimate_normal_table(argv[1])
        np.save(argv[2], np.c_[tab, cnt, cons])
        used = cnt > 0
        print('indices used %d, median consistency %.3f' % (used.sum(), np.median(cons[used])))
    elif cmd == 'obj':
        frame = 0
        if '--frame' in argv:
            frame = int(argv[argv.index('--frame') + 1])
        b = open(argv[1], 'rb').read()
        m = parse(b)
        mesh = build_mesh(m, b, frame, strict=False)
        write_obj(m, mesh, argv[2])
        print('%d triangles written, %d skipped (undecodable vertices)' % (len(mesh['faces']), mesh['skipped']))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
