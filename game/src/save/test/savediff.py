#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G10 - content comparison of two saves of the same module: per section, which chunks differ and where.
# Entities are matched by number (EDNM), scripts by position, the AI block and the rest chunk by chunk.
# Implements: SPEC-303 (container), SPEC-752 1.2 (level section order)
#
# usage: savediff.py A.sof B.sof [--offsets] [--ignore-linkcount] [--top 20]

import argparse
import struct
import sys
from collections import Counter, defaultdict

import soflist


def split_level(chunks):
    """(prefix chunks up to FTSZ, {entity number: [chunks]}, rest chunks after the entities)."""
    names = [c[1] for c in chunks]
    try:
        i = names.index("FTSZ")
    except ValueError:
        return chunks, {}, []
    pre = chunks[:i]
    j = names.index("NMED", i)
    pre += chunks[i:j + 1]
    ents = {}
    k = j + 1
    while k < len(chunks) and names[k] == "EDNM":
        num = struct.unpack_from("<i", chunks[k][2])[0]
        grp = [chunks[k + 1]]
        k += 2
        while k < len(chunks) and names[k] == "STRG":
            grp.append(chunks[k])
            k += 1
        ents[num] = grp
    return pre, ents, chunks[k:]


def byte_diff(a, b):
    n = min(len(a), len(b))
    offs = [o for o in range(n) if a[o] != b[o]]
    return offs, len(a) != len(b)


def words(offs):
    return sorted({o & ~3 for o in offs})


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--offsets", action="store_true")
    ap.add_argument("--ignore-linkcount", action="store_true", help="EDCT offset 0x7C (relinked on save/load)")
    ap.add_argument("--top", type=int, default=20)
    a = ap.parse_args(argv)
    A = soflist.read_chunks(a.a)
    B = soflist.read_chunks(a.b)
    pa, ea, ra = split_level(A)
    pb, eb, rb = split_level(B)
    print("A %s: %d chunks, %d entities; B %s: %d chunks, %d entities" % (a.a, len(A), len(ea), a.b, len(B), len(eb)))
    # prefix (engine part, game section, FTSZ, LVLC)
    for x, y in zip(pa, pb):
        if x[1] != y[1]:
            print("prefix: chunk order differs: %s vs %s" % (x[1], y[1]))
            break
        offs, sz = byte_diff(x[2], y[2])
        if offs or sz:
            print("prefix %s: %d bytes differ%s; words %s" % (x[1], len(offs), " (size)" if sz else "",
                                                             [hex(o) for o in words(offs)[:a.top]]))
    # entities
    only_a = sorted(set(ea) - set(eb))
    only_b = sorted(set(eb) - set(ea))
    if only_a:
        print("entities only in A: %s" % only_a)
    if only_b:
        print("entities only in B: %s" % only_b)
    word_hits = Counter()
    nd = 0
    strdiff = 0
    examples = defaultdict(list)
    for n in sorted(set(ea) & set(eb)):
        x, y = ea[n][0][2], eb[n][0][2]
        offs, sz = byte_diff(x, y)
        if a.ignore_linkcount:
            offs = [o for o in offs if not 0x7C <= o < 0x80]
        if offs or sz:
            nd += 1
            for w in words(offs):
                word_hits[w] += 1
                if len(examples[w]) < 3:
                    examples[w].append((n, struct.unpack_from("<I", x, w)[0], struct.unpack_from("<I", y, w)[0]))
        if [c[2] for c in ea[n][1:]] != [c[2] for c in eb[n][1:]]:
            strdiff += 1
    print("entities in both: %d, records differing: %d, STRG runs differing: %d" % (
        len(set(ea) & set(eb)), nd, strdiff))
    for w, c in word_hits.most_common(a.top):
        ex = ", ".join("e%d %08x/%08x" % e for e in examples[w])
        print("  EDCT word 0x%03x differs in %d entities (%s)" % (w, c, ex))
    # rest: chunk by chunk
    cnt_a, cnt_b = Counter(c[1] for c in ra), Counter(c[1] for c in rb)
    for k in sorted(set(cnt_a) | set(cnt_b)):
        if cnt_a[k] != cnt_b[k]:
            print("  count %s A %d B %d" % (k, cnt_a[k], cnt_b[k]))
    diffs = Counter()
    same = 0
    for x, y in zip(ra, rb):
        if x[1] != y[1]:
            diffs["ORDER " + x[1] + "/" + y[1]] += 1
            continue
        if x[2] != y[2]:
            diffs[x[1]] += 1
        else:
            same += 1
    print("after the entities: %d / %d chunks, %d equal; differing by id: %s" % (len(ra), len(rb), same,
                                                                              dict(diffs.most_common(30))))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
