# SPEC-732: Special characters (bosses, animals, dead-on-arrival soldiers, severed limbs, player corpses)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (game module), version 1.07f
Depends on: SPEC-700 (AI framework: type codes, creation, spawnflags, profile tables, waking), SPEC-701 (perception),
SPEC-710 (decision kinds and spawn decision sets, Dekker phases), SPEC-711 (action classes), SPEC-720 (damage, hit
routing), SPEC-721 (gore, severed pieces), SPEC-722 (bodies: armour types, dog/cow bodies, corpses, face expressions),
SPEC-730 (human roster: notation, common set-up, skin registration), SPEC-761 (box rotation of point entities),
SPEC-780 (player body queue), SPEC-011 (Ghoul instance methods), SPEC-006 (classnames).

## Purpose

SPEC-730 gives the per-classname set-up of ordinary human characters. This spec covers the characters whose set-up
or lifetime differs from that pattern: the three bosses, the dogs and the bull, the "dead on arrival" variant of two
soldier classnames, the two AI types that have no classname (severed limbs and player corpses), and a role index
(snipers, rocket and flame troopers, hostages, civilians, allies) that points to where each behaviour is specified.
Helicopters, the tank and the snowcat are in SPEC-731.

## Interface / layout

### 1. Classnames and type codes covered here

| classname | type code | model directory / set name | start-up set (SPEC-730 table 3 = SPEC-710 spawn sets) |
|---|---:|---|---|
| `m_x_miraqboss` (Amu) | 39 | `enemy/meso` / `amu` | C (officer/boss) |
| `m_x_mraiderboss1` (Dekker, first meeting) | 40 | `enemy/meso` / `dekker1` | C (officer/boss) |
| `m_x_mraiderboss2` (Dekker, final fight) | 41 | `enemy/meso` / `dekker2` | G (Dekker) |
| `m_x_husky` | 11 | `enemy/husky` / `husky` | F (animal) |
| `m_x_germanshep` | 11 | `enemy/husky` / `husky` | F (animal) |
| `m_x_raiderdog` | 11 | `enemy/husky` / `husky` | F (animal) |
| `m_x_rottweiler` | 12 | `enemy/rottweiler` / `rottweiler` | F (animal) |
| `m_x_bull` | 6 | `enemy/bull` / `bull` | F (animal) |
| `m_nyc_mswat`, `m_kos_msniper1a` with spawnflag 65536 | 60, 72 | as SPEC-730 | H (dead on arrival, §2) |
| — (severed piece, created by the gore code) | 13 | the model object of the character it came from | none |
| — (player corpse in the body queue) | 14 | the model object of the dead player | none |

`m_x_germanshep` and `m_x_raiderdog` run exactly the `m_x_husky` spawn behaviour (same type, model and box): there
is no German shepherd or raider-dog model of their own. The stock data contains no `ghoul/enemy/husky/` directory at
all (only `rottweiler` and `bull` animal models exist), so in 1.07f only `m_x_rottweiler` and `m_x_bull` can appear
with a model (UNVERIFIED what happens to a husky without a model; verify with `gimme m_x_husky`).

### 2. Entity bounding boxes set by the spawn functions (after the common set-up)

| classname(s) | mins | maxs | extra |
|---|---|---|---|
| `m_x_husky`, `m_x_germanshep`, `m_x_raiderdog` | (−33, −33, −28) | (33, 33, 16) | — |
| `m_x_rottweiler` | (−26, −26, −20) | (26, 26, 11) | — |
| `m_x_bull` | (−30, −14, −33) | (56, 14, 26) | then rotated by the entity's angles in right-angle steps, as for props (SPEC-761) |
| bosses, dead-on-arrival | common box of SPEC-700 rule 1 | | |

### 3. Bounding-box presets (SPEC-700 §15) of the animal types

