#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - black-box check of SPEC-AMEND-005 GS.15 (sv_violence bit 0x20 = "pure server") on the ORIGINAL SoF.exe of
# the I2d test copy D:\Games\SoF-test-I2d (never the oracle), dedicated and hidden (SW_HIDE, own console):
#   gs_violence.py [--modified]
# Starts a deathmatch map and reads `violence` from the GameSpy \status\ reply. With --modified a copy of the stock
# gamex86.dll with one byte appended (same code, different SPEC-301 section 8 file checksum) is put into
# <test copy>\final\gamex86.dll, which the engine tries first (SPEC-301 section 8), and removed afterwards; base\ is
# never changed. Expected (GS.15): stock modules -> bit 0x20 set, modified game module -> bit 0x20 clear, bits
# 0x01-0x10 equal in both runs.
# Implements: SPEC-AMEND-005 GS.15 (test harness, no game code)
import argparse
import re
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

TEST = Path(r'D:\Games\SoF-test-I2d')
ORACLE = Path(r'D:\Games\SoF-oracle')
GSPORT = 41911


def hidden():
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0
    return si


def status(port):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(1.0)
    out = ''
    try:
        s.sendto(b'\\status\\', ('127.0.0.1', port))
        while True:
            d, _ = s.recvfrom(4096)
            out += d.decode('latin-1')
            s.settimeout(0.3)
    except socket.timeout:
        pass
    s.close()
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--modified', action='store_true')
    ap.add_argument('--map', default='dm/jpndm2')
    ap.add_argument('--wait', type=float, default=25.0)
    a = ap.parse_args()
    assert TEST.resolve() != ORACLE.resolve()
    final = TEST / 'final'
    placed = None
    if a.modified:
        final.mkdir(exist_ok=True)
        placed = final / 'gamex86.dll'
        shutil.copyfile(TEST / 'base' / 'gamex86.dll', placed)
        with open(placed, 'ab') as f:
            f.write(b'\0')
    rel = 'u_i2d_viol'
    (TEST / rel).mkdir(exist_ok=True)
    cmd = [str(TEST / 'SoF.exe'), '+set', 'dedicated', '1', '+set', 'user', './' + rel, '+set', 'logfile', '2',
           '+set', 'gamespyport', str(GSPORT), '+set', 'ip', '127.0.0.1', '+set', 'port', '29550',
           '+set', 'deathmatch', '1', '+set', 'maxclients', '4', '+map', a.map]
    srv = subprocess.Popen(cmd, cwd=str(TEST), startupinfo=hidden(), creationflags=subprocess.CREATE_NEW_CONSOLE)
    try:
        val, end, rep = None, time.time() + a.wait, ''
        while time.time() < end and srv.poll() is None:
            time.sleep(1.0)
            rep = status(GSPORT)
            m = re.search(r'\\violence\\(\d+)', rep)
            if m and '\\mapname\\' in rep and re.search(r'\\mapname\\[^\\]+', rep):
                val = int(m.group(1))
                time.sleep(2.0)  # the bit is set at the end of the spawn: read again
                m = re.search(r'\\violence\\(\d+)', status(GSPORT))
                val = int(m.group(1)) if m else val
                break
        print('status: %s' % rep[:300])
        print('%s modules: violence %s (bit 0x20 %s, bits 0x01-0x10 = %s)' % (
            'modified game' if a.modified else 'stock', val,
            'set' if val is not None and val & 0x20 else 'clear', None if val is None else val & 0x1F))
    finally:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(srv.pid)], capture_output=True)
        if placed:
            time.sleep(1.0)
            placed.unlink()
            try:
                final.rmdir()
            except OSError:
                pass
    return 0 if val is not None else 1


if __name__ == '__main__':
    sys.exit(main())
