#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - black-box runs of a game module in the original engine, after G2's harness
# (game/src/ai/tools/airun.py: hidden dedicated server, optional single-player shim, headless client recording what
# it receives) pointed at the G4 test copy D:\Games\SoF-test-G4, the G4 ports 29470/29471 and <openmerc-data>/g4/<tag>/.
# Developer mode: `--dev 1` (default) sets it from the cfg as G2/G18 do (`+set developer 1` on the command line
# opens a modal "Developer Mode" box that blocks the hidden server; measured 2026-10-25: with developer 0 the
# original dedicated server stops after "Server Initialization" and never spawns the map). Our module's test trace
# (`rg_trace`) prints with either setting. Ports: server 29470, client 29471 (G4's base, testing notes
# "Network ports"); g4client.py warns when a run received a second server's connect.
# Implements: SPEC-730, SPEC-731, SPEC-732 (black-box verification against the original game module)
#
# usage: g4sp.py --dll orig|om|<path to gamex86.dll> --map trn1 --tag NAME [--sp] [--seconds 30] [--script ...]
#                [--set "cvar value"] ...
#        (--sp: single player through spshim; without it the dedicated server runs the map in deathmatch)
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "game" / "src" / "ai" / "tools"))
import airun  # noqa: E402

TEST = Path(r"D:\Games\SoF-test-G4")
DATA = Path(r"C:\openmerc-data\g4")
OM_DLL = REPO / "build-G4-x86" / "gamex86.dll"
CLIENT = HERE / "g4client.py"  # sofclient.py on the package's fixed client port
DASHES = "\n" + "-" * 37
airun.TEST, airun.DATA, airun.OM_DLL = TEST, DATA, OM_DLL


def kill_own():
    subprocess.run(["powershell", "-NoProfile", "-Command",
                    "Get-Process SoF -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "
                    r"'D:\Games\SoF-test-G4\*' } | Stop-Process -Force"], capture_output=True)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="om")
    ap.add_argument("--map", default="trn1")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--sp", action="store_true")
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--wait", type=float, default=60)
    ap.add_argument("--script", default="")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--port", type=int, default=29470)  # G4 base port (testing notes "Network ports")
    ap.add_argument("--name", default="G4bot")
    ap.add_argument("--dev", default="1")
    a = ap.parse_args(argv)
    if not any(s.startswith("sv_pmodlistfile") for s in a.set):
        a.set.append("sv_pmodlistfile ghoul/pmodels/g4one")  # fewer player-model sounds (SP maps)
    out = DATA / a.tag
    out.mkdir(parents=True, exist_ok=True)
    user = TEST / ("u_" + a.tag.replace("/", "_"))
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    src = airun.install(a.dll, a.sp)
    cfg = TEST / "base" / ("g4_%s.cfg" % a.tag.replace("/", "_"))
    cfg.write_text("\n".join(["set developer %s" % a.dev] + ["set %s" % s for s in a.set] + ["map %s" % a.map])
                   + "\n", encoding="latin-1")
    args = ("+set dedicated 1 +set user ./%s +set logfile 2 +set ip 127.0.0.1 +set port %d "
            "+exec %s" % (user.name, a.port, cfg.name))
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE: never shown, never focused
    srv = subprocess.Popen([str(TEST / "SoF.exe")] + args.split(), cwd=str(TEST), startupinfo=si)
    log = user / "sof.log"
    t0 = time.time()
    ok = False
    while time.time() - t0 < a.wait:
        if srv.poll() is not None:
            break
        if log.exists():
            txt = log.read_text(encoding="latin-1", errors="replace")
            i = txt.find("SpawnServer")
            if i >= 0 and (DASHES in txt[i:] or "Error" in txt[i:]):
                time.sleep(1.0)
                ok = True
                break
        time.sleep(0.25)
    try:
        if ok and srv.poll() is None:
            cmd = [sys.executable, str(CLIENT), "--clientport", str(a.port + 1),
                   "--server", "127.0.0.1:%d" % a.port, "--seconds", str(a.seconds),
                   "--out", str(out / "cap.jsonl"), "--log", str(out / "client.log"), "--name", a.name]
            if a.script:
                cmd += ["--script", a.script]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 90)
            (out / "client.out").write_text(r.stdout + r.stderr, encoding="utf-8")
            print(r.stdout.strip())
    finally:
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if log.exists():
            shutil.copyfile(log, out / "sof.log")
        print("server %s (dll %s), log %s" % ("was running" if alive else "EXITED early", src.name, out / "sof.log"))
    return 0 if ok else 1


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    finally:
        kill_own()
