# SPEC-AMEND-014: MODEB-GAPS remaining behaviours (Q3–Q11) and gore / blood-pool notes

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
plus owner-reported gore/debris mismatch).
Evidence: static analysis of the 1.07f binaries; shipped string packages (`strip/singleplr.sp` in pak0 and pak2);
cross-checks against SPEC-010/014/131/313/600/721/722/771/790/791/AMEND-005/AMEND-011. Items that need a live
confirm are marked UNVERIFIED.
Describes: SoF.exe, gamex86.dll, player.dll, version 1.07f
Depends on: SPEC-003, SPEC-010, SPEC-014, SPEC-131, SPEC-313, SPEC-401, SPEC-600, SPEC-721, SPEC-722, SPEC-760,
SPEC-771, SPEC-790, SPEC-791, SPEC-AMEND-005, SPEC-AMEND-011
Amends: SPEC-131 / SPEC-313 (string 0x060e), SPEC-721 (debris burst callers; blood pool persistence clarification),
SPEC-014 / SPEC-791 (skin vs model names), SPEC-800 / stats (mid-game SP stats), SPEC-600 (alt-fire pointer)

Item kinds as in SPEC-AMEND-002.

## Summary

Answers the remaining MODEB-GAPS questions that are not save/load (AMEND-012) or level-change player tables
(AMEND-013): Ghoul skin-vs-model names, friendly-loss text / string 0x060e, gut death, god-mode vs mission fail,
SP stats page, MP player-model preview, per-weapon alt fire, cinematic input-freeze, 0 ms sounds and invalid
`environ_soundgen`, plus an explicit note on which gore/debris the original emits on dismemberment versus the
extra meat-looking chunks OpenMerc currently shows, and a separate note that persistent floor blood pools are
**not** original behaviour (planned modern improvement only).

---

## R14.1 — Skin / variant names are not model paths (Q3) (Resolution)

1. Names such as `ACC_BOX_HIP_BRWN`, `ACC_HAT_BERET`, `ACC_HOLSTER`, `MESO/B_BASIC`, `ECTO/A_NYCSKINHLEADER` are
   **skin or bolt-on skin / variant identifiers**, not Ghoul model file paths.
2. Shipped data has models like `Enemy/bolt/ACC_BOX_HIP` (and `_LRG`, `_SMLL`) with skin TGAs
   `acc_box_hip_blk`, `acc_box_hip_smll_brwn`, etc. There is **no** model file named `ACC_BOX_HIP_BRWN`.
3. Registration path: create/find the bolt-on or character object by its real model name, then set or register
   the skin on the appropriate material (Ghoul "register skin" / "set skin by names" — SPEC-010 / SPEC-014 /
   SPEC-791 bolt-on add with optional skin name). Looking the skin string up as a model yields "not found".
4. OpenMerc's "not found" flood for those names matches asking the loader for a **model** named like a skin.

Test: on any SP map, instrument Ghoul open-model calls — none of the `*_BRWN` / `B_BASIC` / `A_NYCSKINHLEADER`
strings may appear as model paths; they may appear only as skin arguments to register/set-skin or bolt-on-add.

## R14.2 — Friendly-loss text and string ID 0x060e (Q4) (Resolution)

1. String IDs are `(package_id << 8) | entry` (SPEC-313). `0x060e` = package **6**, entry **14**.
2. Package 6 is `strip/singleplr.sp` (`ID 6`, `REFERENCE SINGLEPLR`).
3. **pak0** `singleplr.sp` has `COUNT 14` (entries 0..13 only). Entry 14 does not exist → "String ID 060e not
   defined" / empty print is expected when only pak0's package is loaded.
4. **pak2** `singleplr.sp` has `COUNT 15` and defines entry 14:
   - Reference: `INNOCENT_HIT`
   - English: `Check your fire!`
5. Related entries already in both packs (used around mission fail / friendly fire):
   - 6 `MISSION_FAILED` — `Mission Failed!`
   - 7 `FRIENDLY_CASUALTIES` — `Too many friendly casualties`
   - 0 `HOSTAGE_HIT`, 1 `BUDDY_HIT` — captioned warnings when friendlies are hit
6. OpenMerc must load the pak2 (or later) override of `singleplr.sp` so entry 14 resolves. Printing 0x060e with
   only the pak0 package is original-accurate for that package set, but the retail 1.07f data set includes pak2.

Test: with full base paks mounted, trigger a friendly-loss fail — UI shows both the mission-failed line and
"Too many friendly casualties"; any use of id 0x060e shows "Check your fire!".

