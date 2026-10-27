#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-144
"""Readers for env/*.trn (train-level terrain description) and maps/*.raw
(128x128 RGBA level images), SPEC-144.

.raw: 65536 bytes = 128 x 128 pixels x (R, G, B, A), rows stored bottom-up,
alpha always 255. One file per single-player map (same base name as the .bsp).

.trn: byte-packed little-endian: 140-byte header, `features` records of 68
bytes, a sprite section and a texture section, each introduced by the 4-byte
marker 2D 42 01 00, and a final marker. Only one stock file exists.

usage:
  trn_raw.py raw-validate <dir>                 check every .raw under <dir>
  trn_raw.py raw-png <file.raw> <out.png> [scale]
  trn_raw.py raw-sheet <dir> <out.png>          contact sheet of every .raw, labelled
  trn_raw.py trn-info <file.trn>                decode and print a .trn
"""
import os
import struct
import sys

RAW_SIZE = 128 * 128 * 4
TRN_MAGIC = 0x0122            # first uint16 of the only stock file (meaning UNKNOWN)
TRN_HEADER = 140
TRN_RECORD = 68
SECTION_MARK = b"\x2d\x42\x01\x00"


class FormatError(ValueError):
    pass


# ---- .raw --------------------------------------------------------------------
def raw_image(data):
    """Pillow RGB image (top row first) from a .raw file's bytes."""
    from PIL import Image
    if len(data) != RAW_SIZE:
        raise FormatError("size %d != %d" % (len(data), RAW_SIZE))
    img = Image.frombytes("RGBA", (128, 128), data)
    return img.transpose(Image.FLIP_TOP_BOTTOM).convert("RGB")


def raw_check(data):
    probs = []
    if len(data) != RAW_SIZE:
        probs.append("size %d" % len(data))
    elif any(b != 255 for b in data[3::4]):
        probs.append("alpha not constant 255")
    return probs


# ---- .trn --------------------------------------------------------------------
class TRN:
    def __init__(self, data):
        if len(data) < TRN_HEADER + 4:
            raise FormatError("file too short")
        self.magic = struct.unpack_from("<H", data, 0)[0]
        if self.magic != TRN_MAGIC:
            raise FormatError("first word 0x%04x" % self.magic)
        (self.h_a, self.h_b, self.features_count, self.h_c, self.h_d,
         self.textures_count) = struct.unpack_from("<6i", data, 2)
        self.h_float_1a = struct.unpack_from("<f", data, 0x1A)[0]
        self.h_int_32 = struct.unpack_from("<i", data, 0x32)[0]
        self.h_rgba_3b = struct.unpack_from("<4f", data, 0x3B)
        self.h_byte_4b = data[0x4B]
        self.h_tail_54 = struct.unpack_from("<fifff", data, 0x54)
        p = TRN_HEADER
        self.features = []
        for _ in range(self.features_count):
            t, i1 = struct.unpack_from("<ii", data, p)
            vals = struct.unpack_from("<4f", data, p + 8)
            pair = struct.unpack_from("<2f", data, p + 24)
            n = struct.unpack_from("<i", data, p + 32)[0]
            groups = (struct.unpack_from("<4f", data, p + 36), struct.unpack_from("<4f", data, p + 52))
            self.features.append({"type": t, "i1": i1, "start": vals[0], "end": vals[1],
                                  "a": vals[2], "b": vals[3], "pair": pair, "n": n, "groups": groups})
            p += TRN_RECORD
        if data[p:p + 4] != SECTION_MARK:
            raise FormatError("sprite section marker missing at 0x%x" % p)
        nspr = struct.unpack_from("<i", data, p + 4)[0]
        p += 8
        self.sprites = []
        for _ in range(nspr):
            e = data.index(b"\0", p)
            name = data[p:e].decode("latin-1")
            vals = struct.unpack_from("<3f2i", data, e + 1)
            self.sprites.append((name, vals))
            p = e + 1 + 20
        if data[p:p + 4] != SECTION_MARK:
            raise FormatError("texture section marker missing at 0x%x" % p)
        p += 4
        self.textures = []
        for _ in range(self.textures_count):
            lead = struct.unpack_from("<i", data, p)[0]
            e = data.index(b"\0", p + 4)
            name = data[p + 4:e].decode("latin-1")
            vals = struct.unpack_from("<5f", data, e + 1)
            flag = data[e + 21]
            self.textures.append((lead, name, vals, flag))
            p = e + 22
        if data[p:] != SECTION_MARK:
            raise FormatError("end marker missing / %d trailing bytes at 0x%x" % (len(data) - p, p))


def iter_ext(root, ext):
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if f.lower().endswith(ext):
                yield os.path.join(dp, f)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "raw-validate":
        ok = bad = 0
        for p in iter_ext(argv[1], ".raw"):
            probs = raw_check(open(p, "rb").read())
            if probs:
                bad += 1
                print("FAIL %s: %s" % (p, "; ".join(probs)))
            else:
                ok += 1
        print("parsed %d ok, %d failed" % (ok, bad))
        return 0 if bad == 0 else 1
    elif cmd == "raw-png":
        img = raw_image(open(argv[1], "rb").read())
        sc = int(argv[3]) if len(argv) > 3 else 1
        if sc > 1:
            from PIL import Image
            img = img.resize((128 * sc, 128 * sc), Image.NEAREST)
        img.save(argv[2])
        print("wrote", argv[2])
    elif cmd == "raw-sheet":
        from PIL import Image, ImageDraw
        files = list(iter_ext(argv[1], ".raw"))
        cols = 8
        rows = (len(files) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * 136, rows * 150), (20, 20, 20))
        dr = ImageDraw.Draw(sheet)
        for i, p in enumerate(files):
            r, c = divmod(i, cols)
            sheet.paste(raw_image(open(p, "rb").read()), (c * 136 + 4, r * 150 + 4))
            dr.text((c * 136 + 4, r * 150 + 134), os.path.basename(p), fill=(230, 230, 230))
        sheet.save(argv[2])
        print("wrote %s (%d images)" % (argv[2], len(files)))
    elif cmd == "trn-info":
        t = TRN(open(argv[1], "rb").read())
        print("magic 0x%04x  header ints %s  float@0x1a %g  int@0x32 %d  floats@0x3b %s  byte@0x4b %d  tail@0x54 %s"
              % (t.magic, (t.h_a, t.h_b, t.features_count, t.h_c, t.h_d, t.textures_count), t.h_float_1a,
                 t.h_int_32, t.h_rgba_3b, t.h_byte_4b, t.h_tail_54))
        for k, f in enumerate(t.features):
            print("  feature %2d type %d i1 %2d  start %7g end %7g  a %7g b %7g  pair %s n %d groups %s" % (
                k, f["type"], f["i1"], f["start"], f["end"], f["a"], f["b"],
                tuple(round(x, 3) for x in f["pair"]), f["n"],
                [tuple(round(x, 4) for x in g) for g in f["groups"]]))
        for s in t.sprites:
            print("  sprite  %s %s" % s)
        for x in t.textures:
            print("  texture lead %d %s %s flag %d" % x)
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
