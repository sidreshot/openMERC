# SPEC-780: Game-side player — clients, movement hand-off, view, HUD stats, scoring, obituaries, commands
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-001 (game interface: client exports, imports), SPEC-003 (player module: inventory object, owner
context, events), SPEC-006 (game module map, cvars, client command list), SPEC-131 (string packages),
SPEC-201 (messages), SPEC-202 (player state, user command, entity state), SPEC-203 (configstrings).
Related: SPEC-781 (items), SPEC-782 (projectiles and equipment).

## Purpose

Specifies everything the game module does for a player that is not weapon logic (player.dll, SPEC-003) or
item/projectile logic (SPEC-781/782): accepting and rejecting connections, user info, entering the game, spawning
and respawning, the per-command think (movement hand-off to the engine's player movement), the use key, leaning,
crouching side effects, per-frame view computation (view offset, kick, bob, roll, screen blends), falling and
environment damage, HUD statistics and layouts, chase and third-person cameras, spectators, spawn-point selection,
corpses, the deathmatch player model, scoring, obituaries, the deathmatch scoreboard and the client console commands.
Behaviour that Quake II 3.19 already has is described as a difference from it ("as Quake II" means the 3.19
`p_client.c` / `p_view.c` / `p_hud.c` / `g_cmds.c` / `g_chase.c` behaviour applies unchanged).

Terms: *deathmatch* = any game mode with cvar `deathmatch` ≠ 0 (SPEC-006 lists the modes: 1 deathmatch,
2 Assassin, 3 Arsenal, 4 CTF, 5 Realistic, 6 Control, 7 Conquer); *single player* = `deathmatch` 0. *Team game*:
`deathmatch` 4 or 6; or 7 when that mode reports team play; or any deathmatch mode with `dmflags` bit 0x80.
*Frame* = one 100 ms server frame. Distances in world units, angles in degrees.

## Interface / layout

### 1. Client record

The per-client record kept by the game is 1536 bytes. Its first 200 bytes are the player state (layout:
SPEC-202 §4). The 32-bit integer at byte 200 is the client's ping in milliseconds, written by the engine and read
by the game for scoreboards. Everything after that is private to the game.

### 2. Player-state fields written by the game

| SPEC-202 field (order) | Written by the game as |
|---|---|
| 1 view-weapon model | instance of the in-view weapon model (player module) |
| 4 weapon-state sequence | from the player module |
| 5 prediction resync number | the client's current resync counter (§ Prediction restart) |
| 6 button inhibit mask | bit 0x01 set while the attack button is "not yet re-armed" (see ClientThink, buttons) |
| 8 pm_type | see table "pm_type selection" |
| 14 delta_angles | as Quake II (spawn angles, teleporters, saved-game restore) |
| 15 extra pmove byte | trunc(move scale × 255), see "Movement speed scale" |
| 16 viewoffset | SV_CalcViewOffset result (§ View) |
| 17 viewangles | from player movement; overridden while dead / chasing / in intermission |
| 18–20 remote camera | third-person / remote-camera view (§ Cameras) |
| 21 kick_angles | § View |
| 22 weapon kick | the client's weapon kick angles (set by weapons, decays, § View) |
| 23 blend | § Screen blends |
| 24 fov | 95 at spawn and during intermission; restored from the saved value after a savegame load |
| 25 rdflags | bit 0x1 eye in liquid (water, slime or lava); bit 0x4 light-amplification goggles active |
| 26 stats | table "Stats" |
| 27 cinematic freeze | the game's cinematic state (SPEC-001 export 0x54) |
| 28 music value | managed by the music subsystem (not specified here) |
| 31 spectated player | entity number of the chased player while chasing (pm_type 7), else 0 |

### 3. Stats (16 shorts, SPEC-202 field 26)

| Index | Meaning |
|---|---|
| 0 | selected item type (player module: selected item, SPEC-003 slot 25) |
| 1 | health |
| 2 | rounds in the current clip (SPEC-003 slot 28) |
| 3 | reserve ammunition for the current weapon (SPEC-003 slot 27) |
| 4 | a weapon-table value of the current weapon (SPEC-003 slot 29; meaning UNVERIFIED) |
| 5 | armor |
| 6 | current weapon id (0 during intermission) |
| 7 | selected item count |
| 8 | status flags (table below) |
| 9 | layout flags: 0x1 = show the layout (scoreboard/help) area; 0x2 = deathmatch HUD (always set in deathmatch) |
| 10 | score ("frags"); also used by the engine's `status`/server-info player list |
| 11 | meter value shown when stats[8] bit 0x200 is set (single player: stealth meter = trunc(value × 100); Realistic mode: stamina) |
| 12 | 1 when the level's worldspawn key `forceHUD` is non-zero |
| 13, 14 | 0 (not written) |
| 15 | damage direction indicator of the last hit this frame (table below); 0 when not hit |

stats[8] status flags:

| Bit | Set when |
|---|---|
| 0x0001 | the player module reports its internal flag (SPEC-003 slot 33) |
| 0x0004 | (evaluated only when any other bit of this stat is already set) a trace straight up from the player for 10000 units hits a sky surface |
| 0x0008 | single player: the number of dead hostages has reached the level's limit (worldspawn `maxDeadHostages`, default 3) |
| 0x0010 | crosshair is on a damageable character or player (target identification) |
| 0x0020 | together with 0x0010: that target is friendly (team-mate, or an allied character in single player) |
| 0x0040 | a `trigger_countdown` has expired |
| 0x0080 | mission status message state 1 |
| 0x0100 | mission status message state 2 |
| 0x0200 | show the meter of stats[11] |
| 0x0400 | mission objectives updated (state 3), for 5 s after it was entered |
| 0x0800 | flash-pack blindness above 0.5 |

Damage direction (stats[15]), computed when a client takes damage from an attacker: let a = the yaw of the
direction from the player to the attacker relative to the player's view yaw, in [0, 360). a < 30 or a > 340 or
150 < a < 210 → 0x10; else a ≥ 180 → 0x4; else 0x8. Damage of the environment kinds (damage flags 0x1000, 0x2000,
0x4000, see falling/drowning) sets 0x10, or 0x2 when damage flag 0x2000 is present. UNVERIFIED which of 0x4/0x8 is
left; verify with a damage test from a known side.

### 4. pm_type selection (every ClientThink)

| Condition (first match) | pm_type |
|---|---|
| intermission running | 5, or 7 for spectators |
| movetype noclip (cheat `phantom`) | 1 |
| spectator movetype | 2 |
| the entity's model is not the player model (gibbed) | 4 |
| dead | 3 |
| remote-camera view type > 1 | 6 |
| otherwise | 0 |

While chasing another player (§ Cameras) pm_type is 7 (target alive) or 3 (target dead).

### 5. Player information configstring

Configstring 1415 + client slot (SPEC-203 CS_PLAYERSKINS) = `name\team\model\N`:
`name` = player name (first 15 characters of userinfo `name`); `model` = the player model actually in use
(file name without extension, `mullins` outside deathmatch and when no model could be set up); `team` = the team
name declared by that model, or the model name when the model declares the team `Noteam`; `N` = team number − 1
(team 1 → 0, team 2 → 1, no team → −1). When only the name changed, `N` is written as `0`. Cleared (empty) when the
client disconnects. Bots get no entry update from the team code.

### 6. Userinfo keys

| Key | Use |
|---|---|
| `name` | player name (15 characters kept) |
| `skin` | requested player model (deathmatch): `ghoul/pmodels/<skin>.gpm` |
| `teamname` | team name; written back by the game (see § Player model) |
| `spectator` | deathmatch only: non-empty and not `0` = wants to be a spectator |
| `password`, `spectator_password` | compared with the cvars of the same names |
| `ip` | address, checked against the engine's ban filter at connect |
| `bestweap` | passed to the player module as the weapon-preference tag (SPEC-003 slot 50); also enables switching to a better picked-up weapon (SPEC-781) |
| `predicting` | `0` disables weapon prediction for this client (passed to SPEC-003 slot 73) |
| `team_red_blue` | team games: requested team, (value AND 1) + 1; written back by the game when it assigns a team |
| `rejmsg` | written by the game when it rejects a connection |
Invalid userinfo (fails the info-string validity check) is replaced by `\name\badinfo\skin\mullins`.

### 7. Entity events added by player code (SPEC-202 entity events)

| Event | When |
|---|---|
| 3 / 4 | footstep, walking (alternating feet) |
| 5 / 6 | footstep, running (horizontal speed > 225) |
| 7 | landing, light |
| 8 | landing, falling damage range (delta < 55) |
| 9 | landing, far fall (delta ≥ 55) |
| 15 | respawn / spawn teleport (not for spectators) |
| 22 | deathmatch spawn (effect slots reset) |

## Behaviour

### 1. Connecting

**Pre-connect (SPEC-001 export 0x20).** Always accepts. If the slot is held by a bot, that bot is removed first.
Deathmatch also informs the bot system that the slot is taken.

**ClientConnect.** Checks in order; the first failure writes userinfo `rejmsg` and rejects:
1. the engine's ban filter on userinfo `ip` → `rejected_banned`;
2. deathmatch and the client asks to be a spectator: if cvar `spectator_password` is set (non-empty and not
   `none`) and userinfo `spectator_password` differs → `rejected_spectator_password`; if the number of other
   connected spectators exceeds cvar `maxspectators` → `rejected_spectator_limit_exeeded` (spelling as shipped).
   A spectator is not checked against `password`;
3. otherwise, if cvar `password` is set (non-empty and not `none`) and userinfo `password` differs →
   `rejected_password`.
On success the client record is bound to the entity. If the entity was not in use (a fresh connection rather than
a level change or savegame), the respawn data is cleared (team kept) and the persistent data is initialised:
health 100, maximum health 100, no remembered weapon. Then user info is processed (in-game flag 0), the console
shows `<name> connected` when `maxclients` > 1, and the client is marked connected.

**ClientUserinfoChanged** (SPEC-001 export 0x2C):
- validates userinfo (see above); copies the name; if the name changed, rewrites the player information
  configstring in its short form;
- deathmatch: `spectator` decides the wanted spectator state; outside deathmatch it is always 0;
- copies `bestweap`;
- before the client has entered a deathmatch game, stops here (the model is set up at spawn);
- team games (`deathmatch` 4, 6, 7): the mode assigns the team. Conquer with forced joining refuses changes with
  string DM_GENERIC `NO_CHANGE` ("Cannot Change Teams when in a Force Join game.") and balances new players onto
  the smaller team (random on a tie); otherwise the team is taken from `team_red_blue`. Changing team while alive
  in the game schedules a forced suicide that resets the score to 0 (§ Death, team change);
- sets up the player model (§ Player model). When the requested model/skin/team cannot be used, the userinfo keys
  `skin` and `teamname` are rewritten to `*<actual value>` in deathmatch, or to the actual values outside it;
- in a deathmatch team-play game (`dmflags` 0x80; not in CTF or Control, and in Conquer only without forced joining), if the model's
  team changed while in the game and the player is not a spectator, the player is killed as a suicide
  (means of death 25) and respawns;
- rewrites the full player information configstring;
- stores the userinfo and passes `bestweap` to the player module.

### 2. Entering the game (ClientBegin)

In deathmatch:
1. initialise the entity; clear the respawn data except the team, record the entry frame;
2. destroy the client's inventory object and create a new one (SPEC-003 exports 9/10); for humans send a
   prediction restart (§ Prediction restart);
3. clear the name-colour tables so they are resent; call the game mode's begin hook; remove any body model;
4. PutClientInServer;
5. string DM_GENERIC `ENTERED` ("%s has entered the game!") to everyone; for humans the welcome message
   (SPEC-001 import #16); bots are notified;
6. ClientEndServerFrame.

In single player:
1. reset the mission status message state; call the game mode's begin hook;
2. fresh entity: initialise it with classname `player`, clear respawn data; if there is no inventory object,
   create one and send a prediction restart, otherwise remember the current weapon id as the persistent weapon;
   PutClientInServer;
3. entity still in use (savegame load): delta angles = current view angles; send a prediction restart, then
   svc 30 with the full inventory state (SPEC-003 slot 78), then let the player module rebuild its view weapon
   (SPEC-003 export 11); restore the field of view;
4. if an intermission is running move the client there, else show the welcome message when `maxclients` > 1;
   ClientEndServerFrame; if the level registered a "player began" entity, its use action runs with the player
   as activator (UNVERIFIED which entity; observed as a level-start hook).

**Prediction restart**: the client's resync counter (one byte, wrapping) is incremented and sent reliably as
svc 29 followed by the counter (SPEC-201, SPEC-003). The counter is also mirrored into player-state field 5 every
frame.

### 3. Disconnecting (ClientDisconnect)

Only for clients that had entered: broadcast `<name> disconnected` (print level 2); game-mode hook; remove the
lean, crouch and gun hit volumes (§ Hit volumes); remove the body model and model instance; unlink, make not
solid, model 0, not in use, classname `disconnected`, persistent "connected" cleared; clear the player
information configstring; game-mode disconnect hook; notify the bot system. The engine's drop hook (export 0x3C)
only clears the client's team.

### 4. PutClientInServer (spawn)

1. Pick a spawn point (§ Spawn points) → origin (point origin, z + 9) and angles.
2. Deathmatch: keep the respawn data; for humans re-initialise the persistent data (health 100, max 100, no weapon)
   and re-run user-info processing in its spawn variant (no forced-suicide checks); bots copy their stored
   userinfo first and are flagged as bots. Single player: clear the respawn data.
3. Bots get a fresh inventory object on every spawn.
4. Clear the client record, preserving: inventory object, body object, view-weapon instance, model/skin/team
   names, name-colour tables, prediction counter, persistent data, respawn data. If persistent health < 1 the
   persistent data is re-initialised.
5. Entity: health / max health from the persistent data; takedamage "aim"; movetype walk; view height 35;
   classname `player`; mass 200; solid box; alive; air until now + 12 s; clip mask = player-solid
   (SPEC-001 trace content mask 0x02010003); pain does nothing; die = player death (§ Death); not in liquid;
   knockback allowed; not a dead body for collision; bounding box (−16, −16, −24)–(16, 16, 40); zero velocity.
6. Player state: field of view 95; remote-view entity −1 (none); pmove origin = spawn origin ×8.
7. Game-mode hooks: deathmatch humans get a new inventory object and a prediction restart; then the mode's spawn
   loadout (deathmatch default below; single player § Single-player start).
8. Player module: set owner context, top up clips (SPEC-003 slot 5); clear the music value; model index 255
   (player), skin number = client slot; origin z + 1; delta angles = spawn angles − command angles; entity angles
   (0, spawn yaw, 0); view angles and v_angle = those angles.
9. Not a spectator: player-solid clip mask, visible to clients, KillBox at the spawn point, link. Deathmatch:
   spawn protection until now + 4 s; reset effect slots and tint, add event 22; spawn effect `environ/prespawn`
   at the origin and sound `dm/prespawn.wav` (channel 4, volume 0.6, attenuation 1).
10. Spectator: no chase target, spectating = yes, spectator movetype, not solid, no clip mask, invisible to clients,
    link.
11. Attack button not yet re-armed (the next attack press only re-arms it). If the level's worldspawn has
    `startmusic`, humans get that music value. Leg-injury slowdown cleared.

Deathmatch default spawn loadout (all deathmatch modes except Arsenal, which has its own):
1. remove everything from the inventory, including the CTF flag;
2. player variable A (ammunition used) = −1, or 0 when `dmflags` 0x800 (infinite ammo);
3. humans: player variable C = 0 when `dmflags` 0x40, else −1; weapon tag = userinfo `bestweap`; player
   variable B (weapon dropping) = 1 unless `dmflags` 0x4 (weapons stay); kept-weapon mask = knife, plus the 9 mm
   pistol when `dmflags` 0x10. Bots: weapon tag `unsafe`;
4. give knife (1), 9 mm pistol (3) and the two hand pseudo-weapons (13, 14) with empty clips;
5. unless infinite ammo: 6 knives (ammo 1) and 150 rounds 9 mm (ammo 3);
6. switch immediately to the 9 mm pistol.

Spawn protection (deathmatch): until it expires the player is drawn translucent (Ghoul tint alpha 0.5, back to 1.0
when it ends), takes no damage (unless the damage has the "no protection" flag 0x20), and cannot use `kill`.
Holding the primary or alternate attack button ends it at once (the check runs after the weapon think each frame), and touching an item ends it as well (SPEC-781). The entity effects bit 0x4000 set at respawn is cleared 0.5 s after the respawn
time unless a remote camera is active.

### 5. ClientThink (SPEC-001 export 0x38)

1. Intermission: pm_type 5 (7 for spectators); after 10 s of intermission, an attack press (button 0x01) ends it.
   Nothing else happens.
2. Forced suicide pending (team change): health 0, means of death 25, player death, score := 0; deathmatch →
   corpse + immediate respawn (§ Respawn), single player → console command `respawn`. Stop.
3. Chasing another player: only the command angles are recorded (no movement). Otherwise:
4. **Command splitting**: if msec > 33 and the vertical velocity is not 0, the command is run as msec ÷ 10
   (integer) consecutive moves each lasting (msec mod 10) + 10 ms; otherwise once with its own msec. (This runs
   longer than msec when msec mod 10 ≠ 0; reproduce it for parity.)
5. Each move: pm_type (§ Interface 4); pmove gravity = cvar `sv_gravity`; origin and velocity ×8; the "snap" flag
   is set when the stored pmove state differs from the client's state. Then:
   - **run**: button 0x20 doubles forward, side and up move, unless pm_flags bit 0x40 (exhausted, Realistic mode)
     is set; the client is then "running";
   - **movement speed scale**: q = trunc(scale × 255) AND 255 where scale comes from the game mode (default:
     cvar `g_movescale` × (1 − leg-injury slowdown), the slowdown being 0..0.5; single player: 1.0 or 0.5625
     depending on a level setting, UNVERIFIED which); forward and side move are multiplied by q / 255 and
     truncated;
   - the pmove block gets the trace callback (box trace with the player-solid mask while alive, mask 0x00010003
     while dead), point contents, a callback that answers "is entity number n a client" (1 ≤ n ≤ `maxclients`),
     and the remaining knock-back stagger time (time set by strong knockback: 0.5 + 0.75 × knockback / mass
     seconds, see damage spec) when positive;
   - after pmove: copy the pmove state back; origin and velocity ÷ 8; bounding box from pmove; v_angle from the
     resulting view angles;
   - **jump sound**: was on the ground, now not, up move ≥ 10, pmove water level 0 → `player/jump.wav` on channel
     2, volume 0.6, attenuation 1;
   - lean value (−1, 0, +1 as returned by pmove), view height, water type/level and ground entity are stored;
   - dead: view angles roll 40, pitch −15, yaw = killer yaw; alive: view angles = movement result;
   - link; unless noclip or spectator, touch triggers; then call the touch function of every distinct entity
     pmove touched (activator = player).
6. **Buttons**: old buttons := buttons; buttons := command buttons; latched |= newly pressed. A non-zero primary
   or alternate fire time in the command (SPEC-202 §6.1) is stored for the player module (SPEC-003 server
   callbacks 13–16). While the attack button is not re-armed, the stored primary fire time is cleared each
   command, and a fresh attack press re-arms it.
7. Light level from the command is stored on the entity.
8. Target identification and the use key (below), spectator chase controls (§ Cameras), then the chase cameras of
   all clients chasing this player are updated.

**Use key** (button 0x02, on the press only, not while held):
1. among trigger-type entities within half the player's height (32) of the origin, the first one that has a
   use-key action and whose classname contains `trigger` is used;
2. else among solid entities within 80 units of the eye that have a use-key action: take those whose horizontal
   direction from the player has a dot product ≥ 0.2 with the horizontal view direction and that are reachable by
   a straight trace from the eye (the trace hits them or is unobstructed); use the one with the largest dot product;
3. else trace 75 units straight ahead from the eye with the player-solid mask; if the hit entity has a use-key
   action it is used.
The action is called with (target, player, player). Eye = origin + view-offset height.

**Target identification** (every command): trace 2000 units along the view direction from (origin z + 26, moved
sideways by 2.5 × the current lean roll) with the shot mask 0x06000081. A hit on an auxiliary hit volume counts
as its player unless the player is behind the viewer. Dead targets clear the bits. Team-mate → bits 0x10|0x20;
allied character outside deathmatch → 0x10|0x20; any other damageable character or client → 0x10 only; else
both cleared (stats[8]).

### 6. Weapon think and per-frame start (ClientBeginServerFrame)

Skipped during intermission. Every frame (bots too) the music logic runs first. Then:
- deathmatch: when the wanted spectator state differs from the current one and at least 5 s passed since the last
  respawn → spectator change (§ Spectators);
- alive and not spectating: build the owner context (SPEC-003): attack = (buttons OR latched) bit 0x01 and the
  attack button re-armed; alternate = bit 0x04; bits 0x08, 0x10; lean right / left = lean > 0 / < 0; the
  cinematic-freeze state; third-person flag; pass `predicting` (0 when userinfo `predicting` is `0`, else
  non-zero) to slot 73; run the player module think. In deathmatch, primary or alternate attack held ends spawn protection;
- the notarget flag follows the player module's state flag (SPEC-003 slot 35) unless the `ninja` cheat is on;
- single player: add the current origin to the player trail when the last trail spot is not visible;
- latched buttons cleared; the item system's per-frame update runs (SPEC-781).

Dead players: once level time > respawn time, respawn when (deathmatch) an attack press is latched or `dmflags`
0x200 (force respawn) is set, or (single player) any button is latched. Single player respawn = console command
`respawn` (reloads). Deathmatch respawn = corpse copy, PutClientInServer, event 15, pmove flags "teleport time"
(0x20) with time 14, effects bit 0x4000, respawn time := now. Afterwards (humans) the attack button is not re-armed.

Respawn time after death: now + 4 s (single player) or now + 1 s (deathmatch).

### 7. Death (player death function)

1. Clear angular velocity; takedamage yes; movetype toss; solid type "corpse" (value 5); pitch and roll 0; no
   looping sound; maxs z = −8; marked as dead body for collision.
2. If not already dead: set the respawn time; humans get killer yaw = yaw towards the attacker (else the
   inflictor, else own yaw); pm_type 3; game-mode obituary hook (§ Obituaries); game-mode death-drop hook
   (deathmatch default: the player module drops weapons according to its rules, SPEC-003 slot 2, and the view
   weapon is hidden); scoring hook (§ Scoring); animation priority "death".
3. Deathmatch frag sounds, played only to the attacker (local sound, channel 2, volume 1, attenuation 3), for a
   human attacker: `dm/selffrag.wav` when the attacker killed himself or a team-mate; `dm/frag.wav` for other
   kills except in Assassin (`deathmatch` 2). Bots are notified of the kill. Human victims get the scoreboard
   (`score` toggle).
4. Dead; link; health := −1.

Also in deathmatch, the moment a client's damage kills him: armor is removed and the weapon models in both hands
are taken off the body. While dead, health decreases by 2 per frame down to −100.

**Team change**: see ClientThink step 2. **`kill` command**: refused while the suicide flood delay is active
(string GENERAL `TEXT_NOKILL`, "SUICIDE FLOOD PROTECTION … for another %hd seconds", with the remaining seconds
+ 1); otherwise only when at least 5 s passed since the last respawn and spawn protection is over: god mode off,
health 0, means of death 25, player death, respawn; the delay is then set to now + cvar `flood_killdelay`.

### 8. Spectators (deathmatch)

Spectator change (when wanted ≠ current, ≥ 5 s after the last respawn):
- becoming a player: if `password` is set and userinfo `password` differs → DM_GENERIC `TEXT_BAD_PWD`, the wanted
  state is forced back and the client is told `spectator 1` (svc 13 stufftext, unicast reliable);
- becoming a spectator: if `spectator_password` is set and wrong → `TEXT_SPECTATOR_BAD_PWD`, client told
  `spectator 0`; if the number of spectators already exceeds `maxspectators` → `TEXT_SPECTATOR_LIMIT_FULL`,
  client told `spectator 0`;
- otherwise: a carried CTF flag is dropped (game-mode drop hook), score := 0, PutClientInServer, event 15 and
  teleport time for players, respawn time := now; DM_GENERIC `TEXT_SPECTATOR_TO_GAME` ("%p joined the game") or
  `TEXT_SPECTATOR_TO_SIDELINES` ("%p has moved to the sidelines").

### 9. Spawn points

- Single player: the `info_player_start` whose `targetname` equals the requested spawn point; with no request,
  the first one without `targetname`, else the first one at all. None → error `Couldn't find spawn point %s`.
- Deathmatch, not a team spawn mode: the `info_player_deathmatch` that maximises the distance to the nearest
  living player (ties: the first found); `dmflags` is not consulted.
- Team spawn modes (the game mode decides; Conquer): team 1 or userinfo `teamname` `blue` → `info_player_team1`,
  team 2 or `red` → `info_player_team2`, other team names → random deathmatch point. Among the team's points,
  when there are 3 or more the two closest to living players are excluded; one of the rest is chosen at random.
  No team point → random deathmatch point.
- Deathmatch with no point at all → error `Couldn't find deathmatch spawn point`.
- `info_player_deathmatch`, `info_player_team1`, `info_player_team2` are removed outside deathmatch; in
  deathmatch they are numbered in spawn order (used by bot routes), not solid and not sent to clients.

### 10. Corpses (body queue)

At level start 8 entities with classname `bodyque` are created. On every deathmatch respawn the dying player is
copied into the next one (round robin): origin, angles with pitch 0, entity flags, bounding box and link data,
movetype, solid; it gets a corpse AI and keeps its Ghoul model; thinks every 0.1 s and disappears once its death
animation has finished. If the player was burning (fire time more than 1.2 s ahead) the corpse shows
`environ/onfireburst` and burns for 4 s; phosphorus burning (more than 0.5 s ahead) shows
`weapons/world/phosburn` for 2 s. Cvar `ai_corpselife` is read but does not affect the corpse lifetime
(UNVERIFIED in game).

### 11. Per-frame end (ClientEndServerFrame)

1. pmove origin/velocity ← entity origin/velocity ×8; extra pmove byte ← trunc(move scale × 255).
2. Intermission: blend alpha 0, fov 95, stats; stop.
3. Vectors from v_angle; world effects (§ 13).
4. Entity angles: pitch = v_angle pitch ÷ 3 (pitch > 180 reduced by 360 first), yaw = v_angle yaw, roll =
   4 × side-roll, where side = velocity · right, roll = sign(side) × (|side| < `sv_rollspeed` ?
   |side| ÷ `sv_rollspeed` × `sv_rollangle` : `sv_rollangle`).
5. Bob: xyspeed = horizontal speed. xyspeed < 5 → bob time := 0 and bobmove 0; else on ground: bobmove = 0.3 when
   xyspeed > 210, 0.2 when > 10, else 0.1 (in the air bobmove keeps its previous value). bob time += bobmove;
   bobcycle = trunc(bob time); bobfracsin = |sin(bob time × π)|. (Quake II: 0.25/0.125/0.0625 and halving when
   ducked — not in SoF.)
6. Falling damage (§ 13); cinematic camera update; view offset and cameras (§ 12); screen blend (§ 14); stats.
7. Footsteps: on the ground and bobcycle changed → event 3 (or 5 when xyspeed > 225), +1 when bobcycle is odd;
   bots hear it (radius 250).
8. Objectives-updated jingle: when the level's objectives counter changed (a `target_help` was used), play
   `misc/pc_up.wav` (channel 2, volume 0.6, attenuation 3) at the next three frames whose number is a multiple of
   64.
9. Third-person body animation (§ 16); body per-frame update; weapon pose / carried flag.
10. Remember velocity and view angles; clear the kick origin; weapon kick angles: each component with magnitude
    ≥ 0.8 is multiplied by 0.7, smaller ones become 0.
11. Every 32 frames: refresh the scoreboard layout while it is shown, and the help layout while help is shown.
12. Remote view: unless third-person is on: an active remote camera sets remote entity/type and effects bit 0x4000,
    else remote entity −1; third-person: remote entity 0.
13. Game-mode end-frame hook; send changed name colours (svc 33); player module: flush its events to the client
    (SPEC-003 slots 72 then 71, humans only); resync number; button inhibit bit 0x01 = attack not re-armed;
    spectated-player field 0 unless pm_type 7; cinematic-freeze field.

### 12. View (SV_CalcViewOffset) and lean

Kick angles start at 0. Alive: weapon kick field ← client weapon kick angles; view kick (set by damage and by weapon recoil, SPEC-782): ratio = max(0,
(kick end time − now) × 2), pitch += ratio × kick pitch, roll += ratio × kick roll (both reset when the ratio
reaches 0);
fall kick: ratio = max(0, (fall time − now) ÷ 0.3), pitch += ratio × fall value; run pitch / roll: velocity ·
forward × `run_pitch`, velocity · right × `run_roll`; bob pitch = xyspeed × `bob_pitch` × bobfracsin and bob roll
= xyspeed × `bob_roll` × bobfracsin, each ×6 when crouched on the ground (pmove flags 0x1 and 0x4 both set), bob
roll negated on odd bobcycles. Dead: no weapon kick, view angles roll 40, pitch −15, yaw = killer yaw.

View offset = kick origin + (0, 0, view height − fall ratio × fall value × 0.4 + min(6, xyspeed × `bob_up` ×
bobfracsin)), clamped to x, y ∈ [−14, 14], z ∈ [−11, 40] (Quake II: z ∈ [−22, 30]).

**Lean**: when lean ≠ 0, the player stands on the ground, speed² < 30 and health > 0: a box (−8…8 on all axes) is
traced from the eye to eye + right × (−30 × lean) − up × 5 (lean −1 = right, +1 = left as in SPEC-202) with the
player-solid mask; the view offset becomes the trace end − origin, and roll −= lean × fraction × 8.

### 13. Falling and world effects

Falling (only for the player model and not in noclip): delta = the downward speed lost on landing as in Quake II
(previous vertical velocity when it was negative and the player is airborne with a smaller fall speed now, else
the change of vertical velocity when on the ground). d = delta² × 0.0001; water level 3 → nothing; 2 → d × 0.25;
1 → d × 0.5. d < 1 → nothing. Otherwise: single player tells the player module (SPEC-003 slot 63, with the attack
buttons in the context); effect `land` on the player; d < 15 → event 7. d ≥ 15: fall value = min(d × 0.5, 40),
fall time = now + 0.3; alive → event 8 (d < 55) or 9; pain time = now; damage = trunc((d − 24) × 2); damage < 1 →
none; deathmatch with `dmflags` 0x8 set and 0x10 clear → none; else damage from the world with knock direction
(0, 0, 1), damage flags 0x2002, means of death 24.

World effects: noclip → air := now + 12, stop. Entering liquid → `player/land/water.wav` (channel 4, volume 0.6,
attenuation 1), "in water" flag, damage-debounce = now − 1. Leaving → flag cleared. Head under water (level 3):
looping sound `player/underwtr/underwtr.wav` with volume field 248 while no other looping sound; cleared when the
head leaves. Not head under: air := now + 12, drown damage := 2. Drowning (air over, next drown time passed,
alive): next drown time = now + 1; drown damage += 2 (max 15); if health ≤ that damage play `player/drown1.wav`
(channel 2, volume 0.6); damage from the world, flags 0x4002, means of death 20. In lava (water type bit 0x08):
alive and pain time passed → `impact/player/hit<1..4>.wav` (channel 2), pain time = now + 1; damage 3 × water
level, flags 0x1000, means of death 28. In slime (bit 0x10): damage = water level, flags 0x4000, means of death 21.

### 14. Screen blends (SV_CalcBlend)

The blend starts at (0, 0, 0, 0) every frame and colours are added with the Quake II rule
a' = a + (1 − a) × α, then each channel c' = c × (a ÷ a') + C × (1 − a ÷ a'):
| Condition | Colour C | α |
|---|---|---|
| eye in lava or solid (spectators: lava only) | (1.0, 0.3, 0.0) | 0.6 |
| eye in slime | (0.0, 0.1, 0.05) | 0.6 |
| eye in water | (0.03, 0.08, 0.03) | 0.2 |
| damage | the client's damage blend colour | damage alpha |
| pickup bonus | (0.85, 0.7, 0.3) | bonus alpha |
| flash-pack blindness, goggles off | (1, 1, 1) | flash alpha |
| flash-pack blindness, goggles on | (0.5, 1, 0.5) | flash alpha |
| scripted blend | the scripted colour | scripted alpha |
| gold flash | (198/255, 171/255, 20/255) | gold alpha |
The eye point is origin + view offset; with a remote view of type 1 no contents are tested, other remote types
test the origin. Decay per frame: damage alpha −0.06, bonus alpha −0.1, flash alpha clamped to 0.995 then
reduced by (1 − alpha) × its fade rate, gold alpha × 0.88 and additionally −0.06 once below 0.3; all floored at 0.
A per-server random flicker value changes by a random step in [−0.15, 0.15] on one frame in three (clamped 0..1)
(UNVERIFIED use; believed to drive goggle noise).

### 15. Auxiliary hit volumes

Each player may own up to three invisible, non-solid helper entities chained to it (flag "helper" in their server
flags) that shots and AI line-of-sight checks resolve to the player:
| Helper | Exists | Box (mins–maxs) | Position |
|---|---|---|---|
| lean head | while leaning | (−12, −12, −24)–(12, 12, 26) | the leaned eye point (lean trace end) |
| crouch | deathmatch, alive, while crouched | (−12, −12, 0)–(12, 12, 8) | origin + forward × 8, z + maxs z |
| gun | deathmatch, alive, not spectating | (−12, −12, −4)–(12, 12, 4) | see below |
Gun volume: from the view angles with pitch clamped to [−50, 28] standing or [−5, 28] crouched, the box spans from
the origin to origin + forward × 48 (sorted per axis); its z range is [origin z + 15, z of that point + 32] when
looking up, or [that point z, origin z + 42] otherwise (crouched: 26 and −1 instead of 42 and 15) (UNVERIFIED
reading; verify by shooting near a player's weapon). All helpers are removed when the player dies or disconnects;
death also switches the goggles off. Helper clip mask 0x02030003.

### 16. Deathmatch player model and third-person animation

At level start in deathmatch the game reads `<sv_pmodlistfile>.gpl` (default `ghoul/pmodels/everybody`; an empty
cvar is the fatal error "No pmodel file specified!", a missing file "Could not open %s.gpl") and then
`<sv_altpmodlistfile>.gpl` (default `custom`, optional). Each whitespace-separated token names a model
`ghoul/pmodels/<name>.gpm`; each loadable model is precached (its Ghoul model, the gore-zone skins `gz_sing1..3`,
`gz_sing_k`, `gz_multi1..3`, `gz_multi_k`, `gz_mass1..3`, `gz_mass_k`, the `c_blood` / `c_black` cap skins, face
skins (number depends on `mskins_expression_limit`), the portrait `ghoul/pmodels/portraits/<name>.m32`, the team
icon `ghoul/pmodels/teamicons/<name>.m32` or `pics/menus/teamicons/<name>.m32`, `menus/players/<name>.rmf`, the
armour skin `b_dm_armor` and bolt-ons). The first model that loads is the default model; if none, `mullins.gpm`.

When a player's model is set up (userinfo change, spawn): the impact and pain sounds are precached; the old body
model is removed; the model of userinfo `skin` is loaded, falling back to the default model, then to an error
("No default player model.", "Couldn't set up default player model."). A model whose base is `fem_play` uses the
female body (obituary "herself" texts), others the male body. If the player has armour, the chest/hip parts are
switched to their armoured variants and the material `b` gets skin `b_dm_armor`; the model is rebuilt whenever
armour goes from 0 to > 0 or back. A dead player plays `lb_sautofire_end_n_n`. In CTF a carried flag is drawn as
`items/ctf_flag` bolted to `abolt_hip_r` with skin `ctf_flag_blue` (team 1) or `ctf_flag_red` (team 2), sequence
`flag_hold_idle` while facing away from the movement direction, `flag_run` while running forward.

Third-person animation (deathmatch, alive): Quake II style priorities (death > others; a jump is followed by a
landing). Standing still / moving / airborne states choose: idle `std_ialert_n_p_n`; ladder `std_xladder_up_a_a`
/ `std_xladder_dwn_a_a` (vertical speed > 2 / < −2); jumping forward `std_jrun_fwd_a_a` (horizontal speed > 250
and moving forward); leaning `std|cch_hlean{lt|rt}sht_{to|mid|frm}_{a_n|l_2}` (stand/crouch, left/right, into /
hold / out of the lean, normal / two-handed); grenade throw `std_tgrenade_n_a_n` (after a grenade, C4, flash-pack
or claymore use while standing); waves (command `wave`): 1 `std_hsignalgo_n_a_a`, 2 `std_hsignalstop_n_a_a`,
3 `std_htaunt_n_a_a`, 4 `std_itouchnuts_n_a_n` (else `std_xdance_n_n_n`), other values print
`Unrecognized wave value: %d, trying to play 'go'.` and play 1. `wave` is ignored when crouched or while a
higher-priority animation plays. Animation playback speed follows the movement speed in 13 classes (1 ÷ speed
factor above 1.875, 1.75, …, 0.5 in steps of 0.125; leg injury forces the slow class 5). The weapon pose follows the
current weapon (weapon id → pose: 0→0, 1→1, 2→4, 3→3, 4→5, 5→6, 6→7, 7→9, 8→11, 9→12, 10→14, 11→16, 12→18).

Pain sounds when there is no body model (single player), for living players: fire damage (means of death 27) →
`impact/gore/sizzle.wav` (channel 4); damage taken > 0 → `impact/gore/impact<1..3>.wav` (channel 4) and
`impact/player/hit<1..4>.wav` (channel 2); fully absorbed by armour → `impact/player/armor.wav` (channel 4); all
volume 1, attenuation 1. Single player armour: damage without flag 0x2 is first offered to the player module's
armour rule (SPEC-003 slot 31). Picking up health refreshes the body's wound appearance, and an armour change
between zero and non-zero rebuilds the model (SPEC-781). A medkit clears the leg-injury slowdown (SPEC-782).

### 17. Cameras

**Chase camera (spectators)**: attack press → if not chasing, chase the first other non-spectating player
(none → DM_GENERIC `TEXT_NO_MORE_CHASE`, "No other players to chase"); if chasing, stop (if a box
(−20,−20,−24)–(20,20,40) at the current spot starts in solid, move to the target's origin) and re-enable
prediction. Use press → next player (wraps; skips spectators and free slots). Camera: target's view angles with
pitch clamped to ±56; position 35 units behind the target's eye along the view (16 higher when the target is
airborne), traced against the world; view height 0; pmove "no prediction" flag 0x80. Target alive → pm_type 7 and
the target's view angles; target dead → pm_type 3 with roll 40, pitch −15 and the target's killer yaw. If the
target leaves or becomes a spectator the next valid one is taken, none → chase ends.

**Third-person / follow camera** (`teamview <n>`: n ≠ 0 on, 0 off; cvars `thirdpersoncam`, `followenemy`,
`camera_viewdist`): `followenemy` ≥ 1 forces it on and follows the n-th living character whose classname starts
with `m_` (re-selected when it dies; none → off and `followenemy` reset to 0); `followenemy` −1 uses
`thirdpersoncam`. The camera yaw follows the subject's yaw with 25 % smoothing per frame, sits `camera_viewdist`
(60) behind the subject's eye, is box-traced (±1) with mask 0x3 and moves halfway to its target each frame; it
is sent as remote camera origin (×8) and angles, remote type 1.

### 18. Scoring and obituaries (deathmatch default rules)

Scoring (score = stats[10]):
| Event | Change |
|---|---|
| a client kills another client | killer +1, or −1 when the means of death carries the friendly-fire flag 0x08000000 |
| a client is killed by a non-client (world, character, trap) | victim −1 |
| a client kills himself | victim score := trunc(score − cvar `sv_suicidepenalty`) (default 2) |
Assassin, Arsenal, CTF, Control and Conquer use their own rules (game-mode spec).

Obituaries go to all clients as string-package obituary prints (SPEC-001 import #49) of package DM_GENERIC with the
victim (and attacker) as player arguments; dedicated servers also print the text to the console. Single player has
no obituaries. Rules, means of death m (friendly-fire flag ignored):
1. attacker = victim: `TEXT_OBT_MOD_PHOS_GRENADE` (m 11), `TEXT_OBT_MOD_ROCKET_SPLASH_F/_M` (13),
   `TEXT_OBT_MOD_C4_F/_M` (17), `TEXT_OBT_MOD_CLAYMORE` (18), `TEXT_OBT_MOD_NEURAL_GRENADE_F/_M` (19),
   `TEXT_OBT_MOD_GRENADE_F/_M` (36), any other m → `TEXT_OBT_F/_M` ("killed herself/himself"); `_F` when the
   player's body is the female body;
2. world kinds (any attacker): 20 `TEXT_OBT_MOD_WATER`, 21 `_SLIME`, 22 `_CRUSH`, 24 `_FALLING`, 25 `_SUICIDE`,
   26 `_EXPLOSIVE`, 27 and 32 `_FIRE`, 28 `_LAVA`, 29 `_EXIT`, 30 `_BARBWIRE`, 31 `_DOGBITE`;
3. otherwise, the victim remembers the attacker as killer. Attacker not a client → `TEXT_OBT_DIED`. Client
   attacker → by m: 1 `KNIFE_SLASH`, 2 `KNIFE_THROWN`, 3 `PISTOL1`, 4 `PISTOL2`, 5 `MPISTOL`, 6 `ASSAULTRIFLE`,
   7 `SNIPERRIFLE`, 8 `AUTOSHOTGUN`, 9 `SHOTGUN`, 10 `MACHINEGUN`, 11 `PHOS_GRENADE_2`, 12 `ROCKET`,
   13 `ROCKET_SPLASH`, 14 `MPG`, 15 `FLAMEGUN`, 16 `FLAMEGUN_NAPALM`, 17 `C4`, 18 `CLAYMORE_2`,
   19 `NEURAL_GRENADE`, 23 `TELEFRAG`, 36 `GRENADE_2`, 37 `CONC_GRENADE` (all prefixed `TEXT_OBT_MOD_`), other
   m → `TEXT_OBT_DIED`.

### 19. Scoreboard and help layouts (deathmatch default)

`score` toggles the scoreboard (closing help); `help` (not during intermission) closes the scoreboard and shows
the help layout for 30 s or closes it; `putaway` closes all. While shown they are refreshed every 32 frames; a
dying human gets the scoreboard. The scoreboard is sent as string-package prints of DM_GENERIC layout entries:
1. `LAYOUT_RESET` (`*`);
2. team play (`dmflags` 0x80): teams are formed from players whose `teamname` differs from their `skin`, grouped
   with the team test, summed, sorted, at most 6: `LAYOUT_SCOREBOARD_TEAM` (`team_sb x y client score`), x = 0 for
   teams 0–2 and 160 for 3–5, y = −64 + 32 × (index mod 3);
3. players sorted by score descending, at most 12: `LAYOUT_SCOREBAORD_CLIENT` (`client_sb x y client score ping
   minutes`), x = 0 for rows 0–5 and 160 for 6–11, y = base + 32 × (row mod 6), base = 32 without teams, 32 with
   1–2 teams and 64 with 3 or more; minutes = frames in game ÷ 600;
4. spectators while rows remain (up to 12 rows in total): `LAYOUT_SCOREBAORD_SPECTATOR` (`spect_sb x y client
   ping minutes`) with the same placement.
When a game log file is open the same data is written to it.

### 20. Client commands

Dispatch order: bot waypoint commands (only while `bot_wp_edit` is non-zero and bots are active); `players`,
`say`, `say_team`, `score`, `help` (always); then nothing during intermission or while the cinematic freeze is
on; then — unless spectating — the player module's commands (SPEC-003 slot 4, force 0); then the remaining
commands; any unrecognised command is said to everybody with the command word included (as Quake II).

| Command | Behaviour |
|---|---|
| `say`, `say_team` | as Quake II with these differences: team chat only in team play; text = `<name>: ` + message; both the full text and the message are cut to 150 characters (the sender gets `Chat string too long - truncated to 150 chars`); flood control as Quake II with cvars `flood_msgs`, `flood_persecond`, `flood_waitdelay` (string GENERAL `TEXT_SHUT_UP`, seconds as argument); delivered with the sender-tagged print (SPEC-001 #15, print level 0 = say, 1 = team) to every human client (bots excluded); dedicated servers echo the full text at print level 3 |
| `players` | lists connected players sorted by score: `%3i %s %s` (score, name, a userinfo `ip`; the address printed on each line is the one of the next player in the sorted list — reproduce or fix knowingly), then `%i players` |
| `playerlist` | as Quake II (`%02d:%02d %4d %3d %s%s`, ` (spectator)`, `And more...` past 1350 characters) |
| `score`, `help`, `putaway` | § 19 |
| `kill` | § 7 |
| `wave <n>` | deathmatch only; § 16 |
| `teamview <n>` | § 17 |
| `heretic`, `ninja`, `phantom` | cheats (SPEC-006); `ninja` also makes the game treat every player as not targetable |
| `gimme <classname>` | cheat: spawns the entity 100 units forward and 50 up from the player (truncated to whole units), with the player's yaw |
| `killallmonsters` | cheat: 100000 damage (flags 0x20, means of death 25) to every living character except the protected one |
| `entlist` | cheat: per-classname counts (`Size Type`, `%.4d Monsters`, `%.4d Funcs`, `%.4d Total`) |
| `listpickups` | cheat: item census (SPEC-781) |
| `playlevel`, `elbow`, `bigelbow` | cheats: recreate the inventory object (keeping the player variables and a carried CTF flag), send a prediction restart and give fixed kits, then top up clips and precache the in-view models — playlevel: knife and 9 mm pistol, 150 rounds 9 mm and 6 knives unless infinite ammo, 2 C4, selects the 9 mm, removes all armour, god mode off, cvar `nodamage` 0; elbow: knife, 9 mm, .44, shotgun, sniper rifle, assault rifle, 2000 of every ammunition type 1–9 unless infinite ammo (capped by the player module's maximums), 100 each of grenades, goggles, flash packs and C4 (unlimited maximum), selects the 9 mm; bigelbow: knife, slug thrower, rocket launcher, flame gun, MPG, machine pistol, machine gun, the same ammunition and items, selects the slug thrower |
| `defaultweapons` | single player cheat: the level's default weapons |
| `givesnipertutorial`, `givemoretutorial` | single player: recreate the inventory and give the sniper rifle with 200 rounds 5.56 / the .44 pistol with 90 rounds, and select it (tutorial levels) |
| `updateinvfinal` | single player: end-of-mission statistics update (`stat_moneyadd` added to the money total, mission timers and counters accumulated) |
| `turnoffmissionmsg`, `turnonmissionmsg` | single player: mission message state 0 / 3 (stats[8]) |
| `loadpoint`, `destroyents` | navigation editing (navigation spec) |
| `go <x> <y> <z>` | cheat: sets a debug position (UNVERIFIED use) |
Cheats require cheats to be enabled ("Cheats not available.").

### 21. Single-player start

At every single-player spawn: health 100; the mission statistics counters and the level start time are reset.
Unless a level-script setting overrides it, the player module is reset (all items removed), gets the knife kept,
the hand pseudo-weapons, and then loads the inventory carried over from the previous level from the persistence
cvars (SPEC-003 slot 64, which also restores health); a level flag can force health back to 100; the in-view
models of all owned weapons are precached; the remembered weapon is selected if still owned, else the best weapon.
Level-script override (UNVERIFIED trigger): value −1 on the first tutorial map (`tut1…`) only precaches the sniper
rifle and .44 pistol; value −2 gives a fixed kit — knife, 9 mm pistol, shotgun, 6 knives, 128 rounds 9 mm,
32 shells, 2 C4, 5 flash packs, armour 100, best weapon.

### 22. Noise for AI hearing

Weapons, landings and other actions report noises (origin, radius, kind, whether line of sight is needed). Bots
always hear them. Outside deathmatch and when the source is not "notarget": a noise of kind 4 alerts only the given
entity; otherwise every living character within the radius is alerted (when line of sight is required: only
characters that have the source in front of them and visible); without the line-of-sight requirement, entities
registered as sound-activated are used when the noise origin is closer than the square root of their health value.

### 23. Player trail (single player)

As Quake II: 8 `player_trail` markers, added when the last marker is not visible from the player.

### 24. Intermission and level change

- Starting an intermission (level exit): ignored if one runs. Dead players are respawned first; the intermission
  time and the next map are recorded. In single player, when the next map name contains no `*` the level changes
  at once without an intermission view (and the first client's view weapon field is cleared). Otherwise the
  camera point is a random `info_player_intermission` (one of the first four, chosen by skipping a random 0–3 of
  them), else the first `info_player_start`, else the first `info_player_deathmatch`; every client is moved there.
- Moving a client to the intermission: deathmatch shows the scoreboard; origin and pmove origin at the point; view
  angles of the point; pm_type 5 (7 for spectators); blend alpha 0; underwater flag cleared; view height 0;
  model 0, no effects, no looping sound, not solid, Ghoul model flag cleared; deathmatch sends the scoreboard (the
  first client's call also writes the game log); view-weapon field cleared.
- Clients that connect during an intermission are moved there by ClientBegin.
- Before a level change the game stores, per connected client, health, maximum health, the current weapon id and
  the field of view into the persistent data (SaveClientData); they are restored by PutClientInServer /
  ClientBegin on the next level (inventory itself travels in the persistence cvars, SPEC-003).

## Constants

| Name | Value |
|---|---|
| Client record size | 1536 bytes (player state 200, then 32-bit ping) |
| Player bounding box | (−16, −16, −24) – (16, 16, 40); dead: maxs z −8 |
| View height at spawn | 35 |
| Field of view at spawn / intermission | 95 |
| Mass | 200 |
| Air supply | 12 s |
| Spawn point lift | +9 (point), entity +1 more |
| Respawn delay after death | 4 s single player, 1 s deathmatch |
| Spawn protection (deathmatch) | 4 s, tint alpha 0.5 |
| Suicide restrictions | ≥ 5 s after respawn, then `flood_killdelay` (10) |
| Spectator change delay | 5 s after the last respawn |
| Intermission minimum | 10 s before attack exits |
| Chat limit | 150 characters |
| Flood ring | 10 entries |
| Corpses | 8 |
| Player trail | 8 |
| Use key | trigger radius = half player height (32); facing search 80 units, dot ≥ 0.2; forward trace 75 |
| Target identification | 2000 units, eye z + 26, sideways 2.5 × lean roll, mask 0x06000081 |
| Lean | 30 units sideways, 5 down, box ±8, roll 8 × fraction, speed² < 30 |
| View offset clamp | x, y ±14; z −11 … 40 |
| Bob | bobmove 0.3 (> 210), 0.2 (> 10), 0.1; bob_up max 6; crouch factor 6 |
| View kick ratio / fall kick time | (end − now) × 2 / 0.3 s |
| Weapon kick decay | ×0.7 while ≥ 0.8, else 0 |
| Falling | d = delta² × 0.0001; light < 15; far ≥ 55; damage trunc((d − 24) × 2); fall value min(d/2, 40) |
| Drowning | 2 … 15 per second (+2 each second), means of death 20, flags 0x4002 |
| Lava / slime | 3 × water level (MOD 28, flags 0x1000) / water level (MOD 21, flags 0x4000) |
| Dead health decay | −2 per frame to −100 |
| Chase camera | 35 behind, pitch ±56, +16 when target airborne |
| Follow camera | `camera_viewdist` 60, yaw smoothing 25 %, box ±1, mask 0x3 |
| Leg-injury slowdown | 0 … 0.5 of the move scale |
| Knock-back stagger | 0.5 + 0.75 × knockback ÷ mass seconds |
| Scoreboard | 12 rows, 6 teams, columns x 0 / 160, rows 32 apart |
| Objectives jingle | `misc/pc_up.wav` × 3, every 64 frames |
| `dmflags` bits used here | 0x4 weapons stay (no dropping), 0x8 no falling damage, 0x10 keeps the 9 mm and cancels 0x8, 0x40 player variable C off, 0x80 team play, 0x200 force respawn, 0x800 infinite ammo |

Means of death (values used by the game; flag 0x08000000 = friendly fire):

| Value | Meaning | Value | Meaning |
|---|---|---|---|
| 1 | knife slash | 17 | C4 |
| 2 | thrown knife | 18 | claymore |
| 3 | 9 mm pistol | 19 | neural grenade |
| 4 | .44 pistol | 20 | water (drowning) |
| 5 | machine pistol | 21 | slime |
| 6 | assault rifle (SMG) | 22 | crushed |
| 7 | sniper rifle | 23 | telefrag |
| 8 | slug thrower | 24 | falling |
| 9 | shotgun | 25 | suicide |
| 10 | machine gun | 26 | explosive |
| 11 | phosphorus grenade | 27, 32 | fire (27 also flash pack, molotov) |
| 12 | rocket | 28 | lava |
| 13 | rocket splash | 29 | exit |
| 14 | MPG | 30 | barbed wire |
| 15 | flame gun | 31 | character melee (obituary: dog bite) |
| 16 | flame gun napalm, fire pool | 35 | throwing star (no obituary text: "died") |
| 0 | heavy bullet (characters; obituary "died") | 36 / 37 | grenade / concussion grenade |

Sounds: `player/jump.wav`, `player/land/water.wav`, `player/underwtr/underwtr.wav`, `player/drown1.wav`,
`impact/player/hit1..4.wav`, `impact/gore/impact1..3.wav`, `impact/player/armor.wav`, `impact/gore/sizzle.wav`,
`dm/prespawn.wav`, `dm/frag.wav`, `dm/selffrag.wav`, `misc/pc_up.wav`.
Effects: `environ/prespawn`, `land`, `environ/onfireburst`, `weapons/world/phosburn`.
Classnames: `player`, `bodyque`, `player_trail`, `disconnected`, `info_player_start`, `info_player_deathmatch`,
`info_player_team1`, `info_player_team2`, `info_player_intermission`.

## Test vectors / how to verify

1. Connect with userinfo `\name\x\password\bad` to a server with `password secret` → rejected, client shows the
   `rejected_password` reason. With `spectator 1` and `maxspectators 0` while one spectator is connected →
   `rejected_spectator_limit_exeeded`.
2. Deathmatch: spawn → inventory shows knife, 9 mm with 150 rounds, 6 knives, 9 mm selected; the player is
   translucent for 4 s or until he fires; `kill` during that time does nothing.
3. Deathmatch `kill` 6 s after spawning → console obituary "<name> killed himself", score −2 (default penalty);
   immediate second `kill` → "SUICIDE FLOOD PROTECTION" with 10 or 11 seconds.
4. Fall from 600 units onto ground (fall speed ≈ 693 units/s): d ≈ 48 → damage 48, fall kick for 0.3 s; from a
   height giving d = 20 → event 8, no damage.
5. Stand still, press lean right: view moves 30 units right and rolls +8° (open space); against a wall the move
   is shortened.
6. HUD stats with a packet capture: stats[1] = health, stats[5] = armor, stats[10] = score; stats[9] bit 2 set in
   deathmatch.
7. `players` on a 3-player server: the third column of each line shows another player's address.
8. Deathmatch obituary text for a rocket self-kill by a female model: "<name> blew herself up".
9. Spectator chase: attack → "No other players to chase" when alone.
10. Hold the use key: only the first frame triggers the use action.

## Open questions

- Exact player-module meanings of stats[4] and stats[8] bit 0x0001 (SPEC-003 slots 29 and 33).
- The single-player move-scale level setting (1.0 vs 0.5625) and the level-script value that selects the
  single-player start override.
- The "player began" level hook entity and the `go` command's use.
- Gun hit-volume geometry (reading above is from static analysis only).
- Damage-direction side assignment (0x4 vs 0x8).
- Third-person animation selection details owned by the human body model (body spec).