| preset | dogs (types 11, 12) mins / maxs | bull (type 6) mins / maxs |
|---:|---|---|
| 0 | (−24, −24, −20) / (24, 24, 20) | (−24, −14, −20) / (36, 14, 20) |
| 1 | (−16, −16, −16) / (16, 16, 8) | (−16, −16, −16) / (16, 16, 8) |
| 2 | (−32, −32, −8) / (32, 32, 8) | (−32, −32, −8) / (36, 32, 8) |
| 3 | (−24, −24, −8) / (24, 24, 8) | (−24, −24, −8) / (24, 24, 8) |
| 4 | (−32, −32, −8) / (32, 32, 8) | (−32, −32, −8) / (32, 32, 8) |
| 5 | (−32, −32, −26) / (32, 32, 8) | (−32, −32, −26) / (32, 32, 8) |
| 6 | (−24, −24, −32) / (24, 24, −24) | (−24, −24, −32) / (24, 24, −24) |
| 7 | (−32, −32, −28) / (32, 32, 16) | (−22, −26, −20) / (36, 26, 0) |
| 8 | all zero | all zero |

Human bosses use the human presets of SPEC-730 §2. Player corpses: §8.

## Behaviour

### 4. Bosses

All three use the human set-up of SPEC-730 §1 (male soldier family; for the two Dekker types the body is the
armoured male soldier body of SPEC-722) with the data below. Notation as SPEC-730 §5. Health starts at 100 like every
character (SPEC-700) and is changed only by armour types 4/5.

**m_x_miraqboss** (Amu) — `enemy/meso/amu`, type 39, set C. No weapon in either hand. Accessories (directory
`Enemy/Bolt`): `acc_holster (acc_holster_brwn) abolt_hip_r>to_abolt_hip_r ×1.14286`; `acc_hat_beret
(acc_hat_beret_red) abolt_head_t>to_abolt_head_t ×1.18286`; `acc_amu_head (rubbish) abolt_head_t>to_abolt_head_t
×1.18786`. Skins `b=b_xamu`, `a=a_xamu`, `h=h_amu`; face prefix `f_amu_` (talk faces `f_amu_f`, `f_amu_n`). All
registrations level 3 (§4 of SPEC-730), plus the gore skins. Voice (`soldier`, 2, 0). No armour. Profile: explicit
entry of SPEC-700 (type 39 row "31–37, 39, 53, 54, 103–107").

**m_x_mraiderboss1** (Dekker) — `enemy/meso/dekker1`, type 40, set C. Size scale 1.1. R 4 (heavy pistol),
no left-hand item, no accessories. Skins `b=b_xdekker`, `a=a_xdekker`, `h=h_dekker`, `f=f_dekker` (no face prefix:
no facial animation); registrations: gore skins and these four (level 3); no `gz_armor`. Voice (`soldier`, 2, 0).
Armour 7 (all 26 zones, armoured-boss base P = 75 / 99 / 150 at skill 0 / 1 / 2–3, SPEC-722). Profile: SPEC-700
explicit row for type 40 (192, 512, 0.7, 1.0, 0.38, 1.0, 275, flags 5, 1.5). Not counted in the armour-pickup drop
(SPEC-722).

**m_x_mraiderboss2** (Dekker, final) — `enemy/meso/dekker2`, type 41, set G (SPEC-710 kinds 11–14 and 5; Dekker
phases C10) plus an omniscience sense (SPEC-701). Set-up differences:
- size scale 1.1; step height 32 (default 24, SPEC-700 record offset 460);
- the Ghoul instance's playback-speed index is set to 4 (normal, SPEC-011 method 6);
- weapons only when spawnflag 131072 is clear: R 29 (Dekker pulse gun on the MPG model, SPEC-722), L 22 (thrown
  grenade);
- skins and voice as `m_x_mraiderboss1`; registrations additionally include `gz` `gz_armor`;
- entity flag "no knockback" (the flag spawnflag 128 sets, SPEC-700 rule 2) is set;
- an entity flag is set that makes hits on his held weapons ignore the held-weapon cases b) and c) of SPEC-720
  "Bolt-on first": a shot that hits his weapon first is neither stopped by it nor knocks it out of his hand
  (UNVERIFIED in game: shoot the pulse gun repeatedly on `ger4`; it should never fly away);
- armour 5 (health and maximum health ×2 → 200) and then armour 7 (all zones −P, boss base);
- the effect `environ/dekarmor1` is precached (played by the Dekker decisions, SPEC-710 C10).
Waking and alert special cases for type 41 (un-sticking, staying blind/deaf until triggered) are SPEC-700 §5/§6 and
SPEC-701.

### 5. Dogs (`m_x_husky`, `m_x_germanshep`, `m_x_raiderdog`, `m_x_rottweiler`)

