#!/usr/bin/env python3
"""List and extract PACK archives (the Quake II archive layout used by the game data).

Layout (public Quake II format): "PACK", int32 dir_offset, int32 dir_length,
directory of 64-byte records: char name[56], int32 offset, int32 length.

usage:
  pak.py ls <pak>...                 list entries
  pak.py stats <pak>...              count/size per extension
  pak.py x <pak> <outdir> [glob]     extract entries matching glob
"""
import fnmatch
import os
import struct
import sys
from collections import defaultdict


def read_dir(path):
    with open(path, "rb") as f:
        magic, dofs, dlen = struct.unpack("<4sii", f.read(12))
        if magic != b"PACK":
            raise ValueError(f"{path}: not a PACK file")
        f.seek(dofs)
        raw = f.read(dlen)
    entries = []
    for i in range(0, dlen, 64):
        name, ofs, ln = struct.unpack_from("<56sii", raw, i)
        entries.append((name.split(b"\0", 1)[0].decode("latin-1"), ofs, ln))
    return entries


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "ls":
        for p in argv[1:]:
            for name, ofs, ln in read_dir(p):
                print(f"{ln:>10} {name}")
    elif cmd == "stats":
        agg = defaultdict(lambda: [0, 0])
        for p in argv[1:]:
            for name, _, ln in read_dir(p):
                ext = os.path.splitext(name)[1].lower() or "(none)"
                agg[ext][0] += 1
                agg[ext][1] += ln
        for ext, (n, sz) in sorted(agg.items(), key=lambda kv: -kv[1][1]):
            print(f"{ext:10} {n:>6} files {sz/1048576:>9.1f} MB")
    elif cmd == "x":
        pak, out = argv[1], argv[2]
        pat = argv[3] if len(argv) > 3 else "*"
        with open(pak, "rb") as f:
            for name, ofs, ln in read_dir(pak):
                if not fnmatch.fnmatch(name.lower(), pat.lower()):
                    continue
                dst = os.path.join(out, *name.split("/"))
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                f.seek(ofs)
                with open(dst, "wb") as o:
                    o.write(f.read(ln))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
