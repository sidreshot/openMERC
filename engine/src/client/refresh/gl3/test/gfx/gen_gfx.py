# SPDX-License-Identifier: GPL-2.0-or-later
# GFX-modern black-box script generator: writes omtest.cfg and step files s<N>.cfg into <outdir>. For every
# map, every configuration is set, the view settles, cl_showfps runs for <secs> seconds between
# "echo OMFPS <map>_<config>" and "echo OMFPS_END", then "screenshot <map>_<config>". The steps are chained
# with om_delay (real time), since "wait" counts packet frames.
# usage: python gen_gfx.py <outdir> <maps comma-separated> <configs comma-separated> [secs] [load secs]
# Implements: GFX-modern.md measurement (test harness, no game code)
import os
import sys

OFF = {"r_om_bloom": 0, "r_om_volfog": 0, "r_om_csm": 0, "r_om_pdlight": 0, "r_om_softparticles": 0,
       "r_om_ppfog": 0, "r_om_overbright": 0, "r_om_post_debug": 0,
       "r_om_csm_debug": 0}
# tuning cvars are archived: every config restores them so a "=cvar:value" override does not leak into later captures
OFF.update({"r_om_volfog_intensity": 0.4, "r_om_volfog_quality": 2, "r_om_volfog_overlaydim": 0.85,
            "r_om_volfog_shafts": 1, "r_om_volfog_noise": 0.25, "r_om_pdlight_test": 0})
DEFAULTS = dict(OFF, r_om_bloom=1, r_om_volfog=1, r_om_csm=1, r_om_pdlight=1)
CONFIGS = {
    "off": OFF,
    "def": DEFAULTS,
    "all": dict(DEFAULTS, r_om_pdlight=1, r_om_softparticles=1, r_om_ppfog=1),
    "bloom": dict(OFF, r_om_bloom=1),
    "emis": dict(OFF, r_om_bloom=1, r_om_post_debug=1),
    "volfog": dict(OFF, r_om_volfog=1),
    "csm": dict(OFF, r_om_csm=1),
    "pdlight": dict(OFF, r_om_pdlight=1),
    "soft": dict(OFF, r_om_softparticles=1),
    "ppfog": dict(OFF, r_om_ppfog=1),
    "overbright": dict(OFF, r_om_overbright=1),
}


def main():
    out, maps, configs = sys.argv[1], sys.argv[2].split(","), sys.argv[3].split(",")
    secs = float(sys.argv[4]) if len(sys.argv) > 4 else 4.0
    load = float(sys.argv[5]) if len(sys.argv) > 5 else 10.0
    os.makedirs(out, exist_ok=True)
    steps = []
    for m in maps:
        # [cmd;cmd!]map[@cmd;cmd]: console commands before and after the load (e.g. deathmatch with bots, pause)
        pre, _, m = m.rpartition("!")
        m, _, post = m.partition("@")
        m, _, alias = m.partition("#")  # map#name: the name of the captures (several views of one map)
        steps.append(([c for c in pre.split(";") if c] + ["map %s" % m], load))
        m = alias or m
        steps.append(([c for c in post.split(";") if c] + ["r_om_csm_info"], 1.0))
        steps.append((["r_om_csm_info layers"], 0.5))
        for c in configs:
            name = c.split("=")[0]
            sets = dict(CONFIGS[name])
            for extra in c.split("=")[1:]:  # config=cvar:value=cvar:value
                k, v = extra.split(":")
                sets[k] = v
                name += "_" + k.replace("r_om_", "") + v.replace(".", "p")
            lines = ["set %s %s" % kv for kv in sets.items()]
            steps.append((lines, 1.5))
            steps.append((["echo OMFPS %s_%s" % (m, name), "set cl_showfps 1", "set r_om_gputime 1"], secs))
            steps.append((["set cl_showfps 0", "set r_om_gputime 0", "echo OMFPS_END", "screenshot %s_%s" % (m, name)], 0.7))
    steps.append((["quit"], 0))
    with open(os.path.join(out, "omtest.cfg"), "w", newline="\r\n") as f:
        f.write("set sv_jumpcinematic 1\nset cl_subtitles 0\nset developer 0\nset con_notifytime 0\nexec s0.cfg\n")
    for i, (lines, delay) in enumerate(steps):
        with open(os.path.join(out, "s%d.cfg" % i), "w", newline="\r\n") as f:
            f.write("\n".join(lines) + "\n")
            if i + 1 < len(steps):
                f.write("om_delay %g exec s%d.cfg\n" % (delay, i + 1))


if __name__ == "__main__":
    main()