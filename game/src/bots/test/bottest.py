#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G5 - black-box bot test: run the original SoF.exe as a hidden dedicated server in the G5 test copy
# with a chosen gamex86.dll (original or OpenMerc), add bots from the command line, connect the headless
# protocol-33 client of G18 (game/test/sofclient.py) and record what reaches it: join times, names and skins
# (player-skin configstrings), and the bots' positions over time.
# Implements: SPEC-741 (test vectors 1-3: join delay, slots, names, roaming)
#
# usage: bottest.py run --dll orig|om|<path> --map dm/jpndm2 --tag T [--seconds 70] [--maxclients 8]
#                       [--cmd "bot_add 4 Kirt"] [--set "bot_fillserv 1"] [--script "..."]
#        bottest.py analyze <tag>               (re-run the analysis of a stored capture)
#        bottest.py compare <tagA> <tagB>       (side-by-side summary)
# Output: <openmerc-data>/g5/runs/<tag>/{cap.jsonl, sof.log, client.log, summary.json}
#
# The window is hidden (SW_HIDE) and never focused; input only through the command line and the client.

import argparse
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
# environment overrides let another package reuse the harness with its own test copy, output and build
# (F5: OM_G5_TEST, OM_G5_RUNS, OM_G5_DLL)
TEST = Path(os.environ.get("OM_G5_TEST", r"D:\Games\SoF-test-G5"))
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(os.environ.get("OM_G5_RUNS", r"C:\openmerc-data\g5\runs"))
WRS = Path(r"C:\openmerc-data\g5")
OM_DLL = Path(os.environ.get("OM_G5_DLL", str(REPO / "build-G5-x86" / "gamex86.dll")))
CLIENT = REPO / "game" / "test" / "sofclient.py"
DASHES = "\n" + "-" * 37

sys.path.insert(0, str(REPO / "tools" / "net"))
import sofnet  # noqa: E402

CS_PLAYERSKINS = 1415


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


def run(a):
    out = DATA / a.tag
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True, exist_ok=True)
    user = TEST / ("u_" + a.tag.replace("/", "_"))
    if user.exists():
        shutil.rmtree(user)
    user.mkdir()
    src = install(a.dll)
    # developer mode through a config file: "+set developer 1" on the command line opens a modal
    # "Developer Mode" message box first (hidden window: the server would wait forever)
    cfg = TEST / "base" / "g5dev.cfg"
    if not cfg.exists():
        cfg.write_bytes(b"set developer 1" + bytes([13, 10]))
    sets = " ".join("+set " + s for s in a.set)
    cmds = " ".join("+" + c for c in a.cmd)
    args = ("+set dedicated 1 +set rcon_password g5 +set user ./%s +set logfile 2 +exec g5dev.cfg +set ip 127.0.0.1 "
            "+set port %d +set deathmatch %s +set maxclients %s %s +map %s %s"
            % (user.name, a.port, a.dm, a.maxclients, sets, a.map, cmds))
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE
    t_start = time.time()
    srv = subprocess.Popen([str(TEST / "SoF.exe")] + args.split(), cwd=str(TEST), startupinfo=si)
    log = user / "sof.log"
    ok = False
    while time.time() - t_start < a.wait:
        if srv.poll() is not None:
            break
        if log.exists():
            txt = log.read_text(encoding="latin-1", errors="replace")
            i = txt.find("SpawnServer")
            if i >= 0 and (DASHES in txt[i:] or "Error" in txt[i:]):
                ok = True
                break
        time.sleep(0.05)
    t_map = time.time()
    meta = dict(dll=str(src), map=a.map, maxclients=a.maxclients, dm=a.dm, cmd=a.cmd, set=a.set,
                map_loaded_after=round(t_map - t_start, 2), client_wall0=None)
    try:
        if ok and srv.poll() is None:
            c = [sys.executable, str(Path(__file__).resolve()), "client", "--server", "127.0.0.1:%d" % a.port,
                 "--seconds", str(a.seconds), "--out", str(out / "cap.jsonl"), "--log", str(out / "client.log"),
                 "--name", a.name]
            if a.spectate:
                c += ["--spectate"]
            if a.script:
                c += ["--script", a.script]
            meta["client_wall0"] = time.time()
            r = subprocess.run(c, capture_output=True, text=True, timeout=a.seconds + 40)
            (out / "client.out").write_text(r.stdout + r.stderr, encoding="utf-8")
    finally:
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if log.exists():
            shutil.copyfile(log, out / "sof.log")
        shutil.rmtree(user, ignore_errors=True)
    meta["map_wall"] = t_map
    (out / "meta.json").write_text(json.dumps(meta, indent=1))
    print("server %s (dll %s)" % ("ran" if alive else "EXITED early", src.name))
    if (out / "cap.jsonl").exists():
        analyze(a.tag)


