#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G9 - black-box game-mode tests: runs the original SoF.exe as a hidden dedicated server in the G9 test
# copy (never the oracle) with a chosen gamex86.dll, connects several scripted headless clients at once and
# records what each client receives: configstrings, string-package prints with decoded arguments (scoreboards,
# status layouts, mode messages), plain prints, name-colour tables (svc 33), score/meter stats, map changes.
# Implements: SPEC-790 (verification of scores, teams, scoreboards, level end and rotation, game log)
#
# usage: modetest.py --dll orig|om|<path> --map dm/jpndm2 --dm 4 --tag NAME [--set "cvar value"] ...
#          --client "name|key=val,key=val|script|delay" (repeatable)  [--seconds 30] [--port 29490]
# A client script is the sofclient.py script ("t:action;t:action"), plus: "kill", "score", "help" shortcuts.
# Output: <openmerc-data>/g9/<tag>/{<name>.json, <name>.txt, sof.log, gamelog/*}

import argparse
import json
import math
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))
import sofclient  # noqa: E402
import sofnet  # noqa: E402

TEST = Path(r"D:\Games\SoF-test-G9")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\g9")
STRINGS = Path(r"C:\openmerc-data\ui\strings")
OM_DLL = DATA / "dll" / "om.dll"  # snapshot of build-G9-x86/gamex86.dll (the build tree changes under test)
DASHES = "\n" + "-" * 37

# ------------------------------------------------------------------------------------------------------------
# string packages (from the stock data, decoded by tools/formats/strings.py)

_PKGS = None


def packages():
    global _PKGS
    if _PKGS is None:
        _PKGS = {}
        for pak in ("pak0", "pak2", "pak3", "gs"):  # later wins (gs, pak3, pak2 above pak0; zmain skipped)
            for f in sorted((STRINGS / pak / "strip").glob("*.sp.json")):
                d = json.loads(f.read_text(encoding="utf-8"))
                ents = {}
                for e in d["entries"]:
                    txt = e.get("text", {}).get("ENGLISH") or e.get("texts", {}).get("ENGLISH") or ""
                    ents[e["index"]] = (e.get("reference"), txt, e.get("flags", []))
                _PKGS[d["id"]] = (d["reference"], ents)
    return _PKGS


def decode_sp(sid, data):
    pkg, idx = sid >> 8, sid & 0xFF
    p = packages().get(pkg)
    if not p:
        return "?%04x" % sid, ""
    name, ents = p
    ent = ents.get(idx)
    if not ent:
        return "%s_#%d" % (name, idx), ""
    ref, fmt, _ = ent
    out, i, pos = [], 0, 0
    while i < len(fmt):
        c = fmt[i]
        if c != "%":
            out.append(c)
            i += 1
            continue
        j = i + 1
        while j < len(fmt) and fmt[j] in "0123456789-+ .#lh":
            j += 1
        conv = fmt[i:j + 1]
        k = fmt[j] if j < len(fmt) else ""
        try:
            if k == "p":
                if j + 1 < len(fmt) and fmt[j + 1] == "s":
                    j += 1
                out.append("<%d>" % data[pos])
                pos += 1
            elif k in "du" and "h" in conv:
                v = struct.unpack_from("<h" if k == "d" else "<H", data, pos)[0]
                pos += 2
                out.append(str(v))
            elif k in "dui":
                v = struct.unpack_from("<i", data, pos)[0]
                pos += 4
                out.append(str(v))
            elif k == "s":
                e = data.index(0, pos)
                out.append(data[pos:e].decode("latin-1"))
                pos = e + 1
            elif k == "%":
                out.append("%")
            else:
                out.append(conv)
        except (IndexError, ValueError, struct.error):
            out.append("<?>")
        i = j + 1
    return "%s_%s" % (name, ref), "".join(out)


# ------------------------------------------------------------------------------------------------------------
# recording session / client

STATS = {8: "flags", 9: "layouts", 10: "score", 11: "meter"}


