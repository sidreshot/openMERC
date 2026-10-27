# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-800, SPEC-801, SPEC-802 test vectors (test harness, no game code)
#
# Runs the specs' test vectors against the OpenMerc menu runtime: writes small .rmf test pages into the user
# directory of a test home (top of the search path, never the game data), runs the client with a console script
# (om_after / om_menukey / om_menudump test commands of m_test.c / menu.c) through run_om.ps1, parses the
# console log and prints PASS/FAIL per vector. Uses the English data copy (pak0 fonts, as the vectors assume).
#
# usage: python vectors.py [--exe <openMerc.exe>] [--data D:\Games\SoF-test-I6m-en]
import argparse
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
# OM_I6M_VEC_HOME / OM_I6M_EXTRA: another package's home directory and extra settings (F5: its package ports)
HOME = os.environ.get("OM_I6M_VEC_HOME", r"C:\openmerc-data\I6m\home-vec")

PAGES = {
    # SPEC-800 vectors 1-5a, 15 (pages shown in frames of the given widths)
    "omt_p1": "<stm><font type small> ABC DE <br> A </stm>",
    "omt_p2": "<stm><font type small> A <br> <br> B </stm>",
    "omt_p3": "<stm><font type small> ABC ABC </stm>",
    "omt_p4": "<stm><font type small> <center> \"AB\" </stm>",
    "omt_p5": "<stm><font type small> <left> <text \"AB\" border 16 1 red> <right> <text \"AB\" border 16 1 red> </stm>",
    "omt_p5b": "<stm><font type small> <hbr> <left> <text \"AB\" border 16 1 red> <right> <text \"AB\" border 16 1 red> </stm>",
    "omt_p5a": "<stm><font type small> AB <br> <right> <text \"CD\" regular> </stm>",
    "omt_p5c": "<stm><font type small> AB <br> <right> <text \"CD\"> </stm>",
    "omt_p15": "<stm><font type small> <text \"X\" ifne omt_a 0 ifeq omt_a 0> <text \"Z\"> </stm>",
    "omt_p15b": "<stm><font type small> <text \"X\" ifne omt_b 0 ifeq omt_b 0> <text \"Z\"> </stm>",
    "omt_d1": "<stm><frame f1 200 0 page omt_p1><frame f2 100 0 page omt_p2><frame f3 40 0 page omt_p3>"
              "<frame f4 100 0 page omt_p4><frame f5a 100 0 page omt_p5a></stm>",
    "omt_d2": "<stm><frame f5 320 0 page omt_p5><frame f5b 320 0 page omt_p5b></stm>",
    "omt_d3": "<stm><frame f5c 100 0 page omt_p5c><frame f15 100 0 page omt_p15><frame f15b 100 0 page omt_p15b></stm>",
    # vector 6 / 7: frame cuts
    "omt_d6": "<stm><frame a 160 0><frame b 0 92><frame c 0 0></stm>",
    "omt_d7": "<stm><frame main 640 372 border 40 1 gray><frame chat 0 272 cut main></stm>",
    # vector 10: only the text up to the first </stm is used
    "omt_d10": "<stm> A </stm> B",
    # vector 11: includecvar inserts the cvar's text
    "omt_d11": "<stm><includecvar menu_x></stm>",
    # vector 12: string reference outside / inside quotes
    "omt_d12": "<stm><strings menu_generic><font type small> ^MENU_GENERIC_SURE^ <br> \"^MENU_GENERIC_SURE^\" </stm>",
    # vector 13: error report (developer 1), unknown tag on line 3
    "omt_d13": "<stm>\r\n<font type small>\r\n<frobnicate>\r\nA </stm>",
    # SPEC-802 vectors 1-5, 7, 10
    "omt_d20": "<stm><strings menu_dm_generic><font type small>"
               "<list \"^MENU_DM_GENERIC_NO_YES^\" bitmask 13 cvar menu_dmflags><br>"
               "<list \"16 Bit,8 Bit\" cvar s_loadas8bit><br>"
               "<list \"Half,Full\" match \"0.65,1.0\" cvar g_movescale><br>"
               "<list \"\" files \"maplists\" \"*\" \"lst\" cvar sv_maplistfile><br>"
               "<slider cvar m_cursor_speed min 4 max 50 step 2><br>"
               "<input cvar menu_chat width 100 maxchars 4 global><br>"
               "</stm>",
    "omt_d21": "<stm><frame pad 40 0><frame body 560 0 page omt_p21></stm>",
    "omt_p21": "<stm><hr width 200 line 2 tint 0xff0000ff></stm>",
    # SPEC-801 vectors 1, 3, 4, 8
    "omt_q": "<stm><font type small><text \"X\" key mouse1 \"echo VEC one\"><key mouse1 \"echo VEC two\"><br>"
             "<text \"Y\" key mouse1 \"echo VEC m\" key enter \"echo VEC e\">"
             "<key k \"echo VEC a;echo VEC b\"></stm>",
    "omt_a": "<stm> a </stm>",
    "omt_b": "<stm> b </stm>",
    "omt_ox": "<stm><onexit omt_changed omt_alter> ox </stm>",
    "omt_alter": "<stm> alter </stm>",
    "omt_other": "<stm> other </stm>",
}

