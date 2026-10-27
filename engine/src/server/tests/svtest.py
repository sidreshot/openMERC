#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc I1b - run a dedicated server (the OpenMerc openMerc-ded with the original game module, or the
# original SoF.exe for reference) hidden in the I1b test copy, optionally connect the headless
# protocol-33 client (game/test/sofclient.py) or relay a real client through tools/net/udpproxy.py,
# then stop everything and collect the logs.
# Implements: test harness for SPEC-001 / SPEC-200..203 (black-box)
#
# usage: svtest.py --engine om|orig --tag NAME [--map dm/jpndm2] [--dm 1] [--maxclients 8]
#                  [--seconds 20] [--client none|headless] [--script "..."] [--set "cvar value" ...]
#                  [--port 29400] [--proxy 29402] [--clientport 29401]
# ports: I1b base 29400 (docs/process/testing.md "Network ports"): server 29400, client 29401,
# relay 29402; never the defaults
# output: C:\openmerc-data\i1b\<tag>\ (server.log, cap.jsonl, client.log)

import argparse
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
TEST = Path(r"D:\Games\SoF-test-I1b")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\i1b")
OM_DED = REPO / "build-I1b-x86" / "release" / "openMerc-ded.exe"
SOFCLIENT = REPO / "game" / "test" / "sofclient.py"
UDPPROXY = REPO / "tools" / "net" / "udpproxy.py"


def hidden():
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE: never shown, never focused
    return si


def start_server(a, out):
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    user = out / "user"
    if user.exists():
        shutil.rmtree(user)
    user.mkdir(parents=True)
    sets = []
    for s in a.set:
        sets += ["+set"] + s.split(None, 1)
    common = ["+set", "logfile", "2", "+set", "developer", a.developer, "+set", "ip", "127.0.0.1",
              "+set", "port", str(a.port), "+set", "rcon_password", "i1btest", "+set", "deathmatch", a.dm, "+set", "maxclients",
              a.maxclients] + sets + ["+map", a.map]
    for c in a.cmd:
        common += ["+" + c.split()[0]] + c.split()[1:]
    if a.engine == "om":
        cmd = [str(OM_DED), "-portable", "-basedir", str(TEST), "-user", str(user)] + common
        cwd = DATA
    else:
        # the original keeps its user directory relative to its own folder
        rel = "u_i1b_" + a.tag.replace("/", "_")
        ouser = TEST / rel
        if ouser.exists():
            shutil.rmtree(ouser)
        ouser.mkdir()
        cmd = [str(TEST / "SoF.exe"), "+set", "dedicated", "1", "+set", "user", "./" + rel] + common
        cwd = TEST
        user = ouser
    flags = subprocess.CREATE_NEW_CONSOLE
    srv = subprocess.Popen(cmd, cwd=str(cwd), startupinfo=hidden(), creationflags=flags)
    return srv, user


def wait_ready(srv, log, seconds):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if srv.poll() is not None:
            return False
        if log.exists():
            txt = log.read_text(encoding="latin-1", errors="replace")
            i = txt.find("SpawnServer")
            if i >= 0 and ("\n-----" in txt[i:] or "Error" in txt[i:]):
                time.sleep(1.0)
                return srv.poll() is None
        time.sleep(0.25)
    return srv.poll() is None


