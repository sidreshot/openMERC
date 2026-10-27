# SPEC-760: Quake II world core — how SoF's game module differs from the Quake II 3.19 game source
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (server-side game module), version 1.07f
Depends on: SPEC-001 (game module interface), SPEC-003 (player module interface), SPEC-006 (game module map),
SPEC-201 (network messages), SPEC-202 (entity/player state), SPEC-203 (configstrings), SPEC-302 (cvars),
SPEC-761 (new world entities)

## Purpose
SoF's spawning, entity utilities, physics, brush movers, triggers, targets and miscellaneous world entities descend
from the Quake II 3.19 game source (`g_spawn.c`, `g_utils.c`, `g_phys.c`, `g_func.c`, `g_trigger.c`, `g_target.c`,
`g_misc.c`, `g_main.c`). A clean implementation may start from that source. This spec lists every observable
difference found, so that the Quake II code can be patched to behave like SoF. Anything not mentioned here behaves
as in Quake II 3.19 (function names below are the public Quake II names, used only to say *which* behaviour
changes). Entities that do not exist in Quake II are specified in SPEC-761.

Conventions: one server frame = 0.1 s ("FRAMETIME"); "time" = level time in seconds; "player" = an entity with a
client; "character" = an entity owning a character-AI object (SPEC-006); "use key" = the player's use button.
Sound calls are written as (channel, volume, attenuation); channel numbers are Quake II's (0 auto, 2 voice,
10 = 8|2 "voice, no phs"), attenuation 0 = heard everywhere. "−1 sound" = no sound (SoF uses −1, not 0, as the
"no sound" value for mover sounds).

## Interface / layout

### 1. Entity record: engine-visible prefix and new per-entity properties
The entity record is 1124 bytes (SPEC-001). The engine-visible prefix differs from Quake II:

| Offset | Size | Field |
|---|---|---|
| 0x000 | 116 | entity state (SPEC-202 §2; modelindex at +0x20, secondary model +0x24, frame +0x28, skin +0x2C, effects +0x30, renderfx +0x34, solid +0x38, loop sound +0x3C, loop data +0x40, events +0x44) |
| 0x074 | 4 | client pointer |
| 0x078 | 4 | in use |
| 0x07C | 4 | link count |
| 0x080 | 152 | area link (8), cluster count (4), 32 cluster numbers (128), head node (4), area numbers (2×4) — Quake II has 16 clusters |
| 0x118 | 4 | server flags |
| 0x11C | 60 | mins, maxs, absmin, absmax, size (5 × 3 floats) |
| 0x158 | 4 | solid |
| 0x15C | 4 | clip mask |
| 0x160 | 4 | owner |
| 0x164 | 4 | Ghoul model instance (SPEC-011) |
| 0x168 | 12 | three floats, initialised to 1000.0 each by entity allocation (meaning: engine-side, UNVERIFIED) |
| 0x174 | 12 | three floats, initialised to −1000.0 each |
| 0x180 | 12 | three floats, initialised to 10000.0 each |
| 0x18C | 4 | int, initialised to −1 |
| 0x190 | 12 | (not touched by the world code) |
| 0x19C | — | start of game-private fields |

Game-private fields are not an interface; a clean implementation may lay them out freely. SoF adds these
per-entity *properties* (values used by the rules below):

| Property | Default at allocation | Meaning |
|---|---|---|
| stop speed | 0 (set to 100 the first time step physics runs) | minimum "control" speed in friction formulas |
| ground friction | 1.0 (characters: 0.01) | multiplier of ground/rotational friction |
| gravity | 1.0 | as Quake II |
| air friction | 1.0 | friction multiplier in the air (movetypes 10/11) |
| buoyancy | 0 | upward acceleration factor while waterlevel is 1 |
| elasticity | 0.5 | bounce factor of movetypes 10/11; −1 = never change velocity on impact |
| secondary model index (state) | −1 | |
| last-used time | — | set to time when a usable entity is used; usable entities start at time − 10 |
| attached-to entity | none | an attached entity runs no movetype-10/11 physics and is never pushed by movers |
| lean | 0 | the player's current lean command (−1, 0, 1) |

### 2. Movetypes and solid values
| Value | Movetype | SoF behaviour |
|---|---|---|
| 0 | NONE | think only |
| 1 | NOCLIP | origin += velocity·0.1, angles += avelocity·0.1, link, **then** think (Quake II thinks first and skips the move when the think ran) |
| 2, 3 | PUSH, STOP | pusher (§B4.4) |
| 4 | WALK | error "SV_Physics: bad movetype %i" (players are not run here) |
| 5 | STEP | §B4.6 |
| 6, 7, 8, 9 | FLY, TOSS, FLYMISSILE, BOUNCE | toss physics (§B4.5) |
| 10 | new: tumbling object (falling props, debris, shoved bodies) | §B4.7 |
| 11 | new: as 10; additionally its touch function runs even while it is not solid (thrown weapons/gore) | §B4.7 |

| Solid | Meaning |
|---|---|
| 0–3 | NOT, TRIGGER, BBOX, BSP as Quake II |
| 4 | new solid class that counts as standable ground like BSP; an entity standing on it probes 2.5 units down instead of 0.25 (UNVERIFIED which entities use it) |
| 5 | new: corpse. Moves with a reduced box (−8,−8,mins.z)–(8,8,8), does not touch triggers after moving; set on dying players and characters |

### 3. Flag bits used by world code
| Field | Bit | Meaning |
|---|---|---|
| server flags | 0x01 | no client (Quake II) |
| | 0x02, 0x04 | dead monster, monster (Quake II) |
| | 0x08, 0x10, 0x20 | rotating door rotates about roll, pitch, yaw axis (set at spawn; engine collision hint, UNVERIFIED) |
| | 0x40 | player is inside a trigger_safezone (set by its touch; SPEC-761) |
| | 0x80 | player collision proxy (player code sets 0x82); deflects thrown equipment (§B4.8) |
| entity flags | 0x01, 0x02 | fly, swim (Quake II) |
| | 0x10 | god mode (Quake II FL_GODMODE); also makes entities immune to ignition |
| | 0x400 | team slave (Quake II) |
| | 0x8000 | lean proxy of a player: treated like a player by trigger_multiple; follows its owner (§B4.2) |
| | 0x10000 | entity was created by a spawner (SPEC-761) |
| | 0x400000 | thrown/placed equipment (grenade, C4, mine) |
| | 0x800000 | entity is registered in the item list (removed from it when freed) |
| effects (state) | 0x08 / 0x10 | button idle / pressed animation group (Quake II EF_ANIM01 / EF_ANIM23 have other values) |
| | 0x20 / 0x40 | animate all frames / fast (Quake II EF_ANIM_ALL / EF_ANIM_ALLFAST) |
| | 0x1000 | set on a func_button with spawnflag 2 (UNVERIFIED visual meaning) |
| renderfx (state) | 0x20 | target_laser beam |

