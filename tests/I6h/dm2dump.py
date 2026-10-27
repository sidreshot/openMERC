#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-311
# Lists the blocks of a Soldier of Fortune 1.07f demo (.dm2): {int32 length, message bytes}, ended by -1.
# Every block is decoded with tools/net/sofnet.py (written from the specs) when it is available, so each
# message id and every byte is checked; svc_frame blocks show their frame and delta frame numbers.
# Also checks SPEC-311 test vectors 1 (header layout, end marker) and 4 (ghoul blocks, then a frame with
# delta -1).
#
#   python -I dm2dump.py <file.dm2> [--no-decode]
import argparse
import struct
import sys
from collections import Counter
from pathlib import Path

SOFNET = Path(__file__).resolve().parents[2] / "tools" / "net"


def read_blocks(data):
    """Yields (offset, length, bytes or None for the end marker); raises on a truncated file."""
    p = 0
    while p + 4 <= len(data):
        (n,) = struct.unpack_from("<i", data, p)
        if n == -1:
            yield p, -1, None
            return
        if n < 0 or p + 4 + n > len(data):
            raise ValueError("block at %d: bad length %d (file %d bytes)" % (p, n, len(data)))
        yield p, n, data[p + 4:p + 4 + n]
        p += 4 + n
    if p != len(data):
        raise ValueError("%d trailing bytes" % (len(data) - p))


def cstring(b, p):
    e = b.index(0, p)
    return b[p:e].decode("latin-1"), e + 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("file")
    ap.add_argument("--no-decode", action="store_true")
    args = ap.parse_args()
    data = Path(args.file).read_bytes()

    sess = None
    if not args.no_decode:
        try:
            sys.path.insert(0, str(SOFNET))
            import sofnet
            sess = sofnet.Session()
        except Exception as e:  # noqa: BLE001 - decoder optional
            print("(no decoding: %s)" % e)

    blocks = list(read_blocks(data))
    print("%s: %d bytes, %d blocks" % (args.file, len(data), len(blocks)))
    print("%5s %8s %6s %4s  %s" % ("idx", "offset", "len", "id", "messages"))
    first_frame = None
    errors = 0
    msgsum = Counter()
    for i, (ofs, n, b) in enumerate(blocks):
        if b is None:
            print("%5d %8d %6s %4s  end marker (FF FF FF FF)" % (i, ofs, -1, ""))
            continue
        line = ""
        if sess is not None:
            m = sofnet.Msg(b)
            ctx = {"t": float(i), "seq": i}
            try:
                sess.parse_svc(m, ctx)
                names = [s[0] for s in ctx.get("spans", [])]
                frames = []
                for name, st, en in ctx.get("spans", []):
                    if name == "svc_frame":
                        fr, de = struct.unpack_from("<ii", b, st + 1)
                        frames.append("frame %d delta %d" % (fr, de))
                        if first_frame is None:
                            first_frame = (i, fr, de)
                c = Counter(names)
                msgsum.update(c)
                line = ", ".join("%s%s" % (k.replace("svc_", ""), "" if v == 1 else " x%d" % v)
                                 for k, v in c.items())
                if frames:
                    line += "  [" + "; ".join(frames) + "]"
                if m.left() != 0:
                    line += "  !! %d bytes left" % m.left()
                    errors += 1
            except Exception as e:  # noqa: BLE001
                line = "!! decode error: %s" % e
                errors += 1
        else:
            if b and b[0] == 23 and len(b) >= 9:
                fr, de = struct.unpack_from("<ii", b, 1)
                line = "frame %d delta %d" % (fr, de)
                if first_frame is None:
                    first_frame = (i, fr, de)
        print("%5d %8d %6d %4d  %s" % (i, ofs, n, b[0] if b else -1, line))

    print()
    print("== summary")
    if sess is not None:
        print("messages: " + ", ".join("%s %d" % kv for kv in sorted(msgsum.items())))
        print("blocks with decode errors or leftover bytes: %d" % errors)
    end_ok = bool(blocks) and blocks[-1][1] == -1 and blocks[-1][0] + 4 == len(data)
    print("end marker FF FF FF FF at the end of the file: %s" % ("yes" if end_ok else "NO"))

    # SPEC-311 test vector 1: header layout
    hdr = blocks[0][2] if blocks and blocks[0][2] else b""
    if hdr[:5] == bytes([0x0E, 0x21, 0x00, 0x00, 0x00]):
        spawn, = struct.unpack_from("<i", hdr, 5)
        demo, dm = hdr[9], hdr[10]
        gamedir, p = cstring(hdr, 11)
        pnum, = struct.unpack_from("<h", hdr, p)
        level, p = cstring(hdr, p + 2)
        print("vector 1: header starts 0E 21 00 00 00: yes - spawn count %d, demo flag %02X, deathmatch %d, "
              "game dir %r, player %d, level %r" % (spawn, demo, dm, gamedir, pnum, level))
    else:
        print("vector 1: header does NOT start with 0E 21 00 00 00: %s" % hdr[:16].hex(" "))

    # SPEC-311 test vector 4: after the header blocks, ghoul blocks (26), then a frame with delta -1
    hdr_end = next((i for i, (_, n, b) in enumerate(blocks) if b and b[:1] in (b"\x1a", b"\x17")), None)
    if hdr_end is not None:
        g = 0
        i = hdr_end
        while i < len(blocks) and blocks[i][2] and blocks[i][2][0] == 26:
            g += 1
            i += 1
        print("vector 4: header blocks 0..%d, then %d ghoul block(s) (first byte 26) from block %d; "
              "first frame %s" % (hdr_end - 1, g, hdr_end,
                                  "block %d frame %d delta %d" % first_frame if first_frame else "none"))
        ghoul_ok = all(blocks[j][2][0] == 26 and struct.unpack_from("<H", blocks[j][2], 1)[0] + 3 == blocks[j][1]
                       for j in range(hdr_end, hdr_end + g))
        print("          ghoul block length = L + 3 with short L after byte 26: %s" % ("yes" if ghoul_ok else "NO"))
    return 1 if (errors or not end_ok) else 0


if __name__ == "__main__":
    sys.exit(main())
