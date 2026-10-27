#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - decoder for the Soldier of Fortune 1.07f network protocol (protocol 33).
# Implements: SPEC-200, SPEC-201, SPEC-202, SPEC-203
#
# Written from docs/specs only. The constant tables of the original (direction vectors, configstring
# dictionary, ghoul dictionary, Huffman weights) are no longer in the public specs (2026-10-26); they are read from
# the private record docs/private/ when it is present (decoding captures of the original), otherwise the tool uses
# OpenMerc's own generated tables (tools/net/om_tables.py) and empty dictionaries. OM_SOFNET_TABLES=openmerc forces
# OpenMerc's tables.
#
# usage:
#   sofnet.py selftest                       run the spec test vectors
#   sofnet.py decode <capture.jsonl> [-v] [--bsp <map.bsp>] [--dump <file>]
#                                            decode a udpproxy capture, every byte accounted for

import argparse
import json
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

SPEC_DIR = Path(__file__).resolve().parents[2] / "docs" / "specs"
PRIVATE_DIR = Path(__file__).resolve().parents[2] / "docs" / "private"
import os as _os
TABLES = "original" if (PRIVATE_DIR / "SPEC-201-appendix-A.md").exists() and \
    _os.environ.get("OM_SOFNET_TABLES") != "openmerc" else "openmerc"
sys.path.insert(0, str(Path(__file__).resolve().parent))
import om_tables  # noqa: E402


# ---------------------------------------------------------------------------------------------
# tables from the specs
# ---------------------------------------------------------------------------------------------

def _appendix_block(path, heading):
    lines = path.read_text(encoding="utf-8").split("\n")
    i = next(n for n, l in enumerate(lines) if l.startswith(heading))
    while not lines[i].startswith("```"):
        i += 1
    i += 1
    out = []
    while not lines[i].startswith("```"):
        out.append(lines[i].rstrip("\r"))
        i += 1
    return out


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def load_dirs():
    if TABLES != "original":
        return om_tables.build_dirs()
    rows = _appendix_block(PRIVATE_DIR / "SPEC-201-appendix-A.md", "## Appendix A")
    vecs, raw = [], bytearray()
    for r in rows[1:]:
        parts = r.split("\t")
        idx = int(parts[0])
        assert idx == len(vecs)
        words = parts[4].split()
        b = b"".join(bytes.fromhex(w) for w in words)
        raw += b
        vecs.append(struct.unpack("<3f", b))
    assert len(vecs) == 250
    return vecs, bytes(raw)


def load_cs_dict():
    if TABLES != "original":
        return []
    rows = _appendix_block(PRIVATE_DIR / "SPEC-203-appendix-A.md", "## Appendix A")
    out = []
    for r in rows:
        idx, _, text = r.partition("\t")
        assert int(idx) == len(out)
        out.append(text.encode("latin-1"))
    assert len(out) == 1927, len(out)
    return out


def load_ghoul_dict():
    if TABLES != "original":
        return []
    rows = _appendix_block(PRIVATE_DIR / "SPEC-201-appendix-C.md", "## Appendix C")
    out = []
    for r in rows:
        idx, _, js = r.partition("\t")
        assert int(idx) == len(out)
        out.append(json.loads(js).encode("latin-1"))
    assert len(out) == 3713
    return out


def load_huff_weights():
    if TABLES != "original":
        return list(om_tables.HUFF_WEIGHTS)
    rows = _appendix_block(PRIVATE_DIR / "SPEC-201-appendix-D.md", "## Appendix D")
    w = [struct.unpack("<f", bytes.fromhex("acc52737"))[0]] * 256
    for r in rows[1:]:
        code, raw, _ = r.split("\t")
        w[int(code)] = struct.unpack("<f", bytes.fromhex(raw))[0]
    return w


DIRS, DIR_BYTES = load_dirs()
CS_DICT = load_cs_dict()
CS_DICT_INDEX = {s: i for i, s in enumerate(CS_DICT)}
GHOUL_DICT = load_ghoul_dict()
HUFF_W = load_huff_weights()


def build_huffman(weights):
    slots = [(w, i) for i, w in enumerate(weights)]  # node = int leaf or (branch0, branch1)
    for _ in range(255):
        s1 = None
        for i in range(256):
            if slots[i] is not None and (s1 is None or slots[i][0] < slots[s1][0]):
                s1 = i
        s2 = None
        for i in range(256):
            if i != s1 and slots[i] is not None and (s2 is None or slots[i][0] < slots[s2][0]):
                s2 = i
        w = f32(slots[s1][0] + slots[s2][0])
        slots[s1] = (w, (slots[s2][1], slots[s1][1]))
        slots[s2] = None
    root = next(s for s in slots if s is not None)[1]
    codes = {}

    def walk(n, path):
        if isinstance(n, int):
            codes[n] = path
        else:
            walk(n[0], path + "0")
            walk(n[1], path + "1")
    walk(root, "")
    return root, codes


HUFF_ROOT, HUFF_CODES = build_huffman(HUFF_W)


# ---------------------------------------------------------------------------------------------
# MD4 block checksum (public algorithm, RFC 1320) as used by Quake II's Com_BlockChecksum
# ---------------------------------------------------------------------------------------------

def _md4(data):
    def F(x, y, z): return (x & y) | (~x & z)
    def G(x, y, z): return (x & y) | (x & z) | (y & z)
    def H(x, y, z): return x ^ y ^ z
    def rol(v, s): v &= 0xFFFFFFFF; return ((v << s) | (v >> (32 - s))) & 0xFFFFFFFF
    msg = bytearray(data)
    ml = (len(data) * 8) & 0xFFFFFFFFFFFFFFFF
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack("<Q", ml)
    a, b, c, d = 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476
    for off in range(0, len(msg), 64):
        X = struct.unpack("<16I", msg[off:off + 64])
        aa, bb, cc, dd = a, b, c, d
        for i in range(16):
            k = i
            s = (3, 7, 11, 19)[i % 4]
            if i % 4 == 0: a = rol(a + F(b, c, d) + X[k], s)
            elif i % 4 == 1: d = rol(d + F(a, b, c) + X[k], s)
            elif i % 4 == 2: c = rol(c + F(d, a, b) + X[k], s)
            else: b = rol(b + F(c, d, a) + X[k], s)
        for i in range(16):
            k = (i % 4) * 4 + i // 4
            s = (3, 5, 9, 13)[i % 4]
            if i % 4 == 0: a = rol(a + G(b, c, d) + X[k] + 0x5A827999, s)
            elif i % 4 == 1: d = rol(d + G(a, b, c) + X[k] + 0x5A827999, s)
            elif i % 4 == 2: c = rol(c + G(d, a, b) + X[k] + 0x5A827999, s)
            else: b = rol(b + G(c, d, a) + X[k] + 0x5A827999, s)
        order = (0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15)
        for i in range(16):
            k = order[i]
            s = (3, 9, 11, 15)[i % 4]
            if i % 4 == 0: a = rol(a + H(b, c, d) + X[k] + 0x6ED9EBA1, s)
            elif i % 4 == 1: d = rol(d + H(a, b, c) + X[k] + 0x6ED9EBA1, s)
            elif i % 4 == 2: c = rol(c + H(d, a, b) + X[k] + 0x6ED9EBA1, s)
            else: b = rol(b + H(c, d, a) + X[k] + 0x6ED9EBA1, s)
        a = (a + aa) & 0xFFFFFFFF
        b = (b + bb) & 0xFFFFFFFF
        c = (c + cc) & 0xFFFFFFFF
        d = (d + dd) & 0xFFFFFFFF
    return a, b, c, d


def block_checksum(data):
    a, b, c, d = _md4(data)
    return a ^ b ^ c ^ d


def move_checksum(region, sequence):
    """SPEC-202 6.3"""
    region = bytes(region[:60])
    p = (sequence & 0xFFFFFFFF) % 2996
    salt = bytes([DIR_BYTES[p] ^ (sequence & 0xFF), DIR_BYTES[p + 1],
                  DIR_BYTES[p + 2] ^ ((sequence >> 8) & 0xFF), DIR_BYTES[p + 3]])
    return block_checksum(region + salt) & 0xFF


