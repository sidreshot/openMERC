# SPEC-770: Map objects (props) — misc_generic_*, level-themed misc_*_* and light_generic_* classes
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (server-side game module), version 1.07f
Depends on: SPEC-006 (classname list), SPEC-010/011/012/014 (Ghoul models, instance operations), SPEC-201 (messages,
temp entities), SPEC-202 (entity state), SPEC-400/401 (client effects, temp-entity drawing, surface and debris
materials, client sound list), SPEC-720 (damage, means of death), SPEC-721 (gore), SPEC-752 (savegame), SPEC-760
(world core: movetypes, material-flag string, sound notation), SPEC-761 (ignition, flash blinding, camera shake),
SPEC-771 (server-side effect emission), SPEC-781 (items)

## Purpose
Full behaviour of the 194 map-object classnames ("props") of the game module: the 102 `misc_generic_*` classes, the
70 level-themed `misc_<level>_*` classes (Kosovo/Bosnia, castle, Iraq, New York, Siberia, Tokyo, Uganda), the two
classes `misc_exploding_heli` and `misc_test_gun_auto`, and the 20 `light_generic_*` light fixtures. Every prop is a point
entity drawing a Ghoul model taken from a fixed model table; most can be shot to pieces, pushed, knocked over or
switched. The spec gives the shared framework (model tables, standard setup, breaking, shared reactions) and then,
per family, the model, defaults and every pain/death/use/touch/think rule with its numbers, so that a
reimplementation reproduces what the player sees and hears and what other entities experience (damage, targets).
`misc_ctf_base` belongs to the game-mode spec. Effect message forms are in SPEC-771; client drawing in SPEC-400/401.

## Interface / layout

### Classnames covered
| Family | Count | Classnames |
|---|---|---|
| generic | 102 | `misc_generic_access_card`, `banner`, `barbwire_coil`, `barrel_burning`, `barrel_chemical`, `barrel_wine`, `bench`, `blinds`, `bookshelf`, `broom`, `bunk`, `car_american`, `car_european`, `car_honda`, `car_rolls`, `car_sports`, `cart`, `cash_register`, `chair_legs`, `chair_legs2`, `chair_office`, `chunks_brick`, `chunks_glass`, `chunks_metal`, `chunks_rock`, `chunks_wall`, `chunks_wood`, `comp_keyboard`, `comp_monitor`, `comp_monitor2`, `crate_fish`, `desk_phone`, `fan_big`, `faucet`, `fire_extinguisher`, `fire_hydrant`, `fish`, `fish_dead`, `flag_pole`, `forklift`, `garbage_bag`, `gun_auto`, `gun_auto2`, `gun_auto3`, `gun_big`, `gun_castle`, `heap`, `light_beam`, `light_flare`, `motorcycle`, `orange_cone`, `palette_jack`, `paper_blowing`, `parking_meter`, `phone_booth`, `pillow`, `plant_potted`, `plant_tall`, `radar_dish`, `radio`, `security_camera`, `security_camera2`, `shovel`, `shrub`, `sink`, `sofa`, `sofa_chair`, `speaker`, `sprinkler`, `stoplight`, `street_name_sign`, `sword`, `sword_large`, `table1`, `table2`, `table_round`, `tank_gas`, `tank_propane`, `toilet`, `train`, `trash_ashtray`, `trashcan_barrel`, `tree_dead`, `tree_thin`, `truckcab`, `tv_ceiling`, `tv_wall`, `urinal`, `urinal_cake`, `urn`, `waste_basket_wire`, `web_moss_animated`, `wet_floor_sign`, `wpn_bat`, `wpn_c4`, `wpn_desert_eagle`, `wpn_glock`, `wpn_ingram`, `wpn_katana`, `wpn_minimi`, `wpn_sig3000`, `wpn_spas12` (all with prefix `misc_generic_`) |
| Kosovo/Bosnia | 10 | `misc_bosnia_antipersonnel_jack`, `_flag_wall`, `_landmine`, `_rubble1`, `_rubble2`, `_searchbeam`, `_searchlight`, `_tank_jack`, `_truck_chunks`, `_truck_old` |
| castle | 8 | `misc_castle_armor`, `_bust`, `_cannon`, `_chair`, `_chair_big`, `_globe`, `_hedge`, `_tree` |
| Iraq | 12 | `misc_iraq_basket`, `_cart_camel`, `_flatbed`, `_fruit_melon`, `_fruit_orange`, `_fruit_stand`, `_partition`, `_pottery`, `_sprinkler`, `_stealth`, `_tree_palm`, `_valve` |
| New York | 5 | `misc_newyork_burned_car`, `_gumball`, `_news_machine`, `_trash_pile`, `_trashcan` |
| Siberia | 9 | `misc_siberia_carton1` … `_carton4`, `_flag`, `_icicles`, `_roadflare`, `_searchlight`, `_snowcat` |
| Tokyo | 11 | `misc_tokyo_bowl`, `_buddha`, `_fan`, `_gong`, `_gym_equipment1`, `_gym_equipment2`, `_plant_bonsai`, `_samurai_suit`, `_samurai_suit_small`, `_shoe`, `_stone_light` |
| Uganda | 15 | `misc_uganda_african_tree`, `_chainsaw`, `_cow`, `_cow_head`, `_gore_pile`, `_gutted_cow`, `_hook`, `_nuke`, `_rocket_core`, `_rocket_engine`, `_side_o_beef`, `_train_arm`, `_train_bumper`, `_train_coupling`, `_train_wheel` |
| other | 2 | `misc_exploding_heli`, `misc_test_gun_auto` |
| lights | 20 | `light_generic_alley_wall`, `_alley_wall_old`, `_caged`, `_candelabra`, `_candelabra_small`, `_chandelier`, `_desk_lamp`, `_emergency`, `_emergency_blue`, `_halfshell`, `_hanging`, `_lamp_hang`, `_lantern`, `_lantern2`, `_lantern3`, `_military_door`, `_military_door_outside`, `_shop`, `_street_modern`, `_street_old` |

All are spawned through the classname tables (SPEC-006, SPEC-760 §B1.1). Keys are those of the key table (SPEC-760
§4); classes reuse generic keys (`health`, `count`, `wait`, `delay`, `style`, `skin`, `speed`…) as parameters, listed
per class.

### 1. Terms and scope

- **Prop**: an entity spawned from one of the classnames of this spec. Every prop is a point entity that shows a
  Ghoul model (SPEC-010/011/012) chosen from a fixed *model table* (§2).
- "Sound (ch, vol, attn)" = the game's positioned entity sound with channel, volume and attenuation as in SPEC-760.
  "Loop sound" = the entity's state sound field set to the sound with full volume and attenuation class 0 (state
  sound data value 0xF8, SPEC-202), heard continuously while the entity exists.
- "Effect `name`" = the effect configstring of that name sent with svc_effect (SPEC-201 §5) — at a position, on the
  entity, or on a named bolt of its model (SPEC-771 §1 gives the exact message forms). Temp entities are numbered as
  in SPEC-201 §1 (TE n) and drawn as in SPEC-401.
- Surface type = impact material 0..42 (SPEC-401 "Surface materials"); debris material = chunk type 0..34
  (SPEC-401 "Debris chunk types"). Means of death (MOD) and damage flags: SPEC-720.
- Ghoul operations are named by their meaning in SPEC-011: *show/hide part* (method 8), *hide all parts*
  (method 10), *set skin* (method 11), *tint* (method 14), *play sequence* (method 17) with mode *loop* (0),
  *once-and-hold* (1) or *static pose* (3), *pause* (method 19), *bolt-on* (method 28), *bolt matrix* (method 50),
  *bounds* (method 51).
- "Effect slot `name`" = the effect is added to one of the four effect slots of the entity state (bolt 0 = the
  entity origin unless a bolt is named; scale 0 = default size) and removed again by name — rules in SPEC-771 §C.4.
  "Rotation record" (also "spin record") = the entity-state rotation of SPEC-771 §C.4 (effects bit 0x0001, v0 = 999,
  v1..v3 = trunc(512 × angular rate), v4..v7 = Ghoul instances to turn, 0 = the whole entity). "Fade out over t s"
  = the fade of SPEC-771 §C.4.
- Time unit: seconds of level time; "every 0.1 s" = each server frame.

### 2. Model tables

Each prop family has a model table: an array of 40-byte entries. A spawn behaviour passes one entry to the
standard prop setup (§3) and may attach further entries as bolt-on parts (§5.6). The entry also supplies defaults:

| Bytes | Field | Use |
|---|---|---|
| 0–3 | model directory | under `ghoul/`; e.g. `objects/generic/bench` |
| 4–7 | model file | Ghoul object `ghoul/<dir>/<file>.ghb`; its default sequence is the one registered as `ghoul/<dir>/<file>.ghl` |
| 8–11 | surface type | default for key `surfacetype` |
| 12–15 | debris material | default for key `material` |
| 16–19 | health | default for key `health` |
| 20–23 | solid | default solid value (0 not solid, 2 box) |
| 24–27 | skin material | name of the model material whose skin the setup may replace (none = no skin change) |
| 28–31 | debris count | count for the table break (§4.2); only the low byte is used |
| 32–35 | debris size | float, truncated: debris size class for the table break |
| 36–39 | part table | filled at spawn by classes with shootable bolt-on parts (§5.7); 0 in the file image |

The tables, their order and the index of every entry are part of the savegame format: an entity's model-table entry
is saved as a "sequence reference" (SPEC-752 §2.1) = table number × 65536 + entry index. Table numbers:
0 Kosovo/Bosnia (14 entries), 1 castle (15), 2 generic (121), 3 Iraq (15), 4 items (41, SPEC-781), 5 lights (23),
6 New York (5), 7 Siberia (11), 8 Tokyo (12), 9 Uganda (15), 10 helicopter (12), 11 tank (5), 12 snowcat (3)
(10–12 belong to the vehicle characters). Appendix A lists every prop table entry.

## Behaviour


### 3. Standard prop setup

The spawn behaviour of a prop first sets the bounding box (`mins`/`maxs`, given per class) and any class
callbacks, then runs the standard setup with a model-table entry, an optional skin name and an optional
"only part" name:

1. The model is created from the entry (unless the entity already has a Ghoul instance) and its default sequence is
   played in loop mode. Errors are developer prints (`SetSimpleGhoulModel couldn't …`).
2. If a skin name is given, that skin is applied to the entry's skin material (`WARNING: Can't find material:%s` /
   `WARNING: Can't find skin:%s` when missing). Skins are files `ghoul/<dir>/<skin>.tga` (or as listed in the
   model's `.ifl`).
3. If an "only part" name is given, all mesh parts are hidden and only that part is shown.
4. Defaults from the entry are applied to every value that is still 0: surface type, debris material, health and
   solid. A solid value of 4 on an entity without a Ghoul instance prints
   `ERROR: attempting to set SOLID_GHOUL without ghoulInst (model '%s')!` and becomes 0.
5. **Right-angle box rotation.** The box is rotated with the entity angles, axis by axis in the order pitch, yaw,
   roll; each angle is truncated to an integer and only the values 90, 180 and 270 rotate (anything else leaves
   that axis alone). With (mins, maxs) = (n, x) before the step:
   - yaw 90: x-range = [−x.y, −n.y], y-range = [n.x, x.x]; yaw 180: x-range = [−x.x, −n.x], y-range = [−x.y, −n.y];
     yaw 270: x-range = [n.y, x.y], y-range = [−x.x, −n.x];
   - pitch 90: x-range = [n.z, x.z], z-range = [−x.x, −n.x]; pitch 180: x-range = [−x.x, −n.x], z-range =
     [−x.z, −n.z]; pitch 270: x-range = [−x.z, −n.z], z-range = [n.x, x.x];
   - roll 90: y-range = [−x.z, −n.z], z-range = [n.y, x.y]; roll 180: y-range = [−x.y, −n.y], z-range = [−x.z,
     −n.z]; roll 270: y-range = [n.z, x.z], z-range = [−x.y, −n.y].
   When more than one axis rotates, the later axes read the extents of the original box except for the x minimum,
   which is read after the earlier rotation (original quirk; only matters for combined right angles, UNVERIFIED in
   shipped maps).
6. Clip mask 0x02030003 (solid | window | monster clip | player clip | dead-monster area, as SPEC-760).
7. Key `scale` (spawn-time float) ≠ 0: the model is scaled uniformly by it (the collision box is not changed).
8. **Damage and physics flags.**
   - Classnames starting with `light`: damageable only with spawnflag 2; default death = standard break with
     computed debris (§4.1). With spawnflag 4 the prop gets physics (movetype 10, SPEC-760 §B4.7) and, unless the
     class set its own touch, the player push (§5.1); otherwise movetype NONE.
   - All other props: damageable unless spawnflag 1; default death = table break (§4.2). With spawnflag 2 the prop
     is static (movetype NONE, no push); otherwise movetype 10 with the player push unless the class set its own
     touch.
   Many classes force spawnflag bits before the setup runs (listed per class as "forces +1", "clears 1", "toggles 1").
9. If the solid value is 4 the box is replaced by the model's bounds at the current time (for the entity angles).
10. Key `color` (spawn-time RGB, each 0–1) non-zero: the model is tinted (r, g, b, 1).
11. The entity is linked; mass = 800.
Classes that build their model themselves use the same steps 4–10 without a model table (no mass change).

General rules that apply to every prop:
- Spawnflag 64 on a `misc_*` classname removes the entity at spawn when cvar `gl_pictip` is non-zero (SPEC-006).
- The `health` key overrides the table health; health ≤ 0 after damage calls the death behaviour.
- `targetname` + a class use behaviour: the prop reacts to being fired (per class). Props never respond to the
  player use key unless stated.
- At level load each prop's debris material and surface type are merged into the material-flag string
  (configstring 16, SPEC-760 §B1.5). Some classes additionally set characters of that string or mark entries of the
  server's sound precache list directly (listed per class as "precaches sound list k"). That list has the same
  indices and files as the client sound list of SPEC-401 (0–113; entry 44 is the never-loaded placeholder); every
  marked entry is registered as a sound at the end of the level spawn (SPEC-760 §B1.3 step 5), so that the client's
  temp-entity sounds of that index are available.

### 4. Breaking

#### 4.1 Standard break (n, s)
Used as the death behaviour (the attacker is the entity credited with the kill) and by many class deaths:
1. If n = 0 or s = 0: V = size.x · size.y · size.z of the entity box; if n = 0, n = trunc(V / 9000 + 2); n is then
   clamped to 10…20 (this clamp also applies to a given n when s = 0); if s = 0, s = 1.
2. If the debris material is > 0: one debris temp entity (TE 18) from the box centre c = (absmin + absmax)/2 with
   direction normalize(attacker origin − c), count n, size class s, chunk type = debris material, extents =
   trunc(maxs.x), trunc(maxs.y), trunc(maxs.z), surface type (the client plays the break sound, SPEC-401).
3. If the prop has a `message`, it is centre-printed to the attacker and `Misc/Talk.wav` is played on the attacker
   (channel 0, volume 0.6, attenuation 1).
4. All targets fire with the prop as activator (Quake II target rules: `target`, `killtarget`, `delay`, `message`).
5. The prop is removed.

#### 4.2 Table break
Standard break with n = the entry's debris count and s = trunc(entry debris size). Entries with count 0 or size 0
therefore get the computed count (for example a 20×20×30 box: V = 12000 → n = trunc(3.33) = 3 → clamped to 10).

### 5. Shared reactions

#### 5.1 Player push
Touch by a player (an entity with a client): velocity = normalize(prop origin − player origin) · 100, ground
friction 0; 0.1 s later the ground friction is restored to 1 (so the prop slides for one frame and then stops by
the movetype 10 friction). Touches by non-players do nothing.

#### 5.2 Knock over (k)
Pain reaction of tippable props: d = normalize(prop origin − attacker origin); φ = 360° − the entity state's
yaw-offset field (SPEC-202; 0 for props, so φ = 0). Pitch rate = 70·(d.x·cos φ − d.y·sin φ), roll rate =
−70·(d.x·sin φ + d.y·cos φ) degrees per second — with φ = 0 simply 70·d.x and −70·d.y, i.e. in world axes whatever
the prop's yaw (original quirk). Velocity = d · damage · k with vz replaced by 150; elasticity 0.7; then the
**tumble** runs every 0.1 s (k = 6 for the shared helper; classes give their own k).
Tumble: while health > 0 and both |pitch| and |roll| are below 90°, the pitch and roll rates are multiplied by 1.6
every 0.1 s; once either reaches ±90° the angle is clamped to ±90, all angular velocity stops, and if health < 25
the check repeats every 0.3 s, else the tumble ends.

#### 5.3 Throw away
Pain reaction of light props: d = normalize(prop origin − attacker origin); velocity = d · rand(4, 10) · damage,
then vz = rand(4, 6) · damage; yaw rate = rand(4, 6) · damage.
**Throw away with spin (axis a):** velocity = d · rand(10, 15) · damage; angular velocity component a = d[a] · 200.

#### 5.4 Standard prop explosion (ignite, flash)
1. Radius damage 100 within 100 units, inflictor = attacker = the prop, the prop itself ignored, means of death 0,
   no flags (SPEC-720 §14 linear).
2. If *ignite*: everything within 100 units in line of sight is set on fire by the prop (SPEC-761 §7.1, as
   func_ignite with radius 100).
3. Noise alert for characters (loudness 200, radius 200; AI spec).
4. Effect `weapons/world/airexplode` at the prop origin; explosion debris temp entity (TE 19) at the prop.
5. If *flash*: flash blinding (SPEC-761 §7.3) from the prop origin with radius = 10 × the prop's current health,
   maximum 0.9, duration 0.5. Because the explosion normally runs from the death behaviour (health ≤ 0), the radius is
   ≤ 0 and nobody is blinded (original quirk); only explosions triggered by use with positive health blind.
6. One camera-shake tick at the origin with the SPEC-761 §3.12 formula: maximum intensity 100, radius 200, event
   parameter 30 (a single tick, not repeated).

#### 5.5 Sequences and parts
Classes play named sequences of their model (once-and-hold or loop), pause the model, show/hide named parts and
change skins. Each use is listed per class with the exact names; names are Ghoul sequence/part names inside the
model file (SPEC-014).

#### 5.6 Bolt-on parts
A class may attach other model-table entries to named bolts of its main model (bolt names such as `DUMMY01`…; the
child's own bolt or origin is aligned). At most 6 bolt-on parts per prop are tracked (index 1…6, in attach order),
each with a "shown" bit. The skin argument is applied to the child's skin material (or, when the child entry has no
skin material, to the parent entry's). Bolt-ons are drawn, traced and saved with the prop (savegame chunks `EDOI`,
`EDBN`, `EDBL`, SPEC-752 §1.2 item 7).
- **Remove part i (with debris):** the bolt-on is hidden and its shown bit cleared; with debris, a debris temp
  entity (TE 18) of 5 chunks, size 1, the prop's debris material and surface type, zero direction, at the bolt
  position, plus a coloured smoke puff (TE 45) (120, 120, 120, 200) there.

#### 5.7 Shootable parts
Classes with a part table: when the prop is hurt at a point p, each listed bolt-on that is still shown is tested:
its model bounds at the current pose, placed at its bolt position and grown by 2 units on every side, must contain
p. The first match is hidden (shown bit cleared) and then either its listed reaction runs or, by default, the
5-chunk debris and grey puff of §5.6 at the part position. Then — on every pain of a prop with a part table,
also when no part was hit — every entity linked to the prop (objects lying on it) is thrown off: velocity =
normalize(its origin − prop origin) · rand(75, 125) with vz + 50, yaw + rand(−20, 20), origin z + 15, and the links
are cleared. Only the two tables `misc_generic_table1` / `misc_generic_table2` create such links (§6B.6).

#### 5.8 Fall over
Used by tall props pushed by a player (per class): the direction of the fall is chosen from the push direction
relative to the prop's forward/right axes (|forward · push| > 0.707 → fall forward/backward, else left/right); a
trace of the prop's length in that direction (and one along the fall arc) must be free, else nothing happens. The
prop then rotates about the bottom edge on that side: angular speed starts at 30°/s and grows by 30°/s every 0.1 s
until it has turned 85° from the start, then it is set to exactly 90°, the origin is placed so that the pivot edge is
unchanged, and the box is re-rotated (§3 step 5). The player use key is disabled during the fall; the previous
think resumes after it.

#### 5.9 Riders
Classes that move (per class) carry players standing on them: each player whose ground entity is the prop and whose
movetype is not NONE/NOCLIP/PUSH/STOP is moved by the prop velocity × 0.1 each frame unless that position is
blocked.

### 6A. Generic map objects, part 1 (`misc_generic_access_card` … `misc_generic_light_flare`)

All classes below use model table 2 (Appendix A.3); "G n" = entry n of that table. Unless stated otherwise a class
runs the standard prop setup (§3) with the listed entry, box and skin, has no pain/use/touch behaviour, and dies
with the table break (§4.2). "Skin k → name" = the skin passed to the setup when the entity's `skin` value
(`skinnum`) is k; "else" covers every other value. Box values are before the right-angle rotation (§3 step 5).
"Forces +1" etc. refer to spawnflag bits changed before the setup runs; "toggles 1" means the bit is inverted, so
the mapper's flag 1 makes such a prop *damageable* and its absence makes it indestructible.

#### 6A.1 Cards, banners, wire and barrels

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_access_card` | G0 `access_card/card` | — | (−2,−3,−1) / (5,3,1) | 500 | 0 / 0 | table (0 / 1) | 2 | spawnflags 1 and 2 are set *after* the setup (no effect on damage/physics) |
| `misc_generic_banner` | G1 `banner_generic/banner` | 1 → `banner2`; 2 → `banner3` (`banner5` with `lock_textures`); 3 → `banner4`; else `banner1` | (−4,−51,−70) / (4,51,70) | 999999 (fixed) | 0 / 0 | — | 2 | clears 1, forces +2; precaches `Ambient/Models/Banner/BanFlap.wav` |
| `misc_generic_barbwire_coil` | G2 `barbwire_coil/barbwire` | — | (−15,−59,−17) / (15,59,17) | 500 | 28 / 0 | table (15 / 1) | 2 | toggles 1, forces +2 |
| `misc_generic_barrel_burning` | G3 `barrel_burning/barrel` | — | (−11,−10,−15) / (11,10,15) | 260 | 28 / 9 | table (0 / 2) | 2 | toggles 1 |
| `misc_generic_barrel_chemical` | G4 `barrel_chemical/barrel` | spawnflag 4: `barrel2`; else skin 1 → `barrel3`, else `barrel` | (−9,−9,−15) / (10,8,15) | 260 (spawnflag 4 and no `health` key: 1) | 28 / 9 | computed (§4.1) | 2 | precaches `weapons/C4/C4Exp.wav` (precache list), `Ambient/Gen/FireFX/FireSmall.wav`; sets material-flag characters 19 and 20 |
| `misc_generic_barrel_wine` | G5 `barrel_wine/barrel` | — | (−15,−10,−10) / (15,10,10) | 500 | 14 / 17 | table (0 / 2) | 2 | model file absent in the shipped archives |

- **Banner.** Spawnflag 4 = "still": the model is paused at spawn and never animates. Otherwise a think runs once
  after (irand(0..4) · 0.1) s: it sets the loop sound `Ambient/Models/Banner/BanFlap.wav` and loops sequence
  `banner`. The banner cannot be destroyed (health 999999, static). Pain: if it has a model, it plays sequence
  `pain` once-and-hold and registers for that sequence's `EOS` note; at `EOS` it does what the think does
  (pause when spawnflag 4, else loop sound + loop `banner`).
- **Barbed wire coil.** Touch (any entity that can take damage): damage 1, knockback 1, flags TRACED (0x100), means
  of death 30 (barbed wire), inflictor = attacker = the coil, direction = toucher origin − coil origin, point =
  toucher origin. It runs every frame of contact (≈ 10 damage per second).
- **Burning barrel.** Spawnflag 4 = "lit": the entity gets the effect `environ/firesmall1` in an entity-state effect
  slot on its model bolt `DUMMY01` (default scale), the loop sound `Ambient/Gen/FireFX/FireSmall.wav`, and a touch:
  when the toucher's lowest point (origin z + mins z) is above the barrel's origin, damage 1, knockback 0, flags
  NO_ARMOR | FIRE (0x1002), means of death 27 (fire), direction (0, 0, 1), point = (toucher x, toucher y, toucher
  absmin z). Without spawnflag 4 the barrel is an ordinary prop.
- **Chemical barrel.**
  - Pain: knock over (§5.2) with k = 4 (velocity d · damage · 4, vz 150, elasticity 0.7), followed by its own
    tumble: as §5.2 but only while health > 0; when it stops tumbling with health ≥ 25 it settles: unless spawnflag 4,
    if each velocity component truncated is below 50 in magnitude a floor-decal temp entity TE 17 (pos = origin,
    long 10000; the client draws nothing for it, SPEC-201) is sent; then gravity 1, movetype STEP (5), the pain
    behaviour is removed and mins z is raised by 6 (the box shrinks from below).
  - Death (and use, see below): with spawnflag 4 ("explosive") — explosion ground flash TE 13 at the origin, effect
    `weapons/world/airexplode` at the origin, explosion debris TE 19 (from origin + 2·forward along the forward
    vector), radius damage 350 within 128 units (inflictor = attacker = the barrel, the barrel ignored, means of
    death 0, no flags, SPEC-720 §14 linear), character noise alert (loudness 150, radius 200), camera shake at the
    origin (one tick of the SPEC-761 §3.12 rule: maximum intensity 700, radius 1000, event parameter 30), alert-meter noise with amount 0.5 (SPEC-761 §8). Then, with or
    without spawnflag 4, the standard break with computed debris (§4.1). The death behaviour is cleared first so the
    explosion cannot run twice.
  - Use (`targetname`): exactly the death behaviour, with the entity that fired it as the attacker of the break.

#### 6A.2 Furniture with loose or shootable parts

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_bench` | G6 `bench/bottom` + bolt-on G7 `bench/top` | 1 → `bench2`; else `bench` (both parts) | (−10,−30,−17) / (10,30,17) | 250 | 14 / 17 | table (10 / 2) | 2 | part table: bolt-on 1 (top) on bolt `DUMMY02`, default reaction |
| `misc_generic_blinds` | G8 `blinds/blinds` | skin `blinds` on material `blinds` | (−1,−55,−65) / (0,55,65) | 200 | 28 / 9 | table (0 / 2) | 2 | toggles 1, forces +2; model absent in the shipped archives |
| `misc_generic_bookshelf` | G9 `bookcase/case` + 3 × bolt-on G10 `bookshelf/shelf` | 1 → `middle`; else `case` (main model only) | (−40,−16,0) / (40,16,64) | 500 | 14 / 17 | table (0 / 3) | 2 | part table: bolt-ons 1–3 on bolts `DUMMY01`–`DUMMY03`, reaction = none |
| `misc_generic_broom` | G12 `broom/broom` | 1 → `broom2`; else `broom` | (−2,−16,−25) / (2,16,25) | 60 | 14 / 17 | table (0 / 1) | 2 | has a pain behaviour that does nothing |
| `misc_generic_bunk` | G13 `bunk/side2` + bolt-ons G15 `bunk/side1`, G14 `bunk/bed`, G14, G15 | 1 → `bunk2`; else `bunk` (main model only) | (−40,−20,−29) / (40,20,29) | 500 | 28 / 8 | table (0 / 2) | 2 | part table: bolt-ons 1–4, bunk reaction (below) |
| `misc_generic_chair_legs` | G23 `chair_legs/chair` + 4 × bolt-on G24 `chair_legs/legs` | 1 → `legsb` (surf 14, mat 17); 2 → `legsplastic` (surf 0, mat 31); else `legs` (surf 28, mat 8) — all parts | (−10,−9,−12) / (10,9,18) | 400 | per skin | table (0 / 1) | 2 | part table: bolt-ons 1–4 on bolts `DUMMY01`–`DUMMY04`, chair reaction (below); a mass of 50 is set but the setup replaces it with 800 |
| `misc_generic_chair_legs2` | G25 `chair_legs2/legs2` | — | (−12,−10,−20) / (12,10,20) | 400 | 28 / 8 | table (0 / 1) | 2 | model absent in the shipped archives |
| `misc_generic_chair_office` | G27 `chair_office/base` + bolt-on G26 `chair_office/top` | 1 → `office2` (and debris material 9); else `office` (both parts) | (−13,−13,−17.5) / (13,13,24) | 600 | 28 / 11 (skin 1: 9) | table (0 / 1) | 2 | precaches `Impact/Chair/Squeak1.wav` |

