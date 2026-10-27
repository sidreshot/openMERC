# SPEC-741: Multiplayer bots (hidebot) — management, behaviour and waypoints
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-143 (`.wrs` layout), SPEC-133 (`.bpf`, `bot/botlist.txt`, `bot/mapcfgs`), SPEC-313 (engine
commands `bot_add`, `bot_rem`, `hackkillbots`, cvars `bot_name0`…`bot_name31`), SPEC-302 / SPEC-006 (cvar
registrations), SPEC-780 (client record, spawn points, client think, footstep events), SPEC-781 (item indices,
CTF and Control flags), SPEC-600 (weapon ids), SPEC-601 (inventory, item types), SPEC-201 (user command fields),
SPEC-131 (string packages), SPEC-740 (single-player navigation — not used by bots)

## Purpose

The multiplayer bots shipped inside the game module (the "hidebot 1.0" design by Rich Whitehouse, documented for
users in `bot/README.TXT`). This spec covers how bots are added and removed, what a bot client is, the per-frame
think that turns perception into a user command, the aim and weapon model, game-mode objectives (CTF, Control,
Conquer, Assassin, Arsenal, Realistic), chat, and the waypoint system (`.wrs` load/write, automatic links, routes,
in-game editor). Bots exist only under deathmatch-family rules (cvar `deathmatch` ≠ 0); in single player none of
this runs.

Notation: *max* = the value of cvar `sv_maxvelocity` (default 2000), used as the "full" movement command;
U(a, b) = uniform random float; "intelligence", "accuracy", "fov", "turning speed", "view distance", "can chat",
"chat frequency" are the personality values of SPEC-133. Distances in units, angles in degrees, times in seconds.
A bot thinks once per server frame (0.1 s).

## Interface / layout

### Files

| File | Use |
|---|---|
| `hidebot.cfg` | executed (`exec hidebot.cfg`) at each level start when cvar `bot_configexec` ≠ 0 (not shipped) |
| `bot/mapcfgs/<map>.cfg`, else `bot/mapcfgs/default.cfg` | executed at each level start; `<map>` = map name without directories (`dm/jpndm2` → `jpndm2`) |
| `bot/<bot_listfile>` | personality list (default `botlist.txt`) |
| `bot/bots/<name>.bpf` | personalities (SPEC-133; parse rules below) |
| `bot/wpfiles/<map>.wrs` | waypoints (SPEC-143; semantics below); written to `<gamedir>/bot/wpfiles/` |
| `strip/bot.sp` | string package 12 `BOT` (messages below) |

### Cvars (registered by the game module; defaults per SPEC-006)

| Cvar | Default | Effect |
|---|---|---|
| `bot_waypoints` | 1 | 0: no waypoint file is read; bots roam ("roam mode") and may join without a `.wrs` |
| `bot_wp_edit` | 0 | 1 (before the level loads): waypoint edit mode — editor commands and drawing; no bots join |
| `bot_wp_info` | (set by code) | ≠ 0 in edit mode: centre-print of the waypoint nearest to client 1; edit cursor = that waypoint |
| `bot_listfile` | `botlist.txt` | list file name under `bot/` |
| `bot_random` | 1 | 0: personalities in list order; ≠ 0: random, least-used first |
| `bot_fillserv` | 0 | ≠ 0: keep the server filled with bots up to maxclients − 1 clients |
| `bot_configexec` | 1 | ≠ 0: `exec hidebot.cfg` at level start |
| `bot_chat` | 0 | ≠ 0: bots may chat |
| `bot_wppref` | 1 | ≠ 0: clever bots travel to better-weapon spots ("hot spots") |
| `bot_hunt` | 1 | ≠ 0: bots track enemies they hear |
| `bot_notarget` | 0 | ≠ 0: bots acquire and keep no enemy |
| `bot_runtact` | 1 | ≠ 0: clever bots drop explosives while running away |
| `bot_timing`, `bot_clientstatus` | 0 | registered, never read |
| `bot_name0`…`bot_name31` | `??????` | engine-side slots (SPEC-313); the game polls them every frame |
| `bot_team0`…`bot_team31` | `0` | per-slot team, read only when the rules allow per-bot teams (below) |

The engine's `bot_num` is never read by the game module. There are no game-side `addbot` / `removebot` commands in
1.07f (the README's commands are served by the engine's `bot_add` / `bot_rem` and the menus).

### Waypoint editor commands (client commands; accepted only in edit mode, after the waypoint system is initialised)

| Command | Effect |
|---|---|
| `makewp [n]` | waypoint at the player origin: kind 1 (crouch) when the player is ducked, else 0; with 0 < n < count it is inserted at index n |
| `makewpj` | kind 2 (jump) waypoint (argument ignored) |
| `makewp_nv` | kind 9 ("force visible") waypoint (argument ignored) |
| `makewp_el [n]` | kind 4 (elevator) waypoint (insert index honoured) |
| `remwp` | delete the waypoint at the edit cursor (last placed, or nearest to client 1 when `bot_wp_info` ≠ 0) |
| `teletowp [n]` | player origin ← waypoint n (0 or absent: the last one); needs ≥ 1 waypoint and `bot_waypoints` ≠ 0 |
| `writewpfile [name]` | write `<gamedir>/bot/wpfiles/<name or map>.wrs` |
| `bot_wp_connect a [b]` | tag a ↔ b (both must be untagged); without b (or 0): clear a's tag pair |
| `bot_wp_drawonly a.b` | draw only waypoints a…b; `0` draws all |
| `bot_wp_verifypaths` | prints `ERROR! Path %d has two junctures coming off of waypoint %d` per offending waypoint, then `Paths verified. %d error%s reported.` |
| `bot_wp_whichpath id` | `Node %d is in path %d` / `Node %d is not in a path (probably doesn't exist)` |
| `bot_wp_pathinfo n` | `PATH %d: start node = %d, end node = %d` then `...with path %d between nodes %d and %d` per juncture; else `%d is not a valid path.` |
| `bot_wp_botroute` | for the bot nearest to client 1: `BOT: %s near node %d, path %d  DESTINATION: %s %s` (destination origin and `node %d`, or `NONE` `NONE`), then `Step %d: node %d on path %d` per route step; inconsistent route: `Man, I don't know. Try again.` |

