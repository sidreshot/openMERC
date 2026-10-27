#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G7 - black-box tests of the props / server effects against the original game module.
# Implements: SPEC-770, SPEC-771 (verification: entity baselines, configstrings, temp entities, effects)
#
# Uses G18's harness (game/test/runtest.py, sofclient.py) with the G7 test copies D:\Games\SoF-test-G7(b) and the
# decoder tools/net/sofnet.py. Nothing here reads the original module's code: only what a client receives.
#
# usage:
#   g7test.py run --dll orig|om|<path> --map dm/raven --tag NAME [--seconds 15] [--script "..."] [--set "k v"] [--sp]
#       (ports are always G7's: server 29480 / client 29481, second copy G7_TEST=...SoF-test-G7b 29482 / 29483;
#        a run that saw packets from another address, a second serverdata or connect is discarded)
#   g7test.py cmp <tagA> <tagB> [--bsp <file.bsp>] [--all]   compare two runs (configstrings, baselines, TEs)
#   g7test.py dump <tag> [--bsp <file.bsp>]                  list baselines (with matched map classnames)
# Runs go to openmerc-data/g7/<tag>/.

import argparse
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]
DATA = Path(r"C:\openmerc-data\g7")
sys.path.insert(0, str(REPO / "game" / "test"))
sys.path.insert(0, str(REPO / "tools" / "net"))
import sofnet  # noqa: E402

OM_DLL = REPO / "build-G7-x86" / "gamex86.dll"  # set OM_BUILD_DIR=...\build-G7-x86, game\build.cmd Release
# G7's private test copies (paks hard-linked, never the oracle). G7_TEST selects the copy of a run, so two runs can
# go in parallel (a copy holds one loaded base\gamex86.dll at a time).
TESTS = [Path(r"D:\Games\SoF-test-G7"), Path(r"D:\Games\SoF-test-G7b")]
TEST = Path(os.environ.get("G7_TEST", str(TESTS[0])))
# Network ports (docs/process/testing.md): G7 owns 29480-29489, never the defaults. Copy i (0, 1):
# server port 29480 + 2i, test client 29481 + 2i, the server's own client port 29484 + i, second client 29486 + i.
PORT_BASE = 29480
COPY = next((i for i, t in enumerate(TESTS) if t.resolve() == TEST.resolve()), 0)
SERVER_PORT = PORT_BASE + 2 * COPY
CLIENT_PORT = PORT_BASE + 1 + 2 * COPY
SERVER_CLIENTPORT = PORT_BASE + 4 + COPY
CLIENT2_PORT = PORT_BASE + 6 + COPY


def cmd_run(argv):
    """Like G18's runtest.py, but on a G7 test copy and G7's ports. The original dedicated server needs developer 1
    set *inside an exec'd cfg* (`+exec g18dev.cfg`, written by runtest): with developer 0 it never spawns the map,
    and `+set developer 1` on the command line opens a modal "Developer Mode" box (SPEC-301 B1 step 7). So any
    command-line developer setting is dropped and the exec of the cfg is guaranteed (brief, 2026-10-25)."""
    import runtest
    runtest.TEST = TEST
    runtest.DATA = DATA
    runtest.OM_DLL = OM_DLL
    orig_popen = runtest.subprocess.Popen

    clients = [0]

    def popen(args, *a, **kw):
        args = [x for x in args]
        if len(args) > 1 and str(args[1]).endswith("sofclient.py"):
            # the test client: G7's client port and a filter for packets from other servers (g7client.py)
            cport = CLIENT_PORT if clients[0] == 0 else CLIENT2_PORT
            clients[0] += 1
            return orig_popen([args[0], str(HERE / "g7client.py"), "--bind-port", str(cport)] + args[2:], *a, **kw)
        out = []
        k = 0
        while k < len(args):
            if args[k] == "+set" and k + 2 < len(args) and args[k + 1] == "developer":
                k += 3  # never on the command line
                continue
            out.append(args[k])
            k += 1
        if out and str(out[0]).lower().endswith("sof.exe"):
            if not any(out[j] == "+exec" and str(out[j + 1]).endswith("dev.cfg") for j in range(len(out) - 1)):
                (TEST / "base" / "g7dev.cfg").write_text("set developer 1\n")
                i = out.index("+map") if "+map" in out else len(out)
                out[i:i] = ["+exec", "g7dev.cfg"]
            # the server's own client socket on a G7 port too (the default is shared by every test run)
            i = out.index("+map") if "+map" in out else len(out)
            out[i:i] = ["+set", "clientport", str(SERVER_CLIENTPORT)]
            if len(out) - 1 > 50:
                raise SystemExit("SoF.exe command line has %d words (> 50 crashes it); use +exec" % (len(out) - 1))
        return orig_popen(out, *a, **kw)

    argv = list(argv)
    if "--port" in argv:
        k = argv.index("--port")
        if int(argv[k + 1]) != SERVER_PORT:
            print("note: --port %s replaced by G7's port %d" % (argv[k + 1], SERVER_PORT))
        del argv[k:k + 2]
    argv += ["--port", str(SERVER_PORT)]
    tag = argv[argv.index("--tag") + 1]
    for p in (SERVER_PORT, CLIENT_PORT):
        if not runtest.port_free(p):
            print("INVALID RUN: G7 port %d is in use (a run of ours still alive, or someone uses our range)" % p)
            return 3
    runtest.subprocess.Popen = popen
    try:
        r = runtest.main(argv)
    finally:
        runtest.subprocess.Popen = orig_popen
    log = DATA / tag / "sof.log"
    reason = None
    if log.exists() and "Couldn't allocate" in log.read_text(encoding="latin-1", errors="replace"):
        reason = "the server could not bind port %d" % SERVER_PORT
    else:
        reason = check_run(tag)
    if reason:
        cap = DATA / tag / "cap.jsonl"
        if cap.exists():
            cap.replace(DATA / tag / "cap.invalid.jsonl")
        print("INVALID RUN (discarded): " + reason)
        return 3
    return r


