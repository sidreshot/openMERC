# SPEC-700: Character AI framework
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (game module), version 1.07f
Depends on: SPEC-001 (game module interface: imports #55, #48 string print, savegame chunks), SPEC-006 (game module
map: `m_*` classnames, `monster_start` equivalent), SPEC-010/SPEC-011 (Ghoul instances), SPEC-131 (string packages),
SPEC-140 (scripts), SPEC-143 (navigation data), SPEC-303 (savegames), SPEC-701 (character perception).
Companion specs still to come: character decisions and actions, character bodies and gore, the character roster
(per-classname setup), skill tables.

## Purpose

Every single-player character (classnames `m_*`, 105 of them, plus dogs, helicopters, the tank, the snowcat, the bull,
cut-off body parts and player corpses) is driven by a per-entity *character AI object*. This spec describes that
object: its parts and their contracts, how it is created, the order in which it runs every server frame, how it
wakes up, how it reacts to damage and death, the rules for teams, priorities and statistics, its interfaces to the
Ghoul body, navigation and scripts, and its savegame records. What individual decisions and actions do, and how the
body picks animations and handles gore, belong to the companion specs; perception (sight, hearing, memory of
targets) is SPEC-701.

## Interface / layout

### Parts of a character AI object

| part | count | role |
|---|---|---|
| owner | 1 | the character's entity record |
| target | 0–1 | the entity the character currently attends to (set when alerted, read back by decisions) |
| body | 0–1 | animation and gore controller for the character's Ghoul instance (companion spec) |
| Ghoul model object | 0–1 | the shared model/skin description the body was built from |
| senses | ordered list | perception components (SPEC-701); new ones are appended |
| decisions | ordered list | goal components; the best one is asked to plan |
| action queue | ordered list | pending animation-driven actions; front = next |
| current action | 0–1 | the action being executed this frame |
| previous action | 0–1 | the last finished action, kept so that a new action of the same kind can reuse it |
| combat profile | 15 values | ranges, accuracy and timing scales, flags, kill score, personality (below) |
| bounding-box presets | 9 | mins/maxs pairs selected by animation (below) |
| state values | see "Saved record" | awake flag, alert state, view mode, priority, timers |

All parts are created and destroyed together with the AI object, except that decisions, actions and senses can be
removed individually when they report that they are finished.

### Character type codes

The AI object is chosen by a numeric *type code*. The code is also what the savegame stores for each character
(see "Savegame chunks"). Columns: team = value used by the team rules; innocent = the type counts as an innocent
bystander (see "Player-caused casualties"); family = shared base behaviour (human male, human civilian/officer,
human female, or a special type).

| code | classname(s) | team | innocent | family |
|---:|---|---:|:---:|---|
| 6 | `m_x_bull` | 100 | | special |
| 7 | — (unused; creates a plain AI object) | 1 | | — |
| 8 | `m_x_chopper_black`, `m_x_chopper_green`, `m_x_chopper_white`, `m_x_hind` | 10 | | helicopter |
| 9 | `m_x_tank` | (own rules) | | vehicle |
| 10 | `m_x_snowcat` | (own rules) | | vehicle |
| 11 | `m_x_husky`, `m_x_germanshep`, `m_x_raiderdog` | 1 | | dog |
| 12 | `m_x_rottweiler` | 1 | | dog |
| 13 | cut-off body part (created by the gore code, no classname of its own) | 1 | | special |
| 14 | player corpse in the body queue | 1 | | special |
| 15 | `m_nyc_eskinhead3` | 3 | | civilian/officer |
| 16 | `m_nyc_estockbroker` | 0 | | civilian/officer |
| 17 | `m_nyc_etourist` | 0 | | civilian/officer |
| 18 | `m_nyc_ebum` | 0 | yes | civilian/officer |
| 19, 20 | — (no spawn function) | 0 | | civilian/officer |
| 21 | `m_irq_eman1` | 1 | yes | civilian/officer |
| 22 | `m_afr_ecommander` | 1 | | civilian/officer |
| 23 | `m_kos_eofficer` | 1 | | civilian/officer |
| 24 | `m_kos_erefugee` | 0 | | civilian/officer |
| 25 | `m_irq_eofficer` | 1 | | civilian/officer |
| 26 | `m_afr_eworker` | 1 | | civilian/officer |
| 27 | — (no spawn function) | 1 | | civilian/officer |
| 28 | `m_sib_eofficer` | 1 | | civilian/officer |
| 29 | `m_sib_escientist1` | 1 | yes | civilian/officer |
| 30 | `m_nyc_epunk` | 3 | | civilian/officer |
| 31 | `m_x_mcharacter` | 0 | | male |
| 32 | `m_x_mcharacter_snow` | 0 | | male |
| 33 | `m_x_mcharacter_desert` | 0 | | male |
| 34 | `m_x_mmerc` | 0 | | male |
| 35 | `m_x_mhurtmerc` | 0 | | male |
| 36 | `m_x_msam` | 0 | | male |
| 37 | `m_irq_moilworker` | 1 | yes | male |
| 38 | `m_nyc_mpunk` | 3 | | male |
| 39 | `m_x_miraqboss` | 1 | | male |
| 40 | `m_x_mraiderboss1` | 1 | | male (own setup) |
| 41 | `m_x_mraiderboss2` | 1 | | male (own setup) |
| 42–54 | `m_irq_msoldier1`, `m_irq_msoldier2a`, `m_irq_msoldier2b`, `m_irq_mrepguard1a`, `m_irq_mrepguard1b`, `m_irq_mpolice`, `m_irq_mcommander`, `m_irq_mbrute1a`, `m_irq_mbrute1b`, `m_irq_mbodyguard`, `m_irq_mrocket`, `m_irq_msaddam`, `m_irq_mman2` (in this order) | 1 | 54 only | male |
| 55 | `m_x_romulan`, `m_afr_msniper` | 1 | | male |
| 56–59 | `m_afr_msoldier1a`, `m_afr_msoldier1b`, `m_afr_msoldier2`, `m_afr_msoldier3` | 1 | | male |
| 60 | `m_nyc_mswat` | 0 | | male |
| 61 | `m_nyc_mswatleader` | 0 | | male |
| 62–66 | `m_cas_mraider1`, `m_cas_mraider2a`, `m_cas_mraider2b`, `m_cas_mbrute`, `m_cas_mrocket` | 1 | | male |
| 67 | `m_sib_mtrooper2` | 1 | | male |
| 68 | `m_sib_mcleansuit` | 1 | | male |
| 69–77 | `m_kos_mgrunt1`, `m_kos_mgrunt2`, `m_kos_mgrunt3`, `m_kos_msniper1a`, `m_kos_msniper1b`, `m_kos_mcomtroop`, `m_kos_mbrute1a`, `m_kos_mbrute1b`, `m_kos_mmechanic` | 1 | 77 only | male |
| 78 | `m_kos_mrebel` | 0 | | male |
| 79 | `m_kos_mklaleader` | 0 | | male |
| 80–83 | `m_nyc_mskinhead1`, `m_nyc_mskinhead2a`, `m_nyc_mskinhead2b`, `m_x_mskinboss` | 3 | | male |
| 84 | `m_nyc_mpolitician` | 0 | | male |
| 85 | `m_tok_mman2` | 0 | yes | male |
| 86–90 | `m_tok_mhench1`, `m_tok_mhench2`, `m_tok_mkiller`, `m_tok_mninja`, `m_tok_mbrute` | 1 | | male |
| 91 | `m_tok_mman1` | 0 | | male |
| 92 | `m_afr_mbrute` | 1 | | male |
| 93 | `m_afr_mrocket` | 1 | | male |
| 94–98 | `m_sib_mtrooper1a`, `m_sib_mtrooper1b`, `m_sib_mguard1`, `m_sib_mguard3`, `m_sib_mguard4` | 1 | | male |
| 99 | `m_sib_mmechanic` | 1 | yes | male |
| 100 | `m_nyc_fskinchick` | 3 | | female |
| 101 | `m_nyc_fwoman` | 0 | | female |
| 102 | `m_sib_fguard2` | 1 | | female |
| 103 | `m_sib_fscientist2` | 1 | | female |
| 104 | `m_irq_fwoman1` | 1 | yes | female |
| 105 | `m_irq_fwoman2` | 1 | yes | female |
| 106 | `m_tok_fwoman1` | 0 | | female |
| 107 | `m_tok_fwoman2` | 0 | | female |
| 108 | `m_tok_fassassin` | 1 | | female |
| 109 | `m_cas_ffemale` | 1 | | female |
| 110 | `m_x_ftaylor` | 0 | | female |

