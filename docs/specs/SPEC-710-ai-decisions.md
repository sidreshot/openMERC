# SPEC-710: Character AI decisions (goal selection: combat, search, dodge, retreat, patrol, scripts)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-700 (character AI framework: decision list, scheduling, AI record, combat profile, teams, alerts),
SPEC-701 (perception: records, alert sounds, danger notices), SPEC-711 (character actions), SPEC-722 (character
bodies: animation choice for move/stand/attack/jump/dodge requests, weapon handling), SPEC-750 (script VM: AI
command, scripted actions), SPEC-143 (navigation data), SPEC-760 (skill rows T-a … T-i), SPEC-752 (savegame
reference encodings), SPEC-303 (savegame container).

## Purpose
SPEC-700 defines that every character owns an ordered list of *decisions*; each think the decision with the
greatest priority is asked to *plan*, i.e. to queue one or more *actions* (SPEC-711). This spec defines every
decision kind of 1.07f: which characters receive which decisions, how each one computes its priority, what it
plans (attack, take cover, back off, close in, search, dodge, flee or cower, follow a patrol path, hold a combat
point, obey a script, the Dekker boss phases), the timings, ranges, probabilities and switching conditions, the
events that decisions react to, and the savegame records of decisions. It also gives the meaning of the combat
profile values and AI record fields that SPEC-700 left to the decision spec.

Terms: *owner* = the character entity; *target* = the enemy a decision currently attends to (kept per decision,
see "Target memory"); *designated character* = the character the character manager currently designates (the
nearest awake character to the sight client, SPEC-700 "Character manager"); *reaction age* = now − the AI's
reaction moment (SPEC-700 §14: the moment the current player record was (re)acquired after being older than 1 s);
*k* = 0.5 for the designated character, 1.0 for every other character; *profile[i]* = combat profile value i
(SPEC-700 §19, meanings below); *T-a … T-i* = the skill parameters of SPEC-760 B9.2 for the current toughness level.
"Body choice X" means the request of SPEC-722 that returns an animation record for situation X (move, stand/aim/idle,
jump, attack, dodge); the decision then turns the record into an action (rule D3).

## Interface / layout

### Decision kinds
The kind is the class code stored in savegames (SPEC-700 "Component class codes"). Priority = base priority +
bonus; the bonus is recomputed by the decision every time the AI decides (SPEC-700 rule 8: only a priority > 0 can
be chosen; equal priorities go to the earlier decision in the list).

| kind | chunk | bytes | role | base priority | bonus (see Behaviour) | removed when |
|---:|---|---:|---|---:|---|---|
| 0 | — | — | not creatable: prints `ERROR: only allowed to use leaves of the decision class tree` and makes an inert decision | 0 | — | — |
| 1 | `AIBD` | 36 | idle (vehicles, unknown codes) | 0 | 1 | its time limit expires (never, as created) |
| 2 | `AIPD` | 160 | combat | 0 | 1, or 2–3 with a target | its time limit expires (never, as created) |
| 3 | `AIOD` | 216 | inert (never wins) | 0 | 0 | time limit (never) |
| 4 | `AISD` | 232 | search | 0 | 1, or 2 with a target | time limit (never) |
| 5 | `AIDD` | 200 | dodge | 0 | 0, or 4 for one plan | time limit (never) |
| 6 | `AIRD` | 192 | retreat / cower | 0 | 0, 1 or 4 | time limit (never) |
| 7 | `AIHD` | 184 | follow `path_corner` chain | 0 | 1 | path ends, or the character was alerted within the last 1 s |
| 8 | `AIPD` | 184 | hold `point_combat` chain | 0 | 5 while alerted, else 0 | no point left |
| 9 | `AISD` (+`AION`, `SDSO`) | 220 | scripted (SPEC-750) | 25 | 15 | a script "end" action runs, or RESET_AI |
| 10 | — | — | invalid: prints `ERROR: invalid decision class code: %d` and makes an inert decision | | | |
| 11 | `AIPD` | 160 | Dekker phase 1 | 10 | as kind 2 | owner health < maximum health |
| 12 | `AIPD` | 160 | Dekker phase 2 | 8 | as kind 2 | owner health < 0.75 × maximum health |
| 13 | `AIPD` | 164 | Dekker: reach the console | 5 | as kind 2 | console reached |
| 14 | `AIPD` | 164 | Dekker: last stand | 2 | as kind 2 | never |
Any other code: as kind 10. Kinds 1 and 2–14 share the record prefixes described under "Savegame records".

### Decision sets given at spawn
Added in this order by the per-type set-up (after the `path_corner`/`point_combat` decisions of SPEC-700 rule 2,
which therefore come first in the list):

