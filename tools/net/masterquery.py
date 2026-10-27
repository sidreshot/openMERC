#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - GameSpy (v0) master server browser and server query tool for Soldier of Fortune.
# Implements: SPEC-204 (GameSpy master list, validate, heartbeats, query responder), SPEC-200
#
# The validation algorithm ("gsseckey") is the public GameSpy master-server algorithm as documented
# by Luigi Auriemma's gsmsalg (GPL-2.0-or-later); this is an independent Python implementation of it
# (see THIRD_PARTY.md).
#
# Usage:
#   python masterquery.py                       # query the default masters, then every listed server
#   python masterquery.py --masters sof1master.sofvault.org --no-ping
#   python masterquery.py --server 1.2.3.4:28911 --query '\status\'     # raw query of one server
#   python masterquery.py --server 127.0.0.1:28911 --query '\basic\\secure\ABCDEF' --raw
#   python masterquery.py --validate ABCDEF     # print the validate response for a challenge
#   python masterquery.py --fake-master 27900   # log heartbeats and query back like a master would
#   python masterquery.py --selftest            # check the validate algorithm against SPEC-204 vectors
#
# Master protocol (TCP, default port 28900):
#   master -> client  \basic\\secure\<6-char challenge>
#   client -> master  \gamename\<game>\gamever\1.6\location\0\validate\<response>\final\\queryid\1.1\
#   client -> master  \list\cmp\gamename\<game>\final\
#   master -> client  6 bytes per server (IPv4, big-endian port) followed by \final\   (list type "cmp")
#                     or \ip\a.b.c.d:port\ip\...\final\                               (list type "")
# Server query protocol (UDP, default port 28911): see SPEC-204 section 4.
# Unlike the 1.07f browser (one master, 10 queries in flight, 3 s, no retry) this tool queries several
# masters, removes duplicates and sends every server one query at once with one retry.

import argparse
import json
import re
import select
import socket
import sys
import time

DEFAULT_MASTERS = ["sof1master.sofvault.org", "master.333networks.com", "master.openspy.net"]
GAMENAME = "sofretail"
GAMEKEY = "iVn3a3"
GAMEVER = "1.07f"          # asic\ reply of a server
SDKVER = "1.6"             # gamever sent by the 1.07f browser to the master (SPEC-204 section 2)
MASTER_LIST_PORT = 28900
MASTER_HEARTBEAT_PORT = 27900
QUERY_PORT = 28911


# ---------------------------------------------------------------------------------------------
# GameSpy "secure/validate" (public gsseckey algorithm, enctype 0)
# ---------------------------------------------------------------------------------------------

def _gs_b64char(v):
    if v < 26:
        return chr(v + ord("A"))
    if v < 52:
        return chr(v - 26 + ord("a"))
    if v < 62:
        return chr(v - 52 + ord("0"))
    return "+" if v == 62 else "/"


SPEC204_VECTORS = [("ABCDEF", "Xm1uyTLB"), ("abcdef", "XC5ysZ+8"), ("123456", "QHc2Mxl/"),
                   ("QWERTY", "d3u9xHSi"), ("000000", "Qd53sUB8"), ("IQPMTF", "5HLJquGU"),
                   ("IVJYHE", "5Pd8dI9K"), ("QZvHrG", "dzohGPto"), ("abcdefgh", "XC5ysZ+8yaoA"),
                   ("abc", "XC5y"), ("abcdefg", "XC5ysZ+8yQAA"), ("A", "XgAA"),
                   # black-box: answers of the original 1.07f dedicated server (2026-10-21)
                   ("OMTEST", "gAk85Qf5"), ("TWSFID", "wxVCD8YN"),
                   # SPEC-AMEND-005 GS-V1: OpenSpy's 8-character challenge
                   ("oyMl8D24", "a5df+rC3Ni8A")]


def gs_validate(challenge, key=GAMEKEY):
    """Return the \\validate\\ response for a \\secure\\ challenge (both ASCII strings)."""
    src = challenge.encode("latin-1")
    k = key.encode("latin-1")
    if not 1 <= len(src) <= 65 or not k:
        return ""
    # keyed permutation of 0..255 (RC4 key schedule)
    table = list(range(256))
    a = 0
    for i in range(256):
        a = (a + table[i] + k[i % len(k)]) & 0xFF
        table[a], table[i] = table[i], table[a]
    # scramble the challenge; the index walks with the challenge bytes, not a counter
    a = b = 0
    out = bytearray()
    for c in src:
        a = (a + c + 1) & 0xFF
        x = table[a]
        b = (b + x) & 0xFF
        y = table[b]
        table[b] = x
        table[a] = y
        out.append(c ^ table[(x + y) & 0xFF])
    while len(out) % 3:
        out.append(0)
    # base64 with the standard alphabet, no padding characters
    res = []
    for i in range(0, len(out), 3):
        x, y, z = out[i], out[i + 1], out[i + 2]
        res.append(_gs_b64char(x >> 2))
        res.append(_gs_b64char(((x & 3) << 4) | (y >> 4)))
        res.append(_gs_b64char(((y & 15) << 2) | (z >> 6)))
        res.append(_gs_b64char(z & 63))
    return "".join(res)


