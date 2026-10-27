# SPEC-701: Character AI perception (senses, sight, hearing, alert sounds)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-700 (character AI framework), SPEC-001 (game module interface), SPEC-006 (game module map),
SPEC-303 (savegame container)

## Purpose
Defines how a character (an entity of an `m_*` classname, SPEC-006) perceives players and other characters:
which senses a character owns, what each sense records, when senses are updated, the sight test (view cone,
range, line of sight, leaning players, darkness), the alert-sound system that feeds hearing, sense muting
(blinding and deafening by flashes and explosions), and how the rest of the AI (SPEC-700 and the decision and
action specs) reads the result. A clean implementation that follows these rules reproduces when characters
notice the player.

Terms used here:
- **character**: an entity with an AI object (SPEC-700). **player**: an entity with a client.
- **now**: game time in seconds. A character thinks every 0.1 s (SPEC-700).
- **eye point**: entity origin plus its view height (integer entity field, as in Quake II).
- **alerted**, **active**, **vision mode**, **perceive-teammates flag**, **team rule**, **enemy**,
  **sense throttle**: per-character AI state defined in SPEC-700. Their roles here are stated where used.
- **range class** of a squared distance d²: 0 (melee) if d² < 6400, 1 (near) if d² < 250000,
  2 (mid) if d² < 2250000, otherwise 3 (far). These are the SPEC-006 range classes (80 / 500 / 1500 units).

## Interface / layout

### Sense kinds
Each character owns an ordered list of senses. Every sense has a class code (stored in savegames), a type mask
value (1, 2 or 4: which update and mute requests it answers to; a request carries a mask and a sense answers
when the masks share a bit; characters always update with mask 7) and two **records**.

