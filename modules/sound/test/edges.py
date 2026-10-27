#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 1.3 (start/end of sounds in a capture; black-box harness, no game code)
import os
# gains at the start and end of each occurrence of a sound, in 2 ms windows (shows a cut beginning or end)
# python edges.py <capture.wav> <ref.wav|.adp> [--trim N] [--stretch PPM] [--ms 30]
import sys, argparse
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav, read_adp
ap = argparse.ArgumentParser()
ap.add_argument('capture'); ap.add_argument('ref')
ap.add_argument('--trim', type=int, default=0)
ap.add_argument('--stretch', type=float, default=0.0)
ap.add_argument('--ms', type=float, default=30)
ap.add_argument('--min-corr', type=float, default=0.8)
a = ap.parse_args()
r, cap = read_wav(a.capture)
if a.stretch:
    cap = signal.resample(cap, int(round(len(cap) * (1 + a.stretch * 1e-6))), axis=0)
if a.ref.lower().endswith('.adp'):
    rr, ref = read_adp(a.ref)
else:
    rr, ref = read_wav(a.ref)
ref = ref[:, 0]
if a.trim:
    ref = ref[:len(ref) - a.trim]
refc = signal.resample_poly(ref, 320, 147) if (r, rr) == (48000, 22050) else ref
mono = cap.mean(axis=1)
c = signal.fftconvolve(mono, refc[::-1], mode='valid')
win = np.concatenate([[0], np.cumsum(mono ** 2)])
e = win[len(refc):] - win[:-len(refc)]
nc = c / np.sqrt(np.maximum(e * np.dot(refc, refc), 1e-9))
peaks, _ = signal.find_peaks(nc, height=a.min_corr, distance=r)
h = int(0.002 * r)
n = int(a.ms / 2)
for k in peaks:
    g = np.dot(mono[k:k + len(refc)], refc) / np.dot(refc, refc)
    def wg(s):
        x = mono[k + s:k + s + h]
        y = refc[s:s + h]
        ey = np.dot(y, y)
        return np.dot(x, y) / ey / g if ey > 1e3 else float('nan')
    head = [wg(i * h) for i in range(n)]
    tail = [wg(len(refc) - (n - i) * h) for i in range(n)]
    print(f'occurrence {k / r:.3f} s gain {g:.4f}')
    print('  start (relative gain per 2 ms):', ' '.join(f'{v:.2f}' for v in head))
    print('  end   (relative gain per 2 ms):', ' '.join(f'{v:.2f}' for v in tail))
