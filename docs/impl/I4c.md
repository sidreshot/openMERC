# I4c — Renderer: the remaining features of I4a's specs (work log)

Package: SPEC-002, SPEC-144, SPEC-410, SPEC-412, SPEC-413 (what I4a left open, see `docs/impl/I4a.md` *Remaining*).
Directory: `engine/src/client/refresh/gl3/` (I4a's code; I4b works in `gl3_ghoul*.c` at the same time).
Build tree: `build-I4c-x86`. Test copy: `D:\Games\SoF-test-I4c` (paks hard-linked from the oracle, executables and
DLLs copied, `user\config.cfg` from the I4a copy: `vid_brightness 0.6`, `vid_contrast 0.6`).

## State
- 2026-10-22 09:5x: started; read the specs and I4a's code; audit below; test copy and own harness copies
  (`test/i4c/`).
- 10:0x: coordinator addition (from I6m): "SoF pictures come out as flat colour in the `ref_gl3` front end". Checked
  with the current tree (below, *ref_gl3 front end*).
- 10:xx–14:45: all open items of the audit implemented (see *Implementation*); terrain verified against the oracle.
  Interrupted at 14:45 (journal not updated then; reconstructed from the tree and the captures).
  Note: the coordinator's I4b commit `1fd81f5` already contains this package's work of that time
  (`gl3_terrain.c`, the I4c parts of `gl3_detail.c`, `gl3_fog.c`, `gl3_lightmap.c`, `gl3_shaders.c`, `gl3_surf.c`,
  `local.h`, `sof.h`, `gl3_sof2d.c`); the later parts (`gl3_avi.c`, `gl3_liquid.c`, hooks) are uncommitted.
- 2026-10-25 (resumed after a reboot): reviewed the existing code against the specs, `build-I4c-x86` builds as is
  (`ref_gl3`, `ref_gles3`, `ref_omgl`, no work to do). Next: black-box views of the remaining features.
- 2026-10-25: harness extended (view override in the capture shim, `faceview.py`, `gen_views.py`, `movecmp.py`,
  `envprobe.py`, `run_pair.ps1`, `build_ref.cmd`; see *Testing*). Flowing surfaces, inline-model detail, liquids
  verified. Two fixes from the comparisons: the second pass of liquids with an environment map (the original never
  shows the environment map, black-box rule below), and the fog overlay of translucent world faces drawn right after
  the face (was in the first fog pass, under the face: kos1 water 2.1 → 0.5).
- Coordinator 2026-10-25: SPEC-AMEND-005 approved; items I4a.1–I4a.8 and I4b.1–I4b.5 added to this package (after
  the I4c features).
- 2026-10-25: `gl_fogmode 1`, `gl_monolightmap`, `avi`, slot 0x54 flips, `tp` verified; `avi record` without a name
  fixed. SPEC-AMEND-005 I4a/I4b items done (section below). Full build of all targets passes. Next: the smaller audit
  items (lightmap coordinates for `lm_scale` ≠ 1, dynamic lights on lightmaps, diagnostics, `r_speeds` fields).
- 2026-10-25: smaller audit items: lightmap coordinates of scale-2 faces, SoF dynamic lights per lightmap sample
  (`gl3_dynlight.c`), `gl_dynamic 0`, `gl_showtris` (none on SoF maps, as observed), `r_speeds` `fp`; found on the
  way: the outline pass bypassed the program cache (`glUseProgram`), which left the following fog/effect batches on
  the wrong program when `gl_showtris` was set. Coordinator rule "Network ports" applied to both harnesses (server
  29410, client 29411); `run_sof.ps1` now restores `user\config.cfg` from `openmerc-data\I4c\config.base.cfg` before
  every run (a run's `+set` of an archived cvar is written back at quit: one run left `gl_modulate 3` in the copy;
  the two runs it affected were repeated). Paused spawn views of 8 maps after all changes: 0.0–0.7 (`spom2`).

- 2026-10-25 end of this session: the coordinator's I1c snapshot `a584427` contains the I4c work up to the liquid
  rule; uncommitted since then: `gl3_dynlight.c` (new), changes in `gl3_image.c`, `gl3_light.c`, `gl3_lightmap.c`,
  `gl3_main.c`, `gl3_shaders.c`, `gl3_surf.c`, `header/sof.h`, `engine/CMakeLists.txt`, `test/i4c/*`. Full build of
  all targets (incl. `ref_gles3`) passes; the OpenMerc engine runs tsr1 and the menus through `ref_gl3`
  (`rg3map_tsr1.png`, `rg3_*.png`). Open: the items in the last row of *Implementation*, OPEN_QUESTIONS I4c.1–4.

- 2026-10-25, after the commit `20aaca8`: SPEC-AMEND-006 I4c.1–I4c.4 applied (section *SPEC-AMEND-006* below).

## Audit of the module against SPEC-002/410/412/413 (written before the work)
Legend: **open** = missing or different from the spec, *ok* = present.

SPEC-410 World
- **Terrain sets** (whole section, SPEC-144 layout): only the selection messages of slot 0x38 exist.
- **Flowing surfaces**: lightmapped faces use Quake II's 1-D scroll (`-64 * frac(time / 40)` along s); SPEC-410 wants
  both 16-bit speeds of the texinfo value (s and t, 0.01 texture per unit per second of game clock). The alpha-surface
  path and the detail pass already use the SoF formula.
- **Detail/environment pass on inline brush models**: not queued (world only).
- **`gl_fogmode 1`** (OpenGL linear fog on interior region faces, overlay only on boundary faces): treated like 0.
- **`gl_monolightmap` ≠ 0** (`L`/`I`/`C`/`A`/other texel and upload formats): always the colour-table path.
- **Liquids**: the Quake II warp shader is used: client time instead of the game clock, a constant brightness 0.5
  instead of the light sampled straight down at every vertex, the Quake II flowing scroll on warp faces (SoF: no
  scroll term), texture divisor = texture size instead of 64, no wave displacement/colour/alpha for texinfo 0x2000,
  no environment-map second pass, no halving of the warp amplitude on every renderer start (SPEC-413 §1).
- Alpha surfaces: the fog overlay of a translucent world face is drawn in the second fog pass, not right after the
  face (no visible difference expected; *ok* otherwise).
- Lightmap texture coordinates for `lm_scale` ≠ 1 are not floored (`floor((s - m) / (16 k))`); dynamic lights on
  lightmaps use Yamagi's per-pixel formula instead of the Quake II per-sample rule (r − dist, cut-off 64).
- Diagnostics not implemented: `gl_drawflat`, `gl_drawmode`, `gl_showtris` 2–4, `gl_dynamic 0`; `gl_lightmap` and
  `gl_saturatelighting` only affect the two-pass path (never used: multitexture is always available, so *ok*).
- **A3D geometry export** (slot 0xd8): not implemented (table entry NULL).
- *ok*: classification, alpha images without the alpha flag, texture animation, sky, fog overlay/shimmer/far wall,
  point fog, light sampling, model lights, damaged surfaces, inline-model alpha faces.

SPEC-412
- **`avi` command** (§13): not registered.
- Slot 0x54 rotations 4–7: I4a used `rotation & 3`; SPEC-802 (approved, "Picture flips") defines 4–7 as flips.
- *ok*: 2D primitives, clipping, fonts, world labels, alpha quads, chains, sorted pass, overlays, screenshots,
  rawshot, thumbnails.

SPEC-413
- `r_speeds 1`: `fp`, `gt`, `tp` printed as 0, `lm` counts nothing (terrain `tp` belongs to this package).
- *ok*: cvar table, start-up, images, colour table, per-frame order (two-pass world path not needed, see above).

SPEC-002: slot 0x38 (terrain) and 0xd8 (A3D) as above; *ok* otherwise.

## Implementation (where each audit item lives)
| Item | Code | State |
|---|---|---|
| Terrain sets (slot 0x38, `.trn`, rows, clouds, fog/clears, `tp`) | `gl3_terrain.c` (new), drawn from `GL3_DrawWorld` after the sky box | verified (below) |
| Flowing surfaces: s and t speeds of the texinfo value × game clock × 0.01 | `gl3_surf.c` `GL3_DrawGLFlowingPoly`, uniform `scrollT` (`local.h`, `gl3_shaders.c`, `gl3_main.c`) | verified |
| Detail/environment pass on inline brush models (model-space vertices) | `gl3_detail.c`, queued in `gl3_surf.c` `DrawInlineBModel`, pass after the model's 0x800 faces | verified |
| Fog overlay of translucent world faces in the first pass and again right after the face | `gl3_fog.c` `GL3_Fog_DrawSurfaceNow` (+ `DrawQueued`), called from `gl3_fx.c` `DrawAlphaSurface`; walk queue in `gl3_surf.c` | verified (kos1 0.0, irq3b `fp` 659) |
| `gl_fogmode 1`: linear fog on interior region faces while drawn, overlay on boundary faces only | `gl3_fog.c` (`GL3_Fog_HardwareRegion`, `…SetHardwareFog`), `DCFlag_UseFog`, lightmapped shaders, effect batch for translucent faces | verified |
| `gl_monolightmap` `0`/`L`/`I`/`C`/`A`/other, latched when the lightmaps are built | `gl3_lightmap.c`, `GL3_SoF_ApplyMonoLightmap` + `omMono` in the lightmapped shaders | verified (difference: the original also rebuilds dynamically lit/changing-style faces with the current value; OpenMerc keeps the value of the map load for all faces) |
| Liquids: SoF warp (game clock, divisor 64, no scroll), light sampled straight down per vertex, waves of 0x2000, second pass for textures with an environment map (SPEC-AMEND-006 I4c.1), warp table halved at every start | `gl3_liquid.c` (new), called from `RenderBrushPoly` (opaque) and the sorted pass (0x800) | verified (stock `iraq/water` too since SPEC-AMEND-006) |
| `avi` command (record/pause/stop, 10 frames/s, codec dialog; test cvar `r_om_avinodialog 1` = uncompressed without dialog) | `gl3_avi.c` (new), capture in `GL3_EndFrame` before the swap | verified (messages vs original; recording in OpenMerc only) |
| Slot 0x54 rotations 4–7 = SPEC-802 flips | `gl3_sof2d.c` | verified |
| `r_speeds 1` `tp` | `gl3_terrain.c` / `gl3_main.c` | verified (below) |
| Lightmap coordinates floored for `lm_scale` ≠ 1 | `gl3_lightmap.c` | verified (scale-2 faces: ger1 0.3 → 0.1, irq3b 1.0 → 0.4) |
| Dynamic lights on lightmaps by the per-sample rule (SoF constants, not × `gl_modulate`), marking without side test | `gl3_dynlight.c` (new): per-page float layer on texture unit 6, added in the lightmapped shaders; `gl3_light.c`, `gl3_surf.c`, `gl3_image.c` hooks | verified (tsr1 police-car lights: 1.04 → 0.06 in the lit area) |
| `gl_dynamic 0` (no dynamic lights, styles as at load) | `gl3_surf.c`, `gl3_main.c`, `gl3_dynlight.c` | partly: the original then also shows stale blocks of its per-frame page (not reproduced) |
| `gl_showtris` | `gl3_surf.c` | SPEC-AMEND-006 I4c.3: only in two-pass frames (`r_fullbright`, night vision), chained lightmapped world faces; verified 0.0 |
| `r_speeds 1` `wp`, `fp`, `qp` | `gl3_fog.c`, `gl3_fx.c`, `gl3_surf.c`, `gl3_main.c` | SPEC-AMEND-006 I4c.4: irq3b `352:wp 659:fp` equal to the original |
| Not done: A3D geometry export (slot 0xd8: stays null, so the engine does not hand the table to A3Dsnd.dll, SPEC-002 loading step 10; only that library with Aureal hardware uses it), `gl_drawflat` (the original fills faces with flat colours, `drawmode.cfg`; the colour "derived from its identity" is not specified), `gl_drawmode` (original: see-through world), the lightmap grid of `gl_showtris` 3/4, `gt`/`gp`/`lm` of `r_speeds 1` (Ghoul statistics) | | open |

## Results (black box, `<openmerc-data>\shots\I4c\`)
Harness `test/i4c/run_sof.ps1` (I4c copy of I4a's: original SoF.exe of `D:\Games\SoF-test-I4c` on a hidden desktop,
renderer behind the capture shim); scripts in `<openmerc-data>\I4c\scripts\`; pairs compared with
`test/i4c/compare2.py` (mean absolute difference per channel 0–255, share of pixels > 24).

| View | Pair | Result | Notes |
|---|---|---|---|
| trn1 spawn, terrain set on | `pgl_trn1a` / `pom_trn1a` | 1.0 / 1.1 / 1.1, 0.4 % | landscape is random (flrand): hills differ in detail, layout, tint, fade and clouds match |
| trn1 after `terrain ""` | `pgl_trn1off` / `pom_trn1off` | 2.2 / 2.4 / 2.4 | landscape gone in both, nothing printed |
| trn1 view to the side, paused (`tp1`–`tp3`) | `tgl_*` / `tom_*` | 0.5–2.3 | terrain stops while paused in both |
| `terrain badver` (byte 0 = `#`), `terrain train`, `terrain inactive` (byte 1 = 0) | `vgl_*` / `vom_*` | 1.0–3.4 | same messages (`Invalid version for Terrain Set badver`); inactive set: no landscape |
| `r_speeds 1` in trn1 | logs `tgl`/`tom` | `tp` 483 / 483, then 478 / 476 | other fields: `wp` equal (69); `gp`/`gt` 0 in OpenMerc (Ghoul statistics, not in this package) |

Views at a spot of a map (shim view override, Ghoul/weapon entities left out, `+set fixedtime 30`, paused; scripts
from `gen_views.py`). Each view at two game moments 0.3 s apart (`_0`, `_1`). `movecmp.py` compares only the pixels
that move between the two original captures ("step" = the original's own change, so a renderer that ignores the
motion scores about "step"):

| View (prefix `f`/`l`/`d` gl / om) | Faces | Whole frame | Moving pixels: step → OpenMerc |
|---|---|---|---|
| sud2 conveyor (world, value 0xc8: t 2.0/s) | 2613 | 0.3 / 0.3 / 0.2 | 14.7 → 0.3 |
| sud3 conveyor (value 0xffff0064: s −0.01/s, t 1.0/s) | 16303 | 0.3 / 0.2 / 0.1 | 20.7 → 0.1 |
| sud3 conveyor side (value 0xa0) | 9099 | 0.1 | 21.9 → 0.1 |
| kos1 inline model 84 `bosnia/water4b` (value 0x40) | 17957 | 0.3 / 0.2 / 0.1 | 38.8 → 0.2 |
| ger1 inline model 30 `castle/3_machine_7` / `3_machines_1` (detail `d_metalsmooth`) | 15602, 15629 | 0.1–0.2 | detail scroll too small to mask |
| arm1 door inline model 15 (`armory/door1`, environment map `env1a`, planar) | 2662 | 0.2 | — |
| ger1 `castle/water` (opaque liquid), `gl_modulate 3`, fog off | 6255 | 0.0 | 1.2 → 0.0 (threshold 0) |
| sud2 `uganda/2_blood1` 0x2008 (waves) | 9200 | 0.2 / 0.0 / 0.0 | 1.4 → 0.1 (threshold 0) |
| kos1 `bosnia/water4` 0x809 (translucent liquid, in fog) | 1990 | 0.5 / 0.4 / 0.1 (was 2.1 / 1.5 / 0.1) | |
| kos2 `sprites/gb_water2` 0x2818 (translucent, waves) | 7252 | 0.4 / 0.3 / 0.1 | 1.0 → 0.6 |
| irq2a / irq2b `iraq/water` (environment map `Iraq/env_irq`) | 1320, 5268 | 6.3 / 7.3 | see below |

Not shown by these views: kos1 inline models 17/19/26/27/114 (`floodfirst` etc. are not in the frame at the level
start), irq3b inline oil (model 53 is elsewhere at run time); the sud3 eye of the first run was inside a solid leaf
and showed a far fog wall of a different region than the original's (`cmpf/cmp_f_sud3side_0.png`, 13.5): the
original's choice of the wall region from a solid view leaf is not known (OPEN_QUESTIONS I4c.2).

`gl_fogmode` 0 / 1 / 2 at the same paused moment (`fogmode.cfg` spawn views, `fogviews.cfg` views inside and
outside fog regions; prefixes `fm`/`fv`, gl / om). The original's own change between the modes shows the view is
sensitive:

| View | orig m0→m1 / m0→m2 | OpenMerc vs original m0 / m1 / m2 |
|---|---|---|
| irq3b spawn | 4.4 / 7.7 | 0.1 / 0.2 / 0.2 |
| ger1 regions 14, 15 (height fog), eye inside | 2.6, 2.4 / 3.9, 3.4 | ≤ 0.2 in all modes |
| nyc2 region 11 seen from outside / region 14 inside | 4.8, 0.3 / 5.0, 1.8 | ≤ 0.5 |
| sud3 region 4, eye inside | 6.7 / 10.1 | 0.1 / 0.2 / 0.2 |
| tsr1 region 3 (two views) | 1.0, 0.5 / 2.2, 1.1 | ≤ 0.4 |
| ger1, nyc2, tsr1 spawns | ≤ 0.3 (little fog in view) | 0.0–1.1 |

`gl_monolightmap` `0`, `L`, `I`, `C`, `A`, `x` (ger1 spawn, the map loaded again for every value with `flushmap 1`
— with `flushmap 0` the same map keeps its world and lightmaps, in both renderers; prefixes `mm`/`mb`): original's
change against `0` with `gl_modulate 1`: L 3.0, I 0.7, C 1.5, A 3.0, x 3.0; with `gl_modulate 3`: 5.5 / 1.6 / 3.6 /
5.1 / 5.5. OpenMerc vs original: 0.0 in every mode (0.0–0.1 with `gl_modulate 3`). (`A` and other letters look like
`L` in the original too.)

`avi` (`avi.cfg`, logs `avgl.log` / `avom.log`): usage line for no and unknown sub-commands, `AVI is not recording`
for `avi pause`, nothing for `avi stop` without a recording — identical. `avi record` without a name: the original
prints the usage line (SPEC-412 §13 says it only stops a recording) — fixed (stop, then the usage line). The original
then opened its codec dialog (no input on the hidden desktop: killed by the harness after 300 s, as expected).
OpenMerc with the test cvar `r_om_avinodialog 1`: 0.9 s of real time recorded 9 frames (`Record 9 frames`), pause
printed `AVI Recording Paused`, `avi record` of a second file stopped the first; the file has 10 frames/s, 640 × 480,
24-bit uncompressed, 921 600 bytes per frame; its first frame is the presented picture with console text (checked).

Slot 0x54 flips (`flips.cfg`, video menu: `pics/menus/misc/cap` rotation 5 and `misc/vend` rotation 6 per the
call trace): whole menu 0.00 (`flgl_video` / `flom_video`); the same crops mirrored the other way would differ by
48.7 and 30.6.

`r_speeds 1` after `terrain (null)` (vectors.cfg, `vom2.log`): `tp` keeps the last drawn count (476; original 493),
as in the original. Seen on the way: `wp` 317 vs 346 and 23 vs 25 in the same views (not investigated; `gp`/`gt` are
Ghoul statistics, 0 in OpenMerc).

### Liquids with an environment map (black box)
SPEC-410: "If the base image's second-pass texture is an environment map (type 8), the polygons are drawn again with
blend (destination colour, source colour), sphere-mapped texture coordinates …". Probe textures written as loose
files into the test copy's `user\textures` (SPEC-300: they override the paks; `test/i4c/envprobe.py`, removed after
each run) show what the original does:
1. Environment map encoding its own coordinates (R = 100 s, G = 100 t, B = 100), base texture white: the water stays
   plain grey (128, 128, 128) in irq2a and sud2 — no trace of the environment map (B would be 100).
