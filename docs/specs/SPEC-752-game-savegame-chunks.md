# SPEC-752: Game-module savegame chunks
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f (savegame content written and read by the game module)
Depends on: SPEC-303 (container, engine chunks, chunk services), SPEC-001 (exports 0x10–0x1C, imports 85/86),
SPEC-750 (script state), SPEC-751 (cinematic state), SPEC-003 (player module `INVN` chunk), SPEC-006 (entity keys), SPEC-700 (AI registries and AI object chunks)

## Purpose
SPEC-303 defines the `.sof` chunk container and the engine's own chunks and says only which game chunks exist.
This spec defines what the game module writes in its two save entries ("write game" export 0x10 and "write level"
export 0x18), in which order, how pointers inside saved records are encoded, the complete layout of every chunk of
the script system, the game-wide and level-wide records, and what is restored and re-linked on load. Chunk
families owned by other subsystems (AI objects, Ghoul objects, entity attachments, items) are listed with their
position in the sequence and left to their own specs.

All values are little-endian. Chunk ids are written here as their four letters (SPEC-303 explains the on-disk
byte order). Sizes are data lengths N of SPEC-303. "Record image" means the in-memory record copied byte for byte,
after the pointer fields listed for it were replaced by the encodings of §2.

## 1. Sequence

### 1.1 Game section (export 0x10 "write game", argument: autosave flag)
1. If this is not an automatic save, the clients' persistent data is refreshed from their entities first (as in
   Quake II: health and similar values are copied into the persistent part).
2. `GAME` (1688): the game-wide record (§3.1) with the clients link set to 0 and byte 0x614 = autosave flag.
3. For each client slot 0 … maxclients − 1: `CLNT` (1536) = client record image (§3.2), followed by the `STRG`
   chunks of its string fields (§2.2); then, if the client has a player-module inventory object, that object writes
   `INVN` (640; SPEC-003).
Reading (export 0x14): the 32-bit value designated through import 67 (SPEC-001) is set to 0; `GAME` is read into the
game record while the in-memory clients link is kept; each `CLNT` is read into its slot and decoded; a client
whose record says it had an inventory object gets a new one from the player module, which then reads `INVN`;
each client also gets a new empty helper object. The export returns byte 0x614 (autosave flag).

### 1.2 Level section (export 0x18 "write level")
Verified against four 1.07f saves (levels `TSR1`, `ARM1`, `TRN1`):
| # | Chunks | Owner |
|---|---|---|
| 1 | `FTSZ` (36) | this spec §2.3 |
| 2 | `LVLC` (1440) + its `STRG` chunks | this spec §3.3 |
| 3 | `NMED` (4) = number of entities in use; then per entity in use, in entity-number order: `EDNM` (4) = entity number, `EDCT` (1124) = entity record image (§3.4), its `STRG` chunks | this spec |
| 4 | `SGNM` (4) = number of level globals; per global: `SCID` + variable chunk (§4) | script system |
| 5 | `SCVR` (4) = 3; `SCSZ` (4) = number of scripts; per script, in level-list order: the script record (§4.4) | script system |
| 6 | AI registries and AI/body/sense/decision/action objects (`AICT`, `AICC`, `AI..`, `GG..`, `GHLI`, `STRG`, `SDSO`, `SCAT`, `HELI`, `TANK`, `HEWP` …) | AI framework (SPEC-700) / Ghoul glue specs |
| 7 | `OINM` (4) = number of entities that have attached objects; per object `EDOI`, `EDBN`, optional `EDBL` | entity attachment (Ghoul object) spec |
| 8 | `PLNM`, `PLPI` × n, `PLWA` | item system spec |
Reading (export 0x1C) uses the same order. Before reading, all level-tagged memory is freed, the Ghoul id map
(§5) is cleared, the item system is reset and the entity count is reset to maxclients + 1. Specific steps:
- `FTSZ` is compared with the module's own nine table sizes; any difference raises the game error
  `Function index tables have changed size`.
- Each `EDNM`/`EDCT` pair is written into entity slot `EDNM`, the entity count is raised to cover it, the record is
  decoded and the entity is linked into the world.
