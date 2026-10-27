# SPEC-761: New world entities — Raven brush entities, triggers, cameras, mission brushes and spawners
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (server-side game module), version 1.07f
Depends on: SPEC-001, SPEC-003 (player module, inventory slots), SPEC-006 (classname list), SPEC-201 (messages),
SPEC-202 (entity events, player-state remote camera fields), SPEC-760 (world core differences, terms, key table)

## Purpose
Full behaviour of the world entity classes of the "Quake II world core" package that do not exist in Quake II:
new `func_*` brush and point entities, new `trigger_*` classes, the remote camera, mission/player brushes, the level
and world spawners, and the shared world helpers they use (ignition, flash blinding, camera shake). Terms, sound
notation and the meaning of "use key" are those of SPEC-760. Map keys are the keys of the key table (SPEC-760 §4);
several classes reuse generic keys (`health`, `count`, `volume`, `style`…) as parameters — the reuse is listed per
class. "Point helper" = movetype NONE, not solid, box (−4,−4,−4)–(4,4,4), not drawn.

## Interface / layout
Classnames covered (all spawned through the classname tables of SPEC-006):

| Group | Classnames |
|---|---|
| doors | `func_door_portcullis`, `func_door_rotating_smart` |
| brushes | `func_breakable_brush`, `func_trigger_brush`, `func_pushkillbox` |
| point helpers | `func_alarm`, `func_ignite`, `func_Dekker_console`, `func_Dekker_diehere`, `func_Dekker_jumphere`, `func_Dekker_split`, `func_hide_the_players_gun`, `func_snipercam_zoom`, `func_mission_status`, `func_player_health`, `func_player_armor`, `func_player_item`, `func_score`, `func_camerashake`, `func_fade` |
| camera | `func_remote_camera` |
| triggers | `trigger_activate`, `trigger_conquer`, `trigger_countdown`, `trigger_useable`, `trigger_heal`, `trigger_movedown`, `trigger_keep_using`, `trigger_safezone`, `trigger_sound`, `trigger_objectives` |
| spawners | `spawner`, `spawner_monster`, `spawner_boosterpack`, the world spawner (driven by `worldspawn` keys), `dm_KOTH_targetzone` |
| development | `test_model` |

New triggers share Quake II's trigger setup: if `angles` is non-zero it becomes the move direction; solid TRIGGER,
movetype NONE, brush model from `model`, not sent to clients.

Mission status values (a level-wide integer shown by the HUD, SPEC-006 stats): 0 none, 2 "exit warning",
3 "objectives updated"; also any value given by a `func_mission_status` count.

## Behaviour

### 1. Doors
**1.1 func_door_portcullis** — a gate that rises only while it is being "worked" and falls back when released.
Spawn: as func_door (SPEC-760 §B5.7) with: `speed` (default 20, doubled in deathmatch) = rising speed;
`volume` (default 10) = lowering speed; accel/decel default to speed; lip default 8; dmg default 2; START_OPEN (1)
swaps the end positions; `wait` forced to 0; message/sp_message touch as doors; blocked behaviour as doors.
- Use (any source, any number of times): marks the gate as worked now (count = 1, last-used time = now). Typical
  driver: `trigger_keep_using` (§3.7).
- Every 0.1 s: if not worked during the last 0.2 s it is released (count = 0).
  - released: speed = `volume`; if it is at rest closed nothing happens; if it is open or rising it starts lowering
    after the door clearance check (SPEC-760 §B5.7: blocked by players/characters within 96 units, re-checked every
    tick); while lowering it continues;
  - worked: speed = `speed`; if it is lowering or closed it starts rising (`door_go_up` with the gate as activator);
    while rising or open it continues.
  - Then, every tick, the use behaviour of every entity whose targetname equals the gate's `target` is called
    directly (other = activator = the gate; no message, delay or killtarget processing). Every such target must
    have a use behaviour.
- Its movement steps advance on these 0.1 s ticks (same Quake II accelerated-move formulas).

