# SPEC-771: Server-side effects — environ_* entities, ambient sound sets and music, effect emission rules
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (server-side game module), version 1.07f
Depends on: SPEC-006 (classname list), SPEC-112 (ambient set file `.ams`), SPEC-111 (music set), SPEC-142 (effect
files), SPEC-201 (messages: temp entities, svc_effect), SPEC-202 (entity state, events), SPEC-203 (configstrings),
SPEC-400/401 (client drawing of effects and temp entities), SPEC-720 (damage, means of death), SPEC-760 (world core,
sound notation), SPEC-761 (shared world helpers), SPEC-770 (props)

## Purpose
What the game module sends to clients to make effects happen, and when. It covers (A) the 27 `environ_*` effect
entities, (B) the ambient sound set and music entities (`func_ambientset`, `func_musicControl`, `environ_soundgen`),
(C) the rules of every server-side effect helper — which message it sends (temp entity number or svc_effect form),
which parameters it fills, to whom it is multicast, and which game events use it — and (D) `target_temp_entity` and
the two hit-scan attack helpers. Message byte layouts are those of SPEC-201 and are not repeated; how the client
draws each effect is in SPEC-400/401.

## Interface / layout
Classnames covered (all spawned through the classname tables of SPEC-006):

| Group | Classnames |
|---|---|
| environment effects (section A) | `environ_bloodrain`, `environ_chunk_spewer`, `environ_cloudtrigger`, `environ_dustsource`, `environ_effect`, `environ_effect_continual`, `environ_emergency_lights`, `environ_explosion`, `environ_fire`, `environ_inferno`, `environ_invisible_attack`, `environ_lightning`, `environ_linefountain`, `environ_linetrap`, `environ_player_snow`, `environ_rain_worldtrigger`, `environ_raingen`, `environ_skyfade`, `environ_smoke_burst`, `environ_smokegen`, `environ_snow`, `environ_sparkgen`, `environ_splashfountain`, `environ_steamgen`, `environ_trainarm_spawn`, `environ_waterglobgen` |
| ambient sound and music (section B) | `environ_soundgen`, `func_ambientset`, `func_musicControl` |
| other (section D) | `target_temp_entity` |

Interface values used: temp-entity numbers and svc_effect forms (SPEC-201 §1, §5), player-state fields 28 (music
value) and 29 (ambient-set value) (SPEC-202), entity-state effect slots, effects bits 0x0001 / 0x0100 / 0x0800 /
0x8000 and events (SPEC-202), configstrings 5 (light-style-13 colour) and 1159 + n (light styles) (SPEC-203), the
ambient set file `sound/sound.ams` (SPEC-112), cvar `lock_blood` (SPEC-721).

Usage: of these classnames, `environ_cloudtrigger`, `environ_emergency_lights`, `environ_lightning`,
`environ_player_snow`, `environ_rain_worldtrigger`, `environ_raingen`, `environ_skyfade`, `environ_snow`,
`environ_splashfountain`, `environ_trainarm_spawn`, `func_musicControl` and `target_temp_entity` are not placed in any of the 76 `.bsp`
files of the 1.07f archives `pak0`, `pak2`, `pak3`; all others are.

Amendments to approved specs made by this spec:
- SPEC-760 §B5.1 (mover sounds): a sound set with fewer than three listed waves does not give "no sound" for the
  missing entries; they read as wave 0 of the file (section B.2). An unset or unknown set name still gives no sound.
- SPEC-203 configstring 5: written by `environ_emergency_lights` and `environ_skyfade` (section A).

## Behaviour

### A. Environment effect entities (`environ_*`)

#### A.0 Terms used in this section

- **Standard environ setup.** Every class below (except `environ_inferno` and `environ_chunk_spewer`, which differ
  as stated) is spawned as an invisible point entity: movetype NONE; solid NOT (a solid value of 1 that is already
  set is kept); clip mask 0x02030003; box (−5, −5, −7)–(5, 5, 12), rotated by the right-angle box rotation of
  SPEC-770 §3 step 5; model index 0; linked. The same setup is also used by `trigger_sound` (SPEC-761) and by a
  temporary entity of the microwave-gun burst (SPEC-782).
- **Effect slot.** "Starts effect `name` in a slot (scale s)" = the effect's configstring index is written into the
  first empty one of the entity's four effect slots (SPEC-202: id, bolt, scale × 16 truncated to 16 bits) with bolt
  0 (= follow the entity origin and angles) and the given scale; scale 0 = the effect's default size. When all four
  slots are in use nothing happens. "Stops effect `name`" = the first slot holding that effect id is emptied. The
  client restarts a slot effect on its own (SPEC-400 "Entity effect slots"); the server sends nothing else. While a
  slot is filled the entity is a packet-entity candidate even though it has no model (SPEC-202).
- **Toggle use** (for slot effects): if the entity's slot holds the effect it is stopped, otherwise it is started
  again with the same scale as at spawn.
- **One-shot effect** = svc_effect at a position (SPEC-201 §5, flags 0x01), multicast PHS unreliable, nobody
  excluded; optional extension fields named per class (direction = ext 0x08, second position = ext 0x04).
- Light-style-13 colour = configstring 5, written as `"%f %f %f"` (RGB, 0–1); the client uses it as the colour of
  light style 13 (SPEC-203; this section supplies the "set by a game entity" of that row).
- Keys are the entity keys of SPEC-760 §4: `wait`, `delay`, `random`, `count`, `health`, `dmg`, `soundname`
  (entity fields) and `height`, `lip`, `color`, `endpoint` (spawn-time). "rand(a, b)" = uniform float in [a, b];
  "irand(a, b)" = uniform integer in [a, b].
- The class `environ_soundgen` belongs to section B.

#### A.1 Family table

