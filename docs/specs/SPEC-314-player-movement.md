# SPEC-314: Player movement — the engine's player-move service, the server move and the client's prediction
Status: approved (G, 2026-10-25) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: SoF.exe (the player-move service = game import #81, client movement prediction), with the parts of
gamex86.dll that drive it, version 1.07f
Depends on: SPEC-001 (import #81, traces #52, point contents #54), SPEC-202 (player state, user command, pm_type
values), SPEC-AMEND-005 (G18.19 player-state layout, G18.20 user command, G18.21 movement block, G18.21b),
SPEC-313 (trace result layout, §8 spectator movement), SPEC-780 (§4–§5 the game's per-command think, §13 falling),
SPEC-310 (§12 view smoothing), SPEC-312 (movement values from input), SPEC-012 (`gserver SIZEMATTERS`),
SPEC-101 (contents bits)

## Purpose

The engine contains one player-movement routine. The server half reaches it through game import #81: the game
module fills a 256-byte movement block per user command and the routine moves the player through the world. The
client half runs the same routine for prediction. This spec describes that routine completely — its inputs and
outputs, the order of its steps and every rule with its constants — together with what the game module and the
client feed into it, so that a reimplementation produces the same positions, velocities and flags as 1.07f to the
1/8 unit. It replaces SPEC-313 §8 (kept there for the spectator details) and the difference list of SPEC-AMEND-005
G18.21b as the reference; everything in G18.21b is confirmed here, with additions.

The routine descends from Quake II 3.19 `pmove.c`. Where a rule is unchanged from Quake II it is still written out,
so that this spec can be implemented without the Quake II source; public Quake II names (PMF_DUCKED, PM_FREEZE, …)
are used only to point at the corresponding Quake II behaviour.

All results in "Test vectors" were produced by running the original 1.07f movement routine (import #81 of the
original SoF.exe) on synthetic worlds, and an independent implementation of the rules below reproduces every one
of them exactly (911 moves compared, 0 differences; the spectator type is excluded, see Open questions). The game
module's and the client's parts (§19–§21) were read from the binaries but not exercised by that harness; in-game
checks for them are listed at the end.

## Interface / layout

### 1. Units and arithmetic
- Positions in world units, velocities in units per second, angles in degrees inside the routine.
- The stored state keeps origin and velocity as 16-bit integers in **1/8 units** (value × 8) and angles as 16-bit
  integers (65536 per turn; to degrees: × 0.005493164, i.e. 360/65536 as a single-precision constant).
- Frame time t = msec × 0.001, rounded to single precision (10 ms → 0.0099999998).
- **Arithmetic is IEEE single precision with round-to-nearest after every addition, subtraction, multiplication,
  division and square root.** The original runs the x87 FPU with precision control set to 24 bits (control word
  0x007F, measured inside the dedicated server when the game module is loaded). This is observable: one 10 ms ground
  acceleration from rest gives 29.999998 units/s, stored as 239/8, not 240/8. Doing the maths in double precision
  changes many results by 1/8.
- Every float→integer conversion truncates toward zero.
- Direction vectors from angles (pitch p, yaw y, roll r; Quake II convention, positive pitch looks down):

  | Vector | x | y | z |
  |---|---|---|---|
  | forward | cos p·cos y | cos p·sin y | −sin p |
  | right | −sin r·sin p·cos y + cos r·sin y | −sin r·sin p·sin y − cos r·cos y | −sin r·cos p |
  | up | cos r·sin p·cos y + sin r·sin y | cos r·sin p·sin y − sin r·cos y | cos r·cos p |

  "Normalise" leaves a zero vector unchanged and returns the old length.

### 2. Movement state (28 bytes = player-state bytes 0x00–0x1B, SPEC-AMEND-005 G18.19)
| Offset | Type | Field | Used by the routine |
|---|---|---|---|
| 0x00 | int32 | movement type (pm_type, Behaviour 16) | read |
| 0x04 | int16[3] | origin × 8 | read, written |
| 0x0A | int16[3] | velocity × 8 | read, written |
| 0x10 | u8 | movement flags (table below) | read, written |
| 0x11 | u8 | timer (pm_time), counted down by msec >> 3 per move (Behaviour 6) | read, written |
| 0x12 | int16 | gravity, units/s² | read |
| 0x14 | int16[3] | delta angles (added to the command angles) | read |
| 0x1A | u8 | movement-speed byte q | **not read** (game and client use it, §19–20) |
| 0x1B | u8 | padding | — |

Movement flags:
| Bit | Quake II name | Meaning in 1.07f | Set / cleared by |
|---|---|---|---|
| 0x01 | PMF_DUCKED | crouched box | routine (Behaviour 3) |
| 0x02 | PMF_JUMP_HELD | jump must be released before the next jump | routine (Behaviour 7) |
| 0x04 | PMF_ON_GROUND | standing on ground | routine (Behaviour 4) |
| 0x08 | PMF_TIME_WATERJUMP | water-jump flight (Behaviour 11) | routine |
| 0x10 | PMF_TIME_LAND | landed hard; no jumping until the timer runs out | routine (Behaviour 4) |
| 0x20 | PMF_TIME_TELEPORT | teleport pause (Behaviour 11) | game (teleporters, spawns, SPEC-780) |
| 0x40 | — | "exhausted": the run button does not double the moves | game (Realistic mode, SPEC-780); not read by the routine |
| 0x80 | — | "no prediction" for the client (§20) | game; not read by the routine |
Bits 0x08, 0x10 and 0x20 are always cleared together ("the timer flags").

### 3. The user command as the routine sees it (24 bytes, SPEC-AMEND-005 G18.20)
| Offset | Field | Use |
|---|---|---|
| 0 | msec (u8) | frame time |
| 1 | buttons | **not read** (running is applied before the call, §19) |
| 2 | light level | not read |
| 3 | lean (int8: −1, 0, +1) | read only by the duck rules (Behaviour 3) |
| 4 | angles int16[3] | view angles (with the delta angles) |
| 10, 12, 14 | forward, side, up move (int16) | wish velocities in units/s: forward along the view's forward axis, side along its right axis (positive = right), up = jump/crouch/swim-up |
| 16, 20 | fire times | not read |

With the stock client the values reaching the routine are: walking forward/back 200, sideways 160, up/down 200
(SPEC-312, sent unscaled), doubled by the run button to 400/320/400, then forward and side multiplied by q/255 (§19). The routine
never changes the command except that it sets forward, side and up move to 0 for movement types 3–7.

### 4. Movement block (256 bytes, argument of import #81; SPEC-AMEND-005 G18.21)
| Offset | Type | Dir | Field |
|---|---|---|---|
| 0x00 | 28 bytes | in/out | movement state (§2) |
| 0x1C | 24 bytes | in | user command (§3) |
| 0x34 | int32 | in | snap-initial flag: non-zero = the state was changed outside player movement (Behaviour 15) |
| 0x38 | int32 | out | number of touched entities (0–32) |
| 0x3C | pointer[32] | out | touched entities, in touch order, duplicates kept |
| 0xBC | float[3] | out | view angles |
| 0xC8 | float | out | view height above the origin |
| 0xCC | float[3] | out | box mins |
| 0xD8 | float[3] | out | box maxs |
| 0xE4 | pointer | out | ground entity or null |
| 0xE8 | int32 | out | water type (contents at the feet sample) |
| 0xEC | int32 | out | water level 0–3 |
| 0xF0 | function | in | box trace (§5) |
| 0xF4 | function | in | point contents (§5) |
| 0xF8 | function | in | ground-entity-is-client (§5) |
| 0xFC | float | in | stagger value v (Behaviour 9) |
At the start of every call the routine sets touch count, view angles, view height, ground entity, water type and
water level to 0; they are pure outputs. Mins and maxs are written only by the duck rules (Behaviour 3), so movement
types 1, 2, 5 and 7 leave whatever the caller put there. Nothing after 0xFC is read or written.

### 5. Callbacks (all `cdecl`)
| Callback | Signature | Called |
|---|---|---|
| trace (0xF0) | (start, mins, maxs, end) → 56-byte trace result (SPEC-313 layout) **returned by value** (hidden result pointer as the first argument) | stand-up test, snap and emergency-placement position tests (start = end), ground probe, ladder probe, every slide segment, step-up tests |
| point contents (0xF4) | (point) → int contents | water sampling (up to 3 per classification), water-jump test (up to 2) |
| ground-entity-is-client (0xF8) | (ground entity pointer) → int | at most once per move, only when a jump would start (Behaviour 7); non-zero refuses the jump |
The routine reads from a trace: all-solid, start-solid, fraction, end position, plane normal, surface (only its
flags, byte 32 of the surface record: bit 0x2 = slick), contents and entity. An entity pointer counts as "something
was touched" whenever it is non-null; the server trace (SPEC-313) reports the world entity even when nothing was hit,
the client's prediction trace reports a non-null placeholder only on a hit (§20).

### 6. Contents and surface bits used
| Bits | Meaning |
|---|---|
| 0x01 | solid (water-jump test) |
| 0x08, 0x10, 0x20 | lava, slime, water; 0x38 = liquid |
| 0x40000, 0x80000, 0x100000, 0x200000 | current toward +x, +y, −x, −y |
| 0x400000, 0x800000 | current up, down |
| bit 29 | ladder (Quake II CONTENTS_LADDER) |
| surface flag 0x2 | slick: no ground friction |

## Behaviour

### 1. Order of one move
1. Clear the outputs (§4). Convert origin and velocity to floats (÷ 8); remember the stored origin as the
   *fallback origin*; t = msec × 0.001; acceleration scale k (Behaviour 9).
2. View angles and direction vectors (Behaviour 2).
3. Type 1: noclip flight, then the final snap; done. Type 2: spectator flight (SPEC-313 §8), then the final snap;
   done.
4. Types 3–7: forward, side and up move := 0. Types 5 and 7: done (nothing else changes; the stored state is not
   rewritten).
5. Box, duck state and view height (Behaviour 3).
6. If the snap-initial flag is set: initial snap (Behaviour 15).
7. Ground and water classification (Behaviour 4).
8. Type 3 on ground: dead slow-down (Behaviour 16).
9. Ladder detection and water-jump start (Behaviour 5).
10. Timer countdown (Behaviour 6).
11. Exactly one of: teleport pause (flag 0x20: nothing); water-jump flight (flag 0x08, Behaviour 11); otherwise,
    unless the type is 6: jump check (7), friction (8), then swimming (10) when the water level is ≥ 2, else walking /
    air / ladder movement (9).
12. Ground and water classification again (Behaviour 4).
13. Final snap (Behaviour 15).

### 2. View angles
- Normally each view angle = (16-bit sum of command angle and delta angle, wrapped to −32768…32767) × 360/65536,
  giving −180 ≤ a < 180. Pitch above 89 (and below 180) becomes 89; the second Quake II rule (180 ≤ pitch < 271 →
  271) can never apply, so **looking up is not limited by the routine** (−120° stays −120°; the client keeps pitch within ±89°, SPEC-312).
- During the teleport pause (flag 0x20): pitch = roll = 0, yaw = (command yaw + delta yaw) × 360/65536 without the
  16-bit wrap.
- forward, right, up are computed from these angles (§1) and the view angles are output.

### 3. Box, ducking and view height
- Box: mins (−16, −16, −24), maxs (16, 16, 40) standing, maxs z **14** ducked. Type 4 (gib): (−16, −16, 0) to
  (16, 16, 16), view height 8, duck flag untouched; nothing else below applies to type 4.
- Duck flag: type 3 always ducks. Otherwise with up move < 0 the player ducks — **also in the air** — except that a
  non-zero lean in the command keeps a standing player standing. With up move ≥ 0 a ducked player stands up only
  when the lean is 0 and a position test (trace with start = end) of the standing box at the current origin is not
  all-solid; otherwise he stays ducked.
- View height: standing 35, ducked 0. When `gserver SIZEMATTERS s` (SPEC-012, a process-wide value, default 1) is
  not exactly 1: standing = s × 59 − 24 clamped to −8…39, ducked = s × 24 − 24 clamped to −10…13 (single
  precision, not truncated: s = 1.3 gives 39 and 7.2).

### 4. Ground and water classification
**Ground.**
- If the vertical velocity is above **100**: not on ground (flag 0x04 cleared, ground entity null). No probe.
- Otherwise trace the box from the origin to 0.25 units below. The ground plane, surface and contents are kept for
  friction and conveyors. The player is on ground when the trace has an entity and (the hit plane normal z ≥ 0.7 or
  the trace started in solid); a probe that hits nothing has normal z = 0 and is not ground.
  - On ground: ground entity = trace entity. A water-jump flight ends (all timer flags cleared, timer 0). If flag
    0x04 was not yet set, set it, and if the vertical velocity is below −200 set flag 0x10 with timer **18**, or
    **25** when below −400 (the **landing flag**).
  - Not on ground: ground entity null, flag 0x04 cleared.
  - **Landing velocity:** whenever the probe hit something (fraction < 1, entity non-null) and the vertical velocity
    is negative, the vertical velocity becomes 0 — also on surfaces too steep to stand on.
  - The probe's entity (if non-null) is appended to the touch list.
- The landing flag is decided from the velocity that comes out of the slide (Behaviour 13 keeps the falling
  velocity when the stepped path wins), so a 10 ms move that ends on the floor at −500 units/s sets flags 0x14 and
  timer 25; a single 50 ms move of the same fall clips the velocity in the slide and sets no landing flag (test
  vectors L1–L4). The game's command splitting (§19) keeps the moves of a vertically moving player at 10–19 ms.

**Water** (always, after the ground part): water level and type := 0. Let h = view height, s2 = trunc(h − mins z),
s1 = s2 / 2 (integer division). Sample the contents at (origin x, origin y, z) for z = origin z + mins z + 1, then
origin z + mins z + s1, then origin z + mins z + s2; while the sample has a liquid bit (0x38) the level becomes 1, 2,
3 in turn; the water type is the full contents of the first sample. Standing (mins z −24, h 35): samples at origin
z − 23, + 5, + 35; ducked (h 0): − 23, − 12, + 0.

### 5. Ladder detection and water-jump start
Skipped entirely while the timer is non-zero (the ladder flag is then false).
- Ladder: let f = the forward vector with z := 0, normalised. Trace the box from the origin to origin + f (1 unit).
  The player is on a ladder for this move when the trace fraction < 1 and its contents have the ladder bit.
- Water jump: only at water level exactly 2. Point A = origin + 30·f, raised by 4. If A's contents have the solid
  bit and the contents 16 units above A are 0 (completely empty): velocity := (200 × forward x, 200 × forward y,
  350) using the **full** forward vector (pitched, not normalised — looking up 30° gives 173.2), flag 0x08,
  timer 255.

### 6. Timer
If the timer is non-zero: d = msec >> 3, at least 1. If d ≥ timer: clear the timer flags and set the timer to 0;
otherwise timer −= d.

### 7. Jumping
In this order:
1. Flag 0x10 set (landed hard): nothing (flag 0x02 also unchanged).
2. Up move < 10: clear flag 0x02; done.
3. Flag 0x02 set or type 3: nothing.
4. Water level ≥ 2 ("swim jump"): ground entity := null; if the vertical velocity is above −300 it is set to 100
   when the water type is exactly 0x20 (water with no other bit), 80 when exactly 0x10, else 50. Flag 0x02 is not
   set, so holding jump in water repeats this every move.
5. No ground entity: nothing.
6. Call the ground-entity-is-client callback with the ground entity. Non-zero: nothing happens (the jump is
   refused; a player standing on another player cannot jump). Zero: set flag 0x02, ground entity := null, vertical
   velocity := **270** (exactly, whatever it was).

### 8. Friction
Speed = length of the 3D velocity. Speed < 1: horizontal velocity := 0 (vertical kept); done. Otherwise
drop = 0; if (there is a ground entity and the ground surface is not slick) or the player is on a ladder:
drop += max(speed, 100) × 6 × t; if the water level is non-zero and the player is not on a ladder:
drop += speed × 1 × water level × t. The whole velocity is scaled by max(0, speed − drop) / speed. Ducking and
leaning have no friction of their own.

### 9. Walking, air and ladder movement (water level < 2)
- Acceleration scale k: when the block's stagger value v is > 0, k = max(0, 1 − v), else k = 1. k multiplies every
  acceleration of this behaviour and of swimming, nothing else.
- Wish velocity: f and r = forward and right with z := 0, each normalised (so pitch does not reduce the horizontal
  wish speed); wish = f × forward move + r × side move, wish z = 0; then add currents and conveyors (below).
  Wish speed = length, wish direction = normalised wish.
- Maximum speed 300, or **75** when the player is ducked **and** on ground (flags 0x01 and 0x04 both set; a ducked
  player in the air keeps 300). A longer wish is cut to the maximum.
- Accelerate (wish direction d, speed w, rate a): add = w − (velocity · d); if add ≤ 0 nothing; else
  velocity += d × min(add, a × t × w).
- Ladder (flag from Behaviour 5): accelerate with rate 10 × k. Then, only if the wish has no vertical part, gravity
  pulls the vertical velocity toward 0 by gravity × t without crossing 0 (from above or below). Slide (Behaviour 13).
- On ground (not ladder): accelerate with rate 10 × k; then vertical velocity := 0 (afterwards only — with negative
  gravity the player stays on the ground). If both horizontal components are exactly 0 there is no slide this move.
  Otherwise slide.
- In the air: accelerate with rate **1** × k (fixed; there is no air-acceleration setting); vertical velocity −=
  gravity × t; slide.
- Ladder wish (applied while building the wish, only when on a ladder and |vertical velocity| ≤ 200): vertical
  wish = +200 when pitch ≤ −15 and forward move > 0; else −200 when pitch ≥ 15 and forward move > 0; else +200 for
  up move > 0, −200 for up move < 0, 0 otherwise. Horizontal wish components are each clamped to −25…25.
- Water currents: when the water type has current bits, add speed 400 (200 when the water level is 1 and there is a
  ground entity) times the current direction (sum of the unit axes of §6). Conveyors: when there is a ground entity,
  add 100 × the current direction of the ground contents.

### 10. Swimming (water level ≥ 2)
wish = forward × forward move + right × side move using the full (pitched) vectors; if forward, side and up move
are all 0 the vertical wish −= 60 (sinking), else vertical wish += up move; add currents/conveyors (Behaviour 9);
wish speed = min(length, 300) × 0.5; accelerate with rate 10 × k; slide. No gravity. Swimming speed is therefore at
most 150 units/s.

### 11. Water-jump flight and teleport pause
- Flag 0x08: no control; vertical velocity −= gravity × t; if it becomes negative the timer flags are cleared and
  the timer set to 0; slide. Jump, friction and acceleration do not run.
- Flag 0x20: nothing moves (velocity kept as it is) until the timer clears the flag.

### 12. Sliding along planes
One slide covers time t in at most 4 segments; the velocity at the start is the *primal velocity*:
1. Trace the box from the origin to origin + velocity × remaining time.
2. All-solid: vertical velocity := 0 and emergency placement (Behaviour 14); the slide ends at once.
3. Fraction > 0: move to the end position and forget the collected planes.
4. Fraction = 1: done.
5. Record the hit entity in the touch list (not for the spectator). Remaining time −= remaining × fraction.
6. Collect the hit plane normal (at most 5; a sixth stops the player: velocity 0, done).
7. One plane n: let u = velocity normalised, rub = 1 + 0.5 × (u · n). velocity := velocity − n × (velocity · n) ×
   1.01, each component with |value| < 0.1 set to 0, then the whole vector × rub.
   Two planes (the second hit without moving in between): rub from the first plane as above; velocity := c ×
   (c · velocity) × rub, c = cross(first normal, second normal).
   Three or more: velocity 0, done.
8. If velocity · primal velocity ≤ 0: velocity 0, done.
When the slide ends normally (not through step 2) and the timer is non-zero, the velocity is reset to the primal
velocity. Consequences: hitting a wall head-on stops the player dead (the 1.01 overbounce reverses the velocity and
step 8 zeroes it); sliding along a wall loses speed (a 45° wall at 300 units/s leaves ≈ 137 units/s).

### 13. Stepping
Every slide is a step-slide:
1. Remember start origin and velocity; slide → result D (origin, velocity).
2. Raised start U = start + 18 up. If a position test of the box at U is all-solid: keep D; done.
3. From U with the start velocity: slide → origin E. Trace the box from E to E − 18 up; if not all-solid move to its
   end (F), else F = E.
4. Compare (F − U) · start velocity with (D origin − start) · start velocity. Keep D when the first is smaller, or
   when the push-down trace's plane normal z is below 0.7 (including "hit nothing"); otherwise keep F with the
   velocity of the raised slide unchanged.
There is **no step-down**: walking off any edge is a fall. Steps up to 18 units are climbed without losing speed,
ramps up to the 0.7 limit are climbed with a horizontal velocity, and a fall that ends on the floor keeps its
falling velocity for the landing rule when the raised slide did not reach the floor.

### 14. Emergency placement (stuck in solid during a slide)
Developer message `EmergencySnapPosition()`. Candidates: stored origin + (dx, dy, dz) eighths with dz from +10 down
to −10 in steps of 2 (outer), dy likewise, dx likewise (inner) — the first candidate is +1.25 units on every axis.
Types 1 and 2 take the first candidate untested; others take the first whose position test is not all-solid. The
chosen candidate becomes the current origin and the fallback origin. If none is free the position is unchanged and
the slide ends.

### 15. Snapping to network precision
- Final snap (end of every move except types 5 and 7): velocity × 8 truncated into 16 bits (values beyond
  ±32767 wrap). For the origin, per axis: base = trunc(origin × 8); sign = +1 for origin ≥ 0, −1 below, 0 when
  base/8 equals the origin exactly. Candidates base + sign on the axes selected by the bit patterns 0, 4, 1, 2, 3, 5,
  6, 7 (bit 0 = x, 1 = y, 2 = z), in that order; the first whose position test is not all-solid is stored (types 1
  and 2: always the first). If none: the fallback origin is stored.
- The position test counts a box that exactly touches a surface as solid (Quake II brush rules), while a sweep stops
  1/32 unit short of it; so a player resting on a floor at z = 0 is stored at z = 24.125 (193/8), and a player
  stored exactly at 24.0 stays there (all candidates solid, fallback).
- Initial snap (snap-initial flag): candidates stored origin + (dx, dy, dz) eighths with each offset taken in the
  order **+1, 0, −1**, z outermost, x innermost. The first good candidate (types 1 and 2: the first) becomes the
  origin and the fallback origin. None good: developer message `Bad InitialSnapPosition` and nothing changes. A
  free spawn point therefore moves by +1/8 on all three axes every time the game sets the flag (Quake II tries the
  unchanged position first).

### 16. Movement types
| Type | Meaning (SPEC-202) | Movement |
|---|---|---|
| 0 | normal | everything above |
| 1 | noclip | view height 35. Friction: speed < 1 → velocity 0, else scale by max(0, speed − max(speed, 100) × 9 × t)/speed. Wish = normalised full forward × forward move + normalised full right × side move, vertical += up move, cut to 300; accelerate with rate 10 (no stagger scale); **only if the acceleration added speed** does the origin move by velocity × t, without collision. Box untouched. |
| 2 | spectator | SPEC-313 §8: as type 1 with view height 5, then (only if speed was added) a step-slide with box (−5, −5, −2)–(5, 5, 8) against the world only (content mask 0x02000003), no touch list. |
| 3 | dead | moves zeroed, always ducked; when on ground at the first classification the speed drops by 20 per move (3D, not below 0) before the ladder, jump and friction steps; never jumps; otherwise normal physics |
| 4 | gib | moves zeroed, gib box, normal physics |
| 5, 7 | frozen / chase | moves zeroed; only view angles are computed; state unchanged, view height 0, nothing touched |
| 6 | remote view | moves zeroed; duck, classification, ladder/water-jump start, timer, water-jump flight and snaps run; no jump, friction, acceleration or gravity — the player hangs in place, even in the air |

### 17. Speed limits (stock input, scale byte 255, 10 ms moves)
| State | Steady speed |
|---|---|
| walking (forward 200 / side 160) | 200 / 160 units/s |
| running (moves doubled) | 300 (also diagonally: 400/320 is cut to 300) |
| crouched on ground | 75, walking or running |
| crouched in the air, or airborne | wish cut to 300, but air acceleration is only 1 × 300 × t per move (3 units/s per 10 ms) |
| water level 1 on ground | as on ground (extra water friction is outweighed by acceleration) |
| swimming (level ≥ 2) | 150 maximum (100 when walking); sinking 60 × 0.5 wish without input |
| ladder | about 200 vertical, up or down (pressing into the ladder face periodically costs speed through the slide rule: 196.9–202.8, mean 201.6 when climbing with forward 200), horizontal wish ≤ 25 per axis |
| noclip, spectator | 300 |
| prone | does not exist in 1.07f |
Gravity is the state's value (the game writes trunc(`sv_gravity`)). Jumps: vertical 270; with gravity 800 the
ideal apex is 45.6 units, but the per-move truncation of the stored origin to 1/8 lowers it (10 ms moves: 42.25;
33 ms moves: 40.6).

### 18. Landing and the hand-off to the game
The routine itself causes no damage. The game compares the vertical velocity of the previous server frame with the
new one and turns the difference into falling damage, landing events and the jump sound (SPEC-780 §5, §13). Because
the landing-velocity rule zeroes the vertical velocity on the move that reaches the floor, the game sees the whole
fall speed as the difference. Flag 0x10 with timer 18 or 25 blocks jumping after hard landings; each move removes
msec >> 3 timer units (at least 1), so the lock lasts 180 / 250 ms with 10 ms moves and 165 / 231 ms with 33 ms
moves.

### 19. The server move (game module side)
Per user command the game (SPEC-780 §4–§5, with the correction below):
- **Command splitting:** if msec > 33 and the entity's vertical velocity is not 0, the command is run as
  msec / 10 (integer) moves: the first lasts (msec mod 10) + 10 ms, every further one 10 ms, so the total equals
  msec. Otherwise one move of msec. (Correction: SPEC-780 §4 says every split move lasts (msec mod 10) + 10 ms and
  the total exceeds msec; the game resets the command's msec to 10 after the first move.)
- Per move: zero the block; movement type per SPEC-780; gravity = trunc(`sv_gravity`); origin and velocity =
  trunc(entity value × 8); snap-initial when the movement state differs from the one stored after the previous move;
  command copied; run button 0x20 doubles forward, side and up move unless flag 0x40 is set; forward and side move
  := trunc(move × (q × (1/255))) with q = trunc(movement scale × 255) AND 255 (single-precision 1/255, so q = 255
  keeps the values); trace = the engine trace #52 with the player as pass entity and mask 0x02010003 (0x00010003
  when the player's health is below 1); point contents = import #54; ground-entity-is-client = "entity number
  1…`maxclients`"; stagger value = remaining knock-back stagger time in seconds (0 when none).
- After the move the game copies the state back, sets origin and velocity from it (÷ 8), takes box, view height,
  water level/type, ground entity and view angles, stores the command's lean, plays the jump sound and calls the
  touch functions of the distinct touched entities (SPEC-780 §5).

### 20. Client prediction
Each client frame, while connected and active, not paused, and the player state's cinematic-freeze byte is 0:
- If `cl_predict` is 0 or the received flags have 0x80: no movement prediction; predicted angles = view angles +
  delta angles × 360/65536. Done.
- If 64 or more commands are unacknowledged: nothing (with `cl_showmiss` the message `exceeded CMD_BACKUP`).
- Otherwise a zeroed block gets the movement state of the last received frame and the client's callbacks; the
  stagger value is 0 and the snap-initial flag is never set. For each sent but unacknowledged command (oldest first):
  the command is copied; the run button doubles forward, side and up move unless the received flags have 0x40;
  forward and side are scaled with the received movement-speed byte as in §19; the move runs with the same splitting
  rule as §19 (the test uses the predicted vertical velocity); the resulting stored origin is remembered for that
  command.
- Outputs: predicted origin = stored origin ÷ 8; predicted view angles = the routine's view angles. Step smoothing
  (Quake II rule, applied by SPEC-310 §12): when the final stored z exceeds the z stored for the command before the
  last predicted one by more than 63 and less than 160 eighths and flag 0x04 is set, predicted step = difference ÷ 8
  and step time = real time − frame time × 500 (ms).
- Prediction error (each received frame, when prediction is on): delta = received origin − the origin predicted for
  the acknowledged command, in eighths. |dx| + |dy| + |dz| > 640: the error is cleared (teleport). Otherwise, with
  `cl_showmiss` and a non-zero delta, `prediction miss on <server frame>: <dx+dy+dz>` is printed; the remembered
  origin is replaced by the received one and the error = delta ÷ 8 (decayed by the view code, SPEC-310 §12).
- Client callbacks: trace = world with mask 0x02010003 (a world hit reports a placeholder non-null entity) plus every
  entity of the current frame except the local player: brush models (packed solid 31) through their collision
  model, other solids as boxes from the packed solid (SPEC-202), mask 0x02010003. Point contents = world + brush-model
  entities of the frame. Ground-entity-is-client = "a real entity whose number is 1…configstring 31 (maxclients)".

### 21. Differences between the server move and the prediction
| Aspect | Server (game) | Client prediction |
|---|---|---|
| Movement routine | the same engine routine | the same engine routine |
| Stagger value | remaining stagger time | always 0 (staggered players are mispredicted) |
| Snap-initial | set after outside changes (spawn, teleport) | never (the +1/8 initial snap is not predicted; corrected through the prediction error) |
| Trace mask | 0x02010003, dead: 0x00010003 | always 0x02010003 |
| Entities in traces | all server entities via #52 | entities of the last received frame |
| Point contents | world + the solid entities at the point (brush models through their model, other solids as boxes reporting 0x2000000; solid type 4 is ignored) | world + brush models of the frame only |
| Run / scale input | current flags and current movement scale | flags and movement-speed byte of the last received frame |
| `gserver SIZEMATTERS` | server process value | client process value (view height and water sampling differ if they differ) |
| FPU precision | 24-bit (measured) | UNVERIFIED (expected the same) |
| Touch list, ground entity | used by the game | ignored |

## Constants
| Name | Value |
|---|---|
| Box | (−16, −16, −24)–(16, 16, 40); ducked maxs z 14; gib (−16, −16, 0)–(16, 16, 16) |
| View heights | standing 35, ducked 0, gib 8, noclip 35, spectator 5; SIZEMATTERS s·59 − 24 (−8…39), s·24 − 24 (−10…13) |
| Stop speed / maximum speed / ducked maximum | 100 / 300 / 75 |
| Acceleration ground / air / water | 10 / 1 / 10 (× stagger factor max(0, 1 − v)) |
| Friction ground (and ladder) / water | 6 / 1 × water level; noclip and spectator 6 × 1.5 |
| Water current / conveyor speed | 400 (200 at level 1 on ground) / 100 |
| Swim | wish cut to 300 then × 0.5; sinking wish −60; swim jump 100 / 80 / 50, not at vz ≤ −300 |
| Jump | vertical velocity := 270; needs up move ≥ 10 |
| Water jump | probe 30 ahead, +4 solid, +20 empty; velocity 200 × forward x/y, 350 up; timer 255 |
| Ground | probe 0.25 down; normal z ≥ 0.7; airborne when vz > 100 |
| Landing | flag 0x10, timer 18 below −200, 25 below −400 |
| Ladder | probe 1 unit along flat forward; vertical wish ±200 (pitch ±15 with forward move, or up move); horizontal ±25; active while the vertical speed is at most 200 |
| Slide | 4 segments, 5 planes, overbounce 1.01, stop epsilon 0.1, rub 1 + 0.5 (v̂ · n) |
| Step | 18 up, 18 push-down, step normal 0.7 |
| Emergency placement | ±10 eighths per axis, step 2, z then y then x, each from +10 down |
| Initial snap | +1, 0, −1 eighth per axis, z outer, x inner |
| Final snap jitter order | 0, 4, 1, 2, 3, 5, 6, 7 |
| Timer countdown | msec >> 3, at least 1 |
| Touch list | 32 entries |
| Command splitting | msec > 33 and vz ≠ 0 → msec/10 moves: (msec mod 10) + 10, then 10 ms each |
| Prediction | 64 commands backup; step smoothing 64…159 eighths; error reset above 640 eighths |
| Developer messages | `EmergencySnapPosition()`, `Bad InitialSnapPosition` |

## Test vectors / how to verify

### Setting
A test harness calls the routine directly (as the original game does through import #81) with these callbacks:
- World = a list of convex brushes (planes with outward normals; inside = n·p − d < 0 for every plane), each with
  contents, surface flags and an entity number. Only brushes whose contents intersect 0x02010003 block traces.
- Trace: start = end → position test (Quake II rule: for each plane, offset = the box corner chosen per normal sign
  (maxs where the normal component is negative, else mins); the box is inside when n·start − (d − offset·n) ≤ 0 for
  every plane → all-solid, start-solid, fraction 0; the first such brush gives contents and entity). Otherwise the
  Quake II box sweep against each brush in list order, stopping when the fraction is 0: per plane d1, d2 = distances
  of start and end from the plane moved out by the box offset; a brush is skipped when d1 > 0 and d2 ≥ d1 on any
  plane; entering planes give (d1 − 1/32)/(d1 − d2), leaving planes (d1 + 1/32)/(d1 − d2); when no plane has d1 > 0
  the sweep started inside: start-solid, and all-solid as well when no plane has d2 > 0, **fraction unchanged** (as
  in the engine's own traces); otherwise the largest enter fraction is accepted (clamped at 0) when it is below the
  smallest leave fraction, above −1 and below the current fraction, giving plane, surface and contents.
  End = start + fraction × (end − start) (= end at fraction 1). The entity is that of the brush that last changed the
  result, or the world entity (number 0) when none did.
- Point contents = OR of the contents of every brush that strictly contains the point.
- Ground-entity-is-client returns 1 for entity numbers 1…N (N given per vector, default 0).
Worlds: **F** = floor box (−8192, −8192, −64)–(8192, 8192, 0) solid, entity 0; **E** = empty; others named in the
row. Unless stated: msec 10, gravity 800, angles 0, lean 0, stagger 0, snap-initial 0, flags 0. Origins and
velocities are the stored eighths (divide by 8 for units); "×n" = the command repeated n times, the state carried
over (the block re-zeroed except state, command and callbacks; snap-initial only on the first move).

### Ground movement (world F, start origin (0, 0, 193) = standing on the floor, flags 0x04)
| # | Start / command | Result |
|---|---|---|
| G1 | forward 400 | ×1: origin (2, 0, 193), velocity (239, 0, 0); ×2: (6, 0, 193) (431); ×3: (12, 0, 193) (623); ×10: (103, 0, 193) (1800); ×15: (2384); ×20: (328, 0, 193) (2400) and 2400 from then on |
| G2 | forward 200 | ×1: velocity 159; ×10: 1141; ×20 and later: 1600 |
| G3 | side 160 | ×1: origin (0, −1, 193), velocity (0, −127, 0); ×30: (0, −274, 193), (0, −1280, 0) |
| G4 | forward 400, side 320 | ×1: velocity (187, −149, 0); ×30: (1874, −1498, 0) |
| G5 | forward 400, msec 50 | ×1: origin (60, 0, 193), velocity 1200; ×2: (162, 0, 193) 2040; ×3: 2400 |
| G6 | forward 400, up −200 | ×1: flags 0x05, maxs z 14, view height 0, velocity 59; ×10: 167; ×40: 526; ×49 and later: 600 (75 units/s) |
| G7 | flags 0x05, velocity (2400, 0, 0), forward 400, up −200 | ×1: 2256; ×5: 1759; ×10: 1288; ×20: 685; ×23 and later: 600 |
| G8 | velocity (2400, 0, 0), no moves | ×1: origin (22, 0, 193), velocity 2256; ×2: 2120; ×3: 1992; ×6: 1653; ×9: 1371; ×12: 1137 |
| G9 | velocity (400, 0, 0), no moves | ×1: 352; ×3: 256 |
| G10 | forward 400, stagger 0.25 | velocity 179 |
| G11 | forward 400, stagger 1.5 | ×3: no movement, velocity 0 |
| G12 | forward 400, pitch 60° (angles (10923, 0, 0)) | identical to G1 (×30: origin (568, 0, 193), velocity 2400) |
| G13 | forward 400, yaw 90° (angles (0, 16384, 0)) | ×1: origin (0, 2, 193), velocity (0, 239, 0) |
| G14 | velocity (0, 0, 720) (90 up), forward 400 | on ground: velocity (239, 0, 0) |
| G15 | velocity (0, 0, 1200) (150 up), forward 400 | airborne: flags 0, origin (0, 0, 204), velocity (24, 0, 1136) |
| G16 | world F with floor contents 0x40001 (conveyor +x), no moves | ×1: velocity 79; ×3: origin (2, 0, 193), 142 |
| G17 | world F with surface flags 0x2 (slick), velocity 2400, no moves | ×3: velocity 2400, origin (72, 0, 193) |
| G18 | type 3, velocity (800, 0, 0), forward 400, up 200 | ×1: flags 0x05, velocity 592, command moves (0, 0, 0); ×2: 384; ×3: 176 |
| G19 | type 6, velocity (800, 0, 0), forward 400, up 200 | position and velocity unchanged, command moves (0, 0, 0) |

### Jumping and landing (world F unless stated)
| # | Start / command | Result |
|---|---|---|
| J1 | standing, up 200 | flags 0x02, origin (0, 0, 213), velocity (0, 0, 2096), no ground; callback called once with entity 0 |
| J2 | as J1 ×60 | ×33: z 531, vz 48; ×34: z 530, vz −16 (apex 531 = 42.25 units above the start); ×60: z 289, vz −1680 |
| J3 | as J1, msec 33, ×40 | ×1: z 257, vz 1948; ×10: z 518 (apex), vz 40; ×19: z 212, vz −1859; ×20: flags 0x16, timer 18, z 193, vz 0; ×21–24: timer 14, 10, 6, 2; ×25 and later: flags 0x06 (jump still held, no new jump) |
| J4 | flags 0x06, up 200 | no jump, flags 0x06 |
| J5 | flags 0x06, up 0 | flags 0x04 |
| J6 | flags 0x14, timer 18, up 200 | no jump, flags 0x14, timer 17 |
| J7 | up 9 | no jump |
| J8 | velocity (0, 0, 400) (50 up), up 200 | as J1 (vz 2096: the jump sets 270, it does not add) |
| J9 | floor is entity 3, N = 8, up 200 | jump refused: flags 0x04, velocity 0, callback once with entity 3, touch list (3, 3) |
| J10 | floor is entity 3, N = 2, up 200 | jumps as J1 |
| L1 | flags 0, origin (0, 0, 216), velocity (0, 0, −4000) | flags 0x14, timer 25, origin (0, 0, 193), velocity 0, touches (0, 0, 0) |
| L2 | as L1 with velocity −2400 | flags 0x14, timer 18 |
| L3 | as L1 with velocity −1200 | still airborne: origin (0, 0, 203), velocity −1264 |
| L4 | as L1 with msec 50 | flags 0x04 only (no landing flag), velocity 0 |
| L5 | origin (0, 0, 200), velocity −4000, up 200, ×3 | flags 0x14, timer 25, 24, 23; no jump |
| A1 | origin (0, 0, 800), forward 400 | velocity (24, 0, −64), origin (0, 0, 799) |
| A2 | origin (0, 0, 800), velocity (2400, 0, 0), forward 400, up −200 | flags 0x01, maxs z 14, view height 0, velocity (2400, 0, −64) (no 75 limit in the air) |
| A3 | world E, gravity 0, flags 0x01, origin (0, 0, 800), forward 400, up −200, ×30 | velocity (720, 0, 0) |
| A4 | world E, origin (0, 0, 0) ×10 | origin (0, 0, −30), velocity (0, 0, −640) |

### Ducking, timer, angles
| # | Start / command | Result |
|---|---|---|
| D1 | standing, lean +1, up −200 | stays standing (flags 0x04, maxs z 40) |
| D2 | flags 0x05, lean −1, no moves | stays ducked |
| D3 | flags 0x05, no moves | stands up: flags 0x04, maxs z 40, view height 35 |
| D4 | world F + ceiling box z 50…100, flags 0x05, no moves | stays ducked |
| D5 | `gserver SIZEMATTERS 0.5` | standing view height 5.5, ducked −10 |
| D6 | SIZEMATTERS 2 / 1.3 / 0.1 | standing 39 / 39 / −8, ducked 13 / 7.2 / −10 |
| T1 | flags 0x14, timer 18, msec 50 | timer 12 |
| T2 | as T1 with msec 5 | timer 17 |
| T3 | flags 0x14, timer 2, msec 30 | flags 0x04, timer 0 |
| T4 | flags 0x34, timer 10, velocity 2400, angles (2000, 3000, 500), forward 400 | no movement, velocity 2400, timer 9, view angles (0, 16.479, 0) |
| V1 | angles (21845, 0, 0) | view pitch 89 |
| V2 | angles (−21845, 0, 0) | view pitch −119.998 (not limited) |
| V3 | angles (−500, 30000, 0), delta (1000, −2000, 0) | view angles (2.747, 153.809, 0) |

### Snapping and types
| # | Start / command | Result |
|---|---|---|
| S1 | world E, origin (0, 0, 192), snap-initial | origin (1, 1, 193) |
| S2 | world F, origin (0, 0, 192), snap-initial | origin (1, 1, 193) |
| S3 | world F, origin (0, 0, 192), no moves | stays (0, 0, 192) |
| S4 | floor top at z 1, origin (0, 0, 192), forward 400 | `EmergencySnapPosition()`; origin (10, 10, 202), on ground, velocity 239 |
| S5 | world F, origin (0, 0, 192), forward 400 | origin (10, 10, 202), not on ground (1.25 above the floor) |
| S6 | one solid box covering everything, snap-initial, forward 400 | `Bad InitialSnapPosition` and `EmergencySnapPosition()`; origin unchanged, velocity 239 |
| Y1 | type 1, world E, forward 400 | ×1: origin (2, 0, 0), velocity 239; ×2: (6, 0, 0), 407; view height 35 |
| Y2 | type 1, world E, angles (−5461, 0, 0) (pitch −30), forward 400, up 100 | origin (1, 0, 1), velocity (181, 0, 157) |
| Y3 | type 1, world E, velocity (2400, 0, 0), no moves | ×1: velocity 2184, origin unchanged; ×2: 1987, origin unchanged |
| Y4 | type 4, world F, origin (0, 0, 8) | box (−16, −16, 0)–(16, 16, 16), view height 8, origin (0, 0, 7), velocity −64 |
| Y5 | type 5 or 7, velocity (800, 0, 0), forward 400, up 200 | state unchanged, view height 0, nothing touched, command moves (0, 0, 0) |
| Y6 | type 6, world F, origin (0, 0, 800), velocity (800, 0, −800) ×3 | unchanged (hangs in the air) |

### Steps, walls, ramps (start standing, forward 400, velocity (2400, 0, 0))
| # | World / start | Result |
|---|---|---|
| P1 | F + box (40, −512, 0)–(512, 512, 16) | ×7: origin (168, 0, 193); ×8: (192, 0, 321) — 16 units up, velocity 2400 kept; msec 50 ×2: (240, 0, 321) |
| P2 | as P1 with box height 20 | ×10: origin (191, 0, 193), velocity 0 (blocked dead) |
| P3 | P1 world, start (480, 0, 321) on the step, yaw 180 (angles (0, 32768, 0)), velocity (−2400, 0, 0) | ×10: (240, 0, 321); ×13: (168, 0, 321) airborne; ×14: (144, 0, 320), vz −64; ×20: (0, 0, 300); ×25: (−120, 0, 265) (no step-down) |
| P4 | gravity 0, no floor, brush {x − y > 40} (normal (−0.70710678, 0.70710678, 0), distance −28.2842712), start (52, 0, 0), no moves | origin (67, 4, 0), velocity (767, 783, 0) |
| P5 | gravity 0, boxes (20, −512, −512)–(100, 512, 512) and (−512, 20, −512)–(512, 100, 512), start (16, 16, 0), velocity (2400, 2400, 0), no moves | origin (31, 31, 0), velocity 0, six touches |
| P6 | F + ramp brush {normal (−0.5, 0, 0.8660254), distance −50; x ≤ 2000; z ≥ 0}, start (400, 0, 193) | ×11: (664, 0, 193); ×12: (688, 0, 202); ×20: (880, 0, 313); velocity stays (2400, 0, 0) |

### Water and ladders
| # | World / start / command | Result |
|---|---|---|
| W1 | F + water box (contents 0x20) z 0…20, standing / ducked (flags 0x05, up −200) | level 1 / level 2, type 0x20 |
| W2 | water to z 30 | standing 2, ducked 3 |
| W3 | water to z 60 | 3 / 3 |
| W4 | water to z 30, forward 400 | ×1: velocity 119; ×30: 1200 (150 units/s) |
| W5 | water to z 20, forward 400 | ×30: 2400 |
| W6 | water to z 30, SIZEMATTERS 0.5 / 2 | level 3 / 1 |
| W7 | all water, origin (0, 0, 0), no moves | ×1: velocity (0, 0, −23); ×5: −110, origin (0, 0, −1) |
| W8 | all water, forward 400 | ×1: 119; ×30: 1200 |
| W9 | all water, up 200 | ×1: vz 800, origin z 8; ×2: 800, z 16 |
| W10 | all water, velocity (0, 0, −2400), up 200 | no swim jump: velocity −2248 |
| W11 | all slime (0x10) / all lava (0x08), up 200 | vz 700 / 468 |
| W12 | all water with currents (contents 0x60020), up 200 / no moves | (107, 0, 441) / ×1: (118, 0, −17), ×3: (344, 0, −50) |
| W13 | all water, forward 400, stagger 0.5 | velocity 59 |
| W14 | F + water z 0…40 + box (30, −512, 0)–(512, 512, 32), start (16, 0, 193), forward 400, pitch −30 (angles (−5461, 0, 0)) | flags 0x08, timer 254, origin (29, 0, 220), velocity (1385, 0, 2736); same with pitch 0: velocity (1600, 0, 2736); ×10: timer 245, z 418, level 1; ×40: (475, 0, 743), vz 240, still flag 0x08 |
| W15 | world E, flags 0x08, timer 255, velocity (800, 0, 2800) | ×1: timer 254, velocity (800, 0, 2736); with velocity (800, 0, 40): flags 0, timer 0, velocity (800, 0, −24) |
| K1 | F + ladder box (17, −512, 0)–(40, 512, 512) contents (bit 29 + solid), standing, pitch −20 (angles (−3641, 0, 0)), forward 200 | ×1: origin (0, 0, 194), velocity (20, 0, 160), flags 0; ×3: velocity (48, 0, 384); ×10: origin (4, 0, 254), velocity (141, 0, 1142); with the ladder 2048 high, from ×20 on the vertical velocity cycles 1575, 1622, 1621, 1620, 1619, 1617, 1616, 1615 |
| K2 | K1 world, origin (0, 0, 800), pitch +20, forward 200 | ×1: velocity (20, 0, −160) |
| K3 | K1 world, origin (0, 0, 800), up 200 | ×1: (0, 0, 159); ×3: 383 |
| K4 | K1 world, origin (0, 0, 800), velocity (0, 0, 800), no moves | ×1: 688; ×3: 464 |
| K5 | K1 world, origin (0, 0, 800), velocity (0, 0, −2400), up 200 | −2192 (vertical speed above 200: no ladder wish; friction and gravity damping only) |

### In-game checks (UNVERIFIED)
1. Client FPU precision: run a listen server and a remote client with `cl_showmiss 1`; walk from rest on flat
   ground. Any `prediction miss` while walking straight means the client computes differently (most likely the FPU
   precision; G1's 239 vs 240).
2. Stagger misprediction: get hit by a strong knock-back on a remote server with `cl_showmiss 1`: misses while the
   stagger lasts (§21).
3. Spawn snap: after `kill` and respawn on a remote server one small prediction miss (the +1/8 initial snap).
4. Landing: jump down 300 units in a demo; the first frame on the floor shows flags 0x10 and the game's landing
   event (SPEC-780 §13).
5. Command splitting: a client with `cl_maxfps 20` (50 ms commands) jumping: the jump arc matches J3-style 10–19 ms
   moves, not one 50 ms move (server-side movement in a demo).

## Open questions
1. Where the 24-bit FPU precision is set (engine start-up or a system library); whether the client process runs
   with the same precision after the renderer and sound back-ends are initialised (§21, check 1).
2. The spectator move's world-only trace (type 2) uses the loaded map directly; it could not be exercised with the
   synthetic callbacks (the harness runs before a map is loaded). Its rules are SPEC-313 §8.

## Corrections and answers for other specs
- **SPEC-780 §4 (command splitting):** replace "each lasting (msec mod 10) + 10 ms … This runs longer than msec when
  msec mod 10 ≠ 0; reproduce it for parity" by "the first lasting (msec mod 10) + 10 ms and every further one 10 ms;
  the total equals msec". The client's prediction splits the same way (§20).
- **SPEC-313 §8:** the spectator move is part of this spec's type table (Behaviour 16); SPEC-313 keeps the details.
- **SPEC-AMEND-005 G18.21b:** every row is confirmed by the original routine. Additions: the landing-velocity rule
  applies to any probe hit regardless of slope (row 13); the stepped path keeps its velocity (row 16), which is what
  makes the landing flag appear after falls; arithmetic is single precision (§1); looking up is not clamped by the
  routine (Behaviour 2); type 6 still starts and flies water jumps (Behaviour 16); the ladder's vertical wish only
  applies while |vz| ≤ 200 (Behaviour 9).
- **SPEC-780 §5 jump sound** relies on the ground entity being cleared by the jump (Behaviour 7 step 6); a refused
  jump keeps the ground entity, so no sound.
