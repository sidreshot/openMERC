#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-141
"""Reader for .rof motion files (ROFF version 1, SPEC-141).

Layout: char[4] "ROFF", int32 version (=1), float32 frame count, then frames of
6 float32: origin delta (x, y, z) and angle delta (pitch, yaw, roll) in degrees.
Scripts (.os, SPEC-140) play a ROF on an entity with opcode 0x29.

usage:
  rof.py info <file.rof>                         header + per-frame deltas + totals
  rof.py validate <roff dir> [ds root ...]       parse every .rof; with script roots, also
                                                 cross-check names used by scripts
  rof.py chart <file.rof> <out.png>              per-frame deltas and accumulated pose
  rof.py plot <map.bsp> <out.png> <ds root> ...  draw every ROF path played by the map's
                                                 scripts over a top-down outline
"""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

MAGIC = b"ROFF"
VERSION = 1
HEADER = 12
FRAME = 24


class ROFError(ValueError):
    pass


class ROF:
    def __init__(self, data, strict=False):
        if len(data) < HEADER:
            raise ROFError("file too short")
        magic, self.version, self.count_field = struct.unpack_from("<4sif", data, 0)
        if magic != MAGIC:
            raise ROFError("bad magic %r" % magic)
        if self.version != VERSION:
            raise ROFError("version %d" % self.version)
        body = len(data) - HEADER
        if body % FRAME:
            raise ROFError("body %d bytes is not a multiple of %d" % (body, FRAME))
        self.frames_in_file = body // FRAME
        self.count_mismatch = (self.count_field != int(self.count_field)
                               or int(self.count_field) != self.frames_in_file)
        if strict and self.count_mismatch:
            raise ROFError("count %g != %d frames present" % (self.count_field, self.frames_in_file))
        self.frames = [struct.unpack_from("<6f", data, HEADER + FRAME * i) for i in range(self.frames_in_file)]

    def totals(self):
        t = [0.0] * 6
        for fr in self.frames:
            for k in range(6):
                t[k] += fr[k]
        return t

    def path(self, origin=(0.0, 0.0, 0.0), angles=(0.0, 0.0, 0.0)):
        """Absolute (origin, angles) after each frame, starting from the given pose (frame 0 = start)."""
        o, a = list(origin), list(angles)
        out = [(tuple(o), tuple(a))]
        for fr in self.frames:
            for k in range(3):
                o[k] += fr[k]
                a[k] += fr[3 + k]
            out.append((tuple(o), tuple(a)))
        return out


def load(path, strict=False):
    with open(path, "rb") as f:
        return ROF(f.read(), strict)


def iter_rofs(root):
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if f.lower().endswith(".rof"):
                yield os.path.join(dp, f)


def script_rof_uses(script_roots):
    """{rof basename (lower): [(script relpath, opcode)]} for opcodes 0x29, 0x2F, 0x32."""
    import os_script
    uses = {}
    for root in script_roots:
        for path in os_script.iter_files(root):
            s = os_script.load(path)
            for op in (0x29, 0x2F, 0x32):
                for ins, args in s.calls(op):
                    names = [a.args[1] for a in args if a.op == 0x11 and a.args[0] == os_script.P_STRING]
                    for n in names:
                        base = os.path.basename(n.replace("\\", "/")).lower()
                        uses.setdefault(base, []).append((os.path.relpath(path, root).replace("\\", "/"), op))
    return uses


