# ENG2 - movement jitter (SPEC-AMEND-010 M1-M11, SPEC-314, AMEND-009 C2/D1/D2)

Worker: C (ENGINE-SERVICES), branch `engine-services`. Sources: SPEC-314, SPEC-AMEND-010, SPEC-AMEND-009 only.

## Changes
- `common/sof_pmove.c/.h`: the SoF player-movement routine (SPEC-314 Behaviour 1-17, section 19/20 command
  expansion) with callbacks only, shared by the server (#81) and client prediction. AMEND-009 C2 (step-slide) is
  part of it. `test_sof_pmove.c` (`sofpm_test`, CMake option `OM_SOFPM_TESTS`): every SPEC-314 vector
  (G J L A D T V S Y P W K) plus the expansion (X1-X5): 324 checks, 0 failed (gcc and MSVC x86).
- `server/sv_pmove.c`: import #81 is a thin wrapper (block -> `sofpm_t`, the game's trace callback by default,
  `sv_pmove_gametrace 0` = engine trace; stagger, ground-is-client callback, `gserver SIZEMATTERS`).
- `client/cl_prediction.c`: SoF prediction through the same routine (section 20: cinematic freeze, flag 0x80,
  run doubling unless 0x40, speed byte = player-state field 15, splitting, stagger 0, no snap, mask 0x02010003,
  ground-is-client 1..configstring 31; step rule against the command before the last one). `cl_om_predlog`
  PRED line extended (moves, pm_type, flags, cinfreeze, speed byte, received origin).
- 0x80 is the no-prediction flag for SoF (was Quake II's 0x40) in `CL_CheckPredictionError` and the frame parse.
- `common/frame.c` (M1/M2): with a server in this process every rendered frame is a packet frame (one command,
  prediction and view per frame; `cl_maxfps` caps only a remote connection). `server/sv_main.c` (M10): the
  sub-millisecond remainder of the host frame time is kept (SV_Frame now runs every host frame); the existing
  low/high clamps already give at most one game frame per host frame.
- M3, M5-M8: audited against the existing code (parse and render clamps, view origin, 100 ms stair smoothing,
  error x (1 - fraction), angles, entity lerp) - no change needed. M9: no client-side Quake II bob in the SoF view.
- AMEND-009 D2 (`ghoul/ghoul_net.cpp`): records are matched by id only; the destroy/recreate and stale-removal
  bookkeeping are gone; the server's id-reuse avoidance stays. Test `TestIdOnlyRecords` (fails on the old code).
- AMEND-009 D1 (`fx/fx_system.c`, `cl_sofview.c`): zoom sound on a fov change between the frames, only for remote
  view type 16 or 0/0; more than 0.18 s on the client game-time estimate or that clock back by > 1 s;
  `Weapons/Sniper/Zoom.wav`, listener entity, channel 0, volume 0.6, attenuation 1.

## Measurements (`openmerc-data/eng2`, `jitstat.py`; dm/nycdm4 SP, 125 fps, 8 ms frames)
Before = 4ccb224 (`eng2_jit_base_dm`, `jit_dm.cfg`), after = this change (`eng2_jit_after2`, `jit_dm2.cfg`: the same
segments with 4x the waits, since `wait` now counts rendered frames).

| Segment | view/frame cv before -> after | view stalls | pred-stalls | p95 view jerk | prediction misses |
|---|---|---|---|---|---|
| walk | 1.28 -> 0.27 | 2 -> 0 | 131/175 -> 1/476 | 5.49 -> 0.002 | 14 -> 0 |
| back | 1.23 -> 0.18 | 5 -> 0 | 125/169 -> 1/418 | 5.32 -> 0.001 | 13 -> 0 |
| strafe | 1.27 -> 0.39 | 4 -> 10 | 131/175 -> 11/363 | 4.28 -> 0.124 | 14 -> 0 |
| run | 1.52 -> 0.33 | 14 -> 0 | 115/162 -> 1/423 | 7.16 -> 0.008 | 11 -> 0 |
| runback | 1.58 -> 0.12 | 12 -> 0 | 122/170 -> 1/380 | 7.97 -> 0.003 | 12 -> 0 |
| jump | 1.71 -> 0.44 | 4 -> 1 | 69/94 -> 1/243 | 2.39 -> 0.000 | 7 -> 0 |

No clamp messages with `cl_showclamp 1` / `sv_showclamp 1`. tsr1 (`eng2_jit_after_tsr1`, 16k-frame wait): the
intro runs with cinematic freeze 1 for about 64 s; afterwards one -35 miss at the hand-over and five misses of
2-3 eighths in the run/jump segments (open).
