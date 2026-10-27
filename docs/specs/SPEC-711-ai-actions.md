# SPEC-711: Character AI actions (movement, turning, aiming, firing, animation-driven actions, vehicle commands)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-700 (AI framework: action queue, current/previous action, think order, bounding-box presets),
SPEC-701 (perception records), SPEC-710 (decisions: plans, success feedback, kinds), SPEC-722 (bodies: animation
records and flags, playback, root motion and yaw limits, weapon handling, firing, throwing, model events, corpses),
SPEC-750 (script AI commands and scripted action life cycle), SPEC-760 (skill rows T-a … T-i), SPEC-752 (savegame
reference encodings), SPEC-303 (savegame container), SPEC-001 (engine trace/sound imports).

## Purpose
An *action* is the unit of behaviour a character executes, one at a time, from its action queue (SPEC-700 rule 8).
Every action is driven by one animation record of the character's body (SPEC-722): the action starts the
animation, turns the character towards its facing point within the animation's yaw limits, moves it by the
animation's root motion (with Quake-II-style step movement), aims and fires the hand-held weapon, handles the
animation's model events, and decides when it is complete. This spec defines the action classes, their common
per-think behaviour, the aiming/accuracy model, completion rules, the vehicle command actions, and their savegame
records. Which action a decision creates is SPEC-710 rule D3; which action a script command creates is SPEC-750.

Terms: *owner* = the character; *record* = the action's animation record; *destination* and *facing point* =
two positions given at creation (zero vector = none); *duration* d and *end time* = start + d; *ends with
animation* = a creation flag: the action completes when the animation is done (SPEC-722 "Animation done") rather
than only when the duration elapses; *owning decision* = the decision that created it (none for script/pain/death
actions created by the framework); *linked action* = an action to notify on completion (pain actions link the
action they interrupted); *move result* m and *result* r = the two values reported at completion (SPEC-710 D1 uses
m). T-a … T-d and profile[i] as in SPEC-710.

## Interface / layout

### Action classes
Class code = savegame class code (SPEC-700). Kind = the value decisions read (SPEC-710 D1: kind 4 gives no success
feedback; spawnflag 262144 and the scripted decision use the owning decision, not the kind).

| class | chunk | bytes | role | created by | kind |
|---:|---|---:|---|---|---:|
| 0 | — | — | not creatable (`ERROR: only allowed to use leaves of action class tree: %d`, makes an inert action) | | |
| 1 | `AISA` | 112 | play in place (stand, idle, aim, hit reaction, waits) | decisions (D3 classes 0 and 5), path/point waits | 0 |
| 2 | `AIPA` | 112 | pain reaction | the AI's pain rule (SPEC-700 §10) | 2 |
| 3 | `AIDA` | 124 | dying / corpse | death (SPEC-700 §10), D3 class 6 | 3 |
| 4 | `AIXA` | 124 | dying without a start step (the pose already shown is kept until the death choice changes it) | death with spawnflag 65536; player corpses (AI type 14) | 3 |
| 5 | `AIXA` | 124 | special death `std_shead_2frnt_n_n` played at 1.5 × speed | "interrupt with animation" requests (no 1.07f decision uses it) | 3 |
| 6 | `AIEA` | 112 | end of script control | scripted decision (D3 class 8) | 0 |
| 7 | `AIWA` | 112 | move to a destination | D3 class 1, scripts (kind 1) | 1 |
| 8 | `AIDW` | 124 | animal move with left/right turn animations | dog AI move requests | 1 |
| 9 | `AIJA` | 124 | jump to a point | D3 class 3, scripts (kind 3) | 5 |
| 10 | `AIFA` | 120 | fall | D3 class 4, scripts (kind 4) | 6 |
| 11 | `AISA` | 112 | timed animation | no 1.07f caller | 7 |
| 12 | `AICA` | 112 | timed animation | no 1.07f caller | 4 |
| 13 | `AIAA` | 120 | attack: aim and fire (standing, crouched, while moving) | D3 classes 1, 2, 0 | 4 |
| 14 | `AIRA` | 124 | dog leap attack | dog AI attack requests | 4 |
| 15 | `AIHE` | 132 | helicopter: autonomous (damage/death) behaviour | helicopter AI | 0 |
| 16–19 | `AIHE` | 132 | helicopter command (flight, weapons, misc families) | HELI script command (SPEC-750 §12.8) | 0 |
| 20 | `AITK` | 132 | snowcat command | VEHICLE script command | 0 |
| 21 | `AITK` | 132 | tank command | VEHICLE script command | 0 |
Other codes: `ERROR: invalid action class code: %d` and an inert action.

