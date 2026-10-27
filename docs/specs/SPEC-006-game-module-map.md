# SPEC-006: Game module map — what SoF's game logic keeps, changes and adds relative to Quake II
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: gamex86.dll (server-side game module), version 1.07f
Depends on: SPEC-001 (game module interface), SPEC-003 (player module interface), SPEC-005 (engine module map),
SPEC-302 (cvars and commands)

## Purpose
Scope the game-logic work for OpenMerc. The *Soldier of Fortune* game module descends from the Quake II 3.x game
module (the `game/` directory of the Quake II GPL release) but most of it is new code. This document says, subsystem
by subsystem, whether the Quake II game source is a usable starting point, needs changes, or is absent and replaced.
It also lists the data interfaces a map can rely on: every entity classname the module can spawn, the entity keys
it reads, the item classnames, the console variables it registers and the client commands it accepts.
It describes features and external names only.

## Interface / layout

### Size and composition (for planning)
- The module is written in C++ (exceptions enabled, no run-time type information) and statically links its C and C++
  runtime libraries; its only operating-system dependency is KERNEL32.
- Roughly 4,000 game functions (about 1.0 MB of machine code) plus the runtime library. About a tenth of the game
  code corresponds to Quake II game functions; the rest is new.
- Approximate share of game code by area: character AI (brains, senses, decisions, animation-driven actions,
  per-character setup) 31 %; character bodies, gore zones and dismemberment 12 %; bots 8 %; decorative/breakable map
  objects 8 %; level scripting 5 %; player/client 5 %; Quake II world entities 4 %; navigation 4 %; game modes,
  model-system glue, server-side weapons, items about 3 % each; effects, physics, spawning, savegame, shared math the rest.

### Subsystem table
| Subsystem (Quake II game file) | SoF status | Notes for OpenMerc |
|---|---|---|
| Module entry, init, frame loop, level exit (g_main.c, g_save.c InitGame) | kept, extended | Same structure (InitGame, RunFrame, ExitLevel, CheckDMRules, EndDMLevel). Adds skill cvars, bot/AI/Ghoul cvars, player-module loading (SPEC-003), game-stats and inventory export to the engine (SPEC-001 exports), save limits. |
| Entity spawning and map entity parsing (g_spawn.c) | kept, changed | ED_ParseEdict/ED_ParseField/G_FindTeams/SpawnEntities/SP_worldspawn are recognisable. The classname lookup is split into 15 per-topic tables plus a separate item table (see Behaviour 1). The key table is different (see entity keys). |
| Entity utilities (g_utils.c) | kept, extended | G_Find, findradius (with an extra "area type" argument: solid vs trigger), G_PickTarget, G_UseTargets/Think_Delay, vtos, G_SetMovedir, vectoyaw, vectoangles, G_InitEdict, G_Spawn, G_FreeEdict, G_TouchTriggers, KillBox. No separate G_TouchSolids behaviour was found. Adds radius-list queries returning up to 1024 entities. |
| Physics / movetypes (g_phys.c) | kept, changed | SV_TestEntityPosition, SV_RunThink, SV_Impact, ClipVelocity, SV_FlyMove, SV_PushEntity, SV_Push, SV_Physics_Pusher, SV_Physics_Toss, SV_Physics_Step, G_RunEntity present; one extra movetype with its own mover; friction split into linear and angular parts; the think step also ticks an entity's AI object. |
| Brush movers (g_func.c) | kept, extended | Plats, rotating, buttons, doors (sliding, rotating, secret), water, trains, elevator trigger, timer, conveyor, killbox: same state machines as Quake II (Move_Calc/AngleMove, accelerated plat moves, door teams and area portals). Adds rotating "smart" doors, portcullis, push-killbox, remote cameras and several scripted/mission brushes (see classnames). Doors check for obstruction before closing (UNVERIFIED: verify by standing in a closing func_door). |
| Triggers (g_trigger.c) | kept, extended | multiple/once/relay/key/counter/always/push/hurt/gravity/monsterjump keep Quake II behaviour (trigger_push still special-cases the classname "grenade"). Adds activate, conquer, countdown, useable, heal, movedown, keep_using, safezone, sound, objectives. |
| Targets (g_target.c) | kept | All Quake II targets except target_blaster and target_actor; target_laser on/off handling merged. |
| Misc entities (g_misc.c) | partly kept | path_corner, point_combat, info_*, light, func_wall, func_object, func_areaportal, target_character/string, func_clock kept. Quake II gibs/debris/explobox/misc_* models (viper, banner, satellite…) are absent; replaced by the Raven map-object families (misc_generic_*, level-themed misc_*_*, light_generic_*). |
| Damage (g_combat.c) | changed | CanDamage, T_Damage, T_RadiusDamage present (plus two radius-damage variants). Damage is routed to hit locations ("gore zones") on the character body model and drives dismemberment, blood and death animations (new). Power armour, Quake II armour types absent. |
| Items (g_items.c) | replaced | Item definitions are a table of 39 item classnames (see Item classnames) held by a new inventory system shared with the player module (SPEC-003). Only droptofloor and SpawnItem keep their Quake II roles. CTF and control flags are items. |
| Weapons (g_weapon.c, p_weapon.c) | replaced | Weapon logic runs in the player module (SPEC-003); the game module supplies callbacks (fire, alt-fire, use equipment: grenade, C4, claymore, medkit, flashpack, goggles) and server-side projectiles (rockets, slugs, grenades, thrown knives, flame), muzzle flashes, tracers and impact effects. |
| Client (p_client.c) | kept, changed | ClientConnect/Begin/UserinfoChanged/Disconnect/Think, PutClientInServer, respawn, spectator_respawn, SelectSpawnPoint, body queue, ClientBeginServerFrame recognisable. Adds team spawn points (info_player_team1/2), bot clients, player model files (.gpm/.gpl under ghoul/pmodels), prediction restart messages. |
| View/HUD (p_view.c, p_hud.c) | kept, changed | ClientEndServerFrame, P_FallingDamage, P_WorldEffects, G_SetStats, intermission and scoreboard recognisable; view roll/bob cvars kept; blends/sounds differ (UNVERIFIED in detail). |
| Commands (g_cmds.c) | changed | players, say, say_team, score, help, kill, putaway, wave, playerlist remain; inventory/weapon commands moved to the player module. Cheats renamed: `heretic` toggles god mode, `ninja` notarget, `phantom` noclip (all require cheats; print "godmode ON/OFF", "notarget ON/OFF", "noclip ON/OFF"). See client command list. |
| Chase camera (g_chase.c) | kept | UpdateChaseCam, ChaseNext, GetChaseTarget. |
| Server commands (g_svcmds.c) | absent | IP filtering lives in the engine (SPEC-005). |
| Savegame (g_save.c) | replaced | Chunked savegame through the engine's chunk interface (SPEC-001, SPEC-303); entity fields are enumerated from a field table; callback pointers saved as indexes into function tables; AI objects saved in separate registries. Level-to-level carry-over follows Quake II (WriteGame/ReadGame vs WriteLevel/ReadLevel). |
| Monsters (g_ai.c, g_monster.c, m_move.c, m_*.c) | replaced | Only range (thresholds 80/500/1500 units), visible, infront, M_CheckBottom, M_CheckGround, M_CatagorizePosition, M_WorldEffects, M_droptofloor and a generic monster_start survive. All characters use a new object-oriented AI (below). No Quake II monster exists. |
| Player trail (p_trail.c) | kept | PlayerTrail_Init/Add/LastSpot. |
| Shared math (q_shared.c) | kept | Same helpers as the engine copy (SPEC-005). |
| Turrets (g_turret.c) | absent | — |
| **New: character AI** | new | Each character entity owns an AI object chosen by character type (one per m_* classname). The AI combines senses (sight, hearing, memory), an ordered list of decisions (patrol, combat, path-combat, retreat, scripted…) and animation-driven actions played on the character's Ghoul body. Characters are configured per classname: model directory (enemy/meso, enemy/ecto, enemy/female, enemy/dog…), skins, accessories bolted onto the model, weapon, health. |
| **New: bodies, gore and dismemberment** | new | Character bodies map damage locations to gore zones (head, chest, limbs, groin…), show wounds/caps, sever limbs and play matching death animations; parental-lock cvars `lock_deaths`, `lock_blood`, `lock_sever`, `lock_gorezones`, `lock_textures` restrict it. |
| **New: navigation** | new | Per-map node graph loaded from `maps/<map>.nvb` (versioned; see Behaviour 6 for the mismatch message); regions, node connections, path queries; editor helpers controlled by `ai_*` cvars. |
| **New: bots** | new | Multiplayer bots: list `bot\botlist.txt` (cvar `bot_listfile`), personalities `bot\bots\<name>.bpf` (keys `userinfo=`, `accuracy=`, `fov=`, `turning_speed=`, `view_distance=`, `general_intelligence=`, `canchat=`, `chatfreq=`), waypoints `bot\wpfiles\<map>.wrs`, per-map configs `bot\mapcfgs\<map>.cfg` (default `bot/mapcfgs/default.cfg`), waypoint editing commands (see client commands). |
| **New: level scripting** | new | Script programs `ds/<name>.os` (versioned binary; loaded per level) run by `script_runner` entities; instructions spawn entities, wait, branch, print, play/cache/unload sounds, cache string packages, play and cache ROF motion files (`ds/roff/<name>`, extension `.rof`), set cvars, run console commands, reset AI, copy player attributes, set view angles, arithmetic on script variables (int, float, vector, entity, string). |
| **New: game modes** | new | Game rules are objects selected by the `deathmatch` cvar and server settings: single player, deathmatch, team deathmatch, Arsenal, Assassin, CTF, Control, Conquer ("CTB"), Realistic. Each registers its string package and sounds (dm/…) and supplies scoreboard/log lines. Optional end-of-game log file (`log_file_*` cvars). |
| **New: map objects and lights** | new | About 200 decorative/breakable/animated model entities (see classnames) built on the Ghoul model system with damageable parts, debris and sounds. |
| **New: effects and ambience** | new | Environmental effect generators (environ_*), an effects library sending temporary effects to clients (SPEC-201), ambient sound sets from `sound/sound.ams` (versioned) selected per area (func_ambientset, environ_soundgen), music control (func_musicControl). |
| **New: spawners** | new | `spawner*` entities and a "world spawner" that bring in characters during play, governed by `ai_spawnfrequency` and skill settings. |
| **New: cinematics** | new | Remote cameras (func_remote_camera), cinematic freeze state exported to the engine (SPEC-001), cinematic text printing, view-locking entities. |
| **New: string packages** | glue only | The module registers packages "items", "weapons", "general" when a level spawns and one per game mode; messages are sent by id (SPEC-001 imports). |

