# SPEC-302: Cvar system, command system, and the complete cvar/command lists
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (lists also cover cvars/commands created by ref_gl.dll, Defsnd.dll and the retail .cfg files as seen in a running client)
Depends on: SPEC-300, SPEC-301, SPEC-303

## Purpose
Defines console variables (flags, setting rules, special values, persistence), the command buffer and
command execution (aliases, macro expansion, forwarding), and lists every cvar and command present in a
running 1.07f client and dedicated server, with defaults, flags and meaning. Behaviour not described here
follows Quake II 3.19.

## Interface / layout

### Cvar flags
| Bit | Value | `cvarlist` column (position 1..13) | `set` letter | Meaning |
|---|---|---|---|---|
| 0 | 0x0001 | `*` (1) | — | Archive: written to config.cfg at shutdown (SPEC-301). |
| 1 | 0x0002 | `U` (2) | `u` | Userinfo: part of the client's userinfo string; a change marks userinfo as modified. |
| 2 | 0x0004 | `S` (3) | `s` | Serverinfo: part of the server's serverinfo string. |
| 3 | 0x0008 | `-` (4) | — | No-set: `set`/console assignment prints `<name> is write protected.`; only command-line `+set` (before registration), engine code or a save load can change it. |
| 4 | 0x0010 | `L` (5) | — | Latch: while a server is running, a change is stored and prints `<name> will be changed for next game.`; applied when the server (re)starts. Saved in savegames. |
| 5 | 0x0020 | `I` (6) | — | Integer: on every assignment the value is converted with C `atoi` and the string is replaced by the decimal integer (`"7.9"` → `"7"`, `"abc"` → `"0"`). |
| 6 | 0x0040 | `P` (7) | — | Literal: console assignment does not interpret `!`, `+`, `-` (see Behaviour 2). |
| 7 | 0x0080 | `N` (8) | — | Hidden: the cvar cannot be created, read, set, completed or listed from the console or by modules; only the engine's own privileged code uses it. |
| 12 | 0x1000 | `P` (9) | — | Protected: forced sets (save loading, module force-set) are ignored; not listed. |
| 8 | 0x0100 | `W` (10) | `w` | Inventory: weapon. Saved in savegames; listed by `userweapons`. |
| 9 | 0x0200 | `E` (11) | `i` | Inventory: item. Saved in savegames; listed by `useritems`. |
| 10 | 0x0400 | `A` (12) | `a` | Inventory: ammo. Saved in savegames; listed by `userammo`. |
| 11 | 0x0800 | `M` (13) | `m` | Inventory: misc (e.g. health). Saved in savegames; listed by `usermisc`. |

Cvars with flags 0x0002, 0x0004, 0x0100, 0x0200, 0x0400 or 0x0800 may not have `\`, `"` or `;` in their
name or value (`Invalid info cvar name` / `Invalid info cvar value` / `invalid info cvar value`; the change
is refused).
Savegames store every cvar whose flags contain any of 0x0010, 0x0100, 0x0200, 0x0400, 0x0800 (SPEC-303).

### `cvarlist` output
One line per cvar, newest-created first: 13 one-character flag columns in the order of the table above
(space when the flag is absent), a space, the name, a space, the value in double quotes. Cvars with flag
0x0080 or 0x1000 are skipped but counted. Last line: `<n> cvars`. Example lines from the reference run:
```
*   L         sv_altpmodlistfile "custom"
  S-          protocol "33"
            M health "100"
```

### Config file `set` lines
`set <name> "<value>"` (SPEC-301 Behaviour 11). Values are written verbatim between double quotes; there is no
escaping.

## Behaviour

### 1. Creating and setting cvars
- Names are case-sensitive.
- Registering an existing cvar keeps its current value and ORs in the new flags (a value created earlier by
  a `set` line therefore survives, but the cvar becomes archived etc. once code registers it).
- `set <name> <value>`: creates the cvar (flags 0) if unknown, otherwise assigns (subject to no-set/latch).
- `set <name> <value> <letter>` with letter `u s w i a m`: creates or overwrites the cvar and *replaces* its
  flags with exactly that one flag (archive is lost). Any other letter prints
  `flags can only be 'u', 's', 'w', 'i', 'a' or 'm'`; wrong argument count prints
  `usage: set <variable> <value> [u / s / w / i / a / m]`.
- `sset <name> <w1> [w2 ...]`: assigns the words 2..n joined by single spaces (fewer than 3 words prints
  `usage: sset <variable> <parm1> [parm2] [parm3....]`).
- `zero <name>`: assigns the empty string (usage text: `usage: clear <variable>`).
- Numeric value: C `atof` of the string (or `atoi` for integer cvars).
- Setting a numeric value from code produces `"%i"` text when the value is integral, otherwise `"%f"`
  (e.g. 0.7 → `"0.700000"`, visible in config.cfg).
- Changing `game` applies the mod change (SPEC-300 Behaviour 5).
- The name `matrix` is an alias of `timescale` in every lookup (reading, setting, registering, console).

### 2. Console assignment `<cvar> [value]`
- No argument: prints `"<name>" is "<value>"`.
- Value `!`: sets `"1"` when the current numeric value is 0, otherwise `"0"`.
- Value `+` (alone): sets the integer part (truncated) of value+1, formatted as an integer; `-` (alone):
  value−1 likewise.
- Otherwise the text is assigned. With the literal flag (0x0040) `!`, `+`, `-` are assigned verbatim.

### 3. Command buffer
- Size 8192 bytes; text that does not fit prints `Cbuf_AddText: overflow` and is dropped.
- Execution splits the buffer into lines at `\n` and at `;` outside double quotes; each line is executed in
  order. `wait` stops execution until the next frame (remaining text stays queued).
- `exec <file>`: if the name is longer than 4 characters and does not end in `.cfg` (case-insensitive),
  `.cfg` is appended. The file is read through the search path and its text is inserted *before* the rest of
  the buffer (so it runs immediately). Prints `execing <file>` or `Unable to exec <file>`; no argument prints
  `exec <filename> : execute a script file`.
- Immediate execution requested by modules runs the line and then everything that line queued, without
  executing text that was already waiting in the buffer.

### 4. Tokenising and macro expansion
- A line longer than 1023 characters is discarded (`Line exceeded 1024 chars, discarded.`).
- Outside double quotes, `$name` is replaced by the value of cvar `name`, and `#name` by the value wrapped in
  double quotes. A token is the following run of non-space characters. If the replacement is empty, two
  double-quote characters are inserted instead. After a replacement scanning continues at the replacement,
  so nested expansions work; more than 100 replacements in one line discards it
  (`Macro expansion loop, discarded.`); an expanded line over 1023 characters is discarded
  (`Expanded line exceeded 1024 chars, discarded.`); an unmatched quote discards the line
  (`Line has unmatched quote, discarded.`).
- At most 80 arguments are kept per line.

### 5. Resolving a command line
In this order:
1. Registered commands, compared case-insensitively. A command registered without a handler (the game
   commands of the client, see table) sends the whole line to the server as `cmd <line>`.
2. Aliases, compared case-insensitively. The alias text is inserted at the front of the buffer. The 16th
   alias expansion during one buffer execution is refused and prints `ALIAS_LOOP_COUNT`.
3. A cvar with exactly that name (case-sensitive): Behaviour 2.
4. Otherwise, if the client is fully connected and the name does not start with `+` or `-`, the line is sent
   to the server as a string command; else prints `Unknown command "<name>"`.
Registering a command whose name is a cvar fails with `Cmd_AddCommand: <name> already defined as a var`;
an existing command name fails with `Cmd_AddCommand: <name> already defined`.

### 6. Aliases
`alias` without arguments lists `Current alias commands:` and `<name> : <text>`. `alias <name> <words...>`
defines or replaces; the stored text is the words joined by spaces plus a newline. Names longer than 31
characters print `Alias name is too long`.

### 7. Arithmetic commands (not in Quake II)
`add <cvar> <n>` sets cvar to value + atof(n). `or`, `and`, `xor <cvar> <n>` apply the bitwise operation to
the truncated integer value and atoi(n). Fewer than 2 arguments print `Error: Too few parameters`.

### 8. `cpuid`
Prints `----------- Cpu info -----------`, `Processor : <vendor> <model>`, `Type      : <type>`,
`Speed     : <MHz> MHz`, optional `Proc ID` and `Name` lines, `MMX instructions supported`,
`3DNow! instructions supported`, `RDTSC instruction supported` as applicable, a dashed line, and
`<MB>MB of physical memory`. It sets: `cpu_speed` = measured MHz × 5/6 for AMD, × 3/4 for Cyrix, unchanged
otherwise; `cpu_speed_level` = 4 if cpu_speed ≥ 391, 3 if > 290, else 2; `cpu_memory` = physical MB + 1;
`cpu_memory_level` = 3 if cpu_memory ≥ 65, 2 if > 48, else 1.

