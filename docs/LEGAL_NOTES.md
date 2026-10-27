# Legal notes - items for review before a public release

Owner decision 2026-10-26: the open source tree contains no data copied from the original executables, and the
engine does not read SoF.exe (SPEC-145 import-time extraction is superseded and not implemented).

## Done (branches legal-clean and legal-game, both merged into main)

| Item | Spec | Was | Now |
|---|---|---|---|
| Direction vector table (250 entries) | SPEC-201 App. A, SPEC-014 13 | copied float table | generated: frequency-5 geodesic icosahedron without poles, same order, max error 7.75e-7 (`engine/src/common/header/om_geodesic.h`) |
| Ghoul string dictionary (3713 strings) | SPEC-201 App. C | copied strings | empty; every string is sent literally (Huffman) |
| Ghoul Huffman weights | SPEC-201 App. D | copied weights | OpenMerc's own character classes (`OM_HuffWeight`) |
| Configstring dictionary (1927 strings) | SPEC-203 App. A | copied strings | empty; configstrings are always sent as text |
| Weapon-descriptor object offsets | SPEC-600 2 | internal object layout of the original DLL | moved to `docs/private/` (not published), prose summary left |
| Table generators that read the private appendices | - | `gen_ghoul_tables.py`, `gen_sof_tables.py` | removed |

Consequence: no network or demo compatibility with original 1.07f servers, clients and demos (both ends must be
OpenMerc). See docs/DEVIATIONS.md "Fixed tables" and docs/impl/LEGAL-tables.md.

## Publishing

- `docs/private/` holds the original-derived appendices for the reverse/verification work. It is listed in
  `.publish-exclude` and must not be exported. `python tools/legal_scan.py --export` checks the tree that would be
  published; it fails on large constant tables, forbidden markers (R workspace paths, analysis-tool names, tool-generated symbols, DLL
  addresses in specs) outside the whitelists.
- The development history, which still contains the removed tables, is not published: the public repository
  starts from a single commit of the cleaned tree, checked with `tools/legal_scan.py --export`.
- Snapshots published before this cleanup (tags v0.1, v0.1.1) contained the copied tables and are not part of this
  history.

## Owner decision on the class-(c) tables (2026-10-26)

The class-(c) tables are kept deliberately as gameplay data, in full:

- human animation attributes (`game/src/damage/g3_humananims.inc`, 520 rows: class, flags, posture);
- gore zone logic (`game/src/damage/g3_gore_tables.inc`: which parts sever together, caps, variants);
- the prop table (`game/src/props/prop_tables.cpp`, 121 rows);
- all 342 script ids (`game/src/script/script_ai_table.inc`), including the 104 not used by the stock scripts,
  because custom maps may use them.

Each value is an observable fact of the game's behaviour, but the animation attributes and the gore zone logic were
taken over as whole tables, so the risk is not zero. Both are scheduled for a black-box re-measurement (values
re-derived by observing the running game, not from the reverse team's material); the tables will then be replaced
by the measured ones. Until then they stay as they are.

No 1.07f compatibility mode will be built (not even one that reads the tables from a user's installed SoF.exe at
run time): it would need the removed executable tables and a dependency on SoF.exe, which the owner ruled out.

## Still to review (behaviour data, kept for now)

These tables are written by the clean team from approved specs. They hold behaviour facts measured black-box or
described by the reverse team (names that come from the game's own data files, frame numbers, damage and timing
values). They are not code, but some were obtained by reading the original game module, so they are listed for the
owner's review. `tools/legal_scan.py` reports them as "review".

Classification of the legal-game pass (2026-10-26, details in `docs/impl/LEGAL-game.md`): (a) derived from the
game data at run time, (b) small gameplay/interface parameters observed black-box, kept, (c) bulk data that can be
neither derived nor reduced, kept (owner decision below).

| File | Spec | Content | Class |
|---|---|---|---|
| `engine/src/client/fx/fx_tables.c` | SPEC-401 | debris part names: now read from the chunk models (removed); material rows and debris model list kept | (a) + (b) |
| `engine/src/common/sof_cvartable.h` (+ `tools/build/gen_sof_cvartable.py`) | SPEC-302 | cvar names (console interface), defaults and flags as printed by cvarlist | (b) interface |
| `game/src/damage/g3_humananims.inc` | SPEC-722 | one row per human sequence name of the game data (520 rows, sorted by name), gameplay attributes | names (a)-checked, attributes (c) |
| `game/src/damage/g3_animalanims.inc` | SPEC-722 | dog (26) and cow (6) rows | (b) |
| `game/src/damage/g3_gore_tables.inc` | SPEC-721 | gore zones: part/bolt names of the models plus zone logic | names (a)-checked, rest (c) |
| `game/src/ai/ai_tables.cpp` | SPEC-700 | AI tuning values, class names of the maps | (b) |
| `game/src/props/prop_tables.cpp` | SPEC-770 App. A | prop model names (from the data) and health/solid/debris values | (b), 121 rows (c) by size |
| `game/src/script/script_ai_table.inc` (+ `gen_ai_table.py`) | SPEC-750 12.7 | ids used by the compiled level scripts and their meaning | (b) interface / (c) by size |
| `player/src/pm_tables.cpp` | SPEC-600/601 | weapon and movement values | (b) |
| long tables in SPEC-006, SPEC-302, SPEC-750 | - | classname, cvar and script-id lists (interface) | (b) |
| SPEC-722 record layout and appendix | - | moved to `docs/private/SPEC-722-animation-tables.md` | removed from the specs |