Bolt-on attachment ("child bolt" = the bolt of the child aligned to the parent bolt; §5.6): bench top on `DUMMY02`
(child `DUMMY01`); bookshelf shelves on `DUMMY01` (child `DUMMY01`), `DUMMY02` (child `DUMMY04`), `DUMMY03`
(child `DUMMY01`), no skin; bunk bolt-on 1 = side on `DUMMY05` (child `DUMMY05`), 2 = bed on `DUMMY01` (child
`DUMMY02`), 3 = bed on `DUMMY03` (child `DUMMY02`), 4 = side on `DUMMY03` (child `DUMMY05`), no skin, then the
main model's part `SIDE02` is hidden and bolt-on 4 is turned 180° (π) about its x axis; chair legs on `DUMMY01`…
`DUMMY04` (child `DUMMY01`); office-chair top on `DUMMY02` (child `DUMMY01`). These classes record their spawn
angles ("start angles") for the reactions below.

- **Bench** pain (followed by the shootable-part test of §5.7 with the hit point, so the top can be shot off):
  with forward f, up u of the entity angles and hit point p, and only while u·(0,0,1) > 0.9 (upright):
  - p behind the bench (f·(p − origin) ≤ 0): if a trace (the entity's clip mask) from the origin 100 units along
    f is completely free, the bench falls backwards: pitch = 70, origin += 20·f + (0, 0, −7) and the box is shifted
    by the opposite amount (so it stays in place in the world); the top part is re-posed: its transform relative to
    its bolt becomes an offset (−1.5, −2, 0) combined with a rotation of 1.03653 rad (59.4°) about its z axis.
  - p in front: if the trace 100 units along −f is free: pitch = −90, origin −= 20·f, box shifted by
    (20·f.x, 20·f.y, 20·f.z + 6); the top's transform becomes a rotation of −0.3927 rad (−22.5°) about its y axis.
  - otherwise (trace blocked or not upright) only the part test runs. The order of offset and rotation in the
    top's new transform is UNVERIFIED (check the pose in game).
- **Blinds.** The model loops its default sequence unless spawnflag 4 (paused at spawn). Pain: the animation
  resumes, 0.5 s later it is paused again, and skin `blindsdmg` replaces `blinds`. Indestructible by default
  (toggled flag 1).
- **Bookshelf.** Each shelf can be shot out (hidden, no debris, no sound); the case dies with the table break.
- **Bunk** — after a part is shot off (its shown bit already cleared) the remaining parts S1 = side (bolt-on 1),
  L1 = bed (2), L2 = bed (3), S2 = side (4) decide the reaction. "Pitch = start ± a" sets the pitch relative to the
  spawn pitch, "pitch ± a" changes the current pitch; boxes are set in entity axes without the right-angle rotation;
  "upright box" = (−40,−20,35) / (40,20,40) with solid 2; the entity is relinked after a change.

  | shot | remaining | reaction |
  |---|---|---|
  | S1 | no beds, S2 | S2 turned +0.9425 rad (54°) about its z axis; pitch = start − 90; upright box |
  | S1 | no beds, no S2 | — |
  | S1 | L2 only bed, no S2 | pitch = start; box (−40,−20,−20) / (40,20,5) |
  | S1 | L2 only bed, S2 | pitch − 6; box (−40,−20,−24) / (40,20,29) |
  | S1 | both beds, no S2 | bed L1 removed with debris (§5.6); pitch = start; box (−40,−20,−20) / (40,20,5) |
  | S1 | both beds, S2 | pitch − 6; box (−40,−20,−24) / (40,20,29); bed L1 turned −0.34558 rad (−19.8°) about its z axis |
  | S1 | L1 only bed, no S2 | pitch = start; box (−40,−20,15) / (40,20,25) |
  | S1 | L1 only bed, S2 | pitch − 30; box (−40,−20,−10) / (40,20,29) |
  | L1 or L2 | the other bed still there | — |
  | L1 or L2 | no beds, S1 and S2 | S1 turned +0.9425 rad and S2 turned −0.9425 rad about their z axes |
  | L1 or L2 | no beds, S1 only | pitch = start + 90; upright box |
  | L1 or L2 | no beds, S2 only | pitch = start − 90; upright box |
  | L1 or L2 | nothing else | — |
  | S2 | no S1, L2 only bed | pitch = start; box (−40,−20,−20) / (40,20,5) |
  | S2 | S1, no beds | S1 turned −0.9425 rad about its z axis; pitch = start + 90; upright box |
  | S2 | S1, L2 only bed | pitch + 6; box (−40,−20,−24) / (40,20,29) |
  | S2 | S1, both beds | bed L1 removed with debris; pitch + 6; box (−40,−20,−24) / (40,20,29) |
  | S2 | S1, L1 only bed | pitch + 30; box (−40,−20,−10) / (40,20,29) |
  | S2 | any other | — |

  Every bunk reaction ends with the 5-chunk debris and grey puff of §5.6, placed at the bolt position of the shot
  part — or, in the rows that turn S2 (first row) or S1 (S2 shot, no beds), at that side's bolt `DUMMY03`, and in
  the row that turns bed L1, at that bed's bolt `DUMMY01`.
- **Chair with legs** — after a leg is shot off, with R = the legs still attached:

  | R | reaction |
  |---|---|
  | three legs | — |
  | legs 1 and 2 | pitch − 40; box (−10,−9,−6) / (10,9,18) |
  | legs 3 and 4 | pitch + 40; same box |
  | legs 2 and 3 | roll + 40; same box |
  | legs 1 and 4 | roll − 40; same box |
  | legs 1 and 3, or 2 and 4 | both remaining legs are removed with debris (§5.6); angles = start angles; box (−10,−9,0) / (10,9,18) |
  | one leg | — |
  | none | angles = start angles; box (−10,−9,0) / (10,9,18); no debris |

  Except in the last row, a debris temp entity TE 18 of 7 chunks, size 1, zero direction, the chair's debris
  material and surface type is sent at the chair origin (no puff). The box is relinked after each change.
- **Office chair** — pain spins the seat: rate w = damage · 0.1; d = normalize(attacker origin − chair origin),
  r = hit point − chair origin, c = (d × r).z. c > 0 spins one way, c < 0 the other, c = 0 does nothing. If the seat
  is not spinning yet, the entity's effect slots are replaced by a *spin record* (effects bit 0x1 set; first slot id
  999; the next three 16-bit values = spin vector × 512 with the vector (±min(w, 30), 0, 0); then the Ghoul instance
  ids of up to four parts to spin = the seat; SPEC-771 §C); if it is already spinning, ±w · 512 is added to the
  first value (no clamp). Every 0.5 s while spinning: with |v| > 2000 (v = that first value) the squeak
  `Impact/Chair/Squeak1.wav` (channel 4, volume 0.8, attenuation 1) is played; with 1000 < |v| ≤ 2000 it is played
  with probability 3/7 (irand(0, 6) ≤ 2); then v = trunc(v · 0.75); when |v| < 153.6 the record is cleared (effect
  slots zeroed, effects bit 0x1 cleared) and the think stops. The client turns the listed parts by the record
  (client behaviour UNVERIFIED — verify by shooting an office chair off-centre).

#### 6A.3 Vehicles, carts and the cash register

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_car_american` | G16 `car_american/car` | 1 → `car2`; 2 → `car3`; 3 → `car4`; else `car1` | (−69,−30,−25) / (69,30,25) | 2000 | 28 / 8 | table (0 / 1) | 2 | forces +2; the box is reset to these values after the setup (it does **not** follow the right-angle rotation) |
| `misc_generic_car_european` | G17 `car_european/car` | 1 → `car2`; 2 → `car3`; else `car` | (−91,−33,−28) / (91,33,30) | 2000 | 28 / 8 | table (0 / 1) | 2 | forces +2 |
| `misc_generic_car_honda` | G18 `car_honda/car` | — | (−55,−25,−23) / (55,25,30) | 2000 | 28 / 8 | table (0 / 1) | 2 | forces +2 |
| `misc_generic_car_rolls` | G19 `car_rolls/car` | — | (−84,−29,−24) / (84,29,46) | 2000 | 28 / 8 | table (0 / 1) | 2 | forces +2; model absent in the shipped archives; precaches `weapons/C4/C4Exp.wav` (precache list), `Ambient/Models/Alarm/CarDisarm.wav`, `Ambient/Models/Alarm/CarAlarm.wav`, effect `environ/helismoke` |
| `misc_generic_car_sports` | G20 `car_sports/car` | 1 → `car2`; else `car` | (−82,−35,−19) / (82,35,19) | 2000 | 28 / 8 | table (0 / 1) | 2 | — |
| `misc_generic_cart` | G21 `cart/cart` | 1 → `cart2`; 2 → `cartwood` (surf 14, mat 17); else `cart` | (−10,−20,−11) / (10,20,11) | 1000 | 28 / 8 (skin 2: 14 / 17) | table (0 / 1) | 2 | a mass of 25 is set but the setup replaces it with 800 |
| `misc_generic_cash_register` | G22 `cash_register/register` | skin `register` (skin 1: `registerd`) on material `register` | (−8,−8,−9) / (8,8,9) | 500 | 28 / 8 | table (0 / 1) | 2 | precaches effect `environ/normalsmoke` |

- **American car** (police car / taxi / plain car by skin): skins 0 and 3 hide the parts `_TAXILIGHT01`,
  `TAXI_OMNI01`, `TAXI_OMNI02`; skins 1 and 2 hide `MIDDLE_SECTION`, `REDSIREN`, `BLUESIREN`, `REDCONE`,
  `BLUECONE`, `WORLD_1`, `WORLD_2`, and skin 2 also the three taxi parts. Then sequence `off` is played
  once-and-hold. Spawnflag 4: sequence `on` once-and-hold (lights on). Spawnflag 8: loop sound
  `Ambient/Models/Alarm/Police.wav` with sound data 0xFA (volume 248, attenuation class 2, SPEC-202) and sequence
  `car` looped.
- **European car.** Spawnflag 4 = "appears when triggered": not solid and the model switched off (invisible) at
  spawn; use makes it solid (2) and switches the model on. It stays damageable while hidden (UNVERIFIED whether
  area damage can kill it while invisible).
- **Rolls-Royce** (model missing in 1.07f, so it is an invisible box): the model is paused at spawn.
  - Touch by a player: loop sound `Ambient/Models/Alarm/CarAlarm.wav` (state sound data 0xF8); the touch is then
    removed (the alarm starts once).
  - Use (map trigger) and player use: sequence `car` once-and-hold and a "played" marker is set.
  - Pain (first damage only; the pain behaviour is removed): if the sequence has not been played, `car` is played
    once; the standard prop explosion (§5.4, no ignite, no flash); if the alarm loop is running it is stopped and
    `Ambient/Models/Alarm/CarDisarm.wav` is played (channel 4, volume 0.8, attenuation 1); the touch is removed; the
    effect `environ/helismoke` is put in an effect slot on model bolt `dummy01`, and removed again 5 s later.
- **Cash register** pain (first damage only; the pain behaviour is removed): two spark temp entities TE 14 at the
  origin with directions (irand(−10, 10), irand(−10, 10), 0) (normalised by the message); skin 0 entities change
  to skin `registerd`; effect `environ/normalsmoke` in an effect slot on the entity origin (bolt 0), removed 2 s
  later.

#### 6A.4 Chunks (single-piece debris props)

The `style` key selects which single mesh part of the model is shown (the setup's "only part", §3 step 3).

| classname | model | skins | style → part | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|---|
| `misc_generic_chunks_brick` | G28 `chunks_brick/brick` | 1 `grey_dk`, 2 `brwn_dk`, 3 `brwn_lt`, 4 `grey_lt`, else `red` | 1 `CHUNK_2`, 2 `CHUNK_3`, else `CHUNK_1` | (−6,−3,−2) / (6,3,2) | 50 | 20 / 2 | table (0 / 1) | 2 | — |
| `misc_generic_chunks_glass` | G29 `chunks_glass/glass` | — | 1 `GLASS02`, 2 `GLASS03`, else `GLASS01` | (0,−1,−1) / (1,1,1) | 25 | 37 / 7 | table (0 / 1) | 2 | model absent |
| `misc_generic_chunks_metal` | G30 `chunks_metal/metal` | 1 `rusty`, 2 `shiny`, 3 `light`, else `dark` | 1 `METAL2`, 2 `METAL3`, 3 `METAL4`, else `METAL1` | (−4,−2,−4) / (4,2,4) | 100 | 28 / 8 | table (0 / 1) | 2 | model absent |
| `misc_generic_chunks_rock` | G31 `chunks_rock/rock` | 1 `brwn_lt`, 2 `grey_dk`, 3 `grey_lt`, else `brwn_dk` | 1 `ROCK2`, 2 `ROCK3`, 3 `ROCK4`, else `ROCK1` | (0,−1,−1) / (1,1,1) | 50 | 20 / 14 | table (0 / 1) | 2 | model absent |
| `misc_generic_chunks_wall` | G32 `chunks_wall/wall` | see below | 1 `_SMALLCHUNK`, 2 `_MEDIUMCHUNK`, 3 `_LARGECHUNK`, else `_LITTLECHUNK` | (−4,−2,−4) / (4,2,4) | 10 | per skin / 14 | table (0 / 1) | 2 | model absent |
| `misc_generic_chunks_wood` | G33 `chunks_wood/wood` | 1 `grey_dk`, 2 `brwn_lt`, 3 `grey_lt`, else none (see below) | 1 `WOOD2`, 2 `WOOD3`, 3 `WOOD4`, else `WOOD1` | (0,−1,−1) / (1,1,1) | 30 | 15 / 18 | table (0 / 1) | 2 | model absent |

- Wall chunk skins and the surface type they set: 1 `brown` (24), 2 `lbrown` (17), 3 `dbrown` (19), 4 `grey`
  (19), 5 `dgrey` (17), 6 `lgrey` (18), 7 `green` (18), 8 `orange_cone` (22), 9 `red` (23), 10 `white` (4),
  else `black` (17).
- Wood chunk with skin 0 or > 3: the skin name passed is not a valid string (an original defect; it reads the
  entity's own memory), so the "can't find skin" warning is printed and the model keeps its default skin. A clean
  implementation should treat it as "no skin".
- In 1.07f only the brick chunk has its model file (the shipped chunk models have other file names), so the other
  five spawn as invisible boxes that can still be shot and broken (UNVERIFIED in game).

#### 6A.5 Office and household objects

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_comp_keyboard` | G34 `comp_keyboard/keyboard` | — | (−4,−11,−2) / (4,11,2) | 100 | 28 / 25 | table (0 / 1) | 2 | — |
| `misc_generic_comp_monitor` | G35 `comp_monitor/monitor` | skin `monitor` on material `monitor` (main model and bolt-ons) | (−9,−12,−12) / (9,12,12) | 250 | 38 / 25 | — (own death) | 2 | precaches `Impact/Explosion/Small.wav` (precache list), `Impact/Glassbreak/Monitor.wav`, `Ambient/Gen/Electric/buzz.wav`, effect `environ/normalsmoke` |
| `misc_generic_comp_monitor2` | G37 `comp_monitor2/tower` + bolt-on G36 `comp_monitor2/monitor2` | skin ≠ 0: skin `monitor2_d` on the tower's material `monitor2` (and bolt-ons) | (−9,−12,−12) / (9,12,12) | 250 | 38 / 25 | table (0 / 1) | 2 | monitor on bolt `DUMMY01` (child `DUMMY01`); precaches as the monitor |
| `misc_generic_crate_fish` | G38 `crate_fish/crate` | — | (−10,−7,−1) / (10,7,8) | 500 | 14 / 17 | table (0 / 1) | 2 | — |
| `misc_generic_desk_phone` | G39 `desk_phone/phone` | 1 → `phone2`; else `phone` | (−8,−10,−5) / (8,10,5) | 50 | 28 / 25 | table (0 / 1) | 2 | precaches `Impact/Explosion/Small.wav`, `Ambient/Models/Phone/Hit.wav` (precache list), effect `environ/normalsmoke` |
| `misc_generic_fan_big` | G40 `fan/fan` | — | (−64,−40,−63) / (63,0,63) | 500 | 28 / 11 | table (0 / 1) | 2 | forces +2; keys `speed`, `volume` (below) |
| `misc_generic_faucet` | G41 `faucet/faucet` | — | (−6,−5,−5) / (11,9,5) | 150 | 28 / 11 | table (0 / 1) | 2 | forces +2 |
| `misc_generic_fire_extinguisher` | G42 `fire_extinguisher/exting` | skin `exting` on material `exting` | (−2,−6,−12) / (4,8,12) | 150 | 28 / 9 | (own death) | 2 | forces +2; precaches `Impact/Exting/ExtingLeak.wav`, effect `environ/extinguisher1` |
| `misc_generic_fire_hydrant` | G43 `fire_hydrant/hydrant` | — | (−8,−8,−10) / (8,8,14) | 500 | 28 / 9 | (own death) | 2 | forces +2; precaches `Ambient/Gen/WtrFX/SprayBig.wav` |
| `misc_generic_fish` | G44 `fish/fish` | 1 → `yellow`; else `white` | (−17,−6,−7) / (17,6,7) | 150 | 35 / 32 | table (0 / 1) | 2 | movetype FLY, no knockback (flag 0x800) |
| `misc_generic_fish_dead` | G45 `fish/fish` | 1 → `yellow`; else `white` | (−17,−6,−1) / (17,6,7) | 150 | 35 / 32 | table (0 / 1) | 2 | roll + 90 after the setup |

- **Keyboard** pain: throw away (§5.3) with damage trunc(0.8 · damage).
- **Broken screen** (monitor, monitor2):
  - Pain (first damage only; the pain behaviour is removed): skin `damaged` (monitor) / `monitor2_d` (monitor2:
    set on the monitor2 entry's material `monitor2`) on the model and its bolt-ons; small explosion TE 46 at
    origin + 10 · forward (monitor) / + 14 · forward (monitor2), size 100 (1.0), silent; effect
    `environ/normalsmoke` in an effect slot on the entity origin; `Impact/Glassbreak/Monitor.wav` (channel 2,
    volume 0.6, attenuation 1); loop sound `Ambient/Gen/Electric/buzz.wav`; a smoke timer T = now + rand(6, 12);
    the broken-screen think starts at once.
  - Broken-screen think, every 0.1 s, forever: while now < T and the smoke slot is present, an electric-arc temp
    entity TE 8 at origin + 8 · forward − 6 · right + (0, 0, irand(−6, 5)), length 18, direction = right; once
    now ≥ T the smoke slot is removed and the loop sound stopped (no more arcs).
  - Death (monitor only): debris TE 18 of 7 chunks, size 1, zero direction, the prop's debris material and surface
    type at the origin; then the television death of §6B.5 ("Death (both TV classes)": three TE 10 sparks, a
    5-chunk glass burst, medium explosion TE 55, standard break with computed debris). The second monitor dies with the table break.
- **Desk phone.** First pain: throw away with trunc(0.9 · damage); small explosion TE 46 at the origin (size 10 →
  0.1, silent); `environ/normalsmoke` effect slot on the origin; sparks TE 14 at the origin along the entity's up
  vector; phone-hit temp entity TE 58 at the origin; smoke timer T = now + rand(0.5, 1.5) checked every 0.2 s (the
  first check after 0.1 s): when now ≥ T the smoke slot is removed and the loop sound field cleared. Every later
  pain: TE 58 at the origin and throw away with trunc(0.9 · damage).
- **Big fan** — a spinning fan driven by a spin record (§6A.2 office chair) whose vector is (0, s, 0) for the
  current speed s, with the fan's own model as the part. Keys: `speed` = full speed (default 1.5 when the fan starts
  on; values above 31 are clamped to 31 — the clamp is applied *after* the spawn-time record, so an initially
  running fan with `speed` > 31 spins at the unclamped value until it is next toggled), `volume` = speed change per
  0.1 s (default 0.05). Spawnflag 4 = start off (s = 0, no record). Use toggles: when off it accelerates by `volume`
  every 0.1 s until it reaches `speed`; when on (or accelerating) it first writes the full-speed record once, then
  decelerates by `volume` every 0.1 s to 0. The record is rewritten at every step. A fan stays static (+2).
- **Fire extinguisher** (indestructible prop on the wall until shot):
  - Death: skin `damaged` on material `exting`; health 50; loop sound `Impact/Exting/ExtingLeak.wav`; effect
    `environ/extinguisher1` on the entity (entity form, SPEC-771 §C); it starts *leaking*.
  - Use (map trigger or player use key): health = 100 + irand(0, 9); loop sound `Impact/Exting/ExtingLeak.wav`; it
    starts leaking (no skin change, no effect).
  - Leaking: health decreases by 1 every 0.1 s. If it is damaged while leaking (pain or death) it **bursts**:
    radius damage = current health within (0.5 · health + 60) units (inflictor = the extinguisher, attacker = the
    entity that hurt it, the extinguisher ignored, means of death 26, no flags), camera shake at the origin
    (one tick of the SPEC-761 §3.12 rule: maximum intensity 60, radius 900, event parameter 30), the loop sound stops, yaw + 180, and 0.1 s later the table break
    (§4.2) removes it. If the leak runs out (health < 1) it becomes an ordinary breakable: health 1, damageable,
    no pain, death = standard break with computed debris, loop sound stopped.
- **Fire hydrant** death (damageable unless spawnflag 1): debris TE 18 of 5 chunks, size 1, chunk type 0, surface
  0, zero direction and extents at the origin; effect `environ/toiletspout` at the origin; the hydrant is removed
  1 s later. No targets are fired and no `message` is shown (unlike the standard break).
- **Swimming fish.** The entity's `angles` value is reused as a *destination point* B (A = spawn origin). If B is not
  (0, 0, 0): the fish faces from A to B and moves at 20 units/s toward B (movetype FLY); every 0.1 s, when it is
  within 10 units of its current goal it turns 180° (velocity and facing reversed) and heads for the other point.
  The model loops sequence `fish`. Use: the velocity doubles and the fish stops turning; from then on it sinks
  1 unit every 0.1 s while its lowest point (origin z + mins z) is above (key `volume` + 3) — a world z height,
  default 0 (UNVERIFIED intent).

