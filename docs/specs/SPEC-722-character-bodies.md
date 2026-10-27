# SPEC-722: Character bodies (human families, animation choice, voices, faces, deaths)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-001 (game imports `sound`, `soundindex`, `effectindex`, `irand`, `flrand`, `FS_FileExists`),
SPEC-003 (player weapon numbers), SPEC-006 (character classnames), SPEC-010 / SPEC-011 (Ghoul model object and
instance: parts, skins, bolts, notes, Play, Pause, speed index), SPEC-120 / SPEC-121 (model node and sequence
names), SPEC-201 (temp-entity effects), SPEC-202 (entity effect slots), SPEC-303 (savegame chunks), SPEC-720 (damage model, damage flags, hit handling), SPEC-721 (gore
zones, wounds, severing, gibs, violence locks).

## Purpose

Every character in the game module owns a *body*: the object that knows how that kind of character is modelled
(SPEC-722 common part, section "Non-human bodies (vehicles, animals) and corpse handling", describes the body
abstraction, the animation-record tables, playback, turning, bolt-ons, animal and vehicle bodies and corpse
lifetime). This part specifies the **human bodies** — male soldier ("meso"), civilian/officer ("ecto"), female and
the armoured male boss — which are used by every human enemy and civilian and by player characters: how a
character type configures its body, how the body chooses every animation (movement, idle, aim, fire, reload,
dodge, jump, posture change, hit reactions, deaths and dying), how it handles hand-held weapons, armour, voices
and facial expressions, and which sounds and effects it precaches. Hit location, damage and gore are in SPEC-720
and SPEC-721; this spec only references them.

## Interface / layout

### Human body families
| family | model directory | used by | differences from the generic human body |
|---|---|---|---|
| male soldier ("meso") | `enemy/meso` | every `m_*_m*` character except the two raider bosses; male player models | own gore-zone/part table (SPEC-721) |
| civilian / officer ("ecto") | `enemy/ecto` | every `m_*_e*` character | own gore-zone/part table; the right hand has no open/closed variants (no hand switching) |
| female | `enemy/female` | every `m_*_f*` character; player models whose base model is `fem_play` | own gore-zone/part table |
| armoured male soldier | `enemy/meso` | `m_x_mraiderboss1`, `m_x_mraiderboss2` | male soldier with its own hit handling (SPEC-720) and its own armour points (Armour below) |

All four share the behaviour below. Every human body has 26 gore zones (SPEC-721); several rules below refer to
them by their SPEC-721 names (`GZ_HEAD_FRONT` … `GZ_GUT_BACK_EXTRA`).

### Character set-up (what a character type tells its body at spawn)
A character type (SPEC-006 classnames; per-type values belong to the enemy-roster spec) configures its body with
these requests, in this order in the original: right-hand weapon, left-hand item, accessories (common part),
face skin prefix, voice, precache, armour.

| request | arguments | effect |
|---|---|---|
| right-hand weapon | attack type (table "Attack types and weapon models") | removes any bolt-on on `wbolt_hand_r`, attaches the weapon model (directory `Enemy/Bolt`, scale 1.0) by its own bolt `to_wbolt_hand_r` (`_to_wbolt_hand_r` for the auto-shotgun and the MPG) and registers the matching player weapon (SPEC-003) for precaching. On success the attack type is remembered as the *right-hand type*; unknown types or a failed attachment leave the right-hand type 0 (unarmed) |
| left-hand item | attack type | stored as the *left-hand type*. Type 27 attaches `w_machete` (directory `Enemy/Bolt`, bolt `to_wbolt_hand_l`) to `wbolt_hand_l`. Any other type is kept only if the model has one of the throw sequences `std_tgrenade_n_a_n`, `cch_tlobgrenade_n_a_n`, `std_tstar_fwd_a_n`, otherwise it is reset to 0 |
| face skin prefix | string (e.g. `f_w_12_`, `f_m_3_`, `f_saddam_`) | enables facial expressions (Face expressions). Empty = no face animation |
| voice | voice name, voice count n, death-voice type t, custom death-voice directory | voice directory := `Enemy/<name>` (empty name = mute character); voice number := irand(1, n) (exactly n while a pak list is being built, cvar `fs_createpak` non-zero); death-voice type t (0 by level, 5 female, 7 custom directory) |
| precache | — | Precache below; also fills the clip and sets up gore zones (SPEC-721) and model events |
| armour | armour type 0–7 | Armour below |

Voice names used by the shipped character types (for orientation; per-type data is roster data): `so`, `sk`,
`skf`, `ra`, `raf`, `npc`, `npcf`, `jm`, `ya`, `sw`, `hawk`, `cln`, `ni`, `sam`, `taylor`, `soldier`, `skinhead`,
with counts 1 or 2 and death-voice types 0 or 5.

### Attack types and weapon models (right hand)
| attack type | model on `wbolt_hand_r` | player weapon cached | weapon class |
|---|---|---|---|
| 1, 2 | `w_knife` | 1 | K (type 1), none (type 2) |
| 3 | `w_pistol1` | 3 | P |
| 4 | `w_pistol2` | 2 | P |
| 5 | `w_machinepistol` | 4 (not cached for classname `m_x_mmerc`) | MP |
| 6 | `w_assault_rifle` | 5 | M |
| 7, 8 | `w_sniperrifle` | 6 | R (type 7), none (type 8) |
| 9, 10 | `w_autoshotgun` | 7 | S (type 9), none (type 10) |
| 11 | `w_shotgun` | 8 | S |
| 12, 13 | `w_machinegun` | 9 | M (type 12), none (type 13) |
| 14 | `w_rocket` | 10 | L |
| 16, 17, 29 | `w_mpg` | 11 | S |
| 18, 19 | `w_flamethrower` | 12 | M (type 18), none (type 19) |
| 0, 15, 20–28 | nothing | — | none |

The **weapon class** selects animation variants. As a bit set: P = 1, L = 2, R = 4, S = 8, M = 16, K = 32,
MP = 64. Sequence-name weapon tokens and the classes that use them:

| token in sequence names | chosen for weapon classes |
|---|---|
| `p` (`_p_2`, `_p_a`, `_p_n`, `_p_cb`, `_p_cbn`) | P, MP (some idle/aim/walk lists also K) |
| `r` (`_r_2`) | R |
| `ms` (`_ms_2`) | M, S |
| `mrs` (`_mrs_2`) | M, R, S |
| `m` / `s` (`_m_2`, `_s_2`, fire only) | M / S |
| `l` (`_l_2`) | L |
| `pk` (`_pk_n`, `_pk_a`) | K and unarmed (fallback) |
| `a`, `n` | any / no weapon |

### Sequence-name convention (orientation only)
Names follow `<posture>_<action><detail>_<direction>_<weapon>_<variant>`: posture `std` standing, `cch`
crouched, `prn` prone, `lb` lying on the back, `lf` lying face down, `ls` lying on the side, `si` sitting, `a4`
on all fours; action letter `i` idle, `w` walk, `r` run, `a` aim, `f` fire, `l` reload, `t` throw, `m` melee,
`j` jump, `x` special/transition, `s` struck (hit reaction or death), `p` lasting injury (limp, hop, blinded,
writhe), `h` lean (hide), `e` scripted event, `c` crawl. Direction tokens: `n`, `fwd`, `bk`, `lt`, `rt`, `up`,
`dwn`, `2frnt`/`2bk`/`2side` (falls towards), `to`/`mid`/`frm` (enter/loop/leave). The convention is not parsed
by the game: only exact names matter (UNVERIFIED for the meaning of the variant token).

### Animation records
All animations are records of the human animation table (common part, appendix "human animation table";
fields: class, posture, group, flags, yaw limits, start offset). Terms used below: *class* (0 general, 1
locomotion, 2 attack, 3 jump, 4 fall, 5 hit reaction, 6 death), *posture* (standing, crouched, prone, sitting,
face down, on back, on side), *group* (3 idle fidget, 2 blinded, …), *available* = the model has the sequence
`ghoul/<model directory>/<name>.ghl`. A request "play the first available of A, B, C" means: the first listed
sequence the model has; if none, the request is refused and the caller falls back as described.

## Behaviour

### Playing an animation (human additions to the common playback)
Before the common playback rules (common part), a human body:
1. Re-snaps the character to the floor when the request restarts, changes the record or a replay was flagged
   (SPEC-721 / common part: ground bolt).
2. Shows the right hand open when the record has the right-hand-open flag (value 0x2000), else closed; shows the
   left hand open when it has the left-hand-open flag (0x4000). Open/closed are part pairs that are swapped
   (hide one, show the other) only if the currently shown one exists and is visible: right `_ropen`/`_rclosed`,
   `_rbossopen`/`_rbossclosed`, `_rhand_open`/`_rhand_closed`; left `_lopen`/`_lclosed`,
   `_lbossopen`/`_lbossclosed`, `_lhand_open`/`_lhand_closed`. The civilian family never swaps the right hand.
3. Grenade-in-hand flag (bit 28; only on `std_tgrenade_n_a_n`): attaches `Items/Projectiles` model `grenade`
   by `to_wbolt_hand_l` to `wbolt_hand_l` while the flag is on; removes it when a record without the flag is
   played.
4. Leak flag (bit 29; only on `std_ipiss_n_a_a`): effect `environ/takeleak` on bolt `gbolt_groin`, unless
   `lock_textures` is set (SPEC-721).
All animation choices below set the Ghoul playback speed index to 4 (normal speed) first, unless stated.

### Movement and standing choice
The AI asks the body for the animation that fits the current situation; the body answers with the first
available sequence of the applicable list. Inputs: posture of the current record, weapon class of the right-hand
type, movement direction relative to facing (dot products), speed, whether the character has a hostile armed
target, expression state, injury state.

Common pre-emption (movement, standing, attack and jump choices):
1. A sequence explicitly requested by the caller wins if available.
2. Characters with spawnflag 0x80 never use locomotion: the standing choice is used instead.
3. *Reaction override* (next section) wins if it returns something.
4. *Posture change* wins if the requested posture differs from the current one.
5. Limp: with the right-leg limp state `std_plimp_lt_n_n`, with the left-leg limp state `std_plimp_rt_n_n`
   (only characters with an AI).