## Behaviour

1. **Spawning an entity from the map.** For each entity block the module parses key/value pairs into the entity
   (unknown keys print `<key> is not a field` and are ignored, as in Quake II), then dispatches on `classname`:
   1. no classname → console `ED_CallSpawn: NULL classname`, nothing is spawned;
   2. classname starting with `_` → the entity is removed silently;
   3. when cvar `gl_pictip` is non-zero, a classname starting with `misc_` whose spawnflags include 64 is removed
      (UNVERIFIED that this is a detail-reduction switch; verify by setting `gl_pictip 1` and loading a level with
      flagged props);
   4. the item table is searched first (39 item classnames below); a hit spawns the item;
   5. otherwise the classname tables are searched (exact, case-sensitive match; the tables are disjoint, so order does
      not matter); a hit calls that classname's spawn behaviour;
   6. otherwise the console shows `<classname> doesn't have a spawn function` and the entity is removed.
2. **Classnames with no own behaviour.** `info_null` and `func_group` remove themselves. `info_player_deathmatch`,
   `info_player_team1` and `info_player_team2` share one behaviour (removed outside deathmatch, as in Quake II).
   `info_player_start`, `info_player_intermission`, `misc_exploding_heli` and `test_rj` do nothing at spawn and stay
   as plain point entities that other code looks up by classname.
3. **Characters.** Every `m_*` classname (105) selects a character type with its own AI behaviour set; there are
   about 100 types, a few classnames share one (UNVERIFIED which; behaviour differences would show in combat
   style), and dogs, helicopters, the tank and the snowcat use dedicated types. Characters share a common setup (Quake II monster_start equivalent): step movement, bounding box 32×32×(−32…+41),
   mass 200, health 100 (1000 when cvar `ai_goretest` is non-zero), dropped to the floor at level
   start unless spawnflag 16 is set (UNVERIFIED flag meaning; verify with a test map). The character's path behaviour
   comes from `target`/`combattarget` and the `path_corner` keys `waitaction1`..`waitaction6` and `moveaction`
   (animation names).
4. **Cheat commands** (only when cheats are enabled; otherwise `Cheats not available.`): `heretic` toggles god mode
   (`godmode ON`/`godmode OFF`), `ninja` toggles notarget (`notarget ON`/`notarget OFF`), `phantom` toggles noclip
   (`noclip ON`/`noclip OFF`), `gimme <classname>` spawns an entity of that classname in front of the player
   (console `Spawning : <classname>`), `killallmonsters` kills every character (`killed %d monsters`), `entlist`
   prints an entity census.
5. **Range classes** used by character AI (Quake II `range()` equivalent): melee below 80 units, near below 500,
   mid below 1500, far beyond (Quake II uses 1000 for mid).
6. **Navigation data**: a level's node file is `maps/<mapname>.nvb`; a wrong version prints
   `Invalid version (%d) for Info Point File (Should be %d) - please re-export the path data`.
   Ambient sound sets come from `sound/sound.ams`; a wrong version prints
   `Invalid version (%d) for Ambient Sound Set (Should be %d)`.

### Entity classnames (422 in the classname tables)
Descriptions marked (Quake II) behave as in Quake II 3.19 unless the subsystem table says otherwise. All other
descriptions are inferred from the classname and from the assets and sounds the spawn code uses — UNVERIFIED in
detail; verify by spawning each classname with `gimme` in a cheat-enabled game or by inspecting the shipped maps'
entity strings.

#### Effects (environ_*) (27)

| classname | description |
|---|---|
| `environ_bloodrain` | falling blood particle effect (gore rain) |
| `environ_chunk_spewer` | spews debris chunks of a material (needs count) |
| `environ_cloudtrigger` | switches cloud layer when triggered |
| `environ_dustsource` | dust particle source |
| `environ_effect` | generic one-shot client effect by name |
| `environ_effect_continual` | generic continuous client effect by name |
| `environ_emergency_lights` | rotating emergency/alarm light effect |
| `environ_explosion` | explosion effect with random explosion sound when triggered |
| `environ_fire` | fire effect (small/large) with looping fire sound |
| `environ_inferno` | large spreading fire hazard |
| `environ_invisible_attack` | invisible attacker that damages the player (scripted hazard) |
| `environ_lightning` | lightning flash effect |
| `environ_linefountain` | stream/line fountain particle effect |
| `environ_linetrap` | tripwire beam trap (fires its targets when crossed) |
| `environ_player_snow` | snow that follows the player |
| `environ_rain_worldtrigger` | switches world rain on/off when triggered |
| `environ_raingen` | rain generator |
| `environ_skyfade` | fades the sky over time when triggered |
| `environ_smoke_burst` | one-shot smoke burst |
| `environ_smokegen` | smoke generator |
| `environ_snow` | snow generator |
| `environ_soundgen` | ambient sound generator from the ambient sound set (sound/sound.ams) |
| `environ_sparkgen` | spark generator |
| `environ_splashfountain` | splash fountain effect |
| `environ_steamgen` | steam generator |
| `environ_trainarm_spawn` | spawns the moving train-arm object (train level) |
| `environ_waterglobgen` | dripping water generator |

