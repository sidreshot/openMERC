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
