#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - analyse x1_probe.cfg runs (xaudit.ps1 -Only probe): stuck at the start, drift while standing
# still (sliding "ice", floating / falling), vertical jumps without a jump, crashes and early exits.
#   xprobe.py <runs dir> [--tag probe_] [--json out.jsonl]
#   xprobe.py --make <intro seconds> <cycles> <out.cfg>   writes the session script (xaudit.ps1 -Only probe)
# A sample is "OMP <label>" followed by the next "position: x y z" line of client.log (viewpos, view origin).
# Implements: black-box test harness (no game code)
import json
import math
import re
import sys
from pathlib import Path

POS = re.compile(r"^position: (-?\d+) (-?\d+) (-?\d+)")
OMP = re.compile(r"^OMP (\S+)")
MARK = re.compile(r"^OMTEST (.+?)\s*$")


def samples(log):
    out, want, phase = [], None, "?"
    for raw in log.read_text(encoding="cp1251", errors="replace").splitlines():
        m = MARK.match(raw)
        if m:
            phase = m.group(1)
            continue
        m = OMP.match(raw)
        if m:
            want = m.group(1)
            continue
        m = POS.match(raw)
        if m and want:
            out.append((phase, want, tuple(int(v) for v in m.groups())))
            want = None
    return out, phase


