# SPEC-303: Savegame files (.sof chunk container, engine part)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (container and engine-written chunks); gamex86.dll chunks only by name/size
Depends on: SPEC-300 (write directory), SPEC-302 (cvar flags)

## Purpose
Defines where single-player saves are stored, the chunk container format shared by the engine and the game
module, the exact chunks the engine writes and reads, the save/load commands, and the rules for automatic
and quick saves.

## Interface / layout

### Files
| File | Meaning |
|---|---|
| `<user>/save/<name>.sof` | A save; `<name>` as typed (no extension). |
| `<user>/save/current.sof` | Staging copy used while saving and loading; deleted afterwards and on every error. |
| `<user>/save/auto.sof` | Automatic save written when a single-player level starts. |
| `<user>/save/quick0.sof` .. `quick3.sof` | Rotating quick saves. |
| `<user>/save/save00.sof` .. `save99.sof` | Slots used by `newsave`. |
`<user>` is the user cvar value (SPEC-300), not the search path. One file holds the whole save (there is no
per-level directory as in Quake II).

### Chunk container
The file is a plain sequence of chunks, no file header, no trailer. Each chunk, little-endian:
| Offset | Size | Type | Field |
|---|---|---|---|
| 0 | 4 | uint32 | chunk id: four ASCII letters packed big-endian into the integer and stored little-endian, so the bytes on disk are the letters reversed (`COMM` is stored as `MMOC`) |
| 4 | 4 | int32 | data length N |
| 8 | 4 | uint32 | checksum of the data |
| 12 | N | bytes | data |
| 12+N | 4 | uint32 | end marker 0x1234ABCD (bytes `CD AB 34 12`) |

