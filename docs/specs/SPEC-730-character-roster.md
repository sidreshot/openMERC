# SPEC-730: Character roster (per-classname set-up of human characters)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (game module), version 1.07f
Depends on: SPEC-700 (character AI framework: type codes, creation order, combat profile, spawnflags, teams),
SPEC-701 (perception), SPEC-720 (damage model, armour configurations), SPEC-721 (gore zones, bolt-on knock-off),
SPEC-722 (character bodies: set-up requests, attack types and weapon models, voices, faces), SPEC-782 (attack ids),
SPEC-010/SPEC-011 (Ghoul model objects and instances), SPEC-120/SPEC-121 (`.ghb`, `.gsq`, `_poff.gsq` files),
SPEC-006 (classnames). Special characters (bosses, dogs, bull, dead-on-arrival variants, severed limbs, player
corpses) are in SPEC-732; helicopters, tank and snowcat in SPEC-731.

## Purpose

SPEC-700 describes the character AI object and SPEC-722 the body; both leave the per-classname data open. This spec
gives, for every human `m_*` classname, everything its spawn function decides: the character type code, the model
(directory, sequence-set name, level model), the start-up decision set, the weapon in each hand, accessories,
skins and face, voice, armour, flags, and the skin-variety registration that limits which variants can appear. It
also answers SPEC-700's open item "which weapon each profile weapon class stands for".

## Interface / layout

### 1. Spawn sequence of a human classname
1. The common character set-up of SPEC-700 rule 1 (Quake II `monster_start` equivalent) runs. Exception: SPEC-732 §7
   (dead-on-arrival variant).
2. The character AI object is created with `(type code, model directory, set name)` (SPEC-700 rule 2). Three
   classnames choose the set name at random with equal chance (table 5).
3. The entity's first-think callback is set to the classname's *start-up set* (table 3). On the first think it runs
   SPEC-700 rule 2 ("monster_start_go": path/point_combat decisions, senses, reactions) and then adds the start-up
   decisions of that set.

Inside step 2 (SPEC-700 rule 2.2) the family preparation creates the body (family table of SPEC-722: ecto →
civilian/officer body, female → female body, meso → male soldier body; `m_x_mraiderboss1/2` → armoured male body)
and the type set-up runs:
1. bounding-box presets of the family (table 2) are stored (size scale 1.0 unless the type says otherwise);
2. the type's skin registration list (§4) is applied to the shared Ghoul model object;
3. the Ghoul model object for `(directory, set name, level model)` is found or created (§3) and an instance of it is
   attached to the entity; the instance transform is "rotate −90° about the vertical axis, then scale by the size
   scale";
4. the per-type body requests (§5 and the roster) run in the order: weapon hands, accessories, skins and parts,
   face prefix, voice, precache, armour.
A failure to create the model object is fatal: `Couldn't find enemy model %s/%s` (directory, set name).