#### Brush movers and functional brushes (func_*) (39)

| classname | description |
|---|---|
| `func_Dekker_console` | scripted console object in the final boss sequence |
| `func_Dekker_diehere` | scripted marker for the final boss (Dekker) sequence |
| `func_Dekker_jumphere` | scripted jump marker in the final boss sequence |
| `func_Dekker_split` | scripted split marker in the final boss sequence |
| `func_alarm` | alarm that alerts enemies when triggered |
| `func_ambientset` | selects the ambient sound set for the level/area |
| `func_areaportal` | area portal toggled by doors (Quake II) |
| `func_breakable_brush` | brush model that breaks into debris when damaged |
| `func_button` | button (Quake II), uses the 'use' key |
| `func_camerashake` | shakes the view of nearby players when triggered |
| `func_clock` | countdown/clock string driver (Quake II) |
| `func_conveyor` | conveyor belt (Quake II) |
| `func_door` | sliding door (Quake II) |
| `func_door_portcullis` | vertical portcullis door |
| `func_door_rotating` | rotating door (Quake II) |
| `func_door_rotating_smart` | rotating door that opens away from the activator |
| `func_door_secret` | secret door (Quake II) |
| `func_fade` | fades the screen (to/from black) when triggered |
| `func_group` | editor grouping only; removed at spawn (Quake II) |
| `func_hide_the_players_gun` | hides/shows the player's weapon when triggered (cinematics) |
| `func_ignite` | sets entities in an area on fire when triggered |
| `func_killbox` | telefrag volume (Quake II) |
| `func_mission_status` | updates mission objectives/status when triggered |
| `func_musicControl` | changes the music state/set when triggered |
| `func_object` | brush object that falls/is pushed (Quake II) |
| `func_plat` | platform/lift (Quake II) |
| `func_player_armor` | sets/adds player armour when triggered |
| `func_player_health` | sets/adds player health when triggered |
| `func_player_item` | gives an inventory item to the player when triggered |
| `func_pushkillbox` | volume that kills/pushes what touches it |
| `func_remote_camera` | remote camera viewpoint (used by cinematic/security camera views) |
| `func_rotating` | continuously rotating brush (Quake II) |
| `func_score` | awards score/team points when used |
| `func_snipercam_zoom` | forces a sniper-camera zoom/field of view when triggered |
| `func_timer` | repeating timer that fires targets (Quake II) |
| `func_train` | train moving along path_corner entities (Quake II) |
| `func_trigger_brush` | brush model that fires its targets when damaged/used |
| `func_wall` | toggleable wall (Quake II) |
| `func_water` | movable water volume (Quake II) |

#### Triggers (trigger_*) (21)

| classname | description |
|---|---|
| `trigger_activate` | toggles other triggers on and off (amended 2026-10-22 per SPEC-761) |
| `trigger_always` | fires targets once at level start (Quake II) |
| `trigger_conquer` | capture zone for the conquer game mode |
| `trigger_countdown` | counts down and fires targets when it expires |
| `trigger_counter` | fires targets after being triggered N times (Quake II) |
| `trigger_elevator` | drives a func_train elevator (Quake II) |
| `trigger_gravity` | changes gravity of touching entities (Quake II) |
| `trigger_heal` | health station: heals while used, with station sounds |
| `trigger_hurt` | damages touching entities (Quake II) |
| `trigger_keep_using` | fires targets only while the player keeps pressing 'use' |
| `trigger_key` | requires a key item (Quake II) |
| `trigger_monsterjump` | makes monsters jump (Quake II) |
| `trigger_movedown` | trigger that moves its target down |
| `trigger_multiple` | repeatable touch trigger (Quake II) |
| `trigger_objectives` | updates mission objectives when touched |
| `trigger_once` | one-shot touch trigger (Quake II) |
| `trigger_push` | pushes touching entities (Quake II) |
| `trigger_relay` | relays use to its targets (Quake II) |
| `trigger_safezone` | volume where the player is safe from enemies |
| `trigger_sound` | fires its targets when a loud noise happens nearby (amended 2026-10-22 per SPEC-761) |
| `trigger_useable` | fires targets when used, plays a .wav |

#### Targets (target_*) (16)

| classname | description |
|---|---|
| `target_changelevel` | level exit (Quake II) |
| `target_character` | one character of a string display (Quake II) |
| `target_crosslevel_target` | fires when cross-level flags match (Quake II) |
| `target_crosslevel_trigger` | sets a cross-level flag (Quake II) |
| `target_earthquake` | earthquake shake (Quake II) |
| `target_explosion` | explosion with radius damage (Quake II) |
| `target_goal` | goal counter (Quake II) |
| `target_help` | sets help/objective text (Quake II) |
| `target_laser` | damaging laser beam (Quake II) |
| `target_lightramp` | ramps a light style (Quake II) |
| `target_secret` | secret counter (Quake II) |
| `target_spawner` | spawns an entity of a classname when used (Quake II) |
| `target_speaker` | sound emitter (Quake II) |
| `target_splash` | splash effect with optional damage (Quake II) |
| `target_string` | drives target_character entities (Quake II) |
| `target_temp_entity` | fires a temporary effect when used (Quake II) |

#### Lights (light, light_generic_*) (21)

| classname | description |
|---|---|
| `light` | light (Quake II); switchable lights use light styles |
| `light_generic_alley_wall` | light fixture model (switchable/breakable): alley wall |
| `light_generic_alley_wall_old` | light fixture model (switchable/breakable): alley wall old |
| `light_generic_caged` | light fixture model (switchable/breakable): caged |
| `light_generic_candelabra` | light fixture model (switchable/breakable): candelabra |
| `light_generic_candelabra_small` | light fixture model (switchable/breakable): candelabra small |
| `light_generic_chandelier` | light fixture model (switchable/breakable): chandelier |
| `light_generic_desk_lamp` | light fixture model (switchable/breakable): desk lamp |
| `light_generic_emergency` | light fixture model (switchable/breakable): emergency |
| `light_generic_emergency_blue` | light fixture model (switchable/breakable): emergency blue |
| `light_generic_halfshell` | light fixture model (switchable/breakable): halfshell |
| `light_generic_hanging` | light fixture model (switchable/breakable): hanging |
| `light_generic_lamp_hang` | light fixture model (switchable/breakable): lamp hang |
| `light_generic_lantern` | light fixture model (switchable/breakable): lantern |
| `light_generic_lantern2` | light fixture model (switchable/breakable): lantern2 |
| `light_generic_lantern3` | light fixture model (switchable/breakable): lantern3 |
| `light_generic_military_door` | light fixture model (switchable/breakable): military door |
| `light_generic_military_door_outside` | light fixture model (switchable/breakable): military door outside |
| `light_generic_shop` | light fixture model (switchable/breakable): shop |
| `light_generic_street_modern` | light fixture model (switchable/breakable): street modern |
| `light_generic_street_old` | light fixture model (switchable/breakable): street old |

#### Characters and vehicles (m_*) (105)

