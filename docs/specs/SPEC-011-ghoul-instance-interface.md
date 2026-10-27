# SPEC-011: Ghoul model system — instance interface, transforms, playback, events
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (provides the interface), gamex86.dll, player.dll, ref_gl.dll (use it), version 1.07f
Depends on: SPEC-010 (conventions, system and model-object interfaces, event callback), SPEC-002 (renderer entity and
refdef layouts), SPEC-001 (savegame chunks).

## Purpose

A Ghoul *instance* is one placed and animated copy of a model object: it has its own transform, current sequence and
playback clock, part visibility, skin overrides, tint, attached child instances ("bolt-ons") and registered event
callbacks. The game module keeps one per Ghoul entity, the player module one per view weapon, the renderer draws them.
This spec gives the 55-method interface in table order, the data blocks passed through it (matrix, hit record, render
parameters) and the observable behaviour of playback, events, bolt matrices, traces and savegames.

## Interface / layout

Conventions are those of SPEC-010 (`thiscall`, callee removes arguments, bool in AL, 16-bit ids, 0 = none). "time" is
in seconds: the game passes its level time, the client its game clock, the renderer the refdef game clock (SPEC-002
refdef offset 0x50). "part", "bolt", "material", "skin", "note", "sequence" ids are those of the instance's model
object (SPEC-010 B). A bolt id is the part id of a null node.

### Lifetime
- Created by model-object method 24 (and by method 36 of an existing instance). The new instance has an identity
  transform, is "on", speed index 4, tint (1,1,1,1), no sequence, all parts shown, no skin overrides, no callbacks,
  no parent, user value NULL.
- Destroyed by method 53, or by its object's methods 27/28. Destroying an instance first destroys every bolt-on child
  (recursively), detaches it from its parent and frees its id; the pointer is invalid afterwards.
- On the client, instances replicated from the server are created and destroyed by the engine; other modules only
  look them up (system method 9) and must not destroy them (UNVERIFIED what happens if they do).

### Method table — 55 methods

