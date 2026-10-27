#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-112
"""Reader for the ambient sound set file sound/sound.ams (SPEC-112).

Layout: uint8 version, four uint32 counts, a table of NUL-terminated wave paths,
then named set records in three groups (general, local, triggered).

usage:
  ams.py show <sound.ams>                       print every set
  ams.py json <sound.ams>                       dump as JSON
  ams.py validate <sound.ams> [<pak root>...]   parse; with roots, check every wave exists
                                                (paths are relative to sound/)
"""
import json
import os
import struct
import sys

VERSION = 11
GROUPS = ("general", "local", "triggered")   # group names are hypotheses (SPEC-112)


class AMSError(ValueError):
    pass


def _cstring(data, pos):
    end = data.find(b"\0", pos)
    if end < 0:
        raise AMSError(f"unterminated string at {pos}")
    return data[pos:end].decode("latin-1"), end + 1


def parse(data):
    if len(data) < 17:
        raise AMSError("file shorter than header")
    version, nwaves, n0, n1, n2 = struct.unpack_from("<BIIII", data, 0)
    if version != VERSION:
        raise AMSError(f"unexpected version {version}")
    pos = 17
    waves = []
    for _ in range(nwaves):
        s, pos = _cstring(data, pos)
        waves.append(s)
    sets = []
    for g, n in zip(GROUPS, (n0, n1, n2)):
        for _ in range(n):
            name, pos = _cstring(data, pos)
            if pos + 14 > len(data):
                raise AMSError(f"set {name!r}: truncated")
            b0, b1, tmin, tmax, loop, cnt = struct.unpack_from("<BBffhH", data, pos)
            pos += 14
            if pos + 2 * cnt + 6 > len(data):
                raise AMSError(f"set {name!r}: truncated wave list")
            rnd = list(struct.unpack_from(f"<{cnt}H", data, pos))
            pos += 2 * cnt
            dist, v0, v1 = struct.unpack_from("<fBB", data, pos)
            pos += 6
            for w in rnd + ([loop] if loop >= 0 else []):
                if w >= nwaves:
                    raise AMSError(f"set {name!r}: wave index {w} out of range")
            sets.append(dict(group=g, name=name, flags=[b0, b1], min_interval=round(tmin, 6),
                             max_interval=round(tmax, 6), loop=loop,
                             loop_wave=waves[loop] if loop >= 0 else None,
                             random=rnd, random_waves=[waves[i] for i in rnd],
                             distance=round(dist, 6), volume_min=v0, volume_max=v1))
    if pos != len(data):
        raise AMSError(f"{len(data) - pos} trailing bytes")
    return dict(version=version, counts=[nwaves, n0, n1, n2], waves=waves, sets=sets)


def load(path):
    with open(path, "rb") as f:
        return parse(f.read())


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd, path = argv[0], argv[1]
    a = load(path)
    if cmd == "show":
        print(f"version={a['version']} waves={a['counts'][0]} sets={a['counts'][1:]}")
        for i, s in enumerate(a["sets"]):
            print(f"[{i:3d}] {s['group']:9s} {s['name']:32s} flags={s['flags']} "
                  f"interval={s['min_interval']}..{s['max_interval']} dist={s['distance']} "
                  f"vol={s['volume_min']}..{s['volume_max']}")
            if s["loop_wave"]:
                print(f"        loop  {s['loop_wave']}")
            for w in s["random_waves"]:
                print(f"        rand  {w}")
    elif cmd == "json":
        print(json.dumps(a, indent=1))
    elif cmd == "validate":
        roots = argv[2:]
        missing = 0
        if roots:
            idx = set()
            for r in roots:
                for d, _, fs in os.walk(r):
                    for fn in fs:
                        idx.add(os.path.relpath(os.path.join(d, fn), r).replace(os.sep, "/").lower())
            for w in a["waves"]:
                if ("sound/" + w).lower() not in idx:
                    missing += 1
                    print(f"MISSING sound/{w}")
        used = {s["loop"] for s in a["sets"] if s["loop"] >= 0}
        for s in a["sets"]:
            used.update(s["random"])
        print(f"# parsed: version {a['version']}, {len(a['waves'])} waves "
              f"({len(used)} referenced by sets), sets per group {a['counts'][1:]} "
              f"= {len(a['sets'])}" + (f"; {missing} waves missing" if roots else ""))
        return 1 if missing else 0
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
