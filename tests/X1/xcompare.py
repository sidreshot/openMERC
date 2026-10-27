#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - compare two sweeps of xsweep.ps1 map by map: exit/crash/sound/captures side by side, the console
# error/warning lines that only one of them has, and a contact sheet per map (A above B, captures m0 m1 m2).
#   xcompare.py <prefixA> <prefixB> [--sheets <outdir>] [--maps a,b]
# Implements: black-box test harness (no game code)
import argparse
import json
import sys
from pathlib import Path

DATA = Path(r"C:\openmerc-data\x1")


def load(prefix):
    out = {}
    p = DATA / (prefix + ".jsonl")
    for ln in p.read_text(encoding="utf-8-sig").splitlines():
        ln = ln.strip().lstrip("\ufeff")
        if ln:
            r = json.loads(ln)
            out[r["map"]] = r
    return out


def brief(r):
    if not r:
        return "-"
    s = r["sound"]
    snd = "%d/%d" % (s["sound_windows"], s["windows"]) if s.get("ok") else "-"
    return "exit %s %4.1fs caps %d snd %s crash %d issues %d" % (
        r.get("exit"), r.get("seconds", 0), r.get("captures", 0), snd, len(r["crash"]), r["n_issue_lines"])


def sheet(prefa, prefb, m, out):
    from PIL import Image, ImageDraw
    rows = []
    for pref in (prefa, prefb):
        ims = []
        for s in ("m0", "m1", "m2"):
            f = DATA / ("%s_%s" % (pref, m)) / (s + ".png")
            ims.append(Image.open(f).convert("RGB").resize((320, 240)) if f.exists() else Image.new("RGB", (320, 240)))
        rows.append(ims)
    im = Image.new("RGB", (960, 500), (40, 40, 40))
    d = ImageDraw.Draw(im)
    for y, (pref, ims) in enumerate(zip((prefa, prefb), rows)):
        for x, s in enumerate(ims):
            im.paste(s, (x * 320, 20 + y * 240))
        d.text((4, 4 + y * 482 if y == 0 else 484), "%s %s" % (pref, m), fill=(255, 255, 0))
    im.save(out / ("%s_%s_%s.png" % (prefa, prefb, m)))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--sheets", default="")
    ap.add_argument("--maps", default="")
    a = ap.parse_args(argv[1:])
    A, B = load(a.a), load(a.b)
    maps = [m for m in a.maps.split(",") if m] or list(A) + [m for m in B if m not in A]
    if a.sheets:
        Path(a.sheets).mkdir(parents=True, exist_ok=True)
    for m in maps:
        ra, rb = A.get(m), B.get(m)
        print("%-6s A: %s" % (m, brief(ra)))
        print("%-6s B: %s" % ("", brief(rb)))
        ia = (ra or {}).get("issues", {})
        ib = (rb or {}).get("issues", {})
        for k in sorted(set(ia) - set(ib)):
            print("       only A: %s (x%d)" % (k[:150], ia[k]))
        for k in sorted(set(ib) - set(ia)):
            print("       only B: %s (x%d)" % (k[:150], ib[k]))
        for c in (ra or {}).get("crash", []):
            print("       A crash: " + c[:150])
        for c in (rb or {}).get("crash", []):
            print("       B crash: " + c[:150])
        if a.sheets:
            sheet(a.a, a.b, m, Path(a.sheets))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