## Constants
| Name | Value |
|---|---|
| Command buffer | 8192 bytes |
| Max line length | 1023 characters |
| Max arguments per line | 80 |
| Max macro expansions per line | 100 |
| Alias loop limit | 16 per buffer execution |
| Max alias name length | 31 |

## Test vectors / how to verify
1. `set foo 7.9` then `set foo 7.9 m` → `cvarlist` shows `            M foo "7.9"` (flags replaced).
2. `sensitivity`-style integer: `set m_sensitivity_x 3.7` → `m_sensitivity_x` prints `"m_sensitivity_x" is "3"`.
3. `cl_run !` toggles 0→1→0; `viewsize -` from `"100"` gives `"99"`.
4. `set a hello; echo $a #a` prints `hello "hello"`.
5. `matrix 0.5` changes `timescale` to `0.5`.
6. `Timescale` (capital T) → `Unknown command "Timescale"` when not connected (cvar names are case-sensitive),
   while `CVARLIST` works (command names are not).
7. `set x "a;b"` with flag `u` → refused (`invalid info cvar value`).
8. Client `cvarlist` count in the reference run: `504 cvars`, of which 496 lines are printed (8 hidden:
   the seven `lock_*` cvars and `cl_violence`).

## Open questions
- Meanings marked `?` in the tables below are inferred from names or data only (UNVERIFIED); they need the
  owning module's spec or an in-game test.
- Exact semantics of the protected flag beyond forced sets.
- `m_cursor_accel`, `m_cursor_speed`, `ip`: a registration with different flags exists (archive / no-set), but the
  reference `cvarlist` shows them without those flags, so that code path had not run; their effective flags
  after first use are UNVERIFIED.
- "Registered by" was derived from static call-site analysis; a cvar listed for several modules is registered
  by each of them (flags OR together).

## Complete cvar list
Columns: Flags as printed by `cvarlist` in the reference client run (hidden cvars show their bit value);
Registered default = value given when the engine or a module first creates the cvar (`(obs)` = registration
not traced, value observed in the reference run; `(computed)` = computed at run time); Registered by =
engine (SoF.exe), renderer (ref_gl.dll), sound (Defsnd.dll and the other sound modules), game (gamex86.dll),
cfg (created only by a shipped .cfg file); Shipped .cfg value = value set by the retail `default.cfg` chain
or `menus/reset.cfg` on every start (renderer driver files not included). Dedicated servers lack the client,
renderer and sound cvars; in a dedicated server the cfg-created cvars appear without the archive flag.
Order: as printed by `cvarlist` (newest first), families grouped.

