# SPEC-301: Startup sequence, command line, configuration files, module loading
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (with the retail data files for the parts that are data-driven)
Depends on: SPEC-300 (filesystem), SPEC-302 (cvars and commands), SPEC-303 (savegames)

## Purpose
Defines what the engine does from process start until the first frame: command-line handling, the order
in which configuration files run, which modules are loaded and how they are chosen, what the console shows,
and what is written at shutdown. Enough to reproduce the same final cvar state from the same files and
command line.

## Interface / layout

### Command line
The command line is split into words by the C runtime (quotes group words). At most 50 words including the
program name; more is a fatal error `argc > MAX_NUM_ARGVS`. A word longer than 255 characters is treated as
an empty word.

| Form | Effect |
|---|---|
| `-cs` (must be the first word) | Support-code utility mode, see Behaviour 9; the game does not start. |
| `-basedir <dir>`, `-cddir <dir>`, `-game <dir>`, `-user <dir>` | Filesystem roots, see SPEC-300. |
| `-nostart` (exact, case-sensitive, any position) | Do not run the default start command (Behaviour 7). |
| `+set <name> <value>` | Sets a cvar *before* the filesystem starts and again after the config files (Behaviour 3). |
| `+<command> <args...>` | Queued as a console command after initialisation (Behaviour 7). |

`dedicated` (dedicated server), `in_initjoy`, `in_initmouse`, `r_force_sgi_software`, `qport`, `gamespyport`,
`noipx`, `noudp`, `cddir`, `basedir` are no-set cvars (SPEC-302): they can only be given as `+set` on the
command line.

`+` commands are formed by joining all words after the program name with single spaces and cutting the
result at every `+`; each piece runs from just after a `+` up to (not including) the next `+` or `-`
character or the end. Consequence (Quake II behaviour, kept): `+map dm-cargo` queues `map dm`.

### Version
The `version` cvar is `1.07fx86F` (serverinfo, no-set). There is no `version` command: typing `version`
prints the cvar in the standard form `"version" is "1.07fx86F"`.

### Console banner
After a successful start the console shows `====== Soldier of Fortune Initialized ======` followed by a blank
line.

## Behaviour

### 1. Order of initialisation (observable effects)
1. Store the command line.
2. Command buffer, command system, cvar system and key bindings start (built-in commands of SPEC-302 exist).
3. `+set` words run (first pass, Behaviour 3).
4. Filesystem starts (SPEC-300). Commands `path`, `link`, `dir` exist.
5. `exec default.cfg`, then `exec config.cfg` are queued and executed.
6. `+set` words run again (second pass) and are removed from the command line.
7. Commands `z_stats`, `error`, `subliminal`, `disconnect` and cvars `host_speeds`, `log_stats`, `developer`,
   `timescale`, `fixedtime`, `logfile`, `showtrace`, `dedicated`, `version` are created. If `developer` is
   non-zero at this point a message box titled `Developer Mode` warns that the mode is for customer support.
   A dedicated server also gets the `quit` command here.
8. System layer: requires Windows 4.0 or later (otherwise fatal `SoF requires windows version 4 or greater`;
   Win32s: `SoF doesn't run on Win32s`). For a dedicated server, or when `developer` has bit 2 set, a text
   console window is opened (failure is fatal: `Couldn't create dedicated server console`). The CPU report
   of the `cpuid` command is printed (`----------- Cpu info -----------` ... `<N>MB of physical memory`)
   and `cpu_speed`, `cpu_speed_level`, `cpu_memory`, `cpu_memory_level`, `cpu_mmx`, `cpu_amd3d` are set.
   Commands `minimise`, `winstart`.
9. String-package system (`sp_language`, `sv_show_strip`).
10. Networking (prints `Winsock Initialized` and the host name / local IPs), network channel cvars.
11. Parental-lock settings are read and `sv_violence` / `cl_violence` computed (registry, see Behaviour 9).
12. GameSpy chat commands (`gsc_*`).
13. Server cvars and server commands (SPEC-302).
14. Client (skipped entirely on a dedicated server), in this order of visible effects: console
    (`Console initialized.`), video/renderer module (Behaviour 5), sound module (Behaviour 6), CD audio,
    input, force feedback, then the autoexec rule (Behaviour 4) and the command buffer is executed.
15. Default start (Behaviour 7) and the banner.

Any error inside steps 1–15 that is not fatal by itself becomes fatal with `Error during initialization: <text>`.

### 2. Configuration files and their order
Executed in this order on every start (each is looked up through the search path, SPEC-300, so the user
directory overrides archives):
1. `default.cfg` (from the archive in a retail install). The retail file in turn executes
   `default_sound.cfg`, `configs/default_keys.cfg`, `default_misc.cfg`, `default_video.cfg`, sets a few cvars,
   defines the aliases `newgame`, `tutorial`, `start` (= `menu disclaimer`), `respawn`,
   `dedicated_start` (= `exec dedicated.cfg`), and executes `menus/reset.cfg` (which creates the inventory
   and statistics cvars of SPEC-302).
