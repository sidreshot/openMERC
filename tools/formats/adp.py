#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-110
"""Reader (and reference encoder) for .adp sounds (SPEC-110).

An .adp is a uint32 sample rate followed by headerless 4-bit IMA/DVI ADPCM,
mono, two samples per byte, high nibble first, decoder state starting at
predictor 0 / step index 0. The ADPCM tables are the public IMA ADPCM tables.

usage:
  adp.py info <file.adp>...                     rate, sample count, duration, trailer
  adp.py decode <in.adp> <out.wav> [--trim]      16-bit PCM WAV (--trim drops the
                                                 encoded RIFF-chunk trailer, see SPEC-110 §4)
  adp.py stereo <left.adp> <right.adp> <out.wav> [--trim]
  adp.py validate <dir> [--out <wavdir>] [--jobs N]
                                                 decode every .adp under <dir>, print stats
"""
import os
import struct
import sys
import wave
from array import array

# Public IMA ADPCM tables (IMA Digital Audio Focus and Technical Working Groups,
# "Recommended Practices for Enhancing Digital Audio Compatibility in Multimedia
# Systems", rev. 3.00, 1992).
STEP_TABLE = (
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230,
    253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796, 876, 963,
    1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327,
    3660, 4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767)
INDEX_TABLE = (-1, -1, -1, -1, 2, 4, 6, 8)

HEADER_SIZE = 4


class ADPError(ValueError):
    pass


def _diff(step, code):
    """Signed predictor change for a 4-bit code at a given step (shift-and-add form)."""
    d = step >> 3
    if code & 4:
        d += step
    if code & 2:
        d += step >> 1
    if code & 1:
        d += step >> 2
    return -d if code & 8 else d


# Combined lookup: key = index*16 + code -> (delta, next index)
_DELTA = []
_NEXT = []
for _i, _s in enumerate(STEP_TABLE):
    for _c in range(16):
        _DELTA.append(_diff(_s, _c))
        _NEXT.append(min(88, max(0, _i + INDEX_TABLE[_c & 7])))
del _i, _s, _c


def parse(data):
    """Split an .adp file into (sample_rate, adpcm_payload)."""
    if len(data) < HEADER_SIZE:
        raise ADPError("file shorter than the 4-byte header")
    rate = struct.unpack_from("<I", data, 0)[0]
    if not 1000 <= rate <= 192000:
        raise ADPError(f"implausible sample rate {rate}")
    return rate, bytes(data[HEADER_SIZE:])


def decode(payload, pred=0, index=0, track_states=0):
    """Decode ADPCM bytes to signed 16-bit samples (array('h')).

    Two samples per byte, high nibble first. With track_states=K, also return a
    list of the decoder states (pred, index) *before* each of the last K samples.
    """
    pred0, index0 = pred, index
    out = array("h", bytes(2 * 2 * len(payload)))
    D, N = _DELTA, _NEXT
    k = 0
    for b in payload:
        j = (index << 4) | (b >> 4)
        pred += D[j]
        if pred > 32767:
            pred = 32767
        elif pred < -32768:
            pred = -32768
        index = N[j]
        out[k] = pred
        j = (index << 4) | (b & 15)
        pred += D[j]
        if pred > 32767:
            pred = 32767
        elif pred < -32768:
            pred = -32768
        index = N[j]
        out[k + 1] = pred
        k += 2
    if not track_states:
        return out
    # Re-run the tail cheaply: states are only needed for the last K samples.
    n = len(out)
    start = max(0, n - track_states)
    states = []
    p, i = _replay_index(payload, start, pred0, index0)
    codes = codes_of(payload)
    for t in range(start, n):
        states.append((p, i))
        j = (i << 4) | codes[t]
        p = min(32767, max(-32768, p + D[j]))
        i = N[j]
    return out, states


def _replay_index(payload, upto, p=0, i=0):
    """Decoder state (pred, index) before sample number `upto`, starting from (p, i)."""
    D, N = _DELTA, _NEXT
    for t, c in enumerate(codes_of(payload)):
        if t == upto:
            break
        j = (i << 4) | c
        p = min(32767, max(-32768, p + D[j]))
        i = N[j]
    return p, i


def codes_of(payload):
    """The 4-bit codes in playback order (high nibble first)."""
    out = bytearray(2 * len(payload))
    out[0::2] = bytes(b >> 4 for b in payload)
    out[1::2] = bytes(b & 15 for b in payload)
    return out