- Before the script chunks the existing script list, globals and ROF cache are destroyed (SPEC-750 §1.2).
- After the item chunks: every client entity (1 … maxclients) is re-attached to its client record; one client
  field (offset 0x2F8 of the client record) is set to 0; the Ghoul id remap of §5 runs; the player module's two
  post-load hooks are called (SPEC-003).

## 2. Encoding of pointer fields

### 2.1 Field kinds
Records are written as images; before writing, each field listed for the record is encoded as follows (the
in-memory record is not changed — a copy is encoded). On load the reverse is applied.
| Kind | On disk | On load |
|---|---|---|
| plain (int, float, vector, angles) | unchanged | unchanged |
| string | length of the string + 1, or −1 for none; the text follows later as a `STRG` chunk | −1: none; else the next `STRG` chunk is read with exactly that length (text and NUL), into new memory |
| entity | entity number, −1 for none | entity, none for −1 |
| client | client slot number (records of 1536 bytes), −1 for none | client record |
| presence | 1 if the pointer was set, else 0 | the value is kept as a marker; the object is re-created by its own chunks |
| cleared | 0 | 0 |
| always none | −1 | none |
| animation reference | table × 65536 + index into one of 4 fixed animation tables of 88-byte records (sizes 1, 540, 26, 6), −1 for none | record |
| sequence reference | table × 65536 + index into one of 13 fixed tables of 40-byte records (sizes 14, 15, 121, 15, 41, 23, 5, 11, 12, 15, 12, 5, 3), −1 for none | record |
| callback | index into one of the eight fixed callback tables (sizes 219, 5, 58, 119, 13, 56, 35, 2), −1 for none | function; an out-of-range index prints `Unable to evaluate <kind> function <n>` and gives none |
| script event | script number in the level list × 65536 + position in that script's event queue (used by AI action records to point at an AI-done event, SPEC-750 §3.2) | event |
A pointer that cannot be encoded prints a console warning (`Unable to enumerate <kind> function`, `Unable to
enumerate bi table pointer`, `Unable to enumerate event`) and is written as −1.
The order of the fixed tables is the order of the original module; a reimplementation that must read original
saves needs the same order (open question 1). A reimplementation that only reads its own saves may use its own
tables but must keep the chunk framing.

### 2.2 `STRG`
One chunk per non-empty string field, in the order of the field list of the record, immediately after the record's
chunk: data = the text plus its NUL. (Several records per save each have their own `STRG` run.)

### 2.3 `FTSZ` (36)
Nine int32: the byte sizes (4 × entries) of nine callback tables: 876, 20, 232, 476, 52, 224, 140, 8, 204. The
first eight are the tables of §2.1 (219, 5, 58, 119, 13, 56, 35, 2 entries); the ninth (51 entries) is used by the
Ghoul glue when re-creating model instances.

## 3. Records