| classname | description |
|---|---|
| `m_afr_ecommander` | character (Africa (Sudan/Uganda), civilian/officer body): commander |
| `m_afr_eworker` | character (Africa (Sudan/Uganda), civilian/officer body): worker |
| `m_afr_mbrute` | character (Africa (Sudan/Uganda), male soldier body): heavy (brute) |
| `m_afr_mrocket` | character (Africa (Sudan/Uganda), male soldier body): rocket trooper |
| `m_afr_msniper` | character (Africa (Sudan/Uganda), male soldier body): sniper |
| `m_afr_msoldier1a` | character (Africa (Sudan/Uganda), male soldier body): soldier variant 1a |
| `m_afr_msoldier1b` | character (Africa (Sudan/Uganda), male soldier body): soldier variant 1b |
| `m_afr_msoldier2` | character (Africa (Sudan/Uganda), male soldier body): soldier variant 2 |
| `m_afr_msoldier3` | character (Africa (Sudan/Uganda), male soldier body): soldier variant 3 |
| `m_cas_ffemale` | character (castle (Germany), female body): female raider |
| `m_cas_mbrute` | character (castle (Germany), male soldier body): heavy (brute) |
| `m_cas_mraider1` | character (castle (Germany), male soldier body): raider variant 1 |
| `m_cas_mraider2a` | character (castle (Germany), male soldier body): raider variant 2a |
| `m_cas_mraider2b` | character (castle (Germany), male soldier body): raider variant 2b |
| `m_cas_mrocket` | character (castle (Germany), male soldier body): rocket trooper |
| `m_irq_eman1` | character (Iraq, civilian/officer body): man variant 1 |
| `m_irq_eofficer` | character (Iraq, civilian/officer body): officer |
| `m_irq_fwoman1` | character (Iraq, female body): woman variant 1 |
| `m_irq_fwoman2` | character (Iraq, female body): woman variant 2 |
| `m_irq_mbodyguard` | character (Iraq, male soldier body): bodyguard |
| `m_irq_mbrute1a` | character (Iraq, male soldier body): heavy (brute) variant 1a |
| `m_irq_mbrute1b` | character (Iraq, male soldier body): heavy (brute) variant 1b |
| `m_irq_mcommander` | character (Iraq, male soldier body): commander |
| `m_irq_mman2` | character (Iraq, male soldier body): man variant 2 |
| `m_irq_moilworker` | character (Iraq, male soldier body): oil worker |
| `m_irq_mpolice` | character (Iraq, male soldier body): police |
| `m_irq_mrepguard1a` | character (Iraq, male soldier body): Republican Guard variant 1a |
| `m_irq_mrepguard1b` | character (Iraq, male soldier body): Republican Guard variant 1b |
| `m_irq_mrocket` | character (Iraq, male soldier body): rocket trooper |
| `m_irq_msaddam` | character (Iraq, male soldier body): Saddam look-alike |
| `m_irq_msoldier1` | character (Iraq, male soldier body): soldier variant 1 |
| `m_irq_msoldier2a` | character (Iraq, male soldier body): soldier variant 2a |
| `m_irq_msoldier2b` | character (Iraq, male soldier body): soldier variant 2b |
| `m_kos_eofficer` | character (Kosovo, civilian/officer body): officer |
| `m_kos_erefugee` | character (Kosovo, civilian/officer body): refugee |
| `m_kos_mbrute1a` | character (Kosovo, male soldier body): heavy (brute) variant 1a |
| `m_kos_mbrute1b` | character (Kosovo, male soldier body): heavy (brute) variant 1b |
| `m_kos_mcomtroop` | character (Kosovo, male soldier body): communications trooper |
| `m_kos_mgrunt1` | character (Kosovo, male soldier body): grunt variant 1 |
| `m_kos_mgrunt2` | character (Kosovo, male soldier body): grunt variant 2 |
| `m_kos_mgrunt3` | character (Kosovo, male soldier body): grunt variant 3 |
| `m_kos_mklaleader` | character (Kosovo, male soldier body): KLA leader |
| `m_kos_mmechanic` | character (Kosovo, male soldier body): mechanic |
| `m_kos_mrebel` | character (Kosovo, male soldier body): rebel |
| `m_kos_msniper1a` | character (Kosovo, male soldier body): sniper variant 1a |
| `m_kos_msniper1b` | character (Kosovo, male soldier body): sniper variant 1b |
| `m_nyc_ebum` | character (New York, civilian/officer body): bum |
| `m_nyc_epunk` | character (New York, civilian/officer body): punk |
| `m_nyc_eskinhead3` | character (New York, civilian/officer body): skinhead variant 3 |
| `m_nyc_estockbroker` | character (New York, civilian/officer body): stockbroker |
| `m_nyc_etourist` | character (New York, civilian/officer body): tourist |
| `m_nyc_fskinchick` | character (New York, female body): skinhead girl |
| `m_nyc_fwoman` | character (New York, female body): woman |
| `m_nyc_mpolitician` | character (New York, male soldier body): politician |
| `m_nyc_mpunk` | character (New York, male soldier body): punk |
| `m_nyc_mskinhead1` | character (New York, male soldier body): skinhead variant 1 |
| `m_nyc_mskinhead2a` | character (New York, male soldier body): skinhead variant 2a |
| `m_nyc_mskinhead2b` | character (New York, male soldier body): skinhead variant 2b |
| `m_nyc_mswat` | character (New York, male soldier body): SWAT officer |
| `m_nyc_mswatleader` | character (New York, male soldier body): SWAT leader |
| `m_sib_eofficer` | character (Siberia, civilian/officer body): officer |
| `m_sib_escientist1` | character (Siberia, civilian/officer body): scientist variant 1 |
| `m_sib_fguard2` | character (Siberia, female body): guard variant 2 |
| `m_sib_fscientist2` | character (Siberia, female body): scientist variant 2 |
| `m_sib_mcleansuit` | character (Siberia, male soldier body): clean-suit technician |
| `m_sib_mguard1` | character (Siberia, male soldier body): guard variant 1 |
| `m_sib_mguard3` | character (Siberia, male soldier body): guard variant 3 |
| `m_sib_mguard4` | character (Siberia, male soldier body): guard variant 4 |
| `m_sib_mmechanic` | character (Siberia, male soldier body): mechanic |
| `m_sib_mtrooper1a` | character (Siberia, male soldier body): trooper variant 1a |
| `m_sib_mtrooper1b` | character (Siberia, male soldier body): trooper variant 1b |
| `m_sib_mtrooper2` | character (Siberia, male soldier body): trooper variant 2 |
| `m_tok_fassassin` | character (Tokyo, female body): assassin |
| `m_tok_fwoman1` | character (Tokyo, female body): woman variant 1 |
| `m_tok_fwoman2` | character (Tokyo, female body): woman variant 2 |
| `m_tok_mbrute` | character (Tokyo, male soldier body): heavy (brute) |
| `m_tok_mhench1` | character (Tokyo, male soldier body): henchman variant 1 |
| `m_tok_mhench2` | character (Tokyo, male soldier body): henchman variant 2 |
| `m_tok_mkiller` | character (Tokyo, male soldier body): killer |
| `m_tok_mman1` | character (Tokyo, male soldier body): man variant 1 |
| `m_tok_mman2` | character (Tokyo, male soldier body): man variant 2 |
| `m_tok_mninja` | character (Tokyo, male soldier body): ninj variant a |
| `m_x_bull` | enemy: bull |
| `m_x_chopper_black` | enemy: black helicopter |
| `m_x_chopper_green` | enemy: green helicopter |
| `m_x_chopper_white` | enemy: white helicopter |
| `m_x_ftaylor` | character (any level, female body): Taylor |
| `m_x_germanshep` | enemy: German shepherd dog |
| `m_x_hind` | enemy: Hind helicopter gunship |
| `m_x_husky` | enemy: husky dog |
| `m_x_mcharacter` | character (any level, male soldier body): generic character |
| `m_x_mcharacter_desert` | character (any level, male soldier body): character desert |
| `m_x_mcharacter_snow` | character (any level, male soldier body): character snow |
| `m_x_mhurtmerc` | character (any level, male soldier body): wounded mercenary |
| `m_x_miraqboss` | character (any level, male soldier body): Iraq boss |
| `m_x_mmerc` | character (any level, male soldier body): mercenary |
| `m_x_mraiderboss1` | character (any level, male soldier body): raider boss variant 1 |
| `m_x_mraiderboss2` | character (any level, male soldier body): raider boss variant 2 |
| `m_x_msam` | character (any level, male soldier body): Sam (ally) |
| `m_x_mskinboss` | character (any level, male soldier body): skinhead boss |
| `m_x_raiderdog` | enemy: raider dog |
| `m_x_romulan` | enemy: romulan (developer test character) |
| `m_x_rottweiler` | enemy: rottweiler dog |
| `m_x_snowcat` | enemy: snowcat (vehicle enemy) |
| `m_x_tank` | enemy: tank (vehicle enemy) |

