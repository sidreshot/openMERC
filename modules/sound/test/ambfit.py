#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 2-4 (black-box measurement of ambient loops and one-shots in a capture; no game code)
"""Measure ambient-set loops and random one-shots in a capture.

usage: ambfit.py <capture.wav> --loop <wav> [--loop <wav> ...] --shot <wav> [--shot <wav> ...]
                 [--win 0.05] [--table 0.5] [--proxy <run.proxy.jsonl>] [--min-corr 0.6] [--from T] [--to T]
                 [--phase-seg 4]

Loops are mixed by the backend as autosound channels from position (sample time mod length) (SPEC-040 3.2), so in
the capture each loop is a periodic signal with a fixed phase. The phase of every loop is found per segment of
--phase-seg seconds (circular cross-correlation over one period); the median phase of the segments where the loop is
clearly present is used for the whole capture, and segments that disagree are reported (a jump of the output
timeline). One-shots are located by normalised cross-correlation of their first 0.3 s (peaks above --min-corr).
Then, per window, the gains of all loops and of the one-shots that overlap the window are fitted jointly by least
squares, per side (gain 1.0 = the wave's own sample values; Defsnd/OMsnd: volume * trunc(256 s_volume) / 65536 for
16-bit PCM). The output lists every one-shot (start, gain L/R, gap to the previous one) and a table of the loop
gains over time; with --proxy the relay's events are printed on the capture's time axis (as musictrack.py).
"""
import argparse
import json
import re
from math import gcd

import numpy as np
from scipy import signal

from soundfit import read_wav


