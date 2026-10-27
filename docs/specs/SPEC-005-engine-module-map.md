# SPEC-005: Engine module map — what SoF keeps, changes and adds relative to Quake II
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (engine executable), version 1.07f
Depends on: —

## Purpose
Scope the engine work for OpenMerc. The *Soldier of Fortune* executable is built on the Quake II 3.x engine
(client, server, common code and Win32 platform layer). This document says, subsystem by subsystem, whether the
Quake II GPL implementation can serve as the starting point, needs changes, or is absent and replaced by something
new. It describes features and external interfaces only (console commands, cvars, file types, DLLs, protocol number).
Rendering, game logic, player logic and sound mixing live in separate DLLs and are covered by other specs.

## Interface / layout

### Engine baseline
- Quake II point release: features up to and including Quake II 3.18 are present (client `spectator` userinfo cvar,
  `spectator_password`, connection-refusal message via the `rejmsg` userinfo key, `allow_download_*` cvars,
  `sv_reconnect_limit`, `sv_enforcetime`, `cl_maxfps`). The 3.18 server cvar `sv_airaccelerate` does **not** exist.
  The engine text and behaviour match the public 3.19 source closely; use 3.19 as the reference.
- Network protocol version announced in the server-data message: **33** (Quake II 3.19 uses 34). A Quake II client
  cannot connect to SoF and vice versa.

