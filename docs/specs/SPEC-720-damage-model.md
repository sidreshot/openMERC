# SPEC-720: Damage model (damage application, hit location, armour, knockback, means of death)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-001 (game imports: traces, sounds, effects, cvars, random numbers), SPEC-003 (player module:
inventory armour methods, weapon ids), SPEC-010/SPEC-011 (Ghoul instance trace method 43 and its 36-byte hit record,
bolt matrices, part visibility), SPEC-201/SPEC-401 (effect messages, blood/debris temp entities), SPEC-721 (gore zones,
wounds, severing, gibs, bolt-on knock-off, violence locks), SPEC-722 (character bodies, pain and death reactions),
SPEC-782 (weapons and projectiles: which attacks call the damage function with which values), SPEC-302 (cvars).

## Purpose

Every source of harm in Soldier of Fortune (bullets, knives, explosions, fire, falling, crushing movers, triggers)
ends in one damage function of the game module. This spec describes that function and everything it decides: who
can be hurt, team and difficulty rules, armour, the knockback push, player feedback, death and pain dispatch, and the
character-specific part — resolving where on a character model a shot landed ("gore zone"), the per-zone damage
multipliers, zone armour and the special rules of each body family. Weapon base damage values are not here (they are
set by the attacks, SPEC-782 and SPEC-003); this spec takes the damage value an attack passes in and gives the health
that is actually removed.

## Interface / layout

### The damage call (12 values)
| # | Value | Meaning |
|---|---|---|
| 1 | target | entity that is hurt |
| 2 | inflictor | entity that caused the damage (projectile, mover, the attacker itself) |
| 3 | attacker | entity credited with the damage (may be none) |
| 4 | direction | direction of the damage (normalised by the function) |
| 5 | point | world point of impact |
| 6 | origin | second point: the shot start for traced weapons, the explosion centre for area damage |
| 7 | damage | integer amount |
| 8 | knockback | integer push strength |
| 9 | damage flags | bit set, table below |
| 10 | means of death | integer, table below; the function stores it as the "last means of death" read by the obituary |
| 11 | armour-piercing | float; non-zero: armour is used up at twice the rate (player armour, SPEC-003 inventory method 31, and character zone armour) |
| 12 | armour bypass | float in [0, 1]: fraction of the damage that ignores character zone armour; also passed to the player armour method |

Area damage has three variants (Behaviour 14). The same 12 values are handed to a character's AI object and from
there to its body (SPEC-722) which returns the damage that is finally subtracted.

### Means of death
Bit 0x8000000 is added when the damage is team damage that was not blocked (Behaviour 2); the obituary ignores it.
Message texts are entries of string package `dm_generic` (ID 0; SPEC-131); "self" messages are used when attacker =
target, gendered pairs use the female text when the player's body class is the female one (SPEC-722 code 8).

| Value | Meaning | Obituary when killed by another player | When self-inflicted |
|---|---|---|---|
| 0 | unspecified | "died" | "killed himself/herself" |
| 1 | knife slash | TEXT_OBT_MOD_KNIFE_SLASH | idem (generic) |
| 2 | thrown knife | TEXT_OBT_MOD_KNIFE_THROWN | generic |
| 3 | pistol 1 | TEXT_OBT_MOD_PISTOL1 | generic |
| 4 | pistol 2 | TEXT_OBT_MOD_PISTOL2 | generic |
| 5 | machine pistol | TEXT_OBT_MOD_MPISTOL | generic |
| 6 | assault rifle | TEXT_OBT_MOD_ASSAULTRIFLE | generic |
| 7 | sniper rifle | TEXT_OBT_MOD_SNIPERRIFLE | generic |
| 8 | slug thrower (auto shotgun) | TEXT_OBT_MOD_AUTOSHOTGUN | generic |
| 9 | shotgun | TEXT_OBT_MOD_SHOTGUN | generic |
| 10 | machine gun | TEXT_OBT_MOD_MACHINEGUN | generic |
| 11 | phosphorus grenade | TEXT_OBT_MOD_PHOS_GRENADE_2 | TEXT_OBT_MOD_PHOS_GRENADE |
| 12 | rocket (direct) | TEXT_OBT_MOD_ROCKET | generic |
| 13 | rocket splash | TEXT_OBT_MOD_ROCKET_SPLASH | TEXT_OBT_MOD_ROCKET_SPLASH_F / _M |
| 14 | microwave gun (MPG) | TEXT_OBT_MOD_MPG | generic |
| 15 | flame gun | TEXT_OBT_MOD_FLAMEGUN | generic |
| 16 | flame gun napalm | TEXT_OBT_MOD_FLAMEGUN_NAPALM | generic |
| 17 | C4 | TEXT_OBT_MOD_C4 | TEXT_OBT_MOD_C4_F / _M |
| 18 | claymore | TEXT_OBT_MOD_CLAYMORE_2 | TEXT_OBT_MOD_CLAYMORE |
| 19 | neural grenade | TEXT_OBT_MOD_NEURAL_GRENADE | TEXT_OBT_MOD_NEURAL_GRENADE_F / _M |
| 20 | water (drowning) | TEXT_OBT_MOD_WATER (always) | — |
| 21 | slime | TEXT_OBT_MOD_SLIME (always) | — |
| 22 | crushed (movers, blocked doors/plats/trains, kill boxes) | TEXT_OBT_MOD_CRUSH (always) | — |
| 23 | telefrag (spawn kill box) | TEXT_OBT_MOD_TELEFRAG | generic |
| 24 | falling | TEXT_OBT_MOD_FALLING (always) | — |
| 25 | suicide (`kill` command, kill-all-monsters cheat) | TEXT_OBT_MOD_SUICIDE (always) | — |
| 26 | explosive (map explosions, explosive barrels, mines) | TEXT_OBT_MOD_EXPLOSIVE (always) | — |
| 27 | fire (burning, flaming barrels, fire bombs) | TEXT_OBT_MOD_FIRE (always) | — |
| 28 | lava | TEXT_OBT_MOD_LAVA (always) | — |
| 29 | exit (level-change trigger) | TEXT_OBT_MOD_EXIT (always) | — |
| 30 | barbed wire | TEXT_OBT_MOD_BARBWIRE (always) | — |
| 31 | dog bite | TEXT_OBT_MOD_DOGBITE (always) | — |
| 32 | hurt trigger / line trap | TEXT_OBT_MOD_FIRE (always) | — |
| 34 | laser (target_laser) | "died" | generic |
| 35 | thrown star (character throwing weapon, SPEC-782) | "died" | generic |
| 36 | grenade | TEXT_OBT_MOD_GRENADE_2 | TEXT_OBT_MOD_GRENADE_F / _M |
| 37 | concussion grenade | TEXT_OBT_MOD_CONC_GRENADE | generic |