class RecSession(sofnet.Session):
    def __init__(self, owner):
        super().__init__()
        self.owner = owner
        self.last_ps = {}

    def ev(self, kind, **kw):
        self.owner.event(kind, **kw)

    def svc_14(self, m, ctx):
        super().svc_14(m, ctx)
        lv = self.serverdata["levelname"]
        self.ev("serverdata", level=lv.decode("latin-1") if isinstance(lv, bytes) else lv,
                dm=self.serverdata["deathmatch"],
                player=self.serverdata["playernum"])

    def svc_15(self, m, ctx):
        p = m.p
        idx = m.short()
        m.p = p
        super().svc_15(m, ctx)
        v = self.cs.get(idx, "")
        self.ev("cs", idx=idx, value=v.decode("latin-1") if isinstance(v, bytes) else v)

    def svc_11(self, m, ctx):
        lvl = m.byte()
        s = m.string()
        self.ev("print", level=lvl, text=s.decode("latin-1") if isinstance(s, bytes) else s)

    def svc_13(self, m, ctx):
        p = m.p
        s = m.string()
        m.p = p
        super().svc_13(m, ctx)
        self.ev("stuff", text=s.decode("latin-1") if isinstance(s, bytes) else s)

    def svc_17(self, m, ctx):
        s = m.string()
        self.ev("center", text=s.decode("latin-1") if isinstance(s, bytes) else s)

    def svc_33(self, m, ctx):
        p = m.p
        count = m.byte()
        covered, runs = 0, []
        while covered < count:
            b = m.byte()
            if b & 0x80:
                first, last = m.byte(), m.byte()
                runs.append((b & 0x7F, first, last))
                covered += last - first + 1
            else:
                s = m.byte()
                runs.append((b & 0x7F, s, s))
                covered += 1
        self.ev("names", count=count, runs=runs, raw=bytes(m.d[p:m.p]).hex())

    def _sp(self, sid, data, obit=False):
        name, text = decode_sp(sid, data)
        self.ev("obit" if obit else "sp", id="%04x" % sid, name=name, data=data.hex(), text=text)

    def svc_34(self, m, ctx):
        self._sp(m.short(), b"")

    def svc_36(self, m, ctx):
        sid = m.short()
        n = m.byte()
        self._sp(sid, bytes(m.take(n)))

    def svc_37(self, m, ctx):
        sid = m.short()
        n = m.short()
        self._sp(sid, bytes(m.take(n)))

    def svc_39(self, m, ctx):
        sid = m.short()
        n = m.byte()
        self._sp(sid, bytes(m.take(n)), obit=True)

    def svc_23(self, m, ctx):
        super().svc_23(m, ctx)
        f = ctx.get("frame")
        if f is None or f not in self.frames:
            return
        ents, ps = self.frames[f]
        self.owner.origin = [v / 8.0 for v in ps["origin"]]
        self.owner.ents = ents
        self.owner.delta = list(ps["delta_angles"])
        cur = {"pm_type": ps["pm_type"][0], "pm_flags": ps["pm_flags"][0]}
        for i, n in STATS.items():
            cur[n] = ps["stats"][i]
        ch = {k: v for k, v in cur.items() if self.last_ps.get(k) != v}
        if ch:
            self.ev("ps", frame=f, **ch)
        self.last_ps = cur