6. Current posture prone (or class "fall" record ending prone): `prn_a_n_mrs_2`.
7. Current posture "on side": keep the current record.

Then, per situation (lists are in preference order; `x|y` = alternatives by weapon class as in the token table):

| situation | sequences |
|---|---|
| crouched, moving away from the target (dot ≤ 0) with a target | `cch_raim_fwd_mrs_2` (M/R/S), `cch_raim_n_l_2` (L), `cch_r_n_pk_n` (K), `cch_raim_fwd_p_2` |
| crouched, moving towards | same list (second set of records), then `cch_a_fwd_r_2` (R), `cch_a_fwd_ms_2` (M/S), `cch_a_fwd_l_2` (L), `cch_a_fwd_p_2` (P/MP) |
| standing still (speed·facing term < 10) | turn in place `std_wturn90_lt_*`/`std_wturn90_rt_*` (`_mrs_2` for M/R/S else `_pk_n`), `std_wturn180_lt_mrs_2` for a near-reverse turn (speed < 0.1, 0 < turn < 10) |
| walking backwards (dot < −0.75) | `std_wbackaim_n_r_2` / `_ms_2` / `_l_2` / `_p_2`; unarmed `std_wnormal_n_pk_a` |
| walking | aimed: `std_waim_fwd_p_2` (P/MP/K), `_r_2`, `_ms_2`, `_l_2`; relaxed: `std_wpatrol_n_mrs_2` (M/R/S), `std_w_n_l_2` (L), `std_wattitude_n_pk_a`, `std_wnormal_n_pk_a`; jog: `std_wjog_n_mrs_2`, `std_wjoggunup_n_p_a` |
| running | `std_r_n_mrs_2` (M/R/S), `std_rgunup_n_p_2` (P/MP), `std_r_n_pk_n`; aimed: `std_raim_n_mrs_2`, `std_raim_n_p_2`, `std_raim_n_p_n` (P/MP/K), `std_raim_n_l_2`; running backwards: `std_rbackaim_n_mrs_2`, `_p_2`, `_l_2`; afraid unarmed (expression 2): `std_rafraid_n_n_n` |
| strafing while running and aiming (right / left) | `std_raimstraf_rt_mrs_2` / `_lt_`, `_l_2`, `_p_cbn` |
| strafing while walking and aiming | `std_waimstraf_rt_ms_2` / `_lt_`, `_r_2`, `_l_2`, `_p_2` |
| standing idle, unarmed | 1 in 6: `std_ilooking_n_a_a`; otherwise `std_ishift_n_a_n` or 1 in 3 `std_istretch_n_a_a`; `std_istand_n_a_n` |
| standing idle, M/R/S | 1 in 5 `std_ilooking_n_mrs_2`, 1 in 3 `std_istretch_n_mrs_2`, `std_istand_n_mrs_2` (a fidget is never repeated back to back) |
| standing alert (has target, not aiming) | M/R/S: 1 in 13 `std_ialertlk_rt_mrs_2`, 1 in 11 `std_ialerttrn_lt_mrs_2`, else `std_ialert_n_mrs_2`; P/K: 1 in 15 `std_ialertlk_lt_p_n`, 1 in 9 `std_ialerttrn_rt_p_n`, else `std_ialert_n_p_n` |
| standing, aiming at target | above/below: `std_a_up_*` / `std_a_dwn_*` (`_p_2`, `_mrs_2`, `_l_2`); level: `std_a_fwd_ms_2`, `std_a_fwd_r_2`, `std_a_fwd_p_2`, `std_a_fwd_p_cb`, `std_a_fwd_l_2`; side-stepping: `std_ashuffle_n_ms_2`, `_r_2`, `_p_2`, `_p_a`, `_l_2`; between bursts (14 in 15): `std_xatkpause1_n_mrs_2` / `std_xatkpause2_n_mrs_2` (`_pk_n` variants for P/K) |
| afraid (expression state 2) | `std_xafraidshake_n_n_n`; 1 in 11 `std_xfearwave_n_n_n`; crouched: `cch_xcchcower_mid_a_a`, `cch_xcchcower2_mid_a_a` |
| lying on the back, wounded | `lb_pcrawl_bk_p_n` (crawl), `lb_xleanupshoot_n_p_n` (shoot from the ground), `lb_sautofire_end_n_n` |

The random fidget choices use a counter taken from the game clock; probabilities are approximate (UNVERIFIED:
compare a long idle recording).

### Attack choice
For an armed character with a target (lists in preference order):
- Crouched: fire `cch_f_fwd_m_2` (M), `cch_f_fwd_r_2` (R), `cch_f_fwd_s_2` (S), `cch_f_fwd_p_2` (P), `cch_a_fwd_p_2`
  (MP), `cch_f_fwd_l_2` (L); knife `cch_rswipe_n_k_n`; grenade `cch_tlobgrenade_n_a_n`; otherwise the crouched aim
  list.
- Standing: knife or pistol at melee range `std_mslash_n_kp_n`, running knife attack `std_rswipe_n_k_n`; fire
  `std_fautosweep_n_m_2` or `std_f_fwd_m_2` (M), `std_f_fwd_r_2` (R), `std_f_fwd_s_2` (S), `std_f_fwd_l_2` (L),
  pistol `std_f_n_p_2`, `std_f_fwd_p_2`, `std_f_fwd_p_cb`, occasionally `std_fattitude_n_p_a`; machine pistol
  `std_a_fwd_p_2`; aiming up/down `std_a_up_*` / `std_a_dwn_*`; throwing `std_tgrenade_n_a_n`,
  `std_tstar_fwd_a_n`.
- While moving: the aimed walk/run/back-aim lists of the movement table.

### Other choices
| request | rule |
|---|---|
| jump | a sequence named by the caller; else reaction override; else posture change; else `std_jstraitup_up_l_2` (L, when asked for a high jump), `std_jstraitup_up_a_a`, `std_jupto_n_a_a`; else the standing choice |
| dodge | left/right/back/forward as asked: `std_xroll_lt_a_a`, `std_xroll_rt_a_a`, `std_jbackflip_n_p_n`, `std_xrolltocch_fwd_a_a`, `std_jdive2prone_n_a_n` |
| posture change | to crouch: `std_xcrouch_dwn_p_a` (P/MP), `std_xcchcower_dwn_a_a` (unarmed), `std_xrolltocch_fwd_a_a` (moving faster than 2), `std_xcrouch_dwn_mrs_2`, `std_xcrouch_dwn_l_2`, `std_xcrouch_dwn_pk_a`; to prone `std_xgoprone_n_a_a`; up from crouch `cch_xcchcower_up_a_a`, `cch_xcrouch_up_mrs_2`, `cch_xcrouch_up_l_2`, `cch_xcrouch_up_pk_a`; up from prone `prn_xtostand_n_a_a`; up from the back `lb_xgetup_n_a_n` (plays the general voice "Getup", 1 variant, volume 0.8) — standing up only when spawnflag 0x80 is clear |
| reload | by weapon class and motion: standing `std_l_n_<w>_2` (`std_l_n_p_n` fallback), walking `std_lwalk_fwd_`, `std_lwalk_bk_`, `std_lwalkstraf_lt_`/`_rt_`, running `std_lrun_fwd_`, `std_lrun_bk_`, `std_lrunstraf_lt_`/`_rt_` (each `<w>` ∈ `ms_2`, `p_2`, `r_2`, `l_2`; direction chosen when the dot product with forward/right exceeds 0.5 or is below −0.5), crouched `cch_l_fwd_<w>`, `cch_lrun_fwd_<w>`, `cch_lrun_bk_<w>`; starting a reload refills the clip |
| cock after a shot | standing, shotgun (attack type 11) `std_xcock_n_s_2`; standing, sniper rifle (7) `std_xcock_n_r_2`; crouched shotgun `cch_xcock_n_s_2` |

### Reaction override (checked before movement, standing, attack and jump choices)
In order; the first that applies is played:
1. **Blinded**: when the AI reports a flash-blindness level ≥ 20: `std_pblinded_to_a_n` (unless already in the
   blinded group), then `std_pblinded_loop_a_n`; when the level drops while a blinded-group record plays:
   `std_pblinded_frm_a_n`.
2. **Limb injury hop / clutch** (only at skill levels 0–3; at skill 4 the injury state is cleared): while the
   injury state set by a hit (SPEC-720) is active and has not expired, standing: right leg →
   `std_phop_lt_n_n`, left leg → `std_phop_rt_n_n`, right arm → `std_parm_rt_n_n`, left arm →
   `std_parm_lt_n_n`; entering one of them plays the general voice "Writhe" (7 variants, volume 0.8).
3. **Startled**: the current record is an idle fidget (group 3), standing, and the AI has just noticed an
   enemy: the AI is notified, the wake voice is played (volume 0.9) unless a wake voice played in the last 7 s,
   and `std_xstartled_n_a_a` is played.
4. **Reload / cock**: an armed, non-ducking character with an empty clip reloads (reload choice); right after a
   shot the cock choice is tried.
5. After `std_xstartled_n_a_a` has finished, an armed character reloads with probability 1/2.

### Hit reactions (non-fatal hits)
Requested by the hit handling (SPEC-720) with the hit point and damage, only while the current record's posture
is standing:
1. Arm hit pending (a hit on an arm or hand zone, SPEC-720): if the character's armour is ≤ 0.5 % or its health is
   ≤ half its maximum, and the AI is not in a no-disarm state: `std_sgunfromhand_n_n_n` with general voice
   "Disarm" (1 variant) — the weapon flies out of the hand (SPEC-721 bolt-on drop). Otherwise `std_sarm_rt_n_n`.
2. Injury state active: right leg `std_sleg_rt_n_n` (voice "Scream", 8 variants) else `std_slegunhurt_rt_a_n`;
   left leg `std_sleg_lt_n_n` (Scream 8) else `std_slegunhurt_lt_a_n`; right arm `std_sarm_rt_n_n` (Scream, 5
   variants) else `std_sarmunhurt_rt_a_n`; left arm `std_sarm_lt_n_n` (Scream 8) else `std_sarmunhurt_lt_a_n`.
