#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc X1 - deathmatch on the open stack: the OpenMerc dedicated server (openMerc-ded.exe of D:\Games\SoF-open-X1 with
# OpenMerc's gamex86.dll / player.dll) on a map rotation (sv_maplist, fraglimit / timelimit) with bots (G5's
# bot_fillserv / bot_add), joined by the ORIGINAL 1.07f client (SoF.exe of the comparison copy D:\Games\SoF-test-X1)
# and the OpenMerc client (openMerc.exe of the open copy). Both clients run on a separate, never shown desktop.
# Both games force every parental lock on this PC (invalid lock words), the server is not "pure" (our modules,
# SPEC-AMEND-005 GS.15) -> sv_violence 31 and every client would be refused; the clients therefore connect through
# I7's player/test/mp_relay.py, which sets cl_violence 0 in the connect userinfo (no registry change).
# The server drives the clients with the OpenMerc test command sv_stufftext (rcon): walk, fire, captures.
#   xdm.py --tag dm1 [--minutes 12] [--maps "dm/jpndm2 dm/nycdm4 ..."] [--fraglimit 10] [--timelimit 2]
#          [--clients orig,om] [--bots fill|N] [--server om|orig]
# Ports (X1 base 29580): server 29580, relay 29582, original client 29583, OpenMerc client 29584.
# Output: openmerc-data\x1\<tag>\ (server.log, orig.log, om.log, relay.log, rcon.log, timeline.txt, om captures)
# Implements: black-box test harness (no game code)
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
OPEN = Path(r"D:\Games\SoF-open-X1")
ORIG = Path(r"D:\Games\SoF-test-X1")
DATA = Path(r"C:\openmerc-data\x1")
RELAY = REPO / "player" / "test" / "mp_relay.py"

k32 = ctypes.WinDLL("kernel32", use_last_error=True)
u32 = ctypes.WinDLL("user32", use_last_error=True)


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [("cb", wt.DWORD), ("lpReserved", wt.LPWSTR), ("lpDesktop", wt.LPWSTR), ("lpTitle", wt.LPWSTR),
                ("dwX", wt.DWORD), ("dwY", wt.DWORD), ("dwXSize", wt.DWORD), ("dwYSize", wt.DWORD),
                ("dwXCountChars", wt.DWORD), ("dwYCountChars", wt.DWORD), ("dwFillAttribute", wt.DWORD),
                ("dwFlags", wt.DWORD), ("wShowWindow", wt.WORD), ("cbReserved2", wt.WORD),
                ("lpReserved2", ctypes.c_void_p), ("hStdInput", wt.HANDLE), ("hStdOutput", wt.HANDLE),
                ("hStdError", wt.HANDLE)]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [("hProcess", wt.HANDLE), ("hThread", wt.HANDLE), ("dwProcessId", wt.DWORD),
                ("dwThreadId", wt.DWORD)]


u32.CreateDesktopW.restype = wt.HANDLE
u32.CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
k32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD,
                               ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                               ctypes.POINTER(PROCESS_INFORMATION)]


class DeskProc:
    """a process on a separate desktop of the interactive window station (never shown, never focused)"""

    def __init__(self, desk, exe, args, cwd):
        si = STARTUPINFOW()
        si.cb = ctypes.sizeof(si)
        si.lpDesktop = "WinSta0\\" + desk
        pi = PROCESS_INFORMATION()
        cmd = ctypes.create_unicode_buffer('"%s" %s' % (exe, args))
        if not k32.CreateProcessW(None, cmd, None, None, False, 0, None, str(cwd), ctypes.byref(si), ctypes.byref(pi)):
            raise OSError("CreateProcess failed: %d" % ctypes.get_last_error())
        self.pi = pi
        self.pid = pi.dwProcessId

    def alive(self):
        code = wt.DWORD()
        k32.GetExitCodeProcess(self.pi.hProcess, ctypes.byref(code))
        return code.value == 259

    def exitcode(self):
        code = wt.DWORD()
        k32.GetExitCodeProcess(self.pi.hProcess, ctypes.byref(code))
        return code.value

    def kill(self):
        if self.alive():
            k32.TerminateProcess(self.pi.hProcess, 1)
            k32.WaitForSingleObject(self.pi.hProcess, 5000)


def rcon(port, password, command, wait=0.6):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(wait)
    oob = b"\xff\xff\xff\xff"
    s.sendto(oob + b"rcon %s %s" % (password.encode(), command.encode()) + b"\0", ("127.0.0.1", port))
    out = b""
    try:
        while True:
            data, _ = s.recvfrom(65536)
            if data.startswith(oob + b"print\n"):
                out += data[10:]
    except socket.timeout:
        pass
    s.close()
    return out.decode("latin-1")


