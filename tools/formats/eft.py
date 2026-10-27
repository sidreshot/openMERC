#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-142
"""Reader for .eft effect definitions (SPEC-142).

Layout (little endian): 20-byte header {float version (1, 2 or 3); int32 h1;
float h2; float h3; float count}, then `count` primitives, each starting with a
float type code, then an 8-byte trailer (two floats). Primitive sizes depend on
(version, type) and, for type 0, on a float "kind" stored 0x60 bytes into the
primitive. Records are memory images: unused bytes hold fill patterns 0xEB/0xCD
and leftovers of earlier strings, which carry no meaning.

usage:
  eft.py info <file.eft>                  header, primitives, embedded names
  eft.py validate <effects dir>           parse every .eft; report counts per kind
  eft.py census <effects dir> [ver kind]  per-offset value census of type-0 records
  eft.py json <effects dir> <out.json>    dump every effect (header, primitives, floats)
"""
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

HEADER = 20
TRAILER = {1.0: 0, 2.0: 8, 3.0: 8}   # version 1: single stock file, see SPEC-142 open questions
KIND_OFFSET = 0x60

# (version, type, kind or None) -> primitive size in bytes, including the type float
SIZES = {
    (1.0, 0.0, 1.0): 572,
    (2.0, 0.0, 1.0): 584, (2.0, 0.0, 2.0): 684, (2.0, 0.0, 3.0): 752,
    (2.0, 0.0, 5.0): 556, (2.0, 0.0, 6.0): 556,
    (2.0, 1.0, None): 84, (2.0, 2.0, None): 44, (2.0, 3.0, None): 92,
    (3.0, 0.0, 6.0): 564, (3.0, 0.0, 7.0): 572,
    (3.0, 1.0, None): 92, (3.0, 2.0, None): 52, (3.0, 3.0, None): 100, (3.0, 4.0, None): 44,
}
TYPE_NAMES = {0.0: "particles", 1.0: "sound", 2.0: "light", 3.0: "decal", 4.0: "type4"}   # names: hypotheses
ROOTS = ("textures/", "effects/", "sprites/", "weapons/", "ambient/", "impact/", "environ/", "enemy/", "models/", "player/")
NAME_RE = re.compile(rb"[A-Za-z0-9_][A-Za-z0-9_/\\.\-]{3,}\x00")


class EFTError(ValueError):
    pass