3. Hit point in front of the character (dot(forward, direction to hit point) ≥ 0): if damage > 75, with
   probability 1/6, when both `std_sblown_2bk_n_n` and `lb_xgetup_n_a_n` are available and the armour type is not
   3: `std_sblown_2bk_n_n` with Scream (knocked down; gets up later with `lb_xgetup_n_a_n`). Otherwise
   `std_sunhurt_bk_mrs_2`, `std_sunhurt_bk_pk_n`.
4. Hit from behind: `std_sunhurt_fwd_mrs_2`, `std_sunhurt_fwd_pk_n`.
A record is "in a hit reaction" while it is one of `std_sstumble_2frnt_n_n`, `std_sviolent_n_n_n`,
`std_sblown_2bk_n_n` (second record), `std_sstumble_2bk_p_n`, `std_sinback_2frnt_n_n`, `std_sshoulder_rt_n_n`,
`std_sshoulder_lt_n_n`; the AI does not start new reactions during those.

### Death animation choice
Requested when the character dies and again whenever its death animation ends (so that dying can continue).
Inputs: attacker, damage flags (SPEC-720 numbering), an optional sequence forced by the caller, the zone of the
killing blow (SPEC-721), posture and class of the current record, `lock_deaths`.

On every request: playback speed index 4; then the corpse bookkeeping of "Dying and corpse state" runs.

With `lock_deaths` set (parental lock): if the current record is already a death, keep it; if the current
posture is sitting and `lb_sautofire_end_n_n` is available play it; else `std_shead_2frnt_n_n` with Scream.
Character spawnflag 0x10000 always gives `lf_sautofire_end_n_n` (or `std_shead_2frnt_n_n` under `lock_deaths`).

Otherwise the first rule that yields an available sequence wins:

| # | condition | result (voice; flags) |
|---|---|---|
| 1 | a sequence was forced by the caller | that sequence |
| 2 | the body's tumble flag is set (set elsewhere; believed: knocked off balance or falling; UNVERIFIED) | `std_xtumble_2frnt_n_n` (Scream unless already tumbling; committed, muted) |
| 3 | current record is a death | dying follow-up (next section) |
| 4 | current posture crouched | `cch_sdead_2bk_n_n` (Scream); only when the model lacks it and the current record is `cch_xonknees_n_n_n`: `cch_sonknees_2frnt_n_n` (Scream) |
| 5 | current posture prone | `lf_sautofire_end_n_n` (Scream; muted) |
| 6 | current posture on back | dying follow-up |
| 7 | damage flag 0x8000, standing | attacker behind the victim → `std_shead_2frnt_n_n`; else `std_sviolent_n_n_n`; else `std_stoknees_2bk_n_n` (committed, muted) |
| 8 | damage flag 0x800, standing | with damage flag 0x200000 and available: `std_snervegun_2bck_n_n` (Choke, 2 variants; effect `environ/shockdeath` on the character); else `std_smicro_n_n_n` (Choke; effect `environ/pulsecook`; sound `Weapons/MPG/Expand.wav` volume 0.6, attenuation 1, channel 4). Both muted; playback speed index random: 8, 9, 10, 11 or 12 (irand 0–4; 0 gives 12) |
| 9 | damage flag 0x1000, standing | `std_sonfire_2bk_n_n` (Burn, 1 variant; muted); if any character started a burning death less than 0.1 s ago (window −0.5 s … 0.1 s), the direction death of rule 12 instead |
| 10 | damage flag 0x10000, standing | `std_sdance_n_n_n` |
| 11 | a killing zone is known | by the sever segment of that zone (SPEC-721 part lists): neck → `std_sthroat_2side_n_n` (Choke 2; blood stump effect at `sbolt_mouth`; throat-shot statistic +1); head → `std_shead_2frnt_n_n` (head-shot statistic +1); groin → `std_sgroin_2side_n_n` (Crotch 2; groin statistic +1); left thigh/calf/foot → `std_sleglt_2bk_n_n` (Scream); right thigh/calf/foot → `std_slegrt_2bk_n_n` (Scream); shoulder zones by sever segment 1 or 4 → `std_sshoulder_rt_n_n`, segment 2 or 3 → `std_sshoulder_lt_n_n` (Scream; for the male soldier table this is `GZ_SHLDR_RIGHT_FRONT`/`GZ_SHLDR_LEFT_BACK` → right and `GZ_SHLDR_RIGHT_BACK`/`GZ_SHLDR_LEFT_FRONT` → left); arm and chest zones have no zone death and fall through; zone `GZ_GUT_FRONT` (no segment) → `std_sgut_2side_n_n` (Scream). Missing sequences print `eek. That hurt my neck.`, `eek. That hurt my left foot.`, `eek. That hurt my right foot.`, `eek. That hurt my groin.`, `Couldn't find right leg death!`, `Couldn't find left leg death!`, `Couldn't find right shoulder death!`, `Couldn't find left shoulder death!`, `Couldn't find gut death!` and fall through. Statistics are not counted during cinematics |
| 12 | probability 3/4 (irand(0,3) ≠ 0) | by direction: attacker in front (or no attacker): irand(0,2) = 0 → `std_schest_2frnt_n_n`; otherwise `std_sblown_2bk_n_n` (committed), or `std_sviolent_n_n_n` when the model lacks the blown-back sequence; attacker behind → `std_sinback_2frnt_n_n` (committed). Scream in each case; muted |
| 13 | probability 3/4 and the character was moving (AI velocity squared > 40) | `std_sstumble_2frnt_n_n` or `std_sstumble_2bk_p_n` (1/2 each) |
| 14 | always | `std_stoknees_2bk_n_n` (Scream; committed, muted) |
| 15 | nothing available | `std_shead_2frnt_n_n` |

"Muted" = no further voice from this body (death voice already played); "committed" = the death will not be
replaced by a writhing follow-up. The damage-flag meanings are those of SPEC-720 (0x8000 believed explosive,
0x800 MPG/microwave, 0x200000 nerve/electric, 0x1000 fire, 0x10000 automatic-weapon "dance"; UNVERIFIED here).

### Dying follow-up (a death record is current and a new death animation is requested)
| current record | condition | next |
|---|---|---|
| `std_sdance_n_n_n` | ≤ 0.3 s since it started | keep; each request has a 1/8 chance of voice "Hit" (3 variants) when the expression state is "dead", else "Scream" |
| `std_sdance_n_n_n` | > 0.3 s | `std_shead_2frnt_n_n` (committed, muted) |
| `lb_sautofire_loop_n_n` | > 0.3 s | `lb_sautofire_end_n_n` (committed, muted) |
| a record with the lying-dying flag (0x800000) or a finished one, posture on back or on side, > 0.1 s | — | on-side records first turn the character 180°; damage flag 0x10000 → `lb_sautofire_loop_n_n`; else `lb_sautofire_end_n_n` (sets the replay flag) |
| `lf_sautofire_loop_n_n` | > 0.3 s | `lf_sautofire_end_n_n` |
| same rule, posture face down | — | `lf_sautofire_loop_n_n` (damage flag 0x10000) or `lf_sautofire_end_n_n` |
| `lb_pgeneric_n_n_n` / `lb_pholdleg_rt_n_n` / `lb_pholdleg_lt_n_n` | finished and playing longer than 3 + flrand(0,3) s | `lb_pgenericdie_n_n_n` / `lb_pholdlegdie_rt_n_n` / `lb_pholdlegdie_lt_n_n` (committed, muted) |
| finished death, not committed: `std_slegrt_2bk_n_n` | 1/3 chance and the head is still attached | `lb_pholdlegto_rt_n_n` (voice "Writhe" 7; muted) |
| `lb_pholdlegto_rt_n_n` | — | `lb_pholdleg_rt_n_n` |
| `std_sleglt_2bk_n_n` | 1/3, head attached | `lb_pholdlegto_lt_n_n` (Writhe) |
| `lb_pholdlegto_lt_n_n` | — | `lb_pholdleg_lt_n_n` |
| `lb_pgenericto_n_n_n` | — | `lb_pgeneric_n_n_n` |
| any other finished non-looping death | 1/3, head attached | `lb_pgenericto_n_n_n` (Writhe) |
| otherwise | — | committed and muted; the current record stays |

(UNVERIFIED: the on-back autofire rule as written also matches ordinary deaths that end on the back; verify by
killing soldiers with a pistol and watching whether `lb_sautofire_end_n_n` follows every backwards fall.)

### Dying and corpse state (human bodies)
Run on every death-animation request:
- When the previous death animation (class 6) has finished: an armour pickup is dropped (Armour), knives stuck in
  the body are released (SPEC-721), and the AI is told once that the death animation completed (only for
  characters that have a recorded killer).
- When the current animation has finished and the expression state is not yet "dead": if the death was
  committed or the animation started more than 10 s ago, the expression becomes "dead" (state 5, for 999999.9 s).
- Corpse freeze: once the current animation has finished, or 5 s after the character's recorded death time, all
  Ghoul instances of the character (body and bolt-ons) are paused at the current time and stay frozen; a new
  animation started within 0.1 s re-arms the freeze.
- Microwave death completion: when `std_smicro_n_n_n` has finished (and `lock_deaths` is clear), the corpse is
  frozen, gibbed with the MPG splatter variant (SPEC-721, damage flag 0x800 added), sound
  `Weapons/MPG/Burst.wav` (volume 1, attenuation 1, channel 0) and the animation `std_shead_2frnt_n_n` is
  returned.
Corpse removal and the player body queue: common part.

