#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - test maps for black-box runs: a copy of a stock map (from the user's extracted data) with extra
# entities appended to its entity lump (BSP lump 0, SPEC-101), written into the G4 test copy's base/maps/ under a
# new name. Used to start vehicle/helicopter scripts at level start (trigger_always -> script_runner) so the
# headless client does not have to walk to their triggers. Game data is never written into the repository.
# Implements: SPEC-101 (entity lump), SPEC-731 test set-up
#
# usage: g4map.py <stock map> <new name> [--drop key=value ...] '<entity text>' ...
#   e.g. g4map.py trn1 g4trn1 '{ "classname" "trigger_always" "target" "t22" }'
#   --drop removes every entity with that key/value (e.g. --drop targetname=introscript)
import re
import struct
import sys
from pathlib import Path

SRC = Path(r"C:\openmerc-data\extract\pak0\maps")
DST = Path(r"D:\Games\SoF-test-G4\base\maps")


def main(argv):
    src, name, rest = argv[0], argv[1], argv[2:]
    drops, extra = [], []
    i = 0
    while i < len(rest):
        if rest[i] == "--drop":
            drops.append(tuple(rest[i + 1].split("=", 1)))
            i += 2
        else:
            extra.append(rest[i])
            i += 1
    data = bytearray((SRC / (src + ".bsp")).read_bytes())
    off, ln = struct.unpack_from("<ii", data, 8)
    text = bytes(data[off:off + ln]).rstrip(b"\0")

    def keep(m):
        kv = dict(re.findall(rb'"([^"]*)"\s+"([^"]*)"', m.group(0)))
        for k, v in drops:
            if kv.get(k.encode()) == v.encode():
                return b""
        return m.group(0)
    if drops:
        text = re.sub(rb"\{[^{}]*\}", keep, text)
    text += b"\n" + "\n".join(extra).encode("latin-1") + b"\n\0"
    # the new lump goes to the end of the file (4-byte aligned); every other lump keeps its place
    while len(data) % 4:
        data.append(0)
    newoff = len(data)
    data += text
    struct.pack_into("<ii", data, 8, newoff, len(text))
    DST.mkdir(parents=True, exist_ok=True)
    (DST / (name + ".bsp")).write_bytes(bytes(data))
    print("wrote", DST / (name + ".bsp"), "entities +%d" % len(extra))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
