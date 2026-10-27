#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-401 / SPEC-AMEND-005 black-box check (no game code)
#
# Explains an OpenMerc client log written with om_fxtrace 1 by the rules of SPEC-401 (as corrected by
# SPEC-AMEND-005): every temp entity block ("svc_temp_entity N" up to its "=> particles ..." summary) is
# checked against what the spec says that type produces - impact sounds and volumes, decal sizes and
# alphas, material effects, particle counts by kind and sprite, lights, timers. Types without a rule are
# listed as "not checked". Pair it with fxtrace_compare.py, which shows that every temp entity the server
# sent was handled.
#
#   python fxtrace_explain.py <client sof.log> [-v]
import re
import sys
from collections import Counter, defaultdict

SUMMARY = re.compile(r"\s+=> particles \+(\d+) \(billboard (\d+), line (\d+), ghoul (\d+), timer (\d+)\)"
                     r"(?: sprites (.*?))?(?:, lights \+(\d+))?$")
SOUND = re.compile(r"\s+sound (\d+) (\S+) vol ([\d.]+) atten ([\d.]+)")
DECAL = re.compile(r"\s+decal (\S+) size ([-\d.]+) \(end [-\d.]+\) rgba (\d+) (\d+) (\d+) (\d+)")


def blocks(path):
    cur = None
    for raw in open(path, encoding="latin-1"):
        line = raw.rstrip("\n")
        if line.startswith(("OMSHOTDONE", "OMFXSHOT")):
            continue
        m = re.match(r"svc_temp_entity (\d+)$", line)
        if m:
            if cur:
                yield cur
            cur = {"type": int(m.group(1)), "lines": [], "summary": None}
            continue
        if cur is None:
            continue
        if line.startswith("TE ") or line.startswith("  "):
            cur["lines"].append(line)
            s = SUMMARY.match(line)
            if s:
                sprites = Counter()
                for part in (s.group(6) or "").split(", "):
                    mm = re.match(r"(\S+) x(\d+)", part.strip())
                    if mm:
                        sprites[mm.group(1)] += int(mm.group(2))
                cur["summary"] = {"n": int(s.group(1)), "bb": int(s.group(2)), "line": int(s.group(3)),
                                  "ghoul": int(s.group(4)), "timer": int(s.group(5)), "sprites": sprites,
                                  "lights": int(s.group(7) or 0)}
                yield cur
                cur = None
            continue
        yield cur
        cur = None
    if cur:
        yield cur


def sounds(b):
    return [(int(m.group(1)), m.group(2), float(m.group(3)), float(m.group(4)))
            for m in (SOUND.match(x) for x in b["lines"]) if m]


def decals(b):
    return [(m.group(1).split("/")[-1], float(m.group(2)), int(m.group(6)))
            for m in (DECAL.match(x) for x in b["lines"]) if m]


def summ(b):
    return b["summary"] or {"n": 0, "bb": 0, "line": 0, "ghoul": 0, "timer": 0, "sprites": Counter(),
                            "lights": 0}


def line_values(b, pattern, cast=float):
    out = []
    for x in b["lines"]:
        m = re.search(pattern, x)
        if m:
            out.append(tuple(cast(g) for g in m.groups()))
    return out


def rule_bullet(b, t):
    """SPEC-401 types 61/62: impact mode 4 power 4, material hole, sound 0.613, material effect power 4/7."""
    p = 4 if t == 61 else 7
    out = []
    ds = decals(b)
    out.append(("at most one hole", len(ds) <= 1))
    for name, size, alpha in ds:
        out.append(("hole size %.2f in [1.8, 1.9] (power capped at 3)" % size, 1.8 <= size <= 1.9001))
        out.append(("hole alpha %d in [200, 240]" % alpha, 200 <= alpha <= 240))
    imp = [s for s in sounds(b) if abs(s[2] - 0.613) < 0.0005]
    out.append(("one impact sound at volume 0.613", len(imp) == (1 if ds else len(imp))))
    cp = line_values(b, r"concrete puffs x3 size ([\d.]+)")
    dp = line_values(b, r"dust puffs style (\d+) x(\d+)", int)
    for (sz,) in cp:
        out.append(("concrete puffs size %.1f = power %d x 1.5" % (sz, p), abs(sz - 1.5 * p) < 0.01))
    if cp:
        out.append(("dust puffs style 2 x2 (power > 2)", dp == [(2, 2)]))
        s = summ(b)
        out.append(("%d billboards = 3 smoke2 + 2 powder" % s["bb"],
                    s["bb"] == 5 and s["sprites"]["smoke2"] == 3 and s["sprites"]["powder"] == 2))
    out.extend(rule_shooter(b, t))
    return out