**1.2 func_door_rotating_smart** — a rotating door (SPEC-760 §B5.7, func_door_rotating keys/spawnflags, distance
default 90) that opens away from whoever opens it. Spawnflag 2 (REVERSE) is ignored. When it starts opening, let o be
the door origin (hinge), c the centre of its bounding box, a the rotation axis in world space (yaw → (0,0,1),
pitch → (0,1,0), roll → (1,0,0)), n = (c − o) × a. If n · (activator origin − c) < 0 the door opens by −distance
instead of +distance. When it starts closing the direction is reset to +distance for the next opening.

### 2. Brushes
**2.1 func_breakable_brush** — static brush (solid BSP, movetype NONE) that breaks into debris.
Keys: `health` (default 100; 50 when material is 7 or surface type is 37), `material` (0–34, else none → no
debris), `surfacetype` (1–42, else 0; debris sound/look class), `dmg` (blast on breaking), `volume` (blast radius,
default = dmg), `count` (debris count override), `mass` (debris size class override), `message`, `target`,
`targetname`. Spawnflags: 1 cannot be damaged (only breaks when used), 2 throws debris when hurt, 8 damage mode 3
(UNVERIFIED: restricted damage, e.g. explosives only).
- Pain (spawnflag 2, material > 0): a debris burst from the box centre toward the attacker: count =
  clamp(trunc(size.x·size.y·size.z / 9000 + 2), 10, 20); size class 3 if volume > 250000, 2 if > 40000, else 1;
  debris box (size.x, size.y, size.z·0.5); material and surface type passed to the effect (FX spec).
- Breaking (killed, or used when it has a targetname — damage = its health): blast first if dmg ≠ 0 (radius damage
  dmg in radius `volume`, means of death 26, ignoring itself; characters within dmg units receive an explosion
  stimulus); debris burst as above unless count is set (then count debris of size class `mass`); `message` is centre
  printed to the attacker with `Misc/Talk.wav` (0, 0.6, 1); targets fire (activator = the brush); the brush is
  removed. Its material/surface type are added to the material-flag string at level load (SPEC-760 §B1.5).

**2.2 func_trigger_brush** — damageable brush that fires its targets when destroyed.
Solid BSP, movetype NONE; `health` default 100; damageable unless spawnflag 2; spawnflag 1 = invisible (not sent to
clients). With a targetname, use toggles damageability. When killed: targets fire (activator = itself), then it is
removed.

**2.3 func_pushkillbox** — invisible solid brush that kills and flings whatever touches it.
Solid BSP, movetype NONE, clip mask player-solid|monster-clip, not sent to clients. Touch by any entity: damage
100000 with direction (0,0,1) at the toucher's feet, knockback 0, means of death 22. Non-players then get their body
put into its falling state (character body) and movetype NOCLIP; players get movetype 10. Velocity =
(cos(count°)·volume, sin(count°)·volume, 0) — `count` is the yaw in degrees, `volume` the speed; elasticity −1.
(UNVERIFIED in-game: corpses fly through walls because of NOCLIP.)

### 3. Point helpers (all are point helpers unless stated; none responds to the use key)
**3.1 func_alarm** — use: raises the level alarm (stays raised until the next level). Effect: level spawners switch
from `delay` to `wait` timing (§5).

**3.2 func_ignite** — use: every entity within `health` units (no default; 0 = nothing) whose origin is reachable by
a line trace from the helper (mask solid|window) is set on fire with the helper as attacker (§7.1).

**3.3 func_Dekker_console, func_Dekker_diehere, func_Dekker_jumphere** — inert markers looked up by classname by the
final-boss behaviour (AI spec).

**3.4 func_Dekker_split** — use: finds the first `m_x_mraiderboss2` that has a character body, deals it 1000 damage
(means of death 22) and applies a special body split (dismemberment spec). UNVERIFIED details.

**3.5 func_hide_the_players_gun** — controls the first player's weapon holster toggle (the player module's toggle,
SPEC-003 inventory slots 35/36). At spawn `health` = 1 if the toggle is currently on, else 0. Use: if `wait` is −1,
only record the current state (count = 1/0) and set wait to 0; otherwise flip the toggle and record the new state in
health.

