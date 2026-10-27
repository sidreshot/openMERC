# SPEC-144 — Train-level terrain (.trn) and level images (.raw)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files `env/train.trn` (the only stock .trn, pak0) and `maps/<map>.raw`
(stock: 32, pak0).
Code: `tools/formats/trn_raw.py`.

Sources: stock data; cross-references to approved specs SPEC-002 (renderer: terrain-set slot, `rawshot`),
SPEC-203 (configstring CS_TERRAINSET) and SPEC-303 (savegame `SHOT` chunk).

## Purpose

- `maps/<map>.raw` — a 128 × 128 true-colour picture of each single-player level (in-game screenshots,
  several with the HUD visible). The 32 names match the 32 single-player maps in `maps/` one to one.
  Per SPEC-303 an automatic save stores the contents of `maps/<map>.raw` (if exactly 65 536 bytes) as
  its 128×128 `SHOT` thumbnail; per SPEC-002 the renderer command `rawshot` writes
  `rawshot/sofNN.raw`, which is how such files are produced. No text file names a `.raw`.
- `env/train.trn` — a "terrain set": description of the procedurally scrolled landscape outside the
  train in level `trn1` (features along the track, sprite and texture lists). `trn1.bsp` worldspawn
  has `"terrainset" "train"`; per SPEC-203 this becomes configstring CS_TERRAINSET and per SPEC-002
  the renderer loads `env/<name>.trn` (console command `terrain <name>` does the same; a bad version
  prints `Invalid version for Terrain Set <name>`).

## .raw layout

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 65 536 | uint8[128][128][4] | pixels R, G, B, A; rows stored **bottom-up** (first row = bottom of the picture); A = 255 in every pixel of every file |

Decoding as RGBA and flipping vertically yields upright screenshots (HUD at the bottom). Contact
sheet: `openmerc-data/scripts/raw/levelshots_sheet.png`.

## .trn layout

Byte-packed little endian (fields are not 4-byte aligned in the header).

Header (140 bytes):

| Offset | Size | Type | Value in train.trn | Meaning |
|---|---|---|---|---|
| 0x00 | 2 | uint16 | 0x0122 | UNKNOWN; probably the version that SPEC-002's "Invalid version" check tests (which byte(s) are tested is not known) |
| 0x02 | 4 | int32 | 150 | UNKNOWN |
| 0x06 | 4 | int32 | 40 | UNKNOWN |
| 0x0A | 4 | int32 | 23 | number of feature records |
| 0x0E | 4 | int32 | 1 | UNKNOWN |
| 0x12 | 4 | int32 | 1 | UNKNOWN |
| 0x16 | 4 | int32 | 6 | number of texture entries |
| 0x1A | 4 | float32 | 3500 | UNKNOWN (distance?) |
| 0x1E | 20 | – | zero | |
| 0x32 | 4 | int32 | 1 | UNKNOWN |
| 0x36 | 5 | – | zero | |
| 0x3B | 16 | float32[4] | 1, 1, 1, 1 | UNKNOWN (hypothesis: RGBA tint) |
| 0x4B | 1 | uint8 | 1 | UNKNOWN |
| 0x4C | 8 | – | zero | |
| 0x54 | 4 | float32 | 1000 | UNKNOWN |
| 0x58 | 4 | int32 | 900 | UNKNOWN |
| 0x5C | 4 | float32 | 8300 | UNKNOWN |
| 0x60 | 4 | float32 | 150 | UNKNOWN |
| 0x64 | 4 | float32 | 200 | UNKNOWN |
| 0x68 | 36 | – | zero | |

Feature record (68 bytes, ×23 from offset 0x8C):

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | int32 | type 0–5 (3 occurs 13×) — UNKNOWN meaning |
| 4 | 4 | int32 | −1 (21×) or 0 (2×) — UNKNOWN |
| 8 | 8 | float32[2] | start, end: increasing values from −500 to 20 020 (hypothesis: distance along the track) |
| 16 | 8 | float32[2] | a, b: e.g. (1100, 1700), (−1020, −1000) (hypothesis: height range of the ground) |
| 24 | 8 | float32[2] | (0.3, 0.2) or 0 — UNKNOWN |
| 32 | 4 | int32 | 2 or 0 — UNKNOWN |
| 36 | 16 | float32[4] | group 1, e.g. (1.385, 0.971, 600, 600) or zeros |
| 52 | 16 | float32[4] | group 2, e.g. (1.481, 2.277, 600, 600) or zeros |

All non-zero first two floats of the groups are multiples of 1/314 (435/314, 305/314, 465/314…) —
recorded as an observation; meaning UNKNOWN.

Sprite section: marker `2D 42 01 00`, int32 count (2), then per sprite: NUL-terminated name, float32
(0.333 / 1.0), float32 64, float32 64, int32 0, int32 0. Names: `sprites/terrain/bush1.sp2`,
`bush2.sp2` — neither file exists in the stock paks.

Texture section: marker `2D 42 01 00`, then (header count) entries: int32 1, NUL-terminated name,
float32 × 5 (1, 32 or 64, 64, 1 or 2, 1 or 2), uint8 1. Names: `textures/uganda/t_dirt_slopeb.m32`,
`t_track_bed.m32`, `t_dirt_sandtran.m32`, `t_terrain5.m32` (twice), `water1.m32` (stored as
`Water1.m32` in pak0; lookup must be case-insensitive). End marker `2D 42 01 00` = end of file.

## Validation

- `trn_raw.py raw-validate`: 32 / 32 `.raw` files are 65 536 bytes with alpha 255 everywhere; all decode to
  recognisable level screenshots (checked visually on the contact sheet).
- `trn_raw.py trn-info env/train.trn`: the single file decodes to exactly its end (header, 23 records,
  2 sprites, 6 textures, final marker). With one sample, the field boundaries of the header and the
  meanings of nearly all values are unverified.

## Open questions

1. Whether the bottom-up row order is kept in the savegame `SHOT` chunk (SPEC-303 copies the bytes) and
   flipped only when displayed.
2. Nearly every `.trn` field: units of start/end, meaning of feature types, the 1/314 quantised group
   values, header integers 150/40/1/1, floats 3500/1000/8300/150/200. Black-box: play trn1 and compare
   the scenery passing the train with the feature list (e.g. the change of a/b to −1020/−1000 at
   9 200–13 270 suggests a valley or bridge).
3. The missing `.sp2` sprites: does the game skip them, or are bushes drawn from elsewhere?