def rule_weapon_impact(b, t):
    """SPEC-401 types 6/7 (SPEC-AMEND-005 I5.4-I5.6): style 1 hole min(p, 3) x 0.6, else <= 0.1; material effect power p."""
    v = line_values(b, r"TE \d+ impact kind (\d+) material (-?\d+) at \(.*\) power (\d+) style (\d+) debris (\d+)", int)
    if not v:
        return [("impact line", False)]
    kind, mat, p, style, debris = v[0]
    out = []
    ds = decals(b)
    if mat < 0 or mat > 42 or style > 42:
        return [("no material / out of range: nothing", not ds and not sounds(b) and summ(b)["n"] == 0)]
    for name, size, alpha in ds:
        if style == 1:
            want = min(p, 3) * 0.6
            out.append(("style 1 hole %.2f in [%.2f, %.2f]" % (size, want, want + 0.1), want - 1e-3 <= size <= want + 0.1001))
        else:
            out.append(("style %d mark %.2f <= 0.1 (power 0)" % (style, size), size <= 0.1001))
    if t == 6:
        vol = 0.6 if style != 1 else min(1.0, 0.4 + 0.053333 * p)
        if style != 1 or p > 1.5:
            out.append(("impact sound volume %.3f" % vol, any(abs(x[2] - vol) < 0.001 for x in sounds(b))))
        for (sz,) in line_values(b, r"concrete puffs x3 size ([\d.]+)"):
            out.append(("concrete puffs size %.1f = power byte %d x 1.5" % (sz, p), abs(sz - 1.5 * p) < 0.01))
    return out


def rule_debris(b, t):
    """SPEC-401 type 18: count chunks, a smoke puff for chunks 0, 5, 10 ... (not type 32), break sound by material."""
    v = line_values(b, r"TE 18 debris x(\d+) size (\d+) chunk type (\d+) material (\d+)", int)
    if not v:
        return [("debris line", False)]
    count, size, ctype, mat = v[0]
    s = summ(b)
    want = 0 if ctype == 32 else (count + 4) // 5
    out = [("%d smoke puffs = every 5th of %d chunks" % (s["sprites"]["smoke2"], count), s["sprites"]["smoke2"] == want)]
    snd = sounds(b)
    if 17 <= mat <= 24 or mat not in (14, 15, 16, 28, 29, 30, 31, 32, 35, 37, 38, 42):
        name = "Break/Stone"
    elif 14 <= mat <= 16:
        name = "Break/Wood"
    elif mat in (28, 29, 30, 31, 32, 42):
        name = "Break/Metal"
    else:
        name = None
    if name:
        out.append(("sound %s volume 0.9" % name, any(name.lower() in x[1].lower() and abs(x[2] - 0.9) < 0.001
                                                       for x in snd)))
    return out


