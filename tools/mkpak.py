#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-300 (PACK archive, public Quake II layout) - OpenMerc's own data archive
"""Write a PACK archive from a directory tree (OpenMerc's zz_openmerc.pak).

Layout (public Quake II format, see tools/pak.py): "PACK", int32 dir_offset, int32 dir_length,
the file data, then the directory of 64-byte records: char name[56], int32 offset, int32 length.
Names are stored with forward slashes, relative to the source directory, in lower case; the
directory is written in sorted order so the archive is reproducible.

usage: mkpak.py <srcdir> <out.pak>
"""
import os
import struct
import sys


def collect(src):
    files = []
    for root, dirs, names in os.walk(src):
        dirs.sort()
        for n in sorted(names):
            if n.startswith('.'):
                continue
            path = os.path.join(root, n)
            rel = os.path.relpath(path, src).replace(os.sep, '/').lower()
            if len(rel.encode('latin-1')) > 55:
                raise SystemExit('%s: name longer than 55 bytes' % rel)
            files.append((rel, path))
    return files


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    src, out = argv
    files = collect(src)
    if not files:
        raise SystemExit('%s: no files' % src)
    os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
    with open(out, 'wb') as f:
        f.write(b'PACK' + struct.pack('<ii', 0, 0))
        records = []
        for rel, path in files:
            with open(path, 'rb') as i:
                data = i.read()
            records.append((rel, f.tell(), len(data)))
            f.write(data)
        dofs = f.tell()
        for rel, ofs, ln in records:
            f.write(struct.pack('<56sii', rel.encode('latin-1'), ofs, ln))
        dlen = f.tell() - dofs
        f.seek(4)
        f.write(struct.pack('<ii', dofs, dlen))
    print('%s: %d files, %d bytes' % (out, len(files), os.path.getsize(out)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
