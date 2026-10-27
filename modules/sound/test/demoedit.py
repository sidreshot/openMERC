#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-311 (demo layout), SPEC-201/202/203 (messages), SPEC-004 (svc_sound_info) - black-box test input
"""Make scripted test demos for the sound backends from a recorded client demo (`record`, SPEC-311).

A client demo is the server message stream; `demomap` replays it, so every sound backend gets exactly the same
input. This tool rewrites a recorded demo:

  --music  T:V[,T:V...]    from time T (seconds after the first frame) on, the player-state music byte
                           (SPEC-202 player-state field 0x00800000) is V in every frame
  --ambient T:V[,...]      same for the general ambient-set field (0x01000000, short)
  --info   T:MASK:V[:V...] append svc_sound_info (SPEC-004: mask bits 0x01 intensity, 0x02 designer value,
                           0x04 general ambient set, 0x08 sound-processing type; one byte per set bit)
                           to the block of the first frame at or after T
  --cs     N=TEXT          replace configstring N in the header (e.g. 13=trn1: the music set)
  --cut    T               end the demo after time T
  --dump                   print blocks, frames and music/ambient values

Frames are 0.1 s apart (one block per server frame); T is converted with the first frame's number as 0.
The player-state field is forced present in every frame from the first T on, so delta coding from any
earlier frame stays consistent. Uses the protocol decoder tools/net/sofnet.py (SPEC-200..203) to find the
message boundaries.

usage: demoedit.py <in.dm2> <out.dm2> [options]      demoedit.py <in.dm2> --dump
"""
import argparse
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools" / "net"))
import sofnet  # noqa: E402

MUSIC_FLAG = 0x00800000
AMBIENT_FLAG = 0x01000000


def read_blocks(path):
    data = Path(path).read_bytes()
    pos, blocks = 0, []
    while pos + 4 <= len(data):
        n = struct.unpack_from("<i", data, pos)[0]
        pos += 4
        if n < 0:
            break
        blocks.append(data[pos:pos + n])
        pos += n
    return blocks


def write_blocks(path, blocks):
    out = bytearray()
    for b in blocks:
        out += struct.pack("<i", len(b)) + b
    out += struct.pack("<i", -1)
    Path(path).write_bytes(out)


def ps_layout(msg, start):
    """Walk a player state (SPEC-202 4) at msg[start:]; returns (flags, {field: (offset, size)}, end)."""
    flags = struct.unpack_from("<I", msg, start)[0]
    p = start + 4
    pos = {}
    for flag, name, fmt in sofnet.PS_FIELDS:
        if flag is None:
            smask = struct.unpack_from("<I", msg, p)[0]
            n = 4 + 2 * bin(smask & 0xFFFF).count("1")
            pos["stats"] = (p, n)
            p += n
        elif flags & flag:
            n = sum(1 if c in "bc" else 2 for c in fmt)
            pos[name] = (p, n)
            p += n
        else:
            pos[name] = (p, 0)  # where it would go
    return flags, pos, p


def frame_ps_start(msg, start):
    """Offset of the player-state flags inside an svc_frame message starting at msg[start] (cmd byte 23)."""
    p = start + 1 + 4 + 4 + 4
    n = msg[p]
    p += 1 + n
    assert msg[p] == 20, "not playerinfo"
    return p + 1


def parse(blocks):
    sess = sofnet.Session()
    out = []
    for i, b in enumerate(blocks):
        ctx = {"t": i, "seq": i}
        m = sofnet.Msg(b)
        sess.parse_svc(m, ctx)
        out.append(ctx.get("spans", []))
    return sess, out


def timeline(spec, conv):
    ev = []
    if spec:
        for item in spec.split(","):
            t, v = item.split(":", 1)
            ev.append((float(t), conv(v)))
    return sorted(ev)


def value_at(ev, t):
    v = None
    for et, ev_ in ev:
        if t + 1e-6 >= et:
            v = ev_
    return v