| Code | Sense | Mask value | Created by the game | Perceives |
|---:|---|---:|---|---|
| 0 | inert base sense | all | never (fallback when a savegame names an unknown code) | nothing; removed at its first update |
| 1 | inert legacy sense | 1 | never at run time | nothing; removed at its first update (the character's update mask is always 7, which contains 1) |
| 2 | sight (legacy variant) | 2 | never at run time | identical to code 3 |
| 3 | sight | 2 | yes | players and characters in view |
| 4 | hearing | 4 | yes | alert sounds (below) |
| 5 | omniscience | 2 | when cvar `ai_pathtest` ≠ 0, and always for `m_x_mraiderboss2` | every valid player and character, no tests |
| 6 | proximity | 2 | yes | players within the character's alert radius, no line-of-sight test |

An unknown code met while loading prints `ERROR: invalid sense class code: <code>` and creates code 0.

### Records
Each sense keeps a **player record** (last player perceived) and a **character record** (last non-player
character perceived). A record holds: entity (or none), position (3 floats), time (float), kind (int).
Initial value: no entity, position (0,0,0), time −10000, kind 0.

| Kind | Meaning | Written by |
|---:|---|---|
| 2 | noise heard (gunfire, impacts, gore, spawner noise) | hearing |
| 4 | directed sound (supported, no 1.07f source emits it) | hearing |
| 5 | told: the character was triggered by an activator | hearing |
| 6 | death cry of a character | hearing |
| 7 | another character's alert call | hearing |
| 8 | seen this think | sight |
| 9 | tracked: seen recently, position refreshed without a new sighting | sight |
| 10 | omniscient | omniscience |

Kinds 1 and 3 are accepted by hearing (treated like 2 and like the "other kinds" respectively) but nothing in
1.07f emits them. The proximity sense updates entity, position and time but leaves the kind unchanged.

### Entity keys, flags and cvars that affect perception
| Item | Effect |
|---|---|
| `m_*` spawnflag 2 | no sight sense until the character is triggered or alerted ("cantsee") |
| `m_*` spawnflag 4 | no hearing sense until the character is triggered or alerted ("canthear") |
| `m_*` spawnflag 32 | unobservant: narrower view cone and target speed ignored (see sight); cleared when alerted |
| `m_*` spawnflag 16 | not a candidate for alert sounds (see hearing) |
| `m_*` spawnflag 16384 | invisible to characters created by spawners |
| key `alertradius` (float) | proximity radius; values below 0.1 become 75 |
| entity flag "notarget" (Quake II FL_NOTARGET, cheat `ninja`, SPEC-006) | the entity is never perceived and its sounds alert nobody |
| server flag "in safe zone" (set while touching `trigger_safezone`, cleared every client think) | the player is unseen by unalerted characters beyond melee range |
| player light level (0–255, the light value the client reports for its position) | sight perception roll (below) |
| classname `m_x_mmerc` | never reported by sight |
| `trigger_sound` key `health` | hearing radius of the trigger (default 128) |
| cvar `ai_pathtest` (default 0) | non-zero: every character also gets the omniscience sense |
| cvar `ai_highpriority` (default 0) | non-zero: no character is sense-throttled |
| cvar `ai_dumb` (default 0) | non-zero: characters with spawnflag 2 show the effect `cantsee`, with spawnflag 4 `canthear` (debug) |

### Savegame chunks (only needed for savegame compatibility with the original)
Each sense is written as one chunk (SPEC-303 container) inside the AI part of a level save; the AI object table
(SPEC-700) stores the class code that selects the reader. Entity references are stored as entity numbers
(0 = none); the first 4 bytes are not meaningful and are ignored when reading.

| Codes | Chunk id | Size |
|---|---|---:|
| 1 | `AIAS` | 88 |
| 2 | `AISS` | 88 |
| 3, 5 | `AINS` | 88 |
| 6 | `AINS` | 92 |
| 4 | `AISO` | 448 |

| Offset | Size | Field (all sense kinds) |
|---:|---:|---|
| 0 | 4 | ignored |
| 4 | 12 | three int32, always 0 |
| 16 | 4 | float creation time |
| 20 | 24 | character record: int32 entity number, float[3] position, float time, int32 kind |
| 44 | 24 | player record, same layout |
| 68 | 4 | int32 mute recovery mode |
| 72 | 4 | float mute start time |
| 76 | 4 | float mute end time |
| 80 | 4 | float mute level |

| Offset | Size | Field (codes 1, 2, 3, 5, 6) |
|---:|---:|---|
| 84 | 4 | float time of the last successful perception roll (sight; 0 at creation) |
| 88 | 4 | float squared proximity radius (code 6 only) |

| Offset | Size | Field (code 4, hearing) |
|---:|---:|---|
| 84 | 360 | 10 queued sound events × 36 bytes: float[3] sound position, float[3] emitter origin when emitted, int32 emitter entity number, int32 kind, float time |
| 444 | 4 | int32 number of queued events |

## Behaviour

### 1. Senses a character starts with
When a character finishes its start-up (the Quake II `monster_start` equivalent, SPEC-700), senses are added in
this order:
1. hearing, unless spawnflag 4 is set;
2. sight (code 3), unless spawnflag 2 is set;
3. proximity with radius `alertradius` (75 if the key is missing or below 0.1);
4. omniscience, if cvar `ai_pathtest` is non-zero.
`m_x_mraiderboss2` (model `dekker2`) additionally always gets an omniscience sense.

**Deferred senses.** A sense withheld by spawnflag 2 or 4 is added later, and the spawnflag cleared, when:
- the character is triggered (its `use` behaviour, e.g. by a `target` or a script) — except an inactive
  character with spawnflag 16, which is only activated by the trigger (SPEC-700), or
- the character becomes alerted (SPEC-700), except for `m_x_mraiderboss2` and `m_x_mskinboss`, which stay
  blind/deaf until triggered.
Becoming alerted also clears spawnflag 32 (same two exceptions).

When a character is triggered by an activator entity (and the spawnflag-16 exception does not apply) it is
alerted with the activator as source and also receives a kind-5 hearing event at the activator's origin (the
activator becomes known at once, see §6).

### 2. Sense update scheduling
- Senses are updated during the character's think, before its decisions run (SPEC-700), every think (0.1 s)
  unless the character is **throttled**.
- A throttled character updates its senses only when now − 2 ≥ T, where T is the integer part of
  (time of its previous sense update + 1); in practice every 2 to 3 seconds.
- Characters start throttled. Every 0.5 s the level-wide AI manager re-evaluates all active, living characters:
  a character whose origin is in the potentially visible set of the current sight client (§3) is unthrottled;
  the others are throttled (unthrottled when cvar `ai_highpriority` ≠ 0). Then, if fewer than 7 characters are
  in that visible set, the nearest out-of-sight characters are unthrottled until 7 characters are unthrottled
  in total. Nearness for this rule uses dx² + dy² + 9·dz² to the sight client.
- One update of one sense: the sense processes its input (§4–§7); afterwards, if the sense now has a player in
  its player record, the character's body is queried once (a pure query whose result is discarded, so it has no
  observable effect; SPEC-700). (UNVERIFIED detail: the original reads that record through a temporary it does not
  initialise.) A sense whose update reports "remove me" (codes 0 and 1) is deleted from the list.

