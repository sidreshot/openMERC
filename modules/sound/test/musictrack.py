#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-040 5 (black-box measurement of dynamic music in a capture; no game code)
"""Measure the level of known music pieces over time in a capture.

usage: musictrack.py <capture.wav> <piece.wav> [<piece.wav> ...] [--win 0.05] [--table 0.5]
                     [--proxy <run.proxy.jsonl>] [--min-corr 0.3] [--fades]

Pieces are stereo renders of `.dms` pieces (openmerc-data/decoded/music/<set>/<piece>.wav, SPEC-111/110).
1. Every occurrence of every piece (each start of the piece from its beginning, or a later entry point) is located
   by cross-correlating 2 s probes of the piece with the capture's left channel.
2. Per window, the gains of all occurrences that overlap it are fitted jointly by least squares, per side.
   Gain 1.0 = the piece's own sample values; Defsnd/OMsnd music gain is trunc(level * s_musicvolume) / 256.
3. With --proxy, the relay's events (music byte changes, svc_sound_info, stuffed commands) are printed on the
   capture's time axis (both use QueryPerformanceCounter: proccap's first packet time stamp, the relay's
   time.perf_counter()).
4. --fades reports, for each change of a piece's gain, the start, the 10-90 % time and the end level.
"""
import argparse
import json
import re

import numpy as np
from scipy import signal

from soundfit import read_wav


def load_piece(path, rate):
    r, x = read_wav(path)
    if r != rate:
        x = np.stack([signal.resample_poly(x[:, c], rate, r) for c in range(x.shape[1])], axis=1)
    return x


