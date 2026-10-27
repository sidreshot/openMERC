#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G9 - compare two game-mode test runs (original vs OpenMerc module) client by client: the configstrings
# the modes write, string-package prints (scoreboards, status layouts, mode messages, obituaries), name-colour
# tables, score / meter stats and map changes, normalised (no timing; pings masked), plus the game-log files.
# Implements: SPEC-790 (verification)
#
# usage: compare.py <case> [--a orig] [--b om] [--all]   -> prints a unified diff per client and a verdict

import argparse
import difflib
import json
import re
import sys
from pathlib import Path

DATA = Path(r"C:\openmerc-data\g9")
CS_KEEP = set([8, 9, 10, 32, 33, 34]) | set(range(1415, 1447)) | set(range(1447, 1451)) | set(range(3545, 3555)) | \
    set(range(1464, 1493))
PING_ROWS = {"client_sb": 5, "spect_sb": 4, "client_ctf_sb": 5}


def mask(text):
    w = text.split()
    if w and w[0] in PING_ROWS and len(w) > PING_ROWS[w[0]]:
        w[PING_ROWS[w[0]]] = "P"
        return " ".join(w)
    return text.rstrip()


def canon(events, full=False, dedupe=False):
    out = []
    for e in events:
        k = e["kind"]
        if k == "cs" and (full or e["idx"] in CS_KEEP):
            out.append("cs %d = %r" % (e["idx"], e["value"]))
        elif k in ("sp", "obit"):
            out.append("%s %s | %s" % (k, e["name"], mask(e["text"])))
        elif k == "print":
            out.append("print[%d] %s" % (e["level"], e["text"].rstrip()))
        elif k == "names":
            out.append("names %s" % (e["runs"],))
        elif k == "ps":
            for f in ("score", "meter", "pm_type"):
                if f in e:
                    out.append("ps %s %s" % (f, e[f]))
            if "flags" in e:
                out.append("ps flags&0x200 %d" % (e["flags"] & 0x200))
            if "pm_flags" in e:
                out.append("ps pm_flags&0x40 %d" % (e["pm_flags"] & 0x40))
            if "layouts" in e:
                out.append("ps layouts %d" % e["layouts"])
        elif k == "serverdata":
            out.append("serverdata %s dm %d" % (e["level"], e["dm"]))
        elif k == "center":
            out.append("center %r" % e["text"])
    # repeated identical stat lines collapse (frame timing differences)
    res = []
    for x in out:
        if res and res[-1] == x and x.startswith("ps "):
            continue
        res.append(x)
    if not dedupe:
        return res
    # Layout blocks (a scoreboard from LAYOUT_RESET, or a status layout) that repeat the previous block of the
    # same kind are the 32-frame refreshes; their phase depends on when the level started, so they are dropped.
    final, last = [], {}
    i = 0
    while i < len(res):
        x = res[i]
        kind = None
        if x.startswith("sp DM_GENERIC_LAYOUT_RESET"):
            kind = "score"
        elif x.startswith(("sp DM_ASSASSIN_LAYOUT", "sp DM_ARSENAL_LAYOUT")):
            kind = "status"
        if not kind:
            final.append(x)
            i += 1
            continue
        j = i + 1
        while j < len(res) and res[j].startswith("sp ") and "LAYOUT" in res[j] and                 not res[j].startswith("sp DM_GENERIC_LAYOUT_RESET") and                 not (kind == "status" and res[j].split(" |")[0] == x.split(" |")[0]):
            j += 1
        block = res[i:j]
        if last.get(kind) != block:
            final.extend(block)
        last[kind] = block
        i = j
    return final


def modesonly(lines):
    teams = any(x.startswith("serverdata") and x.split()[-1] in ("4", "6", "7") for x in lines)
    out = []
    for x in lines:
        if x.startswith("cs 14") and 1415 <= int(x.split()[1]) <= 1446 and not teams:
            continue
        if x.startswith(("sp ITEMS_", "sp GENERAL_TEXT_NOKILL", "print[", "ps pm_flags", "ps pm_type")):
            continue
        out.append(x)
    return out


MODE_CS = set([8, 9, 10, 32, 33, 34]) | set(range(1447, 1451)) | set(range(3545, 3555))
G18_TEXTS = ("ITEMS_", "GENERAL_", "DM_GENERIC_TEXT_SPECTATOR", "DM_GENERIC_ENTERED")


