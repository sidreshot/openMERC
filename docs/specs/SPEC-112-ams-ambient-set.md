# SPEC-112 — `.ams` ambient sound sets (`sound/sound.ams`)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — the single stock file `sound/sound.ams` (pak0, 17505 bytes), cross-checked against
the referenced `.wav` files and the entity strings of the stock `.bsp` maps (pak0). No game binary was
consulted.
Implementation: `tools/formats/ams.py`

## Purpose

`sound.ams` is a library of named ambient sound sets. Maps refer to a set by name through the entity key
`soundName` (found in the `.bsp` entity text). A set combines an optional looping sound, a list of one-shot
sounds played at random intervals, an audible distance and a volume range. Three groups exist, each used by a
different kind of map entity (see Semantics).

## Layout

All integers little-endian.

### Header (17 bytes)

| Offset | Size | Type | Field | Meaning / observed values |
|---|---|---|---|---|
| 0 | 1 | uint8 | `version` | **11** (same value as `.dms`, SPEC-111) |
| 1 | 4 | uint32 | `wave_count` | 337 |
| 5 | 4 | uint32 | `general_count` | 72 — sets of group 0 |
| 9 | 4 | uint32 | `local_count` | 70 — sets of group 1 |
| 13 | 4 | uint32 | `triggered_count` | 27 — sets of group 2 |

### Wave table

`wave_count` NUL-terminated paths, back to back, starting at offset 17, relative to `sound/`
(e.g. `Ambient/Gen/AirPlane/AirBy1.wav` → `sound/Ambient/Gen/AirPlane/AirBy1.wav`). Sets refer to waves by
0-based index into this table.

### Set records

`general_count + local_count + triggered_count` records follow the wave table, in group order, no alignment:

| Offset | Size | Type | Field | Meaning / observed values |
|---|---|---|---|---|
| 0 | var | char[] | `name` | NUL-terminated set name (`Empty`, `FactoryOuter`, `DoorFridge`, …) |
| +0 | 1 | uint8 | `flag_a` | **1** in all 169 records. UNKNOWN |
| +1 | 1 | uint8 | `flag_b` | **3** in all 169 records. UNKNOWN |
| +2 | 4 | float32 | `min_interval` | seconds (hypothesis), 0 … 500 (max_interval up to 600) |
| +6 | 4 | float32 | `max_interval` | seconds (hypothesis), ≥ `min_interval` in all records |
| +10 | 2 | int16 | `loop` | wave index of the looping sound, or **−1** (none) |
| +12 | 2 | uint16 | `count` | number of entries in the list that follows (0–12) |
| +14 | 2·count | uint16[] | `waves` | wave indices: random one-shots (groups 0/1) or start/move/stop sounds (group 2) |
| … | 4 | float32 | `distance` | 0 in groups 0 and 2; > 0 (125 … 1500) in every group-1 record |
| … | 1 | uint8 | `volume_min` | 0 … 254 |
| … | 1 | uint8 | `volume_max` | 0 … 255, ≥ `volume_min` in all records |

The file ends exactly after the last record. Every wave index is < `wave_count`, and each of the 337 waves is
referenced by at least one set.

## Semantics

Group ↔ entity mapping, from the `soundName` keys of all entities in the 52 stock `.bsp` maps of pak0
(single player and `maps/dm/`; 1652 entities whose `soundName` names a set):

| group | count | used by (classname: entities) | content |
|---|---|---|---|
| 0 general | 72 | `func_ambientset` 535, `trigger_multiple` 2 | area ambience: loop (71/72) + random one-shots (33/72); `distance` 0 → non-positional |
| 1 local | 70 | `environ_soundgen` 427, `misc_iraq_flatbed` 2, `misc_generic_radio` 1, `func_train` 1, `func_ambientset` 3 | point sources: loop (66/70) + random one-shots (22/70); `distance` > 0 |
| 2 triggered | 27 | `func_door` 301, `func_door_rotating_smart` 170, `func_door_rotating` 94, `func_button` 60, `spawner_monster` 25, `func_train` 9, `func_breakable_brush` 9, `func_wall` 5, `func_door_portcullis`, `func_door_secret`, `spawner_boosterpack` 2 each, `func_plat` 1, `environ_soundgen` 1 | mover sounds: `loop` = −1; `waves` = 3 entries (22 sets: paths end in `Opn`, `Mid`, `Cls` → start / moving / stop) or 1 entry (5 button sets) |

