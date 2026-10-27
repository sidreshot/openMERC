#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-100
"""Reader for .m32 textures (SPEC-100).

A .m32 file is a fixed 968-byte header followed by up to 16 mip levels of
32-bit RGBA pixels (R, G, B, A byte order, rows top to bottom).

usage:
  m32.py info <file.m32>...                 print the decoded header
  m32.py png <file.m32> <out.png> [level]   write one mip level as RGBA PNG
  m32.py validate <dir>                     parse every .m32 under <dir>
  m32.py sheet <out.png> <file.m32>...      contact sheet (alpha on checkerboard)
"""
import os
import struct
import sys
from dataclasses import dataclass, field

HEADER_SIZE = 968
VERSION = 4
MAX_MIPS = 16
NAME_LEN = 128

# Surface flags (low 24 bits of `flags`). Bits shared with Quake II keep the
# Quake II names (q_shared.h); the others are named after what the data shows.
SURF_LIGHT = 0x1
SURF_SLICK = 0x2
SURF_SKY = 0x4
SURF_WARP = 0x8
SURF_TRANS33 = 0x10
SURF_TRANS66 = 0x20
SURF_FLOWING = 0x40
SURF_NODRAW = 0x80
SURF_HINT = 0x100
SURF_SKIP = 0x200
SURF_ALPHA = 0x800        # set iff the pixels contain alpha < 255 (SPEC-100)
SURF_UNKNOWN_1000 = 0x1000  # screens/lamps/displays; meaning UNKNOWN
MATERIAL_SHIFT = 24       # high byte of `flags` = material id

# Image-type bits at header offset 0x378 (SPEC-100 "kind").
KIND_NO_MIPS = 0x1        # chain stops before 1x1 (UI pictures, env maps)
KIND_DETAIL = 0x2         # this image is a detail texture (d_*)
KIND_UNKNOWN_4 = 0x4      # signs/posters/graffiti; meaning UNKNOWN
KIND_ENVMAP = 0x8         # this image is an environment map (env*)


class M32Error(ValueError):
    pass


def _cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1")


@dataclass
class M32:
    version: int
    name: str
    alt_name: str
    anim_next: str
    damaged_name: str
    widths: list
    heights: list
    offsets: list
    flags: int
    contents: int
    value: int
    scale_x: float
    scale_y: float
    unknown_2d8: int
    detail_name: str
    detail_scale_x: float
    detail_scale_y: float
    detail_shift_x: float
    detail_shift_y: float
    unknown_36c: float
    detail_blend_src: int
    detail_blend_dst: int
    kind: int
    damage_value: float
    reserved: bytes
    data: bytes = field(repr=False, default=b"")
    trailing: int = 0

    @property
    def num_mips(self):
        n = 0
        while n < MAX_MIPS and self.widths[n] and self.heights[n]:
            n += 1
        return n

    @property
    def width(self):
        return self.widths[0]

    @property
    def height(self):
        return self.heights[0]

    @property
    def material(self):
        return (self.flags >> MATERIAL_SHIFT) & 0xFF

    @property
    def surface_flags(self):
        return self.flags & 0xFFFFFF

    def mip(self, level=0):
        """Raw RGBA bytes of one mip level."""
        if not 0 <= level < self.num_mips:
            raise IndexError(level)
        w, h, o = self.widths[level], self.heights[level], self.offsets[level]
        return self.data[o:o + w * h * 4]

    def image(self, level=0):
        from PIL import Image
        return Image.frombytes("RGBA", (self.widths[level], self.heights[level]), self.mip(level))


def parse(data, strict=True):
    """Decode a .m32 file. With strict=True every SPEC-100 invariant is checked."""
    if len(data) < HEADER_SIZE:
        raise M32Error("file shorter than header (%d bytes)" % len(data))
    version = struct.unpack_from("<i", data, 0)[0]
    names = [_cstr(data[4 + i * NAME_LEN:4 + (i + 1) * NAME_LEN]) for i in range(4)]
    widths = list(struct.unpack_from("<16I", data, 0x204))
    heights = list(struct.unpack_from("<16I", data, 0x244))
    offsets = list(struct.unpack_from("<16I", data, 0x284))
    flags, contents, value = struct.unpack_from("<III", data, 0x2C4)
    scale_x, scale_y = struct.unpack_from("<ff", data, 0x2D0)
    unknown_2d8 = struct.unpack_from("<i", data, 0x2D8)[0]
    detail_name = _cstr(data[0x2DC:0x2DC + NAME_LEN])
    dsx, dsy, dux, duy, u36c = struct.unpack_from("<5f", data, 0x35C)
    bsrc, bdst, kind = struct.unpack_from("<III", data, 0x370)
    damage_value = struct.unpack_from("<f", data, 0x37C)[0]
    reserved = data[0x380:HEADER_SIZE]
    m = M32(version, names[0], names[1], names[2], names[3], widths, heights, offsets,
            flags, contents, value, scale_x, scale_y, unknown_2d8, detail_name,
            dsx, dsy, dux, duy, u36c, bsrc, bdst, kind, damage_value, reserved, data)
    end = HEADER_SIZE
    for i in range(m.num_mips):
        end = max(end, offsets[i] + widths[i] * heights[i] * 4)
    m.trailing = len(data) - end
    if strict:
        problems = check(m)
        if problems:
            raise M32Error("; ".join(problems))
    return m