2. Environment map flat (50, 50, 50), base texture a coordinate gradient (alpha 255), on `uganda/2_blood1` with its
   `.m32` second-pass name pointed at the environment map: the base texture appears, magnified, at sphere-map
   coordinates of the face normal (decoded s 0.40 … 0.61 across the view straight down), colour constant over the
   pool (no light, no wave colour), independent of `gl_modulate`. OpenMerc with the rule below: 0.0 / 0.0
   (`hgl1_*` / `homF_*`), at both pools.
3. Same with the base alpha 170 (like the stock `iraq/water`): the warped first pass shows through.
Rule implemented: second pass = the base texture, sphere-mapped (face normal), colour 1, blend (source alpha, one
minus source alpha), depth writes off; the environment map is never drawn on liquids. Exact for opaque textures
(probe 2). The stock `iraq/water` (alpha 170, the only stock liquid with an environment map: irq1a, irq2a, irq2b)
still differed: 6.3 (irq2a) / 7.3 (irq2b) mean, the water lighter than the original; the old SPEC-410 pass gave
3.5 by accident (dark, with black blotches of the environment map that the original never shows). Fitting a second-
pass colour of 0.5 gives 2.5 but contradicts probe 2, so it was not adopted. OPEN_QUESTIONS I4c.1 — answered by
SPEC-AMEND-006 I4c.1 (blend factors (destination colour, source colour), enable inherited); now 0.0 / 0.1.