def rule_shotgun(b, t):
    """SPEC-401 type 34: 10 pellets, first power 4 (hole 1.8-1.9, the only impact sound), others 0.3-1.0."""
    out = []
    ds = decals(b)
    out.append(("%d holes <= 10 pellets" % len(ds), len(ds) <= 10))
    if ds:
        first, rest = ds[0], ds[1:]
        big = [d for d in ds if d[1] >= 1.8]
        out.append(("at most one 1.8-1.9 hole (first pellet)", len(big) <= 1))
        out.append(("other holes 0.3-1.0", all(0.3 <= d[1] <= 1.0001 for d in ds if d[1] < 1.8)))
        imp = [s for s in sounds(b) if abs(s[2] - 0.613) < 0.0005]
        out.append(("impact sounds %d = holes of power > 1.5 (%d)" % (len(imp), len(big)), len(imp) == len(big)))
    cp = [v[0] for v in line_values(b, r"concrete puffs x3 size ([\d.]+)")]
    dp = [v[1] for v in line_values(b, r"dust puffs style (\d+) x(\d+)", int)]
    out.append(("concrete puff sizes in {3.0, 4.5} (power trunc(rand(2, 4)))", all(s in (3.0, 4.5) for s in cp)))
    out.append(("dust x1 for power 2, x2 for power 3", len(cp) == len(dp) and
                all((s == 3.0 and d == 1) or (s == 4.5 and d == 2) for s, d in zip(cp, dp))))
    s = summ(b)
    out.append(("%d billboards = 3 per pellet + dust" % s["bb"], s["bb"] == 3 * len(cp) + sum(dp)))
    return out


def rule_knife(b, t):
    s = summ(b)
    snd = sounds(b)
    return [("sound 69 Weapons/Knife/Impact volume 0.6", any(x[0] == 69 and abs(x[2] - 0.6) < 0.001 for x in snd)),
            ("flash + 8 streaks: billboard 1 flare, line 8 lightning",
             s["bb"] == 1 and s["line"] == 8 and s["sprites"]["flare"] == 1 and s["sprites"]["lightning"] == 8)]


def rule_big_explosion(b, t):
    m = line_values(b, r"TE 44 explosion magnitude (\d+)", int)
    if not m:
        return [("magnitude line", False)]
    m = m[0][0]
    k = (m >> 6) + 2
    s = summ(b)
    want_bb = (m >> 5) + k + k + 1 + 8
    snd = sounds(b)
    return [("billboards %d = %d burning bits + %d bits + %d sparks + flash + 8 fireballs" %
             (s["bb"], m >> 5, k, k), s["bb"] == want_bb),
            ("streak lines %d = (m >> 5) + 4" % s["line"], s["line"] == (m >> 5) + 4),
            ("4 timers", s["timer"] == 4),
            ("sound 58 C4Exp volume 1.0 attenuation 0",
             any(x[0] == 58 and abs(x[2] - 1.0) < 0.001 and x[3] == 0 for x in snd))]


def rule_small_explosion(b, t):
    v = line_values(b, r"TE \d+ explosion size (\d+) sound (\d+)", int)
    if not v:
        return [("size line", False)]
    size, snd_b = v[0]
    s = summ(b)
    out = []
    if t == 56 and snd_b == 2:
        out.append(("type 56 byte 2: sound only", s["n"] == 0))
    else:
        out.append(("flash + 8 fireballs (%d billboards)" % s["bb"], s["bb"] == 9))
    if snd_b:
        want = {46: 91, 55: 90, 56: 89}[t]
        out.append(("sound %d volume 1 attenuation 0" % want,
                    any(x[0] == want and abs(x[2] - 1) < 0.001 and x[3] == 0 for x in sounds(b))))
    return out


def rule_c4(b, t):
    s = summ(b)
    return [("2 flashes + 8 fireballs (%d billboards)" % s["bb"], s["bb"] == 10),
            ("4 timers", s["timer"] == 4),
            ("sound 58 volume 0.8 attenuation 2",
             any(x[0] == 58 and abs(x[2] - 0.8) < 0.001 and abs(x[3] - 2) < 0.01 for x in sounds(b)))]


def rule_flashbang(b, t):
    s = summ(b)
    return [("2 flashes + 15 flares (%d billboards)" % s["bb"], s["bb"] == 17),
            ("one light", s["lights"] == 1),
            ("sound 54 volume 0.6 attenuation 0.3",
             any(x[0] == 54 and abs(x[2] - 0.6) < 0.001 and abs(x[3] - 0.3) < 0.01 for x in sounds(b)))]


