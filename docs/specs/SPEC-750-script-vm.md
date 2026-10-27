# SPEC-750: DesignerScript virtual machine (run-time semantics)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-140 (`.os` file format), SPEC-141 (`.rof` format), SPEC-700 (AI framework: scripted decision,
waking, expression states), SPEC-722 (bodies, expression states), SPEC-751 (cinematics, ROF playback, captions),
SPEC-752 (savegame chunks of scripts), SPEC-001 (game import/export table), SPEC-006 (game module map), SPEC-003
(player module, inventory object)

## Purpose
SPEC-140 describes the bytes of a compiled level script. This spec describes what the game module does with them:
how scripts are created and started, how the interpreter executes and suspends them, how waits, signals,
"watch" handlers and use-events are scheduled frame by frame, the exact semantics of every opcode, of the value
types and of the built-in functions, and the contract of the AI command (opcode 0x21) including the complete
table of the 341 AI command ids the game understands. Cinematic state, remote cameras, ROF playback and captions
are in SPEC-751; how script state is written to savegames is in SPEC-752.

Opcode names below are the mnemonics of SPEC-140. Where this spec corrects SPEC-140 it says so explicitly.

## 1. Objects and lifetime

### 1.1 `script_runner`
| Key | Meaning |
|---|---|
| `script` | script path relative to `ds/` without extension; the file loaded is `ds/<script>.os` |
| `parm1` … `parm10` | parameter strings. They are collected in order `parm1`, `parm2`, … up to the **first missing one** (a gap ends the list; `parm11`+ do not exist as keys) |
| `targetname` | how other entities fire the runner |
| `spawnflags` | 1 = "WILL_KILL_USER": when an AI character's `killtarget` names this runner, the character is kept alive for the script to kill (SPEC-700) |

At spawn the runner becomes invisible to clients, non-solid and non-moving, and gets a *use* behaviour (1.3).
Every runner owns **its own** script instance, even when several runners name the same file (each has its own
variables, instruction pointer and state). All script instances of a level are kept in one level-wide list in
spawn order; that order is the order in which scripts are processed each frame (section 3).

### 1.2 Loading
- The whole file is read through the game file system. A missing file prints, as three console lines,
  a line of 47 `*`, `Could not open script <path>` and the star line again; the instance exists but is
  FINISHED and never runs.
- The first int32 must be 3. Otherwise `Bad script version for <path>: found <n>, expecting 3` (between star lines);
  the instance is FINISHED and never runs.
- The instruction pointer (IP) starts at file offset 4. A loaded script does **not** run by itself; it runs when
  its runner is used (1.3).
- The level-wide script list, all global variables and the ROF cache (SPEC-751) are destroyed when a level is
  spawned, when a savegame's level section is loaded (before the saved scripts are re-created) and when the game
  shuts down.

### 1.3 Using the runner
Using a `script_runner` (by a trigger, a target chain, another script's USE …) queues an *execute event* in the
runner's script with: time = level time rounded to the 0.1 s grid (rule in 3.3), *caller* = the runner itself,
*activator* = the activator of the use. When the event is processed (section 3) the script is run **only if it
is READY** (state machine in 2.2); a use that arrives while the script is FINISHED, waiting for time, or waiting
for signals that are not yet satisfied is consumed and has no effect (UNVERIFIED in-game: re-trigger a door script
while its door is moving).

## 2. Script instance state

### 2.1 Contents
| Item | Meaning |
|---|---|
| code | the file image; IP is an absolute file offset |
| state | READY 0, FINISHED 1, WAIT_ALL 3, WAIT_ANY 4, WAIT_TIME 5 (values are saved, SPEC-752) |
| clear-after-wait flag | 0x80 or 0 (operand bit of WAIT_SIGNALS, 9.3) |
| runner | the `script_runner` that owns the script |
| caller | entity returned by built-in 6; set by the use event (the runner) and by a signal wake-up (the entity whose action completed) |
| activator | entity returned by built-in 7; set by the use event |
| debug flags | bits 0x02 move trace, 0x04 rotate trace, 0x08 activation trace, 0x10 ROF trace (opcode 0x1A) |
| variable table | 100 slots, index = variable index of SPEC-140 (globals, locals and parameters share it) |
| field table | 100 slots, index = field id of SPEC-140 |
| value stack | LIFO of values; normally empty between activations |
| locals, parameters | lists of the variables declared by this script |
| parameter strings | the runner's remaining `parmN` strings, consumed by parameter declarations |
| waited variables | the signal variables of the current WAIT_SIGNALS |
| pending signals | (entity, signal kind, variable) triples registered by MOVE/ROTATE/ROF/AI/HELI/VEHICLE |
| event queue | pending events ordered by time (section 3) |
| watch list | file offsets of armed WATCH instructions, in arming order |
| watch context | "inside a watch handler" flag, saved IP and saved state (section 10) |

### 2.2 State machine
- READY: may run. Initial state of a correctly loaded script.
- Running ends (the script *yields*) when: EXIT executes (→ FINISHED); IP reaches or passes the end of the code
  (→ FINISHED); SUSPEND or any opcode without a handler (0x15, 0x2E, ≥ 0x36) executes (state stays READY);
  WAIT with a positive duration (→ WAIT_TIME); WAIT_SIGNALS whose condition is not yet met (→ WAIT_ALL or
  WAIT_ANY); RESUME (section 10); a USE that ends a cinematic because the player is dead (12.9).
- FINISHED is final for use-events. Watches armed before EXIT still fire (section 10).
- WAIT_TIME → READY when its wait event fires, and the script continues immediately (3.2).
- WAIT_ALL / WAIT_ANY → READY when the signal condition holds (9.3), and the script continues immediately.

### 2.3 Instruction budget
One activation may execute at most 1000 instructions (declarations, pushes and statements all count). The 1001st
raises the game error `Runaway loop for script OR more than 1000 instructions` (the level is aborted, SPEC-001
import 72). A nested activation (a watch handler, a wake-up started from inside another activation) has its own
counter.

### 2.4 Errors
These conditions raise the game error with the quoted text (level aborted): instruction budget (2.3);
`Missing Parameter` (6.3); `Index out of range: <i> > <n>` (variable index > 99); `Index for field out of range`
(field id < 0 or > 99); `Illegal push` (a PUSH or an arithmetic result produced no value, see 6.5); `Invalid stack
for <Name>` (a statement found too few values; names: Add, Subtract, Multiply, Divide, AddAssignment,
SubtractAssignment, MultiplyAssignment, DivideAssignment, If, HandleWait, Time Wait, Print, PlaySound,
HandleSpawn(), HandleUnloadSound, HandleUnloadROFF, HandleChacheSound, HandleCacheStringPackage, HandleCacheROFF,
HandleResetAI(), HandleSetCvar(), HandleConsoleCommand(), HandleCopyPlayerAttributes(), HandleSetViewAngles(),
HandleSetCacheSize()); `Expecting PUSH for on statement`, `Invalid opcode when handling on statement`,
`Resume command executed outside of ON branch` (section 10); `Renner Error #1: Variable '<name>' is being used
for multiple signals` (9.1).
Console-only diagnostics: `entity <name> had no ai--aborting HandleAnimate!` (AI command on an entity without AI;
the statement is skipped), `entity <name> had no ai--aborting HandleResetAI!`.

## 3. Frame scheduling and events

### 3.1 Order inside a game frame
Scripts are processed once per server frame (every 100 ms), **after** all entities have been run (physics,
think functions, client frames) and after the game-rules frame hook, and before the deathmatch rule checks and
the client end-of-frame updates. Scripts are processed in list order (1.1). For each script:
1. If cvar `sv_jumpcinematic` equals 2 and the "stop sounds" latch is armed, the latch is cleared and the console
   command `stopsound` is issued (the latch starts armed at module load and is re-armed whenever a cinematic ends,
   SPEC-751).
2. Watches (section 10): armed watches are tested in arming order; the first whose condition is true fires and is
   removed. At most one watch fires per script per frame.
3. Events: the event queue is scanned from its head. Each event is asked whether it fires now; a firing event
   performs its action and is removed, and the scan restarts at the head; a non-firing event is skipped. The scan
   ends when it reaches the end of the queue.

### 3.2 Event kinds
| Kind | Created by | Fires when | Action when it fires |
|---|---|---|---|
| 0 move done | MOVE with speed or duration | level time ≥ event time | mover velocity := 0, origin := the move's destination (exact snap); signal kind 0 |
| 1 rotate done | ROTATE with speed or duration | level time ≥ event time | angular velocity := 0 (angles are **not** snapped); signal kind 1 |
| 2 wait | WAIT with positive duration | level time ≥ event time | if state is WAIT_TIME it becomes READY; if the script is READY (or its signal wait is satisfied) it runs |
| 3 execute (use) | runner use (1.3) | level time ≥ event time | if READY (or signal wait satisfied) the script runs with caller and activator from the event |
| 4 ROF frame | ROF (SPEC-751) | level time ≥ event time | applies one ROF frame, or (end marker) signals kind 3 |
| 5 AI done | AI command with a signal | the last queued AI action of that command has completed (12.6); time is ignored | signal kind 2 |
| 6 heli done | HELI | the helicopter has no current action and its last completed action is this one (12.8); time ignored | signal kind 4 |
| 7 vehicle done | VEHICLE | same rule for tank/snowcat (12.8) | signal kind 5 |
"Signal kind k for entity E" is broadcast to **every** script of the level (section 9.2).
An entity reference held by an event is not checked for validity when the event fires (UNVERIFIED: freeing a
moving entity before its move-done event).

### 3.3 Event times
Every event time is quantized as T = floor((t + 0.05) × 10) × 0.1 where t is the requested time (level time plus
delay). Use events: t = level time. WAIT: t = level time + seconds. MOVE/ROTATE: t = level time + frames × 0.1
(11). ROF frame i (0-based): t = level time + i × 0.1. HELI/VEHICLE done events carry t = level time + 1000 (not used
for firing).

### 3.4 Queue order and the watch-handler flush
Events are inserted after all queued events whose time is ≤ the new event's time (stable FIFO for equal times).
**If the script is inside a watch handler (section 10) when an event is inserted, every queued event of that
script is deleted first and the "inside handler" flag is cleared.** Consequence: a watch handler that starts a new
WAIT, MOVE, ROF … abandons whatever the interrupted code was waiting for (pending waits, move/rotate-done events,
ROF frames that have not been applied yet — a mover interrupted this way keeps its velocity, UNVERIFIED).
The same flush happens if the handler ended without RESUME and a later use of the runner queues an execute event.

## 4. Values

### 4.1 Value types
| Type | Code (SPEC-140) | Holds | Default when declared |
|---|---|---|---|
| int | 0 | signed 32-bit | 0 |
| float | 1 | 32-bit float | 0.0 |
| vector | 2 | 3 floats | (0, 0, 0) |
| entity | 3 | entity reference or none | **the world entity (entity 0)** |
| string | 4 | text of at most 63 characters (stored in 64 bytes; a longer literal is cut to 64 bytes) | empty |
| reference | 5 | a variable reference (PUSH kind 5) or an entity-field reference (PUSH kind 6); reports the type of the referenced variable | — |
Variable names are stored in 64 bytes.

### 4.2 Conversions ("get as")
Every operation reads its operands through one of five conversions. The table gives what each type returns.
| Source type | as int | as float | as vector | as entity | as string |
|---|---|---|---|---|---|
| int | value | value converted | (0,0,0) | none | empty |
| float | truncated toward zero | value | (0,0,0) | none | empty |
| vector | 0 | 0.0 | value | none | empty |
| entity | entity number, −1 for none | 0.0 | (0,0,0) | value | empty |
| string | 0 | 0.0 | (0,0,0) | none | value |
| variable reference | conversion of the referenced variable | same | same | same | same |
| field reference | see 4.4 | see 4.4 | see 4.4 | see 4.4 | empty |

### 4.3 Arithmetic and comparison
For ADD/SUB/MUL/DIV and their compound forms, and for IF/WATCH comparisons, the **left operand** decides the
operation and the right operand is converted to what the left one needs. For arithmetic the left operand is the
value pushed **last**; for comparisons the left operand is the value pushed **first** (SPEC-140 order: IF pushes
left, then right). The result of arithmetic is a new temporary value of the left operand's type.
| Left type | + − × ÷ (result type) | == != < <= > >= |
|---|---|---|
| int | int, right converted to int; ÷ truncates toward zero; ÷ 0 is undefined (crash) | integer comparison with right as int |
| float | float, right as float | float comparison with right as float |
| vector | vector. Right int/float s: component-wise with (s, s, s). Right vector: component-wise (× and ÷ are component-wise too). Other right types: as (0,0,0) | right int/float s: == and != compare the vector **length** with s; <, <=, >, >= compare the length with s. Right vector: == true when all three components are equal, != true when any differs; <, <=, >, >= compare the two lengths. Other right types: false |
| entity | no result → `Illegal push` error | == / != : right int compares the entity number (none = −1); right entity compares identity; anything else false. <, <=, >, >= always false |
| string | no result → `Illegal push` | always false (strings cannot be compared) |
| variable reference | as the referenced variable | as the referenced variable |
| field reference | the field is read as a temporary of its natural type (4.4) and that temporary is the left operand | same |

ASSIGN converts the value to the **target's** type (int target: as int; float: as float; vector: as vector — so
assigning an int to a vector variable stores (0,0,0); entity: as entity; string: as string). Compound assignment
(0x0C–0x0F) computes target op value with the rules above, then assigns the result to the target.

