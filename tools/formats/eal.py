#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-113
"""Reader for per-map .eal environmental-audio files (SPEC-113).

An .eal is a RIFF file of form type "eal " written by Creative's EAX authoring
tools (EAGLE). Listener/source properties use the public EAX 2.0 and
DirectSound3D structures (see SPEC-113 for field tables and references).

usage:
  eal.py show <file.eal>...          print the decoded chunks
  eal.py json <file.eal>             dump as JSON
  eal.py validate <dir>              parse every .eal under <dir>, print a survey
"""
import json
import os
import struct
import sys
from collections import Counter

PATH_LEN = 260

# EAX 2.0 listener properties (EAXLISTENERPROPERTIES), 14 x 4 bytes.
LISTENER_FIELDS = (
    ("room", "i"), ("room_hf", "i"), ("room_rolloff_factor", "f"), ("decay_time", "f"),
    ("decay_hf_ratio", "f"), ("reflections", "i"), ("reflections_delay", "f"),
    ("reverb", "i"), ("reverb_delay", "f"), ("environment", "I"), ("environment_size", "f"),
    ("environment_diffusion", "f"), ("air_absorption_hf", "f"), ("flags", "I"))

# EAX 2.0 buffer properties (EAXBUFFERPROPERTIES), 13 x 4 bytes, followed by
# 10 x 4 bytes of DirectSound3D-style buffer parameters (field names after the
# first two are hypotheses, see SPEC-113 Open questions).
SOURCE_FIELDS = (
    ("direct", "i"), ("direct_hf", "i"), ("room", "i"), ("room_hf", "i"),
    ("room_rolloff_factor", "f"), ("obstruction", "i"), ("obstruction_lf_ratio", "f"),
    ("occlusion", "i"), ("occlusion_lf_ratio", "f"), ("occlusion_room_ratio", "f"),
    ("outside_volume_hf", "i"), ("air_absorption_factor", "f"), ("flags", "I"),
    ("inside_cone_angle", "I"), ("outside_cone_angle", "I"), ("cone_outside_volume", "i"),
    ("cone_orientation_x", "f"), ("cone_orientation_y", "f"), ("cone_orientation_z", "f"),
    ("min_distance", "f"), ("max_distance", "f"), ("unknown_21", "I"), ("unknown_22", "I"))

# EAX 2.0 environment preset indices (EAX_ENVIRONMENT_*, public eax.h order).
ENVIRONMENTS = (
    "generic", "paddedcell", "room", "bathroom", "livingroom", "stoneroom", "auditorium",
    "concerthall", "cave", "arena", "hangar", "carpetedhallway", "hallway",
    "stonecorridor", "alley", "forest", "city", "mountains", "quarry", "plain",
    "parkinglot", "sewerpipe", "underwater", "drugged", "dizzy", "psychotic")


class EALError(ValueError):
    pass


def _struct(fields, raw):
    fmt = "<" + "".join(t for _, t in fields)
    if len(raw) != struct.calcsize(fmt):
        raise EALError(f"expected {struct.calcsize(fmt)} bytes, got {len(raw)}")
    vals = struct.unpack(fmt, raw)
    return {name: (round(v, 6) if isinstance(v, float) else v) for (name, _), v in zip(fields, vals)}


def _cstr(raw):
    return raw.split(b"\0", 1)[0].decode("latin-1")


def iter_chunks(data, start, end):
    """Yield (id, payload) for RIFF sub-chunks in data[start:end]."""
    pos = start
    while pos < end:
        if pos + 8 > end:
            raise EALError(f"truncated chunk header at {pos}")
        cid, size = struct.unpack_from("<4sI", data, pos)
        if pos + 8 + size > end:
            raise EALError(f"chunk {cid!r} at {pos} overruns its parent")
        yield cid.decode("latin-1"), data[pos + 8:pos + 8 + size]
        pos += 8 + size + (size & 1)


def _decode_list(form, payload):
    """Decode a LIST of presets: num, nams, guid, lisp/fils/srca/mata/gema."""
    out = {"form": form, "count": None, "items": [], "chunks": []}
    sub = {}
    for cid, body in iter_chunks(payload, 4, len(payload)):
        out["chunks"].append(cid)
        sub[cid] = body
    n = struct.unpack("<I", sub["num "])[0] if "num " in sub else 0
    out["count"] = n
    names = sub.get("nams", b"")
    recsize = {"lisp": 56, "srca": 92, "fils": PATH_LEN, "guid": 16}
    for i in range(n):
        item = {}
        if names:
            w = len(names) // n
            item["name"] = _cstr(names[i * w:(i + 1) * w])
        if "guid" in sub and sub["guid"]:
            item["guid"] = sub["guid"][i * 16:(i + 1) * 16].hex()
        if "lisp" in sub:
            item["listener"] = _struct(LISTENER_FIELDS, sub["lisp"][i * 56:(i + 1) * 56])
        if "fils" in sub and sub["fils"]:
            w = len(sub["fils"]) // n
            item["file"] = _cstr(sub["fils"][i * w:(i + 1) * w])
        if "srca" in sub:
            item["source"] = _struct(SOURCE_FIELDS, sub["srca"][i * 92:(i + 1) * 92])
        for cid in ("mata", "gema"):
            if cid in sub and sub[cid]:
                w = len(sub[cid]) // n
                item[cid] = sub[cid][i * w:(i + 1) * w].hex()
        out["items"].append(item)
    for cid, body in sub.items():
        if cid not in ("num ", "nams", "guid", "lisp", "fils", "srca", "mata", "gema") or (
                n == 0 and body):
            out.setdefault("raw", {})[cid] = body.hex()
    return out