Any other code prints `ERROR: invalid ai class code: %d` and creates a plain AI object. Several classnames that
are not listed (for example `m_x_mcharacter` variants) share a code with a listed one only where the table says so;
the m_* spawn behaviour itself (models, skins, weapons, health overrides) is the roster spec.

### Component class codes (for savegames)

Senses, decisions, actions and bodies are also created from class codes when a savegame is loaded. Codes the
framework itself creates at run time:

| kind | code | what |
|---|---:|---|
| sense | 3 | sight (SPEC-701) |
| sense | 4 | hearing (SPEC-701) |
| sense | 5 | omniscient tracking (SPEC-701) |
| sense | 6 | proximity, radius from key `alertradius` (SPEC-701) |
| decision | 7 | follow a `path_corner` chain (from key `target`) |
| decision | 8 | hold a `point_combat` (from key `target` or `combattarget`) |
| decision | 9 | scripted: holds the commands that scripts give to this character, priority 25 |
| action | 4 | play one death animation and hold it (used with spawnflag 65536, below) |

The full code lists (sense 0–6, decision 0–14, action 0–21, body 3–13) and the meaning of the others belong to
SPEC-701 and the companion specs.

### Saved record of the AI object (`AIAI`, 588 bytes)

Byte offsets inside the 588-byte record; little-endian. "id" = object id from the savegame id space (0 = none);
"entity" = entity number (−1 = none).

| offset | type | default | meaning |
|---:|---|---|---|
| 0 | 4 bytes | — | ignored on load (writers may store 0) |
| 4 | float[3] | 0 | the character's own movement velocity (read by the field-of-view rule, SPEC-701) |
| 16 | float[3] | 0 | angles of the character at the moment of death |
| 28 | float, float | 0, 0 | two per-character values read and written only by decisions/actions (companion spec) |
| 36 | int | 0 | first-think-done flag (see "Per-frame update") |
| 40 | int | 0 | unused by the framework |
| 44 | float[9][3] | 0 | bounding-box preset mins, presets 0–8 |
| 152 | float[9][3] | 0 | bounding-box preset maxs, presets 0–8 |
| 260 | int | 9 | index of the preset in use (9 = none) |
| 264 | float | 0 | smoothed floor height used when the preset changes |
| 268 | int | 0 | alerted flag (set by an alert or by damage) |
| 272 | float | 0 | time of the most recent alert |
| 276 | float | 0 | reaction moment (see "Perception summary"; read by decisions) |
| 280 | int | 0 | perceive-teammates flag (lets the sight sense report teammates; SPEC-701) |
| 284 | float | 0 | per-character time value for decisions (companion spec) |
| 288 | float | −20 | "recently called" stamp: time of the last alert call heard + 12 s |
| 292 | 96 bytes | see text | navigation location cache: filled from the `.nvb` data; four ints at 356–368 default −1 |
| 388 | entity | — | owner |
| 392 | entity | none | target |
| 396 | id | 0 | current action |
| 400 | id | 0 | previous action |
| 404 | int | 0 | ground link count seen at the last ground check |
| 408 | int | 0 | awake flag (SPEC-701 calls an awake character "active") |
| 412 | int | 1 | call-for-help pending (cleared by the first alert, see rule 6) |
| 416 | int | 0 | hostage (spawnflag 16384) |
| 420 | int | 0 | buddy (ally whose death by the player is a casualty) |
| 424 | int | 0 | per-character value for decisions (companion spec) |
| 428 | float[3] | 0 | per-character point for decisions (companion spec) |
| 440 | float | 1.0 | size scale (multiplies the bounding-box presets and the model) |
| 444 | int | 7 | sense mask passed to every sense update (never changed) |
| 448 | float | 100 | parameter for decisions (companion spec) |
| 452 | float | 50 | parameter for decisions (companion spec) |
| 456 | float | 1 000 000 | parameter for decisions (companion spec) |
| 460 | float | 24 | step height (ledge and position checks) |
| 464 | int | 7 | parameter for decisions (companion spec) |
| 468 | int | 0 | unused by the framework |
| 472 | int | 1 | low priority (SPEC-701: "sense throttle"; see "Character manager") |
| 476 | int | 0 | whole second of the last sense update |
| 480 | int | 1 | view mode: 0 narrow, 1 normal, 2 wide (awake), 3 vanish when unseen |
| 484 | int | 0 | per-frame flag, cleared at the end of every think |
| 488 | int | 0 | whole second of the next corpse check |
| 492 | int | 0 | per-character value for decisions (companion spec) |
| 496 | float[15] | see "Combat profile" | combat profile |
| 556 | id | 0 | body |
| 560 | id | 0 | Ghoul model object |
| 564 | float[3] | 0 | aim angles (where the character looks or aims) |
| 576 | float[3] | owner angles | facing angles (direction of the body; the field-of-view rule uses them) |

The three component lists (senses, decisions, action queue) are not part of the record; they follow as separate
chunks.

## Behaviour

### 1. Creation

1. The m_* spawn behaviour (roster spec) first runs the common character setup of SPEC-006 ("Characters"): step
   movement, bounding box (−16, −16, −32)…(16, 16, 41), mass 200, gravity 0.01, monster flag, health and maximum
   health 100 (1000 when cvar `ai_goretest` ≠ 0), take-damage on, drop to floor when spawnflag 16 is clear and level
   time < 1.0 s, then not solid until the character wakes, next think 0.1 s later.