### Voices
All voices are stored as one *pending voice* and started on the body's next update with `sound(entity, channel 2,
sound, volume, attenuation)`; a newer voice replaces an older pending one.

1. **Tactical voice** (event names `backup`, `throw`, `duck`, `retreat`, `help`, `react`, compared without case):
   requires a voice directory, the AI not being in a scripted-silence state, no cinematic running, the body's voice
   enabled and not muted, gore zones present, and at least 2 s since the last tactical voice of any character.
   The same event is never played twice in a row by any character. Path `<voice dir><voice number>/<level
   dir>/<event>.wav` or, when a variant count k > 0 is given, `<…>/<event><irand(1,k)>.wav`; attenuation 1.6.
   The file must pass an existence check made with that path (without the `sound/` prefix); in the shipped data
   this check is believed to fail, and no caller of this voice was found (UNVERIFIED: watch for these voices in
   game with `ai_goretest 1`, which prints `voice sound: <path>`).
2. **Wake voice** (the character notices an enemy; also used when startled): requires the same conditions and
   health > 0, at least one wake file, and 2 s since the last wake voice of any character. Variant: irand over the
   available count, never the same index as the previous wake voice of any character (when more than one exists).
   Path `<voice dir><n>/<level dir>/Wake<i>.wav`, attenuation 1.6, volume as requested (0.9 when startled).
   `ai_goretest` non-zero prints `wake sound: <path>`.
3. **Greeting** (the player uses or touches the character): on *use*: health > 0, the greeting timer has expired;
   an armed character that is not on the user's team does not greet; health 9999 (scripted invulnerable) only
   restarts the timer. The expression becomes "talking" for 2 s (talking-afraid when the expression was afraid or
   in pain, talking-neutral when neutral). On *touch*: only when greetings are allowed for this level, health > 0
   and the timer expired, same team/armed rule. The greeting index is chosen once per character, avoiding the
   index the previous greeting of any character used. Path `<voice dir><n>/<level dir>/Greet<i>.adp`, volume 0.9,
   attenuation 1.6; then the timer is set to now + 10 s. `ai_goretest` prints `greet sound: <path>`.
4. **General voices** (pain and death): directory chosen by the death-voice type (table "Death-voice directory");
   requires that directory, the voice enabled (or the event `choke`), and the body not muted. Path
   `<death dir>/<event><irand(1,k)>.wav`, volume 0.8 (0.75 for `Limp`), attenuation 2.0. Each played general voice
   raises the music intensity by 0.05 (music system). `ai_goretest` prints `general voice sound: <path>`.

Voice disabling: a hit whose entry or exit zone is `GZ_HEAD_FRONT`, `GZ_HEAD_BACK` or `GZ_NECK` disables the
character's voice permanently (only `Choke` can still play).

Limp voices: while `std_plimp_lt_n_n` (right-leg limp) or `std_plimp_rt_n_n` (left-leg limp) plays, each footstep
of the injured leg has a 1/2 chance of "Limp" (3 variants, volume 0.75).

Level directory (`<level dir>`): the current map name, except that `tsr1` and unknown maps use `TSR1`, and `tsr2`
uses `TSR2`.

Variant counts are found at precache time: `sound/<voice dir><n>/<level dir>/Wake<i>.wav` (and `Greet<i>.adp`)
for i = 1, 2, … until a file is missing; every found file is registered (`voice cache: <path>` with
`ai_goretest`). While a pak list is being built (`fs_createpak`) every voice number 1…count is cached.

#### Death-voice directory
| death-voice type / level | directory |
|---|---|
| type 7 | the custom directory given at set-up |
| `tut1`, `arm1`, `arm2`, `arm3` (any type) | none (silent); greetings by touch are also disabled on these maps |
| type 5 (female), other maps | `enemy/dth/fem` (on `jpn2`: none) |
| `tsr2`, `kos1`–`kos3`, `sib1`–`sib3` | `enemy/dth/russ` |
| `trn1`, `irq1a`–`irq3a`, `irq1b`–`irq3b`, `sud1`–`sud3` | `enemy/dth/arab` |
| `jpn1`–`jpn3` | `enemy/dth/jpn` |
| `ger1`–`ger4` | `enemy/dth/rad` |
| any other map (`tsr1`, `nyc1`–`nyc3`, unknown) | `enemy/dth/skin` |

General voice events and variant counts: `Burn` 1, `Choke` 2, `Crotch` 2, `Hit` 3, `Scream` 8, `Writhe` 7,
`Limp` 3, `Getup` 1, `Disarm` 1 (the right-arm pain uses only `Scream1`–`Scream5`).

### Face expressions
Characters with a face skin prefix animate the skin of material `f` once per body update:
- Suffix by expression state: 5 dead → `d`; 4 pain → `p`; 3 → `m`; 2 afraid → `f`; 6 talking → each update one of
  `nt3` (1/4), `nt2` (1/3 of the rest), `nt1` (1/2 of the rest) if that skin exists, else `n`; 7 → `mt3`/`mt2`/`mt1`
  likewise, else `m`; 8 → `ft3`/`ft2`/`ft1`, else `f`; any other state (neutral) → `n`, or `b` (blink) when the
  blink timer has expired (next blink after flrand(3,5) s). In state 6 the blink timer is advanced but `n` is shown.
- Skin name `<prefix><suffix>`; if that skin does not exist, `<prefix>n`; applied to the whole model only when the
  suffix changed.
- Expression states are set by hits (4 pain for 10 s; 4 for 99999 s on a mortal wound), by death (5), by AI
  decisions (2 afraid for 5 s when fleeing), by greetings (6/8 for 2 s) and by severing the head (5 for 1 s). The
  state machine itself is in the common part.
- Breath: on maps `tsr2` and `sib1`–`sib3`, effect `enemy/breathe` on bolt `sbolt_mouth` at each expression
  "tick": the next tick is after flrand(1.0,1.5) s in states 2 and 4, flrand(1.25,2.0) s in state 3, never again in
  state 5 (dead), flrand(2.5,4.5) s otherwise (also for characters without a face).

### Model events registered by human bodies
At precache, for characters (players register only `EOS`):
| event | registration | effect |
|---|---|---|
| bolt `quake_origin` | per update, matrix mode 1 | root motion (common part); missing → `WARNING: monster origin not found--wacky!!` |
| bolts `sbolt_heel_r`, `sbolt_heel_l` | per update, mode 5 | footsteps: for a living character whose AI reports a movement value above 9, a heel lower than 7 units above the AI's reference height produces a footstep effect (kind 14 for armour type 3, else kind 4; effect library, SPEC-201) and the limp-voice chance for that leg; the heel height is always reported to the AI |
| bolt `quake_ground` | per update, mode 5 | ground offset (common part) |
| bolt `sbolt_mouth` | per update, mode 5 | look direction and eye height (common part); missing → `WARNING: monster mouth not found--sight will be defective!!` |
| notes `BOS`, `EOS`, `jump`, `thud`, `knee`, `inair`, `fire` | note | forwarded to the AI as in the common part |
| note `throw` | note | the AI releases the thrown item |
| note `dropweapon` | note | the AI drops the weapon, unless the character has spawnflag 0x20000 (spawnflag 0x40000 is cleared) |
| note `sound` | note | plays `<argument>.wav` on channel 0, volume 0.6, attenuation 1.0; at registration every `sound` note argument of the model is registered as `<argument>.wav` |

### Weapon handling
- **Clip**: refilled at precache and whenever a reload starts; rounds by right-hand attack type: 3 → 18, 4 → 9,
  5 → 33, 6 → 40, 7 → 6, 9 → 10, 11 → 8, 12 → 30, 14 → 4, 16/17/29 → 100, 18 → 100000, any other → 1. Each shot
  uses one round; at 0 the reaction override reloads.
- **Ready to fire** (AI query): the time since the last shot must exceed d × F × h × p, where d is the attack
  type's base refire delay (common part, "Weapon identification"; depends on the rounds left for type 9), F a
  refire factor kept by the game (believed skill-dependent; UNVERIFIED source), h = 2 while a limb injury is active
  else 1, and p = 0.75 when this character is the one the game's shared AI state currently designates (believed:
  the lead attacker; UNVERIFIED, AI spec), else 1.0. An MPG (type 16) that has never fired records "last shot =
  now − 1 s" and is not ready.
- **Firing**: when the caller asks for a paced shot, the shot is skipped unless the time since the last shot is at
  least d × F × q × i, with q = 0.5 for the designated character (1.0 otherwise) and i = 2.5 while a limb injury
  is active (1.0 otherwise); a weapon model with a `flash` bolt is required. The shot starts at the flash bolt (directly for records with the corner-peek flag 0x40000) and
  goes towards the target point (optionally a bolt of the target: head, shoulders, chest, groin, gut); a character
  facing more than about 44° away (dot < 0.717, except attack type 18) or behind (dot < 0) does not fire and gets
  the round back. Aim spread: each direction component += flrand(−s, s), then renormalised; s = 0.1 (type 3),
  0.4 (5), 0.3 (6), 0.2 (9, 11, 12, 16, 17, 18, 29), else 0. For types 7, 9, 11, 12, 16, 17, 18 and 29 the
  vertical aim uses the target's real position; type 11 mixes 60 % of the current direction with 40 % towards the
  target. The attack itself is the server-side attack definition of the right-hand type (weapons spec). When the
  target is a client, the music system's "under fire" trigger is raised for it (same trigger as in SPEC-720).
  The facing check is skipped for characters whose AI fires blind (an AI flag; UNVERIFIED which).
- **Weapon charge effects** (each update): MPG (types 16, 17) more than 1 s after the last shot: effect
  `weapons/othermz/mpgcharge` on the weapon's `flash` bolt and, on every 5th tick, `weapons/othermz/mpgcharge2` at
  the character. Flamethrower (type 18): effect `weapons/othermz/flamecharge` on the flash bolt; within 0.5 s of a
  shot the entity's looping sound is `Weapons/flamegun/Fire.wav`, otherwise no looping sound.
- **Throwing** (left-hand item): possible when the left-hand type is set and not 27, the character has a target,
  the target is at least 200 units away (type 30, the throwing star, has no minimum), and no teammate is within
  175 units of the target (for type 30, other than itself). The throw starts at `wbolt_hand_l`. Type 30 flies
  straight at the target; other items need the character to face the target (dot ≥ 0.707) and are lobbed: launch
  angle = the ballistic angle for the target distance + 30°, at most 80°, aiming up to 20 units above the target
  (flrand(0,20)). Sound `Weapons/gren/throw.wav`, volume 0.6, attenuation 1, channel 0. "Can throw now" (AI
  query): left-hand item present, spawnflag 0x80000 or type 30, 10 % chance per query, no teammate within 200
  units and a clear line forward.
- **Disarm / drop both weapons**: both hand bolt-ons are dropped backwards (SPEC-721 bolt-on drop, models aligned by
  `TO_WBOLT_HAND_L` / `TO_WBOLT_HAND_R`); the right-hand type becomes 0.

### Armour
Armour is stored as negative accumulated damage on gore zones (the accumulated value of SPEC-721 starts below
zero, so wounds and severing need more damage). Setting armour type T (only for non-client characters with at least
26 zones) with base P = 300 armour points per zone (armoured male soldier: P = 75 at skill level 0, 99 at 1,
150 at 2 or 3, 300 otherwise):

| type | effect |
|---|---|
| 0 | clears armour on all zones |
| 1 | torso: `GZ_SHLDR_*` (4), `GZ_CHEST_FRONT/BACK`, `GZ_GUT_FRONT/BACK` −P each |
| 2 | torso and limbs: also `GZ_ARM_*` (4), `GZ_GROIN`, all `GZ_LEG_*` (6), `GZ_FOOT_*` (2), `GZ_GUT_*_EXTRA` (2) −P |
| 3 | all 26 zones −4·P |
| 4 | health and maximum health × 1.5 (no zone armour) |
| 5 | health and maximum health × 2 |
| 6 | torso and limbs −0.3·P (integer) |
| 7 | all 26 zones −P |

Armour percentage (used for disarming and the dropped pickup): types 1, 2, 6 and 3, 7 sum the remaining
armour (−accumulated, only where negative) over the armoured zones, divided by (zone count × 300) × 100; type 6
multiplies by 0.25 and caps at 20; types 0, 4, 5 → 0. When a dead character's death animation finishes and its
armour percentage is above 0, an armour pickup item is spawned under the corpse (on its floor) with count =
max(50, armour percentage) and the armour is cleared — except for `m_x_mraiderboss1` and `m_x_mraiderboss2`.

### Precache (character set-up)
Registers: `impact/gore/fall1–3.wav`, `impact/gore/impact1–3.wav`, `impact/gore/limbfall.wav`,
`impact/gore/sizzle.wav`, `impact/gore/hitflesh.wav`, `impact/gore/headexp.wav`; the general voices of its
death-voice directory (`Burn1`, `Choke1–2`, `Crotch1–2`, `Hit1–3`, `Scream1–8`, `Writhe1–7`, `Limp1–3`, `Getup1`,
`Disarm1`); its wake and greeting voices; `Weapons/gren/throw.wav`, `impact/armor/hit.wav`; effects
`gore/stumpblood`, `gore/headcloud`, `gore/bloodcloud`, `gore/bloodcloud_s`, `gore/noblood`, `gore/noblood_s`,
`gore/gibbody`, `spark2`, `weakspark`, `armorhit`, `gore/bloodpool`, `gore/danglyblood`, `gore/hugeburst2`,
`environ/gb_exitw`, and on maps whose name starts with `sib` `enemy/breathe`; the player weapons of both hands; the
gore-zone setup (SPEC-721); then the model events above.

## Common body behaviour, non-human bodies (vehicles, animals) and corpse handling

This section covers what all bodies share (families, animation records, playback, turning, bolt-ons, weapon
identification, savegames, corpses) and the helicopter, tank, snowcat, cow and dog bodies. Human-specific rules are
in the sections above.

### Common part: Purpose (body part)

Every character entity in the game module (enemies, civilians, animals, vehicles, and players in all game
modes) owns a **body**: the object that knows how that kind of character is modelled. The body selects and
plays animation sequences on the entity's Ghoul instance, chooses the animation for each situation (idle,
move, attack, pain, death), owns the per-character animation state, plays the character's non-voice sounds,
attaches bolt-on models, listens to the model's note and bolt events, receives every hit (hit location,
damage scaling and gore: SPEC-720/721) and writes itself into savegames. This part specifies the parts that
are common to all bodies, the animation record tables, and the helicopter, tank, snowcat, cow and dog bodies,
plus corpse lifetime. The human bodies (male soldier, civilian/officer, female, armoured boss) are specified in
the human part of this spec.

### Common part: Interface / layout

#### Body families
The original identifies each body family by a small number. The number has no external meaning except that a
corpse copies the family of the dead player's body (Behaviour: corpses); it is listed so that savegame
contents and test logs can be matched.

| id | family | used by (classnames) | model directory | parent family |
|---|---|---|---|---|
| 1 | human (abstract) | — | — | base |
| 2 | dog (abstract) | — | — | organic |
| 3 | male soldier ("meso") | every `m_*_m*` character except the two raider bosses; also player characters whose model is not the female one | `enemy/meso` | human |
| 4 | civilian / officer ("ecto") | every `m_*_e*` character (e.g. `m_nyc_ebum`, `m_irq_eofficer`, `m_sib_escientist1`) | `enemy/ecto` | human |
| 5 | husky | `m_x_husky`, `m_x_germanshep`, `m_x_raiderdog` | `enemy/husky` | dog |
| 6 | rottweiler | `m_x_rottweiler` | `enemy/rottweiler` | dog |
| 7 | organic (abstract: anything with gore zones) | — | — | base |
| 8 | female | every `m_*_f*` character (e.g. `m_x_ftaylor`, `m_tok_fassassin`); player characters whose player model's base model is `fem_play` | `enemy/female` | human |
| 9 | helicopter | `m_x_chopper_black`, `m_x_chopper_green`, `m_x_chopper_white`, `m_x_hind` | (vehicle AI) | base |
| 10 | tank | `m_x_tank` | (vehicle AI) | base |
| 11 | snowcat | `m_x_snowcat` | (vehicle AI) | tank |
| 12 | cow / bull | `m_x_bull` | `enemy/bull` | organic |
| 13 | armoured male soldier | `m_x_mraiderboss1`, `m_x_mraiderboss2` | `enemy/meso` | male soldier |

- Player bodies: when a client's player model is set up, a female body is created when the player model's
  base model name equals `fem_play` (case-insensitive), otherwise a male soldier body. If the requested player
  model cannot be loaded the default player model and a male soldier body are used.
- Asking the family factory for an unknown id prints `ERROR: invalid body class code: <n>` and creates a
  male soldier body (id 3).

#### Animation records
Bodies never play a sequence by name directly: every animation is an **animation record** taken from one of
three tables in the game module (human, dog, cow; OpenMerc: `game/src/damage/g3_humananims.inc` and
`g3_animalanims.inc`, one row per record). A record has these fields:

| field | meaning |
|---|---|
| name | sequence name; the sequence file is `ghoul/<model directory>/<name>.ghl` |
| class | animation class: 0 stationary, 1 locomotion, 2 attack/fire, 3 jump, 4 fall, 5 hit reaction, 6 death |
| speed scale | 0.0 or 1.0 (the dog run uses 1.1); meaning UNVERIFIED (no effect observed on the bodies) |
| yaw left | maximum yaw change towards the left per think, degrees (before scaling, Behaviour: turning) |
| yaw right | maximum yaw change towards the right per think, degrees, negative |
| flags | flags (below) |
| posture | posture during/after the animation: 0 standing, 1 crouched, 2 prone, 4 sitting, 5 lying face down (falls forwards), 6 lying on the back (falls backwards), 7 lying on the side |
| start offset | start offset in seconds passed to the Ghoul Play call (0 for almost all records) |
| group | behaviour group used by the AI: 0 none, 1 aim/strafe-aim, 2 blinded, 3 idle fidget, 4 right-limb injury, 5 left-limb injury, 6 90° turn, 7 lean right, 8 lean left, 9 talk (meaning inferred from the record names; UNVERIFIED) |

Records are compared by identity, not by name: the dog table has two `std_jattack_n` records with different start
offsets (lunge and bite). The human table has one record per name, looked up case-insensitively.

Flags (bit values; meanings marked UNVERIFIED are inferred from which records carry them):

| flag | meaning |
|---|---|
| 0x08000000 | looping: played in Ghoul mode 0 (loop); without it mode 1 (play once and hold the last frame) |
| 0x01000000 | match phase: on a change of sequence the new cycle continues at the old relative position (SPEC-011 Play `matchPhase`) |
| 0x04000000 | reversed: played backwards (Play `reverse` = true, start offset 1.0) |
| 0x02000000 | keep the start time when switching from another record of the same class (not used by any table record) |
| 0x00040000 | corner-peek / lean animation: started 0.1 s after the request; when it is replaced by a class 5 or 6 animation the character's facing is re-aligned to its head (Behaviour: playback) |
| 0x00800000 | a death animation that may be replaced by another death animation when the corpse is hit again (wounded-lying loops, dog death "paddle" loops) |
| 0x00400000 | a looping animation that still counts as "not finished" until its cycle completes (Behaviour: finished state) |
| 0x00001000, 0x00008000, 0x00010000, 0x00020000 | aim-capable / aim backwards / strafe left / strafe right (UNVERIFIED; used by the human body and AI actions) |
| 0x00002000, 0x00004000, 0x00000800, bit 28, bit 29 | consumed by the human body (hand/weapon handling, grenade throw, jump) — see the human part |

#### Body state visible to other parts of the game
Each body keeps, for its entity: the current animation record (none at creation), a *finished* flag (true at
creation), a pause kind (0 = not paused), an expression state with an expiry time and a lock flag, a 32-bit
event-flag word, a pending-restart flag (animals), the start time of the current playback and family data
listed per family below.

### Common part: Behaviour

#### Playback (all bodies)
Requests come in two strengths: *set* (no restart) and *force* (restart). Playing record R on entity E:
1. If R is the current record, no restart was asked and the current playback is not finished: nothing is
   played; continue with step 9.
2. If the current record is a one-shot (not looping), its playback has finished, and R has the match-phase
   flag: the request becomes a restart without phase matching.
3. Lean exit: if R does not have flag 0x00040000, the current record has it and R's class is 5 or 6, the
   entity's yaw and pitch are replaced by the direction of the model's `sbolt_mouth` bolt (pose mode 5,
   through parents, at the current time) added to the entity angles; dog bodies subtract 90° from that yaw.
   Angles are quantised to 1/65536 of a turn. If R has flag 0x00040000 the playback start time is now + 0.1 s,
   otherwise now.
4. If the body is paused with kind 2, the request fails.
5. If the entity has no Ghoul instance or its model has no sequence `ghoul/<dir>/<name>.ghl`, the request
   fails and prints `<classname> <targetname> couldn't play animation <name>!`, or
   `Some <classname> couldn't play animation <name>!` when there is no targetname, or
   `Unknown monster couldn't play animation <name>!` without a classname.