## R14.3 — Gut death (Q5) (Confirmation of SPEC-722)

1. Death selection rule 11 (killing zone known): zone `GZ_GUT_FRONT` selects sequence `std_sgut_2side_n_n`.
2. When that sequence is missing on the model, the original prints `Couldn't find gut death!` and falls through
   to the later direction / stumble / default death rules.
3. Which tsr2 NPC hits this path: any human whose killing zone is recorded as `GZ_GUT_FRONT` and whose model
   lacks `std_sgut_2side_n_n`. Exact character class on tsr2 is UNVERIFIED (black-box: log the print with the
   entity's model name).

Test: gut-shot a tsr2 human whose model lacks the sequence — console shows `Couldn't find gut death!` once; a
fallback death sequence still plays.

## R14.4 — God mode / heretic vs friendly-loss mission fail (Q6) (Resolution; one UNVERIFIED)

1. Worldspawn key / cvar family `maxDeadHostages` (default 3; −1 disables): when the number of dead hostages
   (friendly characters killed) reaches the limit, **every player is killed** and the mission fails (SPEC-760 B9.4).
2. Fail presentation uses the single-player strings above (`MISSION_FAILED`, `FRIENDLY_CASUALTIES`), not a
   unique binary-only message.
3. Whether `god` / `heretic` (invulnerability cheats) block that kill: **UNVERIFIED**. Mode-B harness observation
   that the player still dies under `heretic` when `killallmonsters` trips the friendly-loss rule is consistent
   with "mission fail kills ignore god", but the exact branch (cheat flags vs forced kill) is not confirmed in
   this draft. Do not implement a special case until a black-box on 1.07f with `god 1` / `heretic` + controlled
   friendly kills is recorded.

Test: on a map with friendlies, `god 1`, kill friendlies up to `maxDeadHostages` — record whether the player
dies and whether a save is refused with "Can't savegame while dead!".

## R14.5 — SP stats page mid-game (Q7) (Resolution)

1. The game module periodically publishes cvars via its stats updater, including at least:
   `stat_playedtime_base`, `stat_guyskilled`, `stat_friendlieskilled`, `stat_throatshots`, `stat_nutshots`,
   `stat_headshots`, and further special-shot / money / saves fields of the same family (full list in the
   game-modes / stats specs).
2. Values are "this level" plus "campaign total" style pairs where the updater adds the level counters to the
   accumulated campaign counters (same accumulators changelevel merges — AMEND-013).
3. Console command `getgamestats` dumps the published set (engine).
4. Menu flag / path `menu_nostats` exists for pages that omit stats. A mid-game SP stats page that shows empty
   in OpenMerc is therefore either not reading these cvars or is opening a `menu_nostats` variant — original
   mid-game stats are the cvar set above, not an empty page.

Test: mid-mission, `getgamestats` shows non-zero played time after a minute of play; the SP stats menu mirrors
those cvars.

## R14.6 — MP player-model preview names (Q8) (Resolution)

1. Menu / preview paths use the common-skin directory helper (`Ghoul/comskin/...`) and character family skins
   under `MESO/`, `ECTO/`, etc.
2. Names like `COMSKIN/F_BAB_*` and `MESO/A_*` are again **skin / sequence-list identities** resolved through
   Ghoul register-skin and `.gsq` apply-skins (SPEC-014 / SPEC-791), often after composing a path under the common-skin folder
   (SPEC-001 imports 62 / 64) — not raw model files named `F_BAB_*`.
3. Preview failure in OpenMerc that logs those strings as missing models is the same class of bug as R14.1.

Test: open the MP player menu; Ghoul open-model calls reference real `.ghb` paths only; skins apply without
"not found" for the `F_BAB_*` / `A_*` tokens.

## R14.7 — Per-weapon alt fire (Q9) (Confirmation / pointer)

1. Behaviour is owned by SPEC-600 (per-weapon alt-fire sequences, ammo, rate) and the player-module inventory method that starts alt fire (SPEC-003) and the
   weapon-specific alt-fire sequences.
2. Reload: the inventory reload request (SPEC-003 request code 1) / client reload paths interrupt or reject alt-fire according to each weapon's
   rules already tabulated in SPEC-600 — this amendment does not duplicate the per-weapon table.
3. MODEB gap: OpenMerc must implement SPEC-600's per-weapon rows; no additional original rule beyond SPEC-600
   was found for "reload always cancels alt fire" as a global — it is per weapon.

Test: for each weapon in SPEC-600's alt-fire column, one magazine of alt fire matches rate/ammo; starting
reload during alt fire matches that weapon's SPEC-600 row.

## R14.8 — Input-freeze flag (Q10) (Refinement of AMEND-011 gate 1; still partly OPEN)

1. Game module exports Set/Get of a **cinematic freeze** byte. While set, game-side cinematic rules apply
   (SPEC-751 / AMEND-009).
2. Engine cvar `cl_cinematicfreeze` exists on the client.
3. AMEND-011 gate 1 (first-person muzzle `effect` notes suppressed while the client's input-freeze flag is set)
   is the same family of freeze: empty movement commands, skipped movement prediction, suppressed view-weapon
   effects. Exact coupling "game cinematic byte → client input-freeze flag" is **OPEN** (believed direct or via
   player-state / configstring; UNVERIFIED).
4. View-model attachment placement during freeze remains as in OPEN_QUESTIONS / AMEND-011.

Test: during tsr1 intro cinematic, client sends empty move commands and first-person muzzle effects do not
start; after cinematic ends both return.

## R14.9 — 0 ms sounds and invalid environ_soundgen (Q11) (Resolution)

1. **0 ms notes (AMEND-011 R11.3):** sequence notes stamped at time 0 on the shotgun (and similar) fire
   sequences are honoured — the Ghoul note boundary at sequence start must deliver them. A backend that drops
   zero-length sound primitives or zero-time notes will silence shotgun fire and other 0 ms events. Original
   honours them.
2. **`environ_soundgen`:** on think/start, the entity looks up `soundName` in the ambient-set table. If the name
   is missing or empty such that lookup fails: prints `Soundgen with invalid name!` and **removes** the entity
   (SPEC-771 B.4). Non-zero spawnflags start the generator off; spawnflags 0 starts on after 0.3 s with the same
   lookup.

Test: shotgun primary fire always produces the playermz sound even though the fire note is at 0 ms; an entity
with a bad `soundName` prints the message once and disappears.

---

## R14.10 — Gore / debris on dismemberment vs OpenMerc "meat chunks" (behaviour note)

This records original emission versus the OpenMerc symptom ("meat chunks not present in the original"). It does
**not** authorise new gore content.

### Original emits on sever / dismemberment (SPEC-721 / SPEC-401 / SPEC-791)

1. **Severed piece:** a new entity with a Ghoul instance built from the severed zone's parts; cap bolt-ons
   (`g_cap*`, bone caps, etc.) with skin `c_blood`; `gore/bloodcloud` (large) on the piece's cap bolt;
   `gore/stumpblood` (or `gore/noblood*` under `lock_blood`) on the body-side stump; optional exit chunk bolt-on
   `Enemy/bolt/g_exit_chunky_anim1` on exit wounds.
2. **Gut / torso gore bolt-ons** when the zone lists them: models under `Enemy/bolt` such as `g_guts`,
   `g_guts3`, `g_guts_lrg`, `guts_bolton`, neck/arm torn anims — the thirteen gore-piece models registered by
   Ghoul glue (SPEC-791). These are bolt-on Ghoul models, not debris-ring chunks.
3. **Head explosion:** temp-entity debris (type 18) with **chunk type 32** (labelled flesh) and material 35,
   counts irand(1,3) size class 1 and irand(3,6) size class 4, plus `gore/headcloud` at `sbolt_mouth` and
   `impact/gore/headexp.wav`. Chunk type 32 maps to the **rock** debris models (SPEC-401: model index
   `7 + size − 1`, i.e. `chunks_rock`), **not** to a `chunks_meat` model.
4. **Per-hit flesh puff** (SPEC-AMEND-005 G3.7): every damaging hit with a non-zero damage point also sends TE 18
   chunk type 32 / material 35 / size class 4 / count irand(0,5), subject to the game-wide TE 18 rate limit
   (same material within 0.1 s dropped). Again: rock-shaped debris used as flesh bits.
5. **Body debris burst (SPEC-721 item 8):** a routine exists that would throw irand(10,20) size-4 + irand(5,10) size-1 +
   irand(0,2) size-2 chunks of type 32, strip bolt-ons, stop events, and make the body non-solid. **No caller
   was found in 1.07f** (SPEC-721 item 8). Original dismemberment does **not** invoke this burst.

### What OpenMerc currently drops that the user sees as extra "meat chunks"

1. OpenMerc implements the per-hit TE 18 puff and head-explosion bursts (aligned with G3.7 / SPEC-721). Those
   should look like small rock/flesh bits, not separate meat props.
2. OpenMerc also attaches the SPEC-791 gut/cap bolt-ons on sever. If a path spawns **additional** debris using
   `objects/generic/chunks_meat` (or Quake II `sm_meat` gibs from the legacy engine game code) on dismemberment,
   that is **not** original SoF behaviour — original flesh debris is chunk type 32 → rock models, and gut "meat"
   is the `Enemy/bolt/g_guts*` bolt-ons only.
3. Invoking a body-debris-burst equivalent on sever/gib (OpenMerc should not; original has no caller) would dump a
   large number of type-32 chunks the retail game never shows for that event.
4. Clean-team action: match SPEC-721 callers only; do not register or throw `chunks_meat` for SoF gore; keep
   gut visuals as the bolt-on list; leave the body debris burst unused.

Test: sever a limb on 1.07f and on OpenMerc; census TE 18 (chunk type 32 only, rock models) and bolt-on model
names (`g_guts*`, caps). OpenMerc must not show `chunks_meat` / `sm_meat` instances on that event.

---

## R14.11 — Blood pools: original vs planned modern improvement (behaviour note)

1. **Original:** when a body that recorded a killing zone needs its blood pool (SPEC-722 death handling /
   SPEC-721 §7), the game sends the **effect** `gore/bloodpool` at the floor position under the tracked bolt
   (or under the character origin). The shipped effect uses a sprite (`textures/sprites/blood_pool`). It is a
   **transient client effect**, suppressed entirely by `lock_blood`. It is **not** a persistent world decal,
   not a brush, and not a lasting floor stain — it goes away with the ordinary effect/particle lifetime and
   does not remain as a puddle on the floor after the effect ends.
2. **Props:** a few prop paths also fire the same `gore/bloodpool` effect at an origin (same transient effect).
3. **Owner direction (not original behaviour):** persistent blood pools / stains on the floor are a **deliberate
   modern improvement to be added later**. They must not be described or implemented as 1.07f behaviour. Specs
   and clean-room code that aim at original parity should keep `gore/bloodpool` as a short-lived effect only.
4. Record for future (non-parity) work: "persistent floor blood pools — modern, post-parity."

Test (parity): kill a soldier, observe a brief blood-pool sprite under the corpse that fades; the floor does
not keep a permanent puddle after the effect ends. Modern pools are out of scope for parity tests.

---

## Open questions

1. Q6 god/heretic vs B9.4 kill — needs a 1.07f black-box capture.
2. Q10 exact game→client freeze coupling.
3. Which tsr2 model lacks `std_sgut_2side_n_n` (Q5).
4. Whether any OpenMerc sever path still calls a body-debris-burst equivalent or throws `chunks_meat` (clean-team audit;
   R only records original has no such caller / no meat chunk type).

## Gatekeeper notes (G, 2026-10-26)

- Gatekeeper edits: removed internal routine names. Two inventory routine names in R14.7 became role descriptions
  pointing to SPEC-003. A path-helper routine name in R14.6 became "composing a path under the common-skin
  folder" (SPEC-001 imports 62 / 64). The body debris-burst routine name in R14.10 (three places) and open
  question 4 became "body debris burst (SPEC-721 item 8)". Nothing else removed. Lint clean; no code addresses,
  code or copied tables. String-package entries quoted in R14.2 are shipped game data (`strip/singleplr.sp`).
  Console messages are observable behaviour.
- **Conflict with the owner's report, to resolve before any gore fix.** The owner sees extra meat chunks on
  dismemberment in mode A, which runs the *original* `gamex86.dll`. R14.10 says the original game never emits
  `chunks_meat` on dismemberment, because flesh debris is chunk type 32 drawn with the rock models. If both are
  true, the extra chunks come from the OpenMerc engine/client side and not from the game module. Candidates:
  the client's chunk-type → debris-model mapping (SPEC-401), the handling of gib effects such as `gore/mpgsplatter`
  / `gore/hugeburst2` (which come from shipped effect files), or leftover Quake II gib code. C must census the
  event in mode A (TE 18 chunk types, effect names and the models actually drawn) before changing anything.
  If the shipped effect files themselves reference `chunks_meat`, then the original shows them too and this is
  not a bug.
- R14.10 "What OpenMerc currently drops" is R's hypothesis about OpenMerc. R has not read OpenMerc code, so C
  must verify it.
- R14.11: persistent floor blood pools are recorded as a planned post-parity modern feature (owner request,
  2026-10-26). It needs its own toggle and must not change parity tests.
- R14.2: C should confirm that the pak2 `singleplr.sp` override is loaded in OpenMerc, so string 0x060e resolves.
- R14.4: do not special-case god / heretic until the black-box capture exists.