def rule_explosion_debris(b, t):
    s = summ(b)
    return [("burning bits 3-4 + 2-3 and 3 sparks: %d billboards in 8..10" % s["bb"], 8 <= s["bb"] <= 10),
            ("no sound, no light", not sounds(b) and s["lights"] == 0)]


def rule_spark(b, t):
    s = summ(b)
    return [("flash + 14 sparks (%d billboards)" % s["bb"], s["bb"] == 15),
            ("one light", s["lights"] == 1),
            ("sound 0/1 volume 0.6 attenuation 3",
             any(x[0] in (0, 1) and abs(x[2] - 0.6) < 0.001 and abs(x[3] - 3) < 0.01 for x in sounds(b)))]


def one_sprite(name, count=1):
    def rule(b, t):
        s = summ(b)
        return [("%d %s billboard(s) (%d)" % (count, name, s["bb"]), s["bb"] == count and s["sprites"][name] == count)]
    return rule


def rule_shooter(b, t):
    """SPEC-401 types 60-63: the shooter's effect at the muzzle, only for a shooter that is not the local player."""
    head = next((x for x in b["lines"] if x.startswith("TE ")), "")
    m = re.search(r"effect (\d+)", head)
    if not m or int(m.group(1)) == 0:
        return []
    started = any("' started" in x for x in b["lines"])
    if "(local)" in head:
        return [("local shooter: no effect %s" % m.group(1), not started)]
    out = [("effect %s started at the muzzle (shooter not local)" % m.group(1), started)]
    mz = line_values(b, r"muzzle: bolt flash at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)")
    at = line_values(b, r"' started \(slot \d+, source \d+\) at \(([-\d.]+) ([-\d.]+) ([-\d.]+)\)")
    if mz and at:
        out.append(("effect placed at the bolt `flash` muzzle point",
                    all(abs(a - b_) <= 1.0 for a, b_ in zip(mz[0], at[0]))))
    return out


RULES = {61: rule_bullet, 62: rule_bullet, 18: rule_debris, 6: rule_weapon_impact, 7: rule_weapon_impact, 34: rule_shotgun, 32: rule_knife, 44: rule_big_explosion,
         46: rule_small_explosion, 55: rule_small_explosion, 56: rule_small_explosion, 40: rule_c4,
         20: rule_flashbang, 19: rule_explosion_debris, 0: rule_spark, 14: rule_spark, 10: rule_spark,
         21: one_sprite("shock2"), 47: one_sprite("shock3"), 26: one_sprite("smoke2", 12),
         45: one_sprite("smoke2"), 11: one_sprite("waterdrop"), 13: one_sprite("boom5"),
         60: rule_shooter, 63: rule_shooter}


def main():
    if len(sys.argv) < 2:
        print("usage: fxtrace_explain.py <client sof.log> [-v]")
        return 2
    verbose = "-v" in sys.argv
    count = Counter()
    passed = Counter()
    failed = defaultdict(list)
    for b in blocks(sys.argv[1]):
        t = b["type"]
        count[t] += 1
        rule = RULES.get(t)
        if not rule:
            continue
        for desc, ok in rule(b, t):
            if ok:
                passed[t] += 1
            else:
                failed[t].append(desc)
            if verbose:
                print("TE %d: %s %s" % (t, "ok " if ok else "BAD", desc))
    bad = 0
    print("%-6s %6s %8s %8s  %s" % ("type", "count", "checks", "failed", "rule"))
    for t in sorted(count):
        if t not in RULES:
            print("%-6d %6d %8s %8s  not checked" % (t, count[t], "-", "-"))
            continue
        nf = len(failed[t])
        bad += nf
        doc = (RULES[t].__doc__ or RULES[t].__name__).strip().split("\n")[0]
        print("%-6d %6d %8d %8d  %s" % (t, count[t], passed[t] + nf, nf, doc[:70]))
        for d in sorted(set(failed[t])):
            print("         FAIL %s (x%d)" % (d, failed[t].count(d)))
    print("RESULT: %s" % ("every checked temp entity as specified" if not bad else "%d failed checks" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
