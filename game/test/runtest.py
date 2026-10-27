#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - run the original SoF.exe as a hidden dedicated server with a chosen gamex86.dll
# in the test copy (never the oracle), connect the headless client, collect capture and server log.
# Implements: SPEC-001 (black-box test of the game module through the original engine)
#
# usage: runtest.py --dll orig|om|<path> --map dm/jpndm2 --tag NAME [--dm 1] [--seconds 20 (after begin)]
#                   [--script "..."] [--set "cvar value" ...] [--cmd "command" ...] [--noclient] [--sp]
#                   [--script2 "..."]
# Output: <openmerc-data>/g18/<tag>/{cap.jsonl, sof.log, client.log}

import argparse
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
TEST = Path(r"D:\Games\SoF-test-G18")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\g18")
OM_DLL = REPO / "build-game-x86-Release" / "gamex86.dll"
SP_PROXY = REPO / "build-G6b-x86" / "spproxy" / "gamex86.dll"
RF_PROXY = REPO / "build-G18-x86" / "g18rfproxy.dll"
DASHES = "\n" + "-" * 37


def install(dll):
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    src = {"orig": TEST / "base" / "gamex86.dll.orig", "om": OM_DLL}.get(dll, Path(dll))
    dst = TEST / "base" / "gamex86.dll"
    for _ in range(50):
        try:
            shutil.copyfile(src, dst)
            return src
        except PermissionError:
            time.sleep(0.2)
    raise SystemExit("cannot replace " + str(dst))


def kill_servers():
    subprocess.run(["taskkill", "/F", "/IM", "SoF.exe", "/FI", "MODULES eq gamex86.dll"],
                   capture_output=True)


def port_free(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.bind(("127.0.0.1", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="om")
    ap.add_argument("--map", default="dm/jpndm2")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--dm", default="1")
    ap.add_argument("--maxclients", default="8")
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--wait", type=float, default=25, help="max seconds to wait for the server")
    ap.add_argument("--script", default="")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--cmd", action="append", default=[], help="console command after +map (e.g. 'bot_add 4 Kirt')")
    ap.add_argument("--noclient", action="store_true")
    # the package's port base (docs/process/testing.md "Network ports"): server base, clients base+1, +2;
    # other packages pass their own base. Never the default 27910.
    ap.add_argument("--port", type=int, default=29450)
    ap.add_argument("--name", default="G18bot")
    ap.add_argument("--script2", default=None, help="run a second client with this script ('' = idle)")
    ap.add_argument("--name2", default="G18two")
    ap.add_argument("--ui", action="append", default=[], help="userinfo KEY=VALUE of the client (empty removes)")
    ap.add_argument("--ui2", action="append", default=[], help="userinfo KEY=VALUE of the second client")
    ap.add_argument("--sp", action="store_true",
                    help="single player: G6b's proxy module (build-G6b-x86/spproxy) loads the DLL under test and "
                         "force-sets deathmatch 0 / maxclients 1 before its Init")
    ap.add_argument("--rfproxy", action="store_true",
                    help="load the DLL under test through game/probe/rfproxy (logs every RunFrame to rf.log)")
    a = ap.parse_args(argv)

    out = DATA / a.tag
    out.mkdir(parents=True, exist_ok=True)
    user = TEST / ("u_" + a.tag.replace("/", "_"))
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    if a.sp:
        # the dedicated server forces deathmatch; the proxy (G6b test code) is installed as base/gamex86.dll
        base = TEST / "base"
        if a.dll == "orig":
            target = "gamex86.dll.orig"
        else:
            shutil.copyfile(OM_DLL if a.dll == "om" else Path(a.dll), base / "gamex86.om.dll")
            target = "gamex86.om.dll"
        os.environ["G6B_PROXY_TARGET"] = target
        os.environ["G6B_PROXY_LOG"] = str(out / "proxy.log")
        src = install(str(SP_PROXY))
        src = Path(target)
        a.dm = "0"
        a.maxclients = "1"
    elif a.rfproxy:
        base = TEST / "base"
        if a.dll == "orig":
            target = "gamex86.dll.orig"
        else:
            shutil.copyfile(OM_DLL if a.dll == "om" else Path(a.dll), base / "gamex86.rft.dll")
            target = "gamex86.rft.dll"
        os.environ["G18_RF_TARGET"] = target
        os.environ["G18_RF_LOG"] = str(out / "rf.log")
        install(str(RF_PROXY))
        src = Path(target)
    else:
        src = install(a.dll)
    # The run's settings go through an exec'd config: SoF.exe crashes with more than 50 command-line words, and
    # "+set developer 1" on the command line opens a modal "Developer Mode" box that blocks a hidden server
    # (seen 2026-10-22); from a config it does not.
    cfg = ["set developer 1", "set rcon_password g18", "set logfile 2", "set g18_crashlog 1", "set ip 127.0.0.1",
           "set deathmatch %s" % a.dm, "set maxclients %s" % a.maxclients] + ["set " + s for s in a.set]
    (TEST / "base" / "g18run.cfg").write_text("\n".join(cfg) + "\n")
    args = ("+set dedicated 1 +set user ./%s +set port %d +exec g18run.cfg +map %s %s"
            % (user.name, a.port, a.map, " ".join("+" + c for c in a.cmd)))
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
            cmd = [sys.executable, str(HERE / "sofclient.py"), "--server", "127.0.0.1:%d" % a.port,
                   "--seconds", str(a.seconds), "--out", str(out / "cap.jsonl"), "--log",
                   str(out / "client.log"), "--name", a.name, "--clientport", str(a.port + 1)]
            if a.script:
                cmd += ["--script", a.script]
            for u in a.ui:
                cmd += ["--ui", u]
            second = None
            if a.script2 is not None:
                # a second client in parallel (cap2.jsonl, client2.log); its script times count from its own begin
                cmd2 = [sys.executable, str(HERE / "sofclient.py"), "--server", "127.0.0.1:%d" % a.port,
                        "--seconds", str(a.seconds), "--out", str(out / "cap2.jsonl"), "--log",
                        str(out / "client2.log"), "--name", a.name2, "--clientport", str(a.port + 2)]
                if a.script2:
                    cmd2 += ["--script", a.script2]
                for u in a.ui2:
                    cmd2 += ["--ui", u]
                second = subprocess.Popen(cmd2, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
                time.sleep(0.5)
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 60)
            if second:
                o2, _ = second.communicate(timeout=a.seconds + 60)
                (out / "client2.out").write_text(o2 or "", encoding="utf-8")
                print("client 2:", (o2 or "").strip())
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
        crash = TEST / "g18crash.log"
        if crash.exists():
            shutil.move(str(crash), str(out / "g18crash.log"))
            print("g18crash.log:", (out / "g18crash.log").read_text())
        print("server %s (dll %s), log %s" % ("was running" if alive else "EXITED early", src.name,
                                               out / "sof.log"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
