# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-200 (connectionless `connect` packet, connect violence check) - test harness, no game code
#
# UDP relay between an original SoF client and an original/OpenMerc dedicated server, so that multiplayer test runs
# need no registry change: on this PC the parental-lock store is invalid for the volume key, both games force every
# lock (client cl_violence 63, server sv_violence 31) and the server refuses the client (`rejected_violence`; the
# server accepts when (sv_violence AND cl_violence) = cl_violence). The relay rewrites the userinfo value
# `\cl_violence\N` of the client's out-of-band `connect` packet to 0; every other datagram passes unchanged in both
# directions. One upstream socket per client address. Stops after --seconds or when <stopfile> appears.
# usage: mp_relay.py --listen PORT --server PORT [--seconds N] [--stopfile PATH] [--log PATH]
import argparse
import os
import re
import selectors
import socket
import time

VIOLENCE = re.compile(rb'\\cl_violence\\[0-9]+')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--listen', type=int, required=True)
    ap.add_argument('--server', type=int, required=True)
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--seconds', type=float, default=1800)
    ap.add_argument('--stopfile', default='')
    ap.add_argument('--log', default='')
    a = ap.parse_args()
    logf = open(a.log, 'a', encoding='utf-8') if a.log else None

    def log(text):
        line = '%.3f %s' % (time.monotonic() - t0, text)
        if logf:
            logf.write(line + '\n')
            logf.flush()

    t0 = time.monotonic()
    sel = selectors.DefaultSelector()
    front = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    front.bind((a.host, a.listen))
    sel.register(front, selectors.EVENT_READ, None)
    upstream = {}  # client address -> socket towards the server
    server = (a.host, a.server)
    log('relay %s:%d -> %s:%d' % (a.host, a.listen, a.host, a.server))
    counts = {'c2s': 0, 's2c': 0}
    while time.monotonic() - t0 < a.seconds:
        if a.stopfile and os.path.exists(a.stopfile):
            break
        for key, _ in sel.select(timeout=0.5):
            sock = key.fileobj
            try:
                data, addr = sock.recvfrom(65536)
            except OSError:
                continue
            if sock is front:
                up = upstream.get(addr)
                if up is None:
                    up = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                    up.bind((a.host, 0))
                    upstream[addr] = up
                    sel.register(up, selectors.EVENT_READ, addr)
                    log('client %s:%d via %s:%d' % (addr + up.getsockname()))
                if data[:12] == b'\xff\xff\xff\xffconnect ':
                    new = VIOLENCE.sub(rb'\\cl_violence\\0', data)
                    log('connect: cl_violence -> 0' if new != data else 'connect: no cl_violence in userinfo')
                    data = new
                up.sendto(data, server)
                counts['c2s'] += 1
            else:
                if addr != server:
                    log('dropped datagram from %s:%d (not the server)' % addr)
                    continue
                if data[:4] == b'\xff\xff\xff\xff':
                    log('server oob: %r' % data[4:40])
                front.sendto(data, key.data)
                counts['s2c'] += 1
    log('stop: %d client->server, %d server->client datagrams' % (counts['c2s'], counts['s2c']))


if __name__ == '__main__':
    main()
