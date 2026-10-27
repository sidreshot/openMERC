#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G3 - lifecycle runs: G9's deathmatch rotation cases (game/test/g9/suite.py "fraglimit", "timelimit") in
# G3's test copy D:\Games\SoF-test-G3 on G3's ports, with the module under test behind g3shim.dll and
# `g3_heapcheck 1` (every process heap validated after each RunFrame, SpawnEntities, ClientDisconnect, Shutdown;
# results in g3heap.log). The original SoF.exe runs as a hidden dedicated server (G9's modetest.py).
# Implements: SPEC-760 B1.3 (level spawn), SPEC-720/722 (body lifetime) - black-box verification harness
#
# usage: g3life.py --dll <path to gamex86.dll under test> [--tag PREFIX] [case ...]   (default: fraglimit timelimit)
import argparse
import importlib.util
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
G9 = REPO / "game" / "test" / "g9"
TEST = Path(r"D:\Games\SoF-test-G3")
DATA = Path(r"C:\openmerc-data\g3\life")
SHIM = REPO / "build-G3-x86" / "g3shim.dll"
PORT = 29510  # G3 base port (testing notes "Network ports"); clients 29511..


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.path.insert(0, str(path.parent))
    spec.loader.exec_module(mod)
    return mod


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", required=True)
    ap.add_argument("--tag", default="om")
    ap.add_argument("cases", nargs="*")
    a = ap.parse_args(argv)
    suite = load("g9suite", G9 / "suite.py")
    mt = load("modetest", G9 / "modetest.py")
    mt.TEST = TEST
    mt.DATA = DATA
    DATA.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(a.dll, TEST / "base" / "gamex86_real.dll")
    for name in a.cases or ["fraglimit", "timelimit"]:
        m, dm, secs, sets, clients = suite.CASES[name]
        for f in ("g3heap.log", "g18crash.log"):
            if (TEST / f).exists():
                (TEST / f).unlink()
        tag = "%s_%s" % (a.tag, name)
        args = ["--dll", str(SHIM), "--map", m, "--dm", str(dm), "--tag", tag, "--seconds", str(secs),
                "--port", str(PORT), "--set", "g3_heapcheck 1", "--set", "g18_crashlog 1", "--set", "g3_debug 1"]
        for s in sets:
            args += ["--set", s]
        for c in clients:
            args += ["--client", c]
        print("==", name, flush=True)
        try:
            mt.main(args)
        except SystemExit:
            pass
        for f in ("g3heap.log", "g18crash.log"):
            if (TEST / f).exists():
                shutil.copyfile(TEST / f, DATA / tag / f)
                print("%s: %s" % (f, (TEST / f).read_text(errors="replace").strip()))
            else:
                print("%s: none" % f)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