### Messages (string package 12 `BOT`, index: text)

0 `Waypoint file '%s' is version %d, expected version %d`; 1 `Incorrect header information for waypoint file %s`;
2 `No visible nodes near the spawnpoint at %s. Put at least one node near it.`; 3 `There are no other waypoints
visible from waypoint %d. That's bad.`; 4 `%s disconnected`; 5 `Waypoint file %s doesn't exist for %s. You must place
the waypoints manually.`; 6 `Waypoints loaded and placed from file.`; 7 `In waypoint edit mode. Use writewpfile to
save any changes.`; 8 `Couldn't create waypoint file %s.`; 9 `Waypoint data has been written to %s.`; 23 `%s: Hey!
There aren't any control points in this map!`; 24 `Waypoint limit of %d hit. Failed to create waypoint.`;
25 `No free slots available for %s. Bot will not be added.` (10–22 are menu texts; 21 and 22 are unused by the game
module). Messages 6 and 7 are printed from literal copies of the same text as developer messages.

### Personality record (what the game keeps per `.bpf`)

userinfo string (up to 512 characters), accuracy, fov, view distance, turning speed, general intelligence, can chat,
chat frequency (default 20 when the key is absent), and six chat sections (lists of lines).

## Behaviour

### 1. Level start and readiness

1. When `worldspawn` spawns under deathmatch rules: the bot list is reloaded (all personalities parsed), every client
   slot becomes available to bots, the danger lists (section 10) are emptied, `hidebot.cfg` is executed when
   `bot_configexec` ≠ 0, then the map config (`bot/mapcfgs/<map>.cfg` if the file exists, else
   `bot/mapcfgs/default.cfg`).
2. 10 s later the waypoint system initialises (capacity 500): with `bot_waypoints` = 0 roam mode is set and the system
   is *ready*. Otherwise `bot/wpfiles/<map>.wrs` is loaded; missing → message 5 (path, full map name) and the system
   is *not ready* (no bot ever joins on that level unless `bot_waypoints` is 0); loaded → ready unless `bot_wp_edit`
   ≠ 0 (edit mode: never ready), developer message 6 or 7. Then item waypoints are tagged (section 14.6).
3. Nothing bot-related runs during these first 10 s; bots therefore appear at the earliest 10 s into a level.
4. At level exit and shutdown the bot count is reset and personalities and waypoints are freed.

### 2. Adding and removing bots (every frame, only when ready or in roam mode)

H = number of human clients in game, R = number of slots with a pending request; *lastHuman* = highest client number
held by a human; *firstBot* = lowest client number held by a bot. The 32 slots `bot_name<i>` are polled every frame;
a slot is *unset* only when its value is exactly `??????`.
1. If firstBot = lastHuman + 1 (a bot sits directly above the last human, e.g. after a human disconnected), that bot
   is removed and its name is written back into its slot only if a higher client number is free (so it re-joins at
   the top), else the slot becomes `??????`.
2. For each slot: unset while its bot is in game → remove that bot. Set, no bot, no request: if H + R < maxclients − 1
   → request (time = now); else message 25 (the name) and the slot is reset to `??????`. Set, bot in game, and the
   value was changed → remove that bot and reset the slot (renaming a bot just removes it in 1.07f).
   With per-bot teams allowed (only Conquer mode with dmflags bit 0x8000) and `bot_team<i>` changed, the bot's team ←
   (int) value and its userinfo is re-applied.
3. Add: the oldest pending request is served when at least 1.5 s passed since the last add and (no bot is in game or
   firstBot − lastHuman > 2): a bot client is created (section 3); on success the request is cleared.
4. `bot_fillserv` ≠ 0: when H + R = maxclients and requests are pending, drop (below); else if the add condition holds
   and H + R < maxclients − 1, the first slot with neither bot nor request gets a random personality name (least-used
   rule, section 4) and a request. Result: bots fill the server to maxclients − 1 clients, one per ≥ 1.5 s, leaving a
   slot for a joining human. Without `bot_fillserv`: H + R = maxclients with requests pending → drop.
5. Drop: the lowest-numbered bot is removed when the number of pending requests ≤ the number of free client edicts from
   its number up to maxclients; otherwise the pending request is cancelled (slot reset).
6. A human connecting into a client number makes that number unavailable to bots (a bot occupying it is removed); a
   human disconnecting makes it available again.

### 3. A bot client