### 3. What sight looks at each frame
Once per server frame, before characters think, the game picks:
- the **sight client**: the next in-use player slot after the previous sight client (wrapping 1 … maxclients);
  none if no player slot is in use;
- the **sight character**: the next entity after the player slots (wrapping) that is in use, has health > 0,
  is not notarget, has an AI and is active; none if there is no such entity.
Every sight, omniscience and proximity sense evaluates exactly these two candidates on each update. With one
player, the player is examined every update; another character is examined once per full rotation over all
eligible characters.

### 4. Perceived distance and muting
- Each sense has a **mute level** L ≥ 0 that inflates distances: a sense judges distance by the range class of
  d² · (1 + L)². A target or sound whose perceived range class is 3 (far) is ignored, so the effective
  perception range is 1500 / (1 + L) units.
- Muting is applied by a request (mask, level, recovery mode, duration). Only senses whose mask bit matches
  respond: sight senses (codes 2, 3) record start = now, end = now + duration; hearing records start = now + 0.3,
  end = now + duration + 0.3 (deafness begins 0.3 s late, so the sound that caused it is still heard).
  Omniscience, proximity and the inert senses ignore muting.
- Mute level at time t, for recovery mode m:
  - m = 0: the stored level, always;
  - m ≠ 0 and not (start < t < end): 0;
  - m = 1: the stored level; m = 2: level · (end − t)/(end − start); m = 3: level · ((end − t)/(end − start))²;
  - any other m: prints `unknown sense muting recovery code: <m>!` and uses the stored level.
- A new mute request replaces the previous one. A new sense starts with mode 1, level 0 (no effect).

Sources of muting in 1.07f (all use mode 2, linear recovery, and duration in seconds equal to the level):

| Source | Senses | Level |
|---|---|---|
| explosion with power P and radius R centred on an entity | hearing | (1 − dist/R) · P, distance between entity centres (box midpoints), capped at 100; characters with health ≤ 0 or level ≤ 0 skipped |
| flash with radius R | sight | (f · d + 1.25) · (R/2) / dist, capped at 25; f·d = cosine between the character's facing (§5) and the direction to the flash; only characters with an unobstructed line (shot content mask) from the flash to their eye point |
| burning / electric shock effects, slug-grenade blast (radius 210) | sight | 25 |

Explosion (P, R) pairs used by the original's sources: (200, 200), (150, 200), (300, 300), (130, 250),
(150, 250), and entity-defined values for `environ_explosion`, `func_breakable_brush` and `target_explosion`
(UNVERIFIED which weapon uses which pair; verify in the weapon specs). Flash radii used: 10000, 2500, 2000.

### 5. Sight (codes 2 and 3)
For each candidate target (§3) the sight sense applies these rules in order:
1. Ignored if: no target, target not in use, target is the character itself; a player with notarget; a
   player on the character's own team by the team rule unless the character's perceive-teammates flag is set
   (default off); a non-player without an AI; a target with spawnflag 16384 when the character was created by a
   spawner; a target of classname `m_x_mmerc`; a target whose perceived range class is 3.
2. **In view** if the perceived range class is 0, or else if both:
   - not (the target is in a safe zone and the character is not alerted), and
   - the target is inside the **view cone**: cos(angle between the character's facing and the direction to
     the target origin) > threshold. Facing = the character's look angles (SPEC-700). Threshold:

     | Vision mode | 0 (narrow) | 1 (normal, initial) | 2 (wide, set when the character becomes active) | none (no AI) |
     |---|---:|---:|---:|---:|
     | threshold | 0.85 | 0.75 | −0.2 | 0.75 |

     Target speed s² (character targets: the AI's own velocity estimate, players: entity velocity): with
     spawnflag 32 on the observer, s² is multiplied by 0.01 and the threshold raised by 0.225; then the
     threshold drops by 0.25 if s² > 10000 and by another 0.25 if s² > 40000 (moving targets are noticed
     further off-axis).
3. **Not in view**: if the player record holds this target and was written less than 4 s ago, its position is
   refreshed to the target's origin and its kind set to 9 (time unchanged); otherwise the same for the
   character record. Nothing else happens.
4. **In view — perception roll** (decides whether a leaning player's head can be spotted, rule 5):
   - if a roll succeeded within the last 4 s: success;
   - otherwise p = B · (light/128 · 0.75 + 0.25), times 0.1 if the character is not alerted, where light is the
     target's light level (always 0 for characters) and B depends on the squared distance d²:
     B = 2.5 if d² < 192; B = (1 − (d² − 36864)/225280) · 0.8 · 2.5 + 0.5 if 192 ≤ d² ≤ 512; B = 0.5 above.
     (The original compares the squared distance with 192 and 512; beyond about 22.6 units B is 0.5.)
     The roll succeeds if a uniform random number in [0, 1) is below p; success stores now as the roll time;
   - then, for a freshly rolled (not remembered) result only: an unalerted character fails against a target
     whose light level is below 64. The roll time stored just before still counts, so the remembered success
     applies on the following updates for up to 4 s.
5. **Line of sight** (Quake II `visible` with an extension): the target is visible if the two eye points are in
   each other's potentially visible set and a zero-size trace between them (content mask 0x19, as Quake II
   MASK_OPAQUE, ignoring the observer) is unobstructed. If that fails and the roll succeeded and the target is a
   player who is leaning, a second trace goes to a point 16 units above the player's lean point (the position
   the leaning head occupies, maintained by the player view code); unobstructed means visible.