| set | decisions (list order) | rank (record offset 492) | classnames |
|---|---|---:|---|
| soldier | 2, 3, 4, 6, 5 | 0 | `m_nyc_epunk`, `m_nyc_eskinhead3`, `m_afr_eworker`, `m_nyc_fskinchick`, `m_sib_fguard2`, `m_cas_ffemale`, `m_tok_fassassin`, `m_x_ftaylor`, `m_x_mmerc`, `m_x_mhurtmerc`, `m_x_msam`, `m_x_mcharacter`, `m_x_mcharacter_snow`, `m_x_mcharacter_desert`, `m_nyc_mskinhead1`, `m_nyc_mskinhead2a`, `m_nyc_mskinhead2b`, `m_nyc_mswat`, `m_nyc_mswatleader`, `m_nyc_mpunk`, `m_afr_msoldier1a`, `m_afr_msoldier1b`, `m_afr_msoldier2`, `m_afr_msoldier3`, `m_kos_mgrunt1`, `m_kos_mgrunt2`, `m_kos_mgrunt3`, `m_kos_msniper1a`, `m_kos_msniper1b`, `m_kos_mcomtroop`, `m_kos_mbrute1a`, `m_kos_mbrute1b`, `m_kos_mrebel`, `m_kos_mklaleader`, `m_sib_mtrooper1a`, `m_sib_mtrooper1b`, `m_sib_mtrooper2`, `m_sib_mguard1`, `m_sib_mguard3`, `m_sib_mguard4`, `m_sib_mcleansuit`, `m_sib_mmechanic`, `m_irq_msoldier1`, `m_irq_msoldier2a`, `m_irq_msoldier2b`, `m_irq_mrepguard1a`, `m_irq_mrepguard1b`, `m_tok_mhench1`, `m_tok_mhench2`, `m_tok_mkiller`, `m_tok_mninja`, `m_tok_mbrute`, `m_cas_mraider1`, `m_cas_mraider2a`, `m_cas_mraider2b`, `m_cas_mbrute`, `m_cas_mrocket` |
| officer / boss | 2, 3, 4, 6, 5 | 3 | `m_kos_eofficer`, `m_kos_erefugee`, `m_irq_eofficer`, `m_afr_ecommander`, `m_sib_eofficer`, `m_sib_escientist1`, `m_x_mskinboss`, `m_x_mraiderboss1`, `m_x_miraqboss`, `m_afr_msniper`, `m_afr_mbrute`, `m_afr_mrocket`, `m_irq_mpolice`, `m_irq_mcommander`, `m_irq_mbrute1a`, `m_irq_mbrute1b`, `m_irq_mbodyguard`, `m_irq_mrocket`, `m_irq_msaddam`, `m_irq_mman2` |
| civilian | 2, 6 | 0 | `m_nyc_estockbroker`, `m_nyc_etourist`, `m_nyc_ebum`, `m_irq_eman1`, `m_nyc_fwoman`, `m_sib_fscientist2`, `m_irq_fwoman1`, `m_irq_fwoman2`, `m_tok_fwoman1`, `m_tok_fwoman2`, `m_x_romulan`, `m_nyc_mpolitician`, `m_kos_mmechanic`, `m_irq_moilworker`, `m_tok_mman1`, `m_tok_mman2` |
| animal | 2 | 0 | `m_x_husky`, `m_x_rottweiler`, `m_x_germanshep`, `m_x_bull` |
| Dekker | 11, 12, 13, 14, 5 | 3 | `m_x_mraiderboss2` (also gets the omniscience sense, SPEC-701) |
| vehicle | 1 | 0 | `m_x_chopper_black`, `m_x_chopper_green`, `m_x_chopper_white`, `m_x_hind`, `m_x_snowcat`, `m_x_tank` |
Two further sets exist in the module but no classname uses them: {2, 3, 4, 6} with rank 2, and {2, 3, 6}.
Characters created by scripts or spawners receive the set of their classname.

The *rank* (AI record offset 492, SPEC-700 "per-character value for decisions") decides right of way when two
characters block each other (rule C9).

### Combat profile meanings (completes SPEC-700 §19)
| index | meaning | where used |
|---:|---|---|
| 0 | preferred minimum distance d₀: closer than 0.75·d₀ is "too close" | range test (C2) |
| 1 | effective distance d₁: farther than 1.2·d₁ is "too far"; hit chance falls with (d₁/d)² beyond d₁ | range test, aim (SPEC-711) |
| 2 | dodge probability factor (× T-a per chance) | dodge (kind 5) |
| 3 | accuracy factor (× T-b) | aim (SPEC-711) |
| 4 | reaction delay: no attack while reaction age < profile[4]·k; aim ramp starts after T-d·profile[4] | C4, C5, SPEC-711 |
| 5 | aim settle time: aim ramps to full over T-d·profile[5] seconds | SPEC-711 |
| 6 | score for killing the character | SPEC-700 |
| 7 | flags, below | |
| 8 | body damage factor (not read by decisions or actions) | SPEC-722 |

Profile flags (profile[7]):
| bit | effect |
|---:|---|
| 1 | read by the hit/gore code only (SPEC-720/722) |
| 2 | never "too far": the character never closes in, it fights from where it is (sniper behaviour; C2, C5) |
| 4 | no reader in 1.07f |
| 8 | when backing off it does not shoot, and it backs off even when the retreat spot has no line of fire (C5) |
| 16 | never crouches (always stands to shoot or wait) |
| 32 | search: no shooting at remembered or heard positions (kind 4) |
| 64 | search: no knowledge of the target's live position during the 10 s after an alert (kind 4) |
| 128 | holds fire while closing in from too far (C5) |

### Skill parameters used by decisions and actions (completes SPEC-760 open question)
| parameter | toughness rows 0–4 | meaning |
|---|---|---|
| T-a | 0.3, 0.5, 1.0, 3.0, 99 | dodge chance multiplier (× profile[2]) |
| T-b | 0.25, 0.4, 1.0, 1.5, 2.0 | accuracy multiplier against players; error multiplier against non-players (SPEC-711) |
| T-c | 0.4, 0.5, 0.8, 1.0, 2.5 | distance scale of the aim error against non-player targets (SPEC-711) |
| T-d | 4.0, 2.0, 1.0, 0.5, 0.3 | time scale of aim reaction and settle, and refire factor F of SPEC-722 "Ready to fire"/"Firing" |
(Toughness row = sk_toughness level, SPEC-760: note that `skill 3` selects row 4 and `skill 4` row 3.)

