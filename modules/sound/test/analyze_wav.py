#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-004 (black-box verification of a sound backend's output)
"""Analyse a capture of OMsnd output (OpenAL Soft wave writer, cvar s_om_wavefile).

usage:
  analyze_wav.py <capture.wav> [--log sof.log] [--ref <piece.wav> ...] [--table]

Reports level statistics (RMS, peak, full-scale samples, silent share), checks that sound starts
logged by `s_om_debug 4` (`[t=<ms>] start <name> ...`) line up with level rises in the capture,
and with --ref finds each reference piece (e.g. decoded music) in the capture by normalised
cross-correlation (returns the best lag and coefficient).
"""
import re
import struct
import sys

import numpy as np


def read_wav(path):
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not a RIFF/WAVE file")
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        body = pos + 8
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", data, body)
        elif cid == b"data":
            end = body + size if 0 < size <= len(data) - body else len(data)   # unfinished header
            pcm = data[body:end]
            break
        pos = body + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    if bits != 16:
        raise SystemExit(f"{path}: only 16-bit PCM supported (got {bits})")
    x = np.frombuffer(pcm[: len(pcm) // (2 * ch) * 2 * ch], dtype="<i2").reshape(-1, ch)
    return rate, x


def db(v):
    return 20 * np.log10(max(v, 1e-9) / 32768.0)


def window_rms(mono, rate, ms):
    n = int(rate * ms / 1000)
    k = len(mono) // n
    w = mono[: k * n].reshape(k, n).astype(np.float64)
    return np.sqrt((w ** 2).mean(axis=1))


def events_from_log(path):
    ev = []
    for line in open(path, encoding="latin-1"):
        m = re.search(r"\[t=(\d+)\] start (\S+) ent (-?\d+) ch (\d+) vol ([\d.]+) attn ([\d.]+)", line)
        if m:
            ev.append((int(m.group(1)), m.group(2), int(m.group(3)), float(m.group(5)), float(m.group(6))))
    return ev


def xcorr_find(capture, ref):
    """Best normalised cross-correlation of `ref` inside `capture` (both mono float, same rate)."""
    n = len(capture) + len(ref)
    size = 1 << (n - 1).bit_length()
    num = np.fft.irfft(np.fft.rfft(capture, size) * np.conj(np.fft.rfft(ref, size)), size)[: len(capture) - len(ref) + 1]
    c2 = np.concatenate([[0.0], np.cumsum(capture.astype(np.float64) ** 2)])
    energy = c2[len(ref):] - c2[: len(capture) - len(ref) + 1]
    den = np.sqrt(np.maximum(energy, 1e-9) * float((ref.astype(np.float64) ** 2).sum()))
    r = num / den
    lag = int(np.argmax(r))
    return lag, float(r[lag]), float(np.median(np.abs(r)))


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    path = argv[0]
    log = argv[argv.index("--log") + 1] if "--log" in argv else None
    refs = [argv[i + 1] for i, a in enumerate(argv) if a == "--ref"]
    rate, x = read_wav(path)
    mono = x.astype(np.float64).mean(axis=1)
    dur = len(x) / rate
    peak = int(np.abs(x.astype(np.int32)).max()) if len(x) else 0
    full = int((np.abs(x.astype(np.int32)) >= 32767).sum())
    rms100 = window_rms(mono, rate, 100)
    silent = float((rms100 < 32768 * 10 ** (-60 / 20)).mean()) if len(rms100) else 1.0
    print(f"{path}: {rate} Hz, {x.shape[1]} ch, {dur:.2f} s")
    print(f"  overall RMS {db(np.sqrt((mono ** 2).mean())):.1f} dBFS, peak {peak} ({db(peak):.2f} dBFS), "
          f"full-scale samples {full} ({full / max(1, x.size) * 100:.4f} %), silent 100-ms windows {silent * 100:.1f} %")
    rms1s = window_rms(mono, rate, 1000)
    if "--table" in argv:
        print("  per-second RMS dBFS: " + " ".join(f"{db(v):.0f}" for v in rms1s))
    else:
        print(f"  per-second RMS dBFS: min {db(rms1s.min()):.1f} median {db(float(np.median(rms1s))):.1f} "
              f"max {db(rms1s.max()):.1f}")

    if log:
        ev = events_from_log(log)
        rises, checked = [], 0
        for t, name, ent, vol, attn in ev:
            s = int(t * rate / 1000)
            pre = mono[max(0, s - int(0.2 * rate)): s]
            post = mono[s: s + int(0.15 * rate)]
            if len(pre) < 100 or len(post) < 100:
                continue
            checked += 1
            r = db(np.sqrt((post ** 2).mean())) - db(np.sqrt((pre ** 2).mean()))
            rises.append((r, t, name, vol))
        print(f"  logged sound starts: {len(ev)}; inside the capture: {checked}")
        if rises:
            arr = np.array([r[0] for r in rises])
            print(f"  level change at starts (post 150 ms vs pre 200 ms): median {np.median(arr):+.1f} dB, "
                  f"> +3 dB for {int((arr > 3).sum())}/{len(arr)}")
            names = {}
            for r, t, name, vol in rises:
                names.setdefault(name, []).append(r)
            for name, rs in sorted(names.items(), key=lambda kv: -len(kv[1]))[:12]:
                print(f"    {name:48s} n={len(rs):3d} median {np.median(rs):+6.1f} dB")

    for ref in refs:
        rr, y = read_wav(ref)
        ym = y.astype(np.float64).mean(axis=1)
        cap = mono
        if rate == 2 * rr:          # 44.1 kHz capture vs 22.05 kHz reference: average pairs
            cap = mono[: len(mono) // 2 * 2].reshape(-1, 2).mean(axis=1)
        elif rate != rr:
            print(f"  {ref}: rate {rr} not comparable with {rate}")
            continue
        seg = ym[: min(len(ym), int(8 * rr))]          # first 8 s of the piece
        if len(seg) >= len(cap):
            seg = ym[: len(cap) // 2]
        lag, best, base = xcorr_find(cap, seg)
        print(f"  ref {ref}: best normalised correlation {best:.3f} at {lag / rr:.2f} s "
              f"(median |r| {base:.3f})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
