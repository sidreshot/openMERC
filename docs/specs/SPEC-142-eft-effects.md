# SPEC-142 — Effect definitions (.eft)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files `effects/**/*.eft` (stock: 211 files in pak0).
Code: `tools/formats/eft.py` (reader, validator, field census, JSON dump).

Sources: stock data; BSP entity lumps (SPEC-101 reader) for the map cross-reference; approved specs
SPEC-005 (effect system, editor commands `fx_save`/`fx_load`) and SPEC-203 (configstrings CS_EFFECTS,
`effects/<name>.eft`).

## Purpose

An `.eft` describes a composite visual/audio effect (muzzle flashes, explosions, blood, smoke, sparks,
rain, debug markers for AI editing in `effects/edit/`). It is a list of *primitives*: particle
emitters, sounds, lights, decals and one further kind. Records are raw memory images of the editor's
structures: unused bytes contain fill patterns (0xEB…, 0xCD…) and leftovers of earlier strings. Fill
and leftovers carry no meaning and must be ignored.

## Layout

Little endian. Counts and type codes are stored as **float32** holding small integers.

### File

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | float32 | format version: 3.0 (152 files), 2.0 (58), 1.0 (1: `spark.eft`) |
| 4 | 4 | int32 | h1: 0 (167) or 1 (44) — UNKNOWN (flag) |
| 8 | 4 | float32 | h2 — UNKNOWN (often 0, 0.05, 0.1, 0.2; equals h3 in 199 files) |
| 12 | 4 | float32 | h3 — UNKNOWN (pair with h2: min/max?) |
| 16 | 4 | float32 | primitive count n (1–10) |
| 20 | … | primitive[n] | consecutive, sizes below |
| end − 8 | 8 | float32[2] | trailer, versions 2 and 3 only; equals (h2, h3) in 197 of 210 files — UNKNOWN |

### Primitives

Every primitive starts with `float32 type`. Its size depends on (version, type) and, for type 0, on
`float32 kind` at primitive offset +0x60.

| Version | Type (our name) | Kind | Size (bytes, incl. type) | Count | Name field |
|---|---|---|---|---|---|
| 3 | 0 particles | 7 | 572 | 426 | +0xC4 char[64] (texture path, no extension) |
| 3 | 0 particles | 6 | 564 | 7 | +0xC4 |
| 2 | 0 particles | 5 | 556 | 12 | +0xC4 |
| 2 | 0 particles | 6 | 556 | 25 | +0xC4 |
| 2 | 0 particles | 1 | 584 | 3 | +0x1B8 |
| 2 | 0 particles | 2 | 684 | 29 | +0x1B8 |
| 2 | 0 particles | 3 | 752 | 1 | +0x1B8 |
| 1 | 0 particles | 1 | 572 | 2 | +0x1B8 |
| 3 | 1 sound | – | 92 | 71 | +0x04 char[64] (`.wav` path) |
| 2 | 1 sound | – | 84 | 5 | +0x04 |
| 3 | 2 light | – | 52 | 14 | none |
| 2 | 2 light | – | 44 | 1 | none |
| 3 | 3 decal | – | 100 | 12 | +0x08 char[64] (texture path) |
| 2 | 3 decal | – | 92 | 6 | +0x08 |
| 3 | 4 (UNKNOWN) | – | 44 | 31 | none |

Type names are hypotheses from content: type 1 holds a `.wav` name; type 2 holds a radius-like pair
(e.g. 280/350) and three 0–255 pairs; type 3 holds a sprite name (`puddle`, `blood_pool`, `scorch`);
type 4 is mostly zeros (files `exphit`, `gotshot`, `land`, `lighthit` contain only a type 4 —
hypothesis: a view effect such as camera shake or screen flash).

### Type 0, version 3, kind 7 (572 bytes) — field census

Offsets relative to the primitive start. "×n" = number of the 426 stock records with that value.
Most numeric fields come in (min, max) pairs. Meanings are hypotheses unless stated.