6. With flag 0x02000000 and a different record of the same class as the current one, the previous start time
   is kept.
7. Ghoul Play (SPEC-011 method 17) is called with: the sequence, the start time, offset = record start offset
   (1.0 for reversed records), restart as requested, mode 0 for looping records and 1 otherwise, match phase =
   the record's flag (cleared by step 2), reverse = the record's reversed flag. The start time is remembered.
8. If the instance's sequence changed or a restart was requested: the finished flag is cleared; if the entity
   has an AI, the `quake_ground` bolt is evaluated at once and handed to the AI exactly as the ground-bolt event
   below (so the model's ground offset never lags a sequence change).
9. The record becomes current and a pause of kind other than 0 and 2 is lifted (resume at the current time).
- Animal bodies (cow, dogs) additionally restart when their pending-restart flag is set, and clear it.
- "Has animation R": true when the model has the sequence file. (For the three human model directories the
  original caches this per character type; the result is the same.)

#### Finished state
- The finished flag is set by the model's `EOS` note (via the AI) and cleared by every new playback.
- "Animation done" is true when there is no current record, or when the current record loops and lacks flag
  0x00400000, or otherwise when the finished flag is set.

#### Pause
- Pause(kind, time) freezes the instance at `time` (SPEC-011 method 19) and stores `kind`.
- Resume (at the current time, method 20) happens only when the stored kind is neither 0 nor 2; kind 2 is a
  hard freeze that also blocks new animations until something else clears it.

#### Expression state
A small state machine used by human faces (values and face skins: human part) but kept by every body:
- Set(state, duration, lock): when the current state is unlocked and `lock` is false, a current state of 4 or 5
  can only be replaced by 5; when the current state is locked and `lock` is false, only 5 is accepted. Otherwise
  the state is stored with expiry = now + duration and the lock flag = `lock`.
- Each update, when the expiry time has passed, the state returns to 1 (neutral) and is unlocked.
- Values used by the original: 1 neutral; 2, 3 (alternate faces); 4 pain; 5 dying/dead; 6 and 8 talking.
  Requests seen: (4, 10 s) and (5, 1 s) from hit handlers, (4, 99999 s) and (5, 999999.9 s) for mortal wounds and
  death, (6 or 8, 2 s) when greeting (human part).

#### Turning and root motion
- Root motion: the model's `quake_origin` bolt is read every update in rate mode (mode 1). Its translation
  (with the second component negated) becomes the entity's animation-driven velocity, and the yaw component of
  its rotation the animation-driven yaw rate. During the four corner-peek fire records
  (`std_fcornerpeak_lt_m_2`, `std_fcornerpeak_rt_m_2`, `std_fcornerpeak_lt_p_a`, `std_fcornerpeak_rt_p_a`) the
  vertical component is zeroed, and once the action is done the velocity is reversed (the character steps back
  out of the peek). Exact yaw formula UNVERIFIED (verify by logging the entity angles during a turn-in-place
  animation such as `std_wturn90_lt_pk_n`).
- Movement delta for a step: each component of the animation-driven velocity × 0.1, clamped to ±50 units, then
  × the step factor supplied by the caller, then converted from the entity's forward/right/up axes to world
  axes.
- Yaw limits for one think, given a fallback limit L: without a current record the limits are +L and −L.
  Otherwise let y = animation yaw rate × 0.1 converted to degrees, and f a factor depending on the distance d
  from the character to the point the AI is facing:
  - when the AI's target is not a client: f = 3 for d < 64, f = 3 − 2·(d − 64)/128 for 64 ≤ d ≤ 192, f = 1
    beyond;
  - when the target is a client: g = 2 for d < 64, g = 2 − (d − 64)/128 for 64 ≤ d ≤ 192, g = 1 beyond, and
    f is g multiplied by a per-character turn factor supplied by the AI;
  - f = 1 when the AI has no target or the character's health is ≤ 0.
  Left limit = record[0x40] × 1.5 + f·y, right limit = record[0x44] × 1.5 + f·y. The animation-driven
  rotation rate is reset to zero afterwards.

#### Model events registered by the cow and dog bodies (at spawn of the character; human list: human part)
| event | Ghoul registration | effect |
|---|---|---|
| bolt `quake_origin` | per-update bolt matrix, mode 1 (rate) | root motion (above); missing bolt prints `WARNING: monster origin not found--wacky!!` |
| bolt `quake_ground` | per-update bolt matrix, mode 5 | the bolt's height is passed to the AI as the model's ground offset, but only while the character is in a state where its feet follow the floor (two entity conditions, UNVERIFIED: "on ground" or step movement) |
| bolt `GBOLT_HEAD` ("mouth") | per-update bolt matrix, mode 5 | the head's horizontal facing (dogs: −90°) added to the entity angles becomes the AI's look direction; the entity's eye height is set to the bolt's height (truncated to an integer); missing bolt prints `WARNING: monster mouth not found--sight will be defective!!` |
| note `BOS` | note | when the current animation is a reversed record, `BOS` marks its end: the AI is told the sequence ended (as for `EOS`) |
| note `EOS` | note | the AI is told the sequence ended (it marks the body finished); for player bodies the body is marked finished directly |
| note `fire` | note | the AI fires its weapon (animals: bite/attack, below) |
| notes `jump`, `thud`, `knee`, `inair` (dogs) | note | forwarded to the AI as take-off, landing (`thud` and `knee` both), in-air events |
Entering this state also clears the entity's "dead" flag, creates the gore-zone array (cow: 6 zones, dogs: 10
zones; zone layout and part names: SPEC-721) and runs the family's gore-zone/part setup.