### Common record (all classes; classes 1, 2, 6, 7, 11, 12 = this record only, 112 bytes)
Little endian, offset 0 ignored on load. Entity = entity number (−1 none); animation and script event = SPEC-752
references; decision/action = AI object id (SPEC-700).
| offset | type | meaning |
|---:|---|---|
| 4 | animation | record (default `std_istand_n_a_n`) |
| 8 | float[3] | facing point |
| 20 | float[3] | destination |
| 32 | float | creation time |
| 36 | byte (+3) | interrupted flag |
| 40 | int | "stopped" flag (A1: set when an action this one is linked from completed with r < 0.001) |
| 44 | decision id | owning decision |
| 48 | action id | linked action |
| 52 | float | movement scale (record value at 0x3c; 1.0 without record) |
| 56 | float | turn allowance (2 × record value at 0x40; 30 without record) |
| 60 | int | movement flags: 1 = may receive a jump impulse, 2 = may step off ledges, 4 = full step height (default 4) |
| 64 | float | start time |
| 68 | float | duration |
| 72 | float | end time |
| 76 | float[3] | jump impulse (horizontal velocity, and vertical speed at 84) |
| 88 | float | earliest time for the next landing check (start + 999 s initially) |
| 92 | int | ends with animation |
| 96 | script event | AI-done marker (SPEC-750 §12.6) |
| 100 | byte (+3) | lethal flag (attack) |
| 104 | int | target-bolt selector (attack) |
| 108 | entity | target entity (attack) / owner (pain) |

Class tails: 3, 4, 5: 112 float death start time, 116 byte (+3) "death notified", 120 animation (requested death
record); 8: 112, 116, 120 animations (straight, turn-left, turn-right records); 9: 112 float[3] the owner's velocity at start (vertical part zeroed);
10: 112 int landed, 116 animation (the original fall record); 13: 112 int target health at creation, 116 float
creation time; 14: as 13 plus 120 int leap state (0 none, 1 airborne, 2 hit); 15–21: 112 int, 116 int command id,
120 float scalar parameter, 124 routine reference, 128 queued routine reference (SPEC-752 function references;
roster spec).

## Behaviour

### A1. Life cycle
1. **Creation**: an action is created with owning decision, linked action, record, destination, facing point,
   duration and the ends-with-animation flag. The standard creation requests reuse the AI's previous action when it
   has the same class (SPEC-700 rule 8 "Reuse").
2. **Start** (when it becomes current): unless "stopped", the record is played (A3) and start time = now, end time
   = now + duration.
3. **Each think** (SPEC-700 rule 7 step 10) the action executes once (A2 then its class rule A8). When the class rule
   reports completion with (m, r): the owning decision is told (SPEC-710 D1 with m), the linked action is told r,
   the AI-done marker (if any) is set, and the next action starts. An action told r < 0.001 by an action it is
   linked to becomes "stopped" (otherwise the stopped flag is cleared).
4. **Interrupt** (SPEC-700 rule 8): the interrupted flag is set and the action goes back to the front of the queue;
   when restarted it keeps its record. A stopped action does not restart its animation and, on its next think,
   completes at once with (0, 0).