"always": the message is printed whoever the attacker is. "died": TEXT_OBT_DIED. "generic": TEXT_OBT_M / TEXT_OBT_F
("killed himself / herself"). A killer that is not a player also gives "died". Value 33 is never produced by the game
module (UNVERIFIED for player.dll).

### Damage flags
| Bit | Name used in these specs | Effect |
|---|---|---|
| 0x1 | AREA | set by every area-damage variant; characters do not get the unaware double damage (Behaviour 9) |
| 0x2 | NO_ARMOR | player armour (inventory) is not used |
| 0x8 | NO_KNOCKBACK | no push |
| 0x20 | NO_PROTECTION | ignores god mode, spawn protection and blocked team damage |
| 0x40 | TEAM_BLOCKED | set by the damage function when team damage is blocked (Behaviour 2) |
| 0x80 | PUSH_ONLY | area damage does only 5 % damage with full knockback; changes the vertical part of the push (Behaviour 8) |
| 0x100 | TRACED | a traced hit (bullet, knife, pellet, bite): the body resolves the hit location from the shot's Ghoul hit list |
| 0x200 | PROJECTILE | traced projectile: like TRACED; bodies with gore ignore it when the shot has no Ghoul hits; exit-wound spray (SPEC-721) |
| 0x400 | — | passed by mine/spike touches; no effect found in the damage model (UNVERIFIED) |
| 0x800 | MICROWAVE | microwave-gun burst; clears a body state flag (UNVERIFIED meaning) and selects the microwave gib (SPEC-721) |
| 0x1000 | FIRE | burning; no hit direction for players; a gibbing blow sets the body on fire (Behaviour 11) |
| 0x2000 | FALL | falling; player hit direction "below"; characters say their "hit" line |
| 0x4000 | DROWN | drowning / slime; no hit direction |
| 0x8000 | EXPLOSIVE | explosion or heavy impact: zone armour and zone multipliers are skipped; gib threshold −50; severing allowed (SPEC-721) |
| 0x20000 | SPREAD | the hit is spread over neighbouring zones (Behaviour 10.4); guts and severing allowed (SPEC-721) |
| 0x100000 | TORSO_BLAST | zones taken from the torso by facing (Behaviour 10.6); no stump blood (SPEC-721) |
| 0x200000 | — | microwave ray (MPG primary, SPEC-782); no effect found in the damage model (UNVERIFIED) |
| 0x400000 | SLASH | melee slash: back-stab multiplier, slash zone rules (Behaviour 11.2) |
| 0x800000 | — | no damage to a body wearing armour configuration 3 (Behaviour 11.1); no game-module caller (player.dll UNVERIFIED) |
| 0x1000000 | THROWN | thrown blade; a killing blow clears the same body state flag as MICROWAVE (UNVERIFIED meaning) |
| 0x2000000 | — | used by character melee/emitter attacks; no effect found in the damage model (UNVERIFIED) |

### Shot hit list
For traced attacks the game keeps one hit list per shot: up to **20** Ghoul hit records (SPEC-011, 36 bytes,
sorted by distance) of the entity being damaged, plus the shot start and unit direction expressed in that entity's
model space (entity origin and angles, SPEC-011 "traces"). It is filled
- by the trace filter that weapons pass to the engine trace (SPEC-782): when a trace touches an entity that has a
  Ghoul instance, the ray is transformed into the entity's space and instance method 43 is run at the current time
  with at most 20 records. **No records → the trace ignores that entity** (the shot passes between limbs). With
  records, the trace result is moved to the first record: fraction = distance of the first hit ÷ ray length (only
  when the ray is longer than 0.01; capped at 0.99), end point = first hit point, plane normal = first hit normal;