#### Bolt-ons and skins (all bodies)
- Attach a bolt-on: the object `<directory>/<name>` is registered in the game's Ghoul model registry (with an
  optional skin), and attached to the named bolt of the character (SPEC-011 method 28). Variants accept a bolt
  id, a bolt name or an already registered object.
- Random accessory: attached with probability p (a uniform random number in [0, 1) is compared with p). When the
  cvar `fs_createpak` is non-zero at level start every random accessory is attached regardless of p (so that a
  pak build references all files) and the call reports "not attached".
- Set skin: material `<m>` of the model gets skin `<s>` (both by name, SPEC-011 method 11, whole model); fails
  silently when either is unknown.

#### Weapon identification (used by human bodies)
The weapon model bolted to a hand is identified by its model name (case-insensitive). Two numberings are used —
the weapon number (player weapon slots) and the attack type (index of the server-side attack definition, also
used for animal attacks):

| bolted model | weapon number | attack type | base refire delay of the attack type (s) |
|---|---|---|---|
| `w_knife`, `w_bat`, `w_kantana` | 1 | 1 | random 1.35–1.65 |
| `w_pistol2` | 2 | 4 | 1.5 |
| `w_pistol1` | 3 | 3 | 0.8 |
| `w_machinepistol` | 4 | 5 | 0.01 |
| `w_assault_rifle` | 5 | 6 | random 0.1–0.5 |
| `w_sniperrifle` | 6 | 7 | 2.5 |
| `w_autoshotgun` | 7 | 9 | 2.0 or 0.6 (see below) |
| `w_shotgun` | 8 | 11 | 4.0 |
| `w_machinegun` | 9 | 12 | random 0.5–1.0 |
| `w_rocket` | 10 | 14 | 4.0 |
| `w_mpg` | 11 | 16 | 6.0 |
| `w_flamethrower` | 12 | 18 | 0.01 |
| anything else / nothing | 0 | 0 | 0 |
Other attack types: 17 → 6.0, 29 → 2.5, every other value (including 20, the animal attack) → 0. For attack
type 9 the delay is 2.0 when a value supplied by the caller (the original passes the character reference) is
≡ 1 (mod 3), otherwise 0.6 — in practice a fixed choice per character (UNVERIFIED in game). How the delay is
combined (skill, hand count) is in the human part.

#### Helicopter body
- Attached to its entity by the helicopter AI; from then on the instance's note processing runs from the
  current time.
- Damage smoke (called by the helicopter AI with h = health / max_health, and with −1 when the smoke must be
  removed): uses the persistent entity effect slots (SPEC-202) with effect `environ/helismoke` on bolts
  `smoke_engine` and `smoke_tail` (nothing happens if either bolt is missing):

  | h | engine smoke scale | tail smoke scale |
  |---|---|---|
  | h < 0 | removed | removed |
  | 0 ≤ h < 0.25 | 2.0 | 1.0 |
  | 0.25 ≤ h < 0.5 | 1.5 | 0.75 |
  | 0.5 ≤ h < 0.75 | 1.0 (both slots created here if the engine smoke is absent) | 0.5 |
  | h ≥ 0.75 | unchanged | unchanged |
  Scales only change existing slots, so smoke appears only after the helicopter has passed through the
  0.5–0.75 band (a single hit taking it from above 0.75 to below 0.5 shows no smoke until the next band change
  — observable quirk).
- Canopy blood (when the helicopter AI kills the pilot; skipped when `lock_blood` is set): the material `canopy`
  of the cockpit model gets skin `glass_blood_fnt` (variant 1), `glass_blood_bk` (2) or `glass_bloody` (3);
  other values remove the override.
- Gun barrel: while firing the gun model plays sequence `barrel` looping; when stopped it plays `barrel_still`
  once.
- Hits: ricochet sounds `Weapons/FX/Rics/ric<n>.wav` and damage routing — SPEC-720.

#### Tank body
- Created by the tank AI. Moving on: the hull model plays sequence `tank` looping; moving off: `tank` played once
  (holds its last frame).
