# Rendering roadmap: open spaces, large maps, mobile

Written 2026-11-04 from an audit of the current ref_gl3 code (file:line facts below) and measurements in the game.
Network protocol, multiplayer contracts mode and gameplay are out of scope here, except where a limit blocks large
maps already in single player.

## Where we are (facts)

### World drawing
- Visibility is the stock Quake 2 scheme: PVS per cluster (`GL3_MarkLeaves`, gl3_surf.c:1883), a recursive BSP
  walk with frustum and areaportal tests (gl3_surf.c:1648-1819). No distance culling, no LOD, no occlusion
  culling. The far plane is `r_farclipdist` 65535 (gl3_sofcvar.c:119); `r_farsee` is registered but read nowhere.
- Submission: faces are chained per texture, every vertex is copied into one stream buffer and re-uploaded
  with `glBufferData(GL_STREAM_DRAW)` each flush (gl3_main.c:917). Nothing static lives on the GPU. A draw call
  breaks on any state change: texture, lightmap page, light styles, fog region, emissive, alpha.
  On a large detailed map the CPU (walk + copy + upload) is the wall long before the GPU.
- Brush entities flush the batch one by one; seam strips are one draw each.

### Fog (the owner's "level draws through the fog, sections pop in")
- **Measured 2026-11-04**: the fog overlay queue had a fixed 1024 faces (gl3_fog.c). sib1 needed 3000-5400 fogged
  faces a frame and dropped 2000-4400 of them (up to 80 % drawn without fog), sud3 dropped 270-385, a different
  set each frame. **Fixed** the same day (growable queue). kos1 and ger1 did not overflow at the toured spots.
- Default `gl_fogmode` 0 = translucent overlay polygons with per-vertex alpha; per-pixel fog (`r_om_ppfog`) is
  off by default. Volumetric fog only covers regions walked this frame, at most 16 (gl3_post.c:1924), and dims
  the overlay to 0.85 when on. The far fog wall is one quad covering about +-51 degrees (gl3_fog.c:904).
- Nothing is culled at the fog end: geometry far behind the fog is still walked and drawn.

### Limits
| Limit | Value | Where | Effect on big maps |
|---|---|---|---|
| Network/prediction coordinates | short x 1/8 = **+-4096** | common/movemsg.c:611, shared.h:692 | single player goes through the local client/server too: a map wider than 8192 breaks movement and entity positions |
| BSP leaf bounds | short, +-32767 | files.h:550 | hard ceiling of the format |
| Lightmap pages | 8 x 1024x512 | local.h:218-221 | ERR_DROP when full (gl3_lightmap.c:72) |
| Textures / models known | 1024 / 512 | local.h:439, gl3_model.c:36 | |
| Edicts / models / sounds / images | 1024 / 256 / 256 / 256 (bytes on the wire) | shared.h:192-196 | |
| Map lumps | Q2 sizes (FACES 65536, EDGES 128000 ...); the SoF loader allows more | files.h:243, collision.c:2367 | |
| Fog regions | 256, 5 planes each | gl3_fog.c:19 | |

### Anti-aliasing and resolution
- MSAA (default 8x) plus alpha-to-coverage. No FXAA, SMAA, TAA, no render scale, no dynamic resolution.
- Half-resolution pieces: volumetric fog, SSAO, mirror.

### Mobile readiness
- `GLES3_RENDERER` builds ref_gles3 from the same sources (`#version 300 es`). HDR, SSAO, SSR, weather and GPU
  tessellation are compiled out on GLES; bloom, volumetric fog, CSM, beams and the world path are shared.
- PS Vita has no GLES3: its GPU (SGX543MP4+) is GLES2-class through vitaGL, 512 MB RAM, 960x544. It needs its own
  "lite" path, not the GLES3 one.

## Plan

Effort in working days of one developer with the test harness we have; each step ends with an A/B inside one run.

### Phase 0 - bugs (done 2026-11-04)
- Corpses standing in one pose (every second death): the corpse freeze ran before the death animation was set
  and paused the body at once (12bdd48).
- Click on the newest-save picture of the landing page loads it (b982942).
- SSR "relief / cel shading" on doors: gloss per texel followed the texture's pattern; now at the scale of stains
  (c7f30cc).
