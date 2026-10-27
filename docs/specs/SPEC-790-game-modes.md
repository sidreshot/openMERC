# SPEC-790: Game modes — selection, deathmatch flags, scoring, teams, mode rules, level end, scoreboards, game log
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-001 (exports/imports, game type name), SPEC-003 (player module: inventory operations, player
variables A–I), SPEC-006 (module map, cvars), SPEC-131 (string packages), SPEC-202/203 (player state, configstrings),
SPEC-302 (cvars), SPEC-720 (damage), SPEC-721 (gore zones), SPEC-760 (world core, skill record, CheckDMRules outline),
SPEC-780 (clients, default scoring, obituaries, default scoreboard, intermission), SPEC-781 (items, flags), SPEC-782
(spread).

## Purpose

The game module plays one *game mode* per level, chosen by the cvar `deathmatch`. Every mode-dependent decision of
the game goes through the current mode: which deathmatch flags are honoured, how kills are scored, what a player
spawns with, what happens to dropped items, which teams exist, when the level ends, what the scoreboard looks like,
and the optional end-of-game log file. This spec gives, mode by mode, everything that differs from the deathmatch
defaults already specified in SPEC-780/781/782, plus the defaults that those specs only mention.
Single-player mission flow (objectives, level transitions, skill presets) is specified in SPEC-760, SPEC-761 and
SPEC-780; this spec only adds what the single-player mode itself decides (Behaviour 11).