| Offset | Type | Typical values | Notes |
|---|---|---|---|
| +0x000 | float | 0 | type |
| +0x004 | float | 3 ×426 | constant, UNKNOWN |
| +0x008, +0x00C | float pair | 0.05 ×272 | hyp. emission interval min/max (s) |
| +0x010, +0x014 | float pair | 0.005 ×285 | UNKNOWN |
| +0x018 | int32 | 1, 0, 4, 8 | UNKNOWN flags |
| +0x01C | int32 | 1…6 | hyp. particles per emission |
| +0x020, +0x024 | int32 | 0…33, 0…20 | UNKNOWN |
| +0x028 … +0x03C | float[3], float[3] | (−20,−20,−20), (20,20,20) | hyp. spawn box mins / maxs |
| +0x040 | float | 128 ×355 | UNKNOWN |
| +0x048, +0x04C | float | 20, 64 | UNKNOWN |
| +0x050 … +0x05C | float | 1, 0, 1, 0…1 | UNKNOWN |
| +0x060 | float | 7 | kind (selects size; 6 = same layout minus +0x218/+0x21C) |
| +0x064 … +0x078 | 3 float pairs | ±10, ±20, ±100 | hyp. initial velocity range per axis |
| +0x07C … +0x088 | 2 float pairs | mostly 0 | UNKNOWN |
| +0x08C, +0x090 | float pair | −800, −600, 300 | hyp. vertical acceleration (gravity) |
| +0x094 | int32 | 0…4 | UNKNOWN |
| +0x098 … +0x0A4 | 2 float pairs | 1…6, 0…10 | UNKNOWN |
| +0x0A8 … +0x0B4 | 2 float pairs | (4,4), (8,8) | hyp. start / end size |
| +0x0B8 | int32 | 0…3 | UNKNOWN |
| +0x0BC, +0x0C0 | float pair | 40, 40 | UNKNOWN |
| +0x0C4 | char[64] | `textures/sprites/smkwht`, `effects/explosion/explode80` | texture/sprite name; empty in 30 records |
| +0x104, +0x108 | float pair | 0.2…1.5 | hyp. particle lifetime (s) |
| +0x10C … +0x128 | float[8] | 0…255 (255 dominant) | colour components; pairing UNKNOWN (two RGBA or four min/max pairs) |
| +0x12C, +0x130 | float | 0/1, 0.5 | UNKNOWN |
| +0x134 … +0x140 | float[4] | −3, 6, −1.5, 5 | UNKNOWN |
| +0x144 | int32 | 5, 37, 289, 293, 305 | bit flags (bits 0x1, 0x4, 0x20, 0x100 seen) — UNKNOWN |
| +0x148 … +0x15C | 3 float pairs | mostly 0, ±2, ±3 | UNKNOWN |
| +0x160 … +0x1A3 | 68 bytes | text leftovers / fill | buffer, no meaning observed |
| +0x1A4 | float | 60 ×403 | UNKNOWN |
| +0x1A8 … +0x1E7 | 64 bytes | fill / leftovers | buffer |
| +0x1E8 … +0x214 | 6 float pairs | mostly 0; ±200, 360, ±500 | UNKNOWN |
| +0x218, +0x21C | int32 | 0…3 | present in kind 7 only |
| +0x220, +0x224 | float pair | 0.05 | UNKNOWN |
| +0x228 | int32 | 0/1 | UNKNOWN |
| +0x22C … +0x238 | float[4] | 0, 0.5, 0.1, 0.1 | UNKNOWN |

The full census (every offset) is produced by `eft.py census <dir> 3 7` and stored in
`openmerc-data/scripts/eft_census_v3_kind7.txt`.

### Other primitive types

| Type | Version 3 layout | Version 2 difference |
|---|---|---|
| 1 sound | +0 type; +4 char[64] file name; +0x44 float pair (0.6/0.6: hyp. volume); float pair (0/2: hyp. attenuation); float pair | 2 floats fewer (84 bytes) |
| 2 light | +0 type; float pair (radius?, e.g. 272/272); 3 float pairs 0–255 (colour); float pair (0.05); float pair (1.35: hyp. duration) | one pair fewer (44 bytes) |
| 3 decal | +0 type; +4 int32 (0/1); +8 char[64] texture; then floats (e.g. 2, 25, −3.1, 3, 0) | 2 floats fewer (92 bytes) |
| 4 | +0 type; 10 floats, mostly 0, sometimes 1 | – |

## Semantics / relation to maps

- Map entities `environ_effect` and `environ_effect_continual` name an effect with the key
  `soundname`/`soundName` (sic), as a path under `effects/` without extension (all 100 such
  entities carry it; 20 distinct effects, all present, e.g. `environ/gb_rain`). Other effects (weapons/, gore/, edit/…) are not named
  by any map or script; they are referenced from engine/game code, which is outside this spec. At run
  time effect names travel to clients as configstrings CS_EFFECTS and are resolved as
  `effects/<name>.eft` (SPEC-203). The files are written by the in-game effect editor (`fx_save`,
  SPEC-005), which explains the memory-image fill bytes.
- Names inside primitives are paths without extension for textures (`textures/sprites/…`,
  `effects/explosion/…`) and with `.wav` for sounds.

## Validation

`python tools/formats/eft.py validate <effects>`: 211 / 211 files parse exactly — header, n primitives
with the size table above, trailer, end of file. Primitive totals: 505 particle (all kinds),
76 sound, 15 light, 18 decal, 31 type 4. JSON dump of every effect (header, primitives, names, all
32-bit words as floats): `openmerc-data/scripts/eft_effects.json`.

The size table was found by constraint solving over all files (single-primitive files give the size of
their kind; multi-primitive files then split uniquely); every file is consistent with one table.

## Open questions

1. Version 1 (`spark.eft` only) has no trailer if its kind-1 primitives are 572 bytes; with one sample a
   568-byte primitive plus 8-byte trailer cannot be excluded.
2. Meaning of header h1/h2/h3, of the trailer, of type 4, and of most particle fields (see census).
   Black-box approach: spawn `environ/gb_rain` etc. through a map entity, edit one field at a time in a
   copy of the file, and observe.
3. Layout of particle kinds 1–3 (name at +0x1B8, 244-byte 0xCD block before it) beyond the name.
4. Whether colour/size pairs are (start, end) or (min, max) random ranges.
5. How the engine chooses which `.eft` to use for weapons, gore and AI-edit markers (code interface).