### 4. Entity keys (the 96 keys of the key table)
Keys are matched case-insensitively. Types: int (decimal via atoi), short (atoi, 16 bits), float (atof), string
(copied; `\n` in the value becomes a newline, `\` followed by any other character becomes `\`), vector (three
floats "%f %f %f"), angle (one float → yaw; pitch and roll set to 0). "Spawn-time" keys live in the spawn-temp block
(cleared before every entity is parsed) and are only visible while the entity spawns. A key not in the table prints
`<key> is not a field` (developer print). Keys beginning with `_` are skipped silently. `light` is accepted and
ignored.

| Key | Type | Storage | Default / notes | Read by |
|---|---|---|---|---|
| classname | string | entity | — | spawning |
| origin | vector | entity | 0 0 0 | everything |
| model | string | entity | — | brush entities, test_model |
| spawnflags | int | entity | 0; bits 0x100/0x200/0x400/0x800 are consumed at spawn (§B1.3) | everything |
| speed | float | entity | per class | movers, triggers, targets, path following |
| accel | float | entity | per class | movers (stored ×0.1 for func_plat) |
| decel | float | entity | per class | movers |
| target | string | entity | — | target firing (§B3.5) |
| targetname | string | entity | — | target firing |
| scripttarget | string | entity | — | characters (AI spec) |
| pathtarget | string | entity | — | trains, path_corner, point_combat, func_clock |
| deathtarget | string | entity | — | characters |
| killtarget | string | entity | — | target firing; level spawner (SPEC-761) |
| killfacing | string | entity | — | characters; copied into delayed-use helpers but not used by target firing |
| combattarget | string | entity | — | characters |
| soundname | string | entity | — | sound set of movers and spawners (§B5.1) |
| spawn1, spawn2, spawn3 | string | entity | — | worldspawn precache + world spawner; level spawners (SPEC-761) |
| message | string | entity | — | target firing, doors, breakables |
| sp_message | short | entity | 0 = none | string-package message id used instead of `message` (doors, buttons, target firing) |
| team | string | entity | — | team chains (Quake II), func_score |
| wait | float | entity | per class | timers, triggers, movers, spawners |
| delay | float | entity | 0 | delayed target firing; per class |
| random | float | entity | 0 | func_timer |
| style | int | entity | 0 | lights, areaportals, func_clock, camera field of view (SPEC-761) |
| count | int | entity | 0 (per class) | many |
| health | int | entity | 0 (per class) | damageable entities; reused as a parameter by several new classes |
| sounds | int | entity | 0 | func_water (1/2), worldspawn CD track, trigger_multiple |
| light | — | ignored | — | — |
| dmg | int | entity | per class | movers, hurt, targets |
| angles | vector | entity | 0 0 0 | |
| angle | angle | entity (angles) | — | Quake II "angle hack" |
| mass | int | entity | 0 | characters/props; level spawner limit (SPEC-761) |
| volume | float | entity | per class | target_speaker; reused as a parameter by new classes (SPEC-761) |
| attenuation | float | entity | 0 → 1; −1 → 0 | target_speaker |
| map | string | entity | — | target_changelevel |
| skin | int | entity (state skin) | 0 | models |
| alertradius | float | entity | 0 | characters |
| personality | int | entity | 0 | characters |
| lip | int | spawn-time | per class | plat/button/door/water/portcullis travel; spawner view radius (SPEC-761) |
| distance | int | spawn-time | rotating doors 90 | rotating doors, some props |
| height | int | spawn-time | per class | func_plat, trigger_monsterjump, some effects |
| noise | string | spawn-time | — | target_speaker, target_secret, target_goal |
| pausetime | float | spawn-time | 0 | func_timer start offset (Quake II) |
| item | string | spawn-time | — | trigger_key (presence check only) |
| gravity | string | spawn-time | — | worldspawn (`sv_gravity`), trigger_gravity |
| sky | string | spawn-time | "none" | worldspawn |
| skyrotate | float | spawn-time | 0 | worldspawn |
| skyaxis | vector | spawn-time | 0 0 0 | worldspawn |
| terrainset | string | spawn-time | — | worldspawn |
| musicSet | string | spawn-time | — | worldspawn |
| color | vector | spawn-time | 0 0 0 | func_fade, environ_skyfade, items, Ghoul glue |
| endpoint | vector | spawn-time | — | environ_skyfade |
| minyaw | float | spawn-time | — | parsed, never read |
| maxyaw, minpitch, maxpitch | float | spawn-time | — | mounted-gun props |
| nextmap | string | spawn-time | — | worldspawn |
| script | string | spawn-time | — | worldspawn: create a `script_runner` |
| killedvalue, survivalvalue | int | spawn-time | — | parsed, never read |
| material | int | spawn-time | — | func_breakable_brush (SPEC-761) |
| scale | float | spawn-time | — | some props, Ghoul glue |
| merctype, mercnum | int | spawn-time | — | parsed, never read |
| waitaction1 … waitaction6 | string | spawn-time | — | path_corner, point_combat (§B8.1) |
| moveaction | string | spawn-time | — | path_corner, point_combat |
| screeneffect | int | spawn-time | 0 | worldspawn (configstring 15) |
| spawnfrequency | float | spawn-time | 0 | worldspawn: stored, never read afterwards |
| parm1 … parm10 | string | spawn-time | — | script_runner |
| surfacetype | int | spawn-time | 0 | func_breakable_brush, script_runner |
| noweapons, setweapons | int | spawn-time | 0 | worldspawn (§B1.4) |
| maxDeadHostages | int | spawn-time | 0 → 3 | worldspawn (§B9.4) |
| ambientSet | string | spawn-time | "sound/sound.ams" | worldspawn |
| startmusic | int | spawn-time | 0 | worldspawn |
| forceHUD | int | spawn-time | 0 | worldspawn (HUD) |
| weaponsAvailable | int | spawn-time | 0 | worldspawn (§B1.4) |
| ainame | string | spawn-time | "" | worldspawn: level identifier string used by character AI |
| distcull, chopsky | int | spawn-time | — | both keys write the same storage; never read |

## Behaviour

### B1. Spawning
B1.1 **Dispatch** (Quake II `ED_CallSpawn`): no classname → developer print `ED_CallSpawn: NULL classname`,
nothing else (the entity stays); `_` prefix → freed; when cvar `gl_pictip` ≠ 0, a classname starting with `misc_`
whose spawnflags include 64 is freed; then the item table (SPEC-006): on a hit the game rules decide whether the item
may appear (SPEC-006 game modes) and, if so, the item is spawned — if not, the entity is left as an inert record;
then the classname tables (exact, case-sensitive); else `<classname> doesn't have a spawn function` and free.

B1.2 **Parsing** (`ED_ParseEdict`, `ED_ParseField`): as Quake II with the key table of §4 (spawn-temp block is
268 bytes and cleared per entity). An entity block with no key/value pairs leaves the record zeroed (Quake II).

B1.3 **Level spawn** (`SpawnEntities`), in order:
1. save client data; reset the level-script system; free level memory; clear the level state; free every entity
   and clear the array; copy map name (63 chars) and spawn point (511 chars);
2. for each client slot: entity health = 100, client pointer attached (client record 1536 bytes);
3. loading progress 0.05, reset Ghoul registries, 0.075, precache material groups present in the map (debris
   sounds), reset the material-flag string (§B1.5), reset game rules, 0.1, register string packages `items`,
   `weapons`, `general`, reset navigation, 0.15, reset bodies and items; cvar `fs_createpak` (default "0") ≠ 0
   turns on "precache everything" mode while entities spawn;
4. parse every entity block (`ED_LoadFromFile: found %s when expecting {` on bad input). The first block is the
   world. For every other entity the difficulty/mode spawnflags are applied before spawning:
   - deathmatch: removed when spawnflags has 0x800;
   - otherwise, with T = current toughness level (§B9.2, 0–4): T 0 or 1 → removed when 0x100; T 2 → removed when
     0x200; T 3 or 4 → removed when 0x400;
   - survivors have bits 0x100–0x800 cleared (bit 0x1000 is kept; Quake II clears 0x1F00); removed entities are
     counted and reported `%i entities inhibited`;
   - after each entity spawns its material/surface type is merged into the material-flag string (§B1.5);
5. after the last entity: finalize items; game-rules level start; progress 0.25; configstring 16 = material-flag
   string; precache the flagged debris/impact sounds; precache effects `environ/waterspurt`, `environ/oilspurt`,
   `environ/chemspurt`, `environ/metal_computer`, `environ/glass_computer`, `environ/metal_steam`,
   `environ/bulletsplash`, `environ/splatgreen`, `environ/splatorange`, `environ/splatred`, `environ/splatbrown`,
   `environ/splatblack`; `%i entities inhibited`; team linking (`G_FindTeams`, message `%i teams with %i entities`,
   Quake II); player trail init; progress 0.28; ambient sound init; weapon availability (§B1.4); notify the player
   module (level start, then available weapons); Ghoul model registration (SPEC-001 import 6); progress 0.33.

B1.4 **worldspawn** (Quake II `SP_worldspawn` replaced). The world is movetype PUSH, solid BSP, model index 1.
Keys and effects:
- `message` non-empty → configstring 0 and level name; else level name = map name;
- configstring 2 = `sky` or `none`; 14 = `terrainset` (only if set); 4 = `"%f"` skyrotate; 3 = `"%f %f %f"` skyaxis;
  1 = `"%i"` sounds; 31 = `"%i"` maxclients; 15 = `"%i"` screeneffect; 13 = `musicSet` (only if set);
  12 = `ambientSet`, or `sound/sound.ams` when absent; `startmusic` stored for the music system;
- `nextmap` copied to the level's next map; `ainame` copied to the AI level-identifier string;
- `forceHUD`, `weaponsAvailable` stored. After spawning, every cvar registered with flag bits 0x100|0x200|0x400 whose
  name matches an entry of the weapon-name list (first entry "nothing", bit = list index) and whose value is
  non-zero ORs that bit into weaponsAvailable; the result is passed to the player module (SPEC-003);
- `noweapons` ≠ 0 → weapon mode 0; else `setweapons` > 0 → mode setweapons; in mode 0 or 1 the world gets a
  weapon-restriction helper with value −1.0 (mode 0) or −2.0 (mode 1) (UNVERIFIED effect; verify with a map
  using these keys);
- `maxDeadHostages` (0 → 3) stored (§B9.4); countdown-expired flag, mission status and alarm flag cleared;
  body queue initialised; world-spawner state reset (SPEC-761 §8);
- `script` set → a `script_runner` entity is created and spawned;
- images `pics/scope/arrows128_1` … `_4` precached; cvar `sv_gravity` = `gravity` key or `800`; `timescale` = `1.0`;
- light styles (configstring 1159 + n): 0 `m`; 1 `mmnmmommommnonmmonqnmmo`;
  2 `abcdefghijklmnopqrstuvwxyzyxwvutsrqponmlkjihgfedcb` (one letter shorter than Quake II's);
  3 `mmmmmaaaaammmmmaaaaaabcdefgabcdefg`; 4 `mamamamamama`; 5 `jklmnopqrstuvwxyzyxwvutsrqponmlkj`;
  6 `nmonqnmomnmomomno`; 7 `mmmaaaabcdefgmmmmaaaammmaamm`; 8 `mmmaaammmaaammmabcdefaaaammmmabcdefmmmaaaa`;
  9 `aaaaaaaazzzzzzzz`; 10 `mmamammmmammamamaaamammma`; 11 `abcdefghijklmnopqrrqponmlkjihgfedcba`;
  12 `mkigegik` (new); 13 `ex` (new); 63 `a`;
- `spawn1`, `spawn2`, `spawn3`: each non-empty classname is spawned once and immediately freed (precaches its
  resources; precache-everything mode on during this); the same keys feed the world spawner (SPEC-761 §8);
- cvar `sv_globalrankings` (default "0") registered; when 0 the bot system is initialised.
Removed from Quake II: CD/sky defaults other than above, status bar layout, item precaches, `snd_fry`, the
"unit1_" sky.

B1.5 **Material-flag string (configstring 16).** 25 characters, initially all `n`. Spawned entities set
characters to `y` according to their material (key `material`, stored by breakables) and surface type:

| material | positions set |
|---|---|
| 1, 3, 4, 5, 6 | 0 |
| 7 | 1, 2, 3 |
| 8–11 | 4, 5, 6 |
| 12–16, 32, 34 | 7, 8, 9, 10 |
| 17–20 | 15, 16, 17 |
| 21–31, 33 | 11, 12, 13, 14 |

| surface type | positions set |
|---|---|
| 32 | 11 |
| 37 | 1 |
| 38 | 1, 11 |
| 39 | 21 |
| 41 | 22 |
(Surface types also select debris/impact sound groups for precaching; exact sound list: open question.)

### B2. Entity allocation and freeing
- Allocation (`G_Spawn`/`G_InitEdict`): search from maxclients+1 for a free record whose free time is < 2 s or more
  than 0.5 s ago (Quake II), else append; reaching `maxentities` is the error `ED_Alloc: no free edicts`. Fields
  initialised: in use, classname `noclass`, state number, the engine-visible defaults of §1, secondary model −1,
  count 0 and the physics properties of §1.
- Freeing (`G_FreeEdict`): remove from the item list (flag 0x800000), destroy any attached helper object, remove
  from the Ghoul registry, unlink, destroy and unregister the character-AI object, detach from any attachment chain.
  Records numbered ≤ maxclients + 8 (body queue) are not cleared; only their state renderfx is zeroed. Others are
  zeroed, classname `freed`, free time = time (Quake II).
- A second variant frees and sets free time 0 (record immediately reusable) — used by player code.

### B3. Utilities
B3.1 `G_Find(from, field, match)` has an optional length: when given, only the first n characters are compared
(case-insensitive); otherwise as Quake II.

B3.2 `findradius(from, origin, radius, area)`: candidates are the entities linked in area `area` (1 = solid,
2 = triggers) whose box touches origin ± radius. For area 1 the entity centre (origin + (mins+maxs)/2) must be within
`radius`; for area 2 every candidate in the box is returned. Entities that are not linked (or not solid for area 1)
are never found (Quake II walks all entities, skipping only SOLID_NOT).

B3.3 Radius lists (new): up to 1024 entities whose centre is within the radius, collected from the solid area,
the trigger area or both; used by doors, explosions, camera shake, ignition, spawners.

B3.4 `G_PickTarget`: as Quake II (up to 8 candidates; messages `G_PickTarget called with NULL targetname`,
`G_PickTarget: target %s not found`).

B3.5 `G_UseTargets(ent, activator)`:
- delay ≠ 0 → a helper entity (`DelayedUse`) copies message, target, killtarget and killfacing (not sp_message),
  fires after `delay`; `Think_Delay with no activator` when activator is missing (Quake II);
- message: when message or sp_message is set and the activator is not a monster: sp_message ≠ 0 → string-package
  message sp_message to the activator, else centre print of `message`; then a sound on the activator: the entity's
  noise (if any) else `Misc/Talk.wav`, played (0, 0.6, 1).
- killtargets and targets as Quake II (doors skip `func_areaportal` targets; `WARNING: Entity used itself.`,
  `entity was removed while using killtargets`, `entity was removed while using targets`).

B3.6 Angles: `G_SetMovedir` as Quake II. `vectoyaw` returns 0 for (0,0), otherwise trunc(atan2(y,x)·180/π), +360 if
negative (Quake II returns −90 for x = 0, y < 0). `vectoangles` is continuous: yaw = atan2(y,x)·180/π (range −180…180,
not wrapped, not truncated), pitch = −atan2(z, √(x²+y²))·180/π, roll 0.

B3.6a Shared math (`q_shared.c`) as Quake II except: `anglemod` reduces an angle into [0, 360) by repeated
±360 without 16-bit quantisation; `COM_Parse` keeps tokens of up to 255 characters (quoted strings are truncated at
256; an unquoted token of 256 or more characters comes back empty — UNVERIFIED edge case); `Info_Validate` rejects
`"` and `;` (Quake II).

B3.7 `KillBox` (mask player-solid, 100000 damage, no-protection, telefrag) and `G_TouchTriggers` (skips dead
players/monsters; up to 1024 triggers) as Quake II.

B3.8 **Use-key target selection** (new; run once per press of the use button, edge-triggered): from the eye
(origin + view height):
1. a trigger-class entity (classname contains `trigger`) with a use-key handler whose box overlaps a cube of half the
   player's width (16) around the eye;
2. else the solid entity with a use-key handler, centre within 80 units, whose horizontal direction from the player
   has the highest dot product (≥ 0.2) with the horizontal view direction and is reachable by a line trace from the
   eye (the trace hits the entity or nothing).
3. else the entity hit by a 75-unit trace along the view (mask player-solid) if it has a use-key handler.
The chosen entity's use-key handler is called with the player as both other and activator. Entities that respond
to the use key ("use-key handler") are listed per class in this spec and SPEC-761.

### B4. Physics (`g_phys.c`)
B4.1 Gravity is a vector: each gravity step adds sv_gravity·gravity·sv_gravityx·0.1 to vx, sv_gravity·gravity·
sv_gravityy·0.1 to vy and **subtracts** sv_gravity·gravity·sv_gravityz·0.1 from vz (defaults 0, 0, 1 reproduce Quake
II). Velocity components are clamped to ±sv_maxvelocity (Quake II).

B4.2 Think (`SV_RunThink`): runs when nextthink > 0 and ≤ time + 0.001 and the entity has a think function or a
character-AI object; nextthink is cleared first. Characters: if the AI reports it is not in control, the AI's
pre-step and then the entity think run; otherwise the AI think runs and then the entity touches triggers. A missing
think function is not an error. When not thinking and flag 0x8000 (lean proxy) is set, the proxy is moved to its
owner's origin + right·(−30·owner lean) (right from the owner's view angles) and touches triggers.

B4.3 `ClipVelocity`: stop epsilon 1.0 (Quake II 0.1). When in·normal = 0 the input vector is first halved (in
place) and backoff = |in|·overbounce, so the result is pushed off the plane.

B4.4 Pushers (`SV_Push`, `SV_Physics_Pusher`): moves are not rounded to 1/8 unit; only entities whose state solid is
non-zero (i.e. solid bounding boxes/brushes) and that are not attached are tested and moved; otherwise as Quake II
(riders, rotation, team moves, blocked callback, nextthink bump on failure, `pushed_p > &pushed[MAX_EDICTS],
memory corrupted`).

B4.5 Toss (`SV_Physics_Toss`): after the think and the team-slave/ground checks of Quake II, gravity is skipped for
FLY and FLYMISSILE; while waterlevel is 1, vz += sv_gravity·buoyancy·0.1. `SV_PushEntity` does not move a TOSS
entity that starts all-solid. On impact: ClipVelocity with overbounce 1.5 (BOUNCE) or 1.0; if normal.z > 0.7 and
(vz < 30 or movetype ≠ BOUNCE) the entity takes the ground entity, **becomes MOVETYPE_STEP**, vz = 0 (horizontal
velocity kept) and step physics runs immediately. Landing events (below). Waterlevel = 1 when the point contents
include lava, slime or water, else 0; no splash sounds. Team slaves follow (Quake II).

B4.6 Step (`SV_Physics_Step`): stop speed defaults to 100 the first time. Corpses (solid 5) recheck ground first.
Rotation: avelocity components with |a| < 1 become 0; angles += avelocity·0.1; then rotational friction (§B4.9).
Gravity conditions as Quake II (vector gravity of B4.1). Vertical friction:
FL_FLY: speed = |vz|, control = max(speed, stop speed), new = speed − ground friction·200·control·0.1 (equals Quake
II's 0.2·control for characters, whose ground friction is 0.01); FL_SWIM: as Quake II (control·waterlevel·0.1).
Horizontal friction (when on ground or flying/swimming, unless dead and not supported, as Quake II): living:
new = speed − stop speed·ground friction·60 (a constant decrement — 60 per frame for characters — instead of
Quake II's 0.6·max(speed, 100)); dead (health < 0): new = speed − ground friction·300; clamp ≥ 0, scale vx, vy. A corpse on the ground whose body is still settling (UNVERIFIED condition
from the body object) has its velocity scaled by 0.5 (vz ≤ 0) or 0.2. Move with mask solid (characters: monster
solid), link, touch triggers unless a corpse; characters then run burning damage (SPEC-761 §9); then think. No
`world/land.wav`.

B4.7 Movetypes 10 and 11 (new mover): think first; then, unless attached: recheck ground when moving or when the
ground entity is not the world; clamp velocity; gravity (when not on ground and gravity ≠ 0, or when any of
sv_gravityx/y is non-zero or sv_gravityz < 0) plus buoyancy as in B4.5; if moving: up to 4 bumps, 5 clip planes,
using the entity's clip mask. Each bump traces the move, then tests the end position with a box shrunk by 1 unit:
if that is solid against anything except a player proxy (server flag 0x80), velocity becomes 0 (taking a standable
BSP ground entity when the plane allows) and the move ends; for a proxy, the proxy's owner is used as the hit entity.
Landing (normal.z > 0.7) sets ground on BSP or solid-4 entities and emits the landing event; impact (§B4.8) can stop
the move. Velocity is reflected with elasticity e: v' = e·(v − 1.8·(v·n)·n), with |vx|, |vy| < 1 → 0 and
|vz| < 100 → 0; e = −1 keeps v. Then ground recheck, rotation and rotational friction, linear friction (§B4.9), link.

B4.8 Impact (`SV_Impact`), returns whether movement continues:
- if the obstacle is a player proxy (server flag 0x80): same owner → stop silently; else if the mover is thrown
  equipment (flag 0x400000) with health ≠ 31337: on a near-floor plane (normal·(0,0,1) > 0.8) vz = −vz and all
  components ×0.4, else all ×(−0.2); the mover's touch runs (if solid or movetype 11); the proxy's touch does not
  run; stop;
- otherwise: mover touch (if solid or movetype 11) with the plane and surface, then obstacle touch (if solid), and
  continue.

B4.9 Friction helpers: rotational — on ground ground friction·12, in air air friction·0.01, subtracted from each
avelocity component toward 0 per frame; linear (movetypes 10/11) — on ground ground friction·60, in air air
friction·0.01, per velocity component toward 0.

B4.10 `SV_FlyMove`: corpses use the reduced box; after each trace the end position is tested again: if it is solid,
the entity takes ground when appropriate and its velocity becomes −normalize(v) + a random value in [−1, 1] per
component (unstick), result 7. Landing on BSP/solid-4 sets ground and, for living non-player entities with
vz ≤ −100, adds entity event 10 (≤ −100), 11 (≤ −200) or 12 (≤ −400) with parameter 0 (UNVERIFIED parameter).
Impact returning "stop" ends the move. Overbounce is 1.1 when the trace fraction is 0, else 1.0; movetype NOCLIP
skips plane clipping. The final check stops only the vertical component: if vz changed sign relative to the start,
vz = 0 and the move ends (Quake II zeroes the whole velocity when v·primal ≤ 0). Max 4 bumps, 5 planes.

B4.11 Ground check for step/corpses: trace 0.25 units down (2.5 when standing on solid 4) with the entity's clip
mask (corpse box for solid 5): not started solid → no hit: no ground; normal.z ≥ 0.7: snap to end, take ground;
started all-solid: take the hit entity as ground; otherwise no ground.

### B5. Brush movers (`g_func.c`)
B5.1 **Mover sounds.** Quake II's `sounds`/`noise` mover sounds are replaced by key `soundname`: the name of a sound
set in the ambient sound-set file (sound/sound.ams, see the sound-set spec); entries 0, 1, 2 of the set are the
start, loop and end sounds (case-insensitive name match; empty or unknown name → no sound). They are looked up
0.1 s after spawn. Start/end sounds play (10, 1, 1) on the mover; the loop sound is set as the entity's looping
sound with full volume (loop data 248, attenuation class 0). func_water keeps Quake II's `sounds` 1/2
(`world/mov_watr.wav`, `world/stp_watr.wav`).

B5.2 **Crushing** non-player, non-monster obstacles: damage 100000 (MOD 22) then the obstacle is freed (Quake II
turns it into an explosion). Mover damage uses means-of-death 22.

B5.3 func_plat: as Quake II except sounds (B5.1).

B5.4 func_rotating: as Quake II except the loop sound (B5.1) and the animate flags 64/128 setting effects
0x20/0x40.

B5.5 func_button (defaults as Quake II: speed 40, wait 3, lip 4):
- effects 0x08 at rest; pressed → effects 0x10 and frame 1; returned → frame 0;
- the start sound is the set's entry 0; additionally, when `soundname` is not set, every use plays
  `Ambient/Buttons/HiTech/Elevator1.wav` (2, 0.6, 1); spawnflag 2 sets effects 0x1000;
- without targetname the button responds to the use key; it is touch-activated only with spawnflag 1
  (Quake II: always touchable when it has neither targetname nor health);
- using it while it is at rest-pressed (top) only refreshes its last-used time; when it starts moving and
  `sp_message` is set, that message is printed to the user with `Misc/Talk.wav` (0, 1, 1).

B5.6 func_train: blocked damage uses damage flags 0x8008 (UNVERIFIED meaning); Quake II's `noise` loop is replaced by
the sound set; otherwise Quake II (including `train_find: no target`, `train_find: target %s not found`,
`train_next: bad target %s`, `connected teleport path_corners, see %s at %s`, `func_train without a target at %s`).
trigger_elevator, func_timer (`func_timer at %s has random >= wait`), func_conveyor, func_killbox: Quake II.

B5.7 **Doors** (func_door, func_door_rotating, func_door_secret; new door classes in SPEC-761). Defaults as Quake II
(speed 100 — doubled in deathmatch for func_door —, wait 3, lip 8, dmg 2, rotating distance 90 without the "no
distance set" message). Spawnflags: 1 START_OPEN, 2 REVERSE, 4 CRUSHER, 8 NOMONSTER, 16 **LOCKED** (new),
32 TOGGLE, 64/128 rotating axis (func_door_rotating), 128 **TRIGGER FIELD** (func_door only, new), 0x2000 **PLAYER
USABLE** (new), 0x4000 no axis server flag (rotating, new), 0x8000 **FIRE TARGETS WHEN CLOSING** (new). Quake II's
animate spawnflags 16/64 are not used by doors.
- **Activation sources.** A door is used either "directly" (a player or character via the use key, or the door's
  trigger field) or "by an event" (target firing, scripts, damage). Direct use is possible only when the door has no
  targetname or has spawnflag 0x2000; otherwise direct use does nothing. Doors without targetname, or with 0x2000,
  respond to the use key. Quake II's automatic trigger field exists only for func_door with spawnflag 128 (its
  bounds: union of the team's bounds, x and y expanded by 60; touch debounce 1 s; requires a living player or monster,
  NOMONSTER excludes monsters; a direct use); the field also responds to the use key, but only when the door has no
  targetname.
- **Locked (16).** A locked door ignores direct use. Being used by an event unlocks it (and every following team
  member that is locked) without opening it; the next use opens it. (UNVERIFIED for mixed locked/unlocked teams.)
- **Opening** (`door_go_up`): as Quake II; plus, when the entity that used the door is a `script_runner`, that
  script entity (instead of the use's activator) becomes the activator; message and sp_message and the touch are cleared on all team members (Quake II
  clears message and touch). Rotating smart doors: SPEC-761.
- **Auto-close.** On reaching the open position a non-toggle team master with wait ≥ 0 schedules, after `wait`,
  a *clearance check*: for each team member, every player or character with health > 0 whose centre is within
  96 units of that member's origin point blocks the close; if blocked the check repeats every 1.0 s. When clear:
  the start sound plays, the loop starts, targets fire when spawnflag 0x8000 is set (activator = the door), and
  every team member starts closing. Note: a func_door's origin is its movement offset (normally near the world
  origin), so for brush doors without an origin brush the check practically never blocks; rotating doors are checked
  around their hinge (UNVERIFIED in-game). Toggle doors use the same clearance check when toggled shut.
- **Closing** (`door_go_down`): no sound of its own (Quake II plays start/loop here); otherwise as Quake II.
- **Blocked** (`door_blocked`): an obstacle whose state solid value is 5 has it reset (no gameplay effect found);
  non-player non-monster obstacles are crushed (B5.2). Non-crusher doors: a character obstacle receives 1 damage with
  knockback 100 pushing it horizontally away from the door (direction (obstacle − door).xy, z 10) and the door keeps
  trying; a player obstacle makes the door (wait ≥ 0) reverse: closing doors reopen (team), opening doors run the
  clearance check; **players are never damaged**. Crusher doors: a character is shoved 50 units sideways
  (perpendicular to the door's move direction in the horizontal plane) and takes 1000 damage, knockback 100, damage
  flag 0x8000; players are not damaged (UNVERIFIED in-game).
- **Area portals.** Instead of setting portal states, doors *toggle* each targeted `func_areaportal` (whose own
  state toggles too). Each door remembers whether it currently holds its portals open: a normal door toggles its
  portals when it starts opening (if not already holding them) and holds them; it toggles them back when it finishes
  closing. A START_OPEN door toggles its portals once 0.1 s after spawn (it holds them), toggles them when it
  finishes closing, and toggles them again when it starts opening. A func_door with both START_OPEN and the trigger
  field toggles twice at spawn (net closed — original quirk). Two doors sharing one portal therefore combine by XOR.
- **Touch message** (doors with targetname and message/sp_message): debounce 5 s, centre print or string-package
  message, sound `Misc/Talk.wav` (0, 1, 1); precache `misc/talk.wav`.
- **Killed** (doors with health): as Quake II.
- func_door_secret: also responds to the use key; no sounds; damage-triggered as Quake II; portals toggled.

B5.8 func_water: as Quake II (behaves as a func_door; `wait` −1 means toggle).

### B6. Triggers (`g_trigger.c`)
- trigger_multiple / trigger_once: entities that count as players are players and lean proxies (flag 0x8000);
  monsters still need spawnflag 1; spawnflag 2 excludes players. New spawnflag 8 **USE KEY**: not touchable, fires
  when a player presses the use key at it (also after enabling with spawnflag 4). `sounds` 1–3 all select
  `misc/talk.wav` as the message sound. Firing records the last-used time. trigger_once has no Quake II
  "old map" conversion of spawnflag 1.
- trigger_relay, trigger_always (minimum delay 0.2), trigger_gravity (`trigger_gravity without gravity set at %s`),
  trigger_monsterjump, trigger_elevator: Quake II.
- trigger_key: validates `item` (`no key item for trigger_key at %s`) and target (`%s at %s has no target`),
  precaches `misc/keytry.wav`, `misc/keyuse.wav`, but **using it does nothing** (no key system).
- trigger_counter: messages are string-package ids 0x060B (with the remaining count; Quake II "%i more to go...")
  and 0x060C (Quake II "Sequence completed!") sent to the activator with `Misc/Talk.wav` (0, 0.6, 1), suppressed by
  spawnflag 1; count default 2.
- trigger_push: no wind sound; an entity whose targetname is `sib2_heli` is never pushed; new spawnflag 2
  **TOGGLE**: the push starts disabled and each use toggles it; spawnflag 1 PUSH_ONCE as Quake II; speed default 1000;
  `grenade` classname special case kept.
- trigger_hurt: use always toggles it (Quake II only with spawnflag 2); no hurt sound is precached (the sound index
  used is 0 — silent); new spawnflag 32 **PLAYERS ONLY**; new spawnflag 64 adds damage flag 2 (armour bypass,
  UNVERIFIED); spawnflag 8 gives damage flag 0x20 (no protection); means of death 32; without spawnflag 16 every
  toucher is damaged every frame (Quake II limits to one toucher per frame); with spawnflag 16 all touchers of the
  frame in which it fires are damaged, then 1 s pause.
- New triggers: SPEC-761.

### B7. Targets (`g_target.c`)
- target_speaker: looping sounds always use full loop volume (loop data 248); one-shot sounds use channel 2 (|16 when
  spawnflag 4), volume and attenuation from the keys.
- target_help: as Quake II (512-byte messages, removed in deathmatch).
- target_secret, target_goal: sound volume 0.6 on channel 2; goal completion sets configstring 1 to `0`; no Quake II
  map hack.
- target_explosion: no temp-entity explosion effect; radius damage dmg with radius dmg + 40 (means of death 26);
  characters within dmg units receive an explosion stimulus of strength (1 − d/dmg)·dmg (max 100) (AI spec).
- target_splash: **no splash effect**; when dmg ≠ 0, radius damage dmg with radius 5 (Quake II: dmg + 40), means of
  death 33, and the explosion stimulus as above.
- target_changelevel: ignored during intermission. Single player: ignored while player 1 is dead; the player's
  inventory is saved to the persistence cvars (SPEC-003, clips added to reserve) with current health; the level's
  mission statistics (kills, friendly kills, special shots, gibs, saves, money, played time …) are added to the
  campaign totals published by `getgamestats` (SPEC-001); the player module is notified. Deathmatch: any entity other
  than the world that uses it takes 10·max_health damage, knockback 1000, damage flag 0x20, means of death 29
  (there is no DF_ALLOW_EXIT); `%s exited the level.` only after that check (cannot be reached by a player in
  practice). `*` in the map name clears the cross-level flags (Quake II). Message `target_changelevel with no map at %s`.
- target_spawner, target_crosslevel_trigger/target, target_lightramp (light styles at configstring 1159+), target_
  earthquake (`world/quake.wav`): as Quake II.
- target_laser: flags as Quake II (colours, fat beam frame 16, start on); renderfx 0x20; damage flags 4, means of
  death 34; trace mask solid|monster|deadmonster; **no laser sparks and the beam end point is not updated**
  (UNVERIFIED whether a beam is visible).
- target_blaster, target_actor: absent.

### B8. Miscellaneous (`g_misc.c`)
B8.1 path_corner / point_combat: keys `waitaction1`…`waitaction6` and `moveaction` name entries of the character
animation table (case-insensitive; table owned by the AI spec); unknown names print
`couldn't find path_corner waitaction: %s (using stand)` / `couldn't find path_corner moveaction: %s (using walk)`
and use the first ("stand") / second ("walk") entry. path_corner thinks every 0.5 s (no effect). Touch: when the
toucher's move target is this corner and it has no enemy: pathtarget fired (Quake II); no target → toucher's goal
and move target cleared; teleport corners (spawnflag 1) move the toucher without a teleport event; no wait/stand/
ideal-yaw handling (the AI handles it). point_combat: removed in deathmatch; no HOLD spawnflag handling; otherwise
Quake II.
B8.2 light: as Quake II (switchable styles ≥ 32 use `m`/`a`). Light fixture props reuse the same style setup.
B8.3 func_wall, func_object (clip mask monster-solid, crush damage MOD 22), target_character (frame 12), target_
string, func_clock (16-char message, TAG_LEVEL), info_notnull, info_null/func_group, func_areaportal (count starts 0):
Quake II behaviour, without Quake II's diagnostic messages for func_wall.
B8.4 Quake II gibs (`gib_*`) remain for internal use: a gib that lands plays `misc/fhit3.wav` (2, 0.6, 1), lies
flat, and disappears 8–18 s later.
B8.5 Delayed respawn helper (used by some props): an object records its spawn position, angles, health, spawn
behaviour and a delay; when triggered, a placeholder waits `delay` seconds and then spawns a fresh copy with the same
origin, angles, health, spawnflags, wait, accel, style, targetname and target.

### B9. Game loop (`g_main.c`) and difficulty
B9.1 InitGame registers the cvars of SPEC-006 (no `coop`, `needpass`, `filterban`, `g_select_empty`, gun offsets).
Parental-lock cvars are read once: `lock_deaths` ≠ 0 disables setting entities on fire (SPEC-761 §9) among other
effects. The engine sound import is wrapped: a sound marked "local" for an entity flagged as silenced is dropped;
non-local sounds are reported to the bot hearing system with radius volume·200.

B9.2 **Skill presets.** When not in deathmatch, InitGame forces the skill cvars from floor(`skill`):

| skill | sk_toughness | sk_spawning | sk_saving | cheats | sk_maxencum |
|---|---|---|---|---|---|
| < 0 or 0 | 0 | 0 | −1 | 1 | 0 |
| 1 | 1 | 1 | 8 | 1 | 1 |
| 2 | 2 | 2 | 5 | 1 | 2 |
| 3 | 4 | 3 | 2 | 0 | 3 |
| 4 | 3 | 4 | 0 | 0 | 4 |
| ≥ 5 | (unchanged — custom) | | | | |

Then the difficulty record is built: saves allowed = sk_saving, cheats = `cheats`, spawning level S =
clamp(sk_spawning, 0, 4), toughness level T = clamp(sk_toughness, 0, 4), encumbrance level E = clamp(sk_maxencum,
0, 4) (negative → 0). Parameters are copied from five rows: the toughness group (T-a … T-i) from row T, the
spawning group (S-a, and the world-spawner parameters) from row S, the encumbrance value from row E:

| Row | T-a | T-b | T-c | T-d | T-e | T-f | T-g | T-h | T-i | S-a | alert gain | alert fade time (s) | max-alert duration (s) | spawn interval at max alert (s) | max characters near player | spawn interval (s) | encumbrance |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | 0.3 | 0.25 | 0.4 | 4.0 | 0.5 | 0.5 | 0.1 | 2.0 | 1 | 2.0 | 0 | 0 | 0 | 0 | 0 | 0 | 0 |
| 1 | 0.5 | 0.4 | 0.5 | 2.0 | 1.0 | 0.5 | 0.2 | 1.0 | 1 | 1.0 | 0.15 | 10 | 4 | 2 | 2 | 15 | 10 |
| 2 | 1.0 | 1.0 | 0.8 | 1.0 | 1.0 | 1.0 | 0.3 | 1.0 | 0 | 1.0 | 0.4 | 10 | 4 | 1 | 3 | 8 | 7 |
| 3 | 3.0 | 1.5 | 1.0 | 0.5 | 1.0 | 1.5 | 0.5 | 1.0 | 0 | 1.0 | 0.5 | 12 | 6 | 1 | 3 | 6 | 5 |
| 4 | 99 | 2.0 | 2.5 | 0.3 | 99 | 3.0 | 1.0 | 0.5 | 0 | 5.0 | 1.5 | 4 | 12 | 0.5 | 4 | 2 | 5 |

T-a … T-i are consumed by character AI and damage code (meanings: AI spec, UNVERIFIED here; T-i is an integer
flag); S-a meaning UNVERIFIED; the six named spawning parameters drive the world spawner (SPEC-761 §8); the
encumbrance value is used by the game modes. The rows also hold default cheats (1,1,1,0,0) and saves (−1,8,5,2,0)
matching the preset table, but the cvar values are what is used.
A difficulty score is computed: base = 0.4 (saves −1), 2.0 (saves 0) or 1 − saves·0.05; ×0.5 when cheats; spawning:
S = 0 → ×0.3, S = 4 → ×4, else + S·0.3; toughness: T = 0 → ×0.3, T = 4 → ×3, else + T·0.4; encumbrance value 0 →
×0.5, else ×(10/value); minimum 0.2 (used by the mission-statistics/money code, UNVERIFIED).
Entity difficulty filtering (§B1.3) uses T.

B9.3 RunFrame differences (SPEC-001 lists the full order): `freezeworld` ≠ 0 stops the frame; time = framenum·0.1;
the world spawner runs each frame (SPEC-761 §8); in single player, when player 1 is dead the screen fades to black
(fade alpha +0.02 per frame) and at full black the console command `respawn` is issued; an entity whose ground
entity's link count changed loses its ground (characters then recheck ground, Quake II only for monsters);
non-player entities with a Ghoul instance get their instance advanced to the current time unless
`ghl_no_server_update` ≥ 0.5; dead-hostage rule (B9.4); in deathmatch players are ranked by score each frame
(rank, player count in the player state, SPEC-202 field 30).

B9.4 **Dead hostages.** When maxDeadHostages is not −1 and the number of dead hostages (friendly characters killed)
reaches it, every player is killed (mission failed). Default 3.

B9.5 Level end (deathmatch): `EndDMLevel` as Quake II, with the game rules deciding "same level"; map list cvar
`sv_maplist` separators as Quake II. `CheckDMRules` (game rules, UNVERIFIED details belong to the game-modes spec):
deathmatch mode 6 rule-specific end (message 0x0D0D), mode 4 flag capture end (message 0x0A0D), time limit measured
per player from that player's entry frame (`timelimit`·600 frames; message 0x3C), frag limit per player or per team
(message 0x3B). `ExitLevel`: `gamemap "%s"`, clears cinematic freeze and `sv_jumpcinematic`, clamps health to
max health.

## Constants
| Name | Value |
|---|---|
| frame time | 0.1 s |
| stop epsilon (ClipVelocity) | 1.0 |
| toss "stays on ground" speed | 30 |
| elastic reflection factor | 1.8 |
| reflection dead-zones | |vx|,|vy| < 1, |vz| < 100 |
| friction factors | ground 60, dead 300 (=600·0.5), fly 200·0.1, rotational ground 12, air 0.01 |
| default stop speed / elasticity | 100 / 0.5 |
| landing events | 10 (vz ≤ −100), 11 (≤ −200), 12 (≤ −400) |
| player-proxy bounce | floor (n·up > 0.8): vz = −vz then ×0.4; else ×(−0.2) |
| door clearance radius / retry | 96 units / 1.0 s |
| door trigger field margin | 60 |
| use-key reach | trigger cube 16; solid 80 units, dot ≥ 0.2; trace 75 units |
| radius list capacity | 1024 |
| magic health that exempts equipment from proxy bounce | 31337 |
| means of death | crush 22, telefrag 23, explosion 26, trigger_hurt 32, splash 33, laser 34, exit 29 |
| configstrings | 0 name, 1 CD track, 2 sky, 3 sky axis, 4 sky rotate, 12 ambient set, 13 music set, 14 terrain set, 15 screen effect, 16 material flags, 31 maxclients, 1159+ light styles |

## Test vectors / how to verify
- `gimme func_door` on a test map with a door without targetname: pressing use opens it; a door with targetname and
  no spawnflag 0x2000 ignores the use key.
- Locked door (16) targeted by a trigger: first trigger does not open it; second does.
- Standing next to a func_door_rotating hinge: it does not close while you stand within 96 units; it closes 1 s after
  you step away.
- Non-crusher door closing on the player: the door reopens and the player takes no damage.
- trigger_hurt without spawnflag 16 touched by two players: both lose dmg health every frame.
- Map with `skill 3`: after load `sk_toughness` = 4, `sk_spawning` = 3, `sk_saving` = 2, `cheats` = 0.
- Entity with spawnflags 0x100 on `sk_toughness 1`: absent; on `sk_toughness 2`: present (developer shows
  `%i entities inhibited`).
- A thrown grenade (TOSS) landing on flat ground keeps sliding (now STEP) instead of stopping dead.
- `sv_gravityx 1`: dropped items drift along +x.
- `vectoangles` of (0, −1, 0) gives yaw −90 (Quake II 270).

## Open questions
- Meanings of the toughness-group skill parameters (T-a … T-i) and S-a — AI work package.
- Engine meaning of the three float triples and the −1 at offsets 0x168–0x18C, and of rotating-door server flags
  8/0x10/0x20.
- The debris/impact sound list precached per material group (115-entry table).
- Exact effect of `noweapons`/`setweapons`.
- Damage flags 0x8008 (train) and 0x2 (trigger_hurt spawnflag 64): confirm meanings with the damage spec.
