#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - black-box checks of SPEC-AMEND-005 items GS.9 (GS-V1), GS.13, GS.14 and GS.16 on a dedicated server:
# the OpenMerc openMerc-ded (build-I2d-x86) or the ORIGINAL SoF.exe of the I2d test copy D:\Games\SoF-test-I2d (never
# the oracle), both started hidden (SW_HIDE, own console, never focused).
#   gs_amend005.py --engine om|orig --case search100|search104|osport
# search100: a helper takes UDP gamespyport+0..+99 on all interfaces; the server must bind +100 (GS.14).
# search104: the helper takes +0..+103; no query port may appear in +104..+209 (GS.14).
# osport:    gamespyport 0; the operating system chooses the port (GS.14).
# In every case where a port was found: the bind address (0.0.0.0 although cvar ip is 127.0.0.1, GS.13), the
# validation of the 8-character OpenSpy challenge (GS-V1: \secure\oyMl8D24 -> a5df+rC3Ni8A), and a 300-byte request
# that gets no answer and does not advance the query id while a 255-byte request is answered (GS.16).
# A base far from 28911 is used (default 41911) so that other GameSpy processes on this PC are not disturbed; the
# rules are relative to gamespyport.
# Implements: SPEC-204 section 3, SPEC-AMEND-005 GS.9/GS.13/GS.14/GS.16 (test harness, no game code)
import argparse
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TEST = Path(r'D:\Games\SoF-test-I2d')
ORACLE = Path(r'D:\Games\SoF-oracle')
OM_DED = REPO / 'build-I2d-x86' / 'release' / 'openMerc-ded.exe'
OUT = Path(r'C:\openmerc-data\I2d\gs')


def hidden():
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 0  # SW_HIDE
    return si


def occupy(base, count):
    socks, taken_by_others = [], []
    for p in range(base, base + count):
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_EXCLUSIVEADDRUSE, 1)
        try:
            s.bind(('0.0.0.0', p))
            socks.append(s)
        except OSError:
            s.close()
            taken_by_others.append(p)
    return socks, taken_by_others


def udp_ports(pid):
    out = subprocess.run(['netstat', '-ano', '-p', 'UDP'], capture_output=True, text=True, errors='replace').stdout
    res = []
    for line in out.splitlines():
        f = line.split()
        if len(f) >= 4 and f[0] == 'UDP' and f[-1] == str(pid):
            addr, _, port = f[1].rpartition(':')
            res.append((addr, int(port)))
    return res


def query(port, data, timeout=1.5):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    s.sendto(data, ('127.0.0.1', port))
    replies = []
    try:
        while True:
            d, _ = s.recvfrom(4096)
            replies.append(d.decode('latin-1'))
            s.settimeout(0.3)
    except socket.timeout:
        pass
    s.close()
    return replies


def qid(reply):
    m = re.search(r'\\queryid\\(\d+)\.', reply)
    return int(m.group(1)) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--engine', choices=['om', 'orig'], required=True)
    ap.add_argument('--case', choices=['search100', 'search104', 'osport'], required=True)
    ap.add_argument('--base', type=int, default=41911)
    ap.add_argument('--wait', type=float, default=8.0)
    a = ap.parse_args()
    assert TEST.resolve() != ORACLE.resolve()
    OUT.mkdir(parents=True, exist_ok=True)
    tag = '%s_%s' % (a.engine, a.case)
    gsport = 0 if a.case == 'osport' else a.base
    count = {'search100': 100, 'search104': 104, 'osport': 0}[a.case]
    held, others = occupy(a.base, count)
    print('helper holds %d ports from %d%s' % (len(held), a.base,
          (', taken by others: %s' % others) if others else ''))
    # no `developer`: the original shows a modal "Developer Mode" box at start when it is non-zero (SPEC-301)
    common = ['+set', 'gamespyport', str(gsport), '+set', 'ip', '127.0.0.1', '+set', 'port', '29550',
              '+set', 'logfile', '2']
    if a.engine == 'om':
        user = OUT / ('user_' + tag)
        user.mkdir(parents=True, exist_ok=True)
        cmd = [str(OM_DED), '-portable', '-basedir', str(TEST), '-user', str(user)] + common
        cwd = TEST
    else:
        rel = 'u_i2d_gs'
        (TEST / rel).mkdir(exist_ok=True)
        cmd = [str(TEST / 'SoF.exe'), '+set', 'dedicated', '1', '+set', 'user', './' + rel] + common
        cwd = TEST
    srv = subprocess.Popen(cmd, cwd=str(cwd), startupinfo=hidden(), creationflags=subprocess.CREATE_NEW_CONSOLE)
    ok = True
    try:
        time.sleep(a.wait)
        ports = udp_ports(srv.pid)
        print('server pid %d UDP sockets: %s' % (srv.pid, ports))
        qports = [(ad, p) for ad, p in ports if p != 29550]
        if a.case == 'search100':
            want = a.base + 100
            hit = [x for x in qports if x[1] == want]
            print('GS.14 expect query port %d: %s' % (want, 'PASS' if hit else 'FAIL'))
            ok &= bool(hit)
        elif a.case == 'search104':
            bad = [x for x in qports if a.base + 104 <= x[1] <= a.base + 209]
            print('GS.14 expect no query port in %d..%d: %s %s' % (a.base + 104, a.base + 209,
                  'PASS' if not bad else 'FAIL', bad))
            ok &= not bad
        else:
            print('GS.14 gamespyport 0: OS-chosen query port(s) %s' % qports)
            ok &= bool(qports)
        for ad, p in qports:
            print('GS.13 bind address of query port %d: %s (%s)' % (p, ad, 'PASS' if ad == '0.0.0.0' else 'FAIL'))
            ok &= ad == '0.0.0.0'
            r = query(p, b'\\basic\\\\secure\\oyMl8D24')
            print('GS-V1 reply: %s' % r)
            v = '\\validate\\a5df+rC3Ni8A\\' in ''.join(r)
            print('GS-V1 validate a5df+rC3Ni8A: %s' % ('PASS' if v else 'FAIL'))
            ok &= v
            q1 = qid(''.join(query(p, b'\\basic\\')))
            long300 = query(p, b'\\basic\\' + b'x' * 293)
            q2 = qid(''.join(query(p, b'\\basic\\')))
            req255 = b'\\basic\\\\echo\\' + b'y' * (255 - 13)
            assert len(req255) == 255
            a255 = query(p, req255)
            q3 = qid(''.join(a255))
            print('GS.16 300-byte request answered: %s; query ids %s -> %s (expect +1), 255-byte answered: %s (id %s)'
                  % (bool(long300), q1, q2, bool(a255), q3))
            g = (not long300) and q1 is not None and q2 == q1 + 1 and bool(a255) and q3 == q2 + 1
            print('GS.16: %s' % ('PASS' if g else 'FAIL'))
            ok &= g
    finally:
        subprocess.run(['taskkill', '/T', '/F', '/PID', str(srv.pid)], capture_output=True)
        for s in held:
            s.close()
    print('RESULT %s %s' % (tag, 'PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
