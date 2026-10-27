#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - headless protocol-33 client for black-box tests of a game module.
# Implements: SPEC-200 (netchan, out-of-band handshake), SPEC-202 (clc_move), SPEC-203 (signon)
#
# Connects to a (dedicated) server, follows the signon, sends user commands from a small script and
# records every datagram both ways in the udpproxy.py JSON-lines format, so that
# tools/net/sofnet.py can decode it and game/test/capdiff.py can compare two runs.
#
# usage: sofclient.py --server 127.0.0.1:27910 --seconds 20 --out cap.jsonl   (seconds after "begin")
#                     [--name G18bot] [--script "t:cmd ..."] [--userinfo-extra \key\val]
#
# Script: a ';'-separated list of steps "<seconds after begin>:<action>", actions:
#   move <forward> <side> <up>     set the movement of the following commands
#   yaw <deg> / pitch <deg>        set the view angles
#   buttons <n>                    set the button bits (SPEC-202 6.1)
#   lean <-1|0|1>
#   aim <entity number>            keep the view on that entity (from the frames; aim 0 stops)
#   approach <entity> <dist>       aim at the entity and walk forward until it is within dist units (0 0 stops)
#   say <text> / cmd <text>        send a reliable stringcmd
#   rcon <command>                 remote console command (password g18, set by runtest.py)
#   userinfo K=V [K=V ...]         change userinfo keys (empty value removes) and send it (clc_userinfo)
#   quit                          disconnect and stop

import argparse
import json
import math
import os
import re
import socket
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402


