#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G3 - black-box runs for the damage package: G2's runner (game/src/ai/tools/airun.py, built on G18's
# harness) pointed at G3's own test copy D:\Games\SoF-test-G3, build tree build-G3-x86 and output
# openmerc-data\g3\<tag>. The original SoF.exe runs as a hidden dedicated server (never focused).
# Implements: SPEC-720, SPEC-721, SPEC-722 (black-box verification harness)
#
# usage: g3run.py --dll orig|om --map tsr1 --sp --tag NAME [--seconds 30] [--script ...] [--set "cvar v"]
import importlib.util
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
spec = importlib.util.spec_from_file_location("airun", REPO / "game" / "src" / "ai" / "tools" / "airun.py")
airun = importlib.util.module_from_spec(spec)
spec.loader.exec_module(airun)
airun.TEST = Path(r"D:\Games\SoF-test-G3")
airun.DATA = Path(r"C:\openmerc-data\g3")
airun.OM_DLL = REPO / "build-G3-x86" / "gamex86.dll"
airun.SHIM = REPO / "build-G3-x86" / "spshim.dll"

# "+set developer 1" on the command line makes the original engine open a modal "Developer Mode" box, and developer 0
# stops the dedicated server after "Server Initialization" (testing notes); the command-line word is dropped and
# developer 1 comes from the exec'd cfg that airun writes.
_popen = airun.subprocess.Popen


def _popen_nodev(args, *a, **k):
    if isinstance(args, list):
        out, i = [], 0
        while i < len(args):
            if args[i] == "+set" and i + 2 < len(args) and args[i + 1] == "developer":
                i += 3
                continue
            out.append(args[i])
            i += 1
        args = out
    return _popen(args, *a, **k)


airun.subprocess.Popen = _popen_nodev

if __name__ == "__main__":
    argv = sys.argv[1:]
    if "--port" not in argv:
        argv += ["--port", "29512"]  # G3 base 29510 + 2 (testing notes "Network ports")
    sys.exit(airun.main(argv))