### AI record fields defined here (SPEC-700 open question 6)
| record offset | meaning |
|---:|---|
| 28, 32 | right / left heel height above the floor reference (footstep detection, SPEC-722) |
| 284 | written by the search decision, never read (no effect) |
| 348 | "too close" latch of the range test (C2) |
| 352 | "too far" latch of the range test (C2) |
| 424 | face-the-player flag (set by `path_corner` spawnflag 4; SPEC-711 turning) |
| 428 | last retreat/sidestep direction (written by C5, cleared by the range test; no other reader) |
| 448 | maximum horizontal jump reach, 100 units (SPEC-711 jump) |
| 452 | jump height, 50 units (SPEC-711 jump) |
| 456, 464 | used only by vehicle AI (roster spec) |
| 468 | bit 1: "firing at a remembered position" (set by the search decision's blind fire, cleared by every combat plan) |
| 492 | rank (table "Decision sets") |

## Behaviour

### D1. Common decision state
Every decision keeps: creation time; bonus (int); base priority (int); *success* s (float, initial 0.5); an
optional time limit; the time and result of its last clear-shot test. Each evaluation of kinds 2, 4, 7, 8, 11–14,
after the bonus has been computed, relaxes s toward 0.5: with Δ = clamp(now − last relax, 0, 1),
s ← 0.05·Δ + (1 − 0.1·Δ)·s.
When an action that a decision queued finishes (SPEC-711 "completion"), the decision is told the action's
movement result m (1 = it moved as planned or did not need to, 0 = it failed or timed out):
- if the action is not of action kind 4 (attacks; SPEC-711), s ← 0.75·s + 0.25·m;
- kinds 2, 4, 6, 7, 8, 9: if m ≤ 0.25 the *unstick* rule C9 runs, otherwise a pending step point (C8) is kept only
  while it is still valid.

### D2. Bonus of the combat-type decisions (kinds 2, 4, 11, 12, 13, 14)
Each evaluation: update the target memory (D4); if the decision has a valid target (D4) **and** the owner's body
reports an armed character (SPEC-722), bonus = 2 + round(s) (i.e. 3 while s ≥ 0.5, else 2); otherwise bonus = 1.
Kind 4 resets s to 0.2 whenever its target is valid and to 0 otherwise before the relaxation, so its bonus is 2 with
a target and 1 without. Consequences for the soldier set: with a target the combat decision (kind 2, listed first)
wins ties and therefore leads; the search decision (kind 4) takes over once kind 2 has forgotten the target (45 s)
while kind 4 still remembers it (90 s), or never otherwise.

### D3. Turning an animation record into an action
A decision that receives an animation record from a body choice queues one action (SPEC-711 action classes),
chosen by the record's animation class (SPEC-722 records) and flags; a duration of 0 means "use the default":

| record class | condition | action created | default duration | other |
|---|---|---|---:|---|
| 1 locomotion | flag 0x00400000 | move (class 7) | 0.2 s | ends only at the end of an animation cycle |
| 1 locomotion | flag 0x1000 clear, or no target | move (class 7) | 0.5 s | |
| 1 locomotion | flag 0x1000 and a target | attack (class 13), no target entity | 0.25 s | ends at animation end |
| 2 attack | a living target entity | attack (class 13) with that target | 0.25 s | ends at animation end |
| 2 attack | otherwise | attack (class 13), no target | 0.25 s | ends at animation end |
| 3 jump | | jump (class 9) | 1.5 s | |
| 4 fall | | fall (class 10) | 0.4 s | |
| 5 hit reaction | | play in place (class 1) | 4.0 s | ends at animation end |
| 6 death | | death (class 3) | 9999 s | |
| 0 / other | flag 0x00400000 | play in place (class 1) | 4.0 s | ends at animation end |
| 0 / other | flag 0x1000 and a target | attack (class 13), no target | 0.25 s | |
| 0 / other | otherwise | play in place (class 1) | the animation's length | |
| 8 (script stand) | | end of script control (class 6) | | |
The action receives the destination and facing point given by the plan, the decision as its owner, and is added
at the back of the queue (SPEC-700 rule 8). No record → nothing is queued.

### D4. Target memory (combat-type decisions)
Each evaluation reads the newest player record and the newest non-player record of all senses (SPEC-701 §8).
- The current target is kept unchanged when it is a player or a character, is not the entity of either record, is
  in use and has health > 0.
- Otherwise: if the player record names a living entity whose record time is not older than the stored time (or
  within 0.01 s of it) and the decision's enemy test accepts it, it becomes the target; else the same for the
  non-player record. Accepting stores the target, its last known position = record position + the centre of the
  target's bounding box (mean of mins and maxs) and the record time.
- Enemy test: not on the same team (SPEC-700 §13). The retreat decision (kind 6) also accepts any character whose
  perceive-teammates flag is set.
- A target is *valid* while it is a player or character, in use, alive, and its record time is at most 45 s old
  (kinds 2, 11, 12, 13, 14), 90 s (kinds 4, 6) or 15 s (kind 3, which also requires an unarmed owner). An invalid
  target is forgotten.
The search decision additionally keeps a trail of the last three target positions that are more than 8 units apart
and a "seen within 0.2 s" mark.

### C1. Combat plan (kinds 2, 4, 11, 12, 13, 14; base for kind 6)
When a combat-type decision is asked to plan:
1. Airborne (no ground entity, movement type other than fly): plan "stand" (body stand choice with no facing) and
   stop.
2. No valid target: plan "stand facing forward" (facing point 1 unit ahead) and stop.
3. If the target is a player or character and an enemy: the owner's face shows expression 3 (body armed or no body)
   or 2, *afraid* (unarmed), for 5 s (unlocked); the owner is alerted (SPEC-700 rule 6) with the target, at the
   target's record time.
4. The range test (C2) runs from the owner's origin to the target's last known position.
5. In range → ideal plan (C4). Out of range → if a pending step point is set (C8) and executable, it is executed;
   otherwise non-ideal plan (C5).

### C2. Range test ("in ideal range")
Inputs: owner origin, target position, combat profile. The result code is kept for C5:
| order | condition | result |
|---:|---|---|
| 1 | owner spawnflag 128 (never moves, SPEC-722) | in range |
| 2 | the body reports an active limb injury (SPEC-722) | too close (code 2, sets the "too close" latch) |
| 3 | the target was last perceived more than 45 s ago | out of range (code 1) |
| 4 | distance < 10 | in range |
| 5 | the body has no current animation | out of range (code 1) |
| 6 | no clear shot (C3) from the owner to the target | out of range (code 3) |
| 7 | distance ≤ 0.75·profile[0], or the "too close" latch is set and distance ≤ 1.2·profile[0] | too close (code 2, latch set); farther than 1.2·profile[0] clears the latch |
| 8 | profile flag 2 clear and (distance ≥ 1.2·profile[1], or the "too far" latch is set and distance ≥ 0.75·profile[1]) | too far (code 1, latch set); otherwise the latch is cleared |
| 9 | otherwise | in range (code 0) |
The latches are AI record offsets 348 and 352. Kind 4 (search) never reports "in range" (it always uses its own
plan, C6); kind 6 uses its own test (C7); kinds 13 and 14 first walk to their landmark (C10).

### C3. Clear shot test
From the owner's eye (origin + view height), or from a given point used as it is, to the target's eye (origin +
view height; for a position without entity: the position raised 20 units). If the owner's right-hand attack type is the rocket
launcher (14) or the auto-shotgun (9) a box trace with mins (−10, −10, −6) and maxs (10, 10, 6) and content mask
0x6000081 is used, otherwise a line trace with mask 0x2000081. Clear when the fraction is above 0.95 and the trace
did not start in a solid, or the trace stopped on the target itself. If blocked and the target is a leaning
player, the test is repeated towards a point 16 units above the player's lean hitbox (box ±6 for the two weapon
types) and also passes when it hits the lean hitbox. The posture requested by the caller does not change the test
(the original computes the crouched box but overwrites it). The entity number of the blocker is reported to the
caller.