### Subsystem table
| Subsystem (Quake II reference) | SoF status | Effort for OpenMerc |
|---|---|---|
| Command buffer and console commands (qcommon/cmd.c) | kept | small |
| Console variables (qcommon/cvar.c) | kept, extended | small |
| Console text buffer and drawing (client/console.c) | kept, extended | small |
| Key bindings and key events (client/keys.c) | kept, extended | small |
| Zone memory, info strings, argv, message buffers (qcommon/common.c) | kept, changed | small |
| MD4 block checksum (qcommon/md4.c) | kept | none |
| Net channel (qcommon/net_chan.c) | kept | small |
| UDP/IPX sockets, loopback (win32/net_wins.c) | kept, extended | small |
| Collision model queries: traces, contents, PVS/PHS, areas/portals (qcommon/cmodel.c) | kept | small |
| BSP loading for collision (qcommon/cmodel.c CM_LoadMap) | changed | medium |
| Player movement (qcommon/pmove.c) | kept, changed | medium |
| Shared math/string helpers (game/q_shared.c) | kept | none |
| File system: search paths, pak files (qcommon/files.c) | changed | medium |
| Server core: connectionless packets, client slots, frames, multicast, world links (server/*) | kept, extended | medium |
| Server savegames (server/sv_ccmds.c) | replaced | medium |
| Client main loop, connection, demos (client/cl_main.c) | kept, extended | medium |
| Client message parsing, entity/player-state deltas (client/cl_parse.c, cl_ents.c) | changed | medium |
| Client prediction (client/cl_pred.c) | kept, extended | small |
| Client input / user commands (client/cl_input.c, win32/in_win.c) | changed | medium |
| Screen/HUD layout (client/cl_scrn.c) and view setup (client/cl_view.c) | changed | medium |
| Light styles and dynamic lights (part of client/cl_fx.c) | kept | small |
| Particle effects, temp entities (client/cl_fx.c, cl_tent.c, cl_newfx.c) | absent → new effect system | large |
| Menus (client/menu.c, qmenu.c) | absent → new script-driven menu system | large |
| Cinematic playback (client/cl_cin.c) | absent | none |
| Inventory display (client/cl_inv.c) | absent | none |
| Sound mixer (client/snd_*.c, win32/snd_win.c) | absent → loaded from a sound DLL | see sound specs |
| CD audio (win32/cd_win.c) | kept | small |
| Renderer loading (win32/vid_dll.c) | kept, extended | small |
| Win32 system layer (win32/sys_win.c, q_shwin.c) | kept, changed | small |
| Dedicated console helper (win32/conproc.c), software-renderer video menu (win32/vid_menu.c) | absent | none |
| Model system "Ghoul" (no Quake II counterpart) | added | very large |
| Localised string tables (no counterpart) | added | medium |
| Force feedback (no counterpart) | added | optional |
| GameSpy server browser, master heartbeat and chat (no counterpart) | added | optional |

Rough share of engine code by area (Quake II-derived ≈ one sixth; the Ghoul model system alone ≈ one third;
menus ≈ one seventh; the effect system ≈ one tenth; GameSpy ≈ one twentieth). UNVERIFIED as a measure of effort.

## Behaviour

### Kept from Quake II (implement from the GPL source)
- Command buffer semantics (`exec`, `echo`, `alias`, `wait`, `cmdlist`), `$cvar` macro expansion, `+`/`-` button commands.
- Cvar flags and `set`/`cvarlist`; console commands `toggleconsole`, `togglechat`, `messagemode`, `messagemode2`,
  `clear`, `condump`; key commands `bind`, `unbind`, `unbindall`, `bindlist`.
- Netchan reliable/unreliable sequencing, `qport`, connectionless packets (`getchallenge`, `connect`, `info`,
  `status`, `rcon`, `ping`), server commands `heartbeat`, `kick`, `status`, `serverinfo`, `dumpuser`, `map`,
  `demomap`, `gamemap`, `say`, `serverrecord`, `serverstop`, `killserver`.
- Client commands `connect`, `reconnect`, `rcon`, `record`, `stop`, `pingservers`, `skins`, `userinfo`,
  `snd_restart`, `changing`, `precache`, `setenv`, `pause`, `cmd`, `timerefresh`, `loading`, `sizeup`, `sizedown`,
  `sky`, `viewpos`, `vid_restart`, `vid_front`, `cd`, `path`, `link`, `dir`, `z_stats`, `error`, `quit`.

### Changed
- **Collision BSP loading:** each lump is validated against a per-lump table (minimum/maximum count, element size)
  and checksummed individually; new cvars `map_noareas` and `flushmap`. Error texts name the lump. UNVERIFIED:
  which lumps and limits — to be specified in the BSP format spec.
- **File system:** search paths are case-normalised ("/BASE/" and "/GHOUL/" prefixes are recognised), a
  `paklist.txt` is written/read (UNVERIFIED purpose), and writable files (configs, saves, demos) appear to go to a
  separate user directory (UNVERIFIED — check where `saveconfig` and `record` write).
- **User commands** are transmitted bit-packed rather than with Quake II's byte-aligned delta; new buttons:
  `+altattack`, `+leanleft`, `+leanright`, `+weaponExtra1`, `+weaponExtra2`. UNVERIFIED field widths — see the
  network protocol spec.
- **Entity/player-state messages** carry extra fields (e.g. near-clip distance and cinematic-freeze state are
  applied from the player state; cvars `r_nearclipdist`, `cl_cinematicfreeze`). UNVERIFIED field list.
- **Savegames:** Quake II's level/server file pair is replaced by a chunked format; commands `save`, `load`,
  `quicksave`, `quickload`, `wipe`, `newsave`, `savesleft`.
- **Server additions in the engine:** IP filtering (`filter_add`, `filter_remove`, `filter_list`, `filter_write`,
  file `listip.cfg`), deathmatch-flag commands (`list_dmflags`, `set_dmflags`, `unset_dmflags`), bots (`bot_add`,
  `bot_rem`), `adddownload`, `gserver`, `getgamestats`, `updateinven`.
- **Client additions:** `ping`, `serverstatus`, `saveconfig`, `savekeys`, `addfave`, `remfave`, `ignore`,
  `unignore`, `listignore`, `maxfpsfromrate`, `reset_predn`, `exit`, `cloud`, `terrain`; the client pre-registers
  many game-DLL commands (e.g. `weaponselect`, `itemuse`, `reload`, `playerlist`, `score`) so they forward to the
  server. Weapon firing appears to be predicted on the client (console messages about weapon prediction; UNVERIFIED).
- **Console/cvar additions:** `sset`, `zero`, `or`, `and`, `xor` (integer cvar arithmetic), `conchars`, a CPU
  information command, tab completion that cycles through matches, console buffer size set by `con_maxlines` and
  `con_buffersize`.
- **Key names** can be shown localised.
- **Quake II commands not present:** `impulse`, `download`, `packet`, `gun_next`, `gun_prev`, `gun_model`,
  all `menu_*`, `play`, `soundlist`, `stopsound`, `soundinfo`, `setmaster`, `sv`. UNVERIFIED for the sound commands
  (they may be registered by the sound DLL).
- **Error handling:** a dropped game shows an error page through the menu system instead of returning to the
  Quake II console (cvar `menu_dc_reason` carries the reason). UNVERIFIED in game.

### Added
- **Ghoul model system** — skeletal/hierarchical models used for all characters, weapons and many objects; files
  under `ghoul/`, extensions `.ghb`, `.ghl`, `.gsq`, `.ifl`, `.tga`, `.m32`; model instances are synchronised
  over the network and stored in savegames. Separate specs needed for the file formats and the network encoding.
- **Localised string tables** — files `strip/*.sp` and `strip/*.spe`, selected by `sp_language`; servers can send
  string-table references; download of string tables is gated by `allow_download_stringpackage`.
- **Effect system** — data-driven effects from `effects/*.eft` (commands `fx_save`, `fx_load`; cvars such as
  `cl_max_debris`, `cl_freezequads`).
- **Menu system** — pages from `menus/*.rmf` with commands `menu`, `popmenu`, `killmenu`, `menuoff`, `return`,
  `intermission`, `reloadall`, `select`, `refresh`, `freshgame`, `checkpass`, `changepass`, `userweapons`,
  `useritems`, `userammo`, `usermisc`.
- **HUD/interface layer** (`scr_interface`), sniper-scope overlays, team-flag icons.
- **Violence/parental controls** (`sv_violence`, `cl_violence`, `lock_deaths`, `lock_blood`, `lock_gorezones`) and
  region/language detection.
- **Sound** — loaded from a DLL named by the `snd_dll` cvar (Defsnd, EAXSnd, A3Dsnd).
- **Force feedback** (`ff_usemouse`, `ff_usejoy`, `ff_tension`, `ff_spring`) — optional for OpenMerc.
- **GameSpy** — server browser/master reporting (`gamespyport`) and IRC-style chat (`gsc_connect`, `gsc_disconnect`,
  `gsc_getchannels`, `gsc_join`, `gsc_part`, `gsc_talk`, `gsc_private`, `gsc_getusers`, `gsc_settopic`,
  `gsc_gettopic`, `gsc_kick`, `gsc_ban`, `gsc_setusermode`) — optional; the public master servers are gone.
- Window helpers `minimise`, `winstart`; `vid_modes`; network `ip_list`; `subliminal`.

## Constants
- Network protocol version: 33.
- Quake II reference version: 3.19 (3.17 and earlier ruled out; 3.19 and 3.20 not distinguishable).

## Test vectors / how to verify
- Start the original with `+set developer 1`, run `cmdlist` and `cvarlist`: every command listed above as kept or
  added must appear; the Quake II commands listed as absent must not (except those registered by the sound DLL).
- Connect a packet capture to a local server: the server-data message carries protocol 33.
- `set sp_language` to another language and restart: menu and HUD strings switch tables (`strip/*.sp`).
- Run `fx_save test` with the effect editor and confirm a file `base/effects/test.eft` is written (UNVERIFIED path root).

## Open questions
- Exact Quake II point release (3.19 vs 3.20): no engine-visible difference found.
- Which pmove constants and behaviours differ from Quake II (air control removed?) — needs in-game measurement.
- Whether the sound DLL registers `play`/`soundlist`/`stopsound`.
- Formats: Ghoul files, effect files, menu scripts, string tables, chunked savegames, bit-packed user commands —
  each needs its own spec.
