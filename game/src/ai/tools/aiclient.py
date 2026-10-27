#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G2 - G18's headless client (game/test/sofclient.py) with waypoint steering for flythroughs of a level:
# after `--route-start` seconds the client flies from waypoint to waypoint (cheats `phantom` = noclip and `heretic` =
# god are sent by the ordinary --script), steering with its own player-state origin and delta angles, and dwells at
# each waypoint so that the characters there can perceive and react to it. The same route in the original game and
# in OpenMerc shows which characters act in one and not in the other (aidiff.py).
# Implements: SPEC-700, SPEC-701, SPEC-710 (black-box verification harness)
#
# usage: aiclient.py <sofclient.py arguments> --route "x y z [r [dwell]];..." [--route-start 16] [--speed 400]
#        [--keep-noclip]
#        (r: arrival radius, default 160 units; dwell: seconds at the waypoint, default 4). At each waypoint the
#        client sends `phantom` (noclip off: the player drops to the floor, is solid and touches triggers - the
#        original game does not seem to perceive a noclip player) and `phantom` again before it moves on;
#        --keep-noclip leaves noclip on throughout.

import argparse
import math
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[2] / "test"))
import sofclient  # noqa: E402


def parse_route(text):
    route = []
    for item in filter(None, (s.strip() for s in text.split(";"))):
        w = item.split()
        x, y, z = (float(v) for v in w[:3])
        r = float(w[3]) if len(w) > 3 else 160.0
        dwell = float(w[4]) if len(w) > 4 else 4.0
        route.append((x, y, z, r, dwell))
    return route


