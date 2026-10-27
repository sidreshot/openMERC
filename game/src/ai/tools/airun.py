#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G2 - black-box runs of a game module in the original engine, for the AI package.
# Based on G18's harness (game/test/runtest.py, sofclient.py): the original SoF.exe runs as a hidden dedicated
# server (SW_HIDE, never focused) in the G2 test copy D:\Games\SoF-test-G2 with the chosen gamex86.dll, the
# headless client connects and records what it receives. For single-player maps (--sp) the module under test is
# loaded through spshim.dll (tools/spshim.cpp), which sets deathmatch 0 after the dedicated server has forced it to 1.
# Implements: SPEC-700, SPEC-701, SPEC-710, SPEC-711 (black-box verification harness)
#
# usage: airun.py --dll orig|om|<path> --map tsr1 --tag NAME [--sp] [--seconds 60] [--script ...] [--set "cvar v"]
# Output: <openmerc-data>/g2/<tag>/{cap.jsonl, sof.log, client.log, client.out}

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
TEST = Path(r"D:\Games\SoF-test-G2")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\g2")
OM_DLL = REPO / "build-G2-x86" / "gamex86.dll"
# --engine om: OpenMerc's own dedicated server (the owner plays the fully open stack: our engine + our modules);
# -portable, data under openmerc-data (docs/process/testing.md "Running games on the owner's PC")
# build-x86-Release/release is a plain Yamagi build of 2026-10-22 (346 KB, no -basedir): the newest engine build of
# the repository is used unless --ded names one
OM_DED = max(REPO.glob("build-*/release/openMerc-ded.exe"), key=lambda f: f.stat().st_mtime, default=None)
CLIENT = REPO / "game" / "test" / "sofclient.py"
DASHES = "\n" + "-" * 37


SHIM = REPO / "build-G2-x86" / "spshim.dll"


def copy(src, dst):
    for _ in range(50):
        try:
            shutil.copyfile(src, dst)
            return
        except PermissionError:
            time.sleep(0.2)
    raise SystemExit("cannot replace " + str(dst))


