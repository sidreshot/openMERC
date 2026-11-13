#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Effect gallery (docs/EFFECTS.md B1): every group of the effects list played at fixed spots of a few maps, a shot
of the same frozen moment in each look (new, original) and the GPU time of that frame in each look.

    fxgal.py --exe build-linux-x86_64-Release/release/openMerc --game ~/Games/openMERC --out /tmp/fxgal
             [--spots jpn1,kos1] [--groups E1,E3] [--looks new,orig] [--no-gpu] [--size 1600x900]
             [--spec tools/fxgal/gallery.json] [--extra "+set r_om_preset 2"] [--dry-run]

Per map the script writes a console script (<out>/run/<map>/fxgal.cfg), starts the engine on the map with a fresh
user folder, collects the shots in <out>/shots and writes <out>/index.html and <out>/results.json.

How a shot is taken (the engine side is om_fxplay, om_fxclear and the pause):
  the view is set (omsetpos, the player flies and is unseen and unhurt), the effect is started (om_fxplay ... with
  its place: on the floor ahead, where the view meets a wall, in the air; "loop" restarts it as a level's effect slot
  does) or a weapon is fired; after <age> seconds of game time the game is paused. The pause stops the effects, the
  dynamic lights and the renderer's clock: each look is set in turn on the same frozen frame, shot after a settling
  second, and its GPU time read from r_om_gputime 1 (OMGPU lines, the mean of 100 frames each). So the shots of one
  item differ by the look only. An item whose new look changes what is simulated (more particles, other timing)
  cannot be compared on one frozen frame; it needs a shot of each look from a start of its own (not done yet).
