#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# OpenMerc G6b - in-game test programs for SPEC-750/751 test vectors (our own test data, never shipped).
# The level script of tsr1 (`ds/tsr1/tsr1pre.os`, run when the client begins) is replaced by a test program, placed
# as a loose file in the run's user directory (which overrides the paks, SPEC-300 1). The program reports through
# text prints (svc_print) and string-package captions, so the same capture of the original module and of OpenMerc
# can be compared frame by frame (g6b_timeline.py).
#   main     V2 wait quantisation, V3 MOVE timing (spawned func_wall), V8 early signal, V7 field read types,
#            SPEC-751 ROF playback (generic_door_cw.rof, 25 frames) and end signal, SOUND, caption, PRECACHE_SOUND,
#            MUSIC, SET_VIEW_YAW, CONSOLE, cinematic start/end (freeze byte), V5 fn8 range, V4 use while waiting /
#            after SUSPEND / after EXIT (spawned script_runner), SPEC-AMEND-003 2e second MOVE of a moving entity
#   runaway  V1: `L: x += 1; goto L` on the first activation
#   spawnwall SPEC-AMEND-005 G6b.7: func_wall *31 with `origin` as a vector constant and as a string (F5)
# Implements: SPEC-750 (test vectors 1-5, 7, 8), SPEC-751 (test vectors 1, 2), SPEC-AMEND-003 (test vector 3)
#
# usage: g6b_vectors.py main|runaway|spawnwall <outdir>     writes <outdir>/ds/tsr1/tsr1pre.os (+ g6bsub.os)

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from os_asm import Asm, T_INT, T_FLOAT, T_VECTOR, T_ENTITY, EQ, LT, LE, GT, GE, NE  # noqa: E402


def sub_script():
    a = Asm()
    a.print_text("sub run\n")
    a.wait(2.0)
    a.print_text("sub after wait\n")
    a.suspend()
    a.print_text("sub after suspend\n")
    a.exit()
    return a.build()


def main_script():
    a = Asm()
    a.field("movetype", T_INT)
    a.field("wait", T_FLOAT)
    a.field("origin", T_VECTOR)
    for n, t in (("e", T_ENTITY), ("p", T_ENTITY), ("sub", T_ENTITY), ("sig", T_INT), ("sig2", T_INT),
                 ("sigr", T_INT), ("t", T_INT), ("f", T_FLOAT), ("x", T_FLOAT), ("mn", T_INT), ("mx", T_INT),
                 ("i", T_INT), ("j", T_INT), ("sig3", T_INT)):
        a.local(n, t)
    v = a.pvar
    a.string_package("tsr1")
    a.assign(lambda: a.pfn(4), lambda: v("p"))
    a.print_text("G6B start\n")
    # V2: wait 0.33 resumes on the 0.1 s grid (3 frames); wait 0 and wait -1 do not yield
    a.print_text("V2 a\n")
    a.wait(0.33)
    a.print_text("V2 b\n")
    a.wait(0.0)
    a.print_text("V2 c\n")
    a.wait(-1.0)
    a.print_text("V2 d\n")
    # a brush entity in front of the player start (-576 976 40, facing +y), mover type push
    a.spawn("e", [("classname", "func_wall"), ("model", "*31"), ("origin", "-576 1000 60")])
    a.print_text("spawned\n")
    a.assign(lambda: a.pint(2), lambda: a.pfield("e", "movetype"))
    # V3: move (0,0,96) in 3 s: speed 32, N = 31 frames, exact snap, signal in the arrival frame
    a.move("e", (0, 0, 96), dur=3.0, sig="sig")
    a.waitsignals(["sig"])
    a.print_text("V3 arrived\n")
    # V8: the signal of a move that completed before the WAIT_SIGNALS is not lost; 0x80 clears it
    a.move("e", (0, 0, 8), dur=0.1, sig="sig2")
    a.wait(1.0)
    a.waitsignals(["sig2"])
    a.print_text("V8 cont\n")
    a.if_not(lambda: v("sig2"), lambda: a.pint(0), EQ, "v8n")
    a.print_text("V8 cleared\n")
    a.label("v8n")
    # V7: an int variable reads 0 from a float field, a float variable reads 2.5
    a.assign(lambda: a.pfloat(2.5), lambda: a.pfield("e", "wait"))
    a.assign(lambda: a.pfield("e", "wait"), lambda: v("t"))
    a.assign(lambda: a.pfield("e", "wait"), lambda: v("f"))
    a.if_not(lambda: v("t"), lambda: a.pint(0), EQ, "v7a")
    a.print_text("V7 int 0\n")
    a.label("v7a")
    a.if_not(lambda: v("f"), lambda: a.pfloat(2.5), EQ, "v7b")
    a.print_text("V7 float 2.5\n")
    a.label("v7b")
    # SPEC-751 3: 25 ROF frames one per server frame from the command's frame, end signal one frame later
    a.rof("e", "generic_door_cw.rof", sig="sigr")
    a.waitsignals(["sigr"])
    a.print_text("ROF done\n")
    # sounds, captions, precache, music, view yaw, console
    a.precache_sound("cin/tsr1/c1d1.adp")
    a.sound("p", "misc/talk.wav", volume=0.9, channel=2)
    a.print_id(0x6417)
    a.music(3)
    a.view_yaw("p", 45.0)
    a.console("say g6b console")
    # cinematic start / end (SPEC-751 1.2, 1.4): freeze byte 1 for one second
    a.cinematic(True)
    a.wait(1.0)
    a.cinematic(False)
    a.print_text("cin done\n")
    # sound variants (which of them reach the client)
    for k, (ent, name, vol, ch) in enumerate((("p", "misc/talk.wav", None, None), ("p", "cin/tsr1/c1d1.adp", 0.9, 0),
                                              ("p", "cin/tsr1/c1d1.adp", 0.9, 2), ("e", "misc/talk.wav", 0.9, 0))):
        a.wait(0.5)
        a.print_text("snd %d\n" % k)
        a.sound(ent, name, volume=vol, channel=ch)
    # V5: fn8(2, 15) over 300 draws (30 per frame, inside the 1000-instruction budget)
    a.assign(lambda: a.pint(100), lambda: v("mn"))
    a.assign(lambda: a.pint(-100), lambda: v("mx"))
    a.assign(lambda: a.pint(0), lambda: v("i"))
    a.assign(lambda: a.pint(0), lambda: v("j"))
    a.label("l5")
    a.assign(lambda: (a.pint(2), a.pint(15), a.pfn(8)), lambda: v("x"))
    a.if_not(lambda: v("x"), lambda: v("mn"), LT, "l5a")
    a.assign(lambda: v("x"), lambda: v("mn"))
    a.label("l5a")
    a.if_not(lambda: v("x"), lambda: v("mx"), GT, "l5b")
    a.assign(lambda: v("x"), lambda: v("mx"))
    a.label("l5b")
    a.add_assign(lambda: a.pint(1), lambda: v("i"))
    a.add_assign(lambda: a.pint(1), lambda: v("j"))
    a.if_not(lambda: v("j"), lambda: a.pint(30), GE, "l5c")
    a.assign(lambda: a.pint(0), lambda: v("j"))
    a.wait(0.1)
    a.label("l5c")
    a.if_not(lambda: v("i"), lambda: a.pint(300), GE, "l5")
    for k in range(0, 17):
        a.if_not(lambda: v("mn"), lambda k=k: a.pint(k), EQ, "mn%d" % k)
        a.print_text("V5 min %d\n" % k)
        a.label("mn%d" % k)
        a.if_not(lambda: v("mx"), lambda k=k: a.pint(k), EQ, "mx%d" % k)
        a.print_text("V5 max %d\n" % k)
        a.label("mx%d" % k)
    # V4: a use while the script waits is consumed; after SUSPEND it continues; after EXIT nothing
    a.spawn("sub", [("classname", "script_runner"), ("script", "tsr1/g6bsub"), ("targetname", "g6bsub")])
    a.use("sub")
    a.wait(0.5)
    a.use("sub")
    a.wait(2.5)
    a.use("sub")
    a.wait(1.0)
    a.use("sub")
    a.wait(1.0)
    # SPEC-AMEND-003 2e: a second timed MOVE while the first runs; the first done event stops the mover at the
    # second destination (A at +64 speed 64: N = 11; after 0.5 s a second +64: the first done event at 1.1 s)
    a.move("e", (0, 0, 64), speed=64.0, sig="sig3")
    a.wait(0.5)
    a.move("e", (0, 0, 64), speed=64.0, sig="sig3")
    a.waitsignals(["sig3"], clear=False)
    a.print_text("2e first done\n")
    a.if_not(lambda: v("sig3"), lambda: a.pint(2), EQ, "e2")
    a.print_text("2e sig 2\n")
    a.label("e2")
    a.wait(1.0)
    a.print_text("G6B end\n")
    a.exit()
    return a.build()