#### 6A.6 Outdoor objects, rubbish and light props

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_flag_pole` | G46 `flag_pole/flag_pole` | 1 → `flag_pole2`; 2 → `flag_pole3`; else `flag_pole` | (−4,−65,−417) / (4,38,36) | 500 | 28 / 9 | table (0 / 1) | 2 | forces +2 |
| `misc_generic_forklift` | G47 `forklift/forklift` | 1 → `forklift2`; 2 → `forklift3`; else `forklift` | (−73,−54,−53) / (91,4,52) | 2000 | 28 / 9 | table (0 / 1) | 2 | not forced static (pushable unless spawnflag 2) |
| `misc_generic_garbage_bag` | G48 `garbage_bag/bag` | — | (−9,−10,−9) / (9,10,10) | 100 | 0 / 25 | table (0 / 1) after the burst | 2 | sets material-flag characters 21 and 22 |
| `misc_generic_heap` | G57 `heap_generic/heap` | 1 → `heap2` (surf 27); 2 → `heap3` (surf 13); else `heap` (surf 3) | (−20,−15,−1) / (16,17,2) | 200 | per skin / 0 | table (0 / 1) | 2 | forces +2 |
| `misc_generic_light_beam` | G58 `beam/beam` | — | (−13,−312,−30) / (50,141,5) | 250 | −1 / −1 | — | 0 (not solid) | forces +1 +2 (indestructible, static) |
| `misc_generic_light_flare` | G59 `objects/light/flare` | — | (−5,−5,−5) / (5,5,5) | 500 | −1 / −1 | — | 0 (not solid) | forces +1 +2 |

- **Flag pole.** The model is paused at spawn; after irand(0..4) · 0.1 s sequence `flag_pole` starts looping
  (restart, offset 0).
- **Forklift.** The model is paused at spawn (still pose). Spawnflag 4: not solid and invisible until used; use
  makes it solid (2) and visible (same as the European car).
- **Garbage bag.** Pain: blowing-paper temp entity TE 48 at the origin with count 3, then throw away with spin about
  the pitch axis (§5.3, axis 0). Death: TE 48 at the origin with count 0 (random 5..10 sheets); soda cans TE 50 from
  the origin in the direction normalize(attacker origin − bag origin) — **toward** the attacker (the inflictor when
  there is no attacker; straight up (0, 0, 1) when there is neither) — speed 100, count 0 (random 1..4); then the
  table break.
- **Light beam / flare** are decorative non-solid models (light cones and lens flares placed by the mapper).

#### 6A.7 Automatic guns (turrets)

All turrets share these rules:
- **Target search** (every 0.1 s once active): every player (entity with a client) within the detection radius
  `volume` (default 400) is tested with a trace (mask 0x060000B9) from the turret origin to the player's origin.
  If the first thing hit is not a player or is a dead player, the turret forgets its enemy and the search ends for
  this frame (later candidates are not tested); otherwise that player becomes the enemy (the last such player in the
  list wins). If the enemy is farther than `volume`, the yaw rate is cleared and, if the loop sound is
  `Enemy/MGun/trnslow.wav`, it is replaced by `Enemy/MGun/trnstop.wav` (as the state sound); otherwise the turret
  aims and fires (below).
- **Aiming.** Parts are turned one axis at a time toward the aim point: yaw first, and the gun pitches only in a
  frame where the yaw has already reached the target. Each frame a part turns at most the step angle; a part that
  would leave its limits stops at the limit. Angles are measured in the parent part's frame.
- **Speed key.** `speed` = turn rate in degrees per second (default 45), applied as `speed`/10 degrees per 0.1 s.
- **Gun death** ("autogun death"): loop sound stopped, yaw rate 0, effect `weapons/world/autogundeath` on the
  entity (entity form), the death behaviour cleared, not damageable, the standard prop explosion (§5.4) **with**
  ignite and flash, then the table break.
- Turrets set entity effects bit 0x200 (client meaning UNVERIFIED) and the no-knockback flag (0x800); they have a
  pain behaviour that does nothing.

| classname | model assembly | box mins / maxs | health | spawnflags | keys (defaults) |
|---|---|---|---|---|---|
| `misc_generic_gun_auto` | G50 `gun_auto/base`; bolt-on G52 `gun_auto/null` on `DUMMY01` (child `DUMMY01`, all its parts hidden) carrying G49 `gun_auto/gun` on its `DUMMY01` (child `DUMMY01`) | (−28,−21,−34) / (21,21,19) | 100 + toughness level (0–4, SPEC-760 §B9.2) | toggles 1 and 2; 4 = active; 8 = pitch limits; forces +32 | `speed` 45, `volume` 400, `wait` 30, `minpitch`, `maxpitch` |
| `misc_generic_gun_auto2` | G51 `gun_auto/arm` (gravity 0) with bolt-on G49 `gun_auto/gun` on `DUMMY02` (child `DUMMY02`) — ceiling arm | (−27,−18,−8) / (21,21,18) | as gun_auto | as gun_auto, forces +16 | as gun_auto |
| `misc_generic_gun_auto3` | as gun_auto, but every part of the base is hidden (gun without base) | (−21,−18,−8) / (21,21,8) | as gun_auto | as gun_auto, forces +16 +32 (16 is cleared again after the model is built) | as gun_auto |
| `misc_test_gun_auto` | as gun_auto, skin `gun` | (−28,−21,−34) / (21,21,19) | 10000 | standard (damageable unless 1) | — |
| `misc_generic_gun_castle` | G53 `gun_big/base` drawn at half size (its model transform scaled by 0.5; the box is not); bolt-on G56 `gun_big/shield` on `DUMMY01` (child `DUMMY01`) carrying G55 `gun_big/gunright` on `DUMMY02` and `DUMMY04` (child `DUMMY02`) and G54 `gun_big/gunleft` on `DUMMY03` and `DUMMY05` (child `DUMMY03`) | (−56,−37,−29) / (24,38,19) | 150 · toughness + 200 | forces +1 +2 (indestructible); 4 = active; 16 = use toggles; 32 = do not start at spawn; 8 = yaw limit | `speed` 45, `volume` 400, `wait` 15, `maxyaw` |
| `misc_generic_gun_big` | as gun_castle at full size | (−111,−74,−58) / (47,75,38) | 40 · (5 · toughness + 10) | forces +2; 4 = active | `volume` 400, `wait` 30 |

Skins (all parts): gun_auto family `gun` (skin 1: `gun2`); gun_castle skin 1 → `gun`, 2 → `gun2`, else
`gun_dekker`; gun_big `gun` (skin 1: `gun2`). Precaches: gun_auto family and gun_castle `Enemy/MGun/trnslow.wav`,
`Enemy/MGun/trnstop.wav`, `Weapons/Autoshot/fire.wav`, `Enemy/MGun/wake.wav`, effects `weapons/othermz/autogun`,
`weapons/world/autogundeath`, `environ/onfireburst`; gun_castle also `Enemy/MGun/Wake2.wav`; gun_big and the test
gun only `weapons/othermz/autogun`.

- **gun_auto / gun_auto2 / gun_auto3.**
  - Active only with spawnflag 4: then damageable and the search starts after `wait` · 0.1 s (default 3 s). Without
    spawnflag 4 nothing ever activates it (no use behaviour). Players with the no-target flag (0x20) are ignored.
    When an enemy is acquired while the turret had none, `Enemy/MGun/wake.wav` is played (channel 2, volume 0.6,
    attenuation 1).
  - Aim point = the enemy origin. Yaw: gun_auto / gun_auto3 turn the hidden "null" part about its bolt, gun_auto2
    turns the whole entity (its yaw angle); limits ±180°, step `speed`. Pitch: the gun part, step `speed`, limits
    [−`minpitch`, +`maxpitch`] (degrees) with spawnflag 8, otherwise no pitch at all (sign convention of the limits
    UNVERIFIED). While the yaw is still turning (step-limited) the loop sound is `Enemy/MGun/trnslow.wav`.
  - Firing: when the gun's bolt `DUMMY01` direction is within acos(0.9) (≈ 25.8°) of the direction to the enemy:
    if the turn sound was playing it is stopped and `Enemy/MGun/trnstop.wav` is played (channel 2, volume 0.6,
    attenuation 1). Bursts: 3 shots, one per 0.1 s; 0.5 s between bursts; after the sixth burst a 3 s pause. For
    each shot a trace (mask 0x060000B9) to the enemy origin + (0, 0, 20), or if that does not hit a player, to the
    enemy origin, must hit a player; then two attacks of attack-table entry 21 (light bullet, SPEC-782) are fired
    from the gun's bolts `_LEFTFLASH` and `_FLASHRIGHT` along the gun direction, the muzzle effect
    `weapons/othermz/autogun` is put on each of those bolts, and `Weapons/Autoshot/fire.wav` is played (channel 2,
    volume 0.9, attenuation 1). A shot whose trace fails clears the loop sound (the shot still counts).
  - Death: health is first set to irand(5, 20), then the gun death — so the flash of the explosion blinds within
    10 × that health (50–200 units). A spark think (electric arcs, TE 8, length 40) is also scheduled but never runs
    because the break removes the entity (original quirk).
- **misc_test_gun_auto** (developer test): every 2.5 s, starting 2.5 s after spawn, the effect
  `weapons/othermz/autogun` at origin + 27 · forward with direction = forward. It never searches or shoots; death
  = gun death.
- **gun_castle.** With spawnflag 4 and without 32 it wakes at spawn + max(0, `wait`/2 − 5) s; with spawnflag 16
  (and 4) use toggles it: a searching gun stops; a stopped gun wakes after max(0, `wait`/2 − 5) s. Waking plays
  `Enemy/MGun/Wake2.wav` (channel 2, volume 1, attenuation 1) and the search starts 5 s later (no no-target check,
  no acquire sound). Aim point = enemy origin + (0, 0, 30). The shield turns in yaw (step `speed`; limits ±180°, or
  ±`maxyaw` degrees with spawnflag 8), the four guns pitch (step `speed`, limits −18°…+45°, i.e. −0.314159…0.785398
  rad); the right gun on `DUMMY04` and the left gun on `DUMMY05` aim at the aim point moved 20 units along world −x
  (original quirk; which pair gets the offset is UNVERIFIED — compare the two gun pairs' pitch in game).
  When the shield's bolt `DUMMY01` points within acos(0.95) (≈ 18.2°) of the aim point and a trace to the enemy
  origin hits a player, all four guns fire attack-table entry 21 from `_FLASHTOPRIGHT` (right guns) /
  `_FLASHTOPLEFT` (left guns) toward the aim point, each with the muzzle effect on that bolt — every 0.1 s, no
  bursts, no fire sound. Whenever aligned, the right guns play `gunright` and the left guns `gunleft`
  once-and-hold (recoil). The castle gun is indestructible (forced spawnflag 1); its death behaviour (the gun death,
  flash radius ≤ 0) is unreachable in normal play.
- **gun_big.** With spawnflag 4: damageable and the search (as gun_castle, no wake) starts after `wait` · 0.1 s.
  Aiming as gun_castle but with fixed values: step 0.04 rad (2.29°) per 0.1 s, yaw unlimited, pitch −18°…+45°;
  aim point = enemy origin + (0, 0, 30); fires as gun_castle when the direction to the enemy is within acos(0.95)
  of the shield's bolt `DUMMY01` direction; no recoil sequences. Death = gun death.

### 6B. Generic map objects, part 2 (`misc_generic_motorcycle` … `misc_generic_wpn_c4`)

Same conventions as 6A: all classes use model table 2 (Appendix A.3), "G n" = entry n; unless stated otherwise a
class runs the standard prop setup (§3) with the listed entry, box and skin, has no pain/use/touch behaviour and dies
with the table break (§4.2). "Skin k → name" = the skin passed to the setup when the entity's `skin` value is k;
"applied after the setup" = the skin is set on the entry's skin material right after the setup (same visible
result). Box values are before the right-angle rotation (§3 step 5) unless the table says the box is set after the
setup (then it is not rotated). "Forces +n" = spawnflag bit n is set before the setup runs. "Sound (ch, vol, attn)"
as in §1; "TE n" as in SPEC-201/401; "debris (n, s, chunk c, surface t) at p" = one TE 18 at p with zero direction,
n chunks, size class s, chunk type c, zero extents and surface byte t (SPEC-771 states the repeat filter that may
drop a second debris message with the same surface byte within 0.1 s). "Model ×k" = the Ghoul model is scaled
uniformly by k after the setup (box unchanged).

#### 6B.1 Plain props

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_palette_jack` | G62 `palette_jack/pallette_jack` | — | (−29,−10,−20) / (29,10,20) | 1000 | 28 / 11 | table (15 / 1) | 2 | — |
| `misc_generic_paper_blowing` | G63 `paper_blowing/paper` | — | (−8,−8,−2) / (8,8,2) | 25 | 0 / 0 | table (0 / 1) | 2 | — |
| `misc_generic_parking_meter` | G64 `parking_meter/parking_meter` | — | (−1,−4,−21) / (1,4,21) | 500 | 28 / 11 | table (0 / 1) | 2 | forces +2; model file absent in the shipped archives |
| `misc_generic_pillow` | G66 `pillow/pillow` | 1 → `pillow2` (and surface type 27); else `pillow` | (−18,−30,−2) / (18,30,2) | 100 | 21 / 31 | table (4 / 1) | 2 | skin file `pillow.tga` absent (only `pillow2.tga`) |
| `misc_generic_plant_potted` | G67 `plant_potted/plant` | 1 → `plant2`; 2 → `plant3`; 3 → `plant4`; else `plant` | (−33,−42,−18) / (23,27,43) | 200 | 0 / 11 | table (0 / 1) | 2 | skin `plant3` absent |
| `misc_generic_plant_tall` | G68 `plant_tall/plant` | — | (−33,−42,−18) / (23,27,43) | 200 | 0 / 0 | table (0 / 1) | 2 | — |
| `misc_generic_radio` | G71 `radio/radio` | — | (−4,−14,−10) / (4,14,9) | 100 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_shovel` | G74 `shovel/shovel` | 1 → `shovel2`; else `shovel` | (−5,−3,−24) / (5,3,24) | 200 | 28 / 9 | table (0 / 1) | 2 | forces +2; skin `shovel2` absent |
| `misc_generic_shrub` | G75 `shrub1/shrub` | 1 → `shrub2`; else `shrub` | (−20,−20,−13) / (20,20,13) | 200 | 0 / 0 | table (0 / 1) | 2 | forces +2; skin `shrub2` absent |
| `misc_generic_street_name_sign` | G81 `street_name_sign/sign` | 1 → `sign2`; else `sign` | (−12,−12,−36) / (12,12,36) | 500 | 28 / 9 | table (0 / 1) | 2 | forces +2; model file absent in the shipped archives |
| `misc_generic_sword` | G82 `sword/sword` | — | (−23,−2,−2) / (23,2,2) | 150 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_sword_large` | G83 `sword/sword` | — | (−29,−2,−2) / (29,2,2) | 150 | 28 / 9 | table (0 / 1) | 2 | model ×1.3 |
| `misc_generic_table_round` | G84 `table_round/table` | 1 → `table2` (surface 14, material 17); else `table` (surface 28, material 11) | (−27,−23,−16) / (27,23,16), set again after the setup (not rotated) | 400 | per skin | table (0 / 1) | 2 | skin file `table.tga` absent (only `table2.tga`) |
| `misc_generic_urinal_cake` | G107 `urinal_cake/urinal_cake` | — | (−3,−3,−1) / (3,3,1) | 100 | 21 / 15 | table (0 / 4) | 2 | — |
| `misc_generic_urn` | G108 `urn/urn` | — | (−16,−16,−14) / (16,16,14) | 25 | 37 / 23 | table (20 / 1) | 2 | — |
| `misc_generic_web_moss_animated` | G110 `web_moss_animated/web_moss` | 1 → `web2`; 2 → `moss1`; 3 → `moss2`; 4 → `bloodflap1`; else `web1` | (−4,−15,−10) / (−1,15,10) | 150 | 0 / 0 | table (0 / 1) | 0 | forces +1 +2 (never damaged, static); skins `web2`, `moss1`, `moss2` absent |

- Every class of this table behaves exactly as the standard setup and table break describe; nothing else is
  attached. Health/surface/material keys override the defaults as usual (§3 step 4); for the round table the skin
  choice sets surface and material on the entity before the setup, so the entry defaults are never used and the
  `surfacetype`/`material` keys are overwritten by the skin choice.
- The round table's box is written again after the setup, so it does not follow right-angle angles (a table rotated
  90° in yaw keeps the unrotated box; original quirk).
- `misc_generic_web_moss_animated` has solid 0 (not solid); the "animated" part is only the model's default looping
  sequence.

#### 6B.2 Weapon display props

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_wpn_bat` | G112 `Enemy/Bolt/w_bat` | — | (−35,−33,55) / (−31,3,60) | 400 | 15 / 18 | table (0 / 1) | 2 | model file absent in the shipped archives |
| `misc_generic_wpn_desert_eagle` | G113 `Enemy/Bolt/w_desert_eagle` | — | (−42,−12,55) / (−30,1,58) | 1000 | 28 / 9 | table (0 / 1) | 2 | model file absent in the shipped archives |
| `misc_generic_wpn_glock` | G114 `Enemy/Bolt/w_pistol1` | — | (−40,−10,56) / (−31,1,58) | 1000 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_wpn_ingram` | G115 `Enemy/Bolt/w_machinepistol` | — | (−44,−13,55) / (−27,2,59) | 1000 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_wpn_katana` | G116 `Enemy/Bolt/w_katana` | — | (−37,−42,54) / (−30,9,61) | 1000 | 28 / 9 | table (0 / 1) | 2 | model file absent in the shipped archives |
| `misc_generic_wpn_minimi` | G117 `Enemy/Bolt/w_machinegun` | — | (−7,−2,−1) / (29,7,3) | 1000 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_wpn_sig3000` | G118 `Enemy/Bolt/w_sniperrifle` | — | (−60,−21,55) / (−26,8,58) | 1000 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_wpn_spas12` | G119 `Enemy/Bolt/w_shotgun` | — | (−60,−6,55) / (−30,1,58) | 1000 | 28 / 9 | table (0 / 1) | 2 | — |
| `misc_generic_wpn_c4` | G120 `Items/Projectiles/c4` | — | (−4,−2,−4) / (3,2,4) | 1000 | 28 / 9 | table (0 / 1) | 2 | movetype NONE after the setup |

- The weapon models are the character hand-weapon models, whose origin is the hand bolt; the boxes are therefore far
  off the entity origin (about 55 units above it and up to 60 units to −x). They are not items: they cannot be
  picked up (SPEC-781 items are separate classnames).
- `misc_generic_wpn_c4`: spawnflag 4 = "hidden": all parts of the model are hidden at spawn. Use: if spawnflag 4 is
  set, all parts are shown again (the c4 appears); otherwise use does nothing. Movetype is forced to NONE after the
  setup, so the prop never moves even without spawnflag 2 (the player push still sets a velocity that has no effect).

#### 6B.3 Tipping and shoved props

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_motorcycle` | G60 `motorcycle/motorcycle` | `motorcycle` applied after the setup | (−44,−17,−25) / (44,20,23) | 2000 | 28 / 9 | table (0 / 1) | 2 | forces +2; model file absent in the shipped archives |
| `misc_generic_orange_cone` | G61 `orange_cone/orangecone` | `orangecone` applied after the setup | (−6,−6,−13) / (6,6,13) | 500 | 21 / 31 | table (5 / 2) | 2 | — |
| `misc_generic_trash_ashtray` | G98 `trash_ashtray/ashtray` | — | (−5,−4,−9) / (5,4,9) | 100 | 28 / 9 | table (0 / 1) | 2 | precaches sound-list entries 34, 35, 36 (`Weapons/FX/Rics/ric1..3.wav`) |
| `misc_generic_trashcan_barrel` | G99 `trashcan_barrel/trashcan` | — | (−15,−16,−17) / (15,16,22) | 200 | 28 / 9 | table (0 / 1) | 2 | death: garbage-bag break |
| `misc_generic_waste_basket_wire` | G109 `waste_basket/waste_basket` | 1 → `waste_basket2`; 2 → `waste_basket3`; 3 → `waste_basket4`; else `waste_basket` | (−6,−7,−7) / (6,7,7) | 150 | 28 / 9 | table (0 / 1) | 2 | skins `waste_basket3`, `waste_basket4` absent |
| `misc_generic_wet_floor_sign` | G111 `wet_floor_sign/sign` | — | (−9,−8,−8) / (9,8,8) | 150 | 0 / 0 | table (0 / 1) | 2 | — |
| `misc_generic_sofa` | G77 `sofa/sofa` | 1 → `sofa2`; 2 → `sofa3`; else `sofa` | (−17,−36,−17) / (17,36,17) | 2000 | 0 / 0 | table (0 / 1) | 2 | ground friction 1 |
| `misc_generic_sofa_chair` | G78 `sofa_chair/chair` | 1 → `chair2`; 2 → `chair2a`; else `chair` | (−17,−16,−17) / (17,16,17) | 1000 | 0 / 0 | table (0 / 1) | 2 | forces +2; ground friction 1; skins `chair2`, `chair2a` absent |

In this family d = normalize(prop origin − attacker origin) ("attacker" = the `other` entity handed to the pain
behaviour), and "the knock-over rates" are the pitch and roll rates of §5.2 (70·d.x and −70·d.y degrees per second
for props). A "tumble (L, f)" is: every 0.1 s, while both |pitch| and |roll| are below L, the pitch and roll rates are
multiplied by f; as soon as either reaches ±L, every angle beyond ±L is clamped to ±L, the angular velocity becomes 0
and the think stops.

- **Motorcycle.** Every pain sets the skin `damaged` on material `motorcycle` (no movement; the prop is static).
- **Orange cone.** First pain only (the pain behaviour is then removed): knock-over rates, velocity (0, 0, 250)
  (no horizontal push, elasticity unchanged), skin `damaged`, then a tumble (100, 1.5) that also sets health to 1 on
  every step while it is still turning (so any further damage destroys it). When the tumble ends the cone becomes
  solid 2 with the box (−7,−7,−7) / (7,7,7).
- **Wet floor sign.** First pain only: knock-over rates, velocity (0, 0, 160); tumble with f = 1.5 and separate
  limits, 128° for pitch and 90° for roll (each angle clamped to its own limit); health set to 1 on every turning step.
- **Trash can barrel.** First pain only: knock-over rates, velocity (0, 0, 120); tumble (85, 1.9); health is not
  changed. Death: the garbage-bag break: paper burst TE 48 (count 0 → the client picks 5–10 sheets) at the origin,
  soda-can debris TE 50 at the origin with direction normalize(attacker origin − prop origin) (toward the attacker;
  the inflictor is used when there is no attacker, (0, 0, 1) when there is neither), speed 100, count 0 (the client
  picks 1–4), then the table break.
- **Ash tray.** First pain: ricochet sound TE 54 at the origin; throw away (§5.3) with damage' = trunc(0.03 ·
  damage); then knock over (§5.2, k = 6, with the full damage), which replaces the velocity, so the net result is
  velocity = d · damage · 6 with vz = 150, elasticity 0.7, the knock-over rates, yaw rate rand(4, 6) · damage' and
  the shared tumble of §5.2. Every later pain: TE 54 at the origin and throw away with spin on the yaw axis with
  damage' = trunc(0.3 · damage): velocity = d · rand(10, 15) · damage', yaw rate = 200 · d.y.
- **Wire waste basket.** As the ash tray without the ricochet sound and with damage' = trunc(0.6 · damage) for both
  the first throw (yaw rate) and every later spin throw.
- **Sofa, sofa chair.** Every pain: health is reset to 10000 (practically indestructible: only a single hit of more
  than 10000 destroys it); velocity += d · 2 · damage, then vz = 20 (the sofa slides; the sofa chair is static, so
  nothing moves); the hit point is moved by an independent random amount (one of −1.0, −0.9, …, +1.0) on each axis
  and a TE 1 is sent there (TE 1 draws nothing on the client, SPEC-401).

#### 6B.4 Bathroom fixtures

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_sink` | G76 `sink/sink` | 2 → `sink`; else `sinkclean` (applied after the setup) | (−11,−12,−6) / (11,12,7) | 200 | 17 / 15 | table (0 / 1) | 2 | forces +2; entity effects bit 0x200; precaches `Ambient/Gen/WtrFX/SprayBig.wav`, `Impact/Break/Ceramic.wav`, effect `environ/k_waterspout3` |
| `misc_generic_toilet` | G96 `toilet/toilet` | 1 → `toiletclean`; else `toilet` (applied after the setup) | (−14,−9,−16) / (14,9,16) | 200 | 21 / 31 | table (0 / 2) | 2 | movetype NONE after the setup (not pushable); precaches `Ambient/Gen/WtrFX/SprayBig.wav`, effect `environ/toiletspout` |
| `misc_generic_urinal` | G106 `urinal/urinal` | 1 → `urinal2`; else `urinal` (applied after the setup) | (−6,−9,−11) / (9,9,14) | 200 | 21 / 31 | table (0 / 1) | 2 | forces +2; renderfx bit 28 (2²⁸) set; precaches `Ambient/Models/Urinal/Flush.wav`, `Ambient/Gen/WtrFX/SprayBig.wav`, effect `environ/k_waterspout3` |

**Water spray.** The deaths below create a *spray*: a new non-solid, invisible entity at the fixture's origin with
the fixture's angles and the loop sound `Ambient/Gen/WtrFX/SprayBig.wav` (§1).
- Sink and urinal spray: 0.1 s after creation it starts the effect `environ/k_waterspout3` at its position, aimed
  along the fixture's backward direction (−forward of its angles); it is removed 3 s after that (the loop sound is
  heard for about 3.1 s).
- Toilet spray: the effect `environ/toiletspout` is started at the toilet origin at once (orientation as for an
  effect without a direction, SPEC-771; UNVERIFIED — check the spout direction in game); the spray entity is removed
  after 3 s.

- **Sink.** First pain only: `Impact/Break/Ceramic.wav` (2, 0.6, 1); debris (5, 1, chunk 0, surface 0) at the origin;
  skin `damaged` if the skin value is 2, else `sinkclean2d`. Death (replaces the standard break: no targets fire, no
  `message`, no debris at that moment): a sink spray; a *broken sink* entity is created — box (−10,−11,−5) /
  (10,11,6), standard setup with G76 and skin `damaged` (spawnflags 0, so it is damageable and has physics), health
  200, entity effects bit 0x200, debris material and surface type copied from the sink, origin = sink origin +
  5 · forward, the sink's angles — and the sink is removed. Touch of the broken sink (by any entity, UNVERIFIED
  whether landing on the world counts): `Impact/Break/Ceramic.wav` (2, 0.6, 1), debris (8, 1, chunk 0, surface 0) at
  its origin, and it is removed. Shot to death it does the table break of G76.
- **Toilet.** Every pain: skin `damaged`, health = 1 (the next damage destroys it), and an idle think that only
  re-schedules itself every 0.0–0.4 s starts 0.5 s later (no visible effect). Death: a toilet spray, then the table
  break with the toilet itself as attacker (debris direction = normalize(toilet origin − box centre), the zero vector
  for a centred box; a `message` reaches no player; targets fire with the toilet as activator).
- **Urinal.** Use and the player use key both play `Ambient/Models/Urinal/Flush.wav` (4, 1, 1) (repeatable). Pain:
  only when the pain timer has run out (initially it has): skin `damaged`, the toilet's idle think starts 0.5 s later,
  and the timer is set to now + rand(6, 12) s. Death: a urinal spray; 0.1 s later the table break with the urinal
  itself as attacker (as the toilet). Until then the urinal still exists and can be damaged again (each new death
  creates another spray, UNVERIFIED in game).

#### 6B.5 Electronics and signals

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_phone_booth` | G65 `phone_booth/phone_booth` | — | (−10,−27,−19) / (10,27,19) | 1000 | 28 / 11 | table (0 / 1) | 2 | forces +2 |
| `misc_generic_radar_dish` | G69 `radar/radar_base` | — | (−64,−64,−15) / (64,64,70) | 5000 | 28 / 9 | table (0 / 1) | 2 | bolt-on G70 `radar/radar_dish` on bolt `DISH_BOLT` |
| `misc_generic_security_camera` | G73 `security_camera/test/arm` | — (only part `camera` shown) | (−8,−2,−4) / (8,2,24) | 200 | 28 / 9 | table (10 / 1) | 2 | forces +2; bolt-on G72 `camera` on bolt `DUMMY01`; model ×2; precaches `Ambient/Models/Camera/Alarm.wav`, `Ambient/Models/Camera/MoveLP.wav` |
| `misc_generic_security_camera2` | G72 `security_camera/test/camera` | — (only part `camera` shown) | (−8,−2,−4) / (8,2,4) | 200 | 28 / 9 | table (10 / 1) | 2 | forces +2; model ×2; precaches as above |
| `misc_generic_speaker` | G79 `speaker/speaker` | — | (−5,−5,−12) / (5,5,12) | 100 | 28 / 9 | table (0 / 1) | 2 | forces +2; spawnflag 4: part `_ARM01` hidden and mins z = −5; precaches sound-list entry 91 (`Impact/Explosion/Small.wav`) |
| `misc_generic_stoplight` | G80 `stoplight/stoplight` | skin value ≠ 0: `stoplight_d` (broken, no cycle) | (−6,−5,−44) / (9,4,44) | 2000 | 28 / 9 | table (0 / 1) | 2 | forces +2; model file absent in the shipped archives |
| `misc_generic_tv_ceiling` | G104 `tv_ceiling/supports` | — (`style` 1: screen skin `tv_br0`) | (−18,−19,−22) / (19,19,28) | 300 | 28 / 9 | standard break (computed) | 2 | forces +2; bolt-on G103 `tv_ceiling/tv` (the screen) on `DUMMY01`; model ×1.5; precaches list entries 90, 91, `Impact/Glassbreak/Monitor.wav`, `Ambient/Gen/Electric/buzz.wav`, `Ambient/Models/Monitor/Static.wav`, effect `environ/normalsmoke`; sets material-flag characters 1 and 4 |
| `misc_generic_tv_wall` | G105 `tv_wall/tv_wall` | `tv_wall` applied after the setup | (−5,−9,−9) / (6,9,7) | 300 | 28 / 9 | standard break (computed) | 2 | forces +2; model ×1.5; precaches list entries 90, 91, `Impact/Glassbreak/Monitor.wav`, `Ambient/Gen/Electric/buzz.wav`, effect `environ/normalsmoke`; sets material-flag characters 1 and 4 |

- **Phone booth.** Every pain: TE 58 at the origin (the client plays sound 92 `Phone/Hit`, SPEC-401).
- **Radar dish.** At spawn the base model is paused (it never animates) and the dish is bolted on. The dish turns on
  the clients through the entity-state "rotate" effects bit 0x0001 (SPEC-400 *Entity effects bits*): the four effect
  slots carry v0 = 999, angular velocity (v1, v2, v3) = 512 × (0, 1.5, 0) = (0, 768, 0) and v4 = the dish
  instance's handle (v5–v7 = 0), so each client turns the dish instance about its yaw axis by 1.5 units per second
  (SPEC-400 incremental branch; the unit is UNVERIFIED there). The server never changes the entity's angles.
- **Security cameras** (both classes). Before the setup:
  - Spawnflag 4 = "pan". Keys `speed` = degrees per 0.1 s (0 → 5) and `count` = half-range in degrees (0 → 90).
    With n = trunc(2 · count / speed), the camera starts a counter at trunc(n / 2) and every 0.1 s adds `speed` to
    its yaw and decrements the counter; when the counter reaches 0 the direction is reversed (`speed` negated) and
    the counter restarts at n. Net: it first turns trunc(n / 2) · `speed` degrees to positive yaw, then swings
    n · `speed` degrees back and forth (with the defaults: ±90° around the spawn yaw; odd n makes the swing
    asymmetric). While panning it has the loop sound
    `Ambient/Models/Camera/MoveLP.wav`.
  - Spawnflag 8 = "alarm". Key `attenuation` (re-used as the detection half-angle in degrees; 0 → 30). The camera
    looks for the first entity with classname `player` once per second (for panning cameras this test is part of
    the pan step and runs at most once per second; for fixed cameras a think repeats every 1 s, starting 1 s after
    spawn). The player is seen when a trace with the camera's clip mask from the camera origin to the player's
    origin (ignoring both) is not blocked and the angle between the camera's forward direction and the direction to
    the player, both flattened to the horizontal plane, is at most the half-angle (dot ≥ cos(half-angle); a debug
    line `Threshold %f  Dot %f` is printed to the console at each test). When seen: `Ambient/Models/Camera/Alarm.wav`
    (2, 0.6, 1) and the targets fire with the player as activator — every second as long as the player stays seen.
  - Spawnflag 2 is always set (static).
  The first camera shows only part `camera` of its arm model and carries G72 on bolt `DUMMY01`, whose sequence
  `camera` loops; the second shows only part `camera` of G72 and loops sequence `camera` on itself. Both are drawn
  at twice the size. Death: table break (10 chunks, size 1).
