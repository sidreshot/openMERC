#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - summary of a sound capture written by OMsnd's s_om_wavefile (OpenAL Soft wave writer, 16-bit PCM):
# duration, peak, share of 0.5 s windows with sound. Works on files whose header was never finalised (process
# killed): the data size is then taken from the file size.
# usage: wavstat.py <snd.wav> [--json]
# Implements: black-box test harness (no game code)
import json
import struct
import sys
from pathlib import Path


def stat(path):
    b = Path(path).read_bytes()
    if len(b) < 44 or b[:4] != b"RIFF" or b[8:12] != b"WAVE":
        return {"ok": False, "why": "no RIFF/WAVE header", "bytes": len(b)}
    pos, fmt, data_off, data_len = 12, None, None, None
    while pos + 8 <= len(b):
        cid, clen = b[pos:pos + 4], struct.unpack_from("<I", b, pos + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", b, pos + 8)
        elif cid == b"data":
            data_off = pos + 8
            data_len = clen if clen and data_off + clen <= len(b) else len(b) - data_off
            break
        pos += 8 + clen + (clen & 1)
    if not fmt or data_off is None:
        return {"ok": False, "why": "no fmt/data chunk", "bytes": len(b)}
    tag, ch, rate, _, align, bits = fmt
    if bits != 16:
        return {"ok": False, "why": "bits %d" % bits}
    n = data_len // 2
    import array
    a = array.array("h")
    a.frombytes(b[data_off:data_off + n * 2])
    frames = len(a) // ch
    win = max(1, rate // 2)
    loud = 0
    wins = 0
    peak = 0
    for w in range(0, frames, win):
        seg = a[w * ch:(w + win) * ch]
        if not seg:
            break
        p = max(abs(min(seg)), abs(max(seg)))
        peak = max(peak, p)
        wins += 1
        if p > 64:
            loud += 1
    return {"ok": True, "rate": rate, "channels": ch, "seconds": round(frames / rate, 1), "peak": peak,
            "windows": wins, "sound_windows": loud}


def main(argv):
    if len(argv) < 2:
        print(__doc__ or "usage: wavstat.py <wav>")
        return 2
    p = Path(argv[1])
    r = stat(p) if p.exists() else {"ok": False, "why": "missing"}
    if "--json" in argv:
        print(json.dumps(r))
    elif r.get("ok"):
        print("snd %.1fs peak %d sound %d/%d" % (r["seconds"], r["peak"], r["sound_windows"], r["windows"]))
    else:
        print("snd: " + r.get("why", "?"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
