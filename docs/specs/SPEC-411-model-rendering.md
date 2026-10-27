# SPEC-411: Model rendering — Ghoul models, inline brush models, placeholder model
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: ref_gl.dll (entity drawing, Ghoul link) and the Ghoul drawing code of SoF.exe, version 1.07f
Depends on: SPEC-002 (renderer interface, entity and frame layouts, render flags), SPEC-010 (Ghoul system interface,
GL entry-point table, light record), SPEC-011 (instance interface, render-parameter block, matrix), SPEC-012 (Ghoul
runtime: materials, textures, `.ifl` lists, shadows, billboards), SPEC-014 (Ghoul file semantics), SPEC-101 (BSP lump
20), SPEC-102 (`.ifl`), SPEC-410 (world rendering: light point, fog volumes, inline-model surfaces), SPEC-413
(renderer cvars)

## Purpose

This spec describes how the renderer draws the three kinds of scene entity (SPEC-002): Ghoul models, inline brush
models and the placeholder drawn for an entity without a model. For Ghoul models it covers both halves of the work:
what the renderer computes and hands to the Ghoul system (SPEC-011 render-parameter block), and how the Ghoul drawer
turns those parameters, the world lights of the map and the dynamic lights into vertex colours. Material state,
texture lookup, `.ifl` handling and the shadow mask construction are specified in SPEC-012 and are only referenced
here; where this spec disagrees with SPEC-012 the difference is listed under *Proposed amendments*.

## Interface / layout

### Cvars used by model rendering (all created by the renderer at start-up)

Flag 1 = archived. "Rounded" means ⌊value + 0.5⌋. Unless stated, a cvar is read every frame.

| Cvar | Default | Flag | Effect (details in *Behaviour*) |
|---|---|---|---|
| `ghl_light_method` | 2 | 1 | Lighting model of Ghoul models: 0, exactly 1, 2 or 3 (thresholds in §6.1) |
| `ghl_specular` | 1 | 1 | > 0.5 allows specular highlights (also needs `ghl_light_method` > 0.5) |
| `ghl_max_lights` | 3 | 1 | World lights per model for software lighting (rounded, clamped 1–8) |
| `ghl_fast_lights` | 0 | 1 | > 0.5 (and `ghl_light_method` < 3): world and dynamic lights become directional lights |
| `ghl_light_multiply` | 1.3 | 1 | Multiplies the model light colour; clamped to 0.25–2 |
| `ghl_gllight_local_viewer` | 1 | 1 | Hardware lighting only: OpenGL local-viewer light model |
| `ghl_gllight_directional` | 0 | 1 | Hardware lighting only: lights placed at infinity (w = 0) |
| `ghl_gllight_attenuation` | 1 | 1 | Hardware lighting only: linear attenuation (ignored when directional) |
| `ghl_gllight_max_lights` | 6 | 1 | Hardware lighting: world lights per model (rounded, clamped 1–8) |
| `ghl_shadows` | 2 | 1 | Shadow mode: 0 none, 1 blob, 2 projected silhouette (rounded, clamped 0–2) |
| `ghl_player_shadow` | 1 | 1 | < 0.5: the player's own model (RF_VIEWERMODEL) is not drawn at all |
| `ghl_shadow_darkness` | .75 | 1 | Shadow opacity; ≤ 0 disables shadows |
| `ghl_shadow_dist` | 25 | 1 | Distance of the virtual shadow light from the model origin (SPEC-012 shadows) |
| `ghl_shadow_tilt` | .25 | 1 | Never read in 1.07f: no effect |
| `ghl_obj_fog` | 1 | 1 | > 0.5: per-model fog amount at the model origin; otherwise the fog region's own parameters |
| `ghl_no_render` | 0 | 0 | > 0.5: no Ghoul model is drawn |
| `ghl_wireframe` | 0 | 0 | > 0.5: Ghoul models are drawn as outlines (polygon mode LINE) |
| `ghl_no_cull` | 0 | 0 | ≠ 0: no view-frustum culling of Ghoul models |
| `ghl_pre_cull` | 1 | 0 | > 0.5: the model's bounding box is tested against the view frustum before drawing |
| `ghl_no_texture_stack` | 0 | 1 | > 0.5: animated texture coordinates are ignored (SPEC-012) |
| `ghl_poly_fact`, `ghl_poly_unit` | -.1, -.1 | 1 | Polygon offset of blended model surfaces; read once at renderer start-up |
| `ghl_mip` | 0 | 1 | Texture detail reduction (rounded); read once at renderer start-up |
| `ghl_precache_verts`, `ghl_precache_texture` | 0, 1 | 0 | Created by the renderer, used only by the game module (SPEC-010) |
| `gl_dlightintensity` | 2.0 | 1 | ≤ 0.01: models get no dynamic lights and their own lights are not exported; 0: no light exchange with the Ghoul system at all |
| `r_drawentities` | 1 | 0 | 0: no entity of any kind is drawn |
| `r_fullbright` | 0 | 0 | ≠ 0: Ghoul models are drawn fully lit and without shadows |
| `gl_drawflat` | 0 | 0 | ≠ 0: entities are drawn without texturing |
| `gl_drawmode` | 0 | 0 | ≠ 0: brush entities are drawn translucent (alpha 0.4) without depth test (debug) |