**3.6 func_snipercam_zoom** — use: sets the first player's field of view to `wait` clamped to 6…60, or to 95 when
`wait` is −1. When cvar `ei_show` ≠ 0 prints `new fov = %d`.

**3.7 func_mission_status** — use(other): if other's targetname is exactly `exitwarning_on` → mission status 2; if
`exitwarning_off` → 0; otherwise mission status = this entity's `count`. (The comparison is on the targetname of the
entity that fired the use, not this entity's.)

**3.8 func_player_health** — use: first player's health += `count`, clamped to 0…100.
**3.9 func_player_armor** — use: first player's armour += `count` (player module, clamped 0…100).
**3.10 func_player_item** — use: gives the first player `count` items of player-module item type `health` (1–7)
with no maximum. Errors (developer prints): count 0 → `hey! you can't add zero items to the player's inventory!`;
no inventory → `hey! no inventory!`; type outside 1–7 → `hey! adding an invalid item type to the player's
inventory!`.

**3.11 func_score** — not solid, not damageable. Use(activator): if the activator is a player: without `team` key its
score += `count`; with `team`, only if the player's userinfo `teamname` equals `team` (case-sensitive).

**3.12 func_camerashake** — `count` (default 30) = maximum intensity, `health` (default 300) = radius, `delay`
(default 3) = duration, `style` 1 = play `Ambient/Gen/Battle/ExpRvb.wav` (2, 0.8, 0) on use (precached at spawn).
Use: shaking runs now and every 0.2 s until `delay` seconds after the use. Each tick, for every player whose centre is
within the radius: intensity = (1 − distance/radius)·count; level = 4 if intensity > 80, 3 if > 55, 2 if > 35,
1 if > 17, else 0; entity event 16 + level with parameter 30 is added to that player (SPEC-202 events; the client
shakes the view).

**3.13 func_fade** — screen fade for all players. `color` (spawn key, RGB 0–1) = fade colour, `delay` (default 5) =
duration; spawnflag 1 = fade *from* the colour back to clear, otherwise fade *to* the colour. Use: every entity with
classname `player` gets fade colour = color and fade alpha = 0 (to colour) or 1 + 0.1/delay (from colour); every 0.1 s
alpha changes by 0.1/delay until it reaches 1 (stays) or 0. (Fade colour/alpha feed the view blend; the same fields
are used for the single-player death fade, SPEC-760 §B9.3.)

### 4. func_remote_camera
Point entity (movetype NONE, not solid, box (−4,−4,−4)–(4,4,4)), count 0 = inactive. Requires `target`; without it:
`Object 'func_remote_camera' without a target.` and removed. `volume` = 4 sets a level-wide camera option (UNVERIFIED
meaning). Keys: `target` (what the camera looks at), `style` (field of view while active, 0 = keep), `volume`
(remote-view type, passed to the client as an integer, UNVERIFIED), spawnflags 1 activator only (else all players),
2 freeze players (not solid + god mode while viewing), 4 reusable (else removed after deactivation), 8 fixed angles
(do not track the target).
- Use(activator):
  - a dead player activator → ignored;
  - if the activator is a player already viewing another camera, that camera is deactivated first (if it cannot be
    found: `Error in Camera Scripting!  Check your 'use' and 'remove' commands!/n`);
  - if this camera is active → it is deactivated (toggle) and nothing else happens;
  - activate: count = 1; for each affected player (all in-use players, or only the activator — a non-player
    activator with spawnflag 1 prints `Error in Camera Scripting!  Activator (%s) is not a client!` and aborts):
    remember its field of view and set `style` (if non-zero), increase its camera nesting count, set its camera to this
    entity and its remote-view type to `volume`;
  - the first entity whose targetname equals `target` becomes the look target; the camera angles are set to look at
    it; if that target is `misc_generic_security_camera` or `misc_generic_security_camera2` the view is taken from the
    target model's position and angles instead of the camera's (internal flag 16);
  - the remote-view origin (sent in 1/8 units) and angles of every affected player are set (SPEC-202 player state
    fields 18/19); spawnflag 2 makes affected players not solid and god-mode;
  - every 0.1 s while active: unless spawnflag 8 the camera re-aims at the target; the origin/angles are copied to
    every player whose current camera is this one.
