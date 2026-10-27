# I4b — Renderer: Ghoul model drawing in the 3D view (work log)

Package: SPEC-411 (model rendering, renderer side §1–§7), SPEC-012 (Ghoul runtime, drawing), SPEC-002 (entities
with Ghoul instances), SPEC-010/011 (system, instance, render-parameter block, GL table), SPEC-AMEND-002 (1.11,
4.1, 4.2), SPEC-410 (light point, point fog for models).
Directory: `engine/src/client/refresh/gl3/gl3_ghoul.c`, `gl3_ghoulemu.c`, `header/gl3_ghoul.h` (new), hooks in I4a's
files marked `/* I4b */`; tests in `engine/src/client/refresh/gl3/test/ghoul/`.

## State (2026-10-22)
- Ghoul entities of the 3D view are drawn in the SoF renderer module `ref_omgl.dll` inside the original SoF.exe
  1.07f (with the original Ghoul system of SoF.exe): characters, bolt-ons (weapons in hands, helmets), the police car
  with its model lights, props, client-side debris, the view weapon, shadows (blob and projected), model lights
  exported to the world, fog, all `ghl_*` lighting variants. Black-box comparison with the original `ref_gl.dll`
  in paused, deterministic frames: Ghoul pixels identical or within 0.4 mean |difference| (details below).
- GL 1.1 emulation for the Ghoul system on a GL 3.2 **core** profile (`gl3_ghoulemu.c`): verified the same way,
  in `ref_omgl` on a real core-profile context (`+set r_om_coreprofile 1`), against the original — 3D view and main
  menu models. This is the path of the OpenMerc-hosted `ref_gl3.dll`/`ref_gles3.dll` (no compatibility context).
- Builds: x86 (`ref_omgl`, `ref_gl3`, `ref_gles3`, tree `build-I4b-x86`) and x64 (`ref_gl3`, `ref_gles3`,
  `build-I4b-x64`), no warnings in the I4b files.
- `-portable` check: I4b never started the OpenMerc engine; all runs are the original SoF.exe of the test copy
  `D:\Games\SoF-test-I4b` (the oracle stays vanilla) with `ref_omgl.dll` or the original `ref_gl.dll`.
- Not done: the OpenMerc engine does not draw Ghoul entities yet, because its client has no Ghoul client system
  (svc 26/27 are skipped in `cl_sof.c`, Ghoul entities become null-model placeholders). The renderer side is ready
  for it — see *For the engine*.

## Design
- `gl3_ghoul.c` — SPEC-411 §5: per Ghoul entity (flag 0x400000) from the sorted translucent pass (`gl3_fx.c`
  DrawGhoulEntity hook): instance by system method 9 with the **signed** low 16 bits of `frame`; cull object
  (frustum / never, C struct whose first word points to a one-entry method table); entity matrix (rows forward,
  −right, up of (−pitch, yaw, −roll), origin); pre-cull with method 51; shadow mode; RF_VIEWERMODEL shadow-only;
  option bytes; light colour from `GL3_SoF_LightPoint`, the probe vector (method exactly 1), GL-style / hardware
  light options, post-processing (multiply, viewer-model vector, MINLIGHT, GLOW), light output pointer; fog
  (`ghl_obj_fog` point fog or the region's linear fog); state of step 11, method 41, restore of step 12.
  Calls into the Ghoul objects: MSVC x86 thiscall through `__fastcall` with a dummy EDX argument; elsewhere the
  object pointer is the first argument (x64 / GCC / Clang C++ ABI).
- World ray test for shadows: the engine's import 0x5c in the SoF module; in the Yamagi-hosted renderer a point
  trace through the world BSP (`WorldTrace`, solid = CONTENTS_SOLID | CONTENTS_WINDOW, −1 when the start is solid).
- GL back end, chosen once per renderer start: native opengl32 GL 1.1 (SoF module on a compatibility context:
  I4a's table `GL3_Ghoul_GLTable` with the SPEC-411 substitutions) or the emulation (core profile, GLES 3, or
  `r_om_ghlemu 1`). `GL3_Ghoul_Table()` gives system method 1 the matching table; the renderer's own state calls
  around method 41 go through the same back end (`ghlff_t`). Menu model views (slot 0xb8/0xb4, I4a's
  `gl3_sof2d.c`) use `GL3_Ghoul_MenuEnter/MenuProjection/MenuLeave`, so they work on both back ends.
- `gl3_ghoulemu.c` — the 52 entries: matrix stacks (modelview 32, projection/texture 8), Begin/End → triangles
  (TRIANGLES, STRIP, FAN, QUADS, QUAD_STRIP, POLYGON, lines, points; flat shading by the provoking vertex),
  vertex stage on the CPU (OpenGL lighting with 8 lights, inverse-transpose normals, sphere-map / linear texgen,
  texture matrix, fog coordinate; vertex colours clamped to 0–1), one shader for the fragment stage (texture
  environments MODULATE/REPLACE/DECAL/BLEND/ADD for the GL 1.1 base formats incl. ALPHA textures — shadow masks —
  via swizzles, fog, alpha test). Blending, depth, culling, polygon offset/mode stay real GL state. GL_CLAMP →
  CLAMP_TO_EDGE; filters follow `gl_texturemode`.

## Hooks in other packages' files (all marked `/* I4b */`)
- `gl3_fx.c`: DrawGhoulEntity → `GL3_Ghoul_DrawEntity`.
- `gl3_sofapi.c`: GhoulStartup gives `GL3_Ghoul_Table()` to method 1 in both builds; shutdown calls
  `GL3_Ghoul_Shutdown`; **light exchange no longer increments the engine's `num_dlights`** (the original works on a
  copy: with the old code a paused scene accumulated up to 32 copies of the police-car lights, lighting the street
  red/blue); the Yamagi-hosted `Init` slot accepts the Ghoul system; non-x86 Ghoul calls are real calls (were
  stubs).