- Client number: the highest available number whose edict is not in use (none → no bot).
- The edict gets the bot flag and entity flag 0x200; its client userinfo is the personality's `userinfo` text
  verbatim (no name de-duplication), userinfo-changed processing and the normal client-begin run; the engine never
  sees a network connection (server-side emulation). An idle-chat timer is set to now + U(75, 100).
- Removal: print message 4 (bot name) to everyone, the rules' client-leave hooks run, its body/hitbox parts are freed,
  the edict is unlinked and freed (classname `disconnected`), and the player-skin configstring of the slot
  (1414 + entity number) is emptied.
- Every bot runs the normal player code each frame with the user command built in section 5 (msec 100); spawn,
  damage, pickups, scoring and respawn are those of a human (SPEC-780). When spawn-point selection picks a spot for a
  bot, the bot's "last waypoint" becomes the waypoint recorded for that spawn point (section 14.5).

### 4. Personalities

- List file: one personality name per line (CR LF line ends), lines starting with `#` ignored, names truncated to 63
  characters; each name's `bot/bots/<name>.bpf` is parsed at once (a file that cannot be read yields no record).
- `.bpf`: only the first 65 536 bytes are used. Every `#` starts a comment running up to and including the next line
  feed (so a comment after a value also swallows that line's end). Keys are found by a case-sensitive substring search
  over the comment-free text (first occurrence anywhere): `userinfo=` (value up to CR), `accuracy=`, `fov=`,
  `turning_speed=`, `view_distance=`, `general_intelligence=`, `canchat=` (numeric prefix of the text up to the line
  feed, truncated towards zero; no clamping), optional `chatfreq=` (default 20). A missing mandatory key makes the
  original read invalid memory (UNVERIFIED crash; a re-implementation should treat it as 0). Chat sections: for
  n = 1…6 in order, `[SECT=n]` … `[SECTEND=n]`, one line per chat line (empty lines count); parsing stops at the first
  missing marker.
- Choice for a new bot: a name requested through a slot is matched case-insensitively against the list (no match →
  the first personality). Otherwise `bot_random` = 0 → list order, cycling (the cursor survives level changes);
  `bot_random` ≠ 0 → r = random 0…n−1, step = n; while r's use count this level exceeds the current round: r ←
  (r + step/2) mod n and step halves (while > 1); after n probes the round increases and r ← random 0…step−1. The use
  count of the chosen entry increases (fill requests choose the same way without counting).

### 5. Per-frame think (each bot, every server frame)

1. Prepare: command cleared (msec 100); a weapon change detected → firing held for 2 s; when the player's use probe
   (SPEC-780) finds an entity whose classname contains `door` and the previous door pause ended more than 2 s ago, a
   door pause of 1.5 s starts (waypoint following is suspended, the bot's use reaction opens the door); health 1…49
   → the medkit (item type 5) is selected; the last-waypoint record is refreshed (section 13).
2. Enemy update (section 6).
3. Chat (section 12): while a line is being "typed" the bot stands (view kept) — end of think.
4. Fight (section 7) when an enemy exists and the bot neither retreats nor pursues an item.
5. Chase the last sighting (section 8) — may stand and end the think.
6. Item goal (section 9) — may end the think.
7. Waypoint following (section 13), not during a door pause — may end the think.
8. Command finalization (section 11), then the normal client think with the command.
A dead bot clears enemy, waypoint, goal and item, and presses attack with probability 1/2 each frame (respawn).

### 6. Perception and enemy choice

- **Noise record** (every client has one): a sound started by an entity that is a player (classname `player`) or is
  owned by one records loudness = volume × 200 and kind 0 for that player, unless the sound's last start argument is 1
  (UNVERIFIED meaning; a bot's own sounds with that argument are not played at all); `PlayerNoise` records its loudness argument with kind 0 (UNVERIFIED argument); each footstep event (SPEC-780,
  events 3–6) records loudness 250, kind 2. A record lasts 1 s from the latest noise; within that second the loudness
  is the maximum heard; after it expires the next noise starts from 0.
- A client C is *audible* to a bot when C's record is current, C's noise kind ≤ the bot's intelligence, and the
  distance ≤ loudness (× 2 when intelligence > 2).
- **Acquisition** (when no enemy and `bot_notarget` = 0): over clients 1…maxclients that are in use, alive, not
  spectators, not the bot, the nearest one that (is audible, or lies within the field of view: target yaw within
  ±⌊fov/2⌋ of the bot's horizontal view yaw and target pitch within ±⌊fov/2⌋ of level, integer degrees), is within the
  view distance, passes the Assassin rule (mode 2: only the bot's assigned target or hunter), is in the bot's PVS and
  visible (Quake II `visible`), and is not on the bot's team. The bot itself must not be a spectator.
- **Damage**: an attacker becomes the enemy of a client that has none.
- **Keeping**: a visible enemy is dropped when it is dead, a spectator, on the bot's team, the bot itself, fails the
  Assassin rule, or `bot_notarget` ≠ 0. An enemy that is not visible is dropped; before that, when the bot does not
  retreat and has no active item goal, an enemy that is less than 15 units higher than the bot makes a bot with
  intelligence > 2 head for the waypoint nearest to the enemy (visible, within 128) or, if none, start the
  last-sighting chase (section 8) for 10 s. A retreating (or item-seeking) bot with intelligence > 3, an enemy within
  512 at about the same height (±5) and `bot_runtact` ≠ 0 arms the *run tactic* (section 10).
- **Hunting** (`bot_hunt` ≠ 0, no destination): the nearest audible live non-spectator client that the bot would not
  retreat from becomes the destination (waypoint nearest to it, visible, within 64).

### 7. Fighting: aim, fire and combat movement

- **Aim point**: enemy origin with z − 10 (− 20 more when the enemy is ducked; rocket launcher: z − 30). Pitch is set
  instantly; yaw goal = yaw to the aim point + e, where e is an integer error 0…accuracy−1 re-drawn when its timer
  expires (timer U(3,5) for intelligence < 2, U(2,4) for 2, U(1,2) for 3, U(0.5,1) for ≥ 4). The error is always added
  in the positive yaw direction (counter-clockwise; the original draws a sign but applies it so that both signs add
  |e|).
- **Turning**: each frame the yaw moves towards the goal along the shorter way by at most turning speed degrees
  (yaw kept on the 1/65536-turn grid).
- **Fire cone**: the bot fires only when the yaw to the enemy is within ±(min(max(accuracy, 0), 160) + 10)° of its
  view yaw. With intelligence > 3, accuracy < 20 and the slug thrower or rocket launcher, it also needs ±10°.
- **Fire timing**: no fire before the client's fire-allowed time or while the weapon is held after a change; when the
  bot's next-fire time has passed it presses alternate or primary fire (below) and sets the next-fire time:

| Weapon id (SPEC-600) | primary: next fire after | alternate: next fire after |
|---|---|---|
| 1 knife | 0 | 2.0 |
| 2 pistol2 | 0.5 | 0 |
| 3 pistol1 | 0.3 | 0 |
| 4 mpistol | 0.16 | 0 |
| 5 assault | 0.08 | 0 |
| 6 sniper | 2.0 | 0 |
| 7 slugger | 0.3 | 2.0 |
| 8 shotgun | 1.5 | 0 |
| 9 machinegun | 0.08 | 1.5 |
| 10 rocket | 3.0 | 3.0 |
| 11 mpg | 0.08 | 2.0 |
| 12 flamegun | 0 | 1.3 |
| 13, 14, 15 | 1.0 | 0 |

  Otherwise both buttons are released. Unless ammunition is infinite (dmflags) or the mode is Arsenal, a bot with no
  ammunition at all (both inventory counts 0) waits 2 s.
- **Alternate fire** is chosen (with d = distance to the enemy, "ammo" = one of the two inventory ammunition counts
  of the current weapon — UNVERIFIED which —, "free" = infinite ammunition or Arsenal): knife: ≥ 2 knives (the other
  count) and 300 < d² < 500 (squared distance as coded, so
  practically never); slugger: (ammo ≥ 3 or free) and 350 < d < 500; machinegun: (ammo ≥ 10 or free) and 350 < d < 500;
  rocket: (ammo ≥ 4 or free) and d > 600; mpg: (ammo > 10 or free), d < 300 and accuracy < 20; flamegun: (ammo ≥ 6 or
  free) and 350 < d < 500; otherwise primary.
- **Range keeping**: preferred range r = 350 (slugger, rocket), 30 (knife), else 100. Closer than
  √(r² + 40·(10 − r)) (100 → 80, 350 → 330, 30 → 10) and 200 units straight behind clear → back off (forward −max, with a
  ledge check behind); farther than √(r² + 8·(5r + 50)) (100 → 120, 350 → 370, 30 → 50) → advance (forward +max, ledge
  check ahead); in between → no forward move. The ledge check (intelligence > 2) reverses the forward move when the
  point 50 units ahead/behind is clear and the floor there is more than ~124 units down or is slime/lava.
- **Strafing**: direction right, left or none; switches after now + 1 + U(1,3) (then a pause "none" for 0.5 + U(0,1)
  with chance 5/10 for intelligence < 2, 3/10 for 2, never above); switches early (next switch now + 1 + U(1,5)) when
  the side is unsafe (intelligence ≥ 2: a probe 100 units sideways and 50 down — also ±8 units forward — finds no
  floor, or the floor is slime/lava), blocked by a wall within 50 (intelligence > 2), or would lose sight of the enemy
  30 units over (intelligence > 2).
- A jump is requested for 0.1 s when an obstacle needs a jump (section 13).

### 8. Retreat and last-sighting chase

- The bot *retreats* (outside roam mode) when it carries a CTF flag, or (intelligence ≥ 3 and (health ≤ 45, or its
  weapon is weak against the enemy's, or it holds the flamegun and the enemy is farther than 350, or any explosive is
  listed as live, section 10)). Weapon strength ranks: pistol1 1, pistol2 2, sniper 3, mpistol 4, shotgun 5, assault 6,
  slugger 7, rocket 8, flamegun 9, mpg 10, machinegun 11, others 0; *weak* = own rank < enemy rank − 2 or own rank < 2.
  A retreating bot does not fight; it keeps following waypoints (with the danger rule of section 13).
- Last-sighting chase: stage 1 walks back to where the bot stood when it last fought, stage 2 (after reaching it,
  < 30) to where the enemy was; it ends on arrival (< 30), after 10 s, or when the way drops off (150 ahead, floor
  > 124 down or slime/lava).

### 9. Items

- Wanted items (registered item entities that are not hidden): health items (#37, #38) when health < 100; ammunition
  and weapons always; armour and equipment when intelligence ≥ 3; the CTF flag of the bot's own team when it is not
  at base (to return it) and the enemy flag ("enemy flag" kind).
- When the bot has no item goal and the item retry time has passed: the nearest wanted item within 512 that is not
  higher than the bot and has floor halfway towards it (a trace from the bot to the halfway point 41 below hits
  something; items not more than 5 above), or the enemy flag at any height, becomes the goal, give-up time now + 10.
- The goal is dropped when no longer wanted, out of PVS, higher than the bot or unreachable by the halfway test
  (retry after 15 s), when reached (< 10; retry after 5 s), or at the give-up time (retry after 10 s). Meanwhile:
  without enemy the bot faces it and moves towards it (45° side windows, section 11), with an enemy it aims, fires
  and moves towards it (90° windows); obstacles are handled as in section 13.

### 10. Explosives

- Live grenades, C4 charges and flash packs are listed when thrown/placed and removed when they go off. The "danger
  near" test of 1.07f ignores the distance: it is true whenever any of the three lists is non-empty.
- Grenades in a fight (waypoint following with an enemy): when the next-grenade time has passed and the bot carries a
  grenade (item type 6) or a flash pack (type 1): with probability 1/2 (or when a switch is already in progress) it
  selects the grenade (else the flash pack); the throw happens through normal fire control; next try after U(15, 30).
- Run tactic (armed in section 6): while retreating without an enemy, the bot selects C4 (type 2) or else a grenade
  and fires it; then item switching is locked for 2 s.

### 11. Command construction

- Movement values are kept as forward, side and up; the waypoint and item code move by "windows": target within ±w°
  of ahead → forward +max, of behind → −max; within ±w° of right → side +max, of left → −max (w = 90 for forward/back;
  side windows 90 in fights, 45 otherwise).
- Roam mode (no enemy or retreating, no item, no waypoint): pitch 0; when the way ahead is unsafe (50 ahead blocked,
  a drop > 124 or slime/lava there, or either ±18 side probe blocked) forward 0 and a random new yaw U(0, 360), else
  forward +max.
- Up: crouch (−max) at a crouch waypoint or under a low ceiling (50 ahead blocked at origin height but clear 23
  lower); jump (+max) while a jump is requested; otherwise 0. A non-zero up value is sent for one frame and then
  suppressed for 0.2 s (bots jump and crouch in pulses).
- The flame gun keeps the attack button held while firing.
- Before the client think the values are scaled: forward ±max → ±0.2·max; side +max → +0.2·max, but side −max is left
  at −max (1.07f asymmetry: bots strafe left at full command and right at 0.2·max; the player movement code then
  limits the speed, so leftward moves are dominated by the side component). Realistic mode: full moves become ±0.1·max while
  the bot is "tired": for 5 s after a per-bot tired flag was seen, and for 3 s after a per-bot counter exceeded 50
  while the bot had no enemy (both values are maintained outside the bot code; UNVERIFIED meaning: stamina).
- User command fields (SPEC-201): msec 100, buttons, view angles, forward/side/up moves, fire and alternate-fire
  values; the bot's delta angles are zeroed every frame.

### 12. Chat (only with `bot_chat` ≠ 0 and the bot's can-chat value ≠ 0)

- A line is prepared for a bot B towards another client X from a chat section; `%` is replaced by X's name, `$` by
  the name in upper case, `&` in lower case (result ≤ 256 characters). It is said at now + ⌊(length − 1)/5⌋ s,
  at least 2.5 s later ("typing"); a bot with intelligence > 2 drops a pending line when it has an enemy. The line is
  shown to every human client and on a dedicated server's console as `name: text` (team lines only to the team when
  teamplay is on).
- Triggers (each needs: B has no pending line, a roll U(0, 100) ≤ chat frequency, X not on B's team): B killed by X
  (deathmatch rules, X a client ≠ B) → section index 1; B killed X → index 2; idle (no enemy, idle timer passed, timer
  re-armed U(75, 100); X = the k-th in-use client, k = ⌊U(1, number of in-use clients)⌋, may be B itself) → index 3.
- Reply: when a said line names a bot X, X answers with chance ⌊chatfreq_X × 2.5⌋ (capped at 90 when that exceeds 90
  and chatfreq_X < 90): index 1 → 5, 2 → 6, 3 → 4, provided X has nothing pending or its pending index is below 5.
- **Section numbering (1.07f off-by-one)**: index k uses file section `[SECT=k+1]`; index 6 has no section. So the
  README's "section 1 you-killed-me" lines are never used and "you killed me" uses section 2, and so on. Stock files
  have empty sections, so stock bots never chat.

### 13. Waypoint following (section 5 step 7)

Skipped while an item goal exists, or while an enemy exists and the bot does not retreat.
1. **Current waypoint**: dropped when it is out of PVS or the straight line to it is blocked by something other than a
   player and no `door` is within 150 ahead (kind 9 waypoints are never dropped this way). Without one, the bot
   takes, among the link targets of its last waypoint (all waypoints when it has none, or for file versions < 4),
   the nearest one that is visible, not at the origin and less than 5 units above the bot; failing that the nearest
   waypoint with a clear line from the bot. Deadline = now + 30.
2. **Mode objectives** (destination waypoints, section 15) are refreshed.
3. **Danger** (intelligence ≥ 3, any live explosive): the walk direction is set from the danger's position (towards
   the neighbouring waypoint that is farther from it — UNVERIFIED sense); otherwise, with `bot_wppref`, a hot spot may
   be chosen (14.7).
4. **Reaching**: within 30 units (kind 10: within 5) the waypoint is reached: a jump waypoint (kind 2) requests a
   1 s jump; a reached destination is cleared; the next waypoint is chosen (route or roaming, 14.8–14.9); deadline =
   now + 30.
5. **Stuck handling**, with remaining = deadline − now: between 10 and 29 s remaining the bot jumps (1 s) when an
   obstacle needs a jump (knee-high block: clear 24 above the origin but blocked 23 below over 50 units towards the
   waypoint; or a second probe pair 40 above / 10 below over 20 units), when the waypoint is a jump waypoint, when it
   is kind 2 and less than 26 s remain, or when less than 15 s remain; otherwise, when not jumping, it side-steps away
   from walls (forward probes from ±18 sideways and centre: right blocked → step left, left or centre blocked → step
   right). With no enemy and the deadline passed, the waypoint is dropped and the bot stands.
6. **Elevator waypoint** (kind 4) whose mover (a `func_door`, `func_train` or `func_plat` within 40 below the
   waypoint) is not there: the bot waits facing it (or fights); it backs off when another bot uses the same waypoint
   within 256, or when itself within 128.
7. **Moving**: no enemy → face the waypoint (turning rule) and move with 45° side windows (run tactic, section 10);
   enemy → aim and fire at it while moving with 90° windows (grenade rule, section 10).
8. **Ladders** (kind 11 followed by kind 10): forward +max, no up move.

### 14. Waypoint system

1. **Records**: waypoints are numbered 1…n (position = id; 0 is an unused dummy), each with an origin, a kind, a tag
   (another waypoint id or 0), the stored `next_d2` value, a weapon preference value (run time), and up to 8 directed
   links (target id with flag 0x8000, cost). Capacity 500 waypoints.
2. **Loading `.wrs`** (SPEC-143 grammar): header `Version N` (first character not `V` or no `Version` word → message 1
   / 0; N ≠ 5 → message 0 with the file, N and 5; parsing continues anyway). Waypoint lines: id, kind, `(x y z)`;
   for N > 4 an optional `c<id>` tag; for N ≥ 2 the `next_d2` number (a line that ends there is not added); for N ≥ 3
   the link pairs. Ids are taken positionally (the file's id field is not checked). A `$` ends parsing. The links of
   the file are trusted as stored. The `dm`/`team1`/`team2` sections are read (N ≥ 4) but then discarded: the spawn
   table is rebuilt from the map (14.5). Then paths are built (14.4) and ladders marked: for consecutive ids i, i+1,
   when z rises by more than 64 and the horizontal distance is below 64, i becomes kind 10 and i+1 kind 11 (this also
   upgrades stock waypoints stored as 0).
3. **Automatic links** (built by `writewpfile`, `makewp` with an insert index, `remwp`): first a waypoint is added at
   every spawn point that has no waypoint with a clear line within 10 units; then for every waypoint i the candidates
   are the other waypoints j with a clear point trace i → j (mask 0x2010003, ignoring client 1) or a trace blocked by
   an entity whose classname contains `door`; the 8 nearest by squared distance are kept (duplicates and spawn-point
   waypoints skipped); each becomes a link with cost = squared distance and flag 0x8000 when the distance is below
   150. Links are stored most-expensive first (the file order). A waypoint without candidates → message 3.
   The original lists links in the reverse of insertion; consumers below use "cheapest" explicitly.
4. **Paths and junctures**: a *path* is a maximal run of consecutive ids k, k+1, … where each waypoint links to the
   next and none is a spawn-point waypoint (at most 255 paths; more → `Too many paths...some paths will not used for
   route-finding.`). For each path end, the cheapest link with cost < 16384 (128 units) whose target is not one of its
   consecutive linked neighbours and not a spawn waypoint gives a *juncture* to the path containing the target
   (target outside any path → dead-end juncture); duplicates are skipped. In edit mode the paths are verified
   (`bot_wp_verifypaths` messages).
5. **Spawn table**: for every `info_player_deathmatch`, `info_player_team1`, `info_player_team2` (each class
   separately, keyed by the spawn point's spawn ordinal) the nearest waypoint with a clear point trace is recorded; the
   writer emits these tables as the `dm`/`team1`/`team2` sections (key, waypoint id, ascending key). A bot spawned at
   such a point starts with that waypoint as its last waypoint.
6. **Item tagging** (after loading and after writing): every in-use item entity (weapons, CTF flags #36, control flags
   #39) is associated with the nearest visible waypoint within 64 (waypoint below the item + 41 and not more than 45
   under it). CTF flags define the blue and red base waypoints; control flags register control points (their waypoint
   kind becomes 3); weapon items set the waypoint's preference: pistol2 1, pistol1 0, mpistol 2, assault 6, sniper 3,
   slugger 5, shotgun 4, machinegun 10, rocket 7, mpg 9, flamegun 8. In Conquer mode the first waypoint inside a
   `trigger_conquer` zone becomes the mode goal.
7. **Hot spots** (`bot_wppref` ≠ 0, intelligence ≥ 4, a current waypoint and no destination): minimum preference by
   held weapon (pistol2 2, mpistol 3, assault 7, sniper 4, slugger 6, shotgun 5, machinegun 11, rocket 8, mpg 10,
   flamegun 9, others 1; 4 if the weapon is unknown); among waypoints with preference ≥ that minimum and farther than
   1024, a candidate replaces the best so far when its preference exceeds the best by more than 3, or exceeds best − 3
   and it is closer. It becomes the destination.
8. **Routes** (to a destination waypoint): cached for 20 s per destination. Start = the bot's last waypoint (or the
   nearest non-spawn waypoint). A depth-first search over junctures (from the juncture of the start's path nearest to
   the start id, towards the juncture of the destination's path nearest to the destination) tries, at each juncture,
   the next juncture along the same path, then crossing to the other path, then the previous juncture, never
   revisiting a juncture side and never immediately undoing the last move; it stops after 100 steps (the partial
   route is used). It is not a shortest-path search. The bot then walks along path ids (+1 or −1) to each juncture,
   crosses to the other path's id, and finally walks to the destination.
9. **Roaming** (no destination), on reaching the current waypoint: continue to id ± 1 in the walk direction when the
   current waypoint links to it; otherwise jump to the tagged waypoint and continue along its path; otherwise (path end)
   take the cheapest link that leaves the consecutive chain, choosing the walk direction from the new waypoint's
   linked neighbours; with no such link return along the remembered way or reverse direction.
10. **Writing**: links are rebuilt (14.3), then `Version 5`, the three sections, and one line per waypoint
    `"%d %d %s%s%9.1f %s"`: id, kind, origin as `(x y z)` with each coordinate truncated to an integer, the tag
    (` c<id> ` when set, else one space), `next_d2` = squared distance to waypoint id + 1 from the unrounded origins
    (0 for the last), and the links as ` %d %9.1f` (target with flag, cost) for the first 8 links in stored order.
    Mode text (CR LF). A failure → message 8; success → message 9 (the path, or `backup file` when a name was given).
    Without a name the file is written once per level unless in edit mode.
11. **Editing**: placement is refused at the capacity (message 24, and `bot_wp_info` is set to 0) and in play mode;
    insertion and removal shift tags ≥ the index, untag a removed waypoint's partner, renumber ids and rebuild all links.
    Drawing (each frame in edit mode, at most two waypoints per frame visible from client 1, round robin): a 20-unit
    debug box coloured by kind (README: standard red, crouch green, jump blue; the last placed, or the one the player
    stands at, multicoloured), a line to the next id when linked and visible (red; yellow for a ladder pair), a line to
    the tagged waypoint (blue), a ring on juncture waypoints. With `bot_wp_info` ≠ 0 each frame centre-prints
    `%s #%d of %d    type: %s    origin: %s` (label `node`, or `**SPAWNPOINT**` + newline + `node` for spawn
    waypoints; type names `standard`, `crouch`, `jump`, `control`, `elevator`; 9, 10, 11 print as force visible,
    bottom/top of ladder — UNVERIFIED exact names for those three).

### 15. Game modes (deathmatch value)

- **Assassin (2)**: enemies are restricted to the bot's target and its hunter (the mode's rule).
- **Arsenal (3)**: ammunition never runs out for the bots' fire logic.
- **CTF (4)**, teams 1 (blue, own flag blue) and 2 (red):
  - role: a bot not carrying a flag becomes *attacker* when an enemy carries a flag; otherwise by balance among bot
    teammates: fewer attackers than defenders → attacker, more → defender, equal → attacker.
  - defender: heads for its own base waypoint when more than 512 away;
  - carrier: heads for its own base waypoint when more than 128 away, unless an enemy carries a flag and every
    teammate is a bot defender;
  - attacker objective, re-evaluated every 5 s: (a) a teammate carries the enemy flag and no enemy carries ours →
    escort (the carrier's nearest waypoint within 512, when more than 256 away); (b) own flag not at base and no enemy
    carrier → go to the dropped own flag, announce `Going to return our flag.`; (c) enemy flag not at base and no enemy
    carrier → go to the dropped enemy flag, `Their flag's been dropped!`; (d) a teammate carries a flag → `We have their
    flag.`, enemy base waypoint when more than 1024 away; (e) otherwise `I'm going after their flag.`, enemy base
    waypoint when more than 128 away. An announcement is made only when the objective changes; it is queued as the
    console command text `say_team <name>[<?>]: <text>` (team 2's return message: `say_team <name>: Going to return our
    flag.`); the bracketed field receives no value in 1.07f (UNVERIFIED what appears), and where the command goes
    depends on the host (on a listen server it is the local player's team chat).
- **Realistic (5)**: walking when tired (section 11).
- **Control (6)**: attacker/defender role as in CTF (with "all points ours"/"none ours" overriding the balance);
  attackers go to the nearest control point not owned by their team (else the nearest owned one), defenders to the
  nearest owned one (else the nearest other one); no control point at all → message 23 with the bot's name; a point
  is kept while its ownership still fits the role; a defender within 512 of its point keeps its goal. Role: all
  points owned → defender; none owned → attacker; otherwise the CTF balance rule.
- **Conquer (7)**: bots without an enemy (or retreating) head for the mode goal waypoint; inside the zone within 50 of
  it they hold position, facing waypoints two ids back and forth, alternating every U(1.5, 3.5) s.

## Constants

| Name | Value |
|---|---|
| bot think interval / command msec | every server frame / 100 |
| waypoint load delay | 10 s after `worldspawn` |
| add interval | ≥ 1.5 s; join condition firstBot − lastHuman > 2; fill limit maxclients − 1 |
| idle chat timer | U(75, 100) s |
| chat delay | ⌊(len − 1)/5⌋ s, min 2.5 s; reply chance ⌊2.5·chatfreq⌋ (cap 90) |
| noise | sounds: volume × 200, kind 0; footsteps 250, kind 2; window 1 s; hearing × 2 for intelligence > 2 |
| FOV half-width | ⌊fov/2⌋ (45 when unknown) |
| aim | z − 10 (ducked − 30, rocket − 30 total); error 0…accuracy−1, always positive; timers U(3,5)/U(2,4)/U(1,2)/U(0.5,1) |
| fire cone | ±(clamp(accuracy, 0, 160) + 10)°; ±10° extra rule |
| retreat health | ≤ 45 |
| ranges | preferred 350 / 30 / 100; back-off probe 200; ledge 50 ahead, drop > ~124 (d² > 15376) |
| strafe | switch 1 + U(1,3), forced 1 + U(1,5); pause 0.5 + U(0,1); probes 100 side + 50 down, wall 50, sight 30 |
| item search | 512; reach 10; give-up 10 s; retries 5 / 10 / 15 s |
| waypoint reach | 30 (kind 10: 5); deadline 30 s; jump window 10–29 s; door pause 1.5 s |
| grenades | retry U(15, 30) s; explosive switch lock 2 s |
| medkit | selected when health < 50 |
| move scale | forward ±0.2·max, right +0.2·max, left −max; realistic walk 0.1·max; jump/crouch pulse 1 frame per 0.2 s |
| links | 8 nearest visible; flag 0x8000 below 150; juncture branch < 128 |
| capacity | 500 waypoints, 255 paths; route search 100 steps, cache 20 s |
| hot spots | distance > 1024; preference margin 3 |
| CTF distances | defend 512, carry home 128, escort 256 (within 512), "we have" 1024, attack 128; objective period 5 s |

## Test vectors / how to verify

1. **Join delay**: DM on `dm/jpndm2` (has a `.wrs`), `bot_add 4 Kirt` at map start: the bot appears after ≥ 10 s, in
   the highest free client slot, named and skinned from `Kirt.bpf`; `set bot_name0 ??????` → `Kirt disconnected`.
2. **No waypoints**: a DM map without `.wrs`: message 5 after 10 s and no bot joins; with `bot_waypoints 0` bots join
   and roam (random turns at ledges).
3. **Fill**: `bot_fillserv 1`, maxclients 8, one human: bots join one per ≥ 1.5 s until 7 clients are in.
4. **Aim error side**: a bot with accuracy 120 facing a stationary target: its shots land consistently to one side
   (counter-clockwise) of the target, never the other.
5. **Turn speed**: a bot with turning speed 18 needs 10 frames (1 s) to turn 180°.
6. **Writer**: in edit mode on a map, `makewp` ×3 along a corridor, `writewpfile`: the file starts with `Version 5`,
   lists spawn sections, lines with costs equal to squared distances and flag 0x8000 on links shorter than 150;
   message 9.
7. **Stock `.wrs` data checks (done)**: cost = d² (68 083/68 136 exact or within print rounding), flag 0x8000 ⟺
   d < 150 (all), ≤ 8 links and descending cost order (all 9 630 waypoints), no link targets a spawn waypoint
   (706/707), ladder rule predicts every stored 10/11, `next_d2` = squared distance to id + 1 (9 583/9 586).
8. **Chat off-by-one**: give a `.bpf` distinct lines in all six sections, `canchat=1`, `chatfreq=100`, `bot_chat 1`;
   being killed by the bot shows a section-3 line, killing it a section-2 line.
9. **Strafe asymmetry**: watch a bot strafe in a fight: leftward strafes are faster/wider than rightward ones.

## Open questions

1. The loudness argument passed by `PlayerNoise` (which player actions call it in 1.07f).
2. The text produced by the unfilled `[%s]` field of the CTF announcements, and where `say_team` from the server
   command buffer ends up on a dedicated server.
3. The exact direction chosen by the danger rule (13.3), and the exact item-switch result codes of the explosive
   selection (10).
4. Conquer mode with dmflags 0x8000 and `bot_team<i>`: the rules' team hook is called with the wrong entity when a bot
   is added (likely crash).
5. With all slots full of humans and pending requests, the drop step writes outside the slot table (memory effect
   unknown).

## Amendments to SPEC-143 (answers to its open questions 4 and 5)

- `.wrs` waypoint flags are kinds: 0 standard, 1 crouch, 2 jump, 10 bottom of ladder, 11 top of ladder (10/11 also
  recomputed on load), plus 3 control, 4 elevator, 9 force visible (never stored in stock files).
- `c<id>` is a manual mutual tag (`bot_wp_connect`); roaming bots jump to the tagged waypoint at the end of a path.
- `next_d2` is the squared distance to waypoint id + 1 at write time; it is loaded but unused.
- Link flag 0x8000 marks links shorter than 150 units; consumers mask it off to get the target id (UNVERIFIED that any
  1.07f behaviour depends on the flag itself).
- The `dm`/`team1`/`team2` sections map spawn ordinals to waypoints and are ignored on load (rebuilt from entities).
- Neither `.nvb` nor `.wrs` is regenerated during normal play: `.nvb` only by the developer editor (SPEC-740), `.wrs`
  links only by the waypoint editor commands.
