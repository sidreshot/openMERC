# SPEC-AMEND-012: Save / load behaviour (MODEB-GAPS Q1, Q12)

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
save-load crashes on ger1/ger3/kos1; level-end auto-save untested).
Evidence: static analysis of the 1.07f binaries plus shipped string packages and cross-checks against
SPEC-303, SPEC-752, SPEC-003, SPEC-721. Not yet black-box verified on every map. Every item ends with a test.
Describes: SoF.exe, gamex86.dll, player.dll, version 1.07f
Depends on: SPEC-001, SPEC-003, SPEC-303, SPEC-752, SPEC-721 (body gore chunks EBIA/ZGIA), SPEC-011 (Ghoul instance save)
Amends: SPEC-303 (auto.sof after spawn; level section vs game section), SPEC-752 (what is restored)

Item kinds as in SPEC-AMEND-002 (Correction / Resolution / Refinement / Confirmation).

## Summary

A manual or quick save writes a game section (game-wide record + every client + that client's inventory
chunk) and a level section (table-size stamp, level record, every in-use entity with its attached objects,
script/AI/Ghoul/body/gore chunks). A load clears level-tagged memory, checks the table-size stamp, restores
entities into their original slots, remaps Ghoul ids, then asks the player module to rebuild each client's
view-weapon after the inventory restore. Corpses, transient statics that are still entities, NPC Ghoul
instances and body gore state are written when present — they are not "re-spawned from the map" on load.
`gamemap` / `map` in single player also write `auto.sof` after the next map has spawned: game section only
(comment starts with ENTERING), no level section; health and inventory ride in the persistence cvars / game
record, not in a level blob.

## R12.1 — What a full save writes (Confirmation / Refinement of SPEC-752)

1. **Game section** (export "write game"):
   - One `GAME` record (game-wide: client count, skill/cheats, campaign statistics accumulators, autosave flag byte).
   - For each client slot: one `CLNT` record image, then the player module's inventory chunk (`INVN` of SPEC-003).
   - When a client has an attached inventory/Ghoul owner object, that object's own save hooks run after the `CLNT` image.
2. **Level section** (export "write level"):
   - `FTSZ` (nine table sizes the module must match on load).
   - Level-wide record (`LVLC`).
   - Count of in-use entities, then per in-use entity: entity-number stamp + entity record image + attached
     string / object / AI / Ghoul / body / gore chunks owned by that entity (families listed in SPEC-752 §1.2 / §4–§5).
   - Human body gore: `EBIA` (body object image) and `ZGIA` (per-zone accumulated damage) — SPEC-721.
3. **Corpses and live NPCs:** both are ordinary entities while they exist. If the entity is still in use at
   save time, its record and attachments are written. There is no separate "re-spawn from map, drop corpses"
   path on load: a corpse that was an entity is restored as an entity; a corpse that had already been removed
   (corpse limit / fade) is gone.
4. **Transient statics:** only those that are still entities (or attachments of entities) are in the level
   section. Pure client-side debris / particles / effects are not saved (SPEC-400 debris ring is client-only).

Test: save on kos1 ~5 s after spawn with many live NPCs; hex/chunk dump of the `.sof` shows `GAME`/`CLNT`/`INVN`
plus a non-empty level section with `FTSZ`, many `EDNM`/`EDCT` pairs, and at least one `EBIA`/`ZGIA` per living
human body that has been set up for gore. Load the same file: NPC count and corpse count match the pre-save
census (within the normal corpse-limit window).

## R12.2 — What load does before and after the chunks (Resolution)

1. Before reading the level section: all level-tagged memory is freed; the Ghoul id-remap table is cleared; the
   item system is reset; the entity count is reset to maxclients + 1 (SPEC-752 §1.2).
2. `FTSZ` is compared to the module's own nine sizes; any mismatch raises the game error that aborts the load
   ("savegame has different size…" family — exact text UNVERIFIED wording).
3. Each saved entity is written into the slot named by its entity-number stamp; the entity is decoded and linked
   into the world; entity count is raised to cover it.
4. After item / AI / Ghoul / body chunks: every client entity (1 … maxclients) is re-attached to its client
   record; one client field used as a post-load latch is cleared; the Ghoul id remap runs; the player module's
   "after full state restore" method (SPEC-003 inventory method that rebuilds the view-weapon) is called per client.
5. Automatic saves (`auto.sof`) have **no level section**. After the game section is read, the map is started
   fresh from its BSP/entities with the restored player/game state (SPEC-303 §3).

Test: load a mid-mission save vs `auto.sof` for the same map — mid-mission restores corpse positions; `auto`
restarts the map layout with the carried health/inventory only.

## R12.3 — Automatic save on level start / level change (Confirmation of SPEC-303 §3; answers Q12)

1. In single player (`deathmatch` 0), after a level has finished spawning, both `map <name>` and `gamemap <name>`
   write `<user>/save/auto.sof`.
2. That write is a **game section only**. The comment / `COMM` text starts with `ENTERING`. There is no level
   section.
3. `map` (not `gamemap`) deletes any existing `auto.sof` before loading the new map; then the post-spawn auto
   write creates a fresh one.
4. **What is carried into the next map** (exit trigger or `gamemap`):
   - Inventory and health are first written to the player module's persistence cvars (SPEC-003 methods that save
     inventory with "add clips to reserve", and the current health) by the change-level path (see SPEC-AMEND-013).
   - Campaign / mission statistics accumulators are added into the game-wide totals that `getgamestats` publishes.
   - The auto save then captures that game-wide + client + inventory state so a crash before the next manual save
     can resume with the carried loadout on a fresh map spawn.

Test: finish a map through the exit trigger; confirm `auto.sof` exists, its comment begins with ENTERING, it has
no level entity chunks, and loading it on the next map restores the pre-exit health and weapons.

## Open questions

1. Exact console / dialog text of the `FTSZ` mismatch error (string present; wording not copied here).
2. Whether every "transient static" class used on ger1/ger3 is an entity attachment or a pure effect — needed to
   explain OpenMerc's load crashes on those maps (likely a restore/remap bug on C side, not a missing original
   write). Black-box: compare entity class census before save vs after load on ger1.

## Gatekeeper notes (G, 2026-10-26)

- No text removed. Lint clean; manual review found no tool-generated symbol names, code addresses, code or copied tables.
  Chunk tags (`GAME`, `CLNT`, `INVN`, `FTSZ`, `LVLC`, `EBIA`, `ZGIA`, ...) are the save-file format and are already
  used in approved specs (SPEC-752, SPEC-721).
- The `FTSZ` stamp only has to match within OpenMerc's own save files; since 1.07f compatibility is not a goal
  (DEVIATIONS), C defines its own nine sizes and need not reproduce the original values. Old OpenMerc saves may be
  refused after a format change, with a clear message rather than a crash.
- C-side lead, already found in OpenMerc testing: the entity field `helperlink` is not written to the save and is
  left as garbage after a load, which matches the ger1 / ger3 / kos1 load crashes (open question 2 here). Fix it
  against R12.1 item 2 / R12.2 item 3 (every entity link in the saved record is either saved and remapped, or
  cleared on load), then run the R12.1 census test on those three maps.
- R12.3 (auto.sof has no level section) must be checked in OpenMerc, not only in 1.07f.
