#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 1.3, SPEC-110 (11025 Hz references; black-box harness, no game code)
# references for the 11025 Hz mixer (SPEC-040 1.3: .adp and music take every 2nd sample at rate/R = 2; Quake II
# ResampleSfx with stepscale 2 also takes every 2nd sample): writes 11025 Hz 16-bit wavs into ref11k/
import os, struct, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_wav, read_adp
base = r'C:\openmerc-data'
out = os.path.join(base, 'i5s2', 'ref11k')
os.makedirs(out, exist_ok=True)


def write(path, x, rate=11025):
    x = np.clip(np.round(x), -32768, 32767).astype('<i2')
    ch = x.shape[1] if x.ndim == 2 else 1
    data = x.tobytes()
    hdr = b'RIFF' + struct.pack('<I', 36 + len(data)) + b'WAVE' + b'fmt ' + struct.pack('<IHHIIHH', 16, 1, ch, rate,
                                                                                         rate * 2 * ch, 2 * ch, 16)
    open(path, 'wb').write(hdr + b'data' + struct.pack('<I', len(data)) + data)


for name in ('JPN3/Tension', 'JPN3/Action'):
    r, x = read_wav(os.path.join(base, 'decoded', 'music', name + '.wav'))
    write(os.path.join(out, name.replace('/', '_') + '.wav'), x[::2])
r, x = read_adp(os.path.join(base, 'i5s2', 'extract', 'pak0', 'sound', 'cin', 'tsr1', 'Laugh.adp'))
write(os.path.join(out, 'Laugh.wav'), x[:len(x) - 38][::2])
r, x = read_wav(os.path.join(base, 'i5s2', 'extract', 'pak0', 'sound', 'player', 'Jump.wav'))
write(os.path.join(out, 'Jump.wav'), x[::2])
print(os.listdir(out))