def install(dll, sp):
    """sp: base/gamex86.dll = spshim.dll (deathmatch 0), the module under test as base/gamex86_real.dll."""
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    src = {"orig": TEST / "base" / "gamex86.dll.orig", "om": OM_DLL}.get(dll, Path(dll))
    if sp:
        copy(SHIM, TEST / "base" / "gamex86.dll")
        copy(src, TEST / "base" / "gamex86_real.dll")
    else:
        copy(src, TEST / "base" / "gamex86.dll")
    return src


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="om")
    ap.add_argument("--map", default="tsr1")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--sp", action="store_true", help="single player: deathmatch 0, maxclients 1")
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--wait", type=float, default=40)
    ap.add_argument("--script", default="")
    ap.add_argument("--set", action="append", default=[])
    # G2's ports (docs/process/testing.md "Network ports"): base 29460; the server uses --port and its
    # client socket --port + 1; the headless client binds --port + 5; a second test copy (--copy b) runs with
    # --port 29462 (client 29467)
    ap.add_argument("--port", type=int, default=29460)
    ap.add_argument("--developer", type=int, default=1,
                    help="developer cvar, set from the exec'd cfg (never on the command line: a modal box)")
    ap.add_argument("--name", default="G2bot")
    ap.add_argument("--copy", default="", help="test copy suffix: D:/Games/SoF-test-G2<suffix> (parallel runs)")
    ap.add_argument("--engine", choices=["orig", "om"], default="orig",
                    help="orig: the original SoF.exe as dedicated server; om: OpenMerc's openMerc-ded.exe")
    ap.add_argument("--route", default="", help="flythrough waypoints (aiclient.py; see aifly.py)")
    ap.add_argument("--route-start", type=float, default=0.0)
    ap.add_argument("--speed", type=int, default=400)
    ap.add_argument("--keep-noclip", action="store_true", help="flythrough: stay in noclip while dwelling")
    ap.add_argument("--ded", default="", help="--engine om: the openMerc-ded.exe to run (default: the newest build-*/release)")
    ap.add_argument("--mc1", action="store_true",
                    help="spshim forces maxclients 1 before Init (needed for `rcon save`; entity numbers shift)")
    a = ap.parse_args(argv)
    global TEST, OM_DED
    if a.ded:
        OM_DED = Path(a.ded)
    if a.copy:
        TEST = Path(str(TEST) + a.copy)

    out = DATA / a.tag
    out.mkdir(parents=True, exist_ok=True)
    user = TEST / ("u_" + a.tag.replace("/", "_"))
    if a.engine == "om":
        user = out / "user"
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    src = install(a.dll, a.sp)
    # developer 1 on the command line makes SoF.exe show a "Developer Mode" message box (SPEC-301 Behaviour 1 step 7)
    # that blocks the hidden server; set inside the exec'd cfg it does not (docs/process/testing.md)
    # rcon (password g18, sofclient.py's script action `rcon`) for saves and loads; the server listens on 127.0.0.1
    cfg_lines = ["set developer %d" % a.developer, "set rcon_password g18"] + ["set %s" % s for s in a.set]
    cfg_lines.append("map %s" % a.map)
    cfg = TEST / "base" / ("g2_%s.cfg" % a.tag.replace("/", "_"))
    cfg.write_text("\n".join(cfg_lines) + "\n", encoding="latin-1")
    args = ("+set dedicated 1 +set user ./%s +set logfile 2 +set ip 127.0.0.1 +set port %d "
            "+set clientport %d +exec %s" % (user.name, a.port, a.port + 1, cfg.name))
    exe = [str(TEST / "SoF.exe")]
    if a.engine == "om":
        exe = [str(OM_DED), "-portable", "-basedir", str(TEST), "-user", str(user)]
        args = ("+set dedicated 1 +set logfile 2 +set ip 127.0.0.1 +set port %d +set clientport %d +exec %s"
                % (a.port, a.port + 1, cfg.name))
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE: never shown, never focused
    env = dict(os.environ)
    env["G2_SHIM_MAXCLIENTS1"] = "1" if a.mc1 else "0"
    # openMerc-ded polls its console input: it needs a console of its own (hidden by SW_HIDE), else "Error getting # of
    # console events"
    srv = subprocess.Popen(exe + args.split(), cwd=str(TEST if a.engine == "orig" else OM_DED.parent),
                           startupinfo=si, env=env,
                           creationflags=subprocess.CREATE_NEW_CONSOLE if a.engine == "om" else 0)
    log = user / "sof.log"
    t0 = time.time()
    ok = False
    while time.time() - t0 < a.wait:
        if srv.poll() is not None:
            break
        if log.exists():
            txt = log.read_text(encoding="latin-1", errors="replace")
            # "SpawnServer" is a developer-only print; the init banner is always there
            i = txt.find("Server Initialization")
            if i >= 0 and (DASHES in txt[i + 25:] or "Error" in txt[i:]):
                time.sleep(1.0)
                ok = True
                break
        time.sleep(0.25)
    try:
        if ok and srv.poll() is None:
            cmd = [sys.executable, str(CLIENT), "--server", "127.0.0.1:%d" % a.port, "--seconds", str(a.seconds),
                   "--out", str(out / "cap.jsonl"), "--log", str(out / "client.log"), "--name", a.name,
                   "--clientport", str(a.port + 5)]
            if a.script:
                cmd += ["--script", a.script]
            if a.route:
                cmd[1] = str(HERE / "aiclient.py")
                cmd += ["--route", a.route, "--route-start", str(a.route_start), "--speed", str(a.speed)]
                if a.keep_noclip:
                    cmd += ["--keep-noclip"]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 60)
            (out / "client.out").write_text(r.stdout + r.stderr, encoding="utf-8")
            print(r.stdout.strip())
    finally:
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if log.exists():
            shutil.copyfile(log, out / "sof.log")
        for f in (user / "save").glob("*.sof"):
            shutil.copyfile(f, out / f.name)
        print("server %s (%s, dll %s), log %s" % ("was running" if alive else "EXITED early",
                                                   a.engine if a.engine == "orig" else str(OM_DED), src.name,
                                                   out / "sof.log"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
