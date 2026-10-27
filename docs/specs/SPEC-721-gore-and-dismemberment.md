# SPEC-721: Gore and dismemberment (gore zones, wounds, severed limbs, gibs, violence locks)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (character bodies: human, dog, cow), version 1.07f
Depends on: SPEC-001 (game imports: random numbers, sound, effect index, savegame chunks), SPEC-010/SPEC-011 (Ghoul
object and instance methods: part lookup/visibility, skins, bolt-ons, bolt matrices), SPEC-120 (model node naming:
`GZ_*`, `_Cap_*`, `gbolt_*`), SPEC-121 (`.gsq` part visibility), SPEC-201/SPEC-401 (effect messages, debris temp
entity), SPEC-302 (lock cvars, `sv_violence`/`cl_violence`), SPEC-720 (damage model, hit location, damage flags,
means of death), SPEC-722 (character bodies, death animations).

## Purpose

Soldier of Fortune shows damage on character models: wounds appear on the exact body area that was hit and grow with
repeated hits, limbs can be shot off, heads explode, guts spill, characters can be blown apart, and hats, helmets,
weapons and stuck knives fall off. All of this is driven by the game module on the server, through the Ghoul model
interface (part visibility, skin overrides, bolt-ons) and through effect messages. This spec describes that system so
that a reimplementation produces the same visible result on the original models: the gore-zone tables of every body
family, the wound-level rules, the conditions for severing and gibbing, the pieces that are spawned, the effects and
sounds, and what the parental-lock ("violence") settings suppress.

Where the hit came from, how much damage it does and which zone it lands in is SPEC-720. Which death animation is
played and how bodies are set up is SPEC-722.

## Interface / layout

### Terms
- **Body**: the per-character object that owns the Ghoul instance logic of a character (SPEC-722). Human, dog and cow
  bodies have gore zones; vehicles (helicopter, tank, snowcat) do not.
- **Gore zone**: a hit area of the body. Each zone refers to parts and bolts of the character's Ghoul model by name.
  Human bodies have 26 zones, dogs 10, cows 6 (tables below). The zone names are the names printed by the diagnostic
  output (see `ai_goretest`).
- **Wound mesh**: a mesh part of the model (material `gz`, names `gz_*` or `GZ_*`, SPEC-120) that overlays the area
  of the zone. Wound meshes are hidden until the zone is first wounded.
- **Owner part**: the visible body mesh the zone belongs to (e.g. `_tightchest`). A zone only takes wounds while its
  owner part is shown; severing hides it.
- **Gore bolt**: bolt where blood spurts and exit chunks are attached (`gbolt_*`, `sbolt_heel_*`).
- **Cap meshes**: mesh parts shown on the stump after severing: the *body cap* (on the character) and the *piece
  cap* (on the severed piece). Both get skin `c_blood` of material `c`.
- **Cap bolts and cap bolt-on**: bolts at the cut on the piece side and on the body side; a small "cap" model (bone,
  torn flesh) is attached there (`Enemy/bolt/<name>`, default `g_cap1`).
- **Sever group**: the ordered list of mesh parts and bolts that leave the body with the severed piece.
- **Wound level**: 0 (none), 1, 2, 3 per zone, derived from damage accumulated in the zone.

### Ghoul names used (all are model names, SPEC-120)
| Kind | Names |
|---|---|
| materials | `gz` (wound meshes), `c` (cap meshes), `h` (head skin), `f` (face skin) |
| wound skins of material `gz` | level 1: `gz_sing1`, `gz_sing2`, `gz_sing3`, `gz_sing_k`; level 2: `gz_multi1`..`gz_multi3`, `gz_multi_k`; level 3: `gz_mass1`..`gz_mass3`, `gz_mass_k`; special: `gz_half_head` |
| cap skin | `c_blood` |
| bolt-on models (directory `Enemy/bolt`) | `g_cap1`, `g_half_head`, `g_neck_torn_anim2`, `g_guts_lrg`, `g_arm_torn_anim1`, `g_bone_anim1`, `guts_bolton`, `g_exit_chunky_anim1`, `g_brain`, `w_knife` |
| bolt-on sequences | `g_exit_guts_anim1` (once), `g_exit_guts_anim2` (loops after the first ends) |
| bolts on bolt-on models | `to_gbolt`, `to_gbolt_Cap_chest_head`, `to_gbolt_Cap_chest_hips`, `to_gbolt_Cap_chest_rbicep`, `to_gbolt_Cap_chest_lbicep`, `to_gbolt_gut_f`, `to_abolt_head_t`, `tip` (knife) |
| other character bolts | `sbolt_mouth`, `gbolt_gut_f`, `abolt_head_t`, `abolt_chest`, `abolt_hip_r`, `sbolt_heel_r`, `sbolt_heel_l`, `gbolt_cap_rbicep_chest_tight`, `gbolt_cap_lbicep_chest_tight`, `wbolt_hand_r`, `wbolt_hand_l` |

### Effects and sounds (effect names are `.eft` names, SPEC-142; sent with SPEC-201 effect messages)
| Name | Kind | Where | Suppressed / replaced by lock_blood |
|---|---|---|---|
| `gore/stumpblood` | effect on entity bolt | gore or cap bolt of a fresh wound/stump | replaced by `gore/noblood` |
| `gore/bloodcloud` (size ≥ 51), `gore/bloodcloud_s` (size ≤ 50) | effect on entity bolt | severing, exit wounds, killing blows | replaced by `gore/noblood` / `gore/noblood_s` |
| `gore/headcloud` | effect on entity bolt | head explosion, at `sbolt_mouth` | suppressed |
| `gore/bloodpool` | effect at position (flag 1) | under a corpse | suppressed |
| `environ/gb_exitw` | effect at position with direction | exit wound spray onto walls | suppressed |
| `gore/hugeburst2`, `gore/mpgsplatter` | effect at position | gibbing | suppressed |
| `gore/gibbody` | effect on entity | full gib | not suppressed (see violence table) |
| `gore/danglyblood` | effect at position with direction | severed piece hits the world | suppressed |
| debris temp entity (SPEC-401 type 18), chunk type 32 (flesh), material 35 | temp entity | head explosion | suppressed |
| `impact/gore/headexp.wav` | sound, channel 4, volume 0.6, attenuation 1 | head explosion | — |
| `impact/gore/limbfall.wav` | sound, channel 4, volume 0.6, attenuation 1 | first touch of a severed piece | — |
| `environ/takeleak` | effect on bolt `gbolt_groin` | frightened character wets himself (animation flag) | suppressed by lock_textures |

### Savegame data
Each human body writes its gore state in two savegame chunks (SPEC-303, game import WriteChunk): chunk id
`EBIA` (the 32-bit id whose bytes in file order are `E`,`B`,`I`,`A`) holding a raw image of the body object (492 bytes, private layout, not portable) and chunk id
`ZGIA` (bytes `Z`,`G`,`I`,`A`) holding one 32-bit integer per gore zone: the zone's accumulated damage, in zone order. On load
the zones are rebuilt from the model (setup below) and each zone's wound level is recomputed from the stored damage
with the level formula. Skin overrides and part visibility are restored by the Ghoul instance savegame (SPEC-011).

## Behaviour

### 1. Zone setup
Zones are (re)built whenever a character's model is set up for gore (after spawning, after loading a savegame) and
reflect the parts that are visible at that moment.
1. Every zone starts empty: no wound mesh, owner, bolts, caps or cap bolt-on, empty sever group, no "next zone of
   piece", no wound link, accumulated damage 0, level 0. The damage scale of each zone is set separately (SPEC-720).
2. Wound skins: if the model has material `gz`, the 12 wound skins are looked up by name (table in Constants); a
   missing skin is "none" and is simply not applied later.
3. Human bodies (families meso, ecto, female; the armoured meso body uses the meso tables): the fixed assignments of
   the family table are made, then each *variant chain* is examined in order: the first mesh of the chain that is
   currently shown decides the zone's wound mesh, owner part and caps for the zones the chain covers (variant tables
   below). If no mesh of a chain is shown, the zones keep the fixed values (often: no wound mesh → those zones never
   show wounds and cannot be severed).
4. Head cap bolt-on: one random draw r = flrand(0, 1) per setup; r ≥ 0.7 → both head zones use `g_half_head`, else
   `g_neck_torn_anim2` (30 % / 70 %).
5. `g_half_head` is registered with the character's current head skin (material `h`) and face skin (material `f`) so
   that it matches the head; its own `gz` material uses skin `gz_half_head`.
6. Dogs and cows: fixed tables (Constants).

### 2. Wounding a zone
Applies to each zone the hit resolution of SPEC-720 reports for one hit (entry zones, in order), with the hit's
damage d ≥ 1. Nothing happens for a zone without wound mesh or whose owner part is hidden. Let H be the target's
health before this hit is subtracted.
1. Accumulated damage A of the zone grows: if H ≥ 1, A += min(d, H); if H ≤ 0 (corpse), A += d when d > 4, else
   A += 10.
2. New level L = 0 if A ≤ 0, 1 if A ≤ 100, 2 if A ≤ 200, 3 if A > 200.
3. Unless lock_gorezones: the wound mesh of the zone is shown.
4. If L is greater than the stored level:
   - Bolt-ons hanging on the bolts listed in the zone's sever group are offered to rule 11 with a damage of 1, so
     only accessories marked "falls off on any hit" drop.
   - Variant v: random integer in [0, 2]; if the means of death is 1 (knife slash, SPEC-720), v = 3 (the `_k` "slash" skins). If the zone
     already had a wound and lock_gorezones is off and v ≠ 3: v is set to the index (0..3) of the skin the zone
     currently shows among the skins of its old level, so that the wound pattern stays the same; if that index is 3,
     a new random v in [0, 2] is drawn.
   - If the zone has a gore bolt: when the target is alive (H ≥ 1) and damage flag 0x100000 is not set, a
     `gore/stumpblood` spurt is attached to the gore bolt.
   - Guts: if the zone is GZ_GUT_FRONT, d ≥ H, damage flag 0x20000 is set, lock_gorezones is off and the guts have
     not come out yet on this body: bolt-ons on `gbolt_gut_f` are removed, `Enemy/bolt/guts_bolton` is attached there
     by its bolt `to_gbolt_gut_f`, plays `g_exit_guts_anim1` once and, when that sequence ends, `g_exit_guts_anim2` in
     a loop. Once per body.
   - Unless lock_gorezones: the zone's wound mesh gets wound skin (L, v) of material `gz` (level 1 = `gz_sing`,
     2 = `gz_multi`, 3 = `gz_mass`; v 0..2 = suffix 1..3, v 3 = `_k`). If the zone has a wound link, the linked zone
     receives the same accumulated damage, level and skin, and its wound mesh is shown. The new level is stored
     (with lock_gorezones on, levels are never stored, so every hit counts as a level increase).
5. If d < H the body plays its general voice "Hit" (SPEC-722).
6. If d ≥ H the zone is a candidate for severing (rule 3).
7. The first time a hit kills the character (d > H), the body remembers the zone and starts tracking the position
   of its body-side cap bolt (or gore bolt) for the blood pool (section 7). Every hit with
   d > H on a zone that has a wound mesh and a body-side cap bolt or gore bolt also attaches a `gore/bloodcloud_s`
   (size 50) there.
8. Zone index of the last wounded zone is also kept for death-animation choice (SPEC-722).
9. If the entry or exit zone is GZ_HEAD_FRONT, GZ_HEAD_BACK or GZ_NECK the body's voice is silenced (SPEC-722).

