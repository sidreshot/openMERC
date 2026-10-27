# SPEC-751: Cinematics, remote cameras, ROF playback and captions
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-750 (script VM), SPEC-141 (`.rof` format), SPEC-140 (`.os` format), SPEC-001 (exports 0x54/0x58,
engine fast-forward), SPEC-202 (player-state fields), SPEC-003 (player module inventory object), SPEC-303 (save refusal),
SPEC-752 (where the state is saved)

## Purpose
Defines the game-side cinematic state (start, end, effects on the player and on AI actors, fast-forward
behaviour), the `func_remote_camera` entity that drives the player's view during cinematics and security-camera
views, the playback of ROF motion files by scripts (rate, interpolation, axes, file validation, end signal) and the
script caption/print command. It answers the open questions of SPEC-141.

## 1. Cinematic state

### 1.1 Representation
One game-wide byte: 0 = no cinematic, non-zero = cinematic running (the engine writes 1 or 2 through export 0x58
when the player skips; SPEC-001). It is:
- returned/set by game exports 0x54/0x58;
- stored in the savegame (`GAME` chunk, byte 0x615, SPEC-752) — but saving is refused while it is non-zero (SPEC-303);
- copied at the end of every server frame into each client's player-state "cinematic freeze" byte (SPEC-202 field
  27), which freezes client input and sets `cl_cinematicfreeze` on the client;
- reset to 0 when the game initialises, when a level is spawned, and when a level is exited (exit also sets cvar
  `sv_jumpcinematic` to `0`).

