#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 5 (black-box measurement of dynamic music in a capture; no game code)
"""Level and position of known music pieces in a capture, window by window.

usage: musicwin.py <capture.wav> <piece.wav> [<piece.wav> ...] [--win 0.5] [--stretch PPM] [--min-corr 0.3]
                   [--proxy <run.proxy.jsonl>] [--from T] [--to T]

Each window of the capture is located independently in every piece: a coarse search over the whole piece at
8 kHz (normalised cross-correlation), then a search of +-3 coarse samples at the capture rate with parabolic
interpolation and a fractional-delay copy of the piece. The gains of all pieces that match (correlation above
--min-corr) are then fitted jointly by least squares, per side (gain 1.0 = the piece's own sample values;
Defsnd/OMsnd music gain = trunc(level * s_musicvolume) / 256). Because no fixed start is assumed, the measure is
not disturbed by pauses (a track at level 0 does not advance), restarts, loops or self-similar music; the reported
position (seconds into the piece) shows where the piece is.

--stretch: OpenAL Soft plays a 22050 Hz stream on a 48 kHz device with the fixed-point step 30106/65536, 13.3 ppm
fast; for OMsnd loopback captures pass --stretch 13.3 so that a 0.5 s window does not slide against the piece.
"""
import argparse
import json
import re
from math import gcd

import numpy as np
from scipy import signal

from soundfit import read_wav

RA = 8000


def rs(x, src, dst):
    if src == dst:
        return x
    g = gcd(int(src), int(dst))
    return signal.resample_poly(x, dst // g, src // g, axis=0)


def frac_delay_seg(x, d, taps=48):
    n = np.arange(-taps, taps + 1)
    h = np.sinc(n - d) * np.blackman(len(n))
    h /= h.sum()
    return np.stack([np.convolve(x[:, c], h, mode="same") for c in range(x.shape[1])], axis=1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("pieces", nargs="+")
    ap.add_argument("--win", type=float, default=0.5)
    ap.add_argument("--stretch", type=float, default=0.0)
    ap.add_argument("--min-corr", type=float, default=0.3)
    ap.add_argument("--proxy")
    ap.add_argument("--from", dest="t0", type=float, default=0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    a = ap.parse_args()

    rate, cap = read_wav(a.capture)
    if a.stretch:
        cap = signal.resample(cap, int(round(len(cap) * (1 + a.stretch * 1e-6))), axis=0)
    names = [re.sub(r".*[\\/]music[\\/]", "", p) for p in a.pieces]
    pieces, coarse, cen = [], [], []
    for p in a.pieces:
        r, x = read_wav(p)
        if x.shape[1] == 1:
            x = np.repeat(x, 2, axis=1)
        pieces.append(rs(x, r, rate))
        c = rs(x.mean(axis=1), r, RA)
        coarse.append(c)
        cen.append(np.concatenate([[0], np.cumsum(c ** 2)]))
    cap8 = rs(cap.mean(axis=1), rate, RA)
    W = int(a.win * rate)
    W8 = int(a.win * RA)
    scale = rate / RA
    print(f"# {a.capture}: {len(cap) / rate:.1f} s at {rate} Hz" + (f", stretched {a.stretch} ppm" if a.stretch else ""))

    if a.proxy:
        txt = open(a.capture + ".txt").read()
        q0 = int(re.search(r"first_packet_qpc_100ns (-?\d+)", txt).group(1)) * 1e-7
        for line in open(a.proxy):
            r_ = json.loads(line)
            if r_.get("event") in ("values", "sound_info", "stufftext", "first_frame", "sound") and "qpc" in r_:
                print(f"# event at {r_['qpc'] - q0:8.3f} s: {r_['event']} "
                      f"{ {k: v for k, v in r_.items() if k not in ('wall', 'qpc', 't_relay', 'event', 'seq')} }")

    print("#   time_s  " + "  ".join(f"{n[:18]:>18}: gain L  gain R   pos_s " for n in names))
    for w in range(0, len(cap) // W):
        t = w * a.win
        if t < a.t0 or t > a.t1:
            continue
        s = w * W
        x8 = cap8[w * W8:(w + 1) * W8]
        ex = float(np.dot(x8, x8))
        if ex < 1e3 * W8:  # silence (RMS below ~32)
            print(f"{t:9.2f}  " + "  ".join(f"{0:8.4f} {0:8.4f} {'-':>7}" for _ in names))
            continue
        cols, owners = [], []
        info = {}
        for i, (pc8, en) in enumerate(zip(coarse, cen)):
            if len(pc8) < W8:
                continue
            c = signal.oaconvolve(pc8, x8[::-1], mode="valid")
            ep = en[W8:] - en[:-W8]
            nc = c / np.sqrt(np.maximum(ep * ex, 1e-9))
            k8 = int(np.argmax(nc))
            if nc[k8] < a.min_corr:
                continue
            # refine at the capture rate around k8 * scale
            k0 = int(round(k8 * scale))
            D = int(3 * scale) + 2
            lo = max(0, k0 - D)
            hi = min(len(pieces[i]) - W, k0 + D)
            if hi <= lo:
                continue
            xs = cap[s:s + W].mean(axis=1)
            ref = pieces[i][lo:hi + W].mean(axis=1)
            cc = signal.correlate(ref, xs, mode="valid")
            k = int(np.argmax(cc))
            d = 0.0
            if 0 < k < len(cc) - 1:
                den = cc[k - 1] - 2 * cc[k] + cc[k + 1]
                if den:
                    d = 0.5 * (cc[k - 1] - cc[k + 1]) / den
            pos = lo + k + d  # capture window start ~ piece position pos
            ip = int(np.floor(pos))
            fr = pos - ip
            T = 64
            a0, a1 = ip - T, ip + W + T + 1
            seg = np.zeros((a1 - a0, 2))
            ss, se = max(0, a0), min(len(pieces[i]), a1)
            seg[ss - a0:se - a0] = pieces[i][ss:se]
            # capture[m] ~ piece[pos + m]: advance the piece by fr samples
            seg = frac_delay_seg(seg, -fr)
            cols.append(seg[T:T + W])
            owners.append(i)
            info[i] = (pos / rate, float(nc[k8]))
        g = np.zeros((len(names), 2))
        if cols:
            for side in (0, 1):
                A = np.stack([c[:, side] for c in cols], axis=1)
                sol, *_ = np.linalg.lstsq(A, cap[s:s + W, side], rcond=None)
                for i, v in zip(owners, sol):
                    g[i, side] = v
        print(f"{t:9.2f}  " + "  ".join(
            f"{g[i, 0]:8.4f} {g[i, 1]:8.4f} {info[i][0]:7.2f}" if i in info else f"{0:8.4f} {0:8.4f} {'-':>7}"
            for i in range(len(names))))


if __name__ == "__main__":
    main()