### 4.4 Entity-field references
A field reference is (entity variable, field). The field id is resolved by name, at declaration, against this
table (names not listed resolve to "no field": reading gives 0/0.0/(0,0,0)/none, writing is ignored, using it as a
left operand raises `Illegal push`). Stock scripts declare 29 names; `delta_angles`, `skinnum`, `c_mode`,
`ideal_yaw`, `p_origin`, `takedamage` are such "no field" names and are unused by stock scripts; `loopsound` is
known but never declared by stock scripts.
| Name | Natural type | Meaning |
|---|---|---|
| `x`, `y`, `z` | float | component of the **variable's own vector value** (not an entity field). On an entity variable they read 0 |
| `origin` | vector | entity position |
| `origin_x`, `origin_y`, `origin_z` | float | components of the entity position |
| `movetype` | int | movement type |
| `start_origin`, `start_angles` | vector | mover start position / start angles |
| `distance` | float | mover travel distance |
| `state` | int | mover state |
| `owner` | entity | owner entity |
| `wait` | float | the entity's `wait` value |
| `velocity`, `angle_velocity` | vector | linear / angular velocity |
| `team_chain` | entity | next entity of the entity's team |
| `yaw_speed` | float | turning speed |
| `modelindex` | int | model index |
| `count` | int | the entity's `count` value (note: AI actors looked up by a script during a cinematic have 255 here, SPEC-751) |
| `solid` | int | solid type |
| `angles` | vector | orientation |
| `health` | int | health |
| `loopsound` | int | looping sound index of the entity |
Read rules: as int succeeds only for int fields, as float only for float fields, as vector only for vector
fields, as entity only for entity fields; any other combination returns 0 / 0.0 / (0,0,0) / none
(example: assigning `e.wait` to an **int** variable stores 0). `x`/`y`/`z` read the component (truncated for as
int). Through a field reference with the entity variable holding none, reads give the zero value.
Write rules: the value is converted to the field's natural type and stored. **The entity is not relinked**
(writing `origin` moves the entity only for code that does not depend on its link; UNVERIFIED in-game). Writing
`x`/`y`/`z` rewrites the variable's vector with that one component replaced.

## 5. Declarations

### 5.1 Fields (0x06)
Reads name, u8 type, int32 field id; creates the field descriptor of 4.4 in field slot `id`.

### 5.2 Variables (0x00–0x05)
Each declaration creates a variable of the given type with its default value (4.1) in slot `index`; the `_INIT`
forms then read the initial value (int32, float32 or 3 × float32 like PUSH constants; entity: int32 entity number,
−1 = none; string: NUL-terminated). Re-executing a declaration re-creates the variable (the slot is overwritten).
- Global (0x00/0x01): globals are shared **by name** by all scripts of the level. If a global of that name already
  exists the slot is bound to the existing variable and the new one (including its initial value) is discarded;
  otherwise the new variable becomes the global. Globals live until the level ends.
- Local (0x02/0x03): private to the instance.
- Parameter (0x04/0x05): see 5.3.

### 5.3 Parameter binding
On executing a parameter declaration whose name is not yet bound, the next unused parameter string of the runner
is taken (`parm1` first). None left → game error `Missing Parameter`. Conversion by declared type: int → decimal
string to integer (C `atoi`), float → `atof`, vector → three floats separated by spaces, entity → the first entity
whose `targetname` equals the string (none if not found). A string-typed parameter consumes its string but keeps
the empty value (strings cannot be passed; not used by stock data). The initial value of 0x05 is overwritten by the
binding (for int/float/vector/entity types).

## 6. Opcode semantics
"Pop" takes the most recently pushed value. Unless stated otherwise every popped value is discarded after use and a
statement with missing stack values raises `Invalid stack for …` (2.4).

