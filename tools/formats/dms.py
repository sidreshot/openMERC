#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-111
"""Reader for .dms dynamic music sets (SPEC-111).

A .dms lists the music pieces of one level. Each piece is a stereo pair of
mono .adp files (left, right) plus a 4-byte parameter block and 8 reserved bytes.

usage:
  dms.py show <file.dms>...                     print the pieces
  dms.py validate <dir> [<pak root>...]         parse every .dms under <dir>; with roots, check
                                                that every referenced .adp exists (case-insensitive)
  dms.py render <file.dms> <outdir> [<pak root>...] [--trim]
                                                decode every piece to a stereo 16-bit WAV
"""
import os
import struct
import sys

VERSION = 11
HEADER_SIZE = 9
RECORD_TAIL = 12

PIECE_TYPES = {0: "scripted", 1: "tension", 3: "action"}   # names are hypotheses (SPEC-111)


class DMSError(ValueError):
    pass


def _cstring(data, pos):
    end = data.find(b"\0", pos)
    if end < 0:
        raise DMSError(f"unterminated string at {pos}")
    s = data[pos:end]
    if not s or any(c < 32 or c > 126 for c in s):
        raise DMSError(f"bad path string at {pos}: {s!r}")
    return s.decode("ascii"), end + 1


def parse(data):
    """Return dict(version, count, header_unknown, pieces=[...])."""
    if len(data) < HEADER_SIZE:
        raise DMSError("file shorter than header")
    version, count, pad = struct.unpack_from("<BBH", data, 0)
    if version != VERSION:
        raise DMSError(f"unexpected version {version}")
    if pad != 0:
        raise DMSError(f"non-zero bytes 2..3: {pad}")
    hdr_unknown = data[4:9]
    pos = HEADER_SIZE
    pieces = []
    for i in range(count):
        left, pos = _cstring(data, pos)
        right, pos = _cstring(data, pos)
        if pos + RECORD_TAIL > len(data):
            raise DMSError(f"piece {i}: truncated parameter block")
        ptype, trig, flag, volume = struct.unpack_from("<4B", data, pos)
        reserved = data[pos + 4:pos + RECORD_TAIL]
        pos += RECORD_TAIL
        pieces.append(dict(left=left, right=right, type=ptype,
                           type_name=PIECE_TYPES.get(ptype, f"unknown{ptype}"),
                           trigger=trig, loop=flag, volume=volume, reserved=reserved.hex()))
    if pos != len(data):
        raise DMSError(f"{len(data) - pos} trailing bytes")
    return dict(version=version, count=count, header_unknown=hdr_unknown.hex(), pieces=pieces)


def load(path):
    with open(path, "rb") as f:
        return parse(f.read())


def index_roots(roots):
    """Map lower-case game path -> file on disk, later roots override earlier ones."""
    idx = {}
    for r in roots:
        for d, _, fs in os.walk(r):
            for fn in fs:
                full = os.path.join(d, fn)
                rel = os.path.relpath(full, r).replace(os.sep, "/").lower()
                idx[rel] = full
    return idx


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    trim = "--trim" in argv
    args = [a for a in argv[1:] if a != "--trim"]
    if cmd == "show":
        for p in args:
            m = load(p)
            print(f"== {p}: version={m['version']} pieces={m['count']} header_unknown={m['header_unknown']}")
            for i, pc in enumerate(m["pieces"]):
                print(f"  [{i}] type={pc['type']}({pc['type_name']}) trigger={pc['trigger']} "
                      f"loop={pc['loop']} volume={pc['volume']} reserved={pc['reserved']}")
                print(f"       L {pc['left']}\n       R {pc['right']}")
    elif cmd == "validate":
        root, roots = args[0], args[1:]
        idx = index_roots(roots) if roots else None
        files = sorted(os.path.join(d, f) for d, _, fs in os.walk(root) for f in fs
                       if f.lower().endswith(".dms"))
        bad = missing = 0
        refs = set()
        for p in files:
            try:
                m = load(p)
            except DMSError as e:
                bad += 1
                print(f"FAIL {p}: {e}")
                continue
            for pc in m["pieces"]:
                for side in ("left", "right"):
                    refs.add(pc[side].lower())
                    if idx is not None and pc[side].lower() not in idx:
                        missing += 1
                        print(f"MISSING {p}: {pc[side]}")
        print(f"# {len(files)} files, {len(files) - bad} parsed, {bad} failed; "
              f"{len(refs)} distinct .adp referenced" + (f", {missing} missing" if idx is not None else ""))
        return 1 if bad or missing else 0
    elif cmd == "render":
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import adp
        m = load(args[0])
        out = args[1]
        idx = index_roots(args[2:])
        for pc in m["pieces"]:
            rl, l, _ = adp.load(idx[pc["left"].lower()], trim, "left")
            rr, r, _ = adp.load(idx[pc["right"].lower()], trim, "right")
            name = os.path.splitext(os.path.basename(pc["left"]))[0][1:] or "piece"
            dst = os.path.join(out, f"{name}.wav")
            adp.write_wav(dst, [l, r], rl)
            print(f"{dst}: {min(len(l), len(r)) / rl:.1f} s, type={pc['type_name']} volume={pc['volume']}")
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
