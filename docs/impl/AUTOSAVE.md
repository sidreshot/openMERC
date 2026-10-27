# AUTOSAVE - timed autosave (om_autosave_interval)

Owner request t95u: an autosave on a timer, 1 to 5 minutes, set from the menu. The menu control is the menu
stream's (gameplay page, `menu-scratch` bfd6a7b: Off / 1..5 min, bound to `om_autosave_interval`); this branch adds
the cvar, the save logic and the docs. Not in the original game (`docs/DEVIATIONS.md` "Timed autosave").

## Behaviour
- `om_autosave_interval` (archived): minutes of **game time**, `0` off, clamped to 0..5, default `3`
  (a death costs at most a few minutes of replay, while a save - about 1.5 MB on tsr1 - stays rare).
  Fractions work from the console (the X1 load test uses 0.5); the menu offers whole minutes.
- Game time = server frames the game module actually ran: paused time, menus, the console and loading do not
  count. The timer restarts at every level start, after a load and after any save (manual, level-start `auto`,
  or timed).
- When the time is up, the save waits for a **quiet moment**: single-player campaign (not deathmatch, coop,
  a demo or `maxclients > 1`), player spawned and alive, `SOF_PM_NORMAL`, on the ground, no cinematic
  (`sv_jumpcinematic` too), not paused, the client drawing the game (no menu, console or loading plaque in the
  last 250 ms), standing on walkable solid ground (the player box traced 4 units down hits a floor with normal
  z >= 0.7 and does not start in solid), and **3 s of all that** without damage taken, a fire button or a
  stuck-in-solid placement of the movement code (`EmergencySnapPosition()` / `Bad InitialSnapPosition`, counted
  in `SVPM_DevPrint`; pmove runs in the engine in both modes). Level changes and loads reset
  the timer (spawn count), so a save never runs during either. Intermission / mission-end screens freeze the
  player (pm_type), so they are covered by the same check.
- Slots: `autotime1` and `autotime2`, the empty one first, then the older one (file mtime). Manual saves and the
  level-start `auto` save are never touched; `save autotime1` is refused ("reserved for the timed autosave").
  Two slots so that an autosave taken just before an unavoidable death still leaves the earlier one.
- Load menu: the slots are listed with their date and time (COMM chunk) and "Autosave" / "Автосохранение" in
  place of the skill line; the save box hides them.
- Notice: one console notify line "Autosave" / "Автосохранение" (om_language through `SP_RussianActive`,
  CP1251 in the code, so no string package of the menu stream is touched).
- It does **not** call `ge->SaveAllowed`: that is the manual-save limit of the harder skills (`sk_saving`); it
  counts one save down and prints "N saves left". Drawback: an autosave is a free save on those skills; set
  `om_autosave_interval 0` to keep the limit strict.
- `om_autosave_debug 1`: per save "om_autosave: <slot> on <map> at (x y z), write N ms, thumbnail N ms, waited
  N s for a quiet moment", and every 10 s of waiting the reason ("in the air", "settling ...", "dead", ...).
  Without it the line is a developer message unless the write took more than 50 ms.

## Both modes
The save path is the engine's (`common/sof_save.c`, `server/sv_save.c`); the game module only writes its chunks
(WriteGame / WriteLevel) as for a console `save`. The same code runs with the original modules (mode A) and
openMERC's (mode B). Mode A finding: a save taken on the first frame after tsr1's intro cinematic (player back on
the ground) crashed the original `gamex86.dll` in RunFrame on load (2 of 2); with the 3 s steady state after
cinematic / air the same test loads (`as-a-ltsr1b`). Manual saves at that moment probably do the same (not
part of this change).

## Stutter
The write runs between two server frames like a console `save`; the 128x128 thumbnail is read from the client's
next drawn frame (renderer slot 0xd4) one frame earlier, so the two costs never fall into one frame. If no frame
comes within 500 ms the level picture `maps/<map>.raw` is used. Measured on XEONST (640x480 window, release):
write 7-15 ms, thumbnail 10-16 ms, i.e. at most ~16 ms extra in a frame (limit 50 ms). The thumbnail is a full
window `glReadPixels`, so it grows with the window size (not measured above 640x480).