| classname | keys read (defaults) | spawnflags | effect / TE | sounds | notes |
|---|---|---|---|---|---|
| `environ_snow` | — | — | slot `environ/snowchute` (scale 0) | — | always on; no use. Effect file absent from 1.07f paks |
| `environ_player_snow` | `count` | — | slot `environ/playersnow` on the **activator** | — | use: `count` ≠ 0 starts, 0 stops. Effect file absent from 1.07f paks |
| `environ_steamgen` | `height` (35), `dmg` (0), `wait` (0), `delay` (0), `angles` | 1 start off | slot `environ/normalsteam` (scale trunc(height/35)); cone damage | — | pulsing on/off timer |
| `environ_smokegen` | `height` (none) | 1 start off | slot `environ/normalsmoke` (scale 60/height) | — | toggle use |
| `environ_sparkgen` | `lip` = spark style (0), `wait` (0), `delay` (0), `angles` | 1 start off, 2 one burst per use | TE 0 / 10 / 14 | (client spark sound) | random interval |
| `environ_waterglobgen` | brush `model` | — | slot `environ/waterdrip` (scale 0) | — | origin moved to top centre of the brush |
| `environ_raingen` | brush `model` | — | none | — | inert marker (origin moved to the brush centre) |
| `environ_rain_worldtrigger` | — | — | none | — | use does nothing visible |
| `environ_cloudtrigger` | `wait` (0.1), `count` (10) | — | none | — | use does nothing visible |
| `environ_bloodrain` | — | — | slot `environ/bloodrain` (scale 0) | — | always on; no use; blood lock: SPEC-721 |
| `environ_linefountain` | `count` (0) | 1 start off | slot `environ/stream` (scale trunc(count/50)) | — | toggle use |
| `environ_splashfountain` | `count`, `angles` | — | none | — | inert |
| `environ_lightning` | `random` (10) | 1 start off | TE 16 (global flash) | — | toggle use |
| `environ_emergency_lights` | — | — | light-style-13 colour | — | use starts the cycle |
| `environ_skyfade` | `color` (0 0 0), `endpoint` (0 0 0), `delay` | 1 disabled | light-style-13 colour | — | no use; removed when done |
| `environ_explosion` | `health` (150) | 1 no flash, 2 no damage | one-shot `weapons/world/airexplodemute`; TE 19 | `impact/explosion/exp1.wav`…`exp3.wav` | repeatable on every use; precaches sound list 58 |
| `environ_smoke_burst` | `health` (50) | — | TE 26 | — | on use |
| `environ_fire` | `health` = size (12), `count` (64), `delay` (0) | 1 start off, 2 silent | slot by size (A.3) | loop `Ambient/Gen/FireFX/Fire{Small,Med,Big}.wav` | toggle use |
| `environ_linetrap` | `angles`, `dmg` (2 with spawnflag 4, else 0) | 1 fire once, 2 start off, 4 hurts players | one-shot `environ/tripbeam` every 0.1 s | — | toggle use |
| `environ_inferno` | brush `model` | — | none (precaches `environ/onfireburst`) | — | trigger brush with no behaviour |
| `environ_invisible_attack` | `count` = attack id (3), `delay` (0.2), `random` (5), `wait`, `target` | 1 start off, 2 no damage | as the attack (SPEC-782) | as the attack | `wait` −1 = one "mouth shot" |
| `environ_trainarm_spawn` | — | — | — | — | use spawns a fast moving arm (A.3) |
| `environ_dustsource` | `health` (5), `delay` (10) | 1 ceiling impact instead of dust | TE 52, or TE 6/7 | — | automatic + use |
| `environ_effect` | `soundname` = effect name, `angles` | — | one-shot `<soundname>` with direction | — | on use; removed at spawn if the name contains `gore` and blood is locked |
| `environ_effect_continual` | `soundname` = effect name | 1 start off | slot `<soundname>` (scale 0) | — | toggle use |
| `environ_chunk_spewer` | `target` (required), `count` (required, ≠ 0) | 1 wait for use | TE 18, chunk type 32 | (client debris sound) | aims at its target |

#### A.2 Behaviour per class

**`environ_snow`.** Standard environ setup, then starts `environ/snowchute` in a slot (scale 0). Never changes.

**`environ_player_snow`.** Standard environ setup. Use: if `count` ≠ 0, starts `environ/playersnow` (bolt 0,
scale 0) in a slot **of the activator** (normally the player whose trigger fired it), so the snow follows that
player; if `count` = 0, stops that effect on the activator. Two entities (one with `count` 1, one with 0) form an
on/off pair. Using the "on" entity twice fills a second slot of the player with the same effect.

**`environ_steamgen`.**
- Spawn: standard environ setup; H = key `height` (0 → 35); scale = trunc(H / 35) (integer division; H < 35 →
  scale 0 = default); the cone axis = forward vector of `angles` (stored at spawn). Unless spawnflag 1, the steam
  effect starts immediately. Toggle timer T: `wait` = 0 → T = 0 (no pulsing), else T = now + `wait`; spawnflag 1
  forces T = 0. The entity thinks every 0.1 s.
- Think (every 0.1 s): if the steam is on and `dmg` ≠ 0: cone area damage (SPEC-720 §14.3) with inflictor =
  attacker = ignored entity = the steam generator, D = `dmg`, R = H × 96 / 35 (96 units for the default), means of
  death 0, cone axis as above, minimum cosine 0.7. Then, if T ≠ 0 and T < now: if on → stop the steam and T = now +
  `delay` (T = 0, i.e. stay off, when `delay` = 0); if off → start it and T = now + `wait`.
- Use: if on → stop, T = 0; if off → start, T = now + `wait`. (Quirk: with `wait` = 0 a steam generator switched on
  by use is switched off again by the next think, i.e. it puffs for one frame; one that starts on at spawn with
  `wait` 0 stays on. UNVERIFIED in game.)
- Damage therefore repeats every 0.1 s for anything in the cone while the steam is on.

**`environ_smokegen`.** Standard environ setup; H = key `height` (no default); unless spawnflag 1 it starts
`environ/normalsmoke` with scale 60 / H (float; H = 0 gives an infinite scale whose 16-bit stored value is 0 =
default). Use: toggle.

**`environ_sparkgen`.**
- Spawn: standard environ setup; spark style = key `lip`. Next burst time: now + `delay` + rand(0, 1) × `wait` −
  `wait`/2, i.e. `delay` + rand(−`wait`/2, +`wait`/2) from now — unless spawnflag 1 or 2 is set (then idle).
- Burst: sparks at the origin along the forward vector of `angles`, multicast PHS: style 0 → TE 0 (white sparks),
  1 → TE 10 (orange/blue sparks), 2 → TE 14 (sparks). Any other style writes the message without a temp-entity
  type byte (malformed; do not use). With spawnflag 2 nothing further is scheduled; otherwise the next burst is
  scheduled with the same formula.
- Use: schedules a burst with the same formula (it does not switch a running generator off). So spawnflag 1 =
  "starts when first used, then repeats", spawnflag 2 = "one burst per use".

**`environ_waterglobgen`.** Standard environ setup, then the brush model of the entity is set (not drawn: model
index stays 0); origin x, y = centre of the brush's absolute box, origin z = the box top. Starts
`environ/waterdrip` (scale 0). No use.

**`environ_raingen`.** Standard environ setup, brush model set (not drawn), origin = centre of the brush box.
Nothing else: no effect, no use (UNVERIFIED whether the client derives rain from the map instead).

**`environ_rain_worldtrigger`, `environ_cloudtrigger`.** Standard environ setup. `environ_cloudtrigger` stores
`wait` (0 → 0.1) and `count` (0 → 10). Use (both classes): walks the list of `environ_raingen` entities and changes
nothing — no visible or network effect in 1.07f.

