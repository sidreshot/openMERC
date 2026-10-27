# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-312 section 10 / SPEC-202 6.1 check (test tool, no game code)
#
# Decodes a UDP capture (tools/net/udpproxy.py JSON lines) with tools/net/sofnet.py and prints, in time
# order, the runs of identical current user commands (forward, side, up, buttons, lean, light, msec range)
# the client sent, so the values of SPEC-312's test vectors can be read off.
# usage: python -I movecheck.py <capture.jsonl>
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools', 'net'))
import sofnet  # noqa: E402


def main():
    sess = sofnet.decode_capture(sys.argv[1])[0]
    runs = []
    for mv in sess.moves:
        c = mv['cmds'][2]
        key = (c['forward'], c['side'], c['up'], c['buttons'], c['lean'])
        if runs and runs[-1][0] == key:
            runs[-1][1] += 1
            runs[-1][3] = mv['t']
            runs[-1][4].add(c['msec'])
        else:
            runs.append([key, 1, mv['t'], mv['t'], {c['msec']}])
    print('%d moves, %d runs' % (len(sess.moves), len(runs)))
    print('   t0      t1   count  forward side   up  buttons lean  msec')
    for key, n, t0, t1, ms in runs:
        f, s, u, b, l = key
        print('%7.2f %7.2f %6d %7d %5d %4d   0x%02x  %3d   %s' % (t0, t1, n, f, s, u, b, l,
              ','.join(str(m) for m in sorted(ms)[:6])))


if __name__ == '__main__':
    main()