| # | Off | Args (after this) | Returns | Meaning |
|---|---|---|---|---|
| 0 | 0x00 | matrix pointer (68 bytes) | — | Set the instance transform (model space → entity space) |
| 1 | 0x04 | pointer to 16 floats | — | Same, from 16 floats; the identity flag is computed (tolerance 0.0001) |
| 2 | 0x08 | matrix out (68 bytes) | — | Get the transform including its flags word |
| 3 | 0x0c | 16 floats out | — | Get the 16 floats of the transform |
| 4 | 0x10 | bool on, float time | — | Switch the instance on/off. While off it is not drawn, not traced, produces no events. Switching on at `time` restarts event processing from `time` for it and all its bolt-ons |
| 5 | 0x14 | — | bool | Is on |
| 6 | 0x18 | int speed index 0–12 | — | Select the playback-speed index (Constants); 4 = normal. Not range-checked |
| 7 | 0x1c | — | int | Current speed index |
| 8 | 0x20 | id part, bool shown | — | Show or hide one part. Part 0 is invalid (not checked; must not be passed) |
| 9 | 0x24 | id part | bool | Is the part shown |
| 10 | 0x28 | bool shown | — | Show or hide every mesh part (parts of type 1) |
| 11 | 0x2c | id material, id skin, id part (0 = whole model) | — | Use `skin` for `material` on this instance (optionally only on one part); overrides the object's default |
| 12 | 0x30 | id material, id part, int (callers pass 0) | skin id | The override set by 11 for (material, part); 0 = none |
| 13 | 0x34 | id material, id part, int (0) | — | Remove that override |
| 14 | 0x38 | float r, g, b, a | — | Tint/alpha multiplier; each value clamped to 0…1 and stored with 8-bit precision |
| 15 | 0x3c | float* r, g, b, a out | — | Read the tint (value / 255) |
| 16 | 0x40 | float r, g, b, a | — | Method 14 on this instance and on each direct bolt-on |
| 17 | 0x44 | id sequence, float time, float offset, bool restart, int mode, bool matchPhase, bool reverse | — | Play a sequence (Behaviour: playback) |
| 18 | 0x48 | — | sequence id | Current sequence (0 = none) |
| 19 | 0x4c | float time | — | Pause: freeze the pose it has at `time` |
| 20 | 0x50 | float time | — | Resume from the frozen phase |
| 21 | 0x54 | pointer value | — | Store the owner's user value (passed to callbacks). The game stores its entity pointer, the player module its inventory object |
| 22 | 0x58 | — | pointer | The user value |
| 23 | 0x5c | event callback, id note | — | Register a callback for a note (several callbacks per note allowed; also enables note processing) |
| 24 | 0x60 | event callback, id note | — | Remove the registrations of that callback for that note |
| 25 | 0x64 | event callback, id bolt, int matrix mode | — | Register a per-update bolt-matrix callback (enables note and matrix processing) |
| 26 | 0x68 | event callback, id bolt, int matrix mode | — | Remove that registration |
| 27 | 0x6c | id sequence, int mode, float* frame time out or NULL | float | Duration in seconds of one cycle of `sequence` in `mode` at the current speed index; the frame time is written when the pointer is non-NULL. See quirk in Behaviour |
| 28 | 0x70 | id my bolt, child instance, id child bolt | — | Attach `child` so that its bolt `child bolt` follows this instance's bolt `my bolt` (child bolt 0 = the child's origin) |
| 29 | 0x74 | id bolt | — | Set which of this instance's own bolts is aligned to its parent bolt (normally set by 28) |
| 30 | 0x78 | — | bolt id | That bolt |
| 31 | 0x7c | child instance | — | Detach a child (no effect if it is not attached here) |
| 32 | 0x80 | — | int | Number of attached children |
| 33 | 0x84 | int index (0-based), id* my-bolt out | child instance or NULL | Child `index` and the bolt it hangs on; NULL when index ≥ count |
| 34 | 0x88 | — | model object | The instance's model object (SPEC-010 B) |
| 35 | 0x8c | — | instance or NULL | Parent instance |
| 36 | 0x90 | bool withChildren | instance | New instance of the same object with a copy of this state; with `withChildren` the children are cloned and attached with the same bolts |
| 37 | 0x94 | buffer, int capacity, pointer to callback table, int table count, string model name | int bytes | Write the savegame image of the instance (Behaviour: savegames) |
| 38 | 0x98 | buffer, int length, pointer to callback table, int table count, string model name | id | Restore a savegame image into this (new) instance; returns the instance id that was saved |
| 39 | 0x9c | — | id | This instance's id (its handle) |
| 40 | 0xa0 | — | id | Its model object's id |
| 41 | 0xa4 | render-parameter block pointer | — | Draw the instance (and its bolt-ons) with the current GL table (SPEC-010 G). Called by the renderer and the engine's menu code |
| 42 | 0xa8 | three 32-bit values | — | Does nothing (reserved) |
| 43 | 0xac | float time, vec3 start, vec3 direction, hit records out, int max hits | int | Ray test against the posed meshes (Behaviour: traces) |
| 44 | 0xb0 | float time, matrix pointer, vec3, vec3, vec3, vec3, hit records out, int max | int | A second hit query; meaning UNVERIFIED; no caller in the original modules |
| 45 | 0xb4 | float time | — | Advance event processing to `time` and call the due callbacks (Behaviour: events) |
| 46 | 0xb8 | — | — | Stop note and matrix event processing |
| 47 | 0xbc | — | — | Stop matrix event processing only |
| 48 | 0xc0 | float time | — | Start note event processing from `time` (earlier events are skipped) |
| 49 | 0xc4 | float time | — | Start note and matrix event processing from `time` |
| 50 | 0xc8 | float time, matrix out, id bolt, int matrix mode, bool throughParents | — | Get a bolt matrix (Behaviour: bolt matrices) |
| 51 | 0xcc | float time, matrix pointer (root), vec3 mins out, vec3 maxs out, id part (0 = all), bool exact | — | Axis-aligned bounding box at `time` in the space of `root` |
| 52 | 0xd0 | float priority | id or 0 | Engine only (server): offer the instance for this frame's network update of one client; returns the handle to put in the entity state, 0 when it cannot be sent |
| 53 | 0xd4 | — | — | Destroy (see Lifetime) |
| 54 | 0xd8 | — | sequence id | Same result as method 18; no caller in the original modules |

The renderer calls 51 (bounds, `root` = entity matrix, part 0, exact 0) and 41; it receives instance pointers only
from system method 9. The engine calls 52 while building client frames. Methods 42, 44 and 54 have no caller in the
original modules.

### Matrix — 68 bytes
| Offset | Type | Meaning |
|---|---|---|
| 0x00 | float[4] | row 0: image of the local X axis (forward), w = 0 |
| 0x10 | float[4] | row 1: image of the local Y axis (left), w = 0 |
| 0x20 | float[4] | row 2: image of the local Z axis (up), w = 0 |
| 0x30 | float[4] | row 3: translation, w = 1 |
| 0x40 | uint32 | flags: bit 0 = the matrix is the identity |