def map_check_string(bsp):
    """SPEC-203 'Map check string'"""
    ident, ver = struct.unpack_from("<4si", bsp, 0)
    out = ""
    for i in range(22):
        ofs, ln = struct.unpack_from("<ii", bsp, 8 + 8 * i)
        out += "%02X" % (block_checksum(bsp[ofs:ofs + ln]) & 0xFF)
    return out


# ---------------------------------------------------------------------------------------------
# byte reader (SPEC-201 primitives)
# ---------------------------------------------------------------------------------------------

class Underflow(Exception):
    pass


class Msg:
    def __init__(self, data, pos=0):
        self.d = bytes(data)
        self.p = pos

    def left(self):
        return len(self.d) - self.p

    def take(self, n):
        if self.p + n > len(self.d):
            raise Underflow("need %d bytes at %d of %d" % (n, self.p, len(self.d)))
        b = self.d[self.p:self.p + n]
        self.p += n
        return b

    def byte(self): return self.take(1)[0]
    def char(self): return struct.unpack("<b", self.take(1))[0]
    def short(self): return struct.unpack("<h", self.take(2))[0]
    def ushort(self): return struct.unpack("<H", self.take(2))[0]
    def long(self): return struct.unpack("<i", self.take(4))[0]
    def ulong(self): return struct.unpack("<I", self.take(4))[0]
    def float(self): return struct.unpack("<f", self.take(4))[0]

    unterminated = 0

    def string(self):
        # A string that runs to the end of the message ends there (reads past the end yield -1,
        # SPEC-201 primitives); the client's final "disconnect" packet relies on it.
        end = self.d.find(b"\0", self.p)
        if end < 0:
            s = self.d[self.p:]
            self.p = len(self.d)
            Msg.unterminated += 1
            return s
        s = self.d[self.p:end]
        self.p = end + 1
        return s

    def coord(self): return self.short() * 0.125
    def pos(self): return (self.coord(), self.coord(), self.coord())
    def angle(self): return self.char() * 1.40625
    def angle16(self): return self.short() * (360.0 / 65536)

    def dir(self):
        i = self.byte()
        if i >= 250:
            raise ValueError("MSF_ReadDir: out of range (%d)" % i)
        return i


# ---------------------------------------------------------------------------------------------
# bit reader (SPEC-201 B.1, SPEC-202 6.2): LSB first
# ---------------------------------------------------------------------------------------------

class Bits:
    def __init__(self, data, nbits=None):
        self.d = bytes(data)
        self.n = len(self.d) * 8 if nbits is None else nbits
        self.p = 0

    def bit(self):
        if self.p >= self.n:
            raise Underflow("bit stream underflow at bit %d" % self.p)
        v = (self.d[self.p >> 3] >> (self.p & 7)) & 1
        self.p += 1
        return v

    def bits(self, n):
        v = 0
        for i in range(n):
            v |= self.bit() << i
        return v

    def gid(self):
        if not self.bit():
            return 0
        n = self.bits(2)
        return self.bits(4 * n + 4)

    def gtime(self):
        if not self.bit():
            return 0
        neg = self.bit()
        n = self.bits(2)
        v = self.bits(8 * n + 8)
        return -v if neg else v

    def gframeofs(self):
        if not self.bit():
            return 0
        n = self.bits(2)
        return self.bits(8 * n + 8)

    def float32(self):
        return struct.unpack("<f", struct.pack("<I", self.bits(32)))[0]

    def rest_is_zero(self):
        while self.p < self.n:
            if self.bit():
                return False
        return True


# ---------------------------------------------------------------------------------------------
# entity states, player state (SPEC-202)
# ---------------------------------------------------------------------------------------------

EMPTY_ENT = dict(number=0, origin=(0.0, 0.0, 0.0), angles=(0.0, 0.0, 0.0), yawofs=0.0, modelindex=0,
                 modelindex2=0, frame=0, skinnum=0, effects=0, renderfx=0, solid=0, sound=0,
                 sounddata=0, events=((0, 0), (0, 0), (0, 0)), fx=(0,) * 12)


def read_entity_header(m):
    bits = m.byte()
    if bits & 0x80:
        bits |= m.byte() << 8
        if bits & 0x8000:
            bits |= m.byte() << 16
            if bits & 0x800000:
                bits |= m.byte() << 24
    num = m.short() if bits & 0x01000000 else m.byte()
    return bits, num


def read_entity_fields(m, bits, ref):
    """SPEC-202 2.2. Returns the new state; e["_present"] lists the fields on the wire."""
    e = dict(ref)
    pr = []
    e["_present"] = pr
    if bits & 0x00100000: e["modelindex"] = m.byte(); pr.append("modelindex")
    if bits & 0x00000200: e["modelindex2"] = m.byte(); pr.append("modelindex2")
    if bits & 0x00020000:
        e["frame"] = m.byte(); pr.append("frame")
        if bits & 0x00001000:
            raise ValueError("entity frame with both bit 17 and bit 12")
    elif bits & 0x00001000: e["frame"] = m.short(); pr.append("frame")
    b19, b25 = bits & 0x00080000, bits & 0x02000000
    if b19 and b25: e["skinnum"] = m.ulong()
    elif b19: e["skinnum"] = m.byte()
    elif b25: e["skinnum"] = m.ushort()
    if b19 or b25: pr.append("skinnum")
    b16, b14 = bits & 0x00010000, bits & 0x00004000
    if b16 and b14: e["effects"] = m.ulong()
    elif b16: e["effects"] = m.byte()
    elif b14: e["effects"] = m.ushort()
    if b16 or b14: pr.append("effects")
    b10, b8 = bits & 0x00000400, bits & 0x00000100
    if b10 and b8: e["renderfx"] = m.ulong()
    elif b10: e["renderfx"] = m.byte()
    elif b8: e["renderfx"] = m.ushort()
    if b10 or b8: pr.append("renderfx")
    o = list(e["origin"])
    if bits & 0x01: o[0] = m.coord(); pr.append("origin_x")
    if bits & 0x02: o[1] = m.coord(); pr.append("origin_y")
    if bits & 0x20: o[2] = m.coord(); pr.append("origin_z")
    e["origin"] = tuple(o)
    a = list(e["angles"])
    if bits & 0x10: a[0] = m.angle(); pr.append("pitch")
    if bits & 0x04: a[1] = m.angle(); pr.append("yaw")
    if bits & 0x04000000: e["yawofs"] = m.angle(); pr.append("yawofs")
    if bits & 0x08: a[2] = m.angle(); pr.append("roll")
    e["angles"] = tuple(a)
    if bits & 0x00200000:
        s = m.byte()
        if bits & 0x00400000:
            s += 256
        e["sound"] = s
        e["sounddata"] = m.byte()
        pr.append("sound")
    elif bits & 0x00400000:
        raise ValueError("entity header bit 22 without bit 21")
    if bits & 0x40:
        evs = []
        while True:
            ev, par = m.byte(), m.byte()
            evs.append((ev, par))
            if not (ev & 0x80) or len(evs) == 3:
                break
        while len(evs) < 3:
            evs.append((0, 0))
        e["events"] = tuple(evs)
        pr.append("events")
    else:
        e["events"] = ((0, 0), (0, 0), (0, 0))
    if bits & 0x00040000: e["solid"] = m.ulong(); pr.append("solid")
    if bits & 0x00002000:
        mask = m.ushort()
        fx = list(e["fx"])
        for i in range(12):
            if mask & (1 << i):
                fx[i] = m.short()
        if mask & 0xF000:
            raise ValueError("effect-slot mask bits 12..15 set: %04x" % mask)
        e["fx"] = tuple(fx)
        e["_fxmask"] = mask
        pr.append("fx")
    if bits & 0xF8000000:
        raise ValueError("entity header bits 27..31 set: %08x" % bits)
    return e