class Recorder(sofnet.Session):
    """Session that keeps, per received frame, the player entities and the skin configstrings with times."""

    def __init__(self):
        super().__init__()
        self.track = []      # (t, servertime, {num: (origin, angles, modelindex, frame)})
        self.cs_log = []     # (t, idx, text)
        self.sp_log = []     # (t, ids)
        self.print_log = []  # (t, text)
        self.pstrack = []    # (level time, pm_type, spectated entity, own origin)
        self.snd_log = []    # (t, entity, sound index, channel)  [F5: bot fire cadence, SPEC-AMEND-005 G5.3]
        self.shot_log = []   # (t, temp entity type, shooter entity, effect index)
        self.maxclients = 32

    def svc_10(self, m, ctx):
        p0 = m.p
        fl = m.d[p0]
        idx = m.d[p0 + 1] + (256 if fl & 0x20 else 0)
        q = p0 + 2 + (1 if fl & 0x01 else 0) + (1 if fl & 0x02 else 0) + (1 if fl & 0x10 else 0)
        ent = chan = None
        if fl & 0x08:
            ec = struct.unpack_from("<h", m.d, q)[0]
            ent, chan = ec >> 3, ec & 7
        super().svc_10(m, ctx)
        self.snd_log.append((round(ctx["t"], 3), ent, idx, chan))

    def svc_1(self, m, ctx):
        # multiplayer bullets (SPEC-201 temp entities 60-63: effect index, second index, shooter entity, ...)
        p0 = m.p
        t = m.d[p0]
        super().svc_1(m, ctx)
        if 60 <= t <= 63:
            self.shot_log.append((round(ctx["t"], 3), t, m.d[p0 + 3] if t != 63 else m.d[p0 + 2], m.d[p0 + 1]))

    def svc_15(self, m, ctx):
        idx = struct.unpack_from("<h", m.d, m.p)[0]
        super().svc_15(m, ctx)
        if idx is not None and CS_PLAYERSKINS <= idx < CS_PLAYERSKINS + 32:
            v = self.cs.get(idx)
            if isinstance(v, bytes):
                v = v.decode("latin-1", "replace")
            self.cs_log.append((round(ctx["t"], 3), idx, v))
        elif idx == 31:
            try:
                self.maxclients = int(self.cs.get(31) or 32)
            except (TypeError, ValueError):
                pass

    def svc_23(self, m, ctx):
        super().svc_23(m, ctx)
        fr = self.frames_seen[-1]
        ents = self.frames[fr["frame"]][0]
        pl = {}
        for num, e in ents.items():
            if 1 <= num <= 32:
                pl[num] = (tuple(round(x, 1) for x in e["origin"]), tuple(round(x, 1) for x in e["angles"]),
                           e["modelindex"], e["frame"])
        ps = self.frames[fr["frame"]][1]
        self.track.append((round(ctx["t"], 3), fr["time"], pl))
        self.pstrack.append((fr["time"], ps["pm_type"][0], ps["spectated"][0],
                             tuple(v * 0.125 for v in ps["origin"])))


def decode(path):
    recs = [json.loads(l) for l in open(path)]
    sess = Recorder()
    for r in recs:
        data = bytes.fromhex(r["hex"])
        if r["dir"] != "s2c":
            continue
        if data[:4] == b"\xff\xff\xff\xff":
            if data[4:].startswith(b"client_connect"):
                sess.new_connection()
            continue
        if len(data) < 8:
            continue
        m = sofnet.Msg(data)
        m.ulong()
        m.ulong()
        ctx = {"t": r["t"], "seq": 0, "dir": "s2c"}
        try:
            sess.parse_svc(m, ctx)
        except Exception as ex:  # noqa: BLE001
            sess.note("parse error", str(ex))
        for lvl, s in ctx.get("prints", []):
            sess.print_log.append((round(r["t"], 2), s.decode("latin-1", "replace") if isinstance(s, bytes) else s))
        if ctx.get("sp"):
            sess.sp_log.append((round(r["t"], 2), ctx["sp"]))
    return sess