6. **Visible**: a player target writes the player record {target, target origin, now, kind 8}; when the player
   is leaning, the recorded x and y are replaced by the lean point's x and y. A character target writes the
   character record {target, origin, now, kind 8}.
7. **Not visible**: if the player record holds this target and was written less than 0.25 s ago, refresh its
   position and set kind 9 (time unchanged); otherwise the same for the character record.

Consequences an implementation must reproduce: an unobstructed, non-leaning player inside the cone and closer
than 1500 units (perceived) is seen on the next sight update regardless of light or alertness; darkness and
the roll only matter for spotting a leaning head; a target that leaves the cone keeps being tracked for 4 s, one
that ducks behind cover for 0.25 s.

### 6. Hearing (code 4)
**Alert sounds.** Game events broadcast an alert sound with: emitter entity, sound position, kind, optional
target entity, radius, "needs sight" flag and "tell decisions" flag. Broadcasting:
1. The bots' own hearing is informed first (bot spec).
2. Nothing else happens in deathmatch, or if the emitter has notarget.
3. Kind 4: only the target entity's character receives the event.
4. Otherwise the candidates are the characters in the level-wide AI list (§6a) whose origin is within the radius
   of the sound position (plain 3-D distance) and who are not dead. With "needs sight", a candidate receives
   the event only if the target entity (or the emitter when no target is given) is inside its view cone (§5
   rule 2 cone test) and visible by the plain eye-to-eye test (§5 rule 5 without the lean extension).