- by the player damage path (Behaviour 13) for a TRACED or PROJECTILE hit when the list is empty;
- by the thrown-knife rule (Behaviour 10.3).
A hit with an empty list counts as "no Ghoul hits".

### Gore-zone data used here
Each zone of a body (SPEC-721 "Gore zone") has, among the fields described in SPEC-721: its wound mesh part, its owner
(body) part, its gore bolt, its body-side cap mesh, a linked zone, an integer **armour/accumulated damage** value (a
negative value is remaining armour; wounds accumulate once it is ≥ 0) and a float **damage scale**.

### Human zones (26) and damage scales
| Index | Zone | Scale | Index | Zone | Scale |
|---|---|---|---|---|---|
| 0 | GZ_HEAD_FRONT | 10 | 13 | GZ_GUT_FRONT | 1 |
| 1 | GZ_HEAD_BACK | 10 | 14 | GZ_GUT_BACK | 1 |
| 2 | GZ_NECK | 10 | 15 | GZ_GROIN | 1 |
| 3 | GZ_SHLDR_RIGHT_FRONT | 1 | 16 | GZ_LEG_UPPER_RIGHT_FRONT | 0.5 |
| 4 | GZ_SHLDR_RIGHT_BACK | 1 | 17 | GZ_LEG_UPPER_RIGHT_BACK | 0.5 |
| 5 | GZ_SHLDR_LEFT_FRONT | 1 | 18 | GZ_LEG_UPPER_LEFT_FRONT | 0.5 |
| 6 | GZ_SHLDR_LEFT_BACK | 1 | 19 | GZ_LEG_UPPER_LEFT_BACK | 0.5 |
| 7 | GZ_CHEST_FRONT | 1 | 20 | GZ_LEG_LOWER_RIGHT | 0.5 |
| 8 | GZ_CHEST_BACK | 1 | 21 | GZ_LEG_LOWER_LEFT | 0.5 |
| 9 | GZ_ARM_UPPER_RIGHT | 0.5 | 22 | GZ_FOOT_RIGHT | 0.5 |
| 10 | GZ_ARM_UPPER_LEFT | 0.5 | 23 | GZ_FOOT_LEFT | 0.5 |
| 11 | GZ_ARM_LOWER_RIGHT | 0.5 | 24 | GZ_GUT_FRONT_EXTRA | 1 |
| 12 | GZ_ARM_LOWER_LEFT | 0.5 | 25 | GZ_GUT_BACK_EXTRA | 1 |
The scales are set when the body is set up (all human body families: ecto, meso, female and the armoured boss body);
a body with fewer than 26 zones prints `couldn't setup gzone damage scales!!!` and keeps its scales. "No zone" is
written as index 26 in hints.

### Dog zones (10) and scales
Index 0 head (GZ_HEAD) 10; 1 GZ_LCHEST, 2 GZ_RCHEST, 3 GZ_FRONT, 4 left front leg, 5 right front leg, 6 GZ_LHIP,
7 GZ_RHIP, 8 left back leg, 9 right back leg: 1. (Zone contents: SPEC-721.) A dog body with fewer than 10 zones
prints the same message. Cow bodies (6 zones) have no damage scales and do not scale damage.

### Character zone armour (armour configurations)
A character's body may be given an armour configuration 0–7 by its setup (SPEC-722); players never get one, and only
bodies with 26 zones accept it. Let A = 300 armour points; for the armoured boss body (body class 13, used by
`m_x_mraiderboss1` / `m_x_mraiderboss2`) A depends on `sk_toughness`: 0 → 75, 1 → 99, 2 → 150, 3 → 150, 4 → 300.
"Add A to zones Z" means: each zone's armour/accumulated value is decreased by A (armour is stored negative and adds up).

| Config | Effect |
|---|---|
| 0 | all 26 zones' armour/accumulated values set to 0 |
| 1 (vest) | A on zones 3–8, 13, 14 |
| 2 (body suit) | A on zones 9–12, 15–25, then as 1 |
| 3 (full, heavy) | A × 4 on zones 0–2, then as 2 (with A × 4) |
| 4 | health and maximum health × 1.5 (truncated); no armour |
| 5 | health and maximum health × 2; no armour |
| 6 | as 2 with trunc(A × 0.3) |
| 7 | as 3 with plain A |
Larger values are ignored. Stripping armour (SPEC-722) sets the armoured zones of the current configuration back to 0
and the configuration to 0. The "remaining armour" of a body is the sum of the remaining armour of the zones of its
configuration (head zones only count for 3 and 7).

### Difficulty ("toughness") table
`sk_toughness` (0–4, clamped) selects the factor by which single-player damage to players is multiplied:

| sk_toughness | 0 | 1 | 2 | 3 | 4 |
|---|---|---|---|---|---|
| player damage factor t | 0.1 | 0.2 | 0.3 | 0.5 | 1.0 |
It also scales the limb-wound duration (Behaviour 11.6) and the armoured boss armour (above).

## Behaviour

### 1. Who can be damaged
1. A target that cannot take damage takes nothing. A target whose "takes damage" mode is 3 takes nothing while the
   first player is watching a scripted remote camera (camera mode 4, SPEC-780/cinematics).
2. An entity flagged as a sub-part of another entity passes the damage to that owner; without an owner nothing
   happens.
