#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 5 (timeline of a music piece in a capture; black-box harness, no game code)
import os
# local time shift of a capture against one occurrence of a piece, per segment (sub-sample, via 8x upsampled
# cross-correlation peak). python shiftprofile.py cap.wav piece.wav start_s t0 t1 [seg_s] [stretch_ppm]
import sys
import numpy as np
from scipy import signal
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav
from musictrack import load_piece
cap_path, piece_path, start, t0, t1 = sys.argv[1], sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
seg = float(sys.argv[6]) if len(sys.argv) > 6 else 2.0
ppm = float(sys.argv[7]) if len(sys.argv) > 7 else 0.0
r, cap = read_wav(cap_path)
if ppm:
    cap = signal.resample(cap, int(round(len(cap) * (1 + ppm * 1e-6))), axis=0)
pc = load_piece(piece_path, r)
o = int(round(start * r))
W = int(0.004 * r)  # search +-4 ms
t = t0
while t + seg <= t1:
    s, e = int(t * r), int((t + seg) * r)
    x = cap[s:e, 0]
    ref = pc[max(0, s - o - W):max(0, e - o + W), 0]
    if len(ref) < e - s + 2 * W or np.dot(x, x) < 1e3:
        t += seg
        continue
    c = signal.correlate(ref, x, mode='valid')  # lag k: x ~ ref[k:]
    k = int(np.argmax(c))
    # parabolic refinement
    if 0 < k < len(c) - 1:
        d = 0.5 * (c[k - 1] - c[k + 1]) / (c[k - 1] - 2 * c[k] + c[k + 1])
    else:
        d = 0.0
    shift_ms = (k + d - W) / r * 1000
    nc = c[k] / np.sqrt(np.dot(x, x) * np.dot(ref[k:k + len(x)], ref[k:k + len(x)]))
    print(f'{t:7.1f} s: shift {shift_ms:+8.4f} ms  corr {nc:.4f}')
    t += seg