**`environ_bloodrain`.** Standard environ setup; starts `environ/bloodrain` (scale 0). No use. (Suppression under
the blood lock: SPEC-721.)

**`environ_linefountain`.** Standard environ setup; scale = trunc(`count` / 50). Unless spawnflag 1 it starts
`environ/stream`; with spawnflag 1 the effect is only registered. Use: toggle.

**`environ_splashfountain`.** Standard environ setup only; no effect, no use (inert in 1.07f).

**`environ_lightning`.** Standard environ setup; R = `random` (0 → 10). Unless spawnflag 1 the first flash is at
now + rand(0.5 R, 1.5 R). Each flash: temp entity 16 (global lightning flash), multicast to all clients reliably;
the next flash follows after rand(0.5 R, 1.5 R) seconds. Use: if a flash is scheduled it is cancelled (off),
otherwise one is scheduled rand(0.5 R, 1.5 R) from now (on).

**`environ_emergency_lights`.** Standard environ setup; level value L = 100; light-style-13 colour = (1, 1, 1) at
spawn. Use starts (or restarts) this cycle, each step 0.1 s apart unless stated:
1. Dim: L −= 10, colour (L/100, L/100, L/100); when L < 10 go to 2 (so 90, 80, …, 0).
2. Red rise: L += 10, colour (L/100, 0, 0); when L > 70 (L = 80) go to 4 after rand(2, 10) s.
3. Steady: colour (L/100, 0, 0); go to 4 after rand(2, 10) s.
4. Flicker: colour (rand(0.001, 0.005) × L, 0, 0) (0.08–0.4 red for L = 80); go to 3 after 0.1 s.
Steps 3–4 repeat forever. A second use restarts step 1 from the current L.

**`environ_skyfade`.** Standard environ setup; start colour C0 = key `color`, end colour C1 = key `endpoint`, duration
D = `delay`. With spawnflag 1 nothing ever happens (the class has no use). Otherwise: start time S = level time at
spawn truncated to whole seconds; every D × 0.01 s: if S + D < now the entity is removed (the last colour written
stays); else f = (now − S) / D and light-style-13 colour = f·C1 + (1 − f)·C0. (D = 0: removed at its first think
without any colour change, UNVERIFIED.)

**`environ_explosion`.** Standard environ setup; `health` (0 → 150) = H. Precaches sound list 58
(`weapons/C4/C4Exp.wav`), sets characters 19 and 20 of the material-flag string (configstring 16) to `y`, registers
the three explosion sounds and the effect. Each use (the entity stays and can explode again):
1. Unless spawnflag 2: linear area damage (SPEC-720 §14.1) with inflictor = attacker = ignored entity = itself,
   D = 2H, R = H, means of death 0, flags EXPLOSIVE (0x8000); then ignition of everything within 2H units
   (SPEC-761 §7.1, as `func_ignite` with radius 2H).
2. Character noise alert, loudness 2H, radius 2H (AI spec).
3. One-shot effect `weapons/world/airexplodemute` at the origin.
4. One of `impact/explosion/exp1.wav`, `exp2.wav`, `exp3.wav` (irand(1, 3)) as a positioned sound at the origin
   from the world entity: channel 0, volume 1, attenuation 1.
5. Explosion debris temp entity (TE 19) at the origin (SPEC-771 §C).
6. Unless spawnflag 1: flash blinding (SPEC-761 §7.3) from origin + (0, 0, 10), radius 10H, maximum 0.9,
   duration 0.5.
7. Camera shake from the origin: one tick of the `func_camerashake` rule (SPEC-761 §3.12) with maximum intensity H,
   radius 2H and event parameter 100.

**`environ_smoke_burst`.** Standard environ setup; `health` (0 → 50). Use: temp entity 26 (smoke burst) at the
origin with speed byte = `health`, multicast PHS.

**`environ_fire`.**
- Size N = `health` (0 → 12); `count` (0 → 64) and `delay` are stored. Effect by size (A.3). Unless spawnflag 1 the
  fire starts at spawn: the effect is started in a slot (scale 0) and, unless spawnflag 2, the loop sound (A.3) is
  set (state sound, data 0xF8 = full volume, attenuation class 0). With spawnflag 1 only the effect is registered.
- The three fire sounds and the four fire effects are always registered at spawn.
- Use: if burning → stop the effect, clear the loop sound; else → loop sound (unless spawnflag 2) and start the
  effect. The fire does no damage and ignites nothing.
- With `delay` ≠ 0 the entity also thinks every 0.1 s without any visible behaviour; `count` and the remembered
  start time (whole seconds) are not used by any behaviour found (UNVERIFIED).

**`environ_linetrap`.**
- Spawn: standard environ setup; a trace (no box, mask 0x02010003 = player-solid) from the origin 2000 units along
  the forward vector of `angles`, ignoring itself. If it hits nothing within 0.99 of that length the trap is inert
  (no use, no think). Otherwise the hit point E is the beam end; with spawnflag 4 and `dmg` 0, `dmg` = 2; unless
  spawnflag 2 the beam starts 0.1 s later.
