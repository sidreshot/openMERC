#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 (black-box comparison of two backends' captures of the same demo; no game code)
"""Align two captures of the same session (e.g. Defsnd and OMsnd playing the same demo) and compare levels.

usage: compare.py <a.wav> <b.wav> [--win 0.1] [--table 1.0] [--from S] [--to S] [--png out.png]

1. The offset of b relative to a is found by cross-correlating the 1 kHz-band envelope (coarse, whole file)
   and then the waveform around the loudest part (fine, to the sample).
2. Per window: RMS of each side of a and b, and the level difference b - a in dB.
3. Reports the median and spread of the difference over windows where both are above -60 dBFS, the
   waveform correlation after alignment, and a table at --table seconds.
"""
import argparse

import numpy as np
from scipy import signal

from soundfit import read_wav


def envelope(x, rate, hop):
    n = len(x) // hop
    return np.sqrt((x[: n * hop].reshape(n, hop) ** 2).mean(axis=1) + 1e-9)


def align(a, b, rate):
    am, bm = a.mean(axis=1), b.mean(axis=1)
    hop = rate // 200  # 5 ms
    ea, eb = np.log(envelope(am, rate, hop)), np.log(envelope(bm, rate, hop))
    ea -= ea.mean()
    eb -= eb.mean()
    c = signal.fftconvolve(ea, eb[::-1], mode="full")
    lag = (np.argmax(c) - (len(eb) - 1)) * hop  # a[t] ~ b[t - lag]
    # fine: waveform correlation in a 4 s window around a's loudest second
    w = rate * 4
    s = int(np.argmax(envelope(am, rate, rate)) * rate)
    s = max(w, min(s, len(am) - 2 * w))
    seg_a = am[s:s + w]
    lo = max(0, s - lag - hop * 4)
    seg_b = bm[lo:s - lag + w + hop * 4]
    if len(seg_b) > len(seg_a):
        cc = signal.fftconvolve(seg_b, seg_a[::-1], mode="valid")
        k = int(np.argmax(cc))
        lag = s - (lo + k)
    return lag


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--win", type=float, default=0.1)
    ap.add_argument("--table", type=float, default=2.0)
    ap.add_argument("--lag", type=int, help="use this lag (samples) instead of searching")
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    a_ = ap.parse_args()
    ra, a = read_wav(a_.a)
    rb, b = read_wav(a_.b)
    assert ra == rb
    rate = ra
    lag = a_.lag if a_.lag is not None else align(a, b, rate)
    # b shifted so that b2[t] = b[t - lag]
    if lag >= 0:
        b2 = np.concatenate([np.zeros((lag, 2)), b])
    else:
        b2 = b[-lag:]
    n = min(len(a), len(b2))
    a, b2 = a[:n], b2[:n]
    i0, i1 = int(a_.t0 * rate), min(n, int(a_.t1 * rate))
    a, b2 = a[i0:i1], b2[i0:i1]
    corr = float(np.sum(a * b2) / np.sqrt(np.sum(a * a) * np.sum(b2 * b2) + 1e-9))
    hop = int(rate * a_.win)
    k = len(a) // hop
    A = np.sqrt((a[: k * hop].reshape(k, hop, 2) ** 2).mean(axis=1) + 1e-9)
    B = np.sqrt((b2[: k * hop].reshape(k, hop, 2) ** 2).mean(axis=1) + 1e-9)
    floor = 32768 * 10 ** (-60 / 20)
    ok = (A > floor) & (B > floor)
    d = 20 * np.log10(B / A)
    print(f"# a={a_.a}\n# b={a_.b}\n# lag {lag} samples ({lag / rate * 1000:.1f} ms): b is played that much "
          f"{'later' if lag < 0 else 'earlier'}; compared {a_.t0:.1f}..{min(a_.t1, n / rate):.1f} s")
    print(f"# waveform correlation after alignment: {corr:.3f}")
    for side, nm in ((0, "L"), (1, "R")):
        dd = d[ok[:, side], side]
        if len(dd):
            print(f"# {nm}: level b-a over {len(dd)} windows of {a_.win * 1000:.0f} ms: median {np.median(dd):+.2f} dB, "
                  f"p10 {np.percentile(dd, 10):+.2f}, p90 {np.percentile(dd, 90):+.2f}")
    print(f"# overall RMS a {20 * np.log10(np.sqrt((a ** 2).mean()) / 32768):.2f} dBFS, "
          f"b {20 * np.log10(np.sqrt((b2 ** 2).mean()) / 32768):.2f} dBFS")
    step = max(1, int(round(a_.table / a_.win)))
    print("#   time_s   a_L_dB  a_R_dB   b_L_dB  b_R_dB   dL     dR")
    for j in range(0, k - step + 1, step):
        sa = np.sqrt((A[j:j + step] ** 2).mean(axis=0))
        sb = np.sqrt((B[j:j + step] ** 2).mean(axis=0))
        da = 20 * np.log10(sa / 32768)
        db_ = 20 * np.log10(sb / 32768)
        print(f"{a_.t0 + j * a_.win:9.1f}  {da[0]:7.1f} {da[1]:7.1f}  {db_[0]:7.1f} {db_[1]:7.1f}  "
              f"{db_[0] - da[0]:+5.1f}  {db_[1] - da[1]:+5.1f}")


if __name__ == "__main__":
    main()