| Op | Mnemonic | Run-time behaviour |
|---|---|---|
| 0x00–0x06 | declarations | section 5 |
| 0x07 | ASSIGN | pop target (pushed last), pop value; target := value (4.3) |
| 0x08–0x0B | ADD SUB MUL DIV | pop left (pushed last), pop right; push left op right (4.3) |
| 0x0C–0x0F | ADD_ASSIGN SUB_ASSIGN MUL_ASSIGN DIV_ASSIGN | pop target, pop value; target := target op value. **0x0E (×=) and 0x0F (÷=) exist** (SPEC-140 lists them as unused) |
| 0x10 | GOTO | IP := int32 operand |
| 0x11 | PUSH | section 7 |
| 0x12 | POP | pop one value and discard it (never used by stock data) |
| 0x13 | IF | pop right, pop left, compare (comparison byte 0 ==, 1 <, 2 <=, 3 >, 4 >=, 5 !=); if the result is **false** IP := target. An unknown comparison byte counts as false |
| 0x14 | EXIT | state := FINISHED, yield |
| 0x15 | SUSPEND | yield; state stays READY; the next use continues at the following instruction |
| 0x16 | WAIT | pop seconds (as float). If ≤ 0: continue. Otherwise queue a wait event at level time + seconds (3.3), state := WAIT_TIME, yield |
| 0x17 | WAIT_SIGNALS (all) | 9.3 |
| 0x18 | WAIT_SIGNALS_ALT (exactly one) | 9.3 |
| 0x19 | MOVE | 11 |
| 0x1A | DEBUG | u8 operand b. b = 0: print the script name, IP, parameters, globals and locals to the console. b odd: debug flags \|= b without bit 0. b even and non-zero: debug flags &= ~b. Not used by stock data |
| 0x1B | USE | 12.9 |
| 0x1C | ROTATE | 11 |
| 0x1D | PRINT | caption / text print, SPEC-751 §4 |
| 0x1E | SOUND | 12.3 (corrects SPEC-140 flag meanings) |
| 0x1F | ENABLE / CINEMATIC START | u8 flags. 0: pop entity; if it exists: solid := trigger, its use behaviour := "fire like a multiple trigger" (Quake II `trigger_multiple` use), relink. 2: start the cinematic (SPEC-751). Other values: nothing, nothing popped. (SPEC-140 "HOLD") |
| 0x20 | DISABLE / CINEMATIC END | u8 flags. 0: pop entity; if it exists: solid := not solid, use behaviour removed (no relink). 2: end the cinematic. Other values: nothing. (SPEC-140 "UNHOLD") |
| 0x21 | AI | section 12 |
| 0x22 | DEBUG_PRINT | inline NUL-terminated string; printed to the console followed by a newline. Not used by stock data |
| 0x23 | PRECACHE_SOUND | pop name; register the sound name (sound index), except while a cinematic is being skipped (SPEC-751 §1.4) |
| 0x24 | (no-op) | pop two values, discard. Not used by stock data |
| 0x25 | SET_VIEW_YAW | pop vector, pop entity (must be a player): set the player's movement delta angles so that the view becomes pitch 0, yaw = vector[1], roll 0 (each delta = angle − last command angle, in 65536/360 units, truncated to 16 bits); relink. Vector components 0 and 2 are ignored (SPEC-140 OP_25) |
| 0x26 | (no-op) | pop one value, discard |
| 0x27 | REMOVE | pop entity; if it exists it is freed |
| 0x28 | HELI | 12.8 |
| 0x29 | ROF | SPEC-751 §3 |
| 0x2A | MUSIC | pop n (as int). n < 0: the player's music state byte := 2 (automatic music). n ≥ 0: music state byte := n + 3. "The player" is the AI sight target (in single player: the player). The byte is player-state field 28 of SPEC-202 |
| 0x2B | CVAR | pop value, pop name. Value of type string: set the cvar to the text; otherwise set it to the value as float |
| 0x2C | UNLOAD_SOUND | pop name; release the sound name slot (SPEC-001 import 4). **Corrects SPEC-140**, which called it SOUND_GLOBAL |
| 0x2D | VEHICLE | 12.8 |
| 0x2E | — | no handler: acts as SUSPEND |
| 0x2F | PRECACHE_ROF | pop name; load the ROF into the cache (SPEC-751 §3) |
| 0x30 | STRING_PACKAGE | pop name; register the string package (SPEC-001 import 47) |
| 0x31 | CONSOLE | pop text; append it and then a newline to the server command buffer |
| 0x32 | RELEASE_ROF | pop name; drop the ROF from the cache (SPEC-751 §3) |
| 0x33 | WATCH | section 10 |
| 0x34 | RESUME | section 10. Not used by stock data |
| 0x35 | RESET_AI | pop entity; if it has AI, its scripted behaviour (all queued script actions) is removed and the actor returns to its own AI. If it has no AI the console message of 2.4 is printed. (SPEC-140 OP_35) |

## 7. PUSH (0x11)
| Kind | Pushes |
|---|---|
| 0 / 1 / 2 / 4 | int / float / vector / string constant (temporary) |
| 3 | entity constant: int32 entity number; −1 = none |
| 5 | reference to variable slot (int32); its type is the variable's type at push time |
| 6 | field reference: int32 variable slot, int32 field slot |
| 7 | result of a built-in (section 8) |
A push that produces no value raises `Illegal push`.

## 8. Built-in functions (PUSH kind 7)
Arguments are popped from the stack (the last pushed first).
| Id | Arguments (push order) | Result | Notes |
|---|---|---|---|
| 0 | name | entity: first entity (lowest number) whose `targetname` equals name; none if not found | while a cinematic is running, a found entity that has AI is marked as a cinematic actor (SPEC-751 §1.3) |
| 1 | angle in degrees | float sin | not used by stock data |
| 2 | angle in degrees | float cos | not used by stock data |
| 3 | name | entity: first entity whose `scripttarget` equals name | same cinematic marking as id 0 |
| 4 | — | entity: the first entity whose classname is `player` | SPEC-140 hypothesised "activator" — it is the player |
| 5 | n × (value, key) then u8 n inline | newly spawned entity, see 8.1 | |
| 6 | — | entity: the *caller* (2.1): the runner itself after a use, or the entity whose completed action woke the script from WAIT_SIGNALS | not used by stock data |
| 7 | — | entity: the *activator* of the use that started the script | SPEC-140 hypothesised "player": in practice the activator is usually the player |
| 8 | a, b | float: random integer from the engine's integer random function called with (b, a), i.e. min = b (pushed last), max = a | with the engine formula of SPEC-001 and a < b the result is uniform over a+1 … b−1, and b itself with probability 1/32768 (`fn8(2, 15)` gives 3 … 14). UNVERIFIED in-game |
| 9 | n | entity number n + 1 (the n-th client slot, 0-based) | |
Unknown ids push nothing (`Illegal push`).

### 8.1 Spawn (id 5)
A new entity is allocated; then n times: pop key name, pop value; the key is looked up (case-insensitively) in the
entity key table of the spawn system (SPEC-006) and stored with the key's type (int, float, string, vector, `angle`
= yaw only); unknown keys are ignored; spawn-time keys go to the spawn-temporary record. Then the normal spawn
function for the classname runs and the entity is pushed. Stock data uses only `classname`, `spawnflags`, `angle`.

## 9. Signals and waits

### 9.1 Registration
MOVE, ROTATE, ROF, AI, HELI and VEHICLE take an optional *signal variable* (a variable reference, SPEC-140 flag
tables). It is registered as a pending signal (entity, kind, variable) with kind 0 move, 1 rotate, 2 AI, 3 ROF,
4 helicopter, 5 vehicle. Registering a variable that already has a pending signal raises the game error
`Renner Error #1: Variable '<name>' is being used for multiple signals`. MOVE and ROTATE without speed and
duration (instant) and ROF with a missing/invalid file register nothing (the variable is never signalled).

### 9.2 Delivery
When an action completes, signal (entity E, kind k) is broadcast to every script. In each script every pending
signal with that entity and kind is removed and its variable is **incremented by 1** (only int variables
increment; other types are unchanged). If at least one matched and the script is in WAIT_ALL or WAIT_ANY and its
condition now holds, the waited variables are processed as in 9.3 and the script runs at once with *caller* := E.

### 9.3 WAIT_SIGNALS (0x17 all, 0x18 exactly one)
Operand byte: bit 0x80 = clear-after-wait, low 7 bits = n. Pops n variable references (they become the waited
list). State := WAIT_ALL (0x17) or WAIT_ANY (0x18). Condition: count the waited variables whose value as int is
non-zero; WAIT_ALL holds when the count equals n, WAIT_ANY when the count **equals 1** (if two are already set it
never holds — quirk). If the condition holds immediately the script continues without yielding; otherwise it
yields. When the condition holds: if clear-after-wait is set every waited variable is set to 0; the waited list is
emptied; state := READY. A signal that arrived before the WAIT_SIGNALS was executed is therefore not lost
(the variable is already non-zero).

## 10. WATCH (0x33) and RESUME (0x34)
Encoding (SPEC-140): opcode, PUSH (left operand), PUSH (right operand), u8 comparison, int32 target.
- Executing WATCH appends its own file offset to the watch list (executing it twice arms it twice), evaluates and
  discards the two operands, and continues after the target field. Each operand must be exactly one PUSH
  instruction (else `Expecting PUSH for on statement`).
- Each frame (3.1 step 2) armed watches are evaluated in order by re-reading the instruction at the saved offset
  (it must still be opcode 0x33, else `Invalid opcode when handling on statement`): both PUSHes are executed and
  compared like IF; the watch fires when the comparison is **true**. Firing: the watch is removed (one-shot), the
  current IP and state are saved, "inside handler" := yes, state := READY, IP := target, and the script runs
  (even if it was FINISHED or waiting). Only the first true watch fires in a frame.