Terms (as SPEC-780): *frame* = 100 ms; *team game*, *team number* (1 = Blue, 2 = Red, 0 = none), *score* = stats[10]
(the client's frag count). "Message X/Y" = a string-package print of package X entry Y (SPEC-131); the English text
of the shipped package is quoted. "Local sound" = a sound only the named player hears (SPEC-001 import 22, `local` 1).

## Interface / layout

### 1. Mode selection

| `deathmatch` | Mode | Game type name (SPEC-001 export 0x50) | Own string package |
|---|---|---|---|
| 0 | single player | `single player` | `singleplr` |
| 1 | deathmatch | `DM`, or `Team DM` when `dmflags` 0x80 is set | — |
| 2 | Assassin | `Assassin` | `dm_assassin` |
| 3 | Arsenal | `Arsenal` | `dm_arsenal` |
| 4 | capture the flag | `CTF` | `dm_ctf` |
| 5 | Realistic | `Realistic`, or `Team Real` with 0x80 | — |
| 6 | Control | `Control` | `dm_control` |
| 7 | Conquer | `CTB`, or `Team CTB` with 0x80 | — |

- The mode is chosen again at every level start (before any entity is spawned) from the integer value of
  `deathmatch`; `deathmatch` is a latched cvar, so a change takes effect with the next map. Values outside 0..7 are
  not supported (the original's behaviour is undefined for them).
- Before a mode exists (no level loaded) the game type name is `Unknown`.
- Every mode, including single player, registers package `dm_generic` at level start; the deathmatch modes
  (1–7) also precache effects `environ/irespawn`, `environ/prespawn` and sounds `dm/armorpu.wav`, `dm/irespawn.wav`,
  `dm/frag.wav`, `dm/selffrag.wav`, `dm/healthpu.wav`, `dm/prespawn.wav`. The mode's own package and assets are
  registered after all map entities have spawned (Behaviour 1).

### 2. Configstrings written by the modes

| Index | Written | Value |
|---|---|---|
| 8, 9 | CTF | flag state of team 1 / team 2 (SPEC-781); `d` for both when the level exits |
| 8, 9 | Control | `0` at level start, then the team 1 / team 2 control score as decimal text (Behaviour 9) |
| 10 | Conquer | refreshed by every client's end of frame when changed: the decimal value of `dmflags` AND 0x8000 formatted into a 5-byte buffer, i.e. `0` or `3276` (sic, truncated) — UNVERIFIED that clients read it |
| 32 | level start | `1` in Assassin, else `0` (meaning for the client: Assassin target icons; UNVERIFIED) |
| 33 | level start | `1` when the mode is a team game (§4), else `0`; in Conquer refreshed every frame as `-1` / `0` (sic) |
| 34 | Assassin level start | `1` (UNVERIFIED client meaning) |
| 1447..1450 | Assassin level start | CS_DMICONS: `""`, `noshoot`, `targetyou`, `destarget` (icon indices 0–3 of Behaviour 6) |
| 1415 + n | team assignment (CTF, Control, Conquer) | player info string `name\teamname\model\N` (SPEC-780 Interface 5), rewritten when the mode sets the team |
| 3545 + n | Control | control points (SPEC-781 §10) |

### 3. Deathmatch flags (`dmflags`)

Bit meanings (the comments of the shipped `dedicated.cfg` number them 01–15 from bit 0x1):

| Bit | dedicated.cfg text | Effect (where specified) |
|---|---|---|
| 0x1 | No health pickups spawned in level | item filter (SPEC-781 §1) |
| 0x2 | No item pickups spawned in level | item filter |
| 0x4 | Weapons remain after being picked up | weapons stay (SPEC-781 §4); player variable B = 0 at spawn (no dropping by command, SPEC-780 Behaviour 4); the death drop still happens (Behaviour 3) |
| 0x8 | Player takes no damage from falling | SPEC-780 §13 |
| 0x10 | Realistic, body part specific damage | realistic damage (SPEC-720, SPEC-782); spawn keeps the 9 mm pistol (Behaviour 3); medkit limit (SPEC-781) |
| 0x20 | Next level always same as current | Behaviour 2 (map rotation) |
| 0x40 | Weapons do not reload | player variable C = 0 at spawn |
| 0x80 | Teamplay is enabled | team play (§4) |
| 0x100 | Player's fire can't hurt teamates | friendly fire blocked (SPEC-720 §2) |
| 0x200 | Players respawn immediately after dying | SPEC-780 Behaviour 6 |
| 0x400 | No armor pickups spawned in level | item filter |
| 0x800 | Weapons use no ammo | infinite ammunition (player variable A = 0); ammo pickups removed; short knife pickups |
| 0x1000 | Weapon pickups rotate a la Quake | floating spinning pickups (SPEC-781 §1) |
| 0x2000 | Bullet only weapons | standard weapons only (SPEC-781 §1) |
| 0x4000 | CTF FOrce Join | force join (§4); read only by CTF, Control and Conquer |
| 0x8000 | (not listed) | Conquer only: *numbered teams* (§4, Behaviour 10) |

Per mode, a flag is either read from `dmflags` ("cvar"), forced on, or forced off:

| Bit | SP | DM | Assassin | Arsenal | CTF | Realistic | Control | Conquer |
|---|---|---|---|---|---|---|---|---|
| 0x1 | off | cvar | cvar | cvar | cvar | off | cvar | cvar |
| 0x2 | off | cvar | cvar | cvar | cvar | off | cvar | cvar |
| 0x4 | off | cvar | cvar | off | cvar | off | cvar | cvar |
| 0x8 | off | cvar | cvar | cvar | cvar | off | cvar | cvar |
| 0x10 | off | cvar | cvar | cvar | cvar | **on** | cvar | cvar |
| 0x20 | off | cvar | cvar | cvar | cvar | cvar | cvar | cvar |
| 0x40 | off | cvar | cvar | **on** | cvar | off | cvar | cvar |
| 0x80 | off | cvar | off | off | **on** | cvar | **on** | cvar |
| 0x100 | off | cvar | off | off | cvar | off | cvar | cvar |
| 0x200 | off | cvar | cvar | cvar | cvar | cvar | cvar | cvar |
| 0x400 | off | cvar | cvar | cvar | cvar | cvar | cvar | cvar |
| 0x800 | off | cvar | cvar | **on** | cvar | off | cvar | cvar |
| 0x1000 | off | cvar | cvar | off | cvar | cvar | cvar | cvar |
| 0x2000 | off | cvar | cvar | off | cvar | cvar | cvar | cvar |
| 0x4000 | off | off | off | off | cvar | off | cvar | cvar |
| 0x8000 | off | off | off | off | off | off | off | cvar |

Notes: a forced-off 0x8 in Realistic means falling damage always applies; forced-off 0x100 in Realistic means team
mates can always hurt each other; forced-on 0x10 in Realistic means realistic damage always. Whatever the table says,
the item filters of each mode (SPEC-781 §1) only consult the flags listed there.

### 4. Teams

- **Team game**: CTF and Control always; DM, Realistic and Conquer when the mode's 0x80 is on; never in single
  player, Assassin, Arsenal. A team game uses the team spawn points `info_player_team1/2` (SPEC-780 §9 rules for
  team spawn points apply to every team game, not only Conquer).
- **Team play** (the mode's 0x80): enables team chat (`say_team`), the team rows of the default scoreboard, team
  frag limits, and friendly-fire handling (SPEC-720 §2).
- **Same team** (used by damage, chat, scoreboards, frag limit, bots): two client entities without character AI are
  on the same team when
  - `deathmatch` is 4 or 6, or 7 with both numbered teams (0x8000) and team play: their team numbers are equal
    (two players with team number 0 count as the same team — UNVERIFIED in game);
  - otherwise, with team play: their team names are equal (string compare), where a player's team name is `blue`
    for team number 1, `red` for 2, else the userinfo `teamname`;
  - otherwise: never.
  Pairs involving characters follow the character rules of SPEC-700 (not part of this spec).
- **Team number assignment** (only in CTF, Control and Conquer, on every user-info change of a connected player):
  - without force join (0x4000 off): team := (integer value of userinfo `team_red_blue` AND 1) + 1. If the player
    already had a different non-zero team, was connected and in use, a forced team-change suicide is scheduled
    (SPEC-780 Behaviour 5 step 2, score reset);
  - with force join: a player who already has a team keeps it, gets message DM_GENERIC/NO_CHANGE ("Cannot Change
    Teams when in a Force Join game.") and his userinfo `team_red_blue` is rewritten to his team; a player without a
    team is put on the team with fewer players (counting the team numbers of all other client slots up to
    `maxclients`), on a tie a random one (U{0,1}: 0 → Red, 1 → Blue), and `team_red_blue` is written back. Developer
    messages: `player %d %s attempting to join`, one `player %d %s val %d` per slot, `team balance blue %d red %d
    player put on team %d`;
  - in both cases (humans only) the player info configstring 1415 + slot is rewritten.
- On disconnect, CTF and Control clear the player's team number (CTF also drops a carried flag as an item drop).

### 5. Cvars used by the modes

`deathmatch`, `dmflags`, `fraglimit`, `timelimit`, `sv_suicidepenalty`, `g_movescale`, `sv_maplist`, `ctf_loops`,
`ctf_loops_count`, `ctf_flag_captured`, `ctf_flag_respawn_time`, `ctf_control_delay_time`, `control_limit`,
`control_bonus`, `log_file_name`, `log_file_counter`, `log_file_mode`, `log_file_header`, `log_file_line_header`,
`log_file_footer`, `hostname`; single player: the skill record of SPEC-760 B9.2 (spawning level, encumbrance value).

### 6. Messages

| Package / entry | Text | Used by |
|---|---|---|
| DM_GENERIC FRAG_LIMIT_HIT | `$P_YELLOWFrag Limit of %hd frags hit.\n` | level end |
| DM_GENERIC TIME_LIMIT_HIT | `$P_YELLOWTime Limit of %hd minutes hit.\n` | level end |
| DM_GENERIC NO_CHANGE | `Cannot Change Teams when in a Force Join game.\n` | force join |
| DM_GENERIC LAYOUT_RESET, LAYOUT_SCOREBAORD_CLIENT, LAYOUT_SCOREBOARD_TEAM, LAYOUT_SCOREBAORD_SPECTATOR, LAYOUT_CTF_CLIENT, CONQUER_LAYOUT_SCOREBOARD_TEAM | layout strings (§7) | scoreboards |
| DM_ASSASSIN TEXT_LOST_FRAG | `$P_YELLOWYou lost a frag... don't kill bystanders!` | Assassin |
| DM_ASSASSIN LAYOUT_TARGET / LAYOUT_NO_TARGET / LAYOUT_NUM_ENEMIES | `*xv -80 yv 10 string "Target objective: >> %p <<. Terminate!!."\n` / `*xv -80 yv 10 string "No target available to assassinate."` / `yv 34 string "Number of enemies targeting you: %d."` | Assassin status |
| DM_ARSENAL TEXT_BIG_WINNER / TEXT_LAST_WEAPON | `$P_YELLOWWoo hoo!!! %p is a big winner!!!\n` / `$P_YELLOW%p has only one weapon left!!!\n` | Arsenal |
| DM_ARSENAL LAYOUT_STATUS | `*xv -80 yv 10 string "You have %d weapons remaining 'til big winner bonus."` | Arsenal status |
| DM_CTF FRAG_CARRIER_RED/_BLUE | `%p just fragged team Red's flag carrier - bonus of %hd!\n` | CTF scoring |
| DM_CTF FRAG_HURT_CARRIER_RED/_BLUE | `%p fragged %p who shot at team Red's flag carrier. Bonus of %hd\n` | CTF scoring |
| DM_CTF PROTECT_FLAG_RED/_BLUE | `%p protected team Red's flag - bonus of %hd.\n` | CTF scoring |
| DM_CTF END_GAME_BONUS_RED/_BLUE | `$P_YELLOW%p gets bonus of %hd for capturing team Red's flag at team Blue's home base.\n` | capture |
| DM_CTF END_GAME | `Flag captured and returned to base - Game over.\n` | level end |
| DM_CTF TEAM_SCORE_LAYOUT | `ctf_team_sb %hd %hd %hu %hd %hd` | CTF scoreboard |
| DM_CONTROL TEAM_SCORE_LAYOUT | `control_team_sb %hd %hd %hu %hd %hd` | Control scoreboard |
| DM_CONTROL END_GAME | `Score Limit Reached  - Game over.\n` | level end |
"_RED" texts name team Red and "_BLUE" texts team Blue; which variant is chosen is given in each rule below. Other
entries of these packages (ASSIGN_*, BAD_SKIN, NO_CHANGE of DM_CTF/DM_CONTROL, END_GAME_TEAM_BONUS, PROTECT_FLAG of
DM_CONTROL) are not used by the 1.07f game module.

### 7. Scoreboard layout entries

All scoreboards start with DM_GENERIC LAYOUT_RESET (`*`) and are sent as string-package prints to one client.

| Entry | Format | Arguments |
|---|---|---|
| LAYOUT_SCOREBAORD_CLIENT | `client_sb %hd %hd %hu %hd %hu %hu` | x, y, client slot, score, ping, minutes in game |
| LAYOUT_SCOREBOARD_TEAM | `team_sb %hd %hd %hu %hd` | x, y, client slot of the team's first player, team score |
| LAYOUT_SCOREBAORD_SPECTATOR | `spect_sb %hd %hd %hu %hu %hu` | x, y, client slot, ping, minutes |
| LAYOUT_CTF_CLIENT | `client_ctf_sb %hd %hd %hu %hd %hu %hu %hd` | x, y, slot, score, ping, minutes, team number of the flag he carries (0 none) |
| DM_CTF TEAM_SCORE_LAYOUT | `ctf_team_sb %hd %hd %hu %hd %hd` | x, y, team − 1, sum of the team's scores, team's captures |
| DM_CONTROL TEAM_SCORE_LAYOUT | `control_team_sb %hd %hd %hu %hd %hd` | x, y, team − 1, sum of scores, team's control score |
| CONQUER_LAYOUT_SCOREBOARD_TEAM | `client_conq_sb %hd %hd %hu %hd %hu %hu ` | x, y, team − 1, sum of scores (only these four are supplied; the last two fields receive whatever follows — UNVERIFIED rendering) |
Minutes = (frames since the client entered) ÷ 600, integer. Ping = the client's ping (SPEC-780 Interface 1).

## Behaviour

### 1. Level start
1. Before the map's entities are spawned: the mode is selected (§1); configstrings 32 and 33 are written; Assassin
   writes CS_DMICONS (§2); package `dm_generic` is registered and, in deathmatch, the generic deathmatch assets are
   precached; Realistic forgets the item positions remembered on the previous level and Conquer forgets its zones.
2. While entities spawn: items pass the mode's item filter (SPEC-781 §1); Realistic remembers the origin of every
   surviving map item together with its item index (several positions per index); each `trigger_conquer` (an
   ordinary brush trigger in every mode) registers with Conquer, which moves the trigger's origin to the centre of
   its box (the position of its loop sound, Behaviour 10); control flags register with Control (SPEC-781 §10);
   `misc_ctf_base` exists only in CTF (Behaviour 8).
3. After all entities have spawned, the mode's level setup runs:

| Mode | Setup |
|---|---|
| single player | registers `singleplr` |
| DM, Assassin, CTF, Realistic, Control, Conquer | in-view model precache for weapons 1 (knife), 3 (9 mm pistol), 13 and 14 (hands); one 9 mm pistol pickup is spawned and freed at once (model precache) |
| Assassin | also: `dm_assassin`; sounds `dm/assassin/killtarget.wav`, `dm/assassin/badkill.wav`; configstring 34 = `1` |
| Arsenal | `dm_arsenal`; sounds `dm/arsenal/bigwinner.wav`, `dm/arsenal/lastweapon.wav`; in-view precache of weapons 1–14 and one pickup of every weapon 1–14 spawned and freed; draws the weapon set (Behaviour 7) |
| CTF | also: `dm_ctf`; the Ghoul model `items/ctf_flag` with sequences `flag_hold_idle` and `flag_run` is registered for precaching; sounds `dm/ctf/flaggrab.wav`, `dm/ctf/flagret.wav`, `dm/ctf/flagbase.wav`, `dm/ctf/triumphant.wav`, `player/flaglp.wav`; cvar `ctf_flag_respawn_time` registered (default `90`, archived); flag/zone touches allowed from now + `ctf_control_delay_time` |
| Control | also: `dm_control`; `items/ctf_flag` with `flag_hold_idle`; sounds `dm/ctf/flaggrab.wav`, `dm/ctf/triumphant.wav`; configstrings 8 and 9 = `0`; both control scores 0; next accrual time = now; touch delay as CTF |
| Conquer | also: sounds `dm/conquer/beep.wav`, `player/conquerLP.wav`, `null.wav`; the cached values of configstrings 33 and 10 are cleared (so they are resent); touch delay as CTF |
| Realistic | also: dropped-weapon return delay 30 s, retry interval 10 s (SPEC-781 §7) |

The *touch delay* is one shared time: flags (SPEC-781 §9–10) and `trigger_conquer` zones ignore touches until level
time exceeds it. Only CTF, Control and Conquer set it; in other modes it keeps its previous value (UNVERIFIED: zero
after a fresh start).

### 2. Every frame and the end of a deathmatch level
Each server frame, after all entities have run: the mode's frame rule runs (Arsenal: Behaviour 7; Conquer:
Behaviour 10; others nothing), then, in deathmatch and outside an intermission, the end rules are checked in this
order (the first that fires ends the level):
1. Control: the control accrual and limit (Behaviour 9) — reaching `control_limit` prints DM_CONTROL/END_GAME.
2. CTF: cvar `ctf_flag_captured` ≠ 0 → DM_CTF/END_GAME.
3. `timelimit` ≠ 0: any in-use client that has been in the game for at least `timelimit` × 600 frames (counted from
   that client's own entry frame, not from the level start) → TIME_LIMIT_HIT with the limit in minutes.
4. `fraglimit` ≠ 0: without team play, any in-use client with score ≥ `fraglimit`; with team play, any team whose
   summed score (players grouped with the same-team rule, §4; spectators included) is ≥ `fraglimit` →
   FRAG_LIMIT_HIT with the limit.

Ending the level (as Quake II EndDMLevel): when the mode's "same map" flag (0x20) is on, the next map is the current
one. Otherwise, if `sv_maplist` is non-empty it is split at spaces, commas, newlines and carriage returns; if the
current map name is found (case-insensitive) the following entry is used, wrapping to the first entry after the
last; a map that is not in the list ignores the list. Without a list entry: the level's next map, else the first
`target_changelevel` of the map, else the current map. The intermission then starts (SPEC-780 §24).

When the level exits, the mode's level-exit rule runs: CTF clears both teams' capture counts, sets configstrings 8
and 9 to `d` and sets cvars `ctf_loops_count` and `ctf_flag_captured` to 0; Control clears its owned-point counts,
scores and point count; Conquer clears its cached configstring values.

### 3. Deathmatch defaults (DM; and every mode below unless it says otherwise)
- **Scoring** of a kill: SPEC-780 §18 (killer +1, friendly-fire kill −1, killed by the world −1, suicide
  score := trunc(score − `sv_suicidepenalty`)). Obituaries: SPEC-780 §18 in all deathmatch modes; none in single
  player.
- **Spawn kit** (every deathmatch spawn, after a fresh inventory object and a prediction restart, SPEC-780 Behaviour 4):
  remove everything including the flag; player variable A := −1, or 0 with infinite ammo (0x800); humans: C := −1, or
  0 with no reload (0x40); weapon tag := userinfo `bestweap`; B := 0 with weapons stay (0x4), else −1; kept-weapon mask
  I := knife, plus the 9 mm pistol with realistic damage (0x10); bots: tag `unsafe`; then give knife (1), 9 mm pistol
  (3), hands 13 and 14 (no clip rounds); unless infinite ammo, add 6 knives (ammo type 1) and 150 rounds of 9 mm (ammo
  type 3); select the 9 mm pistol. Used by DM, CTF, Control and Conquer unchanged; Assassin adds the steps of
  Behaviour 6.
- **Death drop**: player variable B := −1 (dropping allowed, whatever weapons stay says), the player module carries
  out its drop (SPEC-003 slot 2), the in-view weapon model is hidden.
- **Dropped pickups**: SPEC-781 §7 (20 s lifetime). **Respawn delay**: SPEC-781 §6.
- **Weight limit**: none (single player and Realistic differ, SPEC-781 §4). **Automatic switch** to a better
  picked-up weapon: allowed (Realistic: never).
- **Movement scale** (SPEC-780 Behaviour 5 step 5): `g_movescale` × (1 − slowdown), slowdown being the player's
  impairment 0..0.5 (SPEC-720).
- **Aim spread add-on** (degrees, SPEC-782 Behaviour 1):

| Mode | Sniper rifle | Other weapons |
|---|---|---|
| single player, DM, Conquer | 4.5 when the player's field of view > 60, else 0 | 0 |
| Assassin, Arsenal, CTF, Control | 4.5 when fov > 60, else 0 | \|velocity\|² × 3.75e‑5, plus 3 when the player's body reports the state that SPEC-782 calls "airborne-like" |
| Realistic | as above | \|velocity\|² × 0.5625 × 3.75e‑5, plus 3 in that body state |

- **Help command**: `help` (no intermission) shows the mode's status layout for 30 s, or hides it when shown; while
  shown it is refreshed every 32 frames. Only Assassin and Arsenal have a status layout; in the other modes nothing
  is displayed (the scoreboard is still closed).
- **Damage predicates** used by the damage model in every mode (SPEC-720 rules 3, 6, 10): a hit zone (SPEC-721
  numbering) is *armour-covered* when it is one of 3–8 (shoulders, chest) or 24–25 (gut extras); a zone is a *limb*
  when it is one of 9–12 (arms) or 16–23 (legs, feet). Bolt-ons named `w_pistol1` or `w_knife` are never knocked off
  a body (SPEC-720 rule 8); every other bolt-on may be.
- **Disconnect**: if the end-game log file was written this level, its footer is written again into the already
  closed file (no observable effect expected; reproduce by writing nothing — UNVERIFIED).
- **Player icons** (all modes): when a client enters the game, the icon index every other client shows above him and
  he shows above every other client is reset to 0 (icon tables, Behaviour 6).

### 4. Deathmatch (1) and team deathmatch
Exactly the defaults. With `dmflags` 0x80 the game type name is `Team DM`, team play rules apply and teams are the
model team names (§4); there is no team number assignment (team numbers stay 0, so the same-team rule compares team
names).

### 5. Realistic (5)
- Flags: §3 table (realistic damage always; the health, item, weapons-stay, no-falling-damage, no-reload,
  no-friendly-fire and infinite-ammo flags are ignored). Game type `Realistic` / `Team Real`.
- Items: SPEC-781 (health filter, medkit replacement, weapons do not respawn, dropped weapons return after 30 s,
  remembered positions). Weight limit 4. No automatic weapon switch on pickup.
- Spawn kit: remove everything; A := −1; humans: B := −1 (dropping allowed), I := knife + 9 mm pistol, C := −1,
  D := 0, tag `bestweap`, G := −1, E := −1, F := −1; bots: tag `unsafe`, I := knife + 9 mm, F := −1; give knife, 9 mm
  pistol, hands 13 and 14; add 6 knives and 72 rounds of 9 mm (always); select the 9 mm pistol; fatigue := 0,
  exhausted := no. (Player variables A–I as SPEC-003; the meanings of D–G there are UNVERIFIED.)
- Movement scale 0.5625 × (1 − slowdown) (`g_movescale` is ignored).
- **Fatigue** (every client end of frame, humans): while not a spectator: if the run button (0x20) is held and the
  horizontal velocity is not zero, fatigue += 2, and at ≥ 255 it becomes 255 and the player is *exhausted*;
  otherwise, when fatigue > 0: fatigue −= 3, then if fatigue < 255 the player is no longer exhausted, and below 1
  fatigue is 0. stats[8] bit 0x200 is set and stats[11] = trunc(fatigue / 255 × 100). Spectators: fatigue 0, not
  exhausted, bit 0x200 cleared, stats[11] = 0. Exhausted players get pm_flags bit 0x40 (no running, SPEC-780),
  others have it cleared. Bots keep the same counter in their bot state (+2 while their "running" state is set,
  else −3).
- Disconnect: the player module first drops every weapon not in the kept mask (SPEC-003 slot 62), then the default.

### 6. Assassin (2)
- Flags: no team play, no friendly-fire rule (§3 table). Configstrings 32 = `1`, 34 = `1`, CS_DMICONS set (§2).
- Each player has a *target* (a player or none) and a *pending target*. *Choosing a target* for player P: when fewer
  than 2 players are in the game (in use, with a client, not spectators) — none; otherwise start at client slot
  (r mod `maxclients`) + 1, r being a C-library random number, and step forward cyclically (after `maxclients`
  comes 1) to the first slot that is in use, has a client, is not a spectator and is not P.
- **Icons**: every viewer V keeps, for every other client X, an icon index: 0 when V is a spectator, 3 when X is V's
  target, 2 when X's target is V, else 1 (a bystander). Icons are recomputed for the pairs touched by every target
  change below (CS_DMICONS gives the picture names).
- **Entering the game**: the player's target and pending target are cleared and icons are updated in both
  directions with every other client; unless he is a spectator he chooses a target, and every other player without
  a target chooses one now and is shown his status.
- **Status layout** (no intermission, not a spectator): the scoreboard and inventory layouts are closed, the help time
  is set to now + 5 s, then LAYOUT_TARGET with the target's name, or LAYOUT_NO_TARGET, followed by
  LAYOUT_NUM_ENEMIES with the number of players whose target is this player; sent reliably. `help` shows it too.
- **Spawn**: a non-spectator who was spectating is treated as entering the game (above); then the default spawn kit
  (§3) and the status layout. A spectator spawn makes every player targeting him choose a new pending target and
  clears his own target and pending target.
- **End of frame**: when the pending target differs from the target, the target becomes the pending target, icons
  are updated (old and new target, both directions) and the status layout is shown.
- **Scoring** (victim V not a spectator; attacker A):
  - A is a client other than V:
    - A's target is V: A +4; A hears `dm/assassin/killtarget.wav` (channel 2);
    - else V's target is A (V was hunting A): A +1 and the local sound `dm/frag.wav` (volume 1, attenuation 3); no
      target changes;
    - else (a bystander): A −1; A hears `dm/assassin/badkill.wav` and gets DM_ASSASSIN/TEXT_LOST_FRAG.
    In the first and the last case V's pending target := A (the victim will hunt his killer) and A's pending
    target := V's target (A takes over V's contract; possibly none).
  - otherwise (suicide, world, character): V −1 (`sv_suicidepenalty` is not used).
  The common frag sound of SPEC-780 Behaviour 7 is not played in Assassin.
- **Disconnect**: every player whose target is the leaving player chooses a new pending target (the leaving player
  can be chosen again while still in use — UNVERIFIED), then the default.
- Bots only attack their target and their hunters in Assassin (bot behaviour; bot spec).

### 7. Arsenal (3)
- Flags: no reload and infinite ammo always; weapons stay, team play, friendly-fire rule, spinning pickups and
  bullet-only always off (§3 table).
- **Weapon set**: 6 distinct weapon ids from 1..12, drawn by repeating "id = trunc(U(1, 13)), capped at 12" until 6
  distinct ids are collected; kept in ascending id order. Drawn at level start and again after every big winner.
- Items: every map weapon and ammunition pickup is removed (SPEC-781 §1); players drop nothing (no death drop, no
  item or weapon drops).
- **Kit** (on entering the game, and on every spawn): *fresh* when entering the game, when a new round starts, and
  on a spawn while the player is flagged as spectator (UNVERIFIED intent: the spawn passes the spectator flag, so an
  ordinary respawn is *not fresh*). Fresh → new inventory object (and prediction restart for humans). Then: remove
  everything; A := 0; humans: B := 0, C := 0; tag `unsafe`; H := 0; fresh → give the 6 weapons of the set and save
  the inventory as an info string (weapons only, SPEC-003 slot 67); not fresh → restore the inventory from that
  saved string (slot 66), i.e. a respawning player gets back the weapons he owned when he died (see Scoring); give
  hand 14; switch to the best weapon. After a spawn the status layout is shown.
- **Status layout** (no intermission, not a spectator): scoreboard/inventory closed, help time now + 5 s,
  LAYOUT_STATUS with the number of weapons owned (SPEC-003 slot 60, the hand pseudo-weapon included — UNVERIFIED);
  sent reliably; also shown by `help`.
- **Scoring** (V not a spectator): first V's current weapons are saved as his info string (weapons only), so that his
  next spawn restores them; then: attacker not a client → V −1 (unless V killed himself); V killed himself →
  score := trunc(score − `sv_suicidepenalty`); other client A → A +1, and unless the means of death is 23 (telefrag),
  36 (grenade) or 17 (C4): A loses his current weapon (SPEC-003 slot 57) and owned = number of weapons A still owns:
  owned = 1 → A +10 more, `dm/arsenal/bigwinner.wav` for everyone (volume 0.6), TEXT_BIG_WINNER with A, and a new
  round is scheduled; owned = 2 and "last weapon" not yet announced this round → announced,
  `dm/arsenal/lastweapon.wav` for everyone, TEXT_LAST_WEAPON with A.
- **New round** (the first frame after a big winner): a new weapon set is drawn, the announcement is reset, and every
  in-use client is re-equipped fresh and, when not a spectator and no intermission, shown the status layout.

### 8. Capture the flag (4)
- Flags: team play always; force join from `dmflags` 0x4000 (§4). Flag items, their touch rules, drops and returns:
  SPEC-781 §9.
- **Bases**: `misc_ctf_base` spawns only in CTF (otherwise it is removed): a trigger with box (−32, −32, −24) –
  (32, 32, −16) around its origin; key `count` = 1 makes it the Blue base, any other value the Red base.
- **Capture**: a client touching a base while carrying a flag whose team differs from the base's team, while cvar
  `ctf_flag_captured` is 0, captures when his own team's base flag (`ctf_flag_blue` for the Blue base,
  `ctf_flag_red` for the Red base) is at its base or does not exist. Then: `dm/ctf/flagbase.wav` on him (normal
  volume, no attenuation), his team's capture count + 1, END_GAME_BONUS_BLUE when the carried flag is Blue's
  (else _RED) with (him, 15) to everyone, score + 15. If `ctf_loops` ≠ 0 and either team's capture count ≥
  `ctf_loops`: cvar `ctf_flag_captured` := 1 and all clients are told to force the console notify display (SPEC-001
  import 21); the level ends on the next end-rule check (Behaviour 2). Otherwise `ctf_loops_count` + 1 and both flags
  are returned to their bases: a dropped flag is returned and removed, a carried flag is taken from its carrier
  (attachment and `player/flaglp.wav` loop removed, inventory item 7 removed) and returned.
- **Kill bonuses** (scoring of a kill; V = victim, A = attacker). First, a flag carried by V is dropped (SPEC-781 §9
  drop) and V's flag item removed. Then:
  - A not a client: V −1 unless V killed himself; V killed himself: score := trunc(score − `sv_suicidepenalty`);
  - friendly-fire kill: A −1, and when V was carrying a flag A loses 5 more;
  - otherwise A +1, then:
    1. V hurt an enemy flag carrier less than 7 s ago (any damage by V to a client carrying a flag records the time on
       V): FRAG_HURT_CARRIER_BLUE when V is on team 2 (else _RED) with (A, V, 2) to everyone, A +2;
    2. V carried A's team's flag: FRAG_CARRIER_BLUE when that flag is team 2's (else _RED) with (A, 3), A +3, and
       nothing more;
    3. else the first entity (lowest entity number) whose carried-flag team equals A's team — normally a carrier of
       A's flag if one exists, otherwise A's base flag — is located; when V is within 400 units of it (distance ≤ 400)
       PROTECT_FLAG_BLUE when A is on team 1 (else _RED) with (A, 1), A +1.
- **Scoreboard**: Behaviour 12. **Level exit**: Behaviour 2. **Disconnect**: team number cleared, a carried flag is
  dropped, then the default.
- Spawn kit: the default (§3); bots additionally get player variable I := knife (kept-weapon mask).

### 9. Control (6)
- Flags: team play always; force join from 0x4000. Control flags (points): SPEC-781 §10; each point counts as owned
  by the team that switched it last.
- **Accrual** (checked every frame from the end rules, Behaviour 2): whenever level time is past the next accrual
  time, the next accrual time grows by 3 s, each team's control score grows by the number of points it owns, and when
  the map has at least one point, a team owning all points additionally gets `control_bonus` (default 1); then
  configstring 8 = team 1's and 9 = team 2's control score (decimal). The level ends with DM_CONTROL/END_GAME when
  `control_limit` (default 300) ≠ 0 and a team's control score ≥ `control_limit`.
- Frags still score with the deathmatch defaults; the frag limit applies to the team sums of frags.
- **Disconnect**: team number cleared, then the default.
- Spawn kit: the default (§3), except that bots are treated like humans (tag from their userinfo `bestweap`, B, C
  and I set as for humans); the same holds in Conquer.

### 10. Conquer (7, "CTB")
- Flags: all from `dmflags` (§3 table), including force join (0x4000) and *numbered teams* (0x8000). Game type
  `CTB` / `Team CTB` (team play). Team numbers are assigned (§4) in every Conquer game; the same-team rule uses them
  only with numbered teams and team play.
- **Zones**: every `trigger_conquer` brush. A non-spectator client touching a zone after the touch delay is *in the
  zone* for that frame; the time he entered is recorded when he was not in a zone the frame before.
- **Scoring**: at each client's end of frame, if he is in a zone and has been continuously for ≥ 3 s since the
  recorded time: score + 3, the local sound `dm/conquer/beep.wav` (volume 1, attenuation 3) and the recorded time
  := now (so +3 every 3 s). The in-zone state is then moved to "previous frame" and cleared. Kills score with the
  deathmatch defaults.
- **Occupied zone sound**: each frame, for every zone with at least one client in it: once per whole second of level
  time (and at the moment it becomes occupied) `player/conquerLP.wav` plays at the zone's centre (volume 1,
  attenuation 1) and the console shows a debug line `%f: %d %d` (level time and the whole second twice). An empty
  zone stops counting as occupied.
- **Bots**: a position counts as inside a zone when it lies within the zone's box.
- **Configstrings**: 33 and 10 as §2, refreshed when they change.
- **Scoreboard**: Behaviour 12. Spawn kit: the default (§3).

### 11. Single player (0)
- Not a deathmatch: no obituaries, no deathmatch flags (all off), no team assignment, no dropped-item lifetime,
  no scoreboards and no game log.
- Weight limit = the skill record's encumbrance value (SPEC-760 B9.2; SPEC-781 §4).
- Movement scale: 1.0 when the skill record's spawning level (`sk_spawning` clamped to 0..4) is 4, else 0.5625
  (no impairment term).
- Status meter: every frame stats[8] bit 0x200 is set and stats[11] = trunc(stealth value × 100) (SPEC-780 Interface 3; the
  stealth value comes from the world alert system, SPEC-761).
- Drops: SPEC-781 §7 single-player rows (no lifetime; weapon drops always print DM_GENERIC/TEXT_DROPPED_WEAPON).
- Spawn: SPEC-780 §21 (mission statistics reset, persistent inventory, level-script start override).
- **`defaultweapons`** (cheat): replaces the player's inventory (new inventory object, prediction restart, remove
  everything, A := −1, tag `bestweap`) and gives: ammunition 9 mm 128, knives 6, 5.56 mm 120, .44 36, slugs 40,
  shells 32, rockets 8, fuel 30, batteries 60 (SPEC-003 ammo types 3, 1, 5, 2, 9, 4, 6, 8, 7); then a kit chosen by
  the first three letters of the map name:

| Map prefix | Weapons (SPEC-003 ids) | Items (type × count) |
|---|---|---|
| `tsr` | 1, 3, 8 | flash packs × 5, C4 × 2 |
| `trn` | 1, 3, 8 | flash packs × 5, C4 × 2 |
| `kos` | 1, 3, 8, 5 | flash packs × 10, C4 × 5 |
| `sib` | 1, 3, 6, 4, 5 | flash packs × 10, C4 × 5 |
| `irq`, fifth letter `a` | 1, 3, 6, 4 | flash packs × 10, C4 × 5 |
| `irq`, fifth letter `b` | 1, 3, 9, 10 | flash packs × 10, C4 × 5, grenades × 5 |
| `irq`, other | none | none |
| `nyc` | 1, 3, 8, 5 | flash packs × 10, C4 × 5, grenades × 5 |
| `sud` | 1, 3, 2, 6 | flash packs × 10, C4 × 5, grenades × 5 |
| `jpn` | 1, 3, 4, 5 | flash packs × 10, C4 × 5, grenades × 5 |
| `ger` | 1, 3, 10, 9 | flash packs × 10, C4 × 5, grenades × 5 |
| anything else | 1, 3, 8 | flash packs × 5, C4 × 2 |
  Every kit then adds hands 13 and 14, tops up all clips, refreshes the in-view model precache, selects the 9 mm
  pistol and adds armour 1000 (capped at 100 by the add-armour rule). Items are added with no carry limit. A player
  without an inventory object only gets the developer message `hey! no inventory!`.

