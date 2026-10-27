# SPDX-License-Identifier: GPL-2.0-or-later
# ("~", not "|": python may be a .cmd shim and cmd would pipe)
# Free-form console sequence for run_gfx.ps1: every argument is "cmd;cmd~seconds" (the delay before the next step).
# usage: python gen_seq.py <outdir> "map kos1~14" "+attack~0.3" "screenshot a~1" ...
# Implements: GFX-modern.md measurement (test harness, no game code)
import os
import sys


def main():
    out, steps = sys.argv[1], [a.rpartition("~") for a in sys.argv[2:]]
    os.makedirs(out, exist_ok=True)
    steps = [(c, float(d or 0.5)) for c, _, d in steps] + [("quit", 0)]
    with open(os.path.join(out, "omtest.cfg"), "w", newline="\r\n") as f:
        f.write("set sv_jumpcinematic 1\nset cl_subtitles 0\nset developer 0\nset con_notifytime 0\nexec s0.cfg\n")
    for i, (cmds, delay) in enumerate(steps):
        with open(os.path.join(out, "s%d.cfg" % i), "w", newline="\r\n") as f:
            f.write("\n".join(c for c in cmds.split(";") if c) + "\n")
            if i + 1 < len(steps):
                f.write("om_delay %g exec s%d.cfg\n" % (delay, i + 1))


if __name__ == "__main__":
    main()