The Ghoul drawer also has two lighting tunables set with the console command `gserver` (SPEC-012):
`LIGHT_SMOOTH` (light fade time T, seconds, clamped 0–6, **default 1.0**) and `LIGHT_AMBIENT` (ambient factor K,
clamped 0–1, **default 0.6**).

### Entity flags read by model rendering (SPEC-002 `flags`)

| Bit (value) | Name | Use here |
|---|---|---|
| 0 (0x1) | RF_MINLIGHT | §5 step 9 |
| 1 (0x2) | RF_VIEWERMODEL | drawn shadow-only (§5 step 6, §7) |
| 2 (0x4) | RF_WEAPONMODEL | no culling, no fog, drawn last |
| 3 (0x8) | RF_FULLBRIGHT | colour (1, 1, 1) |
| 4 (0x10) | RF_DEPTHHACK | depth range 30 % |
| 9 (0x200) | RF_GLOW | pulsing colour |
| 12 (0x1000) | — | the entity casts a shadow (§7) |
| 22 (0x400000) | — | Ghoul model |
| 28 (2²⁸) | — | ambient-only lighting (§6.1) |

### World light record handed to the Ghoul system

The world-light set (SPEC-010 system methods 34–36) is filled from BSP lump 20 (SPEC-101 *Light*). Each 36-byte lump
record becomes, or does not become, one 28-byte Ghoul light record (origin, radius, colour) by these rules:

| Lump field | Rule |
|---|---|
| `type` = 3 (sun / sky emitter) | never added |
| radius | `intensity` when `type` is 1 or 2 and the trailing int is 0 or 1; √`intensity` / 2 when `type` = 0 or the trailing int is ≥ 2 |
| radius > 1500 | not added |
| origin, colour | copied unchanged (colour 0–1) |

Examples: a point light with intensity 300 → radius 300; a surface-light patch with intensity 40 000 → radius 100; a
surface patch with intensity 10⁷ → radius 1581 → not added.

### GL entry-point table supplied to the Ghoul system

The renderer hands the Ghoul system (SPEC-010 layout G) its own OpenGL entry points, with these substitutions:

| Entries | Behaviour |
|---|---|
| Enable / Disable of GL_BLEND, GL_CULL_FACE, GL_ALPHA_TEST, GL_DEPTH_TEST, GL_FOG, GL_TEXTURE_GEN_S, GL_TEXTURE_GEN_T, GL_POLYGON_OFFSET_FILL; AlphaFunc, BlendFunc, DepthFunc | Deferred: recorded in the renderer's state cache and applied by entry 51 or the renderer's next draw. Other capabilities go straight to OpenGL |
| BindTexture | Goes through the renderer's texture-binding cache (so `gl_nobind` also applies to models) |
| TexParameteri | For GL_TEXTURE_2D: a MAG_FILTER value is replaced by the renderer's current magnification filter; a MIN_FILTER of LINEAR_MIPMAP_NEAREST is replaced by the renderer's current minification filter and a MIN_FILTER of LINEAR by its magnification filter. So Ghoul textures follow `gl_texturemode`; `_NOMIP` textures (NEAREST) are unaffected |
| GenTextures | The renderer's texture-name allocator (honours `gl_gentextures_broken`) |
| all others | Direct OpenGL calls |

