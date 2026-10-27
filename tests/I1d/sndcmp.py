#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1d test aid: compares the sound starts of two sessions (OMsnd `s_om_debug 4` lines in the logs of the
# original 1.07f client and of the OpenMerc client), segment by segment (`OMTEST marker <name>` lines).
# Each start is shown as (time since the segment's first start in ms, name, entity, channel, volume, attenuation,
# delay); the two lists are aligned by sound name (difflib), so missing / extra / moved sounds stand out.
#   python -I sndcmp.py <orig client.log> <om client.log> [--abs] [--only <segment>]
# Implements: SPEC-400 / SPEC-003 test comparison (no game code)
import argparse
import difflib
import re
import sys

# OMsnd 0.4 (build-I5s2-x86 / build-X1-sound): "[p=<real ms> cl=<client ms>] ... vol <0..255>";
# OMsnd 0.3 (build-sound-x86-Release): "[t=<real ms>] ... vol <0.00..1.00>" - the real time then stands in for the
# client time (use real-time runs, fixedtime 0, when comparing a 0.3 log with a 0.4 log)
START = re.compile(r"OMsnd: \[(?:p=(\d+) cl=(-?\d+)|t=(\d+))\] start (\S+) ent (-?\d+) ch (\d+) vol ([\d.]+) attn ([\d.]+) ofs ([\d.]+)(.*)")
MARK = re.compile(r"OMTEST marker (\S+)")


def segments(path):
    segs = [("start", [])]
    with open(path, encoding="latin-1") as f:
        for line in f:
            m = MARK.search(line)
            if m:
                segs.append((m.group(1), []))
                continue
            m = START.search(line)
            if m:
                cl = int(m.group(2)) if m.group(2) is not None else int(m.group(3))
                vol = m.group(7)
                vol = int(round(float(vol) * 255)) if "." in vol else int(vol)
                segs[-1][1].append(dict(cl=cl, name=m.group(4).lower().lstrip("/"), ent=int(m.group(5)),
                                        ch=int(m.group(6)), vol=vol, attn=float(m.group(8)), ofs=float(m.group(9)),
                                        entity="(entity)" in m.group(10)))
    return segs


def fmt(e, t0):
    return "%5d %-38s e%-2d c%d v%-3d a%.1f%s" % (e["cl"] - t0, e["name"], e["ent"], e["ch"], e["vol"], e["attn"],
                                                  " d%.2f" % e["ofs"] if e["ofs"] else "")


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("orig")
    ap.add_argument("om")
    ap.add_argument("--only", default="")
    a = ap.parse_args(argv)
    so, sm = segments(a.orig), segments(a.om)
    dm = dict()
    for name, ev in sm:
        dm.setdefault(name, []).append(ev)
    total_same = total_o = total_m = 0
    for name, eo in so:
        if a.only and name != a.only:
            continue
        lst = dm.get(name)
        em = lst.pop(0) if lst else []
        print("=== %s: original %d, OpenMerc %d sound starts" % (name, len(eo), len(em)))
        to = eo[0]["cl"] if eo else 0
        tm = em[0]["cl"] if em else 0
        ko = ["%s e%d c%d v%d a%.1f" % (e["name"], e["ent"], e["ch"], e["vol"], e["attn"]) for e in eo]
        km = ["%s e%d c%d v%d a%.1f" % (e["name"], e["ent"], e["ch"], e["vol"], e["attn"]) for e in em]
        sm_ = difflib.SequenceMatcher(None, ko, km, autojunk=False)
        for op, i1, i2, j1, j2 in sm_.get_opcodes():
            n = max(i2 - i1, j2 - j1)
            for k in range(n):
                lo = fmt(eo[i1 + k], to) if i1 + k < i2 else ""
                lm = fmt(em[j1 + k], tm) if j1 + k < j2 else ""
                mark = "  " if op == "equal" else "!="
                if op == "equal":
                    total_same += 1
                    dt = (em[j1 + k]["cl"] - tm) - (eo[i1 + k]["cl"] - to)
                    if dt:
                        mark = "dt%+d" % dt
                print("%-4s %-75s | %s" % (mark, lo, lm))
        total_o += len(eo)
        total_m += len(em)
    print("== equal (by name/entity/channel/volume/attenuation): %d; original %d, OpenMerc %d" % (
        total_same, total_o, total_m))


if __name__ == "__main__":
    main(sys.argv[1:])