# player-state fields in wire order (SPEC-202 section 4): flag, name, primitives
# (b byte, c char, s short); values are kept as raw wire integers
PS_FIELDS = [
    (0x00010000, "vweap", "sb"), (0x00020000, "wcountA", "b"), (0x00040000, "wcountB", "s"),
    (0x40000000, "wseq", "b"), (0x10000000, "resync", "b"), (0x20000000, "inhibit", "b"),
    (0x00100000, "remoteghoul", "s"), (0x00000001, "pm_type", "b"), (0x00000002, "origin", "sss"),
    (0x00000004, "velocity", "sss"), (0x00000008, "pm_time", "b"), (0x00000010, "pm_flags", "b"),
    (0x00000020, "gravity", "s"), (0x00000040, "delta_angles", "sss"), (0x08000000, "pmextra", "b"),
    (0x00000080, "viewoffset", "ccc"), (0x00000100, "viewangles", "sss"),
    (0x00008000, "remoteangles", "sss"), (0x00001000, "remoteorigin", "sss"),
    (0x00002000, "remoteview", "bb"), (0x00000200, "kick", "ccc"), (0x00080000, "wkick", "c"),
    (0x00000400, "blend", "bbbb"), (0x00000800, "fov", "b"), (0x00004000, "rdflags", "b"),
    (None, "stats", None),
    (0x00400000, "cinfreeze", "b"), (0x00800000, "music", "b"), (0x01000000, "ambient", "s"),
    (0x02000000, "rank", "bb"), (0x04000000, "spectated", "b"),
]
PS_EMPTY = {name: (tuple(0 for _ in fmt) if fmt else (0,) * 16) for _, name, fmt in PS_FIELDS}


def _prims(m, fmt):
    return tuple(m.byte() if c == "b" else m.char() if c == "c" else m.short() for c in fmt)


def read_playerstate(m, ref):
    ps = dict(ref)
    f = m.ulong()
    ps["_flags"] = f
    ps["_present"] = []
    for flag, name, fmt in PS_FIELDS:
        if flag is None:
            smask = m.ulong()
            ps["_smask"] = smask
            st = list(ps["stats"])
            for i in range(32):
                if smask & (1 << i):
                    if i >= 16:
                        raise ValueError("stat mask bit %d >= 16" % i)
                    st[i] = m.short()
            ps["stats"] = tuple(st)
        elif f & flag:
            ps[name] = _prims(m, fmt)
            ps["_present"].append(name)
    if f & 0x80000000:
        raise ValueError("player-state flag 0x80000000 set")
    return ps


# ---------------------------------------------------------------------------------------------
# user commands (SPEC-202 section 6)
# ---------------------------------------------------------------------------------------------

ZERO_CMD = dict(angles=(0, 0, 0), forward=0, side=0, up=0, buttons=0, lean=0, light=0, msec=0,
                fire=0.0, altfire=0.0)


def read_usercmd(b, ref):
    c = dict(ref)
    a = list(c["angles"])
    for i in range(3):
        if b.bit():
            a[i] = (b.bits(12) << 4) & 0xFFFF
    c["angles"] = tuple(a)
    for k, lim in (("forward", 200), ("side", 160), ("up", 200)):
        if b.bit():
            v = b.bits(10) - 510
            c[k] = v
            c["_" + k + "_raw"] = v
            if abs(v) > lim:
                c.setdefault("_unclamped", []).append((k, v))
                c[k] = max(-lim, min(lim, v))
    if b.bit():
        c["buttons"] = b.bits(8)
    if b.bit():
        c["lean"] = -1 if b.bit() else 1
    else:
        c["lean"] = 0
    if b.bit():
        c["light"] = b.bits(5) << 3
    c["msec"] = b.bits(8)
    if b.bit():
        c["fire"] = b.float32()
    if b.bit():
        c["altfire"] = b.float32()
    return c


# ---------------------------------------------------------------------------------------------
# ghoul bit stream (SPEC-201 Appendix B)
# ---------------------------------------------------------------------------------------------

def gbits(c):
    b = 0
    while c >= (1 << b):
        b += 1
    return b


class GhoulState:
    """Per-client ghoul registry as seen by the receiver."""

    def __init__(self, array_len_rule="list2"):
        self.strings = [b""]
        self.objects = {}
        self.instances = {}
        self.array_len_rule = array_len_rule
        self.stats = Counter()

    def gstring(self, b):
        kind = b.bit()
        if kind == 1:
            i = b.gid()
            self.stats["str_dict"] += 1
            if i >= len(GHOUL_DICT):
                raise ValueError("ghoul dictionary index %d out of range" % i)
            return GHOUL_DICT[i]
        L = 0
        if b.bit():
            mm = b.bits(2)
            L = b.bits(2 * mm + 2)
        block = bytes(b.bits(8) for _ in range(L))
        if L == 0:
            self.stats["str_empty"] += 1
            return b""
        if block[0] == 0xFF:
            self.stats["str_raw"] += 1
            return block[1:]
        unused = block[0]
        nbits = 8 * L - 8 - unused
        hb = Bits(block[1:], nbits)
        out = bytearray()
        while hb.p < hb.n:
            n = HUFF_ROOT
            while not isinstance(n, int):
                n = n[hb.bit()]
            out.append(n)
        self.stats["str_huff"] += 1
        return bytes(out)

    def read_reliable(self, data):
        b = Bits(data)
        rec = {"strings": [], "objects": [], "rmobj": [], "rminst": []}
        while True:
            i = b.gid()
            if i == 0:
                break
            s = self.gstring(b)
            if i != len(self.strings):
                rec.setdefault("idx_mismatch", []).append((i, len(self.strings)))
            self.strings.append(s)
            rec["strings"].append((i, s))
        while True:
            oid = b.gid()
            if oid == 0:
                break
            lists = []
            for li in range(4):
                cnt = b.gid()
                vals, prev = [], -1
                for k in range(cnt):
                    d = b.gid()
                    v = d if k == 0 else d + prev + 1
                    vals.append(v)
                    prev = v
                lists.append(vals)
            cnt = b.gid()
            l5, prev = [], -1
            for k in range(cnt):
                extra = b.gid() if b.bit() else None
                d = b.gid()
                v = d if k == 0 else d + prev + 1
                l5.append((extra, v))
                prev = v
            lists.append(l5)
            obj = {"id": oid, "lists": lists,
                   "w": (gbits(len(lists[0])), gbits(len(lists[1])), gbits(len(lists[3])),
                         gbits(len(lists[4])))}
            self.objects[oid] = obj
            obj["baseline"] = self.read_state(b, obj)
            rec["objects"].append(obj)
        while True:
            x = b.gid()
            if x == 0:
                break
            rec["rmobj"].append(x)
            self.objects.pop(x, None)
        while True:
            x = b.gid()
            if x == 0:
                break
            rec["rminst"].append(x)
            self.instances.pop(x, None)
        rec["bits_used"] = b.p
        rec["pad_ok"] = b.rest_is_zero() and (len(data) * 8 - rec["bits_used"]) < 8
        return rec

    def array_len(self, obj):
        r = self.array_len_rule
        if r == "list1": return len(obj["lists"][0])
        if r == "list2": return len(obj["lists"][1])
        if r == "list3": return len(obj["lists"][2])
        if r == "list4": return len(obj["lists"][3])
        if r == "list5": return len(obj["lists"][4])
        raise ValueError(r)

    def read_state(self, b, obj):
        w0, w1, w2, w3 = obj["w"]
        st = {}
        st["f1"] = b.bit()
        if b.bit(): st["g2"] = b.bits(4)
        st["fA"] = b.bit()
        st["fB"] = b.bit()
        if w1 > 0 and b.bit(): st["g5"] = b.bits(w1)
        if w0 > 0 and b.bit(): st["g6"] = b.bits(w0)
        if b.bit(): st["g7"] = b.bits(2)
        if b.bit(): st["g8"] = (b.gtime(), b.gtime())
        if st["fA"] and b.bit():
            st["g9"] = 1e30 if b.bit() else b.float32()
        if b.bit():
            st["g10"] = [b.bits(8) if b.bit() else None for _ in range(4)]
        if b.bit():
            t = {}
            if b.bit(): t["rot"] = b.bits(32)
            if b.bit(): t["scale"] = b.float32()
            if b.bit():
                tr = []
                for ax in range(3):
                    if b.bit():
                        tr.append(b.float32() if b.bit() == 0 else None)
                        if tr[-1] is None:
                            tr[-1] = b.bits(11) / 16.0 - 64
                    else:
                        tr.append(None)
                t["trans"] = tr
            st["g11"] = t
        if b.bit():
            n = self.array_len(obj)
            k, changes = 0, 0
            while k < n:
                if b.bit() == 0:
                    for _ in range(min(4, n - k)):
                        changes += b.bit()
                        k += 1
                else:
                    v = b.bit()
                    c = b.bits(5)
                    k += c + 6
                    changes += v * (c + 6)
            if k != n:
                st["g12_overrun"] = (k, n)
            st["g12"] = changes
        if b.bit():
            xs, ys = [], []
            while True:
                key = b.gid()
                if key == 0: break
                xs.append((key, b.bits(w1)))
            while True:
                key = b.gid()
                if key == 0: break
                ys.append((key, b.bits(w1)))
            st["g13"] = (xs, ys)
        if b.bit():
            add, rem = [], []
            while b.bit():
                add.append((b.bits(w2), b.bits(w1), b.bits(w3)))
            while b.bit():
                rem.append((b.bits(w2), b.bits(w1)))
            st["g14"] = (add, rem)
        return st

    def read_unreliable(self, data):
        b = Bits(data)
        ups = []
        while True:
            iid = b.gid()
            if iid == 0:
                break
            oid = b.gid()
            base = b.gframeofs()
            obj = self.objects.get(oid)
            if obj is None:
                raise ValueError("instance %d of unknown object %d" % (iid, oid))
            st = self.read_state(b, obj)
            self.instances[iid] = oid
            ups.append((iid, oid, base, st))
        return {"updates": ups, "bits_used": b.p,
                "pad_ok": b.rest_is_zero() and (len(data) * 8 - b.p) < 8}