def check_run(tag):
    """Project rule: discard a run that saw a second server's serverdata or a connect it did not ask for."""
    out = DATA / tag / "client.out"
    if out.exists():
        m = re.search(r"foreign packets (\d+)", out.read_text(encoding="utf-8", errors="replace"))
        if m and int(m.group(1)):
            return "%s packets from another address" % m.group(1)
    cap = DATA / tag / "cap.jsonl"
    if not cap.exists():
        return None
    connects = 0
    for line in open(cap, encoding="utf-8"):
        r = json.loads(line)
        if r.get("dir") == "s2c" and bytes.fromhex(r["hex"][:40]).startswith(b"\xff\xff\xff\xffclient_connect"):
            connects += 1
    if connects > 1:
        return "%d client_connect messages" % connects
    count = [0]
    orig14 = sofnet.Session.svc_14

    def svc_14(self, m, ctx):
        count[0] += 1
        return orig14(self, m, ctx)

    sofnet.Session.svc_14 = svc_14
    try:
        sofnet.decode_capture(str(cap))
    except Exception:
        pass
    finally:
        sofnet.Session.svc_14 = orig14
    if count[0] > 1:
        return "%d serverdata messages" % count[0]
    return None


# --- decoding with temp-entity / effect payload capture ---------------------------------------------------------

def decode(tag):
    events = []
    orig1, orig5 = sofnet.Session.svc_1, sofnet.Session.svc_5

    def svc_1(self, m, ctx):
        p = m.p
        t = m.d[p]
        orig1(self, m, ctx)
        mm = sofnet.Msg(m.d, p + 1)
        events.append(("te", ctx.get("t", 0), t, sofnet.read_fmt(mm, sofnet.TE_FMT[t])))

    def svc_5(self, m, ctx):
        p = m.p
        orig5(self, m, ctx)
        eff, fl = m.d[p], m.d[p + 1]
        name = self.cs.get(647 + eff, b"?")
        name = name.decode("latin-1") if isinstance(name, bytes) else str(name)
        events.append(("fx", ctx.get("t", 0), name, fl, m.d[p:m.p].hex()))

    orig10 = sofnet.Session.svc_10
    orig15 = sofnet.Session.svc_15

    def svc_15(self, m, ctx):
        p = m.p
        orig15(self, m, ctx)
        idx = sofnet.Msg(m.d, p).short()
        if self.frames:  # only changes during the game, not the signon list
            v = self.cs.get(idx)
            events.append(("cs", ctx.get("t", 0), idx, v.decode("latin-1") if isinstance(v, bytes) else v))

    def svc_10(self, m, ctx):
        p = m.p
        orig10(self, m, ctx)
        mm = sofnet.Msg(m.d, p)
        fl = mm.byte()
        idx = mm.byte() + (256 if fl & 0x20 else 0)
        vol = mm.byte() if fl & 0x01 else 255
        att = mm.byte() if fl & 0x02 else 64
        if fl & 0x10:
            mm.byte()
        ec = mm.short() if fl & 0x08 else 0
        name = self.cs.get(291 + idx, b"?")
        name = name.decode("latin-1") if isinstance(name, bytes) else str(name)
        events.append(("snd", ctx.get("t", 0), name, (ec >> 3, ec & 7, vol, att)))

    sofnet.Session.svc_1, sofnet.Session.svc_5, sofnet.Session.svc_10 = svc_1, svc_5, svc_10
    sofnet.Session.svc_15 = svc_15
    try:
        r = sofnet.decode_capture(str(DATA / tag / "cap.jsonl"))
    finally:
        sofnet.Session.svc_1, sofnet.Session.svc_5, sofnet.Session.svc_10 = orig1, orig5, orig10
        sofnet.Session.svc_15 = orig15
    sess = r[0]
    return sess, events, r


