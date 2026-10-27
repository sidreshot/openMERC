# SPEC-313: Engine miscellany — string-package runtime, parental locks, server admin commands, map loading limits, world linking, traces, spectator movement, randomness, console completion, window glue
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f
Depends on: SPEC-001, SPEC-002, SPEC-101, SPEC-131, SPEC-201, SPEC-202, SPEC-203, SPEC-300, SPEC-301, SPEC-302, SPEC-400, SPEC-760, SPEC-801

## Purpose
Collects the engine behaviour that differs from Quake II 3.19 and is not yet covered by the subsystem specs: the
runtime of the localised string packages (the file format is SPEC-131), the parental violence locks, the server
administration commands added by Raven (IP filter, deathmatch flags, bots, map list), the per-lump limits of map
loading, the texture-information table and damageable textures, the SoF-specific parts of linking entities into the
world and of traces, the spectator movement mode, the engine random-number functions, console tab completion and the
window-system glue. It also answers several open questions of SPEC-001, SPEC-131 and SPEC-760 (listed at the end).

## Interface / layout

### String-package keywords (case-insensitive)
`VERSION`, `ID`, `REFERENCE`, `DESCRIPTION`, `COUNT`, `FLAGS`, `INDEX`, `{`, `}`, `NOTES`, `TEXT_ENGLISH`,
`TEXT_GERMAN`, `TEXT_FRENCH`, `TEXT_BRITISH`, and the flag names below. Any other first word on a line is "unknown".

| Flag name | Value |
|---|---|
| SP_FLAG_CENTERED | 0x01 |
| SP_FLAG_TYPEAMATIC | 0x02 |
| SP_FLAG_CAPTIONED | 0x04 |
| SP_FLAG_CREDIT | 0x08 |
| SP_FLAG_LAYOUT | 0x10 |
| SP_FLAG_ORIGINAL | 0x20 |
| SP_FLAG_ALWAYS_PRINT | 0x40 |
| SP_FLAG_BLINK | 0x80 |

| Text keyword | `sp_language` value |
|---|---|
| TEXT_ENGLISH | 0 |
| TEXT_GERMAN | 1 |
| TEXT_FRENCH | 2 |
| TEXT_BRITISH | 3 |

### Colour tokens inside package texts (replaced by one control byte when the package is loaded)
| Token | Byte | Token | Byte | Token | Byte |
|---|---|---|---|---|---|
| `$P_WHITE` | 0x01 | `$P_HWHITE` | 0x09 | `$P_DPURPLE` | 0x16 |
| `$P_RED` | 0x02 | `$P_HRED` | 0x0B | `$P_DGREY` | 0x17 |
| `$P_GREEN` | 0x03 | `$P_HGREEN` | 0x0C | `$P_PINK` | 0x18 |
| `$P_YELLOW` | 0x04 | `$P_HYELLOW` | 0x0E | `$P_BLOODRED` | 0x19 |
| `$P_BLUE` | 0x05 | `$P_HBLUE` | 0x0F | `$P_RUSSET` | 0x1A |
| `$P_PURPLE` | 0x06 | `$P_CAMOBROWN` | 0x10 | `$P_BROWN` | 0x1B |
| `$P_CYAN` | 0x07 | `$P_CAMOGREEN` | 0x11 | `$P_TEXT` | 0x1C |
| `$P_BLACK` | 0x08 | `$P_SEAGREEN` | 0x12 | `$P_BAIGE` | 0x1D |
| | | `$P_SEABLUE` | 0x13 | `$P_LBROWN` | 0x1E |
| | | `$P_METAL` | 0x14 | `$P_ORANGE` | 0x1F |
| | | `$P_DBLUE` | 0x15 | | |
The bytes are the colour codes of the console/HUD text renderer (SPEC-002 colour table). Bytes 0x0A and 0x0D have no
token.