### C4. Ideal plan (in range)
Let S = time the decision last chose a standing posture, C = time it last chose a crouched posture (both start
long ago). *Crouching allowed* = profile flag 16 clear and (S > C + 3 or S < C) — i.e. not within 3 s after standing
up from a crouch.
- Reaction age < profile[4]·k: **wait** — body stand choice towards the target, crouched (C ← now) if crouching is
  allowed, otherwise standing (S ← now). No attack.
- Otherwise **attack**: when crouching is allowed, the owner's horizontal facing is within ~11.5° of the target
  (dot > 0.98) and C3 passes: crouched attack (body attack choice, crouched; C ← now). Else if C3 passes: standing
  attack (S ← now). Else **blocked**: crouch (if the same crouch conditions hold) or stand, facing the target.
The record is queued per D3 with the target entity.

### C5. Non-ideal plan (out of range: close in, back off, reposition)
1. *May attack* = result code ≠ 7 (search), not (code 1 with profile flag 128), and reaction age ≥ profile[4]·k.
2. Result code 6: body dodge choice to a random side (±1) → queued if any; otherwise code 5. Result code 5:
   crouched stand choice with `cch_xcchcower_mid_a_a` → queued if any; otherwise code 4. (Codes 5, 6 and 8 are
   handled but no 1.07f decision produces them; they can only come from a savegame.)
3. **Stuck**: owner spawnflag 128, or the owner is low priority (SPEC-700) and its target is the sight client → stand
   facing the target (no movement).
4. **Too close** (code 2, or code 8): ask navigation for a retreat path away from the target (navigation query
   "flee", SPEC-143 data; UNVERIFIED details in the navigation spec). If a path point P exists:
   - retreat direction r = horizontal unit vector towards P, bent away from other characters within 96 units in
     front of the owner (each neighbour at distance d deflects r towards the perpendicular with weight 1 − (d/96)²);
   - the step goal is 40 units along r; usability: code 8, profile flag 8, or a clear shot (C3) from 18 units along
     r — otherwise treat as "no retreat";
   - facing while backing: if r points away from the target (dot(r, to-target) < −0.717) the owner faces the target
     and back-pedals (if the body has none of `std_rbackaim_n_mrs_2`, `std_rbackaim_n_p_2`, `std_rbackaim_n_l_2`
     the step shrinks to 4 units); if r points towards the target (> 0.717) it faces along r; otherwise it strafes
     facing the target side (if the body has none of `std_raimstraf_rt_mrs_2`, `std_raimstraf_rt_l_2`,
     `std_raimstraf_rt_p_cbn` the step shrinks to 12 units);
   - with *may attack*, profile flag 8 clear and a clear shot (C3) to the target: **back off and attack** (body
     attack choice while moving to the step goal, which yields the aimed back-pedal/strafe/walk records, S ← now);
     else **back off without attacking** (move choice, S ← now).
   No retreat point: with *may attack* and a clear shot: crouched attack in place if crouching is allowed (C4) and a
   crouched clear shot exists (C ← now), else standing attack (S ← now); without: stand (S ← now).
