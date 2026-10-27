# SPEC-111 — `.dms` dynamic music set

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files only — every `.dms` in the stock paks (30 files, all in `pak0` `sound/music/`). No game
binary was consulted.
Implementation: `tools/formats/dms.py` (reader, reference check, stereo render via `adp.py`)

## Purpose

One `.dms` per level (`GER1.dms`, `trn1.dms`, …) or deathmatch track (`DM1.dms`–`DM5.dms`) lists the music
pieces available in that level. Each piece is stereo, stored as two mono `.adp` files (SPEC-110):
`sound/music/<Set>/l<Name>.adp` (left) and `r<Name>.adp` (right). Most level sets hold a **Tension** and an
**Action** piece; a few hold named, scripted pieces (`Intro`, `CountDown`, `Swarm`, `FinalBoss`, `Outside`,
`Inside`, `Tunnel`, `TrainIntro`, …).

## Layout

All integers little-endian; file sizes 71–335 bytes.

### Header (9 bytes)

| Offset | Size | Type | Field | Meaning / observed values |
|---|---|---|---|---|
| 0 | 1 | uint8 | `version` | **11** in all files (`.ams` starts with the same value, SPEC-112) |
| 1 | 1 | uint8 | `piece_count` | 1–4 |
| 2 | 2 | uint16 | — | 0 in all files. UNKNOWN (padding, or high byte(s) of a wider count) |
| 4 | 5 | uint8[5] | `header_unknown` | `78 32 C8 A0 FF` (120, 50, 200, 160, 255) in all 30 files. UNKNOWN |

### Piece record (repeated `piece_count` times, no alignment)

| Offset | Size | Type | Field | Meaning / observed values |
|---|---|---|---|---|
| 0 | var | char[] | `left` | NUL-terminated path of the left-channel `.adp`, e.g. `sound/music/JPN3/lTension.adp` |
| … | var | char[] | `right` | NUL-terminated path of the right-channel `.adp` (same directory, `r` instead of `l`) |
| +0 | 1 | uint8 | `type` | 0, 1 or 3 — see Semantics |
| +1 | 1 | uint8 | `trigger` | 0 when `type` ≠ 0; 1, 2, 3, 4 numbering the type-0 pieces in file order |
| +2 | 1 | uint8 | `flag` | 0 or 1 (hypothesis: loop) |
| +3 | 1 | uint8 | `volume` | 60…250 (hypothesis: volume, 255 = full) |
| +4 | 8 | uint8[8] | reserved | zero in all 59 records |

The file ends right after the last record (checked in all files).

## Semantics

Observed piece types (59 records):

| `type` | name in path | records | `trigger` | `flag` | `volume` |
|---|---|---|---|---|---|
| 1 | `*Tension` | 21 | 0 | 0 | 100–190 |
| 3 | `*Action` | 21 | 0 | 0 | 130–250 |
| 0 | `Intro`, `TSR2`, `DM1`–`DM5`, `CountDown`, `Swarm`, `FinalBoss`, `Outside`, `Inside`, `Tunnel`, `TrainIntro`, `TrainAction`, `TrainTension`, `TrainExplosion` | 17 | 1…4 | 0 or 1 | 60–240 |

- `type` 1 and 3 always come as a pair in a level set (Tension then Action), and in every one of the 21 pairs
  the Action volume is higher than the Tension volume. Hypothesis: the type is an intensity level of the dynamic
  music (1 = tension/exploration, 3 = action/combat; 2 is unused in stock data), the engine switching between
  them by game state.
- `type` 0 pieces carry a running number `trigger` 1…n. Hypothesis: they are not chosen by intensity but started
  explicitly (map trigger / script / deathmatch) by this number.
- `flag` (type-0 only non-zero): 1 for `DM1`–`DM5`, `CountDown`, `Swarm`, `FinalBoss`, `Outside`, `Inside`,
  `TrainAction`, `TrainTension`; 0 for `Intro`, `TSR2`, `Tunnel`, `TrainIntro`, `TrainExplosion`. The 0 group
  are names of one-shot events (intro, explosion), so hypothesis: 1 = loop, 0 = play once. Tension/Action have
  0 but are presumably looped by the dynamic system.
- `volume`: hypothesis linear 0–255. The DM tracks use 60–70, i.e. quiet background.
- Left/right assignment is confirmed by the data: the trailing RIFF chunk encoded into the music streams
  (SPEC-110 §4) starts with the word `"LI"` in every `l*` file and `"ST"` in every `r*` file, i.e. `l*` holds the
  even (first) words of the interleaved stereo source.
- Both channels of every piece have identical length (59/59).

## Validation

`python tools/formats/dms.py validate <extract>/pak0/sound/music <extract>/pak0 <extract>/pak1 <extract>/zsond`:
**30 / 30** files parsed, 59 pieces, **118 distinct `.adp` referenced, 0 missing**; these are exactly the 118
`.adp` under `sound/music/` (no music file is unreferenced). `dms.py render` decoded all 59 pieces to stereo
WAVs (`openmerc-data/decoded/music/<set>/<piece>.wav`, 3112 s in total).

## Open questions

1. `header_unknown` = `78 32 C8 A0 FF` (UNKNOWN). Constant, so possibly global defaults of the music system
   (e.g. cross-fade times, intensity thresholds or volumes: 120, 50, 200, 160, 255). Black-box test: change
   each byte in a copy of a level's `.dms` in a mod pak and observe the change in transitions (fade lengths,
   when Action starts/stops) with a recorded audio trace.
2. `type` meaning (hypothesis above). Black-box test: in a level with Tension/Action (e.g. `jpn3`), record audio
   while idle and while in a firefight; check that the Action piece (identifiable from our decode) starts on
   combat. Also try `type` 2 in a modified `.dms`.
3. `trigger` (hypothesis above). Black-box test: search map entity strings / scripts for a music trigger
   number (map data, not binaries) and fire it in game; renumber in a modified `.dms` and observe.
4. `flag` = loop (hypothesis). Black-box test: let `TSR1` `Intro` (flag 0) and `GER3` `CountDown` (flag 1) run
   to their end and observe whether they repeat; swap the flag in a copy.
5. `volume` scale (hypothesis linear /255). Black-box test: record the same piece with volume 255 and 128 in a
   modified `.dms` and compare RMS.
6. Bytes 2–3 and the 8 reserved bytes per record: zero everywhere, meaning UNKNOWN; a writer should keep zeros.