class Primitive:
    def __init__(self, data, offset, version):
        self.offset = offset
        self.type = struct.unpack_from("<f", data, offset)[0]
        self.kind = None
        if self.type == 0.0:
            if offset + KIND_OFFSET + 4 > len(data):
                raise EFTError("truncated type-0 primitive at 0x%x" % offset)
            self.kind = struct.unpack_from("<f", data, offset + KIND_OFFSET)[0]
        key = (version, self.type, self.kind)
        if key not in SIZES:
            raise EFTError("no size known for version %g type %g kind %r at 0x%x" % (version, self.type, self.kind, offset))
        self.size = SIZES[key]
        self.raw = data[offset:offset + self.size]
        if len(self.raw) != self.size:
            raise EFTError("primitive at 0x%x runs past end of file" % offset)

    def names(self):
        """(relative offset, text) of embedded path-like strings (texture / sound names)."""
        out = []
        for m in NAME_RE.finditer(self.raw):
            s = m.group()[:-1].decode("latin-1")
            if "/" in s or "\\" in s or "." in s:
                start = m.start()
                # a float's high byte can look like a letter just before the name: trim to a known root
                low = s.lower()
                for root in ROOTS:
                    k = low.find(root)
                    if 0 < k <= 2:
                        s, start = s[k:], start + k
                        break
                out.append((start, s))
        return out

    def floats(self):
        return list(struct.unpack_from("<%df" % (self.size // 4), self.raw, 0))


class Effect:
    def __init__(self, data):
        if len(data) < HEADER:
            raise EFTError("file too short")
        self.version, self.h1, self.h2, self.h3, cnt = struct.unpack_from("<fifff", data, 0)
        if self.version not in (1.0, 2.0, 3.0):
            raise EFTError("version %g" % self.version)
        if cnt != int(cnt) or not 0 <= cnt < 64:
            raise EFTError("count %g" % cnt)
        self.count = int(cnt)
        p = HEADER
        self.prims = []
        for _ in range(self.count):
            pr = Primitive(data, p, self.version)
            self.prims.append(pr)
            p += pr.size
        tl = TRAILER[self.version]
        if p != len(data) - tl:
            raise EFTError("primitives end at 0x%x, file has %d bytes (expected %d-byte trailer)"
                           % (p, len(data), tl))
        self.trailer = struct.unpack_from("<2f", data, p) if tl else ()


def load(path):
    with open(path, "rb") as f:
        return Effect(f.read())


def iter_effects(root):
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if f.lower().endswith(".eft"):
                yield os.path.join(dp, f)


def census(root, version=3.0, kind=7.0):
    """Per 4-byte offset of matching type-0 primitives: value classes and examples."""
    cols = defaultdict(Counter)
    n = 0
    for p in iter_effects(root):
        e = load(p)
        if e.version != version:
            continue
        for pr in e.prims:
            if pr.type != 0.0 or pr.kind != kind:
                continue
            n += 1
            for o in range(0, pr.size, 4):
                w = pr.raw[o:o + 4]
                if w in (b"\xeb" * 4, b"\xcd" * 4):
                    cols[o]["fill"] += 1
                    continue
                i = struct.unpack("<i", w)[0]
                f = struct.unpack("<f", w)[0]
                if all(32 <= c < 127 for c in w):
                    cols[o]["text"] += 1
                elif abs(i) < 65536:
                    cols[o]["i%d" % i] += 1
                else:
                    cols[o]["%.4g" % f] += 1
    return n, cols


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "info":
        e = load(argv[1])
        print("version %g  h1 %d  h2 %g  h3 %g  primitives %d  trailer %s" % (
            e.version, e.h1, e.h2, e.h3, e.count, tuple(round(x, 4) for x in e.trailer)))
        for pr in e.prims:
            print("  0x%04x type %g (%s)%s size %d  names %s" % (
                pr.offset, pr.type, TYPE_NAMES.get(pr.type, "?"),
                "" if pr.kind is None else " kind %g" % pr.kind, pr.size, pr.names()))
    elif cmd == "validate":
        ok = bad = 0
        kinds = Counter()
        for p in iter_effects(argv[1]):
            try:
                e = load(p)
                ok += 1
                for pr in e.prims:
                    kinds[(e.version, pr.type, pr.kind)] += 1
            except (EFTError, struct.error) as ex:
                bad += 1
                print("FAIL %s: %s" % (p, ex))
        print("parsed %d ok, %d failed" % (ok, bad))
        for k, c in sorted(kinds.items(), key=lambda kv: (kv[0][0], kv[0][1], kv[0][2] or 0)):
            print("  version %g type %g (%s) kind %s: %d primitives, %d bytes" % (
                k[0], k[1], TYPE_NAMES.get(k[1], "?"), "-" if k[2] is None else "%g" % k[2], c, SIZES[k]))
        return 0 if bad == 0 else 1
    elif cmd == "census":
        ver = float(argv[2]) if len(argv) > 2 else 3.0
        kind = float(argv[3]) if len(argv) > 3 else 7.0
        n, cols = census(argv[1], ver, kind)
        print("%d primitives (version %g, type 0, kind %g)" % (n, ver, kind))
        for o in sorted(cols):
            c = cols[o]
            print("  +0x%03x  %s" % (o, ", ".join("%s x%d" % kv for kv in c.most_common(6))))
    elif cmd == "json":
        out = {}
        for p in iter_effects(argv[1]):
            e = load(p)
            rel = os.path.relpath(p, argv[1]).replace("\\", "/")
            out[rel] = {"version": e.version, "h1": e.h1, "h2": e.h2, "h3": e.h3, "trailer": e.trailer,
                        "primitives": [{"type": pr.type, "kind": pr.kind, "offset": pr.offset, "size": pr.size,
                                        "names": pr.names(),
                                        "floats": [round(x, 6) if x == x and abs(x) < 1e30 else None for x in pr.floats()]}
                                       for pr in e.prims]}
        with open(argv[2], "w") as f:
            json.dump(out, f, indent=1)
        print("wrote %d effects to %s" % (len(out), argv[2]))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
