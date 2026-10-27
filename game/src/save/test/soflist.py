#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - list the chunks of a SoF savegame (.sof) and compare two saves.
# Implements: SPEC-303 (chunk container, MD4-XOR checksum), SPEC-752 (game chunk names and records)
#
# usage: soflist.py FILE.sof [--all] [--decode]       chunk list (runs of equal ids collapsed unless --all)
#        soflist.py --seq FILE.sof                    one "ID size" line per chunk (input for diff tools)
#        soflist.py --diff A.sof B.sof [--brief]      compare the chunk sequences (ids and sizes) of two saves
#        soflist.py --stats FILE.sof                  chunk counts and bytes per id
#        soflist.py --dump ID[:n] FILE.sof            hex dump of the n-th (default first) chunk with that id
# Exit code 1 when a chunk fails its checksum or end marker.

import argparse
import struct
import sys
from collections import Counter

MARKER = 0x1234ABCD


# ---------------------------------------------------------------------------------------------------------------
# MD4 (RFC 1320), stdlib only (hashlib's md4 is often unavailable)
def _lrot(x, n):
    x &= 0xFFFFFFFF
    return ((x << n) | (x >> (32 - n))) & 0xFFFFFFFF


def _F(x, y, z):
    return (x & y) | (~x & z)


def _G(x, y, z):
    return (x & y) | (x & z) | (y & z)


def _H(x, y, z):
    return x ^ y ^ z


def md4(data):
    msg = bytearray(data)
    ml = (len(data) * 8) & 0xFFFFFFFFFFFFFFFF
    msg.append(0x80)
    while len(msg) % 64 != 56:
        msg.append(0)
    msg += struct.pack("<Q", ml)
    h = [0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476]
    for off in range(0, len(msg), 64):
        X = struct.unpack("<16I", msg[off:off + 64])
        a, b, c, d = h
        for i in range(16):
            s = (3, 7, 11, 19)[i % 4]
            a = _lrot(a + _F(b, c, d) + X[i], s)
            a, b, c, d = d, a, b, c
        for i in range(16):
            k = (i % 4) * 4 + i // 4
            s = (3, 5, 9, 13)[i % 4]
            a = _lrot(a + _G(b, c, d) + X[k] + 0x5A827999, s)
            a, b, c, d = d, a, b, c
        order = (0, 8, 4, 12, 2, 10, 6, 14, 1, 9, 5, 13, 3, 11, 7, 15)
        for i in range(16):
            s = (3, 9, 11, 15)[i % 4]
            a = _lrot(a + _H(b, c, d) + X[order[i]] + 0x6ED9EBA1, s)
            a, b, c, d = d, a, b, c
        h = [(h[0] + a) & 0xFFFFFFFF, (h[1] + b) & 0xFFFFFFFF, (h[2] + c) & 0xFFFFFFFF, (h[3] + d) & 0xFFFFFFFF]
    return struct.pack("<4I", *h)


def block_checksum(data):
    w = struct.unpack("<4I", md4(data))
    return w[0] ^ w[1] ^ w[2] ^ w[3]


# ---------------------------------------------------------------------------------------------------------------
def read_chunks(path, verify=True):
    """[(offset, id, data, ok)] - id as its four letters (SPEC-303: packed big-endian, stored little-endian)."""
    raw = open(path, "rb").read()
    out = []
    pos = 0
    while pos + 12 <= len(raw):
        cid, n, ck = struct.unpack_from("<IiI", raw, pos)
        name = struct.pack(">I", cid).decode("latin-1")
        if n < 0 or pos + 16 + n > len(raw):
            out.append((pos, name, b"", False))
            pos = len(raw)
            break
        data = raw[pos + 12:pos + 12 + n]
        mark = struct.unpack_from("<I", raw, pos + 12 + n)[0]
        ok = mark == MARKER and (not verify or block_checksum(data) == ck)
        out.append((pos, name, data, ok))
        pos += 16 + n
    if pos != len(raw):
        out.append((pos, "????", raw[pos:], False))
    return out


def cstr(b):
    return b.split(b"\0", 1)[0].decode("latin-1", "replace")


