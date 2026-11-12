#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc AIAUDIT - enemy reaction check (docs/impl/AIAUDIT.md): one session of the open stack runs the omaudit
# cheat command (game/src/ai/ai_audit.cpp) on every map of the list, one map after the other, then quits.
#   audit_run.py --exe <openMerc> --game <folder that holds base/pak0.pak> --home <new folder> [--maps tsr1,kos1]
#                [--timescale 3] [--settle 6] [--timeout <seconds>] [--extra "+set r_mode 4"] [--dry-run]
#   audit_run.py --make <folder> [--maps tsr1,kos1]      only writes the console scripts au_00.cfg, au_01.cfg ...
# --home is the session's own user folder (config, logs, caches, the console scripts): it must be new or empty,
# nothing else is written anywhere and the game data is only read. The console goes to <home>/console.log; when the session
# has ended the summary of audit_report.py is printed and its rows are written to <home>/audit.json.
# Each map's script loads the map with cut scenes skipped, waits --settle seconds of real time (om_delay), then
# starts omaudit at --timescale; omaudit starts the next script itself (cvar om_audit_next), the last one quits.
# The session is started with -portable, its own ports (29720 / 29721, docs/process/testing.md), no sound, in a
# window, and is killed after --timeout seconds (default 480 for each map). No input is sent to the desktop.
# Implements: black-box test harness (no game code)
import argparse
import subprocess
import sys
import time
from pathlib import Path

# the 32 single-player maps, in the order of tests/X1/xsweep.ps1
MAPS = ["tut1", "tsr1", "tsr2", "trn1", "kos1", "kos2", "kos3", "nyc1", "nyc2", "nyc3", "sib1", "sib2", "sib3",
        "irq1a", "irq1b", "irq2a", "irq2b", "irq3a", "irq3b", "sud1", "sud2", "sud3", "jpn1", "jpn2", "jpn3",
        "ger1", "ger2", "ger3", "ger4", "arm1", "arm2", "arm3"]
PORT = 29720


def make(out, maps, timescale, settle):
    out.mkdir(parents=True, exist_ok=True)
    for n, m in enumerate(maps):
        nxt = "exec au_%02d.cfg" % (n + 1) if n + 1 < len(maps) else "quit"
        text = (
            "// AIAUDIT: omaudit on %s (written by tests/AIAUDIT/audit_run.py). Needs +set cheats 1.\n"
            "// No semicolons in comments: the command buffer splits lines at them.\n"
            'alias w10 "wait;wait;wait;wait;wait;wait;wait;wait;wait;wait"\n'
            'alias om_au_go "set sv_jumpcinematic 0;set timescale %g;omaudit"\n'
            "killmenu\n"
            "set timescale 1\n"
            "set sv_jumpcinematic 1\n"
            'set om_audit_next "%s"\n'
            "echo OMTEST audit %s\n"
            "map %s\n"
            "w10\n"
            "om_delay %g om_au_go\n" % (m, timescale, nxt, m, m, settle))
        with open(out / ("au_%02d.cfg" % n), "w", encoding="ascii", newline="\n") as f:
            f.write(text)
    return len(maps)


def main():
    ap = argparse.ArgumentParser(description="enemy reaction check of single-player maps (docs/impl/AIAUDIT.md)")
    ap.add_argument("--exe", help="the openMerc client binary")
    ap.add_argument("--game", help="folder that holds base/pak0.pak (default: the engine's own search)")
    ap.add_argument("--home", help="new or empty folder for this session's user files and logs")
    ap.add_argument("--make", metavar="DIR", help="only write the console scripts into DIR")
    ap.add_argument("--maps", default="", help="comma-separated map names (default: all 32 single-player maps)")
    ap.add_argument("--timescale", type=float, default=3.0, help="game speed during the check (default 3)")
    ap.add_argument("--settle", type=float, default=6.0, help="seconds between the map load and omaudit (default 6)")
    ap.add_argument("--timeout", type=float, default=0, help="seconds for the whole session (default 480 per map)")
    ap.add_argument("--extra", default="", help="more engine arguments, e.g. \"+set r_mode 4\"")
    ap.add_argument("--dry-run", action="store_true", help="write the scripts, print the command, start nothing")
    a = ap.parse_args()

    maps = [m for m in a.maps.replace(" ", ",").split(",") if m] or MAPS
    if a.make:
        print("%d console scripts in %s" % (make(Path(a.make), maps, a.timescale, a.settle), a.make))
        return 0
    if not a.exe or not a.home:
        ap.error("--exe and --home are needed (or --make)")
    exe, home = Path(a.exe).resolve(), Path(a.home).resolve()
    if not exe.is_file():
        sys.exit("no such binary: %s" % exe)
    if home.exists() and any(home.iterdir()):
        sys.exit("--home must be new or empty (nothing is overwritten): %s" % home)
    if a.game and not (Path(a.game) / "base" / "pak0.pak").is_file():
        sys.exit("no base/pak0.pak in %s" % a.game)
    make(home, maps, a.timescale, a.settle)

    cmd = [str(exe), "-portable"]
    if a.game:
        # +set basedir, not -basedir: 0.2.3 looks for the game data before it reads the -basedir switch
        cmd += ["+set", "basedir", str(Path(a.game).resolve())]
    cmd += ["+set", "user", str(home), "+set", "om_noimport", "1", "+set", "cheats", "1",
            "+set", "port", str(PORT), "+set", "clientport", str(PORT + 1),
            "+set", "vid_fullscreen", "0", "+set", "s_volume", "0", "+set", "s_musicvolume", "0",
            "+set", "logfile", "2"]
    cmd += a.extra.split()
    cmd += ["+exec", "au_00.cfg"]
    timeout = a.timeout or 480.0 * len(maps)
    print("maps: %d (%s)" % (len(maps), " ".join(maps)))
    print("command: " + " ".join('"%s"' % c if " " in c else c for c in cmd))
    if a.dry_run:
        return 0

    log = home / "console.log"
    sys.stdout.flush()
    t0 = time.time()
    with open(log, "wb") as out:
        p = subprocess.Popen(cmd, cwd=str(exe.parent), stdout=out, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        try:
            code = p.wait(timeout=timeout)
            end = "exit code %d" % code
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()
            code = 1
            end = "KILLED after the %d s time limit" % timeout
    print("session: %s, %.0f s" % (end, time.time() - t0))

    # the console as the engine wrote it (logfile 2) when the standard output carries none (Windows GUI binary)
    logs = [log] + sorted(q for q in home.rglob("*.log") if q != log)
    src = next((q for q in logs if b"OMAUDIT " in q.read_bytes()), log)
    report = Path(__file__).resolve().parent / "audit_report.py"
    sys.stdout.flush()
    r = subprocess.call([sys.executable, "-I", str(report), str(src), "--json", str(home / "audit.json"),
                         "--maps", ",".join(maps)])
    return code or r


if __name__ == "__main__":
    sys.exit(main())