Spawn: common character set-up (SPEC-700 rule 1), then the AI object of type 11 (the three husky classnames) or 12
(`m_x_rottweiler`) with directory `enemy/<set>` and the set name of §1; the entity box is replaced (§2); first-think =
start-up set F (one combat decision, kind 2).
Type set-up (both dog types):
1. bounding-box presets of §3;
2. the model object is created for `(directory, set name)` with no level-specific geometry (the set's own model,
   `ghoul/enemy/rottweiler/ROTTWEILER.ghb` in the stock data; SPEC-730 §3 level models do not apply);
3. size scale: 1.0 when spawnflag 8 is set, otherwise uniform random in [1.0, 1.15]; the instance transform is
   "rotate −90° about the vertical axis, then scale" as for humans;
4. body: husky body for type 11, rottweiler body for type 12 (SPEC-722 dog bodies), created only once; the body
   precaches its data;
5. sounds precached: `Enemy/Dog/Bark1.wav`, `Bark2.wav`, `Bark3.wav`, `Bite1.wav`, `Bite2.wav`, `Die.wav`, `Pain.wav`;
6. combat profile set explicitly (overriding any table): 16, 72, 0, 1.0, 0, 0, 200, flags 112, 1.0, personality
   2, 0, 1, 0, 2, 3 (as SPEC-700 "Dogs").
Movement and attacks use the animal move action (class 8) and the dog leap action (class 14) of SPEC-711; idle,
bark and walk-cycle choices are the dog body's (SPEC-722). Team 1 (SPEC-700). No skins or faces are registered.

### 6. Bull (`m_x_bull`)

Spawn: common set-up, AI type 6 (`enemy/bull`, `bull`), entity box of §2 rotated to the entity's angles in
right-angle steps, start-up set F. Type set-up: presets of §3, model object without level geometry
(`ghoul/enemy/bull/BULL.ghb`), size scale fixed 1.5, the same instance transform, cow/bull body (SPEC-722), body
precache, sounds `Enemy/Cow/Moo1.wav`, `Enemy/Cow/Moo2.wav`, `Enemy/Cow/Die.wav`. Team 100 (SPEC-700 §13). Profile:
object defaults (no table entry). The cow/bull body deals no damage itself (SPEC-722; UNVERIFIED in game whether the
bull can hurt anyone).

### 7. Dead on arrival (`m_nyc_mswat`, `m_kos_msniper1a` with spawnflag 65536)

These two classnames (and only these) check spawnflag 65536 at spawn. When it is set:
1. A variant of the common set-up runs: identical to SPEC-700 rule 1 except that health is 0 (maximum health 100;
   with cvar `ai_goretest` ≠ 0 both are 1000, i.e. the character is then alive) and the entity's render flags
   lack the "casts a shadow" bit (Ghoul model only; SPEC-411 bit 12).
2. The AI object is created as usual (type 60 or 72, same model, skins, weapons and armour as SPEC-730).
3. The first-think callback is the dead-on-arrival set H instead of the start-up set: no decisions, no senses and no
   path/point_combat decisions are added, no touch/use reactions are installed. On that think, if the character
   has a body:
   - an armour item (the item SPEC-722 drops from armoured corpses) is spawned at the character's origin raised by
     (the character's box top + 2), with the character's angles, item count = the character's `count` key, and the
     item's `target` = the character's `targetname`; its box bottom is lowered by 10 like every dropped armour
     item. Because the item targets the corpse and the corpse's action is class 5 (the only class that checks this
     exception, SPEC-711 A1 rule 5), the corpse is never removed by the corpse manager (SPEC-722 "Corpses" rule 1);
   - the body's armour is cleared (type 0);
   - the character is interrupted with an "interrupt with animation" request, i.e. the class-5 action of SPEC-711
     (special death `std_shead_2frnt_n_n` played at 1.5 × speed). This is the only 1.07f use of class 5.
   The next think is 0.1 s later; the AI think of SPEC-700 §7 runs on this and every later think as for any
   character (the callback itself runs only on the first think).
4. Spawnflag 65536 keeps its general meaning (SPEC-700 §3: dies in place, not counted for the SWAT rule).
Without spawnflag 65536 both classnames spawn normally (start-up set B).

### 8. Player corpse character (type 14)