| Name | Flags | Registered default | Registered by | Shipped .cfg value | Meaning |
|---|---|---|---|---|---|
| menu_animated |  | set by menus | engine |  | Menu animation in progress flag. |
| use_ff | * | "1" | engine |  | Use force feedback. |
| fx_editdir | * | "p:" | engine |  | Effects editor directory. |
| fx_editsegment |  | "0" | engine |  | Effects editor segment. |
| fx_edit |  | "0" | engine |  | Effects editor on. |
| fx_run |  | "0" | engine |  | ?Effects editor: run. |
| fx_trail |  | "0" | engine |  | ?Effects editor: trail. |
| fx_collide |  | "0" | engine |  | ?Effects editor: collide. |
| fx_sound |  | "null" | engine |  | ?Effects editor: sound. |
| fx_partsub |  | "0" | engine |  | ?Effects editor: particle subtract. |
| fx_graphic |  | "0" | engine |  | ?Effects editor: graphic. |
| fx_partadd |  | "0" | engine |  | ?Effects editor: particle add. |
| fx_partadjust |  | "0" | engine |  | ?Effects editor: particle adjust. |
| fx_noflash |  | "0" | engine |  | ?Disable flash effects. |
| fx_sim_time |  | "60" | engine |  | ?Effects simulation time step. |
| cl_freezequads |  | "0" | engine |  | ?Freeze particle quads (effects debugging). |
| in_initjoy | - | "1" | engine |  | Initialise the joystick (command line only). |
| in_initmouse | - | "1" | engine |  | Initialise the mouse (command line only). |
| joy_yawsensitivity | * | "-2" | engine |  | Joystick yaw sensitivity. |
| joy_pitchsensitivity | * | "-2" | engine |  | Joystick pitch sensitivity. |
| joy_upsensitivity | * | "-2" | engine |  | Joystick up sensitivity. |
| joy_sidesensitivity | * | "2" | engine |  | Joystick side sensitivity. |
| joy_forwardsensitivity | * | "-2" | engine |  | Joystick forward sensitivity. |
| joy_yawthreshold | * | "0" | engine |  | Joystick yaw dead zone. |
| joy_pitchthreshold | * | "0" | engine |  | Joystick pitch dead zone. |
| joy_upthreshold | * | "0" | engine |  | Joystick up dead zone. |
| joy_sidethreshold | * | "0" | engine |  | Joystick side dead zone. |
| joy_forwardthreshold | * | "0" | engine |  | Joystick forward dead zone. |
| joy_advaxisv | * | "0" | engine |  | Joystick axis mapping (V). |
| joy_advaxisu | * | "0" | engine |  | Joystick axis mapping (U). |
| joy_advaxisr | * | "4" | engine |  | Joystick axis mapping (R). |
| joy_advaxisz | * | "0" | engine |  | Joystick axis mapping (Z). |
| joy_advaxisy | * | "1" | engine |  | Joystick axis mapping (Y). |
| joy_advaxisx | * | "3" | engine |  | Joystick axis mapping (X). |
| joy_advanced | * | "1" | engine |  | Use advanced joystick axis mapping. |
| joy_name | * | "joystick" | engine |  | Joystick name. |
| joy_avail |  | "0" | engine |  | Joystick present (read). |
| in_joystick | * | "0" | engine |  | Use the joystick. |
| in_mouse | * | "1" | engine |  | Use the mouse. |
| sv_altpmodlistfile | *L | "custom" | engine, game |  | ?Alternate player-model list file. |
| predicting | U | "1" (obs) | engine |  | ?Client prediction flag mirrored in userinfo. |
| fov | *U | "95" | engine |  | Field of view in degrees. |
| bestweap | *U | "safe" | engine |  | Automatic best-weapon policy ('safe' or 'unsafe'). |
| msg | *U | "1" | engine |  | Minimum message priority sent to this client. |
| rate | *U | "3000" | engine |  | Maximum bytes per second the client asks the server to send. |
| skin | *U | "mullins" | engine |  | Player model skin (multiplayer). |
| teamname | *U | "mullins" | engine |  | Team model or skin set. |
| spectator | U | "0" | engine |  | Join as spectator. |
| spectator_password | U | "" | engine, game |  | Password for spectator slots. |
| password | U | "" | engine, game |  | Server join password. |
| adr0 .. adr19 | * | "" | engine | | Favourite server address slot (adr0..adr19 alike). |
| cl_nolangames | * | "0" | engine |  | Hide LAN games in the server browser. |
| cl_pitchdebug |  | "0" | engine |  | ?Debug print of pitch. |
| cl_speeds |  | "0" | engine |  | Client timing printout. |
| freezeworld |  | "0" | engine |  | ?Debug: freeze the world on the client. |
| rcon_address |  | "" | engine |  | Address used by rcon when not connected. |
| cl_downloading |  | "" | engine |  | Download status text for menus. |
| cl_download_filename |  | "" | engine |  | Name of the file being downloaded. |
| cl_download_percent |  | "0" | engine |  | Download progress in percent. |
| cl_timeout |  | "120" | engine |  | Seconds without server packets before disconnect. |
| cl_showmiss |  | "0" | engine |  | Print prediction misses. |
| cl_shownet |  | "0" | engine |  | Print received network messages. |
| cl_ffmult | * | "3.0" | engine |  | Force-feedback strength multiplier. |
| cl_obit_filter |  | "0" | engine |  | ?Filter obituary messages. |
| cl_anglespeedkey |  | "1.5" | engine |  | Turn speed multiplier while +speed is held. |
| cl_pitchspeed |  | "150" | engine |  | Keyboard pitch speed. |
| cl_yawspeed |  | "140" | engine |  | Keyboard yaw speed. |
| cl_sidespeed |  | "160" | engine |  | Strafe speed. |
| cl_forwardspeed |  | "200" | engine |  | Forward speed. |
| cl_upspeed |  | "200" | engine |  | Jump/crouch speed. |
| cl_showlocation |  | "0" | engine |  | ?Show player location text. |
| cl_showfps |  | "0" | engine |  | Show frames per second. |
| cl_maxfps |  | "30" | engine |  | Client frame rate cap. |
| cl_minfps |  | "5" | engine |  | ?Minimum client frame rate. |
| cl_predict_weapon |  | "1" | engine |  | Predict weapon animation on the client. |
| cl_predict |  | "1" | engine |  | Predict player movement on the client. |
| cl_footsteps |  | "1" | engine |  | Play footstep sounds. |
| cl_gun |  | "1" | engine |  | Draw the view weapon. |
| cl_entities |  | "1" | engine |  | Draw entities. |
| cl_quads |  | "1" | engine |  | Draw particle quads. |
| cl_lights |  | "1" | engine |  | Draw dynamic lights. |
| cl_blend |  | "1" | engine |  | Draw screen blends. |
| cd_looptrack |  | "11" | engine |  | CD track to loop. |
| cd_loopcount |  | "4" | engine |  | Plays of a CD track before switching to the loop track. |
| cd_nocd | * | "0" | engine |  | Disable CD audio. |
| scr_interface | * | "1" | engine |  | Show the HUD. |
| scr_menuoverlay |  | "0" | engine |  | ?Draw menus over the 3D view. |
| scr_drawall |  | "0" | engine |  | ?Force redraw of all screen elements. |
| graphsize |  | "50" | engine |  | Debug graph size. |
| graphwidth |  | "4" | engine |  | Debug graph line width. |
| graphshift |  | "0" | engine |  | Debug graph shift. |
| graphscale |  | "1" | engine |  | Debug graph scale. |
| graphheight |  | "100" | engine |  | Debug graph height. |
| debuggraph |  | "0" | engine |  | Show debug graph. |
| timegraph |  | "0" | engine |  | Show frame time graph. |
| netgraph |  | "0" | engine |  | Show network graph. |
| scr_printspeed |  | "8" | engine |  | Centerprint typing speed. |
| cinem_centertime |  | "2.5" | engine |  | Seconds a cinematic centerprint stays. |
| scr_centertime |  | "2.5" | engine |  | Seconds a centerprint stays. |
| scr_showpause |  | "1" | engine |  | Show the pause picture. |
| scr_showturtle |  | "0" | engine |  | Show the low frame rate icon. |
| scr_conspeed |  | "10000" | engine |  | Console scroll speed. |
| cursor_blink_speed | * | "500" | engine |  | Menu text cursor blink period in ms. |
| tip_duration | * | "2500" | engine |  | Menu tooltip duration in ms. |
| cl_stats |  | "0" | engine |  | ?Client statistics display. |
| cl_testlights |  | "0" | engine |  | Debug test lights. |
| cl_testblend |  | "0" | engine |  | Debug test blend. |
| s_testsound |  | "0" | sound |  | Test tone. |
| s_show |  | "0" | sound |  | Show playing sounds. |
| s_mixahead | * | "0.2" | sound |  | Mix-ahead in seconds. |
| s_disable |  | "0" | sound |  | Disable sound output. |
| s_nosound | * | "0" | sound |  | No sound. |
| s_initsound |  | "1" | sound |  | Initialise sound. |
| ghl_precache_texture |  | "1" | renderer |  | Ghoul: precache textures. |
| ghl_precache_verts |  | "0" | renderer |  | Ghoul: precache vertices. |
| ghl_light_multiply | * | "1.3" | renderer |  | Ghoul: model light multiplier. |
| ghl_pre_cull |  | "1" | renderer |  | Ghoul: pre-culling. |
| ghl_no_cull |  | "0" | renderer |  | Ghoul: disable culling. |
| ghl_gllight_max_lights | * | "6" | renderer |  | Ghoul: max GL lights. |
| ghl_gllight_attenuation | * | "1" | renderer |  | Ghoul: GL light attenuation. |
| ghl_gllight_directional | * | "0" | renderer |  | Ghoul: directional GL lights. |
| ghl_gllight_local_viewer | * | "1" | renderer |  | Ghoul: local-viewer lighting. |
| ghl_obj_fog | * | "1" | renderer |  | Ghoul: fog on models. |
| ghl_poly_fact | * | "-.1" | renderer |  | Ghoul: polygon offset factor. |
| ghl_poly_unit | * | "-.1" | renderer |  | Ghoul: polygon offset units. |
| ghl_no_render |  | "0" | renderer |  | Ghoul: skip model rendering. |
| ghl_max_lights | * | "3" | renderer |  | Ghoul: max lights per model. |
| ghl_fast_lights | * | "0" | renderer |  | Ghoul: fast lighting. |
| ghl_shadow_dist | * | "25" | renderer |  | Ghoul: shadow distance. |
| ghl_shadow_tilt | * | ".25" | renderer |  | Ghoul: shadow tilt. |
| ghl_shadow_darkness | * | ".75" | renderer |  | Ghoul: shadow darkness. |
| ghl_player_shadow | * | "1" | renderer |  | Ghoul: player shadow. |
| ghl_wireframe |  | "0" | renderer |  | Ghoul: wireframe. |
| s3tc_avail |  | set by renderer | renderer |  | S3TC compression available (read). |
| menu_notruecolour |  | set by renderer/cfg | engine |  | Menus: no true-colour mode available. |
| menu_nofullscreen |  | set by renderer/cfg | engine |  | Menus: fullscreen not available. |
| fx_maxdebrisonscreen |  | "16" | engine |  | Maximum debris pieces on screen. |
| cl_max_debris | * | "128" | engine |  | Maximum client debris pieces. |
| cl_max_quads | * | "256" | engine |  | Maximum client particle quads. |
| mskins_expression_limit |  | "0" | game |  | ?Limit facial-expression skins. |
| mskins_variety_limit |  | "0" | game |  | ?Limit model skin variety. |
| gl_version |  | set by renderer | renderer |  | OpenGL version string (read). |
| gl_renderer |  | set by renderer | renderer |  | OpenGL renderer string (read). |
| gl_vendor |  | set by renderer | renderer |  | OpenGL vendor string (read). |
| gl_fogdensityrange | P | "-1" | renderer |  | ?Fog override (literal value). |
| gl_fogtime | P | "-1" | renderer |  | ?Fog override (literal value). |
| gl_fogend | P | "-1" | renderer |  | ?Fog override end. |
| gl_fogstart | P | "-1" | renderer |  | ?Fog override start. |
| gl_fogdensity | P | "-1" | renderer |  | ?Fog override density. |
| gl_fogcolor | P | "-1" | renderer |  | ?Fog override colour. |
| gl_fogverts | * | "0" | renderer |  | ?Vertex fog. |
| gl_fogmode | * | "0" | renderer |  | ?Fog mode. |
| gl_rj | * | "0" | renderer |  | ?Renderer workaround flag. |
| cpu_memory_using | * | "0" | renderer |  | Renderer memory class in use. |
| gl_gentextures_broken | * | "0" | renderer |  | Driver workaround: texture name generation broken. |
| gl_scissor_broken | * | "0" | renderer |  | Driver workaround: scissor broken. |
| gl_saturatelighting |  | "0" | renderer |  | Saturate lighting. |
| gl_swapinterval | * | "1" | renderer |  | Vertical sync. |
| gl_drawbuffer |  | "GL_BACK" | renderer |  | OpenGL draw buffer. |
| gl_drawflat |  | "0" | renderer |  | Debug flat shading. |
| gl_drawmode |  | "0" | renderer |  | ?Debug draw mode. |
| gl_s3_s3tc | * | "1" | renderer |  | Use S3TC compression. |
| gl_ext_anisotropic | * | "1" | renderer |  | Use anisotropic filtering. |
| gl_ext_multitexture | * | "1" | renderer |  | Use multitexture. |
| gl_ext_swapinterval | * | "1" | renderer |  | Use the swap-interval extension. |
| gl_showeffectfill |  | "0.0" | renderer |  | ?Debug effect fill. |
| gl_displayrefresh | * | "0" | renderer |  | Display refresh rate (0 = default). |
| gl_textureclamp |  | "1" | renderer |  | ?Texture clamp mode. |
| gl_offsetunits | * | "-2.0" | renderer |  | Polygon offset units. |
| gl_offsetfactor | * | "-2.0" | renderer |  | Polygon offset factor. |
| gl_lockpvs |  | "0" | renderer |  | Debug: lock PVS. |
| gl_cull |  | "1" | renderer |  | Back-face culling. |
| gl_finish |  | "0" | renderer |  | Finish GL each frame. |
| gl_ztrick |  | "0" | renderer |  | Depth-buffer trick. |
| gl_showtris |  | "0" | renderer |  | Debug: show triangles. |
| gl_skymip |  | "0" | renderer |  | Sky texture downscale. |
| gl_nobind |  | "0" | renderer |  | Debug: no texture binding. |
| gl_dynamic |  | "1" | renderer |  | Dynamic lightmaps. |
| gl_lightmap |  | "0" | renderer |  | Debug: lightmaps only. |
| gl_log |  | "0" | renderer |  | Log OpenGL calls. |
| gl_allow_cds | I | "0" | renderer |  | Allow display mode changes (integer). |
| gl_allow_software | I | "0" | renderer |  | Allow software OpenGL (integer). |
| r_lightlevel |  | "0" | engine, renderer |  | Light level at the player (written by the renderer). |
| r_warnings |  | "0" | renderer |  | Renderer warnings. |
| r_speeds |  | "0" | renderer |  | Renderer statistics. |
| r_nocull |  | "0" | renderer |  | Debug: no culling. |
| r_novis |  | "0" | renderer |  | Debug: ignore PVS. |
| r_drawworld |  | "1" | renderer |  | Draw the world. |
| r_drawentities |  | "1" | renderer |  | Draw entities. |
| r_fullbright |  | "0" | renderer |  | Fullbright. |
| r_norefresh |  | "0" | renderer |  | Skip refresh. |
| sc_framerate |  | "30" | engine, renderer |  | ?Frame rate for scripted cinematics. |
| r_nearclipdist |  | "0" | renderer |  | Near clip distance. |
| r_farclipdist |  | "65535.0" | renderer |  | Far clip distance. |
| r_force_sgi_software | -I | "0" | engine |  | Force SGI software OpenGL (command line only, integer). |
| win_broken | * | "1" | engine |  | ?Window-system workaround flag. |
| vid_ypos | * | "22" | engine |  | Window Y position. |
| vid_xpos | * | "3" | engine |  | Window X position. |
| vid_ref | * | "gl" | engine, renderer |  | Renderer module name: ref_<vid_ref>.dll is loaded (falls back to 'gl'). |
| console | - | "0" | engine |  | Forces the console open (set when a game is dropped by an error). |
| con_maxlines | * | "512" | engine |  | Console scrollback lines. |
| con_buffersize | * | "32768" | engine |  | Console buffer size in bytes. |
| con_notifytime | * | "3" | engine |  | Seconds that notify lines stay. |
| allow_download_stringpackage | * | "1" | engine |  | Allow string-package downloads. |
| allow_download_sounds | * | "1" | engine |  | Allow sound downloads. |
| allow_download_players | * | "1" | engine |  | Allow player model downloads. |
| allow_download_models | *U | "1" | engine |  | Allow model downloads. |
| allow_download_maps | * | "1" | engine |  | Allow map downloads. |
| allow_download | * | "1" / "0" | engine |  | Allow downloads. |
| bot_name0 .. bot_name31 | | "??????" | engine | | Bot name slot (bot_name0..bot_name31 alike). |
| showclamp |  | "0" | engine |  | Print time clamping. |
| timedemo |  | "0" | engine |  | Play demos as fast as possible and report the frame rate. |
| sv_restartlevel |  | "0" | engine |  | ?Level restart request flag. |
| sv_testsave |  | "0" | engine |  | Debug: run the save path without writing a file. |
| sv_enforcetime |  | "0" | engine |  | Enforce client command timing. |
| sv_no_big_ents |  | "0" | engine |  | ?Debug: drop oversized entities. |
| sv_flash_big_ents |  | "0" | engine |  | ?Debug: flash oversized entities. |
| bot_num |  | "0" | engine |  | Number of bots. |
| sv_jumpcinematic | * | "0" | engine |  | Skip the current cinematic. |
| sv_reconnect_limit | * | "3" | engine |  | Minimum seconds between reconnects from one address. |
| sv_max_ghoul_mp | * | "200" | engine |  | ?Maximum Ghoul model instances in multiplayer. |
| paused |  | "0" | engine, sound |  | Game paused flag (set to 1 right after a save is loaded). |
| zombietime |  | "2" | engine |  | Seconds a disconnected client slot stays reserved. |
| timeout |  | "180" | engine |  | Seconds before an unresponsive client is dropped. |
| rcon_password |  | "" | engine |  | Remote console password. |
| filterban |  | "1" | engine |  | IP filters ban listed addresses (1) or allow only them (0). |
| weaponarena | L | "0" | engine, game |  | ?Weapon arena mode. |
| public |  | "0" | engine |  | Report to the master server. |
| hostname | *S | "noname" / "" | engine, game |  | Server name. |
| welcome_mess | * | "Welcome to Soldier of Fortune Multiplayer" | engine |  | Welcome message for joining players. |
| sv_maplistfile | * | "ravdm" | engine |  | Map list file (maplists/<name>.lst). |
| sv_maplist |  | "" | engine |  | Map rotation list. |
| maxclients | *SL | "8" / "1" / "4" | engine, game |  | Maximum players. |
| deathmatch | L | "1" / "0" | engine, game |  | Deathmatch mode. |
| ctf_control_delay_time | * | "20" | engine, game |  | ?Control-point delay in seconds. |
| team_red_blue | *U | "0" | engine |  | Team choice. |
| control_bonus | *S | "1" | engine |  | ?Control-point bonus. |
| control_limit | *S | "300" | engine |  | ?Control-point score limit. |
| ctf_loops | *S | "3" | engine, game |  | ?Capture-the-flag rounds. |
| sv_quicksave_index | * | "0" | engine |  | Index 0..3 of the newest quick save. |
| ai_corpselife | * | "5" | engine, game |  | Corpse lifetime. |
| ai_maxcorpses | * | "5" | engine, game |  | Maximum corpses. |
| g_movescale | *S | "1" / "1.0" | engine, game |  | ?Movement speed scale. |
| sv_suicidepenalty | *S | "2" | engine, game |  | Frag penalty for suicide. |
| protocol | S- | "33" (computed) | engine |  | Network protocol version. |
| cheats | S | "0" | engine, game |  | Allow cheats. |
| timelimit | *S | "0" | engine, game |  | Minutes per map. |
| fraglimit | *S | "0" | engine, game |  | Frags per map. |
| skill | SL | "2" | engine, game |  | Single-player difficulty 0..5 (Effortless, Easy, Medium, Challenging, Unfair, Custom). |
| gsc_port | * | "6667" | engine |  | GameSpy chat server port. |
| gsc_server | * | "peerchat.gamespy.com" | engine |  | GameSpy chat server host. |
| gsc_user | * | "JohnMullins" | engine |  | GameSpy chat user name. |
| gsc_nick | * | "JohnMullins" | engine |  | GameSpy chat nickname. |
| gamespyport | - | "28911" (obs) | engine |  | GameSpy query port (command line only). |
| sv_violence | SL | "0" | engine |  | Server violence bitmask, one bit per parental lock (1 deaths, 2 blood, 4 gore zones, 8 dismemberment, 16 gore textures); computed from the locks at start (polarity UNVERIFIED). |
| net_receiverate |  | "1" | engine |  | ?Network receive rate factor. |
| net_sendrate |  | "1" | engine |  | ?Network send rate factor. |
| qport | - | random (computed) | engine |  | Client port id that survives NAT port changes (random, command line only). |
| showdrop |  | "0" | engine |  | Print dropped packets. |
| showpackets |  | "0" | engine |  | Print packets. |
| net_shownet |  | "0" | engine |  | ?Print network debug. |
| net_socksPassword | * | "" | engine |  | SOCKS5 password. |
| net_socksUsername | * | "" | engine |  | SOCKS5 user. |
| net_socksPort | * | "1080" | engine |  | SOCKS5 port. |
| net_socksServer | * | "" | engine |  | SOCKS5 server. |
| net_socksEnabled | * | "0" | engine |  | Use the SOCKS5 proxy. |
| net_ttl | * | "0" | engine |  | ?IP time-to-live (0 = default). |
| net_mainip | *- | "-1" | engine |  | ?Index of the local IP to use (-1 = automatic). |
| ipxfix |  | "1" | engine |  | ?IPX workaround. |
| noipx | - | "0" | engine |  | Disable IPX. |
| noudp | - | "0" | engine |  | Disable UDP. |
| sv_show_strip |  | "0" | engine |  | Debug: show string-package identifiers instead of text. |
| sp_language | *I | computed | engine |  | String-package language index (0 = English). |
| use_mmx |  | "1" | engine, renderer, sound |  | Allow MMX code paths. |
| use_amd3d |  | "1" | engine, renderer, sound |  | Allow 3DNow! code paths. |
| cpu_memory_level |  | "0" | renderer |  | Memory class 1..3 derived from cpu_memory (selects drivers/memory<N>.cfg). |
| cpu_memory |  | computed | engine |  | Physical memory in MB plus 1, set at start. |
| cpu_speed_level |  | "0" | renderer |  | CPU class 2..4 derived from cpu_speed (selects drivers/cpu<N>.cfg). |
| cpu_speed |  | computed | engine |  | Measured CPU clock in MHz (vendor-adjusted), set at start. |
| cpu_amd3d | - | "0" | engine, renderer, sound |  | Detected 3DNow! support (read-only). |
| cpu_mmx | - | "0" | engine, renderer, sound |  | Detected MMX support (read-only). |
| version | S- | "1.07f" + "x86" + "F" (computed) | engine |  | Engine version string (also in serverinfo). |
| dedicated | - | "0" | engine, game |  | 1 = dedicated server (no client, system console). Effective only when given on the command line. |
| showtrace |  | "0" | engine |  | Print trace and point-contents counters every frame. |
| fixedtime |  | "0" | engine |  | If non-zero, every frame advances by this many milliseconds. |
| timescale |  | "1" | engine |  | Game time multiplier. |
| developer |  | "0" | engine |  | Debug output bitmask: 1 = developer prints, 2 = also echo console text to a system console window, 4 = verbose file-system and save-chunk traces. Non-zero at start shows a warning message box. |
| log_stats |  | "0" | engine |  | When non-zero, per-frame statistics are written to stats.log in the working directory. |
| host_speeds |  | "0" | engine |  | Per-frame timing printout (1 = text, 2 = text and graph, 3 = game-time graph). |
| stat_moneyadd, stat_playedtime_base, stat_playedtime, stat_cashearned, stat_savesused, stat_gibs, stat_headshots, stat_nutshots, stat_throatshots, stat_friendlieskilled, stat_guyskilled | | (none; created by cfg) | cfg | "" | Mission statistics (stat_* family, filled by getgamestats; shown by menus). |
| *_info (27 cvars) | | (none; created by cfg) | cfg | slug_info = "items/a_slug,40,none,0", fuel_info = "items/a_fuel,30,none,0", battery_info = "items/a_battery,60,none,0", rockets_info = "items/a_rockets,8,none,0", shells_info = "items/a_shells,32,none,0", a556_info = "items/a_556,120,none,0", a44_info = "items/a_44,36,none,0", a9mm_info = "items/a_9mm,50,none,0", aknife_info = "weapons/w_knife,6,none,0", medkit_info = "items/i_medkit,2,none,1", armor_info = "items/i_armor,100,none,1", goggles_info = "items/i_goggles,100,none,1", grenade_info = "items/i_grenade,6,none,1", c4_info = "items/i_c4,4,none,1", fpak_info = "items/i_fpak,6,none,1", mpg_info = "weapons/w_mpg,1,battery,3", rocket_info = "weapons/w_rocket,1,rockets,3", flamegun_info = "weapons/w_flamegun,1,fuel,3", slugger_info = "weapons/w_slugger,1,slug,2", machinegun_info = "weapons/w_machinegun,1,a556,2", mpistol_info = "weapons/w_mpistol,1,a9mm,2", assault_info = "weapons/w_assault,1,a556,2", shotgun_info = "weapons/w_shotgun,1,shells,2", sniper_info = "weapons/w_sniper,1,a556,2", pistol2_info = "weapons/w_pistol2,1,a44,1", pistol1_info = "weapons/w_pistol1,1,a9mm,1", knife_info = "weapons/w_knife,1,aknife,1" | Inventory item descriptor 'model,amount,ammo,slotclass' (all *_info alike; defined by menus/reset.cfg). |
| mpg, rocket, flamegun, slugger, machinegun, mpistol, assault, shotgun, sniper, pistol2, pistol1, knife | W | (none; created by cfg with flag) | cfg | mpg = "0", rocket = "0", flamegun = "0", slugger = "0", machinegun = "0", mpistol = "0", assault = "0", shotgun = "2", sniper = "0", pistol2 = "0", pistol1 = "1", knife = "1" | Carried-over player inventory, weapon (flag W). Saved in savegames. |
| slug, fuel, battery, rockets, a556, shells, a9mm, a44, aknife | A | (none; created by cfg with flag) | cfg | slug = "0", fuel = "0", battery = "0", rockets = "0", a556 = "0", shells = "32", a9mm = "128", a44 = "0", aknife = "6" | Carried-over player inventory, ammo (flag A). Saved in savegames. |
| medkit, armor, grenade, goggles, c4, fpak | E | (none; created by cfg with flag) | cfg | medkit = "0", armor = "100", grenade = "0", goggles = "0", c4 = "2", fpak = "5" | Carried-over player inventory, item (flag E). Saved in savegames. |
| health | M | (none; created by cfg with flag) | cfg | health = "100" | Carried-over player inventory, misc (flag M). Saved in savegames. |
| base_* (27 cvars) | | (none; created by cfg) | cfg | base_slug = "0", base_fuel = "0", base_battery = "0", base_rockets = "0", base_a556 = "0", base_shells = "32", base_a9mm = "150", base_a44 = "0", base_aknife = "6", base_medkit = "0", base_armor = "0", base_goggles = "0", base_grenade = "0", base_c4 = "0", base_fpak = "0", base_mpg = "0", base_rocket = "0", base_flamegun = "0", base_slugger = "0", base_machinegun = "0", base_mpistol = "0", base_assault = "0", base_sniper = "0", base_shotgun = "0", base_pistol2 = "0", base_pistol1 = "0", base_knife = "0" | Starting inventory value for a new game (base_* family). |
| dmflags | *SL | "0" | game | "4100" | Deathmatch option bits (documented in dedicated.cfg). |
| name | *U | "unnamed" | engine | "John Mullins" | Player name. |
| viewsize | * | "100" | engine | "100" | View window size in percent. |
| r_iaf | * | "GL_ALPHA_FORMAT" | renderer | "GL_ALPHA_FORMAT" | Alpha texture internal format. |
| r_isf | * | "GL_SOLID_FORMAT" | renderer | "GL_SOLID_FORMAT" | Solid texture internal format. |
| vid_card | * | "[not set yet]" | renderer | "" | Detected video card family (selects drivers/<card>.cfg). |
| vid_brightness | * | "0.5" | renderer | "0.6" | Brightness. |
| vid_contrast | * | "0.5" | renderer | "0.6" | Contrast. |
| vid_gamma | * | "0.0" | renderer | "0.0" | Gamma. |
| ghl_shadows | * | "2" | renderer | "1" | Ghoul: shadow quality. |
| ghl_no_texture_stack | * | "0" | engine, renderer | "0" | Ghoul: disable texture stacking. |
| ghl_specular | * | "1" | engine, game, renderer | "1" | Ghoul: specular highlights. |
| ghl_light_method | * | "2" | engine, game, renderer | "2" | Ghoul: lighting method. |
| ghl_mip | * | "0" | game, renderer | "0" | Ghoul: texture downscale. |
| gl_dlightintensity | * | "2.0" | renderer | "2" | Dynamic light intensity. |
| gl_effectfill | * | "100.0" | renderer | "4000" | ?Effect fill-rate budget. |
| gl_texturemode | * | "GL_LINEAR_MIPMAP_NEAREST" | renderer | "GL_LINEAR_MIPMAP_NEAREST" | Texture filter mode. |
| gl_detailtexturingdistance | * | "250" | renderer | "250" | Detail texture distance. |
| gl_detailtexturing | * | "1" | renderer | "1" | Use detail textures. |
| gl_modulate | * | "1" | renderer | "1" | Lightmap brightness multiplier. |
| gl_picmip | * | "0" | renderer | "0" | Texture downscale level. |
| gl_pictip | * | "0" | renderer | "0" | ?2D picture downscale level. |
| gl_monolightmap | * | "0" | renderer | "0" | Monochrome lightmaps. |
| gl_clear | * | "0" | engine, renderer | "0" | Clear the frame buffer every frame. |
| gl_bitdepth | * | "0" | renderer | "0" | Colour depth (0 = desktop). |
| gl_driver | * | "opengl32" | renderer | "opengl32" | OpenGL driver DLL name. |
| shownames | * | "0" | engine | "0" | Show player names. |
| freelook | * | "0" | engine | "1" | Mouse look always on. |
| win_noaltenter | * | "1" | engine | "1" | Disable the Alt-Enter fullscreen toggle. |
| win_noalttab | * | "1" | engine | "1" | Disable Alt-Tab while active. |
| cl_subtitles | * | "1" | engine | "1" | Show subtitles. |
| cl_run | * | "0" | engine | "0" | Always run. |
| crosshair | * | "0" | engine | "1" | Crosshair style (0 = off). |
| lookstrafe | * | "0" | engine | "0" | Strafe with mouse look. |
| lookspring | * | "0" | engine | "1" | Recenter view when mouse look ends. |
| m_cursor_accel |  | "2" | engine | "2" | Menu cursor acceleration. |
| m_cursor_speed |  | "8" | engine | "8" | Menu cursor speed. |
| m_doublespeed | * | "200" | engine | "200" | ?Mouse double-speed threshold. |
| m_filter | * | "0" | engine | "0" | Average mouse input over two frames. |
| m_side |  | "1" | engine | "0.8" | Mouse strafe scale. |
| m_forward |  | "1" | engine | "1" | Mouse forward scale. |
| m_yaw |  | "0.022" | engine | "0.022" | Mouse yaw scale. |
| m_pitch | * | "0.022" | engine | "0.022" | Mouse pitch scale. |
| m_sensitivity_wheel | * | "1" | engine | "5" | Mouse wheel sensitivity. |
| m_invert_y | * | "0" | engine | "0" | Invert mouse Y. |
| m_sensitivity_y | *I | "6" | engine | "5" | Mouse Y sensitivity (integer). |
| m_invert_x | * | "0" | engine | "0" | Invert mouse X. |
| m_sensitivity_x | *I | "6" | engine | "8" | Mouse X sensitivity (integer). |
| s_musicenabled | * | "1" | sound | "1" | Enable music. |
| s_primary | * | "0" | sound | "0" | Use the primary sound buffer. |
| s_loadas8bit | * | "0" | sound | "0" | Load sounds as 8-bit. |
| s_khz | * | "22" | sound | "22" | Mixing rate in kHz (11/22/44). |
| s_musicvolume | * | "0.7" | sound | "0.7" | Music volume. |
| s_volume | * | "0.7" | sound | "0.7" | Sound volume. |
| snd_dll | * | "" | engine | "defsnd.dll" | Sound module: '' = auto-detect (A3D, then EAX, else default), or a3dsnd.dll, eaxsnd.dll, defsnd.dll. |
| fs_createpak |  | "0" | engine, renderer |  | Development aid: record every file opened from a loose directory and write paklist.txt at shutdown. |
| user | S- | "./user" | engine |  | Per-user directory: highest search-path priority and the write directory (configs, saves, log). Command line: -user <dir>. |
| game | SL | "" | engine |  | Mod directory name under basedir (empty = base). Command line: -game <dir>. |
| cddir | - | "" | engine |  | Optional second root; when set, <cddir>/base is mounted below <basedir>/base. Command line: -cddir <dir>. |
| basedir | - | "." | engine |  | Root directory that contains base/. Command line: -basedir <dir>. |
| logfile |  | "0" | engine |  | Console log to <user>/sof.log (append mode): 0 off, 1 file kept open, 2 or more reopened and closed for every print. |
| gl_mode | * | "3" | renderer | "3" | Video mode index (3 = 640x480 ... 9 = 1600x1200). |
| vid_fullscreen | * | "1" / "0" | engine, renderer | "1" | Fullscreen mode. |
| ip |  | "localhost" | engine |  | Local IP address to bind. |
| lock_password | (hidden 0x1080) | "" | engine | | Parental-lock password (hidden). |
| lock_deaths | (hidden 0x1080) | "1" (then from parental-lock store) | engine | | Parental lock: death effects (hidden). |
| lock_blood | (hidden 0x1080) | "1" (then from parental-lock store) | engine | | Parental lock: blood (hidden). |
| lock_gorezones | (hidden 0x1080) | "1" (then from parental-lock store) | engine | | Parental lock: gore zones (hidden). |
| lock_sever | (hidden 0x1080) | "1" (then from parental-lock store) | engine | | Parental lock: dismemberment (hidden). |
| lock_textures | (hidden 0x1080) | "1" (then from parental-lock store) | engine | | Parental lock: gore textures (hidden). |
| lock_pure | (hidden 0x1080) | "1" (then from parental-lock store) | engine | | Parental lock: also enforce on servers (hidden). |
| cl_violence | U (hidden 0x80) | "0" (then computed) | engine | | Client violence bitmask derived from the locks (hidden, userinfo). |
| gamedir | S- | "" (set by engine) | engine | | Current mod directory name (empty for base); maintained by the engine. |
| non_multilingual | | set only on German locale | engine | | Set to 1 on German-locale systems. |
| mapname | S- | "" (set by server) | engine | | Current map name (serverinfo). |
| map_noareas | | "0" | engine | | ?Debug: ignore area portals. |
| sv_showdamage | | "0" | engine | | ?Debug: print damage events. |
| hostport, ip_hostport, ipx_hostport | - | "0" | engine | | Server UDP/IPX port overrides (registered when networking opens). |
| clientport, ip_clientport, ipx_clientport, port | - / - / - / - | "0" or computed | engine | | Client port overrides (registered when networking opens). |
| menu_mouse_x, menu_mouse_y | | "0" | engine | | Menu cursor position. |
| menu_gamma, menu_contrast, menu_brightness, menu_modulate | | "" | engine | | Menu working copies of video settings. |