STEPS_640 = [
    "om_menudump", "menu omt_d2",
    "om_menudump", "menu omt_d3",
    "om_menudump", "menuoff", "menu omt_d6",
    "om_menudump", "menuoff", "menu omt_d7",
    "om_menudump", "menuoff", "menu omt_d10",
    "om_menudump", "menuoff", "menu omt_d11",
    "om_menudump", "menuoff", "menu omt_d12",
    "om_menudump", "menuoff", "menu omt_d13",
    "om_menudump", "menuoff", "menu omt_d21",
    "om_menudump", "menuoff", "menu omt_d20",
    "om_menudump",
]


def script_640():
    pre = [
        "set developer 1", "set menu_dmflags 4100", "set s_loadas8bit 5", "set g_movescale 2",
        "set m_cursor_speed 60", "set omt_a 1", "set omt_b 0", "set menu_chat \"\"", "set omt_changed 0",
        "set menu_x \"<font type title>Hello\"", "menu omt_d1",
    ]
    ev = []
    f = 5
    # layout dumps: one step every 5 frames
    group = []
    for s in STEPS_640:
        group.append(s)
        if s.startswith("menu ") or s == STEPS_640[-1]:
            ev.append((f, ";".join(group)))
            group = []
            f += 5
    f += 5
    # SPEC-802 values after opening omt_d20
    ev.append((f, "echo VEC open s_loadas8bit $s_loadas8bit g_movescale $g_movescale m_cursor_speed "
                  "$m_cursor_speed menu_dmflags $menu_dmflags sv_maplistfile $sv_maplistfile"))
    f += 5
    ev.append((f, "om_menukey mouse1 3 4")); f += 5
    ev.append((f, "echo VEC dmflags1 $menu_dmflags")); f += 2
    ev.append((f, "om_menukey mouse1 3 4")); f += 5
    ev.append((f, "echo VEC dmflags2 $menu_dmflags")); f += 2
    ev.append((f, "om_menukey leftarrow 20 45")); f += 5
    ev.append((f, "echo VEC slider $m_cursor_speed")); f += 2
    for k in "abcdef":
        ev.append((f, "om_menukey %s" % k)); f += 2
    ev.append((f, "echo VEC menu_chat $menu_chat")); f += 5
    # SPEC-801
    ev.append((f, "menuoff;menu omt_q")); f += 5
    ev.append((f, "om_menukey mouse1 3 4")); f += 10
    ev.append((f, "echo VEC --")); f += 2
    ev.append((f, "om_menukey k")); f += 10
    ev.append((f, "echo VEC --")); f += 2
    ev.append((f, "om_menukey enter 3 14")); f += 10
    ev.append((f, "echo VEC --")); f += 2
    ev.append((f, "set m_cursor_speed 8")); f += 2
    for i in range(5):
        ev.append((f, "om_menukey +alt rightarrow %s" % ("320 240" if i == 0 else ""))); f += 2
    ev.append((f, "om_menudump")); f += 5
    ev.append((f, "menuoff;menu omt_a;menu omt_b;menu omt_a")); f += 5
    ev.append((f, "om_menudump")); f += 5
    ev.append((f, "menuoff;set omt_changed 1;menu omt_ox")); f += 5
    ev.append((f, "om_menukey escape")); f += 10
    ev.append((f, "om_menudump")); f += 5
    ev.append((f, "killmenu")); f += 5
    ev.append((f, "menu omt_other")); f += 10
    ev.append((f, "om_menudump")); f += 5
    ev.append((f, "menuoff;quit"))
    lines = pre + ['om_after %d "%s"' % (fr, cmd.replace('"', "'")) for fr, cmd in ev]
    return lines