When a deathmatch player is copied into a body-queue entity (SPEC-780 §10, SPEC-722 "Corpses"), a type-14 AI is
created for the corpse. Its set-up:
1. body: a new body of the dead player's body family (SPEC-722); if the player has no player body,
   `Couldn't find player body for replication!` is printed and the corpse has no body;
2. the body's owner is the corpse;
3. all nine bounding-box presets are the player's box widened by 16 units on each horizontal side (mins x, y − 16;
   maxs x, y + 16; z unchanged);
4. the corpse copies the player's render flags; if the player's body has a current animation, the corpse body
   precaches its data, a class-4 action ("dying without a start step", SPEC-711) carrying that animation is
   queued, the corpse body adopts the animation (so the pose shown continues), and the corpse AI runs its death
   (SPEC-700 §10) with no inflictor, no attacker and no damage;
5. the corpse uses the player's Ghoul model object (registered for savegames if new); the player's Ghoul instance
   with its bolt-ons is handed over to the corpse (the player entity no longer references it); the weapon bolt-on on
   `wbolt_hand_r` is then removed;
6. the face expression is set to "dead" (state 5) for 999 999.9 s;
7. the `quake_ground` and `quake_origin` bolt events are registered as for any character (SPEC-722), and the
   corpse's owner link is cleared.
The type's level-model answer is "none" (no level geometry); it has no skins of its own (registration hooks are the
human ones but the type registers nothing).

### 9. Severed limb character (type 13)

A severed piece (SPEC-721 rule 6) gets a type-13 AI. Set-up: the human common set-up (SPEC-730 §1) with the
directory and set of the source character. If the piece's Ghoul instance uses a different model object than the one
found, `Warning! Blown-off limb has object%s-%s-%s; parent has %s-%s-%s` is printed (directory, set and level model
of both) and the piece switches to the parent's object. Its level model is the source character's (for a player
source: `meso_player` when the directory is `enemy/meso`, `fem_play` when `enemy/female`).