Cvars created on demand (not present in the reference `cvarlist` because the code path had not run) are
included at the end of the table.

## Complete command list
Columns: Registered by (engine subsystem, a loaded module, or "client, forwarded to server" for commands the
client registers without a handler so that they are sent to the game module as `cmd <line>`);
Also in Quake II = a command of the same name is registered by Quake II 3.19; Dedicated = present in a
dedicated server (75 commands). Order: as printed by `cmdlist` in the reference client run (239 commands).
Commands registered by the game module at run time (server console commands) are not included.

| Command | Registered by | Also in Quake II | Dedicated | Meaning |
|---|---|---|---|---|
| ff_spring | engine (force feedback) | no |  | ?Force feedback: spring. |
| ff_tension | engine (force feedback) | no |  | ?Force feedback: tension. |
| ff_usejoy | engine (force feedback) | no |  | ?Force feedback: use joystick. |
| ff_usemouse | engine (force feedback) | no |  | ?Force feedback: use mouse. |
| fx_load | engine (effects) | no |  | Load an effect file (editor). |
| fx_save | engine (effects) | no |  | Save an effect file (editor). |
| joy_advancedupdate | engine (input) | yes |  | Apply advanced joystick settings. |
| -mlook | engine (input) | yes |  | Release of +mlook. |
| +mlook | engine (input) | yes |  | Mouse look (press). |
| info | client, forwarded to server (game module) | yes |  | info <userid>: print a client's userinfo (dedicated console). |
| sv_precache | client, forwarded to server (game module) | no |  | ?Precache request. |
| itemdrop | client, forwarded to server (game module) | no |  | Drop the current inventory item. |
| itemprev | client, forwarded to server (game module) | no |  | Select the previous inventory item. |
| itemnext | client, forwarded to server (game module) | no |  | Select the next inventory item. |
| itemuse | client, forwarded to server (game module) | no |  | Use the current inventory item. |
| weaponbestunsafe | client, forwarded to server (game module) | no |  | ?Switch to the best weapon including explosive ones. |
| weaponbestsafe | client, forwarded to server (game module) | no |  | ?Switch to the best non-explosive weapon. |
| weaponlose | client, forwarded to server (game module) | no |  | ?Remove the current weapon. |
| weapondrop | client, forwarded to server (game module) | no |  | Drop the current weapon. |
| weaponselect | client, forwarded to server (game module) | no |  | weaponselect <slot>: select a weapon slot. |
| weapnext | client, forwarded to server (game module) | yes |  | Next weapon. |
| weapprev | client, forwarded to server (game module) | yes |  | Previous weapon. |
| reload | client, forwarded to server (game module) | no |  | Reload the weapon. |
| go | client, forwarded to server (game module) | no |  | ?Game command 'go'. |
| listpickups | client, forwarded to server (game module) | no |  | ?List pickups. |
| turnonmissionmsg | client, forwarded to server (game module) | no |  | ?Enable mission messages. |
| turnoffmissionmsg | client, forwarded to server (game module) | no |  | ?Disable mission messages. |
| givemoretutorial | client, forwarded to server (game module) | no |  | ?Tutorial: give items. |
| givesnipertutorial | client, forwarded to server (game module) | no |  | ?Tutorial: give sniper items. |
| entlist | client, forwarded to server (game module) | no |  | ?List entities. |
| playerlist | client, forwarded to server (game module) | no |  | ?List players. |
| teamview | client, forwarded to server (game module) | no |  | ?Team view. |
| gimme | client, forwarded to server (game module) | no |  | ?Cheat: give items. |
| wave | client, forwarded to server (game module) | yes |  | wave <n>: gesture. |
| putaway | client, forwarded to server (game module) | no |  | ?Holster the weapon. |
| killallmonsters | client, forwarded to server (game module) | no |  | ?Cheat: kill all enemies. |
| kill | client, forwarded to server (game module) | yes |  | Suicide. |
| phantom | client, forwarded to server (game module) | no |  | ?Cheat code. |
| ninja | client, forwarded to server (game module) | no |  | ?Cheat code. |
| heretic | client, forwarded to server (game module) | no |  | ?Cheat code. |
| updateinvfinal | client, forwarded to server (game module) | no |  | ?Inventory update at mission end. |
| defaultweapons | client, forwarded to server (game module) | no |  | ?Give default weapons. |
| bigelbow | client, forwarded to server (game module) | no |  | ?Game command. |
| elbow | client, forwarded to server (game module) | no |  | ?Game command. |
| playlevel | client, forwarded to server (game module) | no |  | ?Game command. |
| destroyents | client, forwarded to server (game module) | no |  | ?Debug: destroy entities. |
| loadpoint | client, forwarded to server (game module) | no |  | ?Game command. |
| help | client, forwarded to server (game module) | no |  | Show mission objectives/help. |
| score | client, forwarded to server (game module) | no |  | Show the scoreboard. |
| say_team | client, forwarded to server (game module) | yes |  | Team chat. |
| say | engine (server) | yes | yes | Chat. |
| players | client, forwarded to server (game module) | no |  | List players. |
| listignore | engine (client) | no |  | List ignored players. |
| unignore | engine (client) | no |  | Stop ignoring. |
| ignore | engine (client) | no |  | Ignore a player's chat. |
| remfave | engine (client) | no |  | Remove a favourite. |
| addfave | engine (client) | no |  | Add a server to favourites (adr cvars). |
| savekeys | engine (client) | no |  | Write key bindings to configs/CURRENT.cfg. |
| saveconfig | engine (client) | no |  | Write config.cfg now. |
| cl_touch | engine (client) | no |  | ?Client touch handling. |
| reset_predn | engine (client) | no |  | ?Reset client prediction / player module. |
| precache | engine (client) | yes |  | Server precache start. |
| setenv | engine (client) | yes |  | Set an environment variable. |
| rcon | engine (client) | yes |  | Remote console. |
| reconnect | engine (client) | yes |  | Reconnect. |
| connect | engine (client) | yes |  | Connect to a server. |
| exit | engine (client) | no |  | Disconnect and exit. |
| quit | engine (client) | yes | yes | Exit. |
| maxfpsfromrate | engine (client) | no |  | Set cl_maxfps from rate (<5000: 30, <10000: 60, else 120). |
| stop | engine (client) | yes |  | Stop demo recording. |
| record | engine (client) | yes |  | Record a demo. |
| changing | engine (client) | yes |  | Server level-change notice. |
| snd_restart | engine (client) | yes |  | Restart the sound module. |
| userinfo | engine (client) | yes |  | Print userinfo. |
| skins | engine (client) | yes |  | Reload and list player skins. |
| ping | engine (client) | no |  | ping <address>. |
| serverstatus | engine (client) | no |  | Query a server's status. |
| pingservers | engine (client) | yes |  | Ping LAN and favourite servers. |
| pause | engine (client) | yes |  | Pause. |
| cmd | engine (client) | yes |  | Forward the rest of the line to the server. |
| -klook | engine (client input) | yes |  | Release of +klook. |
| +klook | engine (client input) | yes |  | Keyboard look (press). |
| -use | engine (client input) | yes |  | Release of +use. |
| +use | engine (client input) | yes |  | Use (press). |
| -weaponExtra2 | engine (client input) | no |  | Release of +weaponExtra2. |
| +weaponExtra2 | engine (client input) | no |  | Weapon extra action 2 (press). |
| -weaponExtra1 | engine (client input) | no |  | Release of +weaponExtra1. |
| +weaponExtra1 | engine (client input) | no |  | Weapon extra action 1 (press). |
| -altattack | engine (client input) | no |  | Release of +altattack. |
| +altattack | engine (client input) | no |  | Alternate fire (press). |
| -attack | engine (client input) | yes |  | Release of +attack. |
| +attack | engine (client input) | yes |  | Primary fire (press). |
| -speed | engine (client input) | yes |  | Release of +speed. |
| +speed | engine (client input) | yes |  | Run/walk modifier (press). |
| -leanright | engine (client input) | no |  | Release of +leanright. |
| +leanright | engine (client input) | no |  | Lean right (press). |
| -leanleft | engine (client input) | no |  | Release of +leanleft. |
| +leanleft | engine (client input) | no |  | Lean left (press). |
| -moveright | engine (client input) | yes |  | Release of +moveright. |
| +moveright | engine (client input) | yes |  | Strafe right (press). |
| -moveleft | engine (client input) | yes |  | Release of +moveleft. |
| +moveleft | engine (client input) | yes |  | Strafe left (press). |
| -strafe | engine (client input) | yes |  | Release of +strafe. |
| +strafe | engine (client input) | yes |  | Strafe modifier (press). |
| -lookdown | engine (client input) | yes |  | Release of +lookdown. |
| +lookdown | engine (client input) | yes |  | Look down (press). |
| -lookup | engine (client input) | yes |  | Release of +lookup. |
| +lookup | engine (client input) | yes |  | Look up (press). |
| -back | engine (client input) | yes |  | Release of +back. |
| +back | engine (client input) | yes |  | Move back (press). |
| -forward | engine (client input) | yes |  | Release of +forward. |
| +forward | engine (client input) | yes |  | Move forward (press). |
| -right | engine (client input) | yes |  | Release of +right. |
| +right | engine (client input) | yes |  | Turn right (press). |
| -left | engine (client input) | yes |  | Release of +left. |
| +left | engine (client input) | yes |  | Turn left (press). |
| -movedown | engine (client input) | yes |  | Release of +movedown. |
| +movedown | engine (client input) | yes |  | Crouch (press). |
| -moveup | engine (client input) | yes |  | Release of +moveup. |
| +moveup | engine (client input) | yes |  | Jump (press). |
| centerview | engine (client input) | yes |  | Center the view. |
| terrain | engine (screen) | no |  | ?terrain <params>. |
| cloud | engine (screen) | no |  | ?cloud <params>: cloud layer. |
| sky | engine (screen) | yes |  | Set the sky. |
| sizedown | engine (screen) | yes |  | Shrink the view. |
| sizeup | engine (screen) | yes |  | Enlarge the view. |
| loading | engine (screen) | yes |  | Show the loading screen. |
| timerefresh | engine (screen) | yes |  | Renderer benchmark. |
| usermisc | engine (menus) | no |  | Print cvars carrying the misc flag. |
| userammo | engine (menus) | no |  | Print cvars carrying the ammo flag. |
| useritems | engine (menus) | no |  | Print cvars carrying the item flag. |
| userweapons | engine (menus) | no |  | Print cvars carrying the weapon flag. |
| hackteam | engine (menus) | no |  | ?Copy team name for menus. |
| refresh | engine (menus) | no |  | ?Refresh the current menu. |
| return | engine (menus) | no |  | Return from a menu. |
| freshgame | engine (menus) | no |  | Start a new game (or the exit menu when in game). |
| changepass | engine (menus) | no |  | ?Menu password change. |
| checkpass | engine (menus) | no |  | ?Menu password check. |
| select | engine (menus) | no |  | ?Menu select. |
| menuoff | engine (menus) | no |  | Menus off. |
| killmenu | engine (menus) | no |  | Close all menus. |
| popmenu | engine (menus) | no |  | Close the top menu. |
| reloadall | engine (menus) | no |  | Reload menus. |
| intermission | engine (menus) | no |  | intermission <menu>: show an intermission menu. |
| menu | engine (menus) | no |  | menu <name>: open a menu. |
| viewpos | engine (view) | yes |  | Print the view position. |
| forcesong | loaded module (renderer or sound) | no |  | ?Force a music set. |
| soundinfo | loaded module (renderer or sound) | yes |  | Sound system info. |
| soundlist | loaded module (renderer or sound) | yes |  | List sounds. |
| stopsound | loaded module (renderer or sound) | yes |  | Stop all sounds. |
| play | loaded module (renderer or sound) | yes |  | Play a sound. |
| avi | loaded module (renderer or sound) | no |  | ?Frame capture. |
| gl_strings | loaded module (renderer or sound) | yes |  | Print OpenGL strings. |
| rawshot | loaded module (renderer or sound) | no |  | ?Save an unprocessed screenshot. |
| screenshot | loaded module (renderer or sound) | yes |  | Save a screenshot. |
| imagelist | loaded module (renderer or sound) | yes |  | List textures. |
| vid_modes | engine (video) | no |  | List video modes. |
| vid_front | engine (video) | yes |  | Bring the window to front. |
| vid_restart | engine (video) | yes |  | Restart the renderer. |
| conchars | engine (console) | no |  | Print displayable characters and colours. |
| condump | engine (console) | yes |  | condump <file>: write console text to a .txt file. |
| clear | engine (console) | yes |  | Clear console text. |
| messagemode2 | engine (console) | yes |  | Chat to team. |
| messagemode | engine (console) | yes |  | Chat to all. |
| togglechat | engine (console) | yes |  | ?Toggle chat display. |
| toggleconsole | engine (console) | yes |  | Toggle the console. |
| hackkillbots | engine (server) | no | yes | ?Remove all bots. |
| bot_rem | engine (server) | no | yes | Remove a bot. |
| bot_add | engine (server) | no | yes | Add a bot. |
| filter_write | engine (server) | no | yes | Write IP filters to listip.cfg. |
| filter_list | engine (server) | no | yes | List IP filters. |
| filter_remove | engine (server) | no | yes | Remove an IP filter. |
| filter_add | engine (server) | no | yes | Add an IP filter. |
| updateinven | engine (server) | no | yes | Ask the game module to refresh inventory cvars. |
| getgamestats | engine (server) | no | yes | Copy end-of-mission statistics into stat_* cvars. |
| killserver | engine (server) | yes | yes | Shut down the local server. |
| wipe | engine (server) | no | yes | wipe <name>: delete a save (not 'auto'). |
| load | engine (server) | yes | yes | load <name>: load <user>/save/<name>.sof. |
| quickload | engine (server) | no | yes | Load the newest existing quick save, else 'auto'. |
| quicksave | engine (server) | no | yes | Advance sv_quicksave_index and save quick<N>. |
| save | engine (server) | yes | yes | save <name>: write <user>/save/<name>.sof. |
| serverstop | engine (server) | yes | yes | Stop a server demo recording. |
| serverrecord | engine (server) | yes | yes | serverrecord <name>: record a server demo. |
| unset_dmflags | engine (server) | no | yes | Clear deathmatch flags by name. |
| set_dmflags | engine (server) | no | yes | Set deathmatch flags by name. |
| list_dmflags | engine (server) | no | yes | List deathmatch flags. |
| gamemap | engine (server) | yes | yes | gamemap <name>: change level keeping game state. |
| demomap | engine (server) | yes | yes | demomap <file>: play a demo ('.dm2' appended). |
| gserver | engine (server) | no | yes | ?Pass arguments to the GameSpy server module. |
| savesleft | engine (server) | no | yes | Print remaining saves (answered by the game module). |
| newsave | engine (server) | no | yes | Save into the first free saveNN slot. |
| map | engine (server) | yes | yes | map <name>: new game on maps/<name>.bsp; single player writes the 'auto' save. |
| dumpuser | engine (server) | yes | yes | dumpuser <userid>: print userinfo. |
| serverinfo | engine (server) | yes | yes | Print serverinfo cvars. |
| status | engine (server) | yes | yes | Server status. |
| kick | engine (server) | yes | yes | kick <userid>. |
| heartbeat | engine (server) | yes | yes | Send a master-server heartbeat now. |
| adddownload | engine (server) | no | yes | ?adddownload <file>: add a file to the server download list. |
| gsc_setusermode | engine (GameSpy chat) | no | yes | GameSpy chat: set user mode. |
| gsc_ban | engine (GameSpy chat) | no | yes | GameSpy chat: ban. |
| gsc_kick | engine (GameSpy chat) | no | yes | GameSpy chat: kick. |
| gsc_gettopic | engine (GameSpy chat) | no | yes | GameSpy chat: get topic. |
| gsc_settopic | engine (GameSpy chat) | no | yes | GameSpy chat: set topic. |
| gsc_getusers | engine (GameSpy chat) | no | yes | GameSpy chat: list users. |
| gsc_private | engine (GameSpy chat) | no | yes | GameSpy chat: private message. |
| gsc_talk | engine (GameSpy chat) | no | yes | GameSpy chat: say. |
| gsc_part | engine (GameSpy chat) | no | yes | GameSpy chat: leave channel. |
| gsc_join | engine (GameSpy chat) | no | yes | GameSpy chat: join channel. |
| gsc_getchannels | engine (GameSpy chat) | no | yes | GameSpy chat: list channels. |
| gsc_disconnect | engine (GameSpy chat) | no | yes | GameSpy chat: disconnect. |
| gsc_connect | engine (GameSpy chat) | no | yes | GameSpy chat: connect. |
| ip_list | engine (network) | no | yes | List local IP addresses. |
| winstart | engine (system) | no | yes | winstart <cmd>: run a system command after the program exits. |
| minimise | engine (system) | no | yes | Minimise the window (unless win_noalttab). |
| disconnect | engine (common) | yes | yes | Leave the server and shut down a local server. |
| subliminal | engine (common) | no | yes | Prints a joke warning only. |
| error | engine (common) | yes | yes | error <text>: raise a fatal error. |
| z_stats | engine (common) | yes | yes | Memory allocator statistics. |
| dir | engine (filesystem) | yes | yes | dir [wildcard]: list matching files and folders in all search paths. |
| link | engine (filesystem) | yes | yes | link <from> <to>: redirect file names starting with <from>; empty <to> removes. |
| path | engine (filesystem) | yes | yes | Print the search path and links. |
| bindlist | engine (keys) | yes | yes | List bindings. |
| unbindall | engine (keys) | yes | yes | Remove all bindings. |
| unbind | engine (keys) | yes | yes | Remove a binding. |
| bind | engine (keys) | yes | yes | Bind a key. |
| cvarlist | engine (cvars) | yes | yes | List cvars with flag columns. |
| zero | engine (cvars) | no | yes | zero <cvar>: set to the empty string. |
| sset | engine (cvars) | no | yes | sset <cvar> <words...>: set to the remaining words joined by spaces. |
| set | engine (cvars) | yes | yes | set <cvar> <value> [u\|s\|w\|i\|a\|m]. |
| xor | engine (commands) | no | yes | xor <cvar> <n>: integer bitwise XOR. |
| and | engine (commands) | no | yes | and <cvar> <n>: integer bitwise AND. |
| or | engine (commands) | no | yes | or <cvar> <n>: integer bitwise OR. |
| add | engine | no | yes | add <cvar> <n>: cvar = cvar + n. |
| cpuid | engine | no | yes | Print CPU and memory info; sets cpu_speed*, cpu_memory*. |
| wait | engine | yes | yes | Stop executing the command buffer until the next frame. |
| alias | engine | yes | yes | alias <name> <text>: define; no arguments lists aliases. |
| echo | engine | yes | yes | Print arguments. |
| exec | engine | yes | yes | exec <file>: run a script ('.cfg' appended when missing). |
| cmdlist | engine | yes | yes | List commands. |