def script_res():
    return ["menu omt_d6", 'om_after 5 "om_menudump;menuoff;menu quit"', 'om_after 15 "om_menudump;menuoff;quit"']


def run(exe, data, lines, tag, w, h):
    user = os.path.join(HOME, "user")
    os.makedirs(os.path.join(user, "menus"), exist_ok=True)
    for name, text in PAGES.items():
        with open(os.path.join(user, "menus", name + ".rmf"), "w", newline="") as fp:
            fp.write(text)
    cfg = os.path.join(HOME, "vec_%s.cfg" % tag)
    with open(cfg, "w", newline="") as fp:
        fp.write("\r\n".join(lines) + "\r\n")
    subprocess.call(["powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                     os.path.join(HERE, "run_om.ps1"), "-Exe", exe, "-Data", data, "-HomeDir", HOME,
                     "-Script", cfg, "-Tag", "vec_" + tag, "-Timeout", "120", "-Width", str(w), "-Height", str(h),
                     "-OutDir", os.path.join(HOME, "out"), "-Extra",
                     ("+set snd_dll OMsnd.dll " + os.environ.get("OM_I6M_EXTRA", "")).strip()])
    log = os.path.join(HOME, "out", "vec_%s.log" % tag)
    return open(log, encoding="latin-1").read() if os.path.exists(log) else ""


def dumps(log):
    """list of screens: {name, frames: [{name, rect, free, pad, page, widgets: [(cls, x, y, w, h, text)]}], line}"""
    out = []
    cur = None
    for line in log.splitlines():
        m = re.match(r"screen (\S+) scale \S+ ready (\d) .* depth (\d+) .* pointer (-?\d+) (-?\d+)", line)
        if m:
            cur = {"name": m.group(1), "depth": int(m.group(3)), "pointer": (int(m.group(4)), int(m.group(5))),
                   "frames": []}
            out.append(cur)
            continue
        m = re.match(r" frame (\S+) rect (-?\d+) (-?\d+) (-?\d+) (-?\d+) free (-?\d+) (-?\d+) (-?\d+) (-?\d+) pad (\d+) page (\S+)", line)
        if m and cur:
            g = [int(v) for v in m.groups()[1:10]]
            cur["frames"].append({"name": m.group(1), "rect": tuple(g[0:4]), "free": tuple(g[4:8]), "pad": g[8],
                                  "page": m.group(11), "widgets": []})
            continue
        m = re.match(r"  (\S+) +(-?\d+) +(-?\d+) +(-?\d+) +(-?\d+)( i)?( b)? ?(.*)$", line)
        if m and cur and cur["frames"]:
            cur["frames"][-1]["widgets"].append((m.group(1), int(m.group(2)), int(m.group(3)), int(m.group(4)),
                                                 int(m.group(5)), m.group(8)))
    return out


RESULTS = []


def check(name, ok, detail=""):
    RESULTS.append((name, ok))
    print("%s  %s  %s" % ("PASS" if ok else "FAIL", name, detail))