### Package owners
Each loaded package carries a set of owner bits; a package is unloaded when its last owner releases it.
| Bit | Owner | Loaded by | Released when |
|---|---|---|---|
| 1 | client | packages named by configstrings 1464–1492 (SPEC-203), when the configstring arrives and again when the level's media are registered | the client clears its state, disconnects or shuts down |
| 2 | server | the game's "register string package" import (SPEC-001 #47; also announces the package in a configstring) and the package list of a loaded savegame (SPEC-303) | the server shuts down or a new map is started with `map`/`gamemap` |
| 4 | menus | `<strings name>` in menu pages (SPEC-130) | the menu system is closed (`menuoff`, `killmenu`, last `popmenu`/`return`, menu shutdown) |
| 8 | engine | `items`, `weapons`, `general`, `dm_generic` at client start; `str_server` at server start | program shutdown |

### Parental-lock cvars
| Cvar | Flags (SPEC-302) | Bit in `sv_violence` / `cl_violence` |
|---|---|---|
| `lock_deaths` | hidden, protected | 0x01 |
| `lock_blood` | hidden, protected | 0x02 |
| `lock_gorezones` | hidden, protected | 0x04 |
| `lock_sever` | hidden, protected | 0x08 |
| `lock_textures` | hidden, protected | 0x10 |
| `lock_pure` | hidden, protected | 0x20 (`cl_violence` only) |
| `lock_password` | hidden, protected | — |
A lock value other than 0 sets its bit; a set bit means "this kind of violence is suppressed".

### Map lumps checked when a map is loaded for collision
| Lump | Name in error texts | Minimum count | Maximum count | Bytes per element on disk |
|---|---|---|---|---|
| 0 | LUMP_ENTITIES | 0 | 262144 | 1 |
| 1 | LUMP_PLANES | 1 | 65536 | 20 |
| 2 | LUMP_VERTEXES | 0 | 4194304 | 12 |
| 3 | LUMP_VISIBILITY | 0 | 1572864 | 1 |
| 4 | LUMP_NODES | 1 | 65536 | 28 |
| 5 | LUMP_TEXINFO | 1 | 8192 | 76 |
| 6 | LUMP_FACES | 0 | 4194304 | 44 |
| 7 | LUMP_LIGHTING | 0 | 4194304 | 1 |
| 8 | LUMP_LEAFS | 1 | 65536 | 32 |
| 9 | LUMP_LEAFFACES | 0 | 4194304 | 2 |
| 10 | LUMP_LEAFBRUSHES | 1 | 65536 | 2 |
| 11 | LUMP_EDGES | 0 | 4194304 | 4 |
| 12 | LUMP_SURFEDGES | 1 | 256000 | 4 |
| 13 | LUMP_MODELS | 1 | 1024 | 48 |
| 14 | LUMP_BRUSHES | 0 | 8192 | 12 |
| 15 | LUMP_BRUSHSIDES | 0 | 65536 | 4 |
| 16 | LUMP_POP | 0 | 4194304 | 1 |
| 17 | LUMP_AREAS | 0 | 256 | 8 |
| 18 | LUMP_AREAPORTALS | 0 | 1024 | 8 |
| 19 | LUMP_REGIONFACES | 0 | 4194304 | 8 |
| 20 | LUMP_LIGHTS | 0 | 4194304 | 36 |
| 21 | LUMP_REGIONS | 0 | 4194304 | 76 |
Lumps 19–21 are SPEC-101's facepolys, lights and volumes; "region" is the engine's own name for a volume.

### Trace result (56 bytes, used by the game imports SPEC-001 #52/#53 and by the engine)
| Offset | Size | Field |
|---|---|---|
| 0 | 1 | all solid (byte, 0/1) |
| 1 | 1 | start solid (byte, 0/1) |
| 2 | 2 | padding |
| 4 | 4 | fraction (float) |
| 8 | 12 | end position |
| 20 | 20 | hit plane: normal (3 floats), distance (float), type (byte), sign bits (byte), 2 padding bytes |
| 40 | 4 | hit surface (texture-information record, see Behaviour 5; a shared empty record when nothing was hit) |
| 44 | 4 | contents of the hit brush |
| 48 | 4 | hit entity |
| 52 | 4 | index of the last BSP leaf with faces that the sweep passed through; −1 when none |

## Behaviour

