# SPEC-AMEND-016: Emissive, sun direction, world lights, fog and shadows - answers for GFX-modern §7

Status: approved (G, 2026-10-26) - produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: ref_gl.dll, SoF.exe (Ghoul drawer), map and model data, version 1.07f
Depends on: SPEC-012, SPEC-014, SPEC-100, SPEC-101, SPEC-410, SPEC-411, SPEC-412, SPEC-413
Amends: SPEC-100 (flag 0x1000 no longer UNKNOWN), SPEC-101 (lump 20 type 3 runtime use; worldspawn `_sun*` keys),
SPEC-410 (fog kind; terrain fog vs regions - clarifications only)
Evidence: static analysis of the 1.07f binaries (behaviour only), string inventories of all shipped binaries, scans of
all 75 stock maps (`.bsp` entity text, texinfo, lump 20, lump 21, lightmaps) and all 477 stock `.ghb` models. A
lightmap ray test (described in R16.2) was used to fit the baked sun direction of 41 maps.

Item kinds as in SPEC-AMEND-002.

## Summary

| Q (GFX-modern §7) | Answer |
|---|---|
| 1. Flag 0x1000 = glow? | **No.** It is time-based texture animation (SPEC-410). Such faces get normal lightmaps; 1.07f has no self-lit world surfaces at all |
| 2. Sun direction rules | Runtime **never reads** any `_sun*` key; they are map-compiler input baked into lightmaps. The compiler convention was recovered from the lightmaps (R16.2) |
| 2b. Lump 20 type 3 at runtime? | **No.** Skipped by the only consumer (Ghoul world-light set) |
| 3. Realtime sun shadows in 1.07f? | **None.** Lightmaps + Ghoul blob / projected-mask shadows only; stencil functions are loaded but never called |
| 4. Fog kind used? | **Unused** in every path that sets fog; confirmed |
| 5. Terrain fog vs region fog | They never meet: terrain fog is scoped to the terrain quads only; objects use region fog only. In stock data neither is active on `trn1` |
| 6. Ghoul self-lit materials | Yes: material self-illumination (P4) and additive blend. 175 / 1030 stock materials have P4 > 0, 70 are additive |

---

## R16.1 - Flag 0x1000 and other glow rules (Q1) (Resolution)

1. Texinfo / `.m32` flag 0x1000 means **"animate the texture chain at the `.m32` rate"**: frame index =
   floor(r × game clock), r = low byte of the base image's `.m32` `value` (SPEC-410 *Texture animation*). It has no
   lighting meaning. This resolves SPEC-100 open question 2: the "small `value` numbers" on 0x1000 textures are the
   animation rate in frames per second.
2. Lightmaps are built for every face whose texinfo has none of 0x4, 0x8, 0x800 (SPEC-410). 0x1000 faces therefore
   get a normal lightmap and are drawn as texture × lightmap like any wall. They are **not** fullbright and are not
   brighter than the lightmap allows.
3. SURF_LIGHT (0x1) also has **no runtime effect** on drawing (SPEC-410 flag table). Its only effect is at map
   compile time (the face emits light into the lightmaps). The face itself is drawn texture × its own lightmap.
4. Map data: 907 texinfos over all maps carry 0x1000 (top names: crate/screen/computer textures in Iraq, Tokyo
   clean-room panels, Uganda computers, castle red/green lights, Tokyo light signs, subway static, Iraq cameras);
   134 of them also carry 0x1 (they emitted light at compile time).
5. Full list of cases where 1.07f draws world or model surfaces **without** being darkened by lighting (the only
   "self-lit" rules that exist):
   - `r_fullbright` ≠ 0, or the night-vision render flag (rdflags bit 4): all world lightmaps off (SPEC-410);
   - liquids (0x8): no lightmap, vertex colour = light sampled below the vertex (lit, not fullbright);
   - alpha surfaces (0x800): no lightmap, vertex-lit (lit, not fullbright);
   - sky (0x4): sky box, not lit;
   - entity flag RF_FULLBRIGHT (colour 1,1,1) and RF_GLOW (pulsing colour) on models (SPEC-411);
   - Ghoul material self-illumination and additive blend (R16.7);
   - 2D/3D effect quads with the additive flag (SPEC-412 blend table) and the far fog wall.
6. There is **no** bloom, glow or HDR pass in 1.07f. Any bloom is a modern addition; choosing seeds is a C design
   decision. R recommendation (not original behaviour): seed from SURF_LIGHT faces (texture × `value`-scaled factor),
   0x1000 faces only where the C team wants screens to glow (a style choice, not 1.07f), additive effects, and Ghoul
   materials per R16.7.

Test: on `irq1a` / `tsr1` stand near a 0x1000 screen in a dark corner with `r_fullbright 0`: it is as dark as the
wall next to it (only its texture animates). With `r_fullbright 1` it becomes bright together with every wall.