- Every 0.1 s while on: trace origin → E (same mask, ignoring itself). If something other than the world blocks it:
  all targets fire with that entity as activator (Quake II rules) — again every 0.1 s while it stays in the beam —
  and with spawnflag 1 the trap switches off for good. With spawnflag 4, if the blocking entity is a player:
  damage `dmg` (knockback `dmg`, no flags, means of death 32, direction 0, point = the player's origin) every 0.1 s.
  While still on, one-shot effect `environ/tripbeam` from the origin with second position E (ext 0x04); next step
  in 0.1 s.
- Use: toggles on/off (an off trap sends no beam effect).

**`environ_inferno`.** Not the standard setup: solid TRIGGER, movetype NONE, the same box and clip mask, then the
brush model is set (not drawn), `health` = 100, classname forced to `environ_inferno`, origin = centre of the
brush box; use and touch do nothing; effect `environ/onfireburst` is registered. No burning, damage or ignition is
done by this class in 1.07f (UNVERIFIED: check a shipped map that uses it).

**`environ_invisible_attack`.** Character attack emitter (attack ids and their effects: SPEC-782 §4, §11).
- Spawn: standard environ setup; interval `delay` (0 → 0.2), attack id `count` (0 → 3), jitter `random`
  (0 → 5 degrees). Without spawnflag 1 it is on and the first attack happens after one interval; with spawnflag 1 it
  is off.
- Each attack (every interval while on): direction = forward vector of (pitch, yaw, roll) each changed by
  rand(−`random`, +`random`); the attack id is fired from the entity's origin in that direction with the entity as
  attacker and extra parameter 500 (the throw speed of thrown attacks). With spawnflag 2 the attack is made with
  damage disabled (effects only, SPEC-782 §11).
- Use: toggles on/off; switching on schedules the next attack one interval later.
- **Mouth shot** (`wait` = −1): on the first use or think the entity looks up its `target` (random pick among
  matches; a target without a Ghoul model is not handled — the original faults, so maps must point it at a
  character); if that entity's model has a bolt `sbolt_mouth`, the direction is the vector from this entity's origin to
  the bolt's current world position; if the target is a character with a body, the body's scripted kill setting is
  armed (variant 2: the next hit is fatal, damage 9999 with SPREAD, SPEC-720 §11.1) and the attack id is fired from
  the origin along that vector (extra parameter 500). The entity is then removed in every case.

**`environ_trainarm_spawn`.** Standard environ setup. Each use spawns a new entity: movetype PUSH, solid BBOX, box
(−4, −2, −4)–(3, 2, 4), origin = the spawner's origin + (0, 0, 1), velocity (−3500, 0, 0), `health` 100; Ghoul model
`objects/uganda/train_arm` object `arm` (default sequence looping), drawn with the model transform "scale 1.5, then
rotate 270° (3π/2) about the third Euler axis, then translate (0, 128, 176)" (UNVERIFIED axis). Every 0.1 s: if its
origin x < −4000 it is removed; otherwise a box test (mins (−512, −64, 0), maxs (512, 64, 64), start = end = origin +
(0, 128, 240), mask 0x06000081, ignoring nothing) and, if an entity other than the world is found: damage 999,
knockback 100, no flags, means of death 22 (crushed), direction (0, 0, 1), point = that entity's origin. The model
directory is absent from the 1.07f paks (the arm is invisible unless added data provides it).

**`environ_dustsource`.** Standard environ setup; amount A = `health` (0 → 5), period P = `delay` (0 → 10). Unless
A = −1, it acts by itself every rand(0.8 P, 1.2 P) seconds (first time rand(0.8 P, 1.2 P) after spawn); use acts once
immediately. Action: without spawnflag 1, temp entity 52 (dust puffs) at the origin with count A, multicast PVS;
with spawnflag 1, a trace (no box, mask solid|window = 3) from the origin 512 units straight up, ignoring itself,
and a weapon impact (TE 6, or TE 7 on a surface that changes damage stage) at the trace end with shot direction
(0, 0, −1), power byte A, mark style 1 (bullet hole), debris flag 1 (SPEC-771 §C impact rules).

**`environ_effect`.** Standard environ setup; effect name = key `soundname`, registered at spawn. If the name contains
`gore` and the blood lock (`lock_blood`, SPEC-721) is set, the entity is removed at spawn. Use: one-shot effect
`<soundname>` at the origin with direction = forward vector of `angles` (ext 0x08).

**`environ_effect_continual`.** Standard environ setup; effect name = key `soundname`. Unless spawnflag 1 it starts
in a slot (scale 0); otherwise only registered. Use: toggle. (No blood-lock check.)

**`environ_chunk_spewer`.**
- Spawn (no standard setup): without `target` → developer print `Object 'environ_chunk_spewer' without a target.`
  and removed; `count` = 0 → `Object 'environ_chunk_spewer' has zero chunks.` and removed. Movetype NONE, solid NOT,
  box (−4, −4, −4)–(4, 4, 4) (not linked). The first entity whose `targetname` equals `target` sets the angles:
  the entity faces it (direction normalised). If `count` < 1 and spawnflag 1 is clear, spewing starts by itself after
  0.1 s; otherwise the entity waits for use.
- Spew: debris temp entity 18 at the origin, direction = forward vector, count = `count` if positive else irand(3, 7),
  size 1, chunk type 32 (flesh), extents 0, 0, 0, material byte 35, multicast PHS (suppressed under the blood lock;
  TE 18 rate rule of SPEC-771 §C). If `count` > 0 it is a single burst: the entity is removed 3 s later. If `count`
  < 0 the next spew follows after |`count`| + rand(−0.2, 0.2) seconds, forever.
- Use: spews immediately, unless the entity that fired it is itself a player (direct player use does nothing;
  firing through a trigger works).

#### A.3 Tables

**Fire effect and sound by size N (`environ_fire`)**

| N | effect | loop sound |
|---|---|---|
| ≤ 3 | `environ/firesmall2` | `Ambient/Gen/FireFX/FireSmall.wav` |
| 4–10 | `gbfire` | `Ambient/Gen/FireFX/FireSmall.wav` |
| 11–19 | `gbfire` | `Ambient/Gen/FireFX/FireMed.wav` |
| 20 | `environ/firelarge2` | `Ambient/Gen/FireFX/FireMed.wav` |
| 21–29 | `environ/firelarge2` | `Ambient/Gen/FireFX/FireBig.wav` |
| ≥ 30 | `environ/firelarge3` | `Ambient/Gen/FireFX/FireBig.wav` |

**Effect-slot scales**

| class | stored scale (× 16 on the wire) |
|---|---|
| `environ_steamgen` | trunc(`height` / 35), default `height` 35 → 1 |
| `environ_smokegen` | 60 / `height` (float) |
| `environ_linefountain` | trunc(`count` / 50) |
| all other slot effects | 0 (default size) |

**Timing constants**

| class | value |
|---|---|
| steam / line trap / fire / train arm think | 0.1 s |
| lightning interval | rand(0.5, 1.5) × `random` (10) |
| spark interval | `delay` + rand(−0.5, 0.5) × `wait` |
| emergency lights | steps 0.1 s; hold rand(2, 10) s; flicker 0.1 s |
| sky fade update | `delay` × 0.01 s |
| invisible attack | `delay` (0.2) |
| dust source | rand(0.8, 1.2) × `delay` (10) |
| chunk spewer | single burst + removal after 3 s, or |`count`| ± 0.2 s |
| train arm | speed 3500 u/s toward −x; removed at x < −4000 |

### B. Ambient sound sets and music

#### B.1 Loading the ambient sound-set file
- At every level spawn, after all entities are spawned, the game loads `sound/sound.ams` (always this path; the
  worldspawn key `ambientSet` / configstring 12 is not consulted by the game). Layout: SPEC-112.
  - File missing: console `Unable to locate sound/sound.ams - all ambient sounds will be disabled` and nothing is
    loaded (lookups then find nothing, or the sets of the previous level — UNVERIFIED, test by removing the file
    between two level loads).
  - First byte not 11: `Invalid version (%d) for Ambient Sound Set (Should be %d)` (with the value read and 11),
    nothing loaded.
- The game keeps, per set, only the name, the loop wave and the wave list (intervals, distance and volume are
  client-side data). Each of the three groups holds at most 256 sets; set order = file order, so a set's *index*
  is its 0-based position inside its own group. The wave table holds at most 1024 paths of fewer than 64
  characters; a set holds at most 14 listed waves; names are compared case-insensitively. (Counts above these
  limits overflow in the original — never in stock data.)
- **Precache.** Then, for every entity in use whose `soundName` is set and not empty, the name is searched in
  group 0 (general), then group 1 (local), then group 2 (triggered); in the first group that contains it, the
  set's loop wave (if any) and every listed wave are precached as sounds (sound configstrings, path relative to
  `sound/`, exactly as written in the file). Unknown names precache nothing. This also covers classes that use
  `soundName` for other purposes (an effect name never matches a set).

