#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 5.2 (music piece boundary in a capture; black-box harness, no game code)
import os
# music piece boundary: relative gain in 1 ms windows around the end of a looping piece, modelled as a seamless loop
# (piece, then the piece again from 0). python boundary.py cap.wav piece.wav approx_start_s [stretch_ppm]
import sys
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav
from musictrack import load_piece
cap_path, piece_path, start = sys.argv[1], sys.argv[2], float(sys.argv[3])
ppm = float(sys.argv[4]) if len(sys.argv) > 4 else 0.0
r, cap = read_wav(cap_path)
if ppm:
    cap = signal.resample(cap, int(round(len(cap) * (1 + ppm * 1e-6))), axis=0)
pc = load_piece(piece_path, r)
L = len(pc)
x = cap[:, 0]
# refine the start on 4 s before the boundary
o = int(round(start * r))
s0 = o + L - 5 * r
seg = x[s0:s0 + 4 * r]
best = None
for d in range(-int(0.02 * r), int(0.02 * r) + 1):
    ref = pc[s0 - (o + d):s0 - (o + d) + 4 * r, 0]
    c = np.dot(seg, ref) / np.sqrt(np.dot(seg, seg) * np.dot(ref, ref))
    if best is None or c > best[0]:
        best = (c, d)
o += best[1]
loop = np.concatenate([pc[:, 0], pc[:, 0]])
g = np.dot(seg, loop[s0 - o:s0 - o + 4 * r]) / np.dot(loop[s0 - o:s0 - o + 4 * r], loop[s0 - o:s0 - o + 4 * r])
print(f'start {o / r:.4f} s (corr {best[0]:.4f}), gain {g:.4f}, boundary at {(o + L) / r:.4f} s')
h = int(0.001 * r)
row = []
for k in range(-20, 60):
    a = o + L + k * h
    xs = x[a:a + h]
    ys = loop[a - o:a - o + h] * g
    e = np.dot(ys, ys)
    row.append(np.dot(xs, ys) / e if e > 1e3 else float('nan'))
print('relative gain per 1 ms from -20 ms:')
print(' '.join(f'{v:.2f}' for v in row))