def decode(name, data):
    """Short readable content of chunks whose layout the specs give."""
    def i32(o):
        return struct.unpack_from("<i", data, o)[0]

    def f32(o):
        return struct.unpack_from("<f", data, o)[0]
    if name in ("COMM", "MPCM", "SNAM", "CVAR", "VALU", "SPID", "STRG"):
        return repr(cstr(data))
    if name == "GAME" and len(data) == 1688:
        return ("spawnpoint %r maxclients %d maxentities %d serverflags %#x autosave %d cinematic %d stats %s time %.1f"
                % (cstr(data[0x408:0x608]), i32(0x608), i32(0x60C), i32(0x610), data[0x614], data[0x615],
                   [i32(0x618 + 4 * k) for k in range(8)], f32(0x638)))
    if name == "LVLC" and len(data) >= 136:
        return "frame %d time %.1f level %r map %r" % (i32(0), f32(4), cstr(data[8:72]), cstr(data[72:136]))
    if name == "SCRP" and len(data) == 32:
        return "state/wait/len/ip/dbg/runner/caller/activator %s" % [i32(4 * k) for k in range(8)]
    if name == "FTSZ" and len(data) == 36:
        return str([i32(4 * k) for k in range(9)])
    if name == "AICC" and len(data) == 8:
        return "id %d class %d" % (i32(0), i32(4))
    if name == "PLPI" and len(data) == 16:
        return "item %d entity %d fade %d" % (i32(4), i32(8), i32(12))
    if name == "EDCT" and len(data) >= 0x80:
        return "number %d inuse %d origin (%.1f %.1f %.1f)" % (i32(0), i32(0x78), f32(4), f32(8), f32(12))
    if len(data) == 4 and name != "SHOT":
        return str(i32(0))
    return ""


def collapse(chunks):
    """Runs of the same id collapsed: [(id, count, sizes)]; a cvar record (CVAR, VALU, FLGS) counts as one 'CVR3'."""
    runs = []
    i = 0
    items = []
    while i < len(chunks):
        n = chunks[i][1]
        if n == "CVAR" and i + 2 < len(chunks) and chunks[i + 1][1] == "VALU" and chunks[i + 2][1] == "FLGS":
            items.append(("CVR3", sum(len(chunks[i + k][2]) for k in range(3))))
            i += 3
            continue
        items.append((n, len(chunks[i][2])))
        i += 1
    for name, size in items:
        data = b"\0" * size
        if runs and runs[-1][0] == name:
            runs[-1][1] += 1
            runs[-1][2].append(len(data))
        else:
            runs.append([name, 1, [len(data)]])
    return runs


def sizes_text(sizes):
    s = sorted(set(sizes))
    return str(s[0]) if len(s) == 1 else "%d..%d" % (s[0], s[-1])