Lifetime (the limb's think, every 0.1 s unless stated; S = the time of its first think − 0.1 s):
1. **Freeze bolt-ons**: once, when more than 1 s has passed since S, every bolt-on still attached to the piece
   freezes its animation at the current time (SPEC-011 method 19).
2. **Flying** (not yet settled): if the squared speed is below 10 or more than 30 s have passed since S, the piece
   *settles*: velocity and angular velocity become zero; the next think is 5 s later (0.1 s if step 1 has not
   happened yet). Otherwise, if its pitch is −85° or lower, the pitch is set to −90°, roll to 0 and the angular
   velocity to zero (a piece that has rotated onto its end lies flat).
3. **Settled**: if 30 s or more have passed since S the piece fades out. Otherwise it stays while at least one
   in-use client is within 800 units and faces it — the dot product of the client's view forward vector with the
   unit vector from the piece to the client is below 0.2 (the piece lies within about 101.5° of the view
   direction); the PVS test of the original passes the piece's own position twice and so always succeeds. If no
   client qualifies the piece fades out.
4. **Fade-out**: the standard entity fade (the one SPEC-722 uses for corpses) with duration 1 s; 1.1 s later the
   piece is removed.

### 10. Role index (where each kind of character is specified)

| role | classnames | where |
|---|---|---|
| snipers (right hand 7, profile class 7 flags 10) | `m_irq_msoldier1`, `m_irq_mbodyguard`, `m_afr_msniper`, `m_x_romulan`, `m_kos_msniper1a`, `m_kos_msniper1b`, `m_sib_mtrooper1b` | weapon SPEC-730; profile SPEC-700; firing SPEC-711; sniper shots at helicopter crews SPEC-720 |
| rocket troopers (14) | `m_irq_mrocket`, `m_afr_mrocket`, `m_cas_mrocket` | SPEC-730 (armour 5 + 3) |
| flamethrowers (18) | `m_irq_mbrute1b`, `m_afr_msoldier3`, `m_cas_ffemale` | SPEC-730; tanks explode when hit, SPEC-720/721 |
| MPG (17) | `m_tok_mkiller`, `m_cas_mbrute` | SPEC-730 §6 |
| knife/machete/stars (27, 30) | `m_afr_mbrute`, `m_tok_fassassin`, `m_tok_mninja` | SPEC-730 |
| hostages | any character with spawnflag 16384 | SPEC-700 §2/§11 |
| innocents (count for mission failure) | types 18, 21, 29, 37, 54, 77, 85, 99, 104, 105 (`m_nyc_ebum`, `m_irq_eman1`, `m_sib_escientist1`, `m_irq_moilworker`, `m_irq_mman2`, `m_kos_mmechanic`, `m_tok_mman2`, `m_sib_mmechanic`, `m_irq_fwoman1`, `m_irq_fwoman2`); `m_irq_msaddam` (53) is not one | SPEC-700 type table and §11 |
| allies / buddies (team 0) | `m_x_mmerc`, `m_x_msam`, `m_x_ftaylor`, `m_nyc_mswatleader`, `m_kos_mklaleader` (buddy flag); `m_nyc_mswat`, `m_kos_mrebel` and civilians of team 0 | SPEC-730, SPEC-700 §13 |
| civilians (set A) | SPEC-710 "civilian" set | SPEC-730 |
| bosses, animals, dead on arrival | this spec | |
| helicopters, tank, snowcat | SPEC-731 | |

## Constants

| item | value |
|---|---|
| Dekker size scale | 1.1 (both types) |
| final Dekker step height | 32 |
| final Dekker playback-speed index | 4 |
| dog size scale | 1.0 with spawnflag 8, else uniform [1.0, 1.15] |
| bull size scale | 1.5 |
| dead-on-arrival armour item height | character box top + 2 above its origin |
| dead-on-arrival special death | `std_shead_2frnt_n_n`, speed 1.5 (SPEC-711 class 5) |
| player corpse box widening | 16 units per horizontal side |
| player corpse expression | state 5 for 999 999.9 s |
| limb bolt-on freeze delay | 1 s |
| limb settle speed² | < 10 |
| limb maximum life | 30 s, then 1 s fade, removed 1.1 s after the fade starts |
| limb first check after settling | 5 s (0.1 s if bolt-ons not yet frozen) |
| limb lie-flat pitch | ≤ −85° → −90° |
| limb viewer distance / facing | ≤ 800 units; dot(view forward, piece→client) < 0.2 |

## Test vectors / how to verify

1. `gimme m_x_rottweiler` 20 times: sizes vary visibly (scale 1.0–1.15); with a map placing one with spawnflag 8 all
   are the same size. `gimme m_x_husky` shows the missing-model behaviour (UNVERIFIED: error, invisible dog or
   nothing).
2. `gimme m_x_bull`: box 86 units long along its facing; placing a bull with `angles 0 90 0` on a test map rotates the
   box (its long side then runs along y).
3. On `ger4` shoot the final Dekker's weapon: it is never knocked away; his health starts at 200; a pickup never
   drops from him (SPEC-722).
4. Test map with `m_kos_msniper1a` spawnflag 65536, `count 75`, `targetname s1`: at level start he falls with the
   head-shot death at fast speed, an armour item of 75 appears above him and drops, and his corpse never fades even
   with `ai_maxcorpses 1`. With `ai_goretest 1` he is alive.
5. Blow off a limb and look at it: it lies until 30 s after it was cut off, then fades in 1 s; turning away (more than
   about 100° from it) or walking more than 800 units away makes it fade soon after it has settled (first check 5 s
   after settling).
6. Deathmatch: die holding a weapon; the corpse in the body queue shows the dead face, keeps hats/accessories but
   not the weapon in the right hand, and can still be shot to pieces.

## Amendments to approved specs (for ERRATA)
1. **SPEC-711, action class table, class 5**: "no 1.07f decision uses it" stays true, but class 5 is used: the
   dead-on-arrival first think of `m_nyc_mswat` / `m_kos_msniper1a` with spawnflag 65536 (§7) interrupts the
   character with it at level start.
2. **SPEC-710, "Decision sets given at spawn", animal row**: also `m_x_raiderdog` (it runs the `m_x_husky` spawn
   behaviour, §1).

## Open questions
1. What happens to the three husky classnames without a model in the stock data (no `enemy/husky` directory).
2. Whether the dead-on-arrival armour item spawns when the character has `count` 0 (the item would have count 0;
   SPEC-781 item rules decide).
3. The role of the flag-1 argument used when creating animal model objects (believed: "no level-specific geometry";
   UNVERIFIED beyond the observable result that the set's own `.ghb` is used).
4. Whether Amu (`m_x_miraqboss`) ever attacks: he has no weapon but gets the officer/boss decision set.