def find_occurrences(cap, piece, rate, min_corr, min_votes=3):
    """start positions (capture samples) of the piece: 2 s probes taken every second of the piece are located by
    normalised correlation with the capture's left channel; each match votes for start = match - probe offset.
    Starts with at least min_votes votes (10 ms bins) are candidates. Self-similar music (a repeated bar or loop)
    also matches at a bar's distance from the true start; such a false start only "explains" capture time that the
    true start explains with more votes. So candidates are taken greedily by votes, and one is kept only if at least
    min_votes of its matches lie in capture seconds not yet explained by a kept start. A track that was silent does
    not advance, so after a pause the same piece continues as a second start shifted by the pause: its matches lie
    in new capture time and it is kept. Starts before the capture are impossible (the capture starts with the game)."""
    L = cap[:, 0]
    plen = 2 * rate
    win = np.concatenate([[0], np.cumsum(L ** 2)])
    e = win[plen:] - win[:-plen]
    votes = {}
    for po in range(0, max(1, len(piece) - plen + 1), rate):
        pr = piece[po:po + plen, 0]
        ep = float(np.dot(pr, pr))
        if ep < 1e6:
            continue
        c = signal.fftconvolve(L, pr[::-1], mode="valid")
        nc = c / np.sqrt(np.maximum(e * ep, 1e-9))
        peaks, _ = signal.find_peaks(nc, height=min_corr, distance=rate // 2)
        for k in peaks:
            b = int(round((int(k) - po) / (rate / 100)))
            votes.setdefault(b, []).append((int(k) - po, int(k)))
    occ = []
    for b in sorted(votes):
        v = votes[b] + votes.get(b + 1, []) + votes.get(b - 1, [])
        if len(v) >= min_votes and len(votes[b]) >= max(len(votes.get(b + 1, [])), len(votes.get(b - 1, []))):
            o = int(np.median([s for s, _ in v]))
            if o < -rate // 10:
                continue
            if not occ or o - occ[-1][0] > rate // 10:
                occ.append((o, [k for _, k in v]))
    best, covered = [], set()
    for o, ks in sorted(occ, key=lambda x: -len(x[1])):
        secs = {k // rate for k in ks}
        if len(secs - covered) >= min_votes:
            best.append(o)
            covered |= {s + d for s in secs for d in (-1, 0, 1)}
    return sorted(best)


def frac_delay(x, d, taps=64):
    """x delayed by d samples (|d| < 1) with a Blackman-windowed sinc"""
    n = np.arange(-taps, taps + 1)
    h = np.sinc(n - d) * np.blackman(len(n))
    h /= h.sum()
    return np.stack([signal.fftconvolve(x[:, c], h, mode="same") for c in range(x.shape[1])], axis=1)


def refine(cap, piece, o, rate, span=30.0, search=0.003):
    """start of an occurrence to a fraction of a sample: cross-correlation peak within +-search s over the first
    `span` seconds of the occurrence, parabolic interpolation. Returns (integer start, fractional delay)."""
    W = int(search * rate)
    s = max(o, 0) + W
    e = min(len(cap) - W, o + len(piece) - W, s + int(span * rate))
    if e - s < rate:
        return o, 0.0
    x = cap[s:e, 0]
    ref = piece[s - o - W:e - o + W, 0]
    c = signal.correlate(ref, x, mode="valid")  # c[k]: x ~ ref[k:]; k = W is no shift
    k = int(np.argmax(c))
    d = 0.0
    if 0 < k < len(c) - 1:
        den = c[k - 1] - 2 * c[k] + c[k + 1]
        if den:
            d = 0.5 * (c[k - 1] - c[k + 1]) / den
    # x[t] ~ ref[t + k - W + d]: the piece starts (k - W + d) samples earlier
    shift = -(k - W) - d
    io = o + int(np.floor(shift))
    return io, shift - np.floor(shift)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("capture")
    ap.add_argument("pieces", nargs="+")
    ap.add_argument("--win", type=float, default=0.05)
    ap.add_argument("--table", type=float, default=0.5)
    ap.add_argument("--proxy")
    ap.add_argument("--min-corr", type=float, default=0.5)
    ap.add_argument("--fades", action="store_true")
    ap.add_argument("--stretch", type=float, default=0,
                    help="stretch the capture by this many ppm before the analysis (OpenAL Soft plays a 22050 Hz "
                         "stream on a 48 kHz device with the 16-bit fixed-point step 30106/65536, i.e. 13.3 ppm fast; "
                         "OMsnd captures: --stretch 13.3)")
    ap.add_argument("--no-refine", action="store_true", help="keep the occurrence starts at whole samples as found")
    ap.add_argument("--lowpass", type=float, default=0,
                    help="low-pass capture and pieces at this frequency (Hz) before fitting: removes the differences "
                         "between the output paths' resamplers near the mixer's Nyquist frequency")
    ap.add_argument("--from", dest="t0", type=float, default=0)
    ap.add_argument("--to", dest="t1", type=float, default=1e9)
    a = ap.parse_args()
    rate, cap = read_wav(a.capture)
    if a.stretch:
        cap = signal.resample(cap, int(round(len(cap) * (1 + a.stretch * 1e-6))), axis=0)
    pieces = [load_piece(p, rate) for p in a.pieces]
    if a.lowpass:
        sos = signal.butter(8, a.lowpass, fs=rate, output="sos")
        cap = signal.sosfiltfilt(sos, cap, axis=0)
        pieces = [signal.sosfiltfilt(sos, x, axis=0) for x in pieces]
    names = [re.sub(r".*[\\/]music[\\/]", "", p) for p in a.pieces]
    occs = []  # (piece index, start)
    for i, pc in enumerate(pieces):
        for o in find_occurrences(cap, pc, rate, a.min_corr):
            if o + len(pc) > 0 and o < len(cap):
                occs.append((i, o))
    # starts to a fraction of a sample: each occurrence gets its own (fractionally delayed) copy of the piece
    refs = {}
    if not a.no_refine:
        new = []
        for i, o in occs:
            io, fr = refine(cap, pieces[i], o, rate)
            key = (i, io)
            if key not in refs:
                refs[key] = frac_delay(pieces[i], fr) if fr > 0.02 and fr < 0.98 else pieces[i]
                if fr >= 0.98:
                    key = (i, io + 1)
                    refs[key] = pieces[i]
            new.append(key)
        occs = new
    print(f"# {a.capture}: {len(cap) / rate:.1f} s")
    for i, o in occs:
        print(f"# occurrence: {names[i]} starts at {o / rate:.3f} s (ends {(o + len(pieces[i])) / rate:.3f} s)")

    hop = int(rate * a.win)
    nwin = len(cap) // hop

    def fit(occs):
        gains = np.zeros((len(occs), nwin, 2))
        for w in range(nwin):
            s, e = w * hop, (w + 1) * hop
            act = [j for j, (i, o) in enumerate(occs) if o < e and o + len(pieces[i]) > s]
            if not act:
                continue
            for side in (0, 1):
                cols = []
                for j in act:
                    i, o = occs[j]
                    seg = np.zeros(hop)
                    ps, pe = max(s, o), min(e, o + len(pieces[i]))
                    seg[ps - s:pe - s] = refs.get((i, o), pieces[i])[ps - o:pe - o, side]
                    cols.append(seg)
                A = np.stack(cols, axis=1)
                if np.abs(A).sum() < 1:
                    continue
                g, *_ = np.linalg.lstsq(A, cap[s:e, side], rcond=None)
                for k, j in enumerate(act):
                    gains[j, w, side] = g[k]
        return gains

    # fit, drop occurrences that never carry level (false starts of repetitive music), fit again
    for _ in range(3):
        gains = fit(occs)
        keep = [j for j in range(len(occs)) if np.percentile(np.abs(gains[j, :, 0][gains[j, :, 0] != 0]), 75)
                > 0.03] if occs else []
        if len(keep) == len(occs):
            break
        occs = [occs[j] for j in keep]
    gains = fit(occs)
    for i, o in occs:
        print(f"# kept occurrence: {names[i]} starts at {o / rate:.3f} s")
    # per piece: the sum of its occurrences
    pg = np.zeros((len(pieces), nwin, 2))
    for j, (i, o) in enumerate(occs):
        pg[i] += gains[j]

    events = []
    if a.proxy:
        txt = open(a.capture + ".txt").read()
        q0 = int(re.search(r"first_packet_qpc_100ns (-?\d+)", txt).group(1)) * 1e-7
        for line in open(a.proxy):
            r = json.loads(line)
            ev = r.get("event")
            if ev in ("values", "sound_info", "stufftext", "first_frame", "configstring") and "qpc" in r:
                events.append((r["qpc"] - q0, ev, {k: v for k, v in r.items() if k not in ("wall", "qpc", "t_relay")}))
        for t, ev, r in events:
            print(f"# event at {t:8.3f} s: {ev} {r}")

    step = max(1, int(round(a.table / a.win)))
    print("#   time_s  " + "  ".join(f"{n[:20]:>20} L/R      " for n in names))
    for w in range(0, nwin, step):
        t = w * a.win
        if t < a.t0 or t > a.t1:
            continue
        row = "  ".join(f"{pg[i, w, 0]:9.4f} {pg[i, w, 1]:9.4f}" for i in range(len(pieces)))
        print(f"{t:9.2f}  {row}")

    if a.fades:
        print("# fades (per occurrence, left side, smoothed over 3 windows)")
        for j, (i, o) in enumerate(occs):
            g = np.convolve(gains[j, :, 0], np.ones(3) / 3, mode="same")
            lo = max(0, o // hop)
            hi = min(nwin, (o + len(pieces[i])) // hop)
            prev = None
            seg_start = lo
            for w in range(lo + 1, hi):
                pass
            # report plateaus: runs where the level is stable within 2 %
            runs = []
            w = lo
            while w < hi:
                v = g[w]
                k = w
                while k + 1 < hi and abs(g[k + 1] - v) <= 0.02 * max(abs(v), 0.01):
                    k += 1
                if k - w >= 4:
                    runs.append((w * a.win, k * a.win, float(np.median(gains[j, w:k + 1, 0]))))
                w = k + 1
            for r0, r1, v in runs:
                print(f"#   {names[i]}@{o / rate:.1f}s: level {v:.4f} from {r0:.2f} to {r1:.2f} s")


if __name__ == "__main__":
    main()