2. It then creates the AI object for its type code, in this order:
   1. the object for the code is made and the entity becomes its owner;
   2. the family's preparation step runs, then the type's own setup (body, model, skins, bounding-box presets,
      weapon; roster spec);
   3. for codes 15–110 the combat profile is copied from the per-type table (below);
   4. the copy is adjusted on training and early maps: when the map name starts with `tsr` or `trn`, or starts with
      `irq` and has `a` as its fifth character (`irq1a`, `irq2a`, `irq3a`): profile[4] increases by 0.25,
      profile[3] is multiplied by 0.75, profile[5] increases by 2.0 and profile[6] is multiplied by 0.5;
   5. the engine's per-leaf value at the owner's origin (SPEC-001 import #55) is read; if it is not −1, profile[3]
      is multiplied by 0.33 when the value is ≤ 1200 and by 0.66 when it is greater;
   6. personality: if the entity key `personality` is 0 (or absent) a random row 0–5 is chosen, otherwise row
      `personality` − 1; rows outside 0–5 leave the personality values at 0. The row's six values become
      profile[9…14] (table under Constants).
3. The per-type setup (`monster_start_go` equivalent) then adds the initial decisions and senses
   (see "Spawn-time decisions and senses") and installs the character's touch, use and player-use reactions.

The adjustment of step 2.4 is applied by a copy routine that also runs when the record is saved and when it is
loaded, so on the affected maps each save-and-load cycle applies it again (twice per cycle). UNVERIFIED in play;
verify by saving and loading repeatedly on `trn1` and watching enemy accuracy and pauses change.

### 2. Spawn-time decisions and senses

- Key `target` naming a `path_corner`: a path-follow decision is added. Naming a `point_combat`: a point-combat
  decision is added and the call-for-help flag is cleared.
- Key `combattarget` naming a `point_combat`: a point-combat decision is added and the call-for-help flag is cleared.
- Spawnflag 32768: the body is told to use protection class 0 (body companion spec).
- Spawnflag 16384: the character is a hostage. Spawnflag 8192: the call-for-help flag is cleared.
- Senses, added in this order: hearing unless spawnflag 4; sight unless spawnflag 2; proximity with radius
  `alertradius` (75 when the key is absent or < 0.1); omniscient tracking when cvar `ai_pathtest` ≠ 0.
- Spawnflag 128: the entity flag "no knockback" is set.

### 3. Character spawnflags (all `m_*` classnames)

| value | meaning |
|---:|---|
| 2 | starts blind: no sight sense until the character is used (triggered) or alerted |
| 4 | starts deaf: no hearing sense until used or alerted |
| 8 | no random size variation (dogs only: otherwise their size scale is random in [1.0, 1.15]) |
| 16 | dormant: not dropped to the floor, never woken by proximity or corpses, excluded from the character manager's active list; wakes only when used |
| 32 | inattentive until alerted: narrower view and less sensitive to motion (SPEC-701); cleared by an alert |
| 64 | wakes on its first think |
| 128 | no knockback |
| 8192 | never calls for help when first alerted |
| 16384 | hostage |
| 32768 | body protection class 0 (body companion spec) |
| 65536 | dies "in place": keeps its current action when killed and uses a held death pose; not counted for the SWAT rule |
| 262144 | when the current action belongs to a decision of kind 6, the entities named by `killtarget` are used once and the flag is cleared |
| 1048576 | with key `wait` = 1: turns every frame to face player 1 (yaw only) |

UNVERIFIED for 32768, 65536 and 262144 beyond the effects listed; verify with a test map per flag.

### 4. Reactions installed on the character entity

- **Touch** by a player, while the character's movement type is not none/push/stop or the special value 9: the
  body is told "touched by the player".
- **Use key** (player presses use on the character), same movement condition: if the player is on the character's
  team (see "Teams"), the character's perceive-teammates flag is set; the body is told "used by the player". With
  cvar `ai_pathtest` ≠ 0 the character is also pushed away from the player (velocity += unit direction × 50000 /
  max(mass, 50)), its ground is cleared, and the animation names of every `path_corner` and `point_combat`
  (`waitaction1`…`6`, `moveaction`) are printed as `%s.ghl` between the lines `starting path_corner anim list:`,
  `end path_corner anim list, starting point_combat anim list` and `end point_combat anim list`.
- **Use** (targeted by a trigger or script), with activator A:
  - asleep and dormant (spawnflag 16): every entity within 40 units that has entity flag 65536 is removed silently
    (rule 9), then the character wakes;
  - otherwise: the deferred sight/hearing senses are added (spawnflags 2/4 cleared), the character wakes if asleep,
    and if there is an activator the character is alerted by it (rule 6) and A is sent to it as a kind-5 "told"
    perception event (SPEC-701).
  - Scripts that use a character also clear its call-for-help flag.

### 5. Waking up

A character is *asleep* until woken; asleep characters still think every 0.1 s but only run the wake checks.
Waking does: awake flag on, movement type step, solid bounding box, spawnflag 16 cleared, view mode 2 (wide). For
type 41 (`m_x_mraiderboss2`) the position is also un-stuck: if the bounding box starts in a solid, the origin is
raised 32 units, and if still stuck lowered 64 units from there. Wake conditions, checked in this order every think
while asleep:
1. cvar `ai_goretest` ≠ 0 and spawnflag 16 clear;
2. spawnflag 64;
3. at most once per second (whole-second timer) and only when spawnflag 16 is clear: a dead character (death flag
   set) lies within 200 units;
4. a sight client exists, spawnflag 16 is clear and the squared distance to it is < 640 000 (800 units).
If none holds the think ends immediately (nothing else runs).

### 6. Alert

An alert carries a time T, a source entity S and a position P. It is raised by "use" with an activator and by
decisions that react to perception (decision spec). Damage sets the alerted flag directly (rule 10) without the
other effects listed here.
1. If the character was not yet alerted, P is closer than 1000 units to the owner and the call-for-help flag is set:
   the flag is cleared; when S is a player and the character has a body, the body plays its "wake" reaction
   with weight 0.9 and a kind-7 "alert call" alert sound of radius 500 (SPEC-701 §6) is broadcast from the character's origin
   with S as emitter (SPEC-701).
2. The alerted flag is set and the latest-alert time becomes max(old, T).
3. Except for types 41 and 83: spawnflag 32 is cleared and the deferred sight/hearing senses are added.
4. The target becomes S.
"Alerted recently" means alerted and T within the last 1.0 s.

### 7. Per-frame update

Server frame (order inside the game frame; 10 frames per second, level time = frame number × 0.1 s):
1. Nothing runs while cvar `freezeworld` ≠ 0.
2. The sight client and sight character advance (SPEC-701 and "Character helper rules").
3. The character manager runs (rule 12).
4. Entities run in entity-number order. For each in-use non-player entity: if its ground entity's link count changed
   the ground is cleared, and a character without fly/swim flags re-checks its ground; if it has a Ghoul instance and
   cvar `ghl_no_server_update` < 0.5 the instance's event processing is advanced to the level time (SPEC-011 instance
   method 45, which delivers due animation events); then the entity's
   physics and think run. For players: if the dead-hostage count has reached the level limit (rule 11) the player is
   killed (mission failure); then the normal player frame.