class RouteClient(sofclient.Client):
    def __init__(self, *args, route=(), route_start=0.0, speed=400, keep_noclip=False, **kw):
        super().__init__(*args, **kw)
        self.keep_noclip = keep_noclip
        self.last_toggle = 0.0
        self.god = None            # from the server's "godmode ON/OFF" prints (sofclient logs every print)
        self.last_heretic = 0.0
        self.log_seen = 0
        self.route = list(route)
        self.total = len(self.route)
        self.route_start = route_start
        self.speed = speed
        self.dwell_until = None
        self.reached = []
        self.last_pos = None
        self.stuck_since = None

    def steer(self):
        now = time.time()
        if not self.route or not self.begun_at or now - self.begun_at < self.route_start:
            return
        fr = self.sess.frames.get(self.last_frame)
        if not fr:
            return
        ps = fr[1]
        if "origin" not in ps or "delta_angles" not in ps:
            return
        me = [ps["origin"][i] / 8.0 for i in range(3)]
        x, y, z, r, dwell = self.route[0]
        d = [x - me[0], y - me[1], z - me[2]]
        dist = math.sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])
        t = now - self.begun_at
        # noclip state from the player state (SPEC-314 movement types: 0 normal, 1 noclip; pm_type is sent only
        # when non-zero, SPEC-202): `phantom` toggles it, so it is sent only when the state is one of the two and
        # differs from what the route needs (flying: on; dwelling: off unless --keep-noclip); a frozen player
        # (cinematic) is left alone
        # god mode is a toggle too (`heretic`): it is sent once the player is in a normal state and again whenever
        # the server's reply says OFF (a command sent during an intro cinematic is lost)
        for i in range(self.log_seen, len(self.log)):
            txt = self.log[i][2]
            if "godmode ON" in txt:
                self.god = True
            elif "godmode OFF" in txt:
                self.god = False
        self.log_seen = len(self.log)
        # noclip is switched off only once the player has settled at the waypoint (0.5 s after arrival): the toggle
        # takes effect up to 0.6 s after it is decided, and a player still flying lands tens of units away from the
        # same spot in another run (tsr1 token booth: z -231 vs -259), which changes the triggers it touches
        settled = self.dwell_until is not None and now >= self.dwell_until - self.route[0][4] + 0.5
        want = 1 if (not settled or self.keep_noclip) else 0
        pm = ps.get("pm_type", 0)
        if isinstance(pm, (tuple, list)):  # sofnet keeps single fields as 1-tuples
            pm = pm[0]
        cin = ps.get("cinfreeze", 0)
        if isinstance(cin, (tuple, list)):
            cin = cin[0]
        if cin:
            pm = -1  # intro cinematic (player-state cinfreeze): commands are dropped by the server, send none
        if pm in (0, 1) and self.god is not True and now - self.last_heretic > 1.5:
            self.stringcmd("heretic")
            self.last_heretic = now
        if pm in (0, 1) and pm != want and now - self.last_toggle > 0.6:
            self.stringcmd("phantom")
            self.last_toggle = now
            self.log.append((t, "route", "phantom -> %s" % ("on" if want else "off")))
        if self.dwell_until is not None:
            self.cmd["forward"] = 0
            if now >= self.dwell_until:
                self.route.pop(0)
                self.dwell_until = None
            return
        if dist <= r:
            self.cmd["forward"] = 0
            self.dwell_until = now + dwell
            self.reached.append((t, x, y, z))
            self.log.append((t, "route", "reached %g %g %g (%d/%d) at %.0f %.0f %.0f" % (
                x, y, z, len(self.reached), self.total, me[0], me[1], me[2])))
            self.stuck_since = None
            return
        if pm != 1:
            self.cmd["forward"] = 0
            return  # wait for noclip before flying (the player would walk into walls)
        yaw = math.degrees(math.atan2(d[1], d[0]))
        pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        da = ps["delta_angles"]
        self.cmd["angles"] = (int(pitch * 65536 / 360 - da[0]) & 0xFFFF, int(yaw * 65536 / 360 - da[1]) & 0xFFFF, 0)
        self.cmd["forward"] = self.speed
        # a player that does not move while steering (cinematic freeze, no noclip) is reported once per 5 s
        pos = tuple(round(v) for v in me)
        if pos != self.last_pos:
            self.last_pos = pos
            self.stuck_since = now
        elif self.stuck_since and now - self.stuck_since > 5:
            self.log.append((t, "route", "not moving at %s towards %g %g %g" % (pos, x, y, z)))
            self.stuck_since = now

    def move_msg(self, seq, hdr):
        self.steer()
        return super().move_msg(seq, hdr)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--server", default="127.0.0.1:27910")
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--out", required=True)
    ap.add_argument("--name", default="G2bot")
    ap.add_argument("--clientport", type=int, default=0)
    ap.add_argument("--userinfo-extra", default="")
    ap.add_argument("--ui", action="append", default=[])
    ap.add_argument("--script", default="")
    ap.add_argument("--log")
    ap.add_argument("-v", "--verbose", action="store_true")
    ap.add_argument("--route", default="")
    ap.add_argument("--route-start", type=float, default=0.0)
    ap.add_argument("--speed", type=int, default=400)
    ap.add_argument("--keep-noclip", action="store_true")
    a = ap.parse_args(argv)
    script = []
    for step in filter(None, (s.strip() for s in a.script.split(";"))):
        t, _, act = step.partition(":")
        script.append((float(t), act.strip()))
    c = RouteClient(a.server, a.out, a.name, a.userinfo_extra, script, a.verbose, a.clientport,
                    route=parse_route(a.route), route_start=a.route_start, speed=a.speed,
                    keep_noclip=a.keep_noclip)
    c.ui_set = [tuple(u.split("=", 1)) for u in a.ui]
    c.run(a.seconds)
    if a.log:
        with open(a.log, "w", encoding="utf-8") as f:
            for t, lvl, s in c.log:
                f.write("%7.2f [%s] %s\n" % (t, lvl, s.rstrip()))
    print("state %s, frames %d, configstrings %d, baselines %d, route %d/%d waypoints%s" % (
        c.state, len(c.sess.frames_seen), len(c.sess.cs), len(c.sess.baselines), len(c.reached), c.total,
        ", FOREIGN PACKETS %d (discard this run)" % c.foreign if c.foreign else ""))


if __name__ == "__main__":
    main(sys.argv[1:])