## SPEC-AMEND-006 (answers to OPEN_QUESTIONS I4c.1–4)
| Item | Change | Re-check (original vs OpenMerc, mean of 0–255) |
|---|---|---|
| I4c.1 liquid second pass | `gl3_liquid.c`: base texture still bound, colour 1, sphere map of the stored (unflipped) plane normal, blend factors (destination colour, source colour) with the blend enable inherited: opaque pass = replace, sorted pass (0x800) = 2 × texture × framebuffer | stock `iraq/water`: irq2a 6.3 → 0.0, irq2b 7.3 → 0.1, close views (`envliq.cfg`, `gl_modulate 3`) 3–6 → 0.0–0.8. Probes (`envprobe6.cfg`, loose textures, removed after): grey 128 / black / white base on irq2a `iraq/water` and on sud2 `uganda/2_blood1` (linked to `Iraq/env_irq`): 0.0–0.7, except one irq2a view (2.0 grey, 3.8 white, 0.0 black): the first pass's vertex light there differs (the light cache's 32-unit cells take the value of the first query of the frame history; slot 0xa4 itself gives identical values at those points, `light2.cfg`) |
| I4c.2 far wall order | the walk queues boundary and sky faces; the other region faces are queued when the texture chains are drawn (`gl3_surf.c`); cap 1024, strict minimum (first wins) and the plane reduction were already so | sud3 eye in a solid leaf (1640 272 −552 and the second sud3 view): 0.0 (was 13.5: region 4/6/15's colour instead of region 5's) |
| I4c.3 `gl_showtris` | outlines on SoF maps only in frames drawn by the two-pass path (`r_fullbright` ≠ 0 or night vision), only the lightmapped world faces of the texture chains | ger1 `r_fullbright 1`, `gl_showtris` 1 / 2: the original draws outlines, OpenMerc 0.0 against them; `r_fullbright 0`: none in both (0.0) |
| I4c.4 `r_speeds 1` | `wp` also counts world and inline-model alpha faces; `qp` = quads passing the collection test, whose leaf test now reads the visit mark of the leaf's first listed surface (`gl3_fx.c`); `fp` counts every region polygon before its vertices and restarts at the first overlay pass. Found with `fp`: the original draws the overlay of a translucent world face twice, in the first pass (queued by the walk, classification step 3) and again right after the face; OpenMerc does both now | irq3b paused spawn: original `352:wp 659:fp`, OpenMerc `352:wp 659:fp` (was 346 / 619); tsr1 `548:wp` both (was 530). `qp`: in the same paused frame the original prints 15 with 15 submitted quads (leaf −1, nyc2) and 0 with none (tsr1); the OpenMerc runs had no quads submitted in those frames (effects differ between runs), 0. kos1 water in fog: 0.4 → 0.0 with the double overlay |

