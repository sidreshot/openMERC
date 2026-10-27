#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 (black-box verification of a sound backend's output; no game code)
"""Find every occurrence of a known sound in a capture and measure its gain per side.

usage: soundfit.py <capture.wav> <reference.wav|.adp> [--min-corr 0.9] [--max 50] [--trim N]

The reference (a stock `.wav`, or an `.adp` decoded with SPEC-110 and trimmed by --trim samples, default 0)
is resampled to the capture rate. Each occurrence is located by normalised cross-correlation of the mono
sum, refined to the sample, and the least-squares gain of the reference in the left and right channel is
reported (gain 1.0 = the reference's own sample values). Used to compare Defsnd and OMsnd levels: both
captures go through the same recorder, so their gains are directly comparable.
"""
import argparse
import struct
import sys

import numpy as np
from scipy import signal


def read_wav(path):
    data = open(path, "rb").read()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise SystemExit(f"{path}: not RIFF/WAVE")
    pos, fmt, pcm = 12, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack_from("<I", data, pos + 4)[0]
        body = pos + 8
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", data, body)
        elif cid == b"data":
            end = body + size if 0 < size <= len(data) - body else len(data)
            pcm = data[body:end]
            break
        pos = body + size + (size & 1)
    tag, ch, rate, _, _, bits = fmt
    if bits == 16:
        x = np.frombuffer(pcm[: len(pcm) // (2 * ch) * 2 * ch], dtype="<i2").astype(np.float64)
    elif bits == 8:
        x = (np.frombuffer(pcm[: len(pcm) // ch * ch], dtype="u1").astype(np.float64) - 128) * 256
    else:
        raise SystemExit(f"{path}: {bits}-bit not supported")
    return rate, x.reshape(-1, ch)


STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
        97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
        724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
        4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
        18500, 20350, 22385, 24623, 27086, 29794, 32767]
IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def read_adp(path):
    data = open(path, "rb").read()
    rate = struct.unpack_from("<I", data, 0)[0]
    p, i, out = 0, 0, []
    for b in data[4:]:
        for c in (b >> 4, b & 15):
            st = STEP[i]
            d = st >> 3
            if c & 4: d += st
            if c & 2: d += st >> 1
            if c & 1: d += st >> 2
            p = p - d if c & 8 else p + d
            p = max(-32768, min(32767, p))
            i = max(0, min(88, i + IDX[c & 7]))
            out.append(p)
    return rate, np.array(out, dtype=np.float64).reshape(-1, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("reference")
    ap.add_argument("--min-corr", type=float, default=0.9)
    ap.add_argument("--max", type=int, default=50)
    ap.add_argument("--trim", type=int, default=0)
    a = ap.parse_args()
    crate, cap = read_wav(a.capture)
    if a.reference.lower().endswith(".adp"):
        rrate, ref = read_adp(a.reference)
        if a.trim:
            ref = ref[: len(ref) - a.trim]
    else:
        rrate, ref = read_wav(a.reference)
    ref = ref[:, 0]
    n = int(round(len(ref) * crate / rrate))
    # nearest-sample resampling as the Quake II mixer does it, then the capture path's own resampler
    # cannot be undone, so use a band-limited resampler for the reference
    refc = signal.resample_poly(ref, crate, rrate) if crate != rrate else ref
    mono = cap.mean(axis=1)
    corr = signal.fftconvolve(mono, refc[::-1], mode="valid")
    e_ref = float(np.dot(refc, refc))
    win = np.concatenate([[0], np.cumsum(mono ** 2)])
    e_cap = win[len(refc):] - win[:-len(refc)]
    ncc = corr / np.sqrt(np.maximum(e_cap * e_ref, 1e-9))
    found = []
    order = np.argsort(-ncc)
    taken = np.zeros(len(ncc), dtype=bool)
    for k in order:
        if ncc[k] < a.min_corr or len(found) >= a.max:
            break
        if taken[max(0, k - len(refc) // 2): k + len(refc) // 2].any():
            continue
        taken[k] = True
        seg = cap[k:k + len(refc)]
        gl = float(np.dot(seg[:, 0], refc) / e_ref)
        gr = float(np.dot(seg[:, 1], refc) / e_ref)
        res = seg - np.outer(refc, [gl, gr])
        snr = 10 * np.log10(max(np.sum(seg ** 2), 1e-9) / max(np.sum(res ** 2), 1e-9))
        found.append((k, ncc[k], gl, gr, snr))
    found.sort()
    print(f"# {a.capture}: {len(cap) / crate:.2f} s at {crate} Hz; reference {a.reference} {len(ref)} samples at {rrate} Hz")
    print("#   time_s   corr   gain_L   gain_R  fit_dB")
    for k, c, gl, gr, snr in found:
        print(f"{k / crate:9.3f}  {c:.3f}  {gl:7.4f}  {gr:7.4f}  {snr:6.1f}")
    if not found:
        print("# not found")


if __name__ == "__main__":
    main()