def resample(x, src, dst):
    if src == dst:
        return x.astype(np.float64)
    g = gcd(src, dst)
    return signal.resample_poly(x, dst // g, src // g)


def mono(path, rate):
    r, x = read_wav(path)
    return r, resample(x[:, 0], r, rate)


def periodic(path, rate, total):
    """the loop tiled and resampled to the capture rate: T[k] = loop at input time k * r_in / rate (periodic)"""
    r, x = read_wav(path)
    loop = x[:, 0]
    reps = int(np.ceil((total * r / rate) / len(loop))) + 3
    t = resample(np.tile(loop, reps), r, rate)
    period = len(loop) * rate / r
    return t, period


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("--loop", action="append", default=[])
    ap.add_argument("--shot", action="append", default=[])
    ap.add_argument("--win", type=float, default=0.05)
    ap.add_argument("--table", type=float, default=0.5)
    ap.add_argument("--proxy")
    ap.add_argument("--min-corr", type=float, default=0.6)
    ap.add_argument("--from", dest="t0", type=float, default=0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    ap.add_argument("--phase-seg", type=float, default=4.0)
    ap.add_argument("--loop-corr", type=float, default=0.5)
    ap.add_argument("--stretch", type=float, default=0.0, help="stretch the capture by PPM first (OMsnd: 13.3)")
    ap.add_argument("-v", action="store_true")
    a = ap.parse_args()

    rate, cap = read_wav(a.capture)
    if a.stretch:
        cap = signal.resample(cap, int(round(len(cap) * (1 + a.stretch * 1e-6))), axis=0)
    n = len(cap)
    L = cap[:, 0] + cap[:, 1]
    name = lambda p: re.sub(r".*[\\/]", "", p)
    print(f"# {a.capture}: {n / rate:.1f} s at {rate} Hz")

    # ---- loops: phase per segment
    loops = []
    for path in a.loop:
        T, period = periodic(path, rate, n + 2 * rate)
        P = int(round(period))
        seg = int(a.phase_seg * rate)
        phases = []
        for s in range(0, n - seg, seg):
            S = L[s:s + seg]
            es = float(np.dot(S, S))
            if es < 1e3:
                continue
            ref = T[: seg + P]
            c = signal.fftconvolve(ref, S[::-1], mode="valid")[:P]
            win = np.concatenate([[0], np.cumsum(ref ** 2)])
            er = win[seg:seg + P] - win[:P]
            nc = c / np.sqrt(np.maximum(er * es, 1e-9))
            k = int(np.argmax(nc))
            # lag k: S[i] ~ T[i + k], so capture index c maps to T[c + (k - s)] (taken modulo the true period)
            phases.append((s / rate, float(nc[k]), (k - s) % period))
        if a.v:
            print("#   segments (t, corr, phase):", [(round(t, 1), round(c, 3), round(p)) for t, c, p in phases])
        good = [p for p in phases if p[1] > a.loop_corr]
        if good:
            ref = max(good, key=lambda p: p[1])[2]
            d = lambda p: (p - ref + period / 2) % period - period / 2
            ph = int(round((ref + float(np.median([d(p[2]) for p in good]))) % period))
            off = [p for p in good if abs(d(p[2]) - (ph - ref)) > rate * 0.002]
        else:
            ph, off = None, []
        print(f"# loop {name(path)}: period {period / rate:.4f} s, phase found in {len(good)} of {len(phases)} "
              f"segments (corr > 0.5)" + (f", phase {ph}" if ph is not None else ""))
        for t, c, p in off:
            print(f"#   segment at {t:.1f} s: phase differs by {((p - ph + period / 2) % period - period / 2) / rate * 1000:.1f} ms "
                  f"(corr {c:.2f})")
        if ph is not None:
            loops.append((name(path), T, ph, P))

    # ---- one-shots, searched in the residual after the loops (fitted per window) are removed
    hop0 = int(rate * a.win)
    R = L.copy()
    if loops:
        for w in range(n // hop0):
            s0, e0 = w * hop0, (w + 1) * hop0
            A = np.stack([T[s0 + ph:e0 + ph] for _, T, ph, _ in loops], axis=1)
            g, *_ = np.linalg.lstsq(A, L[s0:e0], rcond=None)
            R[s0:e0] = L[s0:e0] - A @ g
    shots = []
    occ = []
    for j, path in enumerate(a.shot):
        _, x = mono(path, rate)
        shots.append((name(path), x))
        probe = x[: int(0.3 * rate)]
        ep = float(np.dot(probe, probe))
        c = signal.fftconvolve(R, probe[::-1], mode="valid")
        win = np.concatenate([[0], np.cumsum(R ** 2)])
        e = win[len(probe):] - win[:-len(probe)]
        nc = c / np.sqrt(np.maximum(e * ep, 1e-9))
        peaks, _ = signal.find_peaks(nc, height=a.min_corr, distance=int(0.2 * rate))
        for k in peaks:
            occ.append((int(k), j, float(nc[k])))
    occ.sort()

    # ---- joint fit per window
    hop = int(rate * a.win)
    nwin = n // hop
    lg = np.zeros((len(loops), nwin, 2))
    og = np.zeros((len(occ), 2))
    ocount = np.zeros(len(occ))
    for w in range(nwin):
        s, e = w * hop, (w + 1) * hop
        cols, ids = [], []
        for i, (_, T, ph, P) in enumerate(loops):
            cols.append(T[s + ph:e + ph])  # T is exactly periodic in input time and longer than the capture
            ids.append(("L", i))
        for k, (o, j, _) in enumerate(occ):
            x = shots[j][1]
            if o < e and o + len(x) > s:
                seg = np.zeros(hop)
                ps, pe = max(s, o), min(e, o + len(x))
                seg[ps - s:pe - s] = x[ps - o:pe - o]
                cols.append(seg)
                ids.append(("S", k))
        if not cols:
            continue
        A = np.stack(cols, axis=1)
        for side in (0, 1):
            g, *_ = np.linalg.lstsq(A, cap[s:e, side], rcond=None)
            for (kind, i), v in zip(ids, g):
                if kind == "L":
                    lg[i, w, side] = v
                elif s >= occ[i][0] and e <= occ[i][0] + int(0.5 * rate):
                    og[i, side] += v
        for (kind, i) in ids:
            if kind == "S" and s >= occ[i][0] and e <= occ[i][0] + int(0.5 * rate):
                ocount[i] += 1

    if a.proxy:
        txt = open(a.capture + ".txt").read()
        q0 = int(re.search(r"first_packet_qpc_100ns (-?\d+)", txt).group(1)) * 1e-7
        for line in open(a.proxy):
            r = json.loads(line)
            if r.get("event") in ("values", "sound_info", "stufftext", "first_frame") and "qpc" in r:
                print(f"# event at {r['qpc'] - q0:8.3f} s: {r['event']} "
                      f"{ {k: v for k, v in r.items() if k not in ('wall', 'qpc', 't_relay', 'event', 'seq')} }")

    print("# one-shots: start_s  wave  corr  gain_L gain_R (mean over the windows it covers, first 0.5 s)  gap_s")
    last = None
    for k, (o, j, c) in enumerate(occ):
        t = o / rate
        if t < a.t0 or t > a.t1:
            continue
        gap = "" if last is None else f"{t - last:7.3f}"
        last = t
        print(f"  {t:9.3f}  {shots[j][0]:<14} {c:5.2f}  {og[k, 0] / max(ocount[k], 1):7.4f} "
              f"{og[k, 1] / max(ocount[k], 1):7.4f}  {gap}")

    step = max(1, int(round(a.table / a.win)))
    print("#   time_s  " + "  ".join(f"{nm[:16]:>16} L/R     " for nm, *_ in loops))
    for w in range(0, nwin, step):
        t = w * a.win
        if t < a.t0 or t > a.t1:
            continue
        print(f"{t:9.2f}  " + "  ".join(f"{lg[i, w, 0]:8.4f} {lg[i, w, 1]:8.4f}" for i in range(len(loops))))


if __name__ == "__main__":
    main()