Regression after the changes (`cmpF`): fog-region views 0.0 (21 pairs), paused spawns of 8 maps 0.2, flowing 0.0 (16),
liquids 0.0 (14), fog modes 0.4 (12). Full build of all targets passes.

## SPEC-AMEND-005 items of the finished renderer packages (I4a.1–8, I4b.1–5; coordinator 2026-10-25)
| Item | Result |
|---|---|
| I4a.1 2D projection near +9999 / far −9999 | already so (I4a's menu-model ortho); main menu 0.5 vs original (globe phase, I4a measured 0.4 between two original runs) |
| I4a.2 depth writes after Begin frame | changed: the 2D state (`GL3_SoF2D_Enter2D`: Begin frame, end of RenderFrame) no longer turns depth writes off, only slot 0xb4 does; Begin frame's clear leaves them on. Paused spawn views of 8 maps after the change: 0.0–1.1 (`spgl`/`spom`), video menu 0.00 |
| I4a.3 slot 0x54 values 4–7 = flips | done before (verified above, video menu 0.00) |
| I4a.4 direction table 250 entries | changed (`gl3_sof.c`: 250, was 162). Shim light probe (`om_caplight`, `light.cfg`): slot 0xa4 at 5 points × directions 0–249 and 255 — all 1255 samples equal the original's (`lpgl.log`/`lpom.log`); with the old 162 limit 321 of them would differ |
| I4a.5 char argument low 8 bits | already so |
| I4a.6, I4a.7 | no change (I4a.7: terrain and `avi` are done in this package) |
| I4a.8 lone quads keep their flags | already so |
| I4b.1 low 16 bits of `frame` | already so (`(short)(e->frame & 0xFFFF)`) |
| I4b.2 engine light count unchanged | already so (I4b's copy of the record) |
| I4b.3 who sets 0x1000 / bit 28 | information only; the renderer reads the flags as given |
| I4b.4 probe direction in model axes | Ghoul system side (I3's library); the renderer formula stays; not changed here |
| I4b.5 GL linear attenuation 1/R | changed in `engine/src/ghoul/ghoul_draw.cpp` (was 0.75/R). New check in `omghoul_test` (`tests/test_render.cpp`, soft GL records `Lightf(GL_LINEAR_ATTENUATION)`): Dekker drawn with hardware lights and attenuation under the test's world lights of radius 300 and 400 gives 1/300 and 1/400 (old code: 0.0025 and 0.001875); `omghoul_test --quick`: 1226 checks, 0 failures |

## ref_gl3 front end (coordinator addition from I6m)
- Harness: `test/i4c/run_om.ps1` (I4c copy of I6m's `run_om.ps1`: OpenMerc client, `-portable`, `-basedir
  D:\Games\SoF-test-I4c`, home `openmerc-data\I4c\home*`, hidden desktop; needs `-Extra "+set snd_dll OMsnd.dll"`,
  with `defsnd.dll` the client stalls at sound start in this copy). Run with `powershell.exe` (Windows PowerShell
  5.1; `pwsh` cannot compile `DeskRun.cs`).
- With the current tree (I4a's final commit), the 28 menu pages of I6m's tour drawn through `ref_gl3` with
  `om_menusoft 0` show correct pictures (`openmerc-data\shots\I4c\h6_*.png`, I6m's home config and tour script;
  why that run's start was not affected by the archived `vid_gamma "1"` was not investigated):
  in the strip x ≥ 632, which only shows the tiled backdrop in the original's captures, the difference to I6m's
  oracle captures is **0.0** (all pages), the `disclaimer` page is identical (0.0); I6m's software path differs there
  by 10 (it does not apply the SPEC-413 colour table: the test config has `vid_brightness 0.6`, `vid_contrast 0.6`).
  The remaining page differences are the Ghoul menu models (not drawn outside I4b) and slot 0x54 flips (fixed).
- Re-checked 2026-10-25 with the current tree (`rg3_*.png`, `menus_hw2.cfg`, home `openmerc-data\I4c\home7`): the
  main, video, game and join pages through `ref_gl3` (`om_menusoft 0`) show the pictures correctly (tiled backdrop,
  buttons, frame), and the Ghoul menu models are drawn now (I1c). No renderer change was needed for this item.
- Cause of I6m's white pictures (`openmerc-data\shots\I6m\omre_*.png`): **`vid_gamma 1`**, not the 2D code. I6m's
  `run_om.ps1` puts `set vid_gamma 1` (the Yamagi meaning, 1 = neutral) in front of every script, and the value is
  archived in its home's `config.cfg`. SPEC-413 §9's `vid_gamma` is an exponent offset (default 0.0, neutral); with 1
  the colour table maps every value ≥ 1 to 255, so every picture loaded after the table was built is white (the
  original behaves the same with `vid_gamma 1`). Reproduced: `set vid_gamma 1; vid_restart` → main menu mean
  brightness 196.6 (white, `gr1_main.png`); `set vid_gamma 0; vid_restart` → 18.9, correct (`gr0_main.png`). A
  fresh home starts with `"vid_gamma" is "0.0"` and correct pictures (`fr_main.png`). The I4c copy of the harness no
  longer sets `vid_gamma`. For I6m: drop `set vid_gamma 1` from `run_om.ps1` and reset `vid_gamma` to 0 in
  `openmerc-data\I6m\home\user\config.cfg`; then `om_menusoft 0` can become the default (the renderer applies the
  SPEC-413 colour table, which the software path does not).

## Testing (black box)
- Build: `tools\build\build-msvc.cmd x86 Release` with `OM_BUILD_DIR=...\build-I4c-x86` (all targets); while another
  package's half-finished engine edit breaks that build, `test\i4c\build_ref.cmd` rebuilds only `ref_omgl`,
  `ref_gl3`, `ref_gles3` of the configured tree. Capture shim: `test\refcap\build.ps1 -Out build-I4c-x86\refcap`.
- `test/i4c/run_sof.ps1 -Script <cfg> -Ref gl|omgl -Tag <t> [-Extra ...]` (original SoF.exe of the test copy on a
  hidden desktop, renderer behind the shim), `run_pair.ps1 -Script <cfg> -Tag <t>` (both renderers, tags `<t>gl` /
  `<t>om`, `+set fixedtime 30`). Keep `-Extra` short: SoF.exe hangs before opening its log with a long command line
  (about 330 characters); put further cvars into the script.
- View override (shim, `om_capview "x y z pitch yaw roll [noghoul]"`): the frame is drawn from that eye with all
  areas open, Ghoul/weapon/viewer entities optionally left out; the engine never sees it. Eyes: `faceview.py
  <bsp> <face> [dist [lift] | @dx,dy,dz | auto]` (auto = first eye with a clear line of sight); faces:
  `bspscan.py <maps dir> flow|waves|warp|fog|detail-inline|env-warp` (maps extracted to
  `openmerc-data\I4c\maps`). Inline models with an `origin` key need the entity origin added.
- Scripts: `gen_views.py <dir>` writes `flow.cfg`, `liquid.cfg`, `detail.cfg`, `fogmode.cfg`, `mono.cfg` (into
  `openmerc-data\I4c\scripts`, with `fogviews.cfg`, `envliq.cfg`, `envprobe*.cfg`, `avi.cfg`, `flips.cfg`,
  `terrain.cfg`, `vectors.cfg`, `probe.cfg`, `tpcount.cfg`). Dark maps: `+set gl_modulate 3` (latched) and
  `gl_fogmode 2` make liquids and flowing faces visible; scripts reset these archived cvars before `quit`.
- Comparison: `compare2.py`, `movecmp.py [-t thr]` (moving pixels only, two moments). Probe textures:
  `envprobe.py install|remove|decode` (loose files in the test copy's `user\textures`, always removed after use).