5. **Removal readiness** (SPEC-700 "ready to be removed"): yes for every class except dying (3, 4, 5), which follow
   the corpse rules of SPEC-722 "Corpses". The `item_equip_armor` exception of those rules (rule 1) is checked only
   by class 5; classes 3 and 4 skip it (UNVERIFIED in game: an armour item targeting a normal corpse does not keep
   it).

### A2. Per-think animation step (all ground classes)
In this order, each think, unless the "stopped" flag is set or the owner has no body (then the action completes
with m = r = 0):
1. the body's face/voice update and weapon charge effects run (SPEC-722);
2. the body's event flags for this think are read (A4) and handled;
3. the bounding-box preset of the record's posture is applied (SPEC-700 §15; dying classes allow the fallback);
4. the desired facing is computed (A5);
5. the yaw limits for this think are read from the body (SPEC-722 "Turning and root motion", fallback limit = the
   turn allowance) and the owner turns (A5);
6. the aim angles are updated (A7);
7. the root-motion step of this think is read from the body (movement scale applied); the class rule then moves the
   owner by it (A6) before testing completion.
Flag 0x1000 records (aim-capable) on attack actions add "fire" to the event flags whenever the body reports *ready
to fire* (SPEC-722 Weapon handling).

### A3. Starting the animation
When the body's current animation is still playing, or the new record loops (flag 0x08000000), the record is *set*
(no restart); otherwise it is *forced* (restarted). Attack actions start the other way round first (force unless
looping) and then apply the same rule. Pain actions always restart their animation unless they resume after an
interrupt. Classes 4 and 5 do not use this rule (A8).

### A4. Event flags (from model notes, SPEC-722) and their effects
| flag | source | effect in actions |
|---:|---|---|
| 0x01 | (body) | entity event 3 on the owner (left footstep) |
| 0x02 | note `fire`; aim-capable records when ready (A2) | the body fires (SPEC-722 Firing, without pacing) along the aim, using the action's lethal flag and target-bolt selector; when the lethal flag or bolt selector is set the AI's target first becomes the action's target entity; the lethal flag is cleared after the shot |
| 0x40 | note `jump` | if the action may receive a jump impulse: the owner gets the jump velocity (class 9, A8) |
| 0x80 | (dying bodies) | dying classes: a floor decal 10 units above the owner's origin |
| 0x100 | notes `thud`, `knee` | sound `impact/gore/fall1.wav`, `fall2.wav` or `fall3.wav` (random) on channel 4 |
| 0x200 | (dog bodies) | dog leap: leap state "hit" and the dog's yaw changes by ±20° (random side) |
| 0x400 | note `throw` | the body throws its left-hand item (SPEC-722 Throwing) |
Note `inair` pauses the animation (pause kind 1) until landing; `EOS`/`BOS` mark the animation finished
(SPEC-722).

