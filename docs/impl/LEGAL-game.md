# LEGAL-game: legal pass over the game tables (2026-10-26)

Branch `legal-game` (from main cc617ae). Owner decision: 1.07f compatibility is not needed; the target is the fully
open stack (mode B). Classes: **(a)** derived at run time from the game data in the paks (loader written, table
removed); **(b)** small gameplay or interface parameters observed black-box, kept, provenance reworded;
**(c)** bulk data that can be neither derived nor reduced, kept, listed for the owner.

## Per table

| Table | Class | Done |
|---|---|---|
| `fx_debris_models[].partname` (fx_tables.c) | (a) | Removed. `FXGM_PartName` (fx_ghoulmodel.cpp) returns the n-th part of the chunk model in case-insensitive alphabetical order; fx_ghoul.c uses it for models with `parts > 1`. The part count per model (wood uses 3 of its 4 parts) and the model list stay (b). |
| `fx_materials` (fx_tables.c) | (b) | Kept (43 rows of colours, sounds, effect kinds). |
| `g3_humanAnims` (was g3_animtables.inc, 539 rows in the original's order) | names (a)-checked, attributes (c) | Rewritten as `g3_humananims.inc`: one `G3A(...)` row per sequence name, sorted by name, symbolic class/flags/posture. Dropped 3 junk names (`RUBBISHWAS…`) and 16 later duplicates that no name lookup could reach. 11 rows name sequences that the stock models do not have (never available, harmless). The attributes could not be predicted from the name tokens (tried: a decision tree still mis-predicts class and flags), so they stay as gameplay data. |
| `gg_humananims.inc` (539-name copy for the Ghoul glue availability bytes) | (a) | Removed; gg_ghoul.cpp takes the names from the same `g3_humananims.inc` rows (macro include), so both lists can no longer diverge. |
| `g3_dogAnims`, `g3_cowAnims` | (b) | Moved to `g3_animalanims.inc` (same rows, symbolic form; the dog body addresses rows by position). |
| `G3Anim` comments ("88 bytes in the original", "record offset 0x3c") | - | Reworded (damage_api.h). `AvailNth` (unused, existed only for duplicate records) removed. |
| `g3_gore_tables.inc` | names (a)-checked, rest (c) | Kept. Part and bolt names exist in the stock models (G3.md check); the zone logic (which parts sever together, caps, variants) is not recoverable from the model data that OpenMerc reads (meshes carry no hierarchy in the .ghb part list). |
| `ai_tables.cpp` | (b) | Kept; provenance comment added (tuning values, map class names). |
| `prop_tables.cpp` | (b), (c) by size | Kept; model names come from the data, health/solid/debris are gameplay. 121 rows, addressed by index from 131 call sites and by savegames. |
| `script_ai_table.inc` | (b) interface, (c) by size | Kept; ids are the interface of the compiled `.os` scripts. 104 of 342 ids are not used by the stock scripts (SPEC-750 counts) and are kept for custom maps (owner decision 2026-10-26). |
| `pm_tables.cpp` | (b) | Kept; comment no longer cites descriptor offsets, column names neutral. |
| `sof_cvartable.h` | (b) interface | Kept; header states the black-box provenance. 19 cvars are referenced neither by the data nor by the code (most are stock Quake II names); left in. |
| SPEC-006, SPEC-302, SPEC-750 long tables | (b) interface | Kept (classnames: 351 of the 645 listed appear in the stock maps, the rest are spawned by code or items; cvar list; script ids). |
| SPEC-722 record layout, dog/cow tables, human appendix | - | Moved to `docs/private/SPEC-722-animation-tables.md`; the spec now describes the fields and points to the source tables. |

`tools/legal_scan.py` also lists macro-row tables (`NAME("…", …)`) for review now.

Owner decision (2026-10-26): all class-(c) tables are kept as gameplay data; animation attributes and gore zone logic are
scheduled for a black-box re-measurement (docs/LEGAL_NOTES.md). No 1.07f compatibility mode.

## Behaviour

No intended change. Wall chunks: the part picked for a given random number is now the alphabetical one
(`_LARGECHUNK, _LITTLECHUNK, _MEDIUMCHUNK, _SMALLCHUNK`) instead of the old table order; the choice is uniform, so the
distribution is unchanged. The other chunk models' tables were already alphabetical. Saves of earlier OpenMerc
builds refer to human animation records by position and do not load the right records after this change.

## Verification

- Builds: engine (build-LEGAL2), game, player, sound (Release, x86) - all OK.
- Unit tests: G3 150 checks, 0 failures; omghoul_test 1243 checks passed.
- X1 sweep (mode B, `D:\Games\OpenMerc-legal2-test`, ports 29680/29681): tsr1, kos1, dm/jpndm2 (`deathmatch 1`):
  3 captures each, characters animate and walk, same 6 Ghoul "not found" lines as the phase-1 mode-B run.
- Debris probe (temporary print, removed): kos1 fire gave `ROCK1/ROCK2/ROCK4`, `_SMALLCHUNK` from the model.