3. Cvar `nodamage` (default "0"): when the attacker is a character and the target is a player, the damage becomes
   trunc(1/t + 1) for `nodamage` 1 (so that after the toughness factor of rule 3.1 the player loses 1 point, 2 at
   toughness 4) and trunc(damage × 0.5) otherwise. With `nodamage` 2, damage from characters to players and to other
   characters is 0 (pain and push still happen).
4. Characters (entities with an AI object) are handed to the character rules (Behaviour 9) after rule 3; the two
   end-boss classnames `m_x_mskinboss` and `m_x_mraiderboss1` take no damage at all unless their `count` key is 100
   (set by scripts when they may be killed).

### 2. Team damage (deathmatch)
When target ≠ attacker, the game is a deathmatch, team play is on (dmflags 0x200; always on in capture-the-flag) and
both are on the same team: with "no friendly fire" (dmflags 0x400) the damage becomes 0 and flag TEAM_BLOCKED is set;
otherwise the means of death gets bit 0x8000000.

### 3. Players: toughness, protection, armour
1. Single player only: a player's damage becomes trunc(t × damage); a positive damage never drops below 1.
2. The direction is normalised. Non-player, non-character targets get the general push (Behaviour 8.1).
3. God mode (cheat `heretic`) makes the damage 0 unless NO_PROTECTION. For players also: damage is 0 (unless
   NO_PROTECTION) while the player's spawn protection time has not passed, or when the hit carries TEAM_BLOCKED and the
   attacker is on the same team.
4. Otherwise the player damage path runs (Behaviour 13) and returns the damage that remains; the difference is added
   to the player's "armour absorbed" counter of the frame.

### 4. Player feedback (same frame)
For a player target:
- damage > 0: adds damage to the frame's blood counter and knockback to the frame's knockback counter, records the
  damage point; sends a blood splat behind the point (SPEC-401, size 200, heavy variant with probability 1/6) unless
  means of death is 27 (fire); applies the player push (Behaviour 8.2).
- damage ≤ 0 (not in god mode): adds the knockback, records the point and applies the push.
- when health is reduced: an effect is played on the player by the low bits of the means of death m — m & 3 ≠ 0:
  `lighthit`; else m & 15 ≠ 0: `gotshot`; else m & 63 ≠ 0 and damage > 5: `exphit` and `land`.
- for a player that survives the hit (health > 0, or damage 0): hit direction (stored in the player state for the HUD damage indicator, UNVERIFIED field): with any of FIRE, FALL,
  DROWN set: 2 for FALL, else 0x10; otherwise, with an attacker, let a = (yaw towards the attacker − player yaw)
  in degrees, both reduced to 0..360 in 1/65536 turns: a < 30, a > 340 or 150 < a < 210 → 0x10 (front/back);
  30 ≤ a ≤ 150 → 8; 210 ≤ a ≤ 340 → 4. In deathmatch the game mode is told who hit whom.
- if damage > 10 or the player's health is below 25, the music switches to the "danger" state (state 3) when it was in
  state 0, 2 or 3, and the time is recorded.
- a surviving player hit with damage point (0,0,0) prints `Error: T_Damage - NULL location passed` and gets no hit
  direction and no music change.

### 5. Health, death and pain (non-character targets and players)
1. When damage ≠ 0 health is reduced by it and the attacker is remembered as the target's last attacker if none is
   remembered yet.
2. Health ≤ 0: players get the "no knockback" flag; an entity with a respawn template is re-created after its respawn
   delay; health below −999 is set to −999; the attacker becomes the target's enemy; players lose their armour and
   the weapons in their hands in deathmatch (Behaviour 13); the entity's die callback receives (inflictor, attacker,
   damage, point).
3. Otherwise non-player entities get their pain callback with (attacker, knockback as float, damage, point).

### 6. Character death and pain (AI objects)
See Behaviour 9. A character's health is clamped to −999, it is flagged "no knockback", kill counters, `killtarget`
and `killfacing` keys are processed (SPEC-700), and the body chooses the death reaction (SPEC-722).

### 7. Characters hitting the player in single player
Damage from a character to the player is first halved (rule 1.3), then multiplied by t (rule 3.1). Example: a 20
point bullet at toughness 2 (t = 0.3) removes trunc(10 × 0.3) = 3.

### 8. Knockback
1. **General entities**: only if the knockback is non-zero, NO_KNOCKBACK is not set, the target is not flagged "no
   knockback" and moves (not static, pushers, stop-pushers or the bounce-less "none" movetypes). m = mass, at least 50.
   v = (dir.x, dir.y, dir.z × 0.25), or dir.z × 0.1 with PUSH_ONLY; normalised; velocity += v × 250 × knockback / m.
2. **Players** (same preconditions, mass not floored): with an attacker and without PUSH_ONLY the push points from
   the attacker to the damage point: v = normalise(point − attacker origin) (sign: away from the attacker),
   k = 250 × knockback / mass; velocity += (v.x·k, v.y·k, v.z·k·0.25) and the same amount is added to the player's
   separate push accumulator. Otherwise v = normalise(dir.x, dir.y, dir.z × 0.1), k = 1000 × knockback / mass,
   velocity += v × k, vertical velocity capped at 200, and the player's "pushed" timer is set to
   now + 0.5 + 0.75 × knockback / mass.
