#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 (black-box harness, no game code)
# Levels of a sndhost capture: for every "act" line of <script>.log, the peak (L, R), RMS and onset
# latency of the capture in the window up to the next action; full-scale samples are counted as clipped.
# The wave writer starts writing at context creation, so sample 0 is aligned with the "OMsnd: writing" line
# (the log time of S_Init); onset latency = first sample above -40 dBFS after the action time.
# usage: hostlevels.py <capture.wav> <script.log>
# expectations: an action whose text contains "expect=local" must be heard as the listener's own sound: in its
# window both sides peak at >= LOCAL_MIN (default 29000: a 30935-peak sample at volume 255 non-spatialized gives
# 30814) and within 3 % of each other (no direction). "expect=silent" must stay under -40 dBFS, "expect=heard"
# above it. Failures are listed and the exit code is 1.
# onset: first sample above -40 dBFS from 300 ms before the action, relative to the action (the latency when the
# capture is aligned: proccap's <wav>.txt; for the wave writer only the differences between actions mean anything)
import re, sys, wave
import numpy as np
w = wave.open(sys.argv[1]); r = w.getframerate(); n = w.getnframes()
x = np.frombuffer(w.readframes(n), dtype='<i2').reshape(-1, w.getnchannels()).astype(np.float64)
log = open(sys.argv[2]).read().splitlines()
t0 = None; acts = []
for i, l in enumerate(log):
    m = re.match(r'act ([\d.]+) qpcms ([\d.]+) (.*)', l)
    if m: acts.append((float(m.group(2)), m.group(3)))
    if l.startswith('OMsnd: writing') and t0 is None:
        # S_Init runs right after the qpc0 line; the log has no time for it, take the loop start minus nothing
        pass
    m = re.match(r'loop_start_ms ([\d.]+)', l)
    if m: t0 = float(m.group(1))
off = 0.0  # ms, capture sample 0 relative to qpc0 (proccap writes it to <wav>.txt; wave writer: unknown, 0)
import os
q0 = re.search(r'qpc0_100ns (\d+)', '\n'.join(log))
if os.path.exists(sys.argv[1] + '.txt') and q0:
    off = (int(re.search(r'first_packet_qpc_100ns (-?\d+)', open(sys.argv[1] + '.txt').read()).group(1)) - int(q0.group(1))) / 1e4
    print(f'capture sample 0 at {off:.1f} ms after qpc0')
def longest_run(m):
    idx = np.flatnonzero(np.diff(np.concatenate([[0], m.astype(np.int8), [0]])))
    return int((idx[1::2] - idx[::2]).max()) if len(idx) else 0
groups = []
for t, a in acts:
    if groups and abs(groups[-1][0] - t) < 5: groups[-1][2] += 1; continue
    groups.append([t, a, 1])
fails = []
LOCAL_MIN = 29000
print(f'{sys.argv[1]}: {n/r:.2f} s @ {r} Hz, total peak {np.abs(x).max():.0f}, full-scale samples {(np.abs(x) >= 32767).sum()}, longest full-scale run {longest_run(np.abs(x).max(1) >= 32767)}')
for k, (t, a, c) in enumerate(groups):
    # the window ends 300 ms before the next action (its latency), or at the next action when that is closer than 600 ms
    s = int((t - off - 300) / 1000 * r)
    if k + 1 < len(groups):
        tn = groups[k+1][0]
        e = int((tn - off - (300 if tn - t >= 600 else 0)) / 1000 * r)
    else:
        e = n
    seg = x[max(s,0):e]
    if not len(seg): continue
    pk = np.abs(seg).max(0); rms = np.sqrt((seg**2).mean(0))
    on = np.nonzero(np.abs(seg).max(1) > 327)[0]
    lat = on[0] / r * 1000 - 300 if len(on) else float('nan')
    if s < 0: lat += s / r * 1000
    if 'expect=local' in a:
        ok = pk.min() >= LOCAL_MIN and abs(pk[0] - pk[1]) <= 0.03 * pk.max()
        if not ok: fails.append(f'{t:.0f} ms {a}: peaks {pk[0]:.0f}/{pk[1]:.0f}, want >= {LOCAL_MIN} and equal')
    if 'expect=heard' in a and not pk.max() > 327:
        fails.append(f'{t:.0f} ms {a}: peak {pk.max():.0f}, want > 327')
    if 'expect=silent' in a and not pk.max() <= 327:
        fails.append(f'{t:.0f} ms {a}: peak {pk.max():.0f}, want <= 327')
    print(f'{t:9.1f} x{c:<2} peakL {pk[0]:6.0f} peakR {pk[1]:6.0f} rmsL {rms[0]:6.0f} rmsR {rms[1]:6.0f} onset {lat:6.1f} ms  {a[:70]}')
n_exp = sum(('expect=' in a) for _, a, _ in groups)
for f in fails: print('FAIL', f)
print(f'expectations: {n_exp - len(fails)} of {n_exp} passed')
sys.exit(1 if fails else 0)