# ---------------------------------------------------------------------------------------------
# server -> client messages (SPEC-201)
# ---------------------------------------------------------------------------------------------

SVC_NAMES = ["svc_bad", "svc_temp_entity", "svc_layout", "svc_UNUSED", "svc_sound_info", "svc_effect",
             "svc_equip", "svc_nop", "svc_disconnect", "svc_reconnect", "svc_sound", "svc_print",
             "svc_nameprint", "svc_stufftext", "svc_serverdata", "svc_configstring",
             "svc_spawnbaseline", "svc_centerprint", "svc_captionprint", "svc_download",
             "svc_playerinfo", "svc_packetentities", "svc_deltapacketentities", "svc_frame",
             "svc_culledEvent", "svc_damagetexture", "svc_ghoulreliable", "svc_ghoulunreliable",
             "svc_ric", "svc_restart_predn", "svc_rebuild_pred_inv", "svc_countdown", "svc_cinprintf",
             "svc_playernamecols", "svc_sp_print", "svc_removeconfigstring", "svc_sp_print_data_1",
             "svc_sp_print_data_2", "svc_welcomeprint", "svc_sp_print_obit",
             "svc_force_con_notify"]
CLC_NAMES = ["clc_bad", "clc_nop", "clc_move", "clc_userinfo", "clc_stringcmd"]

# temp-entity payloads (SPEC-201 section 1): p pos, d dir, b byte, s short, l long, I impact,
# N = byte n then n x (short, byte)
TE_FMT = {0: "pd", 1: "p", 2: "pbbbb", 3: "I", 4: "pl", 5: "", 6: "Ipbbb", 7: "Ipbbb", 8: "pbd",
          9: "bppbb", 10: "pd", 11: "psb", 12: "p", 13: "I", 14: "pd", 15: "pl", 16: "", 17: "pl",
          18: "pdbbbbbbb", 19: "I", 20: "p", 21: "p", 22: "", 23: "pb", 24: "ppN", 25: "pp",
          26: "pb", 27: "ppll", 28: "pl", 29: "I", 30: "I", 31: "pd", 32: "pdb", 33: "pb",
          34: "pp", 35: "sssb", 36: "sssss", 37: "ss", 38: "p", 39: "s", 40: "pb", 41: "sssb",
          42: "sss", 43: "pp", 44: "pb", 45: "pbbbb", 46: "pbb", 47: "ps", 48: "ps", 49: "pd",
          50: "ppss", 51: "p", 52: "ps", 53: "pl", 54: "p", 55: "pbb", 56: "pbb", 57: "pp",
          58: "p", 59: "p", 60: "bbbpp", 61: "bbbpp", 62: "bbbpp", 63: "bbpp", 64: "ppbbb"}


def read_fmt(m, fmt):
    out = []
    for ch in fmt:
        if ch == "p": out.append(m.pos())
        elif ch == "d": out.append(("dir", m.dir()))
        elif ch == "b": out.append(m.byte())
        elif ch == "s": out.append(m.short())
        elif ch == "l": out.append(m.long())
        elif ch == "I":
            kind = m.byte()
            if kind == 0: out.append(("impact0", m.pos(), m.pos()))
            elif kind == 1: out.append(("impact1", m.pos()))
            elif kind == 2: out.append(("impact2", m.pos(), m.pos(), m.short(), m.byte()))
            elif kind == 3: out.append(("impact3", m.pos(), m.pos(), m.byte()))
            else: raise ValueError("impact kind %d" % kind)
        elif ch == "N":
            n = m.byte()
            out.append([(m.short(), m.byte()) for _ in range(n)])
    return out


