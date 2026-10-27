# I4a — Renderer: 2D/fonts, sky, fog, detail, effect primitives, cvars, SoF module ABI (work log)

Package: SPEC-002 (renderer module interface), SPEC-410 (sky/fog/detail parts), SPEC-412 (2D, fonts, effects
drawing), SPEC-413 (renderer cvars), SPEC-AMEND-002; formats SPEC-132 (`.fnt`), SPEC-100 (`.m32`).
Directory: `engine/src/client/refresh/gl3/` (world rendering there is package I0, Ghoul model drawing is I4b).

## State
- 2026-10-22 01:1x: started (second attempt after a power loss). Test copy `D:\Games\SoF-test-I4a` (paks
  hard-linked from the oracle, executables/DLLs copied). Black-box harness first (see *Testing*).
- 05:1x: first full pass building (`ref_gl3.dll`, `ref_omgl.dll`); `ref_omgl.dll` runs inside the original SoF.exe.
- 05:xx–07:xx: Ghoul menu models (fixed-function GL table, 2D ortho with reversed depth) → main menu pixel-identical;
  depth clear fix; fog on inline brush models; capture of the last 2D batch.
- 09:2x–10:xx (after the second interruption): integration with I4b (Ghoul in the 3D view); fixed 2D vanishing after
  Ghoul entities; quads drawn alone no longer get the drawn mark; lightmaps through the SPEC-413 colour table;
  `r_fullbright` / night vision for the world; `gl_modulate` on static light.
- Coordinator report: `ref_gles3` failed (`GL_POLYGON_MODE` in the debug dump of `gl3_sof2d.c`); guarded for GLES.
  `ref_gl3`, `ref_omgl` and `ref_gles3` all build in `build-I4a-x86`.
