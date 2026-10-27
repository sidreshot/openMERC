#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G18 - configstring timeline of a capture: every change of the configstrings in an index range, with the
# capture time (seconds since the client started).
# Implements: SPEC-203 (configstrings seen by a client), SPEC-780 5 (player information configstring)
#
# usage: cstrack.py <capture dir or cap.jsonl> [--range 1415-1446]

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "net"))
import sofnet  # noqa: E402

LOG = []
NOW = [0.0]


class LogDict(dict):
    def __setitem__(self, k, v):
        LOG.append((NOW[0], k, v))
        super().__setitem__(k, v)

    def pop(self, k, *d):
        LOG.append((NOW[0], k, None))
        return super().pop(k, *d)


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("cap")
    ap.add_argument("--range", default="1415-1446")
    a = ap.parse_args(argv)
    lo, _, hi = a.range.partition("-")
    lo, hi = int(lo), int(hi or lo)
    p = Path(a.cap)
    if p.is_dir():
        p = p / "cap.jsonl"
    init, newc, parse = sofnet.Session.__init__, sofnet.Session.new_connection, sofnet.Session.parse_svc

    def init2(self, *x, **k):
        init(self, *x, **k)
        self.cs = LogDict()

    def newc2(self):
        newc(self)
        self.cs = LogDict()

    def parse2(self, m, ctx):
        NOW[0] = ctx["t"]
        return parse(self, m, ctx)

    sofnet.Session.__init__, sofnet.Session.new_connection, sofnet.Session.parse_svc = init2, newc2, parse2
    sofnet.decode_capture(str(p))
    for t, k, v in LOG:
        if lo <= k <= hi:
            print("%8.2f %4d %s" % (t, k, "<removed>" if v is None else
                                    (v.decode("latin-1", "replace") if isinstance(v, bytes) else v)))


if __name__ == "__main__":
    main(sys.argv[1:])