- Deactivate: for the affected players: restore field of view (if style), decrease nesting count, remote-view entity
  −1, remote-view type 0, spawnflag 2 restores solid bounding box and clears god mode; count = 0; removed unless
  spawnflag 4.

### 5. trigger_* classes
**5.1 trigger_activate** — use: toggles every entity whose targetname equals `target` between not-solid and
trigger-solid and relinks it (enables/disables other triggers). `WARNING: Entity used itself.` when it targets
itself; `entity was removed while using targets` when it is removed meanwhile. Not touchable, no use key.

**5.2 trigger_conquer** — capture zone for the conquer ("CTB") game mode: registered with the game rules at spawn;
touch by a player in the game (a client state flag clear, UNVERIFIED: not spectating) after the mode's start time
calls the game rules' zone-touch handler (game-modes spec).

**5.3 trigger_countdown** — on-screen countdown. `count` = seconds (default 10). Spawnflags: 1 starts disabled
(not solid; use still works), 2 may be started by non-players, 4 repeatable. Touch and use behave the same: only a
player (or anything with spawnflag 2):
- not running → start: send the countdown message (SPEC-201, message 31, a long with the seconds) to all clients;
  become not solid; remember the starter as activator; every 1 s send the remaining whole seconds
  (count + start − now, truncated);
- running → stop: send 0; non-repeatable → removed; repeatable → solid trigger again, idle.
- reaching 0: send 0; non-repeatable: the level's "countdown expired" flag is set (HUD), targets fire (activator =
  starter), removed; repeatable: targets fire, then it behaves as if used by itself (only effective with spawnflag 2,
  which resets it to idle).

**5.4 trigger_useable** — responds only to the use key (not touch). On use: last-used time = now, targets fire
(activator = the user); if its own targetname contains `wav`, that targetname is played as a sound (0, 1, 1) on the
trigger (precached at spawn).

**5.5 trigger_heal** — health station, use key only. `health` (default 25) = amount. First use by a player below
max health: health += amount (clamped to max health), `dm/healthpu.wav` (3, 1, 2) on the player, station plays
`misc/hlthstation/health.wav` (0, 1, 0), fires its targets (activator = user) and becomes empty; every use (including
that one and uses at full health) then plays `misc/hlthstation/full.wav` (0, 1, 0). An empty station only plays
`misc/hlthstation/empty.wav` (0, 1, 0). All three sounds precached.

**5.6 trigger_movedown** — touch: the toucher's vertical velocity is set to −200.

**5.7 trigger_keep_using** — winch/valve operated by holding position: a use-key press by a player arms it; while armed,
each touch (by anything) refreshes it; every 0.1 s, if refreshed within the last 0.2 s it fires its targets
(activator = itself), otherwise it disarms (a new use-key press is needed). Typically targets a
`func_door_portcullis`.

**5.8 trigger_safezone** — touch by a player sets the player's "safe" server flag (0x40; cleared each frame by player
code, UNVERIFIED) — characters ignore safe players (AI spec). Spawnflag 1 starts disabled; use toggles it.

**5.9 trigger_sound** — noise detector. A point entity with box (−5,−5,−7)–(5,5,12) (rotated with right-angle
`angles`), radius = `health` (default 128). It is registered in a list (at most 16 such triggers per level). When a
loud noise is reported at a point (gunfire, explosions — the same reports that alert characters), every registered
trigger_sound within its radius of that point is used: it fires its targets (if any, activator = itself),
unregisters and is removed. Noise reports are ignored in deathmatch and for noise makers flagged no-target.

**5.10 trigger_objectives** — touch by a player: mission status = 3 ("objectives updated"), the time is recorded
for the HUD, and the trigger is removed. Spawnflag 1 starts disabled; use toggles it.