## Behaviour

### 1. Start-up and shutdown of the Ghoul link

1. At renderer start-up, after creating the `ghl_*` cvars, the renderer calls system method 38 with `ghl_mip`
   rounded and method 39 with (`ghl_poly_fact`, `ghl_poly_unit`). Later changes of these three cvars take effect only
   at the next renderer start (`vid_restart`).
2. After the GL context exists: method 37 with the renderer's 256-entry image gamma table, method 1 with the GL table,
   then method 2 with "specular" = (`ghl_specular` > 0.5 and `ghl_light_method` > 0.5).
3. Renderer shutdown calls method 3 (all Ghoul textures released).

### 2. Per map

When the world is loaded the renderer calls method 36, then method 34 once per accepted lump-20 record (table above),
then method 35.

### 3. Per frame, before anything is drawn

Only when `gl_dlightintensity` ≠ 0 (it is first clamped to at most 4):
1. Every record of the system's "lights from models" list (method 28/29; filled while models were drawn in the
   **previous** frame, see §6.5) is appended to the frame's dynamic lights while fewer than 32 exist: origin, colour,
   intensity = radius × `gl_dlightintensity`.
2. Method 30 empties the model-light and dynamic-light lists.
3. If `ghl_light_method` > 0.5, every dynamic light of the frame (the engine's and those just appended) is given to
   method 31 with radius = intensity × 0.5.

### 4. Entity drawing order

All entities are drawn in the renderer's single sorted translucent pass (SPEC-410), mixed with alpha quads, quad
chains and translucent world surfaces, farthest first. The sort key is the squared distance from the view origin to
the entity origin (Ghoul and placeholder models) or to the entity origin plus the centre of the model's bounding box
(brush models). RF_WEAPONMODEL entities use key 0 and are therefore drawn last. Entities are drawn with depth writes on;
with `gl_drawflat` texturing is off. Nothing is drawn when `r_drawentities` is 0.

Dispatch: entity flag 0x400000 → Ghoul model (§5); otherwise model null → placeholder (§9); otherwise inline brush
model (§8). After every entity the renderer restores blending (SRC_ALPHA, ONE_MINUS_SRC_ALPHA, enabled), texture
environment MODULATE and smooth shading.

### 5. Drawing a Ghoul entity (renderer side)

The renderer fills the SPEC-011 render-parameter block and calls instance method 41. "Fullbright pass" below means
`r_fullbright` ≠ 0 or the frame has rdflags bit 0x4 (night vision).

1. **Skip.** Nothing happens when `ghl_no_render` > 0.5 or the Ghoul system has no instance for the low 16 bits of the
   entity's `frame` (system method 9).
2. **Cull object.** Entities with RF_WEAPONMODEL, and all entities while `ghl_no_cull` ≠ 0, get the cull object that
   never culls; others get the view-frustum test (a box is culled when all 8 corners lie behind one of the four
   frustum side planes; `r_nocull` does not affect it).
3. **Orientation.** The entity matrix is built as SPEC-011 states (rows forward, −right, up of the Quake angle vectors of
   (−pitch, yaw, −roll), then the origin).
4. **Pre-cull.** When `ghl_pre_cull` > 0.5, the entity is not a weapon model and `ghl_no_cull` = 0, the instance's
   bounding box at the frame's game clock (instance method 51, whole model, not exact) is tested with the frustum
   test of step 2; if culled, nothing is drawn.
5. **Shadow mode** = `ghl_shadows` rounded, set to 0 when negative or in the fullbright pass, set to 2 when above 2. If
   it is 0 or `ghl_shadow_darkness` ≤ 0, the mode is 0; otherwise the block gets darkness = `ghl_shadow_darkness`,
   the engine's world ray-test import and shadow distance = `ghl_shadow_dist`.
6. **Viewer model (RF_VIEWERMODEL).** If the shadow mode is 0 or `ghl_player_shadow` < 0.5 the entity is not drawn at
   all; otherwise it is drawn shadow-only.
7. **Option bytes:** time = frame game clock; fog visibility 1.0; view origin and view forward vector of the frame;
   specular = `ghl_specular` > 0.5 and `ghl_light_method` > 0.5; fast lights = `ghl_fast_lights` > 0.5 and
   `ghl_light_method` < 3; "casts shadow" byte = entity flag 0x1000; `ghl_no_texture_stack` > 0.5; "no dynamic
   lights" = `gl_dlightintensity` ≤ 0.01.
8. **Light colour and lighting options.** Let L(p) be the renderer's light point at p, sampled straight down (SPEC-002
   export 0xa4, direction 255).
   - RF_FULLBRIGHT or fullbright pass: colour (1, 1, 1); no other lighting option is set.
   - Else, if `ghl_light_method` equals exactly 1 and the entity lacks flag bit 28 (**probe method**): colour =
     L(origin). With B(p) = |L(p)| (vector length): gx = 4·(B(origin + (16,0,0)) − B(origin − (16,0,0))),
     gy = 4·(B(origin + (0,16,0)) − B(origin − (0,16,0))). If gx² + gy² ≤ 1 then gz = √(1 − gx² − gy²); otherwise
     (gx, gy) is normalised and gz = 0. With D = 500·(gx, gy, gz) and forward, right, up = the Quake angle vectors of
     the entity's **un-negated** angles, the directional vector = (D·forward, D·right, D·up) and "directional vector
     valid" is set.
   - Else: colour = L(origin). If the entity has flag bit 28 or `ghl_light_method` < 2, specular is switched off.
     Otherwise "use GL-style lights" is set and the maximum light count is `ghl_max_lights` (method ≤ 2.5), or, for
     method > 2.5, "hardware lights" is set, the OpenGL light model LOCAL_VIEWER is set immediately to
     (`ghl_gllight_local_viewer` > 0.5), "directional" = `ghl_gllight_directional` > 0.5, "attenuation" =
     `ghl_gllight_attenuation` > 0.5 and not directional, and the maximum is `ghl_gllight_max_lights`. The maximum is
     rounded and clamped to 1–8.
