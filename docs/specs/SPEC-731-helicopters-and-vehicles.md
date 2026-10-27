# SPEC-731: Helicopters and vehicles (helicopter, tank and snowcat AI)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (game module), version 1.07f
Depends on: SPEC-700 (character AI framework, type code 8), SPEC-711 (vehicle command actions, A8), SPEC-720 (damage flags, helicopter hit handling), SPEC-722 (helicopter body: smoke, canopy blood, barrel sequences), SPEC-750 §12.8 (HELI script command), SPEC-752 (savegame chunks), SPEC-760 (entity layout, movetypes, skill record), SPEC-782 (attack ids 14 rocket, 28 heavy bullet), SPEC-701 (alert sounds), SPEC-710 (decision class 1), SPEC-761 §7 (flash blinding, camera shake), SPEC-011 (instance methods: play modes, show/hide, note callbacks), SPEC-201/SPEC-401 (temp entities 18, 44, 56).

## Purpose

This spec has two parts: **A** the helicopters (`m_x_chopper_black`, `m_x_chopper_green`, `m_x_chopper_white`, `m_x_hind`) and **B** the ground vehicles (`m_x_tank`, `m_x_snowcat`). Each section below gives part A first, then part B. A reference "§n" points into the same part; references into the other part name it ("Part B §9.1").

### Part A — helicopters

SPEC-711 describes the generic "vehicle command action" that carries a HELI script command into the helicopter AI,
and SPEC-750 §12.8 how a script queues it. Both leave open what each command id does. This part gives the
helicopter classnames, their spawn set-up, the meaning of every HELI command id 1–49, the flight routines the
commands start, the autonomous combat behaviour, the weapons, damage, crew kills, death and the savegame data of
the helicopter AI.

All times are level seconds; the helicopter thinks every 0.1 s (one *frame*); "per frame" values are applied once
per think. Angles are degrees in Quake II order (pitch, yaw, roll; positive pitch = nose down). anglemod(a) is the
Quake II angle normalisation to [0, 360) with 1/65536-turn resolution.

### Part B — tank and snowcat

Specifies the two ground vehicles of the single-player game, `m_x_tank` and `m_x_snowcat`: spawn values, models,
the vehicle AI think (driving, turret and gun aiming, cannon shell, machine-gun bursts), the five VEHICLE script
commands, damage, death, touch and use, and the saved vehicle state. It answers SPEC-750 open question 1 for VEHICLE
ids, SPEC-722's open items "meaning of spawnflag 1" and "roles of the aiming parameters", and SPEC-711's "routine
semantics of the vehicle commands".

Throughout, **T** is the toughness level 0–4 of SPEC-760 B9.2 (skill presets: `skill` 0 → 0, 1 → 1, 2 → 2,
3 → 4, 4 → 3). "Player 1" is the first client entity. A *think* is one vehicle AI update (every 0.1 s while
active). Angles of turret parts are in radians relative to the part they hang on; ordinary entity angles are in
degrees.

## Interface / layout

### Part A — helicopters

#### 1. Command table (HELI ids, SPEC-750 §12.8)

A HELI command carries (command id, vector V, entity E, scalar S); unused parameters are 0 / none. The command
creates an action of one of three families (SPEC-711 action classes 16, 17, 18). When the action is created the
*command parameters* (V, E, S) are copied into the helicopter's *command slot*; each frame the running routine
reads them from there. The action ends when its routine reports completion **and** no autonomous mode (§8) takes
over (SPEC-711 A8). "Done at once" means the routine completes in its first frame.

Family 1 — flight (ids 1–10, 22, 23, 33–45, 47–49):

| id | parameters | effect |
|---:|---|---|
| 1 | — | **Take off**: spin up the rotors, then climb 224 units (§9.1). Done at once with spawnflag 1 (already airborne). |
| 2 | — | **Return home**: V is replaced by the home point. Done at once with spawnflag 1. Otherwise, unless already at the home x/y and not higher than home z + 224, *go to* home + (0, 0, 224) (§9.4); in every case the *land request* flag is set, so a landing (§9.6) follows when the routine ends. |
| 3 | — | **Repair**: health += trunc(max health / 50) per frame up to max health; done when full. |
| 4 | — | **Reload rockets**: rockets left = 8; done at once. |
| 5 | V | **Go to point** V (§9.4). |
| 6 | E, V | **Go to entity**: goal = E's origin + V (E's position when the action starts). Ignored (no routine) when E is none. |
| 7 / 8 / 9 | E, S | Go to entity with offset (S, 0, 0) / (0, S, 0) / (0, 0, S). Ignored when E is none. |
| 10 | V | **Go to relative**: goal = current origin + V. |
| 11 | — | no action is created |
| 22 / 23 | S | **Strafe** sideways by |S| to the right / to the left (§9.5). |
| 33 | S | **Hover** for S seconds without engaging (§9.7). |
| 34 | S | **Hover and engage**: hover for S seconds, attacking a visible target (§9.7). (Engaging is selected by the id, not by a parameter.) |
| 35 / 36 | V | **Flight bounds**: set the minimum (35) / maximum (36) corner of the flight bounds box to V (§10). Done at once. |
| 37 | S | **Set max health** := trunc(f × S) + 1000, f = 0.33 × (T + 1), T = toughness level (SPEC-760 B9.2); if the current max health is < 1 it is set to 0 instead (quirk: the test looks at the old value, not S). Done at once. |
| 38 | S | **Set health** := 0 when S < 1, else trunc(f × S) + 1000 (f as for 37). Done at once. |
| 39 | V | **Set exit point** := V. Done at once. |
| 40 | S | **Navigation box**: S ≠ 0 → movement traces use the navigation box (§10); S = 0 → point traces. Done at once. |
| 41 | S | **Auto-combat** for S seconds (§11). |
| 42 | V | **Add waypoint** at V (a point waypoint, §10). No routine: the action completes at once (unless an autonomous mode is active). |
| 43 | V | **Bound the last point waypoint** with the extents V (§10). No routine. |
| 44 | V | Set the *minimum corner* of the last point waypoint to V (raw value, used by 45). No routine. |
| 45 | V | Set the *maximum corner* of the last point waypoint to V, then convert it into an area waypoint: centre := (min + min) / 2 (sic — the minimum corner, not the box centre), min and max made relative to that centre, then bounded as in 43 with the relative max as extents. No routine. |
| 47 | S (E = the helicopter) | **Repaint**: the helicopter and all its bolt-ons get the `cobra` material skin `m_x_chopper_black` (S = 1), `m_x_chopper_green` (S = 2) or `m_x_chopper_white` (any other S), model `enemy/chopper` set `cobrass`. No routine. This corrects SPEC-711 A8, which calls it a spawn. |
| 48 | S (E = the helicopter) | **Become Hind** when S = 1 (§7); otherwise nothing. No routine. |
| 49 | — | **Crash**: exit point := (0, 0, 0), crash start time := now + 2 s, crash spin routine (§12.3). |

Family 2 — facing, look and aim (ids 12–21):