### 3.1 `GAME` — game-wide record (1688 bytes)
| Offset | Size | Content |
|---|---|---|
| 0x000 | 512 | help message 1 (Quake II layout) |
| 0x200 | 512 | help message 2 |
| 0x400 | 4 | help-changed counter |
| 0x404 | 4 | clients link — written as 0 |
| 0x408 | 512 | spawn point name for the next level |
| 0x608 | 4 | maxclients (1 in single player) |
| 0x60C | 4 | maxentities (1024 stock) |
| 0x610 | 4 | cross-level trigger bits (server flags) |
| 0x614 | 1 | autosave flag (1 in `auto.sof`) |
| 0x615 | 1 | cinematic state (SPEC-751; always 0 in a valid save) |
| 0x618 | 32 | eight int32 mission-statistics counters accumulated over the levels of the mission (added at each level change from the level's own counters) |
| 0x638 | 4 | accumulated mission time, float seconds |
| 0x63C | 92 | skill settings (from 0x640), cheats-enabled flag (0x668), level-spawner state (0x670–0x687) and other game-wide values owned by their subsystems (UNVERIFIED split) |
Example (reference `auto.sof`, level KOS1 after a finished mission): maxclients 1, maxentities 1024, statistics
73, 4, 1, 1, 3, 0, 3, 32804, time 765.4.

### 3.2 `CLNT` — client record (1536 bytes)
Encoded fields: offset 844 presence (player-module inventory object; an `INVN` chunk follows when set), 852 presence
(second per-client object, UNVERIFIED), 108 cleared. All other bytes are plain image (player state, persistent and
respawn data — client layout belongs to the player spec).

### 3.3 `LVLC` — level record (1440 bytes)
| Offset | Content |
|---|---|
| 0 | level frame number (int32, frames since the level started) |
| 4 | level time (float, seconds = frame × 0.1) |
| 8 | level name, 64 bytes (`TSR1`) |
| 72 | map name, 64 bytes (`tsr1`) |
| 0x4F4 | string field (next map / change-map target; −1 in the samples) |
| 0x514 | entity: AI sight target (the player; 1 in the samples) |
| 0x518, 0x51C, 0x524, 0x52C, 0x550 | entity references (sight/sound bookkeeping; −1 or 0 in the samples) |
Everything else is plain image (owned by the main game module).

### 3.4 `EDCT` — entity record (1124 bytes)
The record is copied while the entity is unlinked from the world (link fields are therefore in their unlinked
state) and the entity is relinked immediately. Encoded fields (byte offsets in the record):
| Kind | Offsets (meaning where known) |
|---|---|
| client | 116 |
| entity | 352 (owner), 532, 676, 680, 800, 804, 808, 812 (activator), 816, 828 (team chain), 832, 836, 840, 844 |
| string | 420 `model`, 428 `message`, 436 `classname`, 452 `target`, 456 `targetname`, 460 `scripttarget`, 464 `killtarget`, 468 `killfacing`, 472 `team`, 476 `pathtarget`, 480 `deathtarget`, 484 `combattarget`, 488 `soundname`, 492 `spawn1`, 496 `spawn2`, 500 `spawn3`, 768 `map` |
| animation reference | 504, 508, 512, 516, 520, 524, 536 |
| sequence reference | 684 |
| callback (table of 219) | 696 (think), 1096 (second general callback) |
| callback (tables of 5, 58, 119, 13, 56, 35, 2) | 700 blocked, 704 touch, 708 use, 712 player-use, 716 pain, 720 die, 944 (two-entry callback, UNVERIFIED role) |
| presence | 1104 (AI object; saved by the AI chunks), 1108 (script; saved by the script chunks and re-attached on load) |
| cleared | 528 (attached-object link; rebuilt from `OINM`) |
String fields are written in the table order above (string order: 420, 428, 436, 452 … 500, then 768).
Example: the first entity of a TSR1 save is followed by four `STRG` chunks of 5, 11, 17 and 18 bytes.

## 4. Script system chunks

### 4.1 Object id prefix `SCID`
Every script object is preceded by `SCID` (4) = object kind:
| Kind | Object | Chunk (size) |
|---|---|---|
| 1 | int variable | `SINT` (80) |
| 2 | float variable | `SFLT` (80) |
| 3 | vector variable | `SVEC` (88) |
| 4 | entity variable | `SENT` (80) |
| 5 | string variable | `SSTR` (140) |
| 6 | variable reference | `SVVR` (80) |
| 7 | field reference | `SFVV` (84) |
| 8 | pending signal | `SSIG` (16) [+ `SCID` 6 + `SVVR`] |
| 9 | move-done event | `SMDE` (20) |
| 10 | rotate-done event | `SRDE` (20) |
| 11 | execute (use) event | `SEEX` (24) |
| 12 | wait event | `SWEV` (16) |
| 13 | script | `SNAM` … (4.4) |
| 14 | field descriptor | `SFDF` (84) |
| 15 | ROF frame event | `SMRE` (44) |
| 16 | AI-done event | `SADE` (24) |
| 17 | helicopter-done event | `SHDE` (24) |
| 18 | vehicle-done event | `STDE` (24) |
An unknown kind on load prints `Warning: Unable to RestoreObject` and creates nothing.

### 4.2 Value objects
Common header (bytes 0–75): 0–3 unused (garbage), 4–67 name (64 bytes, NUL-padded; empty for temporaries),
68–71 value type (SPEC-750 §4.1: 0 int, 1 float, 2 vector, 3 entity, 4 string, 5 reference), 72–75 slot index in
the owning script's variable table, −1 if none (level globals, temporaries, parameter strings).
| Chunk | Bytes 76… |
|---|---|
| `SINT` | int32 value |
| `SFLT` | float value |
| `SVEC` | 3 floats |
| `SENT` | int32 entity number, −1 none |
| `SSTR` | 64 bytes text |
| `SVVR` | int32 slot index of the referenced variable in the same script (−1: unresolved) |
| `SFVV` | int32 variable slot, int32 field slot (80–83) |
On load the object is placed into its slot when the slot index is not −1; references are re-bound to the objects
already restored in those slots (so referenced variables must precede references, which the writing order
guarantees).

### 4.3 Other script objects
| Chunk | Layout |
|---|---|
| `SFDF` (84) | 0–3 unused, 4–67 field name, 68 declared type (int32), 72 entity offset, 76 field kind, 80 field slot. On load offset and kind are re-resolved from the name (SPEC-750 §4.4), the saved ones are ignored |
| `SSIG` (16) | 0–3 unused, 4 entity number, 8 has-variable (1/0), 12 signal kind (0 move, 1 rotate, 2 AI, 3 ROF, 4 helicopter, 5 vehicle). When has-variable is 1, `SCID` 6 + `SVVR` follow |
| event header (16) | 0–3 unused, 4 event time (float, level seconds), 8 event kind (0 move done, 1 rotate done, 2 wait, 3 execute, 4 ROF frame, 5 AI done, 6 helicopter done, 7 vehicle done), 12 int32 (10 for kinds 0, 1, 4–7, 0 for 2 and 3; not used) |
| `SWEV` (16) | header only |
| `SMDE`, `SRDE` (20) | header, 16 entity number |
| `SEEX` (24) | header, 16 caller entity, 20 activator entity |
| `SMRE` (44) | header, 16 entity, 20 origin delta (3 floats), 32 angle delta (3 floats); delta (10000, 10000, 10000) = end marker |
| `SADE` (24) | header (time 0), 16 completed flag (0/1), 20 actor entity |
| `SHDE`, `STDE` (24) | header (time = issue time + 1000), 16 vehicle entity, 20 action identifier (−1 none) |

### 4.4 Script record (`SCID` 13)
| Order | Chunk(s) | Content |
|---|---|---|
| 1 | `SNAM` (260) | script file path as loaded (`ds/<name>.os`), NUL-padded |
| 2 | `SCRP` (32) | eight int32: state (SPEC-750 §2.2), clear-after-wait flag (0 or 0x80), code length, instruction pointer (file offset), debug flags, runner entity, caller entity, activator entity (entities as numbers, −1 none) |
| 3 | `SFNM` (4) + n × (`SCID` 14, `SFDF`) | field descriptors present in the field table (29 for stock scripts) |
| 4 | `SGNM` (4) + n × `SGLB` (68) | level globals bound in this script's variable table: int32 slot index + 64-byte global name |
| 5 | `SLNM` (4) + n × (`SCID`, object) | locals |
| 6 | `SPNM` (4) + n × (`SCID`, object) | bound parameters |
| 7 | `SSNM` (4) + n × (`SCID`, object) | value stack (normally 0) |
| 8 | `SWNM` (4) + n × (`SCID` 6, `SVVR`) | waited signal variables |
| 9 | `SINM` (4) + n × (`SCID` 8, `SSIG` …) | pending signals |
| 10 | `SVNM` (4) + n × (`SCID` 5, `SSTR`) | unused parameter strings (name `parm`) |
| 11 | `SENM` (4) + n × (`SCID`, event) | event queue in order |
| 12 | `SCNM` (4) + `SCND` (4 × n) | armed watches: n int32 file offsets of WATCH instructions (`SCND` is present with length 0 when n = 0) |
Loading a script record: the file named in `SNAM` is read again from disk (a missing file or wrong version prints
the SPEC-750 §1.2 messages); then `SCRP` overrides state, wait flag, length, IP, debug flags and the three entities;
the lists are rebuilt in the order above; `SGLB` entries bind slots to the already-restored level globals by name
(an unknown name leaves the slot empty); finally the runner entity's script link is set to this script. Before the
first script record, `SCVR` must equal 3, else the game error `LoadScripts(): Expecting version 3, found version
<n>`; every entity's script link is cleared first. The "inside watch handler" context (SPEC-750 §10) is **not**
saved: a save made while a handler had not resumed loses the saved IP/state of the interrupted code.

### 4.5 Level globals
`SGNM` (4) = n, then n × (`SCID`, value object with slot −1), before `SCVR`. Restored into the level-wide global
table by name.

## 5. Ghoul instances and model id remap
The Ghoul glue writes a model instance as `GHLI`: an opaque blob produced by the engine's Ghoul instance
serializer (variable length, at most 16 KiB; content SPEC-010/011). On load the blob is read with "any length" and
handed to the owning Ghoul object, which creates a new instance; the instance's old 16-bit id is mapped to the new
one. After the whole level section is read, every in-use entity flagged as Ghoul-rendered (bit 0 of the int32 at
entity offset 48) gets each of its four 16-bit Ghoul ids at entity offsets 100, 102, 104, 106 replaced through that
map (0 stays 0; an id missing from the map becomes 0).

## Constants
| Name | Value |
|---|---|
| `GAME` / `CLNT` / `LVLC` / `EDCT` sizes | 1688 / 1536 / 1440 / 1124 |
| `INVN` size (player module) | 640 |
| `FTSZ` contents | 876, 20, 232, 476, 52, 224, 140, 8, 204 |
| Script save version (`SCVR`) | 3 |
| Script object kinds | 1–18 (§4.1) |
| Value object sizes | 80 (int, float, entity, reference), 84 (field reference, field descriptor), 88 (vector), 140 (string) |
| `SNAM` / `SCRP` / `SGLB` sizes | 260 / 32 / 68 |
| Event header size | 16 |

## Test vectors / how to verify
1. Reference saves (1.07f, single player): `save00.sof` (TSR1, 8815 chunks) has after `PRTS`: `FTSZ` = 876, 20, 232,
   476, 52, 224, 140, 8, 204; `LVLC` with frame 3511 and time 351.1; `NMED`; entity chunks; `SGNM` 0; `SCVR` 3;
   `SCSZ` 55; then 55 script records, the first being `ds/tsr1/tsr1pre.os` with `SCRP` = (1, 0, 1978, 1978, 0, 10,
   10, 1): finished, IP at end of code, runner 10, caller 10, activator 1 (the player).
2. In the same save, `ds/tsr1/lhostage.os` waits on an AI command: `SWNM` 1 (`SCID` 6, `SVVR`), `SINM` 1 (`SSIG` kind
   2 with variable), `SENM` 1 (`SADE`, completed 0, actor entity).
3. A TRN1 save made during the helicopter sequence (`ds/trn1/trn1_heli2.os`) holds 10 `SHDE` events with action
   identifiers 1, 2, 3 … and time = issue time + 1000 (1080.9 at issue time 80.9); a later save holds 64 `SMRE`
   events for the ROF in progress, spaced 0.1 s.
4. `save03.sof` (ARM1, frame 38): `ds/arm/cardrive.os` has one `SWEV` (time 5.3) and one `SMDE` (time 5.6, entity 68).
5. Round trip: save and load in the middle of a scripted door move: the door continues and the script resumes when
   the move-done event fires at the saved time.

## Open questions
1. Order and identity of the eight callback tables (219, 5, 58, 119, 13, 56, 35, 2 entries), the four animation tables
   and the thirteen 40-byte record tables — required only to read saves made by the original game. Verify by
   reading an original save's `EDCT` callback indexes against the entities' known behaviour.
2. Meanings of the unnamed entity reference fields (532, 676, 680, 800–816 except 812, 832–844) and of the
   two-entry callback at 944; meaning of the second per-client presence field (852).
3. Split of the last 92 bytes of `GAME` between skill, cheats and spawner state.