def parse(data):
    if len(data) < 12 or data[:4] != b"RIFF" or data[8:12] != b"eal ":
        raise EALError("not a RIFF 'eal ' file")
    size = struct.unpack_from("<I", data, 4)[0]
    if size + 8 != len(data):
        raise EALError(f"RIFF size {size + 8} != file size {len(data)}")
    res = {"chunks": [], "lists": {}}
    for cid, body in iter_chunks(data, 12, len(data)):
        res["chunks"].append(cid)
        if cid in ("majv", "minv"):
            res[cid] = struct.unpack("<I", body)[0]
        elif cid in ("exe ", "cmd ", "path", "dfil", "exep", "cmds"):
            if len(body) != PATH_LEN:
                raise EALError(f"{cid} length {len(body)}")
            # "exep"/"cmds" (form 2.1 dm maps): the path's first character is a NUL, the rest
            # of both buffers is uninitialised memory.
            if cid == "cmds":
                res["cmds"] = body.hex()          # no string in any stock file
            elif cid == "exep":
                res["exep"] = _cstr(body[1:])     # drive letter lost: "\0:\Raven\Sof\SoF.exe"
            else:
                res[cid.strip()] = _cstr(body)
        elif cid == "gdfm":
            a, b, c = struct.unpack("<ifI", body)
            res["gdfm"] = {"a_int": a, "b_float": round(b, 6), "c_int": c}   # meaning UNKNOWN
        elif cid == "lisa":
            res["lisa"] = dict(zip(("distance_factor", "rolloff_factor", "doppler_factor"),
                                   (round(v, 6) for v in struct.unpack("<3f", body))))
        elif cid == "denv":
            res["denv"] = _struct(LISTENER_FIELDS, body)
        elif cid == "dsrc":
            res["dsrc"] = _struct(SOURCE_FIELDS, body)
        elif cid == "dmat":
            res["dmat"] = body.hex()
        elif cid == "LIST":
            form = body[:4].decode("latin-1")
            res["lists"][form] = _decode_list(form, body)
        else:
            res.setdefault("unknown", {})[cid] = body.hex()
    return res


def load(path):
    with open(path, "rb") as f:
        return parse(f.read())


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "show":
        for p in argv[1:]:
            r = load(p)
            print(f"== {p}  majv={r.get('majv')} minv={r.get('minv')} chunks={r['chunks']}")
            for k in ("exe", "cmd", "path", "dfil", "exep", "gdfm"):
                if r.get(k):
                    print(f"  {k}: {r[k]}")
            print(f"  lisa: {r.get('lisa')}")
            env = r.get("denv", {})
            e = env.get("environment")
            print(f"  denv: {env} ({ENVIRONMENTS[e] if e is not None and e < len(ENVIRONMENTS) else '?'})")
            print(f"  dsrc: {r.get('dsrc')}")
            print(f"  dmat: {r.get('dmat')}")
            for form, lst in r["lists"].items():
                print(f"  LIST {form}: count={lst['count']} chunks={lst['chunks']}")
                for it in lst["items"]:
                    print(f"     {it}")
    elif cmd == "json":
        print(json.dumps(load(argv[1]), indent=1))
    elif cmd == "validate":
        files = []
        for d, _, fs in os.walk(argv[1]):
            files += [os.path.join(d, f) for f in fs if f.lower().endswith(".eal")]
        files.sort()
        bad = 0
        ver, layouts, denvs, dsrcs, lisas, counts = Counter(), Counter(), Counter(), Counter(), Counter(), Counter()
        nonempty = []
        for p in files:
            try:
                r = load(p)
            except Exception as e:  # noqa: BLE001
                bad += 1
                print(f"FAIL {p}: {e!r}")
                continue
            ver[(r.get("majv"), r.get("minv"))] += 1
            layouts[tuple(r["chunks"])] += 1
            denvs[json.dumps(r.get("denv"), sort_keys=True)] += 1
            dsrcs[json.dumps(r.get("dsrc"), sort_keys=True)] += 1
            lisas[json.dumps(r.get("lisa"), sort_keys=True)] += 1
            c = tuple((f, l["count"]) for f, l in sorted(r["lists"].items()))
            counts[c] += 1
            if any(l["count"] for l in r["lists"].values()):
                nonempty.append(p)
        print(f"# {len(files)} files, {len(files) - bad} parsed, {bad} failed")
        print(f"versions (majv, minv): {dict(ver)}")
        for k, v in layouts.items():
            print(f"layout x{v}: {list(k)}")
        print(f"distinct lisa: {len(lisas)}; denv: {len(denvs)}; dsrc: {len(dsrcs)}")
        for k, v in denvs.items():
            print(f"  denv x{v}: {k}")
        for k, v in lisas.items():
            print(f"  lisa x{v}: {k}")
        for k, v in counts.items():
            print(f"preset counts x{v}: {k}")
        print(f"files with non-empty preset lists: {nonempty}")
        return 1 if bad else 0
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
