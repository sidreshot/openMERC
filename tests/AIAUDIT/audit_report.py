#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc AIAUDIT - sums up the OMAUDIT lines of a console log (omaudit, game/src/ai/ai_audit.cpp; the log of
# audit_run.py or of a session where the command was typed). docs/impl/AIAUDIT.md explains the results.
#   audit_report.py <console.log> [--json out.json] [--maps tsr1,kos1,...] [--traces] [--banner <log>]
# --maps: the maps the session was asked for, so that a map that never started is named.
# --traces: also what each silent enemy's AI did in the last two seconds of its turn (the ai_trace 2 lines between
#           "OMTRACE <map> ent=<n> on" and "OMTRACE <map> off"), grouped by combat range code.
# The first lines say where the log comes from (engine version, platform, architecture, GL), read from the log.
# --banner: a second log of the same run, read only for those lines (sof.log starts after the engine's banner; the
#           standard output has it).
# Exit status 1 when the log holds no OMAUDIT line.
# Implements: black-box test harness (no game code)
import argparse
import collections
import json
import re
import sys

REACTED = ("ATTACK", "CHASE", "COWER")
# never fight (SPEC-722 cow/bull body): silence is their normal state
PASSIVE = ("m_x_bull",)
# a level whose own start script ends it before anyone can be checked
CUTSCENE_LEVELS = ("tsr2",)
HEAD = re.compile(r"^(openMERC V\S+.*|Platform: .*|Architecture: .*|GL_(?:VENDOR|RENDERER|VERSION): .*)$")


def fields(words):
    return dict(w.split("=", 1) for w in words if "=" in w)


def parse(lines):
    head, rows, started, done, notes, why = [], [], [], {}, [], {}
    for ln in lines:
        m = HEAD.match(ln.strip())
        if m and m.group(1) not in head:
            head.append(m.group(1))
        i = ln.find("OMWHY")
        if i >= 0:
            q = ln[i:].split()
            if len(q) > 2 and q[2].startswith("ent="):
                why.setdefault((q[1], q[2][4:]), {}).update(fields(q[3:]))
            continue
        i = ln.find("OMNOTE ")
        if i >= 0:
            notes.append(ln[i + 7:].strip())
            continue
        i = ln.find("OMAUDIT ")
        if i < 0:
            continue
        p = ln[i:].split()
        if len(p) < 3:
            continue
        if p[2] == "start":
            if p[1] in started:
                # started again (audit_run.py went on in a new session): only the last check of a map counts
                rows[:] = [r for r in rows if r["map"] != p[1]]
                done.pop(p[1], None)
                for k in [k for k in why if k[0] == p[1]]:
                    del why[k]
            else:
                started.append(p[1])
        elif p[2] == "done":
            done[p[1]] = fields(p[3:])
        else:
            rows.append(dict(map=p[1], result=p[2], **fields(p[3:])))
    return head, rows, started, done, notes, why


def label(rows, why):
    """Silent enemies that are silent by design get their own result and are not counted as silent."""
    for r in rows:
        if r["result"] != "SILENT":
            continue
        w = why.get((r["map"], r.get("ent", "")), {})
        sf, sf0 = int(r.get("sf", "0")), int(r.get("sf0", "0"))
        if r.get("class") in PASSIVE:
            r["result"] = "PASSIVE"
        elif w.get("start") == "1" and not w.get("hit", "").startswith("worldspawn"):
            # woken inside its vehicle, before its script lets it out
            r["result"] = "RIDER"
        elif sf & 128 and w.get("clear") == "0" and not w.get("blocker", "").startswith("0("):
            # never moves (spawnflag 128) and has no clear shot past a prop or vehicle: it holds its post
            r["result"] = "POSTED"
        elif sf0 & 22 == 22:
            # spawned dormant, blind and deaf (spawnflags 16, 2, 4): a cut-scene actor, woken out of order
            r["result"] = "SCRIPTED"


def traces(lines):
    cur, out = None, []
    for ln in lines:
        i = ln.find("OMTRACE ")
        if i >= 0:
            p = ln[i:].split()
            if len(p) > 2 and p[2].startswith("ent="):
                cur = dict(map=p[1], ent=p[2][4:], combat=[], plans=collections.Counter(),
                           queued=collections.Counter(), blocked=0)
                out.append(cur)
            else:
                cur = None
            continue
        if not cur:
            continue
        m = re.search(r"ai %s(:| )(.*)" % cur["ent"], ln)
        if not m:
            continue
        t = m.group(2).strip()
        if t.startswith("combat:"):
            cur["combat"].append(t)
        elif t.startswith("plan kind"):
            cur["plans"][t.split(" prio")[0]] += 1
        elif t.startswith("queue class"):
            cur["queued"][" ".join(t.split()[:4])] += 1
        elif "move blocked" in t:
            cur["blocked"] += 1
    codes = collections.Counter()
    for r in out:
        cs = [m.group(1) for m in (re.search(r"code (\d+)", c) for c in r["combat"]) if m]
        key = ",".join(sorted(set(cs))) or "-"
        codes[key] += 1
        last = r["combat"][-1][:150] if r["combat"] else "-"
        print("%-6s ent %-4s codes %-6s plans %s queued %s blocked %d\n        last: %s" % (
            r["map"], r["ent"], key, dict(r["plans"]), dict(r["queued"].most_common(2)), r["blocked"], last))
    print("silent enemies traced %d, by combat range code %s" % (len(out), dict(codes)))


