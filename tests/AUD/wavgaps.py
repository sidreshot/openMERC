#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc AUD - finds audible faults in an OMsnd capture (s_om_wavefile, 16-bit PCM, any rate/channels):
#   drop  - the level falls from speech/effect loudness to near silence within 3 ms (a voice stopped mid-sound;
#           a sound's own end fades over tens of ms)
#   gap   - a run of >= 5 ms of near silence with sound on both sides within 50 ms (a hole in the output)
#   click - a sample-to-sample step far larger than the local signal (a discontinuity)
#   wobble- 20 ms RMS windows that alternate up/down by more than 3 dB for >= 8 windows in a row (gain tremble)
# usage: wavgaps.py <snd.wav> [--json] [--from s] [--to s]
# Implements: black-box test harness (no game code)
import array, json, math, struct, sys
from pathlib import Path


def load(path):
    b = Path(path).read_bytes()
    pos, fmt, off, n = 12, None, None, None
    while pos + 8 <= len(b):
        cid, clen = b[pos:pos + 4], struct.unpack_from("<I", b, pos + 4)[0]
        if cid == b"fmt ":
            fmt = struct.unpack_from("<HHIIHH", b, pos + 8)
        elif cid == b"data":
            off = pos + 8
            n = clen if clen and off + clen <= len(b) else len(b) - off
            break
        pos += 8 + clen + (clen & 1)
    _, ch, rate, _, _, bits = fmt
    a = array.array("h")
    a.frombytes(b[off:off + (n // 2) * 2])
    if ch > 1:  # max of |channels| as mono envelope source, mean for steps
        m = array.array("h", [0]) * (len(a) // ch)
        for i in range(len(m)):
            s = a[i * ch:(i + 1) * ch]
            m[i] = max(s, key=abs)
        a = m
    return rate, a


def analyse(path, t0=0.0, t1=None):
    rate, a = load(path)
    ms = rate // 1000
    i0 = int(t0 * rate)
    i1 = len(a) if t1 is None else min(len(a), int(t1 * rate))
    # 1 ms peak envelope
    env = []
    for i in range(i0, i1 - ms, ms):
        seg = a[i:i + ms]
        env.append(max(abs(min(seg)), abs(max(seg))))
    loud, quiet = 3000, 120
    drops, gaps = [], []
    for k in range(10, len(env) - 1):
        prev = max(env[k - 10:k - 3])
        if prev >= loud and max(env[k:k + 3]) <= quiet and min(env[k - 10:k - 3]) > loud / 4:
            drops.append(round((i0 + k * ms) / rate, 3))
    k = 0
    while k < len(env):
        if env[k] <= quiet:
            j = k
            while j < len(env) and env[j] <= quiet:
                j += 1
            if j - k >= 5 and k >= 50 and j + 50 < len(env) and max(env[k - 50:k]) >= loud and max(env[j:j + 50]) >= loud \
                    and j - k <= 200:
                gaps.append((round((i0 + k * ms) / rate, 3), j - k))
            k = j
        else:
            k += 1
    clicks = []
    w = rate // 100
    for i in range(i0 + w, i1 - 1):
        d = abs(a[i + 1] - a[i])
        if d > 12000:
            seg = a[i - w:i]
            r = math.sqrt(sum(x * x for x in seg[::4]) / max(1, len(seg[::4])))
            if d > 6 * max(r, 500):
                if not clicks or i / rate - clicks[-1] > 0.05:
                    clicks.append(round(i / rate, 3))
    # wobble: 20 ms RMS, sign alternation of >3 dB steps
    rms = []
    W = 20 * ms
    for i in range(i0, i1 - W, W):
        seg = a[i:i + W:2]
        rms.append(math.sqrt(sum(x * x for x in seg) / len(seg)) + 1)
    wob, run, last = [], 0, 0
    for k in range(1, len(rms)):
        db = 20 * math.log10(rms[k] / rms[k - 1])
        sgn = 1 if db > 3 else -1 if db < -3 else 0
        if sgn and sgn == -last and rms[k] > 300:
            run += 1
            if run == 8:
                wob.append(round((i0 + k * W) / rate, 2))
        else:
            run = 1 if sgn else 0
        last = sgn
    return {"seconds": round(len(a) / rate, 1), "rate": rate, "drops": drops, "gaps": gaps, "clicks": clicks,
            "wobble": wob}


def main(argv):
    p = argv[1]
    t0 = float(argv[argv.index("--from") + 1]) if "--from" in argv else 0.0
    t1 = float(argv[argv.index("--to") + 1]) if "--to" in argv else None
    r = analyse(p, t0, t1)
    if "--json" in argv:
        print(json.dumps(r))
    else:
        print("%s: %.1fs drops %d gaps %d clicks %d wobble %d" % (p, r["seconds"], len(r["drops"]), len(r["gaps"]),
              len(r["clicks"]), len(r["wobble"])))
        for k in ("drops", "gaps", "clicks", "wobble"):
            if r[k]:
                print("  %s: %s" % (k, r[k][:40]))


if __name__ == "__main__":
    sys.exit(main(sys.argv))
