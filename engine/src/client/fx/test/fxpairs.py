#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-400 / SPEC-401 black-box comparison (no game code)
#
# Pairs captures of the original client (ref_omcap, `echo OMSHOT <group>_<k>`) with the OpenMerc client's frame
# bursts after temp entities (om_fxshot: fx<burst>_t<type>_<ms>.tga). Both clients draw from the same fixed eye
# (om_capview / om_fxview), so the pictures differ only in effects and 2D overlays.
# Both clients run on this PC, so the capture files' write times share one clock: the candidates for an original
# capture are the OpenMerc frames written within --window seconds of it (same moment of the game, up to the
# network delay); among them the frame whose change against its client's baseline (original: its first
# capture; OpenMerc: c_00.tga, taken right after om_fxview was set) is closest (grey levels, 2D overlays
# masked) is chosen. Captures with no OpenMerc frame in the window get no pair. Writes per pair
# <tag>_<name>_orig.png, <tag>_<name>_om.png and a side-by-side <tag>_<name>_pair.png (original left, labels
# below), and <tag>_pairs.txt (chosen frame: burst, trigger, ms after it, time offset, score).
#
#   python fxpairs.py --om <OpenMerc scrnshot dir> --orig <original capture dir> --tag <tag> --out <dir>
import argparse
import re
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

FX = re.compile(r"fx(\d+)_([te]\d+)_(\d+)$")
SIZE = (320, 240)


def grey(path):
    return np.asarray(Image.open(path).convert("L").resize(SIZE, Image.BILINEAR), dtype=np.float32)


def mask(shape):
    # 2D overlays: notify lines (top), HUD (bottom), spectator icon / frag count (top right), crosshair
    h, w = shape
    m = np.ones(shape, dtype=bool)
    m[: int(h * 0.08), :] = False
    m[int(h * 0.82):, :] = False
    m[: int(h * 0.20), int(w * 0.85):] = False
    m[h // 2 - 2: h // 2 + 2, w // 2 - 2: w // 2 + 2] = False
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--om", required=True)
    ap.add_argument("--orig", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--window", type=float, default=0.15, help="seconds between the two capture times")
    ap.add_argument("--keep", action="store_true", help="also convert every burst frame to PNG")
    a = ap.parse_args()
    out = Path(a.out)
    out.mkdir(parents=True, exist_ok=True)
    omdir = Path(a.om)

    om = []
    for p in omdir.glob("fx*.tga"):
        m = FX.match(p.stem)
        if m:
            om.append((int(m.group(1)), int(m.group(3)), m.group(2), p))
    om.sort()
    if not om:
        print("no OpenMerc burst frames in", a.om)
        return 1
    origs = sorted(Path(a.orig).glob("*.tga"), key=lambda p: p.stat().st_mtime)
    if not origs:
        print("no original captures in", a.orig)
        return 1

    base_om = omdir / "c_00.tga"
    gom0 = grey(base_om) if base_om.exists() else grey(om[0][3])
    gor0 = grey(origs[0])
    M = mask(gor0.shape)
    dom = [np.abs(grey(p) - gom0) for _, _, _, p in om]

    tom = [p.stat().st_mtime for _, _, _, p in om]
    lines = ["pairs by capture time (window %.2f s)" % a.window]
    for p in origs:
        name = p.stem
        t = p.stat().st_mtime
        cand = [i for i in range(len(om)) if abs(tom[i] - t) <= a.window]
        if not cand:
            near = min(range(len(om)), key=lambda i: abs(tom[i] - t))
            lines.append(f"{name}: no OpenMerc frame within {a.window:.2f} s (nearest {tom[near] - t:+.2f} s)")
            continue
        dor = np.abs(grey(p) - gor0)
        scores = {i: float(np.abs(dor - dom[i])[M].mean()) for i in cand}
        order = sorted(cand, key=lambda i: scores[i])
        best = order[0]
        b, ms, te, op = om[best]
        full = Image.open(p).convert("RGB")
        omfull = Image.open(op).convert("RGB")
        full.save(out / f"{a.tag}_{name}_orig.png")
        omfull.save(out / f"{a.tag}_{name}_om.png")
        w, h = full.size
        sbs = Image.new("RGB", (w * 2 + 4, h + 16), (0, 0, 0))
        sbs.paste(full, (0, 0))
        sbs.paste(omfull.resize((w, h)), (w + 4, 0))
        d = ImageDraw.Draw(sbs)
        d.text((4, h + 2), f"original 1.07f: {name}", fill=(255, 255, 0))
        d.text((w + 8, h + 2), f"OpenMerc: burst {b} ({te}) +{ms} ms", fill=(255, 255, 0))
        sbs.save(out / f"{a.tag}_{name}_pair.png")
        nxt = order[1] if len(order) > 1 else best
        lines.append(f"{name}: burst {b} {te} +{ms} ms, {tom[best] - t:+.3f} s, score {scores[best]:.2f}  "
                     f"(next: burst {om[nxt][0]} +{om[nxt][1]} ms {scores[nxt]:.2f})")
    if a.keep:
        for b, ms, te, p in om:
            Image.open(p).save(out / f"{a.tag}_om_{p.stem}.png")
    per = {}
    for b, ms, te, p in om:
        per.setdefault(b, (te, []))[1].append(ms)
    lines.append("bursts: " + ", ".join(f"{b}:{te} ({len(v)} frames, {min(v)}-{max(v)} ms)"
                                         for b, (te, v) in sorted(per.items())))
    (out / f"{a.tag}_pairs.txt").write_text("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