def encode_cs(idx, text):
    return bytes([15]) + struct.pack("<hh", idx, -1) + text.encode("latin-1") + b"\0"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("inp")
    ap.add_argument("out", nargs="?")
    ap.add_argument("--music")
    ap.add_argument("--ambient")
    ap.add_argument("--info", action="append", default=[])
    ap.add_argument("--cs", action="append", default=[])
    ap.add_argument("--cut", type=float)
    ap.add_argument("--dump", action="store_true")
    a = ap.parse_args()

    blocks = read_blocks(a.inp)
    sess, spans = parse(blocks)
    music = timeline(a.music, int)
    amb = timeline(a.ambient, int)
    infos = []
    for s in a.info:
        parts = s.split(":")
        infos.append((float(parts[0]), int(parts[1], 0), [int(x, 0) for x in parts[2:]]))
    cs_edit = {}
    for s in a.cs:
        k, v = s.split("=", 1)
        cs_edit[int(k)] = v

    first_frame = None
    out_blocks = []
    pending_info = sorted(infos)
    log = []
    for i, (b, sp) in enumerate(zip(blocks, spans)):
        b = bytearray(b)
        edits = []  # (start, end, replacement) in original coordinates, applied back to front
        tframe = None
        for name, s, e in sp:
            if name == "svc_configstring":
                idx = struct.unpack_from("<h", b, s + 1)[0]
                if idx in cs_edit:
                    edits.append((s, e, encode_cs(idx, cs_edit[idx])))
                    log.append("block %d: configstring %d := %r" % (i, idx, cs_edit[idx]))
            elif name == "svc_frame":
                frame = struct.unpack_from("<i", b, s + 1)[0]
                if first_frame is None:
                    first_frame = frame
                tframe = (frame - first_frame) * 0.1
                ps = frame_ps_start(b, s)
                flags, pos, _ = ps_layout(b, ps)
                mv = value_at(music, tframe)
                av = value_at(amb, tframe)
                if a.dump:
                    mo, mn = pos["music"]
                    ao, an = pos["ambient"]
                    print("block %4d frame %6d t %7.1f music %s ambient %s" % (
                        i, frame, tframe, b[mo] if mn else "-", struct.unpack_from("<h", b, ao)[0] if an else "-"))
                newflags = flags
                if mv is not None:
                    newflags |= MUSIC_FLAG
                    off, n = pos["music"]
                    edits.append((off, off + n, bytes([mv & 0xFF])))
                if av is not None:
                    newflags |= AMBIENT_FLAG
                    off, n = pos["ambient"]
                    edits.append((off, off + n, struct.pack("<h", av)))
                if newflags != flags:
                    edits.append((ps, ps + 4, struct.pack("<I", newflags)))
        if a.cut is not None and tframe is not None and tframe > a.cut:
            break
        for s, e, rep in sorted(edits, key=lambda x: -x[0]):
            b[s:e] = rep
        if tframe is not None:
            while pending_info and pending_info[0][0] <= tframe + 1e-6:
                t, mask, vals = pending_info.pop(0)
                b += bytes([4, mask & 0xFF] + [v & 0xFF for v in vals])
                log.append("block %d (t %.1f): svc_sound_info mask 0x%02x %s" % (i, tframe, mask, vals))
        if len(b) > 16384:
            raise SystemExit("block %d grew beyond 16384 bytes" % i)
        out_blocks.append(bytes(b))
    for l in log:
        print(l)
    if pending_info:
        print("warning: %d svc_sound_info after the end of the demo" % len(pending_info))
    if a.out:
        write_blocks(a.out, out_blocks)
        # check: the result parses
        sofnet.Session()
        parse(out_blocks)
        last = (frame - first_frame) * 0.1 if first_frame is not None else 0
        print("wrote %s: %d blocks, %.1f s of frames" % (a.out, len(out_blocks), min(last, a.cut or last)))


if __name__ == "__main__":
    main()