def bsp_entities(path):
    b = Path(path).read_bytes()
    ofs, ln = struct.unpack_from("<ii", b, 8)
    s = b[ofs:ofs + ln].decode("latin-1")
    out = []
    for m in re.finditer(r"\{([^{}]*)\}", s):
        out.append(dict(re.findall(r'"([^"]*)"\s+"([^"]*)"', m.group(1))))
    return out


def origin_key(o):
    return tuple(int(round(float(x))) for x in o)


def name_map(bsp):
    if not bsp:
        return {}
    m = {}
    for e in bsp_entities(bsp):
        if "origin" in e:
            m.setdefault(origin_key(e["origin"].split()), []).append(e.get("classname", "?"))
    return m


def cs_text(v):
    return v.decode("latin-1") if isinstance(v, bytes) else str(v)


def ent_fields(e, cs=None):
    d = {k: v for k, v in e.items() if k not in ("number", "_present", "_fxmask")}
    if cs is not None:
        # resolve configstring indices so that differences in registration order do not show
        def name(base, i):
            v = cs.get(base + i)
            return (v.decode("latin-1") if isinstance(v, bytes) else str(v)).lower() if v is not None else i
        if d.get("sound") and not (d.get("effects", 0) & 0x100):
            d["sound"] = name(291, d["sound"])
        if d.get("modelindex") and d["modelindex"] != 255:
            d["modelindex"] = name(35, d["modelindex"])
        fx = d.get("fx")
        if fx and not (d.get("effects", 0) & 0x8001):
            d["fx"] = tuple(name(647, fx[i]) if i % 3 == 0 and fx[i] else fx[i] for i in range(12))
    return d