### 2. Bounding-box presets of human families (all human types)
Index = the preset an animation record selects (SPEC-700 §15; believed to equal the record's posture value, UNVERIFIED).

| preset | mins | maxs |
|---:|---|---|
| 0 | (−8, −8, −32) | (8, 8, 41) |
| 1 | (−8, −8, −16) | (8, 8, 28) |
| 2 | (−32, −32, −8) | (32, 32, 16) |
| 3 | (−8, −8, −8) | (8, 8, 0) |
| 4 | (−32, −32, −8) | (32, 32, 8) |
| 5 | (−41, −41, −38) | (41, 41, −24) |
| 6 | (−41, −41, −32) | (41, 41, −24) |
| 7 | (−41, −41, −32) | (41, 41, −24) |
| 8 | (0, 0, 0) | (0, 0, 0) |

`m_x_mmerc` and `m_x_mcharacter_snow` replace preset 0 by (−16, −16, −32)…(16, 16, 41) after creation.

### 3. Model files
A character's Ghoul model object is identified by (directory, set name, level model):
- sequences: `ghoul/<directory>/<set name>.gsq`, parts switched off: `ghoul/<directory>/<set name>_poff.gsq` (SPEC-121);
- geometry: `ghoul/<directory>/<level model>.ghb`, where the level model depends on the family and the current
  level number (SPEC-700 §20):

| level number (SPEC-700 §20 order) | ecto family | female family | meso family |
|---|---|---|---|
| 0 (no match) | `ecto` (*) | `female` (*) | `meso` |
| 1 `tut1` | `ecto_tut1` | `female` (*) | `meso_tut1` |
| 2 `tsr1` | `ecto_tsr1` | `fem_pistol` | `meso_nyc` |
| 3 `tsr2` | `ecto` (*) | `female` (*) | `meso_tsr2` |
| 4 `trn1` | `ecto` (*) | `female` (*) | `meso_train` |
| 5 `kos1` | `ecto_kos1` | `female` (*) | `meso_kos1` |
| 6, 7 `kos2`, `kos3` | `ecto` (*) | `female` (*) | `meso_kos2`, `meso_kos3` |
| 8 `sib1` | `ecto` (*) | `fem_pistol` | `meso_sib1` |
| 9 `sib2` | `ecto_sib2` | `fem_pistol` | `meso_sib2` |
| 10 `sib3` | `ecto` (*) | `fem_pisnpc` | `meso_sib3` |
| 11–13 `irq1a`–`irq3a` | `ecto` (*) | `fem_npc` | `meso_irq1a`–`meso_irq3a` |
| 14 `nyc1` | `ecto` (*) | `fem_pistol` | `meso_nyc1` |
| 15 `nyc2` | `ecto` (*) | `female` (*) | `meso_nyc2` |
| 16 `nyc3` | `ecto` (*) | `fem_pisnpc` | `meso_nyc3` |
| 17–19 `sud1`–`sud3` | `ecto_sud` | `female` (*) | `meso_sud1`–`meso_sud3` |
| 20, 21 `jpn1`, `jpn2` | `ecto` (*) | `fem_pisnpc` | `meso_tok1`, `meso_tok2` |
| 22 `jpn3` | `ecto` (*) | `female` (*) | `meso_tok3` |
| 23–25 `irq1b`–`irq3b` | `ecto` (*) | `fem_npc` | `meso_irq1b`–`meso_irq3b` |
| 26 `ger1` | `ecto` (*) | `fem_shot` | `meso_cas1` |
| 27 `ger2` | `ecto` (*) | `female` (*) | `meso_cas2` |
| 28 `ger3` | `ecto` (*) | `fem_shot` | `meso_cas3` |
| 29 `ger4` | `ecto` (*) | `fem_shot` | `meso_cas4` |
| 30–32 `arm1`–`arm3` | `ecto_arm` | `fem_arm` | `meso_arm` |

(*) also prints `WARNING: no level .ghb for ecto--using ecto.ghb!` or `WARNING: no level .ghb for female--using
female.ghb!`. The stock data has no `meso.ghb`, so a meso character on an unknown map (level 0) has no geometry
(UNVERIFIED in game: spawn `m_irq_msoldier1` on a custom map without an `ainame`).

### 4. Skin registration and the two skin cvars
Every type has a *registration list* of (material, skin, level) and (face prefix, level) entries (shown in the
roster as `[L]` after each variant; entries without a mark have level 3). The list is applied when the character is
set up, before its model object is used. An entry is registered (made available to that model object) when
`level = 3` or `mskins_variety_limit − 0.1 < level`. A face-prefix entry P registers, under the same test, the skins
of material `f`: `Pn`, `Pd` always; `Pp` when `mskins_expression_limit − 0.1 < 2`; `Pm` and `Pf` when `< 1`; `Pb` when
`< 0`. Named characters also register talking variants of one face (`<face>t1` when the expression limit test `< 2`
holds, `t2` when `< 1`, `t3` when `< 0`; listed in the roster as "talk").
Every human type first registers the gore skins (all level 3): material `gz`: `gz_sing1`–`gz_sing3`, `gz_sing_k`,
`gz_multi1`–`gz_multi3`, `gz_multi_k`, `gz_mass1`–`gz_mass3`, `gz_mass_k`; material `c`: `c_blood`, `c_black`.
Armoured types also register `gz` `gz_armor`.

Setting a skin on a character succeeds only if that skin was registered; the set-up code relies on this: a random
variant is used only when the dice select it **and** it is registered, otherwise the default variant (always level 3)
is used. So with `mskins_variety_limit 3` every character looks like its default; with 0 (default) every variant can
appear.

| cvar | default | effect |
|---|---|---|
| `mskins_variety_limit` | `0` | skin/face variants with a registration level not above (value − 0.1) are not registered and cannot be chosen |
| `mskins_expression_limit` | `0` | ≥ 0.1 drops blink (`b`) faces; ≥ 1.1 also drops `m` and `f`; ≥ 2.1 also drops pain (`p`) and talking `t1` |

### 5. Notation used in the roster
- **Model**: `<directory>/<set name>`; type code (SPEC-700); start-up set (table 3).
- **R** right-hand attack type and **L** left-hand item (SPEC-722 "character set-up"; numbers are SPEC-782 attack
  ids: 3 `w_pistol1`, 4 `w_pistol2`, 5 machine pistol, 6 assault rifle, 7 sniper rifle, 9 auto-shotgun, 11 shotgun,
  12 machine gun, 14 rocket launcher, 17 MPG burst, 18 flamethrower, 29 Dekker pulse gun (MPG model); left: 22 and 24
  thrown grenade, 26 flash pack, 27 machete, 30 throwing star). "(¬131072)" = only when spawnflag 131072 is clear.
- **Acc**: accessories, all from directory `Enemy/Bolt`, written `model (skin) bolt>to-bolt ×scale`; a leading
  percentage is the chance (random accessory: attached when a uniform random number in [0, 1) is below it); "else"
  means only when the previous random accessory was not attached.
- **Skins**: `material=skin`; alternatives with their chances; `[L]` registration level (§4).
- **Parts**: mesh parts of the instance switched off (`−name`) or on (`+name`).
- **Face** prefix, **Voice** `(name, count, death-voice type)` (SPEC-722 voice request), **Armour** configuration
  (SPEC-720), **Extra** anything else.
- Random choices: "1/2" uses an equal-chance integer draw; "1/2 (odd)" uses the C library random number's parity
  (same distribution, different random stream: the game's `irand` versus the C library generator).

### 3 (table). Start-up decision sets
Decision codes are the decision kinds of SPEC-710 (2 combat, 3 inert, 4 search, 5 dodge, 6 retreat/cower, 11–14
Dekker phases); this table is the same data as SPEC-710 "Decision sets given at spawn". Decisions are appended in
the listed order, all with base priority 0 unless a priority is given.

| set | decisions added (in order) | other effects | used by |
|---|---|---|---|
| A bystander | 2, 6 | — | classnames marked A |
| B fighter | 2, 3, 4, 6, 5 | — | classnames marked B |
| C fighter, value 3 | 2, 3, 4, 6, 5 | the per-character decision value at AI record offset 492 (SPEC-700) is set to 3 | classnames marked C |
| F animal | 2 | — | dogs, bull (SPEC-732) |
| G Dekker | 11 (priority 10), 12 (8), 13 (5), 14 (2), 5 | value 3 as in C; an omniscience sense (SPEC-701 code 5) is added | `m_x_mraiderboss2` (SPEC-732) |
| H dead on arrival | none (no senses either) | SPEC-732 §7 | `m_nyc_mswat`, `m_kos_msniper1a` with spawnflag 65536 |
Two further sets exist only in the savegame callback table and are used by no classname: (2, 3, 4, 6, value 2) and
(2, 3, 6). Set F is used by `m_x_husky`, `m_x_germanshep`, `m_x_raiderdog`, `m_x_rottweiler` and `m_x_bull`.

### 6. Profile weapon classes (answer to SPEC-700 "Which weapon each class stands for")
The weapon class of the SPEC-700 profile builder is the right-hand attack id of the same character: 3 pistol
(`w_pistol1`), 4 heavy pistol (`w_pistol2`), 5 machine pistol, 6 assault rifle, 7 sniper rifle, 9 auto-shotgun,
11 shotgun, 12 machine gun, 14 rocket launcher, 16 MPG, 18 flamethrower. The profile table is separate data and does
not always match the hand: types 65 (`m_cas_mbrute`) and 88 (`m_tok_mkiller`) carry attack 17 (MPG burst) but use
profile class 16; types 23, 24, 74 (class 4) do carry 4; type 69 (class 11) carries 11. Types whose profile is
explicit (SPEC-700) are listed there.

## Behaviour

### 7. Roster — civilian/officer family (`enemy/ecto`, ecto body; Skins also always include `c=c_black` unless noted)

**m_nyc_eskinhead3** — `enemy/ecto/skinleader`, type 15, set B.
R 3 and 25% `acc_holster (acc_holster_blck) abolt_hip_r>to_abolt_hip_r ×1.125` (both ¬131072); 50% `acc_ecto_glasses
(acc_glasses_sun) abolt_head_t>to_abolt_head_t ×1`; 50% `acc_hat_baseball (acc_hat_baseball_grcamo)
abolt_head_t>to_abolt_head_t_bkwd ×1.125`. Skins (no `c`): 1/2 `b=b_nycskinhleader2`[1] + `a=a_nycskinhleader2`[1],
else `b=b_nycskinhleader` + `a=a_nycskinhleader`; 1/2 `h=h_w_1_s`[1] face `f_w_1_`[1], else `h=h_w_12_s` face
`f_w_12_`. Voice (`sk`, 1, 0). Extra: precaches effect `environ/onfireburst`.

**m_nyc_estockbroker** — `enemy/ecto/stockbroke` or `stockbroke2` (1/2 odd), type 16, set A.
50% `w_briefcase (rubbish) wbolt_hand_l>to_wbolt_hand_l ×1`; 50% `acc_ecto_glasses (acc_glasses_norm) ×1`.
Skins `b=b_nycmalestockbroker`; with set name `stockbroke2`: `a=a_nycmalestockbrokerb`[1] and `h=h_b_1_s`[1], face
`f_b_1_`[1]; otherwise (or when either is unavailable): parts `−_lhairbang` and `−_rhairbang` each with 1/2 (odd),
`a=a_nycmalestockbroker`, `h=h_w_2_c`, face `f_w_2_`. Voice (`npc`, 1, 0).

**m_nyc_etourist** — `enemy/ecto/tourist`, type 17, set A. 50% `acc_ecto_glasses (acc_glasses_norm) ×1`. Skins
`b=b_dcmaletourist`, `a=a_dcmaletourist`, `h=h_w_4_s`; face `f_w_4_`. Voice (`npc`, 1, 0).

**m_nyc_ebum** — `enemy/ecto/bum`, type 18 (innocent), set A. `acc_hat_flop (acc_hat_flop_brwn) ×1.125`. Skins
`b=b_nycbum`, `a=a_nycbum`, `hb=hb_h_1_b`; face `f_hb_1_`. Voice (`npc`, 1, 0).

**(type 19, no classname)** — same as `m_nyc_ebum` with `b/a=…_colmaleprisoner1`. **(type 20, no classname)** —
skins `b/a=…_colmaleprisoner2`, `h=h_h_1_c`, face `f_h_1_`, voice (`npc`, 1, 0), no accessories.

**m_irq_eman1** — `enemy/ecto/irqcitizen`, type 21 (innocent), set A. 50% `acc_ecto_glasses (acc_glasses_norm) ×1`;
33.3% `acc_hat_turbin (acc_hat_turbin_wht) ×1.125`, else 50% `acc_hat_top (acc_hat_top_wht) ×1.125`. Skins
`b=b_irqmalecitizen`, `a=a_irqmalecitizen`, `h=h_m_2_s`; part `−_headbeard`; face `f_m_2_`. Voice (`npc`, 1, 0).

**m_afr_ecommander** — `enemy/ecto/commander`, type 22, set C. R 4. `acc_hat_general (acc_hat_general_brwn)
×1.125`; 50% `acc_ecto_glasses (acc_glasses_sun) ×1`; 50% `acc_holster (acc_holster_brwn) abolt_hip_r ×1`; 50%
`acc_pouch_thigh (acc_pouch_thigh_tan) abolt_thigh_l>to_abolt_thigh_l ×1.125`; 50% `acc_box_hip_smll
(acc_box_hip_smll_brwn) abolt_hip_l>to_abolt_hip_l ×1.125`. Skins `b=b_ugncommander`, `a=a_ugncommander`; 1/2 (odd)
`h=h_b_1_s`[0] face `f_b_1_`[0], else `h=h_b_2_s` face `f_b_2_`. Voice (`so`, 1, 0).

**m_kos_eofficer** — `enemy/ecto/serbofficer`, type 23, set C. R 4. `acc_hat_general2 (acc_hat_general2_grn)
×1.125`; 25% `acc_holster (acc_holster_brwn) ×1.125`. Skins `b=b_kosofficer`, `a=a_kosofficer`; 1/3 `h=h_w_2_c`[0]
face `f_w_2_`[0]; else 1/2 `h=h_w_5_c`[1] face `f_w_5_`[1]; else `h=h_h_6_s` face `f_h_6_`. Voice (`so`, 1, 0).

**m_kos_erefugee** — `enemy/ecto/kosrefugee`, type 24, set C. R 4. 20% `acc_hat_baseball (acc_hat_baseball_tan)
abolt_head_t>to_abolt_head_t_fwd ×1.125`; 25% `acc_holster (acc_holster_brwn) ×1.125`. Skins `b=b_kosrefugee`,
`a=a_kosrefugee`, `h=h_w_4_s`; part `−_headbeard`; face `f_w_4_`. Voice (`npc`, 1, 0). Extra: precaches
`environ/onfireburst`.

**m_irq_eofficer** — `enemy/ecto/iraqofficer`, type 25, set C. R 4. `acc_hat_beret (acc_hat_beret_blck) ×1.125`;
50% `acc_ecto_glasses (acc_glasses_sun) ×1`; 50% `acc_holster (acc_holster_brwn) ×1.125`; 50% `acc_pouch_thigh
(acc_pouch_thigh_blck) abolt_thigh_l ×1.125`; 50% `acc_box_hip_smll (acc_box_hip_smll_brwn) abolt_hip_l ×1.125`.
Skins `b=b_irqofficer`, `a=a_irqofficer`, `h=h_m_1_s`; face `f_m_1_`. Voice (`so`, 1, 0).

**m_afr_eworker** — `enemy/ecto/factory`, type 26, set B. R 4. `acc_hat_top (acc_hat_top_wht) ×1.125`; 50%
`acc_box_hip_smll (acc_box_hip_smll_blck) abolt_hip_l ×1.125`; 50% the same on `abolt_hip_r>to_abolt_hip_r`. Skins
`b=b_ugnfactoryworker`, `a=a_ugnfactoryworker`; 1/2 `h=h_b_4_c`[1] face `f_b_4_`[1], else `h=h_b_3_s` face
`f_b_3_`; part `−_headcrew`. Voice (`so`, 1, 0).

**(type 27, no classname)** — `enemy/ecto`, chemist: R 4; 33.3% `acc_ecto_glasses (acc_glasses_norm)`, else 50%
`acc_ecto_glasses (acc_glasses_sun)`; 50% `acc_hat_top (acc_hat_top_wht)`; 50% `acc_box_hip_smll
(acc_box_hip_smll_blck)` on each hip; skins `b/a=…_colchemist`; set name `chemist2` → `h=h_h_1_c` face `f_h_1_`,
otherwise `h=h_h_2_s` face `f_h_2_`; voice (`soldier`, 2, 0).

**m_sib_eofficer** — `enemy/ecto/sibsuit1` or `sibsuit2` (1/2 odd), type 28, set C. R 3. 50% `w_briefcase (rubbish)
wbolt_hand_l ×1`; 50% `acc_ecto_glasses (acc_glasses_norm) ×1`; `acc_hat_tall (acc_hat_tall_gry) ×1.125`; 50%
`acc_box_hip_smll (acc_box_hip_smll_blck) abolt_hip_l ×1.125`; 50% `acc_box_holster (acc_holster_blck)
abolt_hip_r ×1.125`. Skins `b=b_sibsuit`, `a=a_sibsuit`; set name `sibsuit2` → `h=h_w_3_s`[2] face `f_w_3_`[2],
otherwise `h=h_w_5_c` face `f_w_5_`; part `−_headcrew` (in the `h_w_5_c` case `−_headbald` instead when the tall hat
could not be attached). Voice (`so`, 1, 0).

**m_sib_escientist1** — `enemy/ecto/sibscience1` or `sibscience2` (1/2 odd), type 29 (innocent), set C. 50%
`acc_ecto_glasses (acc_glasses_norm) ×1`; 50% `acc_box_hip_smll (acc_box_hip_smll_blck)` on each hip ×1.125. Skins
`b=b_sibscientist`, `a=a_sibscientist`; set name `sibscience2` → `h=h_w_1_s`[0] face `f_w_1_`[0], otherwise
`h=h_w_2_c` face `f_w_2_`. Voice (`so`, 1, 0).

**m_nyc_epunk** — `enemy/ecto/zitpunk`, type 30, set B. R 3 (¬131072). 50% `acc_ecto_glasses (acc_glasses_sun) ×1`.
Skins `a=a_nycpunk2`, `b=b_nycpunk2`, `h=h_w_12_s`; face `f_w_12_`. Voice (`sk`, 1, 0).

### 8. Roster — female family (`enemy/female`, female body; death-voice type 5 for all)

**m_nyc_fskinchick** — `skinchick`, type 100, set B. R 3. Skins `b=b_nycskinchick`, `a=a_nycskinchick`,
`h=h_w_15_c` (no `c`); face `f_w_15_`. Voice (`skf`, 1, 5).

**m_nyc_fwoman** — `nywoman`, type 101, set A. 1/2: `b=b_nycwomanb`[1], `a=a_nycwomanb`[1], `h=h_b_8_c`[1], face
`f_b_8_`[1], part `−_medmhairmhead`; otherwise `b=b_nycwomanw`, `a=a_nycwomanw`, `h=h_w_18_m`, face `f_w_18_`, parts
`−_crewhair −_lear −_rear`. Voice (`npcf`, 1, 5).

**m_sib_fguard2** — `sibguard`, type 102, set B. R 5. 50% `acc_hat_baseball (acc_hat_baseball_sib)
abolt_head_t>to_abolt_head_t ×1.125`. Skins `b=b_sibguard2`, `a=a_sibguard2`; 1/2 `h=h_w_18_m`[1] face `f_w_18_`[1]
parts `−_crewhair −_lear −_rear`, else `h=h_w_17_c` face `f_w_17_` part `−_medmhairmhead`. Voice: none (empty voice
name: mute). Armour 1.

**m_sib_fscientist2** — `sibscience`, type 103, set A. Skins `b=b_sibscientist2`, `a=a_sibscientist2`; 2/3
`h=h_w_18_m`[1] face `f_w_18_`[1] and then 1/2 parts `−_crewhair −_lear −_rear`, else parts `−_medmhairmhead −_lbang
−_rbang`; otherwise (1/3 or unavailable) `h=h_w_17_c` face `f_w_17_` parts `−_medmhairmhead −_lbang −_rbang`. Voice:
none.

**m_irq_fwoman1** / **m_irq_fwoman2** — `irqwoman1` / `irqwoman2`, types 104 / 105 (innocent), set A. Skins `b`, `a`,
`h`, `f` = `b_irqwoman1`, `a_irqwoman1`, `h_irqwoman1`, `f_irqwoman1` (resp. `…2`); no face prefix (no facial
animation). Voice (`npcf`, 1, 5).

**m_tok_fwoman1** / **m_tok_fwoman2** — `tokwoman1` / `tokwoman2`, types 106 / 107, set A. 50% `acc_glasses
(acc_glasses_norm) ×1`. Skins `b=b_tokwoman1`, `a=a_tokwoman1` (resp. `…2`); 1/2 `h=h_j_8_m`[1] face `f_j_8_`[1]
parts `−_crewhair −_lear −_rear`, else `h=h_j_7_c` face `f_j_7_` part `−_medmhairmhead`. Voice (`npcf`, 1, 5).

**m_tok_fassassin** — `tokassassin`, type 108, set B. R 3, L 30 (throwing star). Skins `b=b_tokassassin`,
`a=a_tokassassin`; head/face/parts as `m_tok_fwoman1`. Voice (`skinhead`, 2, 5).

**m_cas_ffemale** — `raider`, type 109, set B. L 26 (flash pack), R 18 (flamethrower). `acc_tanks (acc_tanks_cas)
abolt_back>to_abolt_back ×1.125`. Skins `b=b_dcfemale`, `a=a_dcfemale`, `h=h_raider5`, `f=f_raider5` (no face
prefix). Voice (`raf`, 1, 5). Armour 7.

**m_x_ftaylor** — `taylor`, type 110 (team 0), set B. Buddy (SPEC-700). R 6 (¬131072). Skins `b=b_xtaylor`,
`a=a_xtaylor`, `h=h_taylor`; face `f_taylor_` (talk `f_taylor_n`). Voice (`taylor`, 1, 5). Armour 7.

### 9. Roster — male soldier family (`enemy/meso`, male soldier body; `c=c_black` where marked "c")

Named characters and allies:

**m_x_mcharacter** — `john`, type 31, set B. `acc_backpack_smll (acc_backpack_smll_dgrey) abolt_back>to_abolt_back
×1.14286`; `acc_hat_flop3 (acc_hat_flop3) ×1.05`; `acc_headset (acc_headset) ×1.14286`; `acc_box_hip
(acc_box_hip_canteen1) abolt_hip_l ×1.14286`; `acc_holster (acc_holster_blck) abolt_calf_r>to_abolt_hip_r ×1.14286`;
R 3 (¬131072). Skins `b=b_xjohn`, `a=a_xjohn`, `h=h_john`; face `f_john_` (talk `f_john_n`, `f_john_m`). Voice (`jm`, 1, 0).

**m_x_mcharacter_snow** — `johnsnow`, type 32, set B. `acc_backpack_lrg (acc_backpack_lrg_blk) ×1.14286`;
`acc_hat_snow (acc_hat_snow) ×1.14286`; `acc_headset ×1.14286`; R 3 (¬131072). Skins `b=b_xjohn_snow`,
`a=a_xjohn_snow`, `h=h_john`; face and talk as above. Voice (`jm`, 1, 0). Preset 0 enlarged (§2).

**m_x_mcharacter_desert** — `johndesert`, type 33, set B. `acc_backpack_smll (acc_backpack_smll) ×1.14286`;
`acc_hat_flop3 (acc_hat_flop3_desert) ×1.05`; `acc_headset ×1.14286`; `acc_box_hip (acc_box_hip_canteen1) abolt_hip_l
×1.14286`; R 3 (¬131072). Skins `b=b_xjohn_desert`, `a=a_xjohn_desert`, `h=h_john`; face as above. Voice (`jm`, 1, 0).

**m_x_mmerc** (Hawk) — `hawk`, type 34 (team 0), set B. Entity flag "no knockback"; buddy. `acc_backpack_lrg
(acc_backpack_lrg_tan) ×1.14286`; `acc_glasses (acc_glasses_sun) ×1.14286`; `acc_headset ×1.14286`; R 5 (¬131072;
the machine-pistol player weapon is not precached for this classname, SPEC-722). Skins `b=b_mercdemo1`,
`a=a_mercdemo1`, `h=h_b_6_s`; face `f_b_6_` (talk `f_b_6_n`, `f_b_6_f`). Voice (`hawk`, 1, 0). Extra: precaches sound
`impact/Hawk/LookOut.adp`; preset 0 enlarged (§2); never reported by sight (SPEC-701).

**m_x_mhurtmerc** — `hawk`, type 35, set B. No weapon, no accessories; skins, face, voice as `m_x_mmerc`.

**m_x_msam** — `sam`, type 36 (team 0), set B. Buddy. `acc_glasses (acc_glasses_norm) ×1.14286`. Skins `b=b_xsam`,
`a=a_xsam`, `h=h_sam`; face `f_sam_` (talk `f_sam_n`). Voice (`sam`, 1, 0). No weapon.

**m_nyc_mswatleader** — `nyswatlead`, type 61 (team 0), set B. Buddy. R 3; `acc_helmet2 (acc_helmet2)`; `acc_goggles
(acc_goggles)`; `acc_holster (acc_holster_blck) abolt_hip_r`; `acc_box_hip_smll (acc_box_hip_smll_blck) abolt_hip_l`
(all ×1.14286). Skins `b=b_nycswat`, `a=a_nycswat`, `h=h_b_4_c`; face `f_b_4_` (talk `f_b_4_n`). Voice (`sw`, 1, 0).
Armour 1.

**m_nyc_mswat** — `nyswatguy`, type 60 (team 0), set B (H with spawnflag 65536, SPEC-732 §7). R 3; `acc_helmet2`;
`acc_goggles`; 25% `acc_holster (acc_holster_blck) abolt_hip_r`; 25% `acc_box_hip_smll (acc_box_hip_smll_blck)
abolt_hip_l` (all ×1.14286). Skins `b=b_nycswat`, `a=a_nycswat2`, `h=h_w_3_s`; face `f_w_3_`. Voice (`sw`, 1, 0).
Armour 1. Killing one is mission failure (SPEC-700 §11).

**m_kos_mrebel** — `kosrebel`, type 78 (team 0), set B. R 6; 25% `acc_backpack_low (acc_backpack_low_dgrey)
abolt_back`; 25% `acc_box_hip (acc_box_hip_canteen1) abolt_hip_l`. Skins c; 1/2 `b=b_kosrebel1`[2] + `a=a_kosrebel1`[2],
else `b/a=…_kosrebel2`; `h=h_w_19_s`; face `f_w_19_`; then 25% `acc_helmet (acc_helmet_green1)` → parts `−_crewhead
−_baldhead −_comface`; otherwise parts `−_helmethead −_helmetface`, 25% `acc_hat_baseball (acc_hat_baseball_grcamo)
abolt_head_t>to_abolt_head_t_fwd`, part `−_crewhead`. Voice (`npc`, 1, 0). Extra: `environ/onfireburst` precached.

**m_kos_mklaleader** — `kosklaguy`, type 79 (team 0), set B. Buddy. R 6; `acc_box_hip (acc_box_hip_canteen1)
abolt_hip_l`; `acc_hat_baseball (acc_hat_baseball_grcamo) abolt_head_t>to_abolt_head_t_fwd`. Skins c, `b=b_kosrebel2`,
`a=a_kosrebel2`, `h=h_w_20_c`; face `f_w_20_` (talk `f_w_20_n`); parts `−_helmethead −_helmetface −_crewhead`. Voice:
none. Extra: `environ/onfireburst` precached.

**m_nyc_mpolitician** — `politician`, type 84 (team 0), set A. 50% `w_briefcase (rubbish) wbolt_hand_l ×1`; 25%
`acc_glasses (acc_glasses_norm)`. Skins `b=b_dcmalepol`, `a=a_dcmalepol`, `h=h_b_2_s`; face `f_b_2_`. Voice (`npc`, 1, 0).

New York gang (team 3):

**m_nyc_mpunk** — `nypunk`, type 38, set B. R 11; 25% `acc_glasses (acc_glasses_sun)`; `acc_spikes_shlder
(acc_spikes_shlder)` on `abolt_shoulder_r>to_abolt_shoulder_r` and on `abolt_shoulder_l>to_abolt_shoulder_l`. Skins c,
`b=b_nycpunk1`, `a=a_nycpunk1`, `h=h_w_3_s`; face `f_w_3_`. Voice (`sk`, 1, 0).

**m_nyc_mskinhead1** — `skinhead1`, type 80, set B. R 11; 25% `acc_glasses (acc_glasses_sun)`. 1/2 `b=b_nycskinhead1b`[1]
+ `a=a_nycskinhead1b`[1] with 12.5% `acc_hat_baseball (acc_hat_baseball_grcamo) to_abolt_head_t_bkwd`, else 25% the
same hat with skin `acc_hat_baseball_dgrey`; otherwise 25% `acc_hat_baseball (acc_hat_baseball_dgrey)
to_abolt_head_t_bkwd` and `b/a=…_nycskinhead1`. Head: 1/3 `h=h_w_1_s`[0] face `f_w_1_`[0]; else 1/2 `h=h_w_13_s`[1]
face `f_w_13_`[1]; else `h=h_w_3_s` face `f_w_3_`. Voice (`sk`, 1, 0). Extra: precaches `environ/onfireburst`,
`environ/takeleak`.

**m_nyc_mskinhead2a** — `skinhead2`, type 81, set B. R 3 (¬131072); 25% `acc_glasses (acc_glasses_sun)`. 1/2
`b=b_nycskinhead2b`[1] + `a=a_nycskinhead2b`[1], else `b/a=…_nycskinhead2`. Head: 1/2 `h=h_w_6_s`[1] face `f_w_6_`[1];
else 1/3 `h=h_w_9_s`[2] face `f_w_9_`[2]; else `h=h_w_11_c` face `f_w_11_` with parts `−_baldhead +_crewhead`.
Voice (`sk`, 1, 0). Extra: `environ/onfireburst`.

**m_nyc_mskinhead2b** — `skinhead2b`, type 82, set B. R 6; 25% `acc_glasses (acc_glasses_sun)`. 1/2 (odd)
`b/a=…_nycskinhead2`, else `b/a=…_nycskinhead2b` (all level 3). Head: 1/3 `h=h_w_11_c` face `f_w_11_` parts
`−_baldhead +_crewhead`; else 1/2 `h=h_w_6_s` face `f_w_6_`; else `h=h_w_9_s` face `f_w_9_`. Voice (`sk`, 1, 0).
Extra: `environ/onfireburst`.

**m_x_mskinboss** — `skinboss`, type 83, set C. Size scale 1.1. R 4 and `acc_holster (acc_holster_blck)
abolt_hip_r` (both ¬131072). Skins `b=b_nycskinboss`, `a=a_nycskinboss`, `h=h_w_8_s`; face `f_w_8_` (talk `f_w_8_m`).
Voice (`sk`, 1, 0). Stays blind/deaf until triggered (SPEC-701).

Iraq (team 1; voice `so` unless stated):

**m_irq_msoldier1** — `irqsoldier1`, type 42, set B. R 7, L 22; `acc_hat_beret (acc_hat_beret_red)`; 25%
`acc_glasses (acc_glasses_sun)`; 25% `acc_pouch_thigh (acc_pouch_thigh_green)` on `abolt_thigh_r` and 25% on
`abolt_thigh_l`. Skins c, `b/a=…_irqsoldier1`; 1/2 (odd) `h=h_m_3_s` face `f_m_3_`, else `h=h_m_1_s` face `f_m_1_`.

**m_irq_msoldier2a** / **m_irq_msoldier2b** — `irqsoldier2` / `irqsoldier2b`, types 43 / 44, set B. R 6 / R 12;
`acc_helmet (acc_helmet_tan1)`; `acc_backpack_low (acc_backpack_low_tan) abolt_hip_r>to_abolt_hip_r`; per side
(right, then left): 10% `acc_pouch_thigh (acc_pouch_thigh_tan)` on the thigh, else 15% `acc_box_hip_smll
(acc_box_hip_smll_brwn)` on the hip. Skins c, `b/a=…_irqsoldier2`; 1/2 (odd) `h=h_m_4_c`[2] face `f_m_4_`[2], else
`h=h_m_3_s` face `f_m_3_`.

**m_irq_mrepguard1a** / **m_irq_mrepguard1b** — `irqrepgd` / `irqrepgdb`, types 45 / 46, set B. R 6, L 22 / R 12,
L 24; `acc_helmet (acc_helmet_green1)`; `acc_backpack_smll (acc_backpack_smll_green) abolt_back`; 15% each:
`acc_pouch_thigh (acc_pouch_thigh_dark1)` right thigh, `acc_box_hip_lrg (acc_box_hip_lrg_green)` right hip, the pouch
left thigh, the box left hip. Skins c, `b/a=…_irqrepguard`; 1/2 (odd) `h=h_m_5_s` face `f_m_5_`, else `h=h_m_1_s`
face `f_m_1_`.

**m_irq_mpolice** — `irqpolice`, type 47, set C. R 4; `acc_helmet (acc_helmet_wht1)`; 25% `acc_glasses
(acc_glasses_sun)`; 15% `acc_pouch_thigh (acc_pouch_thigh_tan)` right and 15% left; 10% `acc_holster
(acc_holster_brwn) abolt_hip_r`, else 15% `acc_box_hip_smll (acc_box_hip_smll_blck) abolt_hip_r`; 15%
`acc_box_hip_smll (acc_box_hip_smll_blck) abolt_hip_l`. Skins (no c) `b/a=…_irqpolice`; 1/2 (odd) `h=h_m_4_c`[1]
face `f_m_4_`[1], else `h=h_m_5_s` face `f_m_5_`.

**m_irq_mcommander** — `irqcommand`, type 48, set C. R 4; `acc_hat_beret (acc_hat_beret_grn)`; 25% `acc_holster
(acc_holster_blck) abolt_hip_r`. Skins `b/a=…_irqcommander`; 1/2 `h=hb_h_2_b`[2] face `f_hb_1_`[2] parts
`−_baldhead −_comface`, else `h=h_m_5_s` face `f_m_5_` parts `−_beardhead −_beardface`.

**m_irq_mbrute1a** — `irqbrute`, type 49, set C. R 6, L 22; `acc_backpack_lrg (acc_backpack_lrg_tan) abolt_back`;
25% `acc_holster (acc_holster_brwn) abolt_hip_r`; 15% `acc_box_hip (acc_box_hip_blk) abolt_hip_l`. Skins
`b/a=…_irqbrute`; 1/2 (odd) `h=h_m_5_s` face `f_m_5_`, else `h=h_m_3_s` face `f_m_3_`. Armour 5.

**m_irq_mbrute1b** — `irqbruteb`, type 50, set C. R 18; `acc_tanks (acc_tanks_irq) abolt_back`; 25% `acc_glasses
(acc_glasses_sun)`; 25% holster (brwn) right hip; 15% `acc_box_hip (acc_box_hip_blk)` left hip. Skins and head as
`m_irq_mbrute1a`. Armour 4.

**m_irq_mbodyguard** — `irqbguard`, type 51, set C. R 7, L 22; `acc_hat_beret (acc_hat_beret_blck)`; 25%
`acc_holster (acc_holster_blck)` right hip; 15% `acc_box_hip (acc_box_hip_blk)` left hip. Skins `b/a=…_irqbodyguard`;
1/2 (odd) `h=h_m_4_c`[0] face `f_m_4_`[0], else `h=h_m_1_s` face `f_m_1_`. Armour 1.

**m_irq_mrocket** — `irqrocket`, type 52, set C. R 14; `acc_faceplate (acc_faceplate_irq)`; 25% `acc_backpack_smll
(acc_backpack_smll_tan) abolt_back`. Skins `b/a/f/h=…_irqrocket` (no face prefix). Armour 5 then 3 (health ×2 and
heavy zone armour, SPEC-720).

**m_irq_msaddam** — `saddam`, type 53 (not counted as innocent, SPEC-700), set C. `acc_hat_beret (acc_hat_beret_blck)`. Skins
`b/a=…_irqsaddam`, `h=h_saddam`; face `f_saddam_` (talk `f_saddam_n`). No weapon.

**m_irq_mman2** — `irqman2`, type 54 (innocent), set C. 25% `acc_hat_turbin (acc_hat_turbin_wht)`, else 25%
`acc_hat_top (acc_hat_top)`, else 33.3% `acc_hat_top (acc_hat_top_wht)`; 15% `acc_machette (acc_machette)
abolt_thigh_r>to_abolt_thigh_r`. 1/2 `b=b_irqman2a`[1] + `a=a_irqman2a`[1], else `b/a=…_irqman2b`; 1/2 `h=h_m_2_s` face
`f_m_2_`, else `h=h_m_1_s` face `f_m_1_`; in both cases parts `−_beardhead −_beardface`. Voice (`npc`, 1, 0). No weapon.

**m_irq_moilworker** — `irqworker`, type 37 (innocent), set A. 25% `acc_glasses (acc_glasses_norm)`; 10%
`acc_hat_turbin (acc_hat_turbin_wht)`, else 85% `acc_hat_hard (acc_hat_hard_wht)`. Skins c, `b/a=…_irqworker`; 1/2
(odd) `h=h_m_2_s` face `f_m_2_`, else `h=h_m_1_s` face `f_m_1_`. Voice (`npc`, 1, 0).

**m_x_miraqboss** — see SPEC-732 §4.

Sudan/Uganda (team 1):

**m_afr_msoldier1a** — `ugsoldier1`, type 56, set B. R 3; 50% `acc_hat_flop2 (acc_hat_flop2_tan)`; 25%
`acc_box_hip_smll (acc_box_hip_smll_brwn)` left hip; 25% `acc_holster (acc_holster_brwn)` right hip. Skins c,
`b/a=…_ugnsoldier1`; 1/2 (odd) `h=h_b_4_c`[1] face `f_b_4_`[1] with part `−_baldhead` when the hat was not attached
and `−_crewhead` when it was; else `h=h_b_5_s` face `f_b_5_` part `−_crewhead`. Voice (`so`, 1, 0).

**m_afr_msoldier1b** — `ugsoldier1b`, type 57, set B. R 11, L 22 (both ¬131072); otherwise as `m_afr_msoldier1a`.

**m_afr_msoldier2** — `ugsoldier2`, type 58, set B. R 11; 50% `acc_hat_baseball (acc_hat_baseball_tan)
to_abolt_head_t_fwd`, else `acc_hat_flop2 (acc_hat_flop2_tan)`; 25% `acc_box_hip (acc_box_hip_canteen1)` left hip; 25%
`acc_box_hip_smll (acc_box_hip_smll_brwn)` right hip. Skins c, `b/a=…_ugnsoldier2`; 1/2 (odd) `h=h_b_2_s`[2] face
`f_b_2_`[2], else `h=h_b_3_s` face `f_b_3_`. Voice (`so`, 1, 0).

**m_afr_msoldier3** — `ugsoldier3`, type 59, set B. R 18, L 22. 50% `acc_helmet2 (acc_helmet2_tan)` with parts
`−_baldhead −_comface` and `acc_goggles (acc_goggles)`; otherwise parts `−_helmethead −_helmetface` and 25%
`acc_glasses (acc_glasses_sun)`. Always `acc_tanks (acc_tanks_afr) abolt_back`. Skins c, `b/a=…_ugnsoldier3`,
`h=h_b_3_s`; face `f_b_3_`. Voice (`so`, 1, 0). Armour 1.

**m_afr_msniper** — `ugsniper`, type 55, set C (shares type 55 and its set-up with `m_x_romulan`). R 7, L 22;
`acc_hat_baseball (acc_hat_baseball_tan) to_abolt_head_t_bkwd`; 25% `acc_glasses (acc_glasses_sun)`; 25%
`acc_box_hip_smll (acc_box_hip_smll_brwn)` left hip. Skins c, `b/a=…_ugnsniper`; 1/2 (odd) `h=h_b_2_s`[1] face
`f_b_2_`[1], else `h=h_b_5_s` face `f_b_5_`. Voice (`so`, 1, 0).

**m_x_romulan** — `romulan`, type 55, set A: same type set-up as `m_afr_msniper` but model set `romulan` and the
bystander start-up set (developer test character).

**m_afr_mbrute** — `butch`, type 92, set C. R 4, L 27 (machete); 50% `acc_hat_top (acc_hat_top_wht)`. Skins c,
`b/a=…_ugbutcher`; 1/2 (odd) `h=h_b_7_s`[2] face `f_b_7_`[2], else `h=h_b_2_s` face `f_b_2_`. Voice (`so`, 1, 0).
Armour 5.

**m_afr_mrocket** — `ugrocket`, type 93, set C. R 14; `acc_faceplate (acc_faceplate_afr)`; 25% `acc_backpack_smll
(acc_backpack_smll_tan) abolt_back`. Skins c, `b/a=…_ugnrocket`, `h=h_afrrocket`, `f=f_afrrocket`. Voice (`so`, 1, 0).
Armour 5 then 3.

Kosovo (team 1; voice `so` unless stated):

**m_kos_mgrunt1** — `serbgrunt`, type 69, set B. R 11, L 24; `acc_tanks (acc_tanks_kos) abolt_back`; 25%
`acc_box_hip (acc_box_hip_canteen1)` left hip; 25% model `acc_puch_thigh` (sic; no such stock model, so it never
appears — UNVERIFIED) skin `acc_pouch_thigh_green` left thigh. Skins c, `b/a=…_kosgrunt1`, `md=md_serbgrunt1` (no
face prefix).

**m_kos_mgrunt2** — `serbgrunt2`, type 70, set B. R 6, L 24; `acc_tanks (acc_tanks_kos)`; 25% `acc_box_hip
(acc_box_hip_canteen1)` right hip; 25% `acc_puch_thigh` (skin `acc_pouch_thigh_tan`) left thigh. Skins c,
`b/a=…_kosgrunt2`, `md=md_serbgrunt2`.

**m_kos_mgrunt3** — `serbgrunt3`, type 71, set B. R 6; `acc_helmet (acc_helmet_green1)`; `acc_gas_mask2
(acc_gas_mask2a)`; 25% `acc_backpack_low (acc_backpack_low_tan) abolt_back`; 25% `acc_box_hip
(acc_box_hip_canteen1)` left hip. Skins c, `b/a=…_kosgrunt3`; 1/3 `h=h_w_3_s` face `f_w_3_` — this skin is not in the type's registration list (which
has `h_h_3_s`/`f_h_3_` at level 0 instead), so the attempt fails and the next choice is taken (UNVERIFIED, open
question 2); else 1/2 `h=h_w_5_c`[2] face `f_w_5_`[2]; else `h=h_h_6_s` face `f_h_6_`.

**m_kos_msniper1a** — `serbsniper`, type 72, set B (H with spawnflag 65536). R 7, L 24; `acc_helmet2
(acc_helmet2_green)`; `acc_gas_mask2 (acc_gas_mask2a)`; 25% `acc_goggles`. Skins c, `b/a=…_kossnipe`; 1/3 `h=h_w_2_c`[0]
face `f_w_2_`[0]; else 1/2 `h=h_w_10_s`[1] face `f_w_10_`[1]; else `h=h_h_6_s` face `f_h_6_`. Armour 1.

**m_kos_msniper1b** — `serbsniper`, type 73, set B. As `m_kos_msniper1a` without the gas mask.

**m_kos_mcomtroop** — `serbcomtroop`, type 74, set B. R 4, L 22; `acc_helmet (acc_helmet_green1)`; `acc_gas_mask2
(acc_gas_mask2a)`; `acc_backpack_lrg (acc_backpack_lrg_grn) abolt_back`; `acc_antenna (acc_antenna) abolt_back`.
Skins c, `b/a=…_koscomtroop`; 1/2 `h=h_w_19_s`[1] face `f_w_19_`[1], else `h=h_w_4_s` face `f_w_4_`.

**m_kos_mbrute1a** / **m_kos_mbrute1b** — `serbbrute`, types 75 / 76, set B. R 6, L 22; (1a only) `acc_gas_mask2
(acc_gas_mask2a)`; 10% `acc_backpack_lrg (acc_backpack_lrg_brwn) abolt_back`; 25% `acc_holster (acc_holster_brwn)`
right hip; 25% `acc_hat_beret (acc_hat_beret_red)`. Skins c, `b/a=…_kosbrute`; 1/3 `h=h_w_5_c`[1] face `f_w_5_`[1];
else 1/2 `h=h_w_3_s`[2] face `f_w_3_`[2]; else `h=h_h_6_s` face `f_h_6_`. Armour 5.

**m_kos_mmechanic** — `serbmechanic`, type 77 (innocent), set A. `acc_hat_hard (acc_hat_hard_kos)`. Skins c,
`b/a=…_kosmechanic`; 1/2 `h=hb_h_2_b`[2] face `f_hb_1_`[2] parts `−_baldhead −_comface`, else `h=h_w_4_s` face
`f_w_4_` parts `−_beardhead −_beardface`. Voice (`npc`, 1, 0).

Siberia (team 1; voice `so` unless stated):

**m_sib_mtrooper1a** — `sibtroop1a`, type 94, set B. R 6; `acc_gas_mask (acc_gas_mask)`; `acc_backpack_lrg
(acc_backpack_lrg_wht) abolt_back`; `acc_antenna (acc_antenna) abolt_back`. Skins c, `b/a=…_sibtrooper2`,
`hd=hd_sibtroop`, `f=f_sibtroop`.

**m_sib_mtrooper1b** — `sibtroop1b`, type 95, set B. R 7; `acc_gas_mask`; `acc_backpack_smll (acc_backpack_smll_wht)
abolt_back`; `acc_box_hip_smll (acc_box_hip_smll_blck)` left hip. Skins as 1a.

**m_sib_mtrooper2** — `sibtroop2`, type 67, set B. L 26, R 5; `acc_backpack_low (acc_backpack_low_dgrey)
abolt_hip_r`; `acc_box_hip_smll (acc_box_hip_smll_blck)` left hip. Skins c, `b/a=…_sibtrooper2`, `hd=hd_sibtroop`,
`f=f_sibtroop2`.

**m_sib_mguard1** — `sibguard`, type 96, set B. L 26, R 6; 50% `acc_hat_baseball (acc_hat_baseball_sib)`; 50%
`acc_box_hip (acc_box_hip_blk)` left hip. Skins c, `b/a=…_sibguard1`; 1/2 `h=h_w_10_s`[1] face `f_w_10_`[1], else
`h=h_w_16_s` face `f_w_16_`.

**m_sib_mguard3** — `sibguard3`, type 97, set B. L 26, R 5; `acc_hat_tall (acc_hat_tall_gry)`; 50% `acc_box_hip
(acc_box_hip_blk)` left hip. Skins c, `b/a=…_sibguard1`, `h=h_w_16_s`; face `f_w_16_` (talk `f_w_16_n`).

**m_sib_mguard4** — `sibguard4`, type 98, set B. L 26, R 6. Skins c, `b/a=…_sibguard4`, `h=h_w_21_s`; face `f_w_21_`.

**m_sib_mcleansuit** — `sibclean`, type 68, set B. R 3; 25% `acc_box_hip_smll (acc_box_hip_smll_blck)` right hip.
Skins c, `b/a=…_sibclean`, `md=md_clean`. Voice (`cln`, 1, 0).

**m_sib_mmechanic** — `sibmech`, type 99 (innocent), set B. `acc_hat_hard (acc_hat_hard_sib)`. Skins c,
`b/a=…_sibmechanic`; 1/2 `h=h_w_19_s`[2] face `f_w_19_`[2] parts `−_beardhead −_beardface`, else `h=hb_h_2_b` face
`f_hb_1_` parts `−_baldhead −_comface`. Voice (`npc`, 1, 0).

Tokyo (team 1 unless stated; voice `ya` unless stated):

**m_tok_mhench1** — `tokhench1`, type 86, set B. R 3; 25% `acc_glasses (acc_glasses_sun)`. Skins `b/a=…_tokhench1`;
1/3 `h=h_j_2_s`[0] face `f_j_2_`[0] part `−_crewhead` and 1/2 `−_ponytail`; else 1/2 `h=h_j_6_s`[1] face
`f_j_6_`[1] part `−_crewhead` and 1/2 `−_ponytail`; else `h=h_j_5_c` face `f_j_5_` part `−_baldhead`.

**m_tok_mhench2** — `tokhench2`, type 87, set B. R 6; 25% `acc_glasses (acc_glasses_sun)`. Skins `b/a=…_tokhench2`;
1/2 `h=h_j_6_s`[1] face `f_j_6_`[1] part `−_crewhead`; else 1/3 `h=h_j_3_s`[2] face `f_j_3_`[2] parts `−_baldhead
−_ponytail`; else `h=h_j_4_s` face `f_j_4_` parts `−_crewhead −_ponytail`.

**m_tok_mkiller** — `tokkiller`, type 88, set B. R 17. Skins `b/a=…_tokkiller`; 1/2 `h=h_j_2_s`[0] face `f_j_2_`[0]
part `−_crewhead`, else `h=h_j_5_c` face `f_j_5_` part `−_baldhead`. Armour 1.

**m_tok_mninja** — `tokninja`, type 89, set B. L 30, R 5; `acc_sword (acc_sword) abolt_back`. Skins
`b/a=…_tokninja`, `h=h_ninja`, `f=f_ninja`. Voice (`ni`, 1, 0).

**m_tok_mbrute** — `tokbrute`, type 90, set B. R 6. Skins `b/a=…_tokbrute`; 1/2 `h=h_j_3_s`[2] face `f_j_3_`[2], else
`h=h_j_4_s` face `f_j_4_`. Armour 5.

**m_tok_mman1** — `japansuit`, type 91 (team 0), set A. 25% `acc_glasses (acc_glasses_norm)`. Skins
`b/a=…_tokmhostage`; 1/2 `h=h_j_6_s`[1] face `f_j_6_`[1] parts `−_crewhead −_lbang`, else `h=h_j_5_c` face `f_j_5_`
part `−_baldhead` and 1/2 `−_lbang`. Voice (`npc`, 1, 0).

**m_tok_mman2** — `tokhostage`, type 85 (team 0, innocent), set A. 25% `acc_glasses (acc_glasses_norm)`. Skins
`b/a=…_tokman1`; 1/2 `h=h_j_2_s`[0] face `f_j_2_`[0] parts `−_crewhead −_lbang −_rbang`, else `h=h_j_1_c` face
`f_j_1_` part `−_baldhead`, 1/2 `−_lbang`, 1/2 `−_rbang`. Voice (`npc`, 1, 0).

Germany (castle raiders, team 1; voice `ra`):

**m_cas_mraider1** — `dcraider1`, type 62, set B. R 12 (¬131072); `acc_goggles`; 25% `acc_backpack_smll
(acc_backpack_smll_dgrey) abolt_back`; 33.33% `acc_box_hip (acc_box_hip_blk)` left hip; 33.33% `acc_pouch_thigh
(acc_pouch_thigh_blck)` left thigh. Skins c, `b/a=…_dcraider1`, `h=h_raider1`, `f=f_raider2`.

**m_cas_mraider2a** — `dcraider2`, type 63, set B. R 5, L 22; `acc_nightvision2 (acc_nightvision)`; 25%
`acc_backpack_smll (acc_backpack_smll_dgrey)`, else 33.33% `acc_backpack_lrg (acc_backpack_lrg_blk)` and, when that
one is attached, 50% `acc_antenna (acc_antenna)` (all `abolt_back`); 33.33% `acc_box_hip (acc_box_hip_blk)` left hip.
Skins c, `b/a=…_dcraider2`, `h=h_raider2`, `f=f_raider2`. Armour 6.

**m_cas_mraider2b** — `dcraider2b`, type 64, set B. R 9; 25% `acc_nightvision2` (skin `acc_acc_nightvision`, sic);
back items, hip box and skins as `m_cas_mraider2a`. Armour 6.

**m_cas_mbrute** — `dcraiderbrute`, type 65, set B. R 17, L 22. Skins c, `b/a=…_dcbrute`, `h=h_raider4`,
`f=f_raider4`. Armour 5.

**m_cas_mrocket** — `dcraiderrocket`, type 66, set B. R 14; `acc_faceplate2 (acc_faceplate2)`. Skins c,
`b/a=…_casrocket`, `h=h_raider3`, `f=f_raider3`. Armour 5 then 3.

Bosses `m_x_mraiderboss1`, `m_x_mraiderboss2`, `m_x_miraqboss`: SPEC-732.

### 10. Common facts
- Health: 100 for every human type (1000 with `ai_goretest`), multiplied by 1.5 (armour 4) or 2 (armour 5); no
  type sets health otherwise. Mass 200. Team and innocence: SPEC-700 type table.
- Accessory scales: ecto and female 1.0 (glasses, briefcase) or 1.125 (hats, holsters, pouches); meso 1.14286
  except where stated.
- Spawnflag 131072 ("unarmed"): honoured only by the types marked (¬131072); it also blocks the weapon drop on the
  `dropweapon` note (SPEC-722).
- Combat profile per type: SPEC-700 tables, with weapon classes per §6.

### 11. Summary table (all human classnames)
Model = `enemy/<directory>/<set name>` (SPEC-730 §3 for the level geometry); team from SPEC-700; R/L = hand attack ids
(§5; some depend on spawnflag 131072, see the entry); armour types as SPEC-722 (applied in order); health = starting and
maximum health before skill (×1.5 for armour 4, ×2 for armour 5; 1000 with `ai_goretest`); profile = SPEC-700
"Per-type profile table" as tier / weapon class, or its explicit rows. Bosses: SPEC-732.

| classname | type | model | team | start-up set | R | L | armour | health | profile |
|---|---:|---|---:|---|---|---|---|---:|---|
| `m_nyc_eskinhead3` | 15 | `ecto/skinleader` | 3 | B | 3 | — | — | 100 | 3 / 3 |
| `m_nyc_estockbroker` | 16 | `ecto/stockbroke / stockbroke2` | 0 | A | — | — | — | 100 | explicit |
| `m_nyc_etourist` | 17 | `ecto/tourist` | 0 | A | — | — | — | 100 | explicit |
| `m_nyc_ebum` | 18 | `ecto/bum` | 0 | A | — | — | — | 100 | explicit |
| `m_irq_eman1` | 21 | `ecto/irqcitizen` | 1 | A | — | — | — | 100 | explicit |
| `m_afr_ecommander` | 22 | `ecto/commander` | 1 | C | 4 | — | — | 100 | 4 / 4 |
| `m_kos_eofficer` | 23 | `ecto/serbofficer` | 1 | C | 4 | — | — | 100 | 0 / 4 |
| `m_kos_erefugee` | 24 | `ecto/kosrefugee` | 0 | C | 4 | — | — | 100 | 0 / 4 |
| `m_irq_eofficer` | 25 | `ecto/iraqofficer` | 1 | C | 4 | — | — | 100 | 2 / 4 |
| `m_afr_eworker` | 26 | `ecto/factory` | 1 | B | 4 | — | — | 100 | 2 / 4 |
| `m_sib_eofficer` | 28 | `ecto/sibsuit1 / sibsuit2` | 1 | C | 3 | — | — | 100 | 1 / 3 |
| `m_sib_escientist1` | 29 | `ecto/sibscience1 / sibscience2` | 1 | C | — | — | — | 100 | explicit |
| `m_nyc_epunk` | 30 | `ecto/zitpunk` | 3 | B | 3 | — | — | 100 | 3 / 3 |
| `m_x_mcharacter` | 31 | `meso/john` | 0 | B | 3 | — | — | 100 | explicit |
| `m_x_mcharacter_snow` | 32 | `meso/johnsnow` | 0 | B | 3 | — | — | 100 | explicit |
| `m_x_mcharacter_desert` | 33 | `meso/johndesert` | 0 | B | 3 | — | — | 100 | explicit |
| `m_x_mmerc` | 34 | `meso/hawk` | 0 | B | 5 | — | — | 100 | explicit |
| `m_x_mhurtmerc` | 35 | `meso/hawk` | 0 | B | — | — | — | 100 | explicit |
| `m_x_msam` | 36 | `meso/sam` | 0 | B | — | — | — | 100 | explicit |
| `m_irq_moilworker` | 37 | `meso/irqworker` | 1 | A | — | — | — | 100 | explicit |
| `m_nyc_mpunk` | 38 | `meso/nypunk` | 3 | B | 11 | — | — | 100 | explicit |
| `m_x_miraqboss` | 39 | `meso/amu` | 1 | C | — | — | — | 100 | explicit |
| `m_x_mraiderboss1` | 40 | `meso/dekker1` | 1 | C | 4 | — | 7 | 100 | explicit |
| `m_x_mraiderboss2` | 41 | `meso/dekker2` | 1 | G | 29 | 22 | 5 then 7 | 200 | explicit |
| `m_irq_msoldier1` | 42 | `meso/irqsoldier1` | 1 | B | 7 | 22 | — | 100 | 2 / 7 |
| `m_irq_msoldier2a` | 43 | `meso/irqsoldier2` | 1 | B | 6 | — | — | 100 | 2 / 6 |
| `m_irq_msoldier2b` | 44 | `meso/irqsoldier2b` | 1 | B | 12 | — | — | 100 | 2 / 12 |
| `m_irq_mrepguard1a` | 45 | `meso/irqrepgd` | 1 | B | 6 | 22 | — | 100 | 2 / 6 |
| `m_irq_mrepguard1b` | 46 | `meso/irqrepgdb` | 1 | B | 12 | 24 | — | 100 | 2 / 12 |
| `m_irq_mpolice` | 47 | `meso/irqpolice` | 1 | C | 4 | — | — | 100 | 2 / 4 |
| `m_irq_mcommander` | 48 | `meso/irqcommand` | 1 | C | 4 | — | — | 100 | 2 / 4 |
| `m_irq_mbrute1a` | 49 | `meso/irqbrute` | 1 | C | 6 | 22 | 5 | 200 | 2 / 6 |
| `m_irq_mbrute1b` | 50 | `meso/irqbruteb` | 1 | C | 18 | — | 4 | 150 | 2 / 18 |
| `m_irq_mbodyguard` | 51 | `meso/irqbguard` | 1 | C | 7 | 22 | 1 | 100 | 2 / 7 |
| `m_irq_mrocket` | 52 | `meso/irqrocket` | 1 | C | 14 | — | 5 then 3 | 200 | 2 / 14 |
| `m_irq_msaddam` | 53 | `meso/saddam` | 1 | C | — | — | — | 100 | explicit |
| `m_irq_mman2` | 54 | `meso/irqman2` | 1 | C | — | — | — | 100 | explicit |
| `m_afr_msniper` | 55 | `meso/ugsniper` | 1 | C | 7 | 22 | — | 100 | 4 / 7 |
| `m_x_romulan` | 55 | `meso/romulan` | 1 | A | 7 | 22 | — | 100 | 4 / 7 |
| `m_afr_msoldier1a` | 56 | `meso/ugsoldier1` | 1 | B | 3 | — | — | 100 | 4 / 3 |
| `m_afr_msoldier1b` | 57 | `meso/ugsoldier1b` | 1 | B | 11 | 22 | — | 100 | 4 / 11 |
| `m_afr_msoldier2` | 58 | `meso/ugsoldier2` | 1 | B | 11 | — | — | 100 | 4 / 11 |
| `m_afr_msoldier3` | 59 | `meso/ugsoldier3` | 1 | B | 18 | 22 | 1 | 100 | 4 / 18 |
| `m_nyc_mswat` | 60 | `meso/nyswatguy` | 0 | B (H with 65536) | 3 | — | 1 | 100 | 3 / 3 |
| `m_nyc_mswatleader` | 61 | `meso/nyswatlead` | 0 | B | 3 | — | 1 | 100 | 3 / 3 |
| `m_cas_mraider1` | 62 | `meso/dcraider1` | 1 | B | 12 | — | — | 100 | 6 / 12 |
| `m_cas_mraider2a` | 63 | `meso/dcraider2` | 1 | B | 5 | 22 | 6 | 100 | 6 / 5 |
| `m_cas_mraider2b` | 64 | `meso/dcraider2b` | 1 | B | 9 | — | 6 | 100 | 6 / 9 |
| `m_cas_mbrute` | 65 | `meso/dcraiderbrute` | 1 | B | 17 | 22 | 5 | 200 | 6 / 16 |
| `m_cas_mrocket` | 66 | `meso/dcraiderrocket` | 1 | B | 14 | — | 5 then 3 | 200 | 6 / 14 |
| `m_sib_mtrooper2` | 67 | `meso/sibtroop2` | 1 | B | 5 | 26 | — | 100 | 1 / 5 |
| `m_sib_mcleansuit` | 68 | `meso/sibclean` | 1 | B | 3 | — | — | 100 | 1 / 3 |
| `m_kos_mgrunt1` | 69 | `meso/serbgrunt` | 1 | B | 11 | 24 | — | 100 | 0 / 11 |
| `m_kos_mgrunt2` | 70 | `meso/serbgrunt2` | 1 | B | 6 | 24 | — | 100 | 0 / 6 |
| `m_kos_mgrunt3` | 71 | `meso/serbgrunt3` | 1 | B | 6 | — | — | 100 | 0 / 6 |
| `m_kos_msniper1a` | 72 | `meso/serbsniper` | 1 | B (H with 65536) | 7 | 24 | 1 | 100 | 0 / 7 |
| `m_kos_msniper1b` | 73 | `meso/serbsniper` | 1 | B | 7 | 24 | 1 | 100 | 0 / 7 |
| `m_kos_mcomtroop` | 74 | `meso/serbcomtroop` | 1 | B | 4 | 22 | — | 100 | 0 / 4 |
| `m_kos_mbrute1a` | 75 | `meso/serbbrute` | 1 | B | 6 | 22 | 5 | 200 | 0 / 6 |
| `m_kos_mbrute1b` | 76 | `meso/serbbrute` | 1 | B | 6 | 22 | 5 | 200 | 0 / 6 |
| `m_kos_mmechanic` | 77 | `meso/serbmechanic` | 1 | A | — | — | — | 100 | explicit |
| `m_kos_mrebel` | 78 | `meso/kosrebel` | 0 | B | 6 | — | — | 100 | 0 / 6 |
| `m_kos_mklaleader` | 79 | `meso/kosklaguy` | 0 | B | 6 | — | — | 100 | 0 / 6 |
| `m_nyc_mskinhead1` | 80 | `meso/skinhead1` | 3 | B | 11 | — | — | 100 | 3 / 11 |
| `m_nyc_mskinhead2a` | 81 | `meso/skinhead2` | 3 | B | 3 | — | — | 100 | 3 / 3 |
| `m_nyc_mskinhead2b` | 82 | `meso/skinhead2b` | 3 | B | 6 | — | — | 100 | 3 / 6 |
| `m_x_mskinboss` | 83 | `meso/skinboss` | 3 | C | 4 | — | — | 100 | 3 / 4 |
| `m_nyc_mpolitician` | 84 | `meso/politician` | 0 | A | — | — | — | 100 | explicit |
| `m_tok_mman2` | 85 | `meso/tokhostage` | 0 | A | — | — | — | 100 | explicit |
| `m_tok_mhench1` | 86 | `meso/tokhench1` | 1 | B | 3 | — | — | 100 | 5 / 3 |
| `m_tok_mhench2` | 87 | `meso/tokhench2` | 1 | B | 6 | — | — | 100 | 5 / 6 |
| `m_tok_mkiller` | 88 | `meso/tokkiller` | 1 | B | 17 | — | 1 | 100 | 5 / 16 |
| `m_tok_mninja` | 89 | `meso/tokninja` | 1 | B | 5 | 30 | — | 100 | 5 / 5 |
| `m_tok_mbrute` | 90 | `meso/tokbrute` | 1 | B | 6 | — | 5 | 200 | 5 / 6 |
| `m_tok_mman1` | 91 | `meso/japansuit` | 0 | A | — | — | — | 100 | explicit |
| `m_afr_mbrute` | 92 | `meso/butch` | 1 | C | 4 | 27 | 5 | 200 | 4 / 4 |
| `m_afr_mrocket` | 93 | `meso/ugrocket` | 1 | C | 14 | — | 5 then 3 | 200 | 4 / 14 |
| `m_sib_mtrooper1a` | 94 | `meso/sibtroop1a` | 1 | B | 6 | — | — | 100 | 1 / 6 |
| `m_sib_mtrooper1b` | 95 | `meso/sibtroop1b` | 1 | B | 7 | — | — | 100 | 1 / 7 |
| `m_sib_mguard1` | 96 | `meso/sibguard` | 1 | B | 6 | 26 | — | 100 | 1 / 6 |
| `m_sib_mguard3` | 97 | `meso/sibguard3` | 1 | B | 5 | 26 | — | 100 | 1 / 5 |
| `m_sib_mguard4` | 98 | `meso/sibguard4` | 1 | B | 6 | 26 | — | 100 | 1 / 6 |
| `m_sib_mmechanic` | 99 | `meso/sibmech` | 1 | B | — | — | — | 100 | explicit |
| `m_nyc_fskinchick` | 100 | `female/skinchick` | 3 | B | 3 | — | — | 100 | 3 / 3 |
| `m_nyc_fwoman` | 101 | `female/nywoman` | 0 | A | — | — | — | 100 | explicit |
| `m_sib_fguard2` | 102 | `female/sibguard` | 1 | B | 5 | — | 1 | 100 | 1 / 5 |
| `m_sib_fscientist2` | 103 | `female/sibscience` | 1 | A | — | — | — | 100 | explicit |
| `m_irq_fwoman1` | 104 | `female/irqwoman1` | 1 | A | — | — | — | 100 | explicit |
| `m_irq_fwoman2` | 105 | `female/irqwoman2` | 1 | A | — | — | — | 100 | explicit |
| `m_tok_fwoman1` | 106 | `female/tokwoman1` | 0 | A | — | — | — | 100 | explicit |
| `m_tok_fwoman2` | 107 | `female/tokwoman2` | 0 | A | — | — | — | 100 | explicit |
| `m_tok_fassassin` | 108 | `female/tokassassin` | 1 | B | 3 | 30 | — | 100 | 5 / 3 |
| `m_cas_ffemale` | 109 | `female/raider` | 1 | B | 18 | 26 | 7 | 100 | 6 / 18 |
| `m_x_ftaylor` | 110 | `female/taylor` | 0 | B | 6 | — | 7 | 100 | explicit |

## Constants

| item | value |
|---|---|
| model transform | rotation −90° about z, then uniform scale = size scale |
| size scale | 1.0; 1.1 for `m_x_mskinboss`, `m_x_mraiderboss1`, `m_x_mraiderboss2` |
| registration level that always registers | 3 |
| variety test | level > `mskins_variety_limit` − 0.1 |
| expression tests | `p`, `t1`: limit − 0.1 < 2; `m`, `f`, `t2`: < 1; `b`, `t3`: < 0 |
| fatal message | `Couldn't find enemy model %s/%s` |
| level-model warnings | `WARNING: no level .ghb for ecto--using ecto.ghb!`, `WARNING: no level .ghb for female--using female.ghb!` |

## Test vectors / how to verify
1. On `kos1` spawn `m_kos_mgrunt1` (`gimme m_kos_mgrunt1`) 20 times: every one has the `acc_tanks_kos` tank on its back,
   a shotgun, skins `b_kosgrunt1`/`a_kosgrunt1`; about a quarter have a canteen box on the left hip; none shows a
   thigh pouch (misspelled model name).
2. Same map with `mskins_variety_limit 3` before the map loads: `m_kos_mbrute1a` always has head `h_h_6_s`; with 0,
   about a third use `h_w_5_c`.
3. `mskins_expression_limit 1` then load `tsr1`: civilians never blink (no `…b` face skins registered); pain faces
   still appear.
4. On `irq1a` the console shows no level-model warning for `m_irq_msoldier1` (model `meso_irq1a.ghb`); on `kos2` an
   `m_kos_erefugee` prints `WARNING: no level .ghb for ecto--using ecto.ghb!`.
5. `m_x_romulan` carries a sniper rifle and grenades (type 55 set-up) but never starts combat behaviour on its own
   (bystander set A) — UNVERIFIED in game.
6. Spawn `m_x_mcharacter` with spawnflag 131072: no pistol in the hand, the calf holster is still present.
7. Kill an `m_irq_mrocket`: health was 200 (armour 5) and the zones carried heavy armour (armour 3): an armour pickup
   (≥ 50) drops when its death animation ends (SPEC-722).

## Open questions
1. (closed: decision kinds are specified in SPEC-710.)
2. Whether a variant skin that is not in the type's own registration list (e.g. `h_w_3_s` for `m_kos_mgrunt3`) can
   still be applied when another character type registered it on the same model object (verify on `kos2` with both
   types present).
3. Why the meso family has no level-0 model in the stock data (`meso.ghb`); behaviour on custom maps without `ainame`.
4. Unused types 19, 20 and 27 have set-ups but no classname (possibly removed Colombia characters: `colmaleprisoner`,
   `colchemist` skins).