### 12. Scoreboards
Shown on `score`, to dying humans, at intermission, and refreshed every 32 frames while open (SPEC-780 §19).
- **Default** (DM, Assassin, Arsenal, Realistic, and Conquer without numbered teams + team play): SPEC-780 §19.
- **CTF**: players with a team number, not spectators, in two columns: x = 0 for team 1, 180 for team 2; per team
  sorted by score descending, rows y = 0, 32, 64 … (at most 12 per team), each LAYOUT_CTF_CLIENT. One
  DM_CTF/TEAM_SCORE_LAYOUT row per team that has players, at (team × 180 − 180, −64), with the summed score and the
  team's capture count. Spectators follow only while the longer team column has fewer than 12 rows: at most 12,
  x = 0 for the first three and 180 for the rest, y = (rows of the longer column) × 32 + 32 × k (k restarting at the
  fourth spectator).
- **Control**: as CTF with DM_CONTROL/TEAM_SCORE_LAYOUT (last value = the team's control score) and
  LAYOUT_SCOREBAORD_CLIENT rows.
- **Conquer with numbered teams and team play**: as Control with CONQUER_LAYOUT_SCOREBOARD_TEAM team rows and
  LAYOUT_SCOREBAORD_CLIENT rows (players without a team are not handled correctly by the original — UNVERIFIED).
In every variant the team rows are formed by grouping players with the same-team rule (§4); a team row's team is the
team of its first player.

### 13. End-game log file
Written once per level, when the intermission scoreboard is produced for the first client, and only when
`log_file_name` is non-empty:
1. File name = `log_file_name` (at most 63 characters, opened relative to the process's current directory, its
   directories created first). When `log_file_counter` ≠ 0 the counter is inserted before the extension
   (`logs/sof.txt` with counter 3 → `logs/sof3.txt`) and the cvar is increased by 1.
2. Console: `Dumping end game log to <name>`. Mode `w` (truncate) when `log_file_mode` is `o` (any case), else `a`
   (append). Failure: `ERROR: couldn't open game log-file.` and nothing is written.
3. Lines (L = `log_file_line_header`): `log_file_header`; `L` + `Map Name : <map>`; `L` + `Host Name : <hostname>`;
   `L` + `Game type : <game type name>`; `L` + `Game Duration : HH:MM:SS` (level time, truncated seconds); `L`.
4. Default scoreboard data: with team play, per team `L` + `Team <name>`, `L` + `Score <n>`, then an empty line; per
   player in score order `L` + `Client <name>`, `L` + `Score <n>`, `L` + `Ping <ms>`, `L` + `Time <minutes>`, `L`;
   per spectator `L` + `Client <name>`, `L` + ` spectator`, `L` + `Ping <ms>`, `L` + `Time <minutes>`, `L`.
   CTF: per team `L` + `Team <name>`, `L` + `Score <n>`, `L` + `Flag Captures <n>` and an empty line, then per player
   Client/Score/Ping/Time. Control and Conquer: per team `Team`, `Frags <n>`, `Control Score <n>` + empty line, per
   player Client/`Frags`/Ping/Time. Spectators as above. (The team name text written is UNVERIFIED.)
5. `log_file_footer`, then the file is closed.

## Constants

| Name | Value |
|---|---|
| Modes | 8 (`deathmatch` 0..7) |
| Dropped item / weapon lifetime (deathmatch) | 20 s |
| Frames per minute (time limit, scoreboard minutes) | 600 |
| Default kit ammunition | 6 knives, 150 rounds 9 mm (none with infinite ammo) |
| Realistic kit ammunition | 6 knives, 72 rounds 9 mm |
| Realistic movement scale | 0.5625 × (1 − slowdown) |
| Single-player movement scale | 1.0 at spawning level 4, else 0.5625 |
| Spread add-on | velocity² × 3.75e‑5 (Realistic × 0.5625), +3 body state, sniper 4.5 above fov 60 |
| Fatigue | +2 per frame running, −3 per frame otherwise, maximum 255 (exhausted), meter = fatigue / 255 × 100 |
| Assassin points | target +4, hunter +1, bystander −1, death without a client killer −1 |
| Arsenal | 6 weapons of ids 1..12; +1 per kill, +10 big winner; telefrag (23), grenade (36), C4 (17) kills keep the weapon |
| CTF | capture +15; carrier-defender frag +2 (within 7 s); carrier frag +3; flag protection +1 (≤ 400 units); team-mate kill −1, −5 more when carrying a flag; base box (−32, −32, −24)–(32, 32, −16) |
| Control | accrual every 3 s: + points owned; + `control_bonus` (default 1) for owning all; limit `control_limit` (default 300) |
| Conquer | +3 per 3 s continuously inside a zone; zone loop sound once per second |
| Status layouts | shown for 5 s; `help` shows 30 s; refresh every 32 frames |
| Team scoreboards | columns x = 0 / 180; row step 32; team rows at y −64; at most 12 rows per column |
| Log file name | at most 63 characters |

## Test vectors / how to verify
1. `deathmatch 2`, 3 players: configstring 32 is `1`; each player sees "Target objective: >> name <<"; killing the
   target gives +4 and `dm/assassin/killtarget.wav`; killing a bystander gives −1 and "You lost a frag..."; the
   victim's next target is his killer.
2. `deathmatch 3`: each spawn shows "You have N weapons remaining 'til big winner bonus." (verify whether N counts
   the hand pseudo-weapon); after shedding all six weapons by kills (not grenade/C4/telefrag) the killer gets
   "Woo hoo!!! … big winner!!!", +10, and all players are re-armed with a new set on the next frame.
3. `deathmatch 4`, `ctf_loops 1`: carrying the enemy flag onto the own `misc_ctf_base` while the own flag is home:
   +15 and END_GAME_BONUS text, then "Flag captured and returned to base - Game over." and intermission. With the own
   flag taken, touching the base does nothing.
4. `deathmatch 6`, `control_limit 10`, one point switched to Blue: configstring 8 counts 2, 4, … every 3 s (one point
   owned plus the bonus 1 for owning all points); at 10 "Score Limit Reached  - Game over.".
5. `deathmatch 7`: stand 9 s in a `trigger_conquer`: +3 three times, three `dm/conquer/beep.wav`; the console shows a
   `%f: %d %d` line once per second while occupied.
6. `deathmatch 5`: hold run while moving: the meter (stats[11]) reaches 100 after 128 frames and running stops
   (pm_flags 0x40); release: the meter falls 3 per frame.
7. `timelimit 1`: the level ends one minute after the earliest-entered client entered, not after the map started.
8. `sv_maplist "dm/a dm/b"`: after `dm/b` the next map is `dm/a`; on a map not in the list the list is ignored.
9. `log_file_name logs/x.log`, `log_file_counter 1`: at the end of a deathmatch level the console shows
   `Dumping end game log to logs/x1.log`, the file holds the header lines of Behaviour 13, the counter becomes 2.
10. Single player on a map starting with `sib`, cheats on, `defaultweapons`: knife, 9 mm pistol, sniper rifle,
    9 mm SMG and 5.56 SMG, 10 flash packs, 5 C4, armour 100, 9 mm pistol selected.
11. `deathmatch 1`, `dmflags 128`: `status` shows `Gametype: Team DM`; `say_team` works; `deathmatch 1`,
    `dmflags 0`: `Gametype: DM`.
12. `deathmatch 4` with `dmflags 16384` (force join): changing `team_red_blue` after joining prints "Cannot Change
    Teams when in a Force Join game."; new players fill the smaller team.

## Open questions
- Client-side meaning of configstrings 10, 32, 33, 34 (HUD icons / team display) — check with the original client.
- Arsenal: whether a respawning player is meant to get his saved weapons back (observed rule: yes) and whether the
  status count includes the hand pseudo-weapon.
- Conquer numbered teams (0x8000): intended player-facing meaning; the team scoreboard with players of team 0.
- CTF/Control: two players without a team number count as team mates (same-team rule) — verify with a fresh
  joiner before his team is assigned.
- The touch delay in modes that do not set it; the team name text in the log file.

## Corrections to other specs (for the gatekeeper)
- SPEC-720 §2 and its constants: team play is `dmflags` 0x80 (not 0x200), "no friendly fire" 0x100 (not 0x400),
  realistic damage 0x10 (not 0x40); team play is always on in CTF and Control. Its open question on the
  realistic-damage zone predicates is answered by Behaviour 3 (armour-covered zones 3–8, 24, 25; limb zones 9–12,
  16–23).
- SPEC-780 Behaviour 1 (connecting) and Behaviour 9: force join is `dmflags` 0x4000 and applies to CTF, Control and Conquer alike;
  team spawn points are used by every team game (§4), not only Conquer; "Conquer with forced joining" in the
  model-team-change rule means Conquer with numbered teams (0x8000).
- SPEC-782 Behaviour 1: the velocity/body-state spread term applies only in Assassin, Arsenal, CTF, Control and
  Realistic; plain deathmatch and Conquer add only the sniper term (Behaviour 3).
- SPEC-203: configstrings 10, 32, 33, 34 and 1447..1450 as §2 of this spec.
