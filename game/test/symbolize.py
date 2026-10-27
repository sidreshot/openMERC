#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - turns the "+offset" module offsets in g18crash.log (src/g_crash.cpp) into function names
# using the linker map that the gamex86 build writes next to the DLL.
# Implements: (test tooling, no spec)
#
# usage: symbolize.py <gamex86.map> <g18crash.log | offset ...>

import re
import sys


def load_map(path):
    base = 0x10000000
    syms = []
    for line in open(path, encoding="latin-1"):
        m = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
        if m:
            base = int(m.group(1), 16)
            continue
        m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8})\s+(?:f\s+)?(?:i\s+)?(\S+)", line)
        if m:
            syms.append((int(m.group(2), 16) - base, m.group(1), m.group(3)))
    syms = [s for s in syms if s[0] > 0]
    syms.sort()
    return syms


def demangle(name):
    m = re.match(r"\?([^@]+)@", name)
    return m.group(1) if m else name.lstrip("_")


def lookup(syms, off):
    lo, hi = 0, len(syms) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if syms[mid][0] <= off:
            best = syms[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    if not best:
        return "?"
    return "%s+0x%x (%s)" % (demangle(best[1]), off - best[0], best[2])


def main(argv):
    syms = load_map(argv[0])
    offs = []
    for a in argv[1:]:
        if re.fullmatch(r"\+?[0-9a-fA-F]+", a):
            offs.append(int(a.lstrip("+"), 16))
        else:
            for line in open(a, encoding="latin-1"):
                print(line.rstrip())
                if "stack:" in line:
                    for o in re.findall(r"\+([0-9a-f]+)", line.split("stack:")[1]):
                        print("    +%-6s %s" % (o, lookup(syms, int(o, 16))))
    for o in offs:
        print("+%x %s" % (o, lookup(syms, o)))


if __name__ == "__main__":
    main(sys.argv[1:])
