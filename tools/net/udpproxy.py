#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - black-box UDP relay for network captures.
# Implements: SPEC-200 (only as an observer: it forwards datagrams unchanged)
#
# Listens on --listen (host:port). Every datagram from a client is forwarded to --server from a
# per-client upstream socket; every reply is sent back to that client from the listening socket.
# Each datagram is logged as one JSON line:
#   {"t": seconds since start, "wall": Unix time, "dir": "c2s"|"s2c", "client": "ip:port",
#    "len": n, "hex": "..."}
# Usage:
#   python udpproxy.py --listen 127.0.0.1:27920 --server 127.0.0.1:27910 --out capture.jsonl [--seconds N]

import argparse
import json
import selectors
import socket
import sys
import time


def parse_addr(s):
    host, _, port = s.rpartition(":")
    return (host or "127.0.0.1", int(port))


def main():
    ap = argparse.ArgumentParser(description="UDP relay that logs every datagram both ways")
    ap.add_argument("--listen", required=True, help="host:port the client connects to")
    ap.add_argument("--server", required=True, help="host:port of the real server")
    ap.add_argument("--out", required=True, help="JSON-lines capture file")
    ap.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = until Ctrl+C)")
    ap.add_argument("--upstream-bind", default="0.0.0.0",
                    help="local address of the per-client sockets that talk to the server")
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()

    listen_addr = parse_addr(args.listen)
    server_addr = parse_addr(args.server)

    sel = selectors.DefaultSelector()
    lsock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    lsock.bind(listen_addr)
    lsock.setblocking(False)
    sel.register(lsock, selectors.EVENT_READ, ("listen", None))

    upstream = {}  # client addr -> socket
    t0 = time.time()
    out = open(args.out, "w", encoding="ascii", buffering=1)
    counts = {"c2s": 0, "s2c": 0}

    def log(direction, client, data):
        counts[direction] += 1
        now = time.time()
        rec = {"t": round(now - t0, 6), "wall": round(now, 3), "dir": direction,
               "client": "%s:%d" % client, "len": len(data), "hex": data.hex()}
        out.write(json.dumps(rec) + "\n")
        if not args.quiet:
            preview = data[:48]
            txt = "".join(chr(b) if 32 <= b < 127 else "." for b in preview)
            print("%9.3f %s %5d %s" % (rec["t"], direction, len(data), txt), flush=True)

    print("relay %s:%d -> %s:%d, logging to %s" % (listen_addr + server_addr + (args.out,)), flush=True)
    try:
        while True:
            if args.seconds and time.time() - t0 > args.seconds:
                break
            for key, _ in sel.select(timeout=0.5):
                kind, client = key.data
                sock = key.fileobj
                try:
                    data, addr = sock.recvfrom(65535)
                except (ConnectionResetError, OSError):
                    # Windows reports ICMP port-unreachable as a reset on the next recv; ignore.
                    continue
                if kind == "listen":
                    up = upstream.get(addr)
                    if up is None:
                        up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        up.bind((args.upstream_bind, 0))
                        up.setblocking(False)
                        upstream[addr] = up
                        sel.register(up, selectors.EVENT_READ, ("up", addr))
                        print("new client %s:%d via upstream port %d" % (addr + (up.getsockname()[1],)),
                              flush=True)
                    log("c2s", addr, data)
                    up.sendto(data, server_addr)
                else:
                    log("s2c", client, data)
                    lsock.sendto(data, client)
    except KeyboardInterrupt:
        pass
    finally:
        out.close()
        print("done: %d c2s, %d s2c datagrams" % (counts["c2s"], counts["s2c"]), flush=True)


if __name__ == "__main__":
    sys.exit(main())