### 1. String packages at run time
**Loading a package `<name>`.** The engine first tries `strip/<name>.spe`, then `strip/<name>.sp`, through the file
system (SPEC-300). If neither exists the level is dropped with `Could not open string package '<name>'`. The file is
parsed line by line:
- A line is read up to its line feed. Its first word (up to space, CR or LF) is the keyword. The value starts after
  the following spaces. A value that starts with `"` runs to the first `"` that is directly followed by CR, LF or the
  end of the text (so inner quotes are kept); inside it the two characters `\n` become a line feed. An unquoted value
  runs to the end of the line. Lines are limited to 8192 bytes.
- The first line must be `VERSION 1`; otherwise the package stays empty.
- Header keywords: `ID` (package number, low 8 bits of the decimal value), `REFERENCE` (package name), `COUNT n`.
  Other known keywords (`DESCRIPTION`, …) are ignored; an unknown keyword or the end of the file ends the parse.
  After `COUNT n` exactly n entries are read; each must start with an `INDEX i` line followed by a `{` line, and ends
  at its `}` line. Entry i is stored in slot i (the original does not check i < 256; OpenMerc should reject larger
  values). A missing `INDEX` line or an unknown keyword inside an entry ends the parse early; whatever was read until
  then stays loaded and no error is shown. A package without `ID` gets ID 0.
- Inside an entry: `REFERENCE` sets the entry name; `FLAGS` takes any number of flag names separated by spaces (an
  unknown word ends the flag list); `NOTES` is ignored; text keywords are handled by the language rule below.
- **Language rule:** let L = `sp_language` (values outside 0–3 count as 0). The entry's text is the text of the line
  whose keyword matches L; if the entry has none, the `TEXT_ENGLISH` text. (Precisely: an English line is taken when
  no text is stored yet or when L = 0; any other language line is taken when it matches L.)
- When a text is stored, every colour token of the table above is replaced by its byte (tokens are matched as
  prefixes in table order, so `$P_REDSUICIDE` becomes byte 0x02 followed by `SUICIDE`). When cvar `sv_show_strip`
  is non-zero at load time, every stored text is prefixed with `STRIP-`. Bytes are otherwise copied unchanged (no
  code-page conversion).
- After parsing, the package is filed under its ID. If a package with the same ID is already loaded, the new copy is
  discarded and the loaded one gains the new owner bit (the first loaded copy wins).
- Developer print on success: `SP_Register: Registered client string package '<name>' with ID <id in 2 hex digits>`.

**Entry names.** Entry names are kept per package in a case-sensitive dictionary. When an entry name occurs twice
in one package, the **later** entry (higher position in the file) wins.

**Lookup by number.** A string id is (package ID << 8) | entry slot (SPEC-201 §34). A package ID that is not loaded
drops the level with `String package not registered for ID <id as 4 hex digits>`. An empty slot yields "".

**Lookup by name** (`^NAME^` references in menus, localised key names). The loaded packages are tried in
ascending ID order. A package matches when the reference is at least two characters longer than the package name
and starts with it (case-sensitive); the entry name is the rest of the reference after skipping one more character
(normally `_`; the character is not checked). The first package that has that entry name gives the text. No match →
"" (nothing is printed).

**Changing `sp_language`** (cvar flags archive + integer, default 0). Every loaded package is reloaded immediately
with its owner bits, so menus and HUD texts switch without a restart. Then, when the new value is 1 (German), all
six lock cvars are set to 1 (through the normal change handling of Behaviour 2, so the store is updated too), the
violence cvars are recomputed and cvar `low_violence` is set to 1.

### 2. Parental violence locks
1. At start the engine creates the seven lock cvars (locks default 1, password ""), `sv_violence` (serverinfo,
   latched, default 0) and `cl_violence` (userinfo, hidden, default 0).
2. **German systems.** If the system locale reports country code 49, country name `Germany`, or language name
   `German`, cvar `non_multilingual` is set to 1 and the locks are forced (step 4).
3. **Stored settings.** The password and the six lock values are read from the parental-lock store (Appendix A). If
   the password or any of the first five locks is missing or invalid, the locks are forced; a missing `lock_pure`
   is not an error.
4. **Forced locks**: all six locks become 1 and every lock cvar (and the password) becomes immutable for this run: any
   later change is immediately set back to 1. Otherwise a change of a lock cvar is written to the store and the
   violence cvars are recomputed; a change of `lock_password` is written to the store.