9. **Post-processing of the colour** (in this order):
   - × `ghl_light_multiply`, using 0.25 below 0.25 and 2 above 2 (also in the fullbright case: default 1.3);
   - RF_VIEWERMODEL: the directional vector is replaced by (−25·up.x, −25·up.y, 0), up = the view's up vector;
   - RF_MINLIGHT (0x1): if **all three** components are ≤ 0.1, the colour becomes (0.1, 0.1, 0.1); otherwise unchanged;
   - RF_GLOW (0x200): each component c becomes max(c + 0.1·sin(7·t), 0.8·c), t = the frame's client time (refdef
     0x44);
   - the final colour is written to the entity's light-output pointer if it is not null (SPEC-002 entity 0x58).
10. **Fog.** If the entity is not a weapon model, the world has fog volumes and the leaf containing the origin has a
    volume (SPEC-101 lump 21):
    - `ghl_obj_fog` ≤ 0.5: OpenGL LINEAR fog with start = (volume float at byte 12) + 90, end = (float at 16) /
      (float at 4), or the float at 16 when the float at 4 is 0, and colour = the volume's RGBA bytes / 255. The
      block keeps fog visibility 1.
    - `ghl_obj_fog` > 0.5 (and `gl_fogmode` ≠ 3): the per-point fog of SPEC-410 is evaluated at the origin, giving a
      fog fraction f (at most 1). If f > 0.01, OpenGL LINEAR fog is enabled from 0 to (depth of the origin along the
      view forward vector) / f in the current fog colour, so the model is fogged by about f at its own depth, and the
      block gets fog visibility 1 − f and shadow darkness × (1 − f). Otherwise no fog.
11. **Draw.** Face culling is set to cull GL_BACK, alpha test and blending are disabled and the colour is white.
    `ghl_wireframe` > 0.5 switches polygon mode to LINE for front and back faces during the call. RF_DEPTHHACK (0x10)
    limits the depth range to [near, near + 0.3·(far − near)] during the call. Instance method 41 is called.