### 3. Severing a limb (human bodies)
Condition — all of: the hit kills (d ≥ H), the zone has a body cap mesh, the first node of its sever group is not
the torso, lock_sever is off, and at least one of: cvar `ai_goretest` ≠ 0, damage flag 0x8000 or 0x20000 set, the
zone's accumulated damage A > 500. (In practice: explosions, close shotgun blasts and other flagged weapons sever;
ordinary bullets only after more than 500 points have hit that zone.)
Procedure, in this order —
1. If the group is the head (segment 6): head explosion (rule 4) instead of a flying piece.
2. Otherwise a severed piece is spawned (rule 6) with a `gore/bloodcloud` (size 400) on its cap bolt.
3. Piece side: the cap bolt-on (`g_cap1`, or the zone's cap model) is attached to the piece at the piece-side cap
   bolt; the piece cap mesh is shown on the piece with skin `c_blood`.
4. The zone, and every zone reached through "next zone of piece" (until it returns to the start or ends), hands over
   to the piece: its wound mesh (shown on the piece if it was shown on the body, or — if unwounded but its owner was
   shown — shown on the piece with a random wound skin of level 1..3 and variant 0..2), its body cap mesh, bolt-ons
   on its body-side cap bolt (a stuck knife on its piece-side cap bolt is dropped as a pickup instead, rule 10), and
   bolt-ons on its gore bolt; all these are hidden/removed on the body.
5. The sever group is walked: each mesh is hidden on the body and shown on the piece; each bolt moves its bolt-ons
   to the piece (a weapon in a hand is dropped first, SPEC-722); special entries add the head and both upper arms
   (torso group) or the lower-leg zone (thigh groups).
6. Body side: the body cap mesh is shown with skin `c_blood`; the cap bolt-on (`g_cap1` or the zone's cap model) is
   attached at the body-side cap bolt; if it is `g_half_head` it gets the character's head and face skins and its
   `gz` material gets `gz_half_head`; a `gore/bloodcloud` (size 100) is attached at that bolt.
7. The owner part, the zone's wound mesh and the exit zone's wound mesh are hidden; the body cap is shown.

### 4. Head explosion
Happens instead of a flying head when the head group is severed (rule 3), with these extra steps: the body is
silenced and enters timed body state 5 for 1 s (SPEC-722); if the model has `sbolt_mouth`: two debris bursts at
the hit position (SPEC-401 type 18, chunk type 32 flesh, material 35): irand(1, 3) chunks of size 1 and irand(3, 6)
chunks of size 4; `gore/headcloud` at `sbolt_mouth`; sound `impact/gore/headexp.wav` (channel 4, volume 0.6,
attenuation 1). The head meshes (head sever group) are hidden and the neck cap and cap bolt-on (`g_neck_torn_anim2`
or `g_half_head`) are shown on the body.

### 5. Exit wounds
After the per-zone wounding of a hit, when lock_gorezones is off and the hit has an exit zone (SPEC-720):
1. Killing blow on a living target (0 < H < d) and the exit zone differs from the entry zone, has a wound mesh and a
   shown owner: the exit zone's accumulated damage becomes 201. If its level was below 3: its level becomes 3
   (`gz_mass` skin; the variant follows rule 2.4, a new random variant in [0, 2] when the zone was unwounded), copied
   to its wound link; then, if it has a gore bolt: bolt-ons there are removed and either
   - **brain shot**: entry zone GZ_HEAD_FRONT, exit zone GZ_HEAD_BACK, d > 80, the head mesh is not `_headbeard`
     and the model has `_headhelmet` (an open-skull head mesh): every mesh of the head sever group is hidden and the
     bolt-ons on `abolt_head_t` are removed, `_headhelmet` is shown and becomes the head owner, the exit wound mesh is
     hidden, the front wound mesh becomes `gz_headhelmet_f` (shown with the front zone's current wound skin), the
     back zone's gore bolt becomes `abolt_head_t`, and `Enemy/bolt/g_brain` is attached at `abolt_head_t` by its bolt
     `to_abolt_head_t` with scale 1.17; or
   - `Enemy/bolt/g_exit_chunky_anim1` is attached at the exit gore bolt (bolt `to_gbolt`), a `gore/bloodcloud`
     (size 100) is attached there and, with damage flag 0x200, an `environ/gb_exitw` spray is sent from the hit
     position along the shot direction.
   In all cases of this item the exit wound mesh is shown (also when its level already was 3).
2. Otherwise, if the target is already dead: with probability 1/2 a `gore/bloodcloud_s` (50) at the exit zone's gore
   bolt (or body-side cap bolt); and with probability 1/3 and damage flag 0x200 an `environ/gb_exitw` spray.

### 6. Severed pieces
A severed piece is a new entity that carries a copy of the character's Ghoul instance with every mesh part hidden
except the ones handed over (rule 3).
| Property | Value |
|---|---|
| owner | the character |
| solid / clip | not solid to other entities; clip mask = solid, monster clip and dead-body clip (0x4010003, UNVERIFIED names) |
| AI object | class code 13 (the "limb" AI that manages its lifetime, SPEC-701/WP1) |
| health | 300 |
| movement | bounce physics (movetype 11) with physics parameters 0.5, 1.0, 0.1, 0.7, 0.5 (UNVERIFIED meaning, same fields as SPEC-720 knockback/physics) |
| origin | world position of the piece-side cap bolt (fallback: character origin + 24 up, box ±8, standing sequence `std_istand_n_a_n`) |
| bounding box | the bounds of the shown parts 0.3 s ahead, centred on the origin |
| velocity | (flrand(−50, 50), flrand(−50, 50), flrand(−50, 50)) + u × flrand(100, 200), u = unit vector from the character's origin to the piece |
| angles | the character's angles with yaw + flrand(−30, 30) |
| spin | if the box is taller than it is wide in both x and y: pitch angular velocity −150 °/s |
| think | every 0.1 s (lifetime by the limb AI) |
| touch | first touch plays `impact/gore/limbfall.wav` (channel 4, volume 0.6, attenuation 1); later touches silent |
| blood | `gore/stumpblood` on the piece's cap bolt |
Pieces thrown by a gib (rule 8) differ: no blood cloud, velocity = gib direction × flrand(100, 200), and their touch
sends `gore/danglyblood` (direction = −surface normal) whenever they hit the world (the world entity) while moving
faster than 2 units/s, then their spin stops; lock_blood suppresses the effect.

### 7. Blood pool
When a body that recorded a killing zone (rule 2.7) needs its blood pool (SPEC-722 death handling), `gore/bloodpool`
is sent at the floor position under the tracked bolt (the bolt of the killing zone is followed each frame), or under
the character origin if no zone was recorded.

### 8. Gibbing (blowing a human body apart)
Trigger (SPEC-720): a hit with damage flag 0x8000 that leaves health below −50, or any hit that leaves health below
−750 (damage to a living character is capped at health + 150, so the second case needs a corpse). Each such hit
increments the "gibbed this explosion" counter G; G is reset to 0 at the start of every radius-damage event
(explosion) and of the Dekker split (the counter is *not* reset by ordinary bullet hits — original behaviour).
Procedure (nothing at all when lock_sever, or when the body is already fully gibbed):
1. The body is silenced. If the body's current action is of the first two kinds (believed: standing / moving,
   UNVERIFIED): it plays
   `lf_sautofire_end_n_n` (fall forward) when the shot direction points the way the character faces (dot > 0) and
   the sequence exists, else `lb_sautofire_end_n_n`.
2. A global statistic "gibs" is increased by 1 (not during cinematics).
3. Unless lock_blood, and only while G < 4: `gore/hugeburst2` at the character origin, or — for microwave damage
   (damage flag 0x800) — the instance is tinted (1, 0.3, 0, 1) and `gore/mpgsplatter` is sent instead.
4. The body enters timed body state 5 for 1 s (SPEC-722).
5. Pieces: each of these limbs whose owner part is shown is severed with probability 1/2, in this order: head
   (zones 0+1), left upper arm (10), right upper arm (9), left forearm (12), right forearm (11), left thigh (18+19),
   right thigh (16+17), left calf (21), right calf (20). Direction for each piece = shot direction +
   (flrand(−1.5, 1.5), flrand(−1.5, 1.5), flrand(1, 2)). Then with probability 1/3 the chest (zones 7+8; the piece
   takes head and both upper arms if still attached). Counting a limb as "gone" when its owner part was hidden or it
   was severed now.
6. Piece rules: a head never becomes a piece (only its cap is set up); a piece is spawned only while G < 5 and fewer
   than 2 pieces were spawned by this gib; caps and hand-over as in rule 3 (no blood clouds); a spawned piece is
   freed immediately again if more than 1 piece was already spawned or G > 2 (so at most a couple of flying pieces
   per explosion); spawned non-head pieces count towards the 2.
7. Full gib: if more than 6 of the 9 limbs are gone, or damage flag 0x800 or 0x8000 is set: the body is marked fully
   gibbed, becomes non-solid, `gore/gibbody` is sent on it, its instance is switched off (invisible) and all its
   bolt-ons are destroyed.

### 9. Dekker split
The script-triggered entity `func_Dekker_split` (SPEC-006) resets G and the gib piece counter and severs the chest
group of its target as a gib piece that is kept (not freed) and whose next think is delayed by 60 s.

### 10. Stuck knives
- A thrown knife that hits a character (the inflictor is a thrown-knife projectile — marked by the value 254 in its
  projectile field, SPEC-720 — and the attacker is a player) sticks in the zone: `Enemy/bolt/w_knife` is attached by
  its `tip` bolt at the zone's piece-side cap bolt; zones without one use: shoulders right (3, 4) →
  `gbolt_cap_rbicep_chest_tight`, shoulders left (5, 6) → `gbolt_cap_lbicep_chest_tight`, neck → `abolt_chest`
  (else `abolt_hip_r`), right foot → `sbolt_heel_r`, left foot → `sbolt_heel_l`, all others → `abolt_hip_r`. The knife
  is turned to point along the knife's flight direction (pitch 90°).
- When the character dies (SPEC-722) and when a severed limb carries a knife away, every stuck knife is converted
  into a knife pickup entity at the knife's position: model `Enemy/Bolt/w_knife`, trigger-solid, box (−4, −2, −4) –
  (3, 2, 4), movetype 10, health 10000, item flag set, so a player can pick the knife up again.

### 11. Bolt-ons knocked off by hits
When a hit lands on an accessory bolt-on of a non-player character (and on level increase, rule 2.4), the bolt-on
is looked up in the accessory table (Constants). Nothing happens for the merc character `m_x_mmerc`, for bodies that
do not allow drops (SPEC-722), or for names not in the table.
1. If the entry is not "falls off on any hit" and the hit's remaining damage is below 2, the bolt-on stays.
2. Weapons (`w_*`) become weapon pickups of the matching weapon (SPEC-722 weapon table) unless the character has the
   "no drop" flag; the character's hand slot is cleared. Other accessories become plain thrown models.
3. The dropped thing is a new entity at the character's origin with a copy of the bolt-on model (using its part
   `inworld` if present), thrown with velocity 150 × (impact direction) + 150 × (unit vector from the hit point to
   the character, plus 300 up) and random yaw flrand(−180, 180). Non-pickups: toss physics (movetype 9), trigger
   solid, box ±1, health 10000. After 5 s it starts its removal check: a weapon pickup is removed after 30 s, or
   earlier as soon as no player within 800 units sees it (checked each second: a player within 800 units, in PVS,
   whose view direction is within dot < 0.2 of the item, keeps it); other models are removed the same way at once.
4. Hats and helmets ("swaps head" column): when the character's head mesh is a helmeted variant it is switched to the
   bare variant: meso `_helmethead` → `_baldhead` (back wound mesh `gz_baldhead_b`) and `_helmetface` → `_comface`
   (front wound mesh `gz_comface`); ecto `_headhelmet` → `_headbald` (wound meshes `gz_headbald_f`/`gz_headbald_b`).
5. Damage absorption: bolt-ons marked "absorbs" stop the bullet (the remaining damage becomes 0), except when the
   attacker is a player using weapon slot 6 (sniper rifle), whose shot passes on with full damage.
6. Player victims (deathmatch, game rules that allow it): a hit on the weapon in `wbolt_hand_r` makes the player
   drop it (`weaponlose` player command) and absorbs the hit, for attacker weapons 2, 3, 6 and — with probability
   0.2 — 4, 5, 7, 8, 9 (UNVERIFIED in detail).

7. Flamethrower back tanks: a hit on the bolt-on `acc_tanks` (checked before rule 11.1) removes it, sets the
   wearer's health to 0 and makes the wearer explode: radius damage 250 within radius 175 with the wearer as
   inflictor and attacker (means of death 26, damage flag 0x1000, SPEC-720), everything within 175 units is set on
   fire, an explosion noise alert (200, 200) for the AI, effect `weapons/world/airexplode` on the wearer and a camera
   shake at the hit position (parameters 100, 200, 30; same as `func_camerashake`, UNVERIFIED meaning).
8. A body debris burst exists (no caller found in 1.07f): irand(10, 20) chunks size 4, irand(5, 10) size 1,
   irand(0, 2) size 2 of chunk type 32, all bolt-ons removed, events stopped, body made non-solid.

### 12. Dogs and cows
Dog and cow bodies use the same zone record, wound skins and wounding rules with these differences (tables in
Constants): the accumulated damage grows by the full damage d; wound variants are drawn as a random number mod 3;
an exit zone different from the entry zone always receives a level-3 wound; severing needs a killing blow, a body
cap mesh, lock_sever off and (`ai_goretest` ≠ 0 or damage flag 0x20000) — there is no 500-point rule and no head
explosion. On severing a piece is spawned (with blood cloud), the cap bolt-on is attached to the body-side cap bolt
with a `gore/bloodcloud` (100) when the animal is dead, else a `gore/stumpblood`. Cows have no cap meshes and cannot
be severed.

### 13. Violence settings (parental locks)
The game module reads the five lock cvars once when it initialises (game start / module load) as booleans
"value ≠ 0"; later changes take effect only after the module is reloaded. It does not read `sv_violence` or
`cl_violence`; the engine derives those from the same locks (SPEC-301/SPEC-302) and uses them only for connection
checks.
| Cvar | Effect when set |
|---|---|
| `lock_blood` | No blood effects: blood splats behind targets, blood pools, `gore/headcloud`, exit sprays, gib bursts (`gore/hugeburst2`, `gore/mpgsplatter`), dangly blood are not sent; stump blood and blood clouds are replaced by `gore/noblood` / `gore/noblood_s`; flesh debris (chunk type 32) is not sent; `environ_bloodrain` and other blood effects of map entities are suppressed (SPEC-006) |
| `lock_gorezones` | Wound meshes are never shown, no wound skins, no guts, no exit wounds, no brain shot, no wounds on severed pieces; zone levels are not stored |
| `lock_sever` | No severing, no head explosion, no gibbing at all (rules 3, 4, 8, 9) |
| `lock_deaths` | Burning effects on burning characters (`environ/onfireburst`, `environ/quickfireburst`) and the burn effect on microwaved corpses are not sent; death-animation restrictions in SPEC-722 |
| `lock_textures` | `environ/takeleak` is not sent; banner `misc_generic_banner` with skin 2 uses texture set `banner5` instead of `banner3` |
| `lock_pure` | not read by the game module |

### 14. Diagnostics
`ai_goretest` ≠ 0: every killing blow severs (rule 3 condition), characters spawn with health 1000 (SPEC-006), and
the bodies print their hit analysis to the console ("%d hits!", "Entry wound position: …", "considering %s for
frontHole", "Setting frontHole to %s", "Exit wound position: …", "spraying blood from …; monster pos …", "spraying
sparks from …"), using the zone names of the tables below.

## Constants

### Rules in numbers
| Item | Value |
|---|---|
| Wound level from accumulated damage A | 0: A ≤ 0; 1: 1–100; 2: 101–200; 3: > 200 |
| Accumulated damage set for a level (exit wounds, animals) | level 3 → 201 |
| Accumulation on a corpse for tiny hits | d ≤ 4 → +10 |
| Ordinary-weapon sever threshold | accumulated damage of the zone > 500 |
| Damage flags that allow severing (SPEC-720) | 0x8000 EXPLOSIVE, 0x20000 SPREAD (humans); 0x20000 (animals) |
| Damage flag: no stump blood | 0x100000 (TORSO_BLAST) |
| Damage flag: guts may come out (with a killing blow on GZ_GUT_FRONT) | 0x20000 (SPREAD) |
| Damage flag: exit-wound wall spray | 0x200 (PROJECTILE) |
| Damage flag: microwave (tint + `gore/mpgsplatter`, full gib) | 0x800 (MICROWAVE) |
| Gib trigger | flag 0x8000 and health − d < −50; or health − d < −750 |
| Gib limits per explosion (counter G) | burst effect while G < 4; pieces while G < 5; pieces freed again if G > 2 or > 1 piece already thrown |
| Pieces per gib | at most 2 (heads not counted) |
| Limb sever probability in a gib | 1/2 per limb; chest 1/3 |
| Full gib | more than 6 of 9 limbs gone, or flag 0x800 / 0x8000 |
| Head cap model choice | flrand(0,1) ≥ 0.7 → `g_half_head`, else `g_neck_torn_anim2` |
| Brain shot | front-to-back head shot, killing, d > 80 |
| `g_brain` scale | 1.17 |
| Blood cloud sizes | 400 (piece on sever), 100 (stump, exit chunk), 50 (killing blow, corpse exit); ≤ 50 uses the small cloud |
| Head-explosion debris | irand(1,3) chunks size 1 + irand(3,6) chunks size 4, chunk type 32, material 35 |
| Severed piece | health 300, movetype 11, physics 0.5/1.0/0.1/0.7/0.5, speed 100–200 + ±50 jitter, yaw ±30, pitch spin −150 |
| Gib piece direction jitter | (±1.5, ±1.5, 1..2) added to the shot direction, speed 100–200 |
| Dekker split piece think delay | 60 s |
| Dangly blood minimum speed | 2 units/s |
| Dropped bolt-on | speed 150 + 150, +300 up, yaw ±180, check after 5 s, weapon pickups live ≤ 30 s, visibility radius 800, view dot 0.2, recheck 1 s |
| Knife pickup box | (−4, −2, −4) – (3, 2, 4), health 10000 |
| Knife-sticking projectile marker | 254 |

### Wound skins (material `gz`)
| Level | variant 0 | variant 1 | variant 2 | variant 3 (knife slash) |
|---|---|---|---|---|
| 1 | `gz_sing1` | `gz_sing2` | `gz_sing3` | `gz_sing_k` |
| 2 | `gz_multi1` | `gz_multi2` | `gz_multi3` | `gz_multi_k` |
| 3 | `gz_mass1` | `gz_mass2` | `gz_mass3` | `gz_mass_k` |

### Accessory bolt-ons (rule 11; names are the bolt-on model names)
| Bolt-on | Falls off on any hit | Swaps helmet head to bare head | Absorbs the hit |
|---|---|---|---|
| `acc_antenna` | no | no | no |
| `acc_backpack_low` | no | no | yes |
| `acc_backpack_smll` | no | no | yes |
| `acc_box_hip` | no | no | yes |
| `acc_box_hip_lrg` | no | no | yes |
| `acc_box_hip_smll` | no | no | yes |
| `acc_ecto_glasses` | yes | no | no |
| `acc_faceplate` | no | no | no |
| `acc_faceplate2` | no | no | no |
| `acc_gas_mask2` | yes | no | no |
| `acc_glasses` | yes | no | no |
| `acc_hat_baseball` | no | yes | no |
| `acc_hat_beret` | no | yes | no |
| `acc_hat_flop2` | no | yes | no |
| `acc_hat_general` | no | yes | no |
| `acc_hat_general2` | no | yes | no |
| `acc_hat_hard` | no | yes | yes |
| `acc_hat_tall` | no | yes | no |
| `acc_hat_top` | no | yes | no |
| `acc_hat_turbin` | no | yes | no |
| `acc_helmet` | no | yes | yes |
| `acc_holster` | no | no | no |
| `acc_machette` | no | no | no |
| `acc_nightvision2` | no | no | no |
| `acc_sword` | no | no | no |
| `w_assault_rifle` | no | no | yes |
| `w_autoshotgun` | no | no | yes |
| `w_bat` | no | no | yes |
| `w_briefcase` | yes | no | yes |
| `w_flamethrower` | no | no | yes |
| `w_kantana` | no | no | yes |
| `w_knife` | no | no | yes |
| `w_machete` | no | no | yes |
| `w_machinegun` | no | no | yes |
| `w_machinepistol` | no | no | yes |
| `w_mpg` | no | no | yes |
| `w_pistol1` | no | no | yes |
| `w_pistol2` | no | no | yes |
| `w_rocket` | no | no | yes |
| `w_sack` | yes | no | yes |
| `w_shotgun` | no | no | yes |
| `w_sniperrifle` | no | no | yes |

### Human zone tables
Zone indices 0–25 are the same for all human families. Columns of the first table of each family: the fixed
assignments made before the variant chains are examined (wound mesh, owner part, body/piece caps come from the variant
table). "Next zone of piece": zones handed over together when this zone's piece is severed (followed until the chain
returns to the start). "Wound link": zone that copies this zone's wounds. The *sever group* of a zone lists, in order,
the meshes (hidden on the body, shown on the piece) and bolts (their bolt-ons move to the piece) that leave with it;
"+head & both upper arms" and "+… lower leg zone" are the special entries of rule 3.5. A zone can be severed only if
it has a body cap after the variant step and its sever group does not start in the torso; GZ_CHEST_FRONT/BACK and the
shoulder zones are only removed by gibbing. Variant tables: one row per mesh that is tested, in test order; within a
chain (consecutive rows covering the same zones) the first shown mesh wins, and rows of later chains apply
independently. Field names: *wound mesh*, *owner*, *body cap*, *piece cap*, *body cap bolt*, *piece cap bolt*,
*next zone of piece*, *sever group*.

Damage scales per zone are in SPEC-720 (head zones 10, neck 10, torso and gut 1, arms and legs 0.5).

#### Male soldier family (`enemy/meso`; also the armoured male body)
Special cases: when `_maskchest` is the shown chest, the body-side cap bolt of both head zones is set to the non-existent name `no_gore_for_me_please`, i.e. no cap bolt-on is attached after a head explosion; the shoulder zones then take the neck variant chain (`_maskneck`, `_hoodneck`).

| # | zone | gore bolt | cap bolt on severed piece | cap bolt on body | cap bolt-on model (its bolt) | sever group (segment) | next zone of piece | wound link |
|---|---|---|---|---|---|---|---|---|
| 0 | GZ_HEAD_FRONT | `gbolt_head_f` | `gbolt_Cap_head_neck` | `gbolt_Cap_neck_head` | `g_neck_torn_anim2` (`to_gbolt_Cap_chest_head`) | head | GZ_HEAD_BACK | — |
| 1 | GZ_HEAD_BACK | `gbolt_head_b` | `gbolt_Cap_head_neck` | `gbolt_Cap_neck_head` | `g_neck_torn_anim2` (`to_gbolt_Cap_chest_head`) | head | GZ_HEAD_FRONT | — |
| 2 | GZ_NECK | `gbolt_cap_head_neck` | — | — | — | neck | — | — |
| 3 | GZ_SHLDR_RIGHT_FRONT | `gbolt_shoulder_rf` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_SHLDR_LEFT_FRONT | — |
| 4 | GZ_SHLDR_RIGHT_BACK | `gbolt_shoulder_rb` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_SHLDR_LEFT_BACK | — |
| 5 | GZ_SHLDR_LEFT_FRONT | `gbolt_shoulder_lf` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_SHLDR_RIGHT_BACK | — |
| 6 | GZ_SHLDR_LEFT_BACK | `gbolt_shoulder_lb` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_CHEST_BACK | — |
| 7 | GZ_CHEST_FRONT | `gbolt_chest_f` | `gbolt_Cap_chest_hips_tight` | `gbolt_Cap_hips_chest_tight` | `g_guts_lrg` (`to_gbolt_Cap_chest_hips`) | torso | GZ_SHLDR_RIGHT_FRONT | — |
| 8 | GZ_CHEST_BACK | `gbolt_chest_b` | `gbolt_Cap_chest_hips_tight` | `gbolt_Cap_hips_chest_tight` | `g_guts_lrg` (`to_gbolt_Cap_chest_hips`) | torso | GZ_CHEST_FRONT | — |
| 9 | GZ_ARM_UPPER_RIGHT | — | `gbolt_Cap_rbicep_chest_tight` | `gbolt_Cap_chest_rbicep_tight` | `g_arm_torn_anim1` (`to_gbolt_Cap_chest_rbicep`) | right upper arm | GZ_ARM_LOWER_RIGHT | — |
| 10 | GZ_ARM_UPPER_LEFT | — | `gbolt_Cap_lbicep_chest_tight` | `gbolt_Cap_chest_lbicep_tight` | `g_arm_torn_anim1` (`to_gbolt_Cap_chest_lbicep`) | left upper arm | GZ_ARM_LOWER_LEFT | — |
| 11 | GZ_ARM_LOWER_RIGHT | — | `gbolt_Cap_rforearm_rbicep_t` | `gbolt_Cap_rbicep_rforearm_t` | `g_bone_anim1` (`to_gbolt`) | right forearm | — | — |
| 12 | GZ_ARM_LOWER_LEFT | — | `gbolt_Cap_lforearm_lbicep_t` | `gbolt_Cap_lbicep_lforearm_t` | `g_bone_anim1` (`to_gbolt`) | left forearm | — | — |
| 13 | GZ_GUT_FRONT | `gbolt_gut_f` | — | — | — | end | — | GZ_GUT_FRONT_EXTRA |
| 14 | GZ_GUT_BACK | `gbolt_gut_b` | — | — | — | end | — | GZ_GUT_BACK_EXTRA |
| 15 | GZ_GROIN | `gbolt_groin` | — | — | — | groin | — | GZ_GUT_FRONT |
| 16 | GZ_LEG_UPPER_RIGHT_FRONT | — | `gbolt_Cap_rthigh_hips_tight` | `gbolt_Cap_hips_rthigh_tight` | `g_bone_anim1` (`to_gbolt`) | right thigh | GZ_LEG_UPPER_RIGHT_BACK | GZ_LEG_UPPER_RIGHT_BACK |
| 17 | GZ_LEG_UPPER_RIGHT_BACK | — | `gbolt_Cap_rthigh_hips_tight` | `gbolt_Cap_hips_rthigh_tight` | `g_bone_anim1` (`to_gbolt`) | right thigh | GZ_LEG_UPPER_RIGHT_FRONT | GZ_LEG_UPPER_RIGHT_FRONT |
| 18 | GZ_LEG_UPPER_LEFT_FRONT | — | `gbolt_Cap_lthigh_hips_tight` | `gbolt_Cap_hips_lthigh_tight` | `g_bone_anim1` (`to_gbolt`) | left thigh | GZ_LEG_UPPER_LEFT_BACK | GZ_LEG_UPPER_LEFT_BACK |
| 19 | GZ_LEG_UPPER_LEFT_BACK | — | `gbolt_Cap_lthigh_hips_tight` | `gbolt_Cap_hips_lthigh_tight` | `g_bone_anim1` (`to_gbolt`) | left thigh | GZ_LEG_UPPER_LEFT_FRONT | GZ_LEG_UPPER_LEFT_FRONT |
| 20 | GZ_LEG_LOWER_RIGHT | — | `gbolt_Cap_rcalf_rthigh_tight` | `gbolt_Cap_rthigh_rcalf_tight` | `g_bone_anim1` (`to_gbolt`) | right calf | — | — |
| 21 | GZ_LEG_LOWER_LEFT | — | `gbolt_Cap_lcalf_lthigh_tight` | `gbolt_Cap_lthigh_lcalf_tight` | `g_bone_anim1` (`to_gbolt`) | left calf | — | — |
| 22 | GZ_FOOT_RIGHT | `sbolt_heel_r` | — | — | — | right foot | — | GZ_LEG_LOWER_RIGHT |
| 23 | GZ_FOOT_LEFT | `sbolt_heel_l` | — | — | — | left foot | — | GZ_LEG_LOWER_LEFT |
| 24 | GZ_GUT_FRONT_EXTRA | — | — | — | — | end | — | — |
| 25 | GZ_GUT_BACK_EXTRA | — | — | — | — | end | — | — |

Sever groups (start → parts and bolts that move to the piece):

| zone(s) | segment | contents |
|---|---|---|
| GZ_CHEST_FRONT, GZ_CHEST_BACK | torso | `_tightchest`, `_maskchest`, `_bulkychest`, `_armorchest`, `_bosschest`, `_chest_b_t`, `_neck`, `_bossneck`, `_maskneck`, `_hoodneck`, `_cap_head_neck`, `_cap_head_neck_boss`, bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_cap_chest_rbicep_bulky`, `_cap_chest_lbicep_bulky`, `_cap_rbicep_chest_bulky`, `_cap_lbicep_chest_bulky`, `_cap_chest_rbicep_boss`, `_cap_chest_lbicep_boss`, `_cap_rbicep_chest_boss`, `_cap_lbicep_chest_boss`, +head & both upper arms |
| GZ_NECK | neck | `_neck`, `_bossneck`, `_maskneck`, `_hoodneck`, `_cap_head_neck`, `_cap_head_neck_boss`, bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_cap_chest_rbicep_bulky`, `_cap_chest_lbicep_bulky`, `_cap_rbicep_chest_bulky`, `_cap_lbicep_chest_bulky`, `_cap_chest_rbicep_boss`, `_cap_chest_lbicep_boss`, `_cap_rbicep_chest_boss`, `_cap_lbicep_chest_boss`, +head & both upper arms |
| GZ_SHLDR_RIGHT_FRONT | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_cap_chest_rbicep_bulky`, `_cap_chest_lbicep_bulky`, `_cap_rbicep_chest_bulky`, `_cap_lbicep_chest_bulky`, `_cap_chest_rbicep_boss`, `_cap_chest_lbicep_boss`, `_cap_rbicep_chest_boss`, `_cap_lbicep_chest_boss`, +head & both upper arms |
| GZ_SHLDR_RIGHT_BACK | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_cap_chest_rbicep_bulky`, `_cap_chest_lbicep_bulky`, `_cap_rbicep_chest_bulky`, `_cap_lbicep_chest_bulky`, `_cap_chest_rbicep_boss`, `_cap_chest_lbicep_boss`, `_cap_rbicep_chest_boss`, `_cap_lbicep_chest_boss`, +head & both upper arms |
| GZ_SHLDR_LEFT_FRONT | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_cap_chest_rbicep_bulky`, `_cap_chest_lbicep_bulky`, `_cap_rbicep_chest_bulky`, `_cap_lbicep_chest_bulky`, `_cap_chest_rbicep_boss`, `_cap_chest_lbicep_boss`, `_cap_rbicep_chest_boss`, `_cap_lbicep_chest_boss`, +head & both upper arms |
| GZ_SHLDR_LEFT_BACK | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_cap_chest_rbicep_bulky`, `_cap_chest_lbicep_bulky`, `_cap_rbicep_chest_bulky`, `_cap_lbicep_chest_bulky`, `_cap_chest_rbicep_boss`, `_cap_chest_lbicep_boss`, `_cap_rbicep_chest_boss`, `_cap_lbicep_chest_boss`, +head & both upper arms |
| GZ_HEAD_FRONT, GZ_HEAD_BACK | head | `_baldhead`, `_crewhead`, `_beardhead`, `_helmethead`, `_hoodhead`, `_maskhead`, `_bosshead`, `_comface`, `_beardface`, `_helmetface`, `_bossface`, `_lbang`, `_rbang`, `_mohawk`, `_ponytail`, `_lear`, `_rear`, `_lbossear`, `_rbossear`, bolt `abolt_head_t` |
| GZ_ARM_UPPER_LEFT | left upper arm | `_lbossbicep`, `_lbicep_t_t`, `_lbicep_t_b`, `_lbicep_b_b`, `_lbicep_b_t`, `_lforearm_t_t`, `_lforearm_t_c`, `_lforearm_t_b`, `_lforearm_b_t`, `_lforearm_b_b`, `_lbossforearm`, `_lopen`, `_lclosed`, `_lbossopen`, `_lbossclosed`, bolt `wbolt_hand_l` |
| GZ_ARM_LOWER_LEFT | left forearm | `_lforearm_t_t`, `_lforearm_t_c`, `_lforearm_t_b`, `_lforearm_b_t`, `_lforearm_b_b`, `_lbossforearm`, `_lopen`, `_lclosed`, `_lbossopen`, `_lbossclosed`, bolt `wbolt_hand_l` |
| GZ_ARM_UPPER_RIGHT | right upper arm | `_rbossbicep`, `_rbicep_t_t`, `_rbicep_t_b`, `_rbicep_b_b`, `_rbicep_b_t`, `_rforearm_t_t`, `_rforearm_t_c`, `_rforearm_t_b`, `_rforearm_b_t`, `_rforearm_b_b`, `_rbossforearm`, `_ropen`, `_rclosed`, `_rbossopen`, `_rbossclosed`, bolt `wbolt_hand_r` |
| GZ_ARM_LOWER_RIGHT | right forearm | `_rforearm_t_t`, `_rforearm_t_c`, `_rforearm_t_b`, `_rforearm_b_t`, `_rforearm_b_b`, `_rbossforearm`, `_ropen`, `_rclosed`, `_rbossopen`, `_rbossclosed`, bolt `wbolt_hand_r` |
| GZ_LEG_UPPER_RIGHT_FRONT, GZ_LEG_UPPER_RIGHT_BACK | right thigh | `_rbossthigh`, `_rtightthigh`, `_rbulkythigh`, bolt `abolt_thigh_r`, +right lower leg zone, `_rtightcalf`, `_rcalfcuff`, `_rbulkycalf`, `_rbosscalf`, `_rcalf_t_b`, bolt `abolt_calf_r`, `_rbootfoot`, `_rshoefoot` |
| GZ_LEG_LOWER_RIGHT | right calf | `_rtightcalf`, `_rcalfcuff`, `_rbulkycalf`, `_rbosscalf`, `_rcalf_t_b`, bolt `abolt_calf_r`, `_rbootfoot`, `_rshoefoot` |
| GZ_FOOT_RIGHT | right foot | `_rbootfoot`, `_rshoefoot` |
| GZ_LEG_UPPER_LEFT_FRONT, GZ_LEG_UPPER_LEFT_BACK | left thigh | `_lbossthigh`, `_ltightthigh`, `_lbulkythigh`, bolt `abolt_thigh_l`, +left lower leg zone, `_ltightcalf`, `_lcalfcuff`, `_lbulkycalf`, `_lbosscalf`, `_lcalf_t_b`, bolt `abolt_calf_l`, `_lbootfoot`, `_lshoefoot` |
| GZ_LEG_LOWER_LEFT | left calf | `_ltightcalf`, `_lcalfcuff`, `_lbulkycalf`, `_lbosscalf`, `_lcalf_t_b`, bolt `abolt_calf_l`, `_lbootfoot`, `_lshoefoot` |
| GZ_FOOT_LEFT | left foot | `_lbootfoot`, `_lshoefoot` |
| GZ_GROIN | groin | (groin marker) |
| GZ_GUT_FRONT, GZ_GUT_BACK, GZ_GUT_FRONT_EXTRA, GZ_GUT_BACK_EXTRA | end | — |

Variant table (first visible mesh of each priority chain wins; columns give what that variant sets):

| visible mesh | zone: field = value |
|---|---|
| `_baldhead` | HEAD_BACK wound mesh = `gz_baldhead_b`; HEAD_BACK owner = `_baldhead` |
| `_crewhead` | HEAD_BACK wound mesh = `gz_crewhead_b`; HEAD_BACK owner = `_crewhead` |
| `_beardhead` | HEAD_BACK wound mesh = `gz_beardhead_b`; HEAD_BACK owner = `_beardhead` |
| `_hoodhead` | HEAD_BACK wound mesh = `gz_hoodhead_b`; HEAD_BACK owner = `_hoodhead` |
| `_bosshead` | HEAD_BACK wound mesh = `gz_bosshead_b`; HEAD_BACK owner = `_bosshead` |
| `_maskhead` | HEAD_FRONT wound mesh = `gz_maskhead_f`; HEAD_FRONT owner = `_maskhead`; HEAD_BACK wound mesh = `gz_maskhead_b`; HEAD_BACK owner = `_maskhead` |
| `_helmethead` | HEAD_BACK owner = `_helmethead` |
| `_comface` | HEAD_FRONT wound mesh = `gz_comface`; HEAD_FRONT owner = `_comface` |
| `_beardface` | HEAD_FRONT wound mesh = `gz_beardface`; HEAD_FRONT owner = `_beardface` |
| `_helmetface` | HEAD_FRONT wound mesh = `gz_helmetface`; HEAD_FRONT owner = `_helmetface` |
| `_bossface` | HEAD_FRONT wound mesh = `gz_bossface`; HEAD_FRONT owner = `_bossface` |
| `_neck` | HEAD_FRONT body cap = `_Cap_neck_head`; HEAD_FRONT piece cap = `_Cap_head_neck`; HEAD_BACK body cap = `_Cap_neck_head`; HEAD_BACK piece cap = `_Cap_head_neck`; NECK owner = `_neck` |
| `_bossneck` | HEAD_FRONT body cap = `_Cap_neck_head_boss`; HEAD_FRONT piece cap = `_Cap_head_neck_boss`; HEAD_BACK body cap = `_Cap_neck_head_boss`; HEAD_BACK piece cap = `_Cap_head_neck_boss`; NECK owner = `_bossneck` |
| `_tightchest` | SHLDR_RIGHT_FRONT wound mesh = `gz_tightchest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_tightchest`; SHLDR_RIGHT_BACK wound mesh = `gz_tightchest_rbshldr`; SHLDR_RIGHT_BACK owner = `_tightchest`; SHLDR_LEFT_FRONT wound mesh = `gz_tightchest_lfshldr`; SHLDR_LEFT_FRONT owner = `_tightchest`; SHLDR_LEFT_BACK wound mesh = `gz_tightchest_lbshldr`; SHLDR_LEFT_BACK owner = `_tightchest`; CHEST_FRONT wound mesh = `gz_tightchest_f`; CHEST_FRONT owner = `_tightchest`; CHEST_FRONT piece cap = `_Cap_chest_hips_tight`; CHEST_BACK wound mesh = `gz_tightchest_b`; CHEST_BACK owner = `_tightchest`; CHEST_BACK piece cap = `_Cap_chest_hips_tight`; ARM_UPPER_RIGHT body cap = `_Cap_chest_rbicep_tight`; ARM_UPPER_LEFT body cap = `_Cap_chest_lbicep_tight` |
| `_bulkychest` | SHLDR_RIGHT_FRONT wound mesh = `gz_bulkychest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_bulkychest`; SHLDR_RIGHT_BACK wound mesh = `gz_bulkychest_rbshldr`; SHLDR_RIGHT_BACK owner = `_bulkychest`; SHLDR_LEFT_FRONT wound mesh = `gz_bulkychest_lfshldr`; SHLDR_LEFT_FRONT owner = `_bulkychest`; SHLDR_LEFT_BACK wound mesh = `gz_bulkychest_lbshldr`; SHLDR_LEFT_BACK owner = `_bulkychest`; CHEST_FRONT wound mesh = `gz_bulkychest_f`; CHEST_FRONT owner = `_bulkychest`; CHEST_FRONT piece cap = `_Cap_chest_hips_bulky`; CHEST_BACK wound mesh = `gz_bulkychest_b`; CHEST_BACK owner = `_bulkychest`; CHEST_BACK piece cap = `_Cap_chest_hips_bulky`; ARM_UPPER_RIGHT body cap = `_Cap_chest_rbicep_bulky`; ARM_UPPER_LEFT body cap = `_Cap_chest_lbicep_bulky` |
| `_chest_b_t` | SHLDR_RIGHT_FRONT wound mesh = `gz_chest_b_t_rfshldr`; SHLDR_RIGHT_FRONT owner = `_chest_b_t`; SHLDR_RIGHT_BACK wound mesh = `gz_chest_b_t_rbshldr`; SHLDR_RIGHT_BACK owner = `_chest_b_t`; SHLDR_LEFT_FRONT wound mesh = `gz_chest_b_t_lfshldr`; SHLDR_LEFT_FRONT owner = `_chest_b_t`; SHLDR_LEFT_BACK wound mesh = `gz_chest_b_t_lbshldr`; SHLDR_LEFT_BACK owner = `_chest_b_t`; CHEST_FRONT wound mesh = `gz_chest_b_t_f`; CHEST_FRONT owner = `_chest_b_t`; CHEST_FRONT piece cap = `_Cap_chest_hips_tight`; CHEST_BACK wound mesh = `gz_chest_b_t_b`; CHEST_BACK owner = `_chest_b_t`; CHEST_BACK piece cap = `_Cap_chest_hips_tight`; ARM_UPPER_RIGHT body cap = `_Cap_chest_rbicep_bulky`; ARM_UPPER_LEFT body cap = `_Cap_chest_lbicep_bulky` |
| `_bosschest` | SHLDR_RIGHT_FRONT wound mesh = `gz_bosschest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_bosschest`; SHLDR_RIGHT_BACK wound mesh = `gz_bosschest_rbshldr`; SHLDR_RIGHT_BACK owner = `_bosschest`; SHLDR_LEFT_FRONT wound mesh = `gz_bosschest_lfshldr`; SHLDR_LEFT_FRONT owner = `_bosschest`; SHLDR_LEFT_BACK wound mesh = `gz_bosschest_lbshldr`; SHLDR_LEFT_BACK owner = `_bosschest`; CHEST_FRONT wound mesh = `gz_bosschest_f`; CHEST_FRONT owner = `_bosschest`; CHEST_FRONT piece cap = `_Cap_chest_hips_boss`; CHEST_BACK wound mesh = `gz_bosschest_b`; CHEST_BACK owner = `_bosschest`; CHEST_BACK piece cap = `_Cap_chest_hips_boss`; ARM_UPPER_RIGHT body cap = `_Cap_chest_rbicep_boss`; ARM_UPPER_LEFT body cap = `_Cap_chest_lbicep_boss` |
| `_armorchest` | SHLDR_RIGHT_FRONT wound mesh = `gz_armorchest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_armorchest`; SHLDR_RIGHT_BACK wound mesh = `gz_armorchest_rbshldr`; SHLDR_RIGHT_BACK owner = `_armorchest`; SHLDR_LEFT_FRONT wound mesh = `gz_armorchest_lfshldr`; SHLDR_LEFT_FRONT owner = `_armorchest`; SHLDR_LEFT_BACK wound mesh = `gz_armorchest_lbshldr`; SHLDR_LEFT_BACK owner = `_armorchest`; CHEST_FRONT wound mesh = `gz_armorchest_f`; CHEST_FRONT owner = `_armorchest`; CHEST_FRONT piece cap = `_Cap_chest_hips_bulky`; CHEST_BACK wound mesh = `gz_armorchest_b`; CHEST_BACK owner = `_armorchest`; CHEST_BACK piece cap = `_Cap_chest_hips_bulky` |
| `_maskchest` | HEAD_FRONT body cap bolt = `no_gore_for_me_please`; HEAD_BACK body cap bolt = `no_gore_for_me_please`; CHEST_FRONT wound mesh = `gz_maskchest_f`; CHEST_FRONT owner = `_maskchest`; CHEST_FRONT piece cap = `_Cap_chest_hips_bulky`; CHEST_BACK wound mesh = `gz_maskchest_b`; CHEST_BACK owner = `_maskchest`; CHEST_BACK piece cap = `_Cap_chest_hips_bulky`; ARM_UPPER_RIGHT body cap = `_Cap_chest_rbicep_bulky`; ARM_UPPER_LEFT body cap = `_Cap_chest_lbicep_bulky` |
| `_maskneck` | SHLDR_RIGHT_FRONT wound mesh = `gz_maskneck_rfshldr`; SHLDR_RIGHT_FRONT owner = `_maskneck`; SHLDR_RIGHT_BACK wound mesh = `gz_maskneck_rbshldr`; SHLDR_RIGHT_BACK owner = `_maskneck`; SHLDR_LEFT_FRONT wound mesh = `gz_maskneck_lfshldr`; SHLDR_LEFT_FRONT owner = `_maskneck`; SHLDR_LEFT_BACK wound mesh = `gz_maskneck_lbshldr`; SHLDR_LEFT_BACK owner = `_maskneck` |
| `_hoodneck` | SHLDR_RIGHT_FRONT wound mesh = `gz_hoodneck_rfshldr`; SHLDR_RIGHT_FRONT owner = `_hoodneck`; SHLDR_RIGHT_BACK wound mesh = `gz_hoodneck_rbshldr`; SHLDR_RIGHT_BACK owner = `_hoodneck`; SHLDR_LEFT_FRONT wound mesh = `gz_hoodneck_lfshldr`; SHLDR_LEFT_FRONT owner = `_hoodneck`; SHLDR_LEFT_BACK wound mesh = `gz_hoodneck_lbshldr`; SHLDR_LEFT_BACK owner = `_hoodneck` |
| `_rbicep_b_b` | ARM_UPPER_RIGHT wound mesh = `gz_rbicep_b_b`; ARM_UPPER_RIGHT owner = `_rbicep_b_b`; ARM_UPPER_RIGHT piece cap = `_Cap_rbicep_chest_bulky`; ARM_LOWER_RIGHT body cap = `_Cap_rbicep_rforearm_b` |
| `_rbicep_b_t` | ARM_UPPER_RIGHT wound mesh = `gz_rbicep_b_t`; ARM_UPPER_RIGHT owner = `_rbicep_b_t`; ARM_UPPER_RIGHT piece cap = `_Cap_rbicep_chest_bulky`; ARM_LOWER_RIGHT body cap = `_Cap_rbicep_rforearm_t` |
| `_rbicep_t_b` | ARM_UPPER_RIGHT wound mesh = `gz_rbicep_t_b`; ARM_UPPER_RIGHT owner = `_rbicep_t_b`; ARM_UPPER_RIGHT piece cap = `_Cap_rbicep_chest_tight`; ARM_LOWER_RIGHT body cap = `_Cap_rbicep_rforearm_b` |
| `_rbicep_t_t` | ARM_UPPER_RIGHT wound mesh = `gz_rbicep_t_t`; ARM_UPPER_RIGHT owner = `_rbicep_t_t`; ARM_UPPER_RIGHT piece cap = `_Cap_rbicep_chest_tight`; ARM_LOWER_RIGHT body cap = `_Cap_rbicep_rforearm_t` |
| `_rbossbicep` | ARM_UPPER_RIGHT wound mesh = `gz_rbossbicep`; ARM_UPPER_RIGHT owner = `_rbossbicep`; ARM_UPPER_RIGHT piece cap = `_Cap_rbicep_chest_boss`; ARM_LOWER_RIGHT body cap = `_Cap_rbicep_rforearm_boss` |
| `_lbicep_b_b` | ARM_UPPER_LEFT wound mesh = `gz_lbicep_b_b`; ARM_UPPER_LEFT owner = `_lbicep_b_b`; ARM_UPPER_LEFT piece cap = `_Cap_lbicep_chest_bulky`; ARM_LOWER_LEFT body cap = `_Cap_lbicep_lforearm_b` |
| `_lbicep_b_t` | ARM_UPPER_LEFT wound mesh = `gz_lbicep_b_t`; ARM_UPPER_LEFT owner = `_lbicep_b_t`; ARM_UPPER_LEFT piece cap = `_Cap_lbicep_chest_bulky`; ARM_LOWER_LEFT body cap = `_Cap_lbicep_lforearm_t` |
| `_lbicep_t_b` | ARM_UPPER_LEFT wound mesh = `gz_lbicep_t_b`; ARM_UPPER_LEFT owner = `_lbicep_t_b`; ARM_UPPER_LEFT piece cap = `_Cap_lbicep_chest_tight`; ARM_LOWER_LEFT body cap = `_Cap_lbicep_lforearm_b` |
| `_lbicep_t_t` | ARM_UPPER_LEFT wound mesh = `gz_lbicep_t_t`; ARM_UPPER_LEFT owner = `_lbicep_t_t`; ARM_UPPER_LEFT piece cap = `_Cap_lbicep_chest_tight`; ARM_LOWER_LEFT body cap = `_Cap_lbicep_lforearm_t` |
| `_lbossbicep` | ARM_UPPER_LEFT wound mesh = `gz_lbossbicep`; ARM_UPPER_LEFT owner = `_lbossbicep`; ARM_UPPER_LEFT piece cap = `_Cap_lbicep_chest_boss`; ARM_LOWER_LEFT body cap = `_Cap_lbicep_lforearm_boss` |
| `_lforearm_b_b` | ARM_LOWER_LEFT wound mesh = `gz_lforearm_b_b`; ARM_LOWER_LEFT owner = `_lforearm_b_b`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_b` |
| `_lforearm_b_t` | ARM_LOWER_LEFT wound mesh = `gz_lforearm_b_t`; ARM_LOWER_LEFT owner = `_lforearm_b_t`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_b` |
| `_lforearm_t_t` | ARM_LOWER_LEFT wound mesh = `gz_lforearm_t_t`; ARM_LOWER_LEFT owner = `_lforearm_t_t`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_t` |
| `_lforearm_t_b` | ARM_LOWER_LEFT wound mesh = `gz_lforearm_t_b`; ARM_LOWER_LEFT owner = `_lforearm_t_b`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_t` |
| `_lforearm_t_c` | ARM_LOWER_LEFT wound mesh = `gz_lforearm_t_c`; ARM_LOWER_LEFT owner = `_lforearm_t_c`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_t` |
| `_lbossforearm` | ARM_LOWER_LEFT wound mesh = `gz_lbossforearm`; ARM_LOWER_LEFT owner = `_lbossforearm`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_boss` |
| `_rforearm_b_b` | ARM_LOWER_RIGHT wound mesh = `gz_rforearm_b_b`; ARM_LOWER_RIGHT owner = `_rforearm_b_b`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_b` |
| `_rforearm_b_t` | ARM_LOWER_RIGHT wound mesh = `gz_rforearm_b_t`; ARM_LOWER_RIGHT owner = `_rforearm_b_t`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_b` |
| `_rforearm_t_t` | ARM_LOWER_RIGHT wound mesh = `gz_rforearm_t_t`; ARM_LOWER_RIGHT owner = `_rforearm_t_t`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_t` |
| `_rforearm_t_b` | ARM_LOWER_RIGHT wound mesh = `gz_rforearm_t_b`; ARM_LOWER_RIGHT owner = `_rforearm_t_b`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_t` |
| `_rforearm_t_c` | ARM_LOWER_RIGHT wound mesh = `gz_rforearm_t_c`; ARM_LOWER_RIGHT owner = `_rforearm_t_c`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_t` |
| `_rbossforearm` | ARM_LOWER_RIGHT wound mesh = `gz_rbossforearm`; ARM_LOWER_RIGHT owner = `_rbossforearm`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_boss` |
| `_coathips` | GUT_FRONT wound mesh = `gz_coathips_rfthigh`; GUT_FRONT owner = `_coathips`; GUT_BACK wound mesh = `gz_coathips_rbthigh`; GUT_BACK owner = `_coathips`; GROIN owner = `_coathips`; GUT_FRONT_EXTRA wound mesh = `gz_coathips_lfthigh`; GUT_BACK_EXTRA wound mesh = `gz_coathips_lbthigh` |
| `_tighthips` | CHEST_FRONT body cap = `_Cap_hips_chest_tight`; CHEST_BACK body cap = `_Cap_hips_chest_tight`; GUT_FRONT wound mesh = `gz_tighthips_f`; GUT_FRONT owner = `_tighthips`; GUT_BACK wound mesh = `gz_tighthips_b`; GUT_BACK owner = `_tighthips`; GROIN owner = `_tighthips`; LEG_UPPER_RIGHT_FRONT body cap = `_Cap_hips_rthigh_tight`; LEG_UPPER_RIGHT_BACK body cap = `_Cap_hips_rthigh_tight`; LEG_UPPER_LEFT_FRONT body cap = `_Cap_hips_lthigh_tight`; LEG_UPPER_LEFT_BACK body cap = `_Cap_hips_lthigh_tight` |
| `_bulkyhips` | CHEST_FRONT body cap = `_Cap_hips_chest_bulky`; CHEST_BACK body cap = `_Cap_hips_chest_bulky`; GUT_FRONT wound mesh = `gz_bulkyhips_fgut`; GUT_FRONT owner = `_bulkyhips`; GUT_BACK wound mesh = `gz_bulkyhips_bgut`; GUT_BACK owner = `_bulkyhips`; GROIN owner = `_bulkyhips`; LEG_UPPER_RIGHT_FRONT body cap = `_Cap_hips_rthigh_bulky`; LEG_UPPER_RIGHT_BACK body cap = `_Cap_hips_rthigh_bulky`; LEG_UPPER_LEFT_FRONT body cap = `_Cap_hips_lthigh_bulky`; LEG_UPPER_LEFT_BACK body cap = `_Cap_hips_lthigh_bulky` |
| `_bosships` | CHEST_FRONT body cap = `_Cap_hips_chest_boss`; CHEST_BACK body cap = `_Cap_hips_chest_boss`; GUT_FRONT wound mesh = `gz_bosships_fgut`; GUT_FRONT owner = `_bosships`; GUT_BACK wound mesh = `gz_bosships_bgut`; GUT_BACK owner = `_bosships`; GROIN owner = `_bosships`; LEG_UPPER_RIGHT_FRONT body cap = `_Cap_hips_rthigh_boss`; LEG_UPPER_RIGHT_BACK body cap = `_Cap_hips_rthigh_boss`; LEG_UPPER_LEFT_FRONT body cap = `_Cap_hips_lthigh_boss`; LEG_UPPER_LEFT_BACK body cap = `_Cap_hips_lthigh_boss` |
| `_rtightthigh` | LEG_UPPER_RIGHT_FRONT wound mesh = `gz_rtightthigh_f`; LEG_UPPER_RIGHT_FRONT owner = `_rtightthigh`; LEG_UPPER_RIGHT_FRONT piece cap = `_Cap_rthigh_hips_tight`; LEG_UPPER_RIGHT_BACK wound mesh = `gz_rtightthigh_b`; LEG_UPPER_RIGHT_BACK owner = `_rtightthigh`; LEG_UPPER_RIGHT_BACK piece cap = `_Cap_rthigh_hips_tight`; LEG_LOWER_RIGHT body cap = `_Cap_rthigh_rcalf_tight` |
| `_rbulkythigh` | LEG_UPPER_RIGHT_FRONT wound mesh = `gz_rbulkythigh_f`; LEG_UPPER_RIGHT_FRONT owner = `_rbulkythigh`; LEG_UPPER_RIGHT_FRONT piece cap = `_Cap_rthigh_hips_bulky`; LEG_UPPER_RIGHT_BACK wound mesh = `gz_rbulkythigh_b`; LEG_UPPER_RIGHT_BACK owner = `_rbulkythigh`; LEG_UPPER_RIGHT_BACK piece cap = `_Cap_rthigh_hips_bulky`; LEG_LOWER_RIGHT body cap = `_Cap_rthigh_rcalf_bulky` |
| `_rbossthigh` | LEG_UPPER_RIGHT_FRONT wound mesh = `gz_rbossthigh_f`; LEG_UPPER_RIGHT_FRONT owner = `_rbossthigh`; LEG_UPPER_RIGHT_FRONT piece cap = `_Cap_rthigh_hips_boss`; LEG_UPPER_RIGHT_BACK wound mesh = `gz_rbossthigh_b`; LEG_UPPER_RIGHT_BACK owner = `_rbossthigh`; LEG_UPPER_RIGHT_BACK piece cap = `_Cap_rthigh_hips_boss`; LEG_LOWER_RIGHT body cap = `_Cap_rthigh_rcalf_boss` |
| `_ltightthigh` | LEG_UPPER_LEFT_FRONT wound mesh = `gz_ltightthigh_f`; LEG_UPPER_LEFT_FRONT owner = `_ltightthigh`; LEG_UPPER_LEFT_FRONT piece cap = `_Cap_lthigh_hips_tight`; LEG_UPPER_LEFT_BACK wound mesh = `gz_ltightthigh_b`; LEG_UPPER_LEFT_BACK owner = `_ltightthigh`; LEG_UPPER_LEFT_BACK piece cap = `_Cap_lthigh_hips_tight`; LEG_LOWER_LEFT body cap = `_Cap_lthigh_lcalf_tight` |
| `_lbulkythigh` | LEG_UPPER_LEFT_FRONT wound mesh = `gz_lbulkythigh_f`; LEG_UPPER_LEFT_FRONT owner = `_lbulkythigh`; LEG_UPPER_LEFT_FRONT piece cap = `_Cap_lthigh_hips_bulky`; LEG_UPPER_LEFT_BACK wound mesh = `gz_lbulkythigh_b`; LEG_UPPER_LEFT_BACK owner = `_lbulkythigh`; LEG_UPPER_LEFT_BACK piece cap = `_Cap_lthigh_hips_bulky`; LEG_LOWER_LEFT body cap = `_Cap_lthigh_lcalf_bulky` |
| `_lbossthigh` | LEG_UPPER_LEFT_FRONT wound mesh = `gz_lbossthigh_f`; LEG_UPPER_LEFT_FRONT owner = `_lbossthigh`; LEG_UPPER_LEFT_FRONT piece cap = `_Cap_lthigh_hips_boss`; LEG_UPPER_LEFT_BACK wound mesh = `gz_lbossthigh_b`; LEG_UPPER_LEFT_BACK owner = `_lbossthigh`; LEG_UPPER_LEFT_BACK piece cap = `_Cap_lthigh_hips_boss`; LEG_LOWER_LEFT body cap = `_Cap_lthigh_lcalf_boss` |
| `_ltightcalf` | LEG_LOWER_LEFT wound mesh = `gz_ltightcalf`; LEG_LOWER_LEFT owner = `_ltightcalf`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh_tight` |
| `_lcalf_t_b` | LEG_LOWER_LEFT wound mesh = `gz_lcalf_t_b`; LEG_LOWER_LEFT owner = `_lcalf_t_b`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh_tight` |
| `_lbulkycalf` | LEG_LOWER_LEFT wound mesh = `gz_lbulkycalf`; LEG_LOWER_LEFT owner = `_lbulkycalf`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh_bulky` |
| `_lbosscalf` | LEG_LOWER_LEFT wound mesh = `gz_lbosscalf`; LEG_LOWER_LEFT owner = `_lbosscalf`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh_boss` |
| `_lcalfcuff` | LEG_LOWER_LEFT wound mesh = `gz_lcalfcuff`; LEG_LOWER_LEFT owner = `_lcalfcuff`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh_tight` |
| `_rtightcalf` | LEG_LOWER_RIGHT wound mesh = `gz_rtightcalf`; LEG_LOWER_RIGHT owner = `_rtightcalf`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh_tight` |
| `_rcalf_t_b` | LEG_LOWER_RIGHT wound mesh = `gz_rcalf_t_b`; LEG_LOWER_RIGHT owner = `_rcalf_t_b`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh_tight` |
| `_rbulkycalf` | LEG_LOWER_RIGHT wound mesh = `gz_rbulkycalf`; LEG_LOWER_RIGHT owner = `_rbulkycalf`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh_bulky` |
| `_rbosscalf` | LEG_LOWER_RIGHT wound mesh = `gz_rbosscalf`; LEG_LOWER_RIGHT owner = `_rbosscalf`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh_boss` |
| `_rcalfcuff` | LEG_LOWER_RIGHT wound mesh = `gz_rcalfcuff`; LEG_LOWER_RIGHT owner = `_rcalfcuff`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh_tight` |
| `_lbootfoot` | FOOT_LEFT owner = `_lbootfoot` |
| `_lshoefoot` | FOOT_LEFT owner = `_lshoefoot` |
| `_rbootfoot` | FOOT_RIGHT owner = `_rbootfoot` |
| `_rshoefoot` | FOOT_RIGHT owner = `_rshoefoot` |

#### Civilian/officer family (`enemy/ecto`)

| # | zone | gore bolt | cap bolt on severed piece | cap bolt on body | cap bolt-on model (its bolt) | sever group (segment) | next zone of piece | wound link |
|---|---|---|---|---|---|---|---|---|
| 0 | GZ_HEAD_FRONT | `gbolt_head_f` | `gbolt_Cap_head_chest` | `gbolt_Cap_chest_head` | `g_neck_torn_anim2` (`to_gbolt_Cap_chest_head`) | head | GZ_HEAD_BACK | — |
| 1 | GZ_HEAD_BACK | `gbolt_head_b` | `gbolt_Cap_head_chest` | `gbolt_Cap_chest_head` | `g_neck_torn_anim2` (`to_gbolt_Cap_chest_head`) | head | GZ_HEAD_FRONT | — |
| 2 | GZ_NECK | — | — | — | — | end | — | — |
| 3 | GZ_SHLDR_RIGHT_FRONT | `gbolt_shoulder_rf` | `gbolt_Cap_chest_hips` | — | — | shoulder | GZ_SHLDR_LEFT_FRONT | — |
| 4 | GZ_SHLDR_RIGHT_BACK | `gbolt_shoulder_rb` | `gbolt_Cap_chest_hips` | — | — | shoulder | GZ_SHLDR_LEFT_BACK | — |
| 5 | GZ_SHLDR_LEFT_FRONT | `gbolt_shoulder_lf` | `gbolt_Cap_chest_hips` | — | — | shoulder | GZ_SHLDR_RIGHT_BACK | — |
| 6 | GZ_SHLDR_LEFT_BACK | `gbolt_shoulder_lb` | `gbolt_Cap_chest_hips` | — | — | torso | GZ_CHEST_BACK | — |
| 7 | GZ_CHEST_FRONT | `gbolt_chest_f` | `gbolt_Cap_chest_hips` | `gbolt_Cap_hips_chest` | `g_guts_lrg` (`to_gbolt_Cap_chest_hips`) | torso | GZ_SHLDR_RIGHT_FRONT | — |
| 8 | GZ_CHEST_BACK | `gbolt_chest_b` | `gbolt_Cap_chest_hips` | `gbolt_Cap_hips_chest` | `g_guts_lrg` (`to_gbolt_Cap_chest_hips`) | torso | GZ_CHEST_FRONT | — |
| 9 | GZ_ARM_UPPER_RIGHT | — | `gbolt_Cap_rbicep_chest` | `gbolt_Cap_chest_rbicep` | `g_arm_torn_anim1` (`to_gbolt_Cap_chest_rbicep`) | right upper arm | — | — |
| 10 | GZ_ARM_UPPER_LEFT | — | `gbolt_Cap_lbicep_chest` | `gbolt_Cap_chest_lbicep` | `g_arm_torn_anim1` (`to_gbolt_Cap_chest_lbicep`) | left upper arm | — | — |
| 11 | GZ_ARM_LOWER_RIGHT | — | `gbolt_Cap_rforearm_rbicep` | `gbolt_Cap_rbicep_rforearm` | `g_bone_anim1` (`to_gbolt`) | right forearm | — | — |
| 12 | GZ_ARM_LOWER_LEFT | — | `gbolt_Cap_lforearm_lbicep` | `gbolt_Cap_lbicep_lforearm` | `g_bone_anim1` (`to_gbolt`) | left forearm | — | — |
| 13 | GZ_GUT_FRONT | `gbolt_gut_f` | — | — | — | end | — | — |
| 14 | GZ_GUT_BACK | `gbolt_gut_b` | — | — | — | end | — | — |
| 15 | GZ_GROIN | — | — | — | — | end | — | — |
| 16 | GZ_LEG_UPPER_RIGHT_FRONT | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | — | — |
| 17 | GZ_LEG_UPPER_RIGHT_BACK | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | — | — |
| 18 | GZ_LEG_UPPER_LEFT_FRONT | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | — | — |
| 19 | GZ_LEG_UPPER_LEFT_BACK | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | — | — |
| 20 | GZ_LEG_LOWER_RIGHT | — | — | — | `g_bone_anim1` (`to_gbolt`) | right calf | — | — |
| 21 | GZ_LEG_LOWER_LEFT | — | — | — | `g_bone_anim1` (`to_gbolt`) | left calf | — | — |
| 22 | GZ_FOOT_RIGHT | — | — | — | — | end | — | — |
| 23 | GZ_FOOT_LEFT | — | — | — | — | end | — | — |
| 24 | GZ_GUT_FRONT_EXTRA | — | — | — | — | end | — | — |
| 25 | GZ_GUT_BACK_EXTRA | — | — | — | — | end | — | — |

Sever groups (start → parts and bolts that move to the piece):

| zone(s) | segment | contents |
|---|---|---|
| GZ_CHEST_FRONT, GZ_CHEST_BACK | torso | `_chest`, bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_coatchest`, +head & both upper arms |
| GZ_SHLDR_RIGHT_FRONT | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_coatchest`, +head & both upper arms |
| GZ_SHLDR_RIGHT_BACK | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_coatchest`, +head & both upper arms |
| GZ_SHLDR_LEFT_FRONT | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_coatchest`, +head & both upper arms |
| GZ_SHLDR_LEFT_BACK | torso | `_coatchest`, +head & both upper arms |
| GZ_HEAD_FRONT, GZ_HEAD_BACK | head | `_headbald`, `_headcrew`, `_headbeard`, `_headhelmet`, `_lhairbang`, `_rhairbang`, `_hairmohawk`, `_hairponytail`, `_lear`, `_rear`, bolt `abolt_head_t` |
| GZ_ARM_UPPER_LEFT | left upper arm | `_lbicep`, `_lforearm`, `_lforearmcuff`, `_Cap_lforearmcuff_lhand`, `_lhand_open`, `_lhand_closed`, bolt `wbolt_hand_l` |
| GZ_ARM_LOWER_LEFT | left forearm | `_lforearm`, `_lforearmcuff`, `_Cap_lforearmcuff_lhand`, `_lhand_open`, `_lhand_closed`, bolt `wbolt_hand_l` |
| GZ_ARM_UPPER_RIGHT | right upper arm | `_rbicep`, `_rforearm`, `_rforearmcuff`, `_Cap_rforearmcuff_rhand`, `_rhand_open`, `_rhand_closed`, bolt `wbolt_hand_r` |
| GZ_ARM_LOWER_RIGHT | right forearm | `_rforearm`, `_rforearmcuff`, `_Cap_rforearmcuff_rhand`, `_rhand_open`, `_rhand_closed`, bolt `wbolt_hand_r` |
| GZ_LEG_LOWER_RIGHT | right calf | `_rcalf`, `_rcalfcuff`, `_cap_rcalfcuff_rfoot`, bolt `abolt_calf_r`, `_rfoot` |
| GZ_LEG_LOWER_LEFT | left calf | `_lcalf`, `_lcalfcuff`, `_cap_lcalfcuff_lfoot`, bolt `abolt_calf_l`, `_lfoot` |
| GZ_NECK, GZ_GUT_FRONT, GZ_GUT_BACK, GZ_GROIN, GZ_LEG_UPPER_RIGHT_FRONT, GZ_LEG_UPPER_RIGHT_BACK, GZ_LEG_UPPER_LEFT_FRONT, GZ_LEG_UPPER_LEFT_BACK, GZ_FOOT_RIGHT, GZ_FOOT_LEFT, GZ_GUT_FRONT_EXTRA, GZ_GUT_BACK_EXTRA | end | — |

Variant table (first visible mesh of each priority chain wins; columns give what that variant sets):

| visible mesh | zone: field = value |
|---|---|
| `_headbald` | HEAD_FRONT wound mesh = `gz_headbald_f`; HEAD_FRONT owner = `_headbald`; HEAD_BACK wound mesh = `gz_headbald_b`; HEAD_BACK owner = `_headbald` |
| `_headcrew` | HEAD_FRONT wound mesh = `gz_headcrew_f`; HEAD_FRONT owner = `_headcrew`; HEAD_BACK wound mesh = `gz_headcrew_b`; HEAD_BACK owner = `_headcrew` |
| `_headbeard` | HEAD_FRONT wound mesh = `gz_headbeard_f`; HEAD_FRONT owner = `_headbeard`; HEAD_BACK wound mesh = `gz_headbeard_b`; HEAD_BACK owner = `_headbeard` |
| `_headhelmet` | HEAD_FRONT wound mesh = `gz_headhelmet_f`; HEAD_FRONT owner = `_headhelmet`; HEAD_BACK owner = `_headhelmet` |
| `_chest` | HEAD_FRONT body cap = `_Cap_chest_head`; HEAD_BACK body cap = `_Cap_chest_head`; SHLDR_RIGHT_FRONT wound mesh = `gz_chest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_chest`; SHLDR_RIGHT_BACK wound mesh = `gz_chest_rbshldr`; SHLDR_RIGHT_BACK owner = `_chest`; SHLDR_LEFT_FRONT wound mesh = `gz_chest_lfshldr`; SHLDR_LEFT_FRONT owner = `_chest`; SHLDR_LEFT_BACK wound mesh = `gz_chest_lbshldr`; SHLDR_LEFT_BACK owner = `_chest`; CHEST_FRONT wound mesh = `gz_chest_f`; CHEST_FRONT owner = `_chest`; CHEST_FRONT body cap = `_Cap_hips_chest`; CHEST_FRONT piece cap = `_Cap_chest_hips`; CHEST_BACK wound mesh = `gz_chest_b`; CHEST_BACK owner = `_chest`; CHEST_BACK body cap = `_Cap_hips_chest`; CHEST_BACK piece cap = `_Cap_chest_hips`; ARM_UPPER_RIGHT body cap = `_Cap_chest_rbicep`; ARM_UPPER_LEFT body cap = `_Cap_chest_lbicep` |
| `_coatchest` | HEAD_FRONT body cap = `_Cap_coatchest_head`; HEAD_BACK body cap = `_Cap_coatchest_head`; SHLDR_RIGHT_FRONT wound mesh = `gz_coatchest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_coatchest`; SHLDR_RIGHT_BACK wound mesh = `gz_coatchest_rbshldr`; SHLDR_RIGHT_BACK owner = `_coatchest`; SHLDR_LEFT_FRONT wound mesh = `gz_coatchest_lfshldr`; SHLDR_LEFT_FRONT owner = `_coatchest`; SHLDR_LEFT_BACK wound mesh = `gz_coatchest_lbshldr`; SHLDR_LEFT_BACK owner = `_coatchest`; CHEST_FRONT wound mesh = `gz_coatchest_f`; CHEST_FRONT owner = `_coatchest`; CHEST_FRONT body cap = `_Cap_coathips_coatchest`; CHEST_FRONT piece cap = `_Cap_coatchest_coathips`; CHEST_BACK wound mesh = `gz_coatchest_b`; CHEST_BACK owner = `_coatchest`; CHEST_BACK body cap = `_Cap_coathips_coatchest`; CHEST_BACK piece cap = `_Cap_coatchest_coathips`; ARM_UPPER_RIGHT body cap = `_Cap_coatchest_rbicep`; ARM_UPPER_LEFT body cap = `_Cap_coatchest_lbicep` |
| `_rbicep` | ARM_UPPER_RIGHT wound mesh = `gz_rbicep`; ARM_UPPER_RIGHT owner = `_rbicep`; ARM_UPPER_RIGHT next zone of piece = GZ_ARM_LOWER_RIGHT |
| `_lbicep` | ARM_UPPER_LEFT wound mesh = `gz_lbicep`; ARM_UPPER_LEFT owner = `_lbicep`; ARM_UPPER_LEFT next zone of piece = GZ_ARM_LOWER_LEFT |
| `_lforearm` | ARM_LOWER_LEFT wound mesh = `gz_lforearm`; ARM_LOWER_LEFT owner = `_lforearm`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep` |
| `_lforearmcuff` | ARM_LOWER_LEFT wound mesh = `gz_lforearmcuff`; ARM_LOWER_LEFT owner = `_lforearmcuff`; ARM_LOWER_LEFT piece cap = `_Cap_lforearmcuff_lbicep` |
| `_rforearm` | ARM_LOWER_RIGHT wound mesh = `gz_rforearm`; ARM_LOWER_RIGHT owner = `_rforearm`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep` |
| `_rforearmcuff` | ARM_LOWER_RIGHT wound mesh = `gz_rforearmcuff`; ARM_LOWER_RIGHT owner = `_rforearmcuff`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearmcuff_rbicep` |
| `_coathips` | GUT_FRONT wound mesh = `gz_coathips_fgut`; GUT_FRONT owner = `_coathips`; GUT_BACK wound mesh = `gz_coathips_bgut`; GUT_BACK owner = `_coathips`; LEG_UPPER_RIGHT_FRONT wound mesh = `gz_coathips_rfthigh`; LEG_UPPER_RIGHT_FRONT owner = `_coathips`; LEG_UPPER_RIGHT_BACK wound mesh = `gz_coathips_rbthigh`; LEG_UPPER_RIGHT_BACK owner = `_coathips`; LEG_UPPER_LEFT_FRONT wound mesh = `gz_coathips_lfthigh`; LEG_UPPER_LEFT_FRONT owner = `_coathips`; LEG_UPPER_LEFT_BACK wound mesh = `gz_coathips_lbthigh`; LEG_UPPER_LEFT_BACK owner = `_coathips` |
| `_hips` | GUT_FRONT wound mesh = `gz_hips_fgut`; GUT_FRONT owner = `_hips`; GUT_BACK wound mesh = `gz_hips_bgut`; GUT_BACK owner = `_hips`; LEG_UPPER_RIGHT_FRONT body cap = `_Cap_hips_rthigh`; LEG_UPPER_RIGHT_FRONT piece cap bolt = `gbolt_Cap_rthigh_hips`; LEG_UPPER_RIGHT_FRONT body cap bolt = `gbolt_Cap_hips_rthigh`; LEG_UPPER_RIGHT_BACK body cap = `_Cap_hips_rthigh`; LEG_UPPER_RIGHT_BACK piece cap bolt = `gbolt_Cap_rthigh_hips`; LEG_UPPER_RIGHT_BACK body cap bolt = `gbolt_Cap_hips_rthigh`; LEG_UPPER_LEFT_FRONT body cap = `_Cap_hips_lthigh`; LEG_UPPER_LEFT_FRONT piece cap bolt = `gbolt_Cap_lthigh_hips`; LEG_UPPER_LEFT_FRONT body cap bolt = `gbolt_Cap_hips_lthigh`; LEG_UPPER_LEFT_BACK body cap = `_Cap_hips_lthigh`; LEG_UPPER_LEFT_BACK piece cap bolt = `gbolt_Cap_lthigh_hips`; LEG_UPPER_LEFT_BACK body cap bolt = `gbolt_Cap_hips_lthigh` |
| `_rthigh` | LEG_UPPER_RIGHT_FRONT wound mesh = `gz_rfthigh`; LEG_UPPER_RIGHT_FRONT owner = `_rthigh`; LEG_UPPER_RIGHT_FRONT piece cap = `_Cap_rthigh_hips`; LEG_UPPER_RIGHT_FRONT next zone of piece = GZ_LEG_UPPER_RIGHT_BACK; LEG_UPPER_RIGHT_FRONT sever group = `_rthigh`…; LEG_UPPER_RIGHT_BACK wound mesh = `gz_rbthigh`; LEG_UPPER_RIGHT_BACK owner = `_rthigh`; LEG_UPPER_RIGHT_BACK piece cap = `_Cap_rthigh_hips`; LEG_UPPER_RIGHT_BACK next zone of piece = GZ_LEG_UPPER_RIGHT_FRONT; LEG_UPPER_RIGHT_BACK sever group = `_rthigh`…; LEG_LOWER_RIGHT body cap = `_Cap_rthigh_rcalf`; LEG_LOWER_RIGHT piece cap bolt = `gbolt_Cap_rcalf_rthigh`; LEG_LOWER_RIGHT body cap bolt = `gbolt_Cap_rthigh_rcalf` |
| `_lthigh` | LEG_UPPER_LEFT_FRONT wound mesh = `gz_lfthigh`; LEG_UPPER_LEFT_FRONT owner = `_lthigh`; LEG_UPPER_LEFT_FRONT piece cap = `_Cap_lthigh_hips`; LEG_UPPER_LEFT_FRONT next zone of piece = GZ_LEG_UPPER_LEFT_BACK; LEG_UPPER_LEFT_FRONT sever group = `_lthigh`…; LEG_UPPER_LEFT_BACK wound mesh = `gz_lbthigh`; LEG_UPPER_LEFT_BACK owner = `_lthigh`; LEG_UPPER_LEFT_BACK piece cap = `_Cap_lthigh_hips`; LEG_UPPER_LEFT_BACK next zone of piece = GZ_LEG_UPPER_LEFT_FRONT; LEG_UPPER_LEFT_BACK sever group = `_lthigh`…; LEG_LOWER_LEFT body cap = `_Cap_lthigh_lcalf`; LEG_LOWER_LEFT piece cap bolt = `gbolt_Cap_lcalf_lthigh`; LEG_LOWER_LEFT body cap bolt = `gbolt_Cap_lthigh_lcalf` |
| `_lcalf` | LEG_LOWER_LEFT wound mesh = `gz_lcalf`; LEG_LOWER_LEFT owner = `_lcalf`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh` |
| `_lcalfcuff` | LEG_LOWER_LEFT wound mesh = `gz_lcalfcuff`; LEG_LOWER_LEFT owner = `_lcalfcuff`; LEG_LOWER_LEFT piece cap = `_Cap_lcalfcuff_lthigh` |
| `_rcalf` | LEG_LOWER_RIGHT wound mesh = `gz_rcalf`; LEG_LOWER_RIGHT owner = `_rcalf`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh` |
| `_rcalfcuff` | LEG_LOWER_RIGHT wound mesh = `gz_rcalfcuff`; LEG_LOWER_RIGHT owner = `_rcalfcuff`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalfcuff_rthigh` |

#### Female family (`enemy/female`)
The right/left upper-arm zones use the opposite side's cap meshes and cap bolts (original data).

| # | zone | gore bolt | cap bolt on severed piece | cap bolt on body | cap bolt-on model (its bolt) | sever group (segment) | next zone of piece | wound link |
|---|---|---|---|---|---|---|---|---|
| 0 | GZ_HEAD_FRONT | `gbolt_head_f` | `gbolt_Cap_neck_chest` | `gbolt_Cap_chest_neck` | `g_neck_torn_anim2` (`to_gbolt_Cap_chest_head`) | head | GZ_HEAD_BACK | — |
| 1 | GZ_HEAD_BACK | `gbolt_head_b` | `gbolt_Cap_neck_chest` | `gbolt_Cap_chest_neck` | `g_neck_torn_anim2` (`to_gbolt_Cap_chest_head`) | head | GZ_HEAD_FRONT | — |
| 2 | GZ_NECK | `gbolt_cap_head_neck` | — | — | — | neck | — | — |
| 3 | GZ_SHLDR_RIGHT_FRONT | `gbolt_shoulder_rf` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_SHLDR_LEFT_FRONT | — |
| 4 | GZ_SHLDR_RIGHT_BACK | `gbolt_shoulder_rb` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_SHLDR_LEFT_BACK | — |
| 5 | GZ_SHLDR_LEFT_FRONT | `gbolt_shoulder_lf` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_SHLDR_RIGHT_BACK | — |
| 6 | GZ_SHLDR_LEFT_BACK | `gbolt_shoulder_lb` | `gbolt_Cap_chest_hips_tight` | — | — | shoulder | GZ_CHEST_BACK | — |
| 7 | GZ_CHEST_FRONT | `gbolt_chest_f` | `gbolt_Cap_chest_hips_tight` | `gbolt_Cap_hips_chest_tight` | `g_guts_lrg` (`to_gbolt_Cap_chest_hips`) | torso | GZ_SHLDR_RIGHT_FRONT | — |
| 8 | GZ_CHEST_BACK | `gbolt_chest_b` | `gbolt_Cap_chest_hips_tight` | `gbolt_Cap_hips_chest_tight` | `g_guts_lrg` (`to_gbolt_Cap_chest_hips`) | torso | GZ_CHEST_FRONT | — |
| 9 | GZ_ARM_UPPER_RIGHT | — | `gbolt_Cap_lbicep_chest_tight` | `gbolt_Cap_chest_lbicep_tight` | `g_arm_torn_anim1` (`to_gbolt_Cap_chest_rbicep`) | right upper arm | GZ_ARM_LOWER_RIGHT | — |
| 10 | GZ_ARM_UPPER_LEFT | — | `gbolt_Cap_rbicep_chest_tight` | `gbolt_Cap_chest_rbicep_tight` | `g_arm_torn_anim1` (`to_gbolt_Cap_chest_lbicep`) | left upper arm | GZ_ARM_LOWER_LEFT | — |
| 11 | GZ_ARM_LOWER_RIGHT | — | `gbolt_Cap_rforearm_rbicep_tight` | `gbolt_Cap_rbicep_rforearm_tight` | `g_bone_anim1` (`to_gbolt`) | right forearm | — | — |
| 12 | GZ_ARM_LOWER_LEFT | — | `gbolt_Cap_lforearm_lbicep_tight` | `gbolt_Cap_lbicep_lforearm_tight` | `g_bone_anim1` (`to_gbolt`) | left forearm | — | — |
| 13 | GZ_GUT_FRONT | `gbolt_gut_f` | — | — | — | end | — | GZ_GUT_FRONT_EXTRA |
| 14 | GZ_GUT_BACK | `gbolt_gut_b` | — | — | — | end | — | GZ_GUT_BACK_EXTRA |
| 15 | GZ_GROIN | `gbolt_groin` | — | — | — | groin | — | GZ_GUT_FRONT |
| 16 | GZ_LEG_UPPER_RIGHT_FRONT | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | GZ_LEG_UPPER_RIGHT_BACK | GZ_LEG_UPPER_RIGHT_BACK |
| 17 | GZ_LEG_UPPER_RIGHT_BACK | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | GZ_LEG_UPPER_RIGHT_FRONT | GZ_LEG_UPPER_RIGHT_FRONT |
| 18 | GZ_LEG_UPPER_LEFT_FRONT | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | GZ_LEG_UPPER_LEFT_BACK | GZ_LEG_UPPER_LEFT_BACK |
| 19 | GZ_LEG_UPPER_LEFT_BACK | — | — | — | `g_bone_anim1` (`to_gbolt`) | end | GZ_LEG_UPPER_LEFT_FRONT | GZ_LEG_UPPER_LEFT_FRONT |
| 20 | GZ_LEG_LOWER_RIGHT | — | `gbolt_Cap_rcalf_rthigh_tight` | `gbolt_Cap_rthigh_rcalf_tight` | `g_bone_anim1` (`to_gbolt`) | right calf | — | — |
| 21 | GZ_LEG_LOWER_LEFT | — | `gbolt_Cap_lcalf_lthigh_tight` | `gbolt_Cap_lthigh_lcalf_tight` | `g_bone_anim1` (`to_gbolt`) | left calf | — | — |
| 22 | GZ_FOOT_RIGHT | `sbolt_heel_r` | — | — | — | right foot | — | GZ_LEG_LOWER_RIGHT |
| 23 | GZ_FOOT_LEFT | `sbolt_heel_l` | — | — | — | left foot | — | GZ_LEG_LOWER_LEFT |
| 24 | GZ_GUT_FRONT_EXTRA | — | — | — | — | end | — | — |
| 25 | GZ_GUT_BACK_EXTRA | — | — | — | — | end | — | — |

Sever groups (start → parts and bolts that move to the piece):

| zone(s) | segment | contents |
|---|---|---|
| GZ_CHEST_FRONT, GZ_CHEST_BACK | torso | `_chest`, bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_neck`, `_armorchest`, +head & both upper arms |
| GZ_SHLDR_RIGHT_FRONT | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_neck`, `_armorchest`, +head & both upper arms |
| GZ_SHLDR_RIGHT_BACK | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_neck`, `_armorchest`, +head & both upper arms |
| GZ_SHLDR_LEFT_FRONT | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_neck`, `_armorchest`, +head & both upper arms |
| GZ_SHLDR_LEFT_BACK | shoulder | bolt `abolt_shoulder_l`, bolt `abolt_shoulder_r`, bolt `abolt_back`, `_neck`, `_armorchest`, +head & both upper arms |
| GZ_NECK | neck | `_neck`, `_armorchest`, +head & both upper arms |
| GZ_HEAD_FRONT, GZ_HEAD_BACK | head | `_headcrew`, `_comface`, `_crewhair`, `_medmhairmhead`, `_lbang`, `_rbang`, `_lear`, `_rear`, bolt `abolt_head_t` |
| GZ_ARM_UPPER_LEFT | left upper arm | `_ltightbicep`, `_ltightforearm`, `_lopen`, `_lclosed`, bolt `wbolt_hand_l` |
| GZ_ARM_LOWER_LEFT | left forearm | `_ltightforearm`, `_lopen`, `_lclosed`, bolt `wbolt_hand_l` |
| GZ_ARM_UPPER_RIGHT | right upper arm | `_rtightbicep`, `_rtightforearm`, `_ropen`, `_rclosed`, bolt `wbolt_hand_r` |
| GZ_ARM_LOWER_RIGHT | right forearm | `_rtightforearm`, `_ropen`, `_rclosed`, bolt `wbolt_hand_r` |
| GZ_LEG_LOWER_RIGHT | right calf | `_rtightcalf`, bolt `abolt_calf_r`, `_rheels` |
| GZ_FOOT_RIGHT | right foot | `_rheels` |
| GZ_LEG_LOWER_LEFT | left calf | `_ltightcalf`, bolt `abolt_calf_l`, `_lheels` |
| GZ_FOOT_LEFT | left foot | `_lheels` |
| GZ_GROIN | groin | (groin marker) |
| GZ_GUT_FRONT, GZ_GUT_BACK, GZ_LEG_UPPER_RIGHT_FRONT, GZ_LEG_UPPER_RIGHT_BACK, GZ_LEG_UPPER_LEFT_FRONT, GZ_LEG_UPPER_LEFT_BACK, GZ_GUT_FRONT_EXTRA, GZ_GUT_BACK_EXTRA | end | — |

Variant table (first visible mesh of each priority chain wins; columns give what that variant sets):

| visible mesh | zone: field = value |
|---|---|
| `_crewhair` | HEAD_BACK wound mesh = `gz_crewhair_b`; HEAD_BACK owner = `_crewhair` |
| `_MEDMHAIRMHEAD` | HEAD_BACK wound mesh = `gz_MEDMHAIRMHEAD_b`; HEAD_BACK owner = `_MEDMHAIRMHEAD` |
| `_comface` | HEAD_FRONT wound mesh = `gz_comface`; HEAD_FRONT owner = `_comface` |
| `_neck` | HEAD_FRONT body cap = `_Cap_chest_neck`; HEAD_FRONT piece cap = `_Cap_neck_chest`; HEAD_BACK body cap = `_Cap_chest_neck`; HEAD_BACK piece cap = `_Cap_neck_chest`; NECK owner = `_neck` |
| `_tightchest` | SHLDR_RIGHT_FRONT wound mesh = `gz_tightchest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_tightchest`; SHLDR_RIGHT_BACK wound mesh = `gz_tightchest_rbshldr`; SHLDR_RIGHT_BACK owner = `_tightchest`; SHLDR_LEFT_FRONT wound mesh = `gz_tightchest_lfshldr`; SHLDR_LEFT_FRONT owner = `_tightchest`; SHLDR_LEFT_BACK wound mesh = `gz_tightchest_lbshldr`; SHLDR_LEFT_BACK owner = `_tightchest`; CHEST_FRONT wound mesh = `gz_tightchest_fgut`; CHEST_FRONT owner = `_tightchest`; CHEST_FRONT piece cap = `_Cap_chest_hips_tight`; CHEST_BACK wound mesh = `gz_tightchest_bgut`; CHEST_BACK owner = `_tightchest`; CHEST_BACK piece cap = `_Cap_chest_hips_tight`; ARM_UPPER_RIGHT body cap = `_Cap_chest_lbicep_tight`; ARM_UPPER_LEFT body cap = `_Cap_chest_rbicep_tight` |
| `_armorchest` | SHLDR_RIGHT_FRONT wound mesh = `gz_armorchest_rfshldr`; SHLDR_RIGHT_FRONT owner = `_armorchest`; SHLDR_RIGHT_BACK wound mesh = `gz_armorchest_rbshldr`; SHLDR_RIGHT_BACK owner = `_armorchest`; SHLDR_LEFT_FRONT wound mesh = `gz_armorchest_lfshldr`; SHLDR_LEFT_FRONT owner = `_armorchest`; SHLDR_LEFT_BACK wound mesh = `gz_armorchest_lbshldr`; SHLDR_LEFT_BACK owner = `_armorchest`; CHEST_FRONT wound mesh = `gz_armorchest_f`; CHEST_FRONT owner = `_armorchest`; CHEST_FRONT piece cap = `_Cap_chest_hips_tight`; CHEST_BACK wound mesh = `gz_armorchest_b`; CHEST_BACK owner = `_armorchest`; CHEST_BACK piece cap = `_Cap_chest_hips_tight` |
| `_rtightbicep` | ARM_UPPER_RIGHT wound mesh = `gz_rtightbicep`; ARM_UPPER_RIGHT owner = `_rtightbicep`; ARM_UPPER_RIGHT piece cap = `_Cap_lbicep_chest_tight`; ARM_LOWER_RIGHT body cap = `_Cap_lbicep_lforearm_tight` |
| `_ltightbicep` | ARM_UPPER_LEFT wound mesh = `gz_ltightbicep`; ARM_UPPER_LEFT owner = `_ltightbicep`; ARM_UPPER_LEFT piece cap = `_Cap_rbicep_chest_tight`; ARM_LOWER_LEFT body cap = `_Cap_rbicep_rforearm_tight` |
| `_ltightforearm` | ARM_LOWER_LEFT wound mesh = `gz_ltightforearm`; ARM_LOWER_LEFT owner = `_ltightforearm`; ARM_LOWER_LEFT piece cap = `_Cap_lforearm_lbicep_tight` |
| `_rtightforearm` | ARM_LOWER_RIGHT wound mesh = `gz_rtightforearm`; ARM_LOWER_RIGHT owner = `_rtightforearm`; ARM_LOWER_RIGHT piece cap = `_Cap_rforearm_rbicep_tight` |
| `_dresships` | GUT_FRONT wound mesh = `gz_dresships_fgut`; GUT_FRONT owner = `_dresships`; GUT_BACK wound mesh = `gz_dresships_bgut`; GUT_BACK owner = `_dresships`; LEG_UPPER_RIGHT_FRONT wound mesh = `gz_dresships_rfthigh`; LEG_UPPER_RIGHT_FRONT owner = `_dresships`; LEG_UPPER_RIGHT_BACK wound mesh = `gz_dresships_rbthigh`; LEG_UPPER_RIGHT_BACK owner = `_dresships`; LEG_UPPER_LEFT_FRONT wound mesh = `gz_dresships_lfthigh`; LEG_UPPER_LEFT_FRONT owner = `_dresships`; LEG_UPPER_LEFT_BACK wound mesh = `gz_dresships_lbthigh`; LEG_UPPER_LEFT_BACK owner = `_dresships` |
| `_tighthips` | GUT_FRONT wound mesh = `gz_tighthips_fgut`; GUT_FRONT owner = `_tighthips`; GUT_BACK wound mesh = `gz_tighthips_bgut`; GUT_BACK owner = `_tighthips`; LEG_UPPER_RIGHT_FRONT body cap = `_Cap_hips_rthigh_tight`; LEG_UPPER_RIGHT_FRONT piece cap bolt = `gbolt_Cap_rthigh_hips_tight`; LEG_UPPER_RIGHT_FRONT body cap bolt = `gbolt_Cap_hips_rthigh_tight`; LEG_UPPER_RIGHT_BACK body cap = `_Cap_hips_rthigh_tight`; LEG_UPPER_RIGHT_BACK piece cap bolt = `gbolt_Cap_rthigh_hips_tight`; LEG_UPPER_RIGHT_BACK body cap bolt = `gbolt_Cap_hips_rthigh_tight`; LEG_UPPER_LEFT_FRONT body cap = `_Cap_hips_lthigh_tight`; LEG_UPPER_LEFT_FRONT piece cap bolt = `gbolt_Cap_lthigh_hips_tight`; LEG_UPPER_LEFT_FRONT body cap bolt = `gbolt_Cap_hips_lthigh_tight`; LEG_UPPER_LEFT_BACK body cap = `_Cap_hips_lthigh_tight`; LEG_UPPER_LEFT_BACK piece cap bolt = `gbolt_Cap_lthigh_hips_tight`; LEG_UPPER_LEFT_BACK body cap bolt = `gbolt_Cap_hips_lthigh_tight` |
| `_rtightthigh` | LEG_UPPER_RIGHT_FRONT wound mesh = `gz_rtightthigh_f`; LEG_UPPER_RIGHT_FRONT owner = `_rtightthigh`; LEG_UPPER_RIGHT_FRONT piece cap = `_Cap_rthigh_hips_tight`; LEG_UPPER_RIGHT_FRONT sever group = `_rtightthigh`…; LEG_UPPER_RIGHT_BACK wound mesh = `gz_rtightthigh_b`; LEG_UPPER_RIGHT_BACK owner = `_rtightthigh`; LEG_UPPER_RIGHT_BACK piece cap = `_Cap_rthigh_hips_tight`; LEG_UPPER_RIGHT_BACK sever group = `_rtightthigh`…; LEG_LOWER_RIGHT body cap = `_Cap_rthigh_rcalf_tight` |
| `_ltightthigh` | LEG_UPPER_LEFT_FRONT wound mesh = `gz_ltightthigh_f`; LEG_UPPER_LEFT_FRONT owner = `_ltightthigh`; LEG_UPPER_LEFT_FRONT piece cap = `_Cap_lthigh_hips_tight`; LEG_UPPER_LEFT_FRONT sever group = `_ltightthigh`…; LEG_UPPER_LEFT_BACK wound mesh = `gz_ltightthigh_b`; LEG_UPPER_LEFT_BACK owner = `_ltightthigh`; LEG_UPPER_LEFT_BACK piece cap = `_Cap_lthigh_hips_tight`; LEG_UPPER_LEFT_BACK sever group = `_ltightthigh`…; LEG_LOWER_LEFT body cap = `_Cap_lthigh_lcalf_tight` |
| `_ltightcalf` | LEG_LOWER_LEFT wound mesh = `gz_ltightcalf`; LEG_LOWER_LEFT owner = `_ltightcalf`; LEG_LOWER_LEFT piece cap = `_Cap_lcalf_lthigh_tight` |
| `_rtightcalf` | LEG_LOWER_RIGHT wound mesh = `gz_rtightcalf`; LEG_LOWER_RIGHT owner = `_rtightcalf`; LEG_LOWER_RIGHT piece cap = `_Cap_rcalf_rthigh_tight` |
| `_lheels` | FOOT_LEFT owner = `_lheels` |
| `_rheels` | FOOT_RIGHT owner = `_rheels` |

### Dog zones (husky and rottweiler bodies, 10 zones)
Diagnostic output uses the names `GZ_DOG_LHIP`, `GZ_DOG_RHIP`, `GZ_DOG_HEAD`, `GZ_DOG_FRONT`, `GZ_DOG_RCHEST`,
`GZ_DOG_LCHEST` (an older 6-entry list that does not match the 10 zones; UNVERIFIED which name is printed for which
zone). The table names zones by meaning. Values: husky / rottweiler where they differ. No zone has a cap bolt-on model
or a piece-side cap bolt (the default `g_cap1` is used); no wound links.
| # | zone | wound mesh | owner part | gore bolt | body cap | piece cap | body-side cap bolt | sever group | next zone |
|---|---|---|---|---|---|---|---|---|---|
| 0 | head | `GZ_HEAD` | `_husky_torso` / `_rot_torso` | `gbolt_head` | — | — | — | — | — |
| 1 | left chest | `GZ_LCHEST` | torso | `gbolt_lchest` | — | — | — | — | — |
| 2 | right chest | `GZ_RCHEST` | torso | `gbolt_rchest` | — | — | — | — | — |
| 3 | front | `GZ_FRONT` | torso | `gbolt_front` | — | — | — | — | — |
| 4 | left front leg | — | `_husky_lleg_front` / `_rot_lleg_front` | — | `_CAP_HUSKY_TORSO_LSHOLDR` / `_CAP_ROT_TORSO_LSHOULDER` | `_CAP_HUSKY__LLEG_LSHOLDR` / `_CAP_ROT_LLEG_SHOULDER` | `GBOLT_CAP_HUSKY_TORSO_LSHOLDR` / `GBOLT_CAP_ROT_TORSO_LSHOULDER` | `_HUSKY_LLEG_FRONT` / `_ROT_LLEG_FRONT` | — |
| 5 | right front leg | — | `_husky_rleg_front` / `_rot_rleg_front` | — | `_CAP_HUSKY_TORSO_RSHOLDR` / `_CAP_ROT_TORSO_RSHOULDER` | `_CAP_HUSKY_RLEG_RSHOLDR` / `_CAP_ROT_RLEG_SHOULDER` | `GBOLT_CAP_HUSKY_TORSO_RSHOLDR` / `GBOLT_CAP_ROT_TORSO_RSHOULDER` | `_HUSKY_RLEG_FRONT` / `_ROT_RLEG_FRONT` | — |
| 6 | left hip | `GZ_LHIP` | `_husky_lhip` / `_rot_torso` | — | — | — | — | — | — |
| 7 | right hip | `GZ_RHIP` | `_husky_rhip` / `_rot_rleg_back` | — | `_CAP_HUSKY_TORSO_RHIP` / `_CAP_ROT_TORSO_RHIP` | `_CAP_HUSKY_RHIP_BODY` / `_CAP_ROT_RLEG_HIP` | `GBOLT_CAP_HUSKY_TORSO_RHIP` / `GBOLT_CAP_ROT_TORSO_RHIP` | husky: `_HUSKY_RHIP`, `_HUSKY_RLEG_BACK`; rottweiler: `_ROT_RLEG_BACK` | 9 |
| 8 | left back leg | — | `_husky_lleg_back` / `_rot_lleg_back` | — | `_CAP_HUSKY_LHIP_LLEG` / `_CAP_ROT_TORSO_LHIP` | `_CAP_HUSKY_LLEG_LHIP` / `_CAP_ROT_LLEG_HIP` | `GBOLT_CAP_HUSKY_LHIP_LLEG` / `GBOLT_CAP_ROT_TORSO_LHIP` | `_HUSKY_LLEG_BACK` / `_ROT_LLEG_BACK` | — |
| 9 | right back leg | — | `_husky_rleg_back` / `_rot_rleg_back` | — | as zone 7 | as zone 7 | as zone 7 | as zone 7 | 7 |
"torso" = `_husky_torso` / `_rot_torso`. Owner parts are only assigned when the named mesh is shown at setup.
Severable dog zones: 4, 5, 7, 8, 9 (zone 6 has no cap: the left hip cannot be severed).

### Cow zones (6 zones; owner part `_body_1` for all, assigned only when shown)
| # | zone (diagnostic name) | wound mesh | gore bolt |
|---|---|---|---|
| 0 | `GZ_HEAD` | `GZ_HEAD` | `gbolt_head` |
| 1 | `GZ_CHEST` | `GZ_CHEST` | `gbolt_chest` |
| 2 | `GZ_FRONT_R` | `GZ_FRONT_R` | `gbolt_front_r` |
| 3 | `GZ_FRONT_L` | `GZ_FRONT_L` | `gbolt_front_l` |
| 4 | `GZ_BACK_R` | `GZ_BACK_R` | `gbolt_back_r` |
| 5 | `GZ_BACK_L` | `GZ_BACK_L` | `gbolt_back_l` |
Cows have no caps and no sever groups.

## Test vectors / how to verify
1. Wound growth (needs `lock_gorezones 0`): shoot a meso soldier (health 100) once in the chest with a 20-point
   weapon → chest front wound mesh visible with a `gz_sing*` skin; keep shooting the same spot until 101+ points have
   accumulated → skin changes to `gz_multi*` with the same suffix number; > 200 → `gz_mass*`. With
   `lock_gorezones 1` no wound is ever visible.
2. Accumulation is capped by health: a 500-point hit on a 100-health character adds 100 to the zone (level 1).
3. Severing by bullets: on a corpse, keep hitting the left forearm with a pistol until more than 500 points have
   accumulated in that zone (each hit ≥ 5); the next hit throws the forearm and hand (with the weapon dropped first),
   the body shows the elbow cap with skin `c_blood` and a `g_bone_anim1` bone. With `lock_sever 1` nothing is severed.
4. Head explosion: with `ai_goretest 1` kill a meso soldier (neck mesh `_neck` visible) with a head shot → head
   disappears, sound `impact/gore/headexp.wav`, flesh debris and `gore/headcloud`; the neck shows `_Cap_neck_head` and
   one of `g_neck_torn_anim2` (≈70 %) / `g_half_head` (≈30 %, wearing the character's head skin).
5. Brain shot: kill a meso soldier wearing `_helmethead` from the front with a sniper head shot (> 80 damage) that
   exits the back of the head → head changes to `_headhelmet` and `g_brain` appears at the exit; a bearded head never
   does this.
6. Gib: a grenade killing three soldiers at once shows `gore/hugeburst2` on at most three of them, at most two flying
   limbs per body and no flying limbs from the third body onward; with `lock_sever 1` no body is blown apart.
7. Microwave gun kill that gibs: body tinted orange-red (1, 0.3, 0) and `gore/mpgsplatter` instead of
   `gore/hugeburst2`; always a full gib.
8. Thrown knife: throw a knife into a soldier's thigh, kill him → a knife pickup appears where the knife was.
9. Hat: shoot the `acc_hat_beret` of a meso character wearing `_helmethead` → hat drops, head switches to
   `_baldhead`; shooting `acc_helmet` absorbs the bullet unless the attacker uses the sniper rifle.
10. Savegame: save after wounding; the `ZGIA` chunk of the body holds 26 integers equal to the zones' accumulated
    damage; after loading, further hits continue from those levels.
11. Dog: kill a husky with a shotgun blast flagged 0x20000 on a front leg → the leg flies off with
    `_CAP_HUSKY__LLEG_LSHOLDR` on it.

## Open questions
- Damage flags are named in SPEC-720: 0x200 PROJECTILE, 0x800 MICROWAVE, 0x1000 FIRE, 0x8000 EXPLOSIVE,
  0x20000 SPREAD, 0x100000 TORSO_BLAST; which weapons set them is listed there.
- Severed piece physics parameters (0.5, 1.0, 0.1, 0.7, 0.5) and movetype 11: names to be fixed with the physics
  spec (WP7). The clip mask value 0x4010003 likewise.
- Lifetime of severed pieces: managed by the class-13 limb AI object (WP1/SPEC-701 family); not covered here.
- Player-victim weapon knock-out (rule 11.6): the game-rules query and the 0.2 probability branch are UNVERIFIED;
  test in a deathmatch game with realistic rules.
- If a zone's current skin is not one of its old level's four skins (possible after loading a savegame) the
  original reads the variant index 4, i.e. the next level's first skin (or memory past the table for level 3).
  Reimplementations may clamp; UNVERIFIED visually.
- Cow zones have no damage scale set (SPEC-720 decides how cow damage is computed).
- The female family table assigns left-arm caps/bolts to the right-arm zones and vice versa (as in the original
  data); verify on `fem_*` models which side actually loses the arm.