def chart(r, out_png, title=""):
    """Four line panels (Pillow only): per-frame deltas and accumulated pose."""
    from PIL import Image, ImageDraw
    p = r.path()
    panels = [
        ("origin delta per frame (units)", [[f[k] for f in r.frames] for k in range(3)]),
        ("angle delta per frame (deg)", [[f[3 + k] for f in r.frames] for k in range(3)]),
        ("accumulated origin (units)", [[q[0][k] for q in p] for k in range(3)]),
        ("accumulated angles (deg)", [[q[1][k] for q in p] for k in range(3)]),
    ]
    PW, PH, M = 420, 220, 34
    img = Image.new("RGB", (2 * PW, 2 * PH + 20), (255, 255, 255))
    dr = ImageDraw.Draw(img)
    dr.text((6, 4), title, fill=(0, 0, 0))
    cols = [(214, 39, 40), (31, 119, 180), (44, 160, 44)]
    names = [("x", "y", "z"), ("pitch", "yaw", "roll")]
    for i, (ttl, series) in enumerate(panels):
        ox, oy = (i % 2) * PW, 20 + (i // 2) * PH
        allv = [v for s in series for v in s] or [0.0]
        lo, hi = min(allv + [0.0]), max(allv + [0.0])
        if hi - lo < 1e-6:
            hi = lo + 1
        n = max(1, max(len(s) for s in series) - 1)
        X = lambda k: ox + M + (PW - M - 10) * k / n
        Y = lambda v: oy + PH - M - (PH - M - 20) * (v - lo) / (hi - lo)
        dr.rectangle((ox + M, oy + 20, ox + PW - 10, oy + PH - M), outline=(180, 180, 180))
        dr.line((ox + M, Y(0), ox + PW - 10, Y(0)), fill=(200, 200, 200))
        dr.text((ox + M, oy + 6), ttl, fill=(0, 0, 0))
        dr.text((ox + 2, oy + 20), "%.4g" % hi, fill=(90, 90, 90))
        dr.text((ox + 2, oy + PH - M - 10), "%.4g" % lo, fill=(90, 90, 90))
        dr.text((ox + PW - 70, oy + PH - M + 4), "frame %d" % n, fill=(90, 90, 90))
        for k, s in enumerate(series):
            if len(s) > 1:
                dr.line([(X(j), Y(v)) for j, v in enumerate(s)], fill=cols[k], width=2)
            dr.text((ox + M + 4 + 60 * k, oy + PH - M + 4), names[i % 2][k], fill=cols[k])
    img.save(out_png)


def _entity_origin(b, e):
    if "origin" in e:
        try:
            return tuple(float(x) for x in e["origin"].split())
        except ValueError:
            pass
    m = e.get("model", "")
    if m.startswith("*"):
        mm = b.models[int(m[1:])]
        return tuple((mm.mins[k] + mm.maxs[k]) / 2 for k in range(3))
    return None


def map_paths(bsp_path, script_roots, roff_dirs):
    """[(script, entity name, rof name, list of (origin, angles))] for ROFs played by the map's scripts."""
    import bsp
    import os_script
    b = bsp.load(bsp_path)
    ents = b.entities()
    rofs = {}
    for d in roff_dirs:
        for p in iter_rofs(d):
            rofs.setdefault(os.path.basename(p).lower(), p)
    out = []
    for e in ents:
        sname = e.get("script") or e.get("Script")
        if not sname:
            continue
        spath = None
        for root in script_roots:
            cand = os.path.join(root, *(sname.replace("\\", "/") + ".os").split("/"))
            if os.path.exists(cand):
                spath = cand
                break
        if not spath:
            continue
        s = os_script.load(spath)
        binds = s.entity_bindings({k.lower(): v for k, v in e.items()})
        for ins, args in s.calls(0x29):
            if len(args) < 2 or args[0].args[0] != os_script.P_VAR:
                continue
            fname = args[1].args[1] if args[1].args[0] == os_script.P_STRING else None
            bind = binds.get(args[0].args[1])
            if not fname or not bind or bind[0] not in ("targetname", "scripttarget", "param"):
                continue
            key = "targetname" if bind[0] == "param" else bind[0]
            tgt = [x for x in ents if x.get(key, "").lower() == (bind[1] or "").lower()]
            rp = rofs.get(os.path.basename(fname.replace("\\", "/")).lower())
            if not tgt or not rp:
                continue
            org = _entity_origin(b, tgt[0])
            if org is None:
                continue
            ang = (0.0, float(tgt[0].get("angle", 0) or 0), 0.0)
            out.append((sname, bind[1], os.path.basename(rp), load(rp).path(org, ang), tgt[0].get("classname")))
    return b, out


def plot(bsp_path, out_png, script_roots, roff_dirs, upp=4.0):
    from mapview import TopDown
    b, paths = map_paths(bsp_path, script_roots, roff_dirs)
    pts = [q[0] for _, _, _, pth, _ in paths for q in pth]
    td = TopDown(b, upp, extra_points=pts)
    dr = td.draw()
    palette = [(255, 80, 80), (80, 200, 255), (255, 200, 60), (120, 255, 120), (255, 120, 255), (255, 255, 255)]
    for i, (sname, ename, rname, pth, cls) in enumerate(paths):
        col = palette[i % len(palette)]
        xy = [td.xy(q[0]) for q in pth]
        if len(xy) > 1:
            dr.line(xy, fill=col, width=2)
        x, y = xy[0]
        dr.ellipse((x - 3, y - 3, x + 3, y + 3), outline=col)
        x, y = xy[-1]
        dr.rectangle((x - 3, y - 3, x + 3, y + 3), fill=col)
        # heading ticks every 10 frames
        for k in range(0, len(pth), 10):
            (o, a) = pth[k]
            yaw = math.radians(a[1])
            p0 = td.xy(o)
            dr.line([p0, (p0[0] + 8 * math.cos(yaw), p0[1] - 8 * math.sin(yaw))], fill=col)
        dr.text((xy[0][0] + 5, xy[0][1] + 3), "%s:%s" % (ename, rname[:-4]), fill=col)
    td.img.save(out_png)
    return len(paths)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "info":
        r = load(argv[1])
        print("version %d, count field %g, frames in file %d%s" % (
            r.version, r.count_field, r.frames_in_file, "  (COUNT MISMATCH)" if r.count_mismatch else ""))
        for i, f in enumerate(r.frames):
            print("%4d  d_origin %9.3f %9.3f %9.3f   d_angles %8.3f %8.3f %8.3f" % ((i,) + f))
        t = r.totals()
        print("total d_origin (%.2f %.2f %.2f) |%.2f|, total d_angles (%.2f %.2f %.2f)" % (
            t[0], t[1], t[2], math.sqrt(t[0] ** 2 + t[1] ** 2 + t[2] ** 2), t[3], t[4], t[5]))
    elif cmd == "validate":
        ok = bad = mism = frames = 0
        names = {}
        for p in iter_rofs(argv[1]):
            try:
                r = load(p)
                ok += 1
                frames += r.frames_in_file
                names[os.path.basename(p).lower()] = p
                if r.count_mismatch:
                    mism += 1
                    print("COUNT %s: header %g, frames present %d" % (p, r.count_field, r.frames_in_file))
            except (ROFError, struct.error) as e:
                bad += 1
                print("FAIL %s: %s" % (p, e))
        print("parsed %d ok, %d failed, %d with count field != frames present; %d frames total"
              % (ok, bad, mism, frames))
        if len(argv) > 2:
            uses = script_rof_uses(argv[2:])
            missing = sorted(set(uses) - set(names))
            unused = sorted(set(names) - set(uses))
            print("scripts reference %d distinct ROF names; %d files referenced, %d unreferenced, %d names missing"
                  % (len(uses), len(set(uses) & set(names)), len(unused), len(missing)))
            for m in missing:
                print("  MISSING %s  (used by %s)" % (m, ", ".join(sorted({u for u, _ in uses[m]}))))
            for u in unused:
                print("  UNUSED %s" % u)
        return 0 if bad == 0 else 1
    elif cmd == "chart":
        chart(load(argv[1]), argv[2], os.path.basename(argv[1]))
        print("wrote", argv[2])
    elif cmd == "plot":
        roots = argv[3:]
        roffs = [os.path.join(r, "roff") for r in roots if os.path.isdir(os.path.join(r, "roff"))]
        n = plot(argv[1], argv[2], roots, roffs)
        print("wrote %s with %d ROF paths" % (argv[2], n))
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
