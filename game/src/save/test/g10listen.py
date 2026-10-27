#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - single-player listen games of the original SoF.exe (client + server in one process) in the test copy
# D:\Games\SoF-test-G10, on a separate desktop that is never shown (no window can appear on the owner's screen,
# take the focus or receive input; the same method as player/test/run_session.ps1). All input comes from a console
# script. Used to write manual saves: the original engine's `save` stops a dedicated server (the engine dies before
# it asks the game module, with either module), so saves are written by listen games and loaded on the dedicated
# server by g10run.py.
# Implements: SPEC-303 (save/load commands), SPEC-752 (black-box verification)
#
# usage: g10listen.py --dll orig|om|<path> --script FILE.cfg --tag NAME [--timeout 240] [--saves DIR]
#   The script is copied to the run's user directory as g10test.cfg and executed; helpers (g10common.cfg, exec'd by
#   the script): aliases d1 (8 waits), d5, s1 (10 x d1), s2, s5. It must end with `quit`.
#   --saves DIR: .sof files copied into the user's save directory first (for `load` in the script).
# Output: <openmerc-data>/g10/<tag>/{sof.log, *.sof, demos}

import argparse
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
TEST = Path(r"D:\Games\SoF-test-G10")
ORACLE = Path(r"D:\Games\SoF-oracle")
DATA = Path(r"C:\openmerc-data\g10")
OM_DLL = REPO / "build-G10-x86" / "gamex86.dll"
PROXY = REPO / "build-G10-x86" / "g10proxy" / "gamex86.dll"
DESKTOP = "OMTestG10"
PORT = 29530

COMMON = """// G10 listen-game helpers (no semicolons in comments: the command buffer splits lines at them)
unbindall
alias d1 "wait;wait;wait;wait;wait;wait;wait;wait"
alias d5 "d1;d1;d1;d1;d1"
alias s1 "d5;d5"
alias s2 "s1;s1"
alias s5 "s2;s2;s1"
alias s8 "s5;s2;s1"
alias s10 "s5;s5"
set deathmatch 0
set maxclients 1
set g18_crashlog 1
set developer 1
"""


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


user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32.CreateDesktopW.restype = wt.HANDLE
user32.CreateDesktopW.argtypes = [wt.LPCWSTR, wt.LPCWSTR, ctypes.c_void_p, wt.DWORD, wt.DWORD, ctypes.c_void_p]
kernel32.CreateProcessW.argtypes = [wt.LPCWSTR, wt.LPWSTR, ctypes.c_void_p, ctypes.c_void_p, wt.BOOL, wt.DWORD,
                                    ctypes.c_void_p, wt.LPCWSTR, ctypes.POINTER(STARTUPINFOW),
                                    ctypes.POINTER(PROCESS_INFORMATION)]
kernel32.WaitForSingleObject.argtypes = [wt.HANDLE, wt.DWORD]
kernel32.TerminateProcess.argtypes = [wt.HANDLE, wt.UINT]
kernel32.CloseHandle.argtypes = [wt.HANDLE]
kernel32.GetExitCodeProcess.argtypes = [wt.HANDLE, ctypes.POINTER(wt.DWORD)]


def start_hidden(exe, args, cwd):
    """CreateProcess on a never-shown desktop of this window station. Returns (process handle, desktop handle)."""
    desk = user32.CreateDesktopW(DESKTOP, None, None, 0, 0x10000000, None)  # GENERIC_ALL
    if not desk:
        raise OSError(ctypes.get_last_error(), "CreateDesktopW")
    si = STARTUPINFOW()
    si.cb = ctypes.sizeof(si)
    si.lpDesktop = DESKTOP
    pi = PROCESS_INFORMATION()
    cmd = ctypes.create_unicode_buffer('"%s" %s' % (exe, args))
    if not kernel32.CreateProcessW(str(exe), cmd, None, None, False, 0, None, str(cwd), ctypes.byref(si),
                                   ctypes.byref(pi)):
        raise OSError(ctypes.get_last_error(), "CreateProcessW")
    kernel32.CloseHandle(pi.hThread)
    return pi.hProcess, desk