def semantic(events):
    """Timing-free view of what the game modes produced: mode configstrings, mode messages (in order), score and
    meter sequences, icon tables, map changes, and the *set* of distinct scoreboards / status layouts shown."""
    cats = {"cs": [], "msg": [], "score": [], "meter": [], "icons": [], "maps": []}
    boards, status = set(), set()
    cur, curkind = [], None

    def flush():
        nonlocal cur, curkind
        if cur:
            (boards if curkind == "score" else status).add(" / ".join(cur))
        cur, curkind = [], None

    def add(cat, x):
        if not cats[cat] or cats[cat][-1] != x:
            cats[cat].append(x)
    for e in events:
        k = e["kind"]
        # the test ends with the clients leaving one by one in an order that differs from run to run: everything
        # from the first "... disconnected" on is the harness's end, not the mode's
        if k == "print" and e["text"].rstrip().endswith(" disconnected"):
            break
        if k == "sp" and "LAYOUT" in e["name"]:
            text = mask(e["text"])
            if e["name"] == "DM_GENERIC_LAYOUT_RESET":
                flush()
                curkind = "score"
                continue
            if e["name"].startswith(("DM_ASSASSIN_LAYOUT_TARGET", "DM_ASSASSIN_LAYOUT_NO_TARGET",
                                     "DM_ARSENAL_LAYOUT")):
                flush()
                curkind = "status"
            cur.append(text)
            continue
        if k in ("sp", "obit"):
            flush()
            if not e["name"].startswith(G18_TEXTS):
                add("msg", "%s %s" % (k, e["text"].rstrip()))
        elif k == "cs" and e["idx"] in MODE_CS:
            add("cs", "cs %d = %r" % (e["idx"], e["value"]))
        elif k == "ps":
            if "score" in e:
                add("score", "score %d" % e["score"])
            if "meter" in e:
                add("meter", "meter %d" % e["meter"])
        elif k == "names":
            add("icons", "names %s" % (e["runs"],))
        elif k == "serverdata":
            add("maps", "map %s" % e["level"])
    flush()
    out = []
    for c in ("maps", "cs", "msg", "score", "meter", "icons"):
        out += ["%s: %s" % (c, x) for x in cats[c]]
    out += ["scoreboard: %s" % b for b in sorted(boards)]
    out += ["status: %s" % b for b in sorted(status)]
    return out


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("case")
    ap.add_argument("--a", default="orig")
    ap.add_argument("--b", default="om")
    ap.add_argument("--full", action="store_true", help="all configstrings")
    ap.add_argument("--refresh", action="store_true", help="keep repeated layout blocks (32-frame refreshes)")
    ap.add_argument("--semantic", action="store_true",
                    help="timing-free comparison of what the modes produced (see semantic())")
    ap.add_argument("--modes", action="store_true",
                    help="only what the game modes write: drop player-info configstrings outside the team-assigning "
                         "modes, item pickup texts, kill refusals and plain prints (game-core output)")
    a = ap.parse_args(argv)
    da, db = DATA / ("%s_%s" % (a.a, a.case)), DATA / ("%s_%s" % (a.b, a.case))
    total = 0
    for fa in sorted(da.glob("*.json")):
        fb = db / fa.name
        if not fb.exists():
            print("missing", fb)
            total += 1
            continue
        ca = canon(json.loads(fa.read_text(encoding="utf-8")), a.full, not a.refresh)
        cb = canon(json.loads(fb.read_text(encoding="utf-8")), a.full, not a.refresh)
        if a.modes:
            ca, cb = modesonly(ca), modesonly(cb)
        if a.semantic:
            ca = semantic(json.loads(fa.read_text(encoding="utf-8")))
            cb = semantic(json.loads(fb.read_text(encoding="utf-8")))
        diff = list(difflib.unified_diff(ca, cb, "%s/%s" % (a.a, fa.stem), "%s/%s" % (a.b, fa.stem), n=1,
                                         lineterm=""))
        n = sum(1 for d in diff if d[:1] in "+-" and not d.startswith(("+++", "---")))
        total += n
        print("== %s: %d lines, %d differing" % (fa.stem, len(ca), n))
        for d in diff:
            print(d)
    ga, gb = da / "gamelog", db / "gamelog"
    if ga.exists() or gb.exists():
        for f in sorted(set(p.name for p in ga.glob("*")) | set(p.name for p in gb.glob("*"))):
            ta = (ga / f).read_text(encoding="latin-1").splitlines() if (ga / f).exists() else ["<missing>"]
            tb = (gb / f).read_text(encoding="latin-1").splitlines() if (gb / f).exists() else ["<missing>"]
            # pings and the level time at the end (signon times differ from run to run) are masked
            ta = [re.sub(r"Game Duration : [\d:]+", "Game Duration : D", re.sub(r"Ping \d+", "Ping P", x)) for x in ta]
            tb = [re.sub(r"Game Duration : [\d:]+", "Game Duration : D", re.sub(r"Ping \d+", "Ping P", x)) for x in tb]
            diff = list(difflib.unified_diff(ta, tb, "%s/log/%s" % (a.a, f), "%s/log/%s" % (a.b, f), lineterm=""))
            n = sum(1 for d in diff if d[:1] in "+-" and not d.startswith(("+++", "---")))
            total += n
            print("== game log %s: %d differing" % (f, n))
            for d in diff:
                print(d)
    print("VERDICT %s: %s (%d differing lines)" % (a.case, "SAME" if total == 0 else "DIFFERENT", total))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
