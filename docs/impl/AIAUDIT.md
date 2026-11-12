# AIAUDIT - does every enemy react to the player?

A test aid, not in the original: the cheat command `omaudit` (`game/src/ai/ai_audit.cpp`) and the harness
`tests/AIAUDIT/` (Python 3, standard library only). Ports: AIAUDIT base 29720 (listen server 29720, client 29721).
It changes no rule of the game: nothing runs unless `omaudit` is given with `cheats 1`.

## What the command does

`omaudit` lists every living enemy of the loaded single-player level that has a body (helicopters, tanks and
snowcats are left out). For each one in turn it

1. looks for a floor spot from which the enemy's eyes see the player's: in front of it first (220 units, then
   nearer), then to its sides, then behind it;
2. puts the player there, facing it, invulnerable and in noclip (so the visits fire no walk-in trigger), and wakes
   the enemy as its trigger would (`use`, also for the blind, deaf and dormant spawnflags);
3. waits up to 12 seconds of game time and prints one console line `OMAUDIT <map> <result> ...`;
4. removes the enemy and goes on to the next.

A cut scene holds the clock (alerted characters stand still in one on purpose); a cut scene that does not end
within 15 s is ended. A level gets 1200 s of game time at most. While the check runs the level does not end
(`BeginIntermission` and the script commands `map`, `gamemap`, `intermission` do nothing); a level that still ends
itself is reported with an `OMNOTE` line. **The level is used up afterwards:** the enemies are gone and the player
is left invulnerable in noclip. Load the map again to play it.

| Result | Meaning |
|---|---|
| `ATTACK` | its current action is an attack |
| `CHASE` | alerted and more than 48 units from where it stood |
| `COWER` | plays an afraid or cower animation (unarmed civilians) |
| `SILENT` | armed, saw nothing or did nothing for 12 s - **the result to look at** |
| `BEHIND` | did nothing, but the only place to stand was behind it; a noise was made after 4 s. Proves nothing |
| `UNARMED` | did nothing and holds no weapon |
| `NOSPOT` | no place to stand in its sight was found (closed stalls, ledges); not checked |

A silent enemy also gets five `OMWHY` lines: the sight tests of SPEC-701 section 5 one by one (distance, PVS,
eye-to-eye trace and what it hit, view cone, the perception filter), its newest percept of the player, its ground
entity and combat target, the sight mute level, the clear-shot result and blocker of its combat decision, a box
trace 8 units down and which fire, aim and move records its body has. Its last two seconds are traced with
`ai_trace 2` between `OMTRACE <map> ent=<n> on` and `OMTRACE <map> off`.

When a level is done the command prints `OMAUDIT <map> done total= reacted= silent= nospot= gone=` and runs the
command line in the cvar `om_audit_next`, if any (the harness chains the maps with it).

## One map by hand

Start the game with `+set cheats 1`, load a map, let its opening cut scene end, then type `omaudit` in the
console. `timescale 3` makes it three times as fast. The lines are in the console and, with `logfile 2`, in the log
file of the user folder; `python tests/AIAUDIT/audit_report.py <log>` sums them up.

## One map or all 32 with the harness

```
python tests/AIAUDIT/audit_run.py --exe <path>/openMerc --game <folder with base/pak0.pak> --home <new folder> --maps tsr1
python tests/AIAUDIT/audit_run.py --exe <path>/openMerc --game <folder with base/pak0.pak> --home <new folder>
```

Without `--maps` it takes the 32 single-player maps of `tests/X1/xsweep.ps1`. One session of the open stack loads
each map with the cut scenes skipped, waits 6 s of real time, runs `omaudit` at `timescale 3` and goes on to the
next map; the last one quits. `--home` is the session's own user folder and must be new or empty: the console
scripts `au_NN.cfg`, `console.log` and `audit.json` are written there, next to what the engine keeps in a user
folder (`config.cfg`, its log, the material cache, autosaves), and nowhere else; the game data is only read.
The session runs `-portable`, in a window, without sound, gets no input from the desktop and is killed after
`--timeout` seconds (480 for each map by default). `--dry-run` writes the scripts and prints the command line
only; `--make <folder>` writes the scripts only. A session that stops at one map (crash, hang) is continued with
`--maps` and the maps that are left.

## Reading the report

`audit_report.py <log> [--json out.json] [--maps ...] [--traces]` prints

- `run on:` the engine version, platform and architecture, read from the log itself - a result belongs to that
  build and system and to no other;
- the levels started and finished, the ones that never started or did not finish, and the `OMNOTE` lines;
- the results summed over all enemies and for each level, with the median and slowest reaction time;
- `not counted:` silent enemies that are silent by design, relabelled from their `OMWHY` lines and spawnflags:
  `PASSIVE` (bulls), `RIDER` (woken inside its vehicle before its script lets it out), `POSTED` (spawnflag 128,
  never moves, and its shot is blocked by a prop or vehicle), `SCRIPTED` (spawned dormant, blind and deaf: a
  cut-scene actor woken out of order);
- `to look at:` the `SILENT` and `NOSPOT` enemies that are left, by class, action and record, then one by one with
  map, targetname, entity number, spawnflags and position;
- with `--traces`: for each silent enemy the combat range codes, plans and queued actions of its last two seconds.

A `SILENT` line is a question, not a bug report. Each one has to be read in its `OMWHY` lines and trace: an enemy
behind cover, one whose shot a truck blocks, or one running its script is correct. The check is also not repeatable
to the single enemy: where trains stand, who is crouched and which spot is found differ between sessions, so single
enemies move between `SILENT` and `ATTACK`. Compare totals and look at the ones that are silent every time.
Levels whose enemies arrive later (spawners, scripts) show few or none: only those alive when the command is
given are checked.

## State (2026-11-12)

- Built with the game module on macOS arm64 (Apple clang 21); the unit tests are unchanged and pass. Windows and
  Linux: built by CI only.
- One session on macOS 27.2 arm64, `ref_gl3` on Apple's OpenGL 4.1, started by
  `audit_run.py --maps tsr1`: the map loaded, `omaudit` checked 61 of 62 enemies (one was gone before its turn),
  finished and quit; exit code 0, 57 s in all. ATTACK 32, CHASE 16, COWER 2, BEHIND 2, NOSPOT 6, SILENT 3; median
  reaction 1.2 s. The three silent ones are the riders `rtskinhead1` to `rtskinhead3`, standing on
  `misc_generic_train`: they have the player as combat target, no ground entity, and only play an idle record.
  One run, and the check woke them before their script would; whether they also stand idle in normal play has to be
  shown separately.
- `audit_report.py` was also checked against a console log of the same `OMAUDIT` / `OMWHY` / `OMTRACE` lines from a
  0.2.2 build with other changes (32 levels started, 31 finished, 1558 enemies). That log is not a result for this
  tree.
- **Not run yet:** more than one map in a session (the chain through `om_audit_next` ran only its last step,
  `quit`), the other 31 maps, anything on Windows or Linux.
