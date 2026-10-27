# SPEC-100 — `.m32` texture

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — every `.m32` in `pak0`–`pak3`, `zmain`, `gs` (4322 files). No game binary was consulted.
Implementation: `tools/formats/m32.py`

## Purpose

`.m32` holds every world texture (`textures/<set>/<name>.m32`), sprite (`textures/sprites`), sky face
(`textures/sky`), and most 2D pictures (`pics/…`, `ghoul/pmodels/portraits`). It is a 32-bit relative of
the Quake II `.wal` (`miptex_t` in `qcommon/qfiles.h`): a fixed header with name links and surface
properties, followed by a mip chain of RGBA pixels.

Background: the Valve wiki notes M32 among the WAL variants used by Quake II-engine games
(<https://developer.valvesoftware.com/wiki/Miptex>). No public layout was found; everything below comes
from the data.

## Layout

All integers little-endian. Header is **968 bytes** (`0x3C8`); the first pixel byte is at 968 in every file.

| Offset | Size | Type | Field (our name) | Meaning / observed values |
|---|---|---|---|---|
| 0x000 | 4 | int32 | `version` | always **4** |
| 0x004 | 128 | char[128] | `name` | author's source name, NUL-terminated, rest zero. Informational (see Semantics) |
| 0x084 | 128 | char[128] | `alt_name` | another texture, `<set>/<name>` (714 files). Use UNKNOWN |
| 0x104 | 128 | char[128] | `anim_next` | next texture of an animation/toggle chain (109 files) |
| 0x184 | 128 | char[128] | `damaged_name` | texture shown when the surface is damaged (291 files) |
| 0x204 | 64 | uint32[16] | `widths` | width of mip level *i*; 0 after the last level |
| 0x244 | 64 | uint32[16] | `heights` | height of mip level *i*; 0 after the last level |
| 0x284 | 64 | uint32[16] | `offsets` | file offset of mip level *i*; 0 after the last level |
| 0x2C4 | 4 | uint32 | `flags` | bits 0–23: surface flags; bits 24–31: material id |
| 0x2C8 | 4 | uint32 | `contents` | Quake II contents bits for brushes using this texture |
| 0x2CC | 4 | uint32 | `value` | light intensity for light surfaces; see Semantics |
| 0x2D0 | 4 | float32 | `scale_x` | 1.0 (3266 files) or 0.5 (1056); always equal to `scale_y`. Use UNKNOWN |
| 0x2D4 | 4 | float32 | `scale_y` | |
| 0x2D8 | 4 | int32 | `unknown_2d8` | 1 (3397) or 0 (925); 0 for every `pics/` and `ghoul/` image. UNKNOWN |
| 0x2DC | 128 | char[128] | `detail_name` | second-pass texture: a detail texture (`*/d_*`) or an environment map (`*/env*`) (2739 files) |
| 0x35C | 4 | float32 | `detail_scale_x` | 0, 1, 2, 3, 4, 5, 6, 8, 10 (4 most common) |
| 0x360 | 4 | float32 | `detail_scale_y` | 0, 1, 2, 3, 4, 6, 8, 10, 12 |
| 0x364 | 4 | float32 | `detail_shift_x` | 0 except 4 files (`uganda/2_bsew*`: 0.05/0.04; `uganda/2_blood1`: 1.0). UNKNOWN (scroll speed?) |
| 0x368 | 4 | float32 | `detail_shift_y` | as above |
| 0x36C | 4 | float32 | `unknown_36c` | 0 in all 4322 files |
| 0x370 | 4 | uint32 | `detail_blend_src` | 0x306 or 0 |
| 0x374 | 4 | uint32 | `detail_blend_dst` | 0x300 or 0 |
| 0x378 | 4 | uint32 | `kind` | bit set, see table below |
| 0x37C | 4 | float32 | `damage_value` | non-zero **iff** `damaged_name` is set (291/291): 1, 5, 10, 20, 40, 60 |
| 0x380 | 72 | — | reserved | zero in all files |
| 0x3C8 | … | uint8[] | pixels | mip levels, see below |

### Mip chain

- Level 0 is `widths[0] × heights[0]`. Every stock texture has power-of-two sides (8…512; 64×64 most common).
- Level *i+1* = `max(1, w/2) × max(1, h/2)` of level *i*.
- `offsets[0] = 968` and each next offset = previous offset + `w·h·4`, without padding.
- Normal textures continue until **1×1** (4077 files). If `kind & 0x1` is set, the chain is shorter (242 files with
  one level, 3 with two levels: `conback`, `end_game_art`, `mission_failed` 512×512); this bit and a short chain
  coincide in 245/245 files.
- Unused slots of `widths`/`heights`/`offsets` are zero.
- File size = end of the last level. Single exception: `zmain/pics/console/conchars.m32` has 26 extra bytes, a
  TGA 2.0 footer (`8×00` + `TRUEVISION-XFILE.\0`) left over from conversion. Ignore trailing bytes.

### Pixels

- 4 bytes per pixel in order **R, G, B, A**, rows **top to bottom**, left to right. Confirmed visually: an Iraqi
  flag shows red at the top and green stars, `CLIP` is orange, sign and logo text reads correctly
  (`renders/m32_contact_sheet.png`).
- Each level *n+1* is the 2×2 box-filtered level *n* (mean absolute RGB error 0.25 over 374 random files, which
  is rounding).
- A is real alpha: chain-link, grates, flares, UI pictures and fonts have alpha < 255; see flag 0x800.

## Semantics

### `flags`, low 24 bits (surface flags)

Bits shared with Quake II (`q_shared.h` `SURF_*`) carry the same meaning in the stock data (names checked
against textures such as `clip`, `hint`, `skip`, `sky`, `water`).

| Bit | Files | Meaning |
|---|---|---|
| 0x1 | 166 | light-emitting surface (`value` = intensity) — Q2 `SURF_LIGHT` |
| 0x2 | 1 | slick — Q2 `SURF_SLICK` |
| 0x4 | 10 | sky — Q2 `SURF_SKY` |
| 0x8 | 16 | warped liquid (water, oil, slime, blood, molten metal) — Q2 `SURF_WARP` |
| 0x10 / 0x20 | 34 / 7 | translucent 33 % / 66 % — Q2 `SURF_TRANS33/66` |
| 0x40 | 2 | flowing — Q2 `SURF_FLOWING` |
| 0x80 | 50 | no draw — Q2 `SURF_NODRAW` |
| 0x100 / 0x200 | 9 / 9 | hint / skip (compiler only) |
| **0x800** | 419 | **image has alpha**: set **iff** some pixel of level 0 has A < 255 (419/419, no exception) |
| **0x1000** | 96 | UNKNOWN. On screens, monitors, lamps, cameras and box lids; 85 of the 96 also have `anim_next` |

No stock `.m32` sets other low bits. BSP texinfo adds 0x2000, 0x8000, 0x10000, 0x20000, 0x200000, 0x400000,
0x800000 (SPEC-101).

### `flags`, high byte: material id

Values 0–42. 0 = none (760 files, includes all `pics/`). The names of the textures using each id are consistent
with a surface-material table (used, for example, for footsteps and impacts):

| id | files | name pattern (examples) |
|---|---|---|
| 1 | 1498 | crates, ducts, beams, doors, desks — generic metal/default |
| 2–8 | 78 | sand, sandbags, dirt, gravel, road, hay, plaster, stucco (id 3: one file, `1_lobby3`) |
| 9 | 16 | snow, ice |
| 10, 13 | 14 | water, pools, oil |
| 11, 12 | 2 | tank, molten metal |
| 14–16 | 267 | wood (crates, floors, bookcases, shingles) |
| 17–24 | 1130 | concrete, block, stone, brick, tile, plaster, carpet, rubber |
| 25, 26 | 8 | grass, hay, camouflage net |
| 27 | 5 | sewer, blood, meat |
| 29, 30, 42 | 29 | pipes, barrels, tanks, radiators, vents |
| 32 | 153 | computers, consoles, buttons, panels |
| 34 | 1 | snow (cracked) |
| 37 | 232 | glass, windows, lights |
| 38 | 74 | lit signs, control panels |
| 39 | 28 | vending machines |
| 40 | 27 | paper, paintings, calendars, magazines |

The exact meaning of each id (and of the unused ids 28, 31, 33, 35, 36, 41) is UNKNOWN. In BSP texinfo the same byte
appears in `flags` and agrees with the `.m32` in 99.75 % of 219 199 texinfos.

### `contents`

| Value | Files |
|---|---|
| 0 | 4292 |
| 0x20 (Q2 `CONTENTS_WATER`) | 13: water, slime, blood, molten |
| 0x30000 (Q2 `PLAYERCLIP`\|`MONSTERCLIP`) | 9: every `clip` |
| 0x1000000 (Q2 `CONTENTS_ORIGIN`) | 8: every `origin` |

### `value`

With flag 0x1: 30000 (77 files), 10000 (3), 3000 (4), small values 0–15 (80). Without flag 0x1: only together with
flag 0x1000 or on `subway/1_static*` (50). As in Quake II, mappers can override it per face (texinfo `value`
differs from `.m32` in 10.6 % of texinfos). The meaning of the small values is UNKNOWN.

### Name fields

- `name` ends with the file's game path in 4235 files (case-insensitive, without extension). The other 87 hold a
  different source label (`DM_faces/amu`, `ghoul/pmodels/teamicons/…`, `pics\scope/…`, names with `.m32`).
  The engine must find textures **by path**, not by this field.
- Directory case differs between files and references (`Armory/` vs `armory/`, `pak2/textures/Bosnia`).
  Compare names case-insensitively.
- `alt_name`, `anim_next`, `damaged_name`, `detail_name` are written `<set>/<name>` (no `textures/`, no extension).
  Resolved against `textures/<value>.m32`: alt 714/714, damaged 291/291, detail 2739/2739, anim 108/109 found
  (the dangling one is `tokyo/2_clean_pillar2b → tokyo/2_clean_pillar2a`).
- `anim_next` forms chains or 2-cycles (`button_1 ↔ button_2`, `med_inside → med_inside_used`). In maps, texinfo
  `nexttexinfo` chains follow exactly these links (1358/1358).
- A texture and its `damaged_name` or `anim_next` target always have the same pixel size (291/291, 108/108).
- `alt_name` usually points at the plain base variant (`*_g1`…`*_g6` graffiti → base, `*bot` → base); its size
  differs in 106 of 714 cases. Its use by the engine is UNKNOWN.

### Detail / environment pass

- `detail_name` names a texture whose own `kind` says what it is: `kind & 0x2` (97 files: 96 `d_*` plus
  `subway/sign17`; `siberia/d_woodsmooth` lacks the bit) or `kind & 0x8` (26 files: 24 `env*` plus
  `subway/1_danger`, `1_danger2`).
- With a `d_*` target, `detail_blend_src/dst` = 0x306/0x300 in all 2101 files and `detail_scale_*` is non-zero
  in 2098.
  The pair 0x306/0x300 equals OpenGL `GL_DST_COLOR`/`GL_SRC_COLOR` (2× modulate). This reading is an inference
  from the numbers, not an observation.
- With an `env*` target, 375 files have scale 0 and blend 0; 263 have scale and blend set like a detail texture.
- How the second pass is drawn (scale relative to what, shift as scroll) is UNKNOWN until observed in the game.

### `kind` (0x378)

| Bit | Files | Observation |
|---|---|---|
| 0x1 | 245 | short mip chain (UI pictures, portraits, 18 `env1a/b/c`) — exact |
| 0x2 | 97 | detail texture |
| 0x4 | 47 | UNKNOWN; only on signs, posters, graffiti, calendars, flags, bulletin boards (legible content) |
| 0x8 | 26 | environment map |

Observed combinations: 0, 1, 2, 4, 6 (`subway/sign17`), 8, 9.

### `scale_x` / `scale_y`

0.5 on 1056 files of all sizes (8×8 … 512×512). Texinfo vectors in maps do **not** depend on it (|vecs| of 2.0
and 1.0 are equally common for both values), and texture pairs that must tile identically (`damaged_name`) differ
in scale in 58 cases while their pixel sizes always match. So texture coordinates use the raw pixel size,
as in Quake II. What the scale does is UNKNOWN (an editor default?).

## Validation

- `python tools/formats/m32.py validate <extracted paks>`: **4322 parsed, 0 failed**. Checks: version, power-of-two
  sizes, halving rule, contiguous offsets, zero unused slots, data not truncated, chain length agrees with
  `kind & 0x1`. Notes: 1 file with trailing bytes (conchars, TGA footer); 0 files whose alpha content disagrees
  with flag 0x800.
- Cross-check with 76 BSPs: all 219 199 texinfo names resolve to a `.m32`.
- Visual: `openmerc-data/renders/m32_contact_sheet.png` (73 images from all sets, sprites, sky, UI, portraits,
  alpha on a checkerboard). The textured BSP renders (SPEC-101) also show correct texture orientation.

## Open questions

1. `alt_name` — when does the engine substitute it?
2. Flag 0x1000 and the small `value` numbers on such surfaces (animation rate? screen? fullbright?).
3. `kind` bit 0x4.
4. `unknown_2d8` (0/1) and `scale_x/y` (1.0/0.5).
5. `detail_shift_x/y` (non-zero only on 4 Uganda sewer/blood textures; scroll?), `unknown_36c` (always 0).
6. Exact drawing of the detail/env pass (scale reference, blend; 0x306/0x300 = GL factors is an inference).
7. `damage_value` units (hits? damage points?).
8. Names of material ids 0–42.