- `-portable` check (coordinator note about runs writing into `Documents\YamagiQ2` at 04:28/05:13): I4a never
  started the OpenMerc engine. Its only launcher, `test/run_sof.ps1`, starts the original `SoF.exe` of
  `D:\Games\SoF-test-I4a`, which does not use that directory; the renderer has no home-directory code (checked
  with grep and in the session log). If an I4a script ever starts the OpenMerc engine, it passes `-portable` with
  a home under `<openmerc-data>\`.

## Results (black box, `<openmerc-data>\shots\I4a\`)
Same SoF.exe 1.07f, data, config and script with the original `ref_gl.dll` and with `ref_omgl.dll`; frames captured
at EndFrame by the shim. `cmp_<name>.png` = original | OpenMerc | 4× amplified difference. Values: mean absolute
difference per channel (0–255), share of pixels differing by more than 24.

| View | Pair | Result | Notes |
|---|---|---|---|
| main menu (Ghoul frame, globe, buttons, fonts) | `orig_basic2_mainmenu` / `om_basic_mainmenu` | 0.0, 0.0 % | identical; two oracle runs differ by 0.4 (globe rotation phase) |
| video menu | `orig_basic_video` / `om_basic_video` | 0.2 | |
| ger1 spawn | `orig_maps_ger1` / `om_maps_ger1` | 0.1 / 0.2 / 0.2 | was 1.5 before the lightmap colour table |
| irq3b (fog regions, inline models in fog) | `…_irq3b` | 0.4 / 0.5 / 0.5 | |
| sib1 | `…_sib1` | 0.5 / 0.4 / 0.5 | |
| trn1 | `…_trn1` | 0.5 / 0.7 / 0.7 | no terrain set in this view |
| jpn1 | `…_jpn1` | 1.0 / 1.1 / 1.1 | |
| kos3, same view | `k_gl_kos3` / `k_om2_kos3` | 0.6 / 0.1 / 0.1 | was 3.5 / 2.3 / 1.8 (lightmaps without colour table) |
| kos3 `r_fullbright 1` | `kf_gl_kos3` / `kf_om1_kos3` | means 64.7/45.9/34.8 vs 64.5/46.0/34.9 | yaw differs per run (spawn looks straight down) |
| tsr1 | `…_tsr1` | 5.5 / 4.3 / 3.5 | view moves slightly between runs; region means equal ±0.1 |
| nyc2 (explosion, Ghoul characters, letterbox) | `t_gl_nyc2` / `t_omgl_nyc2` (+ `.trace.txt`) | 7.3 / 4.2 / 1.6 | random particle placement (traces: 53 vs 46 quads, different spots); 2D now correct |

Remaining visible differences are explained by timing or randomness (traces show the engine submitted different
views or particles), except the Ghoul head skin on nyc2 (below, I4b).

## Design
One renderer core (`gl3_*.c`), two front ends:
- `ref_gl3.dll` / `ref_gles3.dll` (Yamagi `GetRefAPI`, used by the OpenMerc engine). Additionally exports
  `OM_GetSoFRefExports()` (refapi.h: `OM_GetSoFRefExports_t`, name `OM_SOFREF_EXPORTS_NAME`), returning the SPEC-002
  export table bound to the running renderer.
- `ref_omgl.dll` (x86 only, target `ref_omgl`, `OM_SOFREF=1`): SPEC-002 `GetRefAPI` with the 108-byte import and
  220-byte export table, own Win32 window/WGL context (`gl3_win32.c`, window class `SoF`, compatibility profile so the
  Ghoul system's GL 1.1 table works), so the original `SoF.exe` loads it with `vid_ref omgl`. Renaming it to
  `ref_gl.dll` replaces the original.
- SPEC-002 layouts: `engine/src/common/header/sof/refapi.h` (owner I4a), with 32-bit size checks.
- 2D (`gl3_sof2d.c`): the original's fixed-function 2D state (blend, texture mode, colour, texturing left behind
  between calls, SPEC-412 §1) is emulated as virtual state; vertices carry texmode/alpha test into shader `siSoF2D`.
- Effects (`gl3_fx.c`): alpha quads, chains, sorted translucent pass, overlays; one batch shader `siSoFfx` with
  linear fog. Fog regions `gl3_fog.c` (world and inline models), detail/environment pass `gl3_detail.c`, cvars
  `gl3_sofcvar.c`, images with SoF semantics `gl3_image.c` (types, colour table, alt/damage/detail images).
- Lightmaps (SPEC-410 *Building*): Yamagi keeps one lightmap per style and sums them in the shader. The lightmapped
  fragment shaders now round the sum (after dynamic lights), scale it down when the brightest channel exceeds 255
  and look it up in the SPEC-413 colour table (256×1 texture on GL_TEXTURE5, `GL3_SoF_BindLightTable`, once per 3D
  view). An all-255 table gives `r_fullbright` and night vision (rdflags 4). Static styles are multiplied by the
  latched `gl_modulate`. Difference to the original: the table is applied after bilinear filtering, not per texel.

## Entry points for other packages (I5 client effects, I6m menus, I4b Ghoul)
All *done*.
- From the OpenMerc engine: `GetProcAddress(ref, OM_SOFREF_EXPORTS_NAME)` → `const sofrefexport_t *`; call the slots
  with exactly the SPEC-002 prototypes. In-process (SoF.exe) the same table comes from `GetRefAPI`.
- 2D: slots 0x44–0x9c, 0xa0, 0xac, 0xb4 (Set2D), 0xb8 (Set3DInMenu), 0xc8–0xd4; colours by value or pointer as in
  SPEC-002. Rotation argument of 0x54: values & 3 (4 and 6 occur, see OPEN_QUESTIONS). Character of 0x6c: low byte.
  Clipping: 0x8c–0x9c. World labels: 0xa0. Thumbnail: `ReadThumbnail` (also flushes pending 2D).
- Fonts: slots 0xc8/0xcc/0xd0 take `const om_fontref_t *` (MSVC 6 `std::string`, only the character pointer at +4
  is read). Names `small`, `medium`, `title`, `interface` (case-sensitive). Colour codes 1–31 per SPEC-412.
- Menu Ghoul models: draw them between `Set3DInMenu(0)` and `Set2D(0)`: 2D ortho, depth from +9999 (near) to −9999,
  culling on, fixed-function state as the original leaves it.
- Effects: fill `sofalphaquad_t` / `sofquadchain_t` (refapi.h) and pass pointer arrays in `sofrefdef_t`
  (0x70–0x7c) to RenderFrame (slot 0x40). The renderer writes back the image cache (+0x30), fog cache (+0x3c), the
  drawn mark 0x1000 **only on chain members**, the dropped flag 0x0001 (+ collapsed size) and the chain weight
  (+0x1c). Light at a point: slot 0xa4 (direction byte 255 = straight down; table of import 0x68, 162 entries
  assumed).
- Inside the renderer (I4b): `GL3_FX_DefaultState/SetState/Add` (batched effect geometry with fog),
  `GL3_Fog_RegionLinear`, `GL3_Ghoul_EnterFixedFunction(blend, modulate, colour)` /
  `GL3_Ghoul_LeaveFixedFunction()` around any GL 1.1 drawing. Leave turns the compatibility-profile alpha test off
  (it also applies to shader output) and restores depth range, depth func, blend func, buffers and uniform blocks;
  Enter turns alpha test on again (GREATER 0.05). Lighting and other fixed-function-only state are left alone.

## Interplay with I4b (keep)
- Hooks: `gl3_fx.c` DrawGhoulEntity → `GL3_Ghoul_DrawEntity` (`/* I4b */`), `gl3_sofapi.c` GhoulStartup/Shutdown,
  `gl3_sof.c` Ghoul GL trace (`r_om_ghltrace`), I4b's `GL3_Fog_RegionLinear` in `gl3_fog.c`.
- Found while comparing nyc2: after Ghoul entities, all later 2D (letterbox, tint, console text) vanished, because
  GL_ALPHA_TEST stayed on with Ghoul's reference value and in a compatibility context it applies to shader output.
  Fixed in `GL3_Ghoul_LeaveFixedFunction` (diff 59 → 7, rest random particles).
- For I4b: on nyc2 the head of the character in front of the camera shows a wrong, paper-like texture (original:
  skin and hair), see `cmp_nyc2.png` / `t_omgl_nyc2.png`. Texture names cannot collide (all from glGenTextures).

## Findings (black box)
- The original `screenshot` writes all-black TGAs on this PC (reads the back buffer after the swap; OpenGL presented
  through DXGI); comparisons use EndFrame captures.
- Depth writes must be on for the frame's depth clear (slot 0xb4 turns them off; OPEN_QUESTIONS I4a-2).
- The original never marks quads drawn alone with 0x1000 (traces: flags 0202/0402 stay), chain members only.
- The test copy's config has `vid_brightness 0.6`, `vid_contrast 0.6`: the colour table adds ~16 levels at the dark
  end, which made OpenMerc's lightmaps visibly darker until the table was applied to them.

## Remaining
- Terrain sets (SPEC-410/SPEC-144) and `avi` (SPEC-412 §13): not implemented (slot 0x38 messages only).
- Flowing surfaces: 2D scroll formula; detail pass for inline brush models; `gl_fogmode 1`;
  `gl_monolightmap` ≠ 0 variants (lightmaps always use the colour table path).
- Effect sprites could only be compared statistically (random particles); a replay harness feeding one recorded
  frame to both renderers would allow exact checks.
- The Yamagi front end (`ref_gl3.dll`) builds and shares all code, but was not run by I4a.

## Build
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I4a-x86&& tools\build\build-msvc.cmd x86 Release"
powershell -File engine\src\client\refresh\gl3\test\refcap\build.ps1     (capture shim, test only)
```

