#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 1.1, 1.3, SPEC-AMEND-006 I5s-2.1 (start delay of relay-started sounds; black-box harness,
#             no game code)
"""Delay from each svc_sound the relay injected to the start of that sound in the capture.

usage: onsets.py <run tag> <slot>=<reference.wav|.adp> [...] [--runs DIR] [--trim N]

For every relay event `sound` (injproxy.py --sound) whose slot has a reference, the occurrence of the reference
nearest after the event (normalised cross-correlation, refined to the sample) is reported with its delay and gain.
An `.adp` reference is decoded (SPEC-110) and shortened by --trim samples (default 38).
"""
import argparse
import json
import os
import re
import sys

import numpy as np
from scipy import signal

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from soundfit import read_adp, read_wav  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("refs", nargs="+")
    ap.add_argument("--runs", default=r"C:\openmerc-data\i5s2\runs")
    ap.add_argument("--trim", type=int, default=38)
    a = ap.parse_args()
    wav = os.path.join(a.runs, a.tag + ".wav")
    rate, cap = read_wav(wav)
    mono = cap.mean(axis=1)
    q0 = int(re.search(r"first_packet_qpc_100ns (-?\d+)", open(wav + ".txt").read()).group(1)) * 1e-7
    refs = {}
    for r in a.refs:
        slot, path = r.split("=", 1)
        if path.lower().endswith(".adp"):
            rr, x = read_adp(path)
            x = x[: len(x) - a.trim, 0]
        else:
            rr, x = read_wav(path)
            x = x[:, 0]
        if rr != rate:
            x = signal.resample_poly(x, 320, 147) if (rr, rate) == (22050, 48000) else signal.resample(
                x, int(len(x) * rate / rr))
        refs[int(slot)] = x
    print(f"# {a.tag}: delay from svc_sound to the sound's first sample")
    for line in open(os.path.join(a.runs, a.tag + ".proxy.jsonl")):
        e = json.loads(line)
        if e.get("event") != "sound":
            continue
        slot = e["values"][0]
        if slot not in refs:
            continue
        x = refs[slot]
        t = e["qpc"] - q0
        s0 = int(t * rate)
        seg = mono[s0:s0 + int(1.0 * rate) + len(x)]
        c = signal.fftconvolve(seg, x[::-1], mode="valid")
        win = np.concatenate([[0], np.cumsum(seg ** 2)])
        en = win[len(x):] - win[:-len(x)]
        nc = c / np.sqrt(np.maximum(en * np.dot(x, x), 1e-9))
        k = int(np.argmax(nc))
        g = float(np.dot(seg[k:k + len(x)], x) / np.dot(x, x))
        z = e["values"][5]
        print(f"  t {t:8.3f}  slot {slot:3}  vol {e['values'][1]:.2f} attn {e['values'][2]:.0f} up {z:6.0f}: "
              + (f"delay {1000 * k / rate:6.1f} ms, gain {g:.4f}, corr {nc[k]:.3f}" if nc[k] > 0.5 else "not found"))


if __name__ == "__main__":
    main()