#### Map objects (misc_*) (175)

| classname | description |
|---|---|
| `misc_bosnia_antipersonnel_jack` | Kosovo/Bosnia map object: antipersonnel jack |
| `misc_bosnia_flag_wall` | Kosovo/Bosnia map object: flag wall |
| `misc_bosnia_landmine` | Kosovo/Bosnia map object: landmine |
| `misc_bosnia_rubble1` | Kosovo/Bosnia map object: rubble1 |
| `misc_bosnia_rubble2` | Kosovo/Bosnia map object: rubble2 |
| `misc_bosnia_searchbeam` | Kosovo/Bosnia map object: searchbeam |
| `misc_bosnia_searchlight` | Kosovo/Bosnia map object: searchlight |
| `misc_bosnia_tank_jack` | Kosovo/Bosnia map object: tank jack |
| `misc_bosnia_truck_chunks` | Kosovo/Bosnia map object: truck chunks |
| `misc_bosnia_truck_old` | Kosovo/Bosnia map object: truck old |
| `misc_castle_armor` | castle (Germany) map object: armor |
| `misc_castle_bust` | castle (Germany) map object: bust |
| `misc_castle_cannon` | castle (Germany) map object: cannon |
| `misc_castle_chair` | castle (Germany) map object: chair |
| `misc_castle_chair_big` | castle (Germany) map object: chair big |
| `misc_castle_globe` | castle (Germany) map object: globe |
| `misc_castle_hedge` | castle (Germany) map object: hedge |
| `misc_castle_tree` | castle (Germany) map object: tree |
| `misc_ctf_base` | CTF flag base |
| `misc_exploding_heli` | marker for the exploding helicopter (no spawn behaviour) |
| `misc_generic_access_card` | generic map object: access card |
| `misc_generic_banner` | generic map object: banner |
| `misc_generic_barbwire_coil` | generic map object: barbwire coil |
| `misc_generic_barrel_burning` | generic map object: barrel burning |
| `misc_generic_barrel_chemical` | generic map object: barrel chemical |
| `misc_generic_barrel_wine` | generic map object: barrel wine |
| `misc_generic_bench` | generic map object: bench |
| `misc_generic_blinds` | generic map object: blinds |
| `misc_generic_bookshelf` | generic map object: bookshelf |
| `misc_generic_broom` | generic map object: broom |
| `misc_generic_bunk` | generic map object: bunk |
| `misc_generic_car_american` | generic map object: car american |
| `misc_generic_car_european` | generic map object: car european |
| `misc_generic_car_honda` | generic map object: car honda |
| `misc_generic_car_rolls` | generic map object: car rolls |
| `misc_generic_car_sports` | generic map object: car sports |
| `misc_generic_cart` | generic map object: cart |
| `misc_generic_cash_register` | generic map object: cash register |
| `misc_generic_chair_legs` | generic map object: chair legs |
| `misc_generic_chair_legs2` | generic map object: chair legs2 |
| `misc_generic_chair_office` | generic map object: chair office |
| `misc_generic_chunks_brick` | generic map object: debris chunk source (brick) |
| `misc_generic_chunks_glass` | generic map object: debris chunk source (glass) |
| `misc_generic_chunks_metal` | generic map object: debris chunk source (metal) |
| `misc_generic_chunks_rock` | generic map object: debris chunk source (rock) |
| `misc_generic_chunks_wall` | generic map object: debris chunk source (wall) |
| `misc_generic_chunks_wood` | generic map object: debris chunk source (wood) |
| `misc_generic_comp_keyboard` | generic map object: comp keyboard |
| `misc_generic_comp_monitor` | generic map object: comp monitor |
| `misc_generic_comp_monitor2` | generic map object: comp monitor2 |
| `misc_generic_crate_fish` | generic map object: crate fish |
| `misc_generic_desk_phone` | generic map object: desk phone |
| `misc_generic_fan_big` | generic map object: fan big |
| `misc_generic_faucet` | generic map object: faucet |
| `misc_generic_fire_extinguisher` | generic map object: fire extinguisher |
| `misc_generic_fire_hydrant` | generic map object: fire hydrant |
| `misc_generic_fish` | generic map object: fish |
| `misc_generic_fish_dead` | generic map object: fish dead |
| `misc_generic_flag_pole` | generic map object: flag pole |
| `misc_generic_forklift` | generic map object: forklift |
| `misc_generic_garbage_bag` | generic map object: garbage bag |
| `misc_generic_gun_auto` | generic map object: gun auto |
| `misc_generic_gun_auto2` | generic map object: gun auto2 |
| `misc_generic_gun_auto3` | generic map object: gun auto3 |
| `misc_generic_gun_big` | generic map object: gun big |
| `misc_generic_gun_castle` | generic map object: gun castle |
| `misc_generic_heap` | generic map object: heap |
| `misc_generic_light_beam` | generic map object: light beam |
| `misc_generic_light_flare` | generic map object: light flare |
| `misc_generic_motorcycle` | generic map object: motorcycle |
| `misc_generic_orange_cone` | generic map object: orange cone |
| `misc_generic_palette_jack` | generic map object: palette jack |
| `misc_generic_paper_blowing` | generic map object: paper blowing |
| `misc_generic_parking_meter` | generic map object: parking meter |
| `misc_generic_phone_booth` | generic map object: phone booth |
| `misc_generic_pillow` | generic map object: pillow |
| `misc_generic_plant_potted` | generic map object: plant potted |
| `misc_generic_plant_tall` | generic map object: plant tall |
| `misc_generic_radar_dish` | generic map object: radar dish |
| `misc_generic_radio` | generic map object: radio |
| `misc_generic_security_camera` | generic map object: security camera |
| `misc_generic_security_camera2` | generic map object: security camera2 |
| `misc_generic_shovel` | generic map object: shovel |
| `misc_generic_shrub` | generic map object: shrub |
| `misc_generic_sink` | generic map object: sink |
| `misc_generic_sofa` | generic map object: sofa |
| `misc_generic_sofa_chair` | generic map object: sofa chair |
| `misc_generic_speaker` | generic map object: speaker |
| `misc_generic_sprinkler` | generic map object: sprinkler |
| `misc_generic_stoplight` | generic map object: stoplight |
| `misc_generic_street_name_sign` | generic map object: street name sign |
| `misc_generic_sword` | generic map object: sword |
| `misc_generic_sword_large` | generic map object: sword large |
| `misc_generic_table1` | generic map object: table1 |
| `misc_generic_table2` | generic map object: table2 |
| `misc_generic_table_round` | generic map object: table round |
| `misc_generic_tank_gas` | generic map object: tank gas |
| `misc_generic_tank_propane` | generic map object: tank propane |
| `misc_generic_toilet` | generic map object: toilet |
| `misc_generic_train` | generic map object: train |
| `misc_generic_trash_ashtray` | generic map object: trash ashtray |
| `misc_generic_trashcan_barrel` | generic map object: trashcan barrel |
| `misc_generic_tree_dead` | generic map object: tree dead |
| `misc_generic_tree_thin` | generic map object: tree thin |
| `misc_generic_truckcab` | generic map object: truckcab |
| `misc_generic_tv_ceiling` | generic map object: tv ceiling |
| `misc_generic_tv_wall` | generic map object: tv wall |
| `misc_generic_urinal` | generic map object: urinal |
| `misc_generic_urinal_cake` | generic map object: urinal cake |
| `misc_generic_urn` | generic map object: urn |
| `misc_generic_waste_basket_wire` | generic map object: waste basket wire |
| `misc_generic_web_moss_animated` | generic map object: web moss animated |
| `misc_generic_wet_floor_sign` | generic map object: wet floor sign |
| `misc_generic_wpn_bat` | generic map object: weapon prop (bat) |
| `misc_generic_wpn_c4` | generic map object: weapon prop (c4) |
| `misc_generic_wpn_desert_eagle` | generic map object: weapon prop (desert eagle) |
| `misc_generic_wpn_glock` | generic map object: weapon prop (glock) |
| `misc_generic_wpn_ingram` | generic map object: weapon prop (ingram) |
| `misc_generic_wpn_katana` | generic map object: weapon prop (katana) |
| `misc_generic_wpn_minimi` | generic map object: weapon prop (minimi) |
| `misc_generic_wpn_sig3000` | generic map object: weapon prop (sig3000) |
| `misc_generic_wpn_spas12` | generic map object: weapon prop (spas12) |
| `misc_iraq_basket` | Iraq map object: basket |
| `misc_iraq_cart_camel` | Iraq map object: cart camel |
| `misc_iraq_flatbed` | Iraq map object: flatbed |
| `misc_iraq_fruit_melon` | Iraq map object: fruit melon |
| `misc_iraq_fruit_orange` | Iraq map object: fruit orange |
| `misc_iraq_fruit_stand` | Iraq map object: fruit stand |
| `misc_iraq_partition` | Iraq map object: partition |
| `misc_iraq_pottery` | Iraq map object: pottery |
| `misc_iraq_sprinkler` | Iraq map object: sprinkler |
| `misc_iraq_stealth` | Iraq map object: stealth |
| `misc_iraq_tree_palm` | Iraq map object: tree palm |
| `misc_iraq_valve` | Iraq map object: valve |
| `misc_newyork_burned_car` | New York map object: burned car |
| `misc_newyork_gumball` | New York map object: gumball |
| `misc_newyork_news_machine` | New York map object: news machine |
| `misc_newyork_trash_pile` | New York map object: trash pile |
| `misc_newyork_trashcan` | New York map object: trashcan |
| `misc_siberia_carton1` | Siberia map object: carton1 |
| `misc_siberia_carton2` | Siberia map object: carton2 |
| `misc_siberia_carton3` | Siberia map object: carton3 |
| `misc_siberia_carton4` | Siberia map object: carton4 |
| `misc_siberia_flag` | Siberia map object: flag |
| `misc_siberia_icicles` | Siberia map object: icicles |
| `misc_siberia_roadflare` | Siberia map object: roadflare |
| `misc_siberia_searchlight` | Siberia map object: searchlight |
| `misc_siberia_snowcat` | Siberia map object: snowcat |
| `misc_test_gun_auto` | developer test turret gun |
| `misc_tokyo_bowl` | Tokyo map object: bowl |
| `misc_tokyo_buddha` | Tokyo map object: buddha |
| `misc_tokyo_fan` | Tokyo map object: fan |
| `misc_tokyo_gong` | Tokyo map object: gong |
| `misc_tokyo_gym_equipment1` | Tokyo map object: gym equipment1 |
| `misc_tokyo_gym_equipment2` | Tokyo map object: gym equipment2 |
| `misc_tokyo_plant_bonsai` | Tokyo map object: plant bonsai |
| `misc_tokyo_samurai_suit` | Tokyo map object: samurai suit |
| `misc_tokyo_samurai_suit_small` | Tokyo map object: samurai suit small |
| `misc_tokyo_shoe` | Tokyo map object: shoe |
| `misc_tokyo_stone_light` | Tokyo map object: stone light |
| `misc_uganda_african_tree` | Uganda map object: african tree |
| `misc_uganda_chainsaw` | Uganda map object: chainsaw |
| `misc_uganda_cow` | Uganda map object: cow |
| `misc_uganda_cow_head` | Uganda map object: cow head |
| `misc_uganda_gore_pile` | Uganda map object: gore pile |
| `misc_uganda_gutted_cow` | Uganda map object: gutted cow |
| `misc_uganda_hook` | Uganda map object: hook |
| `misc_uganda_nuke` | Uganda map object: nuke |
| `misc_uganda_rocket_core` | Uganda map object: rocket core |
| `misc_uganda_rocket_engine` | Uganda map object: rocket engine |
| `misc_uganda_side_o_beef` | Uganda map object: side o beef |
| `misc_uganda_train_arm` | Uganda map object: train arm |
| `misc_uganda_train_bumper` | Uganda map object: train bumper |
| `misc_uganda_train_coupling` | Uganda map object: train coupling |
| `misc_uganda_train_wheel` | Uganda map object: train wheel |