class RecClient(sofclient.Client):
    serial = 0

    def __init__(self, server, out, name, userinfo, script, deadline, lport=0):
        super().__init__(server, out, name, "", script, False)
        if lport:
            # the package's own client ports (testing notes, "Network ports"), never an ephemeral/default one
            self.sock.close()
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.bind(("127.0.0.1", lport))
            self.sock.settimeout(0.02)
        self.sess = RecSession(self)
        RecClient.serial += 1
        self.qport = (self.qport + 977 * RecClient.serial) & 0xFFFF  # distinct netchan qport per client
        self.uinfo = userinfo
        self.events = []
        self.deadline = deadline
        self.origin = None
        self.delta = [0, 0, 0]
        self.goal = None
        self.aim = None
        self.ents = {}
        self.ticks = 0

    def event(self, kind, **kw):
        t = round(time.time() - (self.begun_at or self.t0), 2)
        kw.update(t=t, kind=kind, active=self.begun_at is not None)
        self.events.append(kw)

    def userinfo(self):
        ui = {"name": self.name, "skin": "mullins", "teamname": "mullins", "rate": "15000", "msg": "0",
              "fov": "95", "bestweap": "safe", "team_red_blue": "0", "spectator": "0", "predicting": "0",
              "cl_violence": "0"}
        ui.update(self.uinfo)
        return "".join("\\%s\\%s" % kv for kv in ui.items())

    def run_script(self):
        if self.begun_at is None:
            return True
        t = time.time() - self.begun_at
        while self.script and self.script[0][0] <= t:
            _, act = self.script[0]
            w = act.split()
            if w and w[0] in ("kill", "score", "help", "putaway"):
                self.script.pop(0)
                self.stringcmd(w[0])
                continue
            if w and w[0] == "goto":
                # steer towards a world point (used with the `phantom` noclip cheat); "goto off" stops
                self.script.pop(0)
                self.goal = None if w[1] == "off" else [float(x) for x in w[1:4]]
                if self.goal is None:
                    self.cmd["forward"] = 0
                continue
            if w and w[0] == "aim":
                # aim at an entity (a player: slot + 1) and fire the primary attack in bursts; "aim off" stops
                self.script.pop(0)
                self.aim = None if w[1] == "off" else int(w[1])
                if self.aim is None:
                    self.cmd["buttons"] &= ~1
                continue
            if w and w[0] == "userinfo":
                self.script.pop(0)
                k, v = w[1], " ".join(w[2:])
                self.uinfo[k] = v
                self.msg += b"\x03" + self.userinfo().encode("latin-1") + b"\0"
                continue
            break
        r = super().run_script()
        self.steer()
        return r

    def steer(self):
        if self.aim and self.origin and self.aim in self.ents:
            t = self.ents[self.aim]["origin"]
            eye = (self.origin[0], self.origin[1], self.origin[2] + 22)
            d = [t[0] - eye[0], t[1] - eye[1], t[2] + 8 - eye[2]]
            yaw = math.degrees(math.atan2(d[1], d[0]))
            pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
            a = list(self.cmd["angles"])
            a[0] = (int(pitch * 65536 / 360) - self.delta[0]) & 0xFFFF
            a[1] = (int(yaw * 65536 / 360) - self.delta[1]) & 0xFFFF
            self.cmd["angles"] = tuple(a)
            on = int(time.time() * 5) % 2 == 0  # press / release every 200 ms (semi-automatic weapons)
            self.cmd["buttons"] = (self.cmd["buttons"] | 1) if on else (self.cmd["buttons"] & ~1)
        if not self.goal or not self.origin:
            return
        d = [g - o for g, o in zip(self.goal, self.origin)]
        dist = math.sqrt(sum(x * x for x in d))
        if dist < 12:
            self.cmd["forward"] = 0
            return
        yaw = math.degrees(math.atan2(d[1], d[0]))
        pitch = -math.degrees(math.atan2(d[2], math.hypot(d[0], d[1])))
        a = list(self.cmd["angles"])
        a[0] = (int(pitch * 65536 / 360) - self.delta[0]) & 0xFFFF
        a[1] = (int(yaw * 65536 / 360) - self.delta[1]) & 0xFFFF
        self.cmd["angles"] = tuple(a)
        self.cmd["forward"] = 200 if dist > 64 else 60


def parse_client(spec):
    parts = spec.split("|")
    name = parts[0]
    ui = {}
    if len(parts) > 1 and parts[1]:
        for kv in parts[1].split(","):
            k, _, v = kv.partition("=")
            ui[k] = v
    script = []
    if len(parts) > 2:
        for step in filter(None, (s.strip() for s in parts[2].split(";"))):
            t, _, act = step.partition(":")
            script.append((float(t), act.strip()))
    delay = float(parts[3]) if len(parts) > 3 and parts[3] else 0.0
    return name, ui, script, delay


# ------------------------------------------------------------------------------------------------------------
# server

def install(dll):
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    src = {"orig": TEST / "base" / "gamex86.dll.orig", "om": OM_DLL}.get(dll, Path(dll))
    dst = TEST / "base" / "gamex86.dll"
    for _ in range(50):
        try:
            shutil.copyfile(src, dst)
            return src
        except PermissionError:
            time.sleep(0.2)
    raise SystemExit("cannot replace " + str(dst))