## R16.2 - Worldspawn sun keys (Q2) (Resolution, data-derived)

### Runtime

1. No shipped binary (SoF.exe, ref_gl.dll, gamex86.dll, player.dll, sound modules) contains any `_sun` key text.
   The renderer and game never read `_sun`, `_sun_target`, `_sun_angle`, `_sun_vector`, `_sun_light`, `_sun_color`,
   `_sun_ambient`, `_sun_diffuse`, `_sun_diffade`, `_sun_surface` or `_sunlight`. They are **compiler input only**;
   their whole effect in 1.07f is what is baked into the lightmaps.
2. Therefore "exact runtime rules" do not exist. The rules below are the **compiler convention as observed in the
   baked lightmaps**, which is what a modern directional light must reproduce to match the look.

### Direction convention (fitted against lightmaps)

Method: for each map, 900-2500 random lightmap luxels on world faces that can see the sky were taken. For each
candidate direction toward the sun (azimuth step 15°, elevation 10°-89°), each luxel was ray-traced through the
BSP; a luxel is "sun-lit" if the ray reaches a sky brush side. The candidate with the largest mean-brightness gap
between sun-lit and shadowed luxels is the fitted sun. "Toward sun" below is the unit vector from a lit point to the
sun; the light travels the opposite way. Azimuth = degrees counter-clockwise from +X (toward +Y); elevation above the
horizontal plane.

| Key | Rule (toward sun) | Evidence |
|---|---|---|
| `_sun_angle "Y P"` (27 maps) | two numbers, yaw Y and pitch P in degrees (separated by one or more spaces). **The angles describe the direction the light travels**: travel = (cos Y cos P, sin Y cos P, sin P); stock P is always negative (downward). Toward sun = azimuth Y + 180°, elevation −P | irq1a 135/−30 → fit 315/30 (exact); irq1b 225/−45 → 45/50; kos2 54/−45 → 225-240/40-50; tsr1 45/−45 → 180-225/50; kos1 270/−45 → 90-135/30-40; tut1 30/−35 → 210-225 (azimuth only); tsr2 75/−30 → 270/30; jpn3 315/−45 → 120/20 (weak) |
| `_sun "name"` or `_sun_target "name"` (19 + 5 maps; same meaning, two spellings) | names an `info_null` (or `info_notnull`). The sun direction is given by a **`light` entity whose `target` is that name**: toward sun = normalise(light origin − info_null origin). The helper `light` has a tiny `light` value (1-20) | 13 of 13 maps tested match within ~20° in azimuth and elevation: arm3 242/52 vs fit 240/50; sud2 58/35 vs 60/40; nyc1 214/32 vs 210/30; sibcon1 315/50 vs 315/50; nycdm1 225/73 vs 225/70; sudctb1 347/40 vs 345/50; ger2 108/43 vs 90/40; sib1 225/79 vs 225/80; ger1 123/63 vs 105/80; gercon1 144/65 vs 150/70; irqctb1 291/68 vs 270/80; nycdm3 50/63 vs 60/60; jpn1 259/69 vs 270/70 |
| `_sun_vector "x y z"` (2 maps) | vector of the direction the light travels; toward sun = −v normalised | irqctf1 (−64 −48 −192) → toward 37/67, fit 15-45/70-80 (match). nycdm4 (−64 −64 −128) → toward 45/55, fit 135-165/60-70 (**mismatch**, weak sun `_sun_light 25`, night map; UNVERIFIED) |
| no direction key, `_sun_surface` present (jpn2, sib3) | fit is near vertical (80-89°): treat as **straight down** (UNVERIFIED) | jpn2 fit 75/80; sib3 240/80 (weak) |
| no direction key, no `_sun_surface` (sibctf1) | no directional sun visible in the lightmaps (only ambient/sky light) | fit gap negative |
| `_sun` naming a missing entity (ger3: `t14`, no such `info_null`) | no directional sun visible in the lightmaps | fit gap negative for every direction |

Notes on the fit
- Elevation from the target rule and the angle rule agree with the fit within the 10° fit grid in most maps; a few
  maps (jpn3) fit lower than the key says, probably because of large shaded courtyards. Use the key, not the
  fit, at runtime.
- When both a name key and an angle key would exist, priority is unknown (no stock map has both). UNVERIFIED.

### Other `_sun*` keys (observed values only; compiler meaning UNVERIFIED)

| Key | Maps | Observed value form |
|---|---|---|
| `_sun_light` | 54 | one number 25-250: sun intensity (kos1 has a sun direction but no `_sun_light`, so a compiler default exists; value unknown) |
| `_sun_color` | 54 | three floats 0-1, R G B |
| `_sun_ambient` | 51 | three numbers 0-255 (R G B) or one number (grey), e.g. "7 7 14", "30" |
| `_sun_diffuse` | 37 | one number 15-100 (sky diffuse amount) |
| `_sun_diffade` | 9 | always ".3" |
| `_sun_surface` | 5 | 0, 1 or 4 |
| `_sunlight` | 1 (jpn1) | "50"; probably a misspelling of `_sun_light`; whether the compiler honoured it is unknown |