# ---------------------------------------------------------------------------------------------
# key/value helpers
# ---------------------------------------------------------------------------------------------

def parse_kv(text):
    """'\\a\\1\\b\\2' -> list of (key, value) in order (duplicates kept)."""
    if text.startswith("\\"):
        text = text[1:]
    parts = text.split("\\")
    pairs = []
    for i in range(0, len(parts) - 1, 2):
        pairs.append((parts[i], parts[i + 1]))
    if len(parts) % 2 == 1 and parts[-1]:
        pairs.append((parts[-1], ""))
    return pairs


def strip_sof_colors(s):
    # SoF names may carry colour/control bytes (< 0x20); drop them for display
    return "".join(ch for ch in s if ord(ch) >= 0x20)


def parse_hostport(s, default_port):
    host, sep, port = s.rpartition(":")
    if not sep:
        return s, default_port
    return host, int(port)


# ---------------------------------------------------------------------------------------------
# master list (TCP)
# ---------------------------------------------------------------------------------------------

class MasterResult:
    def __init__(self, name):
        self.name = name
        self.addr = None
        self.greeting = b""
        self.raw = b""
        self.servers = []
        self.error = None
        self.elapsed = 0.0


def _recv_until(sock, pred, deadline, buf=b""):
    while not pred(buf):
        left = deadline - time.time()
        if left <= 0:
            raise TimeoutError("timed out")
        sock.settimeout(left)
        chunk = sock.recv(8192)
        if not chunk:
            break
        buf += chunk
    return buf


def query_master(host, port=MASTER_LIST_PORT, game=GAMENAME, key=GAMEKEY, list_type="cmp",
                 timeout=10.0, gamever=SDKVER):
    r = MasterResult("%s:%d" % (host, port))
    t0 = time.time()
    deadline = t0 + timeout
    try:
        r.addr = socket.gethostbyname(host)
        s = socket.create_connection((host, port), timeout=timeout)
        try:
            greet = _recv_until(s, lambda b: re.search(rb"\\secure\\[^\\]{6}", b) is not None, deadline)
            r.greeting = greet
            m = re.search(rb"\\secure\\([^\\]+)", greet)
            if not m:
                raise RuntimeError("no \\secure\\ challenge in greeting %r" % greet)
            challenge = m.group(1).decode("latin-1")
            resp = gs_validate(challenge, key)
            hello = "\\gamename\\%s\\gamever\\%s\\location\\0\\validate\\%s\\final\\\\queryid\\1.1\\" % (
                game, gamever, resp)
            s.sendall(hello.encode("latin-1"))
            req = "\\list\\%s\\gamename\\%s\\final\\" % (list_type, game)
            s.sendall(req.encode("latin-1"))
            data = _recv_until(s, lambda b: b.endswith(b"\\final\\"), deadline)
            r.raw = data
        finally:
            s.close()
        r.servers = decode_list(r.raw)
    except Exception as e:  # noqa: BLE001 - report any network failure per master
        r.error = "%s: %s" % (type(e).__name__, e)
    r.elapsed = time.time() - t0
    return r


def decode_list(data):
    """Decode a master list reply (either \\ip\\ text or 6-byte compressed records)."""
    if data.startswith(b"\\error\\") or data.startswith(b"\\basic\\"):
        raise RuntimeError("master error: %r" % data[:200])
    body = data[:-7] if data.endswith(b"\\final\\") else data
    if body.startswith(b"\\ip\\") or body == b"":
        out = []
        for k, v in parse_kv(body.decode("latin-1")):
            if k == "ip":
                h, p = parse_hostport(v, QUERY_PORT)
                out.append((h, p))
        return out
    if len(body) % 6:
        raise RuntimeError("compressed list length %d is not a multiple of 6: %r" % (len(body), data[:80]))
    out = []
    for i in range(0, len(body), 6):
        ip = socket.inet_ntoa(body[i:i + 4])
        port = (body[i + 4] << 8) | body[i + 5]
        out.append((ip, port))
    return out