def load_wrs(mapname):
    """waypoint id -> origin from the stock .wrs extracted under openmerc-data/g5 (SPEC-143 grammar)"""
    base = mapname.split("/")[-1]
    for pak in ("pak3", "pak2"):
        f = WRS / pak / "bot" / "wpfiles" / (base + ".wrs")
        if f.exists():
            out = {}
            n = 0
            for line in f.read_text(encoding="latin-1").splitlines():
                m = re.match(r"\s*(\d+)\s+(\d+)\s+\((-?\d+)\s+(-?\d+)\s+(-?\d+)\)", line)
                if m:
                    n += 1
                    out[n] = tuple(float(x) for x in m.groups()[2:])
            return out
    return {}


def analyze(tag):
    out = DATA / tag
    meta = json.loads((out / "meta.json").read_text())
    sess = decode(out / "cap.jsonl")
    # level time of the first frame the client received = offset of client time against level time
    if not sess.track:
        print("no frames")
        return
    t0, lt0, _ = sess.track[0]
    def level_time(t):
        return round(lt0 + (t - t0), 2)
    joins = {}
    for t, idx, text in sess.cs_log:
        n = idx - CS_PLAYERSKINS + 1
        joins.setdefault(n, []).append((level_time(t), text))
    # per player entity: first/last seen, path length, positions every 5 s
    paths = {}
    for t, lt, pl in sess.track:
        for n, (org, ang, mi, fr) in pl.items():
            p = paths.setdefault(n, dict(first=lt, last=lt, dist=0.0, prev=None, samples=[], frames=0))
            p["frames"] += 1
            if p["prev"] is not None:
                d = math.dist(org, p["prev"])
                if d < 200:  # ignore respawn teleports
                    p["dist"] += d
            p["prev"] = org
            p["last"] = lt
            if not p["samples"] or lt - p["samples"][-1][0] >= 5.0:
                p["samples"].append((round(lt, 1), org, round(ang[1], 1)))
    me = sess.serverdata["playernum"] + 1 if sess.serverdata else None
    # nearest-waypoint trace of every visible player (waypoints of the map's stock .wrs)
    wps = load_wrs(meta["map"])
    for n, p in paths.items():
        seq, visited = [], set()
        for t, lt, pl in sess.track:
            if n in pl and wps:
                org = pl[n][0]
                w = min(wps, key=lambda k: math.dist(org, wps[k]))
                visited.add(w)
                if not seq or seq[-1][1] != w:
                    seq.append((round(lt, 1), w))
        p["wp_seq"] = seq
        p["wp_visited"] = sorted(visited)
    # sounds started on player entities: per entity and sound name the level times and the intervals between them
    sounds = {}
    for t, ent, idx, chan in sess.snd_log:
        if ent is None or not 1 <= ent <= 32:
            continue
        name = sess.cs.get(291 + idx) or ("#%d" % idx)
        if isinstance(name, bytes):
            name = name.decode("latin-1", "replace")
        sounds.setdefault(str(ent), {}).setdefault(name, []).append(level_time(t))
    snd_summary = {}
    for ent, by in sounds.items():
        for name, ts in by.items():
            iv = [round(b - a, 2) for a, b in zip(ts, ts[1:])]
            snd_summary.setdefault(ent, {})[name] = dict(n=len(ts), first=ts[:3], intervals=iv[:60])
    # shots (temp entities 60-63) per shooter and muzzle effect: level times and intervals (one shot per frame
    # counted once: several pellets of a shotgun come in the same frame)
    shots = {}
    for t, te, shooter, eff in sess.shot_log:
        name = sess.cs.get(647 + eff) or ("#%d" % eff)
        if isinstance(name, bytes):
            name = name.decode("latin-1", "replace")
        lst = shots.setdefault(str(shooter), {}).setdefault(name, [])
        lt = level_time(t)
        if not lst or lt - lst[-1] > 0.05:
            lst.append(lt)
    shot_summary = {}
    for ent, by in shots.items():
        for name, ts in by.items():
            iv = [round(b - a, 2) for a, b in zip(ts, ts[1:])]
            shot_summary.setdefault(ent, {})[name] = dict(n=len(ts), first=ts[:3], intervals=iv[:80])
    chase = []
    for lt, pmt, spec, org in sess.pstrack:
        if not chase or chase[-1][1] != spec or chase[-1][2] != pmt:
            chase.append((round(lt, 1), spec, pmt))
    summary = dict(meta=meta, client_entity=me, maxclients=sess.maxclients, chase=chase[:40],
                   joins={str(k): v for k, v in sorted(joins.items())},
                   prints=sess.print_log[:80],
                   sp_prints=sess.sp_log[:80],
                   players={str(n): dict(first_seen=round(p["first"], 2), last_seen=round(p["last"], 2),
                                         frames_visible=p["frames"], path_length=round(p["dist"], 1),
                                         samples=p["samples"], wp_seq=p["wp_seq"][:300],
                                         wp_visited=p["wp_visited"])
                            for n, p in sorted(paths.items())},
                   sounds=snd_summary, shots=shot_summary,
                   frames=len(sess.track), first_frame_level_time=lt0,
                   findings={k: v[:3] for k, v in sess.findings.items()})
    (out / "summary.json").write_text(json.dumps(summary, indent=1))
    print_summary(tag, summary)