3. **Characters**: preconditions as 8.1 (also not for noclip); m = max(mass, 50); v = (dir.x, dir.y, dir.z × 0.2) with
   PUSH_ONLY else (dir.x, dir.y, dir.z + 0.3), normalised; velocity += v × 500 × knockback / m.

### 9. Character (AI) damage rules
1. Means of death 23 (telefrag) does nothing to characters. A player hitting a character of his own team makes it
   remember the betrayal (it may turn hostile, SPEC-700).
2. Unaware characters: when the hit is not AREA, the attacker is a player, the character has no enemy and is alive,
   the damage is doubled ("sneak attack").
3. A character flagged "no knockback" gets knockback 0. The character registers the attacker's position as an
   alert at the current time (SPEC-701, UNVERIFIED).
4. If the character has a body and damage ≠ 0, the body computes the final damage (Behaviour 10–12) with all 12
   values; the AI object records that it was hit.
5. Push: Behaviour 8.3 with the knockback of rule 3.
6. If the final damage ≠ 0, the attacker is a player and the character is alive: a protected hostage prints
   `HOSTAGE_HIT` ("Check your fire; you're here to save these people!", singleplr package ID 6, index 0) and an innocent
   civilian prints `INNOCENT_HIT` (index 14, "Check your fire!") to that player. Innocents: `m_irq_fwoman1`,
   `m_irq_fwoman2`, `m_sib_mmechanic`, `m_tok_mman2`, `m_kos_mmechanic`, `m_irq_mman2`, `m_irq_moilworker`,
   `m_sib_escientist1`, `m_irq_eman1`, `m_nyc_ebum`. Buddies hit by a player print `BUDDY_HIT` (index 1) from the pain
   reaction (UNVERIFIED which characters count as buddies). Health is then reduced.
7. Health ≤ 0: the character dies (rule 6, SPEC-722) and the call reports "hurt". Otherwise, unless the body wears
   armour configuration 3 and the damage is 0, the pain reaction runs (attacker, point, knockback, damage; SPEC-722).

### 10. Hit location on human bodies
Human bodies (ecto, meso, female, armoured boss; also deathmatch players, Behaviour 13) turn one hit into a list of
**hit zones** (at most a few) and possibly an **exit zone**. The method depends on the flags:

| Condition (first match) | Method |
|---|---|
| SPREAD set and PROJECTILE not set | 10.4 spread (or 10.5 when there is no hint) |
| SLASH set and the shot has no Ghoul hits | 10.4/10.5, then slash rule 11.2 |
| EXPLOSIVE set | 10.6 if TORSO_BLAST set, else no zones |
| TORSO_BLAST set | 10.6 |
| otherwise (incl. thrown knives) | 10.2 traced |

Hits without any of TRACED, PROJECTILE, EXPLOSIVE, SPREAD, TORSO_BLAST (and not a thrown knife) skip hit location
and the body returns the damage unchanged (Behaviour 11.1).

1. **Zone of a hit record**: among the zones whose owner part is the part of the record, a zone without gore bolt
   wins by smallest distance; zones with a gore bolt are compared by the squared distance from the record's hit
   point to the bolt position (instance method 50 at the current time); the nearest wins. For thrown knives in
   deathmatch the distances of later candidates are weighted × 1.3 unless the current best is a head zone (UNVERIFIED).
2. **Traced** (shot hit list): only records on the character's own instance with a non-zero part are body records;
   records on other instances are its bolt-ons.
   - Exit: the farthest such record (scanning from the end) that maps to a zone gives the exit zone and the exit point
     = shot start + direction × that record's distance (world space). Thrown knives have no exit.
   - Bolt-on first: if a bolt-on record lies in front of every body record, the bolt-on was hit first:
     a) the fuel-tank accessory (`acc_tanks`) explodes (SPEC-721) and the hit continues;
     b) for a weapon held in a hand, when not in deathmatch and the character is armoured above half health (or in a
        particular AI state, UNVERIFIED), the hit continues with **no zone** (damage unscaled); damage below 4 does
        nothing in that case;
     c) otherwise the bolt-on knock-off rule (SPEC-721 rule 11) decides how much damage passes; if a hand bolt-on is
        lost the body records "disarmed" for that hand.
     If a bolt-on was hit first and no body zone is found before the exit record, the hit does nothing.
   - Entry: the first body record before the exit record that maps to a zone; its point (shot start + direction ×
     distance), clamped into the target's bounding box, becomes the **blood point**; that zone is the single hit zone.
   - No Ghoul hits at all and no forced zone: the hit does nothing.
   - Players only: if the hit zone is head front/back and its owner part is `_MASKHEAD` (or a hood head), the masked head
     part is hidden, `_BALDHEAD` and `_COMFACE` shown, the shot is traced again and resolved, then the parts are
     restored and the zone's owner part re-pointed to the mask. A hit whose only record is on `_MOHAWK` does nothing.
