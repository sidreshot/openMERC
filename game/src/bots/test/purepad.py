#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc F5 test aid - copy a game module and append 32 or 33 bytes so that its SPEC-301 section 8 file checksum equals the
# shipped 1.07f gamex86.dll (64C0FC54, SPEC-AMEND-005 GS.15). The original engine then reports a "pure" server
# (sv_violence bit 0x20) and its own client, whose parental locks are all forced on in the test copies (no lock store,
# cl_violence 63), can join a LISTEN server that runs an OpenMerc module. Test copies only; never shipped. Trailing
# bytes after the PE image do not change how Windows loads the DLL.
# Implements: SPEC-301 8 (module checksum), SPEC-AMEND-005 GS.15 (test harness)
#
# usage: purepad.py <in.dll> <out.dll> [target hex, default 64C0FC54]

import sys

SEED = 0xF8349720
M = 0xFFFFFFFF


def checksum(data, v=SEED, start=0):
    for i, b in enumerate(data, start):
        v = ((v << 1) | (v >> 31)) & M
        v = (v + b) & M if i % 2 == 0 else v ^ b
    return v


def rotl(v, k):
    k %= 32
    return ((v << k) | (v >> (32 - k))) & M if k else v


def main(argv):
    src, dst = argv[0], argv[1]
    target = int(argv[2], 16) if len(argv) > 2 else 0x64C0FC54
    data = open(src, "rb").read()
    s = checksum(data)
    n = len(data)
    # append N bytes, all zero except four at xor (odd) positions whose remaining rotation is 0, 8, 16, 24: zero
    # bytes leave the value alone apart from the rotation, and the four bytes then fill disjoint 8-bit fields
    N = 32 if (n + 31) % 2 == 1 else 33
    d = target ^ rotl(s, N)
    tail = bytearray(N)
    for o in (0, 8, 16, 24):
        tail[N - 1 - o] = (d >> o) & 0xFF
    out = data + bytes(tail)
    assert checksum(out) == target, hex(checksum(out))
    open(dst, "wb").write(out)
    print("%s: checksum %08X -> %08X (%d bytes appended)" % (dst, s, target, N))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