def h(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def analyse(d):
    d = Path(d)
    r = {"tag": d.name, "map": d.name.split("_", 1)[-1], "flags": []}
    run = d / "run.txt"
    t = run.read_text(errors="replace") if run.exists() else ""
    m = re.search(r"exit code (-?\d+) after ([\d,\.]+) s", t)
    r["exit"] = int(m.group(1)) if m else None
    r["seconds"] = float(m.group(2).replace(",", ".")) if m else 0
    wd = d / "user" / "watchdog.txt"
    if wd.exists() and re.search(r"crash:|stall:|exception", wd.read_text(errors="replace"), re.I):
        r["flags"].append("CRASH(watchdog)")
    log = d / "client.log"
    if not log.exists():
        r["flags"].append("NO_LOG")
        return r
    s, last = samples(log)
    r["last_phase"] = last
    r["n_samples"] = len(s)
    if last != "quit":
        r["flags"].append("ENDED_IN(%s)" % last)
    if r["exit"] not in (0, None) and last != "quit":
        r["flags"].append("EXIT(%s)" % r["exit"])
    by = {}
    for ph, lab, p in s:
        by.setdefault(lab, []).append((ph, p))
    # stuck: displacement of each of the four walks
    seq = [x[1][0][1] for x in [(k, by.get(k)) for k in ("f0", "f1", "b1", "l1", "r1")] if x[1]]
    if len(seq) == 5:
        moves = [h(seq[i], seq[i + 1]) for i in range(4)]
        r["walk"] = [round(v) for v in moves]
        if max(moves) < 16:
            r["flags"].append("STUCK_AT_START(max %.0f)" % max(moves))
        elif sum(1 for v in moves if v < 8) >= 2:
            r["flags"].append("BLOCKED_2+_DIRS(%s)" % r["walk"])
        rise = [seq[i + 1][2] - seq[i][2] for i in range(4)]
        if max(rise) > 48:
            r["flags"].append("ROSE_WALKING(%+d)" % max(rise))
    # standing-still triples: still0 -> still1 -> still2 in the same order
    trip, cur = [], {}
    for ph, lab, p in s:
        if lab == "still0":
            cur = {"ph": ph, 0: p}
        elif lab == "still1" and 0 in cur:
            cur[1] = p
        elif lab == "still2" and 1 in cur:
            cur[2] = p
            trip.append(cur)
            cur = {}
    slides, airs, worst = 0, 0, (0, None)
    for i, c in enumerate(trip):
        dh, dz = h(c[1], c[2]), abs(c[1][2] - c[2][2])
        dh0, dz0 = h(c[0], c[1]), abs(c[0][2] - c[1][2])
        if dh > 4:
            slides += 1
        if dz > 4:
            airs += 1
        sc = max(dh, dz)
        if sc > worst[0]:
            worst = (sc, "%s#%d still1->2 dh %.0f dz %d (0->1 dh %.0f dz %d)" % (c["ph"], i, dh, dz, dh0, dz0))
    r["still_checks"] = len(trip)
    r["slide_while_still"] = slides
    r["z_drift_while_still"] = airs
    r["worst_still"] = worst[1]
    if slides:
        r["flags"].append("SLIDE_STILL(%d/%d)" % (slides, len(trip)))
    if airs:
        r["flags"].append("Z_DRIFT_STILL(%d/%d)" % (airs, len(trip)))
    # height above the spawn while standing in the long phase (floating far above the start level)
    sp = by.get("spawn")
    if sp:
        z0 = sp[0][1][2]
        zs = [c[2][2] - z0 for c in trip if c["ph"].startswith("long")]
        if zs:
            r["long_dz_range"] = [min(zs), max(zs)]
    if len(trip) < 12 and last == "quit":
        r["flags"].append("FEW_STILL_SAMPLES(%d)" % len(trip))
    return r



# --- script generator (real-time timeline: "wait" counts frames, and the hidden window runs uncapped, so a
# frame-counted script finishes inside the opening cinematic; om_delay runs a line after real seconds) ---
def timeline(intro, cycles):
    """(seconds to wait before, command line) steps of the probe session"""
    # +use during a cinematic freeze is the skip request (cl_input.c, SPEC-312 11): tap it through the intro
    t = [(3, "echo OMTEST intro")]
    for i in range(max(1, int(intro / 2.5))):
        t += [(2.3, "+use"), (0.2, "-use")]
    t += [(3, "echo OMTEST probe;heretic")]
    t += [(0.5, "echo OMP spawn;viewpos;omshot p0_start"), (0.2, "echo OMTEST stuck;echo OMP f0;viewpos;+forward"),
         (1.5, "-forward"), (0.5, "echo OMP f1;viewpos;+back"), (1.5, "-back"), (0.5, "echo OMP b1;viewpos;+moveleft"),
         (1.5, "-moveleft"), (0.5, "echo OMP l1;viewpos;+moveright"), (3.0, "-moveright"), (0.5, "echo OMP r1;viewpos")]

    def zig(n, dt):
        z = [(0, "+forward")]
        for i in range(n):
            z += [(dt, "-moveright;+moveleft"), (dt, "-moveleft;+moveright")]
        return z + [(dt, "-moveright;-moveleft;-forward")]

    def still():
        return [(0.05, "echo OMP still0;viewpos"), (1.0, "echo OMP still1;viewpos"), (2.0, "echo OMP still2;viewpos")]

    t += [(0, "echo OMTEST ice")] + zig(10, 0.2) + still() + [(0, "omshot p1_ice")]
    t += [(0, "echo OMTEST jumpwalk;+forward;+moveup"), (1.5, "-moveup;-forward")] + still()
    t += [(0, "echo OMTEST long")]
    for c in range(cycles):
        t += [(0, "+right"), (0.3 + 0.1 * (c % 3), "-right")] + zig(4, 0.15 + 0.05 * (c % 2)) + still()
        t += [(0, "+forward;+moveup"), (1.5, "-moveup;-forward")] + still()
        if c % 4 == 3:
            t += [(0, "omshot p_long%d" % c)]
    t += [(0, "echo OMTEST long end;omshot p9_end"), (1, "echo OMTEST quit"), (0, "quit")]
    return t


def make_cfg(intro, cycles):
    """aliases om_p<i>: run step i, then om_delay the wait of step i+1 into it (one om_delay pending at a time)"""
    steps = timeline(intro, cycles)
    out = ["// generated by tests/X1/xprobe.py --make (x1 probe session, black-box harness)"]
    for i, (_, cmd) in enumerate(steps):
        dt = steps[i + 1][0] if i + 1 < len(steps) else 0
        nxt = ("om_delay %.2f om_p%d" % (dt, i + 1)) if i + 1 < len(steps) else ""
        if dt == 0 and nxt:
            nxt = "om_p%d" % (i + 1)
        body = cmd + (";" + nxt if nxt else "")
        out.append('alias om_p%d "%s"' % (i, body))
    out.append("om_delay %.2f om_p0" % steps[0][0])
    return out

def main(argv):
    if argv[1] == "--make":
        # xprobe.py --make <intro seconds> <cycles> <out.cfg>
        Path(argv[4]).write_text("\n".join(make_cfg(float(argv[2]), int(argv[3]))) + "\n", encoding="ascii")
        return
    root = Path(argv[1])
    tag = argv[argv.index("--tag") + 1] if "--tag" in argv else "probe_"
    out = argv[argv.index("--json") + 1] if "--json" in argv else None
    rows = [analyse(p) for p in sorted(root.iterdir()) if p.is_dir() and p.name.startswith(tag)]
    rows.sort(key=lambda r: (-len(r["flags"]), r["map"]))
    for r in rows:
        print("%-8s exit %-4s %5.0fs still %2s slide %s zdrift %s walk %-18s %s | %s" % (
            r["map"], r["exit"], r["seconds"], r.get("still_checks", "-"), r.get("slide_while_still", "-"),
            r.get("z_drift_while_still", "-"), r.get("walk", "-"), " ".join(r["flags"]) or "ok", r.get("worst_still")))
    if out:
        with open(out, "w", encoding="utf-8") as f:
            for r in rows:
                f.write(json.dumps(r) + "\n")


if __name__ == "__main__":
    main(sys.argv)