#### B.2 Lookups used by other entities
| Lookup | Group | Result | Used by |
|---|---|---|---|
| area set index | 0 general | index 0…255, or not found | `func_ambientset` |
| point set index | 1 local | index 0…255, or not found | `environ_soundgen`, other point emitters (per class) |
| mover sound *k* | 2 triggered | sound index of the set's listed wave *k* (k = 0 start, 1 loop/moving, 2 stop), or −1 when `soundName` is unset, empty or unknown | movers and spawners (SPEC-760 §B5.1) |
For the mover lookup the entry *k* is read even when the set lists fewer than *k*+1 waves; such missing entries
read as wave 0 of the file (`Ambient/Gen/AirPlane/AirBy1.wav` in stock data). Stock case: the `func_door`
`new_tok_lever` in `tsr1` uses the one-entry set `ButtonElevator1`, so its moving and stop sounds would be
`AirBy1.wav` (UNVERIFIED in game; listen when that lever-door moves).

#### B.3 `func_ambientset`
- Spawn: not solid, no movement, invisible, box (−5, −5, −7)–(5, 5, 12), clip mask as props (SPEC-770 §3 step 6),
  linked. No think. Keys: `soundName` (set name in group 0), `targetname`. (`sounds` appears on two stock
  entities and is ignored.)
- Use (fired by a trigger; the activator is normally the player who touched it): if `soundName` names a group-0
  set and the activator is an entity in use with a client, that client's player-state **ambient-set value**
  (SPEC-202 field 29, short) becomes the set's index. Otherwise nothing happens. The client then plays that set
  from the ambient file of configstring 12 (SPEC-112). There is no per-area automatic switching on the server:
  the value changes only when an ambient-set entity is used. (Initial value at spawn: UNVERIFIED, presumably 0 =
  `Empty`.)
- Test: in `tsr1` the trigger targeting `t15` switches the player to `SubwayUpperLevel` (group-0 index 59).

#### B.4 `environ_soundgen`
- Spawn: not solid, no movement, invisible, box (−5, −5, −7)–(5, 5, 12) rotated by the right-angle rule
  (SPEC-770 §3 step 5), linked. Keys: `soundName` (set name in group 1), `targetname`, `spawnflags`.
- If `spawnflags` is exactly 0 the generator starts on: 0.3 s after spawn it looks up its set; found → the
  entity's looping-sound field = the set index, sound data 0xF8, and effects bit 0x100 (SPEC-202: "the sound
  field is an ambient-set id"), after which it never thinks again; not found → console
  `Soundgen with invalid name!` and the entity is removed.
  Any non-zero `spawnflags` value (stock: 1 on 78 entities, 2048 on one) makes it start off.
- Use toggles: on → off clears the looping sound and bit 0x100; off → on looks the set up again and sets the
  looping sound to its index (−1 when the name is not found — the bit is still set; original quirk), data 0xF8,
  bit 0x100.
- The client plays the local set (loop + random one-shots, audible distance) at the entity position (SPEC-112).
- Test: `trn1` has an `environ_soundgen` `NuclearDevice` with spawnflags 2048 and no targetname: it is silent for
  the whole level (UNVERIFIED in game).

#### B.5 Music value
Every client has a **music value** byte (player-state field 28, SPEC-202). The game writes these values (the
client chooses the music piece of the level's `.dms` set, SPEC-111):
| Value | Meaning (as the game uses it) | Written by |
|---|---|---|
| 0 | none yet / automatic | player spawn (SPEC-780, spawn step 8) |
| 1 | special value (UNVERIFIED meaning, presumably silence) | `func_musicControl` with `health` −2 (see quirk below) |
| 2 | automatic, calm | script `MUSIC` with n < 0 (SPEC-750), `func_musicControl` with `health` −1, the automatic rule |
| 3 | danger / action | the danger trigger and the automatic rule |
| n + 3 (n ≥ 0) | designer piece n | script `MUSIC n` (n ≥ 0), `func_musicControl` `sounds` n, worldspawn `startmusic` n at spawn |
(Value 3 is both "danger" and "designer piece 0".)

**Automatic rule** (runs for every client at the start of each of its server frames, SPEC-780 §6):
1. Values other than 0, 2 and 3 are left alone (designer music stays until changed by a script or entity).
2. Value 3 is kept until level time ≥ T + 4 s, where T is the level time of the last change truncated to whole
   seconds; values 0 and 2 are re-evaluated every frame.
3. Evaluation: score = 4 × (number of characters within 600 units of the player that are alive and whose
   expression state is 3 — the angry `m` face, SPEC-722 *Face expressions*; a character without a body counts
   as state 1) + 2 if the player's health < 50 + 2 more if health < 25. Result = 3 if score ≥ 9 and at least one such
   character was counted, else 2. (So danger needs three angry characters, or two with the player below 50
   health.)
4. If the result differs from the current value it is stored and T = trunc(level time).

**Danger trigger** (raised when a surviving player is hurt by more than 10 damage or is below 25 health
(SPEC-720), when a character fires at the player (SPEC-722), and when a character's shot flies by or ricochets near
the player (SPEC-782)): if the player's value is 0, 2 or 3 it becomes 3 and T = trunc(level time).

**`func_musicControl`** (unused in stock maps): spawn as `func_ambientset` (box (−5, −5, −7)–(5, 5, 12), not
solid, invisible). Use: `health` −2 → value 1 is written to the *controller's own* client, which it does not
have — the original game crashes (quirk; an implementation should write the activator's value or ignore it);
`health` −1 → the activator's value = 2; otherwise the activator's value = `sounds` + 3. The activator must be a
player (no check in the original).

### C. Effect emission rules

Every server-side effect is one of three kinds: a **temp entity** (svc_temp_entity, SPEC-201 §1, drawn as in
SPEC-401), a **named effect** (svc_effect, SPEC-201 §5, drawn as in SPEC-400), or a change of an entity's
**state** (events, effect slots, rotation, fade; SPEC-202, drawn as in SPEC-400/401). Byte layouts are those of
SPEC-201/202 and are not repeated. Delivery names (SPEC-201 "Delivery"): **PHS** = unreliable to the clients whose
hearable set contains the given point, **PVS** = unreliable to the clients whose visible set contains it,
**ALL-R** = reliable to every client, **unicast** = to one client. "Characters" are AI-driven entities; "players"
are entities with a client (bots included).

#### C.1 Shared rules
- **lock_blood.** With the parental option `lock_blood` set, every blood effect below is suppressed or replaced
  (SPEC-721, constant `lock_blood`): blood splat behind a target, blood pool, head cloud and exit spray are not
  sent; stump blood and blood clouds use `gore/noblood` / `gore/noblood_s`; debris of chunk type 32 (flesh) is
  not sent; a weapon impact on a character or player is not sent.
- **Impact descriptor** (the "impact" sub-structure of SPEC-201 §1), built from a trace result:
  - hit the world: kind 0 (point and surface normal); when no normal is available: kind 1 (point only);
  - hit a brush entity (solid = BSP): kind 2 — the point and normal are expressed in the entity's frame (offset
    from its origin and normal, both rotated by −yaw), the entity number, and the material = the hit surface's
    material when the entity's secondary model index is 0, otherwise the entity's `surfacetype` when non-zero,
    else the surface's material (UNVERIFIED reading of the secondary-model test; verify with a shot at a
    `func_breakable_brush` with `surfacetype` set);
  - hit any other entity: kind 3 — point, horizontal offset from the entity origin, material 35 (flesh) for
    characters and players, else the entity's `surfacetype`.
  - *Impact along a direction d at P* (used by explosion debris): trace from P + 5·d to P − 35·d (solid and window
    contents, ignoring the source entity); fraction < 0.99 → the descriptor of that hit, else kind 1 at P.
  - *Impact below P* (used by the explosion ground flash): the same with d = (0, 0, 1).