- **Speaker.** Death (the speaker is not removed and its targets do not fire): it becomes a physics object (movetype
  10, gravity 1); velocity += d · 5 · damage with d = normalize(speaker origin − inflictor origin), then vz = 0; yaw
  rate = ±(r + 100 + damage) with r a random integer 0…99 and a random sign; sparks TE 0 at the origin along d; small explosion
  TE 46 at the origin with size byte 10 and sound byte 1; the origin moves 10 units along the speaker's forward;
  sequence `speaker_down` once-and-hold; it stops taking damage; the attacker is remembered as its owner; without
  spawnflag 4 mins z becomes −5. Then a spark think runs at once and repeats while its counter (a random integer 0–9, raised to
  4 when lower) is positive: each step decrements the counter, sends sparks TE 0 at origin + 14 · forward +
  4 · right with direction (0, 0, 1), and does linear area damage 20 within radius 10 from the speaker, attacker =
  the remembered owner, means of death 26 (explosive), no flags; the next step follows after 0.1 + 0.1 · (0…29) s.
- **Stop light.** Skin value 0: sequence `yellow` once-and-hold, then every 5 s the next of `blue`, `red`, `yellow`
  (cycle yellow → blue → red → yellow …, each once-and-hold). Pain: sequence `stoplight` once-and-hold, skin
  `stoplight_d`, the cycle stops (repeated on every pain). Skin value ≠ 0: skin `stoplight_d` at spawn, no cycle, no
  pain behaviour. (The model is missing in 1.07f, so nothing of this is visible.)
- **Wall TV.** `style` 0: parts `FACE2`, `FACE2B`, `FACE2C`, `FACE2D` are hidden (the normal screen shows); `style` ≠ 0:
  parts `FACE` and `WORLD_OMNI` are hidden and the loop sound `ambient/models/Panel/Panel1.wav` plays. A think after
  0.1 · (0…4) s loops sequence `tv_wall`.
  - First pain, and every use: it becomes a physics object (movetype 10; spawnflag 2 is toggled, so it falls off the
    wall); `style` 0 hides `FACE` and `WORLD_OMNI`, else hides `FACE2`…`FACE2D` and stops the loop sound; skin
    `tv_wall_d`; at the origin: two sparks TE 10 with directions (i, j, 0), i, j independent integers in −10…10,
    one TE 10 with (i, j, irand(5, 10)), i, j in −20…20; debris (3, 1, chunk 9, surface 28) and (5, 1, chunk 7,
    surface 37); small explosion TE 46 size 100 without sound; `Impact/Glassbreak/Monitor.wav` (2, 0.6, 1); loop sound
    `Ambient/Gen/Electric/buzz.wav`; from then on a broken-TV think runs every 0.1 s (no visible effect for this
    class) and further pains only send two TE 10 sparks (directions as above) at the origin. Use keeps the first-pain
    behaviour, so every use repeats the whole breaking sequence.
  - Death (both TV classes): at the origin two TE 10 sparks (−10…10, −10…10, 0) and one (−20…20, −20…20,
    irand(5, 10)); debris (5, 1, chunk 7, surface 37) with an undefined direction (not initialised in the original;
    use zero); medium explosion TE 55 size 50 with sound byte 1; then the standard break with computed values.
- **Ceiling TV.** A think 0.1 + 0.1 · (0…4) s after spawn loops sequence `tv` on the screen and starts the loop
  sound `Ambient/Models/Monitor/Static.wav`.
  - First pain, and every use: parts `FACE` and `WORLD_LIGHT` of the screen are hidden; at q = origin + 10 · forward:
    the same three TE 10 sparks as the wall TV, debris (3, 1, chunk 9, surface 28) and (5, 1, chunk 7, surface 37),
    TE 46 size 100 without sound; `Impact/Glassbreak/Monitor.wav` (2, 0.6, 1); loop sound
    `Ambient/Gen/Electric/buzz.wav` (replaces the static). The broken-TV think then runs every 0.1 s: while the
    screen's bolt `face` does not point down (its world direction has −z < 0.9) the screen bolt-on is turned by 0.3 rad
    about its local z axis per step (it swings until the face points down); once it points down the TV is marked
    "hanging".
  - Later pains: two TE 10 sparks at the origin; if the TV is marked hanging: parts `support1` and `support2` of the
    main model are hidden, spawnflag 2 is toggled and movetype 10 set (the TV drops), debris (3, 1, chunk 9, surface
    28) at the origin, the mark is cleared, mins z += 9, and from now on pains only send the two sparks and a touch is
    armed: on the first touch of anything, one TE 10 spark (−10…10, −10…10, 0) at the origin, debris (3, 1, chunk 9,
    surface 32) and (5, 1, chunk 7, surface 38), small explosion TE 46 size 200 with sound byte 1, the screen's bolt
    transform is replaced by a pure rotation of 1.58 rad about its z axis, the touch is removed and spawnflag 2 is
    set.
  - `style` 1 sets the screen skin `tv_br0` at spawn.
  - Death: as the wall TV.
  The electric buzz started by the first pain stays on until the TV is removed (the precached `environ/normalsmoke`
  effect is never shown by these two classes in 1.07f).

#### 6B.6 Tables with shootable legs

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_table1` | G85 `table1/top` | 1 → `table1A` (surface 28, material 11); 2 → `table1B` (14 / 17); 3 → `table1C` (28 / 11); else `table1` (14 / 17) | (−41,−24,−18) / (41,24,10), set after the setup (not rotated) | 400 | per skin | table (0 / 1) | 2 | bolt-ons 1–4: G87 `leg` on `DUMMY01`…`DUMMY04` (child bolt `DUMMY01`); 5: G86 `back` on `DUMMY05` (child bolt `DUMMY05`); all with the table skin; skin files `table1A/B/C` absent |
| `misc_generic_table2` | G88 `table2/top01` | — | (−56,−98,−26) / (39,24,4) | 1000 | 28 / 9 | table (0 / 1) | 2 | bolt-ons 1: G89 `side01` on `DUMMY08`; 2: G90 `side02` on `DUMMY09`; 3: G91 `side03` on `DUMMY10`; 4: G89 `side01` on `DUMMY07` (child bolt `DUMMY08`), turned −90° about its x axis; 5: G92 `top02` on `DUMMY12`; key `scale`: 0 → 0.8, above 0.2 → `scale` − 0.2 (applied by the setup, §3 step 7) |

Both tables are shootable-part props (§5.7): their model-table entry receives a part table at spawn.

| table | part table (bolt → bolt-on index) | reaction |
|---|---|---|
| `misc_generic_table1` | `DUMMY01` → 1, `DUMMY02` → 2, `DUMMY03` → 3, `DUMMY04` → 4, `DUMMY05` → 5 | table 1 leg rule |
| `misc_generic_table2` | `DUMMY08` → 1, `DUMMY09` → 2, `DUMMY10` → 3, `DUMMY07` → 4 (the top, 5, cannot be shot off) | table 2 leg rule |

Pain = shootable parts (§5.7). The saved spawn angles (pitch₀, roll₀) are those at spawn. "Tilted box" for table 1 =
(−41,−24,−7) / (41,24,16) (the entity is relinked). In the rules, a part "is gone" when it has been shot off (its
shown bit is clear; the part just hit is already gone when the rule runs). Only the rules listed for the removed part
are evaluated, in the order given; the first one whose condition holds acts and the rest are skipped.

**Table 1 leg rule** (legs L1…L4 = bolt-ons 1–4, back B = bolt-on 5):
1. If three or four legs are still present nothing else happens (in particular no debris).
2. Otherwise debris (5, 1, the table's debris material, its surface type) and a smoke puff TE 45 (120, 120, 120, 200)
   at the bolt position of the removed part.
3. If all four legs and the back are gone: pitch = pitch₀, roll = roll₀, box (−41,−24,10) / (41,24,15), spawnflag 2
   cleared; done.
4. By the removed part:
   - L1: (a) L2 gone → roll += 30, pitch = pitch₀, tilted box. (b) else, if L3 and B are gone and roll = roll₀ →
     pitch −= 20, tilted box.
   - L2: (a) L1 gone → roll += 30, pitch = pitch₀, tilted box. (b) else, if L4 and B are gone and roll = roll₀ →
     pitch += 20, tilted box.
   - L3: (a) L1 and B gone → if roll = roll₀: pitch −= 20, tilted box (else nothing). (b) else, if L1 present and L4
     and B gone → roll −= 30, pitch = pitch₀, tilted box.
   - L4: (a) B present → nothing. (b) L2 present → if L3 gone: roll −= 30, pitch = pitch₀, tilted box. (c) L2 gone →
     if roll = roll₀: pitch += 20, tilted box.
   - B: (a) L1 and L3 gone → if roll = roll₀: pitch −= 20, tilted box. (b) else L2 and L4 gone → if roll = roll₀:
     pitch += 20, tilted box. (c) else L3 and L4 gone → roll −= 30, pitch = pitch₀, tilted box.
   Geometrically: losing both legs of the L1/L2 side tips the table +30° in roll, both of the L3/L4 side −30° in roll
   (only without the back), both of the L1/L3 end −20° in pitch, both of the L2/L4 end +20° in pitch (pitch only
   without the back and while the roll is untouched).

Table 1 with spawnflag 4 ("can be pushed over"): the player use key makes it fall over (§5.8).

**Table 2 leg rule** (P1…P4 = bolt-ons 1–4, top T = bolt-on 5):
1. Always: debris (5, 1, debris material, surface type) and the grey puff TE 45 at the removed part's bolt position.
2. If P1…P4 are all gone: pitch = pitch₀, roll = roll₀, box (−64,−106,−1) / (47,32,4), relinked; done.
3. By the removed part:
   - P1: (a) P2 gone → if P3 gone and P4 present: pitch += 18 (box unchanged, not relinked). (b) P2 present → if P3
     gone and P4 present: T is turned by −10.8° (−0.18850 rad) about its local y axis and pitch += 8.
   - P2: if P1 present, P3 gone and P4 present → pitch −= 12, roll −= 12, box (−64,−106,−16) / (47,32,4), relinked.
   - P3: (a) P1 gone → if P2 and P4 present: T turned −10.8° about its y axis and pitch += 8. (b) P1 present → if P2
     gone and P4 present: pitch −= 12, roll −= 12, box (−64,−106,−16) / (47,32,4), relinked.
   - P4: if P2 gone and P1 and P3 present → roll += 18.

**Objects lying on a table.** Touch of either table by an entity that is not a player, has no AI and is not the world
links that entity to the table: it is appended to the end of the table's chain of linked objects unless it is already
in it. The chain can only continue through entities that have prop object data; when the walk reaches an entity
without it, the toucher is not added (so after one non-prop object no further objects are linked; original quirk).
When the table is hurt, every linked object is thrown off as §5.7 describes (velocity normalize(its origin − table
origin) · rand(75, 125), vz + 50, yaw + rand(−20, 20), origin z + 15) and the chain is cleared — this happens on
every pain of the table, also when the shot hits no part. These two tables are the only generic classes that create
such links.

#### 6B.7 Gas tanks

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_tank_gas` | G94 `tank_gas/gas` | — | (−58,−23,−27) / (58,23,19) | 8 | 28 / 9 | table (0 / 3) | 2 | precaches sound-list entry 58 (`weapons/C4/C4Exp.wav`), effects `weapons/world/airexplode`, `environ/onfireburst` |
| `misc_generic_tank_propane` | G95 `tank_propane/propane` | — | (−3,−3,−25) / (3,3,25) | 8 | 28 / 9 | standard break (computed) | 2 | precaches sound-list entry 58, effect `environ/onfireburst` |

- **Gas tank.** Death: pain and death behaviours are removed first (no re-entry from its own blast), then the
  standard prop explosion (§5.4) with ignite and flash (flash origin = the tank origin; at death the health is ≤ 0,
  so nobody is blinded), then the table break (computed count, size 3). Use: the same explosion and table break as if
  the activator had killed it with damage 100 at the tank origin; because the health is still positive, the flash
  blinds within 10 × health units (80 with the default health 8).
- **Propane tank.** Death: behaviours removed, the standard prop explosion with ignite and flash, then — if the
  entity has any spawnflag set — it is simply removed (no debris, no targets, no `message`); with spawnflags 0 the
  standard break with computed values. No use behaviour.

#### 6B.8 Shattering props

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_tree_dead` | G100 `tree_dead/tree` | — | (−43,−43,−41) / (40,67,110) | 1000 | 15 / 18 | — (shatter) | 2 | forces +2; model file absent in the shipped archives |
| `misc_generic_tree_thin` | G101 `tree_thin/tree` | — | (−26,−15,−66) / (25,16,96) | 500 | 15 / 18 | — (shatter) | 2 | forces +2; model file absent in the shipped archives |
| `misc_generic_truckcab` | G102 `truckcab/truckcab` | — | (−91,−40,−49) / (91,40,49) | 3000 | 28 / 9 | — (shatter) | 2 | forces +2 |

Death of all three: the entity effects bit 0x400 ("shatter", SPEC-400 *Debris* item 6: the client turns every flagged
model part into a falling debris piece) is set and the entity is removed 0.2 s later. No debris temp entity, no
targets, no `message`.

#### 6B.9 Subway train (`misc_generic_train`)

| classname | model | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_generic_train` | G97 `train_generic/train` | 1 → `train2`; 2 → `train3`; 3 → `train4`; else `train` (re-applied after the setup) | (−54,−164,−60) / (55,168,60) | 9000 | 28 / 9 | — | 2 | never damageable; the setup runs as if spawnflags were exactly 2 (static, no push) and the mapper's spawnflags are restored afterwards; precaches `Ambient/Locs/Subway/Train/Move.wav`, `…/Horn2.wav`, `…/Impact.wav` |

Spawn: skin values 1–3 show only part `TRAIN` and hide `WORLD_OMNI`; skin 0 attaches two G58 `beam` bolt-ons on
bolts `headlight1` and `headlight2` (child bolt `to_headlight`). Keys: `style` = top speed in units/s (0 → 600),
`accel` = speed added per 0.1 s and also the starting speed (0 → 100), `wait` = respawn delay.

Spawnflags: 1 = triggered start, 2 = respawn after stopping, 4 = start by itself 0.1 s after spawn, 8 = brake
smoothly, 16 = remove after stopping. With spawnflag 1 or 4 the train is movable: use starts it, movetype 10
(SPEC-760 B4.7), origin raised by 1, elasticity −1 (velocity kept on impacts), entity flag 0x800 (no knockback), the
touch below. With spawnflag 2 the delayed-respawn record of SPEC-760 B8.5 is made with this class, delay `wait` and
the spawn origin.

Let ψ = yaw and m = (−sin ψ, cos ψ, 0) (the train runs along its own +y axis). States, each run every 0.1 s and each
beginning with the riders rule (§5.9):
1. **Start** (use, or the spawnflag-4 auto start): trace from origin + (0, 0, 40) to origin + 8000·m + (0, 0, 40)
   with the train's clip mask; the stop point S = origin + 8000 · fraction · m (horizontal). Ground friction 0,
   gravity 0, velocity = accel · m, use removed, loop sound `Ambient/Locs/Subway/Train/Move.wav` with sound data 0xFB
   (full volume, attenuation class 3, SPEC-202); with probability 1/3 (irand(1, 3) = 1) the horn
   `Ambient/Locs/Subway/Train/Horn2.wav` (4, 1, 1). Then the stop check; next state Accelerate.
2. **Accelerate**: if |v_xy|² ≥ style² the next state is Cruise; else v_xy += accel · m. With probability 1/6 a
   sparks TE 0 at origin + a · forward + b · left + (0, 0, 15) with a ∈ {54, −54, −162}, b ∈ {−105, 105, 315} (each
   chosen with equal chance; the uneven sets are an original quirk) and an undefined direction (use zero). Then the
   stop check.
3. **Cruise**: stop check; with probability 1/7 sparks TE 0 at (origin x ± 52, origin y ± 80, origin z − 55) in world
   axes (signs random), undefined direction.
4. **Stop check**: r = |S − origin|_xy − 167 (distance left for the front of the train), s = 0.1 · |v_xy|. If the
   velocity is exactly zero → next state Stopped. Without spawnflag 8: if r < s: v_xy ×= r / s, vz = 0, next state
   Stopped, and the Move loop sound is turned off unless spawnflag 2 or 16 is set. With spawnflag 8: if r < 5 s:
   v_xy ×= r / (6 s), vz = 0, next state Brake.
5. **Brake**: V = |v_xy|, s = 0.1 V; if V < 20: v_xy ×= r / s, next state Stopped; else v_xy ×= r / (6 s) (each frame
   covers a sixth of the remaining distance); vz = 0.
6. **Stopped**: velocity 0, loop sound off, no further thinking. Spawnflag 2: the delayed respawn is triggered and the
   train removed; else spawnflag 16: the train is removed.

