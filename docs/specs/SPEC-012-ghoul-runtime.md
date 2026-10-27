# SPEC-012: Ghoul runtime — objects, caches, playback, evaluation, bolts, traces, drawing
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f (Ghoul model system internals behind the interfaces of SPEC-010/011)
Depends on: SPEC-010 (system, object, player-model interfaces), SPEC-011 (instance interface, matrix and hit
layouts), SPEC-014 (file decoding), SPEC-120/121 (data formats), SPEC-013 (network and savegame images),
SPEC-002 (renderer entity/refdef)

## Purpose

SPEC-010/011 give the method tables. This spec gives the behaviour behind them that other modules and the
player can observe:
- how names become ids;
- what is cached and for how long;
- how a time becomes a pose;
- how vertices, normals and bolt matrices are interpolated;
- when events fire;
- how bolt-ons compose;
- what ray and box tests hit, and with which tolerances;
- how instances are drawn (materials, lighting, shadows, billboards);
- how the server, client and menu systems differ.

It also lists corrections to SPEC-010/011 found while specifying the internals (section "Amendments").

## Interface / layout

### Conventions
Matrices are SPEC-011's 68-byte row-vector matrices; A·B applies A first. "Pose" = a frame pair (A, B) with weight
w of A (section 4). Names are upper-cased, with `\` → `/`, unless stated otherwise.

### Caches and limits (all shared by the server, client and menu systems unless stated)
| Cache | Key | Lifetime | Notes |
|---|---|---|---|
| Model-file cache | path upper-cased, `\`→`/`, extension removed (`ghoul/enemy/meso/meso_player.ghl` and `…MESO_PLAYER.GHB` are one entry) | reference-counted. With "CacheGHB" on (default) a file stays loaded at count 0 until a purge (system method 7); with it off it is freed at once | a failed load leaves no entry |
| Import #62 file buffers (`.gsq`, other text) | upper-case `Ghoul/<dir>/<file>` | until process exit | a **missing** file is cached too (length −1), so a file added later is not seen until restart |
| Player-model text (`.gpm`/`.gbm`) | upper-case path | until process exit | a missing file is **not** cached (retried every load); a changed file is not re-read |
| Image lists (`.ifl`) / images (`.tga`) | model directory + name (upper-case, extension removed) | reference-counted; destroyed at count 0; purge releases images not used since the last purge; renderer release (system method 3) destroys all | section 7 |
| Decoded vertex chunks ("prepared-mesh cache") | one block per chunk | one 3 MiB ring heap, first-in-first-out eviction | flushed by system methods 33 and 36, `gserver FLUSHMESHCACHE` |
| String table | per system | cleared only by system method 8 | index 0 = "" (SPEC-013) |

Limits of the system:
- object and instance ids 1…32766 (SPEC-010);
- string-table indices 16-bit;
- strings in files at most 100000 bytes;
- textures power of two ≤ 1024, uploaded at most 256 pixels per side;
- event processing at most 2 cycles per call;
- at most 8 OpenGL lights;
- 16 shadow textures in rotation.

## Behaviour

### 1. Model objects: sequences, parts, notes, materials, skins
**Sequence registration** (object method 0, path `ghoul/<dir>/<NAME>.ghl`):
1. The path is normalised and added to the system's string table before loading, even if loading then fails.
2. If a model file already loaded *by this object* contains a sequence named NAME (files searched in the order they
   were first loaded; exact comparison with the upper-cased NAME), that sequence is used.
3. Otherwise the model file `<dir>/<NAME>.ghb` (the last 4 characters of the path replaced by `.ghb`, resolved through
   the engine's full-path callback) is taken from the model-file cache or loaded, and the path denotes **that
   file's first sequence (index 0), whatever its name**. Example: `meso_player.ghl` loads `MESO_PLAYER.ghb` and gets
   its sequence 0. Later `.gsq` lines such as `cch_ecch2walk_n_a_n` are then found inside it by rule 2.
4. Success: id = new sequence count. Failure: 0, and no id is consumed. The exception is objects received from the
   server (client and menu systems), which still consume the id so client ids stay aligned with server ids when a
   file is missing locally. Such a placeholder sequence is never found by lookups.
5. With `alsoOthers` ≠ 0, every sequence of that file is then also registered as `<dir>/<SEQUENCE NAME>.GHL` in
   file order.
6. A path containing `GHOUL/ENEMY` (or `GHOUL\ENEMY`) marks the object as a **character**. This affects shadows
   (section 8) and `gserver SIZEMATTERS`.
7. A loaded file with light nodes marks the object as **having model lights** (section 8).

**Finishing registration** (object method 1) builds three sorted sets over all loaded files and registers them in
**ascending byte order**, so ids are alphabetical, not file order:
- the node names (nulls, meshes, cameras, lights);
- the note names: the event types, upper-cased, plus `BOS` and `EOS`;
- the material names.

With allSkins, the skins are then registered: for every material in id order, the entries of its image list in
ascending order. Example: nodes `WBOLT_HAND_R`, `ABOLT_HEAD`, `_BALDHEAD` get ids 1 `ABOLT_HEAD`, 2 `WBOLT_HAND_R`,
3 `_BALDHEAD` (`_` sorts after letters). For a weapon with types `fire`, `sound`, `altfire`, the notes would be ALTFIRE 1,
BOS 2, EOS 3, FIRE 4, SOUND 5 (UNVERIFIED: this assumes the type names are upper-cased before sorting; check with
object method 12).

**Register methods** (parts 6, materials 15, skins 19):
- They **always append an entry and consume the next id**, even for a duplicate name or a name no loaded file has.
- They return the new id when some loaded file has the name, else 0.
- Notes (11) are de-duplicated: an existing name returns its old id.
- The part type comes from the first file that has the node. The same name with a different type in another file is
  **fatal**: `Ghoul :name Collision between different object types`.
- Skin names have their extension stripped, are upper-cased and are looked up in the material's image list (the
  `.ifl` named by the material's slot 0 texture).
- An unknown skin name gives "no skin", unless it starts with `CUSTOM\` or `CUSTOM/`. Such a name is appended to
  the list as a new image (custom player skins, loaded from `ghoul/custom/…`).

**Lookups:**
- Find sequence/part/material/skin build a sorted table on their **first call and never rebuild it**, so a name
  registered after the first lookup of that kind is not found.
- Find note always uses the live map.
- All lookups are case-insensitive through the upper-casing.

**Part on/off defaults:** all parts of a new instance are on. `.gsq` "parts off" lists and the `.gpm` part block
switch meshes (SPEC-010 #66, SPEC-121).

**Damage and gore** are data conventions handled by the game module through these generic methods; the engine has
no gore logic. Gore zones are meshes `GZ_*` with material `GZ` skins. Caps are meshes `_CAP_<A>_<B>_<type>` (material
`C`, skins `c_black`/`c_blood`), attached at bolts `GBOLT_CAP_<A>_<B>_<type>`, and switched with part on/off
(instance method 8) and per-part skins (instance method 11).

### 2. `.gpm` / `.gbm` parsing details (refines SPEC-010 D and SPEC-121)
- White space is form feed, vertical tab, tab, CR, LF and space. `{` is skipped like white space everywhere. `//`
  starts a comment that runs to the next form feed, vertical tab, CR or LF.
- A token is `"…"` (anything up to the next `"`; no escapes; may be empty) or a run of characters up to white space,
  `{`, `}` or `//`.
- When the next significant character is `}`, reading a token consumes it and reports "end of list"; the field is
  left empty.
- Between the seven header tokens, a single `}` met before any other brace is also skipped. So a header value
  written `{word}` gives `word` and `{}` gives "".
- Skin list: triples (material, skin, **part**), up to the first empty token or end of list. **The third token is a
  part name**; a token beginning with `0` means "whole model". This answers SPEC-121 open question 1: the stock
  "0" is "no part".
- Bolt-on list: quadruples (file, parent bolt, bolt-on bolt, scale), scale = C `atof`. Part list: pairs (part,
  value), shown iff C `atoi(value)` ≠ 0. Then one trailer token.
- `.gbm` files use the same grammar: their brace-less skin triples run to the end of the file.
- Field sizes (bytes including NUL; **no bounds checks**): dir 100, menu path 100 (formatted with a 100-byte
  limit), base 100, set 100, team 50, sound dir 100, face prefix 50, trailer 200; skin entry material 50, skin 100,
  part 50.
- "Loaded" (method 17) means the file could be read; it does not depend on the parse.

### 3. Playback time and frame pair (refines SPEC-011 "Playback")
An instance holds:
- S: start time;
- A: auxiliary time, the time of the last Play, Pause, Resume, switch-on or event restart;
- the paused and reverse flags;
- mode m, duration D, frame time F and frame count N.

A new instance has paused = 1, which Play clears.

**Fraction f(t):**
- paused → t := A;
- off or no sequence → f = 0;
- D = 0 (mode 3, other modes, or zero-length sequences) → f = A, which holds the mode-3 fraction;
- otherwise d = t − S, and:
  - mode 0: f = frac(d/D);
  - mode 1: f = d/D;
  - mode 2: g = frac(d/D), f = 2g for g < 0.5, else 2 − 2g;
- reverse → f := 1 − f;
- finally f is clamped to [0, 1].

**Frame pair** (N frames):
- mode 0: x = N·f, A = ⌊x⌋, B = A + 1 (0 when A + 1 = N), weight of A w = (A + 1) − x;
- other modes: x = (N − 1)·f, A = ⌊x⌋, B = A + 1 if A < N − 1, else A = B = N − 1 and w = 1;
- N = 0 → (0, 0, 1).

**Dominant frame:** the frame with weight ≥ 0.5. If w < 0.5 the pair is swapped and w := 1 − w. Exactly at 0.5
frame A dominates.

**Side effects to reproduce:**
- Switching on (instance method 4), and methods 48 and 49, set A := t. This also moves a paused instance's frozen
  time and **replaces a mode-3 fraction** with the time value, which then clamps to 1 (UNVERIFIED visible effect).
- Method 27 stores the queried sequence's frame count N in the instance, which changes frame selection until the
  next Play.
- Resume(t): S += t − A, then A := t.
- Pause sets a pause-end value of 1e30 that playback never reads; it is carried in network and save images only.

### 4. Pose evaluation
| Quantity | Rule |
|---|---|
| Vertex positions | w·P_A + (1 − w)·P_B per component (chunked or int16 data decoded per SPEC-014) |
| Vertex normals | w·N_A + (1 − w)·N_B per component, **not renormalised**. Non-chunked: each frame's table direction; chunked: the reconstructed vectors |
| Animated UVs | the int16 values are blended, then scaled |
| Mesh visibility, image index (bool/u8 tracks) | value of the **dominant** frame. A mesh therefore switches visibility at w = 0.5 |
| Material f32/vec3 properties | blended |
| UV matrix | blended element-wise when A and B are adjacent frames of one sequence, else frame A's matrix |
| Node (bolt, mesh pivot, light) transforms | rotation elements and quantised positions blended linearly, **no re-orthonormalisation** (SPEC-014 §12) |
| Exact bounds, box traces, ray traces | dominant frame only, no blending |

### 5. Events (refines SPEC-011 "Events")
Processing (instance method 45 at time t) requires a sequence, the instance being on, and note processing enabled.
The cursor time becomes t at once.

1. **Matrix events** (if matrix processing is on): one call for every registered (bolt, mode) pair, in ascending
   bolt then mode order, whose bolt exists in the current sequence's file.
   - The matrix is computed as in section 6, without parents. For bolt-ons the root is the parent's hanging bolt
     times the parent's transform, so bolt-on matrices are in the top instance's entity space.
   - The 68-byte matrix belongs to the engine and is freed after the call.
2. **Notes:** let the raw phase r(t) = (t − S)/D, unwrapped. In mode 1 it is 1.0 once d > D; paused or D = 0 gives
   A. Let r0 be the stored cursor phase.
   - If r(t) < r0, nothing is delivered and the cursor phase is not updated. Time going backwards is ignored until it
     passes r0 again.
   - Otherwise the cursor phase := r(t) and span = r(t) − ⌊r0⌋, capped at 2.
   - The keys delivered are those of the current sequence with frac(r0)·D·1000 ≤ key time (ms) < span·D·1000. Each
     key's type is looked up as a note (case-insensitive), and every callback registered for that note is queued
     with data = the argument text ("" when none).
   - Then span −= 1. If span > −0.0001, `EOS` is queued (data ""). The next pass starts at the first key with limit
     span·D·1000, and passes repeat while span > 0.
3. Bolt-ons are processed the same way, in attach order and recursively, after their parent. A bolt-on needs its own
   note flag; its matrix events need both its own and its parent's matrix flags.
4. All queued callbacks are called after the traversal, in queue order. The arguments are (undefined, user value,
   cursor time, data).

Consequences of these rules:
- At most two cycles are delivered per call.
- Mode 1 delivers `EOS` exactly once.
- Mode 2 delivers keys only during the forward part of the cycle (the first (N − 1)/(2N − 1) of it), and one `EOS`
  per full back-and-forth.
- The reverse flag does not change note order.
- Mode 3 never fires.
- `BOS` is registered but **never delivered**.
- **Key times are not scaled by the speed index, while D is.** With speed multiplier m < 1, keys later than m × the
  nominal length never fire; with m > 1 keys fire early (UNVERIFIED in game).
- Registering the same (callback, note) twice gives two calls per event.

Restart rules:
- Play(t): cursor time := t, cursor phase := r(t).
- Methods 48/49 and switching on: cursor time := A, cursor phase := r(A), recursively for bolt-ons using their own A.

### 6. Bolt matrices and bolt-ons (exact composition; refines SPEC-011 "Bolt matrices")
Let T be the instance transform. Let Ba be the instance's own attach-bolt matrix in model space at the current
pose (the bolt set by method 28/29), and Bb the requested bolt's matrix in model space. Define XF:
- XF = T if there is no attach bolt or it is not in the current sequence's file;
- otherwise XF = Ba⁻¹·T. XF⁻¹ = T⁻¹·Ba.

Method 50 (time, out, bolt, matrix mode, throughParents):
- The output is first set to the identity. It **stays the identity** when there is no sequence, the instance is
  off, the sequence's file is missing, or the bolt is not in the current sequence's file (correcting SPEC-011's
  "left unchanged").
- Pose modes (bit 2 = 1):

  | Bits | Result |
  |---|---|
  | bit 0 = 0, bit 1 = 0 | Bb |
  | bit 0 = 0, bit 1 = 1 | Bb⁻¹ |
  | bit 0 = 1, bit 1 = 0 | Bb·XF |
  | bit 0 = 1, bit 1 = 1 | XF⁻¹·Bb⁻¹ |

- throughParents (low byte of the fifth argument) with a parent: P = parent's method 50 for the bolt this instance
  hangs on, with the same mode and flag. The result becomes own·P (non-inverse) or P·own (inverse).

  So a bolt-on point p reaches root entity space as p·Bb_c·Ba_c⁻¹·T_c·Bb_p·Ba_p⁻¹·T_p·…, where c = child,
  p = parent and Bb_p = the parent's bolt the child hangs on.
- Rate modes (bit 2 = 0) require D > 0, else they give the identity.
  - Let p = f. In mode 0, if f > (N − 1)/N − 0.001 then p = (N − 1)/N − 0.001 and the step goes backwards. In other
    modes the step goes backwards if f + 0.001 > 1.
  - M0 = Bb at p, M1 = Bb at p ± 0.001.
  - Δ = M1·M0⁻¹ forwards, M0·M1⁻¹ backwards.
  - Result = Δ (model only) or Δ·XF, then scaled by diag(s, s, s, 1) with s = 1/(0.001·D), i.e. "per second".
  - Mode 1 (the game's `quake_origin` use) gives the bolt origin's displacement per second through XF. T's
    translation is included.
  - The inverse rate modes (2, 3) give meaningless results; do not use them.
- Several children may hang on one bolt. They are evaluated, drawn and traced in attach order. Method 28 does not
  check whether the child already has a parent.
- Tint with bolt-ons (method 16) affects direct children only.

### 7. Hit testing and bounds
**Ray test (instance method 43):**
- An instance that is off or has no sequence gives 0 hits.
- R = XF·root, where root = identity for the top instance and, for bolt-ons, the parent's bolt matrix × the
  parent's R.
- The ray is taken into model space through R⁻¹, and the model-space direction is normalised. The distance scale
  s = length of R's first column.
- Early reject against the model file's bounding box (all frames). An empty box gives no own hits; bolt-ons are
  still tested.
- Per mesh, in surface order, these are skipped:
  - meshes whose part is off;
  - no-block meshes;
  - meshes invisible in the dominant frame;
  - meshes whose own box the ray misses. The box test fails if the start lies beyond the box on the side the ray
    moves away from, or if the largest |direction| component is < 0.02; otherwise the entry and exit points on the
    dominant axis are compared with the box.
- Triangles come from the index lists, using the **dominant frame** vertex positions. For each triangle
  (p0, p1, p2):
  - n = (p2 − p1) × (p0 − p1);
  - miss if |D·n| < 0.01;
  - t = n·(p0 − S)/(D·n); miss if t < −0.001 or t > 1e30;
  - the hit point H = S + t·D must pass all three edge tests ((p_{k+1} − p_k) × (H − p_k))·n ≥ 0. So triangles are
    **two-sided** and edges are inclusive.
- **Every** intersected triangle gives a record (no per-mesh minimum):
  - part = the object part of that mesh;
  - distance = s × t;
  - normal = R applied to normalise(n), zero if |n| ≤ 1e-10;
  - point = start + distance × the caller's direction, which must be unit length for the point to be right.
- Records from all levels are merged sorted by distance (ties keep insertion order), and the first `max` are
  returned.

**Swept-box test (instance method 44; no caller in the original modules):** arguments (time, root matrix, start, end,
mins, maxs, hits, max).
- The box is moved from start to end in instance space. The file box is rejected first, with tolerance 1.0.
- For each visible, traceable triangle of the dominant frame:
  - if the box at the start already overlaps the triangle, the fraction is −1 ("start solid");
  - otherwise box corners are tested against the triangle, triangle vertices against the box faces, and box edges
    against the triangle edges, giving the smallest fraction < 1.
- The fraction is backed off by one unit of distance: frac·(L − 1)/L, or 0 when L·frac < 1, with L = the move length.
- Records are (instance, part, fraction, two vectors: the unit normal (zero if degenerate) and a second vector,
  UNVERIFIED), sorted by fraction.

**Bounds (instance method 51):**
- exact = 0, part 0: the model file's box (all frames, not per sequence or frame) through R, combined with every
  bolt-on's box by the same rule.
- exact = 0, part n: the mesh record's own box, only when the part is a mesh; no bolt-ons.
- exact = 1, part n: the posed vertices of that mesh at the dominant frame.
- exact = 1, part 0: the instance's own meshes are **not** measured, so the result is mins = maxs = (0,0,0)
  (original quirk).

### 8. Drawing (instance method 41)
**Traversal.**
- Off or no sequence: per-instance render caches are reset and nothing is drawn.
- Otherwise the R composition of section 7 is used per level. The render-cull object (SPEC-011) receives the 8
  world-space corners of the file box; when it answers "outside", that instance is skipped (its children are still
  visited).
- Parts are filtered by part flags. Skins are chosen per material as: per-part instance override, then whole-model
  instance override, then the object default.
- Each draw advances a corner stamp, so shared corners are computed once per draw.

**Draw path per mesh.**
- Shadow-only entities (render option byte 0x35) use a positions-only pass that feeds the shadow (below).
- Hardware lighting (`ghl_light_method` > 2.5, byte 0x31) with lights present uses OpenGL lights GL_LIGHT0 + i,
  i < 8.
- Otherwise lighting is done in software per vertex.
- Texture environment MODULATE.

**Materials** (SPEC-014 §4):
- A surface is skipped when opacity × instance alpha < 0.01.
- Back faces are culled unless the material is two-sided.
- Base colour: diffuse colour × tint for untextured surfaces; white × tint for textured ones. The tint counts when
  it differs from white by more than 0.1 in squared distance.
- Alpha = opacity × instance alpha.
- Additive materials:
  - with fog visibility < 1: alpha × fog visibility, and fog is disabled for the surface;
  - with alpha < 1: colour × alpha, then alpha = 1.
- Blending (depth writes off, polygon offset with system method 39's values, default (−0.1, −0.1), alpha test
  "> 0") is used when the material is alpha-texture, or alpha < 0.99, or the blend mode ≠ 0. Additive uses (ONE, ONE);
  others use (SRC_ALPHA, ONE_MINUS_SRC_ALPHA).
- Specular is drawn when the system specular flag (system method 2 / object method 2) is on and glossiness > 0 and
  specular level > 0.051 and self-illumination < 0.99. The exponent is 2^(10·glossiness), and slot 1's texture (if
  any) is the specular map.
- Environment slots use sphere-map texture coordinate generation.
- Non-static UV transforms go through the GL texture matrix, unless `ghl_no_texture_stack` (option byte 0x36), in
  which case UV animation is ignored.

**Textures** (lookup for a slot texture name N in model directory D; D upper-cased with `/`; N upper-cased,
extension removed when longer than 4 characters):
1. An empty N means no texture.
2. `D N.IFL` is used if it exists and is non-empty. Its lines are the images: split at LF, CR or NUL; empty lines
   skipped; trailing white space removed but not leading; extension removed; upper-cased; image index = line order.
3. Otherwise `D N.TGA` is used as a one-image list.
4. Otherwise steps 2–3 are repeated in `GHOUL/COMSKIN/` (D cut after `GHOUL` + one character, then `COMSKIN/`).
5. Otherwise the console prints `Ghoul :<dir><N> not found` and the slot is untextured.

Further rules:
- List entries starting with `CUSTOM\`/`CUSTOM/` load from `GHOUL/CUSTOM/…`.
- A missing image in a list becomes `GHOUL/COMSKIN/DEFAULT.TGA`, with the console line `Ghoul :<D><N> not found`. If
  DEFAULT itself is missing: fatal `Ghoul :Default Texture Not Found`.
- Image drawn = the per-frame image index + the instance's skin index for that material, clamped to the list (no
  wrap-around). Animated images rebind when the index changes.
- Object method 2 loads image 0 of each textured slot without an explicit skin, or every image when
  `gserver PRECACHEALL` is on.

TGA rules:
- Types 2 and 10 only, 24 or 32 bits, no colour map. The fatal messages are `Ghoul :LoadTGA: Only 32 or 24 bit
  images supported (no colormaps): <name>`, `Ghoul :LoadTGA: Only type 2 and 10 targa RGB images supported:
  <name>`, and `Ghoul :LoadTGA: Bitmap is not a power of 2 (1,2,4,8,16,32,64,128,256): <name>`.
- Width and height must be powers of two ≤ 1024.
- The descriptor's origin bit is ignored. Rows are always treated as stored bottom-up: the last stored row is the
  top of the picture, v = 0. A top-down TGA therefore appears upside down.
- Type 10 (RLE) is decoded with width and height exchanged (quirk; harmless for square images; all stock ghoul TGAs
  are type 2).
- RGB passes through the 256-byte gamma table (system method 37) at load; alpha is unchanged; 24-bit images get
  alpha 255. The format is GL_RGB unless some alpha ≠ 255, then GL_RGBA.
- A full 2×2-average mip chain is built: each channel = (a+b+c+d+2)>>2, with source coordinates taken modulo the
  size.
- Upload starts at level `ghl_mip` (system method 38; 0 for menu objects) and skips levels larger than 256 pixels.
- A name containing `_NOMIP` uploads one level with NEAREST filtering. Otherwise MAG = LINEAR and MIN =
  LINEAR_MIPMAP_NEAREST.
- Wrap is REPEAT/CLAMP from the slot's tile flags.
- Without a GL table (dedicated server) no image is decoded or uploaded. Geometry is still loaded, because traces
  need it.

**Lighting.**
- Render-parameter light colour and direction (SPEC-011 block):
  - When the directional vector is valid (byte 0x2d), both are smoothed per instance (new = old·0.9 + current·0.1)
    and the colour is halved.
  - The per-instance light cache is reset when the instance was not drawn for more than 0.15 s.
  - With GL lights (byte 0x2e) and no directional vector, the colour is scaled by a lighting tunable
    (`gserver LIGHT_AMBIENT`, default 1.0; UNVERIFIED mapping).
- Software vertex lighting: base term (ambient), plus Σ over the instance's lights of colour × factor, plus
  self-illumination, all × tint. For **model (file) omni lights**, with d = light position − vertex,
  dist = |d| and n the vertex normal:
  - the light is ignored when dist > end, dist < 1 or n·d < 0;
  - factor = (n·d)/dist × multiplier × falloff, with falloff = 1 for dist ≤ start, else (end − dist)/(end − start);
  - specular (when enabled and (d·Rv)/dist > 0, Rv = the reflection of the vertex→viewer vector about n):
    colour × ((d·Rv)/dist)^exponent × specular level × multiplier × specular colour;
  - spot lights multiply by a cone factor that is linear from 0 at cos(falloff/2) to 1 at cos(hotspot/2).

  World and dynamic lights use point-light falloff with a radius² cutoff, and one light class uses wrap lighting
  (N·L + 0.5)·0.75. Details UNVERIFIED.
- GL lights: diffuse = specular = colour. Position w = 0 when `ghl_gllight_directional` (byte 0x32), else 1. Linear
  attenuation 1/start for model omni lights and 0.75/radius for world/dynamic lights, only when
  `ghl_gllight_attenuation` (byte 0x33); quadratic 0.
- World lights (system methods 34–36) are kept in a grid of 32 cells per axis. The extent per axis is max(span of
  all light boxes, 10000). A query returns the lights of the point's cell whose distance ≤ radius, ordered by
  increasing distance. Lights affecting a mesh are limited by the render parameter "maximum lights" (1–8).

**Model lights** (resolves SPEC-010 open question):
- While an instance or bolt-on whose object has light nodes is drawn (unless the renderer set "no dynamic lights",
  byte 0xac, or the entity is shadow-only), each light node active in the current pose is used as follows:
  - **world flag 0**: it lights only this model (added to the instance's own light list);
  - **world flag 1**: a 28-byte record (world-space position, radius = attenuation end, colour) is **appended to the
    system's "lights from models" list** (system methods 28/29).
- The renderer copies that list into its dynamic lights (SPEC-010 renderer frame protocol), so `WORLD_*` lights of
  models light the world one frame later.

**Shadows** (render-parameter shadow mode 1 = blob, 2 = projected mask). Drawn only for **character** objects
(section 1) or entities with flag 0x1000 (option byte 0x34), when the shadow mode ≠ 0.
1. Trace 500 units straight down from the entity origin (render-parameter world ray test). A fraction f < 0 means no
   shadow.
2. Darkness = shadow darkness × (1 − f). Ground height = origin.z − 495·f.
3. Light direction: the unit vector from the origin to the first world light found for the origin.
   - Its z is clamped: below −0.95 it is ignored; below 0.75 it is set to 0.75, with x, y rescaled so that
     x² + y² = 0.4375.
   - It is smoothed per instance: new = old·0.95 + current·0.05, or 0.8/0.2 for shadow-only drawing. It starts at
     (0,0,1).
4. k = clamp(24.75·f, 0, 1). The light point = origin + shadow distance × normalise(dir·k + (0, 0, 1 − k)).
5. Projection basis along origin − light point (if its squared length < 0.01, (1,0,0) is used). The two axes are
   scaled by 32/size, with size = 0.25 × the largest box extent, capped at 60. Coordinates are truncated and
   clamped to −30…29 in a 64×64 mask (centre 32).
6. Mode 2:
   - every drawn triangle stamps 10 points into the mask with value 255: the 3 corners, the centroid, the 3 edge
     midpoints, and 3 points at (2a + b + c)/4;
   - the mask is box-blurred twice inside the touched rectangle (3×3 sum / 8, clamped 255) and uploaded as a 64×64
     ALPHA texture (a ring of 16 textures);
   - it is drawn three times, offset by (0,0), (−1,−1) and (+1,+1) texels, with alpha = darkness/3.
7. Mode 1: one fixed 64×64 ALPHA texture with alpha = clamp(trunc(255·(1 − (x² + y²)/2048)³), 0, 255) for
   x, y = −32…31. It is stretched over the projected rectangle with alpha = darkness.
8. Both modes draw on the plane z = ground height with blend (ZERO, ONE_MINUS_SRC_ALPHA), depth writes off,
   polygon offset (−3, −3), alpha test > 0 and colour (1,1,1,alpha). The state is restored afterwards.

**Billboards** (node sprite flags, SPEC-014 §7). When the node is visible:
1. The node matrix is applied.
2. Rotate by (−atan2(−v_z, −v_x)·180/π − 90)° about local Y, where v = the unit vector from the node to the viewer
   in node space.
3. For spriteball, also rotate by atan2(−v_y, √(v_x² + v_z²))·180/π about local X.
4. The inverse node matrix is applied, and the geometry is drawn.

**Statistics** (system methods 40–48):
- A texture counts once per render frame (method 40), and adds width × height × 2 of its full-size image to
  method 41, i.e. bytes at 16 bits per texel.
- Meshes and triangles are counted as drawn.

### 9. System roles: server, client, menu
- All three share the model-file, texture, text and decoded-chunk caches. Each has its own objects, instances, ids
  and string table.
- Server: objects get positive ids. With network clients, creations and removals are queued per client
  (SPEC-013).
- Client and menu: objects and instances received from the server use negated ids (SPEC-010). Received objects
  keep sequence ids aligned even when files are missing (section 1).
- Menu: object method 2 always uses texture detail 0.
- Purge (system method 7):
  1. It frees the decoded-chunk cache.
  2. Unless it is called on the menu system while neither the server nor the client system owns an object, it
     deletes every cached model file with count ≤ 0, purges unused images, and re-prepares every client and menu
     object (object method 2 with the current specular flag). Server objects are not re-prepared.
- `gserver SIZEMATTERS v` (clamped 0.01–100, default 1.0) scales new character instances with id ≥ 16 and no parent
  (debug).

### 10. Debug command `gserver` (complete)
Argument words are upper-cased. Toggles take `!` (toggle), `1` (on) or `0` (off); with no argument the state is just
printed.

| Sub-command | Effect |
|---|---|
| RELOAD | deletes every cached model file regardless of references; live objects then point to freed data (UNVERIFIED crash) |
| SIZEMATTERS v | section 9 |
| IMAGELIST | verbose: `Loaded Maps` + one line per image list, `Loaded Textures` + one line per image; always `%d Maps, %d Textures` |
| USEAGE / USAGE | per cached model file: `Usage Report For: <source path>`, every node, camera, mesh and light never evaluated since load, `End of Usage Report For: <source path>` |
| STATS, LEAKAGE, MEM, MEM_START, MEM_END, SPEED | no effect |
| TEST [t] | unused flag; `Test is On` / `Test is Off` |
| LIGHT_SMOOTH / LIGHTSMOOTH [v] | lighting tunable, 0…6, default 1.0; prints `LIGHT_SMOOTH = %4.2f` |
| LIGHT_AMBIENT / LIGHTAMBIENT [v] | lighting tunable, 0…1, default 1.0; prints `LIGHT_AMBIENT = %4.2f` |
| PRECACHE_ALL / PRECACHEALL [t] | load every image of every list at prepare time (default off); `PreCacheAll is On/Off` |
| AUTOPREP / AUTO_PREP [t] | prefer exporter sources (SPEC-014 §11; default off); `AutoPrep is On/Off` |
| CACHEGHB / CACHE_GHB [t] | keep unreferenced model files (default on); `CacheGHB is On/Off`; switching off frees unreferenced files at once |
| FLUSHMESHCACHE / FLUSH_MESH_CACHE | frees the decoded-chunk cache; `Mesh Cache Flushed` |
| LISTFILES / LIST_FILES | `%d total ghoul files have been loaded.` then one name per line |
| SUMMARY | per system `%s  %d obj   %d inst` for Server, Client, Menu |

Unknown sub-commands do nothing.

## Constants
| Name | Value |
|---|---|
| Decoded-chunk cache | 3 MiB ring heap, FIFO, 20-byte block header; chunk size (n_apos + n_anorm)·n·12 bytes |
| Dominant frame threshold | weight ≥ 0.5 |
| Rate step | 0.001 of the cycle |
| Note limit | ≤ 2 cycles per call; EOS threshold −0.0001 |
| Ray test | direction minimum 0.02 (box pre-test); |D·n| < 0.01 miss; t ∈ [−0.001, 1e30]; normal zero below 1e-10 |
| Swept box | file-box tolerance 1.0; back-off 1 unit |
| Material thresholds | skip < 0.01 alpha; blend < 0.99; specular level > 0.051; self-illumination < 0.99; tint 0.1 |
| Light active | brightness ≥ 0.01 and multiplier ≥ 0.01 |
| World-light grid | 32³ cells, minimum extent 10000 per axis |
| GL lights | ≤ 8; linear attenuation 1/start (model omni) or 0.75/radius |
| Light smoothing | 0.9/0.1 (render colour/direction), reset after 0.15 s |
| Shadow | trace 500, ground 495·f, k = 24.75·f, mask 64×64, 32/size, size = 0.25·max extent ≤ 60, clamp −30…29, blob 1/2048 cubic, 16 textures, 3 passes at darkness/3, polygon offset (−3, −3), z clamp −0.95 / 0.75, x²+y² = 0.4375, smoothing 0.95/0.05 (0.8/0.2 shadow-only) |
| Textures | power of two ≤ 1024; upload ≤ 256 per side; `_NOMIP`; `DEFAULT.TGA` |
| `gserver` defaults | SIZEMATTERS 1.0 (0.01–100), LIGHT_SMOOTH 1.0 (0–6), LIGHT_AMBIENT 1.0 (0–1), PRECACHEALL off, AUTOPREP off, CACHEGHB on |

## Test vectors / how to verify
1. Ids are alphabetical: register `ghoul/enemy/meso/meso_player.ghl`, finish registration, and look up parts
   `ABOLT_HEAD_T` and `WBOLT_HAND_R`. The first has the smaller id. Note `BOS` has a smaller id than `EOS`.
2. Register method 6 called twice with `WBOLT_HAND_R` returns two different ids. Method 6 with `NOPE` returns 0, and
   the part count still grows by one.
3. Lookup freeze: Find part `X` (absent), then Register `X` on a file that has it, then Find again → 0.
4. Events with D = 1.0, mode 0, Play at t = 10:
   - method 45 at 10.5, 11.1, 13.4 delivers EOS 0, 1 and 2 times;
   - method 45 at 13.0 then 12.5: the second call delivers nothing;
   - then 13.6: only keys in [0, 0.6 s) fire, and no EOS.
5. `BOS` callbacks are never called.
6. Bounds: method 51 with exact = 1, part 0 returns (0,0,0)/(0,0,0). With exact = 0 it returns the file box through
   the transform.
7. Method 50 with a bolt absent from the current sequence's file returns the identity, even if the output was
   pre-filled.
8. Bolt chain: a weapon attached with method 28 at `WBOLT_HAND_R`, own attach bolt `TO_WBOLT_HAND_R`. Method 50
   (pose, with transform, throughParents) on the weapon's origin equals Bb_c·Ba_c⁻¹·T_c·Bb_p·T_p.
9. Ray test against a character from 100 units in front:
   - every triangle crossed gives a record, so one ray can hit the front and the back of the torso → ≥ 2 records,
     sorted;
   - no-block meshes and parts switched off never appear.
10. Texture lookup: a material slot `FOO` in `ghoul/enemy/meso/` with neither `FOO.IFL` nor `FOO.TGA` there or in
    `ghoul/comskin` prints `Ghoul :GHOUL/COMSKIN/FOO not found` once and draws untextured.
11. Model lights:
    - a model with a `WORLD_OMNI` light node, drawn with `gl_dlightintensity` ≠ 0, lights the surrounding world
      (renderer dynamic light with radius = attenuation end);
    - the same light renamed so its world flag is 0 lights only the model.
12. Shadows: a non-character model (path outside `ghoul/enemy`) casts no shadow unless its entity has flag 0x1000.

## Open questions
- Exact software-lighting ambient base and the use of the LIGHT_SMOOTH / LIGHT_AMBIENT tunables.
- Light kinds 1–4 (unused by stock data).
- The second vector of swept-box records.
- Visible effect of the mode-3 fraction being overwritten by methods 4/48/49, and of speed-index note drift (in-game
  check).

## Amendments to approved specs (proposed)
- **SPEC-010 B:**
  - Methods 6, 15 and 19 always consume an id. Their result is 0 when no loaded file has the name.
  - The part-type collision is fatal (`Ghoul :name Collision between different object types`), not a console print.
  - Lookup tables (sequence, part, material, skin) are built once and never rebuilt. Note lookup is always current.
  - Ids are assigned in ascending byte order of the names.
- **SPEC-010 A:**
  - The "lights from models" list is filled while instances are drawn, with every active light node whose world
    flag is 1 (records: position, radius = attenuation end, colour).
  - Statistic 41 = Σ width × height × 2 over textures bound once per frame.
- **SPEC-010 D / SPEC-121:** the third token of a `.gpm` skin triple is a part name (`0` = whole model).
- **SPEC-011:**
  - Method 50 returns the identity (not "unchanged") when the bolt is absent.
  - `BOS` is never delivered.
  - Mode 2 delivers notes only in its forward part.
  - Key times are not scaled by the speed index.
  - Method 51 with exact = 1 and part 0 returns zero bounds.
  - Exact = 0 uses the per-file box (not per sequence or frame).
  - The attach-bolt composition is exact as in section 6.
  - Option byte 0x34 (entity flag 0x1000) enables shadows for non-character models.
  - Method 44 is a swept-box trace.