Character think (runs when the entity's next-think time ≤ level time + 0.001; the next-think time is cleared first):
1. On the very first think of a character the entity's own think callback runs once first (this is the per-type
   setup of rule 2); the AI think below runs on every think, including the first.
2. The previous frame's helper entities are removed (see "Helper entity").
3. Next think = level time + 0.1.
4. Spawnflag 262144 rule and spawnflag 1048576 rule (table in rule 3).
5. Cinematic freeze: while the game is in a cinematic freeze (SPEC-001 cinematic state), a character that is not a
   cinematic actor (entity `count` ≠ 255) and whose alerted flag or entity flag 65536 is set is frozen: entity flag
   131072 set, made non-solid, its next-think time is stored negated as the remaining delay, its Ghoul instance is
   switched off at the current level time (SPEC-011 instance method 4), and the think ends.
6. Debug: with cvar `ai_dumb` ≠ 0, the effect `cantsee` is played on characters with spawnflag 2 and `canthear` on
   characters with spawnflag 4. With cvar `ai_freeze` ≠ 0 the think ends here.
7. With `ai_goretest` ≠ 0 and spawnflag 16 clear the character wakes. Asleep characters run the wake checks
   (rule 5) and stop if still asleep.
8. **Senses**: unless the character is low priority and its last sense update was less than 2 s ago (whole-second
   stamp ≤ level time − 2 must hold), every sense updates; senses that report "finished" are removed. The stamp
   becomes the whole second of level time + 1. Then perception is summarised (SPEC-701; "Perception summary").
9. **Plan**: if there is no current action: if the action queue is not empty the next action starts (rule 8), else
   the decisions are consulted (rule 8). If there is still no current action, go to step 11.
10. **Act**: if the "recently called" stamp lies within the last second (level time − 1 < stamp < level time) and the
    character has a body, the stamp is set to level time − 20. The current action executes one frame; when it reports completion, the next action starts.
11. Ground check when the ground entity's link count changed; water level; world effects (drowning, lava, slime;
    "Character helper rules"); the per-frame flag is cleared.