def frame(screens, sname, fname):
    for s in screens:
        if s["name"] == sname:
            for f in s["frames"]:
                if f["name"] == fname:
                    return f
    return None


def wlist(f):
    return [(w[1], w[2], w[3], w[4]) for w in f["widgets"]] if f else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"C:\openmerc-data\I6m\build-ov\release\openMerc.exe")
    ap.add_argument("--data", default=r"D:\Games\SoF-test-I6m-en")
    a = ap.parse_args()

    log = run(a.exe, a.data, script_640(), "640", 640, 480)
    sc = dumps(log)
    vec = [l.split("VEC ", 1)[1].strip() for l in log.splitlines() if "VEC " in l and not l.startswith("om_after")
           and "echo" not in l]

    check("800-1 bare words", wlist(frame(sc, "omt_d1", "f1")) == [(0, 0, 24, 10), (24, 0, 17, 10), (0, 10, 12, 10)],
          wlist(frame(sc, "omt_d1", "f1")))
    p2 = wlist(frame(sc, "omt_d1", "f2"))
    check("800-2 empty line", p2 is not None and p2[1][:2] == (0, 20), p2)
    p3 = wlist(frame(sc, "omt_d1", "f3"))
    check("800-3 wrap", p3 is not None and [w[:2] for w in p3] == [(0, 0), (0, 10)], p3)
    p4 = wlist(frame(sc, "omt_d1", "f4"))
    check("800-4 centre", p4 is not None and p4[0][0] == 41, p4)
    p5 = wlist(frame(sc, "omt_d2", "f5"))
    check("800-5 left/right row", p5 == [(0, 0, 50, 42), (270, 0, 50, 42)], p5)
    p5b = wlist(frame(sc, "omt_d2", "f5b"))
    check("800-5 with leading hbr", p5b is not None and [w[1] for w in p5b] == [10, 10], p5b)
    p5a = wlist(frame(sc, "omt_d1", "f5a"))
    check("800-5a anchor (regular)", p5a is not None and p5a[1][:2] == (83, 0), p5a)
    p5c = wlist(frame(sc, "omt_d3", "f5c"))
    check("800-5a anchor (not regular)", p5c is not None and p5c[1][:2] == (83, 10), p5c)
    f15 = frame(sc, "omt_d3", "f15")
    f15b = frame(sc, "omt_d3", "f15b")
    check("800-15 last condition wins", f15 is not None and f15b is not None and
          [w[5] for w in f15["widgets"]] == ["Z"] and [w[5] for w in f15b["widgets"]] == ["X", "Z"],
          ([w[5] for w in f15["widgets"]] if f15 else None, [w[5] for w in f15b["widgets"]] if f15b else None))
    rects = {fn: frame(sc, "omt_d6", fn)["rect"] if frame(sc, "omt_d6", fn) else None for fn in "abc"}
    check("800-6 frame cuts 640", rects == {"a": (0, 0, 160, 480), "b": (160, 0, 480, 92), "c": (160, 92, 480, 388)},
          rects)
    r7 = (frame(sc, "omt_d7", "main") or {}).get("rect"), (frame(sc, "omt_d7", "chat") or {}).get("rect")
    check("800-7 W != 0 cuts a column", r7 == ((0, 0, 640, 372), (40, 40, 560, 272)), r7)
    d10 = frame(sc, "omt_d10", "default")
    check("800-10 STM window", d10 is not None and [w[5] for w in d10["widgets"]] == ["A"],
          [w[5] for w in d10["widgets"]] if d10 else None)
    d11 = frame(sc, "omt_d11", "default")
    check("800-11 includecvar", d11 is not None and [w[5] for w in d11["widgets"]] == ["Hello"] and
          d11["widgets"][0][4] == 18, d11["widgets"] if d11 else None)
    d12 = frame(sc, "omt_d12", "default")
    check("800-12 reference outside/inside quotes", d12 is not None and len(d12["widgets"]) == 4,
          [w[5] for w in d12["widgets"]] if d12 else None)
    check("800-13 error line", '3 : Command "frobnicate" : Unknown keyword' in log)
    p21 = frame(sc, "omt_d21", "body")
    check("802-10 hr spans the content width, height 8", p21 is not None and [(w[0], w[2], w[3]) for w in wlist(p21)] == [(0, 560, 8)], wlist(p21))

    opened = [v for v in vec if v.startswith("open ")]
    o = dict(zip(opened[0].split()[1::2], opened[0].split()[2::2])) if opened else {}
    check("802-1 bitmask list shows bit 13 clear", o.get("menu_dmflags") == "4100", o)
    check("802-2 out-of-range index normalised", o.get("s_loadas8bit") == "1", o)
    check("802-3 unmatched value -> match[0]", o.get("g_movescale") == "0.65", o)
    check("802-4 files list stores the first map list", o.get("sv_maplistfile", "").upper() == "NONE" or
          o.get("sv_maplistfile", "").upper().startswith("RAV"), o)
    check("802-5 slider clamps on opening", o.get("m_cursor_speed") == "50", o)
    v = {x.split()[0]: x.split()[1] if len(x.split()) > 1 else "" for x in vec if not x.startswith("open")}
    check("802-1 MOUSE1 toggles bit 13 on", v.get("dmflags1") == "12292", v.get("dmflags1"))
    check("802-1 MOUSE1 toggles bit 13 off", v.get("dmflags2") == "4100", v.get("dmflags2"))
    check("802-5 LEFTARROW = value - step", v.get("slider") == "48", v.get("slider"))
    check("802-7 input maxchars", v.get("menu_chat") == "abcd", v.get("menu_chat"))
    seq = [x for x in vec if x in ("one", "two", "a", "b", "m", "e", "--")]
    check("801-3 widget binding before page binding, queue drop", seq[:2] == ["one", "--"], seq)
    check("801-3 'a;b' one per frame", seq[2:5] == ["a", "b", "--"], seq)
    check("801-4 ENTER = MOUSE1 first", seq[5:7] == ["m", "--"], seq)
    q = [s for s in sc if s["name"] == "omt_q"]
    check("801-5 keyboard pointer 320 -> 348", bool(q) and q[-1]["pointer"][0] == 348, q[-1]["pointer"] if q else None)
    a_ = [s for s in sc if s["name"] == "omt_a"]
    check("801-1 no duplicates on the stack", bool(a_) and a_[-1]["depth"] == 1, a_[-1]["depth"] if a_ else None)
    alt = [s for s in sc if s["name"] == "omt_alter"]
    check("801-8 onexit: ESC opens the apply page", len(alt) >= 1 and alt[0]["depth"] == 2,
          [(s["name"], s["depth"]) for s in alt])
    check("801-8 onexit: 'menu other' opens the apply page", len(alt) >= 2 and alt[1]["depth"] == 2,
          [(s["name"], s["depth"]) for s in alt])

    # resolution-dependent vectors
    log8 = run(a.exe, a.data, script_res(), "800", 800, 600)
    sc8 = dumps(log8)
    rects = {fn: frame(sc8, "omt_d6", fn)["rect"] if frame(sc8, "omt_d6", fn) else None for fn in "abc"}
    check("800-6 frame cuts 800x600", rects == {"a": (0, 0, 200, 600), "b": (200, 0, 600, 115), "c": (200, 115, 600, 485)},
          rects)
    log10 = run(a.exe, a.data, script_res(), "1024", 1024, 768)
    sc10 = dumps(log10)
    qm = frame(sc10, "quit", "main")
    qa = frame(sc10, "quit", "alert")
    check("800-8 popup window at 1024x768", qm is not None and qm["rect"] == (312, 264, 400, 240) and
          qa["rect"] == (352, 304, 320, 160), (qm["rect"] if qm else None, qa["rect"] if qa else None))

    n = sum(1 for _, ok in RESULTS if ok)
    print("%d/%d vectors pass" % (n, len(RESULTS)))


if __name__ == "__main__":
    main()