2. `config.cfg` — normally `<user>/config.cfg` written at the previous shutdown. pak0 contains a 2-byte
   placeholder `CONFIG.CFG`, so a missing user file is not an error.
3. Second `+set` pass.
4. When the renderer starts (client only), it executes driver files; the reference run shows
   `drivers/alldefs.cfg`, `drivers/<vid_card>.cfg`, `drivers/memory<cpu_memory_level>.cfg`,
   `drivers/cpu<cpu_speed_level>.cfg` (renderer behaviour; details belong to the renderer spec).
5. `autoexec.cfg` — see Behaviour 4.
Each successful `exec` prints `execing <name>`; a missing file prints `Unable to exec <name>`.

### 3. `+set` passes
Each `+set <name> <value>` triple becomes the console line `set <name> <value>`, or `set <name> "<value>"`
when the value contains a space. The first pass runs before the filesystem starts, so `+set game X`,
`+set user X`, `+set basedir X` take effect for the filesystem; the second pass runs after `config.cfg`
so the command line overrides saved settings. Because `set` on an unknown name creates the cvar and later
registration keeps its value (SPEC-302), `+set dedicated 1` makes a dedicated server even though `dedicated`
is no-set. Cvars that became no-set during step 4 (`basedir`, `cddir`, `user`) print
`<name> is write protected.` during the second pass; the value from the first pass stays in effect.

### 4. autoexec.cfg rule
At step 14 (and after every mod change, SPEC-300 Behaviour 5) the engine checks whether the host file
`<basedir>/<gamedir>/autoexec.cfg` exists, where `<gamedir>` is the `gamedir` cvar or `base` when empty
(hidden, system and directory entries do not count). Only if it exists, `exec autoexec.cfg` is executed — and
that exec goes through the search path, so a copy in the user directory takes precedence. An
`autoexec.cfg` that exists only in the user directory is NOT executed automatically. A dedicated server
skips the client step and therefore does not run autoexec.cfg at start (UNVERIFIED — verify with an
autoexec.cfg containing `echo hello` and `+set dedicated 1`).

### 5. Renderer module
Cvar `vid_ref` (archive, default `gl`) selects the library `ref_<vid_ref>.dll`, loaded from the process
search path of the operating system. The library must export `GetRefAPI`; the returned interface version must
be 3. If loading fails and `vid_ref` is not `gl`, `vid_ref` is set to `gl` and loading is retried; if `gl`
fails the error is fatal (message text comes from the string package). The console shows
`------- Loading ref_<name>.dll -------`.

### 6. Sound module
Cvar `snd_dll` (archive, default empty):
| snd_dll value | Behaviour |
|---|---|
| empty | If A3D 2.0 is available use `a3dsnd.dll` (prints `Found A3D 2.0 support`), else if EAX 2.0 is available use `eaxsnd.dll` (prints `Found EAX support`), else `defsnd.dll` (then prints `Setting Default Sound support`). `snd_dll` is set to the choice. |
| `a3dsnd.dll` | Prints `Attempting A3D 2.0 support`; if unavailable prints `A3D NOT supported` and uses `defsnd.dll`. |
| `eaxsnd.dll` | Prints `Attempting EAX 2.0 support`; if unavailable prints `EAX NOT supported` and uses `defsnd.dll`. |
| `defsnd.dll` | Prints `Setting Default Sound support`. |
| anything else | Loaded as given. |
Then `------- Loading <snd_dll> -------`. If the library does not load, `snd_dll` becomes `defsnd.dll` and the
load is retried; a second failure is fatal (`LoadLibrary("<name>") failed`). The library must export
`S_Init`, `S_Shutdown`, `S_Touch`, `S_FreeSound`, `S_StartSound`, `S_StartLocalSound`, `S_StopAllSounds`,
`S_Update`, `S_Activate`, `S_BeginRegistration`, `S_RegisterSound`, `S_EndRegistration`, `S_FindName`,
`S_RegisterMusicSet`, `S_RegisterAmbientSet`, `S_SetMusicIntensity`, `S_SetMusicDesignerInfo`,
`S_SetGeneralAmbientSet`, `S_SetSoundProcType`, `S_SetSoundStruct` (a missing one is fatal:
`GetProcAddress <name> failed on <dll>`); `SNDEAX_SetEnvironment`, `SNDEAX_SetEnvironmentLevel` and
`S_A3D_ExportRenderGeom` are optional. (Interface details: module-interface spec.)

### 7. Default start command
After initialisation, if no `+` command was queued and `-nostart` was not given, the engine runs the
console line `start` (client) or `dedicated_start` (dedicated server). Both are aliases defined by the retail
`default.cfg`: `start` opens the menu `disclaimer` (which after a timeout opens `menu intro`), and
`dedicated_start` executes `dedicated.cfg`. If `+` commands were given they run instead and the loading
plaque is cleared. There are no engine-driven intro cinematics; the intro sequence is menu data.