#### Other (18)

| classname | description |
|---|---|
| `dm_KOTH_targetzone` | target zone for the king-of-the-hill style mode |
| `ghoul_model` | places an animated Ghoul model (test/decoration) |
| `info_notnull` | reference point kept as a positional entity (Quake II) |
| `info_null` | editor reference point; removed at spawn (Quake II) |
| `info_player_deathmatch` | deathmatch spawn point (Quake II) |
| `info_player_intermission` | intermission camera (Quake II) |
| `info_player_start` | single-player start (Quake II) |
| `info_player_team1` | team 1 spawn point |
| `info_player_team2` | team 2 spawn point |
| `path_corner` | path node for trains and patrolling characters; keys waitaction1..6/moveaction give animations (Quake II-derived) |
| `point_combat` | combat move point for characters (Quake II) |
| `script_runner` | runs a DesignerScript (.os) program when triggered |
| `spawner` | level spawner: periodically spawns characters at its location |
| `spawner_boosterpack` | spawner variant that drops booster packs/items |
| `spawner_monster` | spawner variant for monsters |
| `test_model` | test entity for a model |
| `test_rj` | developer test entity (no behaviour) |
| `worldspawn` | world settings: sky, music, terrain set, weapon restrictions, ambient set (Quake II-derived) |

### Item classnames (39, spawned through the item table)
Ammo classnames containing `_sp_` are single-player variants of the same ammunition.

| classname | display name / kind |
|---|---|
| `item_ammo_knife` | "Knife" — ammo |
| `item_ammo_pistol` | "9mm Ammo" — ammo |
| `item_ammo_shotgun` | "12 gauge shells" — ammo |
| `item_ammo_pistol2` | ".44 Ammo" — ammo |
| `item_ammo_auto` | "5.56 Ammo" — ammo |
| `item_ammo_battery` | "Battery" — ammo |
| `item_ammo_gas` | "Flamethrower Gas" — ammo |
| `item_ammo_rocket` | "NM-22 Rockets" — ammo |
| `item_ammo_slug` | "Slugs" — ammo |
| `item_ammo_sp_pistol` | "9mm Ammo" — ammo (single-player variant) |
| `item_ammo_sp_shotgun` | "12 gauge shells" — ammo (single-player variant) |
| `item_ammo_sp_pistol2` | ".44 Ammo" — ammo (single-player variant) |
| `item_ammo_sp_auto` | "5.56 Ammo" — ammo (single-player variant) |
| `item_ammo_sp_battery` | "Battery" — ammo (single-player variant) |
| `item_ammo_sp_gas` | "Flamethrower Gas" — ammo (single-player variant) |
| `item_ammo_sp_rocket` | "NM-22 Rockets" — ammo (single-player variant) |
| `item_ammo_sp_slug` | "Slugs" — ammo (single-player variant) |
| `item_weapon_pistol2` | ".44 Pistol" — weapon |
| `item_weapon_pistol1` | "9mm Pistol" — weapon |
| `item_weapon_machinepistol` | "9mm Suppressed SMG" — weapon |
| `item_weapon_assault_rifle` | "5.56 SMG" — weapon |
| `item_weapon_sniper_rifle` | "5.56 Sniper Rifle" — weapon |
| `item_weapon_autoshotgun` | "Repeating Slug Thrower" — weapon |
| `item_weapon_shotgun` | "12 Gauge Shotgun" — weapon |
| `item_weapon_machinegun` | "Heavy Machinegun" — weapon |
| `item_weapon_rocketlauncher` | "M-1943 Rocket Launcher" — weapon |
| `item_weapon_microwavepulse` | "Microwave Pulse" — weapon |
| `item_weapon_flamethrower` | "L-32 Flame Gun" — weapon |
| `item_equip_armor` | "Armor" — equipment |
| `item_equip_flashpack` | "Flashpack" — equipment |
| `item_equip_c4` | "C4" — equipment |
| `item_equip_light_goggles` | "Light Amplification Goggles" — equipment |
| `item_equip_claymore` | "Mine" — equipment |
| `item_equip_medkit` | "MedKit" — equipment |
| `item_equip_grenade` | "Grenade" — equipment |
| `item_ctf_flag` | "Flag" — flag |
| `item_health_small` | "Small Health" — health |
| `item_health_large` | "Large Health" — health |
| `item_control_flag` | "Control Flag" — flag |