def runaway_script():
    a = Asm()
    a.local("x", T_INT)
    a.print_text("V1 start\n")
    a.label("l")
    a.add_assign(lambda: a.pint(1), lambda: a.pvar("x"))
    a.goto("l")
    return a.build()


def spawnwall_script():
    """SPEC-AMEND-005 G6b.7: built-in 5 reads every value with the key's own type. Two walls of tsr1 model *31: the
    first with `origin` pushed as a vector constant at the player start (sent and drawn there), the second with
    `origin` pushed as a string (read as a vector it becomes (0 0 0): never sent near the player start)."""
    a = Asm()
    a.local("e", T_ENTITY)
    a.local("f", T_ENTITY)
    a.print_text("G6B start\n")

    def spawn_typed(var, keys):
        for k, push in reversed(keys):
            push()
            a.pstr(k)
        a.pfn(5, len(keys)); a.pvar(var); a.u8(0x07)

    spawn_typed("e", [("classname", lambda: a.pstr("func_wall")), ("model", lambda: a.pstr("*31")),
                      ("origin", lambda: a.pvec(-576, 1000, 60))])
    a.print_text("spawned vector\n")
    spawn_typed("f", [("classname", lambda: a.pstr("func_wall")), ("model", lambda: a.pstr("*31")),
                      ("origin", lambda: a.pstr("-576 1040 60"))])
    a.print_text("spawned string\n")
    a.wait(5.0)
    a.print_text("G6B end\n")
    a.exit()
    return a.build()


def main(argv):
    kind, out = argv[0], Path(argv[1])
    d = out / "ds" / "tsr1"
    d.mkdir(parents=True, exist_ok=True)
    if kind == "main":
        (d / "tsr1pre.os").write_bytes(main_script())
        (d / "g6bsub.os").write_bytes(sub_script())
    elif kind == "runaway":
        (d / "tsr1pre.os").write_bytes(runaway_script())
    elif kind == "spawnwall":
        (d / "tsr1pre.os").write_bytes(spawnwall_script())
    else:
        raise SystemExit("unknown kind " + kind)
    print("wrote", d)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