5. **Recomputation**: `cl_violence` = the bits of all six locks; `sv_violence` = the bits of the first five, updated only
   while no server is running. Both are force-set (they cannot be changed from the console).
6. The menus compare a typed password with `lock_password` (`checkpass`, `changepass`, SPEC-130); the server
   browser hides servers whose flags lack a `cl_violence` bit (SPEC-803).

### 3. Server administration commands
**IP filter** (Quake II's game-side `addip` family moved into the engine; at most 1024 entries):
- `filter_add <a.b.c.d>`: a missing trailing part or a part 0 is a wildcard (mask byte 0), any other part must
  match exactly. A first character that is not a digit is taken as a **player name**: the connected client with
  exactly that name (case-sensitive) is looked up and its userinfo `ip` value is used; unknown name → `Name <name>
  not found in player list`. No argument → `Usage:  filter_add X\nWhere x can be <ip-mask> or <id>\n`. A full list
  → `Filter list is full`. A free slot (left by a failed add) is reused before the list grows. A part that does not
  start with a digit → `Bad filter address: <text>`.
  - Quirk (UNVERIFIED in game): in the player-name form the original reuses the entry number for its scan of the
    client slots. A found player's address is written into the entry whose number equals that player's slot; for an
    unknown name the entry numbered `maxclients` is marked free. In both cases the entry that was reserved for the
    add keeps its previous contents — when the list had to grow, that is an all-zero mask and value, which matches
    every address, so with `filterban 1` everybody is refused until `filter_remove 0.0.0.0` (mask and value both
    zero) removes it. Check: on a fresh server type `filter_add NoSuchName`, then try to connect.
- `filter_remove <a.b.c.d>`: removes the first entry with exactly that mask and value (`Removed.`), otherwise
  `Didn't find <text>.`; no argument → `Usage:  Filter_remove <ip-mask>\n`.
- `filter_list`: `Filter list:` then one line per entry, the four value bytes as `%3i.%3i.%3i.%3i`.
- `filter_write`: writes `<game>/listip.cfg` relative to the working directory, where `<game>` is the `game` cvar
  or, when empty, the literal folder name `SOF RETAIL`; prints `Writing <path>.`. Content: `set filterban <n>` then
  one `Filter_add a.b.c.d` line per entry. If the file cannot be created: `Couldn't open <path>`.
- Incoming connections: an address matching an entry is refused when `filterban` is 1 and accepted when 0; a
  non-matching address the other way round (as in Quake II).

**Deathmatch flags** (`list_dmflags`, `set_dmflags n …`, `unset_dmflags n …`). All three print string 0x050C
(`Deathmatch is not set`) when `deathmatch` is 0. The flag table is the text of string-package entry
(5, deathmatch − 1) — package 5 is `SV_TEXT` (`strip/str_server.sp`). The text ends with `*N`, where N is the number of
flags the mode has. `list_dmflags` fills the text's `%s` placeholders with `1`/`0` for dmflags bits 0 … N−1 and prints
the text up to the `*`. `set_dmflags` ORs bit (n−1) into `dmflags` for each argument n with 1 ≤ n ≤ N (others are
ignored) and `unset_dmflags` clears them; both then list the flags. Without arguments they print the usage strings
0x050A / 0x050B first and leave `dmflags` unchanged. (German texts of modes 6 and 7 end in `*0`, so no flag can be
changed there — a data quirk.)

**Bots** use the cvars `bot_name0` … `bot_name31`; a slot is free when its value starts with `???` (case-insensitive).
- `bot_add [count] [name]`: count defaults to `maxclients`. The first free slot among 0 … count−3 receives the name,
  or a random bot name from the bot list (SPEC-803 bots) when no name is given; prints `Added bot "<name>".`; no free
  slot → `No free slots to add bot.`.
- `bot_rem [count] [name]`: searches slots count−2 down to 0 (one slot more than `bot_add` uses) for the first used
  slot (or the slot whose value equals the name, case-insensitive), sets it to `??????` and prints
  `Removed bot "<name>".`; none → `No bots to remove.`.
- `hackkillbots`: unless `deathmatch` is 1 or 3, sets `bot_name0` … `bot_name29` to `??????`.

**`getgamestats`** without a running game module sets the ten cvars `stat_playedtime_base`, `stat_guyskilled`,
`stat_friendlieskilled`, `stat_throatshots`, `stat_nutshots`, `stat_headshots`, `stat_gibs`, `stat_savesused`,
`stat_playedtime`, `stat_cashearned` to "" (with a game module: SPEC-001).

**`gserver`** is not a GameSpy command: it upper-cases its arguments and passes them to the model system's debug
command interpreter (SPEC-010). This corrects SPEC-302's description.

**Map list.** Whenever the server starts a map with `deathmatch` ≠ 0 and `sv_maplistfile` (archive, default
`ravdm`) not empty, the engine reads `maplists/<sv_maplistfile>.lst` (missing file → drop with
`*************************\nCould not open <name>.lst\n*************************\n`). The file is read as pairs of
Quake II tokens (map, description). A pair whose description is empty is skipped; a map whose `maps/<map>.bsp` does
not exist prints `WARNING: Could not find map <map>.bsp` and is skipped. Every accepted map is appended as `,<map>`;
the result (at most 511 characters; reading stops before an entry that would exceed it) is stored in `sv_maplist`. The
game module reads `sv_maplist` for its rotation.

### 4. Texture-information table and damageable textures
- At every map load the engine empties a table of up to 500 texture records. Record 0 is an empty record. Each texture
  name met in the map's texinfo lump (and every damaged texture reached from it) is registered once, by name with
  `.m32` stripped: the header of `textures/<name>.m32` (SPEC-100) supplies the damaged-texture name, the damage value
  and the material id. Missing file or full table → record 0.
- A record's **health** = the integer part of the damage value (as a signed byte) when the damaged-texture name is
  not empty, else 0. Registering a record also registers its damaged texture (chains are followed).
- **Damage** (game import SPEC-001 #25, a surface from a trace plus an amount): ignored when the surface has no record,
  no damaged name or health 0. Otherwise the amount is added to the surface's accumulated damage; when the sum
  reaches the health, the surface switches to the damaged texture's record, the sum restarts at 0, the surface's
  damage stage increases by one, `svc_damagetexture` (surface index, stage; SPEC-201 §25) is queued for the clients,
  and the import returns true. With cvar `sv_showdamage` ≠ 0 developer prints trace each step (`Doing %d damage`,
  `Damaging %s`, `Changing to %s, level %d`, …).
- The client side uses the materials of all registered records to precache debris (SPEC-400).

### 5. Map loading for collision
- The map file must exist and hold at least the 184-byte header (`Failed to open map file`; the menus show string
  0x0416 through `menu_dc_reason`, disconnect error); the version must be 46
  (`Mod_LoadBrushModel: <name> has wrong version number (<n> should be 46)`, fatal).
- For every lump: length not a multiple of the element size → `CM_LoadLump: funny lump size for <LUMP_NAME>`; fewer
  elements than the minimum → `CM_LoadLump: lump <LUMP_NAME> too small`; more than the maximum →
  `CM_LoadLump: lump <LUMP_NAME> has too many entries (<n>)` (all drop the level). Each lump's MD4 block checksum is kept.
- Leaf 0 must be solid (`Map leaf 0 is not CONTENTS_SOLID`) and an empty leaf must exist (`Map does not have an empty
  leaf`), as in Quake II.
- Each leaf's `volume` index (SPEC-101) links it to a region record; an index outside the region lump means "none".

**Region visibility distance (game import SPEC-001 #55).** For the leaf containing the point: no region → −1.
Otherwise with density d (region float at byte 4) and distance D (region float at byte 16): d ≥ 1 → D;
0 < d < 1 → D / d; d ≤ 0 → −1. This is the fog visibility distance the game compares with 1200.

### 6. Linking entities into the world (differences from Quake II)
Offsets and flags refer to the entity layout of SPEC-760.
- **Rotated brush entities** (solid 3 or 4 with any angle non-zero and none of server flags 0x08/0x10/0x20): the
  absolute box is origin ± 1.75 · r on all axes, where r is the largest absolute component of mins and maxs (Quake II
  uses 1.0 · r).
- **Swinging doors** (solid 3 or 4, an angle non-zero, server flag 0x08, 0x10 or 0x20): the absolute box covers only
  the segment from the hinge (the origin) to the door's far edge in the rotation plane:
  | Flag | Rotation axis | In-plane axes (u, v) | Angle used |
  |---|---|---|---|
  | 0x08 | x | (y, z) | roll |
  | 0x10 | y | (x, z), v negated | pitch |
  | 0x20 (or none of the two above) | z | (x, y) | yaw |
  From the four candidates (mins.u, a), (mins.v, a + 90°), (maxs.u, a), (maxs.v, a + 90°), taken in this order, the
  one whose radius has the largest integer part in absolute value (a later one only wins when strictly larger) gives
  r and θ. Then du = r·cos θ, dv = r·sin θ (negated for flag 0x10). On u the box runs from the origin to origin + du
  (ordered so min ≤ max; a du ≤ 0 puts the far edge at the minimum), the same on v; on the rotation axis it is
  origin + mins … origin + maxs.
- After either rule, as in Quake II, the absolute box is enlarged by 1 unit on every side.
- An entity whose solid is 0 is still linked into the area tree when server flag 0x80 is set (Quake II never links
  non-solid entities). Such entities are found by box queries and are hit by the filtered trace below.
- Solid packing for the network is SPEC-202 §2.3 (solids 3 and 4 both send 31).

### 7. Traces (game imports SPEC-001 #52/#53)
- Field layout: see the table above.
- Filtered trace (#53): the world is traced first. A world fraction of 0 is returned at once (no entities are
  considered). If it is below 1, the entity sweep ends at the world hit point and the world result's fraction is replaced by **0.99** (so a world hit reports 0.99, not the true
  fraction, unless an entity is accepted). Entities are considered when solid ≠ 0 or server flag 0x80 is set, skipping
  the passed entity, its owner and entities it owns, and dead monsters unless the content mask has 0x4000000. Every
  entity hit with fraction < 1 is collected (at most 1024 candidates), the hits are sorted by ascending fraction
  (equal fractions in no defined order) and offered to the callback; the first accepted hit becomes the result.

### 8. Spectator movement (player-movement type 2)
Quake II's noclip flight is used for type 1 only. Type 2 (spectator) moves through entities but collides with world
brushes, using a box from (−5, −5, −2) to (5, 5, 8) and a world-only trace with content mask 0x02000003. The view
height is set to 5. Friction: speed < 1 → stop; otherwise speed −= max(speed, 100) · 6 · 1.5 · frametime (not below 0).
Acceleration: wish direction from the forward/right vectors (both normalised) times the forward/side moves plus the
up move on z; wish speed = min(|wish|, 300); add speed = min(wish speed − current speed along the wish direction,
10 · frametime · wish speed). When the add speed is ≤ 0 the function ends **without moving** in that frame, so a
spectator who releases all movement keys stops at once instead of coasting (UNVERIFIED in game). Otherwise the
velocity grows by the accel speed along the wish direction, the normal step-slide move runs with the small box, and
the original box and trace are restored. The rest of the player movement differences need their own spec.

### 9. Engine random numbers (exports `flrand`, `irand`; game imports #76/#77)
One shared 32-bit state s, seeded at start with the system millisecond timer. Each call first sets
s = s · 214013 + 2531011 (mod 2³²) and takes r = s >> 17 (0 … 32767).
- `flrand(min, max)` = min + (max − min) · r / 32768 — in [min, max).
- `irand(min, max)` = min + (((max − min + 1) · r) >> 15) (32-bit signed arithmetic) — in [min, max].

### 10. Console tab completion (cycling)
The first Tab completes the typed word (without a leading `/` or `\`) to the first command that matches it exactly
or starts with it, else to the first such cvar; the line becomes `/<match> ` (Quake II behaviour). Further Tabs, while
the line has not been edited, step to the next match after the previous one: first exact matches, then prefix
matches, in list order (commands, then aliases, then cvars), wrapping from the end of the command list to the cvars
and from the end of the cvars back to the commands. Hidden cvars (SPEC-302 flag 0x80) are never offered. List order
is the internal registration order (UNVERIFIED: most recently registered first, as in Quake II).

### 11. `conchars`
Prints `Displayable characters :`, a blank line, seven lines of 32 characters (codes 32 … 255) each preceded by a cyan
colour byte and `:`, then `Displayable colors :`, a blank line, and the colour bytes 1 … 31 each followed by `@`
(codes 10 and 13 replaced by spaces).

### 12. Window and system glue
- Alt+Enter toggles `vid_fullscreen`, unless `win_noaltenter` (default 1) is non-zero.
- While the window is active and `win_noalttab` (default 1) is non-zero, Alt+Tab and Alt+Enter are captured as system
  hot keys (NT) or the system's task switching is suppressed (Windows 9x); released on deactivation. Activation also
  clears all key states and (de)activates mouse input, CD audio, the sound module and the renderer.
- Moving the windowed window stores the client-area position in `vid_xpos`, `vid_ypos`.
- The screen saver (system command "screen save") is refused; the shell's AutoPlay is cancelled while the game runs;
  a window repaint request triggers a screen update (UNVERIFIED which).
- `vid_modes` prints `Mode 0: Unsupported`, `Mode 1: Unsupported`, `Mode 2: Unsupported`, `Mode 3: 640x480`,
  `Mode 4: 800x600`, `Mode 5: 960x720`, `Mode 6: 1024x768`, `Mode 7: 1152x864`, `Mode 8: 1280x960`,
  `Mode 9: 1600x1200` (modes 0–2 would be 320×240, 400×300, 512×384).
- Changing `r_force_sgi_software` writes its integer value as DWORD `OverrideDispatch` under
  `HKEY_LOCAL_MACHINE\Software\Silicon Graphics\OpenGL` (a legacy switch of the SGI OpenGL runtime). OpenMerc can ignore it.
- The A3D test of SPEC-301 §6 first (re)writes the A3D COM class registration under `HKEY_CLASSES_ROOT`
  (`A3d`, `A3dApi` and their CLSID keys, threading model "Apartment") and then creates the A3D object; informative only.

## Constants
| Name | Value |
|---|---|
| String-package line limit | 8192 bytes |
| Entry slots per package | 256 |
| Texture-information records | 500 (record 0 = empty) |
| IP filter entries | 1024 |
| Map list text limit | 511 characters |
| Rotated brush bounds factor | 1.75 |
| Filtered-trace world fraction substitute | 0.99 |
| Filtered-trace candidates | 1024 |
| Spectator box | (−5, −5, −2) – (5, 5, 8); view height 5 |
| Spectator move constants | stop speed 100, friction 6 × 1.5, max speed 300, acceleration 10 |
| Random generator | s = s·214013 + 2531011; r = s >> 17 |
| Bot slots used by commands | 0 … maxclients − 3 (add/remove), 0 … 29 (hackkillbots) |

## Test vectors / how to verify
1. Menu `^MENU_GENERIC_QUIT^` resolves to `EXIT GAME` (entry 42), not `Quit` (entry 15): open the quit menu.
2. `set sv_show_strip 1`, `set sp_language 0` (forces a reload): every menu text starts with `STRIP-`.
3. A package text `$P_GREENhello` prints `hello` in green in the console (byte 0x03).
4. `set sp_language 3` and open a menu whose entries have no `TEXT_BRITISH`: English text is shown.
5. Server with `deathmatch 1`, `dmflags 0`: `set_dmflags 1 3` → `dmflags` 5 and the Standard table shows `1` for
   flags 01 and 03; `set_dmflags 40` → unchanged.
6. `sv_maplistfile RavCTF`, `deathmatch 1`, `map dm/nycctf1` → `sv_maplist` =
   `,dm/nycctf1,dm/sibctf1,dm/irqctf1,dm/jpnctf1,dm/sudctf1,dm/sudctf2,dm/gerctf1`.
7. `filter_add 10.0`, `filter_list` → ` 10.  0.  0.  0`; `filter_write` with empty `game` creates
   `SOF RETAIL/listip.cfg` if that folder exists, else prints `Couldn't open SOF RETAIL/listip.cfg`.
8. On a fresh server: `bot_add 4 Fred` → `Added bot "Fred".`, `bot_name0` = Fred; `bot_add 4 Joe` → `bot_name1` = Joe;
   `bot_add 4` → `No free slots to add bot.`; `bot_rem 4` → removes `bot_name2` only if used, so here it removes Joe.
9. Random numbers: with s = 1 before the call, three `irand(0, 9)` give 0, 7, 0; with s = 12345, `flrand(0, 1)` gives
   0.6157 (s becomes 0x9DA03218).
10. Swinging door, origin (0,0,0), mins (0,−2,0), maxs (64,2,100), yaw 90, flag 0x20 → absolute box ≈ (−1,−1,−1) to
    (1, 65, 101).
11. Filtered trace from a point 100 units in front of a wall straight at it, with no entities: fraction 0.99
    (plain trace #52: about 1.0 minus the distance ratio).
12. Spectator (pm_type 2): fly into a closed door entity — passes through; fly into a wall — stops 5 units short.

## Open questions
- Whether `.spe` files exist in any shipped or community data (none in the retail paks); assumed to be an
  "edited" override of the same format.
- The meaning of `SP_FLAG_ORIGINAL` and `SP_FLAG_BLINK` at run time (no reader found in the engine besides SPEC-201
  print paths).
- In-game confirmation of the `filter_add <name>` quirk (Behaviour 3).
- Purpose of the trace's "last leaf with faces" field (game side).

## Answers to open questions of other specs
- SPEC-131 Q1: the complete `$P_` list is the colour-token table. Q3: the later duplicate wins
  (`MENU_GENERIC_QUIT` → `EXIT GAME`). Q5: missing language texts fall back to `TEXT_ENGLISH`. Q7: the engine does
  no code-page conversion; bytes go to the font as they are.
- SPEC-131 "language command runs `reloadall`": not needed — changing `sp_language` reloads the packages itself.
- SPEC-001 open question on import #55: it returns the fog visibility distance of Behaviour 5.
- SPEC-760 open question on rotating-door server flags 0x08/0x10/0x20: they select the swinging-door bounds of
  Behaviour 6 (axis x / y / z respectively).
- SPEC-302 corrections: `gserver` (Behaviour 3); `sv_show_strip` prefixes texts with `STRIP-` (it does not show
  identifiers); `sp_language` default is 0 (archive, integer); `sv_violence` bits set = violence suppressed.
- SPEC-005: "Collision BSP loading … which lumps and limits" — the lump table above.

## Appendix A: parental-lock store (optional; only needed to read settings written by the original)
- Location: `HKEY_CURRENT_USER\Software\Raven Software\SoF`. Value names: `Server` = password (binary),
  `Graphics` = lock_gorezones, `Sound` = lock_blood, `Input` = lock_deaths, `Networking` = lock_sever,
  `Performance` = lock_textures, `Optimizations` = lock_pure (DWORDs).
- Key material: seed = the volume serial number of the first fixed drive (letters A–Z in order) that reports one,
  else 0. A 16-bit generator g() sets seed = seed · 214013 + 2531011 (mod 2³²) and returns seed >> 16. Fourteen
  32-bit words are drawn as w = g() + 65536 · g(): for value numbers 0 … 5 a pair (off, on); then the password key K;
  then the pair for value number 6. (Value numbers: Server 0, Graphics 1, Sound 2, Input 3, Networking 4,
  Performance 5, Optimizations 6.)
- A lock DWORD equal to its "off" word means 0, equal to its "on" word means 1, anything else (or absent) is invalid.
  Writing stores the matching word.
- Password: the bytes of the password including its terminating zero (at most 32) are stored with byte i increased
  by byte (i mod 4) of K (little-endian), followed by a 4-byte little-endian check value c, where c starts at 0,
  c = c·2 + stored byte for each stored byte, then c XOR K. A stored value shorter than 5 bytes or with a wrong check
  value is invalid.
- The `-cs` command-line utility of SPEC-301 §9 prints K as 8 hex digits and writes the typed hex bytes as `Server`.