class Session:
    """Receiver state for one client connection (decoder side)."""

    def __init__(self, ghoul_rule="list2"):
        self.cs = {}
        self.cs_literals = []
        self.cs_dict_hits = 0
        self.baselines = {}
        self.frames = {}          # frame -> (ents dict, ps)
        self.ghoul = GhoulState(ghoul_rule)
        self.ghoul_stats = Counter()
        self.findings = defaultdict(list)
        self.svc_count = Counter()
        self.clc_count = Counter()
        self.te_count = Counter()
        self.stringcmds = []
        self.stuff = []
        self.serverdata = None
        self.frames_seen = []
        self.moves = []
        self.last_cmd = None
        self.delta_refs = []
        self.checks = defaultdict(list)
        self.ent_numbers = Counter()
        self.conn = 0

    def new_connection(self):
        self.conn += 1
        self.frames = {}
        self.baselines = {}
        self.ghoul = GhoulState(self.ghoul.array_len_rule)
        self.cs = {}

    def note(self, key, text):
        if len(self.findings[key]) < 20:
            self.findings[key].append(text)

    # --- server -> client -------------------------------------------------------------------
    def parse_svc(self, m, ctx):
        names = []
        while m.left() > 0:
            start = m.p
            cmd = m.byte()
            if cmd > 40:
                raise ValueError("Illegible server message %d" % cmd)
            name = SVC_NAMES[cmd]
            names.append(name)
            self.svc_count[name] += 1
            if cmd in (20, 21, 22):
                raise ValueError("Out of place frame data (%s)" % name)
            if cmd in (0, 3):
                raise ValueError("fatal svc %d" % cmd)
            h = getattr(self, "svc_%d" % cmd)
            h(m, ctx)
            ctx.setdefault("spans", []).append((name, start, m.p))
        return names

    def svc_1(self, m, ctx):
        t = m.byte()
        if t > 64:
            raise ValueError("temp entity type %d > 64" % t)
        self.te_count[t] += 1
        read_fmt(m, TE_FMT[t])

    def svc_2(self, m, ctx): m.string()

    def svc_4(self, m, ctx):
        mask = m.byte()
        if mask & ~0x0F:
            raise ValueError("Invalid soundinfo type")
        for i in range(4):
            if mask & (1 << i):
                m.byte()

    def svc_5(self, m, ctx):
        eff, fl = m.byte(), m.byte()
        if fl & 0x30:
            self.note("svc_effect flags 0x10/0x20", "flags %02x" % fl)
        if fl & 0x01:
            m.pos()
        elif fl & 0x02:
            m.short()
        elif fl & 0x04:
            m.short()
            if fl & 0x08:
                m.short()
            m.short()
        if fl & 0x80:
            ext = m.byte()
            if ext & 0x01: m.short()
            if ext & 0x02: m.byte()
            if ext & 0x04: m.pos()
            if ext & 0x08: m.pos()
            if ext & 0x10: m.pos()
            if ext & 0x20: m.pos()
            if ext & 0x40: m.short()
            if ext & 0x80: m.byte()
        self.note("svc_effect samples", "effect %d (%s) flags %02x" %
                  (eff, self.cs.get(647 + eff, b"?").decode("latin-1"), fl))

    def svc_6(self, m, ctx):
        kind = m.byte()
        if kind == 1:
            for _ in range(3):
                for _ in range(m.byte()):
                    m.string()
                    m.long()

    def svc_7(self, m, ctx): pass

    def svc_8(self, m, ctx):
        ctx["disconnect"] = m.ushort()

    def svc_9(self, m, ctx): ctx["reconnect"] = True

    def svc_10(self, m, ctx):
        fl = m.byte()
        idx = m.byte()
        if fl & 0x20: idx += 256
        if fl & 0x01: m.byte()
        if fl & 0x02: m.byte()
        if fl & 0x10: m.byte()
        if fl & 0x08:
            ec = m.short()
            if (ec >> 3) > 1024:
                raise ValueError("sound entity %d" % (ec >> 3))
        else:
            self.note("svc_sound without 0x08", "flags %02x" % fl)
        if fl & 0x04: m.pos()
        if fl & 0xC0:
            raise ValueError("svc_sound unknown flags %02x" % fl)

    def svc_11(self, m, ctx):
        lvl = m.byte()
        s = m.string()
        ctx.setdefault("prints", []).append((lvl, s))

    def svc_12(self, m, ctx):
        m.byte(); m.byte()
        ctx.setdefault("prints", []).append(("name", m.string()))

    def svc_13(self, m, ctx):
        s = m.string()
        self.stuff.append((ctx["t"], s))

    def svc_14(self, m, ctx):
        sd = dict(protocol=m.long(), spawncount=m.long(), demo=m.byte(), deathmatch=m.byte(),
                  gamedir=m.string(), playernum=m.short(), levelname=m.string())
        self.serverdata = sd
        if sd["protocol"] != 33:
            raise ValueError("protocol %d" % sd["protocol"])

    def svc_15(self, m, ctx):
        idx = m.short()
        code = m.short()
        if not 0 <= idx <= 3554:
            raise ValueError("configstring > MAX_CONFIGSTRINGS (%d)" % idx)
        if code < 0:
            s = m.string()
            if code != -1:
                self.note("configstring negative code != -1", "idx %d code %d" % (idx, code))
            if s in CS_DICT_INDEX:
                self.note("configstring literal that is in the dictionary",
                          "idx %d %r (dict %d)" % (idx, s, CS_DICT_INDEX[s]))
            self.cs_literals.append((idx, s))
        else:
            if code >= len(CS_DICT) and TABLES != "original":
                s = ("<dict %d>" % code).encode()    # original's dictionary not available (docs/private)
            elif code >= len(CS_DICT):
                raise ValueError("configstring dictionary code %d out of range" % code)
            else:
                s = CS_DICT[code]
            self.cs_dict_hits += 1
        if len(s) > 63:
            self.note("configstring longer than 63", "idx %d len %d" % (idx, len(s)))
        self.cs[idx] = s

    def svc_16(self, m, ctx):
        bits, num = read_entity_header(m)
        e = read_entity_fields(m, bits, EMPTY_ENT)
        e["number"] = num
        self.baselines[num] = e
        ctx["baselines"] = ctx.get("baselines", 0) + 1

    def svc_17(self, m, ctx):
        ctx.setdefault("prints", []).append(("center", m.string()))

    def svc_18(self, m, ctx): m.short()

    def svc_19(self, m, ctx):
        size = m.short()
        m.byte()
        if size > 0:
            m.take(size)

    def svc_23(self, m, ctx):
        frame, delta = m.long(), m.long()
        gtime = m.float()
        n = m.byte()
        if n > 32:
            raise ValueError("areabits %d > 32" % n)
        m.take(n)
        if m.byte() != 20:
            raise ValueError("CL_ParseFrame: not playerinfo")
        self.svc_count["svc_playerinfo"] += 1
        ref_ents, ref_ps = {}, PS_EMPTY
        valid = True
        if delta > 0:
            if delta in self.frames:
                ref_ents, ref_ps = self.frames[delta]
            else:
                valid = False
                self.note("delta from unknown frame", "frame %d delta %d" % (frame, delta))
        ps = read_playerstate(m, ref_ps)
        for name in ps["_present"]:
            self.checks["ps field present => differs from reference"].append(ps[name] != ref_ps[name])
        for i in range(16):
            if ps["_smask"] & (1 << i):
                self.checks["stat present => differs"].append(ps["stats"][i] != ref_ps["stats"][i])
            else:
                self.checks["stat absent => unchanged"].append(True)
        if m.byte() != 21:
            raise ValueError("CL_ParsePacketEntities: not packetentities")
        self.svc_count["svc_packetentities"] += 1
        ents = dict(ref_ents)
        nrec = 0
        while True:
            bits, num = read_entity_header(m)
            if bits == 0 and num == 0:
                break
            nrec += 1
            if num >= 1024:
                raise ValueError("bad entity number %d" % num)
            if bits & 0x800:
                if num not in ents:
                    self.note("REMOVE of entity not in reference", "frame %d ent %d" % (frame, num))
                ents.pop(num, None)
                if bits & ~0x01808880:
                    self.note("REMOVE with other bits", "%08x" % bits)
                continue
            in_ref = num in ents
            ref = ents.get(num) or self.baselines.get(num) or dict(EMPTY_ENT, number=num)
            e = read_entity_fields(m, bits, ref)
            e["number"] = num
            ents[num] = e
            self.ent_numbers[num] += 1
            for f in e["_present"]:
                if f in ("events", "sound", "fx"):
                    continue
                if f.startswith("origin_"):
                    k = "xyz".index(f[-1])
                    diff = e["origin"][k] != ref["origin"][k]
                elif f in ("pitch", "yaw", "roll"):
                    k = ("pitch", "yaw", "roll").index(f)
                    diff = e["angles"][k] != ref["angles"][k]
                else:
                    diff = e[f] != ref[f]
                self.checks["entity field present => differs from reference"].append(diff)
                if not diff:
                    self.note("entity field present but equal after quantisation",
                              "frame %d ent %d %s" % (frame, num, f))
            if in_ref and not e["_present"]:
                self.checks["unchanged entity <=255 not written"].append(num > 255)
            if e["events"][0][0] and "events" not in e["_present"]:
                self.checks["events present when event 1 non-zero"].append(False)
        self.frames[frame] = (ents, ps)
        for k in [k for k in self.frames if k < frame - 32]:
            del self.frames[k]
        self.frames_seen.append(dict(t=ctx["t"], conn=self.conn, frame=frame, delta=delta,
                                     time=gtime, areabytes=n,
                                     nrec=nrec, nents=len(ents), valid=valid, psflags=ps["_flags"],
                                     origin=ps["origin"], seq=ctx["seq"]))
        ctx["frame"] = frame

    def svc_24(self, m, ctx):
        ev, par, dist, d = m.byte(), m.byte(), m.byte(), m.dir()
        if not 1 <= ev <= 23:
            self.note("culledEvent event id out of 1..23", "%d" % ev)

    def svc_25(self, m, ctx): m.short(); m.byte()

    def svc_26(self, m, ctx):
        n = m.short()
        data = m.take(n)
        try:
            rec = self.ghoul.read_reliable(data)
            self.ghoul_stats["reliable_ok" if rec["pad_ok"] else "reliable_pad_bad"] += 1
            self.ghoul_stats["strings"] += len(rec["strings"])
            self.ghoul_stats["objects"] += len(rec["objects"])
            if rec.get("idx_mismatch"):
                self.note("ghoul string index != running count", str(rec["idx_mismatch"][:3]))
            if not rec["pad_ok"]:
                self.note("ghoul reliable padding", "len %d bits used %d" % (n, rec["bits_used"]))
        except Exception as ex:  # noqa: BLE001 - decoder keeps going, framing is by length
            self.ghoul_stats["reliable_fail"] += 1
            self.note("ghoul reliable decode failure", "%s: %s" % (type(ex).__name__, ex))

    def svc_27(self, m, ctx):
        n = m.short()
        data = m.take(n)
        if ctx.get("frame") is None:
            self.note("ghoulunreliable before svc_frame in packet", "t %.3f" % ctx["t"])
        try:
            rec = self.ghoul.read_unreliable(data)
            self.ghoul_stats["unreliable_ok" if rec["pad_ok"] else "unreliable_pad_bad"] += 1
            self.ghoul_stats["instance_updates"] += len(rec["updates"])
            for u in rec["updates"]:
                if "g12_overrun" in u[3]:
                    self.ghoul_stats["g12_overrun"] += 1
            if not rec["pad_ok"]:
                self.note("ghoul unreliable padding", "len %d bits used %d" % (n, rec["bits_used"]))
        except Exception as ex:  # noqa: BLE001
            self.ghoul_stats["unreliable_fail"] += 1
            self.note("ghoul unreliable decode failure", "%s: %s" % (type(ex).__name__, ex))

    def svc_28(self, m, ctx):
        cnt = m.byte()
        for _ in range(cnt):
            h = m.byte()
            eid, cls = h & 0x0F, (h >> 4) & 3
            if h & 0xC0:
                self.note("svc_ric header bits 6..7", "%02x" % h)
            if eid <= 4:
                if cls == 0: m.byte()
                elif cls == 1: m.short()
                elif cls == 2: m.short(); m.byte()
                else: m.long()

    def svc_29(self, m, ctx): m.byte()
    def svc_30(self, m, ctx): m.take(640)
    def svc_31(self, m, ctx): m.long()

    def svc_32(self, m, ctx):
        m.short(); m.short(); m.byte(); m.string()

    def svc_33(self, m, ctx):
        count = m.byte()
        covered = 0
        while covered < count:
            b = m.byte()
            if b & 0x80:
                first, last = m.byte(), m.byte()
                covered += last - first + 1
            else:
                m.byte()
                covered += 1

    def svc_34(self, m, ctx): ctx.setdefault("sp", []).append(m.short())

    def svc_35(self, m, ctx):
        idx = m.short()
        self.cs.pop(idx, None)

    def svc_36(self, m, ctx):
        sid = m.short()
        n = m.byte()
        m.take(n)
        ctx.setdefault("sp", []).append(sid)

    def svc_37(self, m, ctx):
        sid = m.short()
        n = m.short()
        m.take(n)
        ctx.setdefault("sp", []).append(sid)

    def svc_38(self, m, ctx): pass

    def svc_39(self, m, ctx):
        sid = m.short()
        n = m.byte()
        m.take(n)
        ctx.setdefault("sp", []).append(sid)

    def svc_40(self, m, ctx): pass

    # --- client -> server -------------------------------------------------------------------
    def parse_clc(self, m, ctx):
        names = []
        moves = 0
        while m.left() > 0:
            start = m.p
            cmd = m.byte()
            if cmd > 4 or cmd == 0:
                raise ValueError("bad clc %d" % cmd)
            names.append(CLC_NAMES[cmd])
            self.clc_count[CLC_NAMES[cmd]] += 1
            if cmd == 1:
                pass
            elif cmd == 3:
                ctx["userinfo"] = m.string()
            elif cmd == 4:
                s = m.string()
                self.stringcmds.append((ctx["t"], ctx["seq"], s))
            elif cmd == 2:
                moves += 1
                csum = m.byte()
                rstart = m.p
                last = m.long()
                b = Bits(m.d[m.p:])
                c1 = read_usercmd(b, ZERO_CMD)
                c2 = read_usercmd(b, c1)
                c3 = read_usercmd(b, c2)
                nbytes = (b.p + 7) // 8
                pad = Bits(m.d[m.p:m.p + nbytes])
                pad.p = b.p
                pad_zero = pad.rest_is_zero()
                m.p += nbytes
                region = m.d[rstart:m.p]
                calc = move_checksum(region, ctx["seq"])
                self.moves.append(dict(t=ctx["t"], seq=ctx["seq"], conn=self.conn, last=last,
                                       csum=csum, calc=calc,
                                       cmds=(c1, c2, c3), bits=b.p, pad_zero=pad_zero))
            ctx.setdefault("spans", []).append((CLC_NAMES[cmd], start, m.p))
        return names


