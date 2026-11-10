# GFX-modern: bloom, volumetric fog, cascaded shadows, per-pixel dlights, soft particles, overbright (OpenGL 3)

Status: implemented in ref_omgl on branch gfx-modern (not pushed, not in the main build); results and deviations in section 15. Updated 27 Oct 2026 (Asia/Yekaterinburg, UTC+5).
Branch: `gfx-modern` worktree `<worktree>` from main `900a960`.
Author: C team (clean room: specs and black-box observation only). Data hooks from
`docs/specs` (SPEC-100, 101, 410, 411, 413) and black-box BSP/entity scans of stock maps
under `openmerc-data\extract` (75 maps). Owner asks: [pt54u] (bloom / volfog / CSM),
[pt55u] (per-pixel dlights, soft particles, per-pixel region fog, SoF Buddy-style overbright),
[pt69u] (graphics defaults MAXIMUM; all effect settings in the video menu; menu redesign
from scratch later).
R-team behaviour answers folded in from **SPEC-AMEND-016** (approved on main as `d148439`,
gatekeeper notes for C in that amend), see §7.

**Shipping defaults are MAXIMUM** (owner [pt69u], 2026-10-27). Already-exposed quality
cvars (`r_msaa_samples`, `r_anisotropic`, `gl_texturemode`, `r_farsee`, and SoF ones that
were already at max such as `gl_picmip 0`, `gl_detailtexturing 1`, `ghl_shadows 2`) default
to their highest useful values. Bloom, volumetric fog and cascaded sun shadows
**default ON at high quality**; per-pixel dynamic lights, soft particles, per-pixel fog regions and
SoF Buddy-style overbright are **options and default OFF** (owner [pt55u]; f870b5e had wrongly set
them ON). Each has its own toggle (and quality) in the video menu. Existing `CVAR_ARCHIVE` configs keep whatever the user already saved.

**Where this lives.** Everything in this document, including the MAXIMUM defaults (`f870b5e`), is on branch
`gfx-modern` only and is **not merged into `main`** (state 2026-10-27). Builds from `main`, and the owner's
`D:\Games\OpenMerc`, still have the previous defaults. A user's existing `config.cfg` is not rewritten by the new
defaults; "Defaults" in the video menu (or a fresh config) applies them.

**Not implemented yet (shaders).** Cvars are registered; no visual change until post/FBO
work lands. Full features wait until movement / FX-pool / mode-B crash work settles.
Target ship path: `ref_gl3` / `ref_omgl` (OpenGL 3 core). Vulkan is later. Owner GPU:
RX 9070 (plenty of headroom); quality levels keep low-end viable via the same toggles.

## 0. What we already have (injection points)

Frame path today (`gl3_main.c` `GL3_RenderView` / `GL3_RenderFrame`):

1. Optional post FBO (`gl3_usefbo`, default **0**): colour `GL_RGB` 8-bit + depth/stencil
   **renderbuffer** (`GL_DEPTH24_STENCIL8`). Depth is **not** sampleable.
2. World: lightmapped shaders (`fragmentSrc3Dlm`) + SoF colour table + dyn-light layer on
   TMU6 (`gl3_dynlight.c`, SPEC-410) + Yamagi `uniLights` (mostly Q2 path).
3. Fog region overlay pass 1 (`gl3_fog.c`, BSP lumps 19/21, SPEC-410).
4. Entities / FX sorted pass (`gl3_fx.c`, Ghoul via emu or module).
5. Fog overlay pass 2.
6. Unbind FBO → `GL3_DrawFrameBufferObject` (`si2DpostProcess`: only `v_blend`).
7. Screen overlays (night vision tint) → SoF 2D / HUD.

SoF fog volumes are **already** implemented as translucent overlays + optional hardware
linear fog (`gl_fogmode`). Ghoul has blob / projected silhouette shadows (`ghl_shadows`),
not real depth maps. World lights from BSP lump 20 are fed to Ghoul; **type 3 (sun/sky
patches) are skipped** in `GL3_SoF_WorldLights` today.

`horplus` widens horizontal FOV in the client before `refdef` reaches the renderer. All
screen-space FX must use the **actual** viewport / projection the renderer got (already
`r_newrefdef.width/height` and the matrix from `GL3_SetPerspective`). Cascade split
planes must be rebuilt from that same frustum, not from a 4:3 assumption.

## 1. Shared infrastructure (do this once, reuse for all three)

Modern FX need a richer G-buffer-ish post path. Plan one upgrade behind
`r_om_postfx` (auto 1 when any of bloom / volfog / csm / soft particles / per-pixel fog
needs it; per-pixel dlights and overbright do **not** need it):

| piece | today | needed |
|---|---|---|
| colour target | RGB8 FBO | **RGBA16F** (or RGB10_A2 + separate emissive) when bloom or volfog on |
| depth | renderbuffer | **depth texture** (`GL_DEPTH24_STENCIL8` or D32F + S8) so shaders can sample |
| MRT | single colour | colour + **emissive** (RGB16F) when bloom on |
| blit | LDR `si2DpostProcess` | chain: resolve → optional bloom → optional volfog composite → LDR tonemap → HUD |
| `gl3_usefbo` | manual | force on when any `r_om_*` effect is on; keep old path when all off |

New files (suggested, not created yet):

- `gl3_post.c` — FBO / MRT alloc, resolve, tonemap blit
- `gl3_bloom.c` — bright extract + blur + add
- `gl3_volfog.c` — froxel or half-res raymarch
- `gl3_csm.c` — cascade matrices, shadow atlas, PCF sample helpers
- `gl3_pdlight.c` — per-pixel dlight list / UBO upload for SoF maps (§8)
- soft particles and overbright need no new file: shader variants + uniforms (§9, §11)
- shaders living next to existing strings in `gl3_shaders.c` (or split later)

Quality ladder (all effects):

| level | meaning | default |
|---|---|---|
| 0 | off | user can still pick Off in the video menu |
| 1 | low (half-res bloom, 2 cascades 1024, cheap PCF, volfog every 2nd frame or 32 steps) | weaker GPUs |
| 2 | high (full-res / 3–4 cascades 2048, 5×5 PCF, denser volfog) | **ship default** (RX 9070 class) |

Budget (1080p, RX 9070, rough, to be measured): bloom ≤ 0.3 ms, volfog ≤ 0.8 ms,
CSM fill ≤ 1.5 ms, CSM receive ≤ 0.5 ms. On weaker GPUs the same cvars at level 1 or 0.

## 2. Bloom — only what actually glows

### 2.1 Goal

Owner: light what **really** emits light, not a whole-screen wash. So **emissive mask**,
not a naive luminance threshold on the final LDR image (that blooms every bright wall
under a lightmap).

### 2.2 Sources of emissive (SPEC-AMEND-016 approved)

1.07f has **no** bloom / glow / HDR pass (R16.1). Seeds are a C design choice. Gatekeeper
notes for C (AMEND-016): bloom only from what really glows **and is drawn**. Default seeds:

| source | evidence | how to feed bloom |
|---|---|---|
| **SURF_LIGHT** (`flags & 0x1`) + `value` | SPEC-100; runtime has no self-lit world surfaces (R16.1), but gatekeeper default seed | during lightmapped draw: `texel.rgb * emissiveScale(value)` into emissive RT (not × lightmap). `r_om_bloom_surfscale` |
| **Ghoul material self-illumination** (P4 high, ≥ ~`r_om_bloom_ghoulmin`) **as drawn** | R16.7: 175/1030 materials have P4>0; "off" lamp variants still carry GLOW materials in the file | write `texel.rgb * P4` only for pieces that are actually visible this frame |
| **Ghoul additive materials** | R16.7: 70 additive | same as FX additive |
| **additive FX / sprites / muzzle / flares** | SPEC-412 0x0200; R16.1 | FX ADD blend → emissive (`r_om_bloom_fx`) |
| **RF_FULLBRIGHT / RF_GLOW** entities | SPEC-411; R16.1 | models drawn fullbright / pulsing → contribute to emissive |
| flag **0x1000** | texture animation only (R16.1) | **not** a bloom seed by default. Screen glow, if ever wanted, is a separate opt-in later |
| SURF_LIGHT + SURF_SKY (`0x5`) | sky faces | skip |
| HDR threshold on final colour | wrong for SoF | debug only (`r_om_bloom_fromscene`) |

BSP scan: 56/75 maps have `_sun_*` keys; those are for CSM direction (§4), not bloom.

### 2.3 Pipeline

```
world/entity/FX → MRT0 colour (lit) + MRT1 emissive
                 → downsample emissive (¼ → ⅛ → …)
                 → separable Gaussian / dual-kawase
                 → upsample + additive composite into colour
                 → tonemap / gamma (existing pow(gamma) moves here for FBO path)
                 → HUD (never bloomed)
```

Cvars (all `CVAR_ARCHIVE`, defaults safe):

| cvar | default | meaning |
|---|---|---|
| `r_om_bloom` | `1` | 0 off, 1 on (implies post FBO + emissive RT); ship ON |
| `r_om_bloom_threshold` | `0.0` | soft knee on emissive before blur (0 = use raw emissive) |
| `r_om_bloom_intensity` | `0.45` | composite strength |
| `r_om_bloom_radius` | `1` | blur width scale (quality) |
| `r_om_bloom_surfscale` | `1` | SURF_LIGHT contribution from `value` (gatekeeper default seed; 0 disables) |
| `r_om_bloom_ghoulmin` | `0.5` | Ghoul P4 self-illumination threshold for emissive (R16.7) |
| `r_om_bloom_fx` | `1` | include additive FX / additive Ghoul materials in emissive |
| `r_om_bloom_fullbright` | `1` | include RF_FULLBRIGHT / RF_GLOW entities in emissive |
| `r_om_bloom_fromscene` | `0` | debug: also threshold scene luminance (owner does **not** want this as default) |

### 2.4 Interaction / risks

- **Lightmaps:** SURF_LIGHT faces already store emission colour in the lightmap
  (SPEC-101). Bloom must use the **diffuse texel × emission weight**, not the lightmap
  sample, or lamps double-count as lit room and glow.
- **Overbright (§11):** bloom extract reads the emissive RT, not the overbrighted colour,
  so `r_om_overbright` does not change what blooms.
- **Night vision / `r_fullbright`:** disable or hard-clamp bloom (fullbright would wash).
- **`gl_modulate` / colour table:** apply bloom in linear-ish space after colour table on
  the lit buffer; emissive bypasses colour table or uses a fixed curve — pick one and
  document in the first visual spike.
- **Risk — wash:** if `value` buckets (0…10000+) are fed linearly, high-value lights
  blow out. Need a curve (`log` or clamp) tuned on `tokyo/*light*`, `iraq/*light*`.
- **Risk — missing glow / false glow:** 0x1000 screens stay unbloomed by default (R16.1).
  Ghoul lamps: follow **drawn** pieces only (R16.7 item 4), or "off" variants still bloom.

## 3. Volumetric fog

### 3.1 Goal

Own toggle. Must **not** replace SoF region fog (lump 21 overlay). Region fog stays the
gameplay/visibility match; volumetric is an optional look layer (god rays / soft depth
fog / denser air in those same volumes).

### 3.2 Approaches (pick for first ship)

| option | fit | cost | note |
|---|---|---|---|
| **A. Region-aware half-res raymarch** | best legal/visual fit | medium | march view rays; density from fogregion_t (opacity, start, range, AABB, planes) already in `gl3_fog.c` |
| B. Global exponential height fog + sun shafts | pretty outdoors | low–med | ignores indoor regions; fights maps with no `_sun_*` |
| C. Full froxel volume updated each frame | flexible | high | overkill for SoF scale; keep for Vulkan era |

**Recommendation:** A for v1, with optional cheap sun-shaft term when CSM (or a
analytical sun dir) is available. When `r_om_volfog` is on, slightly lower overlay alpha
(`r_om_volfog_overlaydim`, default 0.85) so the two layers do not stack to mud.

### 3.3 Pipeline

Needs sampleable depth (shared infra). After opaque+translucent world/FX (depth final):

```
bind half-res RT
for each pixel: raymarch N steps along view ray
  density = FogThickness(p) from existing region maths (SPEC-410)
  optional: shadow from CSM cascade → shaft term
composite over colour with premul / over
```

Cvars:

| cvar | default | meaning |
|---|---|---|
| `r_om_volfog` | `1` | master; ship ON |
| `r_om_volfog_quality` | `2` | 0=off alias, 1=half-res 24 steps, 2=full/48; ship high |
| `r_om_volfog_intensity` | `0.4` | scales region opacity for the volume term |
| `r_om_volfog_overlaydim` | `0.85` | multiply classic overlay alpha when volfog on |
| `r_om_volfog_shafts` | `1` | use CSM/sun visibility for shafts (no-op if no sun/CSM) |

### 3.4 Interaction / risks

- **Fighting SoF fog:** double fog if overlay undimmed; indoor maps with dense regions
  (e.g. irq3b) go opaque. Always dim overlay when volfog on; provide `gl_fogmode 2` escape.
- **Transparent FX / glass:** raymarch should stop or fade using depth; particles in fog
  already get point fog — do not fog them twice in the composite.
- **Terrain (`trn1`):** has its own fog header (SPEC-410). Decide: include in volfog or
  leave to terrain path (open question).
- **No fog entities in stock maps:** BSP entity scan found **no** `fog*` classnames;
  volumes are **only** lump 21. AMEND-016 confirms: fog **kind** (offset 0) is unused in
  1.07f; volfog density comes from fog regions only.
- **Per-pixel region fog (§10)** is the cheap sibling of volfog and uses the same region
  maths. Only one of them owns the region term in a frame (see §10.3).

## 4. Cascaded shadow maps (smoothed)

### 4.1 Goal

Careful, smoothed directional shadows. Own toggle. AMEND-016: 1.07f has **no realtime sun
shadows** (lightmaps + Ghoul blob/silhouette only), so CSM is a pure addition. When
`r_om_csm` is on, the Ghoul blob / projected model shadows are **disabled** for casters in
the atlas to avoid a double shadow (`r_om_csm_replace_ghl`, default 1). With CSM off,
`ghl_shadows` stays exactly as today.

### 4.2 Light direction (data hooks)

AMEND-016 R16.2: `_sun*` keys are **map-compiler only** in 1.07f (no binary reads them).
Gatekeeper: CSM reads those keys from the map entity text **at load** (user pak data, not a
hard-coded table) and recovers direction by the R16.2 convention. Ship default is **ON**; maps with weak / missing sun (nycdm4 UNVERIFIED mismatch, ger3 missing target, sibctf1):
CSM must look acceptable or be skipped until checked in game.

Worldspawn keys on 56/75 maps (scan): `_sun_light`, `_sun_color`, `_sun_ambient`,
often `_sun_diffuse`, and a direction via `_sun_angle` / `_sun_vector` / `_sun_target` /
`_sun` (target name). 53 maps have at least one direction-like key.

Lump 20 type **3** records are sky/sun patches on a grid (SPEC-101). R16.3: always skipped
by the only runtime consumer (Ghoul world-light set). **Must not** become Ghoul point lights.
Using them as an optional sun-colour hint for CSM is allowed.

**No full dynamic map lighting** in this feature (owner already accepted that earlier).
CSM is **sun / key directional** only. Dynamic point lights stay on lightmaps
(`gl3_dynlight`) and Ghoul local lights; optional shadowed points = later phase.

### 4.3 Pipeline

```
CPU: build 3–4 cascade frusta from camera (post-horplus) + sun dir
     render casters into atlas (2D array or packed 2×2) with depth-only shader
GPU receive: in lightmapped + Ghoul + alias shaders
     sample cascade by view depth, 3×3 or 5×5 PCF (v1); VSM/EVSM optional later
     shadow factor multiplies **sun term only** if we can isolate it; else a gentle
     multiply toward ambient floor (see risks)
```

Casters: opaque world brushes (not sky, not 0x10000 fog boundary, not NODRAW), Ghoul
opaque, alias opaque. Skip first-person weapon (`SOF_RF_WEAPONMODEL`) and
`RF_VIEWERMODEL` body for player self-shadow unless `r_om_csm_player` 1.

Receivers: same plus alpha-tested (alpha clip before depth write in caster pass).

Cvars:

| cvar | default | meaning |
|---|---|---|
| `r_om_csm` | `1` | master; ship ON |
| `r_om_csm_cascades` | `4` | 2–4; ship high |
| `r_om_csm_size` | `2048` | per-cascade resolution; ship high |
| `r_om_csm_soft` | `2` | 0 hard, 1 3×3 PCF, 2 5×5, 3 blocker-search PCSS later |
| `r_om_csm_bias` | `0.0015` | constant depth bias (tune vs acne / peter-pan) |
| `r_om_csm_normalbias` | `0.02` | world-unit slope bias |
| `r_om_csm_split` | `0.75` | practical-split blend (log ↔ uniform) |
| `r_om_csm_maxdist` | `2048` | beyond last cascade → no CSM (keep lightmaps) |
| `r_om_csm_replace_ghl` | `1` | suppress Ghoul silhouette when CSM covers entity |
| `r_om_csm_strength` | `0.65` | how hard shadows darken (keep >0 ambient) |