- **Named effects** are referenced by effect configstring index (the name is registered on first use; an
  unregistered name — index 0 — sends nothing). Optional extension values (scale, second position, direction;
  SPEC-201 §5 extension block) may be attached by the caller; they travel with the next named effect only and are
  then cleared. When that effect's index is 0 the pending extension values are not cleared and are attached to the
  following effect instead (original quirk).
- Unless stated otherwise every temp entity is sent **PHS** at its first position.

#### C.2 Temp entities sent by the game
| TE | Parameters filled in | Delivery | Sent for (callers by meaning) |
|---|---|---|---|
| 0 / 10 / 14 | sparks at P with direction byte; the caller picks white (0), orange/blue (10) or plain (14) | PHS | helicopter and tank bodies hit (SPEC-722); `environ_sparkgen`; damaged/destroyed electrical props (cash register, desk phone, speaker, TV sets, train, `light_generic_caged`; SPEC-770). Any other spark kind value writes no type byte (malformed message; never done in stock code) |
| 1 | P only (draws nothing) | PHS | sofa pain (SPEC-770) |
| 3 | blood splat: impact descriptor of a trace from the hit point P along the shot direction for 128 units; sent only when it hits the world or a brush entity at fraction ≤ 0.99, and not under lock_blood | PHS at P | bullets hitting characters (human, dog, cow bodies; SPEC-720/722), `misc_uganda_gore_pile` pain |
| 6 / 7 | weapon impact: impact descriptor of the shot trace, shot direction ×1000, power, mark style, debris flag (values from the attack, SPEC-782). Type 7 when the hit surface just changed its damage stage (the surface takes 25 damage-texture points, SPEC-001 import #25), else 6. Not sent when the hit surface is sky, nor on characters/players under lock_blood. **Rate limit:** hits on surface materials 32 (computer), 37 (glass), 38 (lit glass panel), 39 (vending), 40 (paper/painting) and 41 (paper pile) send at most one impact per 0.2 s game-wide (a hit at level time t is dropped while t < the stored time; when t > the stored time the stored time becomes t + 0.2; initial stored time −999999) | PHS at the trace end | bullets, knife slash, slug, rockets, split rockets, thrown knife, throwing star, Dekker gun, shots through water/glass (SPEC-782); `environ_dustsource` use |
| 8 | electric arc: start P, length byte, direction byte | PHS | an unused arc-generator think, the automatic gun turrets and the monitor/TV props (SPEC-770) |
| 9 | fly-by: the vector from the listener to the passing point is normalised; byte = trunc(its length) (only the low 8 bits are sent), direction ×1000, a second vector ×1000 (ignored by the client), bytes 100 and 220 (ignored) | unicast, unreliable, to the player concerned | character shots passing the player (SPEC-782 §3 item 6) |
| 13 | explosion ground flash: impact below P (ignoring the source) | PHS | chemical barrel explosion (SPEC-770) |
| 15 / 17 / 28 / 53 | floor decal at P with a long lifetime value (0 → 10000; ignored by the client): kind 1 → TE 15 scorch, 2 → TE 17, 3 → TE 28 "pipeleft", 4 → TE 53 puddle. Kind 0 is the blood-pool named effect (C.3); kinds ≥ 5 write no type byte (malformed; never used) | PHS | landmine explosions (scorch, SPEC-770), chemical barrel coming to rest (kind 2) |
| 16 | lightning flash, no payload | ALL-R | `environ_lightning` |
| 18 | debris: P, direction byte, count, size class, chunk type, extents x/y/z, surface material (all bytes). Chunk type 32 is not sent under lock_blood. **Rate limit:** a burst with the same surface material as the previous burst (game-wide) is dropped while level time ≤ the previous burst's time + 0.1 s | PHS | prop breaks and part removal (SPEC-770 §4/§5), `func_breakable_brush` pain/break (SPEC-761), character hit/gib debris (SPEC-721/722), `environ_chunk_spewer`, other props (computer monitor, fire hydrant, sink, TV wall, gumball and news machines, tables…) |
| 19 | explosion debris: impact along the entity's forward vector from origin + 2·forward | PHS at the entity origin | `environ_explosion`, chemical barrel, standard prop explosion (SPEC-770 §5.4) |
| 20 | flash-grenade explosion at P | PHS | flash grenade (SPEC-782) |
| 26 | smoke burst: P, byte = trunc(speed) | PHS | `environ_smoke_burst` |
| 27 | debug line: start, end, colour long, duration long | caller's choice | AI debug drawing only (SPEC-700) |
| 31 | lit sparks P, direction | PHS | `misc_newyork_gumball` pain |
| 49 | spark shower P, direction | PHS | `misc_newyork_gumball` and `misc_newyork_news_machine` pain |
| 32 | knife impact P, direction, byte = trunc(value) (3 in practice) | PHS | thrown knife and throwing star hits (SPEC-782) |
| 34 | shotgun cosmetic pellets | PHS | shotgun (SPEC-782 §4) |
| 44 | large explosion P, magnitude byte | PHS | vehicle/boss bodies (SPEC-722) |
| 45 | coloured puff P, R, G, B, A | PHS | part removal and shot-off parts (grey (120,120,120,200), SPEC-770 §5.6) and the part reactions of several props (castle chair, broom, tables, cash register, hanging lamp), New York trash props (SPEC-770) |
| 46 / 55 / 56 | small / medium / large explosion: P, size byte (×0.01), sound or mode byte | PHS | small: Bosnia searchlight, computer monitors, desk phone, speaker, TV sets; medium: TV wall death; large: vehicle bodies (SPEC-722) |
| 47 | shock ring P, short radius | **PVS** | AI debug/planning displays only (SPEC-700) |
| 48 | paper burst P, short count (from a byte) | PHS | garbage bag pain/death, news machine, gumball props |
| 50 | soda cans: P, unit direction (not scaled), short speed, short count | PHS | garbage bag death (speed 100, count 0 = random) |
| 52 | dust puffs P, short count | **PVS** | `environ_dustsource` use |
| 54 | ricochet sound P | PHS | train prop, ashtray (SPEC-770) |
| 58 / 59 | phone-hit / trash-can-lid sound P | PHS | desk phone, phone booth / New York trash can (SPEC-770) |
| 60 / 61 / 62 | multiplayer bullet (entity hit / world hit / world hit with large tracer): first effect byte, second effect byte, shooter's entity number, start, end (60) or hit direction ×2048 (61/62). Sent only when the shooter is a player; the shooter is not excluded (its client ignores its own shots) | PHS at the start | deathmatch bullets (SPEC-782 §3 item 5) |
| 63 | multiplayer projectile fired: effect byte, shooter's entity number, start, direction ×2048; only for player shooters | PHS at the start | deathmatch slug, rocket (both modes), MPG burst, phosphorus, concussion, fireball launches (SPEC-782) |
| 64 | debug box | caller's choice | AI debug drawing only (SPEC-700) |
| any (`style`) | `target_temp_entity` (D.1) | PVS | map entities |
Types not listed are never sent by the 1.07f game (SPEC-201 §1 column "Sent by 1.07f game").

#### C.3 Named effects (svc_effect)
| Form (SPEC-201 §5 flags) | Fields filled in | Message origin for delivery |
|---|---|---|
| at a position (0x01) | effect, position | that position |
| following an entity (0x02) | effect, entity number | the entity origin |
| on a bolt of an entity's model (0x04; 0x40 = reversed axes when requested) | effect, entity, bolt id (0 = random bolt) | the entity origin |
| on a bolt of a given Ghoul instance of an entity (0x04 + 0x08; 0x40 optional) | effect, entity, instance id, bolt id | the entity origin |
The extension flag 0x80 is set exactly when extension values are pending (C.1). Delivery is chosen by the caller:
a multicast mode (normally PHS, unreliable) and optionally one player excluded (the player whose client predicts
the same effect itself).

Fixed-name effects sent by shared helpers (lock_blood rules in C.1 and SPEC-721):
| Effect | Form | Sent for |
|---|---|---|
| `gore/bloodpool` | at a position, PHS | blood pool under a corpse (floor decal kind 0: corpse death events, ooze animation notes, the Uganda cow head) |
| `gore/stumpblood` (lock_blood: `gore/noblood`) | on an entity bolt, PHS; nothing when the bolt id is 0 | fresh wound / stump (SPEC-721) |
| `gore/bloodcloud_s` for size ≤ 50, `gore/bloodcloud` for size ≥ 51 (lock_blood: `gore/noblood_s` / `gore/noblood`) | on an entity bolt, PHS; nothing when the bolt id is 0 | severing, exit wounds (SPEC-721) |
| `gore/headcloud` | on an entity bolt, PHS | head explosion (SPEC-721) |
| `environ/gb_exitw` | at a position, with the extension direction = the spray direction, PHS | exit-wound spray onto walls (SPEC-721) |
All other named effects are sent by the owning behaviours with their own names (weapons SPEC-782, bodies
SPEC-721/722, props SPEC-770, environment entities section A, player spawn SPEC-780); callers include character
bodies (hits, gibs, weapon charge, muzzle flashes from bolts, ignition and microwave effects), projectiles and
explosions, `environ_effect`, `environ_explosion`, `environ_linetrap`, props (chemical barrel, fire hydrant,
toilet, fire extinguisher, garbage bag, test turret), the player spawn effect and C4/claymore use.

#### C.4 Entity-state effects
These change the entity state sent with every frame (SPEC-202); the client reacts as in SPEC-400/401.
- **Events.** Adding an event (with or without a parameter) fills the first free of the three event slots:
  slot 1 if empty; otherwise slot 2 (slot 1 gets the "another pair follows" bit 0x80); when slot 2 is already
  used, slot 3 is written (slot 2 gets 0x80) — a fourth event in the same frame overwrites slot 3. Clearing the
  events empties slot 1 (the encoder then sends no events). Users: camera shake (events 16–20 with the decay
  parameter, SPEC-761 §3.12), microwave zap and concussion grenade (SPEC-782), footsteps and landings (events
  3–14: character animation notes and Ghoul heel callbacks, physics landings, falling damage; SPEC-711/722/760/
  780), respawn/spectator events (SPEC-780), and event 22 (model tint reset) from the fade helpers below.
- **Effect slots** (four per entity: effect index, bolt id, scale; SPEC-202/400). *Add* writes the first slot
  whose effect is 0 (nothing when all four are used); scale is sent as trunc(scale × 16). *Remove (effect,
  bolt)* empties the first slot with that effect index (and that bolt, unless the bolt given is 0 = any).
  *Rescale (effect, bolt, scale)* rewrites the scale of the first matching slot. *Has (effect, bolt)* reports
  whether a matching slot exists. Users: environment generators (snow, steam, smoke, water drops, blood rain,
  line fountain, fire, continual effects; section A), burning barrel, car, cash register, monitor, desk phone,
  sprinkler, road flare, chainsaw, gutted cow props (SPEC-770), helicopter damage smoke and snowcat exhaust
  (SPEC-722), projectile trails (rockets, throwing star, phosphorus, concussion, fireball, Molotov; SPEC-782),
  some AI navigation markers.
- **Rotation** (effects bit 0x0001, SPEC-400 "rotate"): sets bit 0x0001; slot data v0 = 999 (marker),
  v1..v3 = trunc(512 × angular rate) per axis (the client turns by v × ms / 512, so the rate is in degrees per
  millisecond), v4..v7 = ids of up to four Ghoul instances to rotate (0 = none; v4 = 0 → the client rotates the whole entity).
  *Clear* removes bit 0x0001 and zeroes the twelve values; *get* returns the values only while the bit is set.
  Users: fans, radar dish, chandelier, chairs, truck chunks, camel cart, caged light (SPEC-770).
- **Fade out** (effects bit 0x8000, SPEC-400 "fade"): zero the slot data, clear bits 0x0001 and 0x8000, reset the
  model tint to (1, 1, 1, 1) and add event 22; then clear bit 0x0800 (pulsing tint), set bit 0x8000, and store
  start = trunc(level time × 1000) ms (v0 + 65536·v1) and duration d = trunc(seconds × 1000) ms (v2). Each check
  of a fading entity: when d ≥ 1 and level time ≥ (start + d)/1000 the fade is finished — bit 0x8000 cleared,
  data zeroed, the model and its bolt-ons tinted (1, 1, 1, 0) — and the caller removes the entity; when d ≤ 0
  the fade is cancelled at the first check (tint (1, 1, 1, 1) on the model and its bolt-ons, event 22), so a
  fade-in is never kept by the server (original quirk; unused in 1.07f). *Cancel fade*: data zeroed, bits
  0x0001/0x8000 cleared, tint (1, 1, 1, 1), event 22. Durations used: 3 s for character corpses before
  removal; 1 s for dropped bolt-ons, body-queue copies and one AI helper (SPEC-722/780). Cancel: at player spawn
  and on fireball hits.

### D. Other

#### D.1 `target_temp_entity`
As Quake II: key `style` = temp-entity type; when used it sends svc_temp_entity with that type and the entity
origin as the only payload, to clients in the PVS of the origin (unreliable). Only types whose payload is exactly
one position parse correctly (SPEC-201 §1: 1, 12, 20, 21, 38, 51, 54, 58, 59). Not used in stock maps.

#### D.2 Hit-scan attack helpers
The bullet trace (single bullets and the shotgun's centre shot) and the shotgun pellet spread are specified in
SPEC-782 §3 and §4, including the temp entities they send (weapon impact TE 6/7, multiplayer TE 60/61/62, shotgun
TE 34, fly-by TE 9). Nothing to add here; their emission rules are listed in section C.

## Constants
All constants are given in place in the rules above.

## Test vectors / how to verify
### Environment effect entities (section A)

1. `environ_steamgen` with no keys: steam starts at load, scale value 16 on the wire (scale 1), no damage. With
   `dmg 5`: a target whose centre is 48 units straight ahead (dot = 1) gets p = 5 × (1 − 48/96) × 1 = 2.5 → 2 damage
   every 0.1 s (cone formula of SPEC-720 §14.3, R = 96); at 45° off the axis (dot 0.707) p = 5 × 0.5 × 0.024 → 0;
   beyond 45.6° (dot < 0.7) it is not a candidate at all.
2. `environ_steamgen` with `wait 2 delay 1`: on 2 s, off 1 s, repeating (observe the effect slot in a demo).
3. `environ_explosion` with no keys, used once: damage 300 at the centre falling to 0 at 150 units; flash blinding
   radius 1500; sound one of exp1–exp3; using it again explodes again.
4. `environ_lightning` with no keys: a TE 16 message to all clients every 5–15 s.
5. `environ_emergency_lights`: at load configstring 5 = `1.000000 1.000000 1.000000`; after use it goes through 0.9 … 0
   grey, then red 0.1 … 0.8 within 1.8 s, then stays 0.8 red with a single dark frame every 2–10 s.
6. `environ_skyfade` with `color 1 1 1`, `endpoint 1 0 0`, `delay 10`: configstring 5 changes 100 times over 10 s from
   white to red, then the entity is gone.
7. `environ_fire` with `health 25`: effect `environ/firelarge2` in a slot and loop sound `FireBig.wav`; use removes
   both; use again restores both.
8. `environ_linetrap` with spawnflag 4: a player standing in the beam takes 2 damage (means of death 32) every 0.1 s and the targets fire
   every 0.1 s; with spawnflag 1 the targets fire once and the beam effect stops.
9. `environ_chunk_spewer` with `count 10`, fired by a trigger: one TE 18 with count 10, chunk type 32; the entity is
   removed 3 s later. With `count -2`: 3–7 chunks every 1.8–2.2 s from level start.
10. `environ_effect` with `soundname gore/headcloud` and `lock_blood 1`: the entity does not exist after load.

### Ambient sets, music and effect emission (sections B–D)
1. Fire a pistol at a window (glass, material 37) 10 times per second: at most 5 impact effects per second appear;
   on a concrete wall all 10 do.
2. Break two crates with the same surface type in the same server frame (one explosion): only one debris burst
   is seen; props with different surface types both burst.
3. With `lock_blood 1`, shoot a character: no wall blood splats, no bullet-impact puffs on the body, stumps show
   `gore/noblood`; flesh chunks (chunk type 32) never appear.
4. `tsr1`: walk through the trigger targeting `t15` — a packet capture shows the player-state ambient-set value
   change to 59 (`SubwayUpperLevel`).
5. `trn1`: the `NuclearDevice` sound generator (spawnflags 2048, no targetname) is never heard.
6. Music: in single player, stand still near two characters that have spotted you (angry faces) with health 40:
   the music value becomes 3 within one frame of the second character turning angry; take one hit of more than 10
   damage at full health with no characters near: value 3 for at least 4 s, then back to 2.
7. A corpse that is removed fades out over 3 s: the entity's effects field carries bit 0x8000 with
   v2 = 3000 and v0 + 65536·v1 = the level time in ms when the fade started.
8. A spark-generating prop sends TE 0/10/14 with the direction byte; a capture of `environ_lightning` shows
   TE 16 with no payload in the reliable stream of every client.

## Open questions
### Environment effect entities (section A)

- `environ_raingen`, `environ_rain_worldtrigger`, `environ_cloudtrigger`, `environ_splashfountain` and
  `environ_inferno` have no observable server behaviour in 1.07f; verify in a shipped map that uses them whether the
  client shows anything (e.g. rain from map data).
- The effects `environ/snowchute` and `environ/playersnow` and the model `objects/uganda/train_arm` are not in the
  1.07f archives; check whether a later patch or the CD data provides them.
- Axis of the train-arm model rotation (third Euler argument) — verify visually once the model exists.
- Meaning of `count` and `delay` on `environ_fire` (stored, unused).

### Ambient sets, music and effect emission (sections B–D)
1. Meaning of music value 1 (written only by the crashing `func_musicControl` `health` −2 path) — client-side
   behaviour; test by sending the value from a modified server.
2. Initial ambient-set value of a newly spawned player (presumably 0 = `Empty`); check with a capture at spawn.
3. Material of kind-2 impacts on brush entities with a non-zero secondary model index (UNVERIFIED reading).
4. Whether the ambient sets of the previous level survive a level load without `sound/sound.ams`.