# ---------------------------------------------------------------------------------------------
# netchan (SPEC-200)
# ---------------------------------------------------------------------------------------------

class Chan:
    def __init__(self, name):
        self.name = name
        self.incoming = 0
        self.dropped = 0
        self.ooo = 0
        self.reliable_rx = 0
        self.out_seq_expected = 1
        self.prev_rel_bit = None
        self.rx_bit = 0


def decode_capture(path, verbose=False, bsp=None, dump=None, ghoul_rule="list2"):
    recs = [json.loads(l) for l in open(path)]
    sess = Session(ghoul_rule)
    report = []
    out = open(dump, "w", encoding="utf-8") if dump else None
    chans = {"c2s": Chan("c2s"), "s2c": Chan("s2c")}
    totals = Counter()
    oob = []
    bad = []
    qports = set()
    for r in recs:
        data = bytes.fromhex(r["hex"])
        d = r["dir"]
        totals[d + "_bytes"] += len(data)
        totals[d + "_packets"] += 1
        if data[:4] == b"\xff\xff\xff\xff":
            text = data[4:]
            oob.append((r["t"], d, text))
            totals[d + "_oob"] += 1
            if d == "s2c" and text.startswith(b"client_connect"):
                # a new netchan starts (SPEC-200); a reconnect after a map change lands here too
                chans = {"c2s": Chan("c2s"), "s2c": Chan("s2c")}
                sess.new_connection()
            if out:
                out.write("%9.3f %s OOB %r\n" % (r["t"], d, text))
            continue
        m = Msg(data)
        if len(data) < 8:
            bad.append((r["t"], d, "runt"))
            continue
        w1, w2 = m.ulong(), m.ulong()
        seq, rel = w1 & 0x7FFFFFFF, w1 >> 31
        ack, ackbit = w2 & 0x7FFFFFFF, w2 >> 31
        ch = chans[d]
        if d == "c2s":
            qports.add(m.ushort())
        if seq <= ch.incoming:
            ch.ooo += 1
            continue
        if seq != ch.incoming + 1 and ch.incoming:
            ch.dropped += seq - ch.incoming - 1
        ch.incoming = seq
        other = chans["s2c" if d == "c2s" else "c2s"]
        # SPEC-200 netchan: word 2 = last sequence received from the peer | peer's reliable bit
        sess.checks["netchan ack = last peer sequence seen"].append(ack == other.incoming)
        sess.checks["netchan ack bit = toggles of peer reliable messages"].append(
            ackbit == other.rx_bit)
        if rel:
            ch.reliable_rx += 1
            ch.rx_bit ^= 1
        ctx = {"t": r["t"], "seq": seq, "dir": d}
        try:
            if d == "s2c":
                names = sess.parse_svc(m, ctx)
            else:
                names = sess.parse_clc(m, ctx)
            if m.left() != 0:
                bad.append((r["t"], d, "%d bytes left" % m.left()))
            totals[d + "_parsed_bytes"] += m.p
        except Exception as ex:  # noqa: BLE001
            bad.append((r["t"], d, "seq %d at %d: %s: %s" % (seq, m.p, type(ex).__name__, ex)))
            names = ["<error>"]
        if d == "s2c":
            check_svc_order(sess, names)
        if out:
            out.write("%9.3f %s seq %d%s ack %d/%d len %d: %s\n" % (
                r["t"], d, seq, "R" if rel else "", ack, ackbit, len(data), " ".join(names)))
            if verbose:
                for nm, a, b in ctx.get("spans", []):
                    out.write("            %-24s %s\n" % (nm, data[a:b].hex(" ")[:240]))
    if out:
        out.close()
    verify_streams(sess, recs)
    return sess, chans, totals, oob, bad, qports