def encode_codes(samples, pred=0, index=0):
    """Reference IMA encoder: returns (codes, pred, index).

    Matches the stock encoder bit for bit (SPEC-110 §5): sign from the
    difference, three magnitude bits by successive comparison against step,
    step/2, step/4; the encoder tracks the decoder's (clamped) reconstruction.
    """
    codes = bytearray()
    for x in samples:
        step = STEP_TABLE[index]
        diff = x - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        if diff >= step:
            code |= 4
            diff -= step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
        if diff >= step >> 2:
            code |= 1
        pred = min(32767, max(-32768, pred + _diff(step, code)))
        index = min(88, max(0, index + INDEX_TABLE[code & 7]))
        codes.append(code)
    return codes, pred, index


def encode(samples, rate=22050):
    """Encode int16 samples to a complete .adp file (odd final sample is padded with code 0)."""
    codes, _, _ = encode_codes(samples)
    if len(codes) % 2:
        codes.append(0)
    payload = bytes((codes[i] << 4) | codes[i + 1] for i in range(0, len(codes), 2))
    return struct.pack("<I", rate) + payload


# --- trailer detection (SPEC-110 §4) -------------------------------------------------

def _w(two):
    return struct.unpack("<h", two)[0]


# Word patterns of the start of a RIFF "LIST" chunk ("LIST", uint32 size, "INFO") as
# the converter saw them. SIZE marks the free low word of the chunk size. Mono sources
# give all words in order; stereo sources were split word-by-word into l*/r* files.
SIZE = None
TRAILER_PATTERNS = {
    "mono": (_w(b"LI"), _w(b"ST"), SIZE, 0, _w(b"IN"), _w(b"FO")),
    "left": (_w(b"LI"), SIZE, _w(b"IN")),
    "right": (_w(b"ST"), 0, _w(b"FO")),
}


def _match(pattern, codes, pos, pred, index):
    """True if IMA-encoding `pattern` from state (pred, index) reproduces codes[pos:]."""
    for k, w in enumerate(pattern):
        if pos + k >= len(codes):
            return k >= 3          # stream ended: accept if enough words matched
        if w is SIZE:
            rest = pattern[k + 1:]
            for size in range(4, 4096, 2):
                c, p1, i1 = encode_codes((size,), pred, index)
                if c[0] == codes[pos + k] and _match(rest, codes, pos + k + 1, p1, i1):
                    return True
            return False
        c, pred, index = encode_codes((w,), pred, index)
        if c[0] != codes[pos + k]:
            return False
    return True


def find_riff_trailer(payload, layout="mono", window=48):
    """Return the sample offset where an IMA-encoded RIFF "LIST" chunk starts (the stock
    converter's artefact, SPEC-110 §4), or None. layout: "mono", "left" or "right"
    (the l*/r* halves of a stereo music piece). The earliest match within the last
    `window` samples is returned.
    """
    pattern = TRAILER_PATTERNS[layout]
    codes = codes_of(payload)
    n = len(codes)
    if n < len(pattern):
        return None
    _, states = decode(payload, track_states=min(window, n))
    base = n - len(states)
    for k, (p0, i0) in enumerate(states):
        if _match(pattern, codes, base + k, p0, i0):
            return base + k
    return None


def guess_layout(path):
    """Stereo music pieces are stored as sound/music/<set>/l<Name>.adp + r<Name>.adp."""
    p = path.replace("\\", "/").lower()
    b = os.path.basename(p)
    if "/music/" in p and b[:1] in ("l", "r"):
        return "left" if b[0] == "l" else "right"
    return "mono"


# --- WAV output ----------------------------------------------------------------------

def write_wav(path, channels, rate):
    """Write 16-bit PCM WAV. channels: list of array('h') of equal length."""
    n = min(len(c) for c in channels)
    nch = len(channels)
    inter = array("h", bytes(2 * n * nch))
    for ci, c in enumerate(channels):
        inter[ci::nch] = c[:n]
    if sys.byteorder != "little":
        inter.byteswap()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with wave.open(path, "wb") as w:
        w.setnchannels(nch)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(inter.tobytes())


def load(path, trim=False, layout=None):
    """Read and decode an .adp file -> (rate, samples, trailer_offset_or_None).

    With trim=True the samples from the detected RIFF-chunk trailer on are dropped.
    """
    with open(path, "rb") as f:
        data = f.read()
    rate, payload = parse(data)
    pcm = decode(payload)
    tr = find_riff_trailer(payload, layout or guess_layout(path)) if trim else None
    if tr is not None:
        pcm = pcm[:tr]
    return rate, pcm, tr