### 1.2 Start (script opcode 0x1F with operand 2)
1. Every entity owned by the first player (entity 1) that is a thrown knife (the knife projectile created by the
   knife's throw attack) is removed. This happens even if a cinematic is already running.
2. If entity 1 has a client and the state is 0: the state becomes 1; the server command buffer receives
   `-attack`, `-altattack`, `-weaponExtra1`, `-weaponExtra2`, `-use` (each followed by a newline) so held buttons are
   released; if the player has an inventory object (SPEC-003) its owner context is set to the player and its
   inventory method 58 ("current weapon hook 1") is called (UNVERIFIED effect: stops the current weapon action).

### 1.3 While running
- The engine does not call ClientThink and handles the skip button (SPEC-001).
- **AI actor freeze.** On its next think while the state is non-zero, every AI actor that is *not a cinematic
  actor* and whose AI allows freezing (UNVERIFIED condition, AI spec) is frozen: it becomes non-solid, its next think
  is suspended (the remaining delay is kept) and its model animation is paused. A frozen actor does nothing until the
  cinematic ends.
- **Cinematic actors.** When a script looks up an entity by `targetname` or `scripttarget` (built-ins 0 and 3,
  SPEC-750 §8) while the state is non-zero and that entity has AI, the entity's `count` value is set to 255; such
  actors are never frozen and are the ones the cinematic animates. (Consequence: a script reading `actor.count`
  during a cinematic sees 255.)
- Other systems pause during cinematics: level spawners do not spawn (console message when spawner debugging is on),
  ambient AI pressure/sound logic is suspended (owned by their packages).
- **Skipping.** While cvar `sv_jumpcinematic` is non-zero and the state is non-zero (the engine is fast-forwarding),
  script captions (PRINT), script sounds (SOUND) and sound precaching (PRECACHE_SOUND) do nothing, and in the first
  script frame in which `sv_jumpcinematic` equals 2 the console command `stopsound` is issued once (the latch is
  re-armed at every cinematic end).

### 1.4 End (script opcode 0x20 with operand 2; also SPEC-750 §12.9 when the player is dead)
1. If `sv_jumpcinematic` equals 2: cvars `sv_jumpcinematic` and `s_disable` are set to `0`.
2. The state becomes 0 and the `stopsound` latch is armed.
3. For every entity with AI: if it is a cinematic actor (`count` 255) its `count` becomes 0; otherwise, if it is
   frozen, it is thawed: its think resumes after the remaining delay, it becomes solid (bounding box) and its
   animation resumes from the current time.
The end runs even if no cinematic was running (it only thaws and resets).

### 1.5 Player death during a cinematic
A script USE executed while the state is non-zero and the player's health is below 1 ends the cinematic and
suspends the script (SPEC-750 §12.9); the level then follows the normal death flow (button 0x40 restarts the level,
SPEC-001).

## 2. Remote cameras (`func_remote_camera`)

### 2.1 Keys
| Key | Meaning |
|---|---|
| `target` | required: `targetname` of the look-at entity; a camera without it prints `Object 'func_remote_camera' without a target.` (developer print) and is removed |
| `targetname` | used to (de)activate the camera |
| `volume` | remote view type sent to the client (truncated to an integer). Stock maps: 2 (316 cameras), 4 (22), 16 (2); one camera has none (type 0). A camera with type 4 also makes the level precache an extra resource group (UNVERIFIED which) |
| `style` | field of view override in degrees while the camera is active (0 = unchanged). Unused by stock maps |
| `spawnflags` | 1: only the activator watches (otherwise every connected client); 2: watchers become non-solid and invulnerable while watching; 4: keep the camera after deactivation (otherwise it is removed on deactivation); 8: do not aim at the target (keep the camera's own angles) |
The camera is a point entity: not solid, not moving, bounds ±4 units.

### 2.2 Player-state fields used (SPEC-202)
Per watching client: remote view entity number (field 20 second byte; −1 → 255 = none), remote view type (field
20 first byte), remote camera origin (field 19, 1/8 units), remote camera angles (field 18), field of view
(field 24). The game also keeps a per-client "active cameras" counter.

### 2.3 Activation (the camera is used)
1. If the activator is a client in the dead-and-waiting state (UNVERIFIED: the state that ignores cameras), nothing
   happens.
2. If the activator's client is currently viewing a different camera, that camera is found (the
   `func_remote_camera` whose entity number equals the client's remote view entity) and deactivated (2.4). If no such
   camera exists the console shows `Error in Camera Scripting!  Check your 'use' and 'remove' commands!/n`
   (sic, literally "/n").
3. If this camera is already active it is **deactivated** instead (a camera toggles) and nothing else happens.
4. The camera becomes active. Watchers (2.1 flag 1: the activator, which must be a client — else
   `Error in Camera Scripting!  Activator (<classname>) is not a client!` and nothing happens; otherwise all
   in-use clients): the old field of view is remembered and replaced by `style` when non-zero; the watcher's camera
   counter is incremented; the watcher's remote view entity := the camera's entity number; remote view type :=
   `volume`; remote camera origin := camera origin.
5. The look-at target is the first entity whose `targetname` equals the camera's `target`. If found, the camera's
   angles are set to look at it; if its classname is `misc_generic_security_camera` or
   `misc_generic_security_camera2`, the camera switches to "follow the target" mode (internal flag 16): the view
   origin and angles are the target's own.
6. The remote camera angles of the watchers := the camera angles (or the target's in follow mode).
7. Flag 2: every watcher becomes non-solid and invulnerable.
8. The camera thinks every 0.1 s (2.5).

### 2.4 Deactivation
For each watcher (flag 1: the stored activator; otherwise all in-use clients): the field of view is restored if
`style` was used; the camera counter is decremented; remote view entity := −1 and remote view type := 0; with flag
2 the watcher becomes solid (bounding box) and loses invulnerability. The camera becomes inactive and, unless flag 4
is set, is removed. (With nested cameras the counter keeps the remote view on while it is ≥ 1; the per-frame update
2.6 uses it.)

### 2.5 Camera think (every 0.1 s while active)
For each watcher that is still looking through this camera (its remote view entity equals the camera and it is not
in the ignored state of 2.3 step 1): remote origin := camera origin (or target origin in follow mode); then, if a
target exists and flag 8 is clear, the camera re-aims at the target's current origin (so cameras track moving
targets and cameras moved by ROF keep aiming); remote angles := camera angles (or target angles in follow mode).

### 2.6 End-of-frame and damage
- At the end of every server frame, for each client: if its camera counter is < 1 its remote view entity is −1;
  otherwise its player entity gets render flag 0x4000 and the remote view entity/type are refreshed from the active
  camera. (The flag is removed 0.5 s after the counter drops to 0; UNVERIFIED meaning: hide the player's own body.)
- A client that takes damage while its remote view entity is ≥ 0 deactivates that camera (it is thrown back to its
  own view).
- While the first player watches a camera of type 4, entities whose damage mode is 3 take no damage
  (UNVERIFIED: guards in security-camera sequences).

## 3. ROF playback (script opcode 0x29)

### 3.1 Loading and cache
- Names are resolved as `ds/roff/<name>` through the game file system (case-insensitive on the stock Windows file
  system). Loaded files are kept in a level-wide cache keyed by the exact name string (case-sensitive key).
- PRECACHE_ROF (0x2F) loads a file into the cache; ROF (0x29) loads it if not cached; RELEASE_ROF (0x32) frees the
  cached copy (a later ROF reloads it). Releasing does not affect motion already started (each scheduled frame
  carries its own copy of the frame data). The cache is emptied when the level ends (SPEC-750 §1.2).
- A file that cannot be read prints (console) a line of 47 `*`, `Could not open .ROF file '<name>'`, the star line;
  nothing is cached and the ROF command does nothing — in particular **no signal is registered**, so a script
  waiting for that ROF's signal waits forever (stock: `trn1/trn1_introcam.os` names the missing `trainhemi.rof`;
  `tsr1/nycintro.os` names two missing files).

### 3.2 Validation
When played: the first four bytes must be `ROFF` (else `Bad header in .ROF file '<name>'`), the version int32 must
be 1 (else `Bad version (<v>) in .ROF file '<name>'`), the frame count float must be > 0 (else `Bad count <c> in .ROF
file '<name>'`); each message is printed between star lines and the command does nothing.

### 3.3 Schedule
Stack (push order): entity, ROF name, [flag 0x01 signal variable] (as SPEC-140). No entity → nothing.
With frame count C = truncate(count field) and frames f[0…C−1] read consecutively from file offset 12:
- if a signal variable is given it is registered for (entity, kind 3) first;
- for i = 0 … C−1 an event "apply frame i" is queued at time floor((T0 + i × 0.1 + 0.05) × 10) × 0.1 where T0 is the
  level time of the ROF command — **one frame per 100 ms server frame**, frame 0 in the frame the command runs (or
  the next script frame);
- if a signal variable was given, an end-marker event is queued at T0 + C × 0.1 (one frame after the last frame);
  when it fires, signal kind 3 is broadcast for the entity (SPEC-750 §9.2).
- The **header count is authoritative**: for the stock files whose count is smaller than the frames present
  (`u_turn_e.ROF`, `u_turn_w.ROF`: 50 of 51) the extra frames are never played; for files whose count is larger
  (`bos_cifrontbeam_lf.ROF`, `bos_cifrontbeam_rt.ROF`: 8 vs 6; `sud3_chaindoor_lf2.ROF`: 14 vs 7) the original reads
  past the end of the loaded file and applies whatever bytes follow in memory (undefined; a reimplementation should
  treat missing frames as zero deltas).

### 3.4 Applying a frame
At its time the event adds the frame's origin delta to the entity's origin and its angle delta to the entity's
angles (pitch, yaw, roll), component by component, in **world axes** (the deltas are not rotated by the entity's
orientation), then relinks the entity. There is no interpolation in the game: the server state changes once per
100 ms frame (clients interpolate between server frames as for any entity). There is no collision test and no
pushing: a ROF moves an entity through other entities (UNVERIFIED: verify a ROF door against a blocking player).
Angle deltas are applied as stored (no wrapping). A frame whose origin delta is exactly (10000, 10000, 10000) is
treated as the end marker (none in stock files).

### 3.5 Answers to SPEC-141 open questions
1. Playback rate: 10 frames per second, stepwise (the 25-frame `jpn2/kickdoor` door moves for 2.5 s of server frames; its end signal comes at +2.5 s). 2. World axes for all
entities. 3. The header count wins (3.3). 4. Rotation is applied to the entity's angles directly; for brush models
the engine rotates the model about its origin (the origin brush), as for any brush entity. 5. RELEASE_ROF frees the
cached file.

## 4. Captions and prints (script opcode 0x1D)
u8 flags. Pops: message (pushed **last**), [0x04 print level int, default 2], [0x01 target entity, default none].
- Message of type string: printed as text — to all clients with the print level, or to the target client only when
  a target is given. The text is used as a format string (a `%` in it is interpreted).
- Message of any other type: taken as an int **string id** (package × 256 + index, SPEC-131) and shown as a caption
  through the string-package print service (SPEC-001 import "sp_print") — to all clients when no target, else to the
  target client.
- Nothing is printed while a cinematic is being skipped (1.3).
Stock data always uses flags 0 with a string id: captions to everyone. Captions are therefore not synchronised with
speech by the game; scripts issue the SOUND and then the PRINT in sequence and WAIT for the line's duration.

## Constants
| Name | Value |
|---|---|
| ROF frame period | 0.1 s (one server frame) |
| ROF directory | `ds/roff/` |
| ROF end-marker delta | (10000, 10000, 10000) |
| ROF signal kind | 3 |
| Cinematic actor mark | `count` = 255 |
| Buttons released at cinematic start | `-attack`, `-altattack`, `-weaponExtra1`, `-weaponExtra2`, `-use` |
| Camera bounds | (−4, −4, −4) … (4, 4, 4) |
| Camera think period | 0.1 s |
| Camera spawnflags | 1 activator only, 2 hide/protect watchers, 4 keep after use, 8 no auto-aim, 16 follow target (internal) |
| Remote view types in stock maps | 2, 4, 16 |
| Security camera classnames (follow mode) | `misc_generic_security_camera`, `misc_generic_security_camera2` |
| Default print level for script text | 2 |

## Test vectors / how to verify
1. `jpn2/kickdoor.os`: the 25 frames of the door's ROF are applied at T0, T0 + 0.1 … T0 + 2.4 (T0 = level time of the
   ROF command); a WAIT_SIGNALS on its signal resumes at T0 + 2.5 (end marker one frame after the last frame).
2. Start a cinematic (any intro script that brackets with 0x1F/0x20 operand 2), press fire during it: no weapon fires;
   enemies not named by the script stand still and are non-solid until the end.
3. Skip a cinematic with the use/skip button: speech stops (`stopsound`), no captions appear while skipping, the
   level continues with `sv_jumpcinematic` back at 0.
4. Use a `func_remote_camera` twice from a trigger: the first use switches the view, the second returns it; without
   spawnflag 4 the camera no longer exists after the second use.
5. Shoot the player while a security-camera view is shown: the view returns to the player.
6. Missing ROF (`trn1` intro): console shows "Could not open .ROF file 'trainhemi.rof'" between star lines.

## Open questions
1. Exact AI freeze eligibility condition (AI framework spec) and whether actors spawned during a cinematic freeze.
2. Meaning of remote view types 2, 4, 16 on the client (render mode of the remote view) and of render flag 0x4000.
3. The precache group requested by type-4 cameras.
4. The client state ignored by cameras (2.3 step 1) — believed "dead".