def check(m):
    """Return a list of SPEC-100 violations (empty when the file conforms)."""
    p = []
    if m.version != VERSION:
        p.append("version %d != %d" % (m.version, VERSION))
    n = m.num_mips
    if n == 0:
        p.append("no mip levels")
        return p
    if any(m.widths[n:]) or any(m.heights[n:]) or any(m.offsets[n:]):
        p.append("non-zero entries after last mip level")
    pos = HEADER_SIZE
    for i in range(n):
        w, h = m.widths[i], m.heights[i]
        if w & (w - 1) or h & (h - 1):
            p.append("level %d not power of two (%dx%d)" % (i, w, h))
        if i and (w != max(1, m.widths[i - 1] // 2) or h != max(1, m.heights[i - 1] // 2)):
            p.append("level %d size %dx%d breaks halving rule" % (i, w, h))
        if m.offsets[i] != pos:
            p.append("level %d offset %d, expected %d" % (i, m.offsets[i], pos))
        pos += w * h * 4
    if pos > len(m.data):
        p.append("pixel data truncated")
    return p


def observations(m):
    """Regularities seen in all stock files but not needed to decode (SPEC-100); deviations are notes, not errors."""
    notes = []
    full = 1
    w, h = m.width, m.height
    while (w, h) != (1, 1):
        w, h = max(1, w // 2), max(1, h // 2)
        full += 1
    if (m.num_mips < full) != bool(m.kind & KIND_NO_MIPS):
        notes.append("mip count %d/%d disagrees with kind bit 0x1" % (m.num_mips, full))
    if has_alpha(m) != bool(m.flags & SURF_ALPHA):
        notes.append("alpha content disagrees with flag 0x800")
    if bool(m.damaged_name) != (m.damage_value != 0):
        notes.append("damage_value set without damaged_name or vice versa")
    if m.trailing:
        notes.append("%d trailing bytes after last mip" % m.trailing)
    return notes


def load(path, strict=True):
    with open(path, "rb") as f:
        return parse(f.read(), strict)


def has_alpha(m):
    a = m.mip(0)[3::4]
    return any(x != 255 for x in a)


def describe(m):
    out = []
    out.append("name          %s" % m.name)
    for lab, v in (("alt_name", m.alt_name), ("anim_next", m.anim_next), ("damaged_name", m.damaged_name), ("detail_name", m.detail_name)):
        if v:
            out.append("%-13s %s" % (lab, v))
    out.append("size          %dx%d, %d mip level(s), trailing bytes %d" % (m.width, m.height, m.num_mips, m.trailing))
    out.append("flags         0x%08x (material %d, surface 0x%06x)" % (m.flags, m.material, m.surface_flags))
    out.append("contents      0x%08x  value %d" % (m.contents, m.value))
    out.append("scale         %g %g   unknown_2d8 %d" % (m.scale_x, m.scale_y, m.unknown_2d8))
    if m.detail_name or m.detail_blend_src:
        out.append("detail        scale %g %g shift %g %g blend 0x%x 0x%x" % (
            m.detail_scale_x, m.detail_scale_y, m.detail_shift_x, m.detail_shift_y, m.detail_blend_src, m.detail_blend_dst))
    out.append("kind          0x%x   damage_value %g" % (m.kind, m.damage_value))
    return "\n".join(out)


def contact_sheet(paths, out_png, cell=160, cols=10):
    from PIL import Image, ImageDraw
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGB", (cols * cell, rows * (cell + 24)), (40, 40, 40))
    dr = ImageDraw.Draw(sheet)
    for i, p in enumerate(paths):
        m = load(p, strict=False)
        im = m.image(0)
        sc = min((cell - 8) / m.width, (cell - 8) / m.height)
        im = im.resize((max(1, int(m.width * sc)), max(1, int(m.height * sc))), Image.NEAREST)
        bg = Image.new("RGBA", im.size, (200, 200, 200, 255))
        px = bg.load()
        for y in range(im.size[1]):
            for x in range(im.size[0]):
                if (x // 8 + y // 8) & 1:
                    px[x, y] = (120, 120, 120, 255)
        bg.alpha_composite(im)
        x, y = (i % cols) * cell, (i // cols) * (cell + 24)
        sheet.paste(bg.convert("RGB"), (x + 4, y + 4))
        dr.text((x + 2, y + cell + 2), os.path.basename(p)[:-4][:24], fill=(255, 255, 0))
        dr.text((x + 2, y + cell + 12), "%dx%d f%08x" % (m.width, m.height, m.flags), fill=(180, 180, 180))
    sheet.save(out_png)


def _walk(root, ext):
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if f.lower().endswith(ext):
                yield os.path.join(dp, f)


def validate_dir(root):
    ok = bad = noted = 0
    for p in _walk(root, ".m32"):
        try:
            m = load(p)
        except (M32Error, struct.error) as e:
            bad += 1
            print("FAIL %s: %s" % (p, e))
            continue
        ok += 1
        notes = observations(m)
        if notes:
            noted += 1
            print("note %s: %s" % (p, "; ".join(notes)))
    print("parsed %d ok, %d failed, %d with notes" % (ok, bad, noted))
    return bad == 0


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "info":
        for p in argv[1:]:
            print("== %s" % p)
            print(describe(load(p, strict=False)))
    elif cmd == "png":
        lvl = int(argv[3]) if len(argv) > 3 else 0
        load(argv[1], strict=False).image(lvl).save(argv[2])
    elif cmd == "validate":
        return 0 if validate_dir(argv[1]) else 1
    elif cmd == "sheet":
        contact_sheet(argv[2:], argv[1])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