# ---------------------------------------------------------------------------------------------
# server query (UDP)
# ---------------------------------------------------------------------------------------------

class ServerReply:
    def __init__(self, addr):
        self.addr = addr
        self.packets = {}        # packet number -> text
        self.final_packet = None
        self.first_rtt = None
        self.sent_at = None
        self.raw = []

    @property
    def complete(self):
        return self.final_packet is not None and all(i in self.packets for i in range(1, self.final_packet + 1))

    def add(self, data, now):
        text = data.decode("latin-1")
        self.raw.append(text)
        if self.first_rtt is None and self.sent_at is not None:
            self.first_rtt = (now - self.sent_at) * 1000.0
        m = re.search(r"\\queryid\\(\d+)\.(\d+)$", text)
        num = int(m.group(2)) if m else len(self.packets) + 1
        body = text[:m.start()] if m else text
        if body.endswith("\\final\\"):
            self.final_packet = num
            body = body[:-7]
        elif "\\final\\" in text and m is None:
            self.final_packet = num
        self.packets[num] = body

    def merged(self):
        return "".join(self.packets[i] for i in sorted(self.packets))

    def kv(self):
        d = {}
        for k, v in parse_kv(self.merged()):
            if k and k not in d:
                d[k] = v
        return d


def query_servers(addrs, query="\\status\\", timeout=3.0, retries=1, bind_port=0):
    """Send `query` to every (ip, port) and collect replies. Returns {addr: ServerReply}."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", bind_port))
    sock.setblocking(False)
    replies = {a: ServerReply(a) for a in addrs}
    payload = query.encode("latin-1")
    try:
        for attempt in range(retries + 1):
            pending = [a for a, r in replies.items() if not r.packets]
            if not pending:
                break
            for a in pending:
                replies[a].sent_at = time.time()
                try:
                    sock.sendto(payload, a)
                except OSError:
                    pass
                time.sleep(0.002)  # do not burst the uplink
            deadline = time.time() + timeout
            while time.time() < deadline:
                if all(r.complete for r in replies.values()):
                    break
                rl, _, _ = select.select([sock], [], [], max(0.0, deadline - time.time()))
                if not rl:
                    break
                while True:
                    try:
                        data, frm = sock.recvfrom(65535)
                    except (BlockingIOError, ConnectionResetError, OSError):
                        break
                    r = replies.get(frm)
                    if r is None:
                        # reply from another address of the same host (multi-homed server)
                        r = next((x for x in replies.values() if x.addr[0] == frm[0]), None)
                    if r is not None:
                        r.add(data, time.time())
    finally:
        sock.close()
    return replies


# ---------------------------------------------------------------------------------------------
# fake master (for black-box observation of heartbeats)
# ---------------------------------------------------------------------------------------------

def fake_master(port, key, seconds, query):
    """Listen for UDP heartbeats like a master; answer each with `query` + \\secure\\ and check validate."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(("0.0.0.0", port))
    sock.settimeout(0.5)
    t0 = time.time()
    challenge = "OMTEST"
    print("fake master listening on UDP %d (challenge %s, expected validate %s)" % (
        port, challenge, gs_validate(challenge, key)), flush=True)
    while seconds <= 0 or time.time() - t0 < seconds:
        try:
            data, frm = sock.recvfrom(65535)
        except socket.timeout:
            continue
        except ConnectionResetError:
            continue
        text = data.decode("latin-1")
        print("[%7.2f] from %s:%d  %r" % (time.time() - t0, frm[0], frm[1], text), flush=True)
        kv = dict(parse_kv(text))
        if "heartbeat" in kv:
            qport = int(kv["heartbeat"]) if kv["heartbeat"].isdigit() else frm[1]
            target = (frm[0], qport)
            q = query + "\\secure\\" + challenge
            sock.sendto(q.encode("latin-1"), target)
            print("          -> %s:%d %r" % (target[0], target[1], q), flush=True)
        elif "validate" in kv:
            ok = kv["validate"] == gs_validate(challenge, key)
            print("          validate %s" % ("OK" if ok else "MISMATCH"), flush=True)


