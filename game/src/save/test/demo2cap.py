#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - convert a client demo (.dm2) into the capture format of game/test/sofclient.py (cap.jsonl), so that
# the capture tools (game/test/capdiff.py, game/src/scriptglue/test/g6b_timeline.py) read what a listen-game client
# received. Each demo block becomes one server packet with a synthetic netchan header (sequence = block number).
# Implements: SPEC-311 (demo file layout), SPEC-200 (netchan header)
#
# usage: demo2cap.py FILE.dm2 OUT.jsonl

import json
import struct
import sys


def main(argv):
    src, dst = argv[0], argv[1]
    raw = open(src, "rb").read()
    p = 0
    n = 0
    with open(dst, "w") as out:
        out.write(json.dumps({"t": 0.0, "wall": 0.0, "dir": "s2c", "client": "demo", "len": 18,
                              "hex": (b"\xff\xff\xff\xffclient_connect\n").hex()}) + "\n")
        while p + 4 <= len(raw):
            ln = struct.unpack_from("<i", raw, p)[0]
            p += 4
            if ln < 0 or p + ln > len(raw):
                break
            n += 1
            pkt = struct.pack("<II", n, 0) + raw[p:p + ln]
            p += ln
            out.write(json.dumps({"t": n * 0.1, "wall": 0.0, "dir": "s2c", "client": "demo", "len": len(pkt),
                                  "hex": pkt.hex()}) + "\n")
    print("%d blocks" % n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