5. **Other codes** (too far, out of time, no clear shot, flee 4, search 7): ask navigation for a path towards the
   target position (query "approach"). If none exists: with *may attack* and a clear shot → attack from the spot,
   else stand. If it exists, the step goal is 10 units towards the next path point; facing: when the code is not 4,
   the body can strafe and the target is closer than 448 units, the back-pedal/forward/strafe facing rules of step
   4 apply (with the same 4-unit back-pedal fallback; strafing needs the strafe records anyway); otherwise the owner
   faces along its path (500 units ahead).
   Then: if the step is at least √2 units and the owner is more than 50 units from the target (or the code is 4),
   and not (profile flag 2 and code ≠ 4): **move** (move choice; S ← now). Otherwise: code 4 → cower with
   `cch_xcchcower_mid_a_a` or `cch_xcchcower2_mid_a_a`; with *may attack* and a clear shot → **attack in place**
   (body attack choice from the current position); else crouch if allowed (C ← now), else stand.
6. With cvar `aipoints_show` ≠ 0 the planned path point and step goal are drawn as debug lines and a ring (no
   gameplay effect).

### C6. Search decision (kind 4)
Events: a "shot at me" notice or an alert-sound notice (rule E1) stores the heard position and time.
Plan (it never reports "in range"):
1. **Blind fire**: if a sound was heard within the last 8 s, or (the target was perceived within 4 s and profile
   flag 32 is clear): aim point = the heard position (within 8 s) else the oldest remembered trail position (else
   the last known position). If a clear shot (C3, no entity) to it exists, plan an attack at that point (body attack
   choice) and set AI record 468 bit 1.
2. Owner spawnflag 128: ideal plan (C4).
3. Destination: when the alert is older than 10 s or profile flag 64 is set, or there is no target: the heard
   position if heard within 20 s, else the target's last known position; otherwise (alert within 10 s, flag 64
   clear) the target's **current** origin (the searcher knows where the target is during 10 s after the alert).
4. Result code 7 (no attacks while moving, C5).
5. If the destination is within 64 units: look around — take a navigation node near the owner (the node list of
   the owner's grid cell, entry chosen by a pseudo-random index) and stand facing it. Otherwise C5 towards the
   destination.

### C7. Retreat / cower decision (kind 6)
State: *fear* f (0–1), danger kind K and time, the owner's health when it first saw its target.
- "Shot at me" notice: f ← min(1, f + 0.4). Every evaluation f ← max(0, f − 0.003).
- Danger notice of kind n at position p (SPEC-700 rule 12, SPEC-701 §8): stored when n ≥ the stored kind (time and
  position updated). Kinds sent in 1.07f: 1 (a death within 128 units) and 2 (a live explosive — C4, grenade,
  phosphor — within 160 units). Only kinds 2 and 3 have an effect.
- Bonus (low-priority characters: fear reset to 0, bonus left at its previous value):
  - unarmed body with f > 0 → 4;
  - otherwise, on the first evaluation or without a target remembered within 90 s → 1 (the health snapshot is
    taken);
  - otherwise, an explosive danger younger than 2 s and owner spawnflag 128 clear → 4;
  - otherwise, alerted and the body holds no right-hand weapon → 4;
  - otherwise → 1.
  So armed soldiers flee only from fresh explosives; unarmed characters (civilians, disarmed soldiers) flee or cower
  as soon as they are alerted or shot at.
- Range test of this kind: the owner's face shows *afraid* (expression 2) for 5 s. If the sight client is farther
  than 1000 units the decision marks "vanish" and reports in range; f > 0.7 → in range; unarmed with f > 0 → in
  range; otherwise out of range with code 2 when the stored danger kind is 2 or 3 (at any age), else code 4 (flee).
- In range: f > 0.7, or unarmed with f > 0 → **cower**: crouched stand choice with `cch_xcchcower_mid_a_a`, else
  `cch_xcchcower2_mid_a_a`. Otherwise stand facing the flee destination; if "vanish" was marked, the AI's view
  mode becomes 3 (SPEC-700: removed silently once out of the sight client's PVS).
- Out of range → C5 with the flee destination below.
- Flee destination: after a fresh explosive (< 2 s) the target's last known position (with code 2, so C5 backs
  away from it); otherwise (marks "vanish"): for characters on the sight client's team, the per-node flee spot of
  the navigation data for the owner's grid cell (SPEC-143 node tail value: the node index stored in tail2 — used
  for team 0 — or tail3, preferred for other teams; UNVERIFIED mapping of flags 0x8000/0x10000/4) when available,
  else and for all other characters the navigation "flee" query away from the target; when navigation reports
  that no point exists the owner's own position is used and "vanish" is cleared.
- Spawnflag 262144 (SPEC-700 rule 3): while the current action was queued by this decision, the first entity whose
  targetname equals the owner's `killtarget` is used once (activator = owner) and the flag is cleared.

### C8. Pending step points, doors, jumps
A combat-type decision can hold one *pending step point* (point, kind, time, entity, normal), set by C9:
- valid while it is younger than 1 s and at least 50 units away horizontally; otherwise discarded;
- kind 0: move to it; kind 1: body jump choice towards it; kind 2: play `std_jdrop_n_a_a` towards it; kind 3 (door):
  the door entity's use-key reaction is triggered (activator = owner) and `std_xopendoor_n_a_n` is played facing a
  point 20 units along the door's normal;
- older than 1 s: the owner stands facing forward first.
Steering: when moving towards a point (pending or planned) and the owner is not low priority, a box trace 60 units
ahead (owner box, top +16, mask 0x2020003) and a scan for characters within 100 units of the point 50 units ahead
(only those within 30 units of the path; nearest relative distance d²/22500 above 0.005 counts) measure the
clearance; if not fully clear (and the blocker is not a usable door) the direction is rotated by ±40°, ±80° (side
order random, up to 5 candidates, each tested 60 units out) and the most open one (clearance > 0.2) is used,
else the move is cancelled.