def pack_cmds(cmds):
    """SPEC-202 6.2: three commands, the first against zero, each next against the previous."""
    bits = []

    def put(v, n):
        for i in range(n):
            bits.append((v >> i) & 1)
    ref = dict(sofnet.ZERO_CMD)
    for c in cmds:
        for i in range(3):
            a = (c["angles"][i] >> 4) & 0xFFF
            if a != ((ref["angles"][i] >> 4) & 0xFFF):
                put(1, 1); put(a, 12)
            else:
                put(0, 1)
        for k, lim in (("forward", 200), ("side", 160), ("up", 200)):
            v = max(-lim, min(lim, c[k]))
            if v != max(-lim, min(lim, ref[k])):
                put(1, 1); put(v + 510, 10)
            else:
                put(0, 1)
        if c["buttons"] != ref["buttons"]:
            put(1, 1); put(c["buttons"], 8)
        else:
            put(0, 1)
        if c["lean"]:
            put(1, 1); put(1 if c["lean"] < 0 else 0, 1)
        else:
            put(0, 1)
        if (c["light"] >> 3) != (ref["light"] >> 3):
            put(1, 1); put(c["light"] >> 3, 5)
        else:
            put(0, 1)
        put(c["msec"], 8)
        for k in ("fire", "altfire"):
            if c[k] != ref[k]:
                put(1, 1); put(struct.unpack("<I", struct.pack("<f", c[k]))[0], 32)
            else:
                put(0, 1)
        ref = c
    out = bytearray((len(bits) + 7) // 8)
    for i, b in enumerate(bits):
        if b:
            out[i >> 3] |= 1 << (i & 7)
    return bytes(out)


class Client:
    def __init__(self, server, out, name, extra, script, verbose, clientport=0):
        host, _, port = server.rpartition(":")
        self.addr = (host, int(port))
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        # the package's own client port (docs/process/testing.md "Network ports"); 0 = any free port
        self.sock.bind(("127.0.0.1", clientport))
        self.foreign = 0
        self.sock.settimeout(0.02)
        self.out = open(out, "w")
        self.t0 = time.time()
        self.qport = (os.getpid() * 7919) & 0xFFFF
        self.name = name
        self.extra = extra
        self.ui_set = []
        self.verbose = verbose
        self.sess = sofnet.Session()
        self.reset_chan()
        self.state = "challenge"
        self.begun_at = None
        self.script = sorted(script, key=lambda s: s[0])
        self.cmd = dict(sofnet.ZERO_CMD, msec=50, light=128)
        self.last_cmds = [dict(sofnet.ZERO_CMD), dict(sofnet.ZERO_CMD)]
        self.last_frame = -1
        self.aim = 0
        self.approach = None
        self.log = []

    def reset_chan(self):
        self.out_seq = 1
        self.in_seq = 0
        self.in_ack = 0
        self.in_rel_ack = 0
        self.in_rel_seq = 0
        self.rel_seq = 0
        self.last_rel_seq = 0
        self.rel_buf = b""
        self.msg = bytearray()

    def rec(self, d, data):
        self.out.write(json.dumps({"t": round(time.time() - self.t0, 4), "wall": time.time(), "dir": d,
                                   "client": "%s:%d" % self.sock.getsockname(), "len": len(data),
                                   "hex": data.hex()}) + "\n")

    def send_raw(self, data):
        self.sock.sendto(data, self.addr)
        self.rec("c2s", data)

    def oob(self, text):
        self.send_raw(b"\xff\xff\xff\xff" + text.encode("latin-1"))

    def stringcmd(self, s):
        self.msg += b"\x04" + s.encode("latin-1") + b"\0"

    def transmit(self, unreliable=b""):
        send_rel = False
        if self.in_ack > self.last_rel_seq and self.in_rel_ack != self.rel_seq:
            send_rel = True
        if not self.rel_buf and self.msg:
            self.rel_buf = bytes(self.msg)
            self.msg = bytearray()
            self.rel_seq ^= 1
            send_rel = True
        w1 = self.out_seq | (0x80000000 if send_rel else 0)
        w2 = self.in_seq | (self.in_rel_seq << 31)
        seq = self.out_seq
        self.out_seq += 1
        pkt = struct.pack("<IIH", w1, w2, self.qport)
        if send_rel:
            pkt += self.rel_buf
            self.last_rel_seq = self.out_seq
        if callable(unreliable):
            unreliable = unreliable(seq, len(pkt))
        pkt += unreliable
        self.send_raw(pkt)

    def aim_angles(self):
        """Command angles that point the view at entity self.aim (from the last frame); None when unknown."""
        fr = self.sess.frames.get(self.last_frame)
        if not fr or self.aim not in fr[0]:
            return None
        ents, ps = fr
        me = [ps["origin"][i] / 8.0 for i in range(3)]
        me[2] += 22  # eye height of a standing player (view offset)
        t = list(ents[self.aim]["origin"])
        t[2] += 10
        d = [t[i] - me[i] for i in range(3)]
        yaw = math.degrees(math.atan2(d[1], d[0]))
        pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        da = ps["delta_angles"]
        return (int(pitch * 65536 / 360 - da[0]) & 0xFFFF, int(yaw * 65536 / 360 - da[1]) & 0xFFFF, 0)

    def move_msg(self, seq, _hdr):
        if self.approach:
            num, dist = self.approach
            fr = self.sess.frames.get(self.last_frame)
            if fr and num in fr[0]:
                me = [fr[1]["origin"][i] / 8.0 for i in range(3)]
                t = fr[0][num]["origin"]
                self.cmd["forward"] = 200 if math.hypot(t[0] - me[0], t[1] - me[1]) > dist else 0
        if self.aim:
            a = self.aim_angles()
            if a:
                self.cmd["angles"] = a
        c = dict(self.cmd)
        cmds = [self.last_cmds[0], self.last_cmds[1], c]
        body = struct.pack("<i", self.last_frame) + pack_cmds(cmds)
        cs = sofnet.move_checksum(body, seq)
        self.last_cmds = [self.last_cmds[1], c]
        return b"\x02" + bytes([cs]) + body

    def userinfo(self):
        ui = ("\\name\\%s\\skin\\mullins\\teamname\\mullins\\rate\\15000\\msg\\0\\fov\\95\\bestweap\\safe"
              "\\team_red_blue\\0\\spectator\\0\\predicting\\0\\cl_violence\\0" % self.name)
        if self.ui_set:
            # --ui KEY=VALUE: replace a key (an empty value removes it), unknown keys are appended
            w = ui.split("\\")[1:]
            keys = list(zip(w[0::2], w[1::2]))
            for k, v in self.ui_set:
                keys = [(a, v if a == k else b) for a, b in keys] if any(a == k for a, _ in keys) else keys + [(k, v)]
            ui = "".join("\\%s\\%s" % (a, b) for a, b in keys if b != "")
        return ui + self.extra

    def handle_oob(self, text):
        line = text.split(b"\n")[0].decode("latin-1", "replace")
        if self.verbose:
            print("OOB", repr(text[:200]))
        if line.startswith("challenge"):
            ch = line.split()[1]
            self.oob('connect 33 %d %s %d 3.14 "%s"\n' % (self.qport, ch, int(self.t0), self.userinfo()))
            self.state = "connecting"
        elif line.startswith("client_connect"):
            if self.state != "connecting":
                return
            self.reset_chan()
            self.sess.new_connection()
            self.stringcmd("new")
            self.state = "signon"
        elif line.startswith("print"):
            print("server:", text[6:].decode("latin-1", "replace").strip())
        elif line.startswith("rejected"):
            print("rejected:", line)
            self.state = "dead"

    def handle_seq(self, data):
        m = sofnet.Msg(data)
        w1, w2 = m.ulong(), m.ulong()
        seq, rel = w1 & 0x7FFFFFFF, w1 >> 31
        ack, ackrel = w2 & 0x7FFFFFFF, w2 >> 31
        if seq <= self.in_seq:
            return
        if ackrel == self.rel_seq:
            self.rel_buf = b""
        self.in_seq = seq
        self.in_ack = ack
        self.in_rel_ack = ackrel
        if rel:
            self.in_rel_seq ^= 1
        ctx = {"t": time.time() - self.t0, "seq": seq, "dir": "s2c"}
        try:
            self.sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001
            print("parse error seq %d: %s" % (seq, ex))
        if "frame" in ctx:
            self.last_frame = ctx["frame"]
        for lvl, s in ctx.get("prints", []):
            self.log.append((round(ctx["t"], 2), str(lvl), s.decode("latin-1", "replace") if isinstance(s, bytes) else s))
            if self.verbose:
                print("print", lvl, s)
        if ctx.get("reconnect"):
            self.state = "challenge"
            self.oob("getchallenge %d\n" % int(self.t0))
        while self.sess.stuff:
            _, s = self.sess.stuff.pop(0)
            self.stuff(s.decode("latin-1"))

    def stuff(self, text):
        for line in text.split("\n"):
            line = line.strip()
            if not line:
                continue
            if self.verbose:
                print("stuff", line)
            if line.startswith("cmd "):
                self.stringcmd(line[4:] + (" " if line.startswith("cmd begin") else ""))
                if line.startswith("cmd begin"):
                    self.state = "active"
                    self.begun_at = time.time()
            elif line.startswith("precache"):
                self.stringcmd("sv_precache " + line.split()[1])
            elif line == "reconnect":
                self.state = "challenge"
                self.oob("getchallenge %d\n" % int(self.t0))
            elif line == "changing":
                pass

    def run_script(self):
        if self.begun_at is None:
            return True
        t = time.time() - self.begun_at
        while self.script and self.script[0][0] <= t:
            _, act = self.script.pop(0)
            w = act.split()
            if w[0] == "move":
                self.cmd["forward"], self.cmd["side"], self.cmd["up"] = (int(x) for x in w[1:4])
            elif w[0] in ("yaw", "pitch"):
                a = list(self.cmd["angles"])
                a[1 if w[0] == "yaw" else 0] = int(float(w[1]) * 65536 / 360) & 0xFFFF
                self.cmd["angles"] = tuple(a)
            elif w[0] == "aim":
                self.aim = int(w[1])
            elif w[0] == "approach":
                n, dist = int(w[1]), float(w[2])
                self.approach = (n, dist) if n else None
                self.aim = n
                if not n:
                    self.cmd["forward"] = 0
            elif w[0] == "buttons":
                self.cmd["buttons"] = int(w[1], 0)
            elif w[0] == "lean":
                self.cmd["lean"] = int(w[1])
            elif w[0] == "fire":
                self.cmd["fire"], self.cmd["altfire"] = float(w[1]), float(w[2])
            elif w[0] == "light":
                self.cmd["light"] = int(w[1])
            elif w[0] in ("say", "cmd"):
                self.stringcmd(act[len(w[0]) + 1:] if w[0] == "cmd" else act)
            elif w[0] == "userinfo":
                # change userinfo keys in the game: clc_userinfo (3) with the whole new string (SPEC-201)
                # values may contain spaces: "userinfo skin=dekker teamname=The Order"
                self.ui_set = self.ui_set + re.findall(r"(\w+)=(.*?)(?=\s+\w+=|$)", act[len(w[0]) + 1:].strip())
                self.msg += b"\x03" + self.userinfo().encode("latin-1") + b"\0"
            elif w[0] == "rcon":
                # out-of-band remote console (runtest.py starts the server with rcon_password g18); the reply is
                # printed as "server: ..." to client.out
                self.oob("rcon g18 %s\n" % act[5:])
            elif w[0] == "quit":
                return False
        return True

    def run(self, seconds):
        self.oob("getchallenge %d\n" % int(self.t0))
        last_send = 0
        last_oob = time.time()
        # seconds count from the client's "begin" (so runs with slow and fast signons are comparable);
        # the signon itself may take up to 30 s
        end = time.time() + 30 + seconds
        while time.time() < end and self.state != "dead":
            if self.begun_at and end > self.begun_at + seconds:
                end = self.begun_at + seconds
            try:
                data, addr = self.sock.recvfrom(65536)
                if addr != self.addr:
                    # another server's packet (a concurrent test run): never let it drive this client
                    self.foreign += 1
                    continue
                self.rec("s2c", data)
                if data[:4] == b"\xff\xff\xff\xff":
                    self.handle_oob(data[4:])
                elif self.state in ("signon", "active"):
                    self.handle_seq(data)
            except socket.timeout:
                pass
            except ConnectionResetError:
                time.sleep(0.2)
            now = time.time()
            if self.state == "challenge" and now - last_oob > 3:
                self.oob("getchallenge %d\n" % int(self.t0))
                last_oob = now
            if not self.run_script():
                break
            if self.state in ("signon", "active") and now - last_send >= 0.05:
                last_send = now
                if self.state == "active":
                    self.transmit(self.move_msg)
                else:
                    self.transmit(b"")
        if self.state in ("signon", "active"):
            for _ in range(3):
                self.transmit(b"\x04disconnect")
        self.out.close()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="127.0.0.1:27910")
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="G18bot")
    ap.add_argument("--clientport", type=int, default=0, help="local UDP port (the package's base + 1 …)")
    ap.add_argument("--userinfo-extra", default="")
    ap.add_argument("--ui", action="append", default=[], help="userinfo KEY=VALUE (empty value removes the key)")
    ap.add_argument("--script", default="")
    ap.add_argument("--log")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args(argv)
    script = []
    for step in filter(None, (s.strip() for s in a.script.split(";"))):
        t, _, act = step.partition(":")
        script.append((float(t), act.strip()))
    c = Client(a.server, a.out, a.name, a.userinfo_extra, script, a.verbose, a.clientport)
    c.ui_set = [tuple(u.split("=", 1)) for u in a.ui]
    c.run(a.seconds)
    if a.log:
        with open(a.log, "w", encoding="utf-8") as f:
            for t, lvl, s in c.log:
                f.write("%7.2f [%s] %s\n" % (t, lvl, s.rstrip()))
    print("state %s, frames %d, configstrings %d, baselines %d%s" % (
        c.state, len(c.sess.frames_seen), len(c.sess.cs), len(c.sess.baselines),
        ", FOREIGN PACKETS %d (discard this run)" % c.foreign if c.foreign else ""))


if __name__ == "__main__":
    main(sys.argv[1:])