Row-vector convention: a point p maps to p′ = p.x·row0 + p.y·row1 + p.z·row2 + row3. A product A·B applies A
first. Axes follow Quake: X forward, Y left, Z up (right-handed). The original renderer builds an entity's matrix from
its angles as rows (forward, −right, up, origin) using the Quake angle-vector convention with pitch and roll negated.
Writers that change any element must clear bit 0; method 1 sets it when all 16 values are within 0.0001 of the
identity.

### Hit record — 36 bytes (method 43)
| Offset | Type | Meaning |
|---|---|---|
| 0x00 | instance pointer | instance actually hit (may be a bolt-on) |
| 0x04 | uint16 | part id hit |
| 0x06 | uint16 | padding |
| 0x08 | float | distance along the ray (point = start + distance · direction) |
| 0x0c | float[3] | surface normal (unit; zero if degenerate) |
| 0x18 | float[3] | hit point |

### Render parameters — 180 bytes (method 41), built by the renderer
The block is itself an object: its first word points to a one-entry method table (interface "render cull").

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | method table pointer | entry 0: `bool (const float corners[8][3])`, `thiscall`, one argument: true when a box with these 8 world-space corners is entirely outside the view, so it is not drawn. The renderer's variant for view weapons and when culling is disabled always returns false |
| 0x04 | float | time = refdef game clock |
| 0x08 | float[3] | light colour at the entity (point light × `ghl_light_multiply` clamped 0.25–2, raised to ≥ 0.1 for RF_MINLIGHT, glow pulse for RF_GLOW; (1,1,1) when fullbright) |
| 0x14 | float[3] | directional light vector (× 500) |
| 0x20 | float[3] | entity origin (light and shadow probe point) |
| 0x2c | byte | 0 |
| 0x2d | byte | directional vector valid |
| 0x2e | byte | use OpenGL lights (`ghl_light_method` ≥ 2) |
| 0x2f | byte | specular |
| 0x30 | byte | fast lights (`ghl_fast_lights`) |
| 0x31 | byte | hardware lights (`ghl_light_method` > 2.5) |
| 0x32 | byte | GL lights directional (`ghl_gllight_directional`) |
| 0x33 | byte | GL lights attenuation (`ghl_gllight_attenuation`) |
| 0x34 | byte | entity flag 0x1000 set (meaning UNVERIFIED, SPEC-002) |
| 0x35 | byte | draw the shadow only (RF_VIEWERMODEL entity) |
| 0x36 | byte | `ghl_no_texture_stack` |
| 0x37 | byte | unused |
| 0x38 | float | shadow darkness (`ghl_shadow_darkness` × fog visibility) |
| 0x3c | float | fog visibility (1 − fog fraction; 1.0 without fog) |
| 0x40 | float | shadow distance (`ghl_shadow_dist`) |
| 0x44 | function pointer | world ray test `float (vec3 start, vec3 end)` — the renderer passes the engine's ray-fraction import (SPEC-002 refimport 0x5c) |
| 0x48 | int | shadow mode 0–2 (`ghl_shadows` rounded and clamped; forced 0 when the shadow darkness setting is ≤ 0 or when the renderer draws the entity in its fullbright pass — UNVERIFIED which renderer path sets that) |
| 0x4c | int | maximum lights 1–8 |
| 0x50 | matrix (68 bytes) | entity → world |
| 0x94 | float[3] | view origin |
| 0xa0 | float[3] | view forward vector |
| 0xac | byte | no dynamic lights (`gl_dlightintensity` ≤ 0.01) |
| 0xad | byte[3] | padding |
| 0xb0 | pointer | the renderer entity (SPEC-002 entity layout) |

Exact semantics of the option bytes inside the drawing code are UNVERIFIED beyond the cvar each one copies; the engine
menu code fills the same block itself to draw menu models.

## Behaviour

### Playback
Let the instance hold: sequence s, mode m, start time S, frozen time A, paused flag, reverse flag, duration D and
frame time F of s, and N = number of frames of s.
- **Duration** (method 27 and every Play): F = (frame time of the sequence in ms) × 0.001 × speed multiplier;
  D = N·F (mode 0), (N − 1)·F (mode 1), (2N − 1)·F (mode 2), 0 (mode 3 or any other value). Sequence 0 → 0.
- **Play(s, t, offset, restart, m, matchPhase, reverse)**: nothing happens when s = 0 or s exceeds the object's
  sequence count. If s, m and the reverse flag equal the current ones and `restart` is false, the playback keeps its
  phase. Otherwise the new playback starts so that its elapsed time at time t is `offset`; with `matchPhase` and a
  different sequence (or direction), the old playback's phase at t (inverted when the direction changes) is added,
  so the new sequence continues at the same relative position. Pause is cleared; note processing restarts at t.
