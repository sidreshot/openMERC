#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc AIAUDIT - enemy reaction check (docs/impl/AIAUDIT.md): one session of the open stack runs the omaudit
# cheat command (game/src/ai/ai_audit.cpp) on every map of the list, one map after the other, then quits.
#   audit_run.py --exe <openMerc> --game <folder that holds base/pak0.pak> --home <new folder> [--maps tsr1,kos1]
#                [--timescale 3] [--settle 6] [--timeout <seconds>] [--start-limit 60] [--quiet-limit 120]
#                [--max-sessions N] [--extra "+set r_mode 4"] [--dry-run]
#   audit_run.py --make <folder> [--maps tsr1,kos1]      only writes the console scripts au_00.cfg, au_01.cfg ...
# --home is the session's own user folder (config, logs, caches, the console scripts): it must be new or empty,
# nothing else is written anywhere and the game data is only read. The engine's log is <home>/sof.log (logfile 2),
# its standard output <home>/console.log; when the last session has ended the summary of audit_report.py is printed
# and its rows are written to <home>/audit.json.
# Each map's script loads the map with cut scenes skipped, waits --settle seconds of real time (om_delay), then
# starts omaudit at --timescale; omaudit starts the next script itself (cvar om_audit_next), the last one quits.
# The harness follows sof.log while the session runs. A map whose check does not start within --start-limit
# seconds (a level that ends itself first, as the cut-scene level tsr2 does), a check that prints nothing for
# --quiet-limit seconds, or a session that ends in the middle (a crash) does not end the run: the session is
# closed, the map is noted as skipped (an OMNOTE line in sof.log) and a new session goes on with the maps after it.
# --max-sessions 1 stops at the first such map instead and prints the maps that are left.
# Every session is started with -portable, its own ports (29720 / 29721, docs/process/testing.md), no sound, in a
# window; all of them together get --timeout seconds (default 480 for each map). No input is sent to the desktop.
# Exit status: 0 when every map of the list was checked, 1 otherwise.
# Implements: black-box test harness (no game code)
import argparse
import subprocess
import sys
import time
from pathlib import Path

# the single-player maps, in the order of tests/X1/xsweep.ps1, without tsr2: that level is a cut scene whose own
# script ends it a few seconds after the load, before a check can start (name it with --maps to see that)
MAPS = ["tut1", "tsr1", "trn1", "kos1", "kos2", "kos3", "nyc1", "nyc2", "nyc3", "sib1", "sib2", "sib3",
        "irq1a", "irq1b", "irq2a", "irq2b", "irq3a", "irq3b", "sud1", "sud2", "sud3", "jpn1", "jpn2", "jpn3",
        "ger1", "ger2", "ger3", "ger4", "arm1", "arm2", "arm3"]
PORT = 29720


def make(out, maps, timescale, settle):
    out.mkdir(parents=True, exist_ok=True)
    for old in out.glob("au_[0-9][0-9].cfg"):
        old.unlink()
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


def watch(p, log, pos, maps, start_limit, quiet_limit, deadline):
    """Follows the engine's log from byte pos while session p runs.
    Returns (checked maps, the map the session stopped at or None, why, time limit reached)."""
    done, cur, started = [], None, False
    mark = time.time()  # the last sign of progress
    buf = b""
    while True:
        ended = p.poll() is not None
        if log.is_file():
            with open(log, "rb") as f:
                f.seek(pos)
                chunk = f.read()
            pos += len(chunk)
            lines = (buf + chunk).split(b"\n")
            buf = lines.pop()
            for raw in lines:
                ln = raw.decode("latin-1")
                i = ln.find("OMTEST audit ")
                if i >= 0 and ln[i + 13:].split():
                    cur, started, mark = ln[i + 13:].split()[0], False, time.time()
                    continue
                if "OMWHY" in ln or "OMTRACE " in ln or "OMNOTE " in ln:
                    mark = time.time()
                    continue
                i = ln.find("OMAUDIT ")
                w = ln[i:].split() if i >= 0 else []
                if len(w) < 3 or cur is None:
                    continue
                mark = time.time()
                if w[2] == "start":
                    if w[1] != cur:
                        return done, cur, "the level changed to %s before its check started" % w[1], False
                    started = True
                elif w[2] == "done" and w[1] == cur:
                    done.append(cur)
                    cur, started = None, False
        at = cur if cur is not None else (maps[len(done)] if len(done) < len(maps) else None)
        if ended:
            if at is None:
                return done, None, "", False
            return done, at, "the session ended during it (exit code %d)" % p.returncode, False
        now = time.time()
        if now > deadline:
            return done, at, "the time limit of the run was reached", True
        if at is None:
            if now - mark > start_limit:  # every map is done and the session does not quit
                return done, None, "", False
        elif not started and now - mark > start_limit:
            return done, at, "its check did not start within %d s" % start_limit, False
        elif started and now - mark > quiet_limit:
            return done, at, "its check printed nothing for %d s" % quiet_limit, False
        time.sleep(0.5)