- `gl3_sof.c`: `gl3_ghoul_trace` = import 0x5c. `gl3_fog.c`: `GL3_Fog_RegionLinear`. `gl3_sof2d.c`: menu state
  through the back end. `gl3_win32.c`: `r_om_coreprofile` (testing). `test/refcap/reftrace.h`: dumps raw entity
  fields, entity light output (entity +0x58) and the dlights after RenderFrame (incl. those past the count).
- `engine/CMakeLists.txt`: `gl3_ghoul.c`, `gl3_ghoulemu.c` in GL3-Source (all three renderers).

## Testing (black box) — results in `<openmerc-data>\shots\I4b\`
Harness `test/ghoul/run_i4b.ps1` (copy of I4a's `run_sof.ps1` for the test copy `D:\Games\SoF-test-I4b`: original
SoF.exe on a hidden desktop, renderer through the capture shim). Determinism: `+set fixedtime 30` (every frame
advances 30 ms, so frame N is the same game moment in every run) and `pause` before each shot. Every shot is taken
twice in the same paused frame, normally and with `ghl_no_render 1`; `ghoulcmp.py` compares original and OpenMerc
only on the pixels that change between the two (Ghoul models and their shadows) and writes
`gcmp_<name>.png` (original | OpenMerc | difference ×4). Scripts from `gen_scripts.py`:
`suite_intro.cfg` (tsr1 intro: characters, police car with model lights; at 16 and 19 all variants
`ghl_light_method 0/1/3`, `ghl_fast_lights 1`, `ghl_max_lights 8`, `r_fullbright 1`, `ghl_light_multiply 0.5`,
`ghl_shadows 0/1`, `ghl_wireframe 1`), `suite_play.cfg` (tsr1 player: view weapon, own shadow, firing, the same
variants + `ghl_player_shadow 0`), `suite_maps.cfg` (10 level starts incl. fogged models, `ghl_obj_fog 0`),
`suite_nyc2.cfg`, `menu_i4b.cfg`.

