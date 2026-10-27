#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - black-box savegame runs: the original SoF.exe as a hidden dedicated server in the test copy
# D:\Games\SoF-test-G10 (never the oracle), single player through G10's proxy module (g10proxy.cpp; it force-sets
# deathmatch 0 / maxclients 1 before the module's Init, as G6b's spproxy), G18's headless client (game/test/sofclient.py) with a script that can
# save and load through rcon. Saves written during the run are copied to the output directory.
# Implements: SPEC-303 (save/load commands), SPEC-752 (black-box verification of the game chunks)
#
# usage: g10run.py --dll orig|om|<path> (--map tsr1 | --load FILE.sof) --tag NAME [--seconds 60]
#                  [--script "10:rcon save g10a;..."] [--set "cvar value" ...] [--cmd "console command" ...]
#                  [--port 29530]
# Output: <openmerc-data>/g10/<tag>/{cap.jsonl, sof.log, client.log, proxy.log, *.sof}

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
GAMETEST = REPO / "game" / "test"
TEST = Path(r"D:\Games\SoF-test-G10")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\g10")
OM_DLL = REPO / "build-G10-x86" / "gamex86.dll"
SP_PROXY = REPO / "build-G10-x86" / "g10proxy" / "gamex86.dll"
DASHES = "\n" + "-" * 37


def copy_retry(src, dst):
    for _ in range(50):
        try:
            shutil.copyfile(src, dst)
            return
        except PermissionError:
            time.sleep(0.2)
    raise SystemExit("cannot replace " + str(dst))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="om")
    ap.add_argument("--map", default=None)
    ap.add_argument("--load", default=None, help=".sof file to copy into the run's save directory and load")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--seconds", type=float, default=30)
    ap.add_argument("--wait", type=float, default=40)
    ap.add_argument("--script", default="")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--cmd", action="append", default=[])
    ap.add_argument("--port", type=int, default=29530)
    ap.add_argument("--name", default="G10bot")
    ap.add_argument("--skill", default="2")
    ap.add_argument("--noclient", action="store_true")
    a = ap.parse_args(argv)
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    if not a.map and not a.load:
        raise SystemExit("--map or --load")

    out = DATA / a.tag
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    user = TEST / ("u_" + a.tag.replace("/", "_"))
    if user.exists():
        shutil.rmtree(user)
    (user / "save").mkdir(parents=True)

    base = TEST / "base"
    if a.dll == "orig":
        target = "gamex86.dll.orig"
    else:
        copy_retry(OM_DLL if a.dll == "om" else Path(a.dll), base / "gamex86.om.dll")
        target = "gamex86.om.dll"
    os.environ["G10_PROXY_TARGET"] = target
    os.environ["G10_PROXY_LOG"] = str(out / "proxy.log")
    os.environ["G10_PROXY_SP"] = "1"
    os.environ["G10_PROXY_PURE"] = "0"
    copy_retry(SP_PROXY, base / "gamex86.dll")

    start = "+map %s" % a.map
    if a.load:
        name = Path(a.load).stem
        shutil.copyfile(a.load, user / "save" / (name + ".sof"))
        start = "+load %s" % name
    cfg = ["set developer 1", "set rcon_password g18", "set logfile 2", "set g18_crashlog 1", "set ip 127.0.0.1",
           "set deathmatch 0", "set maxclients 1", "set skill %s" % a.skill] + ["set " + s for s in a.set]
    (base / "g10run.cfg").write_text("\n".join(cfg) + "\n")
    args = ("+set dedicated 1 +set user ./%s +set port %d +exec g10run.cfg %s %s"
            % (user.name, a.port, start, " ".join("+" + c for c in a.cmd)))
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
        if ok and not a.noclient and srv.poll() is None:
            cmd = [sys.executable, str(GAMETEST / "sofclient.py"), "--server", "127.0.0.1:%d" % a.port,
                   "--seconds", str(a.seconds), "--out", str(out / "cap.jsonl"), "--log", str(out / "client.log"),
                   "--name", a.name, "--clientport", str(a.port + 1)]
            if a.script:
                cmd += ["--script", a.script]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 90)
            (out / "client.out").write_text(r.stdout + r.stderr, encoding="utf-8")
            print(r.stdout.strip())
        elif ok:
            time.sleep(a.seconds)
    finally:
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if log.exists():
            shutil.copyfile(log, out / "sof.log")
        for f in (user / "save").glob("*.sof"):
            shutil.copyfile(f, out / f.name)
        crash = TEST / "g18crash.log"
        if crash.exists():
            shutil.move(str(crash), str(out / "g18crash.log"))
            print("g18crash.log:", (out / "g18crash.log").read_text())
        print("server %s (dll %s), out %s, saves %s" % ("was running" if alive else "EXITED early", target, out,
                                                     sorted(p.name for p in out.glob("*.sof"))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