# ---------------------------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="Query GameSpy v0 masters and SoF servers")
    ap.add_argument("--masters", nargs="*", default=DEFAULT_MASTERS,
                    help="master host[:port] list (TCP list port, default 28900)")
    ap.add_argument("--game", default=GAMENAME)
    ap.add_argument("--key", default=GAMEKEY)
    ap.add_argument("--list-type", default="cmp", help="\\list\\ type: cmp (binary) or '' (text)")
    ap.add_argument("--query", default="\\status\\", help="UDP query sent to each server")
    ap.add_argument("--timeout", type=float, default=3.0, help="UDP reply timeout per round (s)")
    ap.add_argument("--master-timeout", type=float, default=12.0)
    ap.add_argument("--no-ping", action="store_true", help="only list, do not query servers")
    ap.add_argument("--server", action="append", help="query this server host[:queryport] directly")
    ap.add_argument("--raw", action="store_true", help="print raw replies")
    ap.add_argument("--json", action="store_true", help="print the result as JSON")
    ap.add_argument("--validate", metavar="CHALLENGE", help="print the validate response and exit")
    ap.add_argument("--selftest", action="store_true", help="check validate against SPEC-204 vectors")
    ap.add_argument("--fake-master", type=int, metavar="PORT", help="run a heartbeat logger on PORT")
    ap.add_argument("--seconds", type=float, default=0, help="fake master run time (0 = forever)")
    args = ap.parse_args()

    if args.validate:
        print(gs_validate(args.validate, args.key))
        return 0
    if args.selftest:
        bad = 0
        for ch, want in SPEC204_VECTORS:
            got = gs_validate(ch, GAMEKEY)
            print("%-8s %-14s %s" % (ch, got, "ok" if got == want else "MISMATCH (want %s)" % want))
            bad += got != want
        return 1 if bad else 0
    if args.fake_master:
        fake_master(args.fake_master, args.key, args.seconds, args.query)
        return 0

    result = {"masters": [], "servers": []}
    addrs = []
    if args.server:
        addrs = [parse_hostport(s, QUERY_PORT) for s in args.server]
        addrs = [(socket.gethostbyname(h), p) for h, p in addrs]
    else:
        seen = {}
        for m in args.masters:
            host, port = parse_hostport(m, MASTER_LIST_PORT)
            r = query_master(host, port, args.game, args.key, args.list_type, args.master_timeout)
            info = {"master": r.name, "ip": r.addr, "servers": len(r.servers), "error": r.error,
                    "seconds": round(r.elapsed, 2), "greeting": r.greeting.decode("latin-1")}
            result["masters"].append(info)
            if not args.json:
                if r.error:
                    print("master %-32s ERROR %s" % (r.name, r.error))
                else:
                    print("master %-32s %3d records, %3d unique  (%.1fs, greeting %r)" % (
                        r.name, len(r.servers), len(set(r.servers)), r.elapsed,
                        r.greeting.decode("latin-1")))
                if args.raw:
                    print("   raw:", r.raw[:400])
            for a in r.servers:
                seen.setdefault(a, []).append(host)
        addrs = list(seen)
        result["unique"] = len(addrs)
        result["listed_by"] = {"%s:%d" % a: v for a, v in seen.items()}
        if not args.json:
            print("unique servers after merge: %d" % len(addrs))
        if args.no_ping:
            if args.json:
                print(json.dumps(result, indent=1))
            else:
                for a in sorted(addrs):
                    print("  %s:%d  (%s)" % (a[0], a[1], ", ".join(seen[a])))
            return 0

    replies = query_servers(addrs, args.query, args.timeout)
    rows = []
    for a in addrs:
        r = replies[a]
        kv = r.kv()
        row = {"addr": "%s:%d" % a, "answered": bool(r.packets), "complete": r.complete,
               "ping": round(r.first_rtt) if r.first_rtt is not None else None, "keys": kv}
        if args.raw:
            row["raw"] = r.raw
        rows.append(row)
    result["servers"] = rows
    result["answered"] = sum(1 for x in rows if x["answered"])

    if args.json:
        print(json.dumps(result, indent=1))
        return 0

    print()
    print("%-21s %-32s %-16s %-7s %5s %-10s %s" % ("address", "name", "map", "players", "ping",
                                                  "gametype", "ver"))
    for row in sorted(rows, key=lambda x: (not x["answered"], x["ping"] or 9999)):
        kv = row["keys"]
        if not row["answered"]:
            print("%-21s (no answer)" % row["addr"])
            continue
        players = "%s/%s" % (kv.get("numplayers", "?"), kv.get("maxplayers", "?"))
        print("%-21s %-32.32s %-16.16s %-7s %5s %-10.10s %s" % (
            row["addr"], strip_sof_colors(kv.get("hostname", "?")), kv.get("mapname", "?"), players,
            row["ping"], kv.get("gametype", "?"), kv.get("gamever", "")))
        if args.raw:
            for t in row["raw"]:
                print("    raw:", repr(t))
    print("\n%d of %d servers answered %r" % (result["answered"], len(rows), args.query))
    return 0


if __name__ == "__main__":
    sys.exit(main())