def main():
    ap = argparse.ArgumentParser(description="summary of the OMAUDIT lines of a console log (docs/impl/AIAUDIT.md)")
    ap.add_argument("log")
    ap.add_argument("--json", help="write the rows and the per-level totals to this file")
    ap.add_argument("--maps", default="", help="the maps the session was asked for (comma-separated)")
    ap.add_argument("--traces", action="store_true", help="also the AI trace of each silent enemy")
    ap.add_argument("--banner", help="a second log of the same run, read only for the version and platform lines")
    a = ap.parse_args()

    lines = open(a.log, encoding="latin-1").read().splitlines()
    head, rows, started, done, notes, why = parse(lines)
    if a.banner:
        more = parse(open(a.banner, encoding="latin-1").read().splitlines())[0]
        head = [h for h in more if h not in head] + head
    print("log: %s" % a.log)
    print("run on: %s" % ("; ".join(head) if head else "unknown (no engine banner in this log)"))
    if not rows and not started:
        print("no OMAUDIT line in this log: omaudit did not run (cheats off, or no map loaded when it was called)")
        return 1

    asked = [m for m in a.maps.split(",") if m]
    tag = lambda m: m + (" (cut-scene level)" if m in CUTSCENE_LEVELS else "")
    print("levels started %d, finished %d" % (len(started), len(done)))
    if asked and [m for m in asked if m not in started]:
        print("  never started: %s" % " ".join(tag(m) for m in asked if m not in started))
    if [m for m in started if m not in done]:
        print("  not finished: %s" % " ".join(tag(m) for m in started if m not in done))
    for n in notes:
        print("  note: %s" % n)

    raw = collections.Counter(r["result"] for r in rows)
    label(rows, why)
    tally = collections.Counter(r["result"] for r in rows)
    times = sorted(float(r["t"]) for r in rows if r["result"] in REACTED and "t" in r)
    print("enemies checked %d: %s" % (len(rows), " ".join("%s %d" % kv for kv in sorted(tally.items()))))
    if raw != tally:
        print("  silent before the by-design labels (PASSIVE, RIDER, POSTED, SCRIPTED): %d" % raw["SILENT"])
    if times:
        print("  reaction time of those that reacted: median %.1f s, slowest %.1f s (game seconds)" % (
            times[len(times) // 2], times[-1]))
    lights = [(int(r["plight"]), int(r["clight"])) for r in rows if "plight" in r and "clight" in r]
    if lights:
        print("  player light at the check spots: entity field %d..%d, client record field %d..%d" % (
            min(l[0] for l in lights), max(l[0] for l in lights), min(l[1] for l in lights), max(l[1] for l in lights)))

    per = collections.defaultdict(collections.Counter)
    for r in rows:
        per[r["map"]][r["result"]] += 1
    print("\nper level (enemies: results)")
    for m in started:
        c = per.get(m, {})
        print("  %-6s %4d: %s%s" % (m, sum(c.values()), " ".join("%s %d" % kv for kv in sorted(c.items())) or "-",
                                   "" if m in done else "   NOT FINISHED"))

    for r in rows:
        if r["result"] in ("PASSIVE", "RIDER", "POSTED", "SCRIPTED"):
            print("not counted: %s %s %s %s sf0 %s" % (r["map"], r["result"], r.get("class"), r.get("name"), r.get("sf0")))
    bad = [r for r in rows if r["result"] in ("SILENT", "NOSPOT")]
    if bad:
        print("\nto look at: %d silent or without a place to stand, by kind" % len(bad))
        by = collections.Counter((r["result"], r.get("class", "-"), r.get("act", "-"), r.get("rec", "-"),
                                  r.get("dec", "-")) for r in bad)
        for (res, cls, act, rec, dec), n in by.most_common(40):
            print("%4d %-7s %-24s action %-3s %-28s decisions %s" % (n, res, cls, act, rec, dec))
        print("\none by one (first 60)")
        for r in bad[:60]:
            print(" ", r["map"], r["result"], r.get("class"), r.get("name"), "ent", r.get("ent"), "sf", r.get("sf"),
                  "at", r.get("pos"), "awake", r.get("awake"), "alerted", r.get("alerted"), "act", r.get("act"),
                  r.get("rec"))
    else:
        print("\nnothing to look at: no enemy was silent or without a place to stand")
    if a.traces:
        print("\nAI trace of the silent enemies")
        traces(lines)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(dict(run_on=head, started=started, done=done, notes=notes, rows=rows,
                           why={"%s %s" % k: v for k, v in why.items()}), f, indent=0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