12. **Restore.** Polygon mode FILL (if changed), depth range, face culling back to GL_FRONT (the world's setting),
    texturing on, fog off (if it was turned on in step 10), blending off, colour white, blend function (SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA), texture environment REPLACE, all cached texture bindings and texture environments
    forgotten, alpha test enabled with GREATER 0.05.

Night vision and `r_fullbright` therefore show models at full brightness, without shadows, with their own textures.

### 6. Lighting inside the Ghoul drawer

#### 6.1 Lighting model by `ghl_light_method` (m)

| m | Ambient term A | Directional lights | Dynamic lights | Specular |
|---|---|---|---|---|
| 0 ≤ m ≤ 0.5 (and any m that is not exactly 1 and < 2) | colour of the block | none | none when m ≤ 0.5; else as §6.4 | m ≤ 0.5: never; otherwise off (m < 2) |
| exactly 1 | 0.5 × smoothed colour | one wrap light (§6.2) | §6.4 | allowed |
| 2 ≤ m ≤ 2.5 | K × colour (K = `LIGHT_AMBIENT`, 0.6) | up to `ghl_max_lights` world lights (§6.3), vertex lighting in software | §6.4 | allowed |
| m > 2.5 | K × colour | up to `ghl_gllight_max_lights` world lights, OpenGL lighting (§6.7) | §6.4 | allowed |

Entity flag bit 28 forces the first row (ambient only, no specular) whatever m is. The colour of the block is
the post-processed colour of §5 step 9.

Vertex colour in software lighting (SPEC-012 materials): (A + Σ light contributions + self-illumination) × surface
colour, OpenGL clamping to 0–1. The model's own (file) lights also contribute (§6.5).

#### 6.2 Probe method (m = 1): smoothing and wrap light

Each instance keeps a smoothed colour S and a smoothed vector V. On the first draw after a reset (§6.3) they are set
to the block's colour and directional vector; on later draws S = 0.9·S + 0.1·colour and V = 0.9·V + 0.1·vector (per
draw, not per second). Then A = 0.5·S, and one **wrap light** with colour 0.5·S and unit direction V (taken as a
world-space direction and turned into the model's frame like the other lights; see Open questions) is added. Its
contribution at a vertex with unit normal n is colour × 0.75·(n·l + 0.5), not clamped (negative on the far side).

#### 6.3 World lights (m ≥ 2)

Candidates: the lights of the world-light set whose sphere contains the entity origin (distance ≤ radius), nearest
first (SPEC-012 light grid). Each instance keeps up to 32 light states (light, phase, phase start s); phases are
"fading in", "on" and "fading out". `now` is the block's time; T = `LIGHT_SMOOTH`; maxL = the block's maximum.

**Reset.** The per-instance lighting cache (light states, smoothing of §6.2, shadow direction of §7) is reset when the
instance was not drawn for more than 0.15 s, and on its first draw.

**Immediate mode** (T = 0, or first draw after a reset): the state list is replaced by the first maxL candidates, all
"on", each contributing at full colour.

**Smoothed mode** (otherwise):
1. All states are marked unseen. The first 2·maxL candidates are visited in order:
   - no state yet: a new state "fading in" with s = now is created (no contribution this draw);
   - "fading out": becomes "fading in" with s = 2·s + T − now;
   - "fading in" with e = now − s > T: becomes "on";
   - "on", or "fading in" with e > 0: contributes if the instance's light list holds fewer than maxL lights (this
     count includes the model's own lights of §6.5), with colour × e/T while fading in, full colour when on.
   - The state is marked seen.
2. Every unseen state that is not fading out starts fading out: s = now if it was on, s = 2·s + T − now if it was
   fading in.
3. A fading-out state with e = now − s > 0 has level f = 1 − e/T; it is deleted when f is outside 0–1 or
   |colour·f|² < 0.2; otherwise it contributes colour × f if the list holds fewer than maxL + 2 lights. With e ≤ 0 it
   contributes at full colour under the same limit.
4. Every state that did not contribute in this draw (and is not a fresh "fading in") is deleted. If more than 32
   states remain, the oldest are deleted.

**Light shape of a contributing world (or dynamic) light** with origin O, radius R and (faded) colour C:
- Normal lights are **point lights**: at a vertex p with unit normal n, d = O − p, q = |d|². Nothing when n·d < 0,
  q < 1 or q > R². Otherwise contribution = C × (n·d)/|d| × g, with g = 1 when q ≤ (0.75·R)², else
  g = 4·(R² − q)/R². (g jumps from 1 to 1.75 at 0.75·R and falls to 0 at R.)
- With fast lights (block option, m < 3) a light is used only when the entity origin P is within R of O, as a
  **directional light** from direction (O − P)/|O − P| with colour C × h, h = 1 when |O − P| ≤ 0.75·R, else
  (R − |O − P|)/(0.25·R); contribution = colour × max(n·l, 0) (nothing when n·l < 0).
- A light whose colour length is below 0.01 is ignored.

#### 6.4 Dynamic lights

Unless the block says "no dynamic lights" or the entity is drawn shadow-only, every light given to system method 31
in this frame (§3) is added to the instance's light list when its origin lies inside the instance's world-space
bounding box enlarged by the light's radius on every side, with no count limit, as a point or fast directional light
of §6.3 with its full colour. The bounding box used here is built only while gathering the model's own lights
(§6.5), i.e. only for models whose object (or a bolt-on) contains light nodes; for every other model the box is empty
and **no dynamic light reaches it** (UNVERIFIED in game, see Test vectors).

#### 6.5 The model's own lights

For instances whose object or bolt-ons contain light nodes, and unless "no dynamic lights" or shadow-only, every light
node active in the current pose is gathered before the world lights (SPEC-012 *Model lights*): non-world nodes are put
first in the instance's own light list; `WORLD*` nodes are exported to the system's "lights from models" list and
reach the renderer's dynamic lights in the next frame (§3).

#### 6.6 Specular

Per-surface specular needs the block's specular byte and the material conditions of SPEC-012. With r = the reflection
of the vertex-to-viewer direction about n, e = the material exponent and level = the specular level:
- point lights (only when they also passed the diffuse tests): when s = (d·r)/|d| > 0, add colour × s^e × level ×
  specular colour;
- the wrap light: v = the wrap light's vector taken as a position minus the vertex; when |v| ≥ 1, n·v ≥ 0 and
  s = (v·r)/|v| > 0, add 2 × colour × s^e × level × specular colour.
- directional lights: the same form, gated by the light's direction dotted with r being > 0 (exact base of the power
  UNVERIFIED).