### 8. Game and player modules
They are loaded when a server or client needs them (not at start). For each of `gamex86.dll` and
`player.dll` the engine tries, in order: `<working dir>/final/<dll>`, `<working dir>/<dll>`, then
`<dir>/<dll>` for every directory mount of the search path from highest priority down (the current game
directory first). Entry points: game `GetGameAPI` (interface version 3, otherwise
`game is version <n>, not 3`); player `GetPlayerServerAPI` (server side) and `GetPlayerClientAPI` (client side).
A server computes a 32-bit checksum of each loaded DLL file: start with 0xF8349720; for each byte b in file
order, rotate the value left by 1 bit, then add b for even byte positions (0, 2, ...) or XOR b for odd ones.
(How the checksum is used belongs to the network spec.)

### 9. Copy protection, CD and registry
There is no CD or ownership check. CD audio is optional (`cd_nocd`; failure only prints a message). The
registry key `HKEY_CURRENT_USER\Software\Raven Software\SoF` holds parental-lock data (binary values with
neutral names). The `-cs` utility mode opens a text console, prints an 8-digit hexadecimal machine code,
reads one line of up to 32 hexadecimal digits and stores the decoded bytes under that key (value `Server`,
binary), then exits. Detail of the lock semantics: UNVERIFIED / out of scope for this spec.

### 10. Logging and console echo
- `logfile` non-zero: every console print is appended to `<user>/sof.log` with control characters
  (bytes 1..31 except line feed and carriage return) removed. `1` keeps the file open; `2` or more reopens and
  closes it for every print.
- `developer` bit 2 or a dedicated server: console text is also written to the text console window; on a
  dedicated server each line typed there is executed as a console command.
- `log_stats`: when it changes to non-zero, `stats.log` is created (truncated) in the working directory with
  the header line `entities,dlights,parts,frame time`; when it changes back to 0 the file is closed.

### 11. Shutdown and config writing
On normal exit, on a fatal error, and for `saveconfig`, a non-dedicated engine writes `<user>/config.cfg`
(directories created as needed), text mode (CRLF line ends on Windows):
```
// generated by Soldier of Fortune, do not modify
<empty line>
unbindall
<empty line>
bind <KEYNAME> "<command>"          one line per bound key, in key-number order 0..255
set <cvar> "<value>"                one line per archive cvar, newest-created cvar first
```
The console then shows `Wrote configuration to <path>.`. If the file cannot be created:
`Could not write <path>.`. `savekeys` writes the same header and the bind lines (no `set` lines) to
`<user>/configs/CURRENT.cfg` and prints `Wrote key bindings to <path>.`. A dedicated server never writes
config.cfg.
`winstart <cmd>` (SPEC-302) makes the engine run `cmd.exe /c start <cmd>` (Windows NT family) or
`start.exe <cmd>` (Windows 9x) after shutdown.

### 12. Fatal errors
A fatal error writes the config (client), shuts the server down, closes the log and shows a message box
titled `Error` with the message text; the process exits with code 1. Every error (fatal or not) also deletes
`<user>/save/current.sof` (SPEC-303).

## Constants
| Name | Value |
|---|---|
| Max command-line words | 50 |
| Max command-line word length | 255 |
| Version string | `1.07fx86F` |
| Renderer interface version | 3 |
| Game interface version | 3 |
| Command buffer size | 8192 bytes (SPEC-302) |
| Registry key | `HKEY_CURRENT_USER\Software\Raven Software\SoF` |

## Test vectors / how to verify
1. Start the original with `+set logfile 1 +set developer 0`: `<user>/sof.log` begins with the Cpu info
   block and ends each session with `Wrote configuration to ./user/config.cfg.` (reference oracle log).
2. `SoF.exe +set user ./u2` → configs and saves are written under `./u2`, and `path` shows `./u2` on top.
3. `SoF.exe -user .\U3` → the user cvar is `./U3` (normalised, SPEC-300).
4. `SoF.exe +map kos1` skips the disclaimer menu and loads the map; `SoF.exe -nostart` shows only the
   console/background.
5. Put `echo AUTO` into `<user>/autoexec.cfg` only → not printed at start; also create an empty
   `base/autoexec.cfg` → `AUTO` is printed (user copy wins the search).
6. `+set snd_dll ""` on a machine without A3D/EAX → the console shows `Setting Default Sound support` and
   `------- Loading defsnd.dll -------`, and config.cfg then contains `set snd_dll "defsnd.dll"`.
7. Count lines: client `cvarlist` reports `504 cvars` and `cmdlist` `239 commands` after a plain start with
   the retail data (reference run); dedicated: `283 cvars`, `75 commands`.

## Open questions
- Exact console texts between steps (e.g. network IP lines) are network-spec material.
- Whether `dedicated_start` runs before or after `+` commands when both exist (by Behaviour 7 it does not run
  at all) — verify.
