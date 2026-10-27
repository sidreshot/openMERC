# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-200 (out-of-band `cmd` packet), SPEC-602 (driving a predicting client in real time)
#
# Drives an original SoF client through its out-of-band `cmd` packet (SPEC-200: executed only when the sender is a
# LAN address; 127.0.0.1 is not, this PC's own interface address is). A console script with `wait` chains cannot be
# used for a remote connection: the server's stuffed `cmd configstrings` would queue behind the waits.
# Script lines:
#   <delay seconds> <console text>      send after the delay (relative to the previous line)
#   wait <timeout seconds> <regex>      block until the regex matches a new line of the client log (--log)
#   '#' starts a comment.
# usage: mp_drive.py <client pid> <script> [--lan ADDRESS] [--log CLIENT_SOF_LOG] [--server-port N]
import re
import socket
import subprocess
import sys
import time


def client_ports(pid, skip):
    out = subprocess.run(['netstat', '-ano', '-p', 'UDP'], capture_output=True, text=True).stdout
    ports = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) >= 4 and parts[0] == 'UDP' and parts[-1] == str(pid):
            m = re.search(r':(\d+)$', parts[1])
            if m and int(m.group(1)) not in skip:
                ports.append(int(m.group(1)))
    return sorted(set(ports))


def lan_address():
    for info in socket.getaddrinfo(socket.gethostname(), None, socket.AF_INET):
        addr = info[4][0]
        if not addr.startswith('127.'):
            return addr
    raise SystemExit('no LAN address found')


def wait_log(path, timeout, pattern, start_size):
    """wait for a line matching pattern in the part of the log written after start_size"""
    rx = re.compile(pattern)
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            with open(path, 'rb') as f:
                f.seek(start_size[0])
                data = f.read()
        except OSError:
            data = b''
        lines = data.decode('latin-1').split('\n')
        complete = lines[:-1]
        consumed = 0
        for l in complete:
            consumed += len(l.encode('latin-1')) + 1
            if rx.search(l):
                start_size[0] += consumed
                return True
        time.sleep(0.2)
    return False


def main():
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    pid = int(sys.argv[1])
    script = sys.argv[2]
    opt = lambda name, default=None: sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default
    lan = opt('--lan') or lan_address()
    log = opt('--log')
    skip = {int(opt('--server-port', '29430')), 27910, 28910}
    ports = []
    for _ in range(60):
        ports = client_ports(pid, skip)
        if ports:
            break
        time.sleep(0.5)
    if not ports:
        raise SystemExit('client %d has no UDP port' % pid)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((lan, 0))
    print('driving pid %d at %s ports %s from %s' % (pid, lan, ports, sock.getsockname()), flush=True)
    t0 = time.monotonic()
    logpos = [0]
    for raw in open(script, encoding='utf-8'):
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        if line.startswith('wait '):
            _, timeout, pattern = line.split(' ', 2)
            if not log:
                raise SystemExit('wait needs --log')
            ok = wait_log(log, float(timeout), pattern, logpos)
            print('%8.2f wait %r: %s' % (time.monotonic() - t0, pattern, 'seen' if ok else 'TIMEOUT'), flush=True)
            if not ok:
                raise SystemExit(2)
            continue
        delay, _, text = line.partition(' ')
        time.sleep(float(delay))
        packet = b'\xff\xff\xff\xffcmd\n' + text.encode('latin-1') + b'\n'
        for p in ports:
            sock.sendto(packet, (lan, p))
        print('%8.2f %s' % (time.monotonic() - t0, text), flush=True)


if __name__ == '__main__':
    main()