Touch (movable trains only): a toucher that is not a player, cannot take damage and is not the world is removed at
once. Otherwise, while the train moves and the toucher's lowest point (origin z + mins z) is below the train's top
(origin z + maxs z): P = origin + 165 · right (train axes), u = normalize(toucher origin − P); if |u · forward| < 0.25
(the toucher is near the train's long axis, i.e. at one of its ends): a character toucher's AI is notified
(UNVERIFIED meaning), `Ambient/Locs/Subway/Train/Impact.wav` (2, 1, 1) on the train, and damage to the toucher:
inflictor = attacker = the train, point = origin value = the toucher's origin, direction undefined, damage 9000,
knockback 1000, flags EXPLOSIVE | NO_KNOCKBACK (0x8008), means of death 22 (crushed). Otherwise a console line
`Failed fDot test %f` is printed and nothing happens.

### 7A. Level prop families: Kosovo/Bosnia, castle, Iraq

Model numbers refer to Appendix A (table 0 Kosovo/Bosnia, table 1 castle, table 3 Iraq). Unless a class says
otherwise it runs the standard prop setup (§3) with the listed entry, the listed skin and no "only part", and dies by
the table break (§4.2). "Forces +2" = spawnflag 2 is added before the setup (static, no player push); "forces +1" =
not damageable. Boxes are set before the setup and then rotated by it (§3 step 5). Health, surface type, debris
material and solid are the table defaults unless the map sets them; "data no" means the model file is absent from
the shipped archives (the prop has its box but no visible model; UNVERIFIED in game).

Two entity-state mechanisms of SPEC-771 §C.4 are used by the vehicle and globe classes: **effect slots** (an effect
added to / removed from one of the four effect slots of the entity state; bolt 0 = the entity origin, scale 0 =
default) and the **spin record** (the "rotation" state of SPEC-771 §C.4: effects bit 0x0001, v0 = 999, v1..v3 =
trunc(512 × angular rate) per axis, v4..v7 = up to four Ghoul instances to turn). Creating a record clears all
twelve values first; when a record already exists the classes below overwrite only one of v1..v3; "clear the
record" removes bit 0x0001 and zeroes the values.

#### Kosovo/Bosnia (`misc_bosnia_*`)

| classname | model (table 0) | skins (skinnum → skin) | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_bosnia_antipersonnel_jack` | 0 `antipersonnel_jack/jack` (data no) | — | (−15, −17, −10) / (15, 13, 16) | 500 | 15 / 18 | computed | 2 | own touch (spikes) |
| `misc_bosnia_flag_wall` | 1 `flag_wall/flag_wall` (data no) | — | (−45, −18, −25) / (−39, 42, 77) | 500 | 15 / 18 | computed | 2 | — |
| `misc_bosnia_landmine` | 2 `landmine/landmine` (data no) | — | (−13, −13, −2) / (13, 13, 2) | 1 | 28 / 12 | computed | 2 | own touch and death; precaches effect `environ/onfireburst` |
| `misc_bosnia_rubble1` | 3 `rubble1/rubble1` (data no) | — | (−13, −10, −5) / (13, 10, 5) | 1000 | 17 / 15 | computed | 2 | — |
| `misc_bosnia_rubble2` | 4 `rubble2/rubble2` (data no) | — | (−31, −30, −9) / (45, 22, 36) | 1000 | 0 / 16 | computed | 2 | forces +2 |
| `misc_bosnia_searchbeam` | 6 `searchbeam/beam` | — | (−8, −8, −8) / (8, 8, 8) | 1000 | 0 / 0 | none (material 0) | 0 | forces +2; sequence `beam` looped |
| `misc_bosnia_searchlight` | 7 `searchlight/base` + parts 8 `arm`, 9 `lamp` | — | (−40, −40, −8) / (40, 40, 100) | 1000 | 28 / 9 | computed | 2 | no player push; own pain; precaches effect `environ/tv_smoke` |
| `misc_bosnia_tank_jack` | 10 `tank_jack/jack` (data no) | — | (−28, −28, −21) / (28, 28, 21) | 500 | 28 / 9 | computed | 2 | own touch (spikes, players only) |
| `misc_bosnia_truck_chunks` | 11 `truck_chunks/chunks` (data no) | 0 → `chunks`, 1 → `chunks_red`, 2 → `chunks_tan` | (−15, −18, −27) / (15, 18, 27) | 500 | 28 / 9 | computed | 2 | only one part shown, by key `style` |
| `misc_bosnia_truck_old` | 12 `truck_old/truck` + 4 × part 13 `tire` | 0 → `truck`, 1 → `truck2`, 2 → `truck3` | (−96, −39, −45) / (96, 39, 45) | 1000 | 28 / 9 | — (no break) | 2 | forces +2; own death; precaches effect `environ/normalsmoke`, sound `impact/explosion/med.wav` |

Directories are under `objects/bosnia/`. "computed" = the entry has debris count 0, so the table break uses the
computed count (§4.1).

- **Spike jacks.** `misc_bosnia_antipersonnel_jack` hurts any entity touching it whose bottom (origin z + mins z) is
  higher than 2 units below the jack's top (origin z + maxs z), i.e. something standing on it. `misc_bosnia_tank_jack`
  hurts only players, and only while the player's movement speed (all three axes) exceeds 62.5 units/s (pmove
  velocity, in 1/8 units, squared > 250 000). Each touch (every frame of contact): damage 1, knockback 3, damage
  flags 0x400, means of death 26, inflictor = attacker = the jack, direction = toucher origin − jack origin, point =
  the toucher's (x, y, bottom z), second point = the jack origin. Their own touch replaces the player push.
- **Landmine.** Touch by any entity, or death: standard prop explosion with ignite and flash (§5.4), a scorch decal
  (TE 15, SPEC-401) under the mine origin, then the standard break with computed debris (attacker = the toucher, or
  the killer). Because the mine still has its health when touched, the flash radius on touch is 10 × health
  (10 units with the table health 1); when it is shot to death the radius is ≤ 0 (no flash). The touch also runs on
  impacts of the mine's own physics (SPEC-760 B4.8), so a mine without spawnflag 2 that falls onto the floor
  explodes on landing; one placed resting on the floor does not (UNVERIFIED in game).
- **Search beam.** Static looping `beam` model. A frame after spawning it checks for attached parts, finds none and
  never thinks again (no motion; UNVERIFIED in game).
- **Search light.** The base (entry 7) carries the arm (entry 8) on its bolt `DUMMY01` (arm bolt `DUMMY01` aligned)
  and the lamp (entry 9) on the arm's bolt `DUMMY02` (lamp bolt `DUMMY02` aligned). With spawnflag 4 or 8 the arm plays
  its sequence `arm` in loop mode. The light never aims or sweeps (spawnflags 4/8 only start that animation).
  Maximum health = health after setup (1000 by default). Pain: the first time health is ≤ trunc(0.85 × maximum
  health) and the lamp exists, the lamp's material `glass` gets skin `bkglass` (skipped when neither is found),
  the lamp parts `_BEAM` and `WORLD_OMNI` are hidden, a small explosion (TE 46, size 100, sound flag 0) is sent at
  origin + (0, 0, 80), an effect slot `environ/tv_smoke` is added at the entity origin, and from then on every further
  pain only sends that small explosion again. Death: table break (computed debris).
- **Truck chunks.** Shows only one mesh part, chosen by key `style`: 1 `_DOOR`, 2 `_PANEL`, 3 `_TIRE`, any other
  value `_GRILL`. Physics/push as the standard setup (no forced flags).
- **Old truck** (`misc_bosnia_truck_old`). Four tire parts (entry 13) on the truck bolts `DUMMY01`…`DUMMY04` (tire
  bolt `DUMMY01` aligned), bolt-on indices 1–4. Always static at spawn (forces +2).
  - Spawnflag 16 ("spin wheels"): key `speed` default 200. Use toggles: first use creates a spin record with
    w = (speed · π / 94, 0, 0) and all four tires; if a record already exists only v1 is rewritten. Second use clears
    the record; and so on. Touch (while spawnflag 16): when the contact plane normal n and d = normalize(truck
    origin − toucher origin) satisfy n.x·d.x + n.y·d.y > 0.9, the toucher's velocity += 15 × the truck velocity and
    its vz += 25 more; if its health is > 10 it loses 5 (never kills). The reaction needs a contact plane, which
    only the moving truck's own impacts supply (SPEC-760 B4.8); touches by players or other movers come without a
    plane and do nothing, so in practice it acts only with spawnflags 4 + 16 while driving (UNVERIFIED in game).
  - Spawnflag 4 ("drive"): key `speed` default 200, acceleration fixed 30, key `distance` = travel length; the origin
    is raised by 1 at spawn, elasticity −1, entity flag 0x800 (no knockback). The destination point D (horizontal) =
    spawn origin + distance · (cos yaw, sin yaw). Use (once; use is then cleared) starts driving: see "Driving
    vehicles" below. When both 4 and 16 are set, use drives (the spawnflag 16 touch stays).
  - Death (any health ≤ 0): if the truck model has a bolt `ENGINE`: effect slot `environ/normalsmoke` on that bolt,
    sound `impact/explosion/med.wav` at the truck origin (world entity, channel 0, volume 1, attenuation 1), death
    callback removed (the wreck stays and is never removed), and 5 s later the smoke slot is removed. Without an
    `ENGINE` bolt nothing happens.

#### Driving vehicles (`misc_bosnia_truck_old` spawnflag 4, `misc_iraq_flatbed` spawnflag 4)

State machine run every 0.1 s; the approach check and the riders rule (§5.9, run at the start of every think) are
common to both classes.
1. **Start** (on use): movetype 10 (SPEC-760), ground friction 0, gravity 0, velocity = accel · (cos yaw, −sin yaw, 0)
   (note the negated y: for yaw ≠ 0/180 the truck drives mirrored across the x axis relative to D; original quirk,
   UNVERIFIED in shipped maps), use cleared, then the approach check; next state Accelerate.
2. **Accelerate**: if |v_xy|² ≥ speed², the next state is Cruise; otherwise v_xy += accel · (cos yaw, −sin yaw).
   Then the approach check and the wheel update.
3. **Cruise**: approach check and wheel update only (constant velocity).
4. **Approach check**: d = horizontal distance from the origin to D, s = 0.1 · |v_xy| (distance per frame). If the
   velocity is non-zero and d < 3 s: v_xy ×= d / (4 s), vz = 0, next state Brake.
5. **Brake**: with V = |v_xy| and s = 0.1 V: if V < 5, v_xy ×= d / s, vz = 0, next state Stop; else v_xy ×= d / (4 s),
   vz = 0 (each frame covers a quarter of the remaining distance).
6. **Stop**: velocity 0, movetype NONE, loop sound cleared, no further thinks.
Wheel update: `misc_iraq_flatbed` keeps one spin record of all four tires with w = (speed · π / 94, 0, 0) (record
created on the first update, afterwards v1 rewritten). `misc_bosnia_truck_old` uses w = (|velocity| · π / 94, 0, 0)
but, because the record is rebuilt once per tire, only the tire on bolt `DUMMY04` ends up in it (original quirk). The
wheel rate is not updated while braking or after stopping, and the record is never cleared, so the tires keep
turning at the last rate (UNVERIFIED in game).

#### Castle (`misc_castle_*`)

| classname | model (table 1) | skins (skinnum → skin) | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_castle_armor` | 0 `armor/armor` | — | (−4, −11, −38) / (11, 17, 50) | 500 | 28 / 9 | computed / 1 | 2 | — |
| `misc_castle_bust` | 1 `bust/bust` | 0 → `bust`, 1 → `bust2` | (−4, −6, −1) / (4, 3, 13) | 1000 | 18 / 16 | computed / 1 | 2 | — |
| `misc_castle_cannon` | 2 `cannon/cannon` | — | (−27, −11, −13) / (55, 11, 20) | 1000 | 28 / 9 | computed / 1 | 2 | — |
| `misc_castle_chair` | 3 `chair/seat01` + parts 4 `leg01` ×2, 5 `leg02` ×2, 6 `back` | — | (−17, −14, −15) / (11, 13, 38) | 500 | 15 / 18 | computed / 1 | 2 | shootable legs and back |
| `misc_castle_chair_big` | 7 `chair_big/seat01` + parts 8 `leg01` ×2, 9 `arm01` ×2, 10 `back01` | — | (−17, −14, −17) / (12, 14, 38) | 500 | 15 / 18 | computed / 1 | 2 | shootable legs, arms and back |
| `misc_castle_globe` | 12 `globe/base` + part 11 `globe` | — | (−7, −9, −26) / (15, 9, 12) | 1000 | 0 / 25 | computed / 1 | 2 | globe spins when hit |
| `misc_castle_hedge` | 13 `hedge/hedge` | — | (−10, −14, −12) / (9, 11, 11) | 500 | 15 / 18 | 25 / 1 | 2 | forces +2 |
| `misc_castle_tree` | 14 `tree/tree` | — | (−24, −21, −11) / (24, 21, 74) | 1000 | 15 / 18 | 35 / 2 | 2 | forces +2 |

Directories are under `objects/castle/`. All models are present in the archives.

- **Chairs.** The seat is the main model; the spawn angles are remembered. Bolt-on parts in this order (index 1–5):
  `misc_castle_chair`: leg01 on `DUMMY01`, leg01 on `DUMMY02`, leg02 on `DUMMY03`, leg02 on `DUMMY04` (each aligned
  by the part's bolt `DUMMY01`), back on `DUMMY05` (aligned by `DUMMY05`). `misc_castle_chair_big`: leg01 on
  `DUMMY01`/`DUMMY02` (part bolt `DUMMY01`), arm01 on `DUMMY03`/`DUMMY04` (part bolt `DUMMY03`), back01 on `DUMMY05`
  (part bolt `DUMMY05`). Pain = shootable parts (§5.7) with this part table:

  | part (index) | chair reaction | big chair reaction |
  |---|---|---|
  | 1, 2 (legs) | leg rule below | leg rule below |
  | 3, 4 | leg rule below (legs) | default 5-chunk debris and puff (arms) |
  | 5 (back) | default 5-chunk debris and puff | back rule below |

  The hit part is hidden and its shown bit cleared before the reaction runs; the leg reaction makes no debris for the
  hit leg itself (only for legs it removes).
  **Leg rule** (L1…L4 = shown bits of parts 1–4 after the hit). If all four are clear: pose FLAT. Otherwise, for
  the ordinary chair, by the hit leg h and the legs still shown:

  | h | remaining legs → result |
  |---|---|
  | 1 | 2 3 4 → nothing; 3 4 → TILT_F; 2 4 → TILT_R; 2 3 → remove 2 and 3, FLAT; 2 → remove 2, FLAT; 3 → remove 3, FLAT; 4 → remove 4, FLAT |
  | 2 | 1 3 4 → nothing; 3 4 → TILT_F; 1 3 → TILT_L; 1 4 → remove 1 and 4, FLAT; 1 → remove 1, FLAT; 3 → remove 3, FLAT; 4 → remove 4, FLAT |
  | 3 | 1 2 4 → nothing; 1 2 → TILT_B; 2 4 → TILT_R; 1 4 → remove 1 and 4, FLAT; 1 → remove 1, FLAT; 2 → remove 2, FLAT; 4 → remove 4, FLAT |
  | 4 | 1 2 3 → nothing; 1 2 → TILT_B; 1 3 → TILT_L; 2 3 → remove 2 and 3, FLAT; 1 → remove 1, FLAT; 2 → remove 2, FLAT; 3 → remove 3, FLAT |

  (Read as geometry: 1/2, 3/4, 1/3 and 2/4 are the four sides and 1/4, 2/3 the diagonals — two legs left on one
  side tilt the chair, two legs left on a diagonal or a single leg collapse it; which tilt direction looks right
  depends on the bolt placement in the model, UNVERIFIED.) "Remove i" =
  remove part i with debris (§5.6). For the big chair: h = 1 → TILT_R if leg 2 is shown, else FLAT; h = 2 → TILT_L if
  leg 1 is shown, else FLAT (the arms only matter for the "all four clear" test).
  **Poses** (applied to the current angles, so tilts accumulate; f = trace fraction of a point trace from the origin,
  clip mask of the entity, ignoring the chair; forward/right from the current angles; the box is then set and the
  entity relinked):
  - FLAT: angles = spawn angles; box (−10, −9, −2) / (10, 9, 18).
  - TILT_F: trace to origin + 20·forward; if f < 1, origin −= 20 f · forward; pitch += 40; box (−10, −9, −6) / (10, 9, 18).
  - TILT_B: trace to origin − 30·forward; if f < 1, origin += 30 f · forward; pitch −= 37; box (−10, −9, −5.5) / (10, 9, 18).
  - TILT_L: trace to origin − 30·right; if f < 1, origin += 30 f · forward (sic: forward); roll −= 40.
  - TILT_R: trace to origin + 30·right; if f < 1, origin −= 30 f · forward (sic); roll += 40.
  TILT_L/TILT_R box: (−10, −9, m) / (10, 9, 18) with m = −8.7 for the big chair and −6 for the chair.
  **Back rule** (big chair, part 5): debris (TE 18: 5 chunks, size 1, the chair's debris material and surface type,
  zero direction) and a grey puff (TE 45, (120, 120, 120, 200)) at the model's bolt `DUMMY05`; then, if either arm
  is still shown, the same debris and puff at bolts `DUMMY03` and `DUMMY04` and both arms are hidden (their shown
  bits stay set, so a later hit inside an arm's bounds repeats the arm's default debris; UNVERIFIED in game).
  Death: table break of the seat entry (computed count, size 1).
- **Globe.** The globe (entry 11) sits on the base's bolt `DUMMY01` (aligned by the globe's `DUMMY01`). Pain (attacker
  A, damage D, point p): f = 0.1·D; c = normalize(A origin − origin) × (p − origin) (cross product), spin sign from
  c.z. c.z < 0: if a spin record exists, v2 += trunc(512 f) (no cap); else create a record with w = (0, min(f, 30), 0)
  for the globe part. c.z > 0: the same with −f and −min(f, 30). c.z = 0: no change. In all cases the spin-down
  runs 0.5 s later and every 0.5 s: v2 = trunc(0.75 · v2); when |v2| < 153.6 the record is cleared and the spin-down
  stops. Death: table break (computed count, size 1).

#### Iraq (`misc_iraq_*`, and `misc_generic_sprinkler`)

| classname | model (table 3) | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_iraq_basket` | 0 `basket/basket` | — | (−6, −7, −18) / (7, 6, 15) | 50 | 15 / 18 | computed | 2 | — |
| `misc_iraq_cart_camel` | 1 `cart_camel/cart` | — | (−20, −48, −24) / (20, 48, 24) | 300 | 15 / 18 | computed | 2 | — |
| `misc_iraq_flatbed` | 4 `flatbed/on` + 4 × part 3 `tire` (+ 2 × part 8 `objects/generic/beam/beam` with spawnflag 8) | — | (−95, −40, −42) / (95, 40, 45) | 500 | 28 / 8 | — (no break) | 2 | own death; precaches effect `environ/normalsmoke`, sound `impact/explosion/med.wav` |
| `misc_iraq_fruit_melon` | 5 `fruit_melon/melon` | — | (−8, −4, −4) / (8, 4, 4) | 50 | 27 / 0 | none (material 0) | 2 | — |
| `misc_iraq_fruit_orange` | 6 `fruit_orange/orange` | — | (−2, −2, −2) / (2, 2, 2) | 50 | 12 / 0 | none (material 0) | 2 | — |
| `misc_iraq_fruit_stand` | 7 `fruit_stand/fruit_stand` (+ spawned melons/oranges) | — | (−8, −16, −16) / (8, 16, 16) | 125 | 15 / 18 | computed | 2 | own death unless spawnflag 16 |
| `misc_iraq_partition` | 9 `partition/partition` | — | (−63, −1, 0) / (62, 2, 35) | 400 | −1 / 0 | none (material 0) | 2 | — |
| `misc_iraq_pottery` | 10 `pottery/pottery` | — | (−8, −8, −9) / (8, 8, 9) | 25 | 4 / 0 | none (material 0) | 2 | — |
| `misc_iraq_sprinkler` | 11 `sprinklers/sprinklers` (data no) | — | (−7, −63, −2) / (6, 64, 2) | 500 | 28 / 8 | computed | 2 | forces +2 |
| `misc_iraq_stealth` | 12 `stealth/stealth` | — | (−32, −32, −54) / (32, 32, 53) | 5000 | 28 / 8 | — | 2 | forces +1; yaw forced to 270 |
| `misc_iraq_tree_palm` | 13 `tree_palm/tree` | — | (−30, −9, −67) / (−6, 23, 67) | 1000 | 15 / 18 | computed | 0 | forces +2; made non-solid after the setup |
| `misc_iraq_valve` | 14 `valve/valve` | — | (−12, −3, −10) / (12, 1, 11) | 300 | 28 / 8 | — | 2 | forces +1 +2; own use; precaches `Ambient/Models/Valve/ValveTurn.wav` |
| `misc_generic_sprinkler` | no table: `objects/generic/urn/urn` | — | (−4, −4, −4) / (4, 4, 4) | 10 | 21 / 0 | none (material 0) | 2 | forces +2; effect slot `environ/sprinkler` |

Directories are under `objects/iraq/` unless given in full.

- **Flatbed truck.** Key `style` = 0 hides the main-model part `OILTANK`. Without spawnflag 8 the parts `top1`, `top2`,
  `back1`, `back2`, `frt1`…`frt4` are hidden; with spawnflag 8 ("lights on") they are shown and a light-beam part
  (entry 8) is attached to each of the bolts `headlight1` and `headlight2` (beam bolt `to_headlight` aligned; these
  become bolt-on parts 1 and 2). Then four tires (entry 3) go on bolts `DUMMY01`…`DUMMY04` (tire bolt `DUMMY05`
  aligned). The flatbed is not made static: without spawnflag 2 it has physics and the player push (UNVERIFIED that
  maps set spawnflag 2).
  - Spawnflag 16 ("spin wheels"): key `speed` default 200; use toggles a spin record of all four tires with
    w = (speed · π / 94, 0, 0) on and off (as `misc_bosnia_truck_old`, but no touch reaction).
  - Otherwise spawnflag 4 ("drive"): as `misc_bosnia_truck_old` spawnflag 4 (origin + 1, elasticity −1, flag 0x800,
    `speed` default 200, accel 30, destination from `distance`, use once) with the flatbed wheel update (Driving
    vehicles).
  - Death: as `misc_bosnia_truck_old` (smoke slot on bolt `ENGINE` for 5 s, `impact/explosion/med.wav`, no break,
    death callback removed); additionally with spawnflag 8 all mesh parts of bolt-on parts 1 and 2 (the beams) are
    hidden and the main-model parts `HEADLIGHT1` and `HEADLIGHT2` are hidden (lights go out).
- **Fruit stand.** With spawnflag 16 it is a plain prop (table break). Otherwise, if neither 4 nor 8 is set both are
  set, and fruit props are spawned and placed: each fruit gets the stand's angles and the position stand origin +
  (0, 0, up) + a · F + b · R, where F, R are the forward/right axes of (stand pitch, the stand's *yaw-offset* state
  field, stand roll) — that field is 0 for props, so the offsets are applied in world axes whatever the stand's yaw
  (original quirk, UNVERIFIED in game). Offsets (a, b, up):
  - spawnflag 4, five melons (melon entry, box (−8, −4, −4) / (8, 4, 4)): (0, 0, 20), (0, −8, 20), (0, −16, 20),
    (0, −4, 27), and the fifth at (0, −12, 27) when spawnflag 8 is also set, else at (0, 12, 20) with its yaw-offset
    state field increased by 25;
  - spawnflag 8, five oranges (orange entry, box (−2, −2, −2) / (2, 2, 2)): (2, 8, 18), (−2, 8, 18), (0, 12, 18),
    (4, 12, 18), (−8, 18, 18).
  The fruits form one ordered list (melons first, then oranges) owned by the stand; each fruit is an ordinary prop
  (physics, player push, damageable) whose death, while it is still owned, breaks the first N fruits of the list at
  once (N as below; standard break with computed count, no debris for material 0) and empties the stand's list.
  A released fruit does nothing on death until it has been launched.
  Stand death: if it has no fruit list, standard break. Otherwise N = 10 when both 4 and 8 are set, else 5; the
  first N fruits are released; fruits 4 and 5 (and fruits 6–10 when N = 10) are **launched** at once, fruits 1–3
  are launched 0.1 s later; then the stand does the standard break (computed count). Launch: velocity =
  (rand(−100, 100), rand(−100, 100), rand(200, 500)), roll rate = rand(−100, 100) degrees/s, death becomes the
  standard break.
- **Stealth bomber.** Not damageable; the yaw is always 270. It is not made static (physics and player push unless
  spawnflag 2).
- **Valve.** Static, not damageable; after the setup the model is paused at the current time (holds its pose) and
  the valve gets movetype 10 with gravity 0 (so it can turn). Use:
  - spawnflag 128: the entity that fired the valve decides by its mover state (Quake II moveinfo states): going up
    (2) → pitch rate +40 °/s, going down (3) → −40 °/s, any other state → 0 (the turn rate is subject to the
    movetype 10 rotational friction of SPEC-760, UNVERIFIED);
  - otherwise: sound `Ambient/Models/Valve/ValveTurn.wav` on the valve (channel 2, volume 0.6, attenuation 1), the
    sequence `valve` is restarted (loop mode) and, if the sequence has an `EOS` (end-of-sequence) marker, the model
    is paused when the marker is reached (and the entity's loop sound cleared), so each use plays one turn.
- **Generic sprinkler** (`misc_generic_sprinkler`, an Iraq-level class): urn model placed without a model table
  (§3 last paragraph, solid 2), surface type 21 and health 10 forced, static; an effect slot `environ/sprinkler` at
  its origin (bolt 0, default scale). Death: standard break with computed count (material 0 → no debris).

### 7B. Level prop families: New York, Siberia, Tokyo, Uganda

Terms as in §1–§5 (standard prop setup, standard break, table break, player push, effect slot …). Model entries are
given as `table #index` of Appendix A (table 6 New York, 7 Siberia, 8 Tokyo, 9 Uganda); "model" is the entry's
`directory/file`. Health, surface type (surf), debris material (mat) and solid are the entry defaults (map keys
`health`, `surfacetype`, `material` override them when non-zero). "Table break" values are given as they result
for the class box (n = count, s = size class; "computed" = §4.1 rule from the box volume). Unless a class says
otherwise its death is the table break and spawnflags have the standard meaning (1 = not damageable, 2 = static,
no physics / no push). "Forces +2" = spawnflag 2 is always set before the setup (the prop is always static).

All temp entities below are sent to the potentially-hearable set of their position (SPEC-771 §C gives the exact
messages). TE numbers are those of SPEC-201 §1 / SPEC-401.

#### N. New York (`misc_newyork_*`)

| classname | model (entry) | skins | box mins / maxs | health | surf / mat | table break n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_newyork_burned_car` | `objects/newyork/burned_car` (6 #0) | — | (−91, −33, −25) / (91, 33, 25) | 10000 | 28 / 10 | computed: 20 / 1 | 2 | plain prop |
| `misc_newyork_gumball` | `objects/newyork/gumball` (6 #1) | key `skin` 1 → `gumball2`, else `gumball` (on material `gumball`) | (−8, −8, −19) / (8, 8, 19) | 1000 | 28 / 11 | computed: 10 / 1 | 2 | pain; sets material-flag position 1; precaches `Impact/GlassBreak/GBSmall.wav` |
| `misc_newyork_news_machine` | `objects/newyork/news_machine/news` (6 #2) | — | (−21, −35, −20) / (21, 12, 19) | 1000 | 28 / 11 | computed: 10 / 1 | 2 | model paused at spawn; pain; sets material-flag positions 1 and 22 |
| `misc_newyork_trashcan` | `objects/newyork/trashcan` (6 #4) | — | (−10, −10, −18) / (10, 10, 20) | 300 | 28 / 10 | computed: 10 / 1 | 2 | pain, own death; plays `trashcan` once-and-hold at spawn; precaches sound `Ambient/Models/TrashCan/LidSpin.wav`; sets material-flag positions 21 and 22 |
| `misc_newyork_trash_pile` | `objects/newyork/trash_pile` (6 #3) | — | (−10, −11, −6) / (10, 11, 18) | 300 | 28 / 10 | computed: 10 / 1 | 2 | forces +2; pain; key `count` = items already gone |

Data check: the model files of `news_machine` and `trash_pile` are not in the 1.07f archives (Appendix A); these two
classes spawn without a visible model in the shipped game (UNVERIFIED in game).

**misc_newyork_gumball** (gumball machine with a glass globe)
- Spawn: skin chosen by key `skin` (1 = already broken globe `gumball2`).
- Pain, only while the skin number is not 1: the skin becomes `gumball2` and the skin number 1; at p = origin + (0, 0, 10):
  debris TE 18 with zero direction, 5 chunks, size class 1, chunk type 7 (glass), extents 0, surface type 37 (glass);
  lit sparks TE 31, sparks TE 49 and lit sparks TE 31 again, each with its own direction (irand(−21, 20),
  irand(−21, 20), 0) (written as a direction byte); sound `Impact/GlassBreak/GBSmall.wav` on the prop, channel 5,
  volume 0.6, attenuation 1; health is set to 1, so the next damaging hit runs the death (table break).
- Later pains (skin number 1) do nothing.

**misc_newyork_news_machine** (newspaper vending box)
- Spawn: the default sequence is paused at the spawn time (the model shows its first pose).
- First pain: with forward f and right r of the prop angles: paper burst TE 48 with count 3 at q = origin + 10·f;
  debris TE 18 at q, zero direction, 5 chunks, size class 1, chunk type 7, extents 0, surface type 37; sparks TE 49
  at origin + 15·r + (0, 0, 10) with direction f; plays sequence `news` once-and-hold (UNVERIFIED what it shows; check the model).
- Every later pain: paper burst TE 48, count 3, at the prop origin (nothing else).

**misc_newyork_trashcan** (street trash can with a swinging lid)
- Spawn: plays sequence `trashcan` once-and-hold.
- Pain: at most once per 0.5 s for all trash cans together (one shared timer; the timer is restarted by every pain
  that arrives after the 0.5 s, whatever its result). If the hit point is more than 10 units above the prop origin
  (a hit on the lid): the sequence `trashcan` is played again once-and-hold, the damage of this hit is given back
  (health += damage), and TE 59 (lid spin sound) is sent at the origin. Hits on the lower part take normal damage.
- Death: three coloured smoke puffs TE 45 (200, 200, 200, 250) at the origin, then the garbage-bag death of the
  generic family: paper burst TE 48 (count 0 = random) at the origin, soda cans TE 50 at the origin with direction
  normalize(attacker origin − prop origin) (inflictor origin when there is no attacker, (0, 0, 1) when neither
  exists), speed 100, count 0 (random 1–4), then the table break.

**misc_newyork_trash_pile** (heap with four loose items)
- Always static (forces +2); damageable unless spawnflag 1.
- Each pain knocks one item off, chosen by the counter (key `count`, default 0):

  | counter | model part hidden | bolt of the pile |
  |---|---|---|
  | 0 | `_CAN1` | `DUMMY01` |
  | 1 | `_CAN2` | `DUMMY02` |
  | 2 | `_SHOE` | `DUMMY03` |
  | 3 | `_OILCAN` | `DUMMY04` |

  For that item: sound `Weapons/FX/Rics/ric1.wav` … `ric3.wav` (irand(1, 3)) on the pile, channel 2, volume 0.6,
  attenuation 1; the part is hidden on the pile; a new prop is spawned at the pile origin + (0, 0, 30) with box
  (−5, −5, −5)/(5, 5, 5), the same model entry, standard prop setup (spawnflags 0: damageable, physics, player push),
  only that part shown, velocity (rand(−200, 200), rand(−200, 200), rand(50, 150)), and it is removed after
  rand(0, 30) s (it can also be shot: table break with computed debris 10 / 1); a coloured puff TE 45
  (200, 200, 200, 250) at the world position of the item's bolt. The counter is incremented; when it reaches 4 the
  pile is removed (no break, no debris, targets are not fired).
- A counter above 3 (map key `count` ≥ 4) leaves the part name undefined (original defect; UNVERIFIED — avoid).
- If a hit brings the health to ≤ 0 instead, the table break runs (no pain).

#### S. Siberia (`misc_siberia_*`)

| classname | model (entry) | skins | box mins / maxs | health | surf / mat | table break n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_siberia_carton1` | `objects/siberia/carton1` (7 #0) | — | (−15, −15, −15) / (15, 15, 15) | 500 | 28 / 9 | computed: 10 / 1 | 2 | plain prop; model file missing in 1.07f |
| `misc_siberia_carton2` | `objects/siberia/carton2` (7 #1) | — | (−8, −8, −7) / (8, 8, 7) | 10000 | 28 / 9 | computed: 10 / 1 | 2 | plain prop; model file missing |
| `misc_siberia_carton3` | `objects/siberia/carton3` (7 #2) | — | (−3, −3, −3) / (3, 3, 4) | 10000 | 28 / 9 | computed: 10 / 1 | 2 | plain prop |
| `misc_siberia_carton4` | `objects/siberia/carton4` (7 #3) | — | (−10, −10, −15) / (10, 10, 15) | 10000 | 28 / 9 | computed: 10 / 1 | 2 | plain prop; model file missing |
| `misc_siberia_flag` | `objects/siberia/flag` (7 #4) | — | (−16, −1, −1) / (1, 1, 79) | 500 | 0 / 0 | no debris (material 0) | 2 | forces +2 |
| `misc_siberia_icicles` | `objects/siberia/icicles` (7 #5) | — | (−1, −41, −41) / (1, 41, 41) | 10000 | 0 / 0 | no debris (material 0) | 2 | forces +2; model file missing |
| `misc_siberia_roadflare` | `objects/siberia/flare/null` (7 #7) | — | (−2, −2, −2) / (2, 2, 2) | 500 | −1 / −1 | no debris | 0 | forces +2; use toggles; precaches `Ambient/Gen/FireFX/FlareLP.wav` |
| `misc_siberia_searchlight` | `objects/siberia/searchlight/arm` (7 #9) + bolt-on `lamp` (7 #8) | — | (−10, −10, −8) / (10, 10, 12) | 10000 | 28 / 9 | computed: 10 / 1 | 2 | forces +2; no touch; aims at `target` once at spawn |
| `misc_siberia_snowcat` | `objects/siberia/snowcat` (7 #10) | — | (−91, −66, −33) / (91, 66, 58) | 10000 | 28 / 9 | computed: 20 / 1 | 2 | forces +2; spawnflag 4 = headlight beams |

Entry 7 #6 (`objects/generic/beam/beam`, surf/mat −1, health 250, solid 0) is used only as the snowcat bolt-on.
A material or surface type of −1 is not "0", so no default is applied and the break sends no debris (material ≤ 0).

**misc_siberia_roadflare** (burning road flare)
- Spawnflags: 4 = start off, 8 = blue flame. The model is the empty `null` object with all parts shown; solid 0
  (not hit by traces).
- On (at spawn unless spawnflag 4): loop sound `Ambient/Gen/FireFX/FlareLP.wav` (entity state sound, data 0xF8) and an
  entity effect slot (SPEC-202, bolt 0, default scale) with effect `environ/roadflare` (spawnflag 8:
  `environ/roadflare_blue`).
- Use (when fired by a trigger): toggles. Turning off sets spawnflag 4, clears the loop sound and removes the effect
  slot of that effect; turning on clears spawnflag 4 and restores sound and effect slot.

**misc_siberia_searchlight** (fixed searchlight; an arm with a lamp head)
- Spawn: box, forces +2, the arm entry with the player push removed (no touch); the lamp entry is attached as
  bolt-on part 1: its bolt `DUMMY02` to bolt `DUMMY02` of the arm.
- Key `target`: the first entity whose `targetname` matches (searched at spawn, so it must already exist — an entity
  placed earlier in the map; UNVERIFIED for later ones) becomes the aim point. If found: the arm entity is turned in
  yaw so that the target's origin is straight ahead (horizontal aim, no limits), then the lamp's bolt is pitched so
  that the lamp points at the target's origin (vertical aim, no limits). This happens once; the searchlight never
  moves afterwards and has no beam, light or sound.

**misc_siberia_snowcat** (parked snow vehicle)
- Always static. Spawnflag 4: two bolt-on parts of entry 7 #6 (`objects/generic/beam`), each attached by its bolt
  `to_headlight` to the vehicle bolts `HEADLIGHT01` and `HEADLIGHT02` (visible light cones; no dynamic light).

#### T. Tokyo (`misc_tokyo_*`)

| classname | model (entry) | skins | box mins / maxs | health | surf / mat | table break n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_tokyo_bowl` | `objects/tokyo/bowl` (8 #0) | — | (−5, −5, −1) / (5, 5, 2) | 100 | 14 / 17 | 6 / 1 | 2 | plain prop |
| `misc_tokyo_buddha` | `objects/tokyo/buddha` (8 #1) | — | (−14, −24, −32) / (19, 24, 32) | 2000 | 28 / 11 | computed: 13 / 1 | 2 | plain prop; model file missing in 1.07f |
| `misc_tokyo_fan` | `objects/tokyo/fan` (8 #2) | — | (−14, −24, −32) / (19, 24, 32) | 50 | 14 / 14 | computed n: 13 / 4 | 2 | plain prop (same box as the buddha) |
| `misc_tokyo_gong` | `objects/tokyo/gong` (8 #3) | — | (−3, −29, −34) / (3, 29, 34) | 1000 | 28 / 11 | 10 / 1 (entry 6 / 0.5 → size 0 → clamp) | 2 | model paused at spawn; pain |
| `misc_tokyo_gym_equipment1` | `objects/tokyo/gym_equip1/equip1` (8 #4) | — | (−36, −13, −27) / (36, 14, 27) | 2000 | 28 / 11 | computed: 13 / 1 | 2 | plain prop; model file missing |
| `misc_tokyo_gym_equipment2` | `objects/tokyo/gym_equip2/equip2` (8 #5) | — | (−48, −28, −34) / (58, 33, 35) | 2000 | 28 / 11 | computed: 20 / 1 | 2 | plain prop; model file missing |
| `misc_tokyo_plant_bonsai` | `objects/tokyo/plant_bonsai/bonsai` (8 #6) | — | (−5, −10, −8) / (5, 10, 8) | 100 | 14 / 17 | computed: 10 / 1 | 2 | plain prop |
| `misc_tokyo_samurai_suit` | `objects/tokyo/samurai_suit_small/samurai_sm` (8 #7) | — | (−6, −18, −37) / (9, 16, 37) | 1000 | 28 / 10 | computed n: 10 / 2 | 2 | model scaled ×1.1 |
| `misc_tokyo_samurai_suit_small` | `objects/tokyo/samurai_suit_small/samurai_sm` (8 #8) | — | (−3, −8, −17) / (4, 7, 17) | 1000 | 28 / 10 | computed: 10 / 1 | 2 | model scaled ×0.5 |
| `misc_tokyo_shoe` | `objects/tokyo/shoe` (8 #9) | — | (−6, −3, −2) / (6, 3, 2) | 100 | 14 / 17 | 4 / 1 | 2 | plain prop |
| `misc_tokyo_stone_light` | `objects/tokyo/stone_light/light` (8 #10) | — | (−15, −13, 14) / (15, 13, 14) | 1000 | 17 / 15 | computed: 10 / 1 | 2 | forces +2; zero-height box (see below) |

Entry 8 #11 (`objects/generic/stunt_chopper`) is not used by any Tokyo class.

- **Scaling** (samurai suits): after the standard setup the model transform is multiplied by a uniform scale of 1.1
  (`misc_tokyo_samurai_suit`) or 0.5 (`_small`); this multiplies any `scale` key already applied. The box is not
  scaled. Both classes use the same model file.
- **misc_tokyo_gong**: spawn pauses the default sequence at the spawn time; every pain plays sequence `gong`
  once-and-hold (the gong swings; no sound is played by the game code — any sound must come from the model's
  sequence notes, UNVERIFIED). It breaks normally when its health runs out.
- **misc_tokyo_stone_light**: the box minimum and maximum height are both +14 (zero height, 14 units above the
  origin) — original data defect; the volume is 0, so the break uses the computed minimum of 10 chunks. UNVERIFIED how
  well traces hit it; it is always static. It does not emit light.
- `misc_tokyo_fan`'s break size class 4 lies outside the client's debris size classes 1–3 (SPEC-401 chunk models);
  what the client shows is UNVERIFIED.

#### U. Uganda (`misc_uganda_*`)

| classname | model (entry) | skins | box mins / maxs | health | surf / mat | table break n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `misc_uganda_african_tree` | `objects/uganda/african_tree/tree` (9 #0) | — | (−42, −77, −76) / (42, 77, 76) | 1000 | 15 / 18 | computed: 20 / 1 | 2 | plain prop; model file missing in 1.07f |
| `misc_uganda_chainsaw` | `objects/uganda/chainsaw` (9 #1) | — | (−14, −4, −5) / (14, 4, 5) | 500 | 28 / 9 | computed: 10 / 1 | 2 | spawnflag 4: smoke effect slot |
| `misc_uganda_cow` | `objects/uganda/cow` (9 #2) | — | (−13, −38, −29) / (13, 43, 28) | 400 | 27 / 0 | no debris (material 0) | 2 | plain prop; model file missing |
| `misc_uganda_cow_head` | `objects/uganda/cow_head/head` (9 #3) | — | (−10, −13, −8) / (10, 13, 8) | 100 | 27 / 0 | no debris | 2 | spawnflag 4: blood pool after 2 s |
| `misc_uganda_gore_pile` | `objects/uganda/gore_pile` (9 #4) | — | (−26, −23, −3) / (26, 23, 3) | 300 | 27 / 0 | no debris | 2 | pain: blood splat |
| `misc_uganda_gutted_cow` | `objects/uganda/gutted_cow` (9 #5) | — | (−15, −33, −99) / (27, 25, 1), never rotated | 1000 | 27 / 32 | computed: 20 / 1 | 2 | hanging carcass; spawnflags 4, 8, 16; pain swing |
| `misc_uganda_hook` | `objects/uganda/hook` (9 #6) | — | (−1, −4, −7) / (1, 4, 7) | 1000 | 28 / 9 | computed: 10 / 1 | 2 | plain prop |
| `misc_uganda_nuke` | `objects/uganda/nuke` (9 #7) | — | (−31, −28, −24) / (30, 14, 25) | 1000 | 28 / 9 | (never breaks) | 2 | forces +1 +2 |
| `misc_uganda_rocket_core` | `objects/uganda/rocket_core/core` (9 #8) | — | (−55, −14, −14) / (55, 14, 14) | 500 | 28 / 9 | computed: 11 / 1 | 2 | plain prop |
| `misc_uganda_rocket_engine` | `objects/uganda/rocket_engine/engine` (9 #9) | — | (−76, −28, −27) / (76, 33, 32) | 500 | 28 / 9 | computed: 20 / 1 | 2 | plain prop |
| `misc_uganda_side_o_beef` | `objects/uganda/side_o_beef/beef` (9 #10) | key `skin` 1 → `beef_f`, else `beef` (material `beef`) | (−13, −16, −55) / (19, 17, 48) | 500 | 27 / 32 | computed: 14 / 1 | 2 | hanging meat; spawnflag 4; pain swing |
| `misc_uganda_train_arm` | `objects/uganda/train_arm/arm` (9 #11) | — | (−99, −5, −110) / (99, 5, 110) | 500 | 28 / 9 | (never breaks) | 2 | forces +1 +2; model file missing |
| `misc_uganda_train_bumper` | `objects/uganda/train_bumper/bumper` (9 #12) | — | (−32, −15, −16) / (32, 15, 10) | 500 | 28 / 9 | (never breaks) | 2 | forces +1 +2 |
| `misc_uganda_train_coupling` | `objects/uganda/train_coupling/coupling` (9 #13) | — | (−31, −11, −5) / (31, 11, 5) | 500 | 28 / 9 | (never breaks) | 2 | forces +1 +2 |
| `misc_uganda_train_wheel` | `objects/uganda/train_wheel/wheel` (9 #14) | — | (−14, −1, −14) / (14, 1, 14) | 500 | 28 / 9 | computed: 10 / 1 | 2 | plain prop; model file missing |

"Forces +1 +2" = never damageable and always static, whatever the map sets. Debris of chunk type 32 (flesh) is not
sent while the cvar `lock_blood` is set (SPEC-721; rule of the debris message, SPEC-771 §C).

**misc_uganda_chainsaw**
- Spawnflag 4: an entity effect slot (bolt 0, default scale) with effect `environ/normalsmoke` (a smoking chainsaw).
  No sound.

**misc_uganda_cow_head**
- Spawnflag 4: 2 s after spawn, once: effect `gore/bloodpool` at the prop origin (an svc_effect at a position, sent
  to the hearable set), unless the cvar `lock_blood` is set.

**misc_uganda_gore_pile**
- Pain: blood decal behind the hit point: unless `lock_blood`, a trace (solid and window contents, ignoring the pile)
  from the hit point p to p + 128·(p − attacker origin) (the vector is not normalised, so the reach is 128 × the
  attacker distance); if it stops before 99 % of the way on the world or on a BSP entity, a blood decal temp entity
  (TE 3, impact data from that trace) is sent. The pile has no debris on death.

**misc_uganda_gutted_cow** (carcass hanging from a hook)
- Spawn: box set, standard setup, then the box is set again to the unrotated values (the right-angle rotation of the
  setup is undone) and the entity is relinked.
- Spawnflags: 4 = blood pool (as `misc_uganda_cow_head`, 2 s after spawn); 8 = entity effect slot with effect
  `environ/bloodrain` (dripping blood); 16 = swinging: 0–9 s after spawn (a random whole number of seconds) the
  sequence `gutted_cow` starts looping. 16 uses the same timer as 4, so with 4 + 16 the blood pool never appears
  (original quirk). Without 16 the model is paused at its spawn pose.
- Pain: a = yaw of the direction from the cow to the attacker minus the cow's yaw, reduced to 0…360° (quantised to
  1/65536 turn). Front or back hits (a < 30, a > 340, or 150 < a < 210) play `swing1`, side hits play `swing2`,
  once-and-hold. At the end of that sequence (model note `EOS`), if spawnflag 16 is set, `gutted_cow` loops again;
  otherwise the model holds the end pose. Each pain registers one more end-of-sequence reaction (they accumulate;
  harmless but UNVERIFIED).

**misc_uganda_side_o_beef** (side of beef on a hook)
- Spawn: skin `beef_f` when key `skin` is 1, else `beef`; spawnflag 4: after irand(1, 10) s the sequence `SLOW`
  starts looping (gentle sway). Without 4 the default sequence keeps looping (it is not paused).
- Pain: as the gutted cow but with the names swapped: front/back hits play `SWING2`, side hits `SWING1`,
  once-and-hold; at the end of the sequence `SLOW` loops again if spawnflag 4 is set.

### 8. Light fixtures (`light_generic_*`)

Twenty classnames show a light-fixture model from model table 5 (Appendix A.5). They do not emit light
themselves: the visible light comes from the map's compiled lighting, and a fixture switches it by changing a light
style (Quake II light styles, configstring 1159 + style, SPEC-760 B8.2). The fixture changes its own model at the
same time (sequence and/or glow parts). All of them use the standard prop setup (§3) with the `light` rules of §3
step 8.

#### L.1 Rules common to all fixtures

**Keys.** `style` (light style number), `targetname`, `skin` / skin number (only where listed), `health`,
`scale`, `color`, `surfacetype`, `material`, `message`, `target` (as §3/§4).

**Spawnflags.**
| Bit | Meaning |
|---|---|
| 1 | START_OFF: the fixture begins switched off (see "initial state" per class) |
| 2 | damageable (§3 step 8; several classes force it on, see L.3) |
| 4 | physics: movetype 10 and the player push (§5.1) |
| 8 | `light_generic_lamp_hang` only: no handle/rope, static |
| 16 | `light_generic_lamp_hang` only: static (does not sway) |

**Initial light style.** After the setup, if `style` ≥ 32: configstring 1159 + style is set to `a` (dark) when
spawnflag 1 is set, else to `m` (normal), and the Quake II light toggle (below, without the model changes) becomes
the use behaviour. Styles below 32 are left untouched at spawn.

**Toggle (use behaviour).** Every class below that lists "on:" / "off:" model changes has its own use behaviour, set
whatever the style value. Each use:
1. if spawnflag 1 is clear (fixture currently on) → apply the class's "off:" model changes; else apply "on:";
2. if spawnflag 1 was set → configstring 1159 + style := `m` and spawnflag 1 cleared; else configstring 1159 +
   style := `a` and spawnflag 1 set.
Step 2 is done even when `style` < 32, so triggering a fixture whose `style` is 0 (the default) rewrites light style
0 — the style of all unstyled world lighting — to `a`/`m` (original quirk, UNVERIFIED in game: give a
`light_generic_desk_lamp` a `targetname` but no `style`, trigger it, and see whether the whole level darkens).
Classes without their own use (`light_generic_halfshell`, `light_generic_candelabra_small`) only have the Quake II
toggle when `style` ≥ 32 and are otherwise not usable.
Model changes are named in the class lists: "seq X" = play sequence X once-and-hold, "seq X (loop)" = loop mode,
"pause" = freeze the pose at the current time, "show/hide P" = show/hide part P.

**Damage.** Damageable only with spawnflag 2 (or forced, L.3). Default death = standard break with computed debris
count and size 1 (§4.1; the table counts of Appendix A.5 are not used by lights). Breaking a fixture does **not**
change its light style: the lighting it controlled stays as it was (except for the bulb-blow death of L.2.3).
Default debris for every table-5 entry: surface type 28 (metal), debris material 10, except
`light_generic_lantern` (debris material 31).

**Solid.** Table entries 0 (`alley_wall`) and 20 (`street_modern`) have default solid 0: these two fixtures are not
solid, cannot be hit by traces and therefore cannot be shot (UNVERIFIED in game).

**Missing models.** `ghoul/objects/light/alley_wall_old/light.ghb` and `ghoul/objects/light/military_door/light.ghb`
are not in the 1.07f archives: those two classes spawn without a visible model (the setup prints its
`SetSimpleGhoulModel couldn't …` developer message).

#### L.2 Plain switchable fixtures

| classname | model (table 5) | skins (skinnum → skin) | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `light_generic_alley_wall` | 0 `objects/light/alley_wall` / `light` | — | (−48, −41, −53) / (38, 41, 21) | 500 | 28 / 10 | computed / 1 | 0 | not solid |
| `light_generic_alley_wall_old` | 1 `objects/light/alley_wall_old` / `light` | — | (−28, −4, −13) / (4, 4, 9) | 500 | 28 / 10 | computed / 1 | 2 | model file missing |
| `light_generic_caged` | 2 `objects/light/caged` / `caged` | — | (−3, −3, −10) / (3, 3, 5) | 500 | 28 / 10 | see L.2.3 | 2 | bulb-blow death |
| `light_generic_desk_lamp` | 9 `objects/light/desk_lamp` / `lamp` | 0 → `light`, 1 → `lightsilver` (material `light`) | (−6, −6, −5) / (6, 6, 5) | 500 | 28 / 10 | computed / 1 | 2 | |
| `light_generic_halfshell` | 12 `objects/light/halfshell` / `shell` | — | (−2, −7, −2) / (2, 7, 2) | 500 | 28 / 10 | computed / 1 | 2 | Quake II toggle only |
| `light_generic_lantern2` | 15 `objects/light/lantern2` / `lantern2` | — | (−8, −7, −9) / (8, 7, 8) | 500 | 28 / 10 | see L.2.3 | 2 | bulb-blow death |
| `light_generic_lantern3` | 16 `objects/light/lantern3` / `lantern3` | — | (−7, −7, −13) / (4, 7, 8) | 500 | 28 / 10 | computed / 1 | 2 | |
| `light_generic_military_door` | 17 `objects/light/military_door` / `light` | — | (−6, −6, −2) / (6, 9, 3) | 500 | 28 / 10 | computed / 1 | 2 | model file missing |
| `light_generic_military_door_outside` | 18 `objects/light/military_door_outside` / `light` | — | (−19, −22, −8) / (19, 27, 10) | 500 | 28 / 10 | computed / 1 | 2 | |
| `light_generic_street_modern` | 20 `objects/light/street_modern` / `modern` | — | (−88, −75, −218) / (75, 75, 17) | 500 | 28 / 10 | computed / 1 | 0 | not solid |
| `light_generic_street_old` | 21 `objects/light/street_old` / `old` | — | (−7, −7, −48) / (7, 7, 48) | 500 | 28 / 10 | computed / 1 | 2 | |
| `light_generic_candelabra_small` | 7 `objects/light/candle2` / `base` | — | (−10, −5, −17) / (10, 5, 0) | 500 | 28 / 10 | computed / 1 | 2 | Quake II toggle only |

The skin column applies to the skin material of the entry (`light` for the desk lamp); "skinnum" is the entity's
skin number (key `skin`).

##### L.2.1 Model changes per class
| classname | initial state with spawnflag 1 | off: | on: |
|---|---|---|---|
| `light_generic_alley_wall` | seq `light_off`, hide `BEAM` | seq `light_off`, hide `BEAM` | seq `light`, show `BEAM` |
| `light_generic_alley_wall_old` | none (looks on) | seq `light_off`, hide `SPRITEBALL_FLASH01` | show `SPRITEBALL_FLASH01` (sequence unchanged) |
| `light_generic_caged` | hide `SPRITEBALL_FLASH02`, `LIGHTBULB01` | hide `SPRITEBALL_FLASH02`, `LIGHTBULB01` | show both |
| `light_generic_desk_lamp` | none (looks on) | seq `lamp_off`, hide `SPRITEBALL_FLASH01` | seq `lamp`, show `SPRITEBALL_FLASH01` |
| `light_generic_lantern2` | hide `SPRITEBALL_FLASH01` | seq `lantern2_off`, hide `SPRITEBALL_FLASH01` | seq `lantern2`, show `SPRITEBALL_FLASH01` |
| `light_generic_lantern3` | hide `SPRITEBALL_FLASH01` | seq `lantern3`, hide `SPRITEBALL_FLASH01` | seq `lantern3`, show `SPRITEBALL_FLASH01` |
| `light_generic_military_door` | none | hide `SPRITEBALL_FLASH01`, `LIGHT_BULB01` | show both |
| `light_generic_military_door_outside` | none (looks on) | hide `SPRITEBALL_FLASH01`, `BEAM01`, `LIGHT_BULB01` | show all three |
| `light_generic_street_modern` | none (looks on) | seq `modern_off`, hide `SPRITEBALL_GLOW`, `BEAM` | show `SPRITEBALL_GLOW`, `BEAM`, seq `modern` |
| `light_generic_street_old` | none (looks on) | seq `off`, hide `SPRITEBALL_FLASH` | show `SPRITEBALL_FLASH`, seq `old` |
| `light_generic_halfshell`, `light_generic_candelabra_small` | none | — | — |

"none (looks on)": the class does not apply its off state at spawn, so a fixture placed with spawnflag 1 shows its
lit model while its light style is dark until it has been toggled twice (original quirk). For
`light_generic_alley_wall_old`, once switched off the `light_off` sequence stays when it is switched on again.
All sequence and part names exist in the shipped model files (sequences are the `.ghb` files of the model directory,
e.g. `ghoul/objects/light/desk_lamp/lamp_off.ghb`).

##### L.2.2 Damage
Standard light damage rules (L.1): with spawnflag 2 the fixture has the table health 500 (or key `health`) and breaks
with the standard break; the light style is not changed by the break.

##### L.2.3 Bulb-blow death (`light_generic_caged`, `light_generic_lantern2`)
When health drops to 0 or below with a hit of `damage`:
1. If (health after the hit) − damage < −100 ("overkill"): sparks (TE 14) at the fixture origin, direction up,
   multicast PHS, then the standard break with computed debris (§4.1). The light style is not changed.
2. Otherwise:
   - if the fixture is on (spawnflag 1 clear): sparks (TE 14) at the origin, direction up (PHS), and
     configstring 1159 + style := `z` (full brightness flash);
   - 0.2 s later: configstring 1159 + style := `a` (dark), health := 50, and the death behaviour becomes the
     standard break (so a further 50 damage breaks the fixture).
3. Finally, if the fixture is on, part `SPRITEBALL_FLASH01` is hidden. `CAGED.ghb` has no part of that name, so the
   caged fixture keeps showing its glow after the bulb blew (original quirk); the lantern loses its glow.
Spawnflag 1 is not changed by the bulb blow: a later use toggles the style as usual (to `a` if it was on, so the
light stays dark; switching it once more sets `m` again, UNVERIFIED in game). With spawnflag 1 set (fixture off)
step 2 only arms the 0.2 s change (style `a`, health 50).

#### L.3 Swinging and hanging fixtures (forced damageable)

These classes always force spawnflag 2 on (so they always take damage and react to hits). If the map did **not**
set spawnflag 2 their health is set to 999999 (practically unbreakable, they only react); with spawnflag 2 set by the
map they keep the table/key health and break normally. They are frozen ("pause") at spawn and animate only when hit
or toggled.

| classname | model (table 5) | skins (skinnum → skin) | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `light_generic_chandelier` | 8 `objects/light/chandelier` / `swing` | 0 → `light`, 1 → `light2` (material `light`) | (−24, −21, 0) / (25, 21, 51) | 999999 or 500 | 28 / 10 | computed / 1 | 2 | forces +2; swing rotation |
| `light_generic_hanging` | 13 `objects/light/hanging` / `hanging` | — | (−10, −10, −6) / (10, 10, 44) | 999999 or 500 | 28 / 10 | computed / 1 | 2 | forces +2; ricochet sound |
| `light_generic_lantern` | 14 `objects/light/lantern` / `lantern` | — | (−7, −6, −22) / (7, 6, 22) | 999999 or 500 | 28 / 31 | computed / 1 | 2 | forces +2 |
| `light_generic_shop` | 19 `objects/light/shop` / `shop` | 0 → `light`, 1 → `light2` (material `light`) | (−19, −23, −4) / (19, 22, 32) | 999999 or 500 | 28 / 10 | computed / 1 | 2 | forces +2; ricochet sound |
| `light_generic_lamp_hang` | 22 `objects/light/lamp_hang` / `lamp` | — | (−6, −7, −10) / (5, 7, 15) | 999999 or 500 | 28 / 10 | computed / 1 | 2 | forces +2; spawnflags 8, 16 |

**Ricochet sound** (hanging, shop): on every hit, sound `Weapons/FX/Rics/ric<n>.wav` with n random 1…3, on the
fixture, channel 2, volume 0.6, attenuation 1.

##### `light_generic_chandelier`
- Spawn: paused at spawn time; the start-off state is not shown on the model (no part is hidden at spawn).
- Off: hide `SPRITEBALL_FLASH01` … `SPRITEBALL_FLASH04`; on: show all four (no sequence change).
- Hit (pain): plays sequence `swing` in loop mode and drives a **swing rotation** through the entity effects bit
  0x0001 data of SPEC-400 (values v0…v7 of the effect slots):
  1. d = normalize(attacker origin − fixture origin), h = hit point − fixture origin, z = third component of
     d × h (cross product). If z = 0 the rotation is not changed.
  2. s = +1 if z < 0, s = −1 if z > 0.
  3. If the rotation is not yet active: effects bit 0x0001 is set, all twelve slot values are cleared, v0 = 999,
     v1 = v2 = 0, v3 = s × 2560 (5 × 512), v4 = the fixture's own Ghoul instance handle, v5…v7 = 0.
     If it is active: v3 := trunc(v3 + s × 0.1 × damage × 512), then clamped to −2560 … 2560.
  4. The swing check runs 0.5 s after every hit and then every 0.5 s: v3 := trunc(v3 × 0.75); when |v3| < 153.6
     the rotation ends (effects bit 0x0001 cleared, all twelve values zeroed) and the check stops.
  The `swing` sequence keeps looping after the rotation ends.
- Death: standard break (only when the map set spawnflag 2).

##### `light_generic_hanging`
- Spawn: start-off → seq `hanging_off`, pause, hide `SPRITEBALL_FLASH01`; else pause.
- Off: seq `hanging_off`, pause, hide `SPRITEBALL_FLASH01`; on: seq `hanging`, pause, show `SPRITEBALL_FLASH01`.
- Hit: ricochet sound, then seq `hanging_off` (if off) or `hanging` (if on), once-and-hold (one swing, then rest).

##### `light_generic_lantern`
- Spawn: paused (start-off state not shown).
- Off: seq `lantern_off` then pause; on: seq `lantern` then pause.
- Hit: seq `lantern_off` (if off) or `lantern` (if on), once-and-hold. No sound.

##### `light_generic_shop`
- Spawn: paused (start-off state not shown).
- Off: seq `shop_off` then pause; on: seq `shop` then pause.
- Hit: ricochet sound, then seq `shop_off` / `shop` once-and-hold.

##### `light_generic_lamp_hang`
- Spawn, by spawnflags (checked in this order):
  - 8: hide parts `HANDLE_LANTERN` and `ROPE_LANTERN`, pause, no hit reaction (the lamp stands or sits), relinked;
  - 16: pause (static, still reacts to hits);
  - otherwise: after a random whole number of seconds 0…4 (rand mod 5), sequence `lamp` starts in loop mode
    (continuous sway, desynchronised between lamps).
  The start-off state is not shown at spawn.
- Off: hide `SPRITEBALL_FLASH01`, `SPRITEBALL_FLASH02`, seq `lamp_off`; on: show both, seq `lamp`. The sequence
  loops unless spawnflag 16 is set (then once-and-hold).
- Hit (not with spawnflag 8): seq `shot` (if on) or `shot_off` (if off) once-and-hold; when that sequence reaches
  its end (Ghoul note `EOS`, SPEC-014) the lamp returns to: pause if spawnflag 16, else sequence `lamp` in loop
  mode (also when the lamp is off — original quirk; the glow parts stay hidden, UNVERIFIED visually).

#### L.4 Emergency lights

| classname | model (table 5) | skins | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `light_generic_emergency` | 10 `objects/light/emergency` / `emergency` | — | (−6, −6, −5) / (6, 6, 5) | 500 | 28 / 10 | computed / 1 | 2 | precaches `Ambient/Models/Alarm/alarm1.wav`; model × 1.75 |
| `light_generic_emergency_blue` | 11 `objects/light/emergency_blue` / `emergency` | — | (−6, −6, −5) / (6, 6, 5) | 500 | 28 / 10 | computed / 1 | 2 | same |

- Spawn: after the setup the model is additionally scaled by 1.75 (on top of any `scale` key; the box is not
  changed). If on (spawnflag 1 clear): loop sound `Ambient/Models/Alarm/alarm1.wav` (full volume, attenuation class
  0), and after a random 0.0…0.4 s (multiple of 0.1) the sequence `emergency` is restarted in loop mode (random
  rotation phase). If off: seq `emergency_off`.
- Off: seq `emergency_off`, loop sound removed. On: seq `emergency` (loop), loop sound `alarm1.wav` again, and
  after a random 0.0…0.9 s (multiple of 0.1) the `emergency` sequence is restarted from that time in loop mode.
- Both classes behave identically (the blue one only has its own model).
- Damage: standard light rules (L.1).

#### L.5 `light_generic_candelabra` (shootable candles)

| classname | model (table 5) | skins (skinnum → skin) | box mins / maxs | health | surf / mat | debris n / s | solid | notes |
|---|---|---|---|---|---|---|---|---|
| `light_generic_candelabra` | 6 `objects/light/candle` / `base` | 0 → `candle`, 1 → `candle2` (material `candle`) | (−11, −11, −55) / (11, 11, 23) | 500 | 28 / 10 | computed / 1 | 2 | forces +2 (always damageable); 3 bolt-on candles |

- Spawn: r = random 0…2. Three candles are attached as bolt-on parts (§5.6) of the base, each aligned by its own
  bolt `DUMMY01`: bolt `DUMMY01` gets table-5 entry 3 + (r mod 3), `DUMMY02` entry 3 + ((r + 1) mod 3), `DUMMY03`
  entry 3 + ((r + 2) mod 3) (entries 3, 4, 5 = `candle`, `candle2`, `candle3`), so the three candle shapes appear in a
  random rotation. Bolt-on indices 1, 2, 3 = bolts `DUMMY01`, `DUMMY02`, `DUMMY03`.
- Shootable parts (§5.7): the base entry gets a part table of three rows (bolt `DUMMY01` → bolt-on 1, `DUMMY02` → 2,
  `DUMMY03` → 3). A candle hit is hidden and replaced by: debris temp entity (TE 18) at the candle position,
  direction (0, 0, 1), 5 chunks, size 1, chunk type 31, extents 0, 0, 0, surface type byte 35 (the client picks the
  break sound from it, SPEC-401); plus a coloured puff (TE 45) (120, 120, 120, 200) there; both PHS.
- Use (toggle): off: on every candle 1…3 hide `SPRITEBALL_FLASH03` and `SPRITEBALL_FLAME01`; on: show them on
  every candle that has not been shot off. Then the style toggle (L.1). Spawnflag 1 hides nothing at spawn.
- Death: standard break of the whole candelabra.

### 9. Other classes

- **`misc_exploding_heli`**: the spawn behaviour does nothing. The entity stays allocated with its map keys
  (classname, targetname …) but has no model, no box, no callbacks and is never linked, so it is invisible, not
  solid, takes no damage and ignores being fired. No shipped map places it
  (Appendix B).
- `misc_test_gun_auto` is described with the automatic guns (§6A.7).

## Constants
All numeric constants are given in place, per class, in the family tables and rules above; model-table defaults are in Appendix A; the shared numbers of §3–§5 are summarised here:

| Constant | Value | Where |
|---|---|---|
| model-table entry size | 40 bytes | §2 |
| model tables (number: entries) | 0: 14, 1: 15, 2: 121, 3: 15, 4: 41, 5: 23, 6: 5, 7: 11, 8: 12, 9: 15, 10: 12, 11: 5, 12: 3 | §2 |
| savegame sequence reference | table × 65536 + index | §2 |
| prop mass | 800 | §3 step 11 |
| prop clip mask | 0x02030003 | §3 step 6 |
| computed break count | trunc(V / 9000 + 2), clamped 10…20; size class 1 | §4.1 |
| break message sound | `Misc/Talk.wav`, channel 0, volume 0.6, attenuation 1 | §4.1 |
| player push | 100 units/s, friction 0 for 0.1 s, then 1 | §5.1 |
| knock over | angular rate 70°/s per unit of direction, vz 150, elasticity 0.7, ×1.6 per 0.1 s, stop at ±90°, recheck 0.3 s while health < 25 | §5.2 |
| throw away | rand(4,10)·damage horizontal, rand(4,6)·damage vertical and yaw rate; spin variant rand(10,15)·damage, 200 | §5.3 |
| prop explosion | radius damage 100 / 100 units, ignite radius 100, noise 200/200, flash radius 10·health max 0.9 duration 0.5, shake 100 / 200 / 30 | §5.4 |
| removed part debris | 5 chunks, size 1, puff colour (120, 120, 120, 200) | §5.6 |
| shootable part margin | 2 units | §5.7 |
| thrown-off linked parts | rand(75,125) units/s outward, +50 up, yaw ±20, lifted 15 | §5.7 |
| fall over | 30°/s start, +30°/s per 0.1 s, snap to 90° after 85°, axis test 0.707 | §5.8 |
| bolt-on parts per prop | 6 | §5.6 |
| detail-switch spawnflag | 64 with cvar `gl_pictip` ≠ 0 (`misc_*` only) | §3 |

## Test vectors / how to verify
### Shared framework (§2–§5)
1. **Box rotation.** A prop whose class box is (−10, −20, 0)–(10, 20, 30) placed with `angles` 0 90 0 ends with box
   (−20, −10, 0)–(20, 10, 30); a box (0, −20, 0)–(10, 20, 30) with yaw 180 becomes (−10, −20, 0)–(0, 20, 30); with
   yaw 45 the box is unchanged (only right angles rotate). Check with a developer box display or by walking into the
   prop from each side.
2. **Computed debris count.** A table entry with count 0 on a 20×20×30 box: V = 12000 → trunc(12000/9000 + 2) = 3 →
   clamped to 10 chunks. A 100×100×100 box: V = 10⁶ → 113 → clamped to 20. Count the chunks of the TE 18 burst in a
   network capture (count byte).
3. **Damageable / pushable flags.** A non-light prop with spawnflag 1 takes no damage (shots only make impact
   effects); with spawnflag 2 it does not slide when the player walks into it; a `light_generic_*` prop without
   spawnflag 2 cannot be destroyed, with spawnflag 4 it can be pushed.
4. **Player push.** Walking into a pushable prop moves it about 10 units per frame of contact (100 units/s for 0.1 s,
   then friction 1); a character or a thrown object touching it does not move it.
5. **Break message.** A prop with key `message` "hello" that is shot to death centre-prints "hello" to the attacker
   with the `Misc/Talk.wav` sound and fires its `target`s with the prop as activator.
6. **Material flags.** A map containing a prop whose (table or key) debris material is 17 has characters 15, 16 and
   17 of configstring 16 set to `y` (SPEC-760 §B1.5).
7. **Detail switch.** With `gl_pictip 1`, a `misc_*` prop with spawnflag 64 is absent after the level loads; a
   `light_generic_*` prop with spawnflag 64 is still present (prefix rule).
8. **Savegame reference.** A prop using generic-table entry 6 saves the sequence reference 2·65536 + 6 = 131078
   (SPEC-752); loading restores the same model.
9. **Prop explosion.** An exploding prop (any class of this spec that uses the standard explosion) damages a player
   standing 50 units from its origin by about 100·(1 − 50/100) = 50 before armour (SPEC-720 linear radius damage),
   shakes the view of players within 200 units, and — when it explodes because its health reached 0 — blinds nobody.
10. **Knock over.** Shooting a tippable prop from the −x side makes it pitch forward (positive pitch rate) whatever
    its yaw, accelerate ×1.6 per frame and stop at exactly 90°.

### Generic classes, part 1 (§6A)

1. Barbed wire: stand in a `misc_generic_barbwire_coil` for 1 s → about 10 health lost, obituary "barbed wire"
   (MOD 30) on death; shooting the coil does nothing unless the mapper set spawnflag 1.
2. Burning barrel with spawnflag 4: walking against its side does no damage; standing on top costs 1 health per
   frame (fire obituary).
3. Chemical barrel with spawnflag 4 and no `health` key: one bullet (health 1) → flash, airexplode effect, debris;
   a player 64 units away takes about 350 · (1 − d/128) damage (d = centre distance); camera shake within 1000
   units. Without spawnflag 4 it tips away from the shooter (k = 4) and lies on its side.
4. `misc_generic_bench` hit from behind with free space behind it: it falls to pitch 70 and its top swings; a bench
   against a wall does not move but the top can still be shot off (5 chunks + grey puff).
5. `misc_generic_chair_legs`: shoot legs 1 and 2 → the chair tilts 40° in pitch; shoot legs 1 and 3 → the other two
   legs pop off as well and the seat lies flat.
6. `misc_generic_chair_office`: shoot the seat edge → it spins and squeaks while fast, slowing by ×0.75 every
   0.5 s; it stops when the spin value is below 153.6. Example: damage 20 from rest → v = 512 · 2 = 1024; the
   thinks (every 0.5 s) give 768, 576, 432, 324, 243, 182, 136, and the record is cleared at the 7th think (3.5 s).
   only the first think can squeak (v = 1024 is in 1001…2000: probability 3/7), none afterwards.
7. `misc_generic_car_american` skin 0 with spawnflag 8: police siren sound loops and `car` sequence loops; skin 1
   shows the taxi light, skin 2 neither siren nor taxi parts. Rotate the car 90° in yaw: its box stays aligned to
   the world x axis (unrotated) — verify by walking around it.
8. Cash register: first shot → two spark showers, `registerd` skin, smoke for 2 s; later shots only damage it.
9. Monitor: first shot → small explosion, glass sound, buzz loop and arcs for 6–12 s, then silence; kill it → glass
   debris, sparks, medium explosion.
10. Fire extinguisher: kill it → `damaged` skin, leak sound and effect; shoot again within ~5 s → burst damaging
    within (0.5·h + 60) units with MOD 26; if left alone the leak stops after ~5 s and one more shot breaks it.
    Player-use on it → leak for ~10 s without the effect.
11. Fish with `angles` = a point 200 units away: it swims there and back at 20 u/s, turning within 10 units.
12. Big fan with `speed 31`: toggled on, it reaches full speed after 31 / 0.05 = 620 steps = 62 s.
13. `misc_generic_gun_auto` with spawnflag 4: approach within 400 units in its line of sight → wake sound, it turns
    at 45°/s, then fires 6 bursts of 3 double shots (light bullets, SPEC-782 entry 21) 0.5 s apart, pauses 3 s.
    Kill it (spawnflag 1 set) → autogun death effect, explosion with flash blinding a player within 50–200 units.
14. `misc_generic_gun_castle` with spawnflags 4|16: it wakes `wait`/2 − 5 s after spawn (`Wake2.wav`), searches 5 s
    later; firing it with its targetname stops it; it can never be destroyed.
15. Garbage bag: kill it while standing to one side → the soda cans fly toward you.

### Generic classes, part 2 (§6B)

1. Round table rotated 90° in yaw (`angles 0 90 0`): its collision box stays (−27,−23,−16)/(27,23,16) (not swapped),
   while a plain prop such as `misc_generic_radio` with the same angles gets x-range [−14, 14], y-range [−4, 4].
2. Orange cone shot once from the −x side with a pistol: it jumps straight up (vz 250), pitches forward, the skin
   changes to `damaged`; it stops at ±100° and then has the box ±7; a second shot (any damage) breaks it into 5
   chunks of size 2 (orange-cone chunk material 31).
3. Wet floor sign: first shot tips it (vz 160); the pitch stops at 128° or the roll at 90°.
4. Ash tray hit by a 30-damage shot: ricochet sound; it flies off with |v_xy| = 180 · |d_xy| (k = 6) and vz 150 and
   tumbles; the second shot sends it spinning in yaw (200 · d.y) with speed rand(10, 15) · 9.
5. Sofa: unlimited shots never destroy it; each shot with damage D pushes it 2·D units/s away from the shooter.
6. Sink: first shot plays the ceramic break and changes the skin; killing it leaves a broken sink with skin `damaged`
   that shatters (8 chunks) on its first touch, while a water spout plays for about 3 s; targets of the sink do not
   fire.
7. Toilet: first shot sets the skin `damaged`; the second (any damage) destroys it with the spout effect
   `environ/toiletspout` and the spray sound for 3 s; its targets fire.
8. Urinal: pressing use near it plays the flush sound every time.
9. Security camera with spawnflags 4 + 8, `speed 10`, `count 45`: n = 9; it turns +40° in 0.4 s, then swings
   between spawn yaw − 50° and + 40° with 0.9 s per leg; standing in front of it within 30° in clear view plays the
   alarm and fires its targets once per second. With the defaults (speed 5, count 90) it swings ±90°, 3.6 s per leg.
10. Speaker killed by a shot: it drops, spins in yaw, shows a small explosion and emits 4–9 spark bursts 0.1–3.0 s
    apart, each hurting entities whose centre is within 10 units by up to 20 (means of death 26).
11. Wall TV shot once: it falls from the wall, sparks, the face parts disappear and the buzz loop starts; shooting it
    to death gives a medium explosion (sound `Impact/Explosion/Med.wav`).
12. Ceiling TV: first shot breaks the screen, which then swings down within about 0.5 s; a second shot drops the
    TV (supports disappear); when it hits the floor a small explosion with sound plays.
13. Table 1 (`misc_generic_table1`): shoot one leg → it vanishes silently; shoot the second leg on the same side
    (`DUMMY01`+`DUMMY02`) → debris, grey puff and the table rolls 30°. A grenade lying on the table (it touched the
    table) is thrown off by the first shot that hurts the table, even one that hits no leg.
14. Gas tank triggered by a `trigger_once`: explosion (radius damage 100 in 100), fire, air-explode effect, and
    players within 80 units are flash-blinded; shooting it instead gives the same explosion without blinding.
15. Propane tank with spawnflag 2: explodes and disappears without debris and without firing its targets.
16. Train with spawnflags 4, `style 600`, `accel 100`, in a straight tunnel: starts at 100 units/s, reaches 600
    units/s after 0.5 s (5 steps of +100), stops with its front 167 units short of the tunnel's end wall; a player
    standing on the track in front of it is killed (means of death 22); an item lying on the track is removed on
    contact.
17. Trees/truck cab killed: the model breaks into its parts on the client (shatter bit) and vanishes 0.2 s later;
    no debris chunks are sent.

### Kosovo/Bosnia, castle and Iraq classes (§7A)

1. `misc_castle_globe`, shot for damage 50 from a point whose cross product gives c.z < 0: a spin record with
   v2 = trunc(512 · 5) = 2560 appears; after 0.5 s v2 = 1920, then 1440, 1080, 810, 607, 455, 341, 255, 191, 143 →
   cleared at the 10th spin-down step (5 s after the hit). A second hit of damage 400 while spinning adds 20480
   (no cap), a first hit of damage 400 starts at the cap 30 · 512 = 15360.
2. `misc_bosnia_searchlight` with default health: no visible change after 149 damage; at 150 cumulative damage
   (health 850 = trunc(0.85 · 1000)) the lamp glass switches to `bkglass`, the beam parts vanish, a small explosion
   appears 80 units above the origin and `environ/tv_smoke` starts; each later hit only repeats the small explosion;
   at 1000 damage it breaks into computed debris.
3. `misc_castle_chair`: shoot leg 1 → it vanishes without debris, chair unchanged; then leg 2 → the chair pitches
   +40° with box (−10, −9, −6)/(10, 9, 18); then leg 3 → leg 4 is removed with 5 chunks and a grey puff and the chair
   returns to its spawn angles with box (−10, −9, −2)/(10, 9, 18) (FLAT) and drops 13 units to the floor
   (movetype 10).
4. `misc_bosnia_truck_old` spawnflag 4, yaw 0, `distance` 512, `speed` 200: after use the velocity grows by 30 every
   0.1 s (30, 60, …, 210 → cruise at 210, the first value ≥ 200); braking starts once the remaining distance is
   < 3 · 21 = 63 units; each braking frame covers a quarter of the rest; the truck stops within about 1 unit of the
   destination with velocity 0 and movetype NONE. With yaw 90 the truck drives toward −y while the destination lies
   at +y, so it never brakes (quirk check).
5. `misc_iraq_fruit_stand` with spawnflags 0: five melons and five oranges appear at the listed offsets (world
   axes); shooting one orange removes all ten fruits at once and leaves the stand; destroying a fresh stand instead
   throws oranges and melons 4–5 at once, melons 1–3 0.1 s later, with upward speeds 200–500.
6. `misc_bosnia_tank_jack`: a player walking (speed > 62.5) into it takes 1 damage per frame of contact
   (MOD 26 obituary "explosive" text); standing still against it takes none.
7. `misc_iraq_valve` (no spawnflag 128): each use plays `ValveTurn.wav` (volume 0.6, channel 2) and one run of the
   `valve` sequence, after which the model holds its last frame.
8. `misc_bosnia_landmine` (resting on the floor): walking onto it gives the standard prop explosion (100 damage
   within 100 units, `weapons/world/airexplode`, explosion debris TE 19, ignition within 100 units, camera shake),
   flash blinding only within 10 units, a scorch decal (TE 15) under it, and a debris burst (TE 18, chunk type 12,
   computed count 10 for its 26 × 26 × 4 box, size 1) toward the player; the mine is removed.

### New York, Siberia, Tokyo and Uganda classes (§7B)

1. `misc_newyork_burned_car` killed with one 10000-damage hit: one debris message (TE 18) from the box centre,
   20 chunks, size class 1, chunk type 10, extents (91, 33, 25), surface type 28; targets fire; entity removed.
2. `misc_newyork_gumball` (key `skin` absent): first hit of any damage → skin `gumball2`, glass sound
   `Impact/GlassBreak/GBSmall.wav` (channel 5, 0.6, 1), one TE 18 of 5 glass chunks 10 units above the origin and three
   spark messages (TE 31, 49, 31); health becomes 1. Second hit → table break: 10 chunks of type 11. With `skin` 1 hits
   only reduce the health (1000); there is no glass effect before the break.
3. `misc_newyork_trashcan`: a hit 15 units above the origin → TE 59 at the origin, sequence `trashcan` restarts,
   health unchanged; a second lid hit within 0.5 s (of the last accepted pain of any trash can) takes normal damage and
   shows nothing. Hits below +10 reduce health
   (300); at 0: three TE 45 (200, 200, 200, 250), TE 48 (count 0), TE 50 (speed 100, count 0) towards the attacker,
   TE 18 with 10 chunks of type 10.
4. `misc_newyork_trash_pile` shot four times with small damage: items `_CAN1`, `_CAN2`, `_SHOE`, `_OILCAN` leave in
   that order (one ricochet sound and one grey TE 45 per hit at bolts `DUMMY01`…`DUMMY04`), each flying item is a
   10 × 10 × 10 prop removed within 30 s; after the fourth hit the pile vanishes with no debris and its targets do not
   fire. A pile placed with `count` 2 loses `_SHOE` first and vanishes after two hits.
5. `misc_siberia_roadflare` with `targetname` and spawnflag 4: dark and silent at start; first trigger → loop sound
   `Ambient/Gen/FireFX/FlareLP.wav` and effect `environ/roadflare` in an effect slot; second trigger → both gone.
   Spawnflag 8 → `environ/roadflare_blue`.
6. `misc_siberia_searchlight` whose `target` names an `info_null` placed earlier in the map at 45° up and 90° to
   the left: after spawn the entity faces it (yaw +90°) and the lamp points up at it (about 45°, measured at the lamp
   bolt); no later motion.
7. `misc_siberia_snowcat` with spawnflag 4 shows two `objects/generic/beam` models at its headlights; without it none.
8. `misc_tokyo_gong`: model frozen at spawn; each hit plays `gong` once; 1000 damage breaks it with 10 chunks of
   type 11, size class 1.
9. `misc_tokyo_samurai_suit` and `_small` render the same model at 1.1× and 0.5×; their boxes are those of the table.
10. `misc_uganda_gutted_cow` with spawnflags 8 + 16: dripping-blood effect slot `environ/bloodrain`; swinging starts
    0–9 s after load; shooting it from straight in front plays `swing1`, from its side `swing2`, after which the
    `gutted_cow` loop resumes. With spawnflag 4 only: effect `gore/bloodpool` 2 s after load; with 4 + 16: no pool.
    With `lock_blood 1`: no pool, and its death sends no debris message (chunk type 32).
11. `misc_uganda_nuke` and the three train parts take no damage (spawnflags forced) and are never moved by players.
12. `misc_uganda_gore_pile` hit by a player standing 64 units away in front of a wall: a TE 3 blood decal on the
    wall behind the hit point (none with `lock_blood 1`); killing it (300) sends no debris (material 0).

### Light fixtures (§8)
1. `light_generic_desk_lamp` with `style 32`, `targetname t`, no spawnflags: configstring 1191 = `m` at spawn; fire
   `t` → `SPRITEBALL_FLASH01` hidden, sequence `lamp_off`, configstring 1191 = `a`; fire again → `m`.
2. Same lamp with spawnflag 1: configstring 1191 = `a` at spawn but the model still shows the lit sequence and glow.
3. `light_generic_caged`, spawnflags 2, health 500, on, `style 32`: ten hits of 50 → on the tenth (health 0;
   0 − 50 = −50, not overkill) sparks (TE 14) + configstring 1191 = `z`, 0.2 s later `a` and health 50; one more hit
   of 50 breaks it (TE 18, chunk type 10, count 10 after the clamp). A single hit of 700 on a fresh one (health −200;
   −200 − 700 < −100) → sparks + break, configstring 1191 stays `m`. Note that from full health 500 a single hit can
   never give the bulb blow (it needs damage ≥ 500 and ≤ 300 at once).
4. `light_generic_chandelier` (no spawnflags) hit from the side: effects bit 0x0001 appears with v0 = 999,
   v3 = ±2560, v4 = its instance handle; with no further hits v3 decays 2560 → 1920 → 1440 → 1080 → 810 → 607 → 455 →
   341 → 255 → 191 → 143 at 0.5 s steps and the bit clears 5.0 s after the hit.
5. `light_generic_hanging` hit: one of `ric1.wav`…`ric3.wav` at volume 0.6, then a single swing. It survives any
   amount of normal fire (health 999999) unless the map set spawnflag 2.
6. `light_generic_emergency`: the alarm loop is audible while on; using it removes the sound and plays
   `emergency_off`.
7. `light_generic_candelabra`: shooting each candle removes it with a 5-chunk burst and grey puff; switching the
   candelabra on again relights only the remaining candles.
8. `light_generic_street_modern` / `light_generic_alley_wall`: bullets pass through (not solid).

## Open questions
### Shared framework
- Combined right-angle rotations (more than one of pitch, yaw, roll a multiple of 90): the box result relies on an
  original quirk (§3 step 5); no shipped map is known to use it. Verify by searching the shipped `.bsp` entity lumps
  for props with two non-zero right angles.
- Spawnflag 64 + `gl_pictip`: whether this is meant as a detail-reduction switch (SPEC-006 open question).
- The flash part of the standard prop explosion can only blind when the explosion is started with positive health
  (§5.4 step 5); whether any shipped class does so is listed per class.

### Generic classes, part 1 (§6A)

- Client side of the spin record (effect slot id 999; office chair, big fan): which axis the client applies each of
  the three values to and in which units (the server writes degrees-per-something × 512). Verify with the fan
  (vector (0, s, 0)) and the office chair (vector (s, 0, 0)) in game.
- Exact pose of the bench top after it tips (order of the offset and rotation in its new transform).
- Bunk/chair reactions set boxes without the right-angle rotation — do rotated bunks in shipped maps end up with
  wrong boxes? (check `misc_generic_bunk` with yaw 90 after shooting a side).
- Fish sink height uses the key `volume` as a world z; likely a reused field (default 0). Check shipped maps for
  `misc_generic_fish` with `volume`.
- Which castle-gun pair receives the 20-unit aim offset (see 6A.7).
- Entity effects bit 0x200 set by all turrets: client meaning not identified here.
- `misc_generic_chunks_*` other than brick, `misc_generic_access_card`, `barrel_wine`, `blinds`, `car_rolls`,
  `chair_legs2`: model files missing in 1.07f — confirm they appear as invisible solid boxes in game.

### Generic classes, part 2 (§6B)

- Unit of the radar dish's client-side turn rate (1.5 per second in SPEC-400's incremental branch): degrees or
  radians? Verify by timing a dish revolution in a map that uses `misc_generic_radar_dish`.
- Entity effects bit 0x200 (sink, broken sink) and renderfx bit 28 (urinal) have no catalogued meaning
  (SPEC-202 open catalogue). Verify by comparing the drawn sink/urinal with and without the bit.
- Whether the broken sink shatters on landing (touch by the world while falling) or only when touched by another
  entity (SPEC-760 B4.8 calls the mover's touch on impact; UNVERIFIED in game).
- The AI notification made by the train before its crushing damage (two character-AI calls; meaning not
  established). The damage of 9000 kills regardless.
- `misc_generic_wpn_*` boxes lie 55–60 units above the origin: verify in game that the collision box matches the
  drawn weapon (they are designed for weapons mounted on walls/racks, UNVERIFIED).
- Many skin files named by these classes are missing from the 1.07f archives (listed per class); the client
  behaviour for a missing skin (default skin or no texture) is a renderer question (SPEC-411).

### Kosovo/Bosnia, castle and Iraq classes (§7A)

- Whether the yaw-offset state field of props is ever non-zero (fruit placement in world axes, test 5 at a rotated
  stand would show it).
- The angle units of the spin record (SPEC-400): the tire rate speed · π / 94 matches a rolling wheel of radius
  ≈ 30 units if read as radians per second — verify by watching a spinning truck in game.
- Whether a dying vehicle with no death callback is handled gracefully by the damage code when hit again (SPEC-720
  §5; the wreck keeps health ≤ 0 and takes damage).
- `misc_bosnia_searchbeam` / `misc_bosnia_searchlight`: no aiming happens in 1.07f; check in a Bosnia level that the
  lights do not sweep.
- Models missing from the archives (Bosnia entries 0–5, 10, 11; Iraq entry 11): confirm the props are invisible but
  solid.

### New York, Siberia, Tokyo and Uganda classes (§7B)

- `misc_newyork_news_machine` and `misc_newyork_trash_pile` reference model files that are not in the 1.07f archives
  (Appendix A): verify in game whether they appear at all (developer print `SetSimpleGhoulModel couldn't …`); the
  pain behaviours above then act on an invisible prop.
- `misc_newyork_trash_pile` with key `count` ≥ 4 uses an undefined part name (original defect): verify that no
  shipped map sets it.
- `misc_tokyo_stone_light` zero-height box: check in game whether bullets and players collide with it.
- Debris size class 4 (`misc_tokyo_fan`): what the client draws (SPEC-401 chunk model index base + 3).
- `misc_siberia_searchlight` target lookup at spawn: check with a target entity placed after the searchlight in the
  map file (expected: no aim).
- Gutted cow / side of beef: whether the accumulated end-of-sequence reactions cause visible restarts of the loop
  after several hits.
- Gong: whether `gong` carries a sound note in the model (no sound comes from the game code).

### Light fixtures (§8)
- Light style 0 rewrite when a fixture without `style` is toggled (L.1) — confirm in game.
- Axis convention of the chandelier swing (third component of the rotation record; SPEC-400 open point on units).
- Whether shipped maps place `light_generic_alley_wall_old` / `light_generic_military_door` (models missing).

## Appendix A. Prop model tables

Entry index, model (`ghoul/<directory>/<file>.ghb`), defaults surface type / debris material / health / solid, skin
material, table-break debris count and size (§2, §4.2). "—" = none. Sizes are floats in the file; a size below 1
truncates to 0 and selects the computed break. Column "data": whether the model file is present in the 1.07f
archives `pak0.pak`…`pak3.pak` (case-insensitive); entries marked "no" spawn without a visible model in the shipped
game (the setup prints `SetSimpleGhoulModel couldn't …`; UNVERIFIED in game).

### A.1 Table 0 — Kosovo/Bosnia (14 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/bosnia/antipersonnel_jack` | `jack` | 15 | 18 | 500 | 2 | — | 0 | 0 | no |
| 1 | `objects/bosnia/flag_wall` | `flag_wall` | 15 | 18 | 500 | 2 | — | 0 | 0 | no |
| 2 | `objects/bosnia/landmine` | `landmine` | 28 | 12 | 1 | 2 | — | 0 | 0 | no |
| 3 | `objects/bosnia/rubble1` | `rubble1` | 17 | 15 | 1000 | 2 | — | 0 | 0 | no |
| 4 | `objects/bosnia/rubble2` | `rubble2` | 0 | 16 | 1000 | 2 | — | 0 | 0 | no |
| 5 | `objects/bosnia/searchbeam` | `null` | 0 | 0 | 1000 | 0 | — | 0 | 0 | no |
| 6 | `objects/bosnia/searchbeam` | `beam` | 0 | 0 | 1000 | 0 | — | 0 | 0 | yes |
| 7 | `objects/bosnia/searchlight` | `base` | 28 | 9 | 1000 | 2 | — | 0 | 0 | yes |
| 8 | `objects/bosnia/searchlight` | `arm` | 28 | 9 | 1000 | 2 | — | 0 | 0 | yes |
| 9 | `objects/bosnia/searchlight` | `lamp` | 28 | 9 | 1000 | 2 | `glass` | 0 | 0 | yes |
| 10 | `objects/bosnia/tank_jack` | `jack` | 28 | 9 | 500 | 2 | — | 0 | 0 | no |
| 11 | `objects/bosnia/truck_chunks` | `chunks` | 28 | 9 | 500 | 2 | `chunks` | 0 | 0 | no |
| 12 | `objects/bosnia/truck_old` | `truck` | 28 | 9 | 1000 | 2 | `truck` | 0 | 0 | yes |
| 13 | `objects/bosnia/truck_old` | `tire` | 28 | 9 | 1000 | 2 | — | 0 | 0 | yes |

### A.2 Table 1 — Castle (15 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/castle/armor` | `armor` | 28 | 9 | 500 | 2 | — | 0 | 1 | yes |
| 1 | `objects/castle/bust` | `bust` | 18 | 16 | 1000 | 2 | `bust` | 0 | 1 | yes |
| 2 | `objects/castle/cannon` | `cannon` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 3 | `objects/castle/chair` | `seat01` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 4 | `objects/castle/chair` | `leg01` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 5 | `objects/castle/chair` | `leg02` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 6 | `objects/castle/chair` | `back` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 7 | `objects/castle/chair_big` | `seat01` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 8 | `objects/castle/chair_big` | `leg01` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 9 | `objects/castle/chair_big` | `arm01` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 10 | `objects/castle/chair_big` | `back01` | 15 | 18 | 500 | 2 | — | 0 | 1 | yes |
| 11 | `objects/castle/globe` | `globe` | 0 | 25 | 1000 | 2 | — | 0 | 1 | yes |
| 12 | `objects/castle/globe` | `base` | 0 | 25 | 1000 | 2 | — | 0 | 1 | yes |
| 13 | `objects/castle/hedge` | `hedge` | 15 | 18 | 500 | 2 | — | 25 | 1 | yes |
| 14 | `objects/castle/tree` | `tree` | 15 | 18 | 1000 | 2 | — | 35 | 2 | yes |

### A.3 Table 2 — Generic (121 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/generic/access_card` | `card` | 0 | 0 | 500 | 2 | — | 0 | 1 | no |
| 1 | `objects/generic/banner_generic` | `banner` | 0 | 0 | 0 | 2 | `banner` | 0 | 1 | yes |
| 2 | `objects/generic/barbwire_coil` | `barbwire` | 28 | 0 | 500 | 2 | — | 15 | 1 | yes |
| 3 | `objects/generic/barrel_burning` | `barrel` | 28 | 9 | 260 | 2 | — | 0 | 2 | yes |
| 4 | `objects/generic/barrel_chemical` | `barrel` | 28 | 9 | 260 | 2 | `barrel` | 0 | 2 | yes |
| 5 | `objects/generic/barrel_wine` | `barrel` | 14 | 17 | 500 | 2 | — | 0 | 2 | no |
| 6 | `objects/generic/bench` | `bottom` | 14 | 17 | 250 | 2 | `bench` | 10 | 2 | yes |
| 7 | `objects/generic/bench` | `top` | 14 | 17 | 250 | 2 | `bench` | 10 | 2 | yes |
| 8 | `objects/generic/blinds` | `blinds` | 28 | 9 | 200 | 2 | `blinds` | 0 | 2 | no |
| 9 | `objects/generic/bookcase` | `case` | 14 | 17 | 500 | 2 | `case` | 0 | 3 | yes |
| 10 | `objects/generic/bookshelf` | `shelf` | 14 | 17 | 500 | 2 | `shelf` | 0 | 1 | yes |
| 11 | `objects/generic/bookshelf` | `middle` | 14 | 17 | 500 | 2 | `shelf` | 0 | 1 | no |
| 12 | `objects/generic/broom` | `broom` | 14 | 17 | 60 | 2 | `broom` | 0 | 1 | yes |
| 13 | `objects/generic/bunk` | `side2` | 28 | 8 | 500 | 2 | `bunk` | 0 | 2 | yes |
| 14 | `objects/generic/bunk` | `bed` | 28 | 8 | 200 | 0 | `bunk` | 0 | 2 | yes |
| 15 | `objects/generic/bunk` | `side1` | 28 | 8 | 200 | 0 | `bunk` | 0 | 2 | yes |
| 16 | `objects/generic/car_american` | `car` | 28 | 8 | 2000 | 2 | `car` | 0 | 1 | yes |
| 17 | `objects/generic/car_european` | `car` | 28 | 8 | 2000 | 2 | `car` | 0 | 1 | yes |
| 18 | `objects/generic/car_honda` | `car` | 28 | 8 | 2000 | 2 | `car` | 0 | 1 | yes |
| 19 | `objects/generic/car_rolls` | `car` | 28 | 8 | 2000 | 2 | — | 0 | 1 | no |
| 20 | `objects/generic/car_sports` | `car` | 28 | 8 | 2000 | 2 | `car` | 0 | 1 | yes |
| 21 | `objects/generic/cart` | `cart` | 28 | 8 | 1000 | 2 | `cart` | 0 | 1 | yes |
| 22 | `objects/generic/cash_register` | `register` | 28 | 8 | 500 | 2 | `register` | 0 | 1 | yes |
| 23 | `objects/generic/chair_legs` | `chair` | 28 | 8 | 400 | 2 | `legs` | 0 | 1 | yes |
| 24 | `objects/generic/chair_legs` | `legs` | 28 | 8 | 400 | 2 | `legs` | 0 | 1 | yes |
| 25 | `objects/generic/chair_legs2` | `legs2` | 28 | 8 | 400 | 2 | — | 0 | 1 | no |
| 26 | `objects/generic/chair_office` | `top` | 28 | 11 | 600 | 2 | `office` | 0 | 1 | yes |
| 27 | `objects/generic/chair_office` | `base` | 28 | 11 | 600 | 2 | `office` | 0 | 1 | yes |
| 28 | `objects/generic/chunks_brick` | `brick` | 20 | 2 | 50 | 2 | `brick` | 0 | 1 | yes |
| 29 | `objects/generic/chunks_glass` | `glass` | 37 | 7 | 25 | 2 | — | 0 | 1 | no |
| 30 | `objects/generic/chunks_metal` | `metal` | 28 | 8 | 100 | 2 | `metal` | 0 | 1 | no |
| 31 | `objects/generic/chunks_rock` | `rock` | 20 | 14 | 50 | 2 | `rock` | 0 | 1 | no |
| 32 | `objects/generic/chunks_wall` | `wall` | 20 | 14 | 10 | 2 | `wall` | 0 | 1 | no |
| 33 | `objects/generic/chunks_wood` | `wood` | 15 | 18 | 30 | 2 | `wood` | 0 | 1 | no |
| 34 | `objects/generic/comp_keyboard` | `keyboard` | 28 | 25 | 100 | 2 | — | 0 | 1 | yes |
| 35 | `objects/generic/comp_monitor` | `monitor` | 38 | 25 | 250 | 2 | `monitor` | 0 | 1 | yes |
| 36 | `objects/generic/comp_monitor2` | `monitor2` | 38 | 25 | 250 | 2 | `monitor2` | 0 | 1 | yes |
| 37 | `objects/generic/comp_monitor2` | `tower` | 38 | 25 | 250 | 2 | `monitor2` | 0 | 1 | yes |
| 38 | `objects/generic/crate_fish` | `crate` | 14 | 17 | 500 | 2 | — | 0 | 1 | yes |
| 39 | `objects/generic/desk_phone` | `phone` | 28 | 25 | 50 | 2 | `phone` | 0 | 1 | yes |
| 40 | `objects/generic/fan` | `fan` | 28 | 11 | 500 | 2 | — | 0 | 1 | yes |
| 41 | `objects/generic/faucet` | `faucet` | 28 | 11 | 150 | 2 | — | 0 | 1 | yes |
| 42 | `objects/generic/fire_extinguisher` | `exting` | 28 | 9 | 150 | 2 | `exting` | 0 | 1 | yes |
| 43 | `objects/generic/fire_hydrant` | `hydrant` | 28 | 9 | 500 | 2 | — | 0 | 1 | yes |
| 44 | `objects/generic/fish` | `fish` | 35 | 32 | 150 | 2 | `fish` | 0 | 1 | yes |
| 45 | `objects/generic/fish` | `fish` | 35 | 32 | 150 | 2 | `fish` | 0 | 1 | yes |
| 46 | `objects/generic/flag_pole` | `flag_pole` | 28 | 9 | 500 | 2 | `flag_pole` | 0 | 1 | yes |
| 47 | `objects/generic/forklift` | `forklift` | 28 | 9 | 2000 | 2 | `forklift` | 0 | 1 | yes |
| 48 | `objects/generic/garbage_bag` | `bag` | 0 | 25 | 100 | 2 | — | 0 | 1 | yes |
| 49 | `objects/generic/gun_auto` | `gun` | 28 | 9 | 2000 | 2 | `gun` | 0 | 1 | yes |
| 50 | `objects/generic/gun_auto` | `base` | 28 | 9 | 2000 | 2 | `gun` | 0 | 1 | yes |
| 51 | `objects/generic/gun_auto` | `arm` | 28 | 9 | 2000 | 2 | `gun` | 0 | 1 | yes |
| 52 | `objects/generic/gun_auto` | `null` | 28 | 9 | 2000 | 2 | — | 0 | 1 | yes |
| 53 | `objects/generic/gun_big` | `base` | 28 | 9 | 1 | 2 | `gun` | 0 | 2 | yes |
| 54 | `objects/generic/gun_big` | `gunleft` | 28 | 9 | 1 | 2 | `gun` | 0 | 2 | yes |
| 55 | `objects/generic/gun_big` | `gunright` | 28 | 9 | 1 | 2 | `gun` | 0 | 2 | yes |
| 56 | `objects/generic/gun_big` | `shield` | 28 | 9 | 1 | 2 | `gun` | 0 | 2 | yes |
| 57 | `objects/generic/heap_generic` | `heap` | 0 | 0 | 200 | 2 | `heap` | 0 | 1 | yes |
| 58 | `objects/generic/beam` | `beam` | -1 | -1 | 250 | 0 | — | 0 | 1 | yes |
| 59 | `objects/light/flare` | `flare` | -1 | -1 | 500 | 0 | — | 0 | 1 | yes |
| 60 | `objects/generic/motorcycle` | `motorcycle` | 28 | 9 | 2000 | 2 | `motorcycle` | 0 | 1 | no |
| 61 | `objects/generic/orange_cone` | `orangecone` | 21 | 31 | 500 | 2 | `orangecone` | 5 | 2 | yes |
| 62 | `objects/generic/palette_jack` | `pallette_jack` | 28 | 11 | 1000 | 2 | — | 15 | 1 | yes |
| 63 | `objects/generic/paper_blowing` | `paper` | 0 | 0 | 25 | 2 | — | 0 | 1 | yes |
| 64 | `objects/generic/parking_meter` | `parking_meter` | 28 | 11 | 500 | 2 | — | 0 | 1 | no |
| 65 | `objects/generic/phone_booth` | `phone_booth` | 28 | 11 | 1000 | 2 | — | 0 | 1 | yes |
| 66 | `objects/generic/pillow` | `pillow` | 21 | 31 | 100 | 2 | `pillow` | 4 | 1 | yes |
| 67 | `objects/generic/plant_potted` | `plant` | 0 | 11 | 200 | 2 | `plant` | 0 | 1 | yes |
| 68 | `objects/generic/plant_tall` | `plant` | 0 | 0 | 200 | 2 | — | 0 | 1 | yes |
| 69 | `objects/generic/radar` | `radar_base` | 28 | 9 | 5000 | 2 | `radar_base` | 0 | 1 | yes |
| 70 | `objects/generic/radar` | `radar_dish` | 28 | 9 | 5000 | 2 | `radar_dish` | 0 | 1 | yes |
| 71 | `objects/generic/radio` | `radio` | 28 | 9 | 100 | 2 | — | 0 | 1 | yes |
| 72 | `objects/generic/security_camera/test` | `camera` | 28 | 9 | 200 | 2 | `camera` | 10 | 1 | yes |
| 73 | `objects/generic/security_camera/test` | `arm` | 28 | 9 | 200 | 2 | `camera` | 10 | 1 | yes |
| 74 | `objects/generic/shovel` | `shovel` | 28 | 9 | 200 | 2 | `shovel` | 0 | 1 | yes |
| 75 | `objects/generic/shrub1` | `shrub` | 0 | 0 | 200 | 2 | `shrub` | 0 | 1 | yes |
| 76 | `objects/generic/sink` | `sink` | 17 | 15 | 200 | 2 | `sink` | 0 | 1 | yes |
| 77 | `objects/generic/sofa` | `sofa` | 0 | 0 | 2000 | 2 | `sofa` | 0 | 1 | yes |
| 78 | `objects/generic/sofa_chair` | `chair` | 0 | 0 | 1000 | 2 | `chair` | 0 | 1 | yes |
| 79 | `objects/generic/speaker` | `speaker` | 28 | 9 | 100 | 2 | — | 0 | 1 | yes |
| 80 | `objects/generic/stoplight` | `stoplight` | 28 | 9 | 2000 | 2 | `stoplight` | 0 | 1 | no |
| 81 | `objects/generic/street_name_sign` | `sign` | 28 | 9 | 500 | 2 | `sign` | 0 | 1 | no |
| 82 | `objects/generic/sword` | `sword` | 28 | 9 | 150 | 2 | — | 0 | 1 | yes |
| 83 | `objects/generic/sword` | `sword` | 28 | 9 | 150 | 2 | — | 0 | 1 | yes |
| 84 | `objects/generic/table_round` | `table` | 28 | 9 | 400 | 2 | `table` | 0 | 1 | yes |
| 85 | `objects/generic/table1` | `top` | 28 | 9 | 400 | 2 | `table1` | 0 | 1 | yes |
| 86 | `objects/generic/table1` | `back` | 28 | 9 | 400 | 2 | `table1` | 0 | 1 | yes |
| 87 | `objects/generic/table1` | `leg` | 28 | 9 | 400 | 2 | `table1` | 0 | 1 | yes |
| 88 | `objects/generic/table2` | `top01` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 89 | `objects/generic/table2` | `side01` | 28 | 9 | 1000 | 0 | — | 0 | 1 | yes |
| 90 | `objects/generic/table2` | `side02` | 28 | 9 | 1000 | 0 | — | 0 | 1 | yes |
| 91 | `objects/generic/table2` | `side03` | 28 | 9 | 1000 | 0 | — | 0 | 1 | yes |
| 92 | `objects/generic/table2` | `top02` | 28 | 9 | 1000 | 0 | — | 0 | 1 | yes |
| 93 | `objects/generic/tank1` | `tank` | 28 | 9 | 4000 | 2 | — | 0 | 1 | no |
| 94 | `objects/generic/tank_gas` | `gas` | 28 | 9 | 8 | 2 | — | 0 | 3 | yes |
| 95 | `objects/generic/tank_propane` | `propane` | 28 | 9 | 8 | 2 | — | 0 | 1 | yes |
| 96 | `objects/generic/toilet` | `toilet` | 21 | 31 | 200 | 2 | `toilet` | 0 | 2 | yes |
| 97 | `objects/generic/train_generic` | `train` | 28 | 9 | 9000 | 2 | `train` | 0 | 1 | yes |
| 98 | `objects/generic/trash_ashtray` | `ashtray` | 28 | 9 | 100 | 2 | — | 0 | 1 | yes |
| 99 | `objects/generic/trashcan_barrel` | `trashcan` | 28 | 9 | 200 | 2 | — | 0 | 1 | yes |
| 100 | `objects/generic/tree_dead` | `tree` | 15 | 18 | 1000 | 2 | — | 0 | 1 | no |
| 101 | `objects/generic/tree_thin` | `tree` | 15 | 18 | 500 | 2 | — | 0 | 1 | no |
| 102 | `objects/generic/truckcab` | `truckcab` | 28 | 9 | 3000 | 2 | — | 0 | 1 | yes |
| 103 | `objects/generic/tv_ceiling` | `tv` | 28 | 9 | 300 | 2 | `face` | 10 | 1 | yes |
| 104 | `objects/generic/tv_ceiling` | `supports` | 28 | 9 | 300 | 2 | — | 10 | 1 | yes |
| 105 | `objects/generic/tv_wall` | `tv_wall` | 28 | 9 | 300 | 2 | `tv_wall` | 10 | 1 | yes |
| 106 | `objects/generic/urinal` | `urinal` | 21 | 31 | 200 | 2 | `urinal` | 0 | 1 | yes |
| 107 | `objects/generic/urinal_cake` | `urinal_cake` | 21 | 15 | 100 | 2 | — | 0 | 4 | yes |
| 108 | `objects/generic/urn` | `urn` | 37 | 23 | 25 | 2 | — | 20 | 1 | yes |
| 109 | `objects/generic/waste_basket` | `waste_basket` | 28 | 9 | 150 | 2 | `waste_basket` | 0 | 1 | yes |
| 110 | `objects/generic/web_moss_animated` | `web_moss` | 0 | 0 | 150 | 0 | `web_moss` | 0 | 1 | yes |
| 111 | `objects/generic/wet_floor_sign` | `sign` | 0 | 0 | 150 | 2 | — | 0 | 1 | yes |
| 112 | `Enemy/Bolt` | `w_bat` | 15 | 18 | 400 | 2 | — | 0 | 1 | no |
| 113 | `Enemy/Bolt` | `w_desert_eagle` | 28 | 9 | 1000 | 2 | — | 0 | 1 | no |
| 114 | `Enemy/Bolt` | `w_pistol1` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 115 | `Enemy/Bolt` | `w_machinepistol` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 116 | `Enemy/Bolt` | `w_katana` | 28 | 9 | 1000 | 2 | — | 0 | 1 | no |
| 117 | `Enemy/Bolt` | `w_machinegun` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 118 | `Enemy/Bolt` | `w_sniperrifle` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 119 | `Enemy/Bolt` | `w_shotgun` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |
| 120 | `Items/Projectiles` | `c4` | 28 | 9 | 1000 | 2 | — | 0 | 1 | yes |

### A.4 Table 3 — Iraq (15 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/iraq/basket` | `basket` | 15 | 18 | 50 | 2 | — | 0 | 0 | yes |
| 1 | `objects/iraq/cart_camel` | `cart` | 15 | 18 | 300 | 2 | — | 0 | 0 | yes |
| 2 | `objects/iraq/flatbed` | `off` | 28 | 8 | 500 | 2 | — | 0 | 0 | yes |
| 3 | `objects/iraq/flatbed` | `tire` | 28 | 8 | 500 | 2 | — | 0 | 0 | yes |
| 4 | `objects/iraq/flatbed` | `on` | 28 | 8 | 500 | 2 | — | 0 | 0 | yes |
| 5 | `objects/iraq/fruit_melon` | `melon` | 27 | 0 | 50 | 2 | — | 0 | 0 | yes |
| 6 | `objects/iraq/fruit_orange` | `orange` | 12 | 0 | 50 | 2 | — | 0 | 0 | yes |
| 7 | `objects/iraq/fruit_stand` | `fruit_stand` | 15 | 18 | 125 | 2 | — | 0 | 0 | yes |
| 8 | `objects/generic/beam` | `beam` | -1 | -1 | 0 | 0 | — | 0 | 0 | yes |
| 9 | `objects/iraq/partition` | `partition` | -1 | 0 | 400 | 2 | — | 0 | 0 | yes |
| 10 | `objects/iraq/pottery` | `pottery` | 4 | 0 | 25 | 2 | — | 0 | 0 | yes |
| 11 | `objects/iraq/sprinklers` | `sprinklers` | 28 | 8 | 500 | 2 | — | 0 | 0 | no |
| 12 | `objects/iraq/stealth` | `stealth` | 28 | 8 | 5000 | 2 | — | 0 | 0 | yes |
| 13 | `objects/iraq/tree_palm` | `tree` | 15 | 18 | 1000 | 2 | — | 0 | 0 | yes |
| 14 | `objects/iraq/valve` | `valve` | 28 | 8 | 300 | 2 | — | 0 | 0 | yes |

### A.5 Table 5 — Lights (23 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/light/alley_wall` | `light` | 28 | 10 | 500 | 0 | — | 0 | 0 | yes |
| 1 | `objects/light/alley_wall_old` | `light` | 28 | 10 | 500 | 2 | — | 0 | 0 | no |
| 2 | `objects/light/caged` | `caged` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 3 | `objects/light/candle` | `candle` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 4 | `objects/light/candle` | `candle2` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 5 | `objects/light/candle` | `candle3` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 6 | `objects/light/candle` | `base` | 28 | 10 | 500 | 2 | `candle` | 0 | 0 | yes |
| 7 | `objects/light/candle2` | `base` | 28 | 10 | 500 | 2 | `candle` | 0 | 0 | yes |
| 8 | `objects/light/chandelier` | `swing` | 28 | 10 | 500 | 2 | `light` | 0 | 0 | yes |
| 9 | `objects/light/desk_lamp` | `lamp` | 28 | 10 | 500 | 2 | `light` | 0 | 0 | yes |
| 10 | `objects/light/emergency` | `emergency` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 11 | `objects/light/emergency_blue` | `emergency` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 12 | `objects/light/halfshell` | `shell` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 13 | `objects/light/hanging` | `hanging` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 14 | `objects/light/lantern` | `lantern` | 28 | 31 | 500 | 2 | — | 8 | 1 | yes |
| 15 | `objects/light/lantern2` | `lantern2` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 16 | `objects/light/lantern3` | `lantern3` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 17 | `objects/light/military_door` | `light` | 28 | 10 | 500 | 2 | — | 0 | 0 | no |
| 18 | `objects/light/military_door_outside` | `light` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 19 | `objects/light/shop` | `shop` | 28 | 10 | 500 | 2 | `light` | 0 | 0 | yes |
| 20 | `objects/light/street_modern` | `modern` | 28 | 10 | 500 | 0 | — | 0 | 0 | yes |
| 21 | `objects/light/street_old` | `old` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |
| 22 | `objects/light/lamp_hang` | `lamp` | 28 | 10 | 500 | 2 | — | 0 | 0 | yes |

### A.6 Table 6 — New York (5 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/newyork/burned_car` | `burned_car` | 28 | 10 | 10000 | 2 | — | 0 | 0 | yes |
| 1 | `objects/newyork/gumball` | `gumball` | 28 | 11 | 1000 | 2 | `gumball` | 0 | 0 | yes |
| 2 | `objects/newyork/news_machine` | `news` | 28 | 11 | 1000 | 2 | — | 0 | 0 | no |
| 3 | `objects/newyork/trash_pile` | `trash_pile` | 28 | 10 | 300 | 2 | — | 0 | 0 | no |
| 4 | `objects/newyork/trashcan` | `trashcan` | 28 | 10 | 300 | 2 | — | 0 | 0 | yes |

### A.7 Table 7 — Siberia (11 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/siberia/carton1` | `carton1` | 28 | 9 | 500 | 2 | — | 0 | 0 | no |
| 1 | `objects/siberia/carton2` | `carton2` | 28 | 9 | 10000 | 2 | — | 0 | 0 | no |
| 2 | `objects/siberia/carton3` | `carton3` | 28 | 9 | 10000 | 2 | — | 0 | 0 | yes |
| 3 | `objects/siberia/carton4` | `carton4` | 28 | 9 | 10000 | 2 | — | 0 | 0 | no |
| 4 | `objects/siberia/flag` | `flag` | 0 | 0 | 500 | 2 | — | 0 | 0 | yes |
| 5 | `objects/siberia/icicles` | `icicles` | 0 | 0 | 10000 | 2 | — | 0 | 0 | no |
| 6 | `objects/generic/beam` | `beam` | -1 | -1 | 250 | 0 | — | 0 | 0 | yes |
| 7 | `objects/siberia/flare` | `null` | -1 | -1 | 500 | 0 | — | 0 | 0 | yes |
| 8 | `objects/siberia/searchlight` | `lamp` | 28 | 9 | 10000 | 2 | — | 0 | 0 | yes |
| 9 | `objects/siberia/searchlight` | `arm` | 28 | 9 | 10000 | 2 | — | 0 | 0 | yes |
| 10 | `objects/siberia/snowcat` | `snowcat` | 28 | 9 | 10000 | 2 | — | 0 | 0 | yes |

### A.8 Table 8 — Tokyo (12 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/tokyo/bowl` | `bowl` | 14 | 17 | 100 | 2 | — | 6 | 1 | yes |
| 1 | `objects/tokyo/buddha` | `buddha` | 28 | 11 | 2000 | 2 | — | 0 | 0 | no |
| 2 | `objects/tokyo/fan` | `fan` | 14 | 14 | 50 | 2 | — | 0 | 4 | yes |
| 3 | `objects/tokyo/gong` | `gong` | 28 | 11 | 1000 | 2 | — | 6 | 0.5 | yes |
| 4 | `objects/tokyo/gym_equip1` | `equip1` | 28 | 11 | 2000 | 2 | — | 0 | 0 | no |
| 5 | `objects/tokyo/gym_equip2` | `equip2` | 28 | 11 | 2000 | 2 | — | 0 | 0 | no |
| 6 | `objects/tokyo/plant_bonsai` | `bonsai` | 14 | 17 | 100 | 2 | — | 0 | 0 | yes |
| 7 | `objects/tokyo/samurai_suit_small` | `samurai_sm` | 28 | 10 | 1000 | 2 | — | 0 | 2 | yes |
| 8 | `objects/tokyo/samurai_suit_small` | `samurai_sm` | 28 | 10 | 1000 | 2 | — | 0 | 0 | yes |
| 9 | `objects/tokyo/shoe` | `shoe` | 14 | 17 | 100 | 2 | — | 4 | 1 | yes |
| 10 | `objects/tokyo/stone_light` | `light` | 17 | 15 | 1000 | 2 | — | 0 | 0 | yes |
| 11 | `objects/generic/stunt_chopper` | `stunt_chopper` | 28 | 11 | 1 | 2 | — | 0 | 0 | no |

### A.9 Table 9 — Uganda (15 entries)
| # | directory | file | surf | mat | health | solid | skin material | debris n | size | data |
|---|---|---|---|---|---|---|---|---|---|---|
| 0 | `objects/uganda/african_tree` | `tree` | 15 | 18 | 1000 | 2 | — | 0 | 0 | no |
| 1 | `objects/uganda/chainsaw` | `chainsaw` | 28 | 9 | 500 | 2 | — | 0 | 0 | yes |
| 2 | `objects/uganda/cow` | `cow` | 27 | 0 | 400 | 2 | — | 0 | 0 | no |
| 3 | `objects/uganda/cow_head` | `head` | 27 | 0 | 100 | 2 | — | 0 | 0 | yes |
| 4 | `objects/uganda/gore_pile` | `gore_pile` | 27 | 0 | 300 | 2 | — | 0 | 0 | yes |
| 5 | `objects/uganda/gutted_cow` | `gutted_cow` | 27 | 32 | 1000 | 2 | — | 0 | 0 | yes |
| 6 | `objects/uganda/hook` | `hook` | 28 | 9 | 1000 | 2 | — | 0 | 0 | yes |
| 7 | `objects/uganda/nuke` | `nuke` | 28 | 9 | 1000 | 2 | — | 0 | 0 | yes |
| 8 | `objects/uganda/rocket_core` | `core` | 28 | 9 | 500 | 2 | — | 0 | 0 | yes |
| 9 | `objects/uganda/rocket_engine` | `engine` | 28 | 9 | 500 | 2 | — | 0 | 0 | yes |
| 10 | `objects/uganda/side_o_beef` | `beef` | 27 | 32 | 500 | 2 | `beef` | 0 | 0 | yes |
| 11 | `objects/uganda/train_arm` | `arm` | 28 | 9 | 500 | 2 | — | 0 | 0 | no |
| 12 | `objects/uganda/train_bumper` | `bumper` | 28 | 9 | 500 | 2 | — | 0 | 0 | yes |
| 13 | `objects/uganda/train_coupling` | `coupling` | 28 | 9 | 500 | 2 | — | 0 | 0 | yes |
| 14 | `objects/uganda/train_wheel` | `wheel` | 28 | 9 | 500 | 2 | — | 0 | 0 | no |

## Appendix B. Classes not placed in the shipped maps

A scan of the entity lumps of the 76 `.bsp` files in the 1.07f archives `pak0`, `pak2` and `pak3` (67 distinct map
names) finds 142 of the 194 prop classnames of this
spec in at least one map; the following 52 are never placed (they still spawn when a custom map uses them):

`light_generic_alley_wall_old`, `misc_bosnia_antipersonnel_jack`, `misc_bosnia_flag_wall`, `misc_bosnia_landmine`, `misc_bosnia_rubble1`, `misc_bosnia_rubble2`, `misc_bosnia_tank_jack`, `misc_bosnia_truck_chunks`, `misc_exploding_heli`, `misc_generic_access_card`, `misc_generic_blinds`, `misc_generic_car_rolls`, `misc_generic_chair_legs2`, `misc_generic_chunks_glass`, `misc_generic_chunks_metal`, `misc_generic_chunks_rock`, `misc_generic_chunks_wall`, `misc_generic_chunks_wood`, `misc_generic_motorcycle`, `misc_generic_parking_meter`, `misc_generic_security_camera2`, `misc_generic_sprinkler`, `misc_generic_stoplight`, `misc_generic_street_name_sign`, `misc_generic_table2`, `misc_generic_tree_dead`, `misc_generic_tree_thin`, `misc_generic_wpn_bat`, `misc_generic_wpn_desert_eagle`, `misc_generic_wpn_glock`, `misc_generic_wpn_ingram`, `misc_generic_wpn_katana`, `misc_generic_wpn_minimi`, `misc_generic_wpn_sig3000`, `misc_generic_wpn_spas12`, `misc_iraq_fruit_melon`, `misc_iraq_fruit_orange`, `misc_iraq_sprinkler`, `misc_newyork_news_machine`, `misc_newyork_trash_pile`, `misc_siberia_carton1`, `misc_siberia_carton2`, `misc_siberia_carton4`, `misc_siberia_icicles`, `misc_test_gun_auto`, `misc_tokyo_buddha`, `misc_tokyo_gym_equipment1`, `misc_tokyo_gym_equipment2`, `misc_uganda_african_tree`, `misc_uganda_cow`, `misc_uganda_train_arm`, `misc_uganda_train_wheel`.

Classes whose model file is missing (Appendix A, "data no") but which a shipped map places: `light_generic_military_door`
(16 entities in the deathmatch map `dm/gerctb1`, `pak2`). They spawn without a visible model in the shipped game.
