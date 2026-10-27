#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - one-line summary of an xrun.ps1 run directory: exit code (from run.txt), crash reports
# (watchdog.txt, g18crash.log), console errors and warnings (distinct lines with counts), captures, sound.
#   xsummary.py <run dir> [--json]          one run
#   xsummary.py --diff <dir A> <dir B>      error/warning lines only in A, only in B
# Implements: black-box test harness (no game code)
import json
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import wavstat  # noqa: E402

# what counts as an error or warning line in the console log
PAT = re.compile(r"error|fatal|couldn't|can't|cannot|failed|warning|not found|overflow|dumped|missing|illegible|"
                 r"bad |invalid|unknown|no such|exceeded|out of|crash|stall|disconnect", re.I)
# start-up noise of the console that is not about the game (renderer probing, Quake II messages)
NOISE = re.compile(r"^sv_watchdog:|^ - Mode|pics/misc/conback|^SDL |GL_EXT|joystick|feedback|Force Feedback")


def norm(line):
    # numbers that vary between runs (addresses, times) are kept: the lines are short and few
    return line.strip()


def issues(log):
    c = Counter()
    if not log.exists():
        return c
    for raw in log.read_text(encoding="cp1251", errors="replace").splitlines():
        if NOISE.search(raw):
            continue
        if PAT.search(raw):
            c[norm(raw)] += 1
    return c


def summary(d):
    d = Path(d)
    r = {"dir": str(d), "map": d.name.split("_", 1)[-1]}
    run = d / "run.txt"
    if run.exists():
        t = run.read_text(errors="replace")
        m = re.search(r"exit code (-?\d+) after ([\d,\.]+) s", t)
        if m:
            r["exit"] = int(m.group(1))
            r["seconds"] = float(m.group(2).replace(",", "."))
        m = re.search(r"(\d+) capture", t)
        r["captures"] = int(m.group(1)) if m else 0
    crash = []
    wd = d / "user" / "watchdog.txt"
    if wd.exists():
        for ln in wd.read_text(errors="replace").splitlines():
            if re.search(r"crash:|stall:|exception|module", ln, re.I):
                crash.append(ln.strip())
    gc = d / "g18crash.log"
    if gc.exists() and gc.stat().st_size:
        crash += ["g18crash: " + ln.strip() for ln in gc.read_text(errors="replace").splitlines()[:6]]
    r["crash"] = crash[:12]
    iss = issues(d / "client.log")
    r["issues"] = dict(iss.most_common())
    r["n_issue_lines"] = sum(iss.values())
    r["sound"] = wavstat.stat(d / "snd.wav") if (d / "snd.wav").exists() else {"ok": False, "why": "missing"}
    return r


def line(r):
    s = r["sound"]
    snd = ("snd %d/%d" % (s["sound_windows"], s["windows"])) if s.get("ok") else "snd -"
    crash = (" CRASH: " + " | ".join(r["crash"][:3])) if r["crash"] else ""
    top = "; ".join("%s%s" % (k[:90], (" x%d" % v) if v > 1 else "") for k, v in list(r["issues"].items())[:4])
    return "%-6s exit %s %4.1fs caps %d %s issues %d%s%s" % (
        r["map"], r.get("exit", "?"), r.get("seconds", 0), r.get("captures", 0), snd, r["n_issue_lines"], crash,
        (" LOG: " + top) if top else "")


def main(argv):
    if len(argv) >= 4 and argv[1] == "--diff":
        a, b = issues(Path(argv[2]) / "client.log"), issues(Path(argv[3]) / "client.log")
        for k in sorted(set(a) - set(b)):
            print("only A: %s (x%d)" % (k, a[k]))
        for k in sorted(set(b) - set(a)):
            print("only B: %s (x%d)" % (k, b[k]))
        return 0
    r = summary(argv[1])
    print(json.dumps(r, ensure_ascii=False) if "--json" in argv else line(r))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