### Entity keys read from map entities (96)
Keys marked "spawn-time only" are read while the entity spawns and are not kept on the entity (mostly worldspawn and
script parameters), like Quake II's spawn-temp keys. Unknown keys print `<key> is not a field`.

| key | type | storage |
|---|---|---|
| `classname` | string | entity |
| `origin` | vector (3 floats) | entity |
| `model` | string | entity |
| `spawnflags` | integer | entity |
| `speed` | float | entity |
| `accel` | float | entity |
| `decel` | float | entity |
| `target` | string | entity |
| `targetname` | string | entity |
| `scripttarget` | string | entity |
| `pathtarget` | string | entity |
| `deathtarget` | string | entity |
| `killtarget` | string | entity |
| `killfacing` | string | entity |
| `combattarget` | string | entity |
| `soundname` | string | entity |
| `spawn1` | string | entity |
| `spawn2` | string | entity |
| `spawn3` | string | entity |
| `message` | string | entity |
| `sp_message` | integer | entity |
| `team` | string | entity |
| `wait` | float | entity |
| `delay` | float | entity |
| `random` | float | entity |
| `style` | integer | entity |
| `count` | integer | entity |
| `health` | integer | entity |
| `sounds` | integer | entity |
| `light` | ignored | entity |
| `dmg` | integer | entity |
| `angles` | vector (3 floats) | entity |
| `angle` | angle (yaw; sets the angles vector) | entity |
| `mass` | integer | entity |
| `volume` | float | entity |
| `attenuation` | float | entity |
| `map` | string | entity |
| `skin` | integer | entity |
| `alertradius` | float | entity |
| `personality` | integer | entity |
| `lip` | integer | spawn-time only |
| `distance` | integer | spawn-time only |
| `height` | integer | spawn-time only |
| `noise` | string | spawn-time only |
| `pausetime` | float | spawn-time only |
| `item` | string | spawn-time only |
| `gravity` | string | spawn-time only |
| `sky` | string | spawn-time only |
| `skyrotate` | float | spawn-time only |
| `skyaxis` | vector (3 floats) | spawn-time only |
| `terrainset` | string | spawn-time only |
| `musicSet` | string | spawn-time only |
| `color` | vector (3 floats) | spawn-time only |
| `endpoint` | vector (3 floats) | spawn-time only |
| `minyaw` | float | spawn-time only |
| `maxyaw` | float | spawn-time only |
| `minpitch` | float | spawn-time only |
| `maxpitch` | float | spawn-time only |
| `nextmap` | string | spawn-time only |
| `script` | string | spawn-time only |
| `killedvalue` | integer | spawn-time only |
| `survivalvalue` | integer | spawn-time only |
| `material` | integer | spawn-time only |
| `scale` | float | spawn-time only |
| `merctype` | integer | spawn-time only |
| `mercnum` | integer | spawn-time only |
| `waitaction1` | string | spawn-time only |
| `waitaction2` | string | spawn-time only |
| `waitaction3` | string | spawn-time only |
| `waitaction4` | string | spawn-time only |
| `waitaction5` | string | spawn-time only |
| `waitaction6` | string | spawn-time only |
| `moveaction` | string | spawn-time only |
| `screeneffect` | integer | spawn-time only |
| `spawnfrequency` | float | spawn-time only |
| `parm1` | string | spawn-time only |
| `parm2` | string | spawn-time only |
| `parm3` | string | spawn-time only |
| `parm4` | string | spawn-time only |
| `parm5` | string | spawn-time only |
| `parm6` | string | spawn-time only |
| `parm7` | string | spawn-time only |
| `parm8` | string | spawn-time only |
| `parm9` | string | spawn-time only |
| `parm10` | string | spawn-time only |
| `surfacetype` | integer | spawn-time only |
| `noweapons` | integer | spawn-time only |
| `setweapons` | integer | spawn-time only |
| `maxDeadHostages` | integer | spawn-time only |
| `ambientSet` | string | spawn-time only |
| `startmusic` | integer | spawn-time only |
| `forceHUD` | integer | spawn-time only |
| `weaponsAvailable` | integer | spawn-time only |
| `ainame` | string | spawn-time only |
| `distcull` | integer | spawn-time only |
| `chopsky` | integer | spawn-time only |

Notes: `angle` sets the yaw of the angles vector (Quake II "angle hack"); `light` is accepted and ignored
(UNVERIFIED: verify that a `light` key on a non-light entity has no effect); `sp_message` is a string-package
message id (integer) used instead of the literal `message` text.

### Console variables registered by the game module
Registered when the game module initialises (some also exist in the engine — see SPEC-302 for the flag bits;
0x1 archive, 0x2 userinfo, 0x4 serverinfo, 0x8 no-set, 0x10 latch, 0x800 misc-inventory).

