#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 1.3, SPEC-110 (.adp playable length in a capture; black-box harness, no game code)
import os
# .adp trailer check (SPEC-040 1.3, test vector 3): which decoded samples at the end of an .adp sound reach the output.
# python trailer.py <capture.wav> <file.adp> [--approx T] : locate each occurrence, fit the gain on the body, then
# for candidate playable lengths N (2n-38 .. 2n) report the residual energy of the last 120 source samples when the
# reference is cut at N. The best N is the playable length the backend used.
import sys, argparse
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav, read_adp
ap = argparse.ArgumentParser()
ap.add_argument('capture'); ap.add_argument('adp')
ap.add_argument('--min-corr', type=float, default=0.8)
ap.add_argument('--stretch', type=float, default=0.0)
a = ap.parse_args()
r, cap = read_wav(a.capture)
if a.stretch:
    cap = signal.resample(cap, int(round(len(cap) * (1 + a.stretch * 1e-6))), axis=0)
ar, ref = read_adp(a.adp)
ref = ref[:, 0]
nfull = len(ref)                 # 2n decoded samples
print(f'{a.adp}: {nfull} decoded samples, header rate {ar}; tail (last 45):', ref[-45:].astype(int).tolist())
up = lambda x: signal.resample_poly(x, 320, 147) if r == 48000 else x
mono = cap.mean(axis=1)
body = up(ref[: nfull - 200])
c = signal.fftconvolve(mono, body[::-1], mode='valid')
win = np.concatenate([[0], np.cumsum(mono ** 2)])
e = win[len(body):] - win[:-len(body)]
nc = c / np.sqrt(np.maximum(e * np.dot(body, body), 1e-9))
peaks, _ = signal.find_peaks(nc, height=a.min_corr, distance=r)
for k in peaks:
    seg = mono[k:k + len(body)]
    g = np.dot(seg, body) / np.dot(body, body)
    print(f'occurrence at {k / r:.3f} s corr {nc[k]:.3f} gain {g:.4f}')
    res = []
    for N in range(nfull - 45, nfull + 1):
        cut = np.concatenate([ref[:N], np.zeros(400)])
        model = up(cut)
        s0 = int((nfull - 160) * r / ar)
        s1 = int((nfull + 100) * r / ar)
        d = mono[k + s0:k + s1] - g * model[s0:s1]
        res.append((float(np.dot(d, d)), N))
    best = min(res)
    print('  residual energy by playable length N (2n = %d):' % nfull)
    for en, N in res[::3]:
        print(f'    N = 2n{N - nfull:+d}: {en:.3e}' + ('  <- best' if N == best[1] else ''))
    print(f'  best N = 2n{best[1] - nfull:+d}')