#### 6.7 Hardware lighting (m > 2.5)

When the instance's light list is not empty, the first 8 lights become OpenGL lights 0–7 and OpenGL lighting is
enabled for the mesh; LIGHT_MODEL_AMBIENT = (A, 1). A world or dynamic point light sets DIFFUSE = SPECULAR = (C, 1),
POSITION = its origin in model space with w = 0 when "directional" else 1, LINEAR_ATTENUATION = 1/R when
"attenuation" else 0, QUADRATIC_ATTENUATION 0 (constant stays 1). OpenGL lights have no radius cut-off. Model lights
use SPEC-012's GL rules. Material colours, shininess and the extra specular-map pass are SPEC-012's. With an empty
list the software path is used (ambient only).

### 7. Shadows (refinements of SPEC-012)

- An instance casts a shadow only when the shadow mode ≠ 0 **and** (the entity has flag 0x1000 **or** its object is a
  character model, SPEC-012). If the instance does not cast a shadow, an RF_VIEWERMODEL entity is drawn normally
  instead of shadow-only.
- The shadow light direction (SPEC-012 shadows step 3) is updated **only** when the block's "use GL-style lights" byte
  is set (m ≥ 2) and the entity is not drawn shadow-only. With m < 2 (and always for the player's own model) the
  direction stays (0, 0, 1): the shadow falls straight down.
- Shadow-only drawing (player's own model) shifts the shadow horizontally by the block's directional vector, which
  §5 step 9 sets to (−25·up.x, −25·up.y): looking down by pitch θ moves the shadow 25·sin θ units backwards.
- `ghl_shadow_tilt` has no effect.

### 8. Inline brush-model entities

1. Nothing is drawn for a model with no surfaces.
2. Culling (unless `r_nocull`): unrotated entities use the model's box moved to the origin; entities with any non-zero
   angle use a cube of ± the model radius around the origin.
3. Orientation: translate to the origin, rotate by yaw about Z, by **−pitch** about Y, then by **−roll** about X (right-
   handed degrees). Quake II rotated brush models by +pitch and +roll; SoF reverses both, which matches the Ghoul
   orientation of §5 step 3.
4. The view origin is expressed in model space with the Quake angle vectors of (−pitch, yaw, −roll) (y component =
   −right·v).
5. Surfaces are drawn by the world-surface rules of SPEC-410 (texture animation from the entity's `frame`, dynamic
   lights, translucent surfaces). With `gl_drawmode` ≠ 0 the colour is (1, 1, 1, 0.4) with blending on and depth
   test off; otherwise (1, 1, 1).

### 9. Placeholder model

An entity without a model is drawn as an untextured double pyramid: apex (0, 0, −16), the four points (16·cos k·90°,
16·sin k·90°, 0) for k = 0…4 as a triangle fan, and apex (0, 0, 16) with the points in reverse order, in the entity's
orientation (translate, yaw about Z, −pitch about Y, −roll about X). Its colour is L(origin), or (1, 1, 1) with
RF_FULLBRIGHT.

### 10. Skins, `.ifl` lists and texture detail (answers to SPEC-102)

Skins, image lists and texture loading are specified in SPEC-012 *Textures*. They answer the SPEC-102 open
questions as follows.
1. 3ds Max extras are not supported: a line is one name (trailing white space removed, extension cut); anything else
   on the line becomes part of the name, so such entries are not found.
2. A missing entry keeps its index slot and is drawn with `GHOUL/COMSKIN/DEFAULT.TGA`; the console shows
   `Ghoul :<dir><name> not found`.
3. Only `<name>.TGA` is searched for an entry; `.m32` is never used for Ghoul textures.
4. There is no list frame rate: the image index is an animated per-frame value of the material slot, evaluated with
   the model's animation, plus the instance's skin index; it is clamped to the list (no wrap-around).
- Texture detail: the uploaded mip level starts at `ghl_mip` (read at renderer start-up; level sizes above 256 are
  skipped). Filters follow `gl_texturemode` through the GL table (Interface).

## Constants

| Name | Value |
|---|---|
| Light probe offsets / gradient scale / vector length | ±16 units in x and y / 4 / 500 |
| `ghl_light_multiply` clamp | 0.25–2 |
| RF_MINLIGHT floor | 0.1 (all components) |
| RF_GLOW | +0.1·sin(7·t), floor 0.8 × original |
| RF_DEPTHHACK depth range | 30 % of the normal range |
| Viewer-model shadow shift | −25 × view up (x, y) |
| Maximum lights | 1–8 |
| World light acceptance | type ≠ 3, radius ≤ 1500; radius = intensity, or √intensity / 2 for type 0 / trailing int ≥ 2 |
| Dynamic light radius given to Ghoul | intensity × 0.5 |
| Model-light export | radius × `gl_dlightintensity`; `gl_dlightintensity` clamped to ≤ 4 |
| Lighting cache reset | not drawn for > 0.15 s |
| Probe smoothing | 0.9 old + 0.1 new per draw; ambient and wrap colour 0.5 × smoothed |
| Wrap light | 0.75·(n·l + 0.5) |
| Point light | inner radius 0.75·R, outer factor 4·(R² − q)/R², minimum squared distance 1 |
| Fast light | full inside 0.75·R, linear to 0 at R |
| Fading | T = `LIGHT_SMOOTH` (default 1.0 s, 0–6); candidates 2·maxL; fade-out limit maxL + 2; minimum |C·f|² 0.2; at most 32 states |
| Ambient factor K | `LIGHT_AMBIENT`, default 0.6 (0–1) |
| GL light linear attenuation | 1/R |
| Restored renderer state after a Ghoul entity | alpha test GREATER 0.05, cull face GL_FRONT, blend SRC_ALPHA / ONE_MINUS_SRC_ALPHA |
| Placeholder | double pyramid, half-height 16, radius 16 |

## Test vectors / how to verify

1. **Lump 20 conversion.** Load `maps/dm/jpndm2` with a logging Ghoul system object (as in SPEC-010 test 3) and record
   method 34 calls: no record of type 3; every point light's radius equals its `intensity`; every type-0 record's
   radius equals √intensity / 2; no radius above 1500.
2. **Light multiply.** `r_fullbright 1`, `ghl_light_multiply 0.5`: models are drawn at half brightness;
   `ghl_light_multiply 0.1` gives quarter brightness (clamp 0.25).
3. **Probe method.** `ghl_light_method 1` in a room lit from one side: a soldier is brighter on the lit side; turning
   the soldier (yaw) changes which side is lit (see Open question 1).
4. **Method 0 vs 2.** `ghl_light_method 0`: models are flat-shaded with the floor light colour; firing a weapon next to
   them does not light them. `ghl_light_method 2`: the side facing a nearby lamp is brighter.
5. **Fades.** `ghl_light_method 2`, walk a model (or move the camera with a model) from one lamp's sphere into
   another's: the lamp's contribution fades over about 1 s. `gserver LIGHT_SMOOTH 0` makes it switch instantly.
6. **Dynamic lights on models.** `ghl_light_method 2`: fire a weapon next to an enemy whose model has no light nodes
   and next to a model with light nodes; per §6.4 only the second is lit by the muzzle flash.
7. **Shadows.** `ghl_shadows 2`, `ghl_light_method 1`: every character shadow points straight down; with
   `ghl_light_method 2` shadows lean away from the nearest world light. `ghl_shadow_tilt` changes nothing.
   `ghl_player_shadow 0` removes the player's own shadow.
8. **Fullbright pass.** Night-vision goggles: models fully lit, no shadows.
9. **Brush rotation.** A brush entity with angles (30, 0, 0): its +X end rises (nose up), opposite to Quake II.
10. **Placeholder.** An entity with a null model shows a 32-unit double pyramid in the floor light colour.
11. **Wireframe / depth hack.** `ghl_wireframe 1` draws model outlines; the view weapon (RF_DEPTHHACK) never
    intersects walls.

## Open questions

1. Frame of the probe direction (§6.2): the renderer expresses it in the entity's own axes (with the right-handed
   "right" axis), and the Ghoul drawer rotates it again as a world direction. Whether the lit side turns with the
   model's yaw (double rotation) must be checked in game (test 3).
2. §6.4 (dynamic lights only for models with light nodes) follows from the code path; confirm in game (test 6).
3. The fade reversal formula s' = 2·s + T − now does not keep the light level continuous; the visible effect when a
   light leaves and re-enters quickly is unverified.
4. Which game entities set entity flag 0x1000 (cast shadow) and flag bit 28 (ambient-only lighting).
5. Brightness jump of point lights at 0.75·R (g = 1.75 just outside): confirm visually with a slowly moving model.

## Proposed amendments

- **SPEC-002**, render flags: 0x1000 = "cast a shadow" (the Ghoul drawer draws a shadow for this entity even if its
  model is not a character); bit 28 (value 2²⁸, the last row of SPEC-002's render-flag table) = "ambient-only lighting: no light probe, no world lights, no specular".
  RF_MINLIGHT sets (0.1, 0.1, 0.1) only when all three components are ≤ 0.1.
- **SPEC-011**, render-parameter block 0x08: as RF_MINLIGHT above; 0x14: in the probe method the vector is in entity
  axes (§5 step 8); for RF_VIEWERMODEL entities it is (−25·up.x, −25·up.y, 0).
- **SPEC-012**: `LIGHT_AMBIENT` default is 0.6 (not 1.0) and it is the ambient factor K of §6.1; `LIGHT_SMOOTH` is the
  fade time T of §6.3; the shadow-direction smoothing 0.8/0.2 for shadow-only drawing is never reached (§7); the
  "Details UNVERIFIED" of world and dynamic light falloff are given in §6.3; the light node "world flag" consumer is
  §6.5.
- **SPEC-102**: open questions 1–4 are answered in §10.