3. **Thrown knife** (inflictor marked as a thrown knife and attacker is a player): a trace (mask 0x6000081) from the
   attacker's eye to eye + 2 × knife velocity fills the hit list. Without Ghoul hits the zone is the one whose gore bolt
   is nearest to the knife (SPEC-721); none → nothing. In deathmatch, with probability 1/2, zone 0 becomes 3, 1 becomes 4,
   and 2 becomes 7 or 8 (front or back by the target's facing). The hit is then treated as PROJECTILE.
4. **Spread** from a hint zone h (the last zone resolved on any human body; reset to "none" after use):

   | h | zones | h | zones |
   |---|---|---|---|
   | 0, 1 | 2 | 13, 24 | 3, 7, 15 |
   | 2 | 1 if hit from behind (dot > 0.707) else 0 | 14, 25 | 6, 8, 4 |
   | 3 | 5, 7, 9 | 15 | 15 |
   | 4 | 6, 8, 9 | 16 | 15, 20, 13 |
   | 5 | 3, 7, 10 | 17 | 15, 20, 14 |
   | 6 | 4, 8, 10 | 18 | 15, 21, 13 |
   | 7 | 3, 5, 13 | 19 | 15, 21, 14 |
   | 8 | 4, 6, 14 | 20 | 16, 22 (front) or 17, 22 (behind) |
   | 9 | 3, 11 | 21 | 18, 23 (front) or 19, 23 (behind) |
   | 10 | 5, 12 | 22 | 20 |
   | 11 | 9 | 23 | 21 |
   | 12 | 10 | | |
   "Behind" = the horizontal direction from the damage origin to the target is within 45° of the target's forward.
5. **Estimate** (no hint): by the impact height and side. Head chance n: 6 when the point is above the target's
   origin, 4 when it is less than 10 units below, else 10 (UNVERIFIED detail). Candidate sets (1–4 distinct zones are
   drawn at random from the set, in random order); then with probability 1/n a head zone (0 from the front, 1 from
   behind) is added:

   | Situation | exit | candidates |
   |---|---|---|
   | point below the origin, hit from behind (legs) | 16 | 17, 20, 21, 19 |
   | point below the origin, from the front (legs) | 17 | 16, 20, 21, 18 |
   | torso, from behind | 7 | 8, 6, 4, 14 |
   | torso, right side | 8 | 7, 9, 14, 4 |
   | torso, left side | 8 | 7, 10, 14, 6 |
   | torso, from the front | 8 | 7, 5, 3, 13 |
   A second estimator (used when the character is in certain postures, UNVERIFIED which) has no leg case and uses
   right side 7, 3, 16, 11, 4 / left side 7, 5, 18, 12 (up to 5 drawn) and the same front/behind sets.
6. **Torso blast**: from behind (dot > 0.707): zones 8, 6, 4, 14, exit 7; from the side: 7, then 9, 14, 4 (right) or
   10, 14, 6 (left), then 13, exit 8; from the front: 7, 5, 3, 13, exit 8.

### 11. Damage on human bodies
1. **Forced and blocked hits**: a scripted "kill" setting on the body makes the damage 9999 (variant 2 also adds
   SPREAD) and is cleared. With flag 0x800000 a body in armour configuration 3 takes nothing. Hits without location
   (see the table in 10) return the damage unchanged; with FALL the character says its "hit" line (volume 0.8) and,
   in realistic deathmatch, the player's impairment grows by 0.1 (maximum 0.5).
2. **Slash**: if the target faces away from the origin point (forward · normalise(target − origin) > 0.707) the damage
   becomes trunc(damage × 2.5) (back-stab). With estimated zones a slash never lands on zones 0–2 or 20–25: such a
   zone is replaced by 8.
3. **Players** (deathmatch bodies): unless NO_ARMOR, player armour absorbs through the inventory (SPEC-003 method 31,
   with values 11 and 12). With realistic damage (dmflags 0x40) the damage is split evenly over the hit zones and
   each share goes through the armour method only for armour-covered zones (UNVERIFIED: the zone test of the original
   is unreliable). If anything remained the target is flagged "hurt"; if armour absorbed something it is flagged "armour
   hit"; if nothing remains: sparks (`weakspark` when value 11 ≠ 0, else `armorhit` when value 12 ≠ 0, else `spark2`) at
   the blood point facing the shot, sound `impact/armor/hit.wav` (volume 0.6, attenuation 1), a thrown knife drops
   to the ground, and the hit ends with 0.
4. **Character zone armour** (non-players, not EXPLOSIVE): for every hit zone with armour (value < 0): the zone's
   wound mesh and that of its linked zone get skin `gz_armor` on material `gz` and are shown; the damage becomes
   trunc(d − d × bypass) (value 12); d' = that × 2 when value 11 > 0; if d' ≤ remaining armour: armour −= d', the
   pending limb-wound notification of the body is cleared, sparks and sound as in rule 3 at the blood point (only when
   the hit had a blood point), a thrown knife drops, and the hit ends with 0; otherwise the armour becomes 0 and
   damage = d' − armour (halved again when value 11 > 0).
5. **Hit effects**: a hit with a non-zero point sends a debris/blood puff (SPEC-401) at the point; a hit with a blood
   point sends a blood splat (size 200) behind it.
6. **Zone multiplier** (not EXPLOSIVE): with n hit zones, damage = trunc(Σ dᵢ / n) where, for a living target,
   dᵢ = trunc(damage × scaleᵢ) and, in realistic deathmatch for limb zones, dᵢ ≤ trunc(health × 0.33) (UNVERIFIED zone
   test); for a corpse (health ≤ 0) dᵢ = 3 for zones with a body-side cap mesh and 12 otherwise (only when damage > 0).
   No zones: the damage is unchanged.
7. **Overkill cap**: for a living target the damage is at most health + 150.
8. **Reactions**: the body notes a "hit" reaction for 10 s; a killing blow (health ≤ damage) notes it permanently and:
   - if the body drops its gear on death, the bolt-ons on `wbolt_hand_l`, `wbolt_hand_r` and `abolt_head_t` are
     knocked off (SPEC-721 rule 11, up to 10 per bolt) with the hit;
   - **gib**: (EXPLOSIVE and health − damage < −50) or health − damage < −750 → the body is gibbed (SPEC-721) and the
     per-explosion gib counter grows (reset by each area-damage call); with FIRE the remains burn for 2 s (not with
     `lock_deaths`, not for the two end bosses);
   - with THROWN the body state flag also cleared by MICROWAVE is cleared;
   - the entity's effect and render-effect bits are cleared (UNVERIFIED which state fields).
9. **Limb wounds**: hit zones 16, 17, 20, 22 mark the right leg, 18, 19, 21, 23 the left leg, 9, 11 the right arm,
   10, 12 the left arm as hurt until now + U(5, 7) × (4 − toughness) × 0.25 seconds (toughness clamped 0..4; at 4 the
   wound ends immediately); leg hits also add 0.1 impairment to a player in realistic deathmatch. Hurt limbs select
   limping/one-armed animations (SPEC-722).
10. **Wounds**: each hit zone is wounded in order (SPEC-721 rules 2–4) until one of them reports that the body was
    destroyed; the means of death is passed along and value **1 (knife slash) selects the `_k` wound skins**. In
    realistic deathmatch an armoured player gets wounds only on uncovered zones. Unless `lock_gorezones`, exit wounds
    and wound skins follow (SPEC-721 rule 5) with the exit zone.
11. The final damage is returned.

### 12. Other bodies
- **Dog**: PROJECTILE with no Ghoul hits → 0. Otherwise the damage is multiplied by the scale of every zone of its
  hit list in turn (trunc after each); gore per SPEC-721. A scripted kill setting gives 9999 (variant 2 adds SPREAD).
- **Cow**: PROJECTILE with no Ghoul hits → 0; otherwise the damage is unchanged (gore only).
- **Armoured boss body** (class 13): damage from itself is 0. A pending "trigger on first hit" flag fires the
  body's `killtarget` once. Damage per hit is capped at 10 (scripted kills excepted). Hits without location do 1
  (damage ≤ 0: unchanged; damage < 5: 0 with probability 1/2). Otherwise as human bodies (rules 10–11, every zone may
  be armoured). Its pain reaction runs at most once every 5 s.
- **Helicopter**: without Ghoul hits 0. Hits on two marked sub-models (pilot/gunner glass, UNVERIFIED) by a player
  holding the sniper rifle (weapon 6) count down a shot counter (when the body allows it); at 0 that crew position is
  marked killed. Other hits on those, and hits on a third marked sub-model, do full damage; hull hits do
  trunc(damage × 0.1). Sparks plus `Weapons/FX/Rics/ric<n>.wav` at most every 0.3 s, not for sniper hits on the crew.
- **Tank and snowcat**: without Ghoul hits 0; a player holding the knife (weapon 1) does 0; hits on the machine-gun
  bolt-on reduce its own health, at ≤ 0 it is disabled and `environ/machgun_smoke2` plays on `to_bolt_machine_gun`;
  every hit does full damage; sparks at most every 0.3 s except for sniper hits.
- **Other (base) bodies**: damage unchanged.

### 13. Players
Single player (no body object): unless NO_ARMOR the inventory armour absorbs (SPEC-003 method 31, values 11 and 12);
then the hit sound: means of death 27 → `impact/gore/sizzle.wav`; damage left > 0 → `impact/gore/impact<1–3>.wav`
(channel 4) and `impact/player/hit<1–4>.wav` (channel 2); fully absorbed → `impact/player/armor.wav`.
Deathmatch with a player body: the shot hit list is filled for TRACED/PROJECTILE hits if empty; the human body rules
run (Behaviour 10–11); a **female** player body takes trunc(damage × 1.5); if the damage is not lethal the body plays a
pain reaction (SPEC-722); if lethal the death reaction is prepared; the hit sound above; when the armour changed from
some to none (or back) the player's model skin is refreshed. On death in deathmatch the player's armour is removed and
the bolt-ons of both hands are dropped.

### 14. Area damage
Common: the candidates are all entities returned by an area box query of ± radius around the centre whose bounding
box centre (brush models without origin: centre of their absolute box) is within the radius; the world is skipped;
CanDamage (15) must hold from the inflictor. The gib counter is reset at the start of the call.
1. **Linear** (damage D, radius R, ignored entity, means of death, flags): skips the ignored entity, entities that
   cannot take damage, already-gibbed bodies (UNVERIFIED) and entities flagged as not hit by area damage; distance d
   between centres; with means of death 27 an entity is skipped while its burn timer runs. p = D × (1 − d/R). A trace
   (mask 0x6000001) from the explosion centre to the target centre gives the damage point; direction = point − centre;
   damage = trunc(p) (or trunc(trunc(p) × 0.05) with PUSH_ONLY), knockback = trunc(p), flags | AREA, origin value =
   explosion centre, values 11 and 12 = 0. The attacker itself is not spared.
2. **Min/max** (centre point, max M, min m, radius R, ignored entity, means of death, flags): as 1 but p = m + (M − m)
   × (1 − d/R) and no burn-timer rule (C4, claymores, grenades, rockets, SPEC-782).
3. **Cone** (damage D, radius R, ignored entity, means of death, cone direction c, minimum cosine q): only entities
   with normalise(target centre − source centre) · c ≥ q; p = D × (1 − d/R) × (dot − q)/(1 − q); damage = knockback =
   trunc(p); flags AREA only; point = source centre; direction = target origin − source centre (steam generators).

### 15. CanDamage
As Quake II, with trace mask "opaque" (solid, lava, slime) and point inflictor box: a brush model is damageable if
the trace from the inflictor's origin to the centre of its absolute box is clear or ends on it; other targets if any
of the traces to its origin or to origin ± 15 in x and y (four corners) is clear.

### 16. Violence settings
The parental locks (SPEC-302, SPEC-721) do not change damage numbers. `ai_goretest` ≠ 0 prints hit-resolution
diagnostics (`%d hits!`, `Entry wound position: …`, `spraying blood from …`, `spraying sparks from …`) and makes
characters spawn with 1000 health (SPEC-006).

## Constants
| Item | Value |
|---|---|
| Ghoul records per shot | 20 |
| Health clamp on death | −999 |
| Overkill cap | health + 150 |
| Gib thresholds | −50 (EXPLOSIVE), −750 (any) |
| Corpse hit damage per zone | 12; 3 for zones with a body-side cap |
| Back-stab factor / cosine | 2.5 / 0.707 |
| Push factors | general 250 (mass ≥ 50, z × 0.25 or 0.1); player 250 (from attacker, z × 0.25) or 1000 (z × 0.1, vz ≤ 200, timer 0.5 + 0.75·kb/mass); character 500 (mass ≥ 50, z + 0.3 or z × 0.2) |
| PUSH_ONLY area damage | 0.05 |
| Character "nodamage" halving | 0.5 |
| Female player damage factor (deathmatch) | 1.5 |
| Helicopter hull factor | 0.1 |
| Spark/ricochet repeat | 0.3 s |
| Armoured boss: damage cap per hit, pain repeat | 10, 5 s |
| Limb wound duration | U(5, 7) × (4 − toughness) × 0.25 s |
| Impairment step / cap | 0.1 / 0.5 |
| Music danger trigger | damage > 10 or health < 25 |
| Blood splat on player hits | size 200, heavy with probability 1/6 |
| Armour sound | `impact/armor/hit.wav`, volume 0.6, attenuation 1 |
| Hit direction sectors | 30 / 150 / 210 / 340 degrees |
| dmflags bits used | 0x40 realistic damage, 0x200 team play, 0x400 no friendly fire |

## Test vectors / how to verify
1. Single player, `sk_toughness 2`: a character's 20-point bullet to the player removes 3 health; with `nodamage 1`
   any character hit removes 1; with `nodamage 2` none.
2. Shoot an unaware meso soldier (100 health) in the chest with a 40-point bullet: damage 80 (sneak attack × 2, chest
   × 1), he survives with 20; aware: 40. An aware soldier with 100 health shot in the head for 40: 400 → capped at
   250, health −150 (no gib without EXPLOSIVE).
3. Arm hit with 40 damage on an aware 100-health soldier: 20; the arm is hurt (one-armed animations) for 3.75–5.25 s
   at toughness 1.
4. A character with armour configuration 1 shot in the chest with 40 damage: no health loss, sparks and
   `impact/armor/hit.wav`, the chest wound mesh shows the `gz_armor` skin; after 300 points of chest hits damage
   passes.
5. Grenade (min/max 200 → 40 within 200) 100 units from a 100-health soldier: p = 120, health −20 (no gib, needs
   < −50); a rocket direct hit (250, EXPLOSIVE) on the same soldier gives −150 and gibs him.
6. Knife slash from behind: damage × 2.5; slash on a head zone estimate lands on GZ_CHEST_BACK.
7. Deathmatch with team play and no friendly fire: a teammate's rocket does no damage but still pushes.
8. Player hit from the right (attacker yaw offset 270°): hit direction 4; from the front: 0x10.
9. Fire the sniper rifle through the gap between a soldier's arm and torso: no hit (Ghoul records empty), the bullet
   continues to the wall behind.
10. In-game check of `ai_goretest 1`: console prints the number of hits and entry/exit positions per shot.

## Open questions
- Realistic-damage deathmatch zone predicates (which zones count as armour-covered / as limbs): the original's calls
  look broken; verify in a deathmatch with dmflags 0x40 and armour.
- Meaning of flags 0x400, 0x200000, 0x2000000 and of the body state flag cleared by MICROWAVE/THROWN.
- Which marked helicopter sub-models count down and what "crew killed" changes (SPEC-722 vehicles).
- The second zone estimator's selection condition (character posture) and the exact head-chance height rule.
- Name of the player-state field that receives the hit direction (SPEC-201 player state).