def outline(chunks):
    """Section outline of a save (SPEC-303 engine part, SPEC-752 1.1/1.2 game part): [(section, text)]."""
    out = []
    names = [c[1] for c in chunks]
    datas = [c[2] for c in chunks]

    def i32(k):
        return struct.unpack_from("<i", datas[k], 0)[0] if len(datas[k]) >= 4 else None
    i = 0
    n = len(chunks)

    def take(name):
        nonlocal i
        if i < n and names[i] == name:
            i += 1
            return i - 1
        return None
    for nm in ("COMM", "SHOT", "MPCM"):
        k = take(nm)
        if k is not None:
            out.append((nm, "%d bytes %s" % (len(datas[k]), decode(nm, datas[k]) if nm != "SHOT" else "")))
    k = take("CVCN")
    if k is not None:
        cnt = i32(k)
        cv = []
        for _ in range(cnt):
            a, b, c = take("CVAR"), take("VALU"), take("FLGS")
            if a is None or b is None or c is None:
                break
            cv.append("%s=%s" % (cstr(datas[a]), cstr(datas[b])))
        out.append(("CVCN", "%d cvars" % cnt))
    # game section: GAME, CLNT [STRG] [INVN] per client
    k = take("GAME")
    if k is not None:
        out.append(("GAME", "%d bytes %s" % (len(datas[k]), decode("GAME", datas[k]))))
    clients = 0
    while i < n and names[i] == "CLNT":
        clients += 1
        sz = len(datas[i])
        i += 1
        strg = 0
        while take("STRG") is not None:
            strg += 1
        inv = take("INVN")
        out.append(("CLNT", "client %d: %d bytes, %d STRG, INVN %s" % (clients - 1, sz, strg,
                                                                     len(datas[inv]) if inv is not None else "-")))
    k = take("SPNM")
    if k is not None:
        pk = []
        for _ in range(i32(k)):
            s = take("SPID")
            if s is None:
                break
            pk.append(cstr(datas[s]))
        out.append(("SPNM", " ".join(pk)))
    for nm in ("FRNM", "CFST", "PRTS"):
        k = take(nm)
        if k is not None:
            out.append((nm, "%d bytes%s" % (len(datas[k]), " = %d" % i32(k) if nm == "FRNM" else "")))
    if i >= n:
        out.append(("level", "none (automatic save)"))
        return out
    k = take("FTSZ")
    if k is not None:
        out.append(("FTSZ", decode("FTSZ", datas[k])))
    k = take("LVLC")
    if k is not None:
        s = 0
        while take("STRG") is not None:
            s += 1
        out.append(("LVLC", "%d bytes, %d STRG, %s" % (len(datas[k]), s, decode("LVLC", datas[k]))))
    k = take("NMED")
    if k is not None:
        cnt = i32(k)
        nums, strg, sizes = [], 0, set()
        while i < n and names[i] == "EDNM":
            nums.append(i32(i))
            i += 1
            e = take("EDCT")
            if e is not None:
                sizes.add(len(datas[e]))
            while take("STRG") is not None:
                strg += 1
        out.append(("NMED", "%d entities (records %d, sizes %s, %d STRG), numbers %s" % (
            cnt, len(nums), sorted(sizes), strg, compress_ranges(nums))))
    k = take("SGNM")
    if k is not None:
        cnt = i32(k)
        for _ in range(cnt):
            take("SCID")
            i += 1
        out.append(("SGNM", "%d level globals" % cnt))
    k = take("SCVR")
    if k is not None:
        out.append(("SCVR", str(i32(k))))
    k = take("SCSZ")
    if k is not None:
        cnt = i32(k)
        scripts = []
        for _ in range(cnt):
            if take("SCID") is None:
                break
            nm = take("SNAM")
            sc = take("SCRP")
            j = i
            # the record ends with SCNM + SCND
            while i < n and names[i] != "SCND":
                i += 1
            i += 1
            st = struct.unpack_from("<8i", datas[sc], 0) if sc is not None else ()
            scripts.append("%s %s chunks %d" % (cstr(datas[nm]) if nm is not None else "?", list(st), i - j + 3))
        out.append(("SCSZ", "%d scripts" % cnt))
        for s in scripts:
            out.append(("  script", s))
    # AI block: eight AICT tables, then per-object chunks until OINM
    tables = []
    while i < n and names[i] == "AICT":
        cnt = i32(i)
        i += 1
        codes = Counter()
        for _ in range(cnt):
            if i < n and names[i] == "AICC":
                codes[struct.unpack_from("<i", datas[i], 4)[0]] += 1
                i += 1
        tables.append((cnt, codes))
    if tables:
        labels = ("models", "bolt-ons", "instances", "actions", "decisions", "senses", "bodies", "characters")
        for t, (cnt, codes) in enumerate(tables):
            out.append(("AICT", "%-10s %4d  classes %s" % (labels[t] if t < 8 else "?", cnt,
                                                         dict(sorted(codes.items())))))
    j = i
    while i < n and names[i] != "OINM":
        i += 1
    if i > j:
        cnt = Counter(names[j:i])
        out.append(("AI data", "%d chunks: %s" % (i - j, " ".join("%s:%d" % kv for kv in sorted(cnt.items())))))
    k = take("OINM")
    if k is not None:
        cnt = i32(k)
        e = Counter()
        while i < n and names[i] in ("EDOI", "EDBN", "EDBL"):
            e[names[i]] += 1
            i += 1
        out.append(("OINM", "%d attached-object records (EDOI %d, EDBN %d, EDBL %d)" % (cnt, e["EDOI"], e["EDBN"],
                                                                                       e["EDBL"])))
    k = take("PLNM")
    if k is not None:
        cnt = i32(k)
        items = []
        while i < n and names[i] == "PLPI":
            items.append(struct.unpack_from("<4i", datas[i], 0)[1:])
            i += 1
        w = take("PLWA")
        out.append(("PLNM", "%d items %s, PLWA %s" % (cnt, [it[0] for it in items],
                                                    datas[w].hex() if w is not None else "-")))
    if i < n:
        out.append(("rest", "%d more chunks from %s" % (n - i, names[i])))
    return out


