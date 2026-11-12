# Free camera (om_freecam)

Not in the original game. One console command for pictures and videos: `om_freecam` turns it on, `om_freecam`
again off (`om_freecam 1` / `om_freecam 0` set it).

- **Client** (`cl_main.c`): the interface goes - `scr_interface`, `crosshair`, `cl_gun`, `cl_subtitles`,
  `con_notifytime`, `cl_showfps` are set to 0 and their values kept; centre prints and captions are not drawn
  (`cl_sofscreen.c`). Off again, the kept values come back. While it is on, `config.cfg` gets the kept values
  (`Com_WriteConfig` asks `CL_FreecamSavedValue`), so quitting during the flight leaves no hidden interface behind. A
  new level (another `cl.servercount`) or a disconnect ends it on the client.
- **Game** (`g_cmds.cpp`, client command `omfreecam [0|1]`): noclip, notarget and god while it is on; off, the
  movement and those two flags of before come back and the player is put back where the flight began (origin and view
  angles). Single player needs no cheats; with `deathmatch` or more than one client the server's `cheats` is
  required, and a refusal sends `om_freecam 0` back (stufftext) so the client shows its interface again. The record
  names its level and ends with it. Commands are ignored during cinematics and the intermission (as all of
  ClientCommand).
- **Saves and level changes**: `WriteGame` / `WriteLevel` (`g_save.cpp`) put the player back as before the flight for
  the time of the write (`Freecam_SaveSwap`), so a save or an autosave made while flying holds the player where the
  flight began, walking, without the camera's god and notarget. `SaveClientData` carries the god flag of before the
  flight to the next level (`Freecam_RealFlags`).

Not part of SPEC parity: the original has `phantom` / `ninja` / `heretic` (cheats only, toggles), which stay as they
were.

## Checked (2026-11-12, Linux, tsr1 from a save, no cheats)

- From z -476: `om_freecam`, 2 s of `+moveup`: z 110 (flight, not a jump); `om_freecam`: z -476 again, same angles.
- A save made at z 110 during the flight loads at z -476.
- During the flight `scr_interface`, `cl_gun`, `crosshair`, `con_notifytime`, `cl_subtitles` are 0; afterwards 1, 1,
  1, 3, 1 again. Screenshots before / during / after: HUD, weapon and crosshair, then none of them, then all back.
- `saveconfig` and quitting with the camera on: `config.cfg` has the values of before.
- An enemy spawned 100 units in front (`gimme m_nyc_mskinhead1 100`, cheats on for that only): without the camera the
  player is dead after 15 s (health -132); with the camera and its god taken off again (`heretic`), health is still
  100 after 15 s - notarget alone keeps the enemies away.
