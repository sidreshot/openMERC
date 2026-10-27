#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc - compares a lobby-chat run of the original client with the same script run on OpenMerc:
#   compare.py <original tag> <OpenMerc tag>       e.g. compare.py origfake omfake
# 1. the wire traces of tools/net/chattest.py fake (both directions, time stamps and client ports dropped):
#    openmerc-data/I2d/<orig tag>_fake.txt and openmerc-data/I2d/om/<om tag>_fake.txt;
# 2. the console logs after `execing omtest.cfg` (openmerc-data/I2d/shots/<orig tag>.log, om/<om tag>.log), without
#    lines that differ for known reasons: capture notes, string-package and menu-timing prints, the shutdown block,
#    and the order of the developer user listing (the original prints its internal order: each block of user lines
#    is compared as a set).
# Implements: SPEC-204 section 5 (test harness, no game code)
import difflib
import re
import sys
from pathlib import Path

DATA = Path(r'C:\openmerc-data\I2d')
SKIP = re.compile(r'^(OMSHOT|SP_Register|Overall menu creation|Wrote |GL_FindImage|Unknown command "run_om)')
END = re.compile(r'^(Wrote configuration|----------- shutting down|Shutting down sound)')


def wire(path):
    out = []
    for line in path.read_text(encoding='latin-1').splitlines():
        body = line[9:] if len(line) > 9 else line
        if 'client connected from' in body:
            continue
        out.append(body)
    return out


def console(path):
    text = path.read_text(encoding='latin-1', errors='replace').replace('\r', '')
    lines = text.split('\n')
    try:
        lines = lines[next(i for i, l in enumerate(lines) if l.startswith('execing omtest')) + 1:]
    except StopIteration:
        pass
    out, users = [], []
    for l in lines:
        if END.match(l):
            break
        if SKIP.match(l) or not l.strip():
            continue
        if re.match(r'^[ @+]\S+$', l):        # developer user listing
            users.append(l)
            continue
        if users:
            out.append('USERS ' + ' '.join(sorted(users)))
            users = []
        out.append(l.rstrip())
    if users:
        out.append('USERS ' + ' '.join(sorted(users)))
    return out


def show(name, a, b):
    d = list(difflib.unified_diff(a, b, 'original', 'openmerc', lineterm='', n=1))
    print('%s: %s' % (name, 'SAME (%d lines)' % len(a) if not d else 'DIFFERENT'))
    for l in d[:80]:
        print('   ' + l)
    return not d


def main():
    o, m = sys.argv[1], sys.argv[2]
    ok = show('wire', wire(DATA / ('%s_fake.txt' % o)), wire(DATA / 'om' / ('%s_fake.txt' % m)))
    ok &= show('console', console(DATA / 'shots' / ('%s.log' % o)), console(DATA / 'om' / ('%s.log' % m)))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