- Fog queue overflow on open maps (this roadmap's first measurement).

### Phase 1 - open spaces look right (2-3 weeks)
1. **Per-pixel fog for everything** (world, brush models, Ghoul models, particles, sky) in the shaders, the
   SoF region rules kept (density, colour, boundary planes as a distance field per region). Default on; the
   overlay stays as the original-look fallback. Removes per-vertex banding and the overlay's draw cost. 4-5 d.
2. **Cull at the fog end**: per frame the largest fog end around the eye gives a far distance; clusters, faces,
   entities and the far plane beyond it are skipped (the fog hides the cut). The first real distance culling;
   also the mechanism `r_farsee` should have been (0 = cut at the fog, 1 = full distance). 2-3 d.
3. **Sky in the fog**: the horizon of the sky blends to the fog colour, the far wall replaced by that. 1-2 d.
4. **No popping of PVS sections**: union of the PVS of the clusters within a radius of the eye (and the next
   cluster along the velocity), so turning and running do not show sections appearing; areaportal doors fade
   instead of cutting. Measure with an automatic pop test: a camera path recorded at 60 fps, frame-to-frame
   difference spikes are reported with the cluster change. 3-4 d.
5. Volumetric fog: all regions within the far distance (not only walked ones), no 16-region cap, no overlay
   dimming once per-pixel fog is on. 2 d.

### Phase 2 - a renderer for large, detailed maps (6-8 weeks)
1. **Static world on the GPU**: at map load all faces go into one vertex/index buffer, grouped by
   (texture, lightmap page); per frame only index ranges of visible groups are drawn (`glMultiDrawElements`,
   GLES3: merged index lists). No per-frame copy and upload. Expected: CPU world cost down by an order of
   magnitude on big maps. Light styles move to a uniform/texture lookup so they stop breaking batches. 8-10 d.
2. **Texture arrays / atlas for lightmaps** (one 4096x4096 array, many layers): no ERR_DROP, fewer state
   breaks; world textures in arrays by size class. 4-5 d.
3. **Hierarchical culling**: clusters in a BVH, frustum test per node; on desktop GPU occlusion culling with a
   hierarchical Z of the previous frame; on mobile PVS + distance only. 6-8 d.
4. **LOD**: Ghoul models by screen size (the tessellation rule already measures it), world HLOD: distant
   clusters replaced by merged, simplified proxies baked at compile time (lightmap baked into the proxy texture).
   This is what makes "very big detailed maps" affordable. 10-12 d plus tools.
5. **Streaming and memory budget**: textures and lightmaps of far sectors at low mips, loaded on demand; a hard
   VRAM/RAM budget per platform (Vita 512 MB total). 6-8 d.
6. **Lift the limits** (in this order, each behind a format version so retail maps keep loading):
   - coordinates to 32-bit float in the local client/server messages and prediction (needed for any map wider
     than 8192, even in single player; the network side follows in the protocol work);
   - an OpenMerc BSP version: 32-bit indices and bounds, no fixed lump caps, more fog regions;
   - edicts, models, sounds, images beyond 256/1024; lightmap and texture counts dynamic.
   Compiler side: our omlight baker already re-bakes lightmaps; the map compiler needs the same extension. 10-15 d.

### Phase 3 - scaling, AA, platforms (4-6 weeks, partly parallel to phase 2)
1. **Render scale + upscaling**: render the 3D view at a lower resolution, upscale with AMD FidelityFX Super
   Resolution 1.0 (EASU + RCAS; spatial, no motion vectors, runs on GLES3). Dynamic resolution from the GPU timer
   we already have (`r_om_gputime`). 4-5 d.
2. **FXAA 3.11** as the AA of GLES and of low presets (one pass, cheap); SMAA 1x optional on desktop; MSAA stays
   on desktop. TAA later, once motion vectors exist (SSR/SSAO already accumulate temporally). 2-3 d (FXAA),
   4 d (SMAA).
3. **Quality presets**: Low (Vita), Medium (Android mid-range), High (PC), Ultra. Every effect has a GLES path or
   a defined fallback; presets switch render scale, AA, shadows, SSAO/SSR, fog mode, LOD distances. 3 d.
4. **Texture compression pipeline**: ETC2/ASTC for Android, DXT/PVRTC for Vita, BC7 for PC, built offline from
   the HD and retail art. 4-5 d.
5. **PS Vita lite path** (vitaGL, GLES2-class): forward lightmapped world, no MRT, no post except FXAA, 960x544 or
   lower plus upscale, compressed textures, LOD and the far cut from phases 1-2 doing the heavy lifting. Needs
   a device for measurements. 15-20 d.
6. Android build of ref_gles3 with the phase 3 presets; Vulkan as the second renderer later (see platforms note).

### Phase 4 - proving it (continuous)
- A large stress map: several retail maps stitched side by side beyond +-4096 once the coordinates are 32-bit,
  plus a dense detail test map.
- Perf tours (existing tour harness) per platform preset; budgets in ms per pass; pop test from phase 1.4.

## Order and why
Phase 1 first: it fixes what the owner sees today and its far cut is the base of every later step. Phase 2.1
(static GPU world) before LOD and streaming: it removes the CPU wall and makes the effect of the rest measurable.
The coordinate limit (2.6, first item) is small but blocks every large-map test, so it goes in as soon as a
large test map is wanted. FXAA and render scale (3.1-3.2) are cheap and independent: they can be done any time
someone needs a break from phase 2.

## Release readiness (audit 2026-11-04)

Tested: all 32 single-player and 43 deathmatch maps, 6-8 spots each (two fixed shots + a timed 360 degree turn),
GL debug context on; a second pass after the fixes compared shot by shot (tools: session scratchpad audit.py /
audit_run.ps1 / anom.py / regress.py, campath replay + per-frame AVI for motion).

* GL errors: none on any map (only NVIDIA performance notes: the screenshot read-back, and a vertex shader
  recompile on first use of three programs - once per run, not a per-frame cost; an integer generic
  attribute value did not remove it). Three DM maps (sudctf1, suddm1, suddm2) loaded without errors but gave no shots:
  the DM loadout menu kept the test player from spawning (a harness limit).
* Frame cost (1080p window 1280x720 in the tour, GTX 1050 Ti): GPU 4-7 ms on every map with all effects on; CPU
  ~3-4 ms. Hitches seen only on screenshot frames and camera teleports, plus external stalls of the whole process.
* Fixed in this audit: lifecycle bugs after vid_restart / map change (weather, mirrors, night vision, timers, Ghoul
  glow lists, ambient-cube / sky-mask / seam caches, SSAO / SSR targets), NaN in Ghoul per-pixel lighting, driver
  round trips in draw paths, uncached uniform lookups, cascade / mirror / beam redraw costs, GLES build.
* Known, not done (in order of value):
  1. Static world in GPU buffers (phase 2.1) - the CPU still rebuilds the visible world each frame (0.5-0.7 ms of the
     3-4 ms CPU frame here; the wall on large maps).
  2. Mobile tier: lamp shadows fit 16 texture units now (units 11 / 7, r_om_lshadow_compact); CSM 2048 x 8
     layers = 128 MB (mobile: 1024, 2 cascades, DEPTH16), MSAA via EXT_multisampled_render_to_texture, the Ghoul
     emulator's CPU tessellation path on GLES 3.0 (samplerBuffer is 3.2).
  3. Done since: render scale + Catmull-Rom/CAS upscale (r_om_renderscale), quality presets (r_om_preset), map
     load 15.7 -> 4.9 s. Still open: FSR 1.0 EASU itself, SMAA.
  4. Vita: needs a GLES2-class lite renderer (this one uses MRT, array / shadow samplers, UBOs, float targets).
  5. Vulkan: the post passes are self-contained (bloom, HDR, FXAA, SSAO, SSR, volfog, weather, shadows); the Ghoul
     fixed-function emulator and the text-assembled shaders are the hard parts.

Final pass on the last build (32 SP maps, hidden test windows `vid_testwindow 1`, below-normal priority while the
PC was in use): no GL errors on any map; GPU 5.50 ms mean (5.71 on the second pass); tour shots within a few percent
except random effects (rain sheet, AA fire, lightning); map load 4.0 s mean, 7.3 s max (tsr1 15.7 s before). The CPU
column (4.3 ms) is not comparable to the earlier passes: the vertex rings are off again and the runs shared the
machine at a lower priority - CPU A/B has to be done in one run.

CPU A/B (the honest one): the renderer from before the audit and the current one, the same sib1 camera path
(r_om_campath, 1630 frames), hidden window, normal priority, runs old / new / old / new: CPU per frame (sum of the
OMPROF parts) 3.98 / 1.96 / 4.00 / 2.00 ms - half; GPU 7.33 / 7.21 / 7.34 / 7.48 ms - unchanged.
