#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G3 - deathmatch black-box runs with one or more headless clients (G18's game/test/sofclient.py): the
# original SoF.exe runs as a hidden dedicated server (SW_HIDE, never focused; developer 1 from an exec'd cfg) in G3's test copy
# D:\Games\SoF-test-G3 with the original or the OpenMerc gamex86.dll; every client records its own capture.
# Implements: SPEC-720, SPEC-721, SPEC-722 (black-box verification harness)
#
# usage: g3dm.py --dll orig|om --map dm/jpndm2 --tag NAME [--seconds 20] [--set "cvar v"]...
#                [--violence N] [--sp] --client "name|script" [--client "name|script"]...
# --violence N / --sp load the module under test behind g3shim.dll (lock cvars it reads = bits of N; deathmatch 0).
# Output: <openmerc-data>/g3/<tag>/{cap_<name>.jsonl, client_<name>.log, sof.log}
import argparse
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
TEST = Path(r"D:\Games\SoF-test-G3")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\g3")
OM_DLL = REPO / "build-G3-x86" / "gamex86.dll"
SHIM = REPO / "build-G3-x86" / "g3shim.dll"   # build_g3shim.cmd
CLIENT = REPO / "game" / "test" / "sofclient.py"
DASHES = "\n" + "-" * 37


def copy(src, dst):
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
    ap.add_argument("--map", default="dm/jpndm2")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--dm", default="1")
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--wait", type=float, default=40)
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--client", action="append", default=[])
    ap.add_argument("--port", type=int, default=29510)  # G3 base port (testing notes "Network ports")
    ap.add_argument("--violence", type=int, default=None,
                    help="lock_* bits the game module sees (0 = all violence on), through g3shim.dll")
    ap.add_argument("--sp", action="store_true", help="single player (deathmatch 0) through g3shim.dll")
    a = ap.parse_args(argv)
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    out = DATA / a.tag
    out.mkdir(parents=True, exist_ok=True)
    user = TEST / ("u_" + a.tag.replace("/", "_"))
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    src = {"orig": TEST / "base" / "gamex86.dll.orig", "om": OM_DLL}.get(a.dll, Path(a.dll))
    extra = ""
    if a.violence is not None or a.sp:
        # g3shim.dll (g3shim.cpp) as the module; the module under test behind it as gamex86_real.dll
        copy(SHIM, TEST / "base" / "gamex86.dll")
        copy(src, TEST / "base" / "gamex86_real.dll")
        if a.violence is not None:
            extra += " +set g3_violence %d" % a.violence
        if a.sp:
            extra += " +set g3_sp 1"
    else:
        copy(src, TEST / "base" / "gamex86.dll")
    # developer 1 from an exec'd config (as G18's runtest does); on the command line it opens a modal box
    (TEST / "base" / "g3dev.cfg").write_text("set developer 1\n")
    for f in ("g3heap.log", "g18crash.log"):  # written by g3shim (g3_heapcheck) / the module (g18_crashlog)
        if (TEST / f).exists():
            (TEST / f).unlink()
    cfg = TEST / "base" / ("g3_%s.cfg" % a.tag.replace("/", "_"))
    cfg.write_text("\n".join(["set %s" % s for s in a.set]) + "\n", encoding="latin-1")
    args = ("+set dedicated 1 +set user ./%s +set logfile 2 +exec g3dev.cfg +set ip 127.0.0.1 +set port %d "
            "+set deathmatch %s +set maxclients 8%s +exec %s +map %s" % (user.name, a.port, a.dm, extra, cfg.name, a.map))
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE
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
    procs = []
    try:
        if ok and srv.poll() is None:
            for k, c in enumerate(a.client):
                name, _, script = c.partition("|")
                cmd = [sys.executable, str(CLIENT), "--server", "127.0.0.1:%d" % a.port, "--seconds", str(a.seconds),
                       "--out", str(out / ("cap_%s.jsonl" % name)), "--log", str(out / ("client_%s.log" % name)),
                       "--name", name]
                if script:
                    cmd += ["--script", script]
                procs.append(subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True))
                time.sleep(0.5)
            for p in procs:
                try:
                    o, _ = p.communicate(timeout=a.seconds + 60)
                    print(o.strip().splitlines()[-1] if o.strip() else "")
                except subprocess.TimeoutExpired:
                    p.kill()
    finally:
        for p in procs:
            if p.poll() is None:
                p.kill()
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if log.exists():
            shutil.copyfile(log, out / "sof.log")
        for f in ("g3heap.log", "g18crash.log"):
            if (TEST / f).exists():
                shutil.copyfile(TEST / f, out / f)
                print("%s:\n%s" % (f, (TEST / f).read_text(errors="replace").strip()))
        print("server %s (dll %s), out %s" % ("was running" if alive else "EXITED early", src.name, out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