### 4.4 Interaction / risks

| risk | mitigation |
|---|---|
| **Peter-panning** | normal bias + polygon offset on caster; never huge constant bias |
| **Shadow acne** | depth bias + cascade-local bias scale by texel size |
| **Light bleeding (VSM)** | ship **PCF first**; VSM only behind `r_om_csm_filter vsm` after comparison |
| **Cascade seams** | overlap blends; stable splits from camera forward, not jittered every frame without temporal |
| **Indoor wrongness** | lightmaps already bake occlusion. Multiplying whole lighting by CSM double-darkens corridors. Prefer: apply CSM only to a reconstructed sun contribution, or only when the eye leaf / surface can see sky (cluster skybit), or keep `r_om_csm_strength` modest and floor at ambient from `_sun_ambient` |
| **horplus** | rebuild cascades from real `fov_x`/`fov_y` each frame |
| **Weapon depthhack** | exclude view weapon from caster and receiver |
| **Perf** | atlas updates every frame is fine on RX 9070; on weak GPUs drop to 2 cascades @ 512 and `r_om_csm_soft 1`, or update odd cascades alternate frames |

Existing `ghl_shadows` stays the low-end path when `r_om_csm 0`.

## 5. Implementation order (after stability)

1. Shared post FBO upgrade (RGBA16F + depth texture + force when needed). No visible change if effects off.
2. Bloom emissive MRT from SURF_LIGHT + Ghoul self-illum/additive (as drawn) +
   RF_FULLBRIGHT/RF_GLOW + FX. Not 0x1000. Tune on nyc/tokyo lights and Ghoul lamps.
3. CSM PCF directional from worldspawn sun. Compare screenshots day outdoor (`irq*`, `sud*`) vs indoor (`tsr1`, `kos1`).
4. Volumetric fog region raymarch + overlay dim; shafts if CSM present.
5. Overbright option (S, §11) — independent, can land any time after step 0 since it needs
   no FBO.
6. Per-pixel dlights (M, §8) — only after the A/B brightness gate vs `gl_dlightintensity 2.0`.
7. Soft particles (M, §9) — after step 1 (depth texture).
8. Per-pixel region fog (§10) — together with volfog (step 4), shared region shader code.
9. **Video menu: ALL effect settings** (menu stream — see §14). Each effect gets a
   master toggle **and** a quality control where one exists (bloom, volfog, CSM, pdlight,
   soft particles, ppfog, overbright). Do not hide any behind console-only.
10. Vulkan port of the same graph (later).

Do **not** fight `om-wt-engine` / `om-wt-modeb` / menu worktrees. This branch only
touches `engine/src/client/refresh/gl3/**` and `docs/impl/GFX-modern.md`.

All toggles, defaults (ON at high — owner [pt69u]):

| feature | master cvar | ship default | owner | effort |
|---|---|---|---|---|
| bloom (glowing only) | `r_om_bloom` | ON | pt54u | M |
| volumetric fog | `r_om_volfog` (+ quality 2) | ON / high | pt54u | M–L |
| cascaded soft shadows | `r_om_csm` (4×2048) | ON / high | pt54u | L |
| per-pixel dlights | `r_om_pdlight` | OFF (until A/B match) | pt55u | M, gated by A/B |
| soft particles | `r_om_softparticles` | OFF | pt55u | M |
| per-pixel region fog | `r_om_ppfog` | OFF | pt55u | with volfog |
| SoF Buddy-style overbright | `r_om_overbright` | OFF (option, label non-original) | pt55u | S |

## 6. Map / data hooks summary

| hook | where | used by | status |
|---|---|---|---|
| SURF_LIGHT + `value` | `.m32` / texinfo | bloom emissive (default seed) | no runtime self-lit in 1.07f; gatekeeper seed (AMEND-016) |
| flag 0x1000 | `.m32` | not a bloom seed | texture animation only (R16.1) |
| RF_FULLBRIGHT / RF_GLOW | entity flags | bloom emissive | SPEC-411; R16.1 |
| fog regions lump 21 + facepolys 19 | BSP | volfog density + existing overlay | solid (SPEC-410) |
| worldspawn `_sun_*` | entities | CSM direction/colour/ambient | compiler-only in 1.07f; we read them for CSM (AMEND-016). Angle convention still to pin down |
| lump 20 type 3 | BSP lights | optional CSM sun-colour hint only | never Ghoul lights (R16.3) |
| lump 20 types 0/1/2 | BSP lights | Ghoul point/spot (existing); not CSM v1 | solid enough for Ghoul |
| `ghl_shadows` path | Ghoul | fallback when CSM off | solid (SPEC-411) |
| additive FX | client effects | bloom | behaviour OK; exact material list open |
| terrain fog header | `.trn` | volfog include? | open |
| no `fog*` entities | — | — | confirmed absent in stock |
| fog kind (offset 0) | lump 21 | — | unused in 1.07f (AMEND-016) |
| Ghoul self-illumination / additive | Ghoul materials (as drawn) | bloom emissive | R16.7 |
| SoF dlights (`sofdlight_t`, Ghoul lights) | refdef / `GhoulLightExchange` | per-pixel dlights | solid (SPEC-410/411) |
| effect alpha quads (smoke/dust/blood) | `gl3_fx.c` | soft particles | solid (SPEC-412) |

## 7. Questions for R-team (behaviour only — no code from them)

**SPEC-AMEND-016 approved** on main as `d148439` (G, 2026-10-26). Answers:

- **Q1 / R16.1:** 0x1000 = texture animation only, not glow. No self-lit world surfaces in
  1.07f. Bloom seeds (C design, gatekeeper): SURF_LIGHT + Ghoul high self-illum **as drawn**
  + additive materials/FX + RF_FULLBRIGHT/RF_GLOW. Not 0x1000.
- **Q2 / R16.2:** `_sun*` never read at runtime in 1.07f. Convention recovered from lightmaps
  (yaw/pitch travel direction; `_sun`/`_sun_target` → light→info_null; `_sun_vector`). CSM
  reads keys at load. nycdm4 vector mismatch UNVERIFIED; open items remain in OPEN_QUESTIONS.
- **Q2b / R16.3:** lump 20 type 3 never becomes Ghoul lights; optional sun-colour hint OK.
- **Q3 / R16.4:** no realtime sun shadows. CSM ships ON; when on, disable Ghoul blob/mask
  for CSM casters; avoid double-darkening baked lightmap shadows.
- **Q4 / R16.5:** fog kind unused. Volfog from lump 21 fog regions only.
- **Q5 / R16.6:** terrain fog vs region fog never combined in stock; pick one layer.
- **Q6 / R16.7:** Ghoul P4 self-illumination + additive; follow what is drawn.

Original question list (kept for history):

1. **0x1000 textures:** are screens/monitors meant to be self-lit beyond their lightmap
   (i.e. should they bloom)? Any other non-SURF_LIGHT glow rule in 1.07f?
2. **Sun direction:** exact runtime rule for `_sun_angle` (degrees, pitch/yaw order),
   `_sun_vector`, `_sun` / `_sun_target` → entity origin. Does the renderer ever read
   lump 20 type 3 for lighting, or only the compiler?
3. **CSM vs baked:** any evidence the original had realtime world shadows from the sun,
   or only Ghoul silhouettes + lightmaps? (We assume lightmaps only — confirm.)
4. **Fog volumes:** is kind (offset 0: 0/1/2) ever selected in a code path we missed, or
   truly unused (SPEC-410 says unused in 1.07f)?
5. **Terrain fog vs region fog:** when both exist (`trn1`), which wins for objects and
   for a volumetric layer?
6. **Ghoul self-illuminated materials:** any material flag / IFL that draws fullbright
   pieces that should feed bloom?

C proceeds on the gatekeeper bloom seeds + lump-21 volfog + load-time `_sun*` CSM.
Open R items (nycdm4 sun vector, compiler defaults, key precedence) do not block stubs.

## 8. Per-pixel dynamic lights (owner [pt55u], effort M)

### 8.1 Goal

Replace the "square" dlight spots (dlight sampled at **lightmap resolution**, one value per
16-unit luxel, so a muzzle flash lights a blocky patch) with a smooth per-pixel falloff.
Ship default **ON** after the A/B gate (§8.4). User can still pick Off.

### 8.2 Today

`gl3_dynlight.c` (SPEC-410): per lit face, CPU computes Q2-rule light per **lightmap
sample** with SoF constants into an RGBA16F layer on TMU6 (`omDynLight`); the lightmapped
shaders add it to the static sum before rounding / colour table. Ghoul gets the same
lights through `GhoulLightExchange` with `intensity = radius * gl_dlightintensity` (default
**2.0**, clamped to 4). Yamagi's `uniLights` / `passLightFlags` per-pixel loop already
exists in the shaders but is used only for Quake II maps.

### 8.3 Plan

- `r_om_pdlight 1`: skip `GL3_DynLight_Face` CPU fill for SoF world faces (layer stays 0)
  and instead upload the frame's SoF dlights (≤ 32 nearest/brightest, `r_om_pdlight_max`)
  into the existing `uniLights` UBO and set `passLightFlags` from the faces' dlight bits.
- Shader: reuse the Yamagi per-pixel loop, but with the **SoF falloff** of SPEC-410
  (same radius/intensity maths the CPU layer uses), evaluated at the fragment's world
  position instead of the luxel centre. Light is added **before** colour table and **not**
  multiplied by `gl_modulate`, exactly where `omDynLight` is added now, so the rest of the
  lighting pipeline is unchanged.
- Optional `r_om_pdlight_normal 1`: N·L term using the face normal (Q2 rule has a plane
  distance term only). Default 0 to keep brightness comparable.
- Ghoul models keep their own light path (already per-vertex/per-piece); no change.

### 8.4 Gate: A/B brightness vs `gl_dlightintensity 2.0`

Owner condition: ship only after an A/B against the default lightmap layer at
`gl_dlightintensity 2.0`.

- Harness: fixed demo / `timedemo`-style freeze frames on `dm/nycdm4`, tsr1, kos1 with
  muzzle flash, rocket/explosion and flare dlights; capture `r_om_pdlight 0` vs `1`.
- Metric: mean and peak luminance inside the lit area, and total "energy" (sum over the lit
  region) must match within ±5 % (`r_om_pdlight_scale`, default 1.0, is the tuning knob;
  the tuned value goes into the default before shipping).
- Visual: no blocky edges, no hot spot brighter than the old peak, falloff radius equal.
- Until that table is signed off, the cvar default stays 0 (Off); the menu toggle is an option.

### 8.5 Cvars

| cvar | default | meaning |
|---|---|---|
| `r_om_pdlight` | `1` | 0 = SoF lightmap-grid layer, 1 = per-pixel (default ON since fb1; the A/B gate passed) |
| `r_om_pdlight_max` | `32` | max dlights per frame in the UBO |
| `r_om_pdlight_scale` | `1` | brightness match vs the grid layer (A/B tuning) |
| `r_om_pdlight_normal` | `0` | add N·L term (changes brightness; off for A/B) |

### 8.6 Risks

- Light leaking through thin walls (the grid version had the same issue but blurrier):
  keep the face-mark test (only faces the BSP light walk marked receive it).
- Many lights in MP firefights: cap + sort by contribution; fall back to the CPU layer
  above the cap if needed.

## 9. Soft particles (owner [pt55u], effort M)

### 9.1 Goal

Smoke, dust and blood sprites stop showing hard lines where they cut into walls/floor.
Depth-fade near opaque geometry. Ship default **ON**.

### 9.2 Plug-in point

- SoF smoke/dust/blood are **effect alpha quads / quad chains** drawn through the FX batch
  in `gl3_fx.c` (`DrawAlphaQuad`, `DrawQuadChain`, `GL3_FX_Flush`), not the Yamagi
  `GL3_DrawParticles` points (those are Q2-only). The soft path is a variant of the FX
  shader (the one with `texmode / alphatest / fogmode` uniforms).
- Needs the **sampleable depth texture** from §1 (opaque world + Ghoul depth is final
  before `GL3_FX_SortedPass`).
- Fragment: `fade = clamp((sceneLinearDepth - fragLinearDepth) / r_om_softparticles_dist,
  0, 1)`; multiply alpha (for ALPHA blend) or colour (for ADD blend) by `fade`.
- Which quads get it: blend mode ALPHA or ADD and not alpha-tested (`alphatest` off). That
  covers smoke, dust, blood puffs, sparks; decals and alpha-tested grates/foliage are
  excluded (they must stay hard). `r_om_softparticles_add 0` can exclude additive.
- First-person weapon effects (depth-hacked) skip the fade.

### 9.3 Cvars

| cvar | default | meaning |
|---|---|---|
| `r_om_softparticles` | `0` | master; default OFF |
| `r_om_softparticles_dist` | `12` | fade distance in world units |
| `r_om_softparticles_add` | `1` | also soften additive quads |

### 9.4 Risks

- Thin particles on floors become invisible if `dist` too large; tune per SoF scale.
- Fog interaction: point fog on FX (SPEC-410) stays as is; fade only touches alpha.

## 10. Per-pixel region fog (owner [pt55u], effort: together with volfog)

### 10.1 Goal

SoF fog regions today are an overlay of the region's **boundary faces** (per-vertex
thickness, SPEC-410) or `gl_fogmode 1` linear GL fog on interior faces. Per-pixel fog
computes the same region thickness **per fragment** from the depth buffer, so there are no
per-vertex gradients or seams on big faces. Ship default **ON**.

### 10.2 Plan

- Full-screen pass after the opaque world, before the sorted FX pass, using the depth
  texture from §1. For each pixel: reconstruct world position, intersect the view ray with
  the active fog regions (AABB + planes already in `gl3_fog.c`), compute the **same**
  thickness / opacity function as the overlay (start, range, opacity, colour), blend
  "over".
- When on, the classic overlay passes 1/2 and `gl_fogmode 1` interior fog are **skipped**
  for regions handled per-pixel (no double fog). Translucent FX keep point fog.

### 10.3 Relation to volumetric fog — no double application

Per-pixel region fog and volfog (§3) share one shader module (region intersection +
thickness). Exactly **one** owner of the region term per frame:

| `r_om_volfog` | `r_om_ppfog` | region term comes from | classic overlay |
|---|---|---|---|
| 0 | 0 | classic overlay (authentic) | on |
| 0 | 1 | per-pixel pass | off |
| 1 | 0 | volfog raymarch + classic overlay dimmed (`r_om_volfog_overlaydim`) | dimmed |
| 1 | 1 | volfog raymarch only; ppfog is its depth-exact base term (quality 0 of volfog = ppfog) | off |

So `r_om_ppfog` is effectively "volfog quality 0, analytic, no raymarch". Implement as one
code path with two cvars, not two separate fog systems.

### 10.4 Cvars

| cvar | default | meaning |
|---|---|---|
| `r_om_ppfog` | `0` | master; default OFF (volfog owns region term when both on) |

## 11. SoF Buddy-style overbright (owner [pt55u], effort S)

### 11.1 Goal

An **option** for a brighter "overbright" lightmap look like the SoF Buddy community
patch. **Not authentic** (1.07f has no overbright factor; SPEC-410, and today the GL3
code forces `overbrightbits = 1.0` in `gl3_shaders.c`). Ship default **ON** (max graphics);
menu label must say it is non-original.

### 11.2 Plug-in point

- The lightmapped fragment shaders already have `lmTex.rgb *= overbrightbits;` after the
  dlight add. With `r_om_overbright 1` the uniform becomes `r_om_overbright_scale`
  (default 2.0) instead of the forced 1.0; with 0 it stays 1.0 (no change).
- Clamp before colour table so the SoF colour table still maps the result; optional
  `r_om_overbright_models 1` also scales Ghoul/alias light (`passColor` path) so models
  do not look darker than the world.
- No FBO needed. Works with bloom (bloom reads emissive, §2.4) and with per-pixel dlights.
- Not used for the A/B in §8.4 (must be off during the comparison).

### 11.3 Cvars

| cvar | default | meaning |
|---|---|---|
| `r_om_overbright` | `0` | 0 authentic, 1 SoF Buddy-style; option only, default OFF, menu labels non-original |
| `r_om_overbright_scale` | `2` | lightmap multiplier when on |
| `r_om_overbright_models` | `1` | also brighten model lighting when on |


## 12. Stub vs full

So far: **docs + cvar registration (no shaders, no FBO change)**. Commit `bd8c0c2`
registered bloom / volfog / CSM cvars; `9b82ef8` added pdlight / soft particles / ppfog /
overbright; this follow-up ([pt69u]) flips ship defaults to **ON at high** and raises
already-exposed quality cvars (MSAA 8×, anisotropic 16×, trilinear, `r_farsee 1`).
Visual spikes only after coordinator says stability gate is clear enough, and only on
`gfx-modern` with local commits. **Do not deploy a broken menu redesign** with this work.

## 13. Open questions (C)

- Tonemap operator when leaving RGBA16F (Reinhard vs filmic vs keep SoF gamma only).
- Whether bloom emissive should include warp liquids with SURF_LIGHT (rare).
- Mobile/Android later: these effects default off; GLES3 path shares shaders carefully
  (no geometry shaders; PCF via textureGather if available).