- RESUME: if not inside a handler → game error `Resume command executed outside of ON branch`; else "inside
  handler" := no, IP and state are restored from the saved values (the script continues to wait for what it was
  waiting for), and the script yields.
- See 3.4 for what queueing an event inside a handler does.

## 11. MOVE (0x19) and ROTATE (0x1C)
Stack (push order): entity, vector, [flag 0x01 duration in seconds], [flag 0x02 speed], [flag 0x04 signal
variable]. Flag 0x08 (MOVE only for the timed form): the vector is an absolute destination; otherwise it is
relative to the current position. No entity → nothing happens.
- **Instant form** (neither 0x01 nor 0x02): MOVE: origin := vector (flag 0x08) or origin + vector. ROTATE: angles :=
  angles + vector (always relative). In both cases the entity's linked follower entity (the entity it carries,
  e.g. a mover's rider; UNVERIFIED which relation) gets the same **relative** change. No relink, no event, no
  signal.
- **Timed form**: destination D (MOVE: position; ROTATE: angles, flag 0x08 also honoured), distance
  d = |current − D| (units, or degrees as a 3-vector length for ROTATE). Speed s = the 0x02 value; with only 0x01,
  s = d / duration. With **both** 0x01 and 0x02 the original computes the destination as current position +
  normalize(current − D) × (speed × duration) — i.e. *away* from the requested point (bug; never used by stock
  data, UNVERIFIED). Then frames N = floor(10 × d / s) + 1 (N = 0 when d = 0); the entity gets velocity (or angular
  velocity) = direction × (d / N) × 10, so it covers d in N server frames; a done event is queued for level
  time + N × 0.1; the signal (if any) is registered. MOVE's done event snaps the origin to D; ROTATE's only stops
  the rotation. Note the extra frame: `move(e, v, 3 s)` takes 3.1 s (or 3.0 s depending on float rounding).
- The entity's movement type is not changed: the mover must already integrate velocity (scripts set `movetype`).
- Debug flags 0x02/0x04 print "Moving Entity n From (…) To (…)" / "Rotating Entity …".

## 12. Actors and other commands

### 12.1 AI (0x21): operands
int32 flags. Stack in push order: actor entity, command id, then the optional arguments in this order:
| Flag | Argument | Default | Meaning |
|---|---|---|---|
| 0x40 | float | 50 s (9999.9 s when 0x400 is non-zero) | duration of the action (12.4) |
| 0x01 | int | 1 | repeat count: the action is queued this many times |
| 0x02 | vector | (0,0,0) | facing direction; non-zero: the actor faces the point position + direction × 1000 |
| 0x04 | vector | (0,0,0) | destination, relative to the actor's position when the action starts (absolute with 0x20) |
| 0x08 | variable reference | — | signal variable (kind 2) |
| 0x10 | entity | — | popped and ignored |
| 0x80 | entity | none | target entity: when it exists and is in use and (has health > 0 or is an `info_notnull`), its origin replaces the destination and the facing point |
| 0x200 | int | 0 | passed to the animation layer with the action (UNVERIFIED meaning) |
| 0x400 | int | 0 | expression form (12.5) |
| 0x20 | — | — | destination is absolute |
| 0x100 | — | — | the action does not end by duration/animation end; it ends when the actor's movement state reaches "landed" (internal states 13/14; UNVERIFIED meaning) and the flag is passed to the animation layer |
(Corrects SPEC-140's unordered list: the full push order is entity, command, 0x40, 0x01, 0x02, 0x04, 0x08,
0x10, 0x80, 0x200, 0x400.)

### 12.2 AI: execution
If the actor has no AI the console message of 2.4 is printed and nothing else happens. Otherwise: the actor's
scripted behaviour is found or created (a behaviour of highest scripted priority that runs queued script
actions); if 0x400 is zero the command id is looked up in the table of 12.7 giving (animation, action kind,
default speed); if a signal variable is given it is registered (kind 2) and an AI-done event is queued; then the
action is appended `count` times to the actor's action queue (the AI-done event is attached to the last copy
only). Finally the actor is activated: the first time this happens to an actor it becomes solid (bounding box), gets
the stepping movement type and loses spawnflag 0x10, and its call-for-help flag is cleared (SPEC-700 "waking up"). A count ≤ 0 queues nothing and a pending AI-done event then never fires.

### 12.3 SOUND (0x1E)
u8 flags. Pops, in this order: file name (always present, pushed **last**), [0x10 time offset float], [0x08
channel int], [0x04 attenuation float], [0x02 volume float], [0x01 entity]. Defaults: entity none, volume 1.0,
attenuation 1.0, channel 2, time offset 0.0. Plays the sound (index of the file name) on the entity with those
parameters (SPEC-001 import 22, not local). Skipped while a cinematic is being skipped (SPEC-751 §1.4).
Correction to SPEC-140: bit 0x01 is the entity flag (the file name is unconditional), 0x02 is volume (stock 0.9),
0x04 attenuation, 0x08 channel (stock 0, 2, 8 = Quake II channel numbers and the "no PHS add" bit), 0x10 time
offset.

### 12.4 Scripted action life cycle (contract with the AI)
Each queued action is executed by the actor's scripted behaviour in queue order. An action completes when:
- its duration has elapsed (always), or
- 0x100 is clear and either: the kind is 3 or 4 (jump/fall: complete as soon as started); or the animation has
  finished and the 0x40 flag was not given (with an explicit duration the animation end is ignored) and the kind is
  not 1, or it is kind 1 with an animation that is flagged "ends on animation end"; or
- for kind 1 (move): the actor is within 11 units horizontally of the destination, or has passed it (moving away
  from it relative to the start direction).