- A disabled flag is set from spawnflag 1 of `m_x_tank` (meaning of the flag: enemy roster spec).
- Machine-gun part hit points start at 150 (consumed by the hit handler, SPEC-720).
- Aiming parameters handed to the vehicle AI (consumer: enemy roster spec; roles UNVERIFIED): 20, 50, turret
  yaw rate 0.04, gun pitch rate 0.03, machine-gun rate 0.05 (radians per update).

#### Snowcat body
- Built on the tank body with: parameters 30 and 70, turret rate 0.06 + 0.01 × skill, other rates 0,
  machine-gun hit points 150.
- Moving on: the model plays sequence `body` looping and persistent effect `environ/snowspray` is started on
  bolts `snow_effect_left` and `snow_effect_right`; moving off: `body` played once and both sprays removed.
- Spawnflag 1 of `m_x_snowcat` selects the alternate state: the two attached helper models have all their mesh
  parts hidden, the model parts `front_left`, `front_right`, `rear_left`, `rear_right` are hidden and the
  exhaust effects are removed; without the flag the helper models and the four parts are shown and
  `environ/kf_exhaust01` is started on bolts `exhuast_left` and `exhuast_right` (spelling as in the model).

#### Cow / bull body
- Zones: 6 gore zones (`GZ_HEAD`, `GZ_CHEST`, `GZ_FRONT_R`, `GZ_FRONT_L`, `GZ_BACK_R`, `GZ_BACK_L`; SPEC-721).
- Idle choice: keep the current stationary animation while it is not done; otherwise with probability 1/60
  `std_imoo_n` (and the idle sound), else with probability 1/60 `std_ilooking_n`, else `std_i_n` (each only if
  the model has it).
- Pain choice: `std_p_n` if available, else the current animation.
- Death choice: keep a running death animation that is not re-hittable and not finished; otherwise `std_s_rt`.
- Attack: the body only records the attack time and computes the `GBOLT_CHEST` bolt position for the AI; it
  deals no damage itself (UNVERIFIED whether the bull hurts anyone).
- No movement-animation choice of its own (the base answer "none" is used).
- Idle sound: at most every 30 s, `Enemy/Cow/Moo1.wav` or `Enemy/Cow/Moo2.wav` (equal chance).
- Pain sound: `Enemy/Cow/Die.wav`, at most every 0.5 s and only while health > 0.
- Weapon/attack type reported to the AI: 20.

#### Dog bodies (husky, rottweiler)
- Zones: 10 gore zones (zone and severable-limb tables: SPEC-721). Attack range R = 85 units; a second distance
  parameter 100 (use UNVERIFIED).
- Idle choice (when the current stationary animation is done or a non-stationary one is playing):
  `std_itosit_n` → `sit_i_n`; `std_wsniff_n` → `std_wsniff_frm`; when standing: with probability 1/5
  `std_itosit_n`; else if the AI is alerted and with probability 1/3 and not already barking: `std_ibark_n` plus
  a bark sound; else `std_ipant_n` (or `std_w_n` if the model lacks it). When sitting: with probability 1/5
  `sit_itostand_n`, else keep sitting.
- Move choice (calm): while a locomotion animation is playing and not finished it is kept. Leaving the sitting
  pose plays `sit_itostand_n` first. When the AI reports combat (it has a target; UNVERIFIED) the attack choice is used instead, with the AI's
  target. Otherwise the
  walk "look around" cycle runs each time the current cycle finishes:

  | current | next |
  |---|---|
  | `std_w_n`, `std_wlookltrt_n` | (only with probability 1/3 for `std_w_n`, else keep walking) one of `std_wlooklt_to`, `std_wlookrt_to`, `std_wlookltrt_n` (1/3 each) |
  | `std_wlooklt_to` / `std_wlookrt_to` | `std_wlooklt_n` / `std_wlookrt_n` |
  | `std_wlooklt_n` / `std_wlookrt_n` | (with probability 1/3, else keep) `std_wlooklt_frm` / `std_wlookrt_frm` |
  | `std_wlooklt_frm`, `std_wlookrt_frm`, any stationary animation | `std_w_n` |
  | `std_wsniff_n` | `std_wsniff_frm` |
  | any other locomotion | kept |
  Every step only happens if the model has the target sequence; otherwise the current animation is kept.
- Jump choice: always `std_w_n`.
- Attack choice (target T): the attack time is recorded; a lunge (first `std_jattack_n` record) that is still
  playing is kept; sitting dogs stand up first; sniffing dogs finish the sniff. If T is within 60° of the dog's
  facing (dot product of facing and direction > 0.5) and at distance D:
  - 0.8·R ≤ D ≤ 1.2·R: lunge `std_jattack_n` (start offset 0);
  - D < 0.8·R: close bite, the second `std_jattack_n` record (start offset 0.4 s), with a bark sound when it was
    not already playing;
  - D > 1.2·R: run `std_r_n`, bark with probability 1/4.
  Otherwise walk `std_w_n`, bark with probability 1/20.
- Bite (on the `fire` note): the `SBOLT_HUSKY_TONGUE` bolt position (pose mode 5, through parents) and the AI's
  aim direction are used to fire attack type 20 (the animal attack definition of the server weapon table) from
  the dog; the victim's AI is notified; then the bite sound plays. Weapon/attack type reported to the AI: 20.
- Pain choice: none of its own (base: no pain animation).
- Death choice (inputs: the entity, the inflicting entity, the damage of the killing hit, a hit-flag word):
  - a death animation that is not re-hittable and not finished is kept;
  - while dead: `sid_prdl3_rt` that has been playing for more than 0.3 s since the last hit becomes
    `sid_prdl_rt`; if the current death animation is re-hittable or finished and the last hit was at most 0.1 s
    ago, the corpse twitches: `sid_prdl3_rt` when the hit-flag word has bit 0x10000, else `sid_prdl_rt`
    (unless `sid_prdl2_rt` is playing; this choice also forces the next play to restart the sequence); a
    missing `sid_prdl_rt` sequence falls back to `std_w_n`; otherwise the current animation is kept;
  - alive, damage ≤ 90: running dogs stumble (`std_stumble_rt`), others fall on their side (`std_stoside_rt`);
    if the model lacks the sequence, `std_w_n`;
  - alive, damage > 90: `std_sblown_2bk`, and the dog is turned to face the inflicting entity (yaw only).
  The identity of the "hit-flag word" and of "the last hit time" is UNVERIFIED (verify by shooting a dead dog
  with different weapons).
- Sounds (volume 0.8, attenuation 1, body channel 4):
  - bark: at most every 0.5 s, one of `Enemy/Dog/Bark1.wav`, `Bark2.wav`, `Bark3.wav` (equal chance);
  - bite: at most every 0.5 s, `Enemy/Dog/Bite1.wav` or `Bite2.wav`;
  - pain: at most every 0.5 s and only while health > 0, `Enemy/Dog/Die.wav`.
- The cow's moo uses volume 0.8, its pain sound volume 0.9, same channel and attenuation.

#### Savegames
Each body writes one chunk with an image of its state (SPEC-303 container); on load a new body of the same
family is created from it and pointers are re-linked through the game's object registries. Chunk ids and sizes:
helicopter `HELI` 440 bytes, tank `TANK` 172, snowcat `SCAT` 192, cow `AIBC` 148, husky `AIBH` 144, rottweiler
`AIBR` 144 (organic bodies then write their gore-zone state, SPEC-721). The image layout is private to the game
module; a reimplementation only needs it to read original savegames (out of scope).