### 6. Spawners
**6.1 spawner** — brings characters into a level near, but out of sight of, the player.
Spawn: point entity (box (−5,−5,−7)–(5,5,12) rotated with right-angle angles, not solid, model index 0, clip mask
0x02030003), classname forced to `spawner`; the time of the last burst starts at the spawn time; each non-empty `spawn1`/`spawn2`/`spawn3` classname is spawned once
and removed (precache). Keys and defaults: `delay` 30 = seconds between bursts while the alarm is down; `wait` 10 =
seconds between bursts after the alarm (func_alarm); −1 for either = never spawn in that state; `count` 1 =
characters per burst; `mass` 1 = maximum of its spawned characters alive at once (−1 = unlimited); `health`
(0 → unlimited) = total number to spawn; `lip` 450 = "too close to be invisible" radius; `killtarget` = target fired
when the spawner is exhausted and all its characters are dead; `team` = several spawners with the same team form one
spawner with multiple spots (the team master runs it, slaves never think); `soundname` sound set (looked up, not
played, UNVERIFIED). Spawnflags: 1 starts off; 2 one classname per burst; 4 spawned characters immediately know where
the player is; 8 spots count as never visible; 16 ignore the "too many enemies near the player" limit; 128 ambush
mode (no interval: spawns whenever possible; spots are hidden only if not in line of sight, and a spot once seen is
permanently unusable). Use: in ambush mode only arms it, which matters only for spawner_boosterpack (6.4) — a plain spawner
with spawnflags 1 and 128 never starts; otherwise use toggles it on/off (on = first attempt 1 s later).
- Every 1 s while on:
  1. exhausted (total reached 0): for each team member that has a killtarget, when none of the spawner's characters
     is alive any more (developer print `Level spawner running kill target` when `ei_show` ≠ 0), its timing values are
     cleared and the killtarget's entities are fired as targets (activator = the spawner); once every member has done
     so the whole team is removed;
  2. no classnames at all: `spawner set to spawn nothing!`, next attempt in 99999.9 s;
  3. unless ambush mode: wait until (now − time of last burst) > interval, interval = `delay` before the alarm,
     `wait` after;
  4. unless spawnflag 16: when 7 or more enemies are already near the player (as counted by the AI system):
     `Level spawner can't spawn - too many enemies nearby`;
  5. for each of `count` characters: stop if exhausted; when `mass` ≠ −1 and that many of its characters are alive,
     or during a cinematic: `Level spawner can't spawn - either has too many kids or is in cinematics`; choose a spot
     (6.2), none → `Level spawner can't spawn - no valid spots`; pick a random non-empty classname (or keep the first
     one with spawnflag 2); create it 35 units above the spot with the spot's angles and target, box
     (−16,−16,−32)–(16,16,41); if that space is occupied (monster-solid) → removed, `Level spawner can't spawn -
     space is occupied`, retry in 1 s; otherwise it spawns (`Couldn't spawn entity type %s!` on failure), is marked
     as spawned by this spawner, its AI is told it was spawned, with spawnflags 4 or 128 it is alerted to the player;
     total −1 (if limited); time of last burst = now (whole seconds); next attempt = now + interval;
     `Level spawner spawned a guy`. (Messages other than the error are developer prints shown when `ei_show` ≠ 0.)