| cvar | default | flags |
|---|---|---|
| `hostname` | "" | 0x0 |
| `ctf_flag_respawn_time` | "90" | 0x1 |
| `bot_waypoints` | "1" | 0x0 |
| `bot_wp_edit` | "0" | 0x0 |
| `bot_listfile` | "botlist.txt" | 0x0 |
| `bot_random` | "1" | 0x0 |
| `bot_fillserv` | "0" | 0x0 |
| `bot_configexec` | "1" | 0x0 |
| `bot_chat` | "0" | 0x0 |
| `bot_wppref` | "1" | 0x0 |
| `bot_hunt` | "1" | 0x0 |
| `bot_notarget` | "0" | 0x0 |
| `bot_runtact` | "1" | 0x0 |
| `bot_timing` | "0" | 0x0 |
| `bot_clientstatus` | "0" | 0x0 |
| `sv_rollspeed` | "200" | 0x0 |
| `sv_rollangle` | "2" | 0x0 |
| `sv_maxvelocity` | "2000" | 0x0 |
| `sv_gravity` | "800" | 0x0 |
| `sv_gravityx` | "0" | 0x0 |
| `sv_gravityy` | "0" | 0x0 |
| `sv_gravityz` | "1" | 0x0 |
| `g_movescale` | "1.0" | 0x5 |
| `ai_freeze` | "0" | 0x0 |
| `ai_goretest` | "0" | 0x0 |
| `ai_pathtest` | "0" | 0x0 |
| `ai_highpriority` | "0" | 0x0 |
| `ei_show` | "0" | 0x0 |
| `aipoints_show` | "0" | 0x0 |
| `ai_nonodes` | "0" | 0x0 |
| `aidec_show` | "0" | 0x0 |
| `ai_dumb` | "0" | 0x0 |
| `ai_editdir` | "p:" | 0x1 |
| `ai_maxcorpses` | "5" | 0x1 |
| `ai_corpselife` | "5" | 0x1 |
| `ai_spawnfrequency` | "1.0" | 0x800 |
| `ghl_specular` | "1" | 0x1 |
| `ghl_light_method` | "2" | 0x1 |
| `ghl_precache_verts` | "0" | 0x0 |
| `ghl_precache_texture` | "1" | 0x0 |
| `ghl_no_server_update` | "0" | 0x0 |
| `followenemy` | "0" | 0x0 |
| `thirdpersoncam` | "0" | 0x0 |
| `dedicated` | "0" | 0x8 |
| `gamename` | "base" | 0x14 |
| `gamedate` | "Jun 14 2001" | 0x14 |
| `maxclients` | "4" | 0x14 |
| `maxspectators` | "4" | 0x14 |
| `deathmatch` | "0" | 0x14 |
| `maxentities` | "1024" | 0x10 |
| `ctf_loops` | "3" | 0x5 |
| `ctf_control_delay_time` | "20" | 0x1 |
| `control_limit` | "300" | 0x0 |
| `control_bonus` | "1" | 0x0 |
| `mskins_expression_limit` | "0" | 0x11 |
| `mskins_variety_limit` | "0" | 0x11 |
| `dmflags` | "0" | 0x5 |
| `fraglimit` | "0" | 0x5 |
| `timelimit` | "0" | 0x5 |
| `sv_suicidepenalty` | "2" | 0x5 |
| `password` | "" | 0x2 |
| `spectator_password` | "" | 0x2 |
| `skill` | "2" | 0x14 |
| `freezeworld` | "0" | 0x0 |
| `sv_maplist` | "" | 0x0 |
| `sv_pmodlistfile` | "ghoul/pmodels/everybody" | 0x11 |
| `sv_altpmodlistfile` | "custom" | 0x11 |
| `run_pitch` | "0.002" | 0x0 |
| `run_roll` | "0.005" | 0x0 |
| `bob_up` | "0.005" | 0x0 |
| `bob_pitch` | "0.002" | 0x0 |
| `bob_roll` | "0.002" | 0x0 |
| `camera_viewdist` | "60.0" | 0x0 |
| `log_file_name` | "" | 0x1 |
| `log_file_footer` | "" | 0x1 |
| `log_file_header` | "" | 0x1 |
| `log_file_line_header` | "" | 0x1 |
| `log_file_mode` | "a" | 0x1 |
| `sv_restartlevel` | "0" | 0x0 |
| `stealth` | "1" | 0x0 |
| `pickupinfo` | "0" | 0x14 |
| `weaponarena` | "0" | 0x14 |
| `flood_msgs` | "4" | 0x1 |
| `flood_persecond` | "4" | 0x1 |
| `flood_waitdelay` | "10" | 0x1 |
| `flood_killdelay` | "10" | 0x1 |
| `cpu_mmx` | "0" | 0x0 |
| `cpu_amd3d` | "0" | 0x0 |
| `use_mmx` | "1" | 0x0 |
| `use_amd3d` | "1" | 0x0 |
| `gl_pictip` | "0" | 0x0 |
| `ghl_mip` | "0" | 0x1 |
| `fs_createpak` | "0" | 0x0 |
| `sv_globalrankings` | "0" | 0x0 |

Also read but not registered with a default here: `cheats`, `sk_toughness`, `sk_spawning`, `sk_saving`, `sk_maxencum`
(skill settings), `lock_deaths`, `lock_blood`, `lock_sever`, `lock_gorezones`, `lock_textures` (parental lock),
`timescale`, `sv_jumpcinematic`, `s_disable`, `menu_savesleft`, `log_file_counter`, `ctf_loops_count`,
`ctf_flag_captured`, `nodamage`, `bot_wp_info`; the `stat_*` cvars (`stat_playedtime_base`, `stat_guyskilled`,
`stat_friendlieskilled`, `stat_throatshots`, `stat_nutshots`, `stat_headshots`, `stat_gibs`, `stat_savesused`,
`stat_playedtime`, `stat_cashearned`, `stat_moneyadd`) are written for the end-of-mission statistics screen, and the
inventory cvars `knife`, `pistol1`, `pistol2`, `shotgun`, `sniper`, `assault`, `mpistol`, `machinegun`, `slugger`,
`flamegun`, `rocket`, `mpg`, `grenade`, `fpak`, `goggles`, `c4`, `medkit` are written for the menu inventory
(engine `getgamestats`/`updateinven`, SPEC-001).

### Client commands handled by the game module
Sent by clients as console commands and dispatched by the game (commands handled by the player module are in SPEC-003):

| command | purpose |
|---|---|
| `players` | list players (Quake II) |
| `say`, `say_team` | chat (Quake II; flood protection by `flood_*` cvars; long lines print `Chat string too long - truncated to 150 chars`) |
| `score`, `help` | scoreboard / help screen (Quake II) |
| `playerlist` | list players with time, ping, score and spectators (Quake II) |
| `kill` | suicide (Quake II) |
| `putaway` | close the scoreboard/help layout (Quake II) |
| `wave <n>` | gesture (Quake II) |
| `teamview <n>` | choose a team view (UNVERIFIED meaning of the argument) |
| `heretic`, `ninja`, `phantom` | cheats: god mode, notarget, noclip |
| `gimme <classname>` | cheat: spawn an entity in front of the player |
| `killallmonsters`, `entlist`, `listpickups` | cheat/debug: kill all characters, entity census, list item pickups (also writes `pickups.log`) |
| `playlevel`, `elbow`, `bigelbow`, `defaultweapons`, `givesnipertutorial`, `givemoretutorial` | cheat/debug inventory and level helpers (UNVERIFIED exact effects) |
| `updateinvfinal` | update the end-of-mission money/inventory statistics |
| `turnoffmissionmsg`, `turnonmissionmsg` | hide/show the mission objectives message (single player) |
| `loadpoint`, `destroyents` | debug: node/entity tools (UNVERIFIED) |
| `go` | developer command (UNVERIFIED) |
| `bot_wp_verifypaths`, `bot_wp_botroute`, `bot_wp_whichpath`, `bot_wp_pathinfo`, `bot_wp_connect`, `bot_wp_drawonly`, `makewp`, `makewpj`, `makewp_nv`, `makewp_el`, `remwp`, `teletowp`, `writewpfile` | bot waypoint editing (writes `bot/wpfiles/<map>.wrs`) |

## Constants
| name | value |
|---|---|
| classname tables | 15 (422 classnames, 413 distinct spawn behaviours) + item table (39) |
| entity keys | 96 |
| character AI range classes | melee < 80, near < 500, mid < 1500 units |
| character default bounding box | mins (−16, −16, −32), maxs (16, 16, 41) |
| character default mass / health | 200 / 100 (1000 with `ai_goretest`) |
| radius queries | at most 1024 entities per query (UNVERIFIED as observable limit) |
| `maxentities` default | 1024 (latched) |
| `maxclients` default | 4 |

## Test vectors / how to verify
- Load any shipped level with `developer 1`: no `doesn't have a spawn function` lines must appear for an OpenMerc
  implementation that supports all classnames above; the original prints none for shipped maps (UNVERIFIED: check on
  every shipped map).
- `deathmatch 0`, cheats on, `gimme misc_generic_barrel_wine`: a breakable barrel model appears in front of the player
  and console shows `Spawning : misc_generic_barrel_wine`.
- `gimme no_such_class`: console shows `no_such_class doesn't have a spawn function` (UNVERIFIED that gimme reaches
  the same dispatch; the code path is shared).
- `heretic` twice: `godmode ON` then `godmode OFF`; without cheats: `Cheats not available.`.
- A map entity `{ "classname" "_foo" }` spawns nothing and prints nothing.
- A map entity with key `foo` prints `foo is not a field`.

## Open questions
- Exact meaning of several Raven classnames (func_Dekker_*, trigger_movedown, environ_trainarm_spawn, spawner variants)
  — verify in the levels that use them.
- Which cvar values select which game-rules object (deathmatch values vs `dmflags`) — to be specified with the game
  modes work package.
- `.nvb`, `.wrs`, `.bpf`, `.os`, `.rof`, `.ams`, `.gpm/.gpl` file layouts — separate specs.
- Character type behaviour sets (senses, decisions, actions) and body/gore-zone tables — separate specs.