def rcon(port, password, command, wait=1.0):
    """out-of-band rcon (SPEC-200); returns the printed text"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(wait)
    oob = bytes([255, 255, 255, 255])
    s.sendto(oob + b"rcon %s %s" % (password.encode(), command.encode()) + bytes([0]),
             ("127.0.0.1", port))
    out = b""
    try:
        while True:
            data, _ = s.recvfrom(65536)
            if data.startswith(oob + b"print" + bytes([10])):
                out += data[10:]
    except socket.timeout:
        pass
    s.close()
    return out.decode("latin-1")


def lan_ip():
    """the address of this machine on its network (SPEC-200: cmd packets need a LAN sender)"""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect(("192.0.2.1", 9))
        return s.getsockname()[0]
    finally:
        s.close()


def start_sof_client(a, out, target):
    """the original 1.07f client of the test copy, minimised and never activated"""
    rel = "u_i1b_cl_" + a.tag.replace("/", "_")
    cuser = TEST / rel
    if cuser.exists():
        shutil.rmtree(cuser)
    cuser.mkdir()
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 7  # SW_SHOWMINNOACTIVE: no focus change
    cmd = [str(TEST / "SoF.exe"), "+set", "user", "./" + rel, "+set", "logfile", "2",
           # developer must stay 0: otherwise SoF.exe opens a "Developer Mode" message box (SPEC-301)
           "+set", "developer", "0", "+set", "vid_fullscreen", "0", "+set", "gl_mode", "3",
           "+set", "clientport", str(a.clientport), "+set", "name", a.name,
           "+set", "cl_shownet", a.shownet, "+connect", target]
    return subprocess.Popen(cmd, cwd=str(TEST), startupinfo=si), cuser


def start_om_client(a, out, target):
    """the OpenMerc client (test copy om/SoF.exe = openMerc.exe), minimised, -portable"""
    cuser = out / "cuser"
    if cuser.exists():
        shutil.rmtree(cuser)
    cuser.mkdir(parents=True)
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 7  # SW_SHOWMINNOACTIVE: no focus change
    exe = TEST / "om" / "SoF.exe"
    cmd = [str(exe), "-portable", "-basedir", str(TEST), "+set", "user", str(cuser), "+set", "logfile", "2",
           "+set", "developer", "1", "+set", "vid_fullscreen", "0", "+set", "in_grab", "0",
           "+set", "vid_renderer", "omgl", "+set", "clientport", str(a.clientport), "+set", "name", a.name,
           "+set", "cl_shownet", a.shownet]
    for cs in a.clientset:
        cmd += ["+set"] + cs.split(None, 1)
    if a.omexec:
        # a console script the client runs once it is in the level (cl_omexecactive), e.g. against an
        # original server, which has no sv_stufftext
        shutil.copyfile(a.omexec, cuser / "omtest.cfg")
        cmd += ["+set", "cl_omexecactive", "omtest.cfg"]
    cmd += ["+connect", target]
    return subprocess.Popen(cmd, cwd=str(exe.parent), startupinfo=si), cuser


def run_cscript(a, steps):
    """console commands for the client: out-of-band cmd packets from the LAN address (the original
    client); the OpenMerc client takes those only from its loopback, so for it (and with --cmdfrom
    server) the OpenMerc server stuffs the text through the test command sv_stufftext (rcon)"""
    via_server = a.client == "om" or a.cmdfrom == "server"
    ip = lan_ip() if a.cmdfrom == "lan" else "127.0.0.1"
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.bind((ip, 0))
    t0 = time.time()
    for t, text in steps:
        while time.time() - t0 < t:
            time.sleep(0.02)
        if via_server:
            rcon(a.port, "i1btest", "sv_stufftext 0 " + text, wait=0.2)
        else:
            s.sendto(bytes([255, 255, 255, 255]) + b"cmd" + bytes([10]) + text.encode("latin-1"),
                     (ip, a.clientport))
    s.close()


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--engine", choices=["om", "orig"], default="om")
    ap.add_argument("--tag", required=True)
    ap.add_argument("--map", default="dm/jpndm2")
    ap.add_argument("--dm", default="1")
    ap.add_argument("--maxclients", default="8")
    ap.add_argument("--developer", default="1")
    ap.add_argument("--seconds", type=float, default=15)
    ap.add_argument("--wait", type=float, default=40)
    ap.add_argument("--client", choices=["none", "headless", "proxy", "sof", "om"], default="none",
                    help="sof = original 1.07f client, om = OpenMerc client (test copy om/SoF.exe)")
    ap.add_argument("--cmdfrom", default="lan", choices=["lan", "loopback", "server"],
                    help="sender address of the console-text packets")
    ap.add_argument("--cscript", default="", help="sof client: 't:console text;...' after connect")
    ap.add_argument("--omexec", default="", help="om client: console script run once in the level")
    ap.add_argument("--clientset", action="append", default=[], help="om client: 'cvar value' set at start")
    ap.add_argument("--clientport", type=int, default=29401)
    ap.add_argument("--shownet", default="0")
    ap.add_argument("--script", default="")
    ap.add_argument("--name", default="I1bbot")
    ap.add_argument("--set", action="append", default=[])
    ap.add_argument("--cmd", action="append", default=[], help="console command after the map")
    ap.add_argument("--rcon", action="append", default=[], help="rcon command once the server runs")
    ap.add_argument("--rcon-at", action="append", default=[],
                    help="'t:command': rcon t seconds after the client was started, printed at the end")
    ap.add_argument("--port", type=int, default=29400)
    ap.add_argument("--proxy", type=int, default=29402)
    a = ap.parse_args(argv)

    out = DATA / a.tag
    out.mkdir(parents=True, exist_ok=True)
    srv, user = start_server(a, out)
    log = user / "sof.log"
    ok = wait_ready(srv, log, a.wait)
    proxy = None
    try:
        for c in a.rcon:
            print("rcon %s:" % c)
            print(rcon(a.port, "i1btest", c))
        timed = []
        if ok and a.rcon_at:
            import threading

            def timed_rcon():
                t0 = time.time()
                for item in a.rcon_at:
                    t, _, c = item.partition(":")
                    while time.time() - t0 < float(t):
                        time.sleep(0.05)
                    timed.append((c, rcon(a.port, "i1btest", c)))

            threading.Thread(target=timed_rcon, daemon=True).start()
        if ok and a.client == "headless":
            cmd = [sys.executable, str(SOFCLIENT), "--server", "127.0.0.1:%d" % a.port, "--seconds",
                   str(a.seconds), "--out", str(out / "cap.jsonl"), "--log", str(out / "client.log"),
                   "--name", a.name]
            if a.script:
                cmd += ["--script", a.script]
            r = subprocess.run(cmd, capture_output=True, text=True, timeout=a.seconds + 60)
            (out / "client.out").write_text(r.stdout + r.stderr, encoding="utf-8")
            print(r.stdout.strip(), r.stderr.strip()[-2000:])
        elif ok and a.client == "proxy":
            proxy = subprocess.Popen([sys.executable, str(UDPPROXY), "--listen", "127.0.0.1:%d" % a.proxy,
                                      "--server", "127.0.0.1:%d" % a.port, "--out", str(out / "cap.jsonl"),
                                      "--seconds", str(a.seconds), "--quiet"])
            proxy.wait(timeout=a.seconds + 30)
        elif ok and a.client in ("sof", "om"):
            proxy = subprocess.Popen([sys.executable, str(UDPPROXY), "--listen", "127.0.0.1:%d" % a.proxy,
                                      "--server", "127.0.0.1:%d" % a.port, "--out", str(out / "cap.jsonl"),
                                      "--seconds", str(a.seconds + 5), "--quiet"])
            time.sleep(0.5)
            steps = []
            for step in filter(None, (x.strip() for x in a.cscript.split(";"))):
                t, _, act = step.partition(":")
                steps.append((float(t), act.strip()))
            starter = start_sof_client if a.client == "sof" else start_om_client
            cl, cuser = starter(a, out, "127.0.0.1:%d" % a.proxy)
            try:
                run_cscript(a, steps)
                rest = a.seconds - (steps[-1][0] if steps else 0)
                if rest > 0:
                    time.sleep(rest)
            finally:
                if cl.poll() is None:
                    cl.kill()
                    cl.wait()
                if (cuser / "sof.log").exists():
                    shutil.copyfile(cuser / "sof.log", out / "client.log")
                proxy.wait(timeout=30)
        elif ok:
            time.sleep(a.seconds)
        for c, text in timed:
            print("rcon %s:" % c)
            print(text)
    finally:
        alive = srv.poll() is None
        if alive:
            srv.kill()
            srv.wait()
        if proxy and proxy.poll() is None:
            proxy.kill()
        if log.exists():
            shutil.copyfile(log, out / "server.log")
        print("server (%s) %s; log %s" % (a.engine, "was running" if alive else
                                          "EXITED early (code %s)" % srv.returncode, out / "server.log"))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
