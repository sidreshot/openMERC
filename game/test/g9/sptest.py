#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G9 - single-player checks of the game modes through the original SoF.exe as a hidden dedicated server.
# A dedicated server forces deathmatch, so G6b's single-player proxy module (own test code, a snapshot in
# <openmerc-data>/g9/dll/spproxy.dll) is installed as base\gamex86.dll; it loads the module under test
# (gamex86.dll.orig, or OpenMerc's copied to base\gamex86.om.dll) and force-sets deathmatch 0 / maxclients 1 before
# its Init. The recorder is modetest.py with more stats (selected item 0, health 1, armour 5, weapon 6, item count 7).
# Implements: SPEC-790 Behaviour 11 (status meter), test vector 10 (`defaultweapons`)
#
# usage: sptest.py --dll orig|om [case ...]     output: <openmerc-data>/g9/<dll>_<case>/ (see modetest.py)

import argparse
import os
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import modetest  # noqa: E402

PROXY = modetest.DATA / "dll" / "spproxy.dll"
modetest.STATS.update({0: "item", 1: "health", 5: "armor", 6: "weapon", 7: "itemcount"})

# name: (map, seconds, [cvar sets], [client specs as modetest.py])
# SP levels start with a cinematic (commands are ignored while it freezes the player, ~15-20 s), so the
# `defaultweapons` cheat comes at 24 s; then the weapons and items are cycled so the kit shows in stats 6 / 0 / 7.
CYCLE = ";".join(["%d:cmd weapnext" % t for t in range(26, 40, 2)] + ["%d:cmd itemnext" % t for t in range(40, 46)])
CASES = {
    "sp_sib": ("sib1", 48, ["cheats 1"], ["Alpha||24:cmd defaultweapons;" + CYCLE]),
    "sp_irqb": ("irq1b", 48, ["cheats 1"], ["Alpha||24:cmd defaultweapons;" + CYCLE]),
    # SPEC-AMEND-005 G9.11: the meter is the world spawner's alert level; firing raises it, then it decays
    # bolt-on records of the body registry (G9's g3hooks.modelObject): weapons register no skins, a skinned accessory
    # only on the material named like the model (compare with game/src/roster/test/g4cap.py diff)
    "sp_reg_arm1": ("arm1", 8, [], ["Alpha||"]),
    "sp_reg_jpn1": ("jpn1", 8, [], ["Alpha||"]),
    "sp_meter": ("nyc1", 40, [], ["Alpha||" + ";".join("%d:buttons 1;%d.3:buttons 0" % (t, t)
                                                       for t in range(22, 27))]),
}


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--dll", default="orig")
    ap.add_argument("--port", default="29490")  # G9 server port; clients use 29491..29499
    ap.add_argument("cases", nargs="*")
    a = ap.parse_args(argv)
    base = modetest.TEST / "base"
    if a.dll == "orig":
        target = "gamex86.dll.orig"
    else:
        shutil.copyfile(modetest.OM_DLL if a.dll == "om" else Path(a.dll), base / "gamex86.om.dll")
        target = "gamex86.om.dll"
    for name in a.cases or list(CASES):
        m, secs, sets, clients = CASES[name]
        tag = "%s_%s" % (a.dll if a.dll in ("orig", "om") else "dll", name)
        os.environ["G6B_PROXY_TARGET"] = target
        os.environ["G6B_PROXY_LOG"] = str(modetest.DATA / ("proxy_%s.log" % tag))
        args = ["--dll", str(PROXY), "--map", m, "--dm", "0", "--maxclients", "1", "--tag", tag,
                "--seconds", str(secs), "--port", a.port, "--wait", "60"]
        for s in sets:
            args += ["--set", s]
        for c in clients:
            args += ["--client", c]
        print("==", name, flush=True)
        modetest.main(args)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