**6.2 Spot choice:** start at a random team member, try each once in team order: the spot qualifies when the nearest
player is closer than 769 units and the spot is not visible; then trace 256 units down from the spot, add 12; the box
(−16,−16,0)–(16,16,73) there must be free (monster-solid). Visible = there is a player, spawnflag 8 is clear, and
(the spot is in front of the player's view or within `lip` units) and in the player's PVS — with ambush mode a line
of sight is also required and marks the spot as seen forever.
**6.3 spawner_monster** = spawner with spawnflag 64 (no further use found, UNVERIFIED).
**6.4 spawner_boosterpack** = spawner with: health default 8, count 1, mass 20, delay and wait 1, spawnflags +192
(ambush, flag 64). Every 0.4 s: if it has been seen it stops for good; if armed (used) it runs a spawner attempt;
it becomes "seen" when a player has it in PVS and line of sight. (A one-shot ambush that spawns while unseen.)

### 7. Shared world helpers
**7.1 Ignition.** Setting an entity on fire (from func_ignite, flame weapons, burning bodies): nothing when the
parental-lock cvar `lock_deaths` was set at game start; bosses `m_x_mskinboss`, `m_x_mraiderboss1`,
`m_x_mraiderboss2` are immune unless their `count` is 100; the target must be damageable and not in god mode. A dead
target (health < 1) whose burning ends within 2 s gets effect `environ/onfireburst` on its model and burns for 4 more
seconds; otherwise effect `environ/quickfireburst`; the igniter is remembered as attacker. Burning (run for
characters each step frame): while burning and dead, every third frame radius damage 10 in 80 units (damage flag 2,
means of death 27, attacker = igniter or world) and the model is darkened (tint −0.05 per step down to 0.2); while a
second burn timer runs, every third frame direct damage (means of death 27).
**7.2 Microwave hit** (microwave-pulse weapon): same boss/damage rules; dead targets ignite (2 s); living targets
get effect `weapons/world/mpgzap` at most once a second; characters are stunned (AI stimulus 25); players' flash/blind
amount +0.3 (max 0.9), duration set to 0.2 s, and camera-shake event 20 (parameter 30).
**7.3 Flash blinding** (flashpack, explosions with flash): for each player within the radius with a clear line from
the point to the eye (content mask 0x06000081, fraction ≥ 0.99): if the player faces the point
(dot > 0), blind amount += (radius·0.03/distance)·dot (light-amplification goggles on: doubled, at least 0.4), capped
by the given maximum (goggles: max raised halfway to 1, duration halved); duration = the given duration if larger;
characters within the radius with a clear line receive a blind stimulus of at most 25 that grows with how directly
they face the point (AI spec; exact formula UNVERIFIED).
**7.4 Explosion stimulus.** Characters (alive, not the world) whose centre is within R of an explosion centre receive
a stimulus of strength (1 − d/R)·S, capped at 100 (target_explosion, target_splash, breakables use R = S = dmg).

### 8. World spawner and alert meter ("PADD")
Single player only (not deathmatch, not during cinematics), requires a player, `ai_spawnfrequency` ≥ 0.01 and the
skill "alert gain" ≥ 0.01 (SPEC-760 §B9.2 spawning parameters G gain, F fade time, M max duration, Im spawn interval
at max, N max characters near player, Is spawn interval).
- Alert level A ∈ [0, 1], initially 0; cool-down marker C initially −999.
- Noise: weapon fire, explosions, alarms raise A by max(0.1, G·ai_spawnfrequency)·amount·0.7 (amount given by the
  noise source), capped at 1, and at 0.4 when ai_spawnfrequency < 0.1 or G < 0.1; only when time ≥ C + F.
- Each frame:
  - time < C (max-alert phase): A = 1; characters are alerted; `misc/padd/max.wav` (0, 1, 0) on the player at most
    every 0.75 s; every Im seconds a world spawn is attempted;
  - C ≤ time < C + F: A = 1 − (time − C)/F (fading, nothing else);
  - otherwise: if A was 1, C = time + M (enter max phase); A −= (A·0.015 + 0.003)·0.7 (min 0); if A > 0.75 the
    warning interval is 1 − (A − 0.75)·3.2, if 0.5 < A ≤ 0.75 it is 3 − 8·(A − 0.5); `misc/padd/warning.wav`
    (0, 0.4, 0) when that interval has passed since the last warning; when A > 0.5 and Is seconds have passed since the
    last world spawn, a world spawn is attempted;
  - when spawning is disabled A decays as above without sounds.
- World spawn: count characters within 600 units of the player; if more than N + 1 → `World can't spawn - too many
  guys nearby`; else ask the navigation system for a hidden spot near the player (`World can't spawn - no spots
  available for spawning`); the box (−16,−16,−32)–(16,16,41) must be free; choose a random non-empty classname from
  the worldspawn keys `spawn1`–`spawn3` (`World spawn trying to spawn, but no fields set` if none); spawn 8 units
  above the spot facing the player; it must be a character (else removed); occupied → removed (`World spawn trying
  to spawn, but spot is occupied`); success records the time, marks it as spawner-made, and with `ei_show` ≠ 0 prints
  `World spawned in a guy` and plays `Weapons/mpg/fire.wav` on it (debug cue). Messages are developer prints shown
  when `ei_show` ≠ 0.

### 9. Other
- **dm_KOTH_targetzone**: invisible brush zone (not solid, box rotated with right-angle angles); its origin is set
  to the centre of its brush bounds; used by the king-of-the-hill rules (game-modes spec).
- **test_model** (development): movetype NONE, solid BBOX, box (−16,−16,−16)–(16,16,16), model index of `model`.

## Constants
| Name | Value |
|---|---|
| portcullis release time / tick | 0.2 s / 0.1 s; defaults speed 20 (×2 deathmatch), volume 10 |
| keep_using refresh window / tick | 0.2 s / 0.1 s |
| breakable debris count | clamp(trunc(volume/9000 + 2), 10, 20); size class thresholds 40000 / 250000 |
| breakable health defaults | 100 (50 for material 7 or surface 37) |
| camera shake thresholds | 17, 35, 55, 80 → levels 1–4; events 16–20, parameter 30; tick 0.2 s |
| camerashake defaults | count 30, radius 300, duration 3 |
| fade default | 5 s |
| trigger_heal default | 25 |
| trigger_sound default radius / list size | 128 / 16 |
| trigger_movedown speed | −200 |
| countdown default | 10 s; message 31 |
| spawner defaults | delay 30, wait 10, count 1, mass 1, lip 450, total unlimited; spot range 769; drop 256 + 12; spawn 35 up |
| enemies-near-player limit | 7 |
| boosterpack | total 8, mass 20, delay/wait 1, tick 0.4 s |
| world spawner | radius 600, spawn 8 up; decay (A·0.015 + 0.003)·0.7 per frame; gain factor 0.7; warning interval formulas above |
| ignition | 4 s / 2 s windows; radius damage 10 in 80 every 3rd frame; MOD 27 |
| microwave | stun 25, blind +0.3 max 0.9, duration 0.2, event 20 |
| snipercam FOV clamp | 6 … 60; −1 → 95 |
| player health clamp (func_player_health) | 0 … 100 |

## Test vectors / how to verify
- Portcullis + trigger_keep_using: press use inside the trigger and stay — the gate rises at `speed`; step out — after
  0.2 s it lowers at `volume`; stand under it — it does not lower while you are within 96 units of its origin.
- func_door_rotating_smart: open from either side; the door always swings away from the player.
- func_breakable_brush with dmg 50: breaking it damages nearby entities; `count 5` gives exactly 5 debris pieces.
- trigger_heal: first use at 50/100 health → health 75 with health+full sounds; second use → empty sound.
- trigger_countdown count 5: HUD shows 5,4,3,2,1,0; then targets fire.
- `func_snipercam_zoom` wait 100 → fov 60; wait −1 → 95.
- `spawner` with `delay 5`, `count 2`: every ~5 s two characters appear just out of view within 768 units; none
  appear while you look at the spot.
- World spawner: fire weapons repeatedly on `skill 4` — warning beeps (`misc/padd/warning.wav`) accelerate, then the
  max sound plays and characters appear every 0.5 s for 12 s.
- trigger_sound with health 200: firing a gun within 200 units fires its target once.

## Open questions
- Meaning of camera `volume` (remote-view type) and of the level-wide option set when volume = 4.
- `func_breakable_brush` spawnflag 8 (damage mode 3) and `spawner_monster`'s flag 64.
- Exact blind-stimulus formula for characters in flash blinding; the character "falling" body state used by
  func_pushkillbox.
- Who clears the player "safe" flag set by trigger_safezone.