| id | parameters | effect |
|---:|---|---|
| 12 | E, V | **Face entity**: turn (§9.3) until the nose points at E's origin + V. Ignored when E is none. |
| 13 | V | **Face point** V. |
| 14 | V | **Face direction**: face the point origin + normalise(V) × 1000. |
| 15 | V | **Face relative**: face origin + V. |
| 16 / 19 | E, V | **Pilot look** (16) / **gun aim** (19) at E's origin + V until reached (§9.9). Ignored when E is none. |
| 17 / 20 | V | Pilot look / gun aim at origin + V (the point is taken relative to the helicopter's origin). |
| 18 / 21 | V | Pilot look / gun aim at the absolute point V. (Ids 17/20 and 18/21 are the opposite of what their position next to 13/15 suggests; no stock script uses ids 12, 13, 15–21.) |

Family 3 — weapons (ids 24–32):

| id | parameters | effect |
|---:|---|---|
| 24 / 25 | — | Rockets on / off (flag only). Done at once. |
| 26 / 27 | — | Gun on / off. 27 also stops the barrel spin (sequence `barrel_still`), clears the command slot's scalar and ends the helicopter's current action at once (it marks the running action complete when it is queued). |
| 28 | E, V, S | **Attack entity**: attack point = gun aim point = E's origin + V, attack time = S (§13.1); only when E exists, is in use and can take damage; the action itself completes at once and the attack continues as an autonomous mode. |
| 29 | V, S | Attack point V. |
| 30 | V, S | Attack origin-relative point origin + V. |
| 31 / 32 | — | Fire while flying on / off (§13.3). Done at once. |

Script-handler side (SPEC-750 §12.8): id 46 creates no action; it sets spawnflag 8 on the helicopter entity,
which no helicopter code reads in 1.07f (UNVERIFIED for other modules). For ids 47 and 48 the script handler
passes the helicopter itself as E. An entity-taking id given no entity (6–9, 12, 16, 19, 28) creates an action
without routine, which completes at once unless an autonomous mode is active.

Command-slot rule: when an action completes normally, the helicopter clears the command slot (E none, V and S 0).

Queue-time effects: ids 42–45, 47 and 48 take effect when the command is *queued* (while earlier actions may still
be running), not when their action starts; id 27 likewise stops the barrel and ends the current action at queue
time. All other ids act when their action becomes current.

#### 2. Classnames
| classname | model directory / set | main material and skin | notes |
|---|---|---|---|
| `m_x_chopper_black` | `enemy/chopper` / `cobrass` | material `cobra`, skin `m_x_chopper_black` | Cobra-style gunship |
| `m_x_chopper_green` | same | skin `m_x_chopper_green` | |
| `m_x_chopper_white` | same | skin `m_x_chopper_white` | |
| `m_x_hind` | `enemy/hind` / `hind` | material `hind`, skin `hind` | no navigation lights |
All four use character type code 8 (SPEC-700; team 10), the same AI and the same body (SPEC-722). The skins are the
textures `ghoul/enemy/chopper/<classname>.tga`.

#### 3. Spawn set-up
1. Precache: effect `weapons/othermz/heli`, attack 28.
2. Server flags |= 4 (monster); with spawnflag 32 also 0x100 (meaning UNVERIFIED).
3. Use reaction = toggle free fire (§14); touch reaction = crash touch (§14).
4. Box (−280, −280, −50) … (280, 280, 70).
5. Unless spawnflag 1 (starts airborne): the origin is raised by 200 and a box trace straight down to z = −600 is
   made; its hit point (when 0 < fraction < 1) becomes the *exit point candidate*. A second trace with box
   (0, 0, −50) … (0, 0, 0) from the raised origin down to z = −600: when it hits, the origin becomes the hit point
   (the helicopter is put on the ground); otherwise the origin is lowered by 200 again.
6. Health = max health = 1000.
7. The AI is created with type code 8; this creates the body and builds the model (§4).
8. Helicopter state: movement box from the entity box (§10: 280 / 140); home := origin; exit point := the exit point
   candidate — the ground below the spawn point, or the origin itself with spawnflag 1 or when nothing was hit;
   aggression := `volume`; main model skin as in §2; crew sniper kills allowed unless spawnflag 16 is set and the
   toughness level is ≥ 2.
9. Movetype 6, solid 2 (box), entity flag "no knockback", can take damage, next think in 0.1 s.
10. First think: the vehicle decision (SPEC-710, kind "vehicle") is added; precache of effects
    `environ/helismoke`, `heli_expl`, `weapons/world/autogundeath`, `weapons/world/rockettrail2`, sounds
    `impact/explosion/exp1.wav`, `impact/explosion/med.wav`, `weapons/assault/fire.wav`, attack 14 (rocket); with
    spawnflag 16 the Hind model is precached (a temporary Hind is set up and freed).
Body defaults (fresh body): 8 rockets, no combat pattern, crew sniper counter 1 (one qualifying sniper hit kills a
crew member, SPEC-720 — UNVERIFIED whether pilot and gunner share the counter), all mode flags off (rockets, gun,
fire while flying, auto-combat off), point traces off (movement box on).

#### 4. Model and bolt-ons
Built when the AI is created (and again when becoming a Hind, §7). Bolt-on notation: `directory model` attached at
parent bolt → child bolt.
- Main model as in §2. Every bolt-on below that cannot be created only prints a developer warning and is skipped.
- Navigation lights (choppers only): `enemy/chopper nav_lite` at `nav_lite` → `to_nav_lite`, sequence `nav_lite`
  looping; `nav_lite2` at `nav_lite2` → `to_nav_lite2`, sequence `nav_lite2`; `nav_lite3` likewise; `tail_lite` at
  `tail_lite` → `to_tail_lite` (no sequence).
- Gun: `objects/generic/gun_auto null` at `barrel_base` → `DUMMY01`; on it `enemy/chopper barrel` at `DUMMY01` →
  `to_barrel_base`. Gun turning: the null turns in yaw, the barrel in pitch (§9.9).
- Crew heads: `enemy/chopper head` at `body1` → `to_body` (gunner, look offset (0, 0, 6.411)) and at `body2` →
  `to_body` (pilot, look offset (22, 0, 12.935)).
- Rotors: `enemy/chopper rotor` at `rotor_tail` → `to_rotor` and at `rotor_main` → `to_rotor`, scaled uniformly by
  0.25 and 1.25 (choppers) or 0.375 and 1.875 (Hind) — UNVERIFIED which factor belongs to which rotor (expected:
  the small one to the tail rotor). Both play `rotor_anim` looping with spawnflag 1, otherwise `rotor` (stopped).
  The main rotor reports the end of its sequences (used by take-off and landing).
- Canopy: `<own directory> canopy` at `canopy` → `to_canopy`, sequence `canopy` once (canopy blood, SPEC-722).
- Looping entity sound `Enemy/Helicopter/Blade.wav`, sound data 0xFB (volume 248, attenuation class 3).
- Bolts used later: `flash_barrel` (gun muzzle), `FLASH_ROCKET1` / `FLASH_ROCKET2` (rocket pods), `BODY1`,
  `ROTOR_MAIN`, `ROTOR_MAIN1` (crash stages), `smoke_engine`, `smoke_tail` (SPEC-722 smoke).

#### 5. Spawnflags and keys
| spawnflag | meaning |
|---:|---|
| 1 | starts airborne: not put on the ground, rotors spinning; take-off (id 1) and return home (id 2) complete at once |
| 2 | when killed, flies to (a clear spot at) the exit point and is removed there instead of crash-spinning |
| 4 | unlimited rockets (refilled to 8 when empty) |
| 8 | set by HELI id 46; no effect found |
| 16 | will become a Hind (Hind model precached at the first think); sniper crew kills disabled at toughness ≥ 2 |
| 32 | server flag 0x100 (UNVERIFIED meaning) |

| key | use |
|---|---|
| `volume` | aggression a (float): probability of combat interjections, compared with irand(0, 10) (§11.2); 0 = never |
| `killtarget` | used when the helicopter dies (§12.4); any value other than `musicscript` also triggers the wreck teleport of §8 step 8 |
| `targetname` | script/trigger addressing (HELI commands, use) |
Health is fixed at 1000 at spawn (scripts change it with ids 37/38).

#### 6. Savegame data of the helicopter AI
After the common AI record (SPEC-700, SPEC-752) the helicopter AI writes chunk `AIHA` (48 bytes), then one chunk
`HEWP` (44 bytes) per waypoint in list order. (On load `AIHA` is read first, then the common record, then the
waypoints.) Actions use `AIHE` (SPEC-711); the body uses `HELI` (SPEC-722).

`AIHA` (all 4-byte values):
| offset | meaning |
|---:|---|
| 0 | routine-done flag |
| 4 | remove flag |
| 8, 12 | unused |
| 16 | killer (entity index) |
| 20 | action id counter (incremented for every helicopter action created; ids are the HELI done-event ids, SPEC-750) |
| 24 | id of the current action |
| 28 | remembered queue size |
| 32 | waypoint list link (ignored on load) |
| 36 | waypoint count |
| 40 | point waypoint count |
| 44 | area waypoint count |

`HEWP`: 0 ignored (4 bytes), 4 origin (3 floats), 16 box minimum (3 floats, 0 for point waypoints), 28 box maximum
(3 floats), 40 ignored link (4 bytes). A waypoint is a point waypoint when all six box values are 0.
The flight bounds and the crash-stage timer are globals and are not saved.

#### 7. Becoming a Hind (id 48 with S = 1)
The helicopter keeps its aggression, home point and exit point; its body and Ghoul model are discarded and rebuilt
as a Hind (`enemy/hind`, set `hind`, §4); the movement box is recomputed from the entity box; movetype 6, solid box,
no knockback and "can take damage" are set again. The new body starts with the body defaults (§3), so rockets,
mode flags (gun, rockets, fire while flying, auto-combat, hover) and the combat target are reset — UNVERIFIED in game
(verify by switching a helicopter with rockets off to a Hind and checking whether it fires).

### Part B — tank and snowcat

#### 1. Classnames and spawn values

| item | `m_x_tank` | `m_x_snowcat` |
|---|---|---|
| AI type code (SPEC-700) | 9 | 10 |
| model directory / sequence set | `enemy/tank` / `tank` | `enemy/snowcat` / `snowcat` |
| bounding box | (−152, −85, −44)…(152, 85, 80) | (−92, −67, −34)…(92, 67, 58); with spawnflag 1: (−91, −66, −33)…(91, 66, 58) |
| health = max health | 500 × (T + 1) (500, 1000, 1500, 2000, 2500); the map key `health` is overwritten | 1000 |
| solid, takedamage | bounding box (2), on | same |
| movetype | 10 at spawn, changed to 2 (PUSH, SPEC-760) as soon as the Ghoul model is attached | same |
| entity flags | "no knockback" (0x800) added | same |
| clip mask | 0x2030003 | same |
| render flag | Ghoul model (0x400000) | same |
| pain / die / blocked callbacks | none (the AI handles damage) | none |
| touch | crush/shove rule (§15) | none |
| use | toggles the disabled state (§7) | none |
| spawnflags | bit 1 (value 1) read, then the whole field is replaced by 2 | same |
| team reported to the team rules | 1 (as soldiers; SPEC-700 table lists "own rules") | 1 |
| savegame class code | 9 | 10 |
| next think | level time + 0.1 | same |

Keys used: `targetname` (script handle), `killtarget` (§13, §14), `angle`/`angles`, `origin`, `spawnflags`
(bit 1 only). No other key is read by these classnames.

Precached at spawn — tank: sounds `Ambient/Models/Tank/tanklp.wav`, `impact/surfs/metal1.wav`…`metal3.wav`; effects
`environ/wallspark`, `environ/helismoke`, `weapons/othermz/tank`, `weapons/world/rocketexplode`,
`weapons/othermz/machinegun`, `environ/machgun_smoke2`; Ghoul model object `effects/explosion` set `explode80`.
Snowcat: the same plus effects `environ/kf_exhaust01` and `environ/snowspray`.

**Spawnflag 1 ("disabled", resolves SPEC-722).** The vehicle starts in the disabled state of §7: the body's disabled
flag is set (snowcat: the alternate look of SPEC-722 — headlight beams hidden, wheel parts hidden, no exhaust), and the
vehicle stops thinking after its first think (so it never drives, never fires and never finishes a VEHICLE command).
Stock maps use it for static scenery tanks and parked snowcats.

#### 2. Models and bolt-ons
All models are directory/name pairs under `ghoul/`. "A at B → C" = model A is bolted with its bolt C to bolt B of its
parent.

| vehicle | part | model | attachment |
|---|---|---|---|
| tank | hull | `enemy/tank` / `tank` | the entity's own instance |
| tank | turret | `enemy/tank` / `turret` | at hull `bolt_turret` → `to_bolt_turret` |
| tank | cannon | `enemy/tank` / `canon` | at turret `bolt_canon` → `to_bolt_canon` |
| tank | gun mount (invisible: all mesh parts hidden) | `objects/generic/gun_auto` / `null` | at turret `bolt_machine_gun` → `DUMMY01` |
| tank | machine gun | `enemy/tank` / `machine_gun` | at mount `DUMMY01` → `to_bolt_machine_gun` |
| snowcat | hull | `enemy/snowcat` / `body` | the entity's own instance |
| snowcat | turret | `enemy/snowcat` / `turret` | at hull `DUMMY03` → `DUMMY03` |
| snowcat | gun 1 | `enemy/snowcat` / `gun` | at turret `DUMMY01` → `DUMMY01` |
| snowcat | gun 2 | `enemy/snowcat` / `gun` | at turret `DUMMY02` → `DUMMY01` |
| snowcat | headlights (2) | `objects/generic/beam` / `beam` | at hull `headlight_left` / `headlight_right` → `to_headlight` |

Muzzle bolts: tank cannon `flash_canon`, tank machine gun `flash_machine_gun`, snowcat guns `_flash1` (each gun).
The snowcat has no cannon. When the hull model is attached its sequence `tank` (tank) is set to a static pose at its
first frame and `snowcat` (snowcat) is played once (if the model has a sequence of that name; SPEC-011 modes).
A note callback for `EOS` is registered on the turret instance (§16).

Body parameters (SPEC-722 "aiming parameters") and their roles:

| parameter | tank | snowcat | role |
|---|---|---|---|
| first value | 20 | 30 | hull turn rate, degrees per second (§8) |
| second value | 50 | 70 | drive speed, units per second (§8) |
| turret yaw rate | 0.04 | 0.06 + 0.01·T | radians per think: tank turret; snowcat turret **and** both guns' pitch |
| gun pitch rate | 0.03 | 0 (unused) | tank cannon pitch, radians per think |
| machine-gun rate | 0.05 | 0 (unused) | tank gun mount yaw and machine-gun pitch, radians per think |

#### 3. VEHICLE command (SPEC-750 §12.8) — the ids
A VEHICLE command carries (id, vector, entity, scalar). Both vehicles use the same five commands (the snowcat uses the
tank's command class 21; see §17). The entity parameter is unused by every id. Commands queue on the vehicle AI
and run one at a time, one step per think (SPEC-711 classes 20/21); a command "finishes" when its step reports
finished, which completes the action and lets the HELI/VEHICLE done event fire.

| id | parameters | tank | snowcat |
|---:|---|---|---|
| 1 | vector = destination; scalar = 1 → drive in reverse | turn toward, then drive to the destination (§8); finishes on arrival | same rule (§8) |
| 2 | vector = target point | aim turret and cannon at the point (§9.2); when aimed, fire the cannon (§10); finishes when the shell has exploded or missed | aim turret and guns at the point (§9.4); when aimed, finishes at once — nothing is fired |
| 3 | scalar ≠ 0 → on, 0 → off | machine gun automatic fire on/off (§11); finishes at once | same, for both guns |
| 4 | — | self-destruct: the death of §14.1 with the vehicle as its own killer and damage 10000; finishes at once | self-destruct: the reduced death of §14.2 |
| 5 | vector = target point | aim only (§9.2); finishes when aimed | aim guns only (§9.4) |
| other | — | no step: finishes at its first execution (corrects SPEC-750's UNVERIFIED "never fires") | same |

A command whose vehicle has no AI body never finishes. Stock scripts use ids 2, 3 (scalar 1) and 4 only; tanks
that move in stock levels are moved by MOVE or ROF commands, not by id 1.

#### 4. Saved vehicle state (`AITA`, 52 bytes)
Written before `AIAI` for types 9 and 10 (SPEC-700 "Character chunks"); little-endian.

| offset | size | type | meaning |
|---:|---:|---|---|
| 0 | 4 | int | number of commands created so far (the newest command's identifier) |
| 4 | 4 | int | identifier of the command being executed (set each think while one runs) |
| 8 | 4 | int | killer entity, saved as an entity index (SPEC-752 rule for entity references) |
| 12 | 4 | int | removal request flag (always 0 in 1.07f: nothing sets it) |
| 16 | 1 | byte | reverse-drive flag (command 1 scalar = 1) |
| 17 | 3 | — | padding |
| 20 | 12 | int[3] | unused (0) |
| 32 | 1 | byte | machine gun on |
| 33 | 1 | byte | line-of-fire check enabled (always 1) |
| 34 | 1 | byte | "current command step finished" result |
| 35 | 1 | — | padding |
| 36 | 4 | float | start time of the next machine-gun burst (level seconds) |
| 40 | 12 | vec3 | machine-gun target point |

A freshly created AI object holds −1 in the first two fields; the vehicle set-up resets them to 0.

## Behaviour

### Part A — helicopters

#### 8. Per-frame update and action execution
Each think (every 0.1 s):
1. **Dead**: when the entity's dead flag is "dead" (value 2, set by death, §12.4): movetype 6, the shatter effect bit
   (entity effects 0x400, SPEC-400 "shatter") is cleared, the current-action id is reset and the vertical velocity is
   zeroed; when the global crash-stage timer has passed, the current crash stage runs (§12.4). Nothing else happens.
2. `ai_freeze` ≠ 0: nothing happens (the helicopter hangs in place).
3. **Free fire** (toggled by use, §14): while it is on, one heavy-bullet round (attack 28) is fired every frame from
   the gun barrel with muzzle effect `weapons/othermz/heli` on bolt `flash_barrel`; direction = barrel forward ×
   100 with integer jitter irand(−10, 10) added to two of its components (the third random value is drawn and
   discarded — observable only as spread).
4. The common character update runs (senses etc., SPEC-700 §7).
5. **Crew**: if the gunner is dead the crew reaction (§12.2) runs with code 1; if the pilot is dead with code 2;
   if both, with code 3 and then 2.
6. **Actions**: with no current action and a non-empty queue the next action starts. The current queue size is
   remembered (used by auto-combat, §11). The current action runs (SPEC-711 A8 execution, details below); when it
   reports completion the next action starts and the command slot is cleared.
7. The entity is relinked; Quake II ground and water checks run (M_CheckGround when the link count changed,
   M_CatagorizePosition, M_WorldEffects).
8. **Removal**: when the *remove* flag is set and the AI's removal check agrees (UNVERIFIED condition), the death routine (§12.4) runs
   with the remembered killer; then, if the entity has a `killtarget` that is not the string `musicscript`, the
   helicopter is teleported to (−1000, 744, 986) with velocity (−10, 40, 0) and roll −10 and the crash stages start
   1 s later; otherwise the entity is freed at once. (The teleport is a map-specific effect: a scripted wreck
   falling at a fixed place; UNVERIFIED which map relies on it.)

**Running a helicopter action** (each frame):
1. Without a body nothing happens and the action is not complete.
2. If a routine is set: the action's V, S and E are copied into the command slot; a pending routine (set by the
   previous frame's routine) replaces the current one; the routine runs; the damage smoke is updated with
   health / max health (SPEC-722). If the routine did not set the *routine done* flag the action continues, except
   in the "killed but not yet crashing" case (next step).
3. When the routine is done, or there is no routine, or the helicopter is killed but not crashing:
   - killed, not crashing and within 0.1 of its home point on every axis (compared as |origin| − |home| per axis):
     the remove flag is set and the action completes (a helicopter killed while parked at its spawn point simply
     explodes, §12.4);
   - otherwise the **autonomous selector** runs; if it selects a mode the action continues with that routine,
     else the action completes. The routine-done flag is cleared.

**Autonomous selector** (in this order; the first match installs its routine):
1. The "combat just ended" flag is cleared. If auto-combat is on and either (the auto-combat end time has passed
   and the AI has queued actions waiting) or the helicopter is killed: auto-combat, go-to and attack flags are
   cleared and "combat just ended" is set.
2. Killed and not yet crashing → crashing is set. With spawnflag 2: *go to* a clear spot near the exit point
   (§9.11) — the helicopter flees there and is removed on arrival (§9.2 exit leg). Without spawnflag 2: exit point
   := −(right vector) with z = 0, crash start := now + 2 s, crash spin routine (§12.3).
3. Goto flag → go-to routine (§9.4).
4. Land request → land routine (§9.6); the request is cleared.
5. Attack flag → attack routine (§13.1).
6. Hover flag → hover routine (§9.7).
7. Auto-combat → one combat step (§11).
8. Otherwise nothing is selected.

#### 9. Flight routines
Movement is done by moving the origin directly (no physics) except while crashing. Every routine sets the next
think to now + 0.1.

##### 9.1 Take off (id 1)
- With spawnflag 1 the routine is done at once.
- First frame: both rotors (main and tail bolt-ons) play `rotor_start` once; the helicopter waits until the main
  rotor's sequence ends.
- Then both rotors play `rotor_anim` looping and a *leg* (§9.2) starts from the current origin to
  origin + (0, 0, 224).

##### 9.2 Fly a leg (start P0, destination P1)
The leg midpoint M = (P0 + P1)/2. Each frame (speed v starts at 0):
- The pilot's head looks at P1 + normalise(P1 − P0) × |helicopter size| (§9.9). The gun aims: at the attack point
  when attacking; at the point 500 units ahead when not firing while flying; when firing while flying (both the
  fire-while-flying and gun flags set), along the horizontal forward direction tilted down (vector
  (fx, fy, −1) before normalisation; UNVERIFIED exact tilt).
- **Arrival**: when |origin − P1| ≤ v on every axis: origin := P1, v := 0, then the *arrive* routine (§9.8)
  follows; on an *exit leg* (destination = exit point, §9.4) the routine instead sets the remove flag and is done
  (the helicopter vanishes through the death routine at its exit point).
- **First half** (not past M, or exit leg): v += 8/3 per frame. Pitch target = 45 × (1 − q) where q = horizontal
  distance covered from P0 towards M / horizontal |M − P0| (q = 0 at the start); pitch moves towards the target by
  at most 1.5° per frame.
- **Second half** (past M: on every axis where M and P1 differ by more than 1 the origin is on P1's side of M):
  v −= 8/3 per frame, minimum 2. Pitch target = −15 × (1 − r) where r = horizontal |P1 − origin| / horizontal
  |P1 − M| (nose up as the helicopter brakes); 0 when v < 4; pitch (normalised to (−180, 180]) moves towards it by at
  most 0.75° per frame.
- Move: origin += normalise(P1 − origin) × min(v, 80) (exit legs: no cap, so the helicopter keeps accelerating).
- Turn and bank towards P1 (§9.3, without bank control from the leg; returns the yaw error).
- **Fire while flying** (flag set): when the yaw error is ≥ 20° or v ≤ 8 the barrel stops; otherwise the gun fires
  one round (§13.2, if the gun is on) and, if pitch > 0, rockets fire at the normal rate (§13.4).

##### 9.3 Turn and bank towards a point (yaw rate 9° per frame)
- If the helicopter is exactly above/below the point (same x and y): no turn; facing.
- Target yaw = yaw of (point − origin). If the error is ≤ 9° the yaw snaps to the target: facing. Otherwise the
  yaw changes by 9° in the shorter direction (anglemod) and the absolute error is reported.
- **Bank** (only in the turn routine, §9.8, and when requested): with A = the turn between the leg-start yaw and
  the target yaw folded into [0, 180], h = A / 2, B = the turn made so far (folded likewise) and
  p = |B − h| × 100 / h: while B < h the roll target is 0.4 × p, during the rest 0.16 × p, with opposite signs for
  the two halves and for the two turn directions (turning towards increasing yaw: −0.4p then +0.16p). Roll moves
  towards its target by 2° per frame and snaps when within 2°.
- With fire-while-flying, during a banked turn: while p < 90 the gun fires (if on) and rockets fire at the normal
  rate; otherwise the barrel stops.

##### 9.4 Go to (goal G)
- At G (all components equal): done; go-to flag cleared.
- Straight path (trace from the origin to G with the movement box, §10) clear: fly a leg to G. If the exit point
  is non-zero and G equals it, the leg is an *exit leg* (§9.2).
- Blocked: the helicopter climbs to the **cruise altitude** C = min z + 0.75 × (max z − min z) of the flight bounds
  (5000 with the default bounds). It tries, in order: the cruise point above the obstruction seen from the goal
  side (when the reverse trace G → origin is also blocked: the point of that trace's hit, at height C); the cruise
  point above the near obstruction (hit point of origin → G, at height C); if it is already exactly there, with
  probability 1/2 it gives up (done, go-to flag cleared) and otherwise flies to a random wander point (§9.10); the
  point straight above itself at height C; finally a random wander point. The first of these reachable by a clear
  trace becomes the next leg. The go-to flag stays set, so after the leg the selector resumes the go-to.
- Goals from commands and combat are first moved to a clear spot (§9.11).

##### 9.5 Strafe (ids 22/23, combat pattern 0)
- Set-up: destination = origin + right vector × d (d < 0 = left). If it is outside the flight bounds the offset is
  halved up to 10 times; if still outside, the routine is done. The destination is cut back to the hit point of a
  trace from the origin.
- Each frame: the pilot looks at the destination; the gun aims 500 units ahead. Arrival as in §9.2 (snap, v = 0,
  barrel stops, arrive routine). First half: v += 16/3 per frame, roll target ±40 × (1 − q) (q as in §9.2; sign
  + for right, − for left); second half: v −= 4 per frame, minimum 2, roll target of the opposite sign 16 × (1 − r);
  roll moves 3.5° per frame. Move as in §9.2 with the 80 cap. When v ≤ 20 the barrel stops. Every frame the gun
  fires (if on) and rockets fire at the normal rate (if on).

##### 9.6 Land (after id 2)
- First the helicopter levels out (§9.8 rules). Then per frame it descends by s = (z − home z) × 0.0446428571
  (= height / 22.4), minimum 2; when the remaining height is less than s it snaps to the home point.
- At home: both rotors play `rotor_end` once; when the main rotor sequence ends they switch to `rotor` (stopped)
  and the *finish* routine runs: done, unless the helicopter is killed, in which case the remove flag is set.

##### 9.7 Hover (ids 33/34, combat)
Hover lasts until the hover end time (now + S for ids 33/34). Each frame:
1. Once a non-engaging hover has started its fuselage sequence (step 3) the routine waits for that sequence's end
   notice before doing anything else. In 1.07f this notice is never connected to the model, so a **non-engaging
   hover (id 33) never ends** by itself: the helicopter bobs in place until it is killed (dying still works through
   the action rules of §8). UNVERIFIED in game; no stock script uses id 33.
2. Hover time over, killed, or combat just ended: hover flag cleared; level out and finish (§9.8).
3. Non-engaging hover: the main model plays its `cobrass` sequence once (the same name is used for the Hind;
   UNVERIFIED that the Hind model has it) and the routine starts waiting (step 1).
4. Engaging hover: if rockets or gun are on, a target exists (§11.1) and a trace (mask 0x19) from the origin to the
   target's eye (origin + view height) is clear — and, outside auto-combat, the target lies in the forward cone
   (cosine ≥ 0.173, about 80°) — an **attack** starts: attack point = gun aim point = target origin, attack time 1;
   with rockets on, rockets left and hover or auto-combat active, the attack time becomes −1 (probability 5/11) or −2
   (6/11) (a rocket salvo, §13.1). Otherwise the helicopter picks a random look-around point
   origin + (U(−1000, 1000), U(−1000, 1000), 0) and turns towards it (§9.8 turn).

##### 9.8 Turn, level out, arrive
- **Turn routine** (face commands, combat facing): the gun aims at the attack point when attacking, else 500 ahead;
  the pilot looks at the destination; turn and bank (§9.3) with bank and fire-while-flying handling; when facing
  the *arrive* routine follows.
- **Level out**: roll moves to 0 by 4° per frame (snaps near 0); the pilot looks 100 units ahead; with
  fire-while-flying and gun on the gun aims 500 ahead; pitch (normalised to (−180, 180]) moves to 0 by 1.5° per
  frame (snaps within 1.5). Level when roll was already 0 and pitch reaches 0.
- **Arrive**: with fire-while-flying and gun on the barrel stops; when level the routine is done.

##### 9.9 Pilot look and gun aim (ids 16–21)
- The pilot's head and the gun barrel are bolt-ons turned towards a point with limits per update (radians):
  pilot head yaw −0.3927 … 0.7854, pitch −1 … 1, rate 0.314159; gun base yaw −1 … 1 (pitch fixed), gun barrel pitch
  −0.3927 … 0.7854 (yaw fixed), rate 0.314159 (the gun first turns the gunner's head the same way). The look offsets
  of the crew heads are (0, 0, 6.411) gunner and (22, 0, 12.935) pilot. The turning rule itself (rate cap, limits,
  dead zone, results aligned / turning / limited) is the aim step shared with the ground vehicles, Part B §9.1;
  "reached" = aligned.
- The look/aim routines repeat each frame until the bolt-turn helper reports "reached", then are done.

##### 9.10 Random wander point
origin + (U(−2500, 2500), U(−2500, 2500), U(−250, 250)), clamped into the flight bounds, z raised to at least
min z + 112, then cut back to the hit point of a trace from the origin (movement box rules). One shared point for
all helicopters (global; UNVERIFIED observable effect).

##### 9.11 Clear spot
A goal is checked by a trace from the goal to the helicopter's origin with the movement box. If it starts in a
solid, candidates goal + r × D are tried for D in the 18 directions (0,0,1), (−1,1,0), (0,1,0), (1,1,0), (−1,0,0),
(1,0,0), (−1,−1,0), (0,−1,0), (1,−1,0), (0,0,−1), (−1,1,1), (0,1,1), (1,1,1), (−1,0,1), (1,0,1), (−1,−1,1),
(0,−1,1), (1,−1,1) (not normalised) with r = 100, then 200, … (r grows after each full round); a candidate must lie
inside the flight bounds, must not start solid towards the origin and must be reachable (non-zero fraction) from the
origin. After 100 candidates the search continues around the helicopter's own origin instead of the goal.

#### 10. Flight bounds, navigation box and waypoints
- **Flight bounds**: one box shared by all helicopters, default (−10000, −10000, −10000) … (10000, 10000, 10000), set
  only by ids 35/36. It is **not reset** on level change (a later map inherits the previous map's bounds within the
  same game session — UNVERIFIED in game; verify by playing a map that sets bounds, then another helicopter map).
- **Movement box**: by default movement traces use the navigation box (−r, −r, −h) … (r, r, h) with
  r = max(|min(mins x, mins y)|, max(maxs x, maxs y)) and h = 2 × max(|mins z|, maxs z) of the entity box: 280 and
  140 for the stock box. Id 40 with S = 0 switches to point traces, S ≠ 0 back to the box. Traces use content mask 3.
- **Waypoints** (per helicopter, ids 42–45): an ordered list. A *point* waypoint has only an origin; a *bounded*
  (area) waypoint also has a box. Id 43 bounds the last point waypoint with extents E: its box becomes
  (origin − E − mins) … (origin + E − maxs) using the helicopter entity's mins/maxs, i.e. the region of origins for
  which the whole helicopter stays inside origin ± E; it then counts as an area waypoint instead of a point waypoint.
  (A size check of E against the helicopter size exists; its effect is UNVERIFIED.) Counts kept: total, point
  waypoints, area waypoints. Waypoints are saved with the AI (§6).
- Combat uses point waypoints as vantage points and area waypoints as patrol zones (§11).

#### 11. Auto-combat (id 41) and combat patterns
Auto-combat runs while its flag is set; it ends (selector step 1) when its end time (now + S) has passed and further
script actions are queued, or when the helicopter is killed. Each combat step:
- If no pattern is active: the target is re-chosen (§11.1); a pattern 0–3 is drawn as irand(0, 4) with 4 folded
  to 0 (so P(0) = 2/5, P(1) = P(2) = P(3) = 1/5); if it equals the previous pattern the next one (mod 4) is taken;
  the step counter is reset and the routine is done (the selector immediately starts the pattern's first step).
- Otherwise the active pattern runs one *interjection test* (§11.2) and then, if no interjection happened, its next
  step. A pattern ends (back to "no pattern") after its last step, or when there is no valid target.

##### 11.1 Target choice
The combat target is kept for 30 s. When that time has passed, or the target is gone (not in use or cannot take
damage), a new target is chosen: the lowest-numbered client entity (1–32) that is in use and can take damage
(single player: always the player). If none exists there is no target.

##### 11.2 Interjection test (every combat step, before the pattern step)
Aggression a = the entity's `volume` key (float, default 0).
1. No valid target → the pattern ends.
2. Draw n = irand(0, 10). If n ≥ a → no interjection (continue the pattern).
3. Draw n again. If n ≥ a and (the target's eye is not visible — trace mask 0x19 — or not in the forward cone of
   cosine 0.173) → no interjection.
4. Interjection, 50/50:
   - **Attack** (irand(0, 1) = 0): with rockets on and rockets left: attack point = target eye + (U(−10, 10),
     U(−10, 10), 0), attack time −1 (irand(0, 10) ≥ 5) or −2, then possibly redrawn as in §9.7 (−1 with
     probability 5/11 when hovering or in auto-combat); without rockets: attack point = target eye, attack time
     U(2, 4) s (gun burst length). Gun aim point = attack point; attack flag set; attack routine.
   - **Reposition** (otherwise): requires point waypoints (else hover-engage, below) and a further draw
     irand(0, 10) < a (else hover-engage). Among the point waypoints it finds: the nearest to the target from which
     the target's eye is visible, the nearest to the target from which it is not, and, among the visible ones, the
     one nearest to the helicopter (excluding its current position). Let D = distance helicopter → target.
     If the helicopter itself sees the target: when a visible waypoint exists and its target distance + 800 < D,
     go to it; otherwise hover-engage. If it does not see the target: go to the visible waypoint nearest to the
     helicopter (only in the degenerate case that the helicopter is exactly at the target's position: the visible
     waypoint nearest the target); if no
     waypoint sees the target, go to the hidden waypoint nearest the target only when that distance + 800 < D,
     else hover-engage.
   - **Hover-engage**: engaging hover (§9.7) for U(1, 3) s.
   Any interjection replaces the pattern step of this combat step; the pattern itself stays active.
   (Aggression 0, the default, therefore never interjects: such a helicopter only runs its patterns.)

##### 11.3 Patterns (one step per combat step that is not interjected)
| pattern | steps |
|---|---|
| 0 strafing run | 0: find the area waypoint whose origin is nearest the target; only if the target's z is inside its box: pick a horizontal axis at random (the other one if the target is not inside the box's range on the first) along which the target is inside the box; strafe distance s = min(distance from the target to either box face on that axis, 800); if s > 100: go to the target's position moved onto the nearer box face of the other axis (side 0–3 recorded), offset by s along the side's direction (±x or ±y), sign of s random (irand(0, 10) > 4 → negative). 1: face along the side's patrol direction (a point 1000 units away). 2: strafe by 2 × s. 3: end. |
| 1 vantage point | 0: if no point waypoints: end; else go to point waypoint irand(0, 100) mod count. 1: face the target's origin. 2: engaging hover for U(1, 3) s. 3: end. |
| 2 patrol | 0 and 2: if there are area waypoints, go to a uniformly random point inside area waypoint irand(0, 100) mod count. 1 and 3: engaging hover U(1, 3) s. 4: end. |
| 3 zone | (target re-checked first) 0: choose area waypoint k = irand(0, 100) mod count and go to a random point inside it. 1, 2: go to another random point inside area k. 3: engaging hover for 2 s. 4: end. |

Pattern 0 geometry is complex; the step list is reliable, the exact face/offset choice is UNVERIFIED (verify with
a test map that has one area waypoint and watch the strafe line).

#### 12. Damage, crew, crash and death

##### 12.1 Damage
Helicopters take damage through their own handler:
- Means of death 17 (C4) is ignored entirely.
- With a body present: damage 0 is ignored; damage whose flags include PROJECTILE (0x200) or 0x400 is first passed
  to the helicopter body's hit handler (SPEC-720 §12 "Helicopter": hull ×0.1, crew/sniper rules, ricochets), which
  returns the damage actually applied. Damage without those flags (explosions, area damage, script kills) is applied
  in full.
- health −= damage. If health stays above 0 and the hit was routed through the body: the damage smoke is updated
  (pain reaction, SPEC-722 smoke bands).
- At health ≤ 0: the attacker is remembered as killer, the entity gets "no knockback", the helicopter is marked
  *killed*, it can no longer take damage, and it thinks again in 0.1 s. The autonomous selector then makes it crash
  (§8 selector step 2) — or, at its home point, explode at once.

##### 12.2 Crew killed (sniper kills of pilot or gunner, SPEC-720)
The body marks the gunner and/or pilot as dead. Each frame while a crew member is dead (§8 step 5):
1. Movetype 6. If the helicopter is already crashing nothing else happens.
2. All queued actions and the current action are dropped. Unless the helicopter is exactly at its home point, a
   crash command (id 49) is queued and the helicopter is marked *crashing* (a helicopter whose crew is shot while it
   is parked on its spawn point stays put).
3. Unless `lock_blood` is set: the canopy gets blood variant = reaction code (1 gunner, 2 pilot, 3 both; SPEC-722)
   and the dead crew member's head animation is stopped (both heads for code 3).

Spawn-time rule: crew sniper kills are allowed unless spawnflag 16 is set and the toughness level is ≥ 2.

##### 12.3 Crash spin (id 49, and death without spawnflag 2)
Movetype 6 (velocity-driven, SPEC-760 §2). Per frame: pitch += 2 while pitch < 20, else −2 (oscillates about 20°
nose down); vertical velocity += max(−10, −4 × (now − crash start)) (with crash start = now + 2 s the helicopter
first rises for about 2 s, then falls with growing acceleration capped at −10 units/s per frame); horizontal
velocity += forward (x, y only) × (−2.5 × pitch); yaw angular velocity += 45, capped at 180°/s. When the crashing
helicopter touches anything (§14) the remove flag is set and it is destroyed (§12.4).

##### 12.4 Death and explosion
The death routine (called on removal):
- health below −999 is clamped to −999.
- If the entity has a `killtarget`, every entity whose `targetname` equals it is used (activator = the helicopter).
- If not already dead: the global count of killed characters increases; all senses, actions and decisions are
  removed; the dead flag is set (2); the touch reaction is cleared; angles are kept; the entity is relinked.
- The shatter bit (effects 0x400) is set and effect `heli_expl` plays at the origin.

The **crash stages** (used by the `killtarget` teleport of §8 step 8; each stage runs when the global stage timer
has passed, checked every 0.1 s):

| stage | effect | next stage after |
|---|---|---|
| 1 | both crew heads' animations stop; effect `weapons/world/autogundeath` on bolt `BODY1`; sound `impact/explosion/med.wav` | 0.2 s |
| 2 | `weapons/world/autogundeath` on bolt `ROTOR_MAIN`; sound `impact/explosion/exp1.wav` | 1.0 s |
| 3 | `heli_expl` at the position of bolt `ROTOR_MAIN1`; shatter bit set; damage smoke removed; the entity is freed at its next think (0.1 s) | — |

The stage timer is one global value shared by all helicopters (two wrecks at once interfere — UNVERIFIED in game).

#### 13. Weapons

##### 13.1 Attack routine (ids 28–30, hover-engage, interjections)
State: attack point, gun aim point, attack time t (> 0: seconds of gun fire; −1/−2: rocket salvo of 1/2 rockets).
- **Rocket attack** (t < 0, or rockets on with rockets left): turn towards the attack point (no bank). While not
  facing, the turn routine runs. When facing:
  - t = −1 or −2: elevation e = atan(|Δz| / horizontal distance) in degrees, positive when the point is below. If
    |e| < 40: *rocket dip* (§13.5) with dip pitch e × 0.1; attack and dip-phase flags cleared.
  - otherwise, if the helicopter is not at the attack point's height: fly a vertical leg to (x, y, attack z); if
    that is blocked, to the hit height + 40 (cut at the next hit). The attack height becomes that z.
  - at the same height: hover-fire (below).
- **Gun attack** (t ≥ 0 and no rockets): the gun aims at the attack point. While it is still turning nothing else
  happens; if the point is outside the gun's limits the helicopter turns to face it (done, attack flag cleared, when
  facing); when the gun is on target: hover-fire with end time now + t (t = 0: a single frame), attack flag
  cleared.
- **Hover-fire**: turn towards the attack point without bank; the pilot looks at it; the gun aims at the gun aim
  point; the gun fires each frame (if on); rockets fire at the normal rate (§13.4). Salvo mode (t < 0): t += rockets
  fired; ends when rockets run out or t reaches 0. Time mode: ends when now > end time. It also ends when the
  helicopter is killed. At the end the barrel stops and the routine is done.

##### 13.2 Gun (attack 28, "heavy bullet", SPEC-782)
One round per call (callers fire at most once per frame): the barrel bolt-on plays `barrel` looping (started once;
`barrel_still` when it stops), muzzle effect `weapons/othermz/heli` on bolt `flash_barrel`, direction = barrel
forward × 100 plus a jitter of irand(−10, 10) on each component — **no jitter when the command slot scalar is
exactly 1337** (scripts use S = 1337 for a precise scripted gun run, e.g. the Iraq 2b outro) — attack 28 fired from
the barrel's world position, sound `weapons/assault/fire.wav` (volume 0.9, attenuation 0.5) on channel 1. A burst
mode (bursts of irand(2, 10) rounds with pauses) exists but no caller enables it.

##### 13.3 Fire while flying (ids 31/32)
While on, legs (§9.2), banked turns (§9.3) and strafes (§9.5) fire the gun and rockets as described there.

##### 13.4 Rockets
- 8 rockets at spawn ("rockets left"); id 4 reloads to 8; with spawnflag 4 the count is reset to 8 whenever it
  reaches 0 (unlimited).
- Normal rate: one rocket when rockets are on, rockets are left and now > next-rocket time; next-rocket time :=
  now + 1 s. A forced volley of n rockets ignores the timer.
- Firing order: with n rockets left, odd n fires from the left pod (bolt `FLASH_ROCKET1`), even n from the right
  pod (`FLASH_ROCKET2`), tube k = (n + 1) / 2 (integer): tubes 4, 4, 3, 3, 2, 2, 1, 1. Tube offsets from the pod
  bolt position (R = right vector × 5, U = up vector × 5): tube 1 +R+U, tube 2 +R−U, tube 3 −R−U, tube 4 −R+U.
- Aim: straight at the combat target's origin (the target of §11.1; chosen if none) — not at the attack point.
- Rocket entity: movetype 10, solid box (−4, −2, −4) … (3, 2, 4), clip mask 0x6000081, speed 600 along the aim,
  angles from the aim, health 100, owner = the helicopter, a value 0.02 in the air-friction property (SPEC-760 §1;
  UNVERIFIED which property), Ghoul model `Weapon/Projectiles/Rocket` `Rocket`, persistent effect
  `weapons/world/rockettrail2`; removed after 10 s.
- Rocket touch: on a sky surface it just disappears. Otherwise: linear area damage 130, radius 150, attacker = the
  helicopter, the rocket itself ignored, means of death 0 (SPEC-720 §14.1); AI noise alert (150, 250); effect
  `weapons/world/rocketexplode`; camera shake (100, 300, 30) at the impact point; removed.

##### 13.5 Rocket dip
The pilot looks at and the gun aims at the attack point; the gun fires each frame until both phases are done (then
the barrel stops). Phase 1: pitch moves towards the dip pitch by 5° per frame; within 5° it snaps, fires a forced
volley of trunc(−t) rockets (1 or 2) and the dip pitch becomes −½ of itself. Phase 2: pitch moves towards the new
dip pitch by 5° per frame; within 5° phase 2 ends; the next frame the helicopter levels out and finishes (§9.8
arrive).

#### 14. Use and touch
- **Use** (trigger): toggles *free fire* (§8 step 3) and the barrel spin.
- **Touch**: while crashing, any touch sets the remove flag (the wreck explodes on impact).

### Part B — tank and snowcat

#### 5. First think
The entity's think callback (run once, SPEC-760 B4.2) sets the next think to level time + 0.1 and gives the AI one
idle decision (class 1, priority 0, SPEC-710). Vehicles get no senses and never consult decisions afterwards.

#### 6. Vehicle think (replaces the character think of SPEC-700 rule 7)
Each think, in order:
1. Nothing happens while cvar `ai_freeze` ≠ 0 (the think is not re-armed either).
2. **Disabled** (§7): the body's moving state is switched off; the tank also clears its looping sound; the snowcat
   sets its next think to 0. The think ends **without** scheduling another think, so the vehicle stays idle until
   something else schedules a think (only a lethal hit does, §13).
3. **Automatic machine gun**: when the machine gun is on, the body's machine-gun hit points are > 0 and (snowcat only)
   player 1 does not have notarget, the guns are aimed at player 1's origin (§9.3 / §9.4); if the aim step reports
   "on target", that origin becomes the machine-gun target point and the fire rule of §11 runs.
4. Next think := level time + 0.1. The (empty) sense list is updated.
5. If no command is running and the queue is not empty, the next command starts. If a command is running, its
   identifier is recorded and its step runs (SPEC-711); when the step reports finished the next command starts and
   the body's command parameters (entity, vector, scalar) are cleared.
6. The entity is relinked; ground check when the ground entity's link count changed, position categorisation and
   world effects (SPEC-700 "Character helper rules").
7. (Dead rule: when the never-set removal flag is on and every part is ready to be removed, the death of §14 runs
   with the killer as inflictor and attacker and the max health as damage, and the entity is freed.)

#### 7. Disabled state and use
- The body's disabled flag is set by spawnflag 1 at spawn, by the tank's use, and by every death of §14.1.
- Tank use: bit 1 of the entity's spawnflags is toggled and the disabled flag is set to the new bit. Because the
  spawn replaced the spawnflags by 2, the first use always **disables** (bit becomes 1) and the second enables — a tank
  spawned with spawnflag 1 needs two uses before its disabled flag clears. Use never schedules a think, so a vehicle
  that has stopped thinking (§6 step 2) stays inert even after its flag is cleared (UNVERIFIED in game; see open
  question 1).
- The snowcat has no use.

#### 8. Driving (command 1)
Each step:
1. A box trace (the vehicle's bounding box, content mask 3, ignoring the vehicle) from the vehicle's origin to the
   destination. If it is
   blocked the step reports "not finished" and nothing else happens (the command waits, possibly for ever).
2. Path clear: the looping sound `Ambient/Models/Tank/tanklp.wav` is set (sound data 0xFB: volume 248, attenuation
   class 3; SPEC-202), for both vehicles.
3. **Turn**: the wanted yaw is the direction from the origin to the destination (for reverse drive the opposite
   horizontal direction). With yaw normalised to [0°, 360°) and difference d = wanted − current (current taken in
   (−180°, 180°]): if |d| < 1.5° the yaw is set exactly to the wanted yaw, the yaw angular velocity is set to 0 and the
   turn is complete; otherwise the yaw angular velocity is set to +turn rate when d (taken modulo 360°) is below 180°,
   else −turn rate, and the step ends "not finished". The pusher physics applies the angular velocity.
4. **Drive** (only once the turn is complete): when the horizontal distance to the destination is below 5 units on
   both x and y, the velocity is set to 0, the body's moving state switches off and the step reports finished.
   Otherwise, if the velocity is exactly zero, it is set to the forward direction × drive speed (negated for reverse
   drive) and the body's moving state switches on; the step reports "not finished". (The velocity is set once and
   not corrected while driving; a box trace that is blocked at this point switches the moving state off and reports
   finished without clearing the velocity — not reachable in practice because step 1 uses the same trace.)
The reverse flag is updated at every step from the command's scalar (= 1 → reverse).
Speeds: tank 20°/s and 50 u/s, snowcat 30°/s and 70 u/s.

#### 9. Aiming
**9.1 Aim step (generic rule, also used by the helicopter).** A rotating part hangs on a parent part. Each step the
target point is expressed in the parent's frame as yaw (about the vertical axis) and pitch, relative to the part's
rest orientation. Only the enabled axis changes. Each axis has limits [min, max]; a wanted angle outside the limits
(tolerance 0.0005 rad) is clamped to the limit and the step result is **limited** (3). The rotation per step is capped
at the rate; a capped step gives **turning** (2) unless limited. Otherwise the result is **aligned** (1); a remaining
change below 0.001 rad on both axes also counts as aligned when not limited. Wanted changes of at most 0.01 rad are
ignored (dead zone). Missing parts or no enabled axis give result 3. (Exact tolerance interplay UNVERIFIED; verify by
logging turret angles.)

**9.2 Tank cannon aim** (commands 2 and 5). Target = command point raised by 12 units.
1. Turret: yaw only, limits ±4000 (none), rate 0.04.
2. Cannon: pitch only, rate 0.03, limits [p, 0.2] where p = −0.05 when the turret's resulting yaw (relative to the
   hull) lies in (−0.13, 0.13), (0.8, 2.4) or (−2.4, −0.8), otherwise p = −0.02 (keeps the barrel clear of the hull).
3. Result: **aimed** when both steps are aligned; **as aimed as possible** when both are aligned or limited and at
   least one is limited, or when a part is missing; otherwise **not yet**. Commands 2 and 5 proceed on aimed or as
   aimed as possible.

**9.3 Tank machine-gun aim.** Requires T ≠ 0 (at toughness 0 the machine gun never aims, so it never fires).
Target = player 1's origin raised by 20 units. The invisible mount yaws (no limits, rate 0.05), then the machine gun
pitches within [−0.314159, 0.785398] (−18°…+45°, rate 0.05). On target only when both steps are aligned.

**9.4 Snowcat gun aim** (automatic fire and commands 2/5). Requires T ≠ 0. Target raised by 20 units. The turret yaws
(no limits) at 0.06 + 0.01·T; then each gun pitches at the same rate within [p, 0.785398], where p = −0.3 when the
turret's yaw is in (−0.5, 0.5), −0.2 when it is in (1.0, 1.65) or (−1.65, −1.0), otherwise −0.1. On target when the
turret and **gun 2** are aligned (gun 1's result is ignored). For commands 2 and 5 "on target" is required (limited
results do not count) — so a snowcat ordered to aim at a point outside its pitch range never finishes the command
(UNVERIFIED).

#### 10. Tank cannon and shell (command 2 after aiming)
1. Firing (once): temp entity 56 (large explosion) at the tank's origin with size byte 1 and mode 2 (sound only);
   effect `weapons/othermz/tank` on the cannon's `flash_canon` bolt; the cannon plays `canon` once, the turret plays
   `turret` once, the hull plays `tank_fire` once when moving or `tank_still` once when standing (SPEC-011 mode
   "once and hold"); the "fired" flag of §16 is set.
2. Shell trace: a line trace (content mask 3, ignoring the tank) of 4096 units from the `flash_canon` bolt along its
   direction — characters are not hit by it (UNVERIFIED), so the shell explodes at the first solid surface behind
   them. Nothing hit → the command finishes at once, without an explosion. Otherwise the remaining flight
   distance is (trace fraction × 4000) and the impact point is the trace end.
3. Flight: on this and every following think, a remaining distance > 500 is reduced by 500 and the command waits;
   otherwise it becomes 0 and the shell explodes (so the shell travels 500 units per think, 5000 u/s; note the 4000 vs
   4096 scale: a hit at distance D explodes after ⌈(0.977·D − 500)/500⌉ further thinks, 0 when 0.977·D ≤ 500).
4. Explosion at the impact point + (0, 0, 10): effect `weapons/world/rocketexplode`; linear radius damage (SPEC-720
   Behaviour, area damage 1) with damage 130, radius 150, means of death 0, flag EXPLOSIVE (0x8000), attacker = the
   tank, inflictor = a temporary entity at the point (owner = tank, freed at once); noise broadcast loudness 150 radius
   250 (SPEC-700 "helicopter, tank"); camera shake around the point: intensity 100, radius 300, parameter 30 (one tick
   of the SPEC-761 §3.12 rule). Then the command finishes.
The cannon is not used automatically; only command 2 fires it.

#### 11. Machine-gun fire (automatic, §6 step 3)
Tank, per think while on target:
1. The muzzle is the machine gun's `flash_machine_gun` bolt (position and direction).
2. Line-of-fire check (always enabled): a line trace (content mask 3, solid and window) from the muzzle to the target
   point; when its fraction is below 0.95 and the hit entity is the world (or none) or a character on the tank's team
   (team 1), nothing is fired this think. A hit on any other entity (for example a door) does not block. With mask 3
   characters are normally not hit by this trace (UNVERIFIED), so in practice only level geometry blocks.
3. Bursts: let t = level time − burst start and L = 0.2 + U(0, 0.2)·T (drawn anew every think). t ≤ 0: waiting, no
   shot. 0 < t < L: one shot. t ≥ L: the next burst starts at level time + U(0.3, 0.6)·(5 − T), no shot.
4. A shot: effect `weapons/othermz/machinegun` on the muzzle bolt; the direction gets a uniform random offset in
   [−s, s] on each axis with s = (5 − T)·0.03; attack 12 (SPEC-782 machine-gun bullet) from the muzzle.
Snowcat differences: muzzles `_flash1` of gun 1 and gun 2; the line-of-fire check blocks only when the traces from
both muzzles hit something (fraction < 1, same mask); a shot fires two attack-12 bullets: the first from gun 1's
muzzle when gun 1's line is clear, otherwise from gun 2's, along that muzzle's direction with the random offset
(s = (5 − T)·0.05); the second always from gun 2's muzzle along its exact direction (no offset). The muzzle effect
plays on both guns.
Effective rates (one shot per think; the first think of a burst comes 0–0.1 s after its start): 1–3 shots per burst
at T = 1, up to about 10 at T = 4; pauses 1.2–2.4 s at T = 1, 0.3–0.6 s at T = 4. The first burst after switching on
starts only after one pause. The command 3 scalar only switches the automatic fire.

#### 12. Commands 4 and 5
Command 4 runs the vehicle's own death (§14.1 for the tank, §14.2 for the snowcat) with inflictor none, attacker =
the vehicle, damage 10000 and the world origin as the damage point. Command 5 is §9.2/§9.4 without firing.

#### 13. Damage and pain
For every damage call:
1. When the attacker is a player whose current weapon is the knife (weapon 1): effect `environ/wallspark` at the hit
   point and `impact/surfs/metal1.wav`…`metal3.wav` (random, channel 2) on the vehicle. (The body then makes the knife
   hit 0, SPEC-720.)
2. With a body, damage 0 does nothing. Hits carrying damage flag 0x200 or 0x400 go through the body's hit handler
   (SPEC-720 "Tank and snowcat"), which may change the damage; a result of 0 does nothing. Hits without those flags
   (explosions, area damage) do their full damage.
3. Health −= damage. Health still > 0: hits with flag 0x200 or 0x400 call the vehicle's pain (which only updates the
   body damage state — no visible effect for these bodies) and nothing else happens.
4. Health ≤ 0, **tank with a `killtarget`**: every entity whose `targetname` equals the killtarget is used (other and
   activator = the tank) and the tank does **not** die — it keeps driving, aiming and firing. Every further hit that
   leaves health ≤ 0 uses those entities again. (Stock `irq2b`: the killtarget is a `script_runner` whose script sends
   command 4.)
5. Health ≤ 0 otherwise: the killer is recorded, the no-knockback flag is set; snowcat: takedamage is switched off.
   If the vehicle was not already dead the death of §14.1 runs (the snowcat also uses §14.1 here). The next think is
   scheduled at level time + 0.1 (it then finds the vehicle disabled and stops, §6 step 2).

#### 14. Death
**14.1 Wreck** (tank death; snowcat death from damage):
1. Health below −999 is raised to −999; the looping sound is cleared.
2. If the vehicle has a `killtarget`, its targets are used as in §13 step 4 (this happens again on a self-destruct
   of a tank whose killtarget already fired).
3. The vehicle's enemy becomes the attacker.
4. First death only: the "characters killed" counter grows by 1; all senses, decisions, queued and current
   commands are removed; the touch callback is removed; deadflag := dead; the AI keeps the current angles as its death
   angles.
5. With a body: velocity and angular velocity are zeroed; temp entity 44 (large explosion with sound) magnitude 255
   at the origin; flash blinding (SPEC-761 §7.3) radius 5000, maximum 0.9, duration 0.5 s; moving state off; the
   turret, cannon and machine gun (snowcat: turret, both guns) have all mesh parts hidden; the hull's material `tank`
   gets skin `tank_d` (`enemy/tank` model only; on the snowcat the request finds no such material and changes
   nothing, possibly printing a console warning); temp entity 18 (debris) at the origin, direction up, count 30, size 2, chunk type 9, extents
   0, 0, 0, material 28 (metal); persistent effect `environ/helismoke` on the hull bolt `bolt_turret` (snowcat: no such
   bolt → the entity origin) at scale 1.5; the body becomes disabled (snowcat: lights and exhaust off, SPEC-722).
The wreck stays solid; the tank keeps takedamage on.

**14.2 Snowcat self-destruct** (command 4 on a snowcat): steps 1–4 of §14.1, then entity effects bit 0x400 is set
(meaning UNVERIFIED), temp entity 44 magnitude 255 and the same flash blinding. Nothing is hidden, no wreck skin, no
debris, no smoke, and the snowcat is **not** disabled — it keeps thinking and its machine guns keep firing if they
were on (UNVERIFIED in game; open question 2).

#### 15. Tank touch (crushing / shove-off)
When an entity touches the tank and the entity's lowest point (origin z + mins z) is above the tank's origin height
(it is on top of the tank): damage 10 to it (inflictor and attacker the tank, direction (0, 0, 1), point = its
horizontal position at its absolute bottom, knockback 100, flag SPREAD 0x20000, means of death 0); then its velocity
gains n·100 horizontally and max(n_z·100, 10) vertically, n = unit vector from the tank's origin to its origin. This
repeats every frame of contact. Things touching the sides below that height are not affected (the pusher physics of
SPEC-760 handles pushing). The snowcat has no touch.

#### 16. Turret end-of-sequence
When the turret's sequence reaches its end (`EOS` note) after a cannon shot (the "fired" flag is set): the flag is
cleared and the hull returns to its movement animation — `tank` looping when the body is moving, else `tank` as a
static pose (snowcat: `snowcat`, never triggered because the snowcat never fires a cannon).

#### 17. Code that is not reachable in 1.07f
- The game also contains a snowcat-specific command class (class code 20, chunk `AITK`) with its own move/fire/
  machine-gun steps (turn tolerance 1.0°, no reverse, arrival within 30 units on x, y and z, fire waiting for
  §9.4). The snowcat never creates it — its VEHICLE commands use the tank class 21 — so it exists only as a
  loadable savegame class. An implementation may omit it but must accept class code 20 when loading.
- The removal-flag rule of §6 step 7.

## Constants

### Part A — helicopters

| item | value |
|---|---|
| think interval | 0.1 s |
| health at spawn | 1000 (= max health) |
| entity box | (−280, −280, −50) … (280, 280, 70) |
| movement (navigation) box | (−280, −280, −140) … (280, 280, 140) |
| spawn ground placement | raise 200, trace down to z = −600 |
| take-off climb | 224 units |
| return-home approach height | home z + 224 |
| leg acceleration / deceleration | 8/3 units per frame per frame; minimum speed 2 |
| leg speed cap | 80 units per frame (none on exit legs) |
| leg pitch: first half / second half | target 45 × (1 − q), rate 1.5°/frame / target −15 × (1 − r), rate 0.75°/frame, 0 below speed 4 |
| strafe acceleration / deceleration | 16/3 / 4 units per frame per frame; roll 40 × (1 − q) / 16 × (1 − r), rate 3.5°/frame |
| yaw rate | 9° per frame (snap within 9°) |
| bank | 0.4 × p then 0.16 × p, rate 2°/frame (snap within 2°) |
| level out | roll 4°/frame, pitch 1.5°/frame |
| landing descent per frame | max(2, height × 0.0446428571) |
| repair per frame | trunc(max health / 50) |
| health commands | trunc(0.33 × (T + 1) × S) + 1000; T = toughness level 0–4 |
| fire-while-flying gun gate | yaw error < 20° and speed > 8 |
| forward cone | cosine ≥ 0.173 |
| target re-choice | every 30 s |
| aggression roll | irand(0, 10) < `volume` |
| reposition gain threshold | 800 units |
| hover-engage / pattern hovers | U(1, 3) s; pattern 3: 2 s |
| look-around point | ±1000 x/y |
| wander point | ±2500 x/y, ±250 z, z ≥ min z + 112 |
| cruise altitude | min z + 0.75 × (max z − min z) |
| clear-spot search | 18 directions, radius steps 100, re-centre after 100 tries |
| default flight bounds | ±10000 on every axis (global, not reset) |
| gun | attack 28, 1 round per frame, jitter irand(−10, 10) per component (none when S = 1337), sound `weapons/assault/fire.wav` 0.9 / 0.5 |
| gun bolt-turn limits | base yaw ±1 rad, barrel pitch −0.3927 … 0.7854 rad, 0.314159 rad per update |
| pilot head limits | yaw −0.3927 … 0.7854, pitch ±1 rad, 0.314159 rad per update |
| rockets | 8, one per second at the normal rate, pods `FLASH_ROCKET1`/`FLASH_ROCKET2`, tube offsets ±5 right/up |
| rocket projectile | speed 600, box (−4, −2, −4) … (3, 2, 4), lifetime 10 s, damage 130 radius 150, noise (150, 250), shake (100, 300, 30) |
| rocket dip | dip pitch = elevation × 0.1, only when |elevation| < 40°; 5°/frame; volley 1 or 2 |
| salvo choice | −1 with probability 5/11, −2 with 6/11 (irand(0, 10) < 5) |
| crash spin | pitch ±2 about 20; vz += max(−10, −4 t); horizontal += forward × (−2.5 pitch); yaw rate +45 up to 180°/s |
| crash start delay | 2 s |
| crash stages | 0.2 s, 1.0 s, then free |
| wreck teleport | origin (−1000, 744, 986), velocity (−10, 40, 0), roll −10, stages start after 1 s |
| killed-at-home tolerance | 0.1 per axis |
| looping sound | `Enemy/Helicopter/Blade.wav`, sound data 0xFB |
| effects | `weapons/othermz/heli`, `heli_expl`, `weapons/world/autogundeath`, `weapons/world/rockettrail2`, `weapons/world/rocketexplode`, `environ/helismoke` (SPEC-722) |
| sounds | `impact/explosion/exp1.wav`, `impact/explosion/med.wav`, `weapons/assault/fire.wav` |
| savegame chunks | `AIHA` 48 bytes, `HEWP` 44 bytes per waypoint |

### Part B — tank and snowcat

| item | value |
|---|---|
| tank health | 500 × (T + 1) |
| snowcat health | 1000 |
| tank box | (−152, −85, −44)…(152, 85, 80) |
| snowcat box | (−92, −67, −34)…(92, 67, 58); spawnflag 1: (−91, −66, −33)…(91, 66, 58) |
| turn rate / drive speed | tank 20°/s, 50 u/s; snowcat 30°/s, 70 u/s |
| facing tolerance | 1.5° |
| arrival tolerance | 5 units on x and on y |
| drive loop sound | `Ambient/Models/Tank/tanklp.wav`, sound data 0xFB |
| cannon aim target offset | +12 z; machine gun / snowcat guns +20 z |
| tank turret rate | 0.04 rad/think, yaw unlimited |
| tank cannon pitch | rate 0.03, limits [−0.05 or −0.02, 0.2] |
| cannon low-limit zones (turret yaw, rad) | (−0.13, 0.13), (0.8, 2.4), (−2.4, −0.8) → −0.05; else −0.02 |
| tank machine gun | mount yaw rate 0.05 unlimited; pitch rate 0.05, limits [−0.314159, 0.785398] |
| snowcat turret / gun rate | 0.06 + 0.01·T |
| snowcat gun pitch limits | [p, 0.785398]; p = −0.3 (|yaw| < 0.5), −0.2 (1.0 < |yaw| < 1.65), else −0.1 |
| aim tolerances | limit tolerance 0.0005 rad, aligned 0.001 rad, dead zone 0.01 rad |
| shell trace length / distance scale | 4096 / fraction × 4000 |
| shell speed | 500 units per think |
| shell explosion | +10 z, damage 130, radius 150, flag 0x8000, means of death 0, effect `weapons/world/rocketexplode` |
| shell noise | loudness 150, radius 250 |
| shell camera shake | intensity 100, radius 300, parameter 30 |
| cannon firing temp entity | 56, size 1, mode 2; muzzle effect `weapons/othermz/tank` |
| machine-gun line-of-fire threshold | fraction 0.95 (tank), 1.0 (snowcat) |
| burst length | 0.2 + U(0, 0.2)·T s, redrawn each think |
| burst pause | U(0.3, 0.6)·(5 − T) s |
| spread per axis | tank (5 − T)·0.03, snowcat (5 − T)·0.05 |
| machine-gun attack | 12; muzzle effect `weapons/othermz/machinegun` |
| touch damage | 10, knockback 100, flag 0x20000; shove 100, vertical ≥ 10 |
| knife-hit feedback | `environ/wallspark`, `impact/surfs/metal1..3.wav` channel 2 |
| wreck | TE 44 magnitude 255; flash 5000 / 0.9 / 0.5 s; skin `tank_d`; TE 18 (up, 30, 2, 9, 0, 0, 0, 28); `environ/helismoke` ×1.5 on `bolt_turret` |
| self-destruct damage value | 10000 |
| health floor at death | −999 |

## Test vectors / how to verify

### Part A — helicopters

Stock scripts use 21 HELI ids: 1, 5, 6, 14, 22–27, 29, 34–38, 40–43, 48 (counts: 14 ×20, 42 ×17, 5 ×7, 34 ×6,
40 ×6, 22 ×4 …). Script listings come from the `.os` files (SPEC-140 tools).
1. `trn1`, `ds/trn1/trn1_heli2.os`: the script sets health with ids 37/38 (`heliStartHealth`), then hovers with
   id 34 for 4–5 s at a time and switches the navigation box with id 40. While hovering with engage, the helicopter
   turns to look around and attacks the player only when the player's eye is visible and (no auto-combat here)
   inside the forward cone of about 80°; a gun engagement lasts 1 s (attack time 1); rockets only when the script
   turned them on.
2. `jpn3`, `ds/jpn3/blastdoors.os`: bounds via 35/36, health 2500 via 37/38, 15 point waypoints (42), three of them
   turned into area waypoints with extents (477, 864, 335), (404, 1515, 335), (1366, 400, 335) (43), rockets and gun
   on (24, 26), auto-combat with S = 0 (41). Expected: max health = trunc(0.33 × (T + 1) × 2500) + 1000 = 3475 at
   toughness 2 (skill 2) and 1825 at toughness 0; combat continues until another script action is queued; with the
   aggression 0 (no `volume` key on the entity — check the map) no interjections happen, so the helicopter only cycles patterns (vantage points, patrol and
   zone moves with 1–3 s hovers) and fires rockets at one per second whenever it is in a strafe or hover-fire.
3. `jpn3`, `ds/jpn3/j3intro.os` issues id 48 with S = 1: the helicopter turns into a Hind on the spot (navigation
   lights disappear, model `enemy/hind`).
4. `irq2b`, `ds/irq2b/irq2b_outro.os`: gun on (26), attack point `vAmuPos` with S = 1337 (29), gun off (27): the
   gun rounds go exactly along the barrel (no spread).
5. `kos2`, `ds/kos2/helitakeoff.os`: id 1 with a signal: rotors play `rotor_start`, then the helicopter rises 224
   units with nose-down pitch building up and easing off, nose up while braking; the signal fires when the climb
   leg and the level-out are done.
6. Shoot a hovering helicopter with the sniper rifle through the canopy (pilot): canopy shows `glass_blood_bk`, the
   helicopter spins down (crash command), rises briefly for about 2 s first, then falls spinning up to 180°/s and
   explodes with `heli_expl` and shattering when it hits the ground.
7. Damage with explosives: C4 never damages it; grenades/rockets (area damage) take full damage (no ×0.1 hull
   factor); hits flagged PROJECTILE do 10 % on the hull (SPEC-720).
8. With `ai_freeze 1` a flying helicopter stops in mid-air and resumes when the cvar is cleared.
9. Savegame during `trn1_heli2.os`: the `AIHA` chunk's action id counter equals the number of HELI commands issued
   so far; on load the waypoints and counts come back.

### Part B — tank and snowcat

1. `irq2b`: the tank `agentsmith` has health 500 at skill 0 and 1500 at skill 2 (count hits from a known weapon);
   the map key `health 100` has no effect.
2. Skill 0 (T = 0): after its script sends command 3 with scalar 1, the tank never fires its machine gun. Skill 2
   (T = 2): bursts of 2–6 shots separated by 0.9–1.8 s pauses while the player is visible.
3. Shell timing: order command 2 at a wall 2048 units straight ahead (already aimed): the explosion appears on the
   third think after the muzzle flash (0.3 s); a target 400 units away explodes in the same think; a point in open sky
   (nothing within 4096 units) gives a muzzle flash and no explosion, and the done event fires at once.
4. Standing on top of a tank: 10 damage per frame and the player is pushed off; walking into its side does nothing.
5. Knife hits on a tank: sparks and metal sounds, health unchanged.
6. Kill a tank without killtarget: explosion, debris, the turret, cannon and machine gun vanish, the hull shows the
   `tank_d` skin and smokes; it no longer shoots. Kill `agentsmith` (killtarget set): the script `irq2b_tankdeath`
   runs and its command 4 produces the same wreck.
7. `sib2` parked snowcats (spawnflag 1): no exhaust, no headlight beams, never move or fire; their box is one unit
   smaller.
8. Send VEHICLE id 7 to a tank: the done event fires on the next think.
9. Use a tank spawned without spawnflag 1: it stops (disabled) and does not resume after a second use (UNVERIFIED,
   open question 1).

## Open questions

### Part A — helicopters

1. Spawnflag 8 (set by id 46) and spawnflag 32 (server flag 0x100): no reader found in the helicopter code.
2. Which rotor uses which scale factor (0.25/1.25 or 0.375/1.875) — check the model in game.
3. Whether becoming a Hind (id 48) really resets rockets and mode flags (fresh body) — j3intro behaviour.
4. Exact geometry of combat pattern 0 (strafing run along an area waypoint face).
5. The aim-step tolerance interplay (Part B §9.1, open question B5) applies to the gun and pilot head as well.
6. Whether the global flight bounds survive a level change in practice (no reset code was found).
7. The "removal check" before the death routine on removal (§8 step 8) — an AI framework query whose exact rule is
   in SPEC-700 (UNVERIFIED which one).
8. (SPEC-711 A8 on id 47: see "Amendments to approved specs" item 2.)
9. Non-engaging hover (id 33) waits for a fuselage end-of-sequence notice that is never connected, so it should never
   end (§9.7). Verify with a test script `heli(h, 33, num=3)` followed by any other command: the second command
   should never start.

### Part B — tank and snowcat

1. Whether anything re-schedules the think of a vehicle that went idle in the disabled state (a later use or VEHICLE
   command would otherwise have no effect). Verify: spawn `m_x_tank` with spawnflag 1, `use` it twice, send command 1.
2. Whether a self-destructed snowcat keeps firing (§14.2); verify on `sib1`/`sib2` with command 3 then 4.
3. Whether characters block the content-mask-3 traces (shell, machine-gun line of fire, drive path); verify by
   standing a soldier between a tank and a wall and ordering command 2 at the wall.
4. Which hits carry damage flags 0x200/0x400 (routed through the body and causing pain) — believed to be traced
   bullet hits (SPEC-720/782); verify by shooting a tank with bullets and with grenades and comparing machine-gun
   disabling (only body-routed hits can disable it).
5. Exact tolerance interplay of the aim step (§9.1).
6. The meaning of entity effects bit 0x400 set by the snowcat self-destruct.


## Amendments to approved specs (for ERRATA)

1. **SPEC-750 §12.8, VEHICLE ids**: replace "VEHICLE ids 1–5 create an action; others none (the done event never
   fires — UNVERIFIED)" by: every VEHICLE id creates a vehicle command action; ids other than 1–5 have no step, so the
   action completes at its first execution and the done event fires on the next vehicle think (Part B §3). Ids 1–5:
   Part B §3.
2. **SPEC-711 A8, HELI id 47**: id 47 does not spawn a helicopter. It repaints the commanded helicopter itself and
   all its bolt-ons: material `cobra` gets skin `m_x_chopper_black` (S = 1), `m_x_chopper_green` (S = 2) or
   `m_x_chopper_white` (other S) (Part A §1). No entity is created.
3. **SPEC-711 A8, HELI ids 34–36**: the summary "34–36 set a helicopter parameter to the scalar or to 0/1" is
   superseded by Part A §1: 34 = hover and engage for S seconds, 35 / 36 = minimum / maximum corner of the flight
   bounds from the vector V.
4. **SPEC-700, character type table, codes 9 and 10**: the team reported by `m_x_tank` and `m_x_snowcat` to the team
   rules is 1 (the table says "own rules").
5. **SPEC-722, tank and snowcat bodies, open items** (resolutions, not corrections): spawnflag 1 of `m_x_tank` /
   `m_x_snowcat` is "starts disabled" (Part B §1, §7); the aiming parameters are hull turn rate (degrees per second),
   drive speed (units per second), turret yaw rate, cannon pitch rate and machine-gun rate (radians per think), with
   the snowcat using its turret rate for its guns too (Part B §2).
