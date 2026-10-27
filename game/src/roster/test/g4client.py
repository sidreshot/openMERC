#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G4 - G18's headless client (game/test/sofclient.py) bound to a fixed local UDP port instead of an
# ephemeral one: the package's client port of docs/process/testing.md "Network ports" (G4: base 29470,
# client 29471). After the run it reports every server address that sent to this client; more than one means a
# foreign server reached it and the run must be discarded.
#
# Aiming (G4's own extension of the client, sofclient.py itself is not changed): extra script actions that keep
# the view on a point every user command, computed from the last received frame (player origin + eye height):
#   aimpos <x> <y> <z>               a fixed world point
#   aimlocal <ent> <fx> <ly> <uz>    a point fixed to an entity: origin + forward*fx + left*ly + up*uz, with the
#                                    entity's current angles (e.g. a helicopter's crew head, offsets from rg_trace)
#   aimoff                           stop aiming (sofclient's own "aim <ent>" still works as before)
# Each aim update is logged (every 0.5 s) to the --log file as "[aim] ...".
# Implements: test harness only (SPEC-200..203 client side through sofclient.py; SPEC-202 6.1 command angles)
#
# usage: g4client.py --clientport 29471 <sofclient.py arguments>
import json
import math
import socket
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[4]
sys.path.insert(0, str(REPO / "game" / "test"))

EYE = 22.0  # standing eye height above the player origin, as sofclient's "aim"


def angle_vectors(angles):
    """Quake II AngleVectors (degrees, pitch/yaw/roll) -> forward, right, up."""
    p, y, r = (math.radians(a) for a in angles)
    sp, cp, sy, cy, sr, cr = math.sin(p), math.cos(p), math.sin(y), math.cos(y), math.sin(r), math.cos(r)
    fwd = (cp * cy, cp * sy, -sp)
    right = (-sr * sp * cy + cr * sy, -sr * sp * sy - cr * cy, -sr * cp)
    up = (cr * sp * cy + sr * sy, cr * sp * sy - sr * cy, cr * cp)
    return fwd, right, up


def install_aim(sofclient):
    C = sofclient.Client
    orig_run_script = C.run_script
    orig_aim_angles = C.aim_angles

    def target(self, ents):
        mode = self.g4aim
        if mode[0] == "pos":
            return list(mode[1])
        n, off = mode[1], mode[2]
        e = ents.get(n)
        if not e:
            return None
        f, r, u = angle_vectors(e["angles"])
        return [e["origin"][i] + f[i] * off[0] - r[i] * off[1] + u[i] * off[2] for i in range(3)]

    def aim_angles(self):
        mode = getattr(self, "g4aim", None)
        if not mode:
            return orig_aim_angles(self)
        fr = self.sess.frames.get(self.last_frame)
        if not fr:
            return None
        ents, ps = fr
        t = target(self, ents)
        if t is None:
            return None
        me = [ps["origin"][i] / 8.0 for i in range(3)]
        me[2] += EYE
        d = [t[i] - me[i] for i in range(3)]
        yaw = math.degrees(math.atan2(d[1], d[0]))
        pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        now = time.time()
        if now - getattr(self, "g4aimlog", 0) >= 0.5:
            self.g4aimlog = now
            self.log.append((now - self.t0, "aim", "frame %d eye (%.1f %.1f %.1f) target (%.1f %.1f %.1f) "
                             "yaw %.2f pitch %.2f" % (self.last_frame, me[0], me[1], me[2], t[0], t[1], t[2],
                                                      yaw, pitch)))
        da = ps["delta_angles"]
        return (int(pitch * 65536 / 360 - da[0]) & 0xFFFF, int(yaw * 65536 / 360 - da[1]) & 0xFFFF, 0)

    def run_script(self):
        if self.begun_at is None:
            return True
        t = time.time() - self.begun_at
        while self.script and self.script[0][0] <= t:
            item = self.script.pop(0)
            w = item[1].split()
            if w[0] == "aimpos":
                self.g4aim = ("pos", [float(x) for x in w[1:4]])
                self.aim = -1  # truthy: move_msg asks aim_angles every command
            elif w[0] == "aimlocal":
                self.g4aim = ("local", int(w[1]), [float(x) for x in w[2:5]])
                self.aim = -1
            elif w[0] == "aimoff":
                self.g4aim = None
                self.aim = 0
            else:
                # one of sofclient's own actions: let it handle exactly this step
                rest, self.script = self.script, [item]
                ok = orig_run_script(self)
                self.script = rest
                if not ok:
                    return False
        return True

    C.aim_angles = aim_angles
    C.run_script = run_script


def main(argv):
    port = 29471
    if "--clientport" in argv:
        i = argv.index("--clientport")
        port = int(argv[i + 1])
        argv = argv[:i] + argv[i + 2:]
    orig_bind = socket.socket.bind

    def bind(self, addr):
        if tuple(addr) == ("127.0.0.1", 0):
            addr = ("127.0.0.1", port)
        return orig_bind(self, addr)
    socket.socket.bind = bind
    import sofclient  # noqa: E402
    install_aim(sofclient)
    sofclient.main(argv)
    # check: one server only (the capture records the sender of every server packet when it has one)
    out = argv[argv.index("--out") + 1] if "--out" in argv else None
    if out:
        connects = 0
        for line in open(out, encoding="utf-8"):
            r = json.loads(line)
            if r.get("dir") == "s2c" and bytes.fromhex(r["hex"])[4:].startswith(b"client_connect"):
                connects += 1
        if connects != 1:
            print("WARNING: %d client_connect replies in the capture (expected 1): discard this run" % connects)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