### C9. Unstick (after a failed action, D1)
Only when the owner is on the ground (or flying). A box trace 25 units forward from the owner (raised by its step
height, mask 0x2020003); *sidestep along v* means: unless a pending point younger than 1 s exists, pick the side
perpendicular to v (a random side: the reference is the owner's facing — or, with probability 1/4 when the last
sidestep is younger than 1 s, the previous sidestep direction — and the side flips when the reference's dot product
with the perpendicular is below a random threshold, uniform in [−1, 1], or r⁴ with r uniform in [−1, 1] while a
pending point or a recent sidestep exists) and set a pending point (kind 0) at (speed + 30) units to that side plus
half that distance along v; speed = length of the AI's velocity estimate. Then:
- nothing hit: sidestep along the backward direction;
- the trace hit the decision's own target: nothing;
- hit a character: with u uniform in [−20, 0], nothing happens when rank(other) − rank(owner) ≤ u — so a character
  always gives way to a blocker of higher or equal rank and to a lower-ranked one with probability
  1 − difference/20; it also gives way only when the dot product of the blocker's forward vector with the owner's
  forward vector is at most a uniform value in [−1, 1] (likely when they face each other); giving way = sidestep
  along the direction from
  the blocker to the owner with ±15 units of random jitter;
- hit a door-like entity (`func_door*`, or an entity whose use-key reaction is a door, button or useable trigger):
  usable now (for use-key doors: in their closed state and without spawnflag 8; used more than 5 s ago) → pending
  point kind 3 at the owner's position with the door's normal; otherwise, unless a pending point younger than 1 s
  exists, a pending point 70 units along the hit surface normal (kind 0);
- hit the world or another entity: sidestep along the hit surface normal.

### C10. Dekker (kinds 11–14, `m_x_mraiderboss2`)
- Each plan of kinds 11–14 first applies the armour shock: if the sight client is within 70 units it takes 5
  damage (no knockback, damage flag 0x2000000, means of death 14, two factors 0.5) and the effect
  `environ/dekarmor1` is played on Dekker.
- Kind 11 (priority 10–13) runs the combat plan until Dekker is first hurt; kind 12 (8–11) until health < 75 %;
  kinds 11 and 12 use the C5 variant with step parameters 3 and 100 respectively (UNVERIFIED meaning).
- Kind 13 (5–8): destination = the entity of classname `func_Dekker_console`; out of range while farther than 90
  units (horizontal); on arrival (once) the entities with targetname `dekkergun1`, `dekkergun2`, `dekkergun3` are
  used and the decision ends.
- Kind 14 (2–5): destination = `func_Dekker_diehere` until within 70 units; then Dekker becomes stationary
  (spawnflag 128) and fights with the normal range test.
- Kind 5 (dodge) is also present.

### C11. Dodge decision (kind 5)
Event "shot at me" (E1) stores the shot origin, the shot end point and the time.
Bonus 4 for one plan when all of: the owner has no limb injury (SPEC-722); spawnflag 128 clear; it has a target;
it is armed (or has no body); the last dodge was at least 3 s ago; a shot notice arrived within the last 0.3 s; the
target was perceived within the last 0.2 s; a uniform random number in [0, 1] ≤ T-a·profile[2]; the shooter lies
in front (dot(aim forward, owner − shot origin) ≤ −0.707). Direction: away from the shot line (left or right by
which side of the line the owner stands); a 64-unit side trace (box ±16, mask 3) must be clear, else a 64-unit
forward trace (dive forward), else no dodge. Otherwise the bonus is 0 and the notice is forgotten.
Plan: alert towards the target, body dodge choice in the chosen direction; if the dodge record is a backwards one
(flag 0x8000) the facing point is 200 units behind the aim direction. Last-dodge time ← now.

### C12. Path follow (kind 7) and combat points (kind 8)
Created from `target`/`combattarget` (SPEC-700 rule 2) with the first `path_corner` / `point_combat`.
- Priority: kind 7 bonus 1 (wins ties against the later combat decision); it is removed when the path has ended or
  when the owner was alerted within the last 1 s (so any alert ends patrolling for good). Kind 8 bonus 5 while the
  owner is alerted, 0 otherwise; removed when no point is left.
- Move: towards the current corner with the corner's `moveaction` animation (else the body's normal walk). Arrival:
  within 50 units. With cvar `ai_dumb` ≠ 0 the effect `edit/ignore` is shown on arrival.
- Waiting: wait time = the corner's `wait`; `wait` = −1 or corner spawnflag 2 → unlimited. `wait` between −0.1
  and 0.1 without spawnflag 2 → no stop (the next corner is taken at once). While waiting the corner's
  `waitaction1` … `waitaction6` animations play in order (each as a play-in-place action facing the corner's
  angle); after the last one the list restarts at 1 unless spawnflag 2 is set, in which case the wait ends once the
  list has been played. On a `point_combat`, seeing an enemy player alerts the character.
- Leaving: the corner's `pathtarget` entities are used (activator = the newest perceived player, else the owner);
  the next corner is the corner's `target`. Corner spawnflag 4 on arrival: the character stops for good (spawnflag
  128 + "no knockback") and turns to face the player every think (record 424). End of path: when the last corner
  (or for kind 8 any point left) has spawnflag 1, the character becomes stationary (spawnflag 128 + "no knockback").
