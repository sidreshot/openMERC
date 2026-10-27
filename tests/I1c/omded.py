# SPDX-License-Identifier: GPL-2.0-or-later
# Ports: server 29540 (I1c base, docs/process/testing.md).
# Implements: test harness only (no game code). Starts the OpenMerc dedicated server (openMerc-ded.exe of the I1c build)
# hidden, -portable, in the I1c test copy with the original game module, waits until the map is loaded (or the
# timeout), prints the tail of its log and leaves it running for -seconds, then kills it (only that process).
#   python -I omded.py [--seconds 60] [--port 29540] [--map dm/jpndm2]
import argparse
import os
import subprocess
import sys
import time

BUILD = r"C:\openmerc\build-I1c-x86\release\openMerc-ded.exe"
TEST = r"D:\Games\SoF-test-I1c"
USER = r"C:\openmerc-data\I1c\omsrv\user"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=60)
    ap.add_argument("--port", type=int, default=29540)
    ap.add_argument("--map", default="dm/jpndm2")
    a = ap.parse_args()
    os.makedirs(USER, exist_ok=True)
    log = os.path.join(USER, "sof.log")
    if os.path.exists(log):
        os.remove(log)
    cmd = [BUILD, "-portable", "-basedir", TEST, "-user", USER, "+set", "logfile", "2", "+set", "developer", "0",
           "+set", "ip", "127.0.0.1", "+set", "port", str(a.port), "+set", "deathmatch", "1", "+set", "maxclients",
           "4", "+set", "timelimit", "0", "+set", "fraglimit", "0", "+map", a.map]
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    p = subprocess.Popen(cmd, cwd=os.path.dirname(BUILD), startupinfo=si, creationflags=subprocess.CREATE_NEW_CONSOLE)
    print("openMerc-ded pid", p.pid, flush=True)
    t0 = time.time()
    try:
        while time.time() - t0 < a.seconds:
            if p.poll() is not None:
                print("openMerc-ded exited with", p.returncode)
                break
            time.sleep(0.5)
    finally:
        if p.poll() is None:
            p.kill()
            p.wait(5)
    if os.path.exists(log):
        with open(log, "rb") as f:
            text = f.read().decode("ascii", "ignore")
        print("\n".join(text.splitlines()[-25:]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