12. A low-priority character with entity flag 65536 (made by a spawner), or a character in view mode 3, is removed
    silently (rule 9; only happens when it is out of the sight client's PVS).
13. A character with health ≤ 0 whose parts all report "ready to be removed" (rule 10) is freed together with its
    body's remains.
14. A new helper entity is made for the character (see "Helper entity").

### 8. Decisions and actions (scheduling contract)

- **Deciding**: every decision in list order reports "finished" or a priority = base priority + bonus (two integers
  kept by the decision). Finished decisions are removed and destroyed. Of the others, the first one with the
  strictly greatest priority is asked to plan (it queues actions), and then the next action is started.
- **Next action**: the finished current action becomes the previous action (an older previous action is destroyed);
  if the queue is not empty its front is removed and becomes current, and it is started (given the AI object and the
  owner).
- **Queueing**: actions can be added at the back or at the front of the queue. Adding at the back can also wake the
  character.
- **Interrupting**: a new action can pre-empt the current one: the current action is told it is interrupted and is
  put back at the front of the queue; the new action becomes current and is started at once.
- **Removing a decision of a kind**: the first decision that matches (scripted decisions match the entity given;
  with no entity, decisions of kind 9) is removed; every queued action that this decision produced is cancelled and
  destroyed; if the current action came from it, the next action is started. Scripts use this as "reset AI"
  (SPEC-140; message `entity %s had no ai, aborting HandleResetAI` when the entity has none).
- **Reuse**: the standard action-making requests first look at the previous action: if it is of the requested kind
  it is re-initialised and reused instead of making a new one.
- **Events to decisions**: the framework forwards three kinds of notices to every decision: "directed sound"
  (also queued in the character's hearing as kind 4; no 1.07f source sends it, SPEC-701), "alert sound from an
  emitter" (emitter origin and emitter; sent when an alert sound has "tell decisions" set, SPEC-701 §6) and
  "danger" (kind and position; rule 12). What decisions do with them is the companion spec.

### 9. Silent removal

Used for fleeing characters (view mode 3), for spawner-made characters out of view, and by the dormant-use rule.
Nothing happens while the owner is in the sight client's PVS. Otherwise: view mode 0, health 0; if `killtarget` is
set (and, when `killfacing` is set, only if the character faces the `killfacing` entity within dot > 0.96 on the
horizontal plane) the killtargets are used as in rule 10; the entity becomes not solid, collides with nothing,
is hidden from clients, movement none, its body's death times are set to level time − 25; then (unless already dead)
all senses, decisions and actions are destroyed, touch is cleared and the death flag is set. No death animation,
no sound, no statistics. Missing entities print `Couldn't find killtarget %s` / `Couldn't find killfacing ent %s`.

### 10. Damage, pain and death

**Damage** (called by the game's damage routine with target, inflictor, attacker, direction, point, normal, damage,
knockback, damage flags and means of death):
1. If attacker ≠ target, both are on the same team and the attacker is a player: means of death 23 is ignored
   completely; otherwise the character's perceive-teammates flag is set (it can now see the player as a target).
2. Means of death 23 is ignored completely in every case (no effect, returns "not damaged").
3. If damage flag 1 is clear, the attacker is a player, the target has no enemy entity yet and is alive, the damage
   is doubled.
4. If the target already has the "no knockback" entity flag, knockback is 0.
5. A kind-2 "noise" hearing event at the attacker's origin, emitter = attacker, is queued directly in the character's own hearing (SPEC-701 §6; no broadcast).
6. With a body and damage ≠ 0: the alerted flag is set and the body converts the hit into final damage (hit
   location, armour, gore; body companion spec).
7. Knockback (damage flag 8 clear, knockback ≠ 0, attacker present, movement type not none/no-clip/push/stop/9):
   direction = (dir.x, dir.y, dir.z × 0.2 if damage flag 128 else dir.z + 0.3), normalised; velocity +=
   direction × knockback × 500 / max(mass, 50).
8. With final damage ≠ 0, an attacker that is a player and a living target: the attacker gets the caption
   `SINGLEPLR` `HOSTAGE_HIT` (string id 0x600) for a hostage, else `INNOCENT_HIT` (0x60E) for an innocent type.
   Health −= final damage.
9. Health ≤ 0: the "no knockback" flag is set and death runs (below); returns "damaged".
10. Otherwise pain runs unless the body's protection class is 3 and the final damage is 0; returns "damaged" when the
    final damage ≠ 0.

**Pain**: only for a living character with a body. If the attacker is a player and the character is a buddy, the
player gets caption `BUDDY_HIT` (0x601). The body chooses a pain animation for the hit; if it chooses one, a pain
action with that animation pre-empts the current action.

**Death** (target, inflictor, attacker, damage, point, flags):
1. Health is clamped to [−999, 0].
2. The helper entities are removed. A kind-6 "death" alert sound of radius 600 (SPEC-701 §6) is broadcast from the victim's
   origin with the attacker as emitter (requires sight when damage flag 0x1000000 is set; SPEC-701).
3. When the victim is not on the sight client's team and was not already dead, a danger notice of kind 1 and radius
   128 is sent around the victim's origin (rule 12).
4. Statistics, only when the victim was not already dead and no cinematic freeze is active: victim not on the sight
   client's team → enemies killed + 1 and score += profile[6] × the skill score factor (skill spec); otherwise
   friendlies killed + 1.
5. Kill target: if `killtarget` is set and the victim was not dead: with `killfacing` set, only when the victim faces
   that entity within dot > 0.96 (horizontal plane); every entity named `killtarget` is used (activator = victim).
   If one of them is a `script_runner` with spawnflag 1, the console shows `%s is running a WILL_KILL_USER script`,
   the victim's health is restored to its maximum, all its actions are destroyed and its velocity zeroed: the script
   is expected to kill it.
6. The victim becomes a corpse: solid "corpse" (value 5), clip mask 0x4010003, dead-monster server flag, movement
   step, gravity 0.5, enemy = attacker. The body records the death time (first death time only once) and the death
   point.
7. Player-caused casualties (rule 11).
8. All senses, decisions and queued actions are destroyed; the current and previous actions too unless spawnflag
   65536 is set.
9. A death animation is played: the body picks one for the hit; when it has none, `std_stoknees_2bk_n_n` is used; a
   choice that is not a death animation prints `Death action %s not really a death`. With spawnflag 65536 a held
   pose action (code 4) plays it, otherwise a normal death action; it pre-empts.
10. Touch is cleared, the death flag is set, the voice channel is silenced (sound 0, volume 0.6, attenuation 1), the
    death angles are stored and the entity is relinked.
If the victim was already dead, only steps 1–3, 6 and (for spawnflag 65536 with a current action of kind 5) the
rest run.

**Ready to be removed**: every sense, every queued action, every decision and the current action is asked; the
corpse is freed only when all agree. The corpse limits are cvars `ai_maxcorpses` (default 5) and `ai_corpselife`
(default 5) used by the body and action specs.

### 11. Player-caused casualties and mission failure

Counters (reset by the end-of-mission statistics command): enemies killed, friendlies killed, score, dead hostages,
total characters killed. The level limit is worldspawn key `maxDeadHostages` (0 or absent → 3; −1 = unlimited).
At each death, in this order:
- a victim whose classname contains `swat`, killed by a player, without spawnflag 65536: the dead-hostage count
  becomes limit + 1 (an unlimited limit is first turned into 0), i.e. the mission fails;
- else a hostage, or an innocent type killed by a player, or a buddy killed by a player: dead hostages + 1;
- total killed + 1 always.
While the limit is not −1 and dead hostages ≥ limit, every player is killed at the start of its frame.

### 12. Character manager, priority and danger notices

See "Character helper rules" below for the active list, the 0.5-second priority pass (characters in the sight
client's PVS are high priority; up to 7 hidden ones nearest the player too; `ai_highpriority` ≠ 0 makes all high
priority), noise and danger broadcasts and the voice throttles. Low priority has two effects in this spec: the
sense update is throttled (rule 7 step 8) and spawner-made characters out of view are removed (step 12).

### 13. Teams

A character's team is a number per type (table above). Two entities are on the same team when:
- either is a character that is in the "ignored" state reported by its body (vehicles never) → never;
- both are characters → the team numbers are equal;
- one is a character and the other a player → the character's team is 0, and, only in deathmatch, the player's
  `teamname` user-info value is `mullins`;
- neither is a character → the multiplayer rules (deathmatch team play, outside this spec).
So team 0 types are on the player's side in single player; team 3 (New York gang) and team 1 (soldiers) are
enemies of each other as well as of the player.

### 14. Perception summary (what the framework reads back)

After the senses update, the framework asks the senses for the best "player" record and the best "non-player"
record (SPEC-701: entity, position, time, kind).
- A player record of kind 7 (an alert call heard) sets the "recently called" stamp to the record time + 12 and turns the record
  into kind 6.
- If the best player record names a living entity that was last perceived more than 1 s ago, the reaction moment
  becomes level time + a random value in [−0.2, 0.2].
- "Target position": when the target is the entity in the matching record (player record for players, non-player
  record otherwise) the record position is returned, else the owner's origin.
- After each sense update the body is queried once more; the result is not used (no observable effect).

### 15. Body, bounding boxes and animation notes (Ghoul interface)

- The body owns the Ghoul instance (SPEC-011). Animations are named by the body's sequence tables (for example the
  default death `std_stoknees_2bk_n_n`); each animation entry names one of the nine bounding-box presets.
- **Changing the preset** to index i (0–8; other values are refused): the new horizontal extents are the preset's
  × size scale; the box keeps its current bottom (mins z relative to the origin) and gets the height
  size scale × (preset maxs z − preset mins z). If i is already the preset in use, only the floor offset is smoothed:
  the stored floor height is first pulled to within 15 units of the box bottom; when the character is airborne
  (no ground entity and solid value ≠ 5) or the bottom is within 1 unit of the stored height, nothing changes;
  otherwise the bottom moves to the stored height and the origin moves the opposite way, so the box keeps its world
  position. The new box top is raised to the entity's view height when it would end more than 4 units below it.
  Then, unless the movement type is 1, the new box is traced in place with mask 0x2020003. Clear → the new box and
  origin are applied, the entity relinked and the preset stored (result "changed"). Starting in a solid → when the
  caller allows it, the change is retried keeping the old horizontal extents; it is applied if that box is clear or
  the character is dead; otherwise nothing changes (result "refused").
- **Animation notes** forwarded by the body: `rstep` → right footstep effect, `lstep` → left footstep effect,
  `ooze` → a floor decal 10 units above the owner's origin.
- The body is asked for: pain and death animations, final damage for a hit, its protection class, "used/touched by
  the player" reactions, the "wake" reaction, death times and point, and whether it is in the "ignored" state.

### 16. Helper entity

Every think a helper entity with classname `Nug Buddy` is made for each living character: not solid, clip mask
0x2030003, server flags 0x82, owner = the character, entity flag 1048576, chained to the character; its origin and
box follow the character's with the horizontal extents enlarged by 8 units on each side. The helper entities of the
previous think are freed at the start of the next think and at death. They are visible in `entlist`. UNVERIFIED
purpose (the player's lean code uses the same chain); verify with `entlist` while enemies are visible.

### 17. Navigation interface

- When the level's `.nvb` data is loaded (SPEC-143), each awake character's navigation location cache is refreshed
  every frame by the character manager (not when cvar `ai_nonodes` ≠ 0); the cache holds the character's grid layer
  and nearby nodes. Path queries made by decisions read it (companion spec).
- `path_corner` keys `waitaction1`…`waitaction6` and `moveaction` are animation names played by the path-follow
  decision.

### 18. Script interface

- The script `AI` instruction (SPEC-140, opcode 0x21) looks for the character's scripted decision; if there is none,
  one is created with priority 25 and added. Commands are queued on it (companion spec).
- `RESET_AI` removes the scripted decision of that character (rule 8).
- Scripts that use a character clear its call-for-help flag.
- A `script_runner` with spawnflag 1 used as a killtarget makes the death "script-controlled" (rule 10 step 5).

### 19. Combat profile

Fifteen values per character, copied at creation from a per-type table and adjusted as in rule 1:

| index | meaning (as read by decisions/actions; companion spec) |
|---:|---|
| 0 | preferred engagement distance |
| 1 | maximum effective distance (accuracy falls with the square of distance beyond it) |
| 2 | a probability used by combat decisions (UNVERIFIED which choice) |
| 3 | aim spread factor |
| 4 | time scale between attacks |
| 5 | aim settle time |
| 6 | score for killing this character |
| 7 | flags (integer): 2 and 8 change attack choices, 16 never switches between crouch and stand, 32/64 change cover choices, 128 changes aim, 1 is read by the gore code (UNVERIFIED meanings) |
| 8 | a body damage-scaling factor (UNVERIFIED) |
| 9–14 | personality values (six integers; no reader found in 1.07f, UNVERIFIED) |

Per-type entries are either explicit or *built* from a mission tier R (0–6) and a weapon class W:
1. tier sets profile[3], [4], [5], [6], [7] and [8] = 1.0 (table "Profile tiers"); tiers outside 0–6 set nothing;
2. weapon class sets profile[0], [1], [2], ORs flags into [7] and multiplies other values (table "Profile weapon
   classes"); other classes change nothing.

### 20. Level identification

Some per-type setups (skins, accessories) need to know the mission level. A level name matches the current level
when it equals the map name (case-insensitive), or the map name contains it, or it equals the worldspawn key
`ainame` (case-insensitive). Names are tried in the order of the table under Constants; the first match gives the
level number (0 = none). The reverse mapping (number → name) gives `TSR1` for 0 and 2 and `tut1` for 1 and for
numbers above 32.

## Character helper rules (Quake II survivors)

**Range class.** The game works on squared distance between origins. A caller may pass a precomputed squared
distance; zero or less means "compute it".

| squared distance | class |
|---|---|
| below 6400 | 0, melee (under 80 units) |
| below 250 000 | 1, near (under 500 units) |
| below 2 250 000 | 2, mid (under 1500 units) |
| otherwise | 3, far |

**Visible.** Each eye point is the entity's origin raised by that entity's view height (characters: 0). The test
fails if the two points are not in each other's PVS. Otherwise a zero-size trace runs with mask 0x19, ignoring the
looker; it succeeds when the fraction is exactly 1. An optional third argument applies only when the target is a
player: if the first trace fails, a second trace goes to the player's leaning proxy (an entity chained to the player
with entity flag 32768), aimed 16 units above its origin.

**In front (field of view).** See SPEC-701 (thresholds 0.85 / 0.75 / −0.2 by view mode, motion adjustments).

**Bottom (ledge) check.** Quake II method with two changes: if all four corner points 1 unit below the bounding box
are solid the check passes at once; otherwise a trace goes 36 units down from the centre of the box bottom (its end
height is the reference; whether it hit is not tested), each corner is traced down the same way with mask
0x2020003, a corner fails if it hits nothing or ends more than 18 units below the reference, and the check passes
when at most 2 corners fail (Quake II fails on the first bad corner). A second form tests an arbitrary position: step
height = the character's step height (default 24) when flag 2 or 4 is given, otherwise 2.0; it traces down twice the
step height and uses the step height as the corner limit.

**Ground check.** Fly/swim characters are skipped. Upward velocity above 30 (Quake II: 100) clears the ground and
stops. The hull is probed 0.25 units down (2.5 when the current ground entity's solid value is 4). A floor normal z
below 0.7 that does not start in a solid clears the ground; otherwise, if the hull neither starts in nor is entirely
inside a solid, the entity snaps to the trace end, records the ground entity and its link count and sets vertical
velocity 0. When the ground entity changes, the new ground entity's touch reaction is called with (ground,
character, no plane, no surface).

**Water level.** As Quake II: points at mins.z + 1, then 26 and 22 units higher, contents mask 0x38.

**World effects.** As Quake II but silent:

| effect | rule | damage | damage flags | means of death |
|---|---|---|---|---|
| air supply | refills to time + 12 s (swimming characters: + 9 s) | — | — | — |
| drowning | every 1 s after air runs out | 2 + 2 × whole seconds since air ran out, at most 15 | 0x4002 | 20 |
| lava (unless lava-immune, flag 128) | every 0.2 s | 10 × water level | 0x1002 | 28 |
| slime (unless slime-immune, flag 64) | every 1 s | 4 × water level | 0x4002 | 21 |

The "in water" flag (8) is set on entering water and cleared on leaving.

**Drop to floor.** As Quake II: raise 1 unit, trace 256 units down with mask 0x2020003, then ground check and water
level.

**Sight client and sight character.** Updated every frame before entities think. The sight client is the next
in-use player slot after the current one, wrapping (no health or notarget test, unlike Quake II). The sight
character advances to the next entity after the player slots that is in use, alive, not notarget and an awake
character. The sight sense examines both, so character-against-character sighting is round-robin, one per frame.

**Character manager** (runs every frame while a sight client exists; not saved; cleared at level spawn):
- every frame: the active list is rebuilt, up to 64 entries, from in-use characters with health ≥ 1, spawnflag 16
  clear, and either awake or within dx² + dy² + 16·dz² < 1024² of the sight client; each awake character's
  navigation cache is refreshed; the awake characters in the sight client's PVS are counted;
- every 0.5 s (a stored time more than 0.1 s in the future is reset to time − 0.6): entities flagged as attached parts
  whose master's classname is `freed` are freed; awake characters in the PVS become high priority, those outside low
  priority (high when `ai_highpriority` ≠ 0); while fewer than 7 are in view, the nearest hidden ones (distance
  dx² + dy² + 9·dz²) are made high priority until 7 are; the nearest awake character (initial best 999 999) is
  remembered for decisions and the gore code.

**Noise broadcast.** A source with loudness L and radius R: every living character whose bounding-box centre is
within R of the source's centre receives strength (1 − d/R) × L, capped at 100 (≤ 0 ignored) as sense muting (SPEC-701):

| source | loudness | radius |
|---|---|---|
| helicopter, tank | 150 | 250 |
| gore event, Ghoul object | 200 | 200 |
| chemical barrel | 150 | 200 |
| C4, claymore | 300 | 300 |
| weapon effects | 130 / 150 / 100 / 300 | 250 / 250 / 150 / 300 |
| `environ_explosion`, `func_breakable_brush`, `target_explosion`, `target_splash` | entity value | same entity value |

**Danger notice.** Active-list characters whose origin is within R of a point get a danger notice (kind, point) for
their decisions. Live explosives (C4, grenades): radius 160, kind 2. A character's death: radius 128, kind 1.

**Voice throttles** (level-wide, whole seconds): a "wake" yell and a tactical voice line (categories backup 1,
throw 2, duck 3, retreat 4, help 5, react 6) are each allowed only when more than 2 s have passed since the last
one; a tactical line must also differ in category from the last one. A stamp more than 0.1 s in the future is reset
to time − 3. With `ai_goretest` ≠ 0 the console shows `voice sound: %s` and `wake sound: %s`.

**trigger_sound.** Up to 16 `trigger_sound` entities are listed; a perception event that does not require sight uses
each listed one whose squared distance is below its `health` value, which then removes itself from the list.

## Savegame chunks of the AI

Chunk ids are four-character codes stored as little-endian 32-bit integers, as in SPEC-303.

**Ids.** Every AI-related object has a positive 32-bit id from one counter shared by all kinds. The counter starts at
1, only grows and is never reset between levels; 0 means none. On load the counter becomes the largest id read + 1.
Entity references are entity numbers, −1 = none.

**Placement.** In the level save the AI block comes after the script chunks and before the object-instance chunk
`OINM`.

**Part 1: tables**, eight of them in this order: Ghoul model objects, Ghoul bolt-on instances, Ghoul object
instances, actions, decisions, senses, bodies, characters. Each is `AICT` (int32 count) followed by count × `AICC`
{int32 id, int32 class code}, in ascending id order. Class codes: actions, decisions, senses and bodies use their
factory codes; characters use the type code 6–110; Ghoul objects use 0.

**Part 2: per-object data**, same order except that object instances come before bolt-ons: models, object instances,
bolt-ons, actions, decisions, senses, bodies, characters; ascending id within each kind. On load all existing
objects of each kind are destroyed first, objects are created from the tables, then each reads its own chunks in
this order.

| kind | chunks (bytes) |
|---|---|
| actions | `AIPA` 112, `AIDA` 124, `AIXA` 124, `AIJA` 124, `AIFA` 120, `AISA` 112, `AICA` 112, `AIEA` 112, `AIRA` 124, `AIDW` 124, `AIWA` 112, `AIAA` 120, `AIHE` 132 (helicopter), `AITK` 132 (tank/snowcat) |
| decisions | `AIPD` 184/160/164, `AIOD` 216, `AISD` 232 or 220 (+ `AION` 4, `SDSO` 68 × n), `AIRD` 192, `AIDD` 200, `AIBD` 36, `AIHD` 184 |
| senses | `AIAS` 88, `AISS` 88, `AINS` 88 or 92, `AISO` 448 (SPEC-701) |
| bodies | `HELI` 440, `TANK` 172, `SCAT` 192, `AIBC` 148, `AIBH` 144, `AIBR` 144, `AIBK` 500, `AIBE`/`AIBM`/`AIBF` 492 + `AIGZ` (variable) |
| Ghoul | `GGOB` 1432 [+ `GGON` 4 + `STRG` strings], `GGOI` 16, `GGBI` 32 (+ `GHLI` engine instance) |

**Character chunks**, in order: helicopter types first write `AIHA` (48); tank and snowcat types `AITA` (52); then
`AIAI` (the 588-byte record above); `AISL` int32 count followed (only if count ≠ 0) by `AISE` count × sense id;
`AIDL`/`AIDE` the same for decisions; `AIAL`/`AIAE` the same for the action queue; helicopters then write `HEWP`
44 bytes per waypoint. On load a missing id becomes none; each loaded character re-attaches itself to its owner
entity when that entity's AI reference is non-zero.

## Constants

### Spawn and think
| constant | value |
|---|---|
| think interval | 0.1 s |
| wake distance from sight client | 800 units (squared 640 000) |
| corpse wake check | dead character within 200 units, checked once per whole second |
| alert: wake-call distance | P within 1000 units (squared 1 000 000) |
| alert call event | type 7, radius 500 |
| death event | type 6, radius 600 |
| death danger notice | kind 1, radius 128 |
| low-priority sense interval | whole-second stamp ≤ now − 2 (2–3 s) |
| "recently called" stamp | record time + 12; reset to now − 20 |
| reaction-moment jitter | now ± 0.2 when best player record older than 1 s |
| killfacing tolerance | dot > 0.96 on the horizontal plane |
| dormant-use clearance | 40 units, entity flag 65536 |
| corpse gravity / solid / clip mask | 0.5 / 5 / 0x4010003 |
| health clamp at death | [−999, 0] |
| default death animation | `std_stoknees_2bk_n_n` |
| damage doubling | player attacker, damage flag 1 clear, target alive with no enemy |
| knockback | knockback × 500 / max(mass, 50); vertical dir.z × 0.2 (flag 128) or dir.z + 0.3 |
| ignored means of death | 23 |
| character-type helper box enlargement | 8 units per horizontal side |
| bounding-box smoothing | 15 units; dead band 1 unit; view-height margin 4 units |
| ai_goretest health | 1000 |
| maxDeadHostages default | 3 (−1 unlimited) |
| scripted decision priority | 25 |

### Personality rows (key `personality` = row + 1; 0 = random row)
| row | values |
|---:|---|
| 0 | 2, 0, 1, 2, 2, 3 |
| 1 | 0, 0, 0, 0, 0, 2 |
| 2 | 1, 3, 4, 0, 1, 2 |
| 3 | 2, 1, 2, 0, 3, 2 |
| 4 | 1, 1, 3, 1, 1, 2 |
| 5 | 0, 2, 3, 1, 1, 2 |

### Profile tiers (built entries; tier numbers follow the missions that use them)
| tier | used by (examples) | [3] | [4] | [5] | [6] | [7] flags |
|---:|---|---:|---:|---:|---:|---:|
| 0 | Kosovo | 0.2 | 1.0 | 6.0 | 20 | 0 |
| 1 | Siberia | 0.25 | 1.0 | 5.0 | 40 | 0 |
| 2 | Iraq | 0.4 | 1.0 | 3.0 | 120 | 0 |
| 3 | New York | 0.35 | 1.0 | 4.0 | 80 | 0 |
| 4 | Sudan | 0.35 | 1.0 | 4.0 | 120 | 0 |
| 5 | Tokyo | 0.4 | 0.5 | 3.5 | 140 | 0 |
| 6 | Germany | 0.4 | 0.5 | 3.0 | 200 | 5 |
All tiers set [8] = 1.0.

### Profile weapon classes (built entries)
| class | [0] | [1] | [2] | flags OR | multipliers |
|---:|---:|---:|---:|---:|---|
| 3 | 384 | 768 | 1 | — | [5] × 0.5 |
| 4 | 384 | 768 | 1 | — | [5] × 0.5, [6] × 1.5, [3] × 0.9 |
| 5 | 128 | 256 | 1 | — | [4] × 0, [5] × 2, [6] × 2, [3] × 0.35 |
| 6 | 192 | 448 | 1 | 16 | [4] × 0, [5] × 2, [6] × 2.5, [3] × 0.35 |
| 7 | 512 | 1024 | 0 | 10 | [6] × 2, [3] × 3 |
| 9 | 256 | 512 | 1 | — | [6] × 4 |
| 11 | 96 | 192 | 0 | — | [6] × 2, [3] × 0.3 |
| 12 | 256 | 512 | 1 | — | [5] × 1.5, [6] × 4, [3] × 0.35 |
| 14 | 256 | 768 | 0 | 21 | [6] × 7 |
| 16 | 256 | 512 | 1 | — | [6] × 7, [3] × 0.4 |
| 18 | 64 | 160 | 0 | 144 | [4] × 0, [5] × 0, [6] × 3, [8] × 2, [3] × 5 |
Which weapon each class stands for is the roster spec (hypothesis: 7 sniper rifle, 14 rocket launcher).

### Per-type profile table
Built entries:

| tier | weapon class | type codes |
|---:|---:|---|
| 0 | 4 | 23, 24, 74 |
| 0 | 6 | 70, 71, 75, 76, 78, 79 |
| 0 | 7 | 72, 73 |
| 0 | 11 | 69 |
| 1 | 3 | 28, 68 |
| 1 | 4 | 27 |
| 1 | 5 | 67, 97, 102 |
| 1 | 6 | 94, 96, 98 |
| 1 | 7 | 95 |
| 2 | 4 | 25, 26, 47, 48 |
| 2 | 6 | 43, 45, 49 |
| 2 | 7 | 42, 51 |
| 2 | 12 | 44, 46 |
| 2 | 14 | 52 |
| 2 | 18 | 50 |
| 3 | 3 | 15, 30, 60, 61, 81, 100 |
| 3 | 4 | 83 |
| 3 | 6 | 82 |
| 3 | 11 | 80 |
| 4 | 3 | 56 |
| 4 | 4 | 22, 92 |
| 4 | 7 | 55 |
| 4 | 11 | 57, 58 |
| 4 | 14 | 93 |
| 4 | 18 | 59 |
| 5 | 3 | 86, 108 |
| 5 | 5 | 89 |
| 5 | 6 | 87, 90 |
| 5 | 16 | 88 |
| 6 | 5 | 63 |
| 6 | 9 | 64 |
| 6 | 12 | 62 |
| 6 | 14 | 66 |
| 6 | 16 | 65 |
| 6 | 18 | 109 |

Explicit entries (values [0]…[8]; personality 0 unless stated):
| type codes | [0] | [1] | [2] | [3] | [4] | [5] | [6] | [7] | [8] |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 16–21, 29 | 384 | 512 | 0.3 | 1.0 | 0.5 | 3.0 | 25 | 0 | 2.0 |
| 31–37, 39, 53, 54, 103–107 | 192 | 512 | 0.3 | 1.0 | 0.5 | 3.0 | 25 | 0 | 2.0 |
| 77, 84, 85, 91, 99, 101, 110 | 280 | 512 | 0.3 | 1.0 | 0.5 | 3.0 | 25 | 0 | 2.0 |
| 38 | 48 | 128 | 0 | 0.25 | 1.0 | 2.0 | 96 | 16 | 0.6 |
| 40 | 192 | 512 | 0.7 | 1.0 | 0.38 | 1.0 | 275 | 5 | 1.5 |
| 41 | 192 | 384 | 0.28 | 0.5 | 0.75 | 1.0 | 2750 | 37 | 0.6 |
Dogs (types 11, 12) set their own profile at setup: 16, 72, 0, 1.0, 0, 0, 200, flags 112, 1.0, personality
2, 0, 1, 0, 2, 3. Types without an entry keep the object defaults: 100, 300, 0.8, 1.0, 0, 0, 0, 0, 1.0, personality 0.

### Level identification order
`tut1` 1, `tsr1` 2, `tsr2` 3, `arm1` 30, `arm2` 31, `arm3` 32, `trn1` 4, `sib2` 9, `kos1` 5, `kos2` 6, `kos3` 7,
`sib1` 8, `sib3` 10, `irq1a` 11, `irq2a` 12, `irq3a` 13, `nyc1` 14, `nyc2` 15, `nyc3` 16, `sud1` 17, `sud2` 18,
`sud3` 19, `jpn1` 20, `jpn2` 21, `jpn3` 22, `irq1b` 23, `irq2b` 24, `irq3b` 25, `ger1` 26, `ger2` 27, `ger3` 28,
`ger4` 29; none → 0.

### Cvars
| cvar | default | effect |
|---|---|---|
| `ai_freeze` | 0 | characters stop after the debug-effect step of their think |
| `ai_dumb` | 0 | shows `cantsee` / `canthear` effects on blind / deaf characters |
| `ai_goretest` | 0 | characters have 1000 health and wake at once; prints voice/wake sound names |
| `ai_pathtest` | 0 | adds the omniscient sense; use key pushes characters and prints animation lists |
| `ai_highpriority` | 0 | hidden characters stay high priority |
| `ai_nonodes` | 0 | navigation caches are not refreshed |
| `ai_maxcorpses` | 5 | corpse limit (body/action specs) |
| `ai_corpselife` | 5 | corpse lifetime (body/action specs) |
| `ai_spawnfrequency` | 1.0 | spawner rate (roster/spawner spec) |
| `ai_editdir` | `p:` | editor path (unused at run time) |
| `aidec_show` | — | decision debug display (decisions spec) |

## Test vectors / how to verify

1. Wake distance: place a soldier (no flags) 900 units from the player start in an open room: it stays idle; walk to
   790 units: it wakes within one frame (starts its ambient behaviour). With spawnflag 16 it never wakes until a
   trigger uses it.
2. Spawnflag 2: with `ai_dumb 1` the `cantsee` effect plays on the character; it does not react to the player in plain
   view until shot (damage events still alert it).
3. Friendly fire: shoot `m_x_msam` (team 0) once: the caption "Hey!  Check your fire, John!" appears only if it is a
   buddy; with the hostage flag "Check your fire; you're here to save these people!"; for `m_nyc_ebum`
   "Check your fire!". After being shot by the player a team-0 character can see the player as a target.
4. SWAT rule: on `nyc1` kill one `m_nyc_mswat`: the player dies at the start of the next frame (mission failed).
5. Damage doubling: with `ai_goretest 1` (health 1000) hit a sleeping enemy with a pistol and compare the health
   loss with the same shot on a player: the character loses twice the weapon damage (before body location scaling).
6. Silent removal: start a civilian fleeing (shoot near it), break line of sight: it disappears once it is outside
   your PVS (`entlist` no longer lists it as alive).
7. Kill statistics: kill three enemies and one civilian of team 0: the end-of-mission screen shows 3 enemies and
   1 friendly; score grows by profile[6] × skill score factor per enemy.
8. Savegame: write a level save with live characters and list chunks (SPEC-303 tools): expect the `AICT`/`AICC`
   tables, `AIAI` records of 588 bytes, and `AISL`/`AIDL`/`AIAL` counts.
9. Training-map compounding (rule 1): on `trn1` save and load five times; enemies should react faster and aim worse
   each time if the compounding is real.

## Open questions

1. Meanings of profile[2], the flag bits of profile[7] and profile[8]; whether the personality values are read at
   all (no reader found).
2. Exact semantics of spawnflags 32768, 65536 and 262144 and of decision kind 6 (companion spec).
3. Purpose of the `Nug Buddy` helper entity beyond collision with the player's lean code.
4. The meaning of the engine per-leaf value used at creation (lighting or area size?) and why both branches reduce
   profile[3].
5. Whether the training-map adjustment really compounds across save/load (it would in the code as read).
6. The record fields marked "for decisions" (28, 32, 284, 424, 428, 448–464, 492) are defined by the decisions and
   actions spec.