- Menu: strings RU/EN for the toggles (bloom, volfog, CSM, per-pixel dlights, soft
  particles, per-pixel fog, overbright marked "non-original") — hand to menu stream (§14).

## 14. Video menu + redesign (owner [pt69u])

**All modern effect settings must appear in the video menu**, not only as console cvars:

| effect | menu controls |
|---|---|
| bloom | toggle (`r_om_bloom`) + intensity/radius (quality) |
| volumetric fog | toggle (`r_om_volfog`) + quality (`r_om_volfog_quality`) |
| cascaded shadows | toggle (`r_om_csm`) + quality (cascades/size/soft) |
| per-pixel dlights | toggle (`r_om_pdlight`) — expose only after A/B gate |
| soft particles | toggle (`r_om_softparticles`) + fade distance |
| per-pixel region fog | toggle (`r_om_ppfog`) |
| SoF Buddy overbright | toggle (`r_om_overbright`) — label **non-original** |

Also keep/expose the stock quality knobs already in the video page (MSAA, anisotropic,
texture filter / trilinear, detail textures, Ghoul shadows, etc.) at their max defaults.

**Menu redesign:** the current left-edge / stretched 640×480 layout is temporary. Owner
wants the menu **redesigned from scratch**, properly, in the original SoF visual style for
widescreen — not another half-patch. That work belongs to the menu stream (`menu-modern`);
**do not ship a broken redesign** to `D:\Games\OpenMerc` or `OpenMerc-menu-test` until
screenshots at 1080p and 1440p have been checked by whoever reports readiness.

## 15. Implementation status and results (27 Oct 2026)

Branch gfx-modern, worktree om-wt-gfx, tested only in `D:\Games\OpenMerc-gfx-test` (ref_omgl/ref_gl3/ref_gles3 replaced, mode A retail game DLLs).

| effect | cvar | default | state |
|---|---|---|---|
| bloom (emissive only) | `r_om_bloom` | 1 | done |
| volumetric fog in fog regions | `r_om_volfog` | 1 | done, intensity 0.4 |
| cascaded smoothed shadows | `r_om_csm` | 1 | done, active only on maps with a sun |
| per-pixel dlights | `r_om_pdlight` | 1 | done, gate passed; ON since fb1 (cheaper on the GPU, energy within 2.6 %) |
| soft particles | `r_om_softparticles` | 0 | done |
| per-pixel fog regions | `r_om_ppfog` | 0 | done (per-fragment, see 15.2) |
| SoF Buddy-style overbright | `r_om_overbright` | 0 | done |

### 15.1 Measurements

1920x1080 windowed, vsync off, vid_maxfps 999, one instance at normal priority on XEONST (PC shared with other work, so the min-fps column is noisy). fps sits at the 999 cap in every config, so GPU ms (timer query around the 3D view + post) is the number that matters. Log: `openmerc-data/shots/gfx/ft.log`, `fd.log`; `python fps.py <log>`.

| GPU ms | tsr1 | kos1a | irq3ba | ger1a | sud3b |
|---|---|---|---|---|---|
| off | 0.18 | 0.10 | 0.13 | 0.15 | 0.20 |
| bloom | 0.27 | 0.19 | 0.22 | 0.30 | 0.27 |
| volfog | 0.21 | 0.51 | 0.50 | 0.51 | 0.51 |
| csm | 0.50 | 0.31 | 0.44 | 0.49 | 0.20 |
| pdlight | 0.19 | 0.11 | 0.14 | 0.19 | 0.19 |
| soft | 0.22 | 0.14 | 0.18 | 0.22 | 0.24 |
| ppfog | 0.17 | 0.10 | 0.11 | 0.18 | 0.19 |
| overbright | 0.18 | 0.10 | 0.11 | 0.19 | 0.16 |
| def (bloom+volfog+csm) | 0.48 | 0.61 | 0.61 | 0.65 | 0.55 |
| all | 0.49 | 0.63 | 0.63 | 0.66 | 0.53 |

Dlight scene (`r_om_pdlight_test 300`): tsr1 off 0.51 / pdlight 0.17 / def 0.57 / all 0.46 ms; kos1a off 0.30 / pdlight 0.12 / def 0.84 / all 0.92 ms. The luxel-grid path pays for lightmap re-uploads every frame, the per-pixel path does not. kos1a fps dropped to 400-700 (min ~70-170) in all four configs of that run: CPU-side and/or the shared PC, not attributable to one effect.

pdlight energy gate (section 8.4): tsr1 test light 400 +0.2 %, 200 +2.6 %, kos1 400 -1.7 % vs the luxel grid at gl_dlightintensity 2.0 (gate +-5 %). Peak is lower at 200 (110 vs 222): the falloff is smoother, the hot spot is gone.

### 15.2 Deviations from the plan

- ppfog is per-fragment inside the existing overlay pass (same polygons, alpha from the fragment world position against the region planes), not a full-screen pass. No double application with volfog by construction; no half-res mode, so `r_om_ppfog_halfres` was dropped.
- volfog: full-screen pass after the opaque world, position rebuilt from the depth copy; up to N nearest regions (each: AABB slab + up to 5 half-spaces from the region planes), 6/10/16/28 steps by `r_om_volfog_quality`, value noise (`r_om_volfog_noise`), sun shafts from the CSM static cascade (`r_om_volfog_shafts`). The per-vertex overlay is dimmed by `r_om_volfog_overlaydim` (0.85) while volfog is on so the region is not fogged twice. Disabled for gl_fogmode 2/3 and night vision.
- volfog intensity default 0.4 instead of 0.6: in fixed-view A/B (irq3b, kos1, sud3) 0.6 and 0.9 lift the dark corners and flatten contrast; 0.4 keeps the mood. Mean luminance change vs off at 0.4: irq3ba +3.2, kos1a +0.9, sud3b +2.2 (0-255).
- pdlight evaluates the SoF dlight rule (SPEC-410: r = intensity - |d|, dist = max + min/2 along the texture axes, cut at r - 64) per fragment instead of a new falloff; the hard 64 edge that the luxel grid smooths bilinearly gets a 16-texture-unit linear ramp. Not inverse-square on purpose: it must match the retail look.

### 15.3 Test aids (not for players)

- `r_om_fog_regions` - prints the fog regions of the map (AABB, opacity, start/range, flags, colour, planes).
- `r_om_viewtest` - free fixed camera for A/B screenshots: `x_y_z_pitch_yaw` or `r_<region>_<back>` (camera inside region <region>); `_` = blank. Only with deathmatch 0 and coop 0 (SoF single player runs with maxclients 16).
- `r_om_pdlight_test <intensity>` - debug dlight in front of the view.
- harness `test/gfx/gen_gfx.py` + `run_gfx.ps1`: `pre;cmds!map#alias@post;cmds`, configs `name=cvar:value`; every config restores the archived tuning cvars.

### 15.4 Known drawbacks