def summary(c):
    lines = []
    for e in c.events:
        k = e["kind"]
        if k == "cs":
            v = e["value"]
            lines.append("%7.2f cs %d = %r" % (e["t"], e["idx"], v))
        elif k in ("sp", "obit"):
            lines.append("%7.2f %s %s %s | %s" % (e["t"], k, e["id"], e["name"], e["text"].rstrip()))
        elif k == "print":
            lines.append("%7.2f print[%d] %s" % (e["t"], e["level"], e["text"].rstrip()))
        elif k == "names":
            lines.append("%7.2f names count %d runs %s" % (e["t"], e["count"], e["runs"]))
        elif k == "ps":
            d = {x: e[x] for x in e if x not in ("t", "kind", "active", "frame")}
            lines.append("%7.2f ps %s" % (e["t"], d))
        elif k == "serverdata":
            lines.append("%7.2f serverdata level %s dm %d player %d" % (e["t"], e["level"], e["dm"], e["player"]))
        elif k in ("stuff", "center"):
            lines.append("%7.2f %s %r" % (e["t"], k, e["text"]))
    return "\n".join(lines) + "\n"


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="om")
    ap.add_argument("--map", default="dm/jpndm2")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--dm", default="1")
    ap.add_argument("--maxclients", default="8")
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--wait", type=float, default=30)
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--client", action="append", default=[])
    ap.add_argument("--port", type=int, default=29490)
    ap.add_argument("--keep-logs", action="store_true")
    a = ap.parse_args(argv)

    out = DATA / a.tag
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    user = TEST / ("u9_" + a.tag.replace("/", "_"))
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    logs = TEST / "logs"
    if logs.exists() and not a.keep_logs:
        shutil.rmtree(logs)
    src = install(a.dll)
    # `+set developer 1` on the command line makes SoF.exe open a visible "Developer Mode" message box; the same
    # value set from an exec'd config does not
    # The test cvars go into the same config (no command-line quoting). sv_maplistfile is emptied unless the case
    # sets it: the engine otherwise replaces sv_maplist from maplists/ravdm.lst at every map start (SPEC-313).
    cfgname = "g9_%s.cfg" % a.tag.replace("/", "_")
    lines = ["set developer 1", 'set sv_maplistfile ""']
    for s in a.set:
        name, _, value = s.partition(" ")
        lines.append('set %s "%s"' % (name, value))
    (TEST / "base" / cfgname).write_text("\n".join(lines) + "\n", encoding="ascii")
    args = ("+set dedicated 1 +set user ./%s +set logfile 2 +set ip 127.0.0.1 "
            "+set port %d +set deathmatch %s +set maxclients %s +exec %s"
            % (user.name, a.port, a.dm, a.maxclients, cfgname)).split()
    args += ["+map", a.map]
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE: never shown, never focused
    srv = subprocess.Popen([str(TEST / "SoF.exe")] + args, cwd=str(TEST), startupinfo=si)
    log = user / "sof.log"
    t0 = time.time()
    ok = False
    while time.time() - t0 < a.wait:
        if srv.poll() is not None:
            break
        if log.exists():
            txt = log.read_text(encoding="latin-1", errors="replace")
            i = txt.find("SpawnServer")
            if i >= 0 and (DASHES in txt[i:] or "Error" in txt[i:]):
                time.sleep(1.0)
                ok = True
                break
        time.sleep(0.25)
    clients = []
    try:
        if ok and srv.poll() is None:
            deadline = time.time() + a.seconds
            threads = []
            for spec in a.client:
                name, ui, script, delay = parse_client(spec)
                c = RecClient("127.0.0.1:%d" % a.port, str(out / ("%s.cap.jsonl" % name)), name, ui, script,
                              deadline, a.port + 1 + len(clients))
                clients.append(c)

                # every client runs `seconds` (less its connect delay) from its own "begin": the signon takes 5-10 s
                # and differs between runs, so a deadline counted from the server start cut late script steps
                def go(c=c, delay=delay):
                    if delay:
                        time.sleep(delay)
                    c.run(max(1.0, a.seconds - delay))
                th = threading.Thread(target=go, daemon=True)
                threads.append(th)
                th.start()
            for th in threads:
                th.join(a.seconds + 45)
        elif ok:
            time.sleep(a.seconds)
    finally:
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if log.exists():
            shutil.copyfile(log, out / "sof.log")
        if logs.exists():
            shutil.copytree(logs, out / "gamelog")
        for c in clients:
            (out / ("%s.json" % c.name)).write_text(json.dumps(c.events, indent=0), encoding="utf-8")
            (out / ("%s.txt" % c.name)).write_text(summary(c), encoding="utf-8")
            c.sock.close()  # frees the fixed client port for the next case run in the same process (sptest.py)
        print("server %s (dll %s); clients %s; output %s" % (
            "was running" if alive else "EXITED early", src.name, [(c.name, c.state) for c in clients], out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
