#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G7 - G18's headless test client (game/test/sofclient.py) on G7's own client port.
# Implements: SPEC-200 (test harness only: client port selection, foreign-packet filter)
#
# usage: g7client.py --bind-port N <sofclient.py arguments>
# Project rule (docs/process/testing.md "Network ports"): every test client binds its package's port
# (G7: 29481, second copy 29483) instead of an ephemeral or default one. Packets that do not come from the
# requested server are dropped and counted; the count is printed as "foreign packets N" so the run can be discarded.

import socket
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "game" / "test"))
sys.path.insert(0, str(REPO / "tools" / "net"))


def main(argv):
    if len(argv) < 2 or argv[0] != "--bind-port":
        raise SystemExit("usage: g7client.py --bind-port N <sofclient arguments>")
    port = int(argv[1])
    rest = argv[2:]
    server = "127.0.0.1:27910"
    if "--server" in rest:
        server = rest[rest.index("--server") + 1]
    host, _, sport = server.rpartition(":")
    want = (host, int(sport))
    foreign = [0]

    class G7Socket(socket.socket):
        def bind(self, addr):
            if addr[1] == 0:
                addr = (addr[0], port)
            return super().bind(addr)

        def recvfrom(self, n, *a):
            while True:
                data, addr = super().recvfrom(n, *a)
                if addr[0] == want[0] and addr[1] == want[1]:
                    return data, addr
                foreign[0] += 1

    socket.socket = G7Socket
    import sofclient
    try:
        sofclient.main(rest)
    finally:
        print("foreign packets %d" % foreign[0])


if __name__ == "__main__":
    main(sys.argv[1:])
