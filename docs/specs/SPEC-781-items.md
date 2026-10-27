# SPEC-781: Items — map pickups, pickup rules, respawn, dropped items, flag items
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f (item system; uses player.dll through SPEC-003)
Depends on: SPEC-001 (game interface: string-package prints, sounds, configstrings, savegame chunks), SPEC-003 (player
module: inventory methods, weapon ids, ammo types, item types, drop callbacks), SPEC-006 (classname list, cvars, dmflags
cvar), SPEC-131 (string packages), SPEC-202 (effects bits, entity events, sound data byte), SPEC-303 (savegames).
Related: SPEC-780 (player/client: spawn protection, armor appearance), SPEC-782 (equipment use), game-mode spec (WP10:
scoring, CTF captures, control points).

## Purpose

Defines everything the game module does with the 39 `item_*` classnames: what each pickup grants, when a pickup is
refused, how the game mode and `dmflags` filter items at level start, respawn timing and the respawn sequence, items
dropped by players, the CTF and Control flag items, the debugging pickup statistics, and the item savegame chunks.
Weapon, ammo, armor and item *storage* (limits per type, clips) belong to the player module (SPEC-003); this spec says
which inventory operation each pickup performs.

## Interface / layout

### Item kinds
| Kind | Meaning | Inventory operation used on pickup (SPEC-003 slot) |
|---|---|---|
| 0 | ammunition | add ammunition of an ammo type (slot 12) |
| 1 | weapon | owns weapon? (22); give weapon (16); add ammo for weapon (14 or 15); weights (17, 18) |
| 2 | armor | add armor, clamped 0..100 (20) |
| 3 | equipment / flag | add items (type, count, carry limit) (19) |
| 4 | not used by any item | — |
| 5 | health | none (changes the entity's health directly) |

### Item table (index, classname, kind, what it grants)
"Default count" is used when the map entity has no `count` key (or `count` 0). "Respawn" is the base respawn time in
seconds (deathmatch-family modes, see Behaviour 5). "Std" = 1 when the item survives `dmflags` bit 0x2000 (see
Constants). Weapon ids, ammo types and item types are those of SPEC-003.

| # | Classname | Kind | Grants | Default count | Respawn | Std |
|---|---|---|---|---|---|---|
| 1 | `item_ammo_knife` | 0 | ammo type 1 (aknife) — always exactly 1 | 1 | 30 | 1 |
| 2 | `item_ammo_pistol` | 0 | ammo type 3 (9 mm) | 30 | 30 | 1 |
| 3 | `item_ammo_shotgun` | 0 | ammo type 4 (shells) | 5 | 30 | 1 |
| 4 | `item_ammo_pistol2` | 0 | ammo type 2 (.44) | 5 | 30 | 1 |
| 5 | `item_ammo_auto` | 0 | ammo type 5 (5.56) | 30 | 30 | 1 |
| 6 | `item_ammo_battery` | 0 | ammo type 7 (battery) | 30 | 30 | 0 |
| 7 | `item_ammo_gas` | 0 | ammo type 8 (fuel) | 30 | 30 | 0 |
| 8 | `item_ammo_rocket` | 0 | ammo type 6 (rockets) | 5 | 30 | 0 |
| 9 | `item_ammo_slug` | 0 | ammo type 9 (slugs) | 10 | 30 | 0 |
| 10 | `item_ammo_sp_pistol` | 0 | as #2 | 30 | 30 | 1 |
| 11 | `item_ammo_sp_shotgun` | 0 | as #3 | 5 | 30 | 1 |
| 12 | `item_ammo_sp_pistol2` | 0 | as #4 | 5 | 30 | 1 |
| 13 | `item_ammo_sp_auto` | 0 | as #5 | 30 | 30 | 1 |
| 14 | `item_ammo_sp_battery` | 0 | as #6 | 30 | 30 | 0 |
| 15 | `item_ammo_sp_gas` | 0 | as #7 | 30 | 30 | 0 |
| 16 | `item_ammo_sp_rocket` | 0 | as #8 | 5 | 30 | 0 |
| 17 | `item_ammo_sp_slug` | 0 | as #9 | 10 | 30 | 0 |
| 18 | `item_weapon_pistol2` | 1 | weapon 2 (.44 pistol) | — | 30 | 1 |
| 19 | `item_weapon_pistol1` | 1 | weapon 3 (9 mm pistol) | — | 30 | 1 |
| 20 | `item_weapon_machinepistol` | 1 | weapon 4 (9 mm suppressed SMG) | — | 30 | 1 |
| 21 | `item_weapon_assault_rifle` | 1 | weapon 5 (5.56 SMG) | — | 30 | 1 |
| 22 | `item_weapon_sniper_rifle` | 1 | weapon 6 (sniper rifle) | — | 30 | 1 |
| 23 | `item_weapon_autoshotgun` | 1 | weapon 7 (slug thrower) | — | 60 | 0 |
| 24 | `item_weapon_shotgun` | 1 | weapon 8 (shotgun) | — | 30 | 1 |
| 25 | `item_weapon_machinegun` | 1 | weapon 9 (heavy machinegun) | — | 30 | 1 |
| 26 | `item_weapon_rocketlauncher` | 1 | weapon 10 (rocket launcher) | — | 75 | 0 |
| 27 | `item_weapon_microwavepulse` | 1 | weapon 11 (microwave pulse gun) | — | 90 | 0 |
| 28 | `item_weapon_flamethrower` | 1 | weapon 12 (flame gun) | — | 60 | 0 |
| 29 | `item_equip_armor` | 2 | armor points | 50 (a map `count` above 100 becomes 100) | 30 | — |
| 30 | `item_equip_flashpack` | 3 | item type 1 (flash pack) | 1 | 30 | — |
| 31 | `item_equip_c4` | 3 | item type 2 (C4) | 1 | 30 | — |
| 32 | `item_equip_light_goggles` | 3 | item type 3 (goggles) | 100 | 30 | — |
| 33 | `item_equip_claymore` | 3 | item type 4 (claymore) | 1 | 30 | — |
| 34 | `item_equip_medkit` | 3 | item type 5 (medkit) | 1 | 30 | — |
| 35 | `item_equip_grenade` | 3 | item type 6 (grenade) | 1 | 30 | — |
| 36 | `item_ctf_flag` | 3 | item type 7 (CTF flag) — see Behaviour 9 | 1 | — | — |
| 37 | `item_health_small` | 5 | health | 25 | 30 | — |
| 38 | `item_health_large` | 5 | health | 50 | 30 | — |
| 39 | `item_control_flag` | 3 | no inventory change — see Behaviour 10 | — | — | — |

Display names are those listed in SPEC-006 (they are only used by debugging output). A map classname that is not in the
table but begins with the three letters `ctf` (for example `ctf_flag_red`) also spawns item #36.

### Carry limits for equipment (argument "max" of the add-items operation)
| # | Item | Single player | Deathmatch-family modes |
|---|---|---|---|
| 30 | flash pack | 6 | unlimited |
| 31 | C4 | 4 | unlimited |
| 32 | goggles | 100 | 100 |
| 33 | claymore | unlimited | unlimited |
| 34 | medkit | 2 | 2; 1 when `dmflags` bit 0x10 is set; always 1 in Realistic |
| 35 | grenade | 6 | unlimited |
| 36 | CTF flag | unlimited | unlimited |

### Messages (string package ITEMS; text is the English text of the shipped package)
Each item has a pickup message, a secondary message (weapons: the matching ammo pickup message) and a refusal message.
| # | Pickup | Secondary | Refusal |
|---|---|---|---|
| 1 | KNIFE_PICKUP | KNIFE_PICKUP | KNIFE_FULL |
| 2, 10 | 9MM_AMMO_PICKUP | same | 9MM_AMMO_FULL |
| 3, 11 | 12_GAUGE_AMMO_PICKUP | same | 12_GAUGE_AMMO_FULL |
| 4, 12 | 44_AMMO_PICKUP | same | 44_AMMO_FULL |
| 5, 13 | 556_AMMO_PICKUP | same | 556_AMMO_FULL |
| 6, 14 | BATTERY_PICKUP | same | BATTERY_FULL |
| 7, 15 | GAS_PICKUP | same | GAS_FULL |
| 8, 16 | ROCKET_AMMO_PICKUP | same | ROCKET_AMMO_FULL |
| 9, 17 | SLUGS_PICKUP | same | SLUGS_FULL |
| 18 | 44_PISTOL_PICKUP | 44_AMMO_PICKUP | 44_AMMO_FULL |
| 19 | 9MM_PISTOL_PICKUP | 9MM_AMMO_PICKUP | 9MM_AMMO_FULL |
| 20 | 9MM_SMG_PICKUP | 9MM_AMMO_PICKUP | 9MM_AMMO_FULL |
| 21 | 556_SMG_PICKUP | 556_AMMO_PICKUP | 556_AMMO_FULL |
| 22 | 556_SNIPER_PICKUP | 556_AMMO_PICKUP | 556_AMMO_FULL |
| 23 | SLUGTHROWER_PICKUP | SLUGS_PICKUP | SLUGS_FULL |
| 24 | 12_GAUGE_SHOTGUN_PICKUP | 12_GAUGE_AMMO_PICKUP | 12_GAUGE_AMMO_FULL |
| 25 | HEAVY_MACHINEGUN_PICKUP | 556_AMMO_PICKUP | 556_AMMO_FULL |
| 26 | ROCKET_LAUNCHER_PICKUP | ROCKET_AMMO_PICKUP | ROCKET_AMMO_FULL |
| 27 | MICROWAVE_PULSE_PICKUP | BATTERY_PICKUP | BATTERY_FULL |
| 28 | FLAME_GUN_PICKUP | GAS_PICKUP | GAS_FULL |
| 29 | ARMOR_PICKUP | ARMOR_PICKUP | ARMOR_ALREADY_FULL |
| 30 | FLASHPACK_PICKUP | same | FLASHPACK_FULL |
| 31 | C4_PICKUP | same | C4_FULL |
| 32 | LIGHTGOGGLES_PICKUP | same | LIGHTGOGGLES_FULL |
| 33 | CLAYMORE_PICKUP | same | CLAYMORE_FULL |
| 34 | MEDKIT_PICKUP | same | MEDKIT_FULL |
| 35 | GRENADE_PICKUP | same | GRENADE_FULL |
| 36 | CTF_FLAG_PICKUP | CTF_FLAG_PICKUP_2 | CTF_FLAG_PICKUP |
| 37, 38 | HEALTH_PICKUP | same | same (never shown) |
| 39 | none | none | none |
Other ITEMS entries used: ENCUMBERED ("You can't carry any more weapons. You need to drop some of your existing weapons
first."), ALREADY_HAVE_THIS ("You already have this weapon."), and the dropped-weapon names DROPPED_44PISTOL,
DROPPED_9MMPISTOL, DROPPED_9MMSUPSMG, DROPPED_556SMG, DROPPED_SNIPER, DROPPED_SLUGTHROWER, DROPPED_SHOTGUN,
DROPPED_HVYMACH, DROPPED_ROCKETLAUNCHER, DROPPED_MPG, DROPPED_FLAMEGUN (weapons 2..12 in that order of items #18..#28),
inserted into DM_GENERIC TEXT_DROPPED_WEAPON ("Dropped a %s").

### Appearance (Ghoul model `Ghoul/<directory>/<model>`, skin, scale, bottom of the bounding box)
| # | Directory | Model | Skin | Scale | Box bottom | Fade-in sequence |
|---|---|---|---|---|---|---|
| 1 | enemy/bolt | w_knife | — | 1.0 | 0 | none |
| 2 | Items/Pick-ups/Ammo | box | pistolbox | 1.75 | −6 | fade_box |
| 3 | Items/Pick-ups/Ammo | box | shotgunbox | 1.75 | −6 | fade_box |
| 4 | Items/Pick-ups/Ammo | box | pistol2box | 1.75 | −6 | fade_box |
| 5 | Items/Pick-ups/Ammo | box | autobox | 1.75 | −6 | fade_box |
| 6 | Items/Pick-ups/Ammo | box | batterybox | 1.75 | −6 | fade_box |
| 7 | Items/Pick-ups/Ammo | box | gasbox | 1.75 | −6 | fade_box |
| 8 | Items/Pick-ups/Ammo | box | rocketbox | 1.75 | −6 | fade_box |
| 9 | Items/Pick-ups/Ammo | box | slugbox | 1.75 | −6 | fade_box |
| 10 | Items/Pick-ups/Ammo_single | ammo_9mm | ammo_9mm_single | 1.75 | 0 | none |
| 11 | Items/Pick-ups/Ammo_single | ammo_shotgun | ammo_00_single | 1.75 | 1 | none |
| 12 | Items/Pick-ups/Ammo_single | ammo_44 | ammo_44_single | 2.0 | 0 | none |
| 13 | Items/Pick-ups/Ammo_single | ammo_556 | ammo_556_single | 2.0 | 0 | fade_ammo_556 |
| 14 | Items/Pick-ups/Ammo_single | ammo_battery | ammo_bat | 2.0 | 0 | none |
| 15 | Items/Pick-ups/Ammo_single | ammo_gas | ammo_gas_single | 2.0 | 0 | none |
| 16 | Items/Pick-ups/Ammo_single | ammo_rocket | ammo_rocket_single | 2.0 | −0.5 | fade_ammo_rocket |
| 17 | Items/Pick-ups/Ammo_single | ammo_shotgun | ammo_slug_single | 2.0 | 1 | none |
| 18 | enemy/bolt | w_pistol2 | — | 1.75 | −1.5 | fade_pistol2 |
| 19 | enemy/bolt | w_pistol1 | — | 1.75 | −1.5 | fade_pistol1 |
| 20 | enemy/bolt | w_machinepistol | — | 1.75 | −3 | fade_machinepistol |
| 21 | enemy/bolt | w_assault_rifle | — | 1.75 | −2 | fade_assault_rifle |
| 22 | enemy/bolt | w_sniperrifle | — | 1.75 | −2 | fade_sniperrifle |
| 23 | enemy/bolt | w_autoshotgun | — | 1.75 | −8.5 | fade_autoshotgun |
| 24 | enemy/bolt | w_shotgun | — | 1.75 | −3 | fade_shotgun |
| 25 | enemy/bolt | w_machinegun | — | 1.75 | −3 | fade_machinegun |
| 26 | enemy/bolt | w_rocket | — | 1.75 | −10.5 | fade_rocket |
| 27 | enemy/bolt | w_mpg | — | 1.75 | −8 | fade_mpg |
| 28 | enemy/bolt | w_flamethrower | — | 1.75 | −2.5 | fade_flamethrower |
| 29 | Items/Pick-ups/Armor | armor (deathmatch-family: sequence `armordm` is played on it) | — | 1.25 | 0 | armorfade |
| 30 | Items/Projectiles | flashpak | — | 1.75 | −9 | fade_flashpak |
| 31 | Items/Projectiles | c4 (sequence `c4` is played on it) | — | 1.75 | −5 | fade_c4 |
| 32 | enemy/bolt | acc_nightvision | — | 2.0 | −4.5 | fade_nightvision |
| 33 | Items/Projectiles | claymore | — | 1.75 | −5 | fade_claymore |
| 34 | Items/Pick-ups/MedKit | medkit | — | 1.75 | 0.5 | fade_medkit |
| 35 | Items/Projectiles | grenade | — | 1.75 | 0 | fade_grenade |
| 36 | Items/ctf_flag | flag_ground_idle | ctf_flag_blue / ctf_flag_red | 1.75 | 0 | none |
| 37 | Items/Pick-ups/healthsm | health | — | 1.75 | 0 | fade_health |
| 38 | Items/Pick-ups/healthbig | health | — | 1.75 | 0 | fade_health |
| 39 | Items/ctf_flag | flag_ground_idle | ctf_flag_grey (then team colour) | 1.75 | 0 | none |

Bounding box: mins = (−8·s, −8·s, b), maxs = (8·s, 8·s, 8·s), where s = scale and b = "box bottom" (b replaces the
scaled bottom). Flags (#36, #39): mins = (−20·s, −20·s, 0), maxs = (20·s, 20·s, 30·s) = (−35, −35, 0)…(35, 35, 52.5).
An item whose model cannot be set keeps (−8, −8, −8)…(8, 8, 8). Every model-less item prints
`No model specified for %s, can't spawn` (developer print) but still spawns.

### Map entity keys and spawnflags read by items
| Key | Meaning |
|---|---|
| `classname` | item table lookup (exact, case-sensitive; `ctf…` prefix → #36) |
| `origin`, `angles` | position and orientation |
| `count` | > 0: amount granted (ammo, armor, health, equipment count) or, for #36, the team (1 = Blue, anything else = Red); 0 or absent: default count; < 0: a random integer in [0, −count] chosen at spawn (inclusive; can be 0) |
| `targetname` | items with spawnflag 8 are revealed when a `func_breakable_brush` whose `target` equals this name is destroyed |
| `message` | #39 only: the control point's name |

| Spawnflag | Meaning |
|---|---|
| 2 | the item does not use falling physics (stays where placed). UNVERIFIED (shared model-entity setup; verify by placing an item in mid-air with and without the flag) |
| 8 | hidden and untouchable at level start until revealed (see `targetname`) |
| 128 | ammunition (kind 0) respawns even in single player; the respawn effect and sound are precached |
| 0x10000 | set by the game on items dropped by players (mappers must not use it) |
| 0x100000 | set by the game on a weapon whose ammunition was already taken while the player was over the weight limit |
The game always sets spawnflag 1 on items when they spawn. Every item entity is a trigger (touchable, not blocking).

### Configuration strings written by flag items
| Index | Value |
|---|---|
| 8 | Blue CTF flag status: `h` at base, `d` dropped, `r` followed by one byte = entity number of the carrier; Control mode sets `0` |
| 9 | Red CTF flag status, same encoding |
| 3545 + n | Control point n (n = 0..4, 0-based in spawn order): `G<name>` when spawned (neutral), `B<name>` / `R<name>` when owned by Blue / Red; on a capture of a point without `message`, `B0` / `R0` |

### Savegame chunks (written with the level state, SPEC-303)
| Chunk id | Size | Contents |
|---|---|---|
| `PLNM` (bytes `MNLP` in file order) | 4 | int: number N of registered item entities |
| `PLPI` (bytes `IPLP` in file order) | 16 each, N times | 4 bytes without meaning (ignored on load); int item index 1..39; int entity number of the item; int fade state (−1 new, 0 normal, 1 fading in, 2 fade-in just finished) |
| `PLWA` (bytes `AWLP` in file order) | 1 | one byte of item-system state; its meaning is UNVERIFIED (never changed in 1.07f play) |

## Behaviour

### 1. Spawning from the map
1. The classname is looked up in the item table (SPEC-006 order: before the other classname tables).
2. The current game mode then filters the item (removed silently unless stated). Game mode = cvar `deathmatch`:
   0 single player, 1 deathmatch, 2 Assassin, 3 Arsenal, 4 CTF, 5 Realistic, 6 Control, 7 Conquer.

| Rule | SP | DM, Assassin, Conquer | Arsenal | CTF | Realistic | Control |
|---|---|---|---|---|---|---|
| remove #36 CTF flag | no | yes | yes | no | yes | no (removed at spawn, step 4) |
| `dmflags` 0x1: remove health (kind 5) | no | yes | yes | yes | no | yes |
| `dmflags` 0x2: remove equipment (kind 3) | no | yes | yes | yes except #36 | no | yes except #39 |
| `dmflags` 0x400: remove armor | no | yes | yes | yes | yes | yes |
| `dmflags` 0x800: remove ammunition | no | yes | always | yes | no | yes |
| remove weapons | no | no | always | no | no | no |
| `dmflags` 0x2000: remove weapons and ammo with Std = 0 | no | yes | — | yes | yes | yes |
| health | kept | kept | kept | kept | #37 removed; #38 becomes a medkit (#34) | kept |

3. Realistic additionally remembers the position of every map item (used by Behaviour 7).
4. `item_ctf_flag` spawns only when `deathmatch` = 4 and `item_control_flag` only when `deathmatch` = 6; otherwise
   the developer message `No flags in non control or CTF play` is printed and the entity is removed. A sixth control
   flag prints `Max Control Flag count of 5 reached. Dumping flag.` and is removed.
5. The item is set up: classname replaced by the table classname (flags: see Behaviour 9/10), trigger solid, model,
   skin, scale, bounding box (tables above); `count` resolved (see keys; armor capped at 100); weapons mark their
   weapon for in-view model precaching. In the deathmatch-family modes, when `dmflags` bit 0x1000 is set and the mode
   is not Arsenal, map weapons and armor float: no gravity, raised 6 units, auto-rotating (effects bit 0x2000), and
   weapons are rolled 90° (the heavy machinegun −90°).
6. Items with spawnflag 8 are hidden and untouchable once all entities have spawned.

Other code also creates pickups: thrown or embedded knives become `item_ammo_knife` pickups that disappear after 30 s
(10 s in Arsenal, and in DM, Assassin, CTF, Control and Conquer when `dmflags` bit 0x800 is set); some characters drop
`item_equip_armor` pickups (their box bottom is 10 units lower).

### 2. Touching an item — common rules
- Only client entities can pick items up, and only while their health is above 0 (no health test for #39).
- Every touch that passes these tests ends the toucher's post-spawn protection immediately (SPEC-780).
- A refused touch shows the refusal message at most once every 5 s per item (5-s per-item debounce shared by all
  touchers). Messages go only to the toucher.
- Successful pickups play `weapons/gpistol/cock.wav` on the toucher (item channel, volume 1, attenuation 2) unless
  stated otherwise, then print the pickup message.
- A dropped item cannot be picked up by the player who dropped it during the first 2 s.
- When a bot picks up ammunition or a weapon its AI is notified (bot spec).

### 3. Ammunition (kind 0)
- Amount = trunc(count × A), where A is the ammo multiplier for the current `sk_spawning` value (Constants). Item #1
  always gives exactly 1.
- The amount is added to the ammo type, capped at the type's maximum (SPEC-003). The pickup succeeds when at least
  one round was added; the whole item is used up even if only part of the amount fit.
- Refused (type already full): refusal message.
- After a successful pickup: removed in single player and for dropped items, unless spawnflag 128 is set; otherwise it
  respawns (Behaviour 6).

### 4. Weapons (kind 1)
Let w = the weapon, R = rounds carried (dropped weapon: the rounds given by the player module when it was dropped;
map weapon: "default" = −1 passed to the give-weapon operation, whose result is defined by SPEC-003).
- **Weight limit.** The player is *encumbered* when a limit L > 0 exists and weight(w) + total weight carried > L.
  L = single player: by `sk_maxencum` (Constants); Realistic: 4; all other modes: no limit. Weapon weights are the
  player module's (Constants, for reference).
- **Not owned, not encumbered:** w is given with R. If the player's userinfo `bestweap` is non-empty, the mode allows
  automatic switching (all modes except Realistic) and w is now the player's best weapon, a change to the best weapon
  is requested. Sound and pickup message. If "weapons stay" is on (`dmflags` bit 0x4 in DM, Assassin, CTF, Control,
  Conquer) and the item is a map weapon, the item stays. Otherwise: single player → removed; deathmatch-family → map
  weapon respawns (Realistic: removed, no respawn), dropped weapon removed.
- **Not owned, encumbered** (at most every 5 s): in single player only, and only once per item, the weapon's
  ammunition is taken: add ammo for w with R for dropped weapons or the default pickup amount for map weapons; if
  that succeeds and the item still held rounds, the sound and the secondary message play and the item is marked
  (spawnflag 0x100000) so it gives no more ammunition. Then the ENCUMBERED message is shown. The item stays.
- **Owned, weapons stay in effect for this map weapon:** nothing happens.
- **Owned, Realistic:** ALREADY_HAVE_THIS (at most every 5 s); the item stays.
- **Owned, otherwise:** add ammo for w (dropped: R, where 0 means the default amount; map: the default random pickup
  amount of SPEC-003). Success: sound, secondary message, then removed (single player, or dropped) or respawn (map
  weapon in deathmatch-family modes except Realistic, where it is removed). Failure: refusal message.

### 5. Armor, equipment and health
- **Armor (kind 2):** amount = trunc(count × B), B = armor multiplier for `sk_toughness` (Constants); added through the
  add-armor operation (0..100). Success when the armor value changed: sound `dm/armorpu.wav`, ARMOR_PICKUP. Refused
  (armor already 100): ARMOR_ALREADY_FULL. In deathmatch-family modes the player's appearance is refreshed when armor
  changes between zero and non-zero (SPEC-780).
- **Equipment (kind 3, #30–#35):** count (dropped: the count carried) of the item type is added with the carry limit
  of the table. Success: sound and pickup message; refused: refusal message.
- **Health (kind 5):** only when health < maximum health: health ← min(health + count, maximum). Sound
  `dm/healthpu.wav` (played twice), HEALTH_PICKUP; the player's visible wounds are updated (body/gore spec). At full
  health nothing happens and nothing is printed.
- After success, items are removed in single player and when dropped; map items respawn in deathmatch-family modes
  (in Realistic ammunition, armor and equipment respawn; weapons do not, see Behaviour 7 for their return).

### 6. Respawn (deathmatch-family modes; single-player ammunition with spawnflag 128)
1. On pickup the item becomes invisible and untouchable.
2. Delay = T × √(2 / max(2, N)) in deathmatch-family modes, where T = the item's respawn time and N = the number of
   player slots in use (connected players and bots) when it was picked up. In single player the delay is T.
   Examples: N ≤ 2 → T; N = 8 → T/2; N = 32 → T/4.
3. After the delay the item reappears: effect `environ/irespawn` at its origin, sound `dm/irespawn.wav` (item channel,
   volume 0.6, attenuation 1) and its fade-in sequence starts. It stays untouchable until the fade-in sequence ends;
   then its normal sequence plays and it becomes touchable.
4. 0.8 s after reappearing, entity event 22 is raised on it and its effect slots and colour tint are reset.
Items that have no fade-in sequence (table "none") never get step 3's "touchable" transition — UNVERIFIED whether they
stay untouchable after their first respawn; verify with `item_ammo_knife` or `item_ammo_sp_pistol` in a deathmatch map.

### 7. Items dropped by players
The player module asks the game to drop a weapon (weapon id, rounds) or an item (item type, count) (SPEC-003 server
callbacks 5 and 7). The pickup used is the first table item of kind 1 with that weapon id, or of kind 3 with that item
type. Common: marked dropped (spawnflag 0x10000), owner = dropper (2-s re-pickup block), velocity = 250 × the
dropper's view direction flattened to the horizontal plane, plus 150 upward; the rounds/count are stored on the item.

| Mode | Weapon start position | Item start position | Lifetime | Message |
|---|---|---|---|---|
| single player | dropper origin moved 20 units forward, stopped at walls (traced) | dropper origin + 20 forward | unlimited | "Dropped a %s" always |
| DM, Assassin, Control, Conquer, CTF | right-hand bolt `wbolt_hand_r` + 20 forward (traced) | left-hand bolt `wbolt_hand_l` + 20 forward | 20 s, then removed | weapon: "Dropped a %s" if the dropper is alive |
| Realistic | as DM | as DM | items 20 s; weapons return after 30 s (below) | as DM |
| Arsenal | nothing is dropped | nothing is dropped | — | — |
In the deathmatch-family modes nothing is dropped when the dropper has no model instance. A dropped weapon in those
modes also gets a physics parameter value of 123 (UNVERIFIED meaning). In CTF a dropped item (not the flag) starts at
the left-hand bolt, or 40 units above the dropper's origin when it has no model.

Realistic weapon return: 30 s after a weapon is dropped it moves to a free remembered map position of the same item
(free = no entity overlaps the item's box there; candidates are checked within 128 units). It vanishes with
`environ/irespawn` at the old place and reappears 0.1 s later at the new place exactly like a respawn (Behaviour 6).
When no position is free it tries again every 10 s.

### 8. Hidden items
Items with spawnflag 8 are hidden and untouchable from level start. When a `func_breakable_brush` with a `target` is
destroyed, every item whose `targetname` equals that target and that has spawnflag 8 becomes visible and touchable.

### 9. CTF flags (#36, deathmatch 4)
- A flag item with `count` 1 is the Blue base flag (classname becomes `ctf_flag_blue`, skin `ctf_flag_blue`,
  configstring 8 = `h`); otherwise it is the Red base flag (`ctf_flag_red`, configstring 9 = `h`). Flags play the
  sequence `quake_ground`. Of the model parts `world_light`, `world_light_red`, `world_light_blue`, a team flag
  switches off `world_light` and the other team's light, leaving its own colour; a neutral control flag switches off
  both team lights. (UNVERIFIED that the call switches parts off; verify visually.)
- Touches count only after `ctf_control_delay_time` seconds (default 20) from level start.
- **Enemy flag** (the toucher's team number AND the flag's team number = 0): the toucher receives item type 7 (one
  flag), carries the flag's team, shows the flag bolt-on (sequence `flag_hold_idle`) and, if it has no other looping
  sound, loops `player/flaglp.wav` at full volume. The flag entity vanishes (a base flag stays at base, invisible and
  untouchable; a dropped flag is removed). Configstring 8/9 = `r` + carrier entity number byte. Everyone gets
  DM_CTF CAPTURE_FLAG_BLUE / CAPTURE_FLAG_RED. Every player hears `dm/ctf/triumphant.wav` if on the carrier's team,
  otherwise `dm/ctf/flaggrab.wav` (local sound, channel 2, volume 1, attenuation 1).
- **Own dropped flag:** returned to base (base flag visible and touchable again, configstring `h`); the toucher's score
  increases by 2; everyone gets RECOVER_FLAG_BLUE / RECOVER_FLAG_RED (bonus 2); `dm/ctf/flagret.wav` (no attenuation).
- **Own flag at base:** the toucher is remembered for 5 s for the game mode's capture check (game-mode spec).
- **Dropping the flag** (player module item drop of type 7): the dropped flag gets classname `ctf_dropped_flag` and
  the carrier's flag team; configstring = `d`; the carrier loses the bolt-on and the loop sound. Once per second: if the
  flag is in lava or slime or inside a `trigger_hurt`, it returns to base with INACCESSIBLE_BLUE / INACCESSIBLE_RED;
  when `ctf_flag_respawn_time` seconds (default 90) have passed since the drop it returns with RESPAWN_FLAG_BLUE /
  RESPAWN_FLAG_RED; both play `dm/ctf/flagret.wav`.

### 10. Control flags (#39, deathmatch 6)
- Spawned neutral (skin `ctf_flag_grey`, owner none), numbered n = 0, 1, … in spawn order, named by `message`;
  configstring 3545 + n = `G<name>`. Same model/bounding box as CTF flags. Control mode sets configstrings 8 and 9 to `0`.
- Touch (after `ctf_control_delay_time` seconds): when the toucher's team differs from the owner, the previous owner
  team loses the point, the toucher's team gains it (game mode), everyone gets DM_CONTROL SWITCH_BLUE / SWITCH_RED
  ("%p Switched Control Point %s to Team …"), configstring 3545 + n is updated, the skin and light colour change to the
  new team; the capture sounds (as CTF) play to every player at most once per 0.5 s per flag.

### 11. Pickup statistics (`pickupinfo`, `listpickups`)
- When cvar `pickupinfo` ≠ 0 at level start, the game records per item entity: how it was created (`Design` = map,
  `Dropped`), number of pickups, average delay between pickups, average delay from respawn to pickup, number of
  refused touches and their average spacing.
- `listpickups` (cheats required, otherwise "Cheats not available.") prints the table to the console;
  when `pickupinfo` > 1 the same table is appended to `<game directory>/pickups.log` at the end of each level (the
  file is truncated on the first write of a session). Layout: `//`, `// begin pickup list -- map:<map>, elapsed
  gametime: <h>:<mm>:<ss>`, `//`, header `#     classname                 index  count  location`, then one row
  per item (row number, classname, item index, count, origin) and, while recording, the six statistics columns; last
  `// end pickup list`. Columns are padded with spaces to fixed positions (6, 32, 39, 46, 69, 79, 90, 99, 108, 120).
- `weaponarena` is registered (default 0) but not used by the item code.

## Constants

| Name | Value |
|---|---|
| Item table size | 39 classnames (indexes 1..39) |
| Default bounding half-size (before scale) | 8 |
| Re-pickup block for the dropper | 2 s |
| Refusal message debounce | 5 s per item |
| Dropped item lifetime (deathmatch family) | 20 s |
| Realistic dropped-weapon return | after 30 s, retry every 10 s, 128-unit search, 0.1 s reappear delay |
| Respawn scaling | T × √(2 / max(2, N)) |
| Respawn sound | `dm/irespawn.wav`, volume 0.6, attenuation 1 |
| Respawn visual reset | 0.8 s after reappearing (entity event 22) |
| Drop velocity | 250 horizontal + 150 up |
| Drop offset | 20 units forward |
| Knife pickup lifetime | 30 s (10 s: Arsenal, or `dmflags` 0x800) |
| CTF flag return check | every 1 s; timeout `ctf_flag_respawn_time` (default 90) |
| CTF recover bonus | 2 points |
| Flag-touch start delay | `ctf_control_delay_time` (default 20) s after level start |
| Control point sound throttle | 0.5 s |
| Max control points | 5 |
| Spawn-time weapon roll (float/spin) | 90° (heavy machinegun −90°), raised 6 units |

Skill tables (row selected by the cvar value, clamped to 0..4; a negative `sk_maxencum` uses row 0):
| Value | Ammo multiplier A (`sk_spawning`) | Armor multiplier B (`sk_toughness`) | Weight limit L (`sk_maxencum`, single player; 0 = none) |
|---|---|---|---|
| 0 | 2.0 | 2.0 | 0 |
| 1 | 1.0 | 1.0 | 10 |
| 2 | 1.0 | 1.0 | 7 |
| 3 | 1.0 | 1.0 | 5 |
| 4 | 5.0 | 0.5 | 5 |

Weapon weights (player module data, for reference): knife 0; .44 pistol 1; 9 mm pistol 1; 9 mm SMG 2; 5.56 SMG 2;
sniper rifle 2; slug thrower 2; shotgun 2; heavy machinegun 2; rocket launcher 3; microwave pulse gun 3; flame gun 3.

`dmflags` bits used by the item code (deathmatch-family modes, per the filter table): 0x1 no health, 0x2 no equipment,
0x4 weapons stay, 0x10 medkit limit 1, 0x400 no armor, 0x800 no ammunition (and 10-s knife pickups), 0x1000 floating
spinning weapons/armor, 0x2000 standard weapons only.

## Test vectors / how to verify

1. Single player, skill with `sk_spawning` 0: pick up `item_ammo_pistol` with no `count` → 60 rounds of 9 mm are
   added (30 × 2.0) and "Picked up some 9mm ammo." is printed.
2. Deathmatch, 8 players connected, pick up `item_weapon_rocketlauncher`: it reappears 37.5 s later (75 × 0.5) with
   the `environ/irespawn` effect and becomes touchable when its fade-in animation ends.
3. Deathmatch with `dmflags` 4: touching a map weapon gives it but the weapon stays; touching it again prints nothing.
4. Deathmatch with `dmflags` 0x2000: no slug thrower, rocket launcher, MPG or flame gun and none of their ammo boxes
   exist on the map (`listpickups` with cheats).
5. Realistic (`deathmatch 5`): `item_health_small` is absent, `item_health_large` positions hold medkits; touching a
   weapon you already own prints "You already have this weapon."; carrying weapons of total weight 4 and touching a
   weight-1 weapon prints the ENCUMBERED message.
6. Single player, encumbered: touching a new weapon gives its ammunition once (secondary message), then always
   ENCUMBERED.
7. Map entity `item_ammo_shotgun` with `count -3` gives 0..3 shells (re-load the map several times).
8. Drop a weapon in deathmatch: the dropper cannot re-take it for 2 s; it vanishes after 20 s; the console shows
   "Dropped a <name>".
9. `pickupinfo 2`, play a deathmatch level to the end: `<game>/pickups.log` contains the begin/end markers and one
   row per item.
10. Savegame of a level: the chunk sequence `PLNM`, N × `PLPI` (16 bytes), `PLWA` (1 byte) appears in the level part.

## Open questions

- Meaning of the `PLWA` byte and of the extra physics value 123 on dropped deathmatch weapons.
- Whether items without a fade-in sequence become touchable again after a deathmatch respawn (test 2 with
  `item_ammo_knife`).
- Exact meaning of spawnflag 2 for items (shared model-entity setup, WP8) and of entity event 22 (effects spec).
- What the player module does with "default" (−1) rounds when a map weapon is given (SPEC-003 / weapons spec).
- Whether the flag light-part calls switch parts off or on (Behaviour 9).
