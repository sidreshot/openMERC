#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G6b - G18's black-box harness (game/test/runtest.py) pointed at the G6b test copy and build tree.
# The original SoF.exe runs as a hidden dedicated server with the chosen gamex86.dll; the headless client
# records what it receives. The oracle install is never written.
#
# --sp: single-player run. A dedicated server forces deathmatch, so the SP proxy module (spproxy/, our own test
# code) is installed as base\gamex86.dll; it loads the module under test (orig = gamex86.dll.orig, om = our
# build copied to base\gamex86.om.dll) and force-sets deathmatch 0 / maxclients 1 before its Init.
# Implements: SPEC-750, SPEC-751 (black-box comparison of the script system through the original engine)
#
# --files DIR: copy the tree DIR into the run's user directory before the server starts (loose files there override
# the paks, SPEC-300 1) - used for the test programs of g6b_vectors.py.
# usage: g6b_run.py [--sp] [--files DIR] --dll orig|om|<path> --map tsr1 --tag NAME [runtest.py options]
# Output: <openmerc-data>/g6b/<tag>/{cap.jsonl, sof.log, client.log, proxy.log}

import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
sys.path.insert(0, str(REPO / "game" / "test"))
import runtest  # noqa: E402

runtest.TEST = Path(r"D:\Games\SoF-test-G6b")
runtest.DATA = Path(r"C:\openmerc-data\g6b")
runtest.OM_DLL = REPO / "build-G6b-x86" / "gamex86.dll"
PROXY = REPO / "build-G6b-x86" / "spproxy" / "gamex86.dll"

# Since 2026-10-22 ~09:00 `+set developer 1` on this PC makes the original show a modal "Developer Mode" message box
# (hidden window, the server never starts). The harness passes developer 0 instead (G6b journal).
_real_popen = runtest.subprocess.Popen


def _popen(cmd, *a, **k):
    if isinstance(cmd, list):
        s = " ".join(cmd).replace("+set developer 1", "+set developer 0")
        cmd = [cmd[0]] + s[len(cmd[0]) + 1:].split()
    return _real_popen(cmd, *a, **k)


runtest.subprocess.Popen = _popen


def main(argv):
    args = list(argv)
    if "--files" in args:
        i = args.index("--files")
        files = Path(args[i + 1])
        del args[i:i + 2]
        tag = args[args.index("--tag") + 1]
        user = runtest.TEST / ("u_" + tag.replace("/", "_"))
        real_install = runtest.install

        def install_with_files(dll):
            r = real_install(dll)
            shutil.copytree(files, user, dirs_exist_ok=True)
            return r
        runtest.install = install_with_files
    if "--port" not in args:
        args += ["--port", "27961"]
    if "--sp" in args:
        args.remove("--sp")
        i = args.index("--dll") if "--dll" in args else -1
        which = args[i + 1] if i >= 0 else "om"
        base = runtest.TEST / "base"
        if which == "orig":
            target = "gamex86.dll.orig"
        else:
            src = runtest.OM_DLL if which == "om" else Path(which)
            shutil.copyfile(src, base / "gamex86.om.dll")
            target = "gamex86.om.dll"
        tag = args[args.index("--tag") + 1]
        (runtest.DATA / tag).mkdir(parents=True, exist_ok=True)
        os.environ["G6B_PROXY_TARGET"] = target
        os.environ["G6B_PROXY_LOG"] = str(runtest.DATA / tag / "proxy.log")
        if i >= 0:
            args[i + 1] = str(PROXY)
        else:
            args += ["--dll", str(PROXY)]
        for k, v in (("--dm", "0"), ("--maxclients", "1")):
            if k not in args:
                args += [k, v]
    return runtest.main(args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