- **Mode 3** (static pose): the pose is fixed at fraction = offset ÷ (mode-0 duration of s), wrapped into [0, 1) for
  negative values and capped at 1. Sequences with D = 0 in other modes show frame 0.
- **Pose at time t**: if paused, t is replaced by the pause time. d = t − S. Mode 0: f = frac(d/D); mode 1:
  f = clamp(d/D, 0, 1) (plays once and holds the last frame); mode 2: g = frac(d/D), f = 2g for g < 0.5, then
  backwards (ping-pong); reverse flag: f := 1 − f. f is clamped to [0, 1].
- **Frames**: mode 0: x = N·f, frame ⌊x⌋ blended with the next frame, the last frame blending into frame 0 (seamless
  loop); other modes: x = (N − 1)·f, next frame clamped to N − 1. Blend weight = fractional part of x.
- **Pause(t)** freezes at t; **Resume(t)** continues from the frozen phase (the start time moves by the paused span).
- Method 6 affects durations computed by later Play calls; a running playback keeps its duration (UNVERIFIED).
- Quirk: method 27 stores the frame count of the *queried* sequence in the instance, which affects frame selection of
  the current playback until the next Play (UNVERIFIED in game).
- An instance with no sequence or switched off is not drawn, not traced (0 hits), returns identity bolt matrices and
  produces no events.

### Events (methods 23–26, 45–49)
- Event processing is off at creation. Registering a note callback (23) turns note processing on; registering a bolt
  callback (25) turns note and matrix processing on; 46/47/48/49 switch them as listed in the table; 48/49 and
  switching the instance on (4) move the processing cursor to the given time for the instance and its bolt-ons, so
  events before that time are not delivered.
- Method 45(t), when the instance has a sequence, is on and processing is enabled, delivers:
  1. for every registered (bolt, matrix mode) present in the current sequence: one call with `data` = the bolt
     matrix at t (as method 50 with `throughParents` false, root = instance transform included per mode); the matrix
     memory belongs to the engine and is freed after the call;
  2. every note key of the current sequence whose time lies after the cursor and up to t, in time order, to each
     callback registered for that note, with `data` = the key's argument text (never NULL);
  3. `EOS` with `data` = "" once for every completed cycle (once only in mode 1); at most two cycles are processed per
     call;
  4. the same for bolt-ons that have processing enabled.
  All events of one call are delivered after the evaluation, in that order. `BOS` is registered as a note name but
  was not seen to be delivered (UNVERIFIED).
- Callback arguments (SPEC-010 E): first argument undefined (the engine does not set it; implementations must ignore
  it), second = the user value (method 21), third = the time passed to 45, fourth = `data`. The return value is ignored.
- Removing (24, 26) a registration removes only entries that match both the callback and the note (or bolt and mode).

### Bolt matrices (method 50) and bolt-ons
- `matrix mode` is a 3-bit value: bit 0 = include the instance transform (else model space); bit 1 = return the
  inverse; bit 2 = plain pose (when 0, the result is a rate: the difference between the pose at the current phase and
  0.001 later — or earlier at the end of a play-once sequence — scaled to "per second", used for root motion).
  The original game uses mode 5 (pose including the transform) for bolts and mode 1 (rate) on its `quake_origin`
  bolt.
- With `throughParents` ≠ 0 on a bolted instance the result is further transformed by the parent's bolt matrix for the
  bolt it hangs on (same mode, recursively), giving the bolt in the root instance's entity space.
- If the bolt does not exist in the current sequence the output is left unchanged (callers pre-initialise it); with
  no sequence or when off, the identity is returned.
- A child's pose: its own transform, aligned so that its attach bolt (28/29) coincides with the parent's bolt, then
  the parent's bolt matrix and the parent chain (composition order of the alignment UNVERIFIED). Several children may
  hang on one bolt; order = attachment order. Method 28 does not check whether the child already has a parent.

### Traces and bounds
- Method 43: the ray is given in the instance's **entity space** (the original game transforms its world-space shot
  by the entity origin and angles first); the instance transform is applied internally; bolt-ons are included. Up to
  `max` records are written, sorted by increasing distance; the result is their number.
- Method 51: box of the instance at `time` (and of all bolt-ons when part = 0, else of that part only) in the space of
  `root`. `exact` = false uses stored sequence bounds (UNVERIFIED whether per sequence or per frame), true computes it
  from the posed geometry. Nothing to measure → mins = maxs = (0,0,0).