When an action completes its AI-done marker (if any) is set; the next action starts. When the queue is empty the
actor plays a standing idle animation and stays under script control until RESET_AI or until its AI decides
otherwise (AI spec). Kinds: 0, 5, 6 play the animation in place; 1 moves toward the destination using the
animation (no animation → the actor's normal run/walk is chosen); 2 attacks/fires toward the destination or
target using the animation; 3 jump and 4 fall play the animation and complete immediately; 8 plays the default
stand (`std_istand_n_a_n`). The default speed limits how far the steering point may be placed from the actor per
update (0.2 × speed units; not for kinds 3 and 4). Animations named in 12.7 are human-body animation records
(SPEC of the body/animation package); an animation unavailable for the actor's body prints
`script anim: <name> not available for <actor>, using <other>!` and a substitute is used.

### 12.5 Expression form (0x400 non-zero)
The command id is ignored and no animation is chosen. If the actor's script action queue is empty the actor's
**face/expression state** is set immediately to the 0x400 value for the duration (default 9999.9 s); otherwise an
action carrying the expression is queued and applied when it is reached. The scripting tool emits
`ai(actor, N, f = seconds, i400 = N)` with N = 6 (126 uses; the duration equals the speech line length: talking
face), 7 (16), 3 (13), 2 (2), 8 (1). After the duration the expression returns to neutral (1). Expression states are
defined by the body (SPEC-722/SPEC-700): 2 afraid, 3 face suffix `m`, 4 pain, 5 dead, 6 talking; while the state is
4 or 5 only 5 can replace it. Meanings of 7 and 8 UNVERIFIED.

### 12.6 AI completion signal
The AI-done event fires (signal kind 2 for the actor) in the first script frame after the last copy of the action
has completed. RESET_AI or the actor's death removes queued actions without completing them: the signal then never
fires.

### 12.8 HELI (0x28) and VEHICLE (0x2D)
u8 flags; push order: entity, command id, [0x01 entity], [0x02 vector], [0x04 scalar], [0x08 signal variable]
(as SPEC-140). The entity must have AI (helicopter AI for HELI; tank/snowcat AI for VEHICLE); otherwise nothing
happens and no event is queued. The scripted behaviour is found or created like 12.2; the signal (kind 4 / 5) is
registered; an action is created from (command id, vector, entity, scalar) and appended to the vehicle AI's queue;
a done event is queued which fires when the vehicle AI has no current action and the identifier of its last
completed action equals that of this command.
- HELI ids 1–10, 22, 23, 33–45, 49 create a "flight" action; 12–21 a second action family; 24–32 a third family;
  46 only sets a helicopter flag (no action; the done event then waits for an action id that never completes —
  UNVERIFIED); 47 and 48 create a flight action whose target entity is the helicopter itself; other ids create no
  action. Stock ids: 1, 5, 6, 14, 22–27, 29, 34–38, 40–43, 48. The meaning of each id is defined by the
  helicopter AI (enemy-roster spec, open question 1).
- VEHICLE ids 1–5 create an action; others none (the done event never fires — UNVERIFIED). Stock ids 1–5.

### 12.9 USE (0x1B)
Pop entity. If a cinematic is running and the player (entity 1) has health < 1: the cinematic is ended and the
script yields (the use is not performed). Otherwise, if the entity exists, is in use and has a use behaviour, it is
used with other = the script's caller and activator = the script's activator; if the entity has AI, the
character's call-for-help flag is also cleared (SPEC-700). If the use changed the cinematic state
and the player is dead, the cinematic is ended and the script yields.

### 12.7 AI command table
Command id → action kind, default speed, animation. Ids not listed (0, 3, 4, 10–14, 16–19, 28, 135 and every id > 354) use kind 0, speed 0, no animation (UNVERIFIED: the actor's idle is played). "Uses" and "flags" are
the stock-data counts and the AI flag words (hex) seen with that id (from SPEC-140's inventory). Ids used only with
flag 0x400 (2, 3, 6, 7, 8 in that form) are expression commands (12.5) and ignore this table.
Action kinds: 0 play in place, 1 move, 2 attack/fire, 3 jump, 4 fall, 5 and 6 play in place (reaction/death
animations), 8 stand. Speeds 150 = run, 10 = walk, others as listed.

| Id | Kind | Speed | Animation | Uses | Flags |
|---|---|---|---|---|---|
| 1 | 1 | 150 | — (none) | 8 | `4,c` |
| 2 | 3 | 10 | — (none) | 2 | `440` |
| 3 | 0 | — | — (none) | 13 | `440` |
| 5 | 6 | — | `std_stoknees_2bk_n_n` | 0 |  |
| 6 | 6 | 150 | `std_sblown_2bk_n_n` | 127 | `400,440` |
| 7 | 0 | — | `std_xcrouch_dwn_pk_a` | 16 | `440` |
| 8 | 0 | — | `cch_xcrouch_mid_pk_a` | 1 | `440` |
| 9 | 0 | — | `cch_xcrouch_up_pk_a` | 0 |  |
| 15 | 1 | 10 | — (none) | 3 | `4,c` |
| 20 | 0 | — | `std_icleangun2_n_pk_n` | 2 | `40` |
| 21 | 0 | — | `std_ilooking_n_a_a` | 7 | `0,40` |
| 22 | 0 | — | `std_imonitorup_n_a_n` | 0 |  |
| 23 | 0 | — | `std_italk_n_a_n` | 7 | `40` |
| 24 | 0 | — | `std_italkgest1_n_a_n` | 0 |  |
| 25 | 0 | — | `std_italkgest2_n_a_n` | 0 |  |
| 26 | 0 | — | `std_italkgest3_n_a_n` | 0 |  |
| 27 | 0 | — | `std_iwipebrow_n_a_n` | 0 |  |
| 29 | 0 | — | `std_ilooking_n_mrs_2` | 7 | `40` |
| 30 | 0 | — | `std_xalarmpush_n_a_n` | 5 | `0,8` |
| 31 | 0 | — | `std_istand_n_mrs_2` | 9 | `0,40` |
| 32 | 0 | — | `std_italk_n_mrs_2` | 2 | `40` |
| 33 | 0 | — | `std_iturnvalve_frm_n_n` | 1 | `0` |
| 34 | 0 | — | `std_iturnvalve_n_n_n` | 0 |  |
| 35 | 0 | — | `std_iturnvalve_to_n_n` | 0 |  |
| 36 | 4 | — | `std_jdrop_n_a_a` | 34 | `0,4,8,c,40` |
| 37 | 0 | — | `std_mkicklow_n_a_a` | 4 | `0,40` |
| 38 | 0 | — | `std_mpistolwhip_n_p_n` | 2 | `8` |
| 39 | 6 | — | `std_sinback_2frnt_n_n` | 6 | `0,4` |
| 40 | 0 | — | `std_xyellpoint_fwd_a_n` | 9 | `0,8` |
| 41 | 0 | — | `std_xyellwave_bk_a_n` | 18 | `0,8,40` |
| 42 | 2 | — | `std_fattitude_n_p_a` | 1 | `380` |
| 43 | 5 | — | `ls_xgetkicked_n_n_n` | 0 |  |
| 44 | 0 | — | `si_xbywall_n_n_n` | 0 |  |
| 45 | 6 | — | `std_sstumble_2bk_p_n` | 3 | `0,4` |
| 46 | 1 | — | `std_wpush_n_n_n` | 4 | `4,c,44` |
| 47 | 0 | — | `std_xafraidshake_n_n_n` | 5 | `0,40` |
| 48 | 0 | — | `std_xkickdoor_n_a_a` | 20 | `0,8,c` |
| 49 | 0 | — | `std_xyellatyou_n_a_a` | 13 | `0,8,40,48` |
| 50 | 1 | 10 | `std_wcarry1_n_n_n` | 0 |  |
| 51 | 1 | 10 | `std_wnormal_n_pk_a` | 75 | `4,c,24,2c,44,4c` |
| 52 | 1 | 10 | `std_wpatrol_n_mrs_2` | 41 | `4,c,24,2c` |
| 53 | 1 | 10 | `std_wbcksignal_n_n_n` | 2 | `4` |
| 54 | 0 | — | `std_ileanlook_frm_pk_n` | 1 | `c` |
| 55 | 0 | — | `std_ileanlook_mid_pk_n` | 0 |  |
| 56 | 0 | — | `std_ileanlook_to_pk_n` | 1 | `0` |
| 57 | 3 | — | `std_jdown_n_a_a` | 10 | `0,4,c` |
| 58 | 2 | — | `std_f_fwd_p_2` | 90 | `0,8,40,80,88,c0,c8,180,188,1c8,2c0,380,388` |
| 59 | 2 | — | `std_f_fwd_s_2` | 38 | `0,8,80,88,180,188,380` |
| 60 | 1 | 150 | `std_r_n_mrs_2` | 185 | `4,c,24` |
| 61 | 1 | 150 | `std_r_n_pk_n` | 189 | `4,c,24,2c,44,4c` |
| 62 | 1 | 150 | `std_rgunup_n_p_2` | 43 | `4,c,24,2c,44` |
| 63 | 0 | — | `std_xdontshoot_n_a_n` | 2 | `8,40` |
| 64 | 0 | — | `std_xfearwave_n_n_n` | 1 | `0` |
| 65 | 2 | — | `std_fcornerpeak_lt_p_a` | 9 | `0,80,88,180` |
| 66 | 2 | — | `std_fcornerpeak_rt_p_a` | 8 | `8,80,88` |
| 67 | 0 | — | `std_ialert_n_p_n` | 37 | `0,40` |
| 68 | 0 | — | `std_icleangun2_n_mrs_2` | 4 | `0,40` |
| 69 | 0 | — | `std_imonitorup2_n_a_n` | 0 |  |
| 70 | 3 | — | `std_jdive2prone_n_a_n` | 7 | `0,4,40` |
| 71 | 1 | 150 | `std_rafraid_n_n_n` | 17 | `4,c,24,4c` |
| 72 | 6 | — | `std_sblown_2bk_n_n` | 10 | `0,4,40` |
| 73 | 0 | — | `std_xboomcrouch_n_a_n` | 8 | `0,40` |
| 74 | 0 | — | `std_xtumble_2frnt_n_n` | 0 |  |
| 75 | 3 | 150 | `std_jrun_fwd_a_a` | 28 | `0,4,c,24` |
| 76 | 0 | — | `std_ismoke_frm_a_n` | 5 | `8` |
| 77 | 0 | — | `std_ismoke_n_a_n` | 7 | `0,9` |
| 78 | 0 | — | `std_ismoke_to_a_n` | 1 | `0` |
| 79 | 0 | — | `std_itouchnuts_n_a_n` | 3 | `40` |
| 80 | 0 | — | `cch_xcrouch_up_mrs_2` | 5 | `0,40` |
| 81 | 0 | — | `cch_xcrouch_up_pk_a` | 8 | `0,8` |
| 82 | 0 | — | `cch_a_fwd_p_2` | 14 | `0,40,80` |
| 83 | 0 | — | `cch_a_fwd_r_2` | 4 | `40` |
| 84 | 6 | — | `cch_sonknees_2frnt_n_n` | 4 | `0` |
| 85 | 0 | — | `cch_xonknees_n_n_n` | 3 | `0,40` |
| 86 | 0 | — | `cch_xcchcower_mid_a_a` | 0 |  |
| 87 | 0 | — | `cch_xcchcower2_mid_a_a` | 0 |  |
| 88 | 0 | — | `prn_xcoverhead_n_a_a` | 0 |  |
| 89 | 6 | — | `std_sviolent_n_n_n` | 1 | `c` |
| 90 | 6 | — | `lb_pgenericdie_n_n_n` | 5 | `0` |
| 91 | 1 | 10 | `std_wattitude_n_pk_a` | 2 | `4,c` |
| 92 | 1 | 10 | `std_wjog_n_mrs_2` | 0 |  |
| 93 | 1 | 10 | `std_wjoggundwn_n_p_2` | 0 |  |
| 94 | 1 | 10 | `std_wjoggunup_n_p_a` | 7 | `4,2c` |
| 95 | 1 | 10 | `std_wpushcart_n_n_n` | 2 | `4,c` |
| 96 | 2 | — | `std_fautosweep_n_pm_a` | 18 | `0,8,40,80,88,c0,380` |
| 97 | 2 | — | `std_fgangbanger_n_m_2` | 4 | `0,40,380` |
| 98 | 2 | — | `std_fswatstyle_fwd_m_2` | 3 | `80,c0,380` |
| 99 | 8 | — | — (none) | 449 | `0,8` |
| 100 | 2 | — | `std_fviolent_dwn_m_2` | 2 | `0` |
| 101 | 2 | — | `std_fviolent_dwn_p_n` | 3 | `0,180,3c0` |
| 102 | 3 | 150 | `std_jupto_n_a_a` | 1 | `4` |
| 103 | 0 | — | `std_mninjakick_n_a_b` | 2 | `0` |
| 104 | 0 | — | `std_mriflebutt_n_mrs_2` | 3 | `0` |
| 105 | 1 | 150 | `std_r_dwnst_mrs_2` | 0 |  |
| 106 | 1 | 150 | `std_raimstraf_lt_mrs_2` | 0 |  |
| 107 | 1 | 150 | `std_raimstraf_lt_p_cbn` | 0 |  |
| 108 | 1 | 150 | `std_raimstraf_rt_mrs_2` | 0 |  |
| 109 | 1 | 150 | `std_raimstraf_rt_p_cbn` | 0 |  |
| 110 | 1 | 150 | `std_rbackaim_n_mrs_2` | 0 |  |
| 111 | 1 | 150 | `cch_xonknees_n_n_n` | 0 |  |
| 112 | 6 | — | `std_sfallfwd_2frnt_n_n` | 0 |  |
| 113 | 6 | — | `std_sstumble_2frnt_n_n` | 1 | `0` |
| 114 | 0 | — | `std_xcchcower_dwn_a_a` | 0 |  |
| 115 | 0 | — | `std_xchoke_n_n_n` | 3 | `40` |
| 116 | 0 | — | `std_xdance_n_n_n` | 0 |  |
| 117 | 0 | — | `std_xgoprone_n_a_a` | 3 | `0,8` |
| 118 | 1 | 10 | `std_xladder_dwn_a_a` | 1 | `4` |
| 119 | 1 | 10 | `std_xladder_up_a_a` | 1 | `1` |
| 120 | 0 | — | `std_xroll_lt_a_a` | 8 | `0,8,80` |
| 121 | 0 | — | `std_xroll_rt_a_a` | 3 | `0,8` |
| 122 | 0 | — | `std_xrolltocch_fwd_a_a` | 6 | `0,4,c` |
| 123 | 0 | — | `std_xstartled_n_a_a` | 16 | `0,8` |
| 124 | 0 | — | `std_xstumble_n_a_a` | 0 |  |
| 125 | 0 | — | `si_isit_n_a_a` | 0 |  |
| 126 | 0 | — | `si_xalertstand_up_a_a` | 1 | `0` |
| 127 | 0 | — | `prn_c_n_a_2` | 1 | `48` |
| 128 | 0 | — | `prn_xcoverhead_to_a_a` | 0 |  |
| 129 | 4 | — | `prn_xfalling_n_n_n` | 17 | `0,4,8` |
| 130 | 6 | — | `prn_xfallland_n_n_n` | 15 | `0` |
| 131 | 0 | — | `prn_xtostand_n_a_a` | 2 | `0` |
| 132 | 1 | 150 | `cch_raim_fwd_mrs_2` | 2 | `4` |
| 133 | 1 | 150 | `cch_raim_fwd_p_2` | 0 |  |
| 134 | 0 | — | `cch_xcchcower2_mid_a_a` | 5 | `0,40` |
| 136 | 6 | — | `lb_xleanupshoot_n_p_n` | 1 | `80` |
| 137 | 0 | — | `cch_a_fwd_ms_2` | 8 | `0,8,40,80` |
| 138 | 2 | — | `cch_f_fwd_l_2` | 1 | `88` |
| 139 | 1 | 150 | `cch_raim_n_l_2` | 0 |  |
| 140 | 2 | — | `cch_tlobgrenade_n_a_n` | 4 | `0` |
| 141 | 0 | — | `std_echokeknees_to_n_n` | 4 | `0` |
| 142 | 0 | — | `cch_echokeknees_n_n_n` | 2 | `0` |
| 143 | 0 | — | `cch_echokedeath_n_n_n` | 5 | `0` |
| 144 | 0 | — | `std_selectrocute_n_n_n` | 3 | `40` |
| 145 | 0 | — | `std_eoffladder_up_a_n` | 0 |  |
| 146 | 0 | — | `std_epounddoor_n_a_n` | 5 | `40` |
| 147 | 2 | — | `std_f_fwd_r_2` | 8 | `80,180,380` |
| 148 | 0 | — | `std_ebustglass_n_mrs_2` | 1 | `0` |
| 149 | 0 | — | `std_ecomehere_n_a_n` | 5 | `0` |
| 150 | 0 | — | `std_efalldeath_n_n_n` | 1 | `0` |
| 151 | 6 | — | `std_efallimpact_n_n_n` | 1 | `0` |
| 152 | 0 | — | `std_efliptable_n_a_n` | 3 | `0` |
| 153 | 0 | — | `std_epushbuttons_n_n_n` | 1 | `8` |
| 154 | 1 | — | `std_epushcrate_n_a_n` | 2 | `4,40` |
| 155 | 0 | — | `std_esignalalarm_n_a_n` | 0 |  |
| 156 | 0 | — | `std_etyping_n_n_n` | 8 | `9,40` |
| 157 | 1 | — | `std_ewall2walk_n_mrs_2` | 0 |  |
| 158 | 1 | — | `std_ewall2walk_n_pk_n` | 0 |  |
| 159 | 0 | — | `std_ewallidle_n_mrs_2` | 0 |  |
| 160 | 0 | — | `std_ewallidle_n_pk_n` | 2 | `40` |
| 161 | 0 | — | `std_ewalllook_lt_mrs_2` | 1 | `40` |
| 162 | 0 | — | `std_ewalllook_lt_pk_n` | 1 | `40` |
| 163 | 0 | — | `std_ewalllook_rt_mrs_2` | 1 | `40` |
| 164 | 0 | — | `std_ewalllook_rt_pk_n` | 0 |  |
| 165 | 0 | — | `std_ewrenchh_frm_n_n` | 4 | `8` |
| 166 | 0 | — | `std_ewrenchh_n_n_n` | 3 | `0,40` |
| 167 | 0 | — | `std_ewrenchh_to_n_n` | 4 | `0,8` |
| 168 | 0 | — | `std_ewrenchv_frm_n_n` | 4 | `0,8` |
| 169 | 0 | — | `std_ewrenchv_n_n_n` | 2 | `0,40` |
| 170 | 0 | — | `std_ewrenchv_to_n_n` | 4 | `0,8` |
| 171 | 0 | — | `std_ashuffle_n_p_2` | 0 |  |
| 172 | 0 | — | `std_ashuffle_n_p_a` | 0 |  |
| 173 | 0 | — | `std_ashuffle_n_ms_2` | 0 |  |
| 174 | 0 | — | `std_ashuffle_n_r_2` | 0 |  |
| 175 | 0 | — | `std_ashuffle_n_l_2` | 0 |  |
| 176 | 0 | — | `std_ipiss_n_a_a` | 0 |  |
| 177 | 0 | — | `std_athreat_dwn_p_n` | 2 | `40` |
| 178 | 0 | — | `std_athreat_fwd_p_n` | 0 |  |
| 179 | 0 | — | `std_athreatyel_dwn_p_n` | 1 | `40` |
| 180 | 0 | — | `std_athreatyel_fwd_p_n` | 0 |  |
| 181 | 0 | — | `std_eexaminewall_n_a_n` | 0 |  |
| 182 | 2 | — | `std_erageshoot_n_p_n` | 1 | `380` |
| 183 | 0 | — | `std_esabremock_n_a_n` | 3 | `0,40` |
| 184 | 1 | 150 | `std_rtostop_n_pk_n` | 0 |  |
| 185 | 0 | — | `std_etalkboth_n_a_n` | 14 | `0,1,40` |
| 186 | 0 | — | `std_etalk_n_a_n` | 134 | `0,40` |
| 187 | 0 | — | `std_eheadnod_n_a_n` | 10 | `0,8,40` |
| 188 | 0 | — | `std_eheadshake_n_a_n` | 5 | `1,8,40` |
| 189 | 3 | — | `std_ejumprail_n_a_n` | 6 | `4,8,c` |
| 190 | 1 | 10 | `std_wtostop_n_pk_a` | 0 |  |
| 191 | 0 | — | `std_espin180_lt_a_n` | 7 | `0,8` |
| 192 | 0 | — | `std_espin180_rt_a_n` | 8 | `0,8` |
| 193 | 1 | 150 | `std_etorun_n_p_n` | 0 |  |
| 194 | 0 | — | `std_ilean_n_p_n` | 1 | `0` |
| 195 | 1 | 10 | `std_wbackaim_n_ms_2` | 3 | `4,44` |
| 196 | 0 | — | `std_etalkright_n_a_n` | 22 | `40` |
| 197 | 0 | — | `std_eclimbledg_dwn_a_n` | 1 | `8` |
| 198 | 0 | — | `std_eclimbledg_mid_a_n` | 2 | `0,40` |
| 199 | 0 | — | `std_eclimbledge_up_a_n` | 7 | `0,8` |
| 200 | 0 | — | `std_edropgrenade_n_a_n` | 1 | `0` |
| 201 | 0 | — | `std_efrmshadow_n_mrs_2` | 0 |  |
| 202 | 0 | — | `std_efrmshadow_n_p_n` | 4 | `0,8` |
| 203 | 0 | — | `std_ehandtalk_frm_p_n` | 3 | `0,40` |
| 204 | 0 | — | `std_ehandtalk_n_p_n` | 3 | `0,40` |
| 205 | 0 | — | `std_ehandtalk_to_mrs_n` | 0 |  |
| 206 | 0 | — | `std_ehandtalk_to_p_n` | 3 | `0,40,48` |
| 207 | 0 | — | `std_ehandtalk_frm_mrs_n` | 0 |  |
| 208 | 0 | — | `std_eheadset_frm_p_n` | 2 | `0` |
| 209 | 0 | — | `std_eheadset_mid_p_n` | 2 | `40` |
| 210 | 0 | — | `std_eheadset_to_p_n` | 2 | `0,8` |
| 211 | 0 | — | `std_esalute_frm_mrs_n` | 0 |  |
| 212 | 0 | — | `std_esalute_n_mrs_n` | 0 |  |
| 213 | 0 | — | `std_esalute_to_mrs_n` | 0 |  |
| 214 | 0 | — | `std_etalkleft_n_a_n` | 32 | `0,40` |
| 215 | 0 | — | `std_etalklklt_frm_a_n` | 4 | `0,40` |
| 216 | 0 | — | `std_etalklklt_mid_a_n` | 5 | `40` |
| 217 | 0 | — | `std_etalklklt_to_a_n` | 6 | `0,40` |
| 218 | 0 | — | `std_etalklkrt_frm_a_n` | 0 |  |
| 219 | 0 | — | `std_etalklkrt_mid_a_n` | 1 | `40` |
| 220 | 0 | — | `std_etalklkrt_to_a_n` | 0 |  |
| 221 | 1 | 150 | `std_etorun_n_mrs_2` | 0 |  |
| 222 | 0 | — | `std_eturn90_lt_a_n` | 6 | `0,8` |
| 223 | 0 | — | `std_eturn90_rt_a_n` | 7 | `0,8` |
| 224 | 0 | — | `std_igunup_n_a_n` | 9 | `0,8,40,48` |
| 225 | 1 | 150 | `std_ronfire_n_n_n` | 6 | `4,c` |
| 226 | 1 | 150 | `std_ronfiretodth_n_n_n` | 2 | `0` |
| 227 | 1 | 150 | `std_rtostop_n_mrs_2` | 0 |  |
| 228 | 6 | — | `std_sshoulder_lt_n_n` | 2 | `0` |
| 229 | 0 | — | `std_sunhurt_bk_pk_n` | 0 |  |
| 230 | 0 | — | `std_edropcrouch_n_a_a` | 1 | `8` |
| 231 | 1 | 150 | `std_trun_n_a_n` | 0 |  |
| 232 | 1 | 10 | `std_w_n_pn_n` | 15 | `4` |
| 233 | 1 | 10 | `std_wcarry2_n_n_n` | 0 |  |
| 234 | 1 | 10 | `std_wtalk_lt_mrs_2` | 2 | `4` |
| 235 | 1 | 10 | `a4_cscramble_n_a_n` | 2 | `4,2c` |
| 236 | 0 | — | `cch_enodleft_n_a_a` | 1 | `8` |
| 237 | 0 | — | `cch_enodright_n_a_a` | 1 | `8` |
| 238 | 0 | — | `cch_escantalk_lt_a_a` | 5 | `8,40` |
| 239 | 0 | — | `cch_escantalk_rt_a_a` | 4 | `8` |
| 240 | 0 | — | `cch_iready_n_a_a` | 7 | `0,40,48` |
| 241 | 2 | — | `std_fautosweep_n_m_2` | 5 | `0,80,180` |
| 242 | 0 | — | `std_l_n_ms_2` | 4 | `0,40` |
| 243 | 0 | — | `std_istand_n_a_n` | 22 | `0,8,40` |
| 244 | 2 | — | `std_echokeshoot_n_a_n` | 0 |  |
| 245 | 6 | — | `std_sonfire_2bk_n_n` | 5 | `0` |
| 246 | 0 | — | `std_etalk_frm_a_n` | 1 | `40` |
| 247 | 0 | — | `std_etalk_to_a_n` | 2 | `40` |
| 248 | 3 | 150 | `std_jdrop_to_a_n` | 3 | `0,4` |
| 249 | 2 | — | `std_athreatsht_dwn_p_n` | 1 | `388` |
| 250 | 6 | — | `lb_pholdleg_lt_n_n` | 3 | `0,40` |
| 251 | 0 | — | `std_xcock_n_s_2` | 10 | `0,40` |
| 252 | 6 | — | `std_stoknees_2bk_n_n` | 0 |  |
| 253 | 6 | — | `std_sgut_2side_n_n` | 3 | `0` |
| 254 | 1 | 10 | `std_xladderfrm_up_a_a` | 1 | `0` |
| 255 | 5 | — | `std_sgunfromhand_n_n_n` | 1 | `0` |
| 256 | 0 | — | `cch_etostand_n_a_n` | 1 | `0` |
| 257 | 0 | — | `std_ininja_n_p_n` | 5 | `40,48` |
| 258 | 1 | 100 | `std_xhandspring_fwd_a_n` | 1 | `0` |
| 259 | 0 | — | `std_egangsalute_n_a_n` | 2 | `0,8` |
| 260 | 1 | 10 | `std_wbackwards_n_a_n` | 2 | `4,44` |
| 261 | 6 | — | `std_ssabredeath_bk_n_n` | 2 | `0` |
| 262 | 1 | 10 | `std_wstealth_n_pk_n` | 10 | `4,c,6c` |
| 263 | 3 | 150 | `std_jthrowstar_fwd_a_n` | 1 | `0` |
| 264 | 0 | — | `prn_a_n_a_2` | 1 | `40` |
| 265 | 0 | — | `std_eyell_n_a_n` | 1 | `40` |
| 266 | 0 | — | `std_ialertlk_rt_mrs_2` | 1 | `40` |
| 267 | 0 | — | `std_epushbuttonh_n_a_n` | 1 | `40` |
| 268 | 0 | — | `std_ialert_n_mrs_2` | 17 | `0,40` |
| 269 | 1 | 150 | `std_rdynamic_n_a_n` | 1 | `4` |
| 270 | 1 | 10 | `std_waim_fwd_p_2` | 2 | `4,44` |
| 271 | 0 | — | `std_a_fwd_p_2` | 8 | `40` |
| 272 | 0 | — | `std_ietalkpose_n_a_n` | 91 | `0,40` |
| 273 | 2 | — | `std_frapidsweep_lt_p_2` | 1 | `0` |
| 274 | 0 | — | `std_ialerttrn_lt_p_n` | 4 | `0,40` |
| 275 | 0 | — | `std_esuprise90_lt_a_n` | 0 |  |
| 276 | 2 | — | `std_fautosweep_up_m_2` | 0 |  |
| 277 | 0 | — | `std_xatkpause1_n_mrs_2` | 0 |  |
| 278 | 1 | 5 | `lb_pcrawl_bk_p_n` | 2 | `40` |
| 279 | 0 | — | `si_ewallidle_n_a_n` | 2 | `40` |
| 280 | 0 | — | `si_ewalltalk_n_a_n` | 1 | `40` |
| 281 | 0 | — | `std_etoaim_n_p_2` | 2 | `0,8` |
| 282 | 0 | — | `std_a_up_p_2` | 1 | `40` |
| 283 | 0 | — | `si_ewallfear_n_a_n` | 1 | `40` |
| 284 | 0 | — | `std_esalute_to_a_n` | 3 | `0` |
| 285 | 0 | — | `std_esalute_n_a_n` | 4 | `40` |
| 286 | 0 | — | `std_esalute_frm_a_n` | 3 | `0,8` |
| 287 | 0 | — | `std_esalutations_n_a_n` | 0 |  |
| 288 | 0 | — | `std_eclimbshaft_n_a_n` | 1 | `40` |
| 289 | 0 | — | `std_xatkpause2_n_pk_n` | 0 |  |
| 290 | 1 | 50 | `cch_w_n_a_n` | 1 | `4` |
| 291 | 0 | — | `cch_ewalk2cch_n_a_n` | 1 | `0` |
| 292 | 0 | — | `cch_etalk_dwn_p_n` | 1 | `40` |
| 293 | 0 | — | `std_italkpose_up_a_n` | 1 | `40` |
| 294 | 0 | — | `cch_italkpose_dwn_a_n` | 1 | `40` |
| 295 | 0 | — | `std_etalk_up_a_n` | 1 | `40` |
| 296 | 0 | — | `cch_eheadnod_n_a_n` | 0 |  |
| 297 | 0 | — | `cch_ecch2walk_n_a_n` | 0 |  |
| 298 | 0 | — | `std_xatkpause1_n_pk_n` | 0 |  |
| 299 | 1 | 10 | `std_wfast_n_a_n` | 1 | `4` |
| 300 | 0 | — | `std_erageidle_n_p_n` | 0 |  |
| 301 | 0 | — | `cch_xcchcower_up_a_a` | 4 | `8` |
| 302 | 0 | — | `std_ietalkpose_rt_a_n` | 1 | `40` |
| 303 | 0 | — | `std_ebooklook_n_a_n` | 0 |  |
| 304 | 0 | — | `std_ebookscan_n_a_n` | 0 |  |
| 305 | 0 | — | `std_xopendoor_n_a_n` | 0 |  |
| 306 | 0 | — | `std_xcrouch_dwn_pk_a` | 2 | `0` |
| 307 | 0 | — | `cch_eheadset_frm_a_n` | 1 | `8` |
| 308 | 0 | — | `cch_eheadset_mid_a_n` | 1 | `8` |
| 309 | 6 | — | `std_shead_2frnt_n_n` | 1 | `0` |
| 310 | 1 | 10 | `std_w_n_mrs_2` | 0 |  |
| 311 | 0 | — | `std_edkraim_n_a_n` | 4 | `40` |
| 312 | 0 | — | `cch_ehawkknees_n_n_n` | 10 | `40` |
| 313 | 0 | — | `std_edkraimtalk_n_a_n` | 6 | `40` |
| 314 | 0 | — | `std_edkrfirelk_rt_a_n` | 1 | `0` |
| 315 | 6 | — | `cch_ehawkdeath_n_n_n` | 1 | `0` |
| 316 | 0 | — | `cch_ehawkkneetlk_n_n_n` | 1 | `40` |
| 317 | 0 | — | `cch_echokedeath_n_n_n` | 0 |  |
| 318 | 0 | — | `cch_eheadset_to_a_n` | 2 | `8,40` |
| 319 | 0 | — | `lb_eslide_n_pk_n` | 1 | `40` |
| 320 | 0 | — | `std_a_fwd_p_cb` | 4 | `40` |
| 321 | 0 | — | `std_igunuplook_n_a_n` | 0 |  |
| 322 | 2 | — | `std_frapid_n_p_2` | 5 | `c0,380` |
| 323 | 0 | — | `std_edkraimlk_fwd_a_n` | 0 |  |
| 324 | 0 | — | `std_ewhirl_rt_a_n` | 1 | `8` |
| 325 | 0 | — | `std_fsweepup_frm_a_a` | 0 |  |
| 326 | 0 | — | `std_fsweepup_to_a_a` | 0 |  |
| 327 | 1 | 10 | `std_xtakecover_n_a_n` | 0 |  |
| 328 | 0 | — | `std_esabrebluff_n_a_a` | 1 | `40` |
| 329 | 2 | — | `std_eautosweep_n_pm_a` | 1 | `0` |
| 330 | 2 | — | `cch_f_fwd_s_2` | 0 |  |
| 331 | 2 | — | `std_f_fwd_p_cb` | 7 | `0,8,80,180` |
| 332 | 4 | — | `std_soutwindow_n_a_n` | 1 | `0` |
| 333 | 0 | — | `std_xcrouch_dwn_mrs_2` | 1 | `0` |
| 334 | 0 | — | `std_xcrouch_dwn_p_2` | 0 |  |
| 335 | 2 | — | `std_tgrenade_n_a_n` | 1 | `0` |
| 336 | 2 | — | `std_tunder_n_a_n` | 0 |  |
| 337 | 0 | — | `cch_xcrouch_up_l_2` | 0 |  |
| 338 | 0 | — | `std_xcrouch_dwn_l_2` | 0 |  |
| 339 | 0 | — | `std_xcrouch_dwn_p_a` | 0 |  |
| 340 | 1 | 150 | `std_raim_n_mrs_2` | 0 |  |
| 341 | 1 | 10 | `std_wfire_fwd_ms_2` | 2 | `4` |
| 342 | 1 | 10 | `cch_afire_fwd_ms_2` | 0 |  |
| 343 | 2 | — | `std_f_fwd_m_2` | 9 | `40,80,380` |
| 344 | 2 | — | `cch_f_fwd_m_2` | 9 | `40,380` |
| 345 | 0 | — | `std_epushover_n_pk_a` | 1 | `0` |
| 346 | 0 | — | `std_emerchall_n_a_a` | 0 |  |
| 347 | 0 | — | `std_emerchone_n_a_a` | 0 |  |
| 348 | 0 | — | `std_iemerchant_n_a_a` | 0 |  |
| 349 | 0 | — | `std_emerchwave_rt_a_a` | 0 |  |
| 350 | 0 | — | `std_emerchtalk_n_a_a` | 0 |  |
| 351 | 0 | — | `std_emerchwave_lt_a_a` | 0 |  |
| 352 | 0 | — | `std_ebeg_n_a_a` | 0 |  |
| 353 | 0 | — | `std_icleangun2_n_pk_n` | 0 |  |
| 354 | 0 | — | `cch_xcchcower_n_a_n` | 0 |  |

## Constants
| Name | Value |
|---|---|
| Script file version | 3 (int32 at offset 0) |
| First instruction offset | 4 |
| Variable slots / field slots per script | 100 / 100 |
| Name length / string value length | 64 bytes / 63 characters + NUL |
| Instruction budget per activation | 1000 |
| Event time grid | floor((t + 0.05) × 10) × 0.1 |
| Default AI action duration | 50 s (9999.9 s with an expression value) |
| AI move arrival radius | 11 units, horizontal |
| AI steering step | 0.2 × default speed |
| Facing point distance | 1000 units |
| HELI/VEHICLE done-event nominal time | level time + 1000 s |
| Cinematic actor mark | `count` = 255 |
| Number of AI command ids with a table entry | 341 (ids 1–354) |
| Path format | `ds/%s.os` |

## Test vectors / how to verify
1. Instruction budget: a script `L: x += 1; GOTO L` (no WAIT) aborts the level with "Game Error: Runaway loop for
   script OR more than 1000 instructions" on its first activation.
2. WAIT quantization: a script started at level time 10.0 executing `wait 0.33` resumes at 10.3
   (floor((10.33 + 0.05) × 10) / 10); `wait 0` or `wait -1` does not yield.
3. MOVE timing: `move(e, (0,0,96), 3)` (relative, duration 3) on a `func_wall` with movetype push: speed 32,
   N = floor(10 × 96 / 32) + 1 = 31 frames, so it arrives exactly at +96 after 3.1 s; the signal variable becomes 1
   in the script frame of arrival.
4. Use while waiting: firing a runner again while its script is in a WAIT has no effect; after EXIT firing it has
   no effect; after SUSPEND firing it continues after the SUSPEND (`arm/runcomputer.os`: each use of the terminal
   runs one pass of its loop).
5. fn8: `x = fn8(2, 15)` over many runs yields integers 3 … 14 (15 extremely rarely, 2 never).
6. Expression: `ai(sam, 6, f = 2.3, i400 = 6)` (flags 0x440) on an idle actor shows the talking face for 2.3 s
   and does not move the actor.
7. Field read type rule: with `int t; t = e.wait;` on an entity whose wait is 2.5, t becomes 0; with a float
   variable it becomes 2.5.
8. Signals survive early completion: `move(e, v, 0.1, sig); wait 1; waitsignals(sig)` continues immediately at the
   WAIT_SIGNALS (sig is already 1) and sets sig back to 0 (bit 0x80).
9. Savegame cross-check: real 1.07f saves show the script state values used here (finished scripts have state 1
   and IP = code length; a script waiting on an AI command has one pending signal of kind 2 and one AI-done event;
   a helicopter script shows one helicopter-done event per queued HELI command), see SPEC-752 test vectors.

## Open questions
1. Meaning of each HELI command id (1–49) and VEHICLE id (1–5): defined by the helicopter and tank/snowcat AI
   (enemy-roster package). Verify by running `trn1/trn1_heli2.os` and the tank scripts and logging each command.
2. Meaning of AI flags 0x100 and 0x200 (passed to the animation layer) and of expression values 7 and 8.
3. The entity that receives the same relative change in instant MOVE/ROTATE (an internal entity link of the
   mover) — verify on a mover that carries another entity.
4. Whether writing `origin` through a field reference without relinking has visible effects (collision uses the
   old position until the entity is relinked) — in-game test.
5. Exact behaviour of a zero-distance timed MOVE (division by zero while computing the velocity; the done event
   fires in the same or next frame and snaps the origin).
