# SPEC-141 — Scripted motion files (.rof, "ROFF" version 1)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files `ds/roff/*.rof` / `*.ROF` (stock: 312 files in pak0).
Code: `tools/formats/rof.py` (reader, validator, charts, map overlays).

Sources: stock data; BSP entity lumps (SPEC-101 reader) and compiled scripts (SPEC-140) as
cross-references. Public reference for comparison only (no code used): the GPL-2.0 Jedi Knight II
source as published in OpenJK, `codeJK2/game/g_roff.h` / `g_roff.cpp`
(https://github.com/JACoders/OpenJK), which documents a Raven "ROFF" version 1 file with the same
header (`"ROFF"`, int version, float count) and the same 24-byte frame (origin delta, rotation delta).
That later engine plays version 1 files at a hard-coded 100 ms per frame; whether Soldier of
Fortune does the same is an open question below.

## Purpose

A ROF is a pre-recorded rigid-body motion: a list of per-frame position and orientation changes,
exported from an animation tool. Scripts play a ROF on an entity (opcode 0x29 `ROF`, SPEC-140) to
move doors, debris, vehicles and cinematic cameras along hand-animated paths.

## Layout

Little endian, no padding.

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | char[4] | magic `ROFF` |
| 4 | 4 | int32 | version, always 1 |
| 8 | 4 | float32 | frame count, written as a float (integral in all stock files) |
| 12 | 24 × n | frame[n] | n = (file size − 12) / 24 |

Frame (24 bytes):

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 12 | float32[3] | origin delta x, y, z (world units) |
| 12 | 12 | float32[3] | angle delta pitch, yaw, roll (degrees) |

The count field equals n in 307 files. In 5 files it does not, while the file body is still a whole
number of frames: `bos_cifrontbeam_lf.ROF`, `bos_cifrontbeam_rt.ROF` (count 8, 6 frames present),
`sud3_chaindoor_lf2.ROF` (14 / 7), `u_turn_e.ROF`, `u_turn_w.ROF` (50 / 51). A reader must not read
past the end of the file; `rof.py` uses the frames present and reports the mismatch.

## Semantics

- Frames are **deltas**, applied cumulatively to the entity's pose. Evidence:
  `generic_door_cw.rof` (kicked door) has only yaw deltas: −32.2, −32.2, −25.6 (sum −90°) followed by a
  damped oscillation; `arm3_trash_lidrt.rof` rolls 40, 40, 40, −10, 5, 5 (sum 120°);
  `Irq1a_endjohn.rof` moves ~12.7 units per frame with small yaw changes (walking path).
- Of the 312 files, 107 only translate, 36 only rotate, 168 do both; 127 start with an all-zero frame.
  Median length 30 frames (min 5, max 601). Per-frame displacement: median 3.05 units, 99th percentile
  80, maximum 4 354.7 (a camera cut inside one ROF). Angle deltas are not normalised: values near
  ±360 (max 358.6) appear where a small turn crossed the ±180 wrap.
- Overlaying ROF paths on the maps — start at the target entity's `origin` (point entities) or the
  centre of its brush model, accumulate deltas in world axes — gives paths that follow roads and
  corridors (`openmerc-data/scripts/rof/*_rof_paths.png`: ger4, irq1a, kos1, nyc1). This supports
  world-axis deltas for those entities; entities with a non-zero initial yaw were not distinguished.

### Relation to scripts and maps

- Scripts name ROFs by bare file name (`"arm3_trash_lidrt.rof"`), resolved in `ds/roff/`
  case-insensitively (239 stock files have the extension in upper case).
- Opcodes that take a ROF name: `ROF` 0x29 (play on an entity; optional signal variable — hypothesis:
  signalled when the motion ends, since scripts wait on it with WAIT_SIGNALS), `PRECACHE_ROF` 0x2F (precache scripts), `ROF_RELEASE` 0x32 (after use).
- Scripts reference 315 distinct names: all 312 stock files are used; 3 names have no file
  (`trainhemi.rof` in `trn1/trn1_introcam.os`; `trs1_introcam1_ren.rof`, `trs1_introcam1t_ren.rof` in
  `tsr1/nycintro.os`) — the engine must tolerate a missing ROF.
- Targets of `ROF` (from the maps' entity lumps): `func_wall` 168, `func_breakable_brush` 51,
  `func_remote_camera` 43, `info_notnull` 25, `func_door_rotating` 17, `func_door` 14,
  `m_x_chopper_green` 12, others.

## Validation

`python tools/formats/rof.py validate <ds/roff> <ds roots…>`:
312 / 312 files parse (magic, version 1, body a multiple of 24); 14 874 frames; 5 count-field
mismatches (listed above); cross-check with all 730 scripts as described. Viewable outputs:
`openmerc-data/scripts/rof/generic_door_cw.png`, `irq1a_endjohn.png` (per-frame and accumulated
curves) and the four map overlays.

## Open questions

1. Playback rate (frames per second) and whether motion is interpolated between frames. Black-box test:
   time `jpn2/kickdoor.os` (25-frame door) in the running game; 2.5 s would mean 10 Hz.
2. Are deltas applied in world axes or in the entity's local frame when the entity starts rotated?
3. Which value wins for the 5 count mismatches (header count or frames present), and what happens at
   the end of the file when the count is larger.
4. Rotation order / pivot for brush models (origin brush vs model centre) — needs black-box comparison.
5. Exact meaning of ROF_RELEASE (0x32).