def compress_ranges(nums):
    if not nums:
        return "-"
    parts = []
    start = prev = nums[0]
    for x in nums[1:] + [None]:
        if x is not None and x == prev + 1:
            prev = x
            continue
        parts.append(str(start) if start == prev else "%d-%d" % (start, prev))
        if x is not None:
            start = prev = x
    return ",".join(parts)


def hexdump(data, width=16):
    for o in range(0, len(data), width):
        row = data[o:o + width]
        print("  %04x  %-*s %s" % (o, width * 3, " ".join("%02x" % b for b in row),
                                  "".join(chr(b) if 32 <= b < 127 else "." for b in row)))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="+")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--decode", action="store_true")
    ap.add_argument("--seq", action="store_true")
    ap.add_argument("--diff", action="store_true")
    ap.add_argument("--stats", action="store_true")
    ap.add_argument("--brief", action="store_true")
    ap.add_argument("--dump", default=None)
    ap.add_argument("--outline", action="store_true")
    ap.add_argument("--noverify", action="store_true")
    a = ap.parse_args(argv)
    bad = 0
    if a.diff and a.outline:
        A = outline(read_chunks(a.files[0], not a.noverify))
        B = outline(read_chunks(a.files[1], not a.noverify))
        import difflib
        la = ["%-8s %s" % x for x in A]
        lb = ["%-8s %s" % x for x in B]
        for line in difflib.unified_diff(la, lb, a.files[0], a.files[1], n=100, lineterm=""):
            print(line)
        return 0
    if a.diff:
        import difflib
        A = read_chunks(a.files[0], not a.noverify)
        B = read_chunks(a.files[1], not a.noverify)
        sa = ["%s %d" % (n, len(d)) for _, n, d, _ in A]
        sb = ["%s %d" % (n, len(d)) for _, n, d, _ in B]
        ca, cb = Counter(n for _, n, _, _ in A), Counter(n for _, n, _, _ in B)
        print("A %s: %d chunks, B %s: %d chunks" % (a.files[0], len(A), a.files[1], len(B)))
        for k in sorted(set(ca) | set(cb)):
            if ca[k] != cb[k]:
                print("  count %-4s A %5d  B %5d" % (k, ca[k], cb[k]))
        if not a.brief:
            for line in difflib.unified_diff(sa, sb, "A", "B", n=1, lineterm=""):
                print(line)
        return 0
    for f in a.files:
        chunks = read_chunks(f, not a.noverify)
        nbad = sum(1 for c in chunks if not c[3])
        bad += nbad
        total = sum(16 + len(c[2]) for c in chunks)
        print("%s: %d chunks, %d bytes, %d bad" % (f, len(chunks), total, nbad))
        if a.outline:
            for sec, text in outline(chunks):
                print("  %-8s %s" % (sec, text))
            continue
        if a.dump:
            want, _, nth = a.dump.partition(":")
            nth = int(nth or 0)
            hits = [c for c in chunks if c[1] == want]
            if nth < len(hits):
                print("%s #%d at %d, %d bytes" % (want, nth, hits[nth][0], len(hits[nth][2])))
                hexdump(hits[nth][2])
            continue
        if a.seq:
            for _, n, d, ok in chunks:
                print("%s %d%s" % (n, len(d), "" if ok else " BAD"))
            continue
        if a.stats:
            cnt, byt = Counter(), Counter()
            for _, n, d, _ in chunks:
                cnt[n] += 1
                byt[n] += len(d)
            for n in sorted(cnt, key=lambda k: -byt[k]):
                print("  %-4s %6d chunks %9d bytes" % (n, cnt[n], byt[n]))
            continue
        if a.all:
            for off, n, d, ok in chunks:
                extra = decode(n, d) if a.decode else ""
                print("%8d %s %6d%s %s" % (off, n, len(d), "" if ok else " BAD", extra))
        else:
            for n, c, sizes in collapse(chunks):
                print("  %-4s x%-5d %s" % (n, c, sizes_text(sizes)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
