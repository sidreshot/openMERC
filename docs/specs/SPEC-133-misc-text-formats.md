# SPEC-133 — Small text formats: `.cfg`, `.lst`, `.bpf`, `.txt`, `.gpl`

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — 64 `.cfg`, 7 `.lst`, 34 `.bpf`, 18 `.txt`, 4 `.gpl` in `pak0`, `pak2`, `pak3`. No game binary was consulted.
Implementation: `tools/formats/misc.py` (uses `tools/formats/vfs.py`)
Related: SPEC-130 (menus exec these scripts and read these lists), SPEC-131

Public reference used: Quake II 3.19 GPL source (`qcommon/cmd.c` `Cbuf_Execute`, `Cmd_TokenizeString`,
`Cmd_MacroExpandString`; `game/q_shared.c` `COM_Parse`; `qcommon/cvar.c` `Cvar_Set_f`) for the console
script rules. The bot files ship with their author's documentation (`pak2/bot/README.TXT`, hidebot 1.0 by
Rich Whitehouse), which is game data.

## `.cfg` — console scripts

### Layout

Plain text, CR LF. Same rules as Quake II console text **[data: every stock file tokenises cleanly under them]**:
a command ends at a newline or at `;` outside double quotes; tokens are separated by whitespace (TAB is common
as padding: `set gl_driver<TAB><TAB>"opengl32"`); `"…"` makes one token (no escapes); `//` where a token would start begins a comment to the
end of the line (also after a command: `bot_waypoints 1 //Enable waypoints.`; inside a word, as in
`http://…`, it is ordinary text); `$name` is replaced by the cvar's
value when the command runs. SoF scripts also use `#name` (`set skin #menu_temp_skin`, SPEC-130) **[hyp: value
as one quoted token]**.

### Inventory (64 files)

| Group | Files | Content |
|---|---|---|
| root | `default.cfg`, `default_video.cfg`, `default_sound.cfg`, `default_misc.cfg`, `dedicated.cfg`, `CONFIG.CFG` (empty, 2 bytes) | defaults; `default.cfg` execs the other three + `configs/default_keys.cfg` and sets `name "John Mullins"`, `dmflags 4100` |
| `configs/` | `DEFAULT_KEYS`, `CLAVIER_FR`, `KEYPAD`, `OLDSCHOOL`, `WADS` | `unbindall` + 74–88 `bind key "command"`; listed in the keys menu (`<list "" files "configs" "*" "cfg" … "unbindall;exec configs/$menu_config">`) |
| `drivers/` | 34 (pak0 30 + pak2 4: `alldefs`, `highest`, `lowest`, per-card `geforce`, `voodoo3`, `rage128`, `radeon` …, `cpu2–4`, `memory1–3`; 3 empty: `gdi`, `generic`, `memory3`) | graphics/sound scalability presets (`gl_*`, `ghl_*`, `s_khz`, `cl_max_quads` …); `memory1` uses `add` |
| `menus/` | 18 (pak0 17 + pak2 `startdm`: `video`, `audio`, `player`, `misc`, `keys`, `lock`, `joystick`, `apply*`, `defsnd`, `newgame`, `reset`, `outfit`, `testoutfit`, `startdm`, `tutorial`) | menu glue: copy real cvars into `menu_*` shadows on open, copy back on apply, start games (`map tsr1`, `map $menu_map`) |
| `bot/mapcfgs/default.cfg` | 1 | per-map bot settings (`bot_waypoints 1` …); README: `mapcfgs/<map>.cfg` is executed when that map loads, else `default.cfg` |

Commands used: `set` 731, `bind` 397, `exec` 8, `map` 5, `unbindall` 5, `alias` 5, `add` 3, `killserver`,
`menuoff`, `loading`, `disconnect` (2 each), `bot_waypoints`, `bot_wppref`, `bot_hunt`, `bot_runtact`, `echo`,
`hackkillbots`, `joy_advancedupdate`, `savekeys`, `menu`, `skill`, `deathmatch` (1 each; the last two are cvar
names used as commands, Quake II style). 208 distinct cvars are set; full table in
`openmerc-data/ui/misc/cfg_inventory.json` (cvar → file → value).

### Semantics

- `set name value [flag]`: Quake II accepts only `u`/`s` as the 4th token. The stock outfit scripts use
  `w` (12 weapon cvars: `knife` … `mpg`), `a` (9 ammo cvars: `aknife`, `a44`, `a9mm`, `shells`, `a556`,
  `rockets`, `battery`, `fuel`, `slug`), `i` (6 item cvars: `fpak`, `c4`, `goggles`, `grenade`, `armor`,
  `medkit`) and `m` (`health`) **[data: grouping]**; what the flag does is UNKNOWN.
- `dmflags` bit meanings are documented in `dedicated.cfg` comments (bits 0–14: no health, no items, weapon
  stay, no falling damage, realistic damage, same map, no reload, teamplay, no friendly fire, force respawn,
  no armor, infinite ammo, spinning weapon pickups, bullet-only weapons, CTF force join) and agree with the
  menu `bitmask` numbers (SPEC-130) and with the default 4100 = bits 2 + 12 **[data]**.
- `exec` targets: 8 uses, all exist. `map`/`gamemap` targets that are literals exist (`tsr1`).

## `.lst` — server map lists (`maplists/*.lst`)

One entry per non-blank line: `<map>` (path under `maps/`, without `.bsp`), whitespace (TABs), then
`"description"`. Blank lines are ignored.

| File | Entries |
|---|---|
| pak0 `RavDM` / pak3 `ravdm` (adds 5 Platinum maps) | 13 / 18 |
| pak0 `RavCTF`, `RavRL`, `None` (empty) | 7, 8, 0 |
| pak2 `RavCTB`, `RavCON` | 11, 7 |