- Set names are **not unique across groups**: `InsideShakyElevator` and `ElevatorShaft` exist in group 0 and
  group 1 with different parameters. A lookup must therefore be per group (the entity class selects the group);
  name comparison must be case-insensitive (maps use mixed case). 162 of the 167 distinct names are used by maps.
- `loop` waves are looping sounds: 117 of the 122 (set, loop wave) uses point to a `.wav` carrying a `cue `
  chunk (the Quake II loop-start marker, SPEC-110 §8), while only 1 of 168 random one-shot uses does. In group
  2, the middle (`Mid`) wave has a `cue ` chunk in 10 of 22 sets, the start/stop waves never.
- `min_interval`/`max_interval` (hypothesis: seconds between random one-shots, uniformly drawn): typical values
  (1, 2), (2, 4), (35, 50); 43 sets have an interval but no random waves (ignored). `Empty` (no sounds) has 500/600.
  All 27 group-2 sets have exactly (0.5, 3.0) and volume (80, 160) — editor defaults, presumably unused.
- `distance` (hypothesis: audible radius or attenuation distance in world units) appears only in group 1.
- `volume_min/max` (hypothesis: per-playback random volume, 0–255 = silent…full).
- `flag_a`/`flag_b` = 1/3 everywhere (could be a version, channel, attenuation model or priority).

## Validation

`python tools/formats/ams.py validate <extract>/pak0/sound/sound.ams <extract>/pak0 <extract>/pak1 <extract>/pak2`:
parsed with **0 bytes left over**, 337 waves (all referenced), 72 + 70 + 27 = 169 sets, **0 referenced waves
missing** from the paks. Group↔entity mapping checked against the entity text of all 52 pak0 `.bsp` files:
1652 `soundName` values name a set; exceptions to the majority mapping are 3 `func_ambientset` using group-1
names, 1 `environ_soundgen` using a group-2 name and 1 `func_train` using a group-1 name. Three references name
no set at all: `kos1` `func_ambientset` `SoundDry`, and two `jpn1` `func_door` whose value is a wave path
(`Ambient/Doors/Norm/Metal/Elevator/Cls.wav`) — the engine must tolerate unknown names. (Other entity classes,
e.g. `environ_effect*`, use `soundName` for effect names, not sets.) JSON dump:
`openmerc-data/decoded/sound_ams.json`.

## Open questions

1. `flag_a` = 1, `flag_b` = 3 (UNKNOWN). Black-box test: change them in a copy of `sound.ams` in a mod pak and
   observe differences (none expected if they are a format/editor version).
2. Interval units and distribution (hypothesis: seconds, uniform in [min, max]). Black-box test: in a map with a
   `func_ambientset` using `AirBaseOutdoor` (10–15 s, one random wave `AirBy1`), record 10 minutes of audio
   and measure the gaps between fly-bys.
3. `distance` (hypothesis: radius in world units beyond which the source is inaudible, or the rolloff distance).
   Black-box test: walk away from an `environ_soundgen` with a known set (e.g. `ComputerMonitorTerminal`,
   300 units) while recording; find where it fades out.
4. Volume range semantics (random per playback vs. separate loop/one-shot volumes). Black-box test: record many
   repetitions of a random one-shot of a set with a wide range (e.g. a set with `volume_min` ≪ `volume_max`)
   and measure the level spread; set min = max in a copy and check that the spread disappears.
5. How the loop and the random one-shots of a general set are mixed (single channel or two) and whether
   general sets cross-fade when a new `func_ambientset` becomes active — black-box listening test.
6. Whether a map can load another `.ams` (only `sound/sound.ams` exists in stock data).
7. Behaviour for names that match no set (`kos1` `SoundDry`; `jpn1` doors naming a wave path): silence, or a
   wave-path fallback? Black-box test: stand at those entities in `kos1`/`jpn1` and listen (door open/close).