### A5. Facing and turning
- Desired facing (written to AI record offsets 16–27, which therefore hold the desired angles while alive and the
  death angles afterwards): towards the facing point lowered by 20 units when one is given; otherwise towards the
  destination; when neither is given (or the destination equals the owner's position) the current angles are kept.
- With the AI's face-the-player flag (record offset 424, SPEC-710) and a living owner and a sight client: the owner
  faces the sight client horizontally, immediately, and nothing else is turned.
- Otherwise the desired yaw is used, modified for actions owned by path, point or scripted decisions (kinds 7, 8,
  9): record flag 0x8000 (backwards) → + 180°, 0x10000 → + 90°, 0x20000 → − 90°. The yaw change of this think is
  the wrapped difference (−180…180) clamped to the body's left/right limits; angles are quantised to 1/65536 of a
  turn. With debug cvar `ai_pathtest` ≠ 0 a dead character forced beyond its limit prints `dead monster forcing turn
  at %3.3f degrees.`.

### A6. Movement (root motion and step movement)
1. The step vector is the body's root-motion displacement for this think (SPEC-722) × movement scale.
2. The AI's velocity estimate v (AI record offset 4, used by SPEC-701 for character targets) becomes 0.9·v; if the
   step move fails v becomes 0 and the action is told "blocked"; if |step|² < 1, v becomes 0; otherwise v += 0.1 ×
   step.
3. Step move (Quake II `SV_movestep` style):
   - characters whose movement type is not "step" trace their box from the origin to origin + step (mask
     0x2020003; no mask for the no-clip movement type), retrying once without the vertical part; with entity flag
     "fly" and water level 0 the end point (1 unit above the box bottom) must not contain water, slime or lava
     (contents mask 0x38); with flag "swim" and water level < 2 it must contain one of them;
   - step-moving characters move only while on the ground with velocity² ≤ 121; the box is raised by the step
     height (the AI's step height, 24 by default, when movement flag 4 is set, else 2), traced to the step target
     and down by twice that height; when starting in a solid the height is halved, then removed; the result is
     refused when it ends in or starts in a solid, when the character would stand on another living character or
     player (unless movement flag 2, or the other is dead, or the new position is not above the other's top), or in
     liquid when not in liquid;
   - the ledge test (SPEC-700 helper rule "Bottom check", second form, with the movement flags) must pass; otherwise
     with movement flag 2 the character may step off (entity flag "partial ground" set when the floor 20 units below
     is missing or steeper than normal z 0.8), else the move is refused and the old position kept;
   - accepted moves update the ground entity and link the entity.
4. Jump impulse (movement flag 1, on the `jump` note while on the ground and not flying): with the impulse (h, vz)
   computed at the start of a jump action (A8 class 9), g = cvar `sv_gravity` and T = 2·vz/(0.1·g): if T·|h| exceeds
   the jump reach (AI record offset 448, 100) the horizontal part is scaled to length 100/T; the owner's velocity
   becomes (h, vz), its ground is cleared, and the expected landing time is now + 0.1·T.

### A7. Aiming
The aim angles (AI record offsets 564–575) are recomputed every think:
1. Not alerted: aim angles = the owner's angles.
2. Alerted within the last 1 s (SPEC-700 "alerted recently") and the target is a player: **precise model**.
   - Aim reference P: the player's lean hitbox position if leaning, else the player's origin; 20 units lower when the
     player is ducked (player move flag 1).
   - d = distance owner → P; u = unit direction; f = owner forward; t = reaction age − T-d·profile[4].
   - Hit probability p:
     - d < 64: p = 1;
     - otherwise p = (f·u) × profile[3] × T-b × D × M, with D = (profile[1]/d)² when d ≥ profile[1], else 1; M = A ×
       (1 − 0.6·L·s/225), where s = the player's speed, L = 1 − |f·v̂| (v̂ = player velocity direction), and A = 0.6
       when the player is not on the ground (player move flag 4 clear), else 1;
     - settle: if t < T-d·profile[5], p ×= clamp(t / (T-d·profile[5]), 0, 1);
     - near blend: for 64 ≤ d < 128, p = (1 − w) + w·p with w = (d − 64)/64.
   - A uniform number in [0, 1] ≤ p is a **hit**: aim point = P + (U(−16, 16), U(−16, 16), U(−24, 40)); entity flag
     0x1000 of the owner is cleared.
   - Otherwise a **miss**: entity flag 0x1000 of the owner is set — while it is set the owner's bullet traces pass
     through the sight client (a deliberate miss can never hit the player; bullet attack spec) — and the miss mode
     is: player airborne → *below*;
     L·s/225 > 0.1 → *lateral* (*behind* or *ahead* by the sign of the player's motion across the line of fire);
     otherwise *side*. With right vector r and up vector q of the direction to P: side: P + r·(±U(16, 32) + U(−8, 8)) +
     q·U(−8, 8) (random sign); below: P − q·U(16, 32) + r·U(−8, 8); lateral: P ∓ r·U(16, 32) + q·U(−8, 8).
   - The aim angles point at the chosen point; the aim yaw is clamped to ± Y of the owner's yaw, Y = 45° when the
     body's current record posture is crouched or prone, else 20°.
3. Otherwise (non-player target, or a player not seen within ~1 s): **jitter model** towards the target's last known
   position: the aim keeps the previous aim angles moved towards the desired direction with random errors; the
   error amplitude is E = (1 − profile[3]) × T-b × c × e, where c = 5/N (N = characters within 200 units of the
   owner; 20 when none) and e grows with distance: d < 92: (d/92)²; beyond 512·T-c: 1 + (d − 512·T-c)/256; ×0.4 for
   non-player targets. Yaw errors are U(−1, 1)·0.75·E scaled by 1/8 when the remaining turn is within 30° and by 1/4
   within 60°; pitch errors U(−1, 1)·0.375·E. Actions owned by path, point or scripted decisions get no error.
   UNVERIFIED in detail (the jitter rule is reproduced from a partial reading; verify by watching characters fight
   each other).

### A8. Class rules
- **Class 1 play in place**: completes with (1, 1) at the end time; with *ends with animation*, as soon as the
  animation is done or paused, or at the end time.
- **Class 2 pain**: destination = the owner's position; duration 10 s, ends with animation; two entity-state values
  of the owner (record offsets 60 and 64 of the entity, UNVERIFIED which fields) are zeroed at start. It interrupts
  the current action, which becomes its linked action. It completes with (0, 0) when the animation is done or paused
  or at the end time — so the interrupted action is told r = 0, becomes stopped, and on its next think completes with
  (0, 0): the decision then plans again.
- **Class 3 dying**: on start the same two entity-state values are zeroed and the owner becomes dead if still alive
  (health −1, dead flag 2, solid box, clip mask 0x4000000, server flag "dead monster", no touch).
  `std_sblown_2bk_n_n` on a character with vertical speed < 10 throws it back: velocity −200 × forward, vertical 100,
  box top lowered by 26 and origin raised by 26. Each think: when on the ground with a paused animation, the landing
  flag is added and the animation resumed; events, facing, turning as A2; when the animation is done the body is told
  once (death notification, SPEC-722 "Dying follow-up"); movement (A6); the body's death choice is consulted every
  think (SPEC-722 "Dying follow-up") and its record becomes current; a record that is not a death prints `Death
  action %s not really a death!`. Never completes; the corpse rules decide removal.
- **Class 4 held death pose**: no start step (the owner's state and animation are left as they are); each think as
  class 3. Used for spawnflag 65536 "dies in place" (SPEC-700) and for player corpses.
- **Class 5 special death**: makes the owner dead as class 3 and plays `std_shead_2frnt_n_n` at 1.5 × speed directly
  on the model; silences the body's voice. Each think only the bounding box and the face/voice update run (no
  turning, no movement, no follow-up). Never completes.
- **Class 6 end of script**: restores the sight and hearing senses withheld by spawnflags 2 and 4 (and clears those
  spawnflags), tells its owning scripted decision to finish, completes with (1, 1).
- **Class 7 move**: let *cycle* = the record has flag 0x00400000 and its animation is not yet done.
  - step move failed: completes with (0, 0) unless *cycle*;
  - not ends-with-animation: past the end time → (0, 0) unless *cycle*; otherwise, when the horizontal distance to
    the destination is at most √(9·(|step|² + 1)) (about three steps) and not *cycle* → (1, 1);
  - ends with animation: once the animation is done and (it is paused or the end time has passed) → (0, 0); it never
    completes by arrival.
- **Class 8 animal move**: as class 7; each think the record becomes the turn-left or turn-right record when the
  desired yaw differs from the current one (left for positive differences), else the straight record; a changed
  record is restarted.
- **Class 9 jump** (movement flags 1 and 4): at start the jump impulse is computed from the quantities below, then
  the animation is resumed and forced.

  | quantity | value |
  |---|---|
  | destination D | the action's destination, or 64 units ahead when it equals the owner's position |
  | target height z_t | D.z minus the owner's box bottom offset |
  | horizontal distance x | horizontal distance to D, at most 100 |
  | H | jump height, AI record offset 452 (50) |
  | a | the smaller of 0.1·g and H (g = cvar `sv_gravity`) |
  | v₀, T₀ | √(2·H·g) + a, and v₀ / (0.1·g) |
  | vertical speed vz and flight time T, case H + origin.z − z_t ≤ 0 | v₀ and T₀ |
  | case z_t ≤ origin.z | v₀, and (√(2·(H + origin.z − z_t)·g) + a) / (0.1·g) + T₀ |
  | otherwise | 2·a + √(2·(z_t − origin.z)·g), and vz / (0.1·g) |
  | horizontal impulse | direction to D × x·10 / T |

  Each think while
  airborne the horizontal velocity steers towards the destination (speed (dest − origin)/time left, capped at 4000
  when the squared speed exceeds 1.6·10⁷) while still rising and before the expected landing time. Completion once
  on the ground after the animation is done (or once the expected landing time + 3 s has passed): velocity zeroed;
  (1, —) when within 30 units of the destination, else (0, 1).
- **Class 10 fall**: until the owner has left the ground, each think a step of 20 units towards the destination (or
  forward when none) is tried, then retried with the "may step off" movement flag; once it has left the ground the
  animation pauses in the air (`inair`) and resumes on landing; when on the ground again and the animation is done:
  velocity zeroed; completes with m = r = 1, or 0 when the owner's feet ended more than 10 units above the
  destination.
- **Classes 11, 12 timed**: complete with (1, 1) at the end time.
- **Class 13 attack**: target entity optional; with the lethal flag a target with health > 1 is set to health 1 at
  creation (the next hit kills). Fire events come from `fire` notes or, for aim-capable records, from *ready to
  fire* (A2). Values: m = 1 when this think's step move succeeded; r = 1 while the target is alive and in use. With
  *end-ok* = not ends-with-animation, or the animation is done or paused, the action completes when all hold:
  (the lethal flag is clear, or the target is dead); (the target is dead or freed, or the end time has passed with
  *end-ok*); (no target, or the target is alive, or the animation is done).
- **Class 14 dog leap**: as class 13; the `jump` note starts the leap (state 1), the attack-hit flag sets state 2
  and swings the yaw ±20°; while in the leap the facing point tracks the target's perceived position (box centre).
- **Classes 15–21 vehicle commands**: duration 999 999 s, end with animation. Each think the command's parameters
  (destination, scalar, entity) are copied into the vehicle AI state, a queued routine replaces the current one,
  the routine runs, and the vehicle body's damage state is updated with health/maximum health (helicopter smoke,
  tank/snowcat damage; SPEC-722). The action completes (1, 1) when the routine reports completion. When no routine
  is set the helicopter action selects an autonomous one from the helicopter state (dying → crash routine: with
  spawnflag 2 towards a computed crash point, else a spin with 2 s timing; other state bits select their routines;
  roster spec). Command → routine family (HELI ids, SPEC-750 §12.8): ids 1–10, 22, 23, 33–49 (class 16), 12–21
  (class 17), 24–32 (class 18); ids 6–9 and 12, 16, 19, 28 need a non-zero scalar/entity parameter; 22/23 use
  ±|scalar|; 34–36 set a helicopter parameter to the scalar or to 0/1; 42–45 add/edit flight waypoints; 47 spawns a
  `m_x_chopper_black`/`_green`/`_white` (scalar 1/2/other; model `enemy/chopper`, skin `cobra`); 48 spawns a hind
  (`enemy/hind`) when the scalar is 1. VEHICLE ids 1–5 (class 20/21) select one routine each. Meanings of the
  routines: roster spec.

### A9. Corner peeks and leans
Corner-peek fire records (`std_fcornerpeak_lt_m_2`, `std_fcornerpeak_rt_m_2`, `std_fcornerpeak_lt_p_a`,
`std_fcornerpeak_rt_p_a`) are played only on script request (SPEC-750 ids 65, 66) in 1.07f; no decision chooses them.
Their root motion is handled by the body (SPEC-722: vertical motion zeroed, reversed when done) and the start delay
of lean records (flag 0x00040000) applies.

## Constants
| name | value |
|---|---|
| default record / duration | `std_istand_n_a_n` / per creator (SPEC-710 D3) |
| movement scale / turn allowance without record | 1.0 / 30 |
| velocity estimate | v ← 0.9·v + 0.1·step; zeroed when blocked or |step|² < 1 |
| step move | step height 24 (flag 4) or 2; ground speed limit √121 = 11 units per second; ledge test as SPEC-700 |
| move arrival | horizontal distance² ≤ 9·(|step|² + 1) |
| facing point offset | −20 units vertical |
| pain | duration 10 s, ends with animation |
| dying | throw-back 200 / 100, box −26, origin +26, vertical speed < 10; decal +10 |
| special death | `std_shead_2frnt_n_n`, speed factor 1.5 |
| jump | reach 100, height 50, default target 64 ahead, landing tolerance 30, steering cap 4000 (1.6·10⁷ squared), landing check 3 s |
| fall | look-ahead 20, landing tolerance 10 below destination |
| aim (precise) | close 64; blend 64–128; lateral factor 1/225 × 0.6; airborne 0.6; lateral miss threshold 0.1; hit spread ±16, −24…+40; miss offsets 16–32 (±8 jitter); ducked target −20; yaw clamp 20° / 45° |
| aim (jitter) | distances 92 and 512·T-c, slope 1/256, non-player ×0.4; crowd 5/N (20 alone) within 200; yaw ×0.75 (÷8 within 30°, ÷4 within 60°); pitch ×0.375 |
| landing sound | `impact/gore/fall1.wav`, `fall2.wav`, `fall3.wav`, channel 4 |
| dog leap swing | ±20° |
| vehicle command duration | 999 999 s |

## Test vectors / how to verify
UNVERIFIED in game until run on the original.
1. **Reaction and settle.** `skill 2` (T-d 1.0, T-b 1.0), `m_kos_mgrunt2` (tier 0, weapon class 6: profile[3]
   0.2·0.35 = 0.07, profile[4] 0, profile[5] 6·2 = 12; before the per-leaf adjustment of SPEC-700 rule 1): standing
   still 300 units in front of it, the hit rate per shot is near 0 at first and reaches about 0.07 after 12 s of
   continuous sight. At `skill 4` (row 3: T-b 1.5, T-d 0.5) it reaches 0.105 after 6 s.
2. **Distance falloff.** A target beyond profile[1] (e.g. 512 for tier-0 class 6) at 1024 units is hit with 1/4 of
   the in-range chance.
3. **Moving target.** Strafe at 300 units/s perpendicular to a soldier: L ≈ 1, M = 1 − 0.6·300/225 < 0 → no hits; misses
   land 16–32 units behind your motion.
4. **Jumping.** While airborne all hits become misses below you with probability 1 − 0.6·p'.
5. **Leaning.** Leaning out from cover is aimed at the lean hitbox (shots hit the head area).
6. **Move arrival.** A scripted walk (`ai` move, kind 1) ends within about three steps of the destination; a blocked
   walk ends at its duration and the AI-done signal still fires (SPEC-750).
7. **Pain.** Shooting a walking soldier in the arm plays a hit reaction; afterwards the interrupted walk ends at once
   (it is told r = 0) and the soldier re-plans (it may turn towards you instead of continuing).
8. **Savegame.** Save while a soldier attacks: its current action is an `AIAA` record of 120 bytes; a dying one an
   `AIDA` record of 124 bytes.

## Open questions
1. The exact non-player jitter aim (A7.3).
2. Sources of event flags 0x01, 0x80 and 0x200 in the bodies (SPEC-722 owners).
3. Routine semantics of the vehicle commands (roster spec / SPEC-750 open question 1).
4. Classes 11 and 12 have no creator in 1.07f.
