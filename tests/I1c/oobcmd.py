# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-200 (test harness only: the connectionless "cmd" packet)
# Drives a running SoF client through its console without OS input: waits until the client's log
# contains a pattern, then sends the out-of-band packet FF FF FF FF "cmd" LF <text> NUL from the LAN
# address (the client ignores it from 127.0.0.1, see docs/specs/net-verification.md item 5).
#
#   python oobcmd.py --log <client sof.log> --wait "<pattern>" --port 27967 --text "exec omtest.cfg"
import argparse
import re
import socket
import sys
import time


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--log", required=True)
    ap.add_argument("--wait", default="", help="regular expression to wait for in the log")
    ap.add_argument("--port", type=int, required=True, help="client UDP port")
    ap.add_argument("--text", required=True, action="append")
    ap.add_argument("--timeout", type=float, default=60)
    ap.add_argument("--delay", type=float, default=1.0, help="seconds after the pattern before sending")
    args = ap.parse_args()

    t0 = time.time()
    lan = None
    found = not args.wait
    while time.time() - t0 < args.timeout:
        try:
            with open(args.log, "rb") as f:
                text = f.read().decode("latin-1")
        except OSError:
            text = ""
        m = re.search(r"LOCAL IP #1: (\d+\.\d+\.\d+\.\d+)", text)
        if m:
            lan = m.group(1)
        if args.wait and re.search(args.wait, text, re.M):
            found = True
        if found and lan:
            break
        time.sleep(0.1)
    if not (found and lan):
        print("oobcmd: pattern or LAN address not seen (lan=%s found=%s)" % (lan, found))
        return 1
    time.sleep(args.delay)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((lan, 0))
    head = bytes([255, 255, 255, 255]) + b"cmd" + bytes([10])
    for t in args.text:
        # Quake II layout: the first line is the command word, the console text follows as a string
        s.sendto(head + t.encode("latin-1") + bytes([0]), (lan, args.port))
        print("oobcmd: sent %r to %s:%d after %.1f s" % (t, lan, args.port, time.time() - t0))
        time.sleep(0.2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