The list is chosen by `sv_maplistfile` = file name without extension (`dedicated.cfg`: `"RavDM"`; menu
`<list "" files "maplists" "*" "lst" cvar sv_maplistfile>`) **[data]**. The description format
`"Name: N to M players"` is free text. **All 64 entries name an existing `maps/<map>.bsp`.**

## `.bpf` — bot personality files (`bot/bots/*.bpf`, pak2)

Line-based. `#` starts a comment line; blank lines ignored; `key=value` lines; chat sections
`[SECT=n]` … `[SECTEND=n]` for n = 1…6, each line inside a section is one chat line.

| Key | Type | Stock values | Documented range / meaning (file comments, README) |
|---|---|---|---|
| `userinfo` | `\`-separated pairs | always `name\<Name>\skin\<model>` | player userinfo; skin = `ghoul/pmodels/<skin>.gpm` (34/34 exist) |
| `accuracy` | int | 10–120 | "should range from 1 to 180" |
| `fov` | int | 85, 95 (31), 100, 105 | same range as the player's |
| `turning_speed` | int | 18–40 | degrees turned toward the destination per frame |
| `view_distance` | int | 4096 (33), 3396 | distance-vision limit |
| `general_intelligence` | int | 1–4 | decision quality |
| `canchat` | int | 0 (34/34) | bot may chat |
| `chatfreq` | int | 35, 50 (4 files) | 1 = hardly ever … 100 = constantly |

Sections (README / comments): 1 you-killed-me, 2 I-killed-you, 3 idle, 4 responses to idle chat, 5 replies of a
bot this bot killed, 6 replies to its killer; in a line `%` = the other bot's name, `$` = same in capitals,
`&` = same in lower case. **All stock sections are empty** (4 files declare all six, 30 declare none).
`bot/botlist.txt` lists personality names (32, one per line; all have a `.bpf`). The menu's bot table
(`<bot … headings "Name,Accuracy,FOV,Turn Speed,View,Intelligence,Can Chat">`) shows these keys.

## `.txt`

| Files | Format | Stock check |
|---|---|---|
| `pics/sky/*.txt` (13) | exactly 6 lines, each a texture path without extension or `none` | 10 files: 5 faces + `none`; 2 files: 6 faces; `none.txt`: all `none`. All 62 named faces exist as `.m32` |
| `drivers/cards.txt` (pak0 22, pak2 25 lines) | `<cfg> <substring>[,<substring>…]` | maps a graphics card to `drivers/<cfg>.cfg`; 1 missing: `permedia` (both copies) |
| `drivers/drivers.txt` | one GL driver name per line (`opengl`, `opengl32`, `3dfxvgl`, `3dfxogl`) | — |
| `bot/botlist.txt` | one bot name per line | 32/32 `.bpf` exist |
| `bot/README.TXT` | free text documentation | not parsed |

Semantics **[hyp]**: sky face order follows the Quake II suffix order (rt, bk, lf, ft, up, dn) — the files
alternate two side textures and end with up / down (`bos_side2, bos_side1, bos_side2, bos_side1, bos_up, none`);
`none` = face not drawn. `cards.txt`: the first line whose substrings all occur in the GL renderer/vendor string
selects the `.cfg` (lower-case substrings such as `riva,tnt2`, `matrox,g400`).

## `.gpl` — player model lists (`ghoul/pmodels/*.gpl`)

One model name per line (`ghoul/pmodels/<name>.gpm`); blank lines separate groups. In `everybody.gpl` the
groups are the teams **[data]**: each 6-model group's `.gpm` files carry one team name (`"The Order"`,
`"Ministry of Sin"`, `"The December Brigade"`, `"Meatwagon"` (one member with an empty team), `"Red Guard"`,
pak2 `"Capture, Inc"`), then a group of 20 team-less models (`"Noteam"` / empty). Files: pak0 `everybody`
(50 models), `ctf` (31: `mullins` + 5 teams), pak2 `everybody` (57), `individuals` (20). **1 dangling name: `keith`** (pak2
`everybody.gpl`; no `keith.gpm`, no `menus/players/keith.rmf`).

## Validation

`python tools/formats/misc.py validate --out <dir>`:

- **127 files parsed, 0 errors**: 64 `.cfg`, 7 `.lst`, 34 `.bpf`, 18 `.txt`, 4 `.gpl`.
- References: exec targets 8/8, map-list maps 64/64, bot skins 34/34, botlist 32/32, sky faces 62/62,
  gpl models 157/158 (`keith` missing), cards → driver cfgs 45/47 lines (`permedia` missing, in both copies).
- Output: `openmerc-data/ui/misc/misc_dump.json` (parsed content of every file), `cfg_inventory.json`
  (commands, cvars set, binds), `misc_validation.json`; console report `openmerc-data/ui/misc_validation.txt`.

## Open questions (with black-box checks)

1. Meaning of the `set` 4th-token flags `w`/`a`/`i`/`m`. Check: in the original console, `set knife 1 w` vs
   `set knife 1`, then `cvarlist knife` (flag column) and start a mission (outfit applied?).
2. Order and exact matching rule of `drivers/cards.txt`, and when `cpu*`/`memory*`/`highest`/`lowest` are
   executed (no stock file references them). Check: first start with a fresh `config.cfg` and `+set developer 1`,
   watch which configs are executed.
3. Sky face order. Check: load a map using `bos.txt` and look at which side texture faces north.
4. `#cvar` substitution in `.cfg` (`set skin #menu_temp_skin` with a skin name containing spaces).
5. `.gpl` use: which list feeds which menu/game mode (`ctf.gpl` starts with `mullins` before the teams).
