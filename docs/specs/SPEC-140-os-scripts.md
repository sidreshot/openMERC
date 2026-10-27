# SPEC-140 — Compiled DesignerScript files (.os)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files `ds/**/*.os` (stock: 730 files — 727 in pak0, 3 patched copies in pak2 that
replace `ds/kos3/jhendh.os`, `ds/kos3/midgame.os`, `ds/nyc3/standoff.os`).
Code: `tools/formats/os_script.py` (disassembler / validator).

Sources: stock data only, plus two public community documents used as cross-checks (no code was
read; the repository has no licence):
- d3nd3, *sof-os-to-ds*, `README.md` and `dev_notes/ostods.txt`
  (https://github.com/d3nd3/sof-os-to-ds). These notes name the value types, the PUSH kinds and
  opcodes 0x00–0x07, and describe IF as "push, push, condition byte, fail-jump offset". Every
  fact below was re-derived from the data; where the notes agree it is said so.
- BSP entity lumps of the stock maps (read with `tools/formats/bsp.py`) and the string packages
  `strip/*.sp` (SPEC-131) were used as black-box cross-references. String ids follow SPEC-001/SPEC-201:
  16 bits, high byte = package ID, low byte = index.

## Purpose

A `.os` file is a compiled level script ("DesignerScript"; the source form is `.ds`, compiled by a
tool that is not part of the game data). Scripts are started by map entities (`script_runner`
with key `script`, and `worldspawn` key `script` for per-level precache scripts) and drive
cinematics, doors, AI actors, sounds, captions and level flow.

## Layout

The file is: one `int32` version, then a single byte stream of instructions up to end of file.
There is no other header, no symbol or string table, no section directory. All multi-byte values are
little endian. Strings are inline, NUL-terminated, Latin-1.

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | int32 | version, always 3 |
| 4 | … | instruction stream | declarations first, then code; ends exactly at end of file |

Jump operands are **absolute file offsets** (counted from byte 0 of the file, i.e. including the
version word) and always point at the first byte of an instruction.

### Instruction encoding

Every instruction is one opcode byte followed by immediate operands. Most instructions take their
real arguments from a value stack filled by preceding `PUSH` instructions.

| Opcode | Mnemonic (ours) | Immediate operands | Stack args (push order) | Count in stock data |
|---|---|---|---|---|
| 0x00 | DECL_GLOBAL | name\0, u8 type, int32 index | – | 45 |
| 0x01 | DECL_GLOBAL_INIT | as 0x00 + initial value of `type` | – | 0 (not seen; by pattern) |
| 0x02 | DECL_LOCAL | name\0, u8 type, int32 index | – | 5054 |
| 0x03 | DECL_LOCAL_INIT | name\0, u8 type, int32 index, value | – | 51 |
| 0x04 | DECL_PARAM | name\0, u8 type, int32 index | – | 64 |
| 0x05 | DECL_PARAM_INIT | as 0x04 + value | – | 0 (not seen; by pattern) |
| 0x06 | DECL_FIELD | name\0, u8 type, int32 field id | – | 21170 (29 per file) |
| 0x07 | ASSIGN | – | value, target | 5352 |
| 0x08 | ADD | – | a, b → pushes result | 20 |
| 0x09 | SUB | – | a, b → result | 13 |
| 0x0A | MUL | – | a, b → result | 6 |
| 0x0B | DIV | – | (hypothesis by pattern, not seen) | 0 |
| 0x0C | ADD_ASSIGN | – | value, target | 55 |
| 0x0D | SUB_ASSIGN | – | value, target | 22 |
| 0x10 | GOTO | int32 target | – | 129 |
| 0x11 | PUSH | u8 kind + operand (below) | – | 38552 |
| 0x13 | IF | u8 comparison, int32 target | left, right | 217 |
| 0x14 | EXIT | – | – | 1050 |
| 0x15 | SUSPEND | – | – | 46 |
| 0x16 | WAIT | – | seconds (int, float or variable) | 2268 |
| 0x17 | WAIT_SIGNALS | u8 0x80 \| n | n signal variables | 473 |
| 0x18 | WAIT_SIGNALS_ALT | u8 0x80 \| n | n signal variables | 3 |
| 0x19 | MOVE | u8 flags | entity, vector, optional args | 638 |
| 0x1B | USE | – | entity | 3263 |
| 0x1C | ROTATE | u8 flags | entity, vector, optional args | 107 |
| 0x1D | PRINT | u8 flags (always 0) | string id (int) or literal string | 413 |
| 0x1E | SOUND | u8 flags | entity, optional args, file name | 750 |
| 0x1F | HOLD | u8 flags | entity unless flags & 0x02 | 78 |
| 0x20 | UNHOLD | u8 flags | entity unless flags & 0x02 | 76 |
| 0x21 | AI | int32 flags | entity, int command, optional args | 2549 |
| 0x23 | PRECACHE_SOUND | – | file name | 613 |
| 0x25 | OP_25 | – | entity, vector | 1 |
| 0x27 | REMOVE | – | entity | 641 |
| 0x28 | HELI | u8 flags | entity, int command, optional args | 94 |
| 0x29 | ROF | u8 flags | entity, ROF file name, [signal] | 435 |
| 0x2A | MUSIC | – | int | 17 |
| 0x2B | CVAR | – | cvar name, value (float or string) | 127 |
| 0x2C | SOUND_GLOBAL | – | file name | 254 |
| 0x2D | VEHICLE | u8 flags | entity, int command, optional args | 21 |
| 0x2F | PRECACHE_ROF | – | ROF file name | 356 |
| 0x30 | STRING_PACKAGE | – | package name | 32 |
| 0x31 | CONSOLE | – | command text | 65 |
| 0x32 | ROF_RELEASE | – | ROF file name | 175 |
| 0x33 | WATCH | – (condition follows inline) | – | 49 |
| 0x35 | OP_35 | – | entity | 24 |

Opcodes 0x0E, 0x0F, 0x12, 0x1A, 0x22, 0x24, 0x26, 0x2E, 0x34 and ≥ 0x36 never occur in stock data.
Opcode bytes ≤ 0x06 never occur after the first code instruction, except as the WATCH tail below.

**Declarations.** All `DECL_FIELD` instructions come first, then variable declarations, then code.
Field ids and variable indices each count 0, 1, 2… in file order (globals, locals and parameters share
one index space). Value types (`u8 type`): 0 int, 1 float, 2 vector, 3 entity, 4 string (string never
seen in declarations). Initial values of `*_INIT` are encoded like PUSH constants of that type (int
or float: 4 bytes; vector: 12 bytes).

**PUSH operand** (`u8 kind` after 0x11):

| kind | Operand | Pushes |
|---|---|---|
| 0 | int32 | integer constant |
| 1 | float32 | float constant |
| 2 | 3 × float32 | vector constant |
| 3 | int32 | entity constant (never seen) |
| 4 | string\0 | string constant |
| 5 | int32 variable index | variable |
| 6 | int32 variable index, int32 field id | field of an entity variable (`var.field`) |
| 7 | u8 function id (+ u8 n for id 5) | result of a built-in function; consumes its arguments from the stack |

Built-in functions (PUSH kind 7). Names are ours.

| id | Args (push order) | Result | Evidence |
|---|---|---|---|
| 0 | string | entity whose `targetname` equals the string | 3841 of 4072 constant lookups match a `targetname` in a map running the script; 229 match no key at all (entities spawned at run time, or stale names) |
| 3 | string | entity whose `scripttarget` equals the string | 59 of 69 match a `scripttarget` key |
| 4 | – | entity; hypothesis: the activator of the script | assigned to variables named `you`, `sneak`, `irq2a_player`…; one script assigns the literal string `"get entity activator"` where others call id 4 |
| 5 | n × (value string, key string), n = extra byte | newly spawned entity | only use: `classname`, `spawnflags`, `angle` pairs |
| 7 | – | entity; hypothesis: the player | assigned to `player1`, `splayer`… (54 uses) |
| 8 | int, int | number | `hey = fn8(2, 15)` then `wait hey`; hypothesis: random value in a range |
| 9 | int | entity | one use, `player1 = fn9(0)`; hypothesis: player by index |

**IF / WATCH conditions.** Comparison byte (both forms): 0 `==`, 1 `<`, 2 `<=`, 3 `>`, 4 `>=`, 5 `!=`.

- `IF` (0x13): the two operands are pushed before the opcode (left first). The target is taken when
  the comparison is **false**; all 217 IF targets are forward jumps (an `if` block skip; `else` is an
  extra GOTO). This matches the community notes.
- `WATCH` (0x33): the opcode comes **first**, then the instructions computing the two operands, then
  a tail of `u8 comparison, int32 target` *without an opcode byte*. A decoder knows it is in a WATCH
  tail because a byte ≤ 0x06 appears after the operand pushes. Data-derived hypothesis: WATCH arms a
  condition that the interpreter keeps re-evaluating (which is why the expression is stored after the
  opcode); when it becomes **true**, execution continues at the target. Examples: `watch
  (hawk.health <= 0) goto L` with L setting `player.health = -100`; `watch (badguy1.health < 100) goto L`.

**Arithmetic.** For ADD/SUB/MUL the *second-pushed* value is the left operand
(`PUSH 1; PUSH count; SUB` decrements a loop counter that starts at 5 and is tested `count > 0`).
ASSIGN and the compound assignments take `value` first, `target` (variable or field) last.

### Optional-argument flags

Several opcodes carry a flags immediate that says which optional stack arguments are present. Push
order of the optional arguments is fixed per opcode (listed left to right). Bits without a listed
argument consume nothing.

| Opcode | Fixed args | Optional args by flag bit, in push order | Bits with no argument |
|---|---|---|---|
| MOVE 0x19, ROTATE 0x1C | entity, vector | 0x01 scalar (hyp. duration), 0x02 scalar (hyp. speed), 0x04 signal variable | 0x08 (hyp. vector is absolute, not relative) |
| SOUND 0x1E | entity | 0x02 float (0.9 in 734 of 742: hyp. volume), 0x08 int (0 ×498, 2 ×70, 8 ×63: hyp. channel) | 0x01 always set (file name, pushed last) |
| AI 0x21 (int32 flags) | entity, int command | 0x40 float, 0x04 vector, 0x01 int, 0x08 signal variable, 0x80 entity, 0x200 int, 0x400 int | 0x20, 0x100 |
| HELI 0x28, VEHICLE 0x2D | entity, int command | 0x01 entity, 0x02 vector, 0x04 scalar, 0x08 signal variable | – |
| ROF 0x29 | entity, file name | 0x01 signal variable | – |
| HOLD 0x1F, UNHOLD 0x20 | – | entity is present unless flag 0x02 is set | – |
| WAIT_SIGNALS 0x17/0x18 | – | low 7 bits = number of signal variables (1–5) | 0x80 always set |

With these rules every statement in every stock file consumes exactly the values on the stack.
Not observed, so their relative order is unknown: MOVE/ROTATE bits 0x01 and 0x02 together, AI bits
0x01 and 0x04 together, AI bits 0x200 and 0x400 together.

## Semantics (observed behaviour; names are ours)

Entity-field table. Every stock file declares the same 29 fields, ids 0–28, which is the script
interface to entity state: `x y z` float, `origin` vector, `origin_x origin_y origin_z` float,
`movetype` int, `start_origin start_angles` vector, `state` int, `distance` float, `owner` entity,
`wait` float, `velocity angle_velocity` vector, `team_chain` entity, `yaw_speed` float,
`modelindex count solid` int, `angles delta_angles` vector, `skinnum c_mode` int, `ideal_yaw` float,
`p_origin` vector, `takedamage health` int. A script refers to fields only by id.

| Mnemonic | Observed use (target entity classes from the maps' entity lumps) | Confidence |
|---|---|---|
| USE | fires an entity: cameras (561), explosions, breakables, walls, `func_fade`, doors, relays, lights | high (target classes) |
| REMOVE | removes entities (walls, monsters, triggers); the parameter of `common/killer.os` is named `kill_thing` | medium |
| MOVE / ROTATE | brush models, cameras, `info_notnull`; signal variables are later passed to WAIT_SIGNALS | high for args, medium for flag names |
| ROF | plays a `.rof` (SPEC-141) on brushes, cameras, helicopters; all 312 stock ROFs are referenced | high |
| PRECACHE_ROF, PRECACHE_SOUND, STRING_PACKAGE | appear in per-level precache scripts (`worldspawn` key `script`); STRING_PACKAGE names the level's `.sp` package (`arm1` → `strip/arm1.sp`) | high |
| ROF_RELEASE | names a ROF after the script has finished playing it (65 files) — hypothesis: frees it | low |
| PRINT | shows a caption: the int is a string id = package ID × 256 + index into `strip/*.sp`. All 411 ids resolve; 230 match the REFERENCE of the speech file played just before (`cin/arm1/s4d3.adp` → ARM1/S4D3 "JOHN: Hawk."), most others are off by one line | high |
| SOUND | plays a speech/sound file on an entity (`.adp` 545, `.wav` 205) | high |
| SOUND_GLOBAL | plays a file without an entity argument | medium |
| WAIT | pauses the script for the given seconds | high |
| WAIT_SIGNALS | waits until every listed signal variable has been signalled by the MOVE/ROTATE/ROF/AI that received it | medium |
| SUSPEND | stops the script at this point; 27 of 46 uses are followed by a GOTO back to the start or a loop head, the other 19 by more statements (e.g. the next line of a terminal dialogue). Hypothesis: execution resumes after SUSPEND when the script is activated again | medium |
| AI | issues a command to an AI actor (`m_*` classes only); 238 distinct command ids; 99 (448 uses, no args, usually at the end) looks like "release" | args high, meanings UNKNOWN |
| HELI | commands to `m_x_chopper_*`, `m_x_hind` (21 command ids) | targets high |
| VEHICLE | commands to `m_x_snowcat`, `m_x_tank` (5 command ids) | targets high |
| HOLD / UNHOLD | flag 0x02 form brackets cinematics (75/74 uses); entity form targets triggers and buttons | low |
| CVAR | sets a cvar: `menu_mail_available`, `menu_mail_file`, `menu_nextmap`, `menu_nostats`, `ai_spawnfrequency`, `timescale`, `stat_moneyadd` | high |
| CONSOLE | executes a console command: `cl_touch`, `intermission login`/`blankscreen`/`acc`/`acc2`/`endgame`, `menu blankscreen`, `kill`, `givemoretutorial`, `givesnipertutorial`, `disconnect;menu game` | high |
| MUSIC | sets a music state (-1, 0…4) | medium |
| OP_25, OP_35 | UNKNOWN (see open questions) | – |

**Relation to maps.** `script_runner` key `script` (and `Script`, 13 entities) names a script as a path
under `ds/` without extension (`arm/lockeddoor` → `ds/arm/lockeddoor.os`); keys `parm1`…`parm9` match
the script's DECL_PARAM count on 769 of 771 `script_runner` entities (two pass parms to a script that
declares none). Parameters bind to `parm1`, `parm2`… in declaration order (hypothesis, consistent with
the counts and with entity-valued parms naming targetnames).
`worldspawn` key `script` names the precache script. 720 of the 727 distinct scripts are referenced
by some stock map; unreferenced: `irq2b/irq2b_crane2`, `irq2b/irq2b_midcin`, `jpn2/monitor01`,
`jpn2/monitor02`, `jpn2/outwindow`, `jpn2/removewin`, `kos3/sneak1`.

## Validation

`python tools/formats/os_script.py validate <dir> --sp <dir with strip/*.sp>` over all stock files:

- 730 / 730 files parse; 0 failures; 59 033 instructions, 5 214 variables, 725 236 bytes.
- Checks per file: version 3; every instruction decodes and the stream ends exactly at end of file;
  declarations precede code and indices are sequential; every variable/field reference is in range;
  every GOTO/IF/WATCH target is the start of an instruction inside the code; every statement
  consumes exactly the number of stack values the argument scheme above predicts; every file ends
  with EXIT; every PRINT id resolves in the string packages.
- Listings of all 730 files: `openmerc-data/scripts/os_listings/**.dsl` (statement form with captions
  resolved); opcode/command inventory: `openmerc-data/scripts/os_stats.txt`.

## Open questions

1. Exact run-time semantics of SUSPEND, HOLD/UNHOLD, WATCH (one-shot or repeating? cancelled when the
   script ends?) and of WAIT_SIGNALS_ALT vs WAIT_SIGNALS — black-box test: run `arm1` and observe the
   computer terminal script (`arm/runcomputer.os`) and a WATCH script (`irq3a/countdown2.os`).
2. OP_25 (one use: `irq2b_midcin.os`, after setting the player origin, argument looks like angles
   (0.6, −57, 0)) — hypothesis: set view angles. OP_35 (24 uses, AI actors only, e.g. in a WATCH
   handler before releasing an actor) — hypothesis: cancel queued AI commands.
3. Meaning of the 238 AI command ids, 21 HELI ids, 5 VEHICLE ids, and of AI flag bits 0x20/0x100 and
   the int arguments 0x200/0x400. Data hints: id 186 and id 6 take a float equal to the speech line's
   duration; 60/61/62 take a vector (positions); 58/59 take an entity.
4. Meaning of MOVE/ROTATE flag bits 0x01/0x02 (duration vs speed) and 0x08 (absolute target) — test
   with `castle2/bighallgun.os` (bit 0x01, int 3) vs `arm/cardrive.os` (bit 0x02, int 250).
5. SOUND flag 0x02 float (0.9) and 0x08 int (0/2/8) — volume/channel are guesses.
6. Functions 4, 8, 9 (activator / random / player-by-index are hypotheses).
7. Opcodes 0x01, 0x05, 0x0B, 0x0E, 0x0F and PUSH kind 3 are never used by stock files; their encodings
   above are inferred by pattern only.
8. Whether jumps are absolute file offsets for the interpreter too (true for the file; the runtime
   might rebase) — irrelevant for a re-implementation that keeps the same convention.