"""

import argparse
import html
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent

# the interface off for the shots (as om_freecam does; the pause banner too)
HUD_OFF = "scr_showpause 0; scr_interface 0; crosshair 0; cl_gun 0; cl_subtitles 0; con_notifytime 0; cl_showfps 0"

# The effects run at 60 frames a second: their clock steps 1/60 s per frame above 60 fps (SPEC-400 Timebase, as
# 1.07f), so at 1000 fps an explosion is over in a few hundredths of a second; at 60 it lasts as designed. The GPU
# time is read on the paused frame, where nothing moves, with the frame rate free.
PLAY_FPS = 60
MEASURE_FPS = 999
SETTLE = 1.0     # wall-clock seconds after a change of look (exposure, temporal filters)
SHOT_GAP = 0.4   # after a screenshot, before anything changes (the shot is written at the end of a frame)
GPU_TIME = 2.2   # wall-clock seconds of r_om_gputime 1 per look


def cvars(d):
    # "set": a renderer cvar is created when its effect is first drawn; before that, "r_om_x 0" is an unknown command
    return "; ".join("set %s %s" % (k, v) for k, v in d.items())


class Chain:
    """A run of console steps; each step ends by scheduling the next one (om_delay: wall clock; om_after c: the
    client's time, the clock of the effects, which stands still while paused). Aliases keep the ';' of a step out of
    the line that schedules it."""

    def __init__(self):
        self.steps = []

    def add(self, cmds, wait, game_time=False):
        self.steps.append((cmds, wait, game_time))

    def script(self):
        out = []
        for i, (cmds, wait, game) in enumerate(self.steps):
            nxt = "z%d" % (i + 1) if i + 1 < len(self.steps) else "quit"
            sched = ("om_after c%.3f %s" % (wait, nxt)) if game else ("om_delay %.3f %s" % (wait, nxt))
            body = (cmds + "; " + sched) if cmds else sched
            if '"' in body or len(body) > 1000:
                raise ValueError("step %d cannot be an alias: %r" % (i, body))
            out.append('alias z%d "%s"' % (i, body))
        return out


def item_key(spot, group, idx):
    return "%s__%s__%02d" % (spot["id"], group["id"], idx)


def item_label(item):
    if "fx" in item:
        s = item["fx"]
        extra = [item.get("place", "air")]
        if item.get("loop"):
            extra.append("loop")
        extra.append("%.2f s" % item.get("age", 0.3))
        return "%s (%s)" % (s, ", ".join(extra))
    if "shoot" in item:
        return "weapon %d, %.2f s after the trigger" % (item["shoot"], item.get("age", 0.1))
    if "decal" in item:
        return "mark %s" % item["decal"]
    return "?"


# the cheat kits replace the whole inventory: "elbow" has the light weapons, "bigelbow" the heavy ones
LIGHT_KIT = {1, 2, 3, 4, 5, 7}   # knife, 9 mm, .44, shotgun, sniper rifle, assault rifle


def frozen_looks(chain, key, looks, spec_looks, gpu):
    """the game is paused: each look on the same frame, a shot, its GPU time; then the first look again"""
    for name in looks:
        chain.add(cvars(spec_looks[name]), SETTLE)
        chain.add("echo FXGAL SHOT %s %s; screenshot %s__%s" % (key, name, key, name), SHOT_GAP)
        if gpu:
            chain.add("set vid_maxfps %d; echo FXGAL GPU %s %s; r_om_gputime 1" % (MEASURE_FPS, key, name), GPU_TIME)
            chain.add("r_om_gputime 0; echo FXGAL END; set vid_maxfps %d" % PLAY_FPS, 0.1)
    chain.add(cvars(spec_looks[looks[0]]) + "; om_fxclear", 0.3)
    chain.add("paused 0", 0.5)


def map_chain(spec, spots, groups, looks, gpu):
    chain = Chain()
    first_look = spec["looks"][looks[0]]
    chain.add("heretic; ninja; phantom; " + HUD_OFF, 1.0)

    for spot in spots:
        # the place with no effect: what every cost below is measured against
        key = "%s__base" % spot["id"]
        chain.add("echo FXGAL ITEM %s; om_fxclear; %s; omsetpos %s" % (key, cvars(first_look), spot["view"]), 1.5)
        chain.add("paused 1", 0.2)
        frozen_looks(chain, key, looks, spec["looks"], gpu)

        for group in groups:
            for idx, item in enumerate(group["items"]):
                key = item_key(spot, group, idx)
                age = float(item.get("age", 0.3))
                if "fx" in item:
                    place = item.get("place", "air")
                    view = spot["wall"] if place == "wall" else spot["view"]
                    chain.add("echo FXGAL ITEM %s; om_fxclear; omsetpos %s" % (key, view), 0.6)
                    chain.add("om_fxplay %s %g %g %s%s" % (item["fx"], item.get("dist", 150), item.get("up", 0), place,
                                                           " loop" if item.get("loop") else ""), age, game_time=True)
                    chain.add("paused 1", 0.2)
                    frozen_looks(chain, key, looks, spec["looks"], gpu)
                elif "shoot" in item:
                    # the weapon in view; the trigger held until after the shots (a flame stream, a burst)
                    attack = "+altattack" if item.get("alt") else "+attack"
                    kit = "elbow" if item["shoot"] in LIGHT_KIT else "bigelbow"
                    chain.add("echo FXGAL ITEM %s; om_fxclear; omsetpos %s; %s" % (key, spot["wall"], kit), 0.5)
                    chain.add("cl_gun 1; weaponselect %d" % item["shoot"], 2.0)
                    chain.add(attack, age, game_time=True)
                    chain.add("paused 1", 0.2)
                    frozen_looks(chain, key, looks, spec["looks"], gpu)
                    chain.add("-" + attack[1:] + "; cl_gun 0", 1.0)
                elif "decal" in item:
                    chain.add("echo FXGAL ITEM %s; om_fxclear; omsetpos %s" % (key, spot["wall"]), 0.6)
                    chain.add("om_decal %s %g" % (item["decal"], item.get("size", 32)), 0.3)
                    chain.add("paused 1", 0.2)
                    frozen_looks(chain, key, looks, spec["looks"], gpu)
    chain.add("echo FXGAL DONE", 0.5)
    return chain


def parse_log(text):
    """FXGAL markers and OMGPU lines -> {key: {look: [ms...]}}, items that reported a problem"""
    gpu, problems = {}, {}
    cur_gpu, cur_item = None, None
    for line in text.splitlines():
        m = re.match(r"FXGAL (\w+)\s*(\S*)\s*(\S*)", line)
        if m:
            kind = m.group(1)
            if kind == "ITEM":
                cur_item = m.group(2)
            elif kind == "GPU":
                cur_gpu = (m.group(2), m.group(3))
            else:
                cur_gpu = None
            continue
        m = re.match(r"OMGPU ([0-9.]+)", line)
        if m and cur_gpu:
            gpu.setdefault(cur_gpu[0], {}).setdefault(cur_gpu[1], []).append(float(m.group(1)))
            continue
        if cur_item and re.search(r"om_fxplay: |om_decal: |Unknown command", line):
            problems.setdefault(cur_item, []).append(line.strip())
    return gpu, problems


def gpu_ms(values):
    # the first value can hold the frames before the look settled
    v = values[1:] if len(values) > 2 else values
    return statistics.median(v) if v else None


CFG_CHUNK = 20000  # bytes per script file: exec puts a whole file into the 32 KB command buffer (CBUF_SIZE)


def write_scripts(user, mapname, chain):
    """fxgal.cfg, fxgal1.cfg ...: each defines its share of the aliases and runs the next file; the last one starts the
    chain. One file for the whole gallery (73 KB) overflowed the command buffer and nothing ran."""
    head = ["// written by tools/fxgal/fxgal.py: the effect gallery on %s" % mapname, "set cheats 1",
            "set sv_jumpcinematic 1"]
    files, cur, size = [], list(head), 0
    for line in chain.script():
        if size + len(line) + 1 > CFG_CHUNK:
            files.append(cur)
            cur, size = [], 0
        cur.append(line)
        size += len(line) + 1
    files.append(cur)
    names = ["fxgal.cfg"] + ["fxgal%d.cfg" % i for i in range(1, len(files))]
    for i, lines in enumerate(files):
        tail = ["exec %s" % names[i + 1]] if i + 1 < len(files) else ["om_after r3 z0"]
        (user / names[i]).write_text("\n".join(lines + tail) + "\n")


def fresh_user(args, user):
    """a new user folder; the settings go into its config.cfg (run at start, before the video starts): the command
    line holds few arguments (MAX_NUM_ARGVS). Returns the engine's command line up to the map."""
    if user.exists():
        shutil.rmtree(user)
    (user / "lightmaps").mkdir(parents=True)
    w, h = args.size.split("x")
    config = {"vid_fullscreen": "0", "r_mode": "-1", "r_customwidth": w, "r_customheight": h,
              "r_om_preset_auto": "1", "r_om_preset": str(args.preset), "vid_maxfps": str(PLAY_FPS), "r_vsync": "0",
              "s_volume": "0", "s_musicvolume": "0", "om_noimport": "1", "sv_jumpcinematic": "1"}
    (user / "config.cfg").write_text("".join('set %s "%s"\n' % kv for kv in config.items()))
    cmd = [str(Path(args.exe).resolve()), "-portable",
           "+set", "basedir", str(Path(args.game).expanduser().resolve()),
           "+set", "user", str(user), "+set", "cheats", "1"]
    return cmd + (args.extra.split() if args.extra else [])


def warm_lightmaps(args, mapname, out, cache):
    """The engine re-bakes a map's lighting (omlight) at its first load into the user folder and uses it from the next
    load: a fresh user folder would show the map's own lighting and no surface directions (r_om_deluxe). One load per
    map bakes it into the gallery's cache (<out>/lightmaps, kept between runs) before the shots."""
    if (cache / (mapname + ".oml")).exists():
        return
    user = out / "run" / mapname / "warm"
    cmd = fresh_user(args, user) + ["+map", mapname]
    print("== %s: baking the lighting first (once, kept in %s)" % (mapname, cache), flush=True)
    if args.dry_run:
        return
    log = user / "lightmaps" / (mapname + ".log")
    with open(user.parent / "warm.log", "w") as out_log:
        proc = subprocess.Popen(cmd, cwd=str(Path(args.exe).resolve().parent), stdout=out_log,
                                stderr=subprocess.STDOUT)
        t0 = time.time()
        while time.time() - t0 < 400 and proc.poll() is None:
            time.sleep(2)
            if log.exists() and "wrote " in log.read_text(errors="replace"):
                time.sleep(1)
                break
        proc.terminate()
        try:
            proc.wait(10)
        except subprocess.TimeoutExpired:
            proc.kill()
    if (user / "lightmaps" / (mapname + ".oml")).exists():
        cache.mkdir(parents=True, exist_ok=True)
        for ext in (".oml", ".log"):
            shutil.copy2(user / "lightmaps" / (mapname + ext), cache / (mapname + ext))
        print("   baked in %d s" % (time.time() - t0), flush=True)
    else:
        print("   no lighting baked (see %s): the map's own lighting is used" % (user.parent / "warm.log"), flush=True)
    shutil.rmtree(user, ignore_errors=True)


def run_map(args, spec, mapname, spots, groups, looks, out):
    run_dir = out / "run" / mapname
    user = run_dir / "user"
    cache = Path(args.lightmaps).expanduser().resolve() if args.lightmaps else out / "lightmaps"
    warm_lightmaps(args, mapname, out, cache)
    cmd = fresh_user(args, user)
    for ext in (".oml", ".log"):
        if (cache / (mapname + ext)).exists():
            shutil.copy2(cache / (mapname + ext), user / "lightmaps" / (mapname + ext))
    chain = map_chain(spec, spots, groups, looks, not args.no_gpu)
    write_scripts(user, mapname, chain)
    cmd += ["+map", mapname, "+exec", "fxgal.cfg"]
    est = sum(s[1] for s in chain.steps) + 25
    print("== %s: %d spot(s), %d step(s), about %d s" % (mapname, len(spots), len(chain.steps), est), flush=True)
    if args.dry_run:
        print("   " + " ".join(cmd))
        return "", {}
    t0 = time.time()
    with open(run_dir / "console.log", "w") as log:
        try:
            subprocess.run(cmd, cwd=str(Path(args.exe).resolve().parent), stdout=log, stderr=subprocess.STDOUT,
                           timeout=est * 2 + 120, check=False)
        except subprocess.TimeoutExpired:
            print("   timed out", flush=True)
    text = (run_dir / "console.log").read_text(errors="replace")
    # which lighting the shots have (gl3_relight.c): "uses the re-baked lightmaps", "fits its lighting too loosely" (the
    # map's own), "baking ... in the background" (the map's own, not baked yet), "out of date", "broken"
    lit = [l.strip() for l in text.splitlines() if l.startswith("relight: ") and mapname in l]
    print("   " + (lit[-1] if lit else "relight: no line (the map's own lighting)"), flush=True)
    if "Cbuf_AddText: overflow" in text:
        print("   the command buffer overflowed (a script file too big)", flush=True)
    if "FXGAL DONE" not in text:
        print("   the run did not reach its end (see %s)" % (run_dir / "console.log"), flush=True)
    shots = {}
    for f in sorted((user / "scrnshot").glob("*.jpg")) if (user / "scrnshot").exists() else []:
        dest = out / "shots" / f.name
        shutil.move(str(f), dest)
        shots[f.stem] = "shots/" + f.name
    print("   %d shot(s) in %d s" % (len(shots), time.time() - t0), flush=True)
    return text, shots


def write_html(out, spec, spots, groups, looks, results):
    css = """
:root{--bg:#f7f7f5;--fg:#1c1c1c;--muted:#666;--line:#ddd;--card:#fff}
@media (prefers-color-scheme:dark){:root{--bg:#161616;--fg:#e8e8e8;--muted:#999;--line:#333;--card:#202020}}
body{background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif;margin:16px}
h1{font-size:20px}h2{font-size:17px;margin-top:28px}
table{border-collapse:collapse}td,th{border:1px solid var(--line);padding:6px;vertical-align:top;background:var(--card)}
th{text-align:left;font-weight:600}.cell{position:relative;width:320px}
.cell img{width:320px;display:block}.cell .alt{position:absolute;top:0;left:0;opacity:0}
.cell:hover .alt{opacity:1}.cap{color:var(--muted);font-size:12px}.bad{color:#c33}
.wrap{overflow-x:auto}"""
    h = ["<!doctype html><meta charset=utf-8><title>Effect gallery</title><style>%s</style>" % css,
         "<h1>Effect gallery</h1>",
         "<p class=cap>Each picture is the <b>%s</b> look; hover it for <b>%s</b> (the same frozen frame). "
         "GPU: the frame's GPU time in ms, and in brackets what the effect adds to the frame of the place without it."
         " Written %s.</p>" % (html.escape(looks[0]), html.escape(looks[1]) if len(looks) > 1 else "-",
                                time.strftime("%Y-%m-%d %H:%M"))]

    def cell(key):
        r = results.get(key, {})
        imgs = [r.get("shots", {}).get(l) for l in looks]
        parts = ["<div class=cell>"]
        if imgs[0]:
            parts.append("<a href='%s'><img loading=lazy src='%s' alt=''></a>" % (imgs[0], imgs[0]))
        if len(imgs) > 1 and imgs[1]:
            parts.append("<img class=alt loading=lazy src='%s' alt=''>" % imgs[1])
        parts.append("</div>")
        cap = []
        for l in looks:
            ms = r.get("gpu", {}).get(l)
            if ms is not None:
                base = results.get(key.split("__")[0] + "__base", {}).get("gpu", {}).get(l)
                cap.append("%s %.2f%s" % (l, ms, (" (%+.2f)" % (ms - base)) if base is not None else ""))
        if cap:
            parts.append("<div class=cap>GPU %s</div>" % " &middot; ".join(cap))
        for p in r.get("problems", []):
            parts.append("<div class='cap bad'>%s</div>" % html.escape(p))
        return "".join(parts)

    head = "<tr><th>Item</th>" + "".join("<th>%s<div class=cap>%s</div></th>"
                                         % (html.escape(s["id"]), html.escape(s["what"])) for s in spots) + "</tr>"
    h.append("<h2>The places without an effect</h2><div class=wrap><table>%s<tr><td>-</td>%s</tr></table></div>"
             % (head, "".join("<td>%s</td>" % cell("%s__base" % s["id"]) for s in spots)))
    for g in groups:
        h.append("<h2>%s. %s</h2><div class=wrap><table>%s" % (html.escape(g["id"]), html.escape(g["name"]), head))
        for idx, item in enumerate(g["items"]):
            label = html.escape(item_label(item))
            if item.get("note"):
                label += "<div class=cap>%s</div>" % html.escape(item["note"])
            h.append("<tr><td>%s</td>%s</tr>" % (label, "".join("<td>%s</td>" % cell(item_key(s, g, idx))
                                                                  for s in spots)))
        h.append("</table></div>")
    (out / "index.html").write_text("\n".join(h) + "\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", required=True, help="the engine (openMerc)")
    ap.add_argument("--game", required=True, help="the folder with the game data (base/pak0.pak)")
    ap.add_argument("--out", required=True, help="where the shots, index.html and results.json go")
    ap.add_argument("--spec", default=str(HERE / "gallery.json"))
    ap.add_argument("--spots", default="", help="spot ids, comma separated (default all)")
    ap.add_argument("--groups", default="", help="group ids, comma separated (default all)")
    ap.add_argument("--looks", default="new,orig", help="looks of the spec, the first is shown, hover shows the rest")
    ap.add_argument("--no-gpu", action="store_true", help="no GPU time (about half the run time)")
    ap.add_argument("--size", default="1600x900")
    ap.add_argument("--preset", type=int, default=3, help="r_om_preset (3: High)")
    ap.add_argument("--extra", default="", help="more engine arguments")
    ap.add_argument("--lightmaps", default="", help="cache of the baked lighting (default <out>/lightmaps)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    spec = json.loads(Path(args.spec).read_text())
    looks = [l for l in args.looks.split(",") if l]
    for l in looks:
        if l not in spec["looks"]:
            sys.exit("no look %r in %s" % (l, args.spec))
    want_s = set(filter(None, args.spots.split(",")))
    want_g = set(filter(None, args.groups.split(",")))
    spots = [s for s in spec["spots"] if not want_s or s["id"] in want_s]
    groups = [g for g in spec["groups"] if not want_g or g["id"] in want_g]
    if not spots or not groups:
        sys.exit("nothing to play (check --spots / --groups)")

    out = Path(args.out).expanduser().resolve()
    (out / "shots").mkdir(parents=True, exist_ok=True)
    results = {}
    by_map = {}
    for s in spots:
        by_map.setdefault(s["map"], []).append(s)
    for mapname, map_spots in by_map.items():
        text, shots = run_map(args, spec, mapname, map_spots, groups, looks, out)
        gpu, problems = parse_log(text)
        for name, path in shots.items():
            key, _, look = name.rpartition("__")
            results.setdefault(key, {}).setdefault("shots", {})[look] = path
        for key, per_look in gpu.items():
            for look, vals in per_look.items():
                results.setdefault(key, {}).setdefault("gpu", {})[look] = gpu_ms(vals)
        for key, lines in problems.items():
            results.setdefault(key, {})["problems"] = lines
    if args.dry_run:
        return
    (out / "results.json").write_text(json.dumps(results, indent=1, sort_keys=True) + "\n")
    write_html(out, spec, spots, groups, looks, results)
    missing = [k for k, r in results.items() if len(r.get("shots", {})) < len(looks)]
    print("== %s (%d item(s) with a missing shot)" % (out / "index.html", len(missing)))


if __name__ == "__main__":
    main()