5. Each receiving character gets the event {sound position, emitter, emitter's current origin, kind, now} in its
   hearing sense's queue (at most 10 queued; further events before the next update are dropped). With "tell
   decisions", its decisions are also given the emitter's origin and the emitter (decision specs).
6. Without "needs sight", every `trigger_sound` within its radius (key `health`, default 128) of the sound
   position fires its targets once and is removed. At most 16 `trigger_sound` entities are tracked.

**6a. Alert candidates.** Every server frame the AI manager rebuilds a list of at most 64 characters: in use,
health > 0, spawnflag 16 clear, and either active or within dx² + dy² + 16·dz² < 1048576 of the sight client
(1024 units, vertical distance weighted 4×). Only listed characters can hear alert sounds.

**Alert sounds emitted in 1.07f:**

| Event | Emitter | Position | Kind | Target | Radius | Needs sight | Tell decisions |
|---|---|---|---:|---|---:|---|---|
| player fires a weapon (primary or alternate) | shooter | firing point | 2 | — | weapon's noise radius (player module) | no | no |
| bullet / projectile impact | shooter (UNVERIFIED) | impact point | 2 | — | 60 or 128 by weapon | for one impact type: when the damage is flagged silent | no |
| body gore impact | the body | its origin | 2 | — | 300 | no | no |
| a character dies | the killer | victim origin | 6 | victim | 600 | when the killing damage is flagged silent | yes |
| a character is alerted for the first time, the alert position is within 1000 units of it, and its call-for-help flag is set (SPEC-700; cleared by spawnflag 8192 or a `point_combat` start) | the alert source (enemy) | the character's own origin | 7 | — | 500 | no | no |
| level enemy spawner, periodic noise (UNVERIFIED trigger) | sight client | sight client's origin | 2 | — | 800 | no | no |
| enemy spawner with spawnflag 4 or 128 places a character | sight client | the placed character's position (UNVERIFIED) | 7 | — | 90 | no | no |

"Silent" is the damage flag 0x1000000 (damage spec); a silenced kill therefore alerts only characters that can
see the victim.

**Processing on update** (each queued event, in order; the queue is emptied afterwards):
1. Skip if the emitter now has notarget.
2. Kind 5: write the record matching the emitter (player record for a player emitter, character record
   otherwise) = {emitter, emitter origin from the event, now, 5}. No other test.
3. Otherwise skip if the sound position's perceived range class (with the hearing mute level) is 3, if there is
   no emitter, or if the kind is 7 and the sound is closer than 20 units to the listener (a character ignores
   its own alert call).
4. Player emitter: skip if the player record holds a different entity and is less than or exactly 10 s old.
   Then kinds 1 and 2 write {emitter, emitter origin, now − 5, kind}; other kinds are skipped if the record
   holds any entity and is at most 20 s old, else write {emitter, emitter origin, now − 10, kind}.
5. Character emitter: skip if the character record holds any entity and is at most 10 s old. Then as for
   players (now − 5 for kinds 1 and 2, the 20 s rule and now − 10 for other kinds), except that for kinds 1 and
   2 the kind value is stored into the player record's kind instead of the character record's (original
   behaviour; the character record keeps its old kind).
Heard positions are the emitter's origin at the moment of the sound, not the sound position; heard times are
back-dated by 5 or 10 s so that any sighting (time now) is newer.

### 7. Proximity (code 6) and omniscience (code 5)
- Proximity: a candidate player (§3) that is in use, not the character, not notarget and not excluded by the
  team rule (as in §5 rule 1) and whose squared distance to the character is ≤ radius² updates the player
  record's entity, position (player origin) and time (now); its kind is not changed. Characters are ignored.
  No view cone and no line of sight.
- Omniscience: a candidate passing the §5 rule 1 notarget/team/AI filters is recorded with kind 10 and time now
  (player record for players, character record for characters), with no other test.

### 8. What the rest of the AI reads
- **Newest player / newest character**: start from {no entity, time −1·10⁸}; for every sense in list order that
  answers the requested mask (−1 = all), take its record if its time is ≥ the current best. The result is the
  most recent perception; equal times favour the later sense.
- When the newest player record has kind 7, the character's "recently called" time (SPEC-700) is set to the
  record time + 12 and that sense's player-record kind becomes 6, so the effect happens once.
- **Enemy position**: if the character has an enemy (SPEC-700) and the newest record of the matching type
  (player or character) holds that enemy, the record's position is the enemy's last known position; otherwise
  the character's own origin is returned with "unknown".
- After each pass over its senses the character also schedules a reaction moment: if the newest player record
  holds an entity with health > 0 and is older than 1 s, a time now + uniform(−0.2, 0.2) s is stored for the
  decisions (SPEC-700; meaning UNVERIFIED).
- **Danger notices** (not a sense): explosives (radius 160, kind 2) and character deaths not on the sight
  client's team (radius 128, kind 1) tell every listed character within the radius (§6a) the danger kind and
  position; their decisions react (decision spec).
- **Most muted**: the highest current mute level over the senses matching a mask (used by the body/decision
  code, e.g. to play a stunned reaction).
- Perception is not scaled by the skill cvars; no skill value enters any rule in this spec.

## Constants
| Name | Value |
|---|---|
| range class limits (squared) | 6400, 250000, 2250000 (80, 500, 1500 units) |
| perceived distance factor | (1 + mute level)² on d² |
| view cone cosine: narrow / normal / wide / no AI | 0.85 / 0.75 / −0.2 / 0.75 |
| unobservant (spawnflag 32): speed² factor, threshold increase | 0.01, +0.225 |
| moving-target threshold decrease | −0.25 if speed² > 10000, another −0.25 if > 40000 |
| tracking window, out of cone | 4 s |
| tracking window, behind cover | 0.25 s |
| perception roll memory | 4 s |
| roll base B | 2.5 / interpolation over d² 192…512 using 36864 and 225280 / 0.5 |
| light factor | light/128 · 0.75 + 0.25; unalerted × 0.1; unalerted and light < 64: never |
| lean-point trace height | +16 units |
| sight / LOS trace content mask | 0x19 |
| hearing queue | 10 events × 36 bytes |
| own-call ignore distance | 20 units (400 squared) |
| hearing overwrite windows | 10 s (any), 20 s (kinds other than 1, 2) |
| hearing back-dating | 5 s (kinds 1, 2), 10 s (others) |
| hearing mute delay | 0.3 s |
| deafness cap / blindness cap | 100 / 25 |
| flash formula constants | 1.25, 0.5 |
| default alert radius | 75 (keys below 0.1) |
| `trigger_sound` default radius / max tracked | 128 / 16 |
| AI list size / near-player radius | 64 / 1024 units, dz weighted 4× (16 on squares) |
| throttle re-evaluation period / unthrottled minimum | 0.5 s / 7 characters |
| throttled sense period | 2–3 s (integer-second rule) |
| throttle nearness metric | dx² + dy² + 9·dz² |
| initial record time / query start time | −10000 / −1·10⁸ |
| "recently called" offset | +12 s |
| reaction jitter | ±0.2 s, records older than 1 s |
| alert-sound radii | weapon-defined, 60, 128, 300, 500, 600, 800, 90 |
| alerted-by-source distance limit | 1000 units (squared 10⁶) |
| danger notice radii | 160 (explosive, kind 2), 128 (death, kind 1) |

## Test vectors / how to verify
The in-game checks are predictions from the analysis and are UNVERIFIED until run on the original; how the
character reacts once a perception is recorded (turning, shouting, attacking) is governed by the decision specs.

1. **Cone.** Test map with an `m_*` grunt facing +x, no spawnflags. Stay farther than 800 units so it does not
   become active (SPEC-700) and keeps the normal cone. Stand still 1000 units away at 35° off its facing: it
   notices you (cos 35° = 0.82 > 0.75); at 45° (0.71) it does not until you move faster than 100 u/s
   (threshold 0.5) — run past at 45°: noticed. With spawnflag 64 (active at once, wide cone) it notices you at
   90° off-axis. UNVERIFIED in game; verify by watching the character turn to the player.
2. **Range.** Same setup, clear corridor 1600 units long, player straight ahead: not noticed at 1550 units,
   noticed at 1450.
3. **Darkness only matters when leaning.** In a dark room (light < 64) facing an unalerted character with a clear
   line: noticed immediately. Hide behind a corner and lean so only the head shows: never noticed while
   unalerted; once alerted, spotted with probability per think ≈ 0.5 · (light/128 · 0.75 + 0.25).
4. **Tracking.** Alert a character, step out of its cone: its last known position follows you for 4 s
   (it keeps turning toward you), then freezes.
5. **Deaf/blind spawnflags.** Place `m_*` with spawnflags 6 and set `ai_dumb 1`: the effects `cantsee` and
   `canthear` play on it; it ignores you until triggered (e.g. by a `trigger_once` targeting it).
6. **Alert radius.** `alertradius 200` on a blind and deaf character (spawnflags 6): it reacts when you come
   within 200 units even from behind; with the default it reacts at 75 units.
7. **Gunfire.** Fire an unsilenced weapon 400 units from an unalerted character outside its view, inside the
   weapon's noise radius: it turns toward the position you fired from. With a silenced weapon kill a second
   character that the first one cannot see: no reaction.
8. **Flash.** A flash grenade going off in front of a character at distance d blinds it: its sight range becomes
   1500/(1+L) units and recovers linearly over L seconds (L from §4).
9. **Explosion deafness.** A blast with (P, R) = (300, 300) whose centre is 200 units from a character's centre
   gives L = (1 − 200/300) · 300 = 100 (the cap): the character still hears the blast itself (0.3 s delay), then
   hears nothing farther than 1500/101 ≈ 15 units; the limit grows back linearly over 100 s
   (after 50 s: L = 50, about 29 units).
10. **Savegame.** Save next to an alerted character and reload: its senses come back from chunks `AISO`, `AINS`
    with the sizes in the layout table (container per SPEC-303).

## Open questions
1. The exact meaning of the AI fields this spec only names (vision mode 3, "recently called" time, reaction
   moment) belongs to SPEC-700 and the decision spec.
2. Which weapon passes which noise radius and which (power, radius) pair to explosion deafening — weapon specs
   (player module noise radius for gunfire).
3. Whether the impact alert's emitter is always the shooter (UNVERIFIED; check by shooting a wall near a
   character from far away and seeing where it looks).
4. The swap of the kind field for character emitters (§6 rule 5) is reproduced as observed; it has no visible
   effect unless a decision reads the player record's kind.
5. When no entity qualifies as sight character the original clears it; an implementation must also handle the
   case of a level with no characters (the original's search loop terminates after one full rotation).