def hidden():
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    return si


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--tag", required=True)
    ap.add_argument("--minutes", type=float, default=12)
    ap.add_argument("--maps", default="dm/jpndm2 dm/nycdm4 dm/irqdm1 dm/kosdm1 dm/suddm1")
    ap.add_argument("--fraglimit", default="10")
    ap.add_argument("--timelimit", default="2")
    ap.add_argument("--clients", default="orig,om")
    ap.add_argument("--bots", default="fill", help="fill = bot_fillserv 1, N = bot_add N times, 0 = none")
    ap.add_argument("--server", default="om", choices=["om", "orig"],
                    help="om = openMerc-ded of the open copy; orig = original modules on our openMerc-ded (comparison copy om\\)")
    ap.add_argument("--port", type=int, default=29580)
    ap.add_argument("--set", action="append", default=[], help="extra server cvar 'name value'")
    a = ap.parse_args(argv[1:])
    out = DATA / a.tag
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    tl = open(out / "timeline.txt", "w", encoding="utf-8")
    t0 = time.time()

    def note(text):
        line = "%7.1f %s" % (time.time() - t0, text)
        tl.write(line + "\n")
        tl.flush()
        print(line, flush=True)

    maps = a.maps.split()
    srv_dir = out / "srv"
    srv_user = srv_dir / "user"
    srv_user.mkdir(parents=True)
    lines = ["set developer 1", "set deathmatch 1", "set maxclients 8", "set fraglimit %s" % a.fraglimit,
             "set timelimit %s" % a.timelimit, 'set sv_maplist "%s"' % " ".join(maps), "set rcon_password x1",
             "set ip 127.0.0.1", "set port %d" % a.port, "set g18_crashlog 1", "set sv_watchdog 8"]
    if a.bots == "fill":
        lines.append("set bot_fillserv 1")
    for s in a.set:
        lines.append("set " + s)
    lines.append("map %s" % maps[0])
    (srv_user / "x1dm.cfg").write_text("\n".join(lines) + "\n", encoding="ascii")
    game = OPEN if a.server == "om" else ORIG
    ded = (OPEN / "openMerc-ded.exe") if a.server == "om" else (ORIG / "om" / "openMerc-ded.exe")
    cmd = [str(ded), "-portable", "-basedir", str(game).replace("\\", "/"), "+set", "user",
           str(srv_user).replace("\\", "/"), "+set", "logfile", "3", "+exec", "x1dm.cfg"]
    note("server: " + " ".join(cmd))
    srv = subprocess.Popen(cmd, cwd=str(srv_dir), startupinfo=hidden(), creationflags=subprocess.CREATE_NEW_CONSOLE)
    slog = srv_user / "sof.log"
    relay_stop = out / "relay.stop"
    relay = subprocess.Popen([sys.executable, "-I", str(RELAY), "--listen", str(a.port + 2), "--server", str(a.port),
                              "--seconds", str(a.minutes * 60 + 120), "--stopfile", str(relay_stop),
                              "--log", str(out / "relay.log")], startupinfo=hidden())
    procs = {}
    desk = None
    rlog = open(out / "rcon.log", "w", encoding="utf-8")

    def rc(command, wait=0.6):
        r = rcon(a.port, "x1", command, wait)
        rlog.write("=== %.1f %s\n%s\n" % (time.time() - t0, command, r))
        rlog.flush()
        return r

    try:
        # wait for the first map
        ok = False
        while time.time() - t0 < 60:
            if srv.poll() is not None:
                break
            if slog.exists() and re.search(r"Server Initialization[\s\S]*\n-{5,}", slog.read_text("latin-1", "replace")):
                ok = True
                break
            time.sleep(0.25)
        note("server ready" if ok else "server NOT ready (exit %s)" % srv.poll())
        if not ok:
            return 1
        time.sleep(2)
        if a.bots not in ("fill", "0"):
            for i in range(int(a.bots)):
                rc("bot_add 4")
        desk = u32.CreateDesktopW("OMTestX1dm", None, None, 0, 0x10000000, None)
        if not desk:
            raise OSError("CreateDesktop failed: %d" % ctypes.get_last_error())
        target = "127.0.0.1:%d" % (a.port + 2)
        if "orig" in a.clients:
            rel = "u_x1_" + a.tag
            ou = ORIG / rel
            if ou.exists():
                shutil.rmtree(ou)
            ou.mkdir()
            args = ("+set user ./%s +set logfile 2 +set developer 0 +set vid_fullscreen 0 +set gl_mode 3 "
                    "+set s_volume 0 +set in_mouse 0 +set clientport %d +set name X1orig +connect %s"
                    % (rel, a.port + 3, target))
            procs["orig"] = DeskProc("OMTestX1dm", ORIG / "SoF.exe", args, ORIG)
            note("original client pid %d: %s" % (procs["orig"].pid, args))
            time.sleep(3)
        if "om" in a.clients:
            cdir = out / "omcl"
            cu = cdir / "user"
            cu.mkdir(parents=True)
            wav = str(out / "om_snd.wav").replace("\\", "/")
            (cu / "config.cfg").write_text("\n".join([
                "set in_grab 0", "set in_mouse 0", "set clientport %d" % (a.port + 4), "set vid_fullscreen 0",
                "set vid_renderer gl3", "set gl_mode 3", "set r_mode 4", "set snd_dll OMsnd.dll",
                "set s_om_wavefile %s" % wav, "set s_om_focusmute 0", "set name X1om"]) + "\n", encoding="ascii")
            args = ("-portable -basedir %s +set user %s +set logfile 3 +set sv_watchdog 8 +connect %s"
                    % (str(OPEN).replace("\\", "/"), str(cu).replace("\\", "/"), target))
            procs["om"] = DeskProc("OMTestX1dm", OPEN / "openMerc.exe", args, cdir)
            note("OpenMerc client pid %d: %s" % (procs["om"].pid, args))
        # drive: status every 10 s, the clients walk and fire, OpenMerc client captures
        end = t0 + a.minutes * 60
        k = 0
        last_map = None
        while time.time() < end:
            if srv.poll() is not None:
                note("SERVER EXITED code %s" % srv.poll())
                break
            for n, p in procs.items():
                if not p.alive() and not getattr(p, "reported", False):
                    note("client %s EXITED code %d" % (n, p.exitcode()))
                    p.reported = True
            st = rc("status")
            m = re.search(r"map\s*:\s*(\S+)", st)
            if m and m.group(1) != last_map:
                last_map = m.group(1)
                note("map %s" % last_map)
            players = re.findall(r"^\s*(\d+)\s+(-?\d+)\s+(\S+)\s+(\S.*?)\s{2,}", st, re.M)
            note("status: " + "; ".join(" ".join(x).strip() for x in players)[:400])
            for name in ("X1orig", "X1om"):
                rc("sv_stufftext %s +forward" % name, 0.2)
            time.sleep(1.5)
            for name in ("X1orig", "X1om"):
                rc("sv_stufftext %s -forward" % name, 0.2)
                rc("sv_stufftext %s +attack" % name, 0.2)
            time.sleep(1.0)
            for name in ("X1orig", "X1om"):
                rc("sv_stufftext %s -attack" % name, 0.2)
            if k % 3 == 0:
                rc("sv_stufftext X1om omshot dm%03d" % k, 0.2)
            k += 1
            time.sleep(max(0.0, 10 - 2.5 - 1.6))
        rc("status")
    finally:
        for n, p in procs.items():
            note("stop client %s (alive %s)" % (n, p.alive()))
            p.kill()
        if srv.poll() is None:
            rcon(a.port, "x1", "quit", 0.5)
            time.sleep(2)
        if srv.poll() is None:
            srv.kill()
        note("server exit %s" % srv.poll())
        relay_stop.write_text("stop")
        try:
            relay.wait(5)
        except subprocess.TimeoutExpired:
            relay.kill()
        if desk:
            u32.CloseDesktop(desk)
        time.sleep(0.5)
        if slog.exists():
            shutil.copyfile(slog, out / "server.log")
        if "orig" in procs:
            for nm in ("sof.log", "SoF.log"):
                f = ORIG / ("u_x1_" + a.tag) / nm
                if f.exists():
                    shutil.copyfile(f, out / "orig.log")
                    break
        cl = out / "omcl" / "user" / "sof.log"
        if cl.exists():
            shutil.copyfile(cl, out / "om.log")
        shots = out / "omcl" / "user" / "scrnshot"
        if shots.exists():
            try:
                from PIL import Image
                for f in shots.glob("*.tga"):
                    Image.open(f).save(out / (f.stem + ".png"))
            except Exception as e:  # noqa: BLE001
                note("captures: %s" % e)
        rlog.close()
        tl.close()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
