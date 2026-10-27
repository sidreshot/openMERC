#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G9 - the game-mode test suite: one dedicated-server run per case with scripted clients, for the original
# game module ("orig") and OpenMerc's ("om"). Output: <openmerc-data>/g9/<dll>_<case>/ (see modetest.py).
# Implements: SPEC-790 test vectors 1-12 (the parts observable with scripted clients)
#
# usage: suite.py --dll orig|om [case ...]      (no case = all)

import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent

# name: (map, deathmatch, seconds, [cvar sets], [client specs "name|userinfo|script|connect delay"])
CASES = {
    "dm": ("dm/jpndm2", 1, 22, [], [
        "Alpha||3:score;6:kill;9:score;12:help;14:putaway",
        "Bravo||7:kill;10:score;13:kill|0.5",
        "Charlie|spectator=1|11:score|1.0"]),
    "teamdm": ("dm/jpndm2", 1, 18, ["dmflags 128"], [
        "Alpha|skin=mullins,teamname=mullins|3:score;6:kill;9:score",
        "Bravo|skin=hawk,teamname=hawk|7:kill;10:score|0.5",
        "Charlie|skin=mullins,teamname=mullins|11:score|1.0"]),
    "teamdm2": ("dm/jpndm2", 1, 18, ["dmflags 128"], [
        "Alpha|skin=mullins,teamname=blue|3:score;6:kill;9:score",
        "Bravo|skin=hawk,teamname=red|7:kill;10:score|0.5",
        "Charlie|skin=mullins,teamname=blue|11:score|1.0",
        "Delta|spectator=1|12:score|1.5"]),
    "assassin": ("dm/jpndm2", 2, 24, [], [
        "Alpha||3:score;6:kill;9:help;12:score",
        "Bravo||8:kill;14:help|0.5",
        "Charlie||10:kill;15:score|1.0",
        "Delta|spectator=1|12:score|1.5"]),
    "arsenal": ("dm/jpndm2", 3, 20, [], [
        "Alpha||3:help;6:kill;10:help;12:score",
        "Bravo||8:kill;11:score|0.5"]),
    "ctf": ("dm/jpnctf1", 4, 24, ["ctf_control_delay_time 5"], [
        "Alpha|team_red_blue=0|3:score;9:kill;12:score",
        "Bravo|team_red_blue=1|5:score;10:userinfo team_red_blue 0;14:score|0.5",
        "Charlie|team_red_blue=1|7:score|1.0",
        "Delta|spectator=1|8:score|1.5"]),
    "ctf_forcejoin": ("dm/jpnctf1", 4, 20, ["dmflags 16384"], [
        "Alpha|team_red_blue=0|3:score;8:userinfo team_red_blue 1;11:score",
        "Bravo|team_red_blue=0|5:score|0.5",
        "Charlie|team_red_blue=0|6:score|1.0",
        "Delta|team_red_blue=0|7:score|1.5"]),
    "control": ("dm/jpncon1", 6, 22, ["ctf_control_delay_time 5", "control_limit 0"], [
        "Alpha|team_red_blue=0|3:score;9:kill;12:score",
        "Bravo|team_red_blue=1|5:score;14:score|0.5",
        "Charlie|spectator=1|8:score|1.0"]),
    "conquer": ("dm/jpnctb1", 7, 20, [], [
        "Alpha|team_red_blue=0|3:score;9:kill;12:score",
        "Bravo|team_red_blue=1|5:score;14:score|0.5"]),
    "conquer_numbered": ("dm/gerctb1", 7, 20, ["dmflags 32896"], [
        "Alpha|team_red_blue=0|3:score;9:kill;12:score",
        "Bravo|team_red_blue=1|5:score;14:score|0.5",
        "Charlie|spectator=1|8:score|1.0"]),
    "realistic": ("dm/jpndm2", 5, 20, [], [
        "Alpha||2:buttons 32;2:move 200 0 0;6:move 0 0 0;6:buttons 0;9:kill;12:score",
        "Bravo||3:score|0.5"]),
    "ghoulmodel": ("dm/jpndm2", 1, 30, ["cheats 1"], [
        "Alpha||3:cmd gimme ghoul_model"]),
    "ctf_capture": ("dm/jpnctf1", 4, 34, ["cheats 1", "ctf_control_delay_time 1", "ctf_loops 1"], [
        "Alpha|team_red_blue=0|2:cmd phantom;2.5:goto -94 -878 380;10:cmd phantom;10.1:goto off;"
        "12:cmd phantom;12.5:goto 32 -192 40;19:cmd phantom;19.1:goto off;24:score",
        "Bravo|team_red_blue=1|4:score;22:score|0.5"]),
    "ctf_drop": ("dm/jpnctf1", 4, 30, ["cheats 1", "ctf_control_delay_time 1"], [
        "Alpha|team_red_blue=0|2:cmd phantom;2.5:goto -94 -878 380;10:cmd phantom;10.1:goto off;14:kill;20:score",
        "Bravo|team_red_blue=1|6:score;18:score|0.5"]),
    "conquer_zone": ("dm/jpnctb1", 7, 32, ["cheats 1", "ctf_control_delay_time 1"], [
        "Alpha|team_red_blue=0|2:cmd phantom;2.5:goto 128 -160 70;9:cmd phantom;9.1:goto off;24:score",
        "Bravo|team_red_blue=1|5:score|0.5"]),
    "control_switch": ("dm/jpncon1", 6, 40, ["cheats 1", "ctf_control_delay_time 1", "control_limit 0"], [
        "Alpha|team_red_blue=0|2:cmd phantom;2.5:goto 512 1729 880;16:cmd phantom;16.1:goto off;34:score",
        "Bravo|team_red_blue=1|5:score|0.5"]),
    # team-row order: slot 0 (Blue) at 0 while Red's Bravo is at -2 (slot order vs ascending score)
    "ctf_order": ("dm/jpnctf1", 4, 16, [], [
        "Alpha|team_red_blue=0|9:score",
        "Bravo|team_red_blue=1|6:kill|0.5"]),
    "control_limit": ("dm/jpncon1", 6, 56, ["cheats 1", "ctf_control_delay_time 1", "control_limit 10"], [
        "Alpha|team_red_blue=0|2:cmd phantom;2.5:goto 512 1729 880;16:cmd phantom;16.1:goto off;34:score",
        "Bravo|team_red_blue=1|5:score|0.5"]),
    "dm_kill": ("dm/jpndm2", 1, 34, ["cheats 1"], [
        "Alpha||2:cmd phantom;2.5:goto 160 -112 60;9:cmd phantom;9.1:goto off;11:aim 2;24:aim off;26:score",
        "Bravo||2:cmd phantom;2.5:goto -39 -149 60;9:cmd phantom;9.1:goto off;27:score|0.5"]),
    "assassin_kill": ("dm/jpndm2", 2, 34, ["cheats 1"], [
        "Alpha||2:cmd phantom;2.5:goto 160 -112 60;9:cmd phantom;9.1:goto off;11:aim 2;24:aim off;26:score",
        "Bravo||2:cmd phantom;2.5:goto -39 -149 60;9:cmd phantom;9.1:goto off;27:score|0.5"]),
    # SPEC-AMEND-006 G9.14 with three players: Charlie stays at his spawn point; targets are random, so the run shows
    # the rule only when Alpha's target was Bravo (check the +4 and Alpha's next status layout)
    "assassin_kill3": ("dm/jpndm2", 2, 34, ["cheats 1"], [
        "Alpha||2:cmd phantom;2.5:goto 160 -112 60;9:cmd phantom;9.1:goto off;11:aim 2;24:aim off;26:score",
        "Bravo||2:cmd phantom;2.5:goto -39 -149 60;9:cmd phantom;9.1:goto off;27:score|0.5",
        "Charlie||28:score|1.0"]),
    "fraglimit": ("dm/jpndm2", 1, 30, ["fraglimit -1", "sv_maplist dm/jpndm2 dm/suddm3",
                                       "log_file_name logs/g9.log", "log_file_counter 1", "hostname G9test",
                                       "log_file_header HDR", "log_file_line_header LH:", "log_file_footer FTR"], [
        "Alpha||12:buttons 1;12.5:buttons 0;13:buttons 1;13.5:buttons 0",
        "Bravo|spectator=1||0.5"]),
    "timelimit": ("dm/jpndm2", 1, 85, ["timelimit 1", "sv_maplist dm/jpndm2,dm/suddm3",
                                       "log_file_name logs/t.log", "log_file_mode o", "hostname G9test"], [
        "Alpha||3:kill;63:score;72:buttons 1;72.5:buttons 0;74:buttons 1;74.5:buttons 0",
        "Bravo||6:kill|0.5"]),
    # does the CTF game log list teams with team play in the raw dmflags (0x80)? (ctf_fraglimit_team has none)
    "ctf_log_tp": ("dm/jpnctf1", 4, 24, ["fraglimit -3", "dmflags 160", "log_file_name logs/c.log",
                                        "log_file_mode o"], [
        "Alpha|team_red_blue=0|4:kill;8:kill",
        "Bravo|team_red_blue=1||0.5"]),
    "ctf_fraglimit_team": ("dm/jpnctf1", 4, 24, ["fraglimit -3", "dmflags 32", "log_file_name logs/c.log",
                                                "log_file_mode o"], [
        "Alpha|team_red_blue=0|4:kill;8:kill",
        "Bravo|team_red_blue=1||0.5"]),
}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="orig")
    ap.add_argument("--port", default="29490")  # G9 server port; clients use 29491..29499
    ap.add_argument("cases", nargs="*")
    a = ap.parse_args(argv)
    for name in a.cases or list(CASES):
        m, dm, secs, sets, clients = CASES[name]
        cmd = [sys.executable, str(HERE / "modetest.py"), "--dll", a.dll, "--map", m, "--dm", str(dm),
               "--tag", "%s_%s" % (a.dll if a.dll in ("orig", "om") else "dll", name), "--seconds", str(secs),
               "--port", a.port]
        for s in sets:
            cmd += ["--set", s]
        for c in clients:
            cmd += ["--client", c]
        print("==", name, flush=True)
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=secs + 120)
        print((r.stdout + r.stderr).strip()[-600:], flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