R recommendation for CSM (C design choice): direction from the table above; colour `_sun_color`; strength from
`_sun_light` normalised so that 100-250 looks like the baked result; ambient from `_sun_ambient` / 255. Skip the
directional light on maps where the table says "no directional sun".

Test: `tools/formats/bsp.py topdown <map.bsp> out.png - light` on `irq1a`: wall shadows on the ground fall toward
azimuth 135° (away from the sun at 315°).

## R16.3 - Lump 20 type 3 at runtime (Q2b) (Resolution)

1. The only runtime consumer of lump 20 is the build of the Ghoul static world-light set at world load (SPEC-410
   *Model lights*, SPEC-411). Records of **type 3 are always skipped** there, as are records whose radius exceeds
   1500. Nothing else reads the lump at runtime (the engine's map loader only converts its byte order and hands it on).
2. Type 3 counts: 96 543 records over all maps; `trn1` alone has 776. They lie on a grid on sky faces and carry the
   sun colour; they only influenced the compiled lightmaps.
3. Therefore a modern renderer may use them (e.g. average colour as a sun-colour prior) without diverging from any
   1.07f runtime behaviour, but it must not add them as Ghoul point lights if it wants 1.07f model lighting.

## R16.4 - Realtime shadows in 1.07f (Q3) (Resolution)

1. The world is lit **only by lightmaps** (plus dynamic-light additions to lightmaps, SPEC-410). No world geometry
   casts or receives a realtime shadow.
2. The only realtime shadows are the Ghoul model shadows (SPEC-012 / SPEC-411 §7): a blob (mode 1) or a projected
   64×64 mask (mode 2) drawn flat on the ground plane under character models and entities with render flag 0x1000.
   Their light direction comes from the model lighting block (GL-style lights only) and defaults to straight down;
   it is **not** the map sun.
3. The renderer loads the OpenGL stencil entry points (stencil op / mask / func / clear) with the rest of the GL 1.1
   table but **never calls them**; there is no stencil shadow volume path and no shadow map.
4. Conclusion: CSM is a modern addition. To match 1.07f when CSM is off, keep the Ghoul shadow path (SPEC-411 §7).
   When CSM is on, Ghoul blob/mask shadows should be disabled for models that cast a CSM shadow, otherwise they double.

## R16.5 - Fog region kind field (Q4) (Resolution, confirms SPEC-410)

1. At map load, the region kind (offset 0: 1, 2, other) is converted to a fog-falloff selector (exponential,
   exponential-squared, linear respectively) and stored in the region.
2. Both places that set hardware fog for a region (interior faces with `gl_fogmode 1`, and point fog for models /
   alpha quads) always program **linear** fog and never read the stored selector. The overlay path (`gl_fogmode 0`)
   does not use OpenGL fog for faces at all.
3. So the kind is **unused in 1.07f**; SPEC-410 stands. Stock values are 0, 1 or 2 over 210 records; behaviour is
   identical for all three.
4. For a modern volumetric layer the kind may be honoured as a hint (exponential vs linear density), as a
   documented modern deviation.

## R16.6 - Terrain fog vs region fog (Q5) (Resolution)

1. Terrain fog (`.trn` header fog start/end/colour) is enabled **only around the terrain quads** in the world pass
   and disabled immediately after them; clouds are never fogged. It never applies to world faces, Ghoul models,
   effects or the HUD.
2. Region fog (lump 21) applies to faces of the region (overlay or linear per `gl_fogmode`) and to models / alpha
   quads by point fog (SPEC-410). Point fog reprograms the fog parameters per object, so there is no shared state
   that the terrain could leave behind for objects.
3. So when both exist, **objects get region fog only, terrain quads get terrain fog only**; there is no blending
   or priority rule. When terrain fog is active with its clear flag set, it also clears colour and depth before the
   quads (SPEC-410), erasing world drawn earlier in the frame.
4. Stock data: `trn1` (the only terrain map) has **no fog regions** (lump 21 empty) and `train.trn` has fog end 0,
   so **neither fog is active** on `trn1`. The question has no stock case.
5. Recommendation for a volumetric layer (C design choice): drive it only from lump 21 regions; treat terrain fog
   as a separate distance fog for the terrain strip.

## R16.7 - Ghoul self-illuminated and additive materials (Q6) (Resolution, data scan)

1. Material rules (SPEC-014 §4, SPEC-012): property P4 "self-illumination" is **added** to the lit colour (all
   channels) before multiplying by the surface colour/texture; blend mode 1 = additive (ONE, ONE), depth writes
   off. A material with P4 ≥ 0.99 also suppresses specular. There is no other "fullbright" material flag; image
   lists (`.ifl`) only change which texture is shown.
2. Stock scan (477 models, 1030 materials):

| Class | Count |
|---|---|
| additive (blend 1) | 70 |
| P4 > 0 | 175 |
| P4 ≥ 0.99 (effectively fullbright) | 52 |
| P4 animated (keyframed) | 44 |
| additive with P4 = 0 | 10 |

   Top folders: `objects/light` 54 (lamp sprite balls GLOW / GLOW_SPIKE / GLOW1-3, bulbs, emergency lights,
   candles, street lamps), `menus` 40 (menu-only 3D models), `objects/generic` 27 (car headlights, LIGHTS_TAXI,
   BLUESIREN / BLUECONE, train WINDOWS, TV screens), `items/pick-ups` 25 (ammo with P4 0.2 - a "readability" lift,
   not a light), `items/projectiles` 8 (C4 / claymore flare), `enemy/chopper` (NAV_BULB), `objects/siberia`
   snowcat LIGHTS, security camera LIGHTS.
3. Suggested bloom rule (C design choice, not 1.07f behaviour): contribute `texture × P4` for materials with
   P4 ≥ ~0.5, and the full colour of additive materials; ignore low P4 (≤ 0.25) used on pick-ups / props.
4. Caveat: "off" model variants (e.g. `objects/light/lamp_hang/lamp_off`, `street_old/off`) still contain the
   GLOW materials with P4 > 0. Whether the glow piece is drawn is decided by part visibility / model variant at
   runtime, so bloom must follow what is actually drawn, not what the file contains.
5. Related engine rules: entity RF_FULLBRIGHT and RF_GLOW (SPEC-411) and additive effect quads (SPEC-412 0x0200
   flag) are the other runtime "light-looking" sources.

Test: spawn `objects/light/emergency_blue` and a pick-up in a dark room: the bulb stays at full brightness, the ammo
is only slightly lifted (P4 0.2).

## Open questions

- nycdm4 `_sun_vector` mismatch (R16.2): confirm by viewing the map's ground shadows in game or with the top-down
  light render.
- Compiler default direction and intensity when keys are missing (jpn2/sib3 "straight down" and kos1 missing
  `_sun_light` are inferred only).
- Precedence between name and angle/vector keys (no stock map has more than one direction key).

## Gatekeeper notes (G, 2026-10-26)

- Gatekeeper edits: two wrong cross-references in R16.1 (items 5 and 6 pointed to R16.6, the Ghoul material rules
  are R16.7); the R16.2 test command now passes `-` for the texture root so that `light` is read as the render mode
  (`tools/formats/bsp.py` usage). Nothing removed. Lint clean; manual review found no tool-generated symbol names, code
  addresses, code or copied tables. Statements about the binaries are behaviour or string-inventory results only.
  The per-map sun numbers in R16.2 and the material counts in R16.7 are measurements of shipped map / model data.
- For C (GFX-modern, `docs/impl/GFX-modern.md` section 7 is answered by this amendment):
  - Bloom, owner rule "only what really glows": default seeds are SURF_LIGHT faces, Ghoul materials with high
    self-illumination (R16.7 item 3) **as actually drawn** (R16.7 item 4: "off" lamp variants still carry GLOW
    materials), additive materials and additive effect quads, RF_FULLBRIGHT / RF_GLOW entities. Flag 0x1000 is
    texture animation, not glow (R16.1): do not seed bloom from it by default. Screen glow, if ever wanted, is a
    separate opt-in.
  - Sun / CSM: read the `_sun*` keys from the map entity text at load time (this is map data from the user's paks,
    not SoF.exe). Do not hard-code the per-map fit table from R16.2; it is evidence only. Maps with no visible sun in
    the lightmaps (sibctf1, ger3) get no directional light. jpn2 / sib3 "straight down" and nycdm4 are UNVERIFIED:
    the CSM must look acceptable there or be skipped for them until checked in game.
  - 1.07f has no realtime sun shadows (R16.4). CSM stays default off. When on, disable the Ghoul blob / mask shadow
    for models that cast a CSM shadow, and avoid double-darkening where the lightmap already has the sun shadow.
  - Lump 20 type 3 records must not become Ghoul point lights (R16.3); using them as a sun-colour hint is allowed.
  - Fog kind is unused in 1.07f (R16.5); honouring it in the volumetric layer is a modern deviation and must be
    recorded in `docs/DEVIATIONS.md` when implemented. Volumetric fog is driven by lump 21 regions only; terrain
    fog stays a separate distance fog on the terrain quads (R16.6).
- SPEC-100 open question 2 (flag 0x1000 / small `value` numbers) is answered by R16.1 item 1.