## Files
| File | What |
|---|---|
| `engine/src/server/sv_save.c` | cvars, timer, quiet-moment rules, slot write and notice (`SV_AutoSaveGameFrame`, `SV_AutoSaveFrame`) |
| `engine/src/server/sv_main.c` | calls: after the game frame ran, and at the end of `SV_Frame` |
| `engine/src/common/sof_save.c` | `SG_TimedAutoSave` (slot choice, thumbnail, write, time), save counter, label, client frame hook, refusal of `save autotime*` |
| `engine/src/client/cl_sofscreen.c` | `SG_ClientEndFrame` before the buffer swap (in-game flag, thumbnail grab) |
| `engine/src/client/menu/m_data.c` | load/save box: label, slots hidden in the save box |
| `engine/src/client/cl_screen.c` | 3915db5 (integration, base of this branch): a load no longer stays paused |
| `engine/src/server/sv_pmove.c` | `sv_pmove_snaps`: stuck-in-solid placements, for the footing rule |
| `engine/src/server/sv_main.c` | `SV_ShiftClientTimes` (late-save load fix) and the developer drop line |
| `data/base/menus/m_scale.rmf`, strings | the gameplay page row (menu stream bfd6a7b), shown again |
| `tests/X1/x1_autosave.cfg`, `x1_autosave_load.cfg` | X1 sessions: ~3.5 min play with interval 1, and load-back with interval 0.5 |

## Tests (2026-10-27, XEONST, two own test copies of the game, mode A and mode B)
Run: `xrun.ps1 -Script tests\X1\x1_autosave.cfg -Map <map> -Extra '+set cheats 1' -Timeout 260` and
`... x1_autosave_load.cfg -Timeout 200` (`-ExeSub om` for mode A).

| Run | Result |
|---|---|
| B tsr1 play (interval 1) | 3 autosaves 60 s apart, autotime1 -> autotime2 -> autotime1, write 8-15 ms, thumbnail 11-12 ms, om_savecheck 0 bad, "Saves left : 5" before and after |
| B kos1 play | 3 autosaves, write 8-13 ms, thumbnail 11-12 ms |
| A tsr1 play | 3 autosaves, write 8-9 ms, thumbnail 11-16 ms |
| A kos1 play | 3 autosaves, write 7-8 ms; one deferred 3 s after damage (health 40) |
| B/A tsr1, kos1 load-back | both slots load, position back to the saved one after walking away (e.g. tsr1 1187 -> 976), `paused` 0, walking works, screenshots taken |
| A tsr1 interval 1, 3.5 min (`as-a-play`) | autosaves 15:08:28, 15:09:28, 15:10:28 (autotime1, autotime2, autotime1 again); COMM time = file time; 0 bad chunks; "Saves left : 5" before and after |
| A load of those files in a fresh process | autotime2 loads; autotime1 (taken > 3 min into the level) did not - fixed in 70a073d, see below |
| intro cinematic | tsr1: first save waits through the intro (38-42 s) |
| dead | kos1 sessions where the player died: no save after death |

Thumbnails checked by eye: real game frames (squeezed to 128x128 like the original's).

## Open
- Not tested by hand in the real menu (load box look with the menu stream's new menus; this branch changes the
  current `LB_List`).
- The 250 ms "client drew the game" check is a heuristic; a listen server without a local client is excluded by
  `maxclients > 1` anyway.
- Slot choice uses the file mtime (1 s resolution). Two timed saves are always at least 3 s apart (the steady state), so the rotation holds.

## Saves made more than 3 min into a level did not load (fixed, 70a073d)
Found while testing the autosave; it hit manual saves as well. Loading restores the level frame number
(`SVH_SetFrameNumber`), so `sv.time` restarts at the saved level time while `SV_SpawnServer` reset `svs.realtime`
to 0 and the reconnecting local client got `lastmessage` = that small value. The first server frame's lowclamp
moved `svs.realtime` up to `sv.time - 100`, `SV_CheckTimeouts` saw a client silent for more than `timeout`
(180 s) and dropped it (reason TIMEDOUT, now printed with `developer 1`: "SV_DropClient: client 0 (state 2)
reason 1035") before its `new` was read - the client stayed on "client_connect". Position, stairs and
EmergencySnapPosition were a coincidence of the test walk; level time decided it. Fix: `SV_ShiftClientTimes`
moves the clients' `lastmessage` by the same jump.

| Run (fresh process per load, harness copies the save in) | Before | After |
|---|---|---|
| B tsr1, 10 autosaves 15 s apart (`as-gen-B`) | s01..s07 load, s08..s10 (level time > ~180 s) stall, dropped TIMEDOUT | s08 3/3, s09, s10, s01, s05 load, position = saved (e.g. -98 344 for -98.6 344.1) |
| A tsr1, 5 autosaves (`as-gen-A`, a05 at ~193 s) | (A autotime1 at ~200 s stalled 3/3 earlier) | a05 3/3, a04, a01 load, position = saved |