def print_summary(tag, s):
    print("== %s: %s map %s maxclients %s, client entity %s, %d frames (first at level time %.1f)" % (
        tag, Path(s["meta"]["dll"]).name, s["meta"]["map"], s["maxclients"], s["client_entity"], s["frames"],
        s["first_frame_level_time"]))
    for n, v in s["joins"].items():
        print("  skin cs ent %2s: %s" % (n, "; ".join("%.1f %r" % (t, (x or "")[:40]) for t, x in v)))
    for n, p in s["players"].items():
        print("  ent %2s seen %.1f..%.1f (%d frames) path %.0f, %d waypoints visited" % (
            n, p["first_seen"], p["last_seen"], p["frames_visible"], p["path_length"], len(p.get("wp_visited", []))))
        if p.get("wp_seq"):
            print("     nearest waypoints: %s" % " ".join("%d@%.0f" % (w, t) for t, w in p["wp_seq"][:40]))
    print("  chase (level time, spectated, pm_type): %s" % s.get("chase", [])[:12])
    for t, x in s["prints"][:30]:
        print("  print %.1f %s" % (t, x.strip()))


def compare(a, b):
    for tag in (a, b):
        print_summary(tag, json.loads((DATA / tag / "summary.json").read_text()))


def client_main(argv):
    """G18's headless client, optionally connecting as a spectator (SPEC-780 8: a spectator chases the first
    other player on an attack press, so its frames follow a bot)."""
    sys.path.insert(0, str(CLIENT.parent))
    import sofclient  # noqa: E402
    ap = argparse.ArgumentParser()
    ap.add_argument("--server")
    ap.add_argument("--seconds", type=float)
    ap.add_argument("--out")
    ap.add_argument("--log")
    ap.add_argument("--name")
    ap.add_argument("--script", default="")
    ap.add_argument("--spectate", action="store_true")
    a = ap.parse_args(argv)
    script = []
    for step in filter(None, (x.strip() for x in a.script.split(";"))):
        t, _, act = step.partition(":")
        script.append((float(t), act.strip()))

    class C(sofclient.Client):
        def userinfo(self):
            ui = super().userinfo()
            return ui.replace(r"\spectator\0", r"\spectator\1") if a.spectate else ui

        def run_script(self):
            # extra action "rcon <command>": out-of-band remote console (server started with rcon_password g5)
            if self.begun_at is not None:
                t = time.time() - self.begun_at
                while self.script and self.script[0][0] <= t and self.script[0][1].startswith("rcon "):
                    _, act = self.script.pop(0)
                    self.oob("rcon g5 %s\n" % act[5:])
            return super().run_script()

    c = C(a.server, a.out, a.name, "", script, False)
    c.run(a.seconds)
    with open(a.log, "w", encoding="utf-8") as f:
        for t, lvl, x in c.log:
            f.write("%7.2f [%s] %s\n" % (t, lvl, x.rstrip()))
    print("state %s, frames %d" % (c.state, len(c.sess.frames_seen)))


def main(argv):
    if argv and argv[0] == "client":
        client_main(argv[1:])
        return 0
    if argv and argv[0] == "analyze":
        analyze(argv[1])
        return 0
    if argv and argv[0] == "compare":
        compare(argv[1], argv[2])
        return 0
    if argv and argv[0] == "run":
        argv = argv[1:]
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="orig")
    ap.add_argument("--map", default="dm/jpndm2")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--dm", default="1")
    ap.add_argument("--maxclients", default="8")
    ap.add_argument("--seconds", type=float, default=70)
    ap.add_argument("--wait", type=float, default=25)
    ap.add_argument("--script", default="")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--cmd", action="append", default=[])
    ap.add_argument("--port", type=int, default=27930)
    ap.add_argument("--name", default="G5watch")
    ap.add_argument("--spectate", action="store_true", help="connect as a spectator (chase a bot with attack)")
    run(ap.parse_args(argv))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