def stop(p):
    if p.poll() is None:
        p.terminate()
        try:
            p.wait(timeout=10)
        except subprocess.TimeoutExpired:
            p.kill()
            p.wait()


def main():
    ap = argparse.ArgumentParser(description="enemy reaction check of single-player maps (docs/impl/AIAUDIT.md)")
    ap.add_argument("--exe", help="the openMerc client binary")
    ap.add_argument("--game", help="folder that holds base/pak0.pak (default: the engine's own search)")
    ap.add_argument("--home", help="new or empty folder for this run's user files and logs")
    ap.add_argument("--make", metavar="DIR", help="only write the console scripts into DIR")
    ap.add_argument("--maps", default="", help="comma-separated map names (default: the single-player maps "
                    "without the cut-scene level tsr2)")
    ap.add_argument("--timescale", type=float, default=3.0, help="game speed during the check (default 3)")
    ap.add_argument("--settle", type=float, default=6.0, help="seconds between the map load and omaudit (default 6)")
    ap.add_argument("--timeout", type=float, default=0, help="seconds for the whole run (default 480 per map)")
    ap.add_argument("--start-limit", type=float, default=60.0,
                    help="seconds a map has to load and start its check (default 60)")
    ap.add_argument("--quiet-limit", type=float, default=120.0,
                    help="seconds a running check may print nothing (default 120)")
    ap.add_argument("--max-sessions", type=int, default=0,
                    help="engine sessions to start at most (default: as many as needed; 1 = never start again)")
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

    log, out_log = home / "sof.log", home / "console.log"
    t0 = time.time()
    left, checked, skipped, sessions, code = list(maps), [], [], 0, 0
    while left:
        sessions += 1
        make(home, left, a.timescale, a.settle)
        pos = log.stat().st_size if log.is_file() else 0
        sys.stdout.flush()
        ts = time.time()
        with open(out_log, "ab") as out:
            p = subprocess.Popen(cmd, cwd=str(exe.parent), stdout=out, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL)
            done, at, why, late = watch(p, log, pos, left, a.start_limit, a.quiet_limit, t0 + timeout)
            closed = p.poll() is None
            stop(p)
        checked += done
        print("session %d: %d maps checked, %s, %.0f s" % (
            sessions, len(done), "closed by the harness" if closed else "exit code %d" % p.returncode,
            time.time() - ts))
        if at is None:
            code = code or (0 if closed else p.returncode)
            left = []
            break
        skipped.append(at)
        note = "OMNOTE %s audit_run: %s. Not checked" % (at, why)
        print("  " + note[7:])
        with open(log, "a", encoding="ascii", errors="replace") as f:
            f.write(note + "\n")
        left = left[left.index(at) + 1:]
        if late or (a.max_sessions and sessions >= a.max_sessions):
            break
    if left:
        print("not run: %s\n  to go on: the same command with a new --home and --maps %s" % (
            " ".join(left), ",".join(left)))
    print("run: %d of %d maps checked in %d session%s, %.0f s" % (
        len(checked), len(maps), sessions, "" if sessions == 1 else "s", time.time() - t0))

    # the engine's own log holds every session; the standard output is the fallback (logfile off by --extra)
    src = next((q for q in (log, out_log) if q.is_file() and b"OMAUDIT " in q.read_bytes()), log)
    report = Path(__file__).resolve().parent / "audit_report.py"
    sys.stdout.flush()
    r = subprocess.call([sys.executable, "-I", str(report), str(src), "--json", str(home / "audit.json"),
                         "--maps", ",".join(maps)] + (["--banner", str(out_log)] if src != out_log else []))
    return 1 if (skipped or left or code or r) else 0


if __name__ == "__main__":
    sys.exit(main())
