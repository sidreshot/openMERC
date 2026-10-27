#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-132 (uses SPEC-100 for the .m32 glyph page)
"""Bitmap fonts (pics/menus/fonts/*.fnt + same-named .m32 page).

usage:
  fnt.py info <font.fnt>                         glyph table summary
  fnt.py validate [--out DIR]                    parse every stock .fnt in every pak
  fnt.py render <font.fnt> <out.png> <text> [tint 0xAABBGGRR] [scale] [codepage]
  fnt.py sheets <outdir>                         charset sheets + samples for all stock fonts
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import m32  # noqa: E402  (SPEC-100)
from vfs import PakVFS  # noqa: E402

RECORD = 28
FIRST_CHAR = 32
REC_FMT = "<4f3i"   # s0 t0 s1 t1 (float32), width height baseline (int32)


class FNTError(ValueError):
    pass


def parse(data):
    if len(data) % RECORD:
        raise FNTError(f"size {len(data)} is not a multiple of {RECORD}")
    glyphs = []
    for i in range(len(data) // RECORD):
        s0, t0, s1, t1, w, h, base = struct.unpack_from(REC_FMT, data, i * RECORD)
        glyphs.append({"code": FIRST_CHAR + i, "s0": s0, "t0": t0, "s1": s1, "t1": t1,
                       "w": w, "h": h, "baseline": base})
    return glyphs


def is_empty(g):
    return g["w"] == 0 and g["h"] == 0 and g["s0"] == g["s1"] == g["t0"] == g["t1"] == 0


def check(glyphs, page_w, page_h):
    """Returns (errors, notes)."""
    err, notes = [], []
    for g in glyphs:
        if is_empty(g):
            if g["baseline"] != 0:
                err.append(f"code {g['code']}: empty glyph with baseline {g['baseline']}")
            continue
        if not (0 <= g["s0"] < g["s1"] <= 1 and 0 <= g["t0"] < g["t1"] <= 1):
            err.append(f"code {g['code']}: texture rect out of range")
        if g["w"] <= 0 or g["h"] <= 0:
            err.append(f"code {g['code']}: non-positive size")
        if not (-1 <= g["baseline"] <= g["h"]):
            err.append(f"code {g['code']}: baseline {g['baseline']} outside 0..h")
        pw = (g["s1"] - g["s0"]) * page_w
        ph = (g["t1"] - g["t0"]) * page_h
        if abs(pw - g["w"]) > 0.01 or abs(ph - g["h"]) > 0.01:
            notes.append(f"code {g['code']}: w,h {g['w']}x{g['h']} but texture rect is "
                         f"{pw:g}x{ph:g} px (scaled draw)")
        for v, n in ((g["s0"] * page_w, "s0"), (g["t0"] * page_h, "t0")):
            if abs(v - round(v)) > 1e-3:
                notes.append(f"code {g['code']}: {n} not on a pixel boundary")
    return err, notes


def load(vfs, fnt_path, pak=None):
    glyphs = parse(vfs.read(fnt_path, pak))
    page_path = fnt_path[:-4] + ".m32"
    page = m32.parse(vfs.read(page_path, pak))
    return glyphs, page


def metrics(glyphs):
    vis = [g for g in glyphs if not is_empty(g)]
    asc = max((g["baseline"] for g in vis), default=0)
    desc = max((g["h"] - g["baseline"] for g in vis), default=0)
    return asc, desc


def render(glyphs, page, text_bytes, tint=0xFFFFFFFF, scale=2, space=None, bg=(0, 0, 0, 255)):
    """Draw a byte string. Rules from SPEC-132: the glyph for byte c is record c-32;
    the texture rect (s0,t0)-(s1,t1) is drawn at w x h pixels; the pen advances by w (hypothesis);
    the glyph's top is `baseline` rows above the pen's baseline. The width of a space
    (an empty record) is UNKNOWN; `space` defaults to half the advance of 'n' (tool
    choice). Tint is 0xAABBGGRR (menu <tint> format) and multiplies the white glyphs."""
    from PIL import Image
    img = page.image()
    W, H = img.size
    by = {g["code"]: g for g in glyphs}
    if space is None:
        n = by.get(ord("n"))
        space = max(2, (n["w"] if n and not is_empty(n) else 6) // 2)
    asc, desc = metrics(glyphs)
    if asc <= 0:          # inttext: baseline -1 everywhere -> top-aligned
        asc, desc = 0, max(g["h"] for g in glyphs)
    lines = text_bytes.split(b"\n")
    widths = []
    for ln in lines:
        x = 0
        for c in ln:
            g = by.get(c)
            x += space if (g is None or is_empty(g)) else g["w"]
        widths.append(x)
    lh = asc + desc + 1
    out = Image.new("RGBA", (max(widths + [1]) + 4, lh * len(lines) + 4), bg)
    r, gg, b, a = tint & 0xFF, (tint >> 8) & 0xFF, (tint >> 16) & 0xFF, (tint >> 24) & 0xFF
    for li, ln in enumerate(lines):
        x = 2
        base_y = 2 + li * lh + asc
        for c in ln:
            g = by.get(c)
            if g is None or is_empty(g):
                x += space
                continue
            box = (round(g["s0"] * W), round(g["t0"] * H), round(g["s1"] * W), round(g["t1"] * H))
            cell = img.crop(box)
            if cell.size != (g["w"], g["h"]):
                cell = cell.resize((g["w"], g["h"]), Image.BILINEAR)
            cr, cg, cb, ca = cell.split()
            cr = cr.point(lambda v: v * r // 255)
            cg = cg.point(lambda v: v * gg // 255)
            cb = cb.point(lambda v: v * b // 255)
            ca = ca.point(lambda v: v * a // 255)
            cell = Image.merge("RGBA", (cr, cg, cb, ca))
            top = base_y - (g["baseline"] if g["baseline"] >= 0 else asc)
            out.alpha_composite(cell, (x, top))
            x += g["w"]
    if scale != 1:
        out = out.resize((out.width * scale, out.height * scale), Image.NEAREST)
    return out


def charset_bytes():
    rows = []
    for r in range(7):
        rows.append(bytes(range(32 + r * 32, 64 + r * 32)))
    return b"\n".join(rows)


SAMPLES = {
    "latin": "SOLDIER OF FORTUNE - Options: Video, Audio, Controls.\n"
             "The quick brown fox jumps over the lazy dog 0123456789 (100%)\n"
             "Grüße! Élève à l'école: © ® ¿ £ Ÿ",
    "cyrillic": "СОЛДАТ УДАЧИ - Настройки\n"
                "Съешь же ещё этих мягких французских булок",
    "digits": "0123456789",
}


def validate(out=None):
    vfs = PakVFS()
    res = {"files": 0, "parsed": 0, "errors": [], "notes": {}, "fonts": {}}
    for pak in vfs.order:
        for name in sorted(vfs.names("*.fnt", pak=pak)):
            res["files"] += 1
            where = f"{pak}/{name}"
            try:
                glyphs, page = load(vfs, name, pak)
            except Exception as e:  # noqa: BLE001
                res["errors"].append(f"{where}: {e}")
                continue
            err, notes = check(glyphs, page.width, page.height)
            if err:
                res["errors"].extend(f"{where}: {e}" for e in err)
                continue
            res["parsed"] += 1
            vis = [g for g in glyphs if not is_empty(g)]
            asc, desc = metrics(glyphs)
            res["fonts"][where] = {
                "records": len(glyphs), "codes": f"{FIRST_CHAR}..{FIRST_CHAR + len(glyphs) - 1}",
                "glyphs": len(vis), "page": f"{page.width}x{page.height}",
                "empty_codes": [g["code"] for g in glyphs if is_empty(g)],
                "max_h": max(g["h"] for g in vis), "ascent": asc, "descent": desc,
                "advance_range": [min(g["w"] for g in vis), max(g["w"] for g in vis)],
            }
            if notes:
                res["notes"][where] = notes
    if out:
        import json
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, "fnt_validation.json"), "w") as f:
            json.dump(res, f, indent=1)
    return res


def sheets(outdir):
    vfs = PakVFS()
    os.makedirs(outdir, exist_ok=True)
    made = []
    for pak in vfs.order:
        for name in sorted(vfs.names("*.fnt", pak=pak)):
            glyphs, page = load(vfs, name, pak)
            stem = f"{pak}_{os.path.basename(name)[:-4]}"
            render(glyphs, page, charset_bytes(), 0xFFFFFFFF, 3).save(
                os.path.join(outdir, stem + "_charset.png"))
            page.image().save(os.path.join(outdir, stem + "_page.png"))
            cp = "cp1251" if pak == "zmain" else "cp1252"
            if "inttext" in name:
                txt = SAMPLES["digits"].encode()
            elif cp == "cp1251":
                txt = (SAMPLES["cyrillic"] + "\n" + SAMPLES["latin"].split("\n")[0]).encode(cp)
            else:
                txt = SAMPLES["latin"].encode(cp)
            render(glyphs, page, txt, 0xFF97A169, 3).save(os.path.join(outdir, stem + "_sample.png"))
            made.append(stem)
    return made


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    if argv[0] == "info":
        data = open(argv[1], "rb").read()
        for g in parse(data):
            if not is_empty(g):
                print(f"{g['code']:3} {chr(g['code'])!r:6} st=({g['s0']:.4f},{g['t0']:.4f})-"
                      f"({g['s1']:.4f},{g['t1']:.4f}) {g['w']}x{g['h']} base {g['baseline']}")
        return 0
    if argv[0] == "validate":
        out = argv[argv.index("--out") + 1] if "--out" in argv else None
        r = validate(out)
        print(f"fnt files: {r['files']}  parsed: {r['parsed']}  errors: {len(r['errors'])}")
        for e in r["errors"]:
            print("  ERROR", e)
        for k, v in r["fonts"].items():
            print(f"  {k}: {v['records']} records ({v['codes']}), {v['glyphs']} glyphs, page "
                  f"{v['page']}, ascent {v['ascent']} descent {v['descent']}, max h {v['max_h']}, "
                  f"advance {v['advance_range']}, empty codes {len(v['empty_codes'])}")
        for k, v in r["notes"].items():
            for n in v:
                print(f"  NOTE {k}: {n}")
        return 1 if r["errors"] else 0
    if argv[0] == "render":
        vfs = PakVFS()
        glyphs, page = load(vfs, argv[1])
        tint = int(argv[4], 16) if len(argv) > 4 else 0xFFFFFFFF
        scale = int(argv[5]) if len(argv) > 5 else 2
        cp = argv[6] if len(argv) > 6 else "cp1252"
        render(glyphs, page, argv[3].replace("\\n", "\n").encode(cp, "replace"), tint, scale).save(argv[2])
        return 0
    if argv[0] == "sheets":
        print(sheets(argv[1]))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
