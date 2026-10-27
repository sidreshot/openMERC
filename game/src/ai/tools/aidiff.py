#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G2 - compare what a client saw of the characters in two captures (original game module vs OpenMerc's):
# per character entity (matched by entity number), the sequence timeline and the position/yaw sampled once per second.
# Times are seconds of game time since the first frame the client received. Uses aicap.py.
# Implements: SPEC-700, SPEC-711 (black-box comparison)
#
# usage: aidiff.py <orig cap.jsonl> <om cap.jsonl> [--ents 181,182] [--until 60] [--brief]

import argparse
import difflib
import math
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import aicap  # noqa: E402


def timeline(sess):
    t0 = sess.timeline[0]["time"] if sess.timeline else 0
    out = {}
    for row in sess.timeline:
        t = row["time"] - t0
        for num, e in row["ents"].items():
            seq = row.get("seqs", {}).get(num)
            out.setdefault(num, []).append((t, e["origin"], e["angles"][1], seq))
    return out


UNKNOWN = True  # keep the decoder's unnamed sequences ("?") as runs (--no-unknown drops them)


def seqruns(samples):
    """sequence runs (time, name); "?" is a sequence the decoder cannot name (sofnet: a Ghoul string index beyond
    the parsed table, seen for sequences the original registers during play, e.g. tsr1's std_ilooking) - with
    UNKNOWN it counts as a run, so a character that acts is not mistaken for an idle one"""
    runs = []
    for t, o, y, s in samples:
        if s and (UNKNOWN or s != "?") and (not runs or runs[-1][1] != s):
            runs.append((t, s))
    return runs


def at(samples, t):
    best = None
    for s in samples:
        if s[0] <= t + 1e-6:
            best = s
        else:
            break
    return best


def is_char(samples):
    return any(s[3] and "ENEMY/" in s[3].upper() for s in samples)


def short(s):
    return s.rsplit("/", 1)[-1].replace(".GHL", "").lower()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("orig")
    ap.add_argument("om")
    ap.add_argument("--ents", default="")
    ap.add_argument("--until", type=float, default=1e9)
    ap.add_argument("--brief", action="store_true")
    ap.add_argument("--no-pair", action="store_true", help="compare strictly by entity number")
    ap.add_argument("--no-unknown", action="store_true", help="drop the decoder's unnamed (?) sequences")
    a = ap.parse_args(argv)
    global UNKNOWN
    UNKNOWN = not a.no_unknown
    A = timeline(aicap.decode(a.orig))
    B = timeline(aicap.decode(a.om))
    want = set(int(x) for x in a.ents.split(",") if x) if a.ents else None
    # characters made during play (spawners, script spawns) can get other entity numbers in the two runs: an entity
    # seen in only one capture is paired with one of the other capture's unpaired ones by first position (< 64
    # units) and first-seen time (< 5 s); the OpenMerc entity is then compared under the original's number
    if not a.no_pair:
        only_a = [n for n in A if n not in B and is_char(A[n])]
        only_b = [n for n in B if n not in A and is_char(B[n])]
        # the same number on two different characters (first seen far apart): both go to the pairing pools
        Bx = {}
        for n in [n for n in A if n in B and (is_char(A[n]) or is_char(B[n]))]:
            if math.dist(A[n][0][1], B[n][0][1]) > 64:
                only_a.append(n)
                Bx[n] = B.pop(n)
        for n, v in Bx.items():
            key = -n  # parked under a negative key until paired
            B[key] = v
            only_b.append(key)
        for n in sorted(only_a):
            best = None
            for m in only_b:
                d = math.dist(A[n][0][1], B[m][0][1])
                dt = abs(A[n][0][0] - B[m][0][0])
                if d < 64 and dt < 5 and (best is None or d < best[0]):
                    best = (d, m)
            if best:
                m = best[1]
                only_b.remove(m)
                print("paired: orig ent %d = om ent %d (first positions %.0f apart)" % (n, abs(m), best[0]))
                B[n] = B.pop(m)
        for m in only_b:  # unpaired: back under its own number unless that is taken
            if m < 0 and -m not in B:
                B[-m] = B.pop(m)
            elif m < 0:
                B[100000 - m] = B.pop(m)
    nums = sorted(n for n in set(A) | set(B) if (want is None and (is_char(A.get(n, [])) or is_char(B.get(n, []))))
                  or (want and n in want))
    total_ratio, total_err, nerr = [], 0.0, 0
    for n in nums:
        sa = [s for s in A.get(n, []) if s[0] <= a.until]
        sb = [s for s in B.get(n, []) if s[0] <= a.until]
        ra = [short(s) for _, s in seqruns(sa)]
        rb = [short(s) for _, s in seqruns(sb)]
        ratio = difflib.SequenceMatcher(None, ra, rb).ratio() if (ra or rb) else 1.0
        total_ratio.append(ratio)
        errs = []
        t = 0.0
        end = min(sa[-1][0] if sa else 0, sb[-1][0] if sb else 0)
        while t <= end:
            x, y = at(sa, t), at(sb, t)
            if x and y:
                errs.append(math.dist(x[1], y[1]))
            t += 1.0
        merr = sum(errs) / len(errs) if errs else float("nan")
        if errs:
            total_err += merr
            nerr += 1
        print("ent %4d: seen orig %s om %s | sequences %d/%d match %.2f | mean position difference %.1f" % (
            n, "%.1f-%.1f" % (sa[0][0], sa[-1][0]) if sa else "-", "%.1f-%.1f" % (sb[0][0], sb[-1][0]) if sb else "-",
            len(ra), len(rb), ratio, merr))
        if a.brief:
            continue
        ta, tb = seqruns(sa), seqruns(sb)
        for i in range(max(len(ta), len(tb))):
            l = "%6.1f %-28s" % (ta[i][0], short(ta[i][1])) if i < len(ta) else " " * 35
            r = "%6.1f %-28s" % (tb[i][0], short(tb[i][1])) if i < len(tb) else ""
            print("    %s | %s" % (l, r))
    if total_ratio:
        print("summary: %d characters, mean sequence match %.2f, mean position difference %.1f units" % (
            len(total_ratio), sum(total_ratio) / len(total_ratio), total_err / nerr if nerr else float("nan")))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