def cmd_dump(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("tag")
    ap.add_argument("--bsp")
    a = ap.parse_args(argv)
    sess, events, _ = decode(a.tag)
    names = name_map(a.bsp)
    for n in sorted(sess.baselines):
        e = sess.baselines[n]
        o = e.get("origin", (0, 0, 0))
        print(n, names.get(origin_key(o), ["-"]), {k: v for k, v in e.items() if v not in (0, None, (0, 0, 0))})
    print("TE counts:", Counter(ev[2] for ev in events if ev[0] == "te"))
    print("effects:", Counter(ev[2] for ev in events if ev[0] == "fx"))
    return 0


def seen(sess):
    out = {}
    for f in sorted(sess.frames):
        ents, ps = sess.frames[f]
        for n, e in ents.items():
            out[n] = e
    return out


def by_origin_seen(sess):
    d = defaultdict(list)
    for n, e in seen(sess).items():
        if n <= 32:
            continue  # players
        d[origin_key(e.get("origin", (0, 0, 0)))].append((n, e))
    return d


def cmd_cmp(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--bsp")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--filter", default="", help="classname prefix for baseline diffs (needs --bsp)")
    a = ap.parse_args(argv)
    A, ea, _ = decode(a.a)
    B, eb, _ = decode(a.b)
    names = name_map(a.bsp)
    print("== configstrings: A %d, B %d" % (len(A.cs), len(B.cs)))
    diffs = 0
    for i in sorted(set(A.cs) | set(B.cs)):
        va, vb = A.cs.get(i), B.cs.get(i)
        if va != vb:
            diffs += 1
            if diffs <= (1000 if a.all else 60):
                print("  cs %4d  A=%r  B=%r" % (i, cs_text(va) if va is not None else None,
                                              cs_text(vb) if vb is not None else None))
    print("  %d differing configstrings" % diffs)
    # sound / effect / model name sets regardless of index order
    for lo, hi, kind in ((35, 291, "models"), (291, 647, "sounds"), (647, 903, "effects")):
        sa = {cs_text(v).lower() for k, v in A.cs.items() if lo <= k < hi}
        sb = {cs_text(v).lower() for k, v in B.cs.items() if lo <= k < hi}
        print("  %s only in A: %s" % (kind, sorted(sa - sb)))
        print("  %s only in B: %s" % (kind, sorted(sb - sa)))
    print("== baselines: A %d, B %d" % (len(A.baselines), len(B.baselines)))
    # match by origin (entity numbers may differ when other packages' entities spawn differently)
    def by_origin(sess):
        d = defaultdict(list)
        for n, e in sess.baselines.items():
            d[origin_key(e.get("origin", (0, 0, 0)))].append((n, e))
        return d
    oa, ob = by_origin(A), by_origin(B)
    print("== entities seen in frames: A %d, B %d" % (len(seen(A)), len(seen(B))))
    sa, sb = by_origin_seen(A), by_origin_seen(B)
    nd = 0
    for k in sorted(set(oa) | set(ob)):
        cls = names.get(k, ["?"])
        if a.filter and not any(c.startswith(a.filter) for c in cls):
            continue
        la, lb = oa.get(k, []), ob.get(k, [])
        if len(la) != len(lb):
            nd += 1
            print("  %s %s: A has %d entities, B %d" % (k, cls, len(la), len(lb)))
            continue
        for (na, xa), (nb, xb) in zip(la, lb):
            fa, fb = ent_fields(xa, A.cs), ent_fields(xb, B.cs)
            if fa != fb:
                nd += 1
                keys = sorted(set(fa) | set(fb))
                print("  %s %s #%d/#%d: %s" % (k, cls, na, nb, {kk: (fa.get(kk), fb.get(kk)) for kk in keys
                                                               if fa.get(kk) != fb.get(kk)}))
    print("  %d differing baseline groups" % nd)
    nd = 0
    for k in sorted(set(sa) | set(sb)):
        cls = names.get(k, ["?"])
        if a.filter and not any(c.startswith(a.filter) for c in cls):
            continue
        la, lb = sa.get(k, []), sb.get(k, [])
        if not la or not lb:
            if a.all:
                print("  seen only in %s: %s %s" % ("A" if la else "B", k, cls))
            continue
        fa = [ent_fields(x, A.cs) for _, x in la]
        fb = [ent_fields(x, B.cs) for _, x in lb]
        for x in fa:
            x.pop("frame", None)
        for x in fb:
            x.pop("frame", None)
        if sorted(map(repr, fa)) != sorted(map(repr, fb)):
            nd += 1
            print("  frame state %s %s: A %s  B %s" % (k, cls, fa, fb))
    common = len(set(sa) & set(sb))
    print("  %d origins seen in both, %d differ" % (common, nd))
    print("== temp entities A:", dict(sorted(Counter(e[2] for e in ea if e[0] == "te").items())))
    print("   temp entities B:", dict(sorted(Counter(e[2] for e in eb if e[0] == "te").items())))
    print("== effects A:", dict(Counter(e[2] for e in ea if e[0] == "fx")))
    print("   effects B:", dict(Counter(e[2] for e in eb if e[0] == "fx")))
    if a.all:
        for tag, ev in (("A", ea), ("B", eb)):
            print("-- events", tag)
            for e in ev:
                print("  ", e)
    return 0


def write_entities(src, dst, ents):
    """Copy a BSP with its entity lump replaced (the lump is moved to the end of the file)."""
    b = bytearray(Path(src).read_bytes())
    text = "".join("{\n" + "".join('"%s" "%s"\n' % kv for kv in e.items()) + "}\n" for e in ents)
    data = text.encode("latin-1") + b"\0"
    ofs = len(b)
    b += data
    struct.pack_into("<ii", b, 8, ofs, len(data))
    Path(dst).parent.mkdir(parents=True, exist_ok=True)
    Path(dst).write_bytes(bytes(b))


def cmd_mkmap(argv):
    """mkmap <src.bsp> <name> [--keep-spawn x,y,z] [--add 'k=v;k=v' ...]: writes base/maps/<name>.bsp of the copies"""
    ap = argparse.ArgumentParser()
    ap.add_argument("src")
    ap.add_argument("name")
    ap.add_argument("--keep-spawn", default="")
    ap.add_argument("--add", action="append", default=[])
    a = ap.parse_args(argv)
    ents = bsp_entities(a.src)
    out = []
    for e in ents:
        if e.get("classname") == "info_player_deathmatch" and a.keep_spawn:
            if origin_key(e.get("origin", "0 0 0").split()) != tuple(int(x) for x in a.keep_spawn.split(",")):
                continue
        out.append(e)
    for add in a.add:
        out.append(dict(kv.split("=", 1) for kv in add.split(";")))
    for t in TESTS:
        if t.exists():
            dst = t / "base" / "maps" / (a.name + ".bsp")
            write_entities(a.src, dst, out)
            print("wrote", dst, len(out), "entities")
    return 0


def main(argv):
    if not argv:
        print(__doc__ or "see header")
        return 2
    c, rest = argv[0], argv[1:]
    return {"run": cmd_run, "cmp": cmd_cmp, "dump": cmd_dump, "mkmap": cmd_mkmap}[c](rest)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