## Commands not present in Quake II 3.19 (132)
`ff_spring`, `ff_tension`, `ff_usejoy`, `ff_usemouse`, `fx_load`, `fx_save`, `sv_precache`, `itemdrop`, `itemprev`, `itemnext`, `itemuse`, `weaponbestunsafe`, `weaponbestsafe`, `weaponlose`, `weapondrop`, `weaponselect`, `reload`, `go`, `listpickups`, `turnonmissionmsg`, `turnoffmissionmsg`, `givemoretutorial`, `givesnipertutorial`, `entlist`, `playerlist`, `teamview`, `gimme`, `putaway`, `killallmonsters`, `phantom`, `ninja`, `heretic`, `updateinvfinal`, `defaultweapons`, `bigelbow`, `elbow`, `playlevel`, `destroyents`, `loadpoint`, `help`, `score`, `players`, `listignore`, `unignore`, `ignore`, `remfave`, `addfave`, `savekeys`, `saveconfig`, `cl_touch`, `reset_predn`, `exit`, `maxfpsfromrate`, `ping`, `serverstatus`, `-weaponExtra2`, `+weaponExtra2`, `-weaponExtra1`, `+weaponExtra1`, `-altattack`, `+altattack`, `-leanright`, `+leanright`, `-leanleft`, `+leanleft`, `terrain`, `cloud`, `usermisc`, `userammo`, `useritems`, `userweapons`, `hackteam`, `refresh`, `return`, `freshgame`, `changepass`, `checkpass`, `select`, `menuoff`, `killmenu`, `popmenu`, `reloadall`, `intermission`, `menu`, `forcesong`, `avi`, `rawshot`, `vid_modes`, `conchars`, `hackkillbots`, `bot_rem`, `bot_add`, `filter_write`, `filter_list`, `filter_remove`, `filter_add`, `updateinven`, `getgamestats`, `wipe`, `quickload`, `quicksave`, `unset_dmflags`, `set_dmflags`, `list_dmflags`, `gserver`, `savesleft`, `newsave`, `adddownload`, `gsc_setusermode`, `gsc_ban`, `gsc_kick`, `gsc_gettopic`, `gsc_settopic`, `gsc_getusers`, `gsc_private`, `gsc_talk`, `gsc_part`, `gsc_join`, `gsc_getchannels`, `gsc_disconnect`, `gsc_connect`, `ip_list`, `winstart`, `minimise`, `subliminal`, `zero`, `sset`, `xor`, `and`, `or`, `add`, `cpuid`

Quake II commands absent from a 1.07f client `cmdlist`: `cd` (registered only when CD audio initialises),
`download`, `drop`, `force_centerview`, `give`, `god`, `gun_model`, `gun_next`, `gun_prev`, `impulse`, `invdrop`,
`inven`, `invnext`, `invprev`, `invuse`, all `menu_*` Quake II menu commands, `modellist`, `noclip`, `notarget`,
`packet`, `prog`, `setmaster`, `sv`, `use`. (Game-side cheats may still exist inside the game module's own
command parser; out of scope here.)