## Testing (black box)
- `engine/src/client/refresh/gl3/test/run_sof.ps1 -Script <cfg> -Ref gl|omgl -Tag <name>`: runs `SoF.exe` of
  the test copy on a separate, never shown desktop (`DeskRun.cs`; no focus, no input), with the renderer behind the
  capture shim `ref_omcap.dll` (`test/refcap/refcap.c`, written from SPEC-002: wraps `ref_<om_capref>.dll`).
  Console script lines drive it: `echo OMSHOT <name>` saves the next frame as `<Tag>_<name>.png`,
  `echo OMTRACE <name>` logs every renderer call of that frame (`<Tag>_<name>.trace.txt`, written before the real
  call, so flags show what earlier frames left). `compare.py <dir> <tagA> <tagB>` prints the differences and writes
  `cmp_<name>.png`.
- Scripts: `menu.cfg`, `basic.cfg` (menus, tsr1 intro), `maps.cfg` (spawn views of 8 maps, `sv_jumpcinematic 1`,
  `cl_maxfps 30`), `ghltrace.cfg`, `debug1.cfg`. Debug cvars of the module: `r_om_glstate 1` (dump GL state before
  the first 2D flush), `r_om_ghltrace 1` (log the Ghoul system's GL calls of one frame).
- Same SoF.exe, data, config and script for both renderers, so pairs are directly comparable; views that depend on
  timing (moving cameras, particles) are compared with frame traces.