#### Corpses
- Characters (AI-driven) keep their corpse while their death action runs. Each server frame, per dead character
  (counter reset every frame):
  1. A corpse that is the target of an `item_equip_armor` entity (an armour item whose `target` names the
     corpse) is never removed.
  2. A corpse flagged for immediate removal by the game is removed at once.
  3. A corpse that is currently fading out is removed when its fade completes.
  4. If fewer than `ai_maxcorpses` corpses have been counted so far this frame, or a global script/cinematic flag is
     set (UNVERIFIED), or the corpse is on the same team as the player (UNVERIFIED which entity is compared), the corpse is counted and kept. Otherwise (over the
     limit), once it has been dead for more than 3 s it starts a 3 s fade-out (immediately removed when it has
     no model).
  5. A counted corpse becomes eligible for removal when it has been dead for at least `ai_maxcorpses` × 15
     seconds (75 s with the default "5"; the cvar `ai_corpselife` is not used here). It is then removed unless a
     client is within 800 units whose view direction is less than about 110° away from the corpse (dot product of
     the client's forward vector and the direction to the corpse > −0.35); a seen corpse has its death time set
     to now − 25 s, so the check repeats 50 s later (with the default). An unseen eligible corpse ends its death
     action and is removed (UNVERIFIED that the removal is immediate: check with `ai_maxcorpses 1`). The original's visibility test passes
     the corpse position twice to the PVS check, so only distance and facing matter.
- Player corpses use an 8-entry body queue (entities right after the client entities, reused cyclically). The
  queued corpse copies the player's origin and angles (pitch set to 0), model state and Ghoul instance, gets a
  corpse AI of the dead player's body family (so it still takes hits and gore) and a think that merely re-arms
  itself every 0.1 s; `ai_corpselife` seconds are computed for the next think but immediately overwritten
  (no observable effect, UNVERIFIED in game). A player who was burning when the corpse was made (fire still
  lasting more than 1.2 s) passes it on: effect `environ/onfireburst` on the corpse and 4 s of fire; phosphor
  burn lasting more than 0.5 s: effect `weapons/world/phosburn` and 2 s.
- Fade-out: the entity is marked as fading with an end time and the Ghoul instance tint is driven by the
  effect system; at the end the tint is restored and the corpse removed.

### Common part: Constants

#### Families, sizes
See the family table. Gore zones per family: cow 6, dog 10 (human families: human part).

#### Sounds
| sound | used by | volume | throttle |
|---|---|---|---|
| `Enemy/Cow/Moo1.wav`, `Enemy/Cow/Moo2.wav` | cow idle | 0.8 | 30 s |
| `Enemy/Cow/Die.wav` | cow pain | 0.9 | 0.5 s, alive only |
| `Enemy/Dog/Bark1.wav` … `Bark3.wav` | dog bark | 0.8 | 0.5 s |
| `Enemy/Dog/Bite1.wav`, `Bite2.wav` | dog bite | 0.8 | 0.5 s |
| `Enemy/Dog/Die.wav` | dog pain | 0.8 | 0.5 s, alive only |
All on channel 4 with attenuation 1.0 and no time offset.

#### Effects, bolts, parts, sequences, skins
`environ/helismoke` (bolts `smoke_engine`, `smoke_tail`), `environ/snowspray` (`snow_effect_left`,
`snow_effect_right`), `environ/kf_exhaust01` (`exhuast_left`, `exhuast_right`), `environ/onfireburst`,
`weapons/world/phosburn`; bolts `quake_origin`, `quake_ground`, `GBOLT_HEAD`, `GBOLT_CHEST`, `sbolt_mouth`,
`SBOLT_HUSKY_TONGUE`; notes `BOS`, `EOS`, `fire`, `jump`, `thud`, `knee`, `inair`; vehicle sequences `tank`,
`body`, `barrel`, `barrel_still`; parts `front_left`, `front_right`, `rear_left`, `rear_right`; material
`canopy` with skins `glass_blood_fnt`, `glass_blood_bk`, `glass_bloody`.

#### Timing and geometry
| constant | value |
|---|---|
| lean animation start delay | 0.1 s |
| yaw-limit scale of record limits | 1.5 |
| yaw factor distances | 64 / 192 units, slope 1/128 |
| root-motion clamp | ±50 units per axis per 0.1 s step |
| dog attack range R | 85; lunge band 0.8·R … 1.2·R; facing cone dot > 0.5 |
| dog close-bite start offset | 0.4 s |
| dog blown-back damage threshold | > 90 |
| dog corpse twitch windows | 0.1 s after a hit; `sid_prdl3_rt` → `sid_prdl_rt` after 0.3 s |
| corpse over-limit grace / fade | 3 s / 3 s |
| corpse minimum age | `ai_maxcorpses` × 15 s |
| corpse visibility distance / facing | 800 units / dot > −0.35 |
| corpse re-check offset | death time := now − 25 s |
| body queue size | 8 |
| corpse fire hand-over | > 1.2 s left → 4 s `environ/onfireburst`; phosphor > 0.5 s left → 2 s `weapons/world/phosburn` |

#### Animation tables
The dog (26 records, addressed by position by the dog body), cow (6) and human (one record per sequence name of the
human models) tables are kept in the source: `game/src/damage/g3_animalanims.inc` and `g3_humananims.inc`
(fields: "Animation records"). The values are gameplay parameters matched to the observed behaviour.

### Common part: Test vectors / how to verify
1. Spawn `m_x_husky` in an open room, stand 70 units in front of it: it plays the close bite (second
   `std_jattack_n`, starting 0.4 s into the sequence) and a bark; at 85 units the full lunge; at 200 units it
   runs (`std_r_n`).
2. Kill a dog with a pistol (damage ≤ 90): it falls on its side (`std_stoside_rt`), or stumbles if it was
   running. Kill one with a rocket: `std_sblown_2bk`, facing the shooter. Shoot the dead dog: it twitches
   (`sid_prdl_rt` / `sid_prdl3_rt`).
3. `m_x_bull`: wait next to it for several minutes: moos at most every 30 s; shoot it: `Enemy/Cow/Die.wav` at
   most twice per second; on death `std_s_rt`.
4. Helicopter: damage it to 70 %, 40 %, 20 % health: engine/tail smoke scales 1.0/0.5, 1.5/0.75, 2.0/1.0
   (entity effect slots in a demo). Damage a fresh one from 100 % straight to 40 %: no smoke until it drops below
   25 % — expected: none at all, because slots are only created in the 50–75 % band (quirk check).
5. Snowcat: moving shows `environ/snowspray` at both rear bolts; with spawnflag 1 the wheel parts are hidden and
   no exhaust is shown.
6. Corpses: with `ai_maxcorpses 2` kill 4 soldiers in view: after 3 s two of them fade out over 3 s; with the
   default 5, corpses stay at least 75 s and are removed only when no player within 800 units looks towards
   them.
7. Play a female player model in deathmatch (`fem_play` base model): her corpse in the body queue uses the
   female gore setup (e.g. female cap models when a limb is shot off).
8. Rename a sequence file of a soldier so that it is missing: the console shows
   `m_<...> <targetname> couldn't play animation <name>!` when the AI asks for it.

### Common part: Open questions
- Meaning of the speed-scale field and of flags 0x00002000, 0x00004000, 0x00000800 and bits 28 and 29 (human
  body / actions specs).
- Exact yaw-rate formula of the root-motion event; per-character turn factor (AI spec).
- Snowcat/tank spawnflag 1 meaning and the vehicle aiming parameters' exact roles (enemy roster spec).
- The dog's "hit-flag word" bit 0x10000 and the last-hit timestamp source (AI damage path, SPEC-720).
- Whether the bull/cow inflicts damage anywhere (AI actions).
- Player corpses: whether anything removes queued corpses other than queue reuse (`ai_corpselife` appears
  ineffective).

## Constants

| constant | value |
|---|---|
| voice attenuation: tactical, wake, greeting / general | 1.6 / 2.0 |
| voice channel | 2 |
| general voice volume (Limp) | 0.8 (0.75) |
| greeting volume, wake volume when startled | 0.9 |
| global tactical / wake voice spacing | 2 s each |
| greeting repeat time per character | 10 s |
| wake voice suppression after a previous wake (startle) | 7 s |
| talking expression duration on greeting | 2 s |
| blindness level for blinded animations | ≥ 20 |
| knock-down chance and damage | 1/6, damage > 75 |
| disarm thresholds | armour ≤ 0.5 %, or health ≤ 50 % of maximum |
| dance / autofire follow-up delay | 0.3 s |
| lying-death follow-up minimum | 0.1 s |
| writhing duration | 3 + flrand(0,3) s |
| writhing chance | 1/3 (head must be attached) |
| "dead" expression after death start | 10 s, or at once when committed |
| corpse freeze | when the animation finished, or 5 s after death |
| burning-death spacing | 0.1 s (window −0.5 s … 0.1 s) |
| microwave death speed indices | 8, 9, 10, 11, 12 |
| direction-death / stumble chances | 3/4 each |
| stumble requires AI speed² | > 40 |
| face blink interval | flrand(3,5) s |
| breath tick intervals | 1.0–1.5 (states 2, 4), 1.25–2.0 (3), 2.5–4.5 s (others) |
| refire multipliers (ready-to-fire query) | ×2 limb injury; ×0.75 for the designated character |
| refire multipliers (paced shot) | ×2.5 limb injury; ×0.5 for the designated character |
| facing limits for firing | dot ≥ 0.717 (not for attack type 18), never behind |
| throw: min distance, teammate radius, facing, angle offset, max angle, vertical jitter | 200, 175, 0.707, 30°, 80°, 0–20 units |
| can-throw query: chance, teammate radius | 0.1, 200 |
| armour base per zone | 300 (boss 75 / 99 / 150 by skill) |
| armour type 6 factor, percentage scale, cap | 0.3; ×0.25; 20 |
| armour pickup minimum count | 50 |
| limb-injury reactions disabled at skill level | 4 |

## Test vectors / how to verify

1. Spawn a soldier (`m_irq_msoldier1`) on `irq1a` with `ai_goretest 1`: precache prints `voice cache:` lines for
   `sound/Enemy/so<n>/irq1a/Wake<i>.wav`; shoot him in the leg: `general voice sound: enemy/dth/arab/Scream<k>.wav`.
2. On `nyc1` the same soldier screams from `enemy/dth/skin`; a female (`m_*_f*`) from `enemy/dth/fem`; on `tut1`
   nobody screams.
3. Shoot a soldier in the head without killing him (armoured boss): from then on he never screams; shooting his
   throat dead still plays `Choke`.
4. Kill an unarmoured soldier with a pistol shot to the foot: `std_slegrt_2bk_n_n` or `std_sleglt_2bk_n_n`; with
   probability 1/3 he then grabs his leg (`lb_pholdlegto_*`, `lb_pholdleg_*`, voice `Writhe`) and dies 3–6 s later
   (`lb_pholdlegdie_*`).
5. Kill a standing soldier with the microwave (MPG) secondary fire: `std_smicro_n_n_n` faster than normal, Choke,
   `environ/pulsecook`; when it ends the body bursts (`Weapons/MPG/Burst.wav`, MPG splatter).
6. Set `lock_deaths 1` (parental lock): every standing death is `std_shead_2frnt_n_n`.
7. Kill an armoured soldier (armour type 1): an armour pickup appears under the corpse when the death animation
   ends; its amount is at least 50.
8. Watch a civilian with a face prefix talk after pressing use on it: face skins cycle `…nt1/nt2/nt3/n` for 2 s and
   `Greet<i>.adp` plays once; pressing use again within 10 s gives no greeting.
9. On `sib1` an idle soldier shows `enemy/breathe` at his mouth every 2.5–4.5 s; a corpse never does.
10. Shotgun soldier (attack type 11): after each shot he plays `std_xcock_n_s_2`; after 8 shots he reloads
    (`std_l_n_ms_2` or a moving reload variant).

## Open questions
- Callers of the tactical voice (`backup`, `throw`, `duck`, `retreat`, `help`, `react`): none found; and whether
  its existence check (path without `sound/`) can ever succeed.
- Which code sets expression state 3 (suffix `m`); the source of the refire factor F and of the "primary player"
  comparison.
- Exact meaning of the damage flags in the death table (SPEC-720 owns the table).
- The on-back autofire follow-up rule (see Dying follow-up) — confirm in game.
- Per-character-type sequence availability lists used by the original for the three human model directories
  (enemy-roster spec); behaviour is the same as checking the model's sequence files.

## Appendix: human animation table

Moved out of the spec (legal-game pass, 2026-10-26); the table is `game/src/damage/g3_humananims.inc`.