- Ghoul models and translucent FX are not inside the fog volume (no volfog term on them).
- Fog volumes are AABB + planes, so a loose region (irq3b #0/#15 are huge) leaks fog into space the mapper never fogged.
- Region overlap order is ignored; nearest regions win.
- ppfog sets fog state per polygon and breaks overlay batching.
- nyc1 (17 thin ground slabs, rg 10) could not be judged: the tested fixed views landed in solid/void. Needs a playthrough check before volfog stays default-on there.
- shafts/noise had no measurable effect in the tested indoor views (no sun inside the fog regions).
- volfog has no half-res path: ~0.3-0.4 ms at 1080p on this GPU, more on weak iGPUs.

## 16. Owner feedback fb1 (integration 3e3efcb)

- Menu EFFECTS rows set the `r_om_*` cvars live; the renderer reads them per frame, no `vid_restart`.
- `r_om_pdlight` is now ON by default (cheaper on the GPU than the luxel grid, energy within 2.6 %). It is
  meant to look like the SoF grid with smooth pool edges, so it is not a visible "new effect".
- Dynamic lights reach the screen with the owner's exe, DLL and config.cfg on the RX 9070 (tsr1, mode A): a
  muzzle flash raises the mean frame brightness 21.7 -> 56.8. SoF dlights live about 0.1-0.6 s, so they are
  easy to miss. `r_om_dlight_stats 1` prints the frame's dlights (OMDL lines).
- Square shadows: the Ghoul blob (`ghl_shadows 1`) used the SPEC-012 1/2048 cubic texture, which is still
  ~12 % at the quad's edge midpoints, so the blob drew its quad as a trapezoid with hard edges. The texture now
  fades to 0 between radius 24 and 31 texels and is clamped (round, soft blob; centre unchanged).
- With `r_om_csm 1`, the ground blob gives way to the real sun shadow where the ground is in the sun. The
  hand-over now samples the static sun map 3 x 3 (two texels apart) instead of one texel, so the border is soft.
  In shade and on maps without a sun (sud3) the blob stays as the fallback.

## 17. Owner feedback fb2 (integration 8b08614): black squares under corpses

- Cause: the Ghoul ground-shadow textures (blob `g_blobTex`, ghl_shadows 1; mask ring `g_shadowTex[16]`,
  ghl_shadows 2) were created once and their GL names were kept across a renderer restart. Changing the
  video-menu shadow softness runs `vid_restart` (owner's demo: vid_restart at 66 s); in the new context the
  old names belong to other (opaque) textures, so the blob quad darkened its whole rectangle by
  ghl_shadow_darkness: a black square/trapezoid under every body. With ghl_shadows 2 the mask upload went into
  a foreign texture name and could overwrite a world/effect texture (jpn1 light shaft turned black).
  r_om_csm_soft itself was not involved: soft 1 and 2 behave the same until vid_restart.
- Fix: `ReleaseShadowTextures()` (ghoul_draw.cpp) deletes and forgets them from `ReleaseAllTextures()`
  (system method 3, called by the client on every renderer restart while the old context is alive).
- Verified 2560x1440 with the owner's config, int-test binaries vs the fix: jpn1 (moon), kos1 (tunnel),
  tsr1 (subway), irq1a (sun 140): black rectangle after vid_restart before, normal soft blob after.
- "No shadows at soft 2" on jpn1: not a soft-2 bug. jpn1's sun is a moon (`_sun_light 50`, CSM strength
  0.65 x 0.5); r_om_csm_debug shows the streets at the start and all interiors (where the owner fought) get no
  moonlight, so CSM has nothing to darken there and only the Ghoul blob remains (partly hidden under the body).

## 18. fb3: visible character / corpse contact shadows

Owner saw almost no ghoul ground shadow (t105u). Cause was not CSM: on tsr1 (no sun/moon in the static map, debug view all blue) `omCsmGhoulShadowKeep` already returned 1. The blob itself was weak:
the SPEC-012 cubic profile has mean alpha ~0.4 and its dark core sits under the body, and the quad is fitted to the silhouette mask, so little shadow shows outside the model.

Changes:
- Blob texture: full core to r 9 of 32, smoothstep to 0 at r 31 (no visible edge).
- Blob quad (mode 1 only) enlarged about its centre: half-extents max(1.5x, 0.9 x larger half-extent), so thin silhouettes still get a round contact patch.
- `omCsmGhoulShadowKeep`: with replace_ghl the blob is no longer removed in sun; it keeps >= 30% in full sun (scaled by the light-adjusted CSM strength, ~65% under the jpn1 moon), as a soft contact shadow under the CSM caster shadow. Indoors (no static sun) keep stays 1.

Measured (shadows on vs ghl_shadows 0, owner config, 2560x1440): darkened area grows 1.2-2x on tsr1/jpn1/irq1a, core ratio ~0.25-0.35 (darkness .75), mean ~0.7: visible, not black.
Shots: box /workspace/gfx_check/fb3/*_zoom_before_after_off.jpg (before | after | shadows off).

Drawbacks: in sun the 30% blob overlaps the CSM shadow at the feet (slight double darkening); the larger flat quad at ground Z can spill onto an adjacent step or wall base; mode 2 (mask) unchanged.

## 20. fb5: HD texture pack (sofhd2024.pak) and the "black doorway"

Report: release 0.2.0 with sofhd2024.pak (4316 replacement files, same names as retail:
textures/*.m32 and ghoul/*.tga, mostly 4x, some 2x, aspect never changed).

* Cause: world/warp/2D texture coordinates divide by image->width/height, which came from the
  HD m32 header, so every HD wall was stretched 4x (sof04 stone, green door slab).
* Fix (engine common + gl3): FS_ReadOriginalHead() reads the header of the lowest-priority copy
  of a file (retail pak); gl3 uses those dimensions for coords and samples the HD pixels
  (yquake2 r_retexturing logic). Only with r_retexturing != 0. refimport API_VERSION 8 -> 9;
  omgl (SoF import) is unchanged and gets no fix.
* Ghoul skins: upload was capped at 256 (SoF rule); new cvar ghl_om_maxtexsize (2048).
  Visible gain is small: most HD character skins are 512 wide.
* "Door opens onto void / passage shorter": not an areaportal bug. Door *17 (areaportal t1)
  opens and the room beyond renders; the flat dark green "void" in sof03/sof04 is the closed
  door texture stretched 4x.
* Drawbacks: a mod that ships a deliberately different-size replacement keeps the retail scale
  (r_retexturing 0 restores old behaviour); one extra small read per texture load; more VRAM.

## r_om_ssao: ambient occlusion (GTAO)

gl3_post.c, section "r_om_ssao"; called from GL3_RenderView after the sorted pass, before the
light beams and the composite (needs the post path, which r_om_ssao turns on by itself).

* The finished depth is copied (texdepthcopy) and turned into view distance at half resolution
  (R32F). GTAO (Jimenez 2016 / XeGTAO): per pixel 2-4 screen directions rotated by interleaved
  gradient noise, 4-6 steps each way within r_om_ssao_radius world units (falloff over the last
  61.5 %), horizon angles clamped to the hemisphere of the normal rebuilt from depth (smaller
  depth step per axis), cosine-weighted visible arc. Origin moved off the surface by
  0.004 * distance + 0.25 (24-bit depth steps at grazing angles were false occluders); samples
  beyond the view edge take the edge pixel (ignoring them faded the occlusion out towards the
  edges while turning); no distance fade (it made the occlusion build up while walking closer),
  only the far plane (sky) is skipped. Occluders that have left the view still cast nothing:
  that is the screen-space limit (baked occlusion in omlight would cover the static world).
* Two depth-aware 9-tap blurs (RG16F: visibility, distance), then a depth-aware bilinear
  upsample multiplies the scene colour (blend ZERO, SRC_COLOR) of the MSAA framebuffer, or of a
  framebuffer holding only the scene texture without MSAA (the emissive texture is read).
* Left alone: the view weapon (stencil bit 0x80 written while SOF_RF_WEAPONMODEL Ghoul entities
  are drawn, the stencil is now cleared with the depth in GL3_Post_BeginView), the sky and far
  pixels, light sources (visibility -> 1 by the emissive target's brightness x 4).
* Cvars: r_om_ssao 1.5 (exponent of the visibility, 0 = off), r_om_ssao_radius 48,
  r_om_ssao_quality 3 (1: 2x4, 2: 3x5, 3: 4x6 directions x steps), r_om_ssao_full 0 (1 = full
  resolution), r_om_ssao_debug 1 (occlusion only). Not in GLES3 builds.
* Cost (tsr1, 1080p, GTX 1050 Ti, timerefresh): about +1.6 ms at quality 3 (365 -> 230 fps).
* Translucent effects in front of occluded places (smoke in a corner) are darkened with them:
  SoF draws Ghoul models and translucent items in one sorted pass, so there is no point where
  only the opaque view is finished.
* Later additions: temporal accumulation (r_om_ssao_temporal 0.85: the noise rotation moves every frame, a
  half-resolution pass reprojects last frame's result with last frame's projection * view and this view's rigid
  inverse, keeps it where the reprojected distance agrees within 4 % + 1 unit) and multi-bounce (r_om_ssao_bounce
  0.6: Jimenez 2016 polynomial per channel, albedo guessed from a half-resolution copy of the scene colour, its hue
  at a moderate brightness). Cost now about +1.8 ms.

## r_om_hdr: float scene, filmic curve, exposure

gl3_post.c. The scene target is RGBA16F (MSAA renderbuffer too); every wrapped 3D fragment shader clamps its alpha to
0..1 and its colour to 0..64 (an 8-bit target clamped them; out-of-range alpha would break blending).

* Composite: scene (+ bloom) -> linear (pow 2.2, the shaders output display-encoded colour) -> x exposure -> ACES
  filmic fit (Narkowicz) -> saturation r_om_hdr_saturation 1.1 -> pow 1/2.2.
* Exposure: log luminance of the view into 64x64 RG16F (centre-weighted, weight in G), mipmapped to the mean;
  a 1x1 R32F ping-pong eases towards neutral * clamp((r_om_hdr_key / mean)^r_om_hdr_adapt_strength, r_om_hdr_min,
  r_om_hdr_max) at r_om_hdr_adapt per second. neutral = 0.72 / overbright^2.2 (ACES(0.13) ~ 0.18 keeps mid grey).
  Defaults key 0.1, strength 0.35, min 0.7, max 1.6: dark places are lifted a little, the mood stays.
* r_om_hdr_overbright (default 1) scales the lightmaps for headroom above white; above 1 the sky and unlit effects
  get darker by the compensation, so it is off by default. r_om_hdr_debug 1 prints the exposure every 30 frames.
* Off with night vision and r_fullbright. About +0.3 ms (1080p, GTX 1050 Ti).

## r_om_ssr: screen-space reflections, wet ground under the rain

gl3_post.c, section "r_om_ssr"; material side in gl3_shaders.c (omMaterial, omWet, spliced into the lightmapped
world shaders). Called from GL3_RenderView after r_om_ssao, before the rain and the light beams.

* Material target: a third colour attachment (RG8; MSAA renderbuffer + resolve framebuffer), made only while
  r_om_ssr > 0. The fragment wrapper gives every 3D shader an outMaterial output defaulting to (0, 0, 0, a): opaque
  models clear it, translucent surfaces and fog fade it with their alpha. The opaque lightmapped faces write
  reflectivity and roughness x reflectivity (premultiplied, so blending keeps the ratio). Passes that modulate faces
  already drawn (FX blends DST_COLOR: detail/environment textures, marks) are masked off with glColorMaski.
* Reflectivity by GL3_Mat_SurfClass, which now has two more classes: 6 loose ground (sand, dirt, gravel, snow,
  grass, hay, mud: none), 7 liquids (water, slime, blood, tar: 0.4 on floors). Stone/unknown floors 0.13, wood floors
  0.1, scaled 0.3..1 by how smooth the texture is (difference of the texel and the texture at LOD bias 3); metal
  0.16 everywhere, glass 0.45; walls and ceilings of stone and wood none.
* Rain (r_om_rain active, top-down map valid): faces open to the sky (4 taps of the rain occlusion map, the point
  moved 4 units off the face) are wet: floors 0.16, puddles 0.5 from two octaves of value noise (sharp edges),
  roughness 0.3 / 0.03; the diffuse colour is darkened by up to 25 %. r_om_ssr_wet scales it.
* Tracing at half size: position and normal rebuilt from the depth copy (shared with r_om_ssao when it ran), Schlick
  towards grazing angles (at most 3x head-on, less on rough faces), perspective-correct march over the on-screen part
  of the ray (16/24/40 steps by r_om_ssr_quality, quadratic spacing, interleaved-gradient jitter, thickness
  max(4, 1.5 % of the distance) + the step), 5 bisection steps. Colour from the last finished frame: GL3_Post_EndView
  blits the resolved scene to a half-size mipmapped RGBA16F texture, the hit is reprojected with last frame's
  projection * view (no copy of the unfinished view: that copy cost about 0.8 ms with MSAA). LOD from roughness x
  the way to the hit. Fades at the screen edges, at r_om_ssr_dist, for rays coming back at the eye.
* Composite at full size over pixels whose full-size material is reflective, premultiplied (ONE,
  ONE_MINUS_SRC_ALPHA): mirrors (reflectivity >= 0.45) replace the scene by the reflection, dry floors and metal
  mostly add it (a dark wall reflected in a floor must not darken the lightmapped floor; a lamp makes it shine).
  Mirror rays that hit nothing take the blurred colour where they leave the view (puddles were dark stains).
* Cvars: r_om_ssr 1 (strength, 0 = off and no material target), r_om_ssr_wet 1, r_om_ssr_dist 1500,
  r_om_ssr_quality 2, r_om_ssr_debug 1 (reflection weight), 2 (material target: red reflectivity x 3, green
  roughness). Not in GLES3 builds (the shaders still declare the output).
* Cost (arm1 street in the rain, 1080p, MSAA 2x, GTX 1050 Ti, r_om_gputime): about +1.8 ms (5.5 -> 7.3 ms):
  0.6 the world shaders and the extra target, 0.6 tracing, the rest resolves, apply and the colour of the frame.
* Screen-space limits: what is not on screen is not reflected; Ghoul models (cars) write no reflectivity.
* Follow-up (owner: "floors differ: wet catacombs in kos1, metal, mirrors"; "sort the textures that can shine on the
  fly"): GL3_Mat_SurfClass first looks at the texture's file name once per image (gl3image_t.omgloss) - SoF names
  are telling - and adds classes 8 polished stone (marbl, bathroom, pooltile, lobby_floor, _tile ...), 9 polished metal
  (brushed, chrome, metal_clean, metal_smooth, _sink), 10 rusty/dirty metal, 11 mirror (with material 37), 12 damp
  stone (sewer, mildew, gutter, wet, drain); "water" overrides the material (kos1 water4b is "mud"). Damp faces are
  wet without rain (r_om_ssr_damp 1): floors with puddles, walls glisten.
* Lamp glints (r_om_ssr_glint 1): the four nearest map lights (lump 20, gl3_lamps.c) give a normalised Blinn-Phong
  highlight on reflective faces, sharpness from the roughness, Fresnel from the reflectivity, gated by the lightmap
  (no shadows). Cost on kos1 (almost everything damp): +0.36 ms; r_om_ssr total there about 2.1 ms.
* Rain detection fix: "rain" must start a word ("gb_rain", "CityTrafficRainy"); "train"/"drain" had switched the rain
  on in tsr1, tsr2, trn1, kos1, nyc2, sud1, ger1 (drops on ceilings). Same rule in fx_system.c (it also dropped the
  gore effect environ/bloodrain). Splash rings take the highest occlusion height under the ring and skip uneven
  ground (a ring under a recessed ceiling panel showed in an arm1 room); drops are open only within 1 unit.
  Lightning: the openness walk goes on to ~520 units along the light through the top-down map as a height field.
* Mirrors are not real yet: screen space cannot show what is behind the eye; they need a planar reflection pass.
* Follow-up 2 (owner: "seams between textures are visible, balance it so it looks natural"): the material target
  is RGBA8 (reflectivity, x roughness, wetness, light = lightmap luminance in alpha; opaque world faces are drawn
  without blending). A half-size pass smooths it over r_om_ssr_smooth (32) world units, horizontal then vertical,
  9 taps, only between samples on the plane of the centre pixel (floor with floor, never the wall or a character),
  radius at most 8 % of the view. The trace and the apply read the smoothed material. Wet darkening and the lamp
  glints moved from the world shaders into the apply pass, so they fade across seams too; the apply upsamples the
  half-size targets depth-aware (no halo on a character in front of a shiny floor). Glints: eight nearest lamps
  (four cut off the wall lights of kos1), scaled by the light the face receives (lightmap luminance x 1.6) instead
  of an on/off gate, capped lower: soft sheens instead of spotlight blobs on tiles. Cost on kos1: about 3.0 ms in all
  (glints 0.5).
* Follow-up 3 (owner: "stripes in the reflection on the floor", kos1 channel): the march refines every crossing
  behind the depth before judging the thickness (coarse far steps that jumped deep behind a wall were misses: rows of
  hit and missed bands); mirrors fade a hit into the colour a miss takes instead of to nothing; the reflections are
  accumulated over frames (r_om_ssr_temporal 0.85: last frame's result at the surface point, reprojected, clamped to
  this frame's 3x3 range); smooth level faces ripple a little (r_om_ssr_ripple 1, animated by world position), and
  liquids/puddles are a bit rougher, so the arch ribs over the water reflect softly instead of as sharp lines.
  NaN guards: normals from a degenerate depth neighbourhood, NaN/Inf dropped in every r_om_ssr pass and in the HDR
  luminance (a NaN fed back through the last frame's colour and spread as black blotches).
* Follow-up 4 (owner: "everything shines the same; less shine on walls, floors as they are"): outside mirrors and glass, walls get 0.3 of their class reflectivity, ceilings 0.15 (blended by the floor factor); damp walls 0.04 instead of 0.12. Floors unchanged.

### Materials (gl3_material.c, materials/om_materials.txt)

Owner: floors that cannot reflect did (unknown stone floors got 0.13 by default), "sort the textures carefully",
dark parts of concrete and brick must not reflect, a flexible per-material setting; FPS drops.

* Inventory of the 3401 world textures of all maps (scratchpad tooling, not shipped): area on floors/walls/ceilings,
  SoF surface material, the artist's detail texture and the share of rust colours. Two artist hints proved telling:
  the detail texture name (d_stonerough, d_stonecracked, d_stucco, d_metalrough, d_metalsmooth, d_woodsmooth, d_tile
  ...) and an environment map as detail (574 textures, mostly metal objects, glass, signs). Sorted offline from name
  words, material, detail and rust share, checked on contact sheets per class (marbfloor was missed by "marbl", metal
  "clean"/"tile" panels were tiles, shelves with goods were furniture, castle/woodfloor1 has a d_metalribbed detail).
* Classes (fixed numbers, the shader indexes two vec4 arrays): 0 unknown, 1 stone (concrete, brick, asphalt, plaster:
  matte), 2 wood (raw), 3 metal (painted/plain, 0.07), 4 glass, 5 picture, 6 ground, 7 liquid, 8 polished (marble,
  0.28), 9 chrome, 10 rusty (matte), 11 mirror, 12 damp (wet without rain), 13 smoothcon (cement slab, garage floors,
  0.04), 14 varnish (0.10), 15 tiles (0.15), 16 ice, 17 fabric (never puddles), 18 plastic, 19 sky; up to 4 more.
  Per class: floor reflectivity, roughness, wall and ceiling factors, gloss map weight, stain weight, natural
  dampness, rain water held (soak: under 0.5 no puddles, 0 only darkens). The default is matte now: of the floor
  area only polished, tiles, varnish, smoothcon, metal, chrome, ice and liquids reflect dry (about 17 %).
* The table: "class" lines, then "tex <texture> <class>" for every texture ('*' patterns allowed, later lines win).
  A loose copy in the game folder overrides the pak; r_om_materials_reload re-reads it in the game; r_om_material
  prints the face under the crosshair (texture, SoF material, detail, rust share, class and its values, table or
  rules). Textures missing from the table (mods) go through the same rules in short (MatFallback).
* Material map per world texture, made when it loads (GL_TEXTURE14, RGBA8, at most 256 wide, mipmapped): R gloss -
  texels darker than their wide (1/16 texture) and narrow (1/48) surroundings by more than their local spread
  (grout, cracks, grime blotches) and gritty patches (fine change above the texture mean) are dull, normalised so
  the cleanest 15 % keep the class value; G height from the relative brightness (rain water gathers in grout and
  cracks first); B rust/dirt colours (red > green > blue, saturated). The shader: reflectivity x smoothstep(0.08,
  0.55, gloss), roughness + 0.45 x (1 - gloss); stains weigh by class (metal fully, rusty and ground not).
* Mirror replacement only for class 11 (classes above 11 would have matched the old "< -10.5" test).

### Frame time (owner: "strong FPS drops")

* r_om_gputime 2: GPU (timestamp queries) and CPU time of each part of the frame (GL3_Post_ProfMark), mean and worst
  over 200 frames ("OMPROF"), frames over twice the running mean ("OMSPIKE") and whole frames over twice the mean
  including the game ("OMHITCH"). Toured 11 maps at 1080p, MSAA 2x, GTX 1050 Ti while turning and walking.
* Volumetric fog was the heaviest: 4.6 ms mean, 9.3 worst on ger1, up to 6-7 ms on nyc1/kos2. r_om_volfog_half 1:
  marched at half size into an RGBA16F target and brought up depth-aware (four taps weighted by depth agreement, the
  nearest in depth when none agrees; it also fades the material target now). ger1 4.6 -> 1.6 ms, nyc1 2.5 -> 1.5,
  kos2 1.2 -> 0.7; mean difference to the full-size march 0.4-0.6 / 255. Its program is compiled at map load
  (GL3_Post_MapLoad): compiling it when the first fog came into view stalled kos1 for 50 ms.
* SSR: the matte default removes most traced pixels; tracing only from reflectivity 0.015 up, half the steps on
  rough faces, a checkerboard of traced pixels alternating per frame filled by the temporal pass (r_om_ssr_checker
  1), five smoothing taps instead of nine (matte, dry pixels skipped), the apply pass leaves untouched pixels at once
  and computes lamp glints from reflectivity 0.06 (tiled walls cost 1 ms of glints at full size). tsr1 3.5 -> 2.2 ms,
  sud1 3.1 -> 0.5, kos2 1.5 -> 0.5, jpn1 2.5 -> 1.1; ger1 rose 1.8 -> 2.5 (its marble floors are polished now).
* SSR stability (2026-11-02, owner: "SSR keeps noising in stripes, edges flicker, tears at grazing angles"): the
  trace's normal, rebuilt from the half-size depth, wobbled at grazing angles and each wobble moved the reflection
  (jagged torn rows on the jpn2 office floor in motion). A normal within ~10 degrees of a world axis is now that axis
  (map faces are level or upright). r_om_ssr_checker defaults to 0: the filled half zipped along reflection edges.
  A/B in motion at omsetpos -930 948 -1205 -336 4 (jpn2, `omfire officelt`): straight, steady reflections.
  The apply pass blurs the reflections by as much as the trace resolution is reduced (owner: "50 % less resolution,
  50 % blur"): 3x3 depth-weighted taps one trace pixel apart at half size, none at full size; the stair edges of the
  upsampling are gone, nothing bleeds onto the objects standing on the floor.
* SSR replaces the old shine (2026-11-02, owner): with r_om_ssr on, the environment-map detail pass is skipped on
  world faces that SSR reflects (>= 0.045, gl3_detail.c via GL3_Mat_SurfReflect), and liquids lose their
  sphere-mapped second pass. Faces with an environment detail texture reflect at least like gloss plastic (enamel
  signs, news stands, jpn2 tech walls); screens and pictures (SoF materials 32/38/39/40) stay flat and keep the old
  shine, and so do Ghoul models (no SSR on them). tsr1 omsetpos 960 832 -195 180 0: the poster's white env sheen gives
  way to a faint reflection of the opposite wall.
* Lightning leaks closed (2026-11-02, owner: "lightning leaks in the corners"). r_om_lightning_test (test aid):
  1 keeps the last strike's light on, 2 shows only the direct term (red), 3 only the light from above (green); with
  r_om_lightning_rate 50 for a strike at once. Found on jpn2 (lobby -740 1678 -1115, conference room 323 968 -1131,
  office -930 948 -1205): (a) direct: 3 units of normal offset + up to 5 units of depth bias lit every face within
  ~7 units under a ceiling, and PCF taps that fell behind a map's outer wall (no face there) saw open sky - now about
  a unit of offset and bias (1.5 at most) and lit only when all nine taps see the channel; (b) from above: the
  top-down texel a wall runs through holds the street on its other side - wall points move 1.5 texels into their room,
  the nearest occluder of 3x3 texels decides, walls only where nothing is over them (the slope tolerance is for ground
  and roofs). Indoors nothing is lit now; arm1 omsetpos -288 -48 200 90 10 keeps the lit courtyard with its shadows.
* Metal reflects visibly: class metal 0.14 on floors, walls x0.6 (was 0.07 and x0.3, about 0.02 on walls: nothing to
  see, and the env shine stayed); rusty metal stays matte.
* Open: CPU hitches of 20-45 ms a few times a minute (sud1, arm1) inside the Ghoul library while it evaluates prop
  instances (SOF_RF_GHOUL without shadows) in Ghl_Bounds / DrawLevel / Shadow - not GL submission (0 ms), not chunk
  decoding (never over 2 ms, a 64 MiB chunk cache changed nothing), not texture uploads, not CSM, tessellation,
  Ghoul shadows, texture stacking or the light method.
* Hitches found and closed (2026-11-03, owner: "there are drops"). The perf tour (turn in place, walk; arm1, sud1,
  jpn2, nyc1, tsr1, ger1) showed 29-42 ms frames of two kinds:
  - the first lightning strike of a map: the sky mask (sky images read back to the CPU, WXSkyMask), the extra caster
    geometry and the strike's 2048 shadow target were all made then (arm1 "weather" 27 ms CPU, jpn2 "setup" 30 ms).
    They are made at the first frame of a stormy map now;
  - a prop's first draw: timing each Ghoul instance draw (a temporary OMGHLSLOW print) named them - the caged lamp
    23-26 ms, the burning barrel 20, the alley lamp 10, all in DrawLevel. Object preparation decoded the textures
    but uploaded them (the mip chain built on the CPU) at their first bind; a client or menu object uploads them in
    Prepare now (map load). The earlier "not texture uploads" was measured with the original textures, not HD ones.
  After both: no frame over twice the mean on nyc1, arm1, tsr1, jpn2 (two strikes).

* Windows (owner: "SSR on houses does not find the window edges, the whole window reflects with sill and frame",
  tsr1 intro): even surface material 37 is mostly a painted facade (brick, frame, sill, curtains around the glass).
  The material map got an A channel, the panes: texels with no strong step to a neighbour within one texel, little
  detail in their 3x3 window and no red-brown (brick, wooden frame) texel next to them, softened by one texel;
  measured at most 128 wide so HD replacements get the same mask. The glass class (detail weight 1) shines only
  there. Frames, bars, sills and walls are matte now; panes behind curtains or heavy cracks mostly stay matte too.
### Mirrors in motion, coronas (owner: "mirrors vanish when moving"; "turn the coronas off, use the new technology")

* Mirrors: one plane per frame, the plane of the nearest mirror face. In the tsr1 restroom a step towards the side wall
  made its mirror the nearest and the three mirrors over the sinks went blank. Now up to two planes per frame
  (textures on units 13 and 15, one shared depth buffer), chosen by the share of last frame's view their faces took
  (area x facing / distance^2, summed per plane); a plane drawn last frame keeps its place unless another scores
  1.5 times more. Verified strafing in the restroom: both walls keep their images in every frame.
* r_om_coronas 0 (default): the painted halos of the Ghoul lamps (textures named glow*: comskin/glow1, glow3,
  lamp_hang/glow_spike, military_door_outside/glow - 22 lamp models, also the police car siren) are marked by the
  Ghoul system (OM_BEAM_HINT param 2, ghoul_texture.cpp) and drawn into the bloom source only (scene colour and
  material masked): the bloom makes the halo. Without bloom (and on GLES) they are not drawn. 1 = the original sprites.

## r_om_look: colour grading (gl3_look.c, looks/om_looks.txt)

Owner: "gamma and colour filter: the atmosphere of The Darkness - gloom"; "do it properly, take the existing colour
correction away and put ours in its place".

* Before: SoF's colour table (vid_gamma, vid_brightness, vid_contrast; SPEC-413 §9) baked into every texture and
  lightmap at load, in 8 bits (banding, restart per change), then r_om_hdr's exposure, shoulder, saturation and
  shadow gamma. Now (r_om_look 1) the table is neutral (0, 0.5, 0.5) and the composite pass grades the linear scene
  in float as a film camera (2026-11-10, the MGS5 / Fox Engine way, replacing the "Gloom" looks): auto exposure ->
  grade exposure + r_om_hdr_brightness + menu brightness (+0.3 EV per 0.1 above 0.5) -> white balance (luminance
  kept) -> saturation (r_om_hdr_saturation x grade) -> contrast around mid grey -> ACES RRT+ODT (Hill's fit, input
  x1.4619 so mid grey 0.18 stays 0.18) -> menu gamma (v^(1-g)) and contrast (SoF's S power k) as the table had them
  -> brightness lift of lit dark tones -> optional LUT (strip N*N x N, looks/neutral_lut.png to grade in an editor)
  -> vignette, film grain stronger in the dark, dither.
* r_om_camera 0..1 (menu Colour: Film Camera) blends from the plain picture (the shoulder curve, original mid tones)
  to the camera. Grades per map from looks/om_looks.txt ("grade <name> <ev> <temperature> <tint> <saturation>
  <contrast> <vignette> <grain> [lut]" and "map" lines, '*' patterns, loose copy overrides the pak,
  r_om_looks_reload), r_om_look_preset forces one. Grades: standard (default), city (tsr, nyc, arm, tut, trn),
  kosovo, desert (sud, irq), siberia, castle (ger), japan (jpn). Removed with it: r_om_look_strength (Gloom), the
  split toning and toe/shoulder looks, the "adapt" share, r_om_hdr_curve and the Narkowicz ACES of the r_om_look 0
  path (that path keeps the shoulder). r_om_look 0 brings the old table and HDR grade back (renderer restart).

## r_om_probes: reflection probes behind SSR (gl3_post.c)

* Local cube maps (Fox Engine): made lazily around the eye - a 256 x 256 x 128 cell without a probe gets one at its
  centre (the eye's height; the eye when that is solid), 128 px R11G11B10F, one face per frame, world and brush
  entities from the probe's own PVS, held back up to 30 frames while dynamic lights burn. 16 kept, the farthest is
  reused. The box for the parallax correction: 8-unit steps along the six axes until solid (up to 4096).
* The SSR trace binds the four ready probes nearest to the eye (units 24..27) and blends them per pixel by how far
  the point is inside each box (fade over 96 units outside). Rays that leave the view, turn to the eye or reach the
  sky read the probes; hits fade into them at the screen edges instead of to nothing (a mirrored lamp no longer
  vanishes when it leaves the screen). Roughness picks the mip (1 + 5 x roughness). Off in GLES builds for now.

### Characters in mirrors

* The mirror passes draw the Ghoul models too (characters, props and the player's whole body; not the view weapon),
  with the winding turned round (glFrontFace CW); in a mirror image there are no ground shadows, no sun/beam casters
  and no second painted light cone (gl3_ghoul_mirror). Verified in the tsr1 restroom: the player stands in the middle
  mirror.

## Water reflections, anti-aircraft fire, distant storm

* Liquids (gl3_liquid.c, drawn with the FX shader) write the material target through the new FX uniform omFxMat
  (reflectivity 0.5, roughness 0.05); a translucent liquid writes its depth while r_om_ssr is on, so the trace starts
  at its surface. Mirror-like faces (reflectivity >= 0.2) also reflect the sky where the ray reaches it.
  r_om_ssr_debug 4 shows the reflected colour.
* r_om_aa 1 (strength, 0 = off) on the maps of r_om_aa_maps ("kos1 kos2 kos3"): 5..7 guns 4500..8000 units around the
  player (placed again after he has gone 5000 units) fire bursts of tracers into the sky - per gun its own rhythm,
  sweep and colour (orange, red, green), rounds climbing at 2300 u/s and bending down, 18 % ending in a flak burst
  (a growing, fading ball). One draw of 6 x 40 x 8 vertices from gl_VertexID, additive into the scene and the
  emissive target (bloom), the world in front hides them by depth.
* r_om_storm_maps ("trn1"): lightning without rain - bolts 6500..8000 units away, low over the horizon, the sky not
  darkened, the world lit at a third of the rain strength; thunder as with the rain.
* r_om_mirror 1 (gl3_post.c section "r_om_mirror"): planar mirrors. World faces of gloss class 11 note their plane
  while drawn (gl3_surf.c GL3_DrawGLPoly); the nearest one facing the eye is used in the next frame: before the world
  of the view, the world is drawn again from the eye mirrored in that plane (view x reflection matrix, culling
  flipped, Lengyel oblique near plane 0.5 units in front of the glass, the real eye's PVS, the walk's queues for the
  sorted and fog passes dropped afterwards) into a window-sized RGBA16F texture (unit 13). The lightmapped shaders
  replace faces on that plane by the texture at gl_FragCoord (92 %) and write no SSR material there. World only - no
  models or effects in the mirror yet; mirrors on other planes keep SSR. Cost (tsr1 restroom, 1080p): +0.7 ms.

## Snow and the war around kos

* r_om_snow 1 (density) on r_om_snow_maps ("sib1 sib2 sib3"; the maps have only wind sounds, no snow effect):
  14000 flakes in world-fixed cells wrapped around the eye (900 x 700 units), falling 50..90 u/s, each swaying, all
  carried by a wind whose drift is integrated on the CPU (a slow-turning breeze with gusts up to ~175 u/s); only above
  the first surface of the rain's top-down map, so none indoors or under overhangs. Soft round sprites lit by the
  light probe at the eye (never below 0.65). Flake hashing uses an integer hash: the sin() hash of the rain repeats for
  high particle numbers on the GPU and made columns of flakes.
* r_om_war 1 (rate) on the r_om_aa_maps: a distant explosion every 3..9 s 4500..8500 units away (a fireball that
  swells and cools, additive into the scene and bloom; the sky tinted orange for a moment); three or four fires on
  the horizon (flickering glows) with smoke columns (an upright quad turned to the eye, widening and bent by the wind,
  domain-warped fractal noise rolling upwards, the foot lit by the fire); ash drifting down (the snow pass, dark grey,
  sparse). Each explosion is reported in r_om_war_boom ("number distance"); the client (cl_view.c CL_WarBoom_Frame)
  synthesises three booms once (sound/om/boom0..2.wav: a low thump falling in pitch and a short rumble) and plays
  them after distance / 1200 s from a queue (explosions come more often than the sound takes).
* Cost: sib1 snow and kos2 war each under 0.3 ms (1080p, GTX 1050 Ti).
* Snow follow-up (owner: "it hits you in the face, it must be fine particles", "and into the distance"): flakes are 0.5..1 units, kept between 0.8 and 1.6 pixels on the screen, none closer than ~45..110 units; three layers in depth, each fading in where the nearer one ends (box 700 / 1800 / 3600 units, 12000 / 16000 / 14000 flakes), so the fall reaches into the fog.

## jpn2 thunderstorm, rain drops, lightning light (owner: "rain with a thunderstorm on jpn2, not over the skyscrapers, Crysis-3-like drops, no light leaks, strictly physical, remove the old flashes")

* r_om_rain_maps ("jpn2"): rain and lightning on maps whose data has none (WX.forced). The sky is left as it is (no
  darkening, no sky flash: the old whole-sky flash, the fullscreen flash pass and its uniforms are removed).
* Drops: two layers around the eye (near and dense 40..500 units, far and thin 380..1200; nothing reaches the distant
  towers). Each streak is round across (thick in the middle), with a head and a thinning tail, fades over 24 units
  where it meets the world (depth copy), refracts the last frame's colour shifted across the streak and takes the
  light of its surroundings from a blurred level of that colour (bright by the neon, nearly invisible in the dark).
  Without r_om_ssr (no last-frame colour) the drops fall back to their tint.
* Lightning light: the channel is kilometres away, so its light arrives in parallel. Per strike one orthographic
  shadow map (2048², 4096 units around the eye, static world and brush entities, about 1 ms) along the direction to the
  channel; the world shaders add N.L x a 3x3 PCF test of that map, plus 0.22 of a strictly top-down sky term (the
  rain's occlusion map at the point; the old sideways openness walk leaked light under roofs). Unit 9 (shared with the
  beam array, never at the same time).
* Where the bolt is drawn: only over pixels of the sky box (linear depth beyond 30000; the box is at 37800), depth test
  off, so no tower of the map is ever crossed. jpn2 paints the whole street beyond the map on its sky box (tok2, towers
  up to 40..70 degrees), so a sky mask is built once per map from the sky images themselves (WXSkyMask): a direction is
  open where its texel has the colour of the sky (mean of the up face, per channel within 4 + 0.6 x the reference) and
  nothing painted lies above it up to the zenith; eroded by about 3 degrees aside and 1.4 up and down. 256 x 64 (yaw x
  elevation), R8 texture; the bolt shader discards fragments whose direction is not open.
* Where it strikes (rain maps of r_om_rain_maps): every yaw whose column fits is listed - top (32..52 degrees) open and
  at least 5 of 13 samples down to the bottom (12..22 degrees) open; lower down the channel passes behind the painted
  skyline. A column within 60 degrees of the view is preferred. r_om_lightning_zones ("map:yaw-yaw,yaw-yaw ...", world
  yaw in degrees) overrides the search with stretches chosen by hand. No column: the strike stays in the clouds (light
  and thunder only). Bolts 9000..11000 units out, width scaled with distance.
* Sound: thunder as before (r_om_lightning_strike, cl_view.c). The downpour: r_om_rain_audible (set by the renderer
  while it rains) makes the client synthesise sound/om/rain.wav once (band-limited hiss, drop patter, faint roar; 5 s,
  half-second fades) and lay clip over clip every 4.5 s. Loudness s_om_rain_volume (0.8) x (0.12 + 0.88 x openness),
  openness = the share of 9 rays (up and 50 degrees slantwise, 4000 units) that reach the sky or nothing: under a roof
  only a murmur.

## r_om_seams: soft seams between textures (owner: "smooth texture transitions by best practice" - hard seams between different textures on one surface, tiles next to concrete)

* **Removed 2026-11-10** (r_om_seams, r_om_seams_width, si3DlmSeam and the strip code): ~0.1% of the frame, 1.3 ms
  GPU + 1.5 ms CPU at 1080p (while on, detail textures also left the world shader for a full extra pass). History below.

* A seam between two textures on one plane is always an edge two coplanar faces share (one texture per BSP face).
  At the first frame of a map (gl3_surf.c SeamBuild) every shared edge of two coplanar world faces is checked; for
  a seam that passes, a strip is cut on either face: the face clipped to r_om_seams_width (12) units from the edge,
  at most 40 % of the face's depth (thin trims stay themselves), between perpendicular caps at the edge's ends.
  Lightmap coordinates of the new vertices: an affine fit to the face's own vertices; texture coordinates: the
  neighbour's texinfo.
* Drawn after the opaque world, before the detail pass, with the lightmapped shader (si3DlmSeam: the face's own
  lightmaps, styles, dynamic lights, CSM, lightning, fog), the neighbour's texture, alpha-blended, colour target only
  (the face keeps its material for SSR). Alpha: half at the seam (both strips meet, no line left), none at the inner
  end; in between the neighbour's raised texels (brighter than its mean, the lowest mip) reach further and a
  world-space value noise breaks the straight line - a height blend in the manner of vertex-painted terrain blends.
* Which seams: both faces stone, ground, damp, smooth concrete or tiles (material classes, gl3_material.c); no
  pictures, glass, mirrors, metal (steel plates in concrete keep their hard edge), wood, fabric, trims, signs, doors,
  windows, hidden alpha images, animated textures; not between variants of one texture family (1_tile,
  1_tile_graf3, 1_tile_bottom: the artist fitted their edges; same name over 70 % of the shorter one, or the same
  stem before the last '_'); only where the seam shows - the two textures, read back at a mip level of at most 64
  texels, differ along the edge (2 units either side, every 4 units) by more than a quarter of their brightness.
  tsr1: 244 strips, jpn2: about 100.
* r_om_seams 0 off, 2 paints the strips red (where they lie). Menu: Video > Effects. Cost: one small draw per
  visible strip within 2500 units.
* Follow-up (owner: shutter panels got a soft band): also never between two textures of one material class. tsr1: 86
  strips.

### r_om_hdr and r_om_overbright (owner: "dark after the jpn2 elevator")

* The lobby is dark in the map's own data (base light 3..18 of 255; the style-33 office lights come on further on, by a
  trigger_once at y 1020), but r_om_hdr made it darker still: with r_om_hdr the lightmaps get headroom instead of the
  menu's overbright, and r_om_overbright (scale 2 in the owner's config) was silently dropped - the HDR picture was
  0.66 of the non-HDR one everywhere. The overbright now enters the neutral exposure (the adaptation measures the
  brighter scene, the shoulder rolls off what passes white): the lobby 24.8 with HDR, 21.7 without.
* Screenshots (gl3_sof2d.c): beside each sofNN.tga a sofNN.txt with the map and the omsetpos line of the view.

## r_om_tess_round: round props drawn curved (owner: "sort out the objects that must be round and tessellate them, the bin for one")

* PN tessellation was for characters only (props carry flat decals, boxes with smoothed normals swell). The Ghoul
  system now marks the textures of the models listed in Ghoul/om_round.txt (data/base/ghoul, one model folder per
  line: barrels, bins, cans, cones, extinguishers, hydrants, gas tanks, pots, fruit, globe, cannon, lamps, grenade)
  with OM_GHL_BEAM_HINT 3; an instance that drew such a texture is remembered by its handle (gl3_ghoul.c) and from its
  next frame the whole model is tessellated at r_om_tess_models - the whole model, so lid and body meet without a
  crack. The arm1 bin's five-sided lid gets a round silhouette. r_om_tess_round 0 turns it off.

## r_om_lshadow: lamp shadows of moving casters (owner: "at least dynamic shadows from the lights")

* The lightmaps keep the static world's shadows; this adds those of characters and Ghoul props from the map's own
  lamps (gl3_lshadow.c), on top of the lightmap as the sun's CSM does. A full move to dynamic lighting was weighed and
  left: the lightmaps also hold the bounced light, hundreds of lamps per map, and the Android port.
* Lamps: BSP lump 20 point lights and spotlights matched to their light entities (cone and axis as the compiler:
  target, _mangle, angle; _fade; the style key, so a lamp switched off casts nothing), surface-light patches merged
  into one emitter per panel (same face direction within 64 units, the cube 6 units off the face). jpn2: 821 lamps,
  arm1: 504.
* Each view the lamps with a caster in reach are ranked by their light on the nearest caster over the distance to the
  eye (a lamp that has a slot keeps it at 1.5 x, no flicker). Up to r_om_lshadow_lights (4) get a cube of depth maps
  (r_om_lshadow_size 512 a face, one 2D array on texture unit 16): six faces of the static world, drawn once when the
  lamp gets its slot, and six of the casters - the Ghoul triangles of the previous frame the sun's CSM keeps, now also
  without a sun, uploaded once a view; each face draws only the runs of primitives whose boxes reach into it.
* World fragment: the lamp's light there by the compiler's falloff (linear for lamps, omlight's fitted 1/d for panels,
  the spot cone), only where the static faces let the lamp through (the lightmap has its light there only), loses
  strength x share x blocked. Share = the lamp's light x r_om_lshadow_gain (4) against the lightmap, never more than its
  part of the lamps here - four panels over a floor a quarter each, a lone lamp all (the jpn2 lobby: omlight's fit
  explains its lightmaps poorly, R2 0.2, the honest share of one panel was a tenth). Never below 0.2 of the light.
  Fragments outside the cone from each lamp around its casters (box corners, a few degrees wider) or nearer than the
  nearest caster take no texture reads.
* Cost (arm1 shop, 1080p, 4 lamps): the cubes 0.1 ms GPU, the world shader +1.2 ms; 8 lamps about +3.5 ms. The first
  version drew the casters six times per lamp from a fresh copy (98 000 tessellated vertices a view): 9.5 ms.
* r_om_lshadow_debug 1: red the darkening, green a lamp's share, blue a caster blocks a lamp. r_om_lshadow_info: the
  lamps in the slots, views, static cubes drawn and caster vertices since the last call. Menu: Video > Effects (Lamp
  Shadows, Shadowed Lamps, Lamp Shadow Resolution).
* Not yet: shadows on characters (models do not receive).
* Follow-up (owner: "remove the old blob under the feet when there is a lamp shadow; the arm1 bin's shadow is
  literally black"): a character a slotted lamp lights by more than 4 lightmap units loses its ground blob (gone at
  16, gl3_ghoul.c shadowDarkness). The bin under its wall lamp (a lone panel, share 1) kept 0.2 of the light and a
  hard edge: a shadow now keeps r_om_lshadow_floor (0.4) of the lightmap - the bounced and ambient light are in it
  too - and panels filter over twice the texels (an area light, a wider penumbra). The floor counts from the lightmap
  before the sun's shadow, so sun and lamp shadows on one spot do not stack below it; the CSM stays (it is the sun's
  light, which the lamps do not replace).
* Physically based rework (owner: "between lamps the shadow would hardly show in reality; on the ceiling the shadows
  look like garbage and are very sharp; when a man runs the shadow jumps across objects; the shadow's darkness must
  follow the strength of the source and its neighbours; take a proven algorithm; indoors there can be no sun or moon
  shadow"):
  - Darkness: the subtractive shadow of baked lighting (as Unity's Subtractive mixed mode) - a shadow takes away the
    lamp's own light there and no more. r_om_lshadow_gain (4x) is gone; the lamp's light is reckoned at the weights
    omlight fitted to the map (log "fit 2": point, surface, spot; used when R2 >= 0.5 and each within 0.4..2.5 x the
    usual 1.2 / 0.85 / 0.6, else those). A second cap: the lamp's part of the light of all lamps that reach the
    caster's feet (each lamp traced for the world in between, CPU, measured again when the caster moves 12 units).
    arm1 shop, NPC between two lamps: 0.47 and 0.49 - two faint half shadows; under one lamp about a third darker.
  - Edge: percentage-closer soft shadows (Fernando, NVIDIA 2005). A blocker search (half the taps, a sampler object
    reading the depth on unit 17) gives the casters' distance halfway between their mean and the nearest to the
    receiver (the feet keep a contact), penumbra = lamp size x (receiver - blocker) / blocker; lamps
    r_om_lshadow_lightsize (16), panels the span of their patches + 32. Rotated Poisson taps (r_om_lshadow_taps 8,
    r_om_lshadow_softmax 24 texels); every search tap blocked skips the filter. The receivers are never in the casters'
    maps, so the wide filter brings no acne.
  - Ceiling: a caster within 16 units of its lamp (the fixture, its chain) casts nothing for it; faces the lamp grazes
    fade their shadow out (ndl 0.02..0.25).
  - Motion: a lamp that gets or loses a slot fades over 0.35 s (a dropped lamp keeps its slot till then). The casters
    of the previous frame are moved to their entities' origins of this one before the maps are drawn
    (GL3_CSM_Advance, by Ghoul instance handle; sun and lamps) - the shadow no longer trails a runner by a frame.
  - Blob: half of it stays under a lamp shadow (it stands for the bounced light the body hides around its feet), and
    less goes when the lamp's share is small.
  - Sun / moon (gl3_csm.c): a receiver loses only the sun's part of its light, sun weight x ndl against the lightmap
    (the fit's sun weight, else _sun_light). A window's sun patch in a lamp-lit room or a moon a street lamp outshines
    loses little; a map whose lightmaps hold no sun (omlight: "no directional sun explains the lightmap", arm1) casts
    no sun shadow at all. Indoors the static depth of the roof already blocks it. The beam pools' refill of the sun
    shadow (gl3_post.c) went with it.
  - Cost (arm1 shop, NPC under the lamps, 1080p): frame GPU 13.2 ms without lamp shadows, about 16 with (16 taps 16.5);
    the earlier version about 14.6. Cheaper: r_om_lshadow_taps 4, r_om_lshadow_search 0, r_om_lshadow_lights 2.

## Optimisation pass: old off where the new runs, perf tour (owner: "cascades off where the lamps work; FPS drops in random places - run through the levels; the old must switch off when the new is on, e.g. SSR on - all old reflections off; civil graphics cards")

* Perf tour (test aid): r_om_prof_tag - every change prints "OMSPOT <old tag>" for the frames since: whole frame
  (wall) and GPU mean(worst), the first 30 frames at a place apart ("settle": uploads, first sight), each part's GPU
  summed by name (parts that come and go do not restart it). Scratchpad tooling took 2..12 spots per map from its
  entities (player start, monsters, items, farthest-point sampled) and turned the view in place at each: 28 single
  player maps, 1080p, MSAA 2x, GTX 1050 Ti. Mean frame 8..12 ms everywhere; GPU parts over all spots: world 2.2,
  r_om_ssao 2.05 (steady), composite 1.4, Ghoul models 1.15 (6 at most), volfog 0.5 (3), SSR 1.2 in all, CSM 0.35.
* Found and closed:
  - PN tessellation runs on the CPU (9 triangles each at level 3) and the shadow passes took the tessellated
    triangles: sib2 1120 664 -432 16.4 -> 11.9 ms. The casters (sun, lamps, beams) are the triangles as modelled; the
    level follows the model's size on the screen (man-high model over 240 pixels keeps it, from 100 one less, smaller
    none).
  - CSM (owner: off where the lamps do the work): nothing is drawn when the map's lightmaps hold no sun (fit weight
    under 1) or when no sky face lies in the view's PVS (gl3_pvssky, set when the PVS is marked: deep indoors no place
    the sun reaches can be seen). sib2 indoors 0.97 -> 0 ms.
* Old next to new (code audit), now gated:
  - environment-map shine: off on every face SSR traces (reflectivity >= 0.015; was >= 0.045, ceilings and slopes
    between got both);
  - Ghoul ground blob: with r_om_ssao on it goes wholly under a lamp shadow (lshadow) and in the sun (CSM uniform
    params2.z 2) - SSAO darkens the feet itself; a blob wholly replaced is not built (no second pose walk for its mask);
  - light beam pools: their own character shadows (r_om_beams_groundshadow, and the extra depth layer per beam) give
    way while r_om_lshadow is on - the lamp's real shadow, not a second one;
  - the CPU dynamic light layer is not sampled while r_om_pdlight runs.
  Checked and already right: liquids' sphere pass under SSR, mirror faces, dynamic lights, painted light cones under
  r_om_beams, coronas into bloom, colour table under r_om_look, overbright under HDR, ppfog over the vertex overlay,
  the maps' own rain, the old lightning flashes, Q2 stencil shadows (r_shadows 0).
* Quality against cost (scratchpad study: 11 still views on tsr1, kos1, jpn2, sud2, nyc1, arm1; every setting changed
  alone, timed and shot after the picture settled, compared at quarter size against a repeat of the base shot - noise
  floor 2.5 % of pixels changed by more than 6/255):
  r_om_ssao_quality 3 -> 1 saves 0.54 ms (0.87 at most) at 3.0 % changed, by eye the same (the temporal pass evens
  the 2x4 pattern out); r_om_ssr_quality 2 -> 1 0.14 ms, r_om_volfog_quality 2 -> 1 0.18 ms, both at the noise floor.
  Those are the defaults now. Worth their cost: r_om_ssao (2.2 ms, 16 % of the picture), r_om_ssr (2.4 ms, 18 %;
  most of it the fixed passes, not the trace), bloom (0.3 ms). No visible gain from CSM 1024 / 2 cascades or lamp maps
  256 on these views (casters were frozen NPCs off screen), so they stay.
* Result, same tour on tsr1, kos1, nyc1, sib2, sud2, jpn2: mean frame 11.3 -> 9.4 ms, heaviest spot 18.0 -> 13.7 ms,
  worst frame 282 -> 20 ms, hitches 31 -> 0.

## HD texture packs and the removal of r_om_bump (owner: "a texture pack exists online - make installing it very simple, a folder of its own, switched in the menu"; "the old relief is rubbish, remove it")

* HD packs: `<exe dir>/openmerc/hd/*.pak` (docs/impl/INSTALL-LAYOUT.md), switch `om_hdtextures`, menu row Picture
  Quality / HD Textures (shown only when `om_hdpack` exists). sofhd2024.pak (sof1.org, 4x ESRGAN, 3159 world
  .m32 of 256..1024 and 1157 Ghoul .tga) works as it is: the original size for texture coordinates and the Ghoul
  skin cap were already in place (fb5). GTX 1050 Ti, nyc1, A/B/A in one run: +0.2..0.4 ms, no hitches, ~150 MB
  of video memory. Separate tour runs drift by up to 25% (clocks): compare within one run only.
* r_om_bump removed: normal maps made from the colour of the textures, lit by the omlight light directions. Off
  by default (stains and seams became bumps) and still built for every wall texture at load. Gone with it: the
  light-direction pages (the .oml cache keeps the directions, the renderer no longer reads them), GL_TEXTURE10
  and 11, the omBump* uniforms and the r_om_bump_* cvars. Kept and renamed: the map's lamps for the SSR lamp
  glints (gl3_lamps.c, GL3_Lamps_Set / GL3_Lamps_View), the material class (GL3_Mat_SurfClass).

## r_om_a2c: alpha-to-coverage for grates, fences, wires (owner: "alpha-to-coverage")

* MSAA smooths geometry edges only; the alpha test of world faces (texinfo 0x800, texel alpha <= 0.05 discarded) kept
  stair-stepped wire edges, more visible with the HD pack. With `r_om_a2c 1` (default) and a multisampled target, the
  opaque lightmapped faces (si3Dlm, si3DlmFlow, not blended) draw with GL_SAMPLE_ALPHA_TO_COVERAGE and
  GL_SAMPLE_ALPHA_TO_ONE; the shader (omCoverage) widens the 0.05 threshold to about one pixel by fwidth(alpha) and
  outputs it as coverage, so the edge falls between samples like a geometry edge. The threshold position is the same,
  so the wires keep their thickness. Without MSAA (mirror and other single-sample targets, r_msaa_samples 0) the plain
  test runs: Draw3DBatchesNow checks GL_SAMPLE_BUFFERS when the program or blending changes and sets omA2C.
* Ghoul: the same for opaque alpha-tested primitives with GL_GREATER / GL_GEQUAL (gl3_ghoulemu.c).
* arm1 chainlink (alphaspot.py finds 0x800 faces and a view of them), one run: wire edges at a grazing angle get
  intermediate tones instead of steps; the mesh does not thin out with distance. Cost not measurable.
## r_om_ambcube: directional ambient light of models (owner: "lighting of characters and weapon from the baked light")

* Ghoul models (ghl_light_method 3) are lit by up to 6 map lights (BSP lump 20, GL lights) plus an ambient term,
  0.6 x the lightmap under the feet: one colour for the whole body. Now the ambient term follows the surroundings:
  probes on a 32-unit grid look along +x -x +y -y +z -z (LightTrace, the lightmap of the first face within 1024
  units, the sky / nothing = the mean of the hits), a model blends the eight probes around its origin (the view
  weapon: the eye), probes in solid weigh nothing; cached per map (4096 slots). Only the shape is used: the six
  colours divided by the mean brightness of the four horizontal ones, pulled toward 1 by `r_om_ambcube` (0 off,
  default 0.7), clamped 0.25..2 - the sides keep their brightness on average (normalising by all six darkened the
  sides under a bright floor and a lamp-lit ceiling, jpn2), the side facing the lit part of the room is lighter,
  coloured walls tint. Applied in the emulator's ambient term, vertex (Lighting) and per-pixel (omLit) paths, with
  the view's eye-to-world rotation; all lit Ghoul draws on arm1, jpn2, kos1 go through it.
* `r_om_ambcube_debug 1`: +x red, +y green, +z blue (checked: the side facing +x is red); 2 prints the factors
  (OMAMB). arm1 shop: the sides differ by about 15 %, at a doorway or window up to 2 : 0.25.
* Not done: the map lights still reach a model through walls (no occlusion test in GatherWorldLights). Done later:
  r_om_lightocc.
## r_om_fxlight: lit smoke and dust (owner: "lit smoke and particles")

* SoF alpha quads carry a lit flag (SPEC-412); the effects that set it already take the light point below them.
  The others drawn with normal alpha blending (smoke `gb_smoke01`, `smoke2` - the survey while firing on arm1 found
  only smoke there) kept their own colour wherever they were. With `r_om_fxlight 1` (default) such quads, unless
  decals, take the light at their own position (GL3_SoF_LightPoint: the lightmap below plus the dynamic lights -
  muzzle flashes, fire), turned into brightness like the world's lightmaps (SoF colour table, then overbright), so
  smoke is as bright as a white wall beside it. Additive quads (fire, sparks) and darkening ones are left alone;
  r_fullbright and night vision keep the old look. 0..1 blends, `r_om_fxlight 2` paints the quads it lights red.
* Checked: the debug mode paints the flamethrower smoke on arm1. The flamethrower's smoke is nearly black
  (colour 4,0,5, alpha 0.2..0.4), so it changes little; an A/B in one run was dominated by the flicker of the fire
  and gave no clean number. Light-coloured unlit smoke in dark places is where the change shows.

## r_om_lightocc: map lamps no longer light models through walls (owner: "make the lamps not shine on characters through walls")

* 2026-11-10: the switch is gone, the trace is always set (with ghl_shadows on it was set anyway: the paused A/B showed 0 pixels).

* The Ghoul system picks a model's GL lights (BSP lump 20) by distance only (WorldLightGrid::Query). With
  `r_om_lightocc 1` (default) the renderer always hands it the world trace (render parameters 0x44; before only with
  ground shadows on) and GatherWorldLights keeps a light only when the ray from 4 units off the lamp to the model's
  origin + 4 or + 40 units is clear (fraction >= 0.97, or less than 8 units short; a lamp starting inside its fixture
  counts as reaching). The answers are kept per instance until it moves 8 units or half a second passes (doors); the
  next lights in range fill the freed slots, a light that stops reaching fades out with the usual light smoothing.
  The ground shadow still depends on shadowMode only (Shadow is called only for casters). Cost: not measurable (arm1
  fire and three NPCs, A/B in one run).

## FPS drops with characters close by (owner: "I see FPS drops")

* Perf tour A/B in one run (nyc1, kos1, arm1, 28 spots, user's config): r_om_a2c, r_om_ambcube, r_om_fxlight cost
  nothing measurable (+-0.1 ms), no hitches. The drops were characters: three NPCs at arm's length on arm1 took the
  frame from 9 to 16.7 ms.
* Found (QueryPerformanceCounter around the Ghoul draw): GL3_GhoulEmu_Begin asked glIsProgram for every model - a
  query that waits for NVIDIA's threaded driver to finish everything queued - 3..5 ms a frame with characters in
  view. Now a context counter (gl3_contextgen, counted where the context is made) tells a restarted renderer.
* PN tessellation moved to the vertex shader (desktop GL): the original triangles go to a texture buffer (R32F, the
  17 floats of emuvtx_t, GL_TEXTURE20), the draw has no attributes and 3 L^2 vertices per triangle; corner table
  omTessTab (the same sub-triangles and winding as before). CPU: Ghoul drawing 4.2 -> 1.7 ms a frame (= without
  tessellation). GLES keeps the CPU path; `r_om_tess_cpu 1` forces it (test aid).
* The rest is GPU: 19 000 tessellated triangles of three close characters cost 4.8 ms more at level 3, 2.4 at level
  2 (small triangles: MSAA shades the pixels along every edge twice). Level 3 now only above 600 pixels of model
  height (arm's length), from 100 pixels at most 2.
* Measured end state (arm1): firing the flamethrower 9.2 ms a frame, no hitches; three NPCs at arm's length 17 ms
  (GPU, level 3 by choice).

## Texture seams found by position, height blend of both textures (owner: "smoothing of texture transitions is not done - find best practice and implement")

* Removed 2026-11-10 with r_om_seams (see above).
* r_om_seams matched only BSP edges two faces share by index; along a T-junction each face has its own edge. Now
  the sides of the faces are matched by position (collinear, overlapping, on one plane), the strip has caps at the
  overlap. tsr1: 86 -> 96 strips; most borders of different textures there are artist-fitted variants (1_tile,
  1_tile_middle, 1_tile_bottom - 3456 of 3727), correctly left alone.
* Blend as the height-based splatting of terrain shaders (Mishkinis, "Advanced Terrain Texture Splatting";
  Unity "Material by Height"): both textures' heights (texel brightness above the texture's mean; the face's own
  texture now on GL_TEXTURE10 with its texture axes as uniforms omSeamS / omSeamT) plus their shares; only the
  higher shows, mixed within 0.12.

## r_om_lmstitch: lightmap seam stitching (owner's screenshot: hard steps of light across a shop shutter)

* The visible "transitions" on walls are mostly not textures but lightmaps: every BSP face has its own, lit on its
  own grid, and where a plane is cut into several faces they disagree along the border - straight steps of light
  across a wall (jpn1 corridor: rectangles on the wall). As the bakers do it (Unity / Bakery "stitch seams",
  Sylvan's lightmap seam stitching): at map load (Mod_LoadFaces, before the lightmaps are uploaded) every border of
  two coplanar lit faces is read every half sample, bilinearly as drawn, per light style present on both sides; the
  samples of each reading move toward the mean of the two (three rounds), the correction fades into the face over
  `r_om_lmstitch_width` (3) samples. Faces of any texture, not sky, warp, translucent, alpha-tested. jpn1: 11 144
  faces, 75 000 border points; the corridor rectangles are gone. `r_om_lmstitch 0` off (takes effect at the next
  map load of another map).

## "Relief" still visible (owner: "we removed the relief, it is still there")

* r_om_bump is gone from the code (9af6286). SSR on / off at the arm1 shop changes only the floor's sheen. The
  embossed look of bricks with bright joints is in the HD pack's textures themselves (sofhd2024, ESRGAN upscale):
  arm1 shop with om_hdtextures 1 / 0 in one run shows it with the pack only.
## r_om_hdseam: tile seams of the HD pack (owner: "texture transitions are visible")

* Cause: sofhd2024 was upscaled tile by tile without wrapping the borders, so its tiling textures no longer tile.
  Wrap-edge jump against the mean texel step (tilecheck.py over 452 sampled textures): HD median 5.1, retail 2.05;
  2.5x worse in the median, up to 12x (subway/3_sweatwall_top). In game every repeat of a wall texture shows a
  line or a band (jpn1 corridor stucco: hard-edged vertical bands; arm1 bricks: broken mortar on the wrap).
* Fix at load (gl3_image.c HDSeamRepair, only SOFIMG_WALL replacements at least 2x the retail size): the retail
  original is read through FS_ReadOriginalHead. Per axis, only if the original tiles there (wrap jump < 1.6 x the
  90th percentile of column steps, so posters and signs are left alone). Low frequencies (box radius = scale) of
  the HD level 0 are replaced by those of the bilinearly upsampled original, near the edges (1/8 band) for
  r_om_hdseam 1, over the whole texture for 2 (default). Then the wrap jump beyond the retail texels' own jump is
  split and faded over 2*scale texels. The colour of the mip chain is rebuilt from level 0, alpha kept.
* A first version (rebuild the outer texels across the wrap) erased thin features lying on the tile edge (mortar
  lines, trims) and was dropped. Mode 2 also calms the network's exaggerated relief (white mortar, blobs on
  stucco) because the tone comes from the retail art, while the sharpness stays HD.
* A/B/C within one run (vid_restart): jpn1 corridor and arm1 shop, the bands are gone with 2. jpn1 load is about
  the same (20 s, within 1 s). The proper cure is re-upscaling the pack with wrapped borders (hdtex.py already
  does that, it needs Real-ESRGAN to be allowed to run).

## r_om_seams on one texture with a broken placement (owner's F11 shot: arm1 brick pillar, omsetpos 112 -103 37 213 -4)

* Removed 2026-11-10 with r_om_seams (see above).
* The pillar is armory/3_store_white_bottom on several coplanar faces with different texinfo: other offsets and
  mirrored axes (f1948 t -1.33 vs f1954 t +1.33 - the dark foot of the texture turned up to the ceiling on purpose).
  The retail 64x64 image is near noise, so the jumps never showed; the HD pack invented crisp bricks with white
  mortar, and every face split became a hard line through the bricks. Not an HD tile seam (r_om_hdseam can't help).
* SeamBuild now also takes a border between faces with the SAME image when the placement jumps there
  (SeamMapJump: coordinates in retail texels, modulo the tile, more than one texel apart at any of five points).
  The existing strip (the neighbour's mapping blended in, half at the seam, height blend) hides it; the material
  class / family / visibility tests stay for different images only. Strips: arm1 310, jpn1 1167, tsr1 887.
* At the owner's spot: the vertical line through the bricks is gone, the horizontal one is soft (A/B in one run).
* Side finding: ai_freeze 1 set before `map tsr1` from om_after hangs the test (frozen intro) - also with seams off.

## Fog is opaque again in the distance; SSAO and SSR under the fog (owner: "I see the sections being drawn live, the shadows lie over the fog", sib1 and every fogged map)

* Cause of the visible sections: with `r_om_volfog` on, the SoF overlay alpha was multiplied by
  `r_om_volfog_overlaydim` (0.85), so the fog never reached full opacity and the distance stayed 15 % visible.
  Every cluster the view walk added there (PVS, frustum) appeared in plain sight. Now the dim only applies to
  thin fog near the eye and fades out towards full opacity: a' = a * mix(dim, 1, a) (`GL3_Fog_DimAlpha`, the
  per-vertex overlay, the `r_om_ppfog` shader). Fixed view on sib1 (r_om_viewtest 3624_-1888_270_0_195): the far
  mountains and bridges were visible before, now hidden as with volfog off.
* SSAO and SSR run after the overlay and darkened / reflected over it. Both now fade by the overlay alpha of the
  eye's region at the pixel (`GL3_Fog_ViewFog`, shared GLSL `om_viewfog_glsl` in gl3_post.c: squared distance
  from the depth, start / range / opacity / height falloff of the region). Pixels outside that region are
  treated with its fog (eye outside every region: no fade).
* Test aids: `r_om_campath rec:<file>` / `play:<file>` records / replays the eye per rendered frame (user dir);
  `r_om_avims 0` writes one AVI frame per rendered frame (with `r_om_avinodialog 1`), so runs with different
  settings compare frame by frame; `om_testmode 2` (game) is god + notarget with walking collision. A synthetic
  pan path (six 360 degree turns + a walk) on sib1 shows no localized pops after the change; the remaining
  periodic change every 6 frames with a still camera is a 10 Hz light style.

## r_om_fogcull: skip what lies in opaque fog (roadmap phase 1.2) - measured, off by default

* A face of the eye's region whose region polygons cover it gets its overlay alpha from the squared distance
  alone; where the region is opaque (opacity 1, no height falloff, gl_fogmode 0) faces wholly beyond
  start + range * 256 (+ shimmer, + 2 %) are skipped with their overlay; Ghoul models (instance box) and inline
  models (only when every face carries region polygons) in that region likewise. Sky and fog boundary faces are
  kept; nothing with the night-vision goggles. `r_om_fogcull 2` prints OMFOGCULL once a second.
* Measured on sib1 (the pan path, 1628 frames, 1080p, GTX 1050 Ti, on/off/on in one run): world GPU 1.41 -> 1.37 ms
  of a 6.9 ms frame. The PVS already removes nearly everything behind the fog; at the six fixed views at most 82
  of 312 candidate faces were skipped. Of the shipped single-player maps only sib1 has a large opaque region
  (cut 678 units); the others are smoke and steam volumes in rooms (cut 94..320).
* Image: identical except SSR on wet floors, which traced against the depth of the skipped distance and lost
  part of a near reflection (sib1 walkway). Hence default 0; it is meant for the large maps of phase 2, together
  with a depth-only stand-in for skipped faces.

## r_om_ppfog in one batch; r_om_fxaa

* Per-pixel region fog no longer sets an effect state per polygon (the drawback in 15.4): the parameters of every
  region go to a small float texture on GL_TEXTURE4 (8 texels a row: start, range, max alpha, height falloff |
  maxs z, size z, dim, planes | up to 5 shortening planes), the vertex carries its region in s and its shimmer
  offset in t, the colour stays in the vertex colour. All world overlays of a pass share one state. Plane marking
  and the far wall run over every queued face first, so the table is final when drawn (before, a polygon could
  see fewer marked planes than a later one). The texture is re-uploaded only when its content changes. Faces of
  inline models keep the per-vertex path.
* r_om_fxaa (FXAA 3.11 quality after Lottes): the composite draws into a view-size RGBA8 texture and an FXAA pass
  brings it to the window (3D view only, the HUD comes later). -1 (default) = on when the window has no MSAA
  (ports, r_msaa_samples 0), 0 off, 1 always.

## Renderer audit 2026-11-04: streaming, driver syncs, lifecycle, GLES build

Found by a code audit (three read-only passes: per-frame cost, lifecycle / memory safety, portability) and fixed:

* **Streaming rings - measured, vertex rings off by default.** The 3D batches, effect batches and Ghoul primitives can
  write into rings (unsynchronised maps, orphaned once per lap; the 3D VAO's attribute pointers move to the batch's first
  vertex, no BaseVertex draw needed on GLES 3.0): `r_om_ringbuf 1`. A/B/A in one run on a GTX 1050 Ti (tsr1): +0.3 ms GPU
  on the models and 26 ms spikes when the 16 MB ring was orphaned - the driver keeps the mapped stream buffer in system
  memory, its renaming of small glBufferData stores is faster. Default 0; kept for drivers that copy on every
  re-specification (to be measured on mobile). The 3D uniform block ring (`r_om_uboring 1`) measured neutral on the GPU
  and stays on.
* **No driver round trips in draw paths.** A shadow of the tracked GL state (blend, cull, depth test, depth mask,
  blend function, cull mode) in header/local.h: the setters update it and `glIsEnabled`/`glGetIntegerv`/
  `glGetBooleanv` of that state read it (the Ghoul emulator asked 9 times per model, the shadow and beam passes
  per pass). `GL_SAMPLE_BUFFERS` is no longer asked per alpha-tested Ghoul primitive and per world batch: the
  sample count is set where the view's framebuffer is bound (`gl3_drawsamples`). Uniform locations looked up per
  frame (mirror, lightning, material, SSR, mono, seams, look, lamps, weather, sky tint, the view fog) come from a
  cache keyed by (program, name literal, context), cleared by every `glDeleteProgram`.
* **Less redrawn.** The static layers of the sun cascades are kept while the cascade's texel-snapped projection,
  the map and the shadow texture stay the same (before: the whole static world into every cascade every frame).
  The light beams no longer copy the depth again when SSAO / SSR copied the finished view.
* **Lifecycle (vid_restart, map change).** The weather (programs, VAO, occlusion / sky / strike maps), the mirror
  targets, the night-vision noise texture, the GPU timers and the Ghoul glow / round texture lists kept names of
  the old GL context and drew with them or deleted new objects of the new context; they are released at shutdown
  now. Caches keyed on `gl3_worldmodel` never noticed a new map (it is always mod_known[0]): the ambient-cube
  probes, the weather's sky mask, AA guns and lightning warm-up, and the seam strips reset at map load. The
  SSAO / SSR framebuffers that attach the scene colour are deleted with the scene targets (toggling r_om_hdr gave
  the same texture names back and the effects drew into deleted textures). A zero normal in the Ghoul per-pixel
  lighting no longer gives NaN speckles.
* **GLES3 build compiles again** (`ref_gles3`): `GL_SAMPLE_ALPHA_TO_ONE`, texture read-back and `glColorMaski`
  guarded; one GLSL ES header with precision for int and all sampler types; post shaders with several outputs get
  `layout(location)` on GLES; opaque textures upload as RGBA8 on GLES (RGB8 + RGBA data is invalid there); lamp
  shadow sampler objects are looked up on every platform (`GL3_GetProcAddress`), not only on Windows.
* **Mirrors draw only what is seen through them.** The box of the mirror faces on the plane (collected by the world
  walk) is projected through the mirrored camera; its screen rectangle narrows the four frustum planes of the mirror's
  world walk and scissors its drawing (before: the whole mirrored view at 0.7 of the window). ger2 mirror spot:
  1.38 -> 0.26 ms GPU (worst 10.8 -> 5.5 ms); the tsr1 restroom image is unchanged (0.05 % of the pixels differ).

## Map load time (owner: the test windows "stopped responding")

With the HD pack a map load kept the window from pumping messages for 12 s (tsr1; Windows marked it "not responding"
after 5 s and offered to close it). `r_om_loadtime 1` prints each stage ("OMLOAD ..."); tsr1 15.7 s -> 4.9 s,
"not responding" 12.5 s -> 0.5 s:
* Every face texture was first asked for as textures/<name>.wal (yquake2's GetTexImage); each miss searched all
  paths and paks for the .wal and its .png / .tga / .jpg replacements: ~4 s. GL3_FindImage keeps the names it did
  not find in this registration and answers them at once.
* The material maps (r_om_ssr) were computed on one core with a modulo per blur tap: 8 s. The blur's wrap is a
  counter now, its columns run row by row, and the maps are measured on all cores (gl3_jobs.c GL3_ParallelFor,
  64 textures a batch during a registration, uploaded on the main thread) - the same floats in the same order, the
  maps are unchanged (tsr1 tour shots within 0.7 %).
* Left: HD seam repair 1.3 s, uploads 0.6 s, lightmap faces 0.9 s.

## r_om_renderscale: the 3D view below the window resolution (roadmap phase 3.1)

`r_om_renderscale` 0.25..1 (default 1): the scene targets and the 3D viewport are that share of the window; the
composite (and FXAA) draw at the scene size and an upscale pass brings the view to the window - Catmull-Rom from 9
bilinear taps, then a contrast-adaptive sharpening against the four neighbours clamped to their range (after AMD CAS /
FSR RCAS; `r_om_upscale_sharp` 0..1, default 0.25). The HUD stays at the window resolution; mirrors follow the scene
size. tsr1, 1280x720 window, GTX 1050 Ti: GPU 10.25 ms at 1.0, 7.18 at 0.67, 5.07 at 0.5, 6.55 at 0.67 with FXAA; no
GL errors.

`r_om_upscale` 1 Catmull-Rom, 2 FSR 1.0 EASU (default; the presets: low 1, medium / high 2): 12 texels, a
lanczos-like kernel stretched along the local luma edge, clamped to the four nearest texels (no ringing), then the same
CAS. tsr1 at 0.67, PSNR against the native frame on four shots: Catmull-Rom 37.54 / 39.59 / 40.60 / 40.38 dB, EASU
37.89 / 39.69 / 40.71 / 40.55; the post pass 0.66 -> 1.00 ms (GTX 1050 Ti, 720p), so the low / mobile preset keeps
Catmull-Rom. sib1 at 0.5: clean snow edges. The cvars are registered at renderer init (before, the console did not know
them until the first view).

## r_om_preset: one knob for the GFX-modern settings

0 custom (nothing touched, the default on desktop), 1 low (mobile / integrated GPUs: sun cascades 2 x 1024, lamp
shadows 256 / 2 lamps / 4 taps, no SSAO / SSR, half-size volumetric fog, mirrors at 0.5, render scale 0.75 with the
upscale, FXAA without MSAA, no tessellation, no per-pixel model light), 2 medium, 3 high (this build's defaults).
Applied once when it changes, the single settings stay editable; GLES builds start on 1. tsr1 GTX 1050 Ti 1280x720:
high 9.94 ms GPU, medium 9.24, low 3.73.

## Lamp shadows on 16-unit GPUs

The lamp shadows needed texture units 16 and 17 (18 units); GLES guarantees 16 and mobile GPUs report exactly that,
so they were always off there. With fewer than 18 units they now use 11 (compare) and 7 (raw depth for the blocker
search) - the units the world, Ghoul, volumetric fog and beam shaders leave free; the fog and beam passes read the
depth copy on unit 3 instead of 7. `r_om_lshadow_compact 1` forces that layout on desktop (read at renderer start):
arm1 shop with a character, normal vs compact 0.67 % of the pixels differ, normal vs normal 0.61 % (noise); fog and
beam maps (kos1, ger1, arm2) unchanged; no GL errors. r_om_lshadow_info prints the units.

## r_om_csm_depth16: 16-bit sun cascades

The cascade array (2 x cascades layers: the static cache and the dynamic copy) was DEPTH_COMPONENT24, stored as 32 bit:
2048 x 2048 x 8 = 128 MB. The cascades are orthographic, their depth is linear over the light range, and 1/65536 of
it is far below `r_om_csm_bias`; `r_om_csm_depth16 1` (default) halves the memory. ger1 (sun active in the first spot,
two shots): 82.3 / 84.6 dB against 24 bit, no acne, the same csm time. `r_om_csm_info` prints the size, depth and
memory. The cascades stay off where the lightmaps hold no sun (omlight's fit, e.g. kos1 sun 0) or no sky is in the PVS.

## r_om_worldvbo: the world's vertices in a static buffer (roadmap phase 2.1)

Until now every visible world face copied its vertices (44 bytes each) into the batch arrays and the stream buffer
every frame. With `r_om_worldvbo 1` (default) the polygons of the world model and its inline models go into one
static vertex buffer when the first face is drawn after a map's lightmaps are built (tsr1 73k vertices / 3.1 MB,
kos1 82k / 3.4 MB); a face then adds only its fan as 32-bit indices. Batches keep their order; a batch carries its
source (`gl3drawCmd_t.src`) and the flush switches between the world VAO and the stream VAO where it changes.
Faces that stay on the stream path: dynamic light flags this frame (the static copy has none), liquids (warped
texture coordinates), sky, several polygons. Rebuilt on a new map (GL3_LM_BeginBuildingLightmaps) and with a new
context.

Checked: the same frame with the cvar toggled 3 frames apart at a fixed spot (tsr1 max difference 6 / 255, ger1 11,
sib1 only the falling snow and a flickering flare); 13 maps with the GL debug context: no errors, the shader
recompile warnings as before. CPU world time per spot, A/B on tsr1 + sib1 (GTX 1050 Ti): 0.13 / 0.14 / 0.56 / 2.62 /
3.16 / 0.51 / 0.49 / 0.39 / 2.90 / 4.75 ms -> 0.13 / 0.15 / 0.52 / 2.17 / 2.81 / 0.40 / 0.41 / 0.38 / 1.97 / 3.85;
the GPU time unchanged.

## r_om_postaa 2: SMAA 1x

The post antialiasing slot (`r_om_fxaa`: -1 auto = without MSAA, 0 off, 1 on) now runs SMAA 1x by default
(`r_om_postaa` 2; 1 = the FXAA above). Orthogonal patterns only, as the reference's MEDIUM preset (Jimenez et al.
2012, no diagonal or corner detection): luma edges with local contrast adaptation into an RG8 target, blending
weights from the area table into an RGBA8 target, neighbourhood blending into the window (or the upscale target).
The area table (80 x 80 RG8, offset 0 of the reference's AreaTex) is computed at start from the reference
generator's orthogonal formulas, the line searches walk the edge texture texel by texel (up to 32 pixels) instead of
using the search table - no third-party binary data. Checked against the generator's geometry: a one-pixel step
gives 0.125, the pixel next to the step of a 4-pixel line 0.375, maximum 0.5.

tsr1 / ger1 / sib1, a still camera, no MSAA, one run: FXAA changes 3.8-15.7 % of the pixels, SMAA 4.5-11.4 %; both
remove the staircases, SMAA keeps the textures sharper. GTX 1050 Ti 720p, post pass per 200 frames: off 0.81 /
0.86, FXAA 0.86 / 0.89, SMAA 1.02 / 1.09, off again 0.93 / 0.87 ms (about +0.05 FXAA, +0.15 SMAA).

The cvar was first named `r_om_aa` - that name is the anti-aircraft fire of the weather (r_om_aa_maps, kos1-3), two
file-scope declarations of one variable; renamed before release.

## Video menu: RENDER SCALE AND SMOOTHING

`menus/om_video.tbl` has a section for these: quality preset (`r_om_preset`), render resolution (`r_om_renderscale`:
Performance 50 %, Balanced 59 %, Quality 67 %, Ultra Quality 77 %, Native), upscaler (`r_om_upscale`: FSR 1.0 /
Catmull-Rom), upscale sharpening (`r_om_upscale_sharp`), post antialiasing (`r_om_fxaa`) and its method
(`r_om_postaa`: SMAA / FXAA). The rows are shown while the renderer registers the cvars; `r_om_preset` is now
registered at renderer start (before, only on the first 3D frame, so the row would be hidden in the main menu).

SMAA / FXAA on five more maps (same method): kos1 9.5 / 9.9 % of the pixels changed, nyc1 8.2 / 8.5, irq3b 1.3 /
1.7, sud1 5.3 / 5.7, jpn2 3.9 / 4.6; no GL errors. The video menu section was checked on screen (menu main ->
video, scrolled): all six rows with their labels and values.

## Frustum clip flags in the world walk

RecursiveWorldNode passes a mask of the four frustum planes: a node wholly in front of a plane clears its bit, its
children skip that plane (their bounds lie inside the node's). Same frames on tsr1, kos1, sib1, ger1, irq3b, nyc1;
the CPU gain is within the noise of this machine (the frame CPU time is mostly the wait for the GPU).

## Pass on all 75 maps (2026-11-05, this build)

32 SP + 43 DM maps, hidden test windows, GL debug context: 0 GL errors, 0 undefined-behaviour messages, the static
world buffer built on every map with a world. The anomaly flags (flat / orange shots) are the same spots as in the
first pass (a camera in a wall or in thick fog). Average over the SP maps against the first pass: GPU 5.35 -> 5.30 ms,
CPU 3.96 -> 3.95 ms (noise either way per map). Eleven DM maps have no info_player_start (irqdm1, jpndm2, kosdm1,
nycctf1, nycdm1, raven, sibctf1, sudctf1, suddm1-3): started in single player the game stops with "Couldn't find
spawn point", as the original does; the harness starts them with deathmatch 1.


## r_om_pbr_models: physically based light on Ghoul models (gl3_ghoulemu.c omLitPBR, 2026-11-10)

* The per-pixel path (r_om_ppl_models) with omPPL 2: the same Ghoul lights, Lambert diffuse, GGX specular (Smith,
  Schlick) added after the texture (glossy SoF materials - specular in the material - get a texture-coloured, metal
  highlight at half the material's level; the rest a dielectric sheen, roughness 0.65), no clamp to 1, lamps fading
  to 0 over the last quarter of their radius (GL's 1/(1+d/r) was still half bright at the cut: light jumped).
* Surroundings in the gloss: the r_om_probes cube maps (desktop, units 24..27, GL3_Probes_Uniforms once per frame),
  else the ambient cube along the reflection.
* r_om_pbr_sun (0.8): the sun (or moon) of the CSM as a directional light where the static shadow map lets it
  through (not the dynamic layer: self-shadowing), ambient x (1 - 0.3 x sun) there. Only while the CSM is active.
* GL3_SoF_LightPoint (floor light of models and effects) blends the 8 corners of the 32-unit grid (solid corners
  left out) instead of one value per cell: walking characters no longer step in brightness.
* r_om_pbr_models 0 brings the GL 1.1 emulation back; values above 1 scale the specular.

## r_om_glass: see-through glass with refraction (gl3_glass.c, 2026-11-10; owner: "glass transparent with refraction, replace completely")

SoF glass is a translucent texture of material 37: one flat dark grey alpha over the pane (0.36-0.72), frames and
wire solid, cracks as holes - blended, every window was smoked film. Now (world faces of the sorted pass and the
0x800 faces of inline models: breakable windows are brush entities, DrawInlineAlphaFaces):
- the pane's colour and alpha are measured from the texture when it loads (GL3_Glass_PaneStats: the most common
  alpha between holes and solid); alpha above it is frame/wire/dirt (drawn lit as before), below 0.03 a hole;
- the scene drawn so far is copied (GL3_Post_GlassCopy, full size; half on phones) and refracted: low waves of drawn
  glass plus the texture's gradient, scaled with the distance behind the pane, a little per channel, never from
  what is in front of the pane; tinted by the texture's hue (not its darkness), Fresnel F0 0.04 reflection of the
  probes (phones: the light around the pane), GGX-like lamp highlights, frosted under lighter smudges;
- premultiplied blend: colour replaced, emissive dimmed only by the solid parts; the material target is cleared
  (the old blend did too) - else the SSR pass, which runs after, drew the floor glints behind the pane unrefracted
  over the glass (owner: "strange glints on the floor through the glass").
- Cost: the first version copied the scene again for every pane over the union box of the panes drawn since the
  last copy - a window grid covered its own panes, up to 8 copies a frame, each an MSAA resolve + blit + depth blit
  (~0.7 ms each at 1080p; owner: "glass cuts FPS hard"). Now the boxes are kept per pane (touching panes do not
  count), at most 2 extra copies (none on phones), and at full size the MSAA resolve is the copy. kos2 window view
  1080p: 12.5 vs 12.1 ms GPU with r_om_glass 0.
Cvars: r_om_glass (1; 0 the old blend), r_om_glass_refract (1), r_om_glass_tint (0.35), r_om_glass_reflect (1).

## Effects pass 1 (owner: "Q2-era effects: smoke, fire, explosions - do it properly, mind the FPS; smooth every square and unevenness under anything luminous", 2026-11-10)

Gallery: jpn1 start, every weapon fired (`fxgal.cfg`), and `om_fxplay <effect> [dist] [up]` (new console test aid,
client fx module) to start an effect in front of the eye.

* **r_om_smoothnormals** (gl3_smooth.c, map load): smooth vertex normals for the world and each inline model. A
  corner takes the normals of faces touching that point (on the polygon or an edge: T-junctions count) within 60
  degrees; a neighbour weighs at most its area over the face's own, so a big wall next to a small bevel keeps its
  normal, columns and arches round off. jpn1: 7019 corners. Basis for every light/highlight on the world.
* **r_om_dlight_model 1** (gl3_shaders.c omDlightsNew, folded in as gl3_shader_pdl 2): the dynamic lights of muzzle
  flashes, fire and explosions as lights - window `1 - x^2(3-2x)` in the squared distance over the SoF radius (half
  the light at 0.7 R, as SoF's linear rule), wrapped N.L on the smoothed normal, a GGX highlight by the face's
  material (r_om_dlight_spec), added in linear light after the colour table. SoF's rule added them before the table
  and clipped the sum at 255: the flamethrower flooded the ceiling flat in its colour. Eye in omTexDim.zw / omPdl.w
  (unused by the new model). Cost: flamethrower firing 9.7 vs 9.7 ms GPU. r_om_dlight_spec -1: debug view of the
  normal where a dlight is flagged.
* **r_om_fxvolume 1** (gl3_fx.c FxVolumeVerts): unlit alpha-blended puffs (r_om_fxlight's smoke) drawn as a fan of
  five points around the centre (no diagonal crease between two triangles of a card), lit as a ball: the light
  sample turned by the ambient cube along a sphere normal per point, plus a signed term toward each dynamic light
  (mean unchanged). SoF's gun smoke is nearly transparent, so the change is small; CPU only.
* **r_om_fxsoft 1** (gl3_ghoulemu.c): additive passes of effect models (the explosion ball is a low-poly Ghoul
  sphere, its polygon outline was the worst "Q2" artifact of an explosion) fade toward their outline
  (|N.V|, smoothstep 0..0.55; unlit effect models have no normals: the direction from the model origin) and where
  they cut into the world (scene depth, 16 units). The view weapon (own depth range) is left alone - muzzle flashes
  unchanged. r_om_fxsoft_debug 1 prints the blend of the next 60 blended Ghoul passes.
* Soft particles (r_om_softparticles) stay off: the explosion's hard edge was the model, not the sprites.

## Baked light highlight (r_om_deluxe, owner's point 4, 2026-11-10)

* omlight already stores per lightmap sample the mean light direction and its directional share (cache version 2).
  gl3_relight.c keeps that block for the loaded map (also when the map's own lightmaps are used: loose fit or
  guard), gl3_lightmap.c builds an RGBA8 atlas of the same layout as the lightmap pages, bound on GL_TEXTURE10.
* World shaders (omDeluxeSpec): lightmap x directional share x GGX (D F, Kelemen V) along that direction, on the
  smoothed normal, reflectivity/roughness from omMaterial, roughness >= 0.15 so the 16-unit grid never shows.
  Where the r_om_ssr lamp glints already light floors, 0.4 of it. No 1/pi in D: SoF draws diffuse as lightmap x
  texture, so the lightmap holds E/pi (with the 1/pi the highlight stayed under frame noise).
* r_om_deluxe 1 default (strength; 3 is already too much), 0 off, -1 shows the directions, -2 the highlight alone x10.
  PC only (GLES builds without the atlas). Cost: kos1 1080p, one run on/off/on 8.76/8.76/8.90 ms GPU.
* The dynamic light highlight (omDlightsNew) lost its 1/pi too and uses the same Kelemen visibility (0.25/(v.h)^2).

## Wet blood marks (r_om_wetblood, owner's point 3, 2026-11-10)

* An alpha-blended mark used to write "no material" to the reflection target and erased the reflectivity under it.
  Blood marks (sprite bld*, *blood*) now write a wet material (reflectivity 0.45 x r_om_wetblood, roughness 0.18):
  the traced reflection and the lamp glints run over fresh blood. Other marks unchanged.
* Test aid `om_decal <sprite> [size] [alpha] [r g b]` (client fx): a mark where the view meets the world.

## Walls that must not shine (owner, 2026-11-10: "how can painted brick shine?", kos1 walls)

* Both highlights (baked r_om_deluxe, dynamic lights) now follow the class reflectivity: smoothstep(0, 0.2, refl)
  for the baked one, 15%..100% for the dynamic one. Damp sewer walls (refl ~0.03 from their dampness) had got a
  full dielectric highlight.
* om_materials.txt reviewed by eye for the glossy classes: concrete block (jpn1 kitchen 1_kitchblock*), white
  plaster (2_cleanwall1*), a slab and old planks were sorted as tiles/varnish/polished - now stone/wood/
  smoothcon (22 textures). The metal class (618) is not reviewed yet.

## r_om_motionblur: camera motion blur as a film shutter (goal: "Kojima-level cinematic effects", 2026-11-10)

* Composite pass (gl3_post.c MotionScene): the depth of the finished view (copied at EndView, the view weapon
  included) gives each pixel its view-space point (analytic: P00 P11 P22 P32 of the symmetric frustum); the
  previous frame's view-projection x the inverse view takes it to where it was. Up to 8 taps along that motion,
  centred and jittered per pixel (interleaved gradient noise), averaged in linear light (squares - the target
  holds display-encoded colour). Pixels nearer than 24 units (weapon, hands) are neither blurred nor sampled.
* r_om_motionblur = shutter as a share of a 60 Hz frame (0.5 = 180 degrees, 1/120 s): blur = motion x exposure /
  frame time, so it does not depend on the frame rate. r_om_motionblur_max caps the length (0.035 of the view).
  No blur on a cut (eye moved > 96 units or turned > ~45 degrees in a frame), with night vision, gl_ztrick.
* Captured right after SetupGL (GL3_Post_CaptureView), before a weapon's own projection.
* Test aid r_om_motionblur_test N: as if the camera had turned N degrees (N > 0) or moved -N units forward (N < 0)
  since the last frame - repeatable on a still frame. Real motion checked with a generated r_om_campath turn.
* Cost (1080p, composite pass, one run on/off/on): still camera 0 (one depth fetch, early out), 1 deg/frame
  +0.15 ms, 3 deg/frame +0.29 ms (taps by length: one per ~5 px, 3..8; was +0.75 ms with 8 fixed taps and pow).
* Presets: minimal/low 0 (phones), medium/high 0.5. Menu: Colour -> Motion Blur.

## The white wall over arm1's street: a map hole, rain feedback (r_om_skyfill, 2026-11-11)

* What it was: at omsetpos -408 46 109 -99 6 the map has a hole (no face at all along those rays, BSP face test).
  Nothing redrew those pixels, so they kept old frames (Quake's hall of mirrors: one second after the start the
  "SKY'S USED BOOKS" sign of the spawn view was still there), and the rain drops, which show the last frame
  (r_om_ssr's colour copy) x 1.3 plus its blur x 1.1, raised that stale image frame by frame to white - also
  reflected by SSR in the wet street. Found by: the white grew over seconds after r_om_ssr 1 (facade mean 14 -> 102).
* r_om_skyfill 1 (gl3_warp.c): the whole sky box after the world, depth-tested - it fills only what the world left
  at the clear depth (sky faces, holes); covered pixels fail early. The hole now shows the painted New York skyline.
* Rain drops: behind x 0.7 + blur x 0.25 (gain under 1, the loop through the last frame always converges) + the rain
  colour x 0.25 (not fed back) for their sheen.

## r_om_lens: chromatic aberration, flares, lens dirt (2026-11-11)

* All in the composite pass, no new targets. r_om_lens_ca: red and blue sampled apart along the radius, offset ~ r^2
  (none in the middle, early out under 0.5 px), 2 fetches. r_om_lens_flare: three ghosts of the bloom image mirrored
  through the centre (the bloom holds only emissive light - lamps, flashes, fire - so nothing else flares), each its
  own tint and scale, fading to the image edge, plus a thin halo ring at 0.42 of the height. r_om_lens_dirt: the bloom
  x (1 + dirt x smudge mask), the mask two octaves of value noise thresholded into blobs, evaluated only where the
  bloom is above 0.02.
* Cost (1080p composite, one run): all on +0.14 ms (0.70 -> 0.84), CA alone +0.04 ms. The first dirt mask (3x3 cells
  of random discs, 36 hashes a pixel everywhere) cost +0.6..0.9 ms.
* Defaults 0.5 / 0.35 / 0.3 (medium, high presets); minimal / low (phones) 0. Menu: Colour -> Lens Flares,
  Chromatic Aberration.

## r_om_hitfx: the screen tint in the composite, hits as a dark red vignette (2026-11-11)

* Before: r_newrefdef.blend as a flat 2D fill over the finished frame and the HUD (a hit: the whole screen red).
* Now the composite takes it (gl3_fx.c draws no fill when GL3_Post_TintHandled): a red tint (r > 1.5 g, 1.5 b) is a
  hit - colour drained (x 0.7 of the strength), the middle a little warmer, the edges (radius 0.18..0.95) sink into
  a dark red; a short chromatic aberration pulse (lens CA + 4 x strength). Any other tint (pickups, water) is the same
  mix the fill gave, under the HUD. Night vision and r_warnings keep the fill. Cost: none measurable (ALU only).
* Test aid r_om_hitfx_test r,g,b,a (commas: `set` takes one word) forces the game's tint every frame.