# --- validation ----------------------------------------------------------------------

def _stats(pcm):
    n = len(pcm)
    if n == 0:
        return dict(n=0, rms=0.0, dc=0.0, clip=0.0, peak=0)
    s = s2 = 0
    clip = 0
    peak = 0
    for v in pcm:
        s += v
        s2 += v * v
        if v >= 32767 or v <= -32768:
            clip += 1
    peak = max(max(pcm), -min(pcm))
    return dict(n=n, rms=(s2 / n) ** 0.5, dc=s / n, clip=clip / n, peak=peak)


def _validate_one(args):
    path, root, outdir = args
    try:
        with open(path, "rb") as f:
            data = f.read()
        rate, payload = parse(data)
        pcm = decode(payload)
        tr = find_riff_trailer(payload, guess_layout(path))
        body = pcm[:tr] if tr is not None else pcm
        st = _stats(body)
        if outdir:
            rel = os.path.relpath(path, root)
            write_wav(os.path.join(outdir, os.path.splitext(rel)[0] + ".wav"), [body], rate)
        return dict(path=path, ok=True, rate=rate, samples=len(pcm),
                    trailer=tr, **st)
    except Exception as e:  # noqa: BLE001 - report every failure
        return dict(path=path, ok=False, err=repr(e))


def main(argv):
    if not argv:
        print(__doc__)
        return 2
    cmd = argv[0]
    trim = "--trim" in argv
    args = [a for a in argv[1:] if a != "--trim"]
    if cmd == "info":
        for p in args:
            rate, pcm, _ = load(p)
            with open(p, "rb") as f:
                tr = find_riff_trailer(parse(f.read())[1], guess_layout(p))
            st = _stats(pcm)
            print(f"{p}: rate={rate} samples={len(pcm)} dur={len(pcm)/rate:.3f}s "
                  f"rms={st['rms']:.0f} dc={st['dc']:.0f} clip={st['clip']:.4f} "
                  f"trailer_at={tr if tr is not None else '-'}")
    elif cmd == "decode":
        rate, pcm, tr = load(args[0], trim)
        write_wav(args[1], [pcm], rate)
        print(f"{args[1]}: {len(pcm)} samples @ {rate} Hz"
              + (f" (trimmed at {tr})" if tr is not None else ""))
    elif cmd == "stereo":
        rl, l, _ = load(args[0], trim, "left")
        rr, r, _ = load(args[1], trim, "right")
        if rl != rr:
            raise ADPError("channel rates differ")
        write_wav(args[2], [l, r], rl)
        print(f"{args[2]}: {min(len(l), len(r))} frames @ {rl} Hz stereo")
    elif cmd == "validate":
        root = args[0]
        outdir = None
        jobs = os.cpu_count() or 1
        if "--out" in args:
            outdir = args[args.index("--out") + 1]
        if "--jobs" in args:
            jobs = int(args[args.index("--jobs") + 1])
        files = []
        for d, _, fs in os.walk(root):
            for fn in fs:
                if fn.lower().endswith(".adp"):
                    files.append(os.path.join(d, fn))
        files.sort()
        work = [(p, root, outdir) for p in files]
        if jobs > 1:
            from multiprocessing import Pool
            with Pool(jobs) as pool:
                res = pool.map(_validate_one, work, chunksize=4)
        else:
            res = [_validate_one(w) for w in work]
        bad = [r for r in res if not r["ok"]]
        for r in res:
            if r["ok"]:
                tr = r["trailer"]
                tl = r["samples"] - tr if tr is not None else 0
                print(f"{r['rate']:6d} {r['samples']:9d} trailer={tl:3d} rms={r['rms']:7.0f} "
                      f"dc={r['dc']:7.0f} clip={r['clip']:.4f} peak={r['peak']:6d} "
                      f"{os.path.relpath(r['path'], root)}")
            else:
                print(f"FAIL {r['path']}: {r['err']}")
        print(f"# {len(res)} files, {len(res) - len(bad)} decoded, {len(bad)} failed; "
              f"{sum(1 for r in res if r['ok'] and r['trailer'] is not None)} with RIFF-chunk trailer")
        return 1 if bad else 0
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