| Comparison (Ghoul pixels) | Result |
|---|---|
| native `ref_omgl`, intro, 25 shots (`orig_s` / `om_s`) | 0.00–0.37 mean, 0 % > 24, except i25 (client debris with random physics) |
| native, player view, 15 shots (`orig_p` / `om_p`) | 0.00–0.09, except the first shot of a pause (mission text overlay, 1–7) |
| native, 10 level starts × 2 (`orig_m` / `om_m`) | ≤ 1.0, nyc1 (fogged characters) 2.6, trn1 7 (terrain set missing — I4c, not Ghoul) |
| core profile + emulation, intro / player (`core_s`, `core_p`) | as native; `ghl_light_method 3` (OpenGL lighting) 0.00 |
| main menu models (`orig_menu`/`om_menu`/`core_menu`) | 0.6 / 0.5 mean over the whole screen (globe rotation phase) |
| renderer → engine light output (entity +0x58, trace) | identical floats for all 17 Ghoul entities of a frame |
| appended model lights (trace) | identical records (origin, colour, intensity 200) |

Found and fixed on the way: Ghoul handles must be taken as signed 16-bit (client debris has frame −1 → the original
draws it, OPEN_QUESTIONS I4b-1); dynamic-light accumulation (above); emulation: vertex colours must be clamped
before interpolation, normals use the inverse transpose.

I4a's nyc2 report (a "paper-like" head on the character killed by the explosion at the start of nyc2): game
randomness, not drawing. Ten original runs of the same deterministic script: 9× brown hair, 1× the same pale head
(`heads_orig6.png`, `heads_all.png`); OpenMerc shows both too, with identical models otherwise. Uploads, GL errors
and texture deletions were checked (no GL error ever pending, the renderer never deletes a Ghoul texture name).

Observations for the record (GL table trace of the original Ghoul system, `r_om_ghltrace`; no spec change needed):
it draws with TRIANGLE_FAN, TRIANGLE_STRIP and TRIANGLES (the emulation must convert all three), issues empty
Begin/End pairs, and with `ghl_light_method 3` sets light positions after its model matrix (model space), DIFFUSE =
SPECULAR, LINEAR_ATTENUATION, QUADRATIC 0, then LIGHTING, LIGHT_MODEL_AMBIENT and per surface
AMBIENT_AND_DIFFUSE, EMISSION, SHININESS 64, SPECULAR.

## For the engine (OpenMerc client, not in this package)
To get Ghoul entities in the OpenMerc engine the client needs a Ghoul client system (I3 library, role
`OM_GHOUL_CLIENT`): feed svc 26 payloads to system method 24, call 23 when loading has finished, svc 27 payloads to
25 with the frame number and time (I3.md *For consumers*); give the system to the renderer through the SPEC-002
`Init` slot of `OM_GetSoFRefExports()` (`Init(NULL, NULL, system)`; `NULL` detaches); build Ghoul entities with
`flags` 0x400000 (+ the state's other render flags) and `frame` = the entity state's Ghoul handle, and draw through
the SPEC-002 RenderFrame (I5's `CLFX_RenderFrame` already does). On a renderer restart call `Init` again (textures
die with the context). Menu models: the same system pointer makes slot 0xb8 … 0xb4 work (emulated GL 1.1).

## Remaining / limits
- Night vision (rdflags 0x4) shares the code path of `r_fullbright` (tested) but was not seen in game (no console
  way to get the goggles found).
- SPEC-411 open questions 2, 3, 5 (dynamic lights only for models with light nodes, fade reversal, the 0.75·R step)
  are inside the Ghoul system (I3 / the original), not decidable from the renderer side.
- The emulation has no two-sided lighting (stock Ghoul data does not use it) and approximates projective texture
  coordinates per vertex.

## Build
```
cmd /c "set OM_BUILD_DIR=<repo>\build-I4b-x86&& tools\build\build-msvc.cmd x86 Release"
powershell -File engine\src\client\refresh\gl3\test\refcap\build.ps1 -Out <repo>\build-I4b-x86\refcap
powershell -File engine\src\client\refresh\gl3\test\ghoul\run_i4b.ps1 -Script engine\src\client\refresh\gl3\test\ghoul\suite_intro.cfg -Ref omgl -Tag om_s -Extra "+set fixedtime 30"
python engine\src\client\refresh\gl3\test\ghoul\ghoulcmp.py <openmerc-data>\shots\I4b orig_s om_s
```
Debug cvars (engine cvars, OpenMerc only): `r_om_ghlemu 1` (emulation on a compatibility context, read at renderer
start), `r_om_coreprofile 1` (core-profile context, SoF module), `r_om_ghldebug 1|2` (print the render-parameter
block of every / every fogged Ghoul entity).