Checksum: the 16-byte MD4 digest (RFC 1320) of the N data bytes, read as four little-endian uint32 words,
XORed together. (Same function as Quake II's block checksum.) N = 0 is allowed.

Reading always requests a specific id; the reader fails the load with an error (the game is dropped) when:
the id differs (`Loaded chunk ID (<found>) does not match requested chunk ID (<wanted>)`), the requested
length is non-zero and differs (`Loaded chunk (<id>) has different length than requested`), the checksum
differs (`Failed checksum check for chunk`), the marker differs (`Bad savegame magic for chunk <id>`), or the
file ends early (`Error during loading chunk <id>`). A chunk may also be read with "any length", in which case
the engine allocates the buffer.

### Engine chunk sequence
In file order. "auto" = written only for automatic saves; "full" = only for normal saves.
| Id | Size | Written | Content |
|---|---|---|---|
| `COMM` | 64 | always | Comment, NUL-padded: `<time>/<skill>/<level>`, truncated to 63 characters. `<time>` = local time as C `asctime` without the trailing newline (e.g. `Fri Dec 19 03:36:13 2025`) for normal saves, or the word `ENTERING` for automatic saves; `<skill>` = name for the `skill` cvar value 0..5: `Effortless`, `Easy`, `Medium`, `Challenging`, `Unfair`, `Custom`; `<level>` = configstring 0 (level name, e.g. `KOS1`). |
| `SHOT` | 65536 | always | 128×128 thumbnail, 4 bytes per pixel (R, G, B, A=255 in reference files). Normal save: screenshot from the client (all zero if none). Automatic save: contents of `maps/<map>.raw` if that file is exactly 65536 bytes, otherwise all zero. |
| `MPCM` | 256 | always | Map command text (e.g. `kos1`), NUL-padded. |
| `CVCN` | 4 | always | int32 count C of saved cvars. |
| `CVAR`, `VALU`, `FLGS` | var, var, 4 | C times | Cvar name with terminating NUL; value with terminating NUL; int32 flags. Saved are all cvars whose flags contain 0x10, 0x100, 0x200, 0x400 or 0x800 (SPEC-302), newest-created first. |
| (game) | — | always | Chunks written by the game module's "write game" entry (retail: `GAME` 1688 bytes, `CLNT` 1536 bytes, `INVN` 640 bytes). |
| `SPNM` | 4 | always | int32 count S of string packages in use. |
| `SPID` | var | S times | String package name with NUL (e.g. `dm_generic`, `weapons`, `items`, `general`, `singleplr`, `<map>`). |
| `FRNM` | 4 | full | int32 server frame number. |
| `CFST` | 227520 | full | Configstrings: 3555 × 64-byte NUL-padded strings. |
| `PRTS` | 4096 | full | 1024 × int32 area-portal open states. |
| (game) | — | full | Chunks written by the game module's "write level" entry (retail ids include `FTSZ`, `LVLC`, `NMED`, `EDNM`, `EDCT`, `STRG`, `SCID`, `AI..`, `GG..`, `PL..` …; game spec). |

The game module writes its chunks through the same container service (the engine supplies write-chunk and
read-chunk functions with the id, a data pointer and a length).

## Behaviour

### 1. Saving (`save <name>`, also used by `quicksave`/`newsave`)
Refused, with a message and no file change, when: argument count is not 1 (`USAGE: savegame <name>`),
`deathmatch` is non-zero (`Can't savegame in a deathmatch`), name is `current` or `auto`
(case-insensitive; `Can't save to 'current'` / `Can't save to 'auto'`), no game is running
(`You must be in a game to save.`), the player's health is below 1 (`Can't savegame while dead!`), the name
contains `..`, `/` or `\` (`Bad savegame name.`), the game module reports a cinematic is playing
(`Cannot save during cinematic.`), or the game module refuses (e.g. no saves left; no engine message).
Otherwise: prints `Saving game "<name>"...`, deletes `current.sof`, writes all chunks into `current.sof`,
copies it to `<name>.sof` (overwriting), deletes `current.sof`, prints `Done.`. If the copy fails or copies
0 bytes, both files are deleted and `Failed to copy savegame <name> - deleting source and destination` is
printed. If `current.sof` cannot be created: `Failed to create new savegame file current` and
`Failed to create savegame`. With `sv_testsave` non-zero no chunk data is written, so the copy step finds
0 bytes, deletes both files and prints the copy-failure message (a debugging mode that exercises the save
code without producing a file).

### 2. Loading (`load <name>`)
Refused when the argument count is not 1 (`USAGE: loadgame <file>`), the name contains `..`, `/` or `\`
(`Bad loadgame name.`), or the name is `current` (`Can't load from 'current'`). For the name `quick` the
legacy file `<user>/save/quick.sof` must exist, otherwise `No quicksave to load`.
Otherwise prints `Loading game "<name>"...`, then:
1. `sv_restartlevel` = 0, `menu_nostats` = 1; `current.sof` deleted; `<name>.sof` copied to `current.sof`.
2. `current.sof` opened; missing → `Failed to open savegame file current` and `Failed to open savegame`;
   length 0 → `Illegal zero length savegame file current - deleting` (file deleted).
3. Read `COMM` (developer print `Reading: <comment>`), `SHOT` (discarded), `MPCM`, `CVCN`, then for each cvar
   `CVAR`, `VALU`, `FLGS`: the cvar is created if missing (empty value, saved flags) and its value is
   force-set to the saved value (protected cvars, flag 0x1000, ignore this).
4. A new server is started (as `map` would), the game module reads its game chunks and reports whether the
   save contains a level section; the map from `MPCM` is spawned in "loading a save" mode; the `SPNM`/`SPID`
   packages are registered again.
5. If a level section exists: `FRNM` (server time := frame number × 100 ms), `CFST`, `PRTS` (area portals
   applied), then the game module reads its level chunks. Automatic saves have no level section, so the level
   restarts from its map with the saved player/game state.
6. `current.sof` deleted, cvar `paused` set to 1, prints `Done.`.

### 3. Automatic save
`map <name>` and `gamemap <name>` in single player (deathmatch 0), after the level is spawned: the directory
`<user>/save/` is created if needed and an automatic save named `auto` is written (comment starts with
`ENTERING`, no level section). `map` (not `gamemap`) first deletes `auto.sof`.

### 4. Quick save and quick load
- `quicksave`: i = (`sv_quicksave_index` + 1) mod 4; executes `save quick<i>`; sets `sv_quicksave_index` = i
  (archived cvar, so the rotation survives restarts).
- `quickload`: starting at i = `sv_quicksave_index`, for up to 4 tries checks whether `save/quick<i>.sof`
  exists through the search path; the first found is loaded with `load quick<i>`; then i = (i+1) mod 4.
  If none exists, executes `load auto`.

### 5. Other commands
- `newsave`: finds the smallest NN in 00..99 such that `<user>/save/saveNN.sof` does not exist and executes
  `save saveNN`; none free → `No free save slot available`.
- `wipe <name>`: deletes `<user>/save/<name>.sof` and prints `<name> has been wiped`; `auto` is refused
  (`Can't wipe 'auto'`); usage `USAGE: wipe <name>`.
- `savesleft`: answered by the game module.
- Menus read a save's `COMM` text (default when unreadable: `Invalid/savegame/`) and its `SHOT` image
  (registered as a 128×128 picture named after the save).
- Every engine error (fatal or drop) deletes `current.sof`.

## Constants
| Name | Value |
|---|---|
| File extension | `.sof` |
| Chunk end marker | 0x1234ABCD |
| COMM size | 64 |
| SHOT size / format | 65536 / 128×128×4 |
| MPCM size | 256 |
| CFST size | 227520 (3555 × 64) |
| PRTS size | 4096 (1024 × int32) |
| Quick save slots | 4 (quick0..quick3) |
| newsave slots | 100 (save00..save99) |
| Skill names | Effortless, Easy, Medium, Challenging, Unfair, Custom |

## Test vectors / how to verify
1. Chunk header of a reference automatic save (first 12 bytes):
   `4D 4D 4F 43 40 00 00 00 1D D8 E1 CE` = id `COMM`, length 64, checksum 0xCEE1D81D; data starts with
   `ENTERING/Challenging/KOS1`; the bytes after the 64 data bytes are `CD AB 34 12`, followed by `54 4F 48 53`
   (`SHOT`) and length `00 00 01 00` (65536).
2. Checksum: data `30 00` (`"0"` + NUL, a VALU chunk) has checksum 0x77B58F44; data `33 00 00 00` (CVCN = 51)
   has checksum 0x375667E9. All 166 chunks of the reference `auto.sof` verify with the MD4-XOR rule.
3. Reference automatic save (single player, level kos1): 166 chunks = COMM, SHOT, MPCM, CVCN (51),
   51 × (CVAR, VALU, FLGS), GAME, CLNT, INVN, SPNM (5), 5 × SPID; total 73256 bytes.
4. Reference normal save: same prefix, then 6 SPID, FRNM, CFST, PRTS, then several thousand game chunks.
5. Saved cvar examples (name / value / flags): `sk_saving` / `2` / 0x10; `gamename` / `base` / 0x14;
   `gamedate` / `Jun 14 2001` / 0x14; `maxentities` / `1024` / 0x10; `sv_altpmodlistfile` / `custom` / 0x11;
   `ai_spawnfrequency` / `0` / 0x800.
6. In game: `quicksave` three times from a fresh install → files quick1, quick2, quick3 exist and
   `sv_quicksave_index` is 3 (UNVERIFIED end state; follows Behaviour 4).

## Open questions
- Content layout of the game-module chunks (GAME, CLNT, INVN and the level chunks) — game-module spec.
- Exact meaning of the game module's "can save" and "cinematic" queries (e.g. `sk_saving` limits).
- Whether `SHOT` pixel order is top-down; the reference files suggest RGBA with A = 255.