RELIABLE_KINDS = {"svc_configstring", "svc_stufftext", "svc_serverdata", "svc_spawnbaseline",
                  "svc_ghoulreliable", "svc_print", "svc_nameprint", "svc_sp_print",
                  "svc_sp_print_data_1", "svc_sp_print_data_2", "svc_sp_print_obit", "svc_ric",
                  "svc_restart_predn", "svc_rebuild_pred_inv", "svc_welcomeprint",
                  "svc_playernamecols", "svc_removeconfigstring", "svc_disconnect",
                  "svc_centerprint", "svc_force_con_notify"}


def check_svc_order(sess, names):
    """SPEC-201 'Delivery': reliable part, culled events, frame, ghoul unreliable, queued."""
    if "svc_frame" not in names:
        return
    fi = names.index("svc_frame")
    c = sess.checks
    c["datagram: at most one svc_ghoulreliable"].append(names.count("svc_ghoulreliable") <= 1)
    c["datagram: at most one svc_ghoulunreliable"].append(names.count("svc_ghoulunreliable") <= 1)
    c["datagram: svc_ghoulreliable before svc_frame"].append(
        all(i < fi for i, n in enumerate(names) if n == "svc_ghoulreliable"))
    c["datagram: svc_culledEvent before svc_frame"].append(
        all(i < fi for i, n in enumerate(names) if n == "svc_culledEvent"))
    if "svc_ghoulunreliable" in names:
        c["datagram: svc_ghoulunreliable right after svc_frame"].append(
            names.index("svc_ghoulunreliable") == fi + 1)
    after = names[fi + 1:]
    if after and after[0] == "svc_ghoulunreliable":
        after = after[1:]
    for n in after:
        c["datagram: queued unreliable messages after the frame"].append(n not in RELIABLE_KINDS)
        if n in RELIABLE_KINDS:
            sess.note("reliable-type message after svc_frame", " ".join(names))
    for n in names[:fi]:
        ok = n in RELIABLE_KINDS or n == "svc_culledEvent"
        c["datagram: only reliable-type or culled messages before the frame"].append(ok)
        if not ok:
            sess.note("unreliable-type message before svc_frame", " ".join(names))


def _cmd_eq(x, y):
    return ({k: v for k, v in x.items() if not k.startswith("_")} ==
            {k: v for k, v in y.items() if not k.startswith("_")})


def verify_streams(sess, recs):
    c = sess.checks
    mv = sess.moves
    # SPEC-202 6: the three commands are those of sequences s-2, s-1, s
    for a, b in zip(mv, mv[1:]):
        if b["seq"] == a["seq"] + 1 and a["conn"] == b["conn"]:
            c["usercmd: cmd[s-1] of packet s = cmd[s] of packet s-1"].append(
                _cmd_eq(b["cmds"][1], a["cmds"][2]))
            c["usercmd: cmd[s-2] of packet s = cmd[s-1] of packet s-1"].append(
                _cmd_eq(b["cmds"][0], a["cmds"][1]))
    for x in mv:
        for cmd in x["cmds"]:
            c["usercmd: movement within clamp"].append("_unclamped" not in cmd)
    # SPEC-202 5/6: the move acknowledges the newest frame received; the server deltas from it
    events = sorted([(f["t"], 1, f) for f in sess.frames_seen] + [(x["t"], 0, x) for x in mv],
                    key=lambda e: (e[0], e[1]))
    newest_frame = -1
    last_ack = None
    conn = 0
    for t, kind, e in events:
        if e["conn"] != conn:
            conn, newest_frame, last_ack = e["conn"], -1, None
        if kind == 1:
            if last_ack is not None:
                c["frame delta = client's last acknowledged frame"].append(e["delta"] == last_ack)
                if e["delta"] != last_ack:
                    sess.note("frame delta != last ack", "frame %d delta %d ack %d" % (
                        e["frame"], e["delta"], last_ack))
            newest_frame = e["frame"]
        else:
            c["clc_move last frame = newest frame received"].append(e["last"] == newest_frame)
            last_ack = e["last"] if e["last"] > 0 else None
    for x in mv:
        c["clc_move checksum (SPEC-202 6.3)"].append(x["csum"] == x["calc"])
        c["usercmd bit stream padding is zero"].append(x["pad_zero"])
    for f in sess.frames_seen:
        c["frame game time = frame x 0.1"].append(abs(f["time"] - f["frame"] * 0.1) < 1e-3)


def summarize(path, sess, chans, totals, oob, bad, qports, bsp=None):
    P = print
    P("== capture %s" % path)
    P("packets c2s %d (%d OOB, %d bytes), s2c %d (%d OOB, %d bytes)" % (
        totals["c2s_packets"], totals["c2s_oob"], totals["c2s_bytes"],
        totals["s2c_packets"], totals["s2c_oob"], totals["s2c_bytes"]))
    P("sequenced bytes fully parsed: c2s %d, s2c %d; packets with errors/leftovers: %d" % (
        totals["c2s_parsed_bytes"], totals["s2c_parsed_bytes"], len(bad)))
    for b in bad[:30]:
        P("  BAD %9.3f %s %s" % b)
    for k, ch in chans.items():
        P("netchan %s: last seq %d, dropped %d, out-of-order %d, reliable %d" % (
            k, ch.incoming, ch.dropped, ch.ooo, ch.reliable_rx))
    P("qports seen: %s" % sorted(qports))
    P("OOB:")
    for t, d, text in oob:
        P("  %9.3f %s %r" % (t, d, text[:200]))
    P("serverdata: %s" % sess.serverdata)
    P("svc counts: %s" % dict(sess.svc_count.most_common()))
    P("clc counts: %s" % dict(sess.clc_count.most_common()))
    P("temp entities: %s" % dict(sorted(sess.te_count.items())))
    P("configstrings: %d set, %d by dictionary, %d literal" % (
        len(sess.cs), sess.cs_dict_hits, len(sess.cs_literals)))
    P("baselines: %d" % len(sess.baselines))
    P("stringcmds:")
    for t, seq, s in sess.stringcmds:
        P("  %9.3f seq %d %r" % (t, seq, s))
    P("stufftexts:")
    for t, s in sess.stuff:
        P("  %9.3f %r" % (t, s))
    mv = sess.moves
    if mv:
        okc = sum(1 for x in mv if x["csum"] == x["calc"])
        P("clc_move: %d messages, checksum matches %d/%d, zero padding %d/%d" % (
            len(mv), okc, len(mv), sum(x["pad_zero"] for x in mv), len(mv)))
        bad_c = [x for x in mv if x["csum"] != x["calc"]][:5]
        for x in bad_c:
            P("  checksum mismatch seq %d: got %02x calc %02x" % (x["seq"], x["csum"], x["calc"]))
        nonzero = Counter()
        for x in mv:
            c = x["cmds"][2]
            for k in ("forward", "side", "up", "buttons", "lean", "fire", "altfire"):
                if c[k]:
                    nonzero[k] += 1
            for c in x["cmds"]:
                if "_unclamped" in c:
                    nonzero["unclamped"] += 1
        P("  current-command non-zero fields: %s" % dict(nonzero))
        msecs = Counter(x["cmds"][2]["msec"] for x in mv)
        P("  msec values: %s" % dict(msecs.most_common(8)))
        P("  bits per move: min %d max %d" % (min(x["bits"] for x in mv), max(x["bits"] for x in mv)))
    fr = sess.frames_seen
    if fr:
        P("frames: %d, first %d last %d, delta=-1: %d, invalid refs: %d" % (
            len(fr), fr[0]["frame"], fr[-1]["frame"], sum(1 for f in fr if f["delta"] == -1),
            sum(1 for f in fr if not f["valid"])))
        ages = Counter(f["frame"] - f["delta"] for f in fr if f["delta"] > 0)
        P("  delta ages: %s" % dict(sorted(ages.items())))
        tdiff = Counter(round(f["time"] - f["frame"] * 0.1, 3) for f in fr)
        P("  game time - frame*0.1: %s" % dict(tdiff.most_common(5)))
        P("  area bytes: %s" % dict(Counter(f["areabytes"] for f in fr)))
        P("  entities per frame: max %d" % max(f["nents"] for f in fr))
    P("ghoul: %s" % dict(sess.ghoul_stats))
    P("ghoul strings: %s" % dict(sess.ghoul.stats))
    if 6 in sess.cs and bsp:
        mine = map_check_string(Path(bsp).read_bytes())
        P("map check string: server %s, computed %s -> %s" % (
            sess.cs[6].decode(), mine, "MATCH" if mine == sess.cs[6].decode() else "DIFFER"))
    P("checks (pass/total):")
    for k, v in sess.checks.items():
        P("  %-64s %6d/%-6d %s" % (k, sum(1 for x in v if x), len(v),
                                   "OK" if all(v) else "FAILED"))
    P("entity numbers seen: %d distinct, max %d, >255: %d" % (
        len(sess.ent_numbers), max(sess.ent_numbers or [0]),
        sum(1 for n in sess.ent_numbers if n > 255)))
    P("strings terminated by end of message: %d" % Msg.unterminated)
    P("findings:")
    for k, v in sess.findings.items():
        P("  %s: %d" % (k, len(v)))
        for x in v[:6]:
            P("     %s" % x)