def copy_retry(src, dst):
    for _ in range(50):
        try:
            shutil.copyfile(src, dst)
            return
        except PermissionError:
            time.sleep(0.2)
    raise SystemExit("cannot replace " + str(dst))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="om")
    ap.add_argument("--script", required=True)
    ap.add_argument("--tag", required=True)
    ap.add_argument("--timeout", type=float, default=240)
    ap.add_argument("--saves", default=None)
    ap.add_argument("--skill", default="2")
    a = ap.parse_args(argv)
    assert TEST.exists() and TEST.resolve() != ORACLE.resolve()
    out = DATA / a.tag
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    user = TEST / ("u_" + a.tag)
    if user.exists():
        shutil.rmtree(user)
    (user / "save").mkdir(parents=True)
    if a.saves:
        for f in Path(a.saves).glob("*.sof"):
            shutil.copyfile(f, user / "save" / f.name)
    base = TEST / "base"
    # the module under test is loaded by G10's proxy (g10proxy.cpp): savegame export/chunk log, and the "pure server"
    # bit set for both modules alike (else this PC's all-locks-on client refuses a modified module's listen server)
    if a.dll == "orig":
        src = base / "gamex86.dll.orig"
        target = "gamex86.dll.orig"
    else:
        src = OM_DLL if a.dll == "om" else Path(a.dll)
        copy_retry(src, base / "gamex86.om.dll")
        target = "gamex86.om.dll"
    copy_retry(PROXY, base / "gamex86.dll")
    os.environ["G10_PROXY_TARGET"] = target
    os.environ["G10_PROXY_LOG"] = str(out / "proxy.log")
    os.environ["G10_PROXY_PURE"] = "1"
    os.environ["G10_PROXY_SP"] = "0"
    (user / "g10common.cfg").write_text(COMMON + "set skill %s\n" % a.skill)
    shutil.copyfile(a.script, user / "g10test.cfg")
    for f in (TEST / "g18crash.log",):
        if f.exists():
            f.unlink()
    args = ("+set vid_fullscreen 0 +set gl_mode 3 +set logfile 3 +set in_mouse 0 +set in_joystick 0 +set s_volume 0 "
            "+set ip 127.0.0.1 +set port %d +set clientport %d +set user ./%s +set cheats 1 +exec g10test.cfg"
            % (PORT, PORT + 1, user.name))
    hp, desk = start_hidden(TEST / "SoF.exe", args, TEST)
    t0 = time.time()
    r = kernel32.WaitForSingleObject(hp, int(a.timeout * 1000))
    timed_out = r != 0
    if timed_out:
        kernel32.TerminateProcess(hp, 1)
        kernel32.WaitForSingleObject(hp, 5000)
    code = wt.DWORD(0)
    kernel32.GetExitCodeProcess(hp, ctypes.byref(code))
    kernel32.CloseHandle(hp)
    log = user / "sof.log"
    if log.exists():
        shutil.copyfile(log, out / "sof.log")
    for f in (user / "save").glob("*.sof"):
        shutil.copyfile(f, out / f.name)
    if (user / "demos").exists():
        shutil.copytree(user / "demos", out / "demos")
    crash = TEST / "g18crash.log"
    if crash.exists():
        shutil.move(str(crash), str(out / "g18crash.log"))
    # a connect from another package's server makes the run worthless
    txt = (out / "sof.log").read_text(encoding="latin-1", errors="replace") if (out / "sof.log").exists() else ""
    foreign = [ln for ln in txt.splitlines() if "client_connect" in ln and not ln.startswith("loopback")]
    print("%s after %.1f s (exit %d), dll %s, saves %s%s%s" % (
        "TIMEOUT" if timed_out else "finished", time.time() - t0, code.value, src.name,
        sorted(p.name for p in out.glob("*.sof")), ", g18crash.log" if (out / "g18crash.log").exists() else "",
        ", DISCARD (foreign connect: %s)" % foreign if foreign else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