- Missing animations print `path-traversal: %s not available for %s %s, using %s!`, `path-traversal: couldn't set
  %s for %s %s (but it's available!), using %s!`, `path-traversal: no body!`; with debug cvar `ai_pathtest` ≠ 0:
  `Finishing pathpoint: time %f`, `%d getting next point: old--%s, looking for %s!`, `waiting: time %f`.

### C13. Scripted decision (kind 9) and idle (kind 1)
- Kind 9 implements SPEC-750 §12.4: priority 40 while it exists (always wins); it plays the queued script actions in
  order and, with an empty queue, keeps the character standing: `std_ietalkpose_n_a_n` when the current animation
  is a talk animation (group 9), else the previous script animation if it was a looping idle fidget (group 3), else
  `std_istand_n_mrs_2` for right-hand attack types 6, 7, 9, 11, 12, 16, 17, 18, 29, `std_a_fwd_l_2` for type 14, else
  `std_istand_n_a_n` (`Botched idiocy! Couldn't set %s, using %s` when the body substitutes another record).
  Move commands complete within 11 units horizontally (or once passed); after a failed step (result < 0.5) the
  arrival radius becomes the owner's box half-diagonal. With cvar `ai_dumb` ≠ 0 the effect `scripted` is shown
  while scripted and `confusion` while the script queue is empty. Expression commands (SPEC-750 §12.5) are applied
  when reached.
- Kind 1: bonus 1; plans "stand facing forward". It keeps vehicles idle between script commands.
- Kind 3 is never chosen (bonus 0) in 1.07f.

### E1. Events received by decisions
| event | sent when (1.07f) | receivers |
|---|---|---|
| "shot at me" (shot origin, end point, shooter) | a player's bullet hits the world in single player and the character is within 800 units of the shooter and within about 20° of the shot direction (dot > 0.9397). Exception `m_x_mmerc`: if the line `impact/Hawk/LookOut.adp` was not played within the last 10 s it is played on the character (channel 2) instead of the notice; otherwise the notice is sent | also queued in the character's hearing as a kind-4 record at the shooter's origin (corrects SPEC-701 "no source emits kind 4"); kinds 4 (remember position), 5 (dodge), 6 (fear) |
| alert sound (position, emitter) | an alert sound with "tell decisions" (SPEC-701 §6); also a player bullet hitting the world when the character is in the forward half-space (0 < dot ≤ 0.9397) and in the PVS of the shooter or the impact — the position then passed is the unit direction from shooter to character, not a world position (original defect; UNVERIFIED in play) | kind 4 only |
| danger (kind, position) | SPEC-700 rule 12 | kind 6 only |

### Savegame records
Layouts of the chunk payloads (little endian; offset 0 ignored on load). Entity = entity number (−1 none);
animation = SPEC-752 animation reference (−1 none).

Common prefix (all kinds, `AIBD` = this prefix only, 36 bytes):
| offset | type | meaning |
|---:|---|---|
| 4 | float | creation time |
| 8 | int | bonus |
| 12 | int | base priority |
| 16 | float | success s |
| 20 | float | time limit (creation + limit, or creation + 10⁹) |
| 24 | int | has time limit |
| 28 | float | time of the last clear-shot test |
| 32 | int | result of that test |

Combat prefix (kinds 2–9, 11–14; `AIPD` 160 bytes for kinds 2, 11, 12 = this prefix only):
| offset | type | meaning |
|---:|---|---|
| 36 | float | (time) |
| 40 | float | time of the last sidestep choice |
| 44 | float[3] | last sidestep direction |
| 56 | float[3] | pending step point |
| 68 | float[3] | pending door normal |
| 80 | entity | pending door |
| 84 | byte (+3 pad) | pending step point set |
| 88 | int | pending kind (0 move, 1 jump, 2 drop, 3 door) |
| 92 | float | pending time |
| 96 | entity | target |
| 100 | float[3] | target's last known position |
| 112 | float | target record time |
| 116 | float[3] | steering direction |
| 128 | float | time of the last steering |
| 132 | int | last range-test result code |
| 136 | int | blocker entity number of the last clear-shot test |
| 140 | float | S (last standing choice) |
| 144 | float | C (last crouching choice) |
| 148 | float[3] | (cleared by unstick) |

Kind-specific tails (offsets from the start of the record):
| kind | chunk / size | tail |
|---|---|---|
| 3 | `AIOD` 216 | 160–215: fourteen 4-byte values (unused in play) |
| 4 | `AISD` 232 | 160 int, 164 int ("seen within 0.2 s" marks, initial 1), 168–179 three 4-byte values (no reader), 180 float heard time, 184 float[3] heard position, 196 float[3] trail position 1 (newest), 208 float[3] trail 2, 220 float[3] trail 3 (oldest) |
| 5 | `AIDD` 200 | 16 = s; 160 int, 164 float last dodge time, 168 float shot notice time, 172 int dodge direction (written 0), 176 float[3] shot origin, 188 float[3] shot end |
| 6 | `AIRD` 192 | 160 int health snapshot, 164 float fear, 168 byte vanish (+3), 172 int danger kind, 176 int danger time (whole seconds), 180 float[3] danger position |
| 7 | `AIHD` 184 | 160 float wait end, 164 entity corner waited at, 168 int wait-action index, 172 int (1 after arrival), 176 int waiting, 180 animation move action |
| 8 | `AIPD` 184 | as kind 7 |
| 9 | `AISD` 220 | 160 int finished, 164 entity, 168 int widened arrival, 172 float time the current script action started, 176 float[3] start position, 188 float[3] current destination, 200 float[3] direction to it, 212 animation of the last script action, 216 entity matched by RESET_AI; then `AION` (int count n) and n × `SDSO` (68 bytes, below) |
| 13, 14 | `AIPD` 164 | 160 int landmark reached |