# ---------------------------------------------------------------------------------------------
# self test with the spec test vectors
# ---------------------------------------------------------------------------------------------

def selftest():
    res = []

    def check(name, ok, detail=""):
        # vectors that depend on the original's table contents only hold with docs/private present
        if TABLES != "original" and not ok and (name.startswith("202/6") or "dictionary" in name):
            print("%-40s SKIP (original's tables: docs/private)" % name)
            return
        res.append((name, ok, detail))

    # SPEC-202 6.3 spot values
    check("202/6.3 seq 0 empty region -> A5", move_checksum(b"", 0) == 0xA5,
          "%02x" % move_checksum(b"", 0))
    check("202/6.3 seq 1 region 00 -> 85", move_checksum(b"\0", 1) == 0x85,
          "%02x" % move_checksum(b"\0", 1))
    # SPEC-202 vector 5/6
    raw = bytes.fromhex("81 20 00 69 2C 84 10 00 88 C0 00 A2 57 86 04 62 33 33 51 10")
    b = Bits(raw)
    c1 = read_usercmd(b, ZERO_CMD); c2 = read_usercmd(b, c1); c3 = read_usercmd(b, c2)
    ok = (c1["msec"] == 16 and c1["light"] == 128 and c1["angles"] == (0x400, 0x4000, 0) and
          c1["forward"] == 200 and c2["msec"] == 17 and c3["buttons"] == 0x21 and c3["lean"] == 1 and
          c3["angles"][1] == 0x4010 and c3["side"] == -160 and c3["light"] == 128 and
          abs(c3["fire"] - 12.3) < 1e-5)
    check("202/5 usercmd bit stream decodes", ok, "bits %d, c3 %s" % (b.p, c3))
    mm = bytes.fromhex("02 5B 37 02 00 00") + raw
    calc = move_checksum(mm[2:], 1234)
    check("202/6 move checksum 5B for seq 1234", calc == 0x5B, "%02x" % calc)
    # SPEC-202 vector 1
    m = Msg(bytes.fromhex("A7 80 16 05 0C 03 20 03 6C FE C0 00 40 10 10 18 A0"))
    bits, num = read_entity_header(m)
    e = read_entity_fields(m, bits, EMPTY_ENT)
    check("202/1 entity record", bits & ~0x808080 == 0x160027 and num == 5 and e["origin"] == (100, -50.5, 24)
          and e["modelindex"] == 12 and e["frame"] == 3 and e["solid"] == 0xA0181010 and
          abs(e["angles"][1] - 90) < 1e-9 and m.left() == 0, "%x %s" % (bits, e))
    m = Msg(bytes.fromhex("C1 A0 E0 01 2C 01 28 03 2C F9 85 07 03 09 07 00 11 00 02 00 20 00"))
    bits, num = read_entity_header(m)
    e = read_entity_fields(m, bits, dict(EMPTY_ENT, origin=(100, 0, 0)))
    check("202/2 entity 300 record", num == 300 and e["origin"][0] == 101 and e["sound"] == 300 and
          e["sounddata"] == 0xF9 and e["events"][:2] == ((0x85, 7), (3, 9)) and
          e["fx"][:3] == (17, 2, 32) and m.left() == 0, "%x %s left %d" % (bits, e, m.left()))
    # SPEC-202 vector 3/4
    m = Msg(bytes.fromhex("14 02 09 00 00 20 03 6C FE C0 00 00 00 00 40 00 00 5F 01 00 00 00 64 00"))
    m.byte()
    ps = read_playerstate(m, PS_EMPTY)
    check("202/3 playerstate", ps["origin"] == (800, -404, 192) and ps["viewangles"] == (0, 0x4000, 0)
          and ps["fov"] == (95,) and ps["stats"][0] == 100 and m.left() == 0, str(ps))
    m = Msg(bytes.fromhex("14 80 06 01 00 34 12 06 00 00 25 FA 00 01 FF 00 00 7F 00 00 00 00"))
    m.byte()
    ps2 = read_playerstate(m, ps)
    check("202/4 playerstate delta", ps2.get("vweap") == (0x1234, 6) and
          abs(ps2["viewoffset"][2] * 0.2 + 14.6 - 22.0) < 1e-6 and ps2["kick"] == (-6, 0, 1) and
          ps2["blend"] == (255, 0, 0, 127) and m.left() == 0, "%s left %d" % (ps2, m.left()))
    # SPEC-203 vectors
    m = Msg(bytes.fromhex("0F 25 00 00 00"))
    s = Session()
    s.parse_svc(m, {"t": 0, "seq": 0})
    check("203/1 cs dictionary hit", s.cs.get(37) == b"*1", str(s.cs.get(37)))
    check("203/serverdata vector", True)
    # SPEC-201 vectors
    for name, hexs in (("201/1 sound", "0A 28 2C 2A 00"), ("201/2 disconnect", "08 0B 04"),
                       ("201/3 countdown", "1F 5A 00 00 00"), ("201/4 namecols", "21 05 82 00 03 05 07"),
                       ("201/5 obit", "27 01 00 01 03")):
        s = Session()
        m = Msg(bytes.fromhex(hexs))
        try:
            s.parse_svc(m, {"t": 0, "seq": 0})
            check(name + " parses exactly", m.left() == 0, "left %d" % m.left())
        except Exception as ex:  # noqa: BLE001
            check(name + " parses exactly", False, str(ex))
    # 201/1 meaning: flags 0x28 = entity/channel + index>=256; index 0x2c+256=300; ent 5 ch 2 = 0x2a
    check("201/1 sound fields", 0x2C + 256 == 300 and (0x002A >> 3) == 5 and (0x2A & 7) == 2)
    # 201/9 coord, 201/8 angle
    check("201/9 coord 100.0 -> 20 03", struct.pack("<h", int(100.0 * 8)) == bytes.fromhex("2003"))
    check("201/9 coord -50.5 -> 6C FE", struct.pack("<h", int(-50.5 * 8)) == bytes.fromhex("6CFE"))
    # 201/7 dir: (0.219625, 0, 0.975584) -> 0
    check("201/7 dir table entry 0", abs(DIRS[0][0] - 0.219625) < 1e-6)
    # Huffman: code lengths form a full prefix code over 256 symbols
    kraft = sum(2.0 ** -len(c) for c in HUFF_CODES.values())
    check("201/B.3 Huffman tree complete", len(HUFF_CODES) == 256 and abs(kraft - 1) < 1e-9)
    for name, ok, detail in res:
        print("%-40s %s %s" % (name, "OK  " if ok else "FAIL", "" if ok else detail))
    return all(ok for _, ok, _ in res)


def main(argv):
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest="cmd")
    sub.add_parser("selftest")
    d = sub.add_parser("decode")
    d.add_argument("capture")
    d.add_argument("-v", "--verbose", action="store_true")
    d.add_argument("--bsp")
    d.add_argument("--dump")
    d.add_argument("--ghoul-rule", default="list2")
    a = ap.parse_args(argv)
    if a.cmd == "selftest":
        return 0 if selftest() else 1
    if a.cmd == "decode":
        r = decode_capture(a.capture, a.verbose, a.bsp, a.dump, a.ghoul_rule)
        summarize(a.capture, *r, bsp=a.bsp)
        return 0
    ap.print_help()
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
