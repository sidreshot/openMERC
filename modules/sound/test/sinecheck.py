#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 1 (output continuity with the test tone; black-box harness, no game code)
import os
# continuity of the test tone in a capture: per 5 ms window fit A*sin + B*cos at f, report amplitude dips / phase jumps
import sys, numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav
r, x = read_wav(sys.argv[1]); f = 22050 * 0.1 / (2 * np.pi)
L = x[:, 0]
hop = int(r * 0.005)
n = len(L) // hop
t = np.arange(hop) / r
amps, phs = [], []
for k in range(n):
    seg = L[k * hop:(k + 1) * hop]
    tt = (k * hop + np.arange(hop)) / r
    A = np.stack([np.sin(2 * np.pi * f * tt), np.cos(2 * np.pi * f * tt)], 1)
    c, *_ = np.linalg.lstsq(A, seg, rcond=None)
    amps.append(np.hypot(*c)); phs.append(np.arctan2(c[1], c[0]))
amps = np.array(amps); phs = np.unwrap(np.array(phs))
on = amps > 0.5 * np.median(amps[amps > 1000]) if (amps > 1000).any() else amps > 1e9
idx = np.where(on)[0]
print(sys.argv[1], 'tone windows', len(idx), 'median amplitude', round(float(np.median(amps[idx])), 1) if len(idx) else 0)
if len(idx):
    a = amps[idx[0]:idx[-1]]
    p = np.diff(phs[idx[0]:idx[-1]])
    drift = np.median(p)
    jumps = np.where(np.abs(p - drift) > 0.2)[0]
    dips = np.where(a < 0.9 * np.median(a))[0]
    print('  amplitude dips (<90%):', len(dips), [round((idx[0] + d) * 0.005, 3) for d in dips[:15]])
    print('  phase jumps (>0.2 rad):', len(jumps), [round((idx[0] + j) * 0.005, 3) for j in jumps[:15]])