### Savegames (methods 37, 38)
- 37 writes an engine-defined image containing the model name, instance id, transform (with inverse and scale), attach
  bolt, on flag, speed index, playback state (sequence, mode, times, paused, reverse), tint, part flags, skin
  overrides, event cursor and processing flags, and the registered callbacks — each callback recorded as its index in
  the caller-supplied table (or −1 when it is not in the table). It returns the size; more than `capacity` bytes is
  fatal ("Ghoul :GhoulInst savegame packet overflow"). The image format is private to the Ghoul system: a
  reimplementation only needs to read back what it wrote.
- 38 must be called on a new instance of the same object, with the same callback table; callbacks whose saved index
  is −1 or ≥ the table count are dropped. It returns the saved instance id (the game maps old ids to new ones). Fatal
  errors: "Inst has different model than expected" (model name differs), "Number of parts mismatch in savegame",
  "Skin mismatch in savegame", "Part mismatch in savegame".
- The original game stores each image in a savegame chunk with id `GHLI` (SPEC-001 chunk format), uses a 16384-byte
  buffer and a table of 51 callbacks.

### Network (method 52, engine only)
While building a client's frame the server offers each Ghoul entity's instance with the distance from the client's
view point as priority (the view weapon with −1.0). The result goes into the entity state's Ghoul handle (and the
player state's view-weapon handle). It is 0 when the instance has no sequence and no parent, when a bolt-on was
refused (bolt-ons are offered first with priority − 0.1), or when the client does not know the object yet; otherwise
the instance is queued for the unreliable Ghoul update (lowest priority first) and its id is returned. The encoding is
part of the network spec.

## Constants

| Name | Value |
|---|---|
| Methods | 55 |
| Matrix | 68 bytes (16 floats + flags), bit 0 = identity, tolerance 0.0001 |
| Hit record | 36 bytes |
| Render parameters | 180 bytes; cull interface: 1 method |
| Playback modes | 0 loop, 1 once and hold, 2 ping-pong, 3 static pose at fraction |
| Speed multipliers (index 0–12, multiply the duration) | 0.5, 0.625, 0.75, 0.875, 1.0, 1.125, 1.25, 1.375, 1.5, 1.625, 1.75, 1.875, 2.0; default index 4 |
| Matrix mode bits | 1 include instance transform, 2 inverse, 4 pose (0 = rate per second) |
| Rate step | 0.001 of the cycle |
| Events per call | at most 2 cycles |
| Notes delivered by the engine | note keys of the sequence, `EOS` |
| Savegame (original game) | chunk `GHLI`, buffer 16384 bytes, callback table 51 entries |

## Test vectors / how to verify
1. Duration: a sequence of N = 10 frames at 100 ms per frame: method 27 returns 1.0 (mode 0), 0.9 (mode 1), 1.9
   (mode 2), 0 (mode 3) at speed index 4; 0.5 / 0.45 / 0.95 at index 0.
2. Loop phase: Play(s, t=10, offset 0, restart 1, mode 0, 0, 0) on that sequence; the pose at t = 10.25 equals frame
   2.5 (frames 2/3 blended 50 %); at t = 10.95 frames 9 and 0 are blended.
3. Pause/Resume: pause at 10.3, resume at 20.3 → pose at 20.4 equals the pose that 10.4 would have given.
4. Events: register a logging callback on note `EOS`; with mode 0 and D = 1.0, calling method 45 at 10.5, 11.1,
   13.4 delivers EOS 0, 1 and 2 times (the last call is capped at two cycles); every call's second argument is the user
   value from method 21.
5. Matrix: method 1 with the identity sets flag bit 0; a matrix with translation (1,2,3) moves point (0,0,0) to (1,2,3)
   (elements 12–14).
6. Bolt: attach a weapon instance to a character bolt with method 28; method 50(t, m, bolt, 5, 1) on the weapon's own
   bolt returns a matrix whose translation equals the character's bolt position (in-game: weapon stays in the hand).
7. Trace: a ray from 100 units in front of a standing character towards it returns ≥ 1 record whose distance is ≈ 100
   minus the body's front depth and whose part is a mesh of the torso.
8. Savegame: save, load into a fresh instance of a different object → fatal "Ghoul :Inst has different model than
   expected".

## Open questions
- Method 44 parameters (no caller anywhere).
- Method 54: why a separate slot with the same result as 18.
- `BOS` delivery; ping-pong (mode 2) note timing.
- Whether method 6 changes a running playback.
- Method 27's side effect on the current frame count (visual check).
- Composition order of the child attach-bolt alignment (verify with a bolted weapon).
- Bounding box with `exact` = false: per sequence or per frame.
- What the render-cull method is used for on bolt-ons; meaning of entity flag 0x1000 (option byte 0x34).
