#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - extract the Ghoul stream payloads (svc 26 / svc 27) of a network capture for omghoul_test --replay.
# Implements: SPEC-201 (svc 26/27 framing), SPEC-013 (test input for the client decoder)
#
# usage: ghoul_payloads.py <capture.jsonl> <out.bin>
# The capture is decoded with tools/net/sofnet.py; every svc 26 / svc 27 payload is written as
#   u8 kind (26 / 27), u8 connection, i32 frame, f32 game time (of the packet's svc_frame), u32 length, bytes
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[4] / "tools" / "net"))
import sofnet  # noqa: E402

records = []
_svc26, _svc27 = sofnet.Session.svc_26, sofnet.Session.svc_27


def svc_26(self, m, ctx):
    start = m.p
    _svc26(self, m, ctx)
    records.append((26, self.conn, -1, 0.0, bytes(m.d[start + 2:m.p])))


def svc_27(self, m, ctx):
    start = m.p
    _svc27(self, m, ctx)
    fr = self.frames_seen[-1] if self.frames_seen else None
    records.append((27, self.conn, ctx.get("frame", -1), fr["time"] if fr else 0.0, bytes(m.d[start + 2:m.p])))


def main(argv):
    if len(argv) != 2:
        print(__doc__ or "usage: ghoul_payloads.py <capture.jsonl> <out.bin>")
        return 2
    sofnet.Session.svc_26 = svc_26
    sofnet.Session.svc_27 = svc_27
    sofnet.decode_capture(argv[0])
    with open(argv[1], "wb") as f:
        for kind, conn, frame, t, raw in records:
            f.write(struct.pack("<BBifI", kind, conn, frame, t, len(raw)))
            f.write(raw)
    print("records %d (svc 26: %d, svc 27: %d), connections %s" % (
        len(records), sum(1 for r in records if r[0] == 26), sum(1 for r in records if r[0] == 27),
        sorted(set(r[1] for r in records))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
