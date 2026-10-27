#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 1 (output latency of the mixer test tone; black-box harness, no game code)
# latency of the mixer test tone: time from the relay's stufftext "set s_testsound 1" (and "... 0") to the tone's
# onset (and end) in the capture. python tonelat.py <run tag> [runs dir]
import json, re, sys, os
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav
tag = sys.argv[1]
d = sys.argv[2] if len(sys.argv) > 2 else r'C:\openmerc-data\i5s2\runs'
wav = os.path.join(d, tag + '.wav')
r, x = read_wav(wav)
q0 = int(re.search(r'first_packet_qpc_100ns (-?\d+)', open(wav + '.txt').read()).group(1)) * 1e-7
ev = {}
for line in open(os.path.join(d, tag + '.proxy.jsonl')):
    e = json.loads(line)
    if e.get('event') == 'stufftext' and 's_testsound' in e.get('values', ''):
        ev[e['values']] = e['qpc'] - q0
f = 22050 * 0.1 / (2 * np.pi)
hop = int(r * 0.001)
L = x[:, 0]
n = len(L) // hop
amp = np.zeros(n)
for k in range(n):
    seg = L[k * hop:(k + 1) * hop]
    tt = (k * hop + np.arange(hop)) / r
    A = np.stack([np.sin(2 * np.pi * f * tt), np.cos(2 * np.pi * f * tt)], 1)
    c, *_ = np.linalg.lstsq(A, seg, rcond=None)
    amp[k] = np.hypot(*c)
on = np.where(amp > 10000)[0]
t_on, t_off = on[0] * 0.001, (on[-1] + 1) * 0.001
print(f'{tag}: tone {t_on:.3f}..{t_off:.3f} s; events', {k: round(v, 3) for k, v in ev.items()})
for k, v in ev.items():
    t = t_on if k.endswith('1') else t_off
    print(f'  {k!r}: event {v:.3f} s -> tone change {t:.3f} s, latency {1000 * (t - v):.0f} ms')