`SDSO` scripted action (68 bytes): 0 animation; 4 float duration (used only when flag 0x40 is in the flag word);
8 float[3] facing direction; 20 float[3] destination; 32 int destination absolute; 36 byte (+3) "lethal" flag (the
attack makes the target's health 1 first, SPEC-711 attack); 40 int script action kind (SPEC-750); 44 int AI flag
word; 48 SPEC-752 script event (AI-done marker); 52 float default speed (steering limit 0.2 × speed); 56 entity
target; 60 int target-bolt selector for the shot (SPEC-722 firing); 64 int expression value (0x400 operand).
The target entity, the lethal flag and the bolt selector are passed to the action only when the lethal flag or the
bolt selector is non-zero. UNVERIFIED: which AI command operand (SPEC-750 §12.1, flag 0x200 or 0x80) fills fields
36 and 60; verify by saving after a scripted `ai` command with every optional operand.

## Constants
| name | value |
|---|---|
| success relaxation / feedback | toward 0.5 at 10 %/s; s ← 0.75·s + 0.25·m |
| combat bonus | 1 without target or unarmed; 2 + round(s) with |
| target memory | 45 s (kinds 2, 11–14), 90 s (4, 6), 15 s (3) |
| range test | 0.75 / 1.2 × profile[0]; 1.2 / 0.75 × profile[1]; < 10 units in range; > 45 s out of time |
| crouch/stand lock | 3 s after standing up from a crouch |
| crouch-attack facing | dot > 0.98 |
| reaction delay factor k | 0.5 (designated character), 1.0 |
| clear shot | fraction > 0.95; masks 0x2000081 / 0x6000081; box ±10, ±10, ±6; lean retry +16 (box ±6); point targets +20 |
| back-off | neighbour radius 96; test point 18; step 40; no back-pedal 4; no strafe 12; facing thresholds ±0.717; facing points 2440 units |
| close-in | step 10; strafe/back-pedal facing only below 448 units; stop distance 50 (2500 squared); minimum step² 2; forward facing point 500 |
| attack-while-backing test point | 64 units towards the target |
| search | heard position 8 s (shoot) / 20 s (go); target live position 10 s after alert; look-around within 64 units; trail spacing 8 units |
| dodge | 3 s between dodges; notice ≤ 0.3 s; target seen ≤ 0.2 s; front cone dot ≤ −0.707; traces 64 units, box ±16; backward facing 200 |
| retreat | fear +0.4 per shot notice, −0.003 per evaluation; cower above 0.7; explosive danger 2 s; vanish beyond 1000 units; afraid face 5 s |
| path / points | arrival 50 units; zero-wait band ±0.1; unlimited wait for −1 or spawnflag 2 |
| pending points | life 1 s; minimum distance 50; door 20 units; unstick trace 25; obstacle normal 70; sidestep (speed + 30) |
| steering | look-ahead 60; crowd point 50 ahead, radius 100, path band 30; rotations ±40°, ±80°; clearance > 0.2 |
| Dekker | shock radius 70, damage 5, means of death 14; console 90 units; diehere 70 units |
| scripted | priority 25 + 15; arrival 11 units |

## Test vectors / how to verify
All UNVERIFIED in game until run on the original.
1. **Reaction delay.** `skill 2` (toughness row 2, T-d 1.0), `m_kos_mgrunt2` (tier 0, weapon class 6: profile[4] =
   0 → no delay): it shoots on the first plan after seeing you. `m_x_mraiderboss1` (profile[4] 0.38) waits about
   0.38 s (0.19 s when it is the character nearest to you) before its first shot.
2. **Range bands.** `m_irq_msoldier1` (class 7 sniper: d₀ 512, d₁ 1024, flags 10): approach to 300 units — it backs
   away until about 615 units (1.2·512) — and never closes in from far (flag 2).
3. **Crouch lock.** A soldier in range that has just stood up does not crouch again within 3 s (watch posture
   changes in `ai_goretest 1` mode).
4. **Search.** Break line of sight with an alerted grunt and wait 50 s: after 45 s it stops attacking/closing and
   walks to your last position, then looks around (stands facing nearby nodes).
5. **Dodge.** At toughness row 4 (T-a 99) fire past a grunt facing you: it rolls sideways on the first shot; the
   next roll comes at least 3 s later.
6. **Retreat.** Throw a grenade within 160 units of an armed soldier who has seen you: he backs away from you for
   2 s. Shoot near a civilian: he cowers (fear > 0.7 after two shots) and, once you are 1000 units away and out of
   PVS, disappears (SPEC-700 silent removal).
7. **Patrol.** A grunt with a `path_corner` chain (`wait 2`, `waitaction1 std_ilooking_n_mrs_2`): walks, stops
   2 s playing the animation, continues; shooting near it ends the patrol permanently.
8. **Dekker.** In ger4 the first hit switches Dekker to phase 2; below 75 % health he walks to the console and the
   three `dekkergun` entities fire.
9. **Savegame.** Save with a soldier in combat: its decisions write `AIPD` 160, `AIOD` 216, `AISD` 232, `AIRD` 192,
   `AIDD` 200 in list order.

## Open questions
1. Exact navigation queries ("flee", "approach", flee-spot per node) are defined by the navigation code (WP5); this
   spec only states their use.
2. The Dekker C5 variant parameters 3 and 100.
3. Whether kind 3 had a purpose (its plan pieces exist but its bonus is always 0).
4. The alert-sound notice from bullet impacts passes a direction as a position (E1); confirm its in-game effect.
5. Which body records carry flag 0x8000 (backwards dodge) — SPEC-722 table.
