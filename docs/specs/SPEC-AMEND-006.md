# SPEC-AMEND-006: Answers to the clean team's open questions of 2026-10-25 (round 2)
Status: approved (G, 2026-10-25) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: SoF.exe, gamex86.dll, player.dll, ref_gl.dll, Defsnd.dll, version 1.07f
Depends on: SPEC-001, SPEC-002, SPEC-003, SPEC-006, SPEC-010–014, SPEC-040, SPEC-101, SPEC-202, SPEC-204, SPEC-301, SPEC-302, SPEC-310, SPEC-410–413, SPEC-600–602, SPEC-700, SPEC-710, SPEC-711, SPEC-720–722, SPEC-730–732, SPEC-750, SPEC-760, SPEC-761, SPEC-770, SPEC-771, SPEC-780–782, SPEC-790, SPEC-791, SPEC-803, SPEC-AMEND-001, SPEC-AMEND-005
Amends: SPEC-002 (frame record 0x48–0x50, entity), SPEC-202 §2/§2.4/§5, SPEC-310 §12/§13, SPEC-012 §8, SPEC-411 §6.7, SPEC-010 system method 24 (I1c items); SPEC-003, SPEC-600, SPEC-601, SPEC-602 (I7 items); SPEC-780 §1/§5, SPEC-001 RunFrame, SPEC-751 §2.1, SPEC-760 B1.4/B9.3, SPEC-761 §8, SPEC-782 §7, SPEC-AMEND-005 G18.27 (G18 items 29–35); SPEC-700, SPEC-710, SPEC-711, SPEC-722, SPEC-750 §12 (G2 items 10–16); SPEC-720, SPEC-721, SPEC-722 (G3 items 16–22); SPEC-730, SPEC-731, SPEC-732 (G4 items); SPEC-770, SPEC-771, SPEC-721 §13, SPEC-760 (G7 items); SPEC-790, SPEC-791, SPEC-722 bolt-on registry (G9 items 12–16); SPEC-204 §5, SPEC-302, SPEC-803 §3 (I2d items); SPEC-410, SPEC-413 §7 (I4c items); SPEC-040 §1.1/§1.3/§3.2, SPEC-AMEND-001 A, SPEC-301 command line (I5s-2 items); SPEC-741 §14 bot editor (F5 items)

## Purpose

Answers the items of `docs/specs/OPEN_QUESTIONS.md` that SPEC-AMEND-005 did not answer, including the items added while this amendment was written. It covers the sections "I1c engine client: the Ghoul client system" (items 1–9), "I7 player module" (items 1–17), "G18 game module" items 29–35, "G2 character AI" items 10–16, "G3 damage model, gore, bodies" items 16–22, "G4 roster, helicopters, vehicles, special characters" (items 1–10), "G7 props and server effects" (items 1–11), "G9 game modes and Ghoul glue" items 12–16, "I2d GameSpy lobby chat" (items 1–8), "I4c renderer" (items 1–4), "I5s-2 sound backend behaviour" (items 1–6) and "F5 SPEC-AMEND-005 follow-ups" (items 1–4). Each answer was
decided from the 1.07f binaries. Several items report a black-box observation of the original that contradicts an
approved spec. For each of them the item says plainly which one is right, the spec or the observation.

Items are numbered by section prefix and by the item number used in OPEN_QUESTIONS (`I1c.n`, `I7.n`, `G18.29`,
`G2.n`, `G3.n`, `G4.n`, `G7.n`, `G9.n`, `I2d.n`, `I4c.n`, `I5s-2.n`, `F5.n`), so that the gatekeeper can mark each one. Item kinds are
as in SPEC-AMEND-002:
- **Correction**: the old text (or OpenMerc's interpretation, or the black-box observation) is wrong. An
  implementation that follows it differs visibly from the original.
- **Resolution**: the old text was UNVERIFIED, silent or an open question, and the answer is now known.
- **Refinement**: the old text is true but incomplete in a way that matters for matching the original.

Every item names its target spec and section. It quotes or paraphrases the old text ("OpenMerc interpretation: …"
where the spec was silent), gives the new text, and says whether OpenMerc's current choice matches the original. It
ends with a test (in-game, black-box probe or data check). Where only running the game can decide, the item says so
and gives the black-box test. Observations that need no change are acknowledged in one line.

## Behaviour

### A. I1c engine client: the Ghoul client system (items 1–9)

#### I1c.1 SPEC-002 frame record 0x48–0x50 and SPEC-202 §5 — the game clock (Resolution and Refinement)

- Old: SPEC-002: "0x48 Game clock from the previous frame; 0x4c game-clock delta; 0x50 game clock in seconds. It
  stops while the game is paused." SPEC-202 §5 describes the client's game-time estimate. OpenMerc interpretation:
  0x50 is that estimate. The clean team asked for confirmation.
- New: confirmed. Offset 0x50 is the client's game-time estimate of SPEC-202 §5 itself, not a separate clock. The
  exact rules are:
  1. **Pull at every svc_frame.** The rule runs while the frame message is parsed, after the delta-reference checks
     and before the area bits. It runs for every frame, also for a frame that is then marked invalid. With t = the
     frame's game time: if |estimate − t| ≤ 0.5, then estimate := 0.95 × estimate + 0.05 × t; otherwise
     estimate := t.
  2. **Advance per client frame.** Once per client frame the estimate grows by the frame's duration in seconds
     (milliseconds × 0.001). This is the **unclamped** duration: the cap of the client frame time to 1/`cl_minfps`
     (SPEC-310 §12.10) does not apply to it.
  3. **Frozen.** The estimate does not advance while the cvar `paused` ≠ 0, or the cvar `freezeworld` ≠ 0, or the
     client is not in the active state. The pull of rule 1 still happens.
  4. **Frame record.** The three values are written only when the view is rebuilt, which is when a valid frame exists
     and (the refdef is forced or `paused` = 0), i.e. the same condition under which entities are added:
     - 0x48 := the value 0x50 had at the previous rebuild;
     - 0x50 := the estimate;
     - 0x4c := 0x50 − 0x48.
     When the view is not rebuilt (paused), the record keeps its old values, so 0x4c keeps its last non-zero value
     and is not zero.
  - The same estimate is sent in the user command's fire-time fields (SPEC-202 §5).
  - The observed lead of 0.29–0.30 s over the client time (0x44) follows from these rules. The client time is
    clamped to [frame time − 100 ms, frame time] of the server frame number, while the estimate tracks the game's
    level time.

  OpenMerc **matches**, provided its advance uses the unclamped frame time and freezes on `freezeworld` too.
- Verify: `cl_minfps 30`, `cl_maxfps` low (e.g. 10 fps): over 10 s of play the game clock (0x50) keeps pace with the
  svc_frame game time without being pulled back in steps. `freezeworld 1` in a running game: 0x50 stops and then
  jumps back to the frame time (difference > 0.5 s) after `freezeworld 0`.

#### I1c.2 SPEC-002 entity and SPEC-310 §12/§13 — how the client fills packet and view-weapon entities (Refinement)

- Old: SPEC-002 lists entity fields 0x38, 0x44–0x4c and 0x5c as UNVERIFIED; SPEC-310 §13 covers the view weapon
  only. Observed by the clean team: packet Ghoul entities carry pitch and roll negated (auto-rotating ones: pitch 0,
  auto-rotate yaw, roll negated); frame = oldframe = handle; alpha 1.0; a light-output pointer; byte 0x5c = 255;
  the viewer's own entity has flags 0x400002 at the view origin after the 1/16 nudge; the view weapon is a zeroed
  entity at the view origin before the nudge.
- New: add to SPEC-310 §12 (and point to it from the SPEC-002 entity table):

  **Order.** Each client frame, while the view is rebuilt, the packet entities are added **first**. Then the view
  values are computed (SPEC-310 §12, which adds the view weapon), then the effects. Then the view origin is nudged by
  +1/16 on each axis.

  **Packet entities.** The 96-byte entity is built in one scratch record per frame. The record is set to zero once
  before the first entity, and its byte 0x5c is set to 255. Fields not written for an entity keep the value written
  for the entity before it. Entities are processed in the order of the frame's entity list. For each entity:
  1. Alpha (0x48) := 1.0. Fields 0x44 and 0x4c stay 0. Byte 0x5c stays 255.
  2. Frame (0x34): the state's frame, unless one of the auto-animate effect bits 0x1078 is set. Then, with
     a = trunc(2 × client time in ms / 1000): bit 0x20 → a; else bit 0x40 → trunc(client time in ms / 100); else
     (a AND 1), plus 2 with bit 0x10, the sum halved (integer) with bit 0x1000 (Quake II rules).
     Old frame (0x38) := the previous state's frame. For a Ghoul entity whose instance id did not change, frame =
     old frame = the instance id. Back-lerp (0x3c) := 1 − the interpolation fraction.
  3. Origin (0x28): the current origin with render flag 0x40; otherwise interpolated from the previous to the
     current origin. The interpolated origin is also kept per entity (Quake II "lerp origin").
  4. Only when render flag 0x400000 (Ghoul) is **clear**, the model, skin, skin number and box are written:
     - model index 255: the model and skin come from the player-info entry of slot = skin number (the base player
       info when either is missing), skin number := 0;
     - otherwise skin := none, skin number := the state's skin number. The model is the model of the secondary model
       index, unless that index is 255, when the model of the model index is used. An index of 0 gives model 0 (no
       model, drawn as the placeholder). SPEC-202 §2 says "non-zero and not 255"; the correct rule is "not 255".
     - the box (0x10, 0x1c) is unpacked from `solid` when solid ≠ 31.
     For a Ghoul entity these five fields keep the previous entity's values. The renderer does not use them for Ghoul
     entities, so an implementation may write zeros.
  5. Angles (0x04):
     - **auto-rotate** (effects 0x2000): pitch and roll are interpolated from the previous to the current state
       (shortest way round, ±180). They are **not** negated. The yaw is the auto-rotate yaw (I1c.7).
     - **otherwise**: all three angles are interpolated the same way. Then, when the secondary model index is
       non-zero, pitch := −pitch, roll := −roll, and yaw := (interpolated yaw + the state's yaw offset), quantised to
       1/65536 of a turn (truncation toward zero of yaw × 65536/360, taken modulo 65536) and returned in [0, 360).
       Every spawned game entity has secondary model index 255 (SPEC-760), so in practice every non-rotating packet
       entity is negated this way, Ghoul or not.
  6. Flags (0x54) := the state's render flags.
  7. Render flag 0x80 (sound only): the entity is not added and its effect slots and effect bits are not processed.
     Otherwise the effect slots and the effect bits are processed with this entity record (SPEC-400) before it is
     added.
  8. **Viewer's own entity** (entity number = own client slot + 1): flag 0x2 is added. A non-Ghoul own entity is
     **not added at all**. A Ghoul own entity gets origin := the frame record's view origin as the **previous**
     rendered frame left it, i.e. after that frame's 1/16 nudge. This is because the packet entities are added before
     the view is computed. With a still view this equals the current nudged view origin; while moving it lags one
     frame.
  9. Light-output pointer (0x58) := a 3-float vector kept per entity number. The renderer writes the entity's
     light colour into it (SPEC-002).
  10. The entity is added when its model index ≠ 0 or it is a Ghoul entity.

  **View weapon** (SPEC-310 §13, completed): it uses its own record, which is zeroed **every frame** before it is
  filled. So alpha 0, light-output pointer none, byte 0x5c = 0, and 0x44/0x4c = 0. Then:
  - origin := the view origin of this frame, before the nudge (the weapon is added during the view computation);
  - angles := the view angles with pitch and roll negated;
  - frame := the view-weapon instance id;
  - back-lerp := 1 − the interpolation fraction;
  - flags := 0x400015;
  - then the sway of §13 is applied.

  OpenMerc **matches** the observation in the static case. It **differs** in two points:
  - the viewer's own entity uses the current frame's nudged origin, not the previous frame's;
  - "roll negated" for auto-rotating entities: the original does not negate them. The observed "negated roll" can
    only have been a roll of 0 (e.g. −0.0 or 0.0). Do not negate auto-rotating entities.
- Verify:
  - Spawn an item with effects 0x2000 and angles (10, 0, 20): the RenderFrame trace shows pitch 10 and roll 20, not
    −10 and −20.
  - A `misc_*` Ghoul prop with angles (10, 30, 20): pitch −10, yaw 30 (quantised), roll −20.
  - Walk forward with `ghl_shadows 2`: the own-entity origin in the trace equals the previous frame's refdef
    vieworg.

#### I1c.3 SPEC-012 §8 "Model lights" — which instances a model light lights (Correction)

- Old: "world flag 0: it lights only this model (added to the instance's own light list)". OpenMerc interpretation:
  the omni lights of an instance light the instances drawn after it in the same method-41 call.
- New: the original collects the lights **over the whole hierarchy first**, before anything is drawn. The answer is
  neither "only this model" nor "drawing order". One instance-method-41 call (drawing an instance and all its
  bolt-ons) works like this:
  1. **Gather pass.** Unless the renderer set "no dynamic lights" (byte 0xac) or the entity is shadow-only, the
     hierarchy is visited: the called instance first, then its bolt-ons depth-first in bolt-on list order. Each
     instance is visited at its pose at the draw time. An instance is visited, and its bolt-ons reached through it,
     only when all of these hold:
     - it is on and has a sequence;
     - its own model **or the model of one of its direct bolt-ons** has light nodes. The instance carries a flag
       for this, set when such a bolt-on is attached. The test is not transitive: a light on a grandchild is missed
       when neither the root's model nor the child's model has light nodes.

     For every light node that is active by SPEC-AMEND-005 I3.9:
     - world flag 1: the record goes to the system's "lights from models" list (unchanged);
     - world flag 0: the light (with its bolt-on's pose and matrix) goes into **one light list shared by the whole
       call**.
  2. Then the world lights of the grid query for the entity origin and the dynamic lights whose box contains the
     origin are appended to the same list (SPEC-012 §8 rules). For the called instance only, the directional
     render-parameter light is appended at the end, when the direction is valid (byte 0x2d).
  3. **Draw pass.** Every instance of the hierarchy is drawn with that list. Per instance, each light passes the
     light's own activity and range tests for that instance (point lights: the light reaches the instance's box)
     before it is used.

  So a world-flag-0 light of any model in the hierarchy can light every model of the hierarchy, including models
  drawn before it, its own model, and the called (root) instance. In the main menu the four omni lights of
  `m_body_main` light the globe, the rings and the doors, whatever the drawing order. Lights do **not** cross into
  a different method-41 call (another entity). OpenMerc **differs** for models drawn before the light's owner. Use
  a gather pass.
- Verify:
  - In the main-menu trace, the vertex colours of the model drawn *first* under `b_web_home_ingame` (the root) also
    carry the teal/warm light colours.
  - A test hierarchy whose only omni light sits on the last bolt-on: the root's vertex colours change when that light
    node's part is hidden (method 8).

#### I1c.4 SPEC-012 §8 — ShadeModel around an instance draw (Refinement)

- Old: not in the spec. OpenMerc: ShadeModel(SMOOTH) at the start and ShadeModel(FLAT) at the end of every instance
  draw.
- New: confirmed. Each instance-method-41 call that draws calls ShadeModel(SMOOTH) once before anything else (one
  call per method-41 call, not per bolt-on). It calls ShadeModel(FLAT) once after the hierarchy and the shadow are
  drawn and the temporary lights are released. A call on an instance that is off or has no sequence returns before
  either call: no ShadeModel is issued, and the per-instance render caches are reset (SPEC-012 §8 "Traversal").
  OpenMerc **matches** if it also skips both calls for an off or sequence-less instance.
- Verify: GL-table trace of a menu with a model that is switched off: no ShadeModel pair for it.

#### I1c.5 SPEC-012 §8 / SPEC-AMEND-005 I3.9 — light node activity (acknowledged, no change)

- The observation (a `WORLD_OMNI` muzzle light lights only in the frames where its visibility track shows it) is the
  rule of SPEC-AMEND-005 I3.9: the part must be shown and the visibility track must be true at the dominant frame.
  The gather pass of I1c.3 uses the same test. OpenMerc now **matches**.
- Verify: as in I3.9.

#### I1c.6 SPEC-010 system method 24 — the specular argument passed by the client (Resolution)

- Old: "24 | int (unused), engine bit reader, bool specular". OpenMerc interpretation: `ghl_specular` > 0.5 and
  `ghl_light_method` > 0.5, as for the renderer's system method 2.
- New: for every svc 26 chunk the client passes specular = (`ghl_specular` **> 0**) and (`ghl_light_method` **> 0**),
  both read at that moment. This is the same test the game module uses for object method 2 (SPEC-010 B). The client
  creates both cvars with the defaults "1" and "2" (archive), the same as the renderer. The renderer's own system
  method 2 call keeps its > 0.5 threshold (SPEC-010). The first argument is always 0.
  - svc 27 (unreliable chunk, method 25) passes the frame number and the game time of the **last parsed svc_frame**
    (the same value that is pulled into the clock of I1c.1).
  OpenMerc **differs** only for values in (0, 0.5], e.g. `ghl_specular 0.3`: the original passes true.
- Verify: `ghl_specular 0.3`, `ghl_light_method 2`, connect: the instances created from the reliable stream are
  prepared with specular on (shiny materials show highlights). With `ghl_light_method 0` they are not.

#### I1c.7 SPEC-202 §2.4 / SPEC-002 — the auto-rotate yaw clock (Correction)

- Old: SPEC-202 §2.4 "0x2000 = auto-rotate (yaw)"; the Quake II formula uses the client time
  (anglemod(client time / 10 in ms)). OpenMerc uses the client time.
- New: the auto-rotate yaw uses the **real-time millisecond clock** (the engine's system millisecond counter, the
  same one as `Sys_Milliseconds` in Quake II), read once per frame when the packet entities are added. It does not
  use the client time or the game clock. With m = that counter in milliseconds:
  - c = trunc(m / 10) (centiseconds);
  - yaw = ((trunc(c × 65536/360) taken modulo 65536)) × 360/65536 degrees.

  So the yaw advances 100° per second of real time, in steps of one centisecond, and is quantised to 1/65536 of a
  turn. The counter starts at an arbitrary point: its zero is the first reading in the process, rounded down to a
  multiple of 65536 ms. So the phase is not tied to the level or to the server. The yaw keeps turning while the game
  is paused, and while `timescale` or demo speed changes the client time. Every auto-rotating entity of a frame gets
  the same yaw. OpenMerc **differs**: use a real-time clock.
- Verify: pause the game next to a pickup: the pickup keeps spinning at 100°/s. In a demo played with `timescale 0.5`
  the pickups still spin at 100° per real second.

#### I1c.8 SPEC-803 §1 — one 3D pass for the main-menu model tree (acknowledged, no change)

- The observation matches SPEC-803 §1: the menu draws its single root model (with everything bolted to it) in one
  Set3DInMenu(0)/Set2D(0) pair after the backdrop pictures. OpenMerc **matches**. With I1c.3, the lights of every
  model in that tree light all of it.

#### I1c.9 SPEC-012 §8 / SPEC-AMEND-005 I3.7 / SPEC-411 §6.7 — the two passes of a specular-map material under hardware lighting (Resolution)

- Old: SPEC-AMEND-005 I3.7: "With a specular map the mesh is drawn twice: the specular map lit by the specular term
  only (ambient and diffuse black), then the base texture added with (ONE, ONE) (pass details UNVERIFIED)." The clean
  team's GL-table trace of the in-view SMG (`ghl_light_method 3`) shows two passes and asks which one is opaque.
- New: the trace is right and agrees with I3.7. The pass details are now confirmed. With `ghl_light_method` > 2.5
  (OpenGL lights on) and a material whose specular is on (I3.7 conditions) **and** which has a slot-1 texture, each
  surface's triangles are drawn twice in a row:
  1. **Specular-map pass, first and opaque.**
     - texture: slot 1 (the specular map);
     - colour (1, 1, 1, alpha = opacity × instance alpha);
     - lighting on;
     - polygon offset fill on, with the **negated** system offsets (system method 39; the default (−0.1, −0.1) gives
       (0.1, 0.1));
     - material AMBIENT_AND_DIFFUSE (0, 0, 0, alpha), no emission;
     - SHININESS = 4 × the specular exponent of I3.7 (11.31 in the trace);
     - SPECULAR = specular colour × specular level, alpha = opacity;
     - blending and depth writes as for the material's normal pass (opaque materials: no blending, depth writes on).
  2. **Base pass, second and additive.**
     - polygon offset off; texture: slot 0 (or texturing off for an untextured material);
     - blending on with (ONE, ONE), depth writes off;
     - lighting still on: AMBIENT_AND_DIFFUSE = (diffuse colour, opacity), EMISSION = the self-illumination term,
       SHININESS 0, SPECULAR (0, 0, 0, 1);
     - afterwards blending off, depth writes on, polygon offset off.
  So the specular map is the opaque pass and the base texture is added over it. The GL texture names in the trace fit
  this: 809 = specular map, 807 = base, the base loaded first.
  - A specular material **without** a slot-1 texture is drawn once: base texture, AMBIENT_AND_DIFFUSE = diffuse
    colour, SPECULAR = specular colour × level, SHININESS = 4 × the exponent.
  - A non-specular material is drawn once with SPECULAR 0 and SHININESS 0.
  - At the end of the mesh: colour (1, 1, 1, 1), blending off, texturing on, blend factors (SRC_ALPHA,
    ONE_MINUS_SRC_ALPHA), face culling on, the used lights off, lighting off.

  The driver configuration setting `ghl_light_method 3` / `ghl_shadows 2` is acknowledged (SPEC-002 driver `.cfg`
  files). OpenMerc's blue cast with method 3 must come from its own OpenGL lighting state, e.g. the 0.4 × colour world
  lights, LOCAL_VIEWER or the specular colour. The pass structure above leaves no other blue source.
- Verify: GL-table trace of the SMG with `ghl_light_method 3`: two draws per surface. The first has polygon offset
  (0.1, 0.1), black ambient/diffuse and the specular-map texture. The second has (ONE, ONE), depth writes off and the
  base texture. A material without a specular map shows one draw.

### B. I7 player module (items 1–17)

#### I7.1 SPEC-601 Interface 2 — default of player variable B (Correction)

- Old: SPEC-601 Interface 2 table: "B | 39/40 | 0 | weapon dropping allowed". The clean team observed −1 (on).
- New: a new inventory starts with **B = −1 (on)**. The full set of constructor defaults of the variables is
  A −1, B −1, I 0x6001, C −1, D −1, E 0, F 0, G 0, H −1, tag `safe`. The game sets B explicitly only in the modes
  listed under "Values set by the game"; single player never sets it. So `weapondrop`, `weaponlose` and the death drop
  are enabled by default in single player. SPEC-600 test vector 7 already assumes this. The observation is right and
  the spec table is wrong. OpenMerc matches (it follows the observation).
- Verify: start a single-player map in a fresh process and `give` the 9 mm, then `weapondrop`: a pistol pickup appears
  and `throwhand` plays. Probe: method 39 on a freshly created inventory returns −1 (all 32 bits set).

#### I7.2 SPEC-601 Interface 3 (block 0x270) and SPEC-600 Behaviour 10 — start value of the idle-variation counter (Correction)

- Old: SPEC-600 Behaviour 10: "an 'idle variation' counter (start 0)". SPEC-601 Interface 3 lists block 0x270 without
  a default. The clean team observed 0 in fresh memory and heap words elsewhere, falling by one per inventory.
- New: the constructor does **not** initialise the counter. It applies one "decrement, not below 0" step to whatever
  the memory held. With v the previous 32-bit content (signed):
  - v ≥ 1 gives v − 1;
  - v ≤ 0 gives 0;
  - the most negative value wraps to 2³¹ − 1 (the step tests the sign of the result).

  The module allocates every inventory (668 bytes, see below) from its own C runtime heap. Three cases follow:
  - Memory used for the first time reads as 0, so the counter is 0. This is the single-player server case in a fresh
    process.
  - A block freed by an earlier inventory and reused gives that inventory's last counter − 1. In practice that is
    0 or 1.
  - Any other reuse gives an arbitrary heap word. This was observed on predicting clients and on dedicated servers.
    Such a counter practically never reaches 0, because a plain idle choice lowers it by one only.

  The counter is part of the 640-byte block. svc 30 and the `INVN` chunk therefore copy the sender's (or the saved)
  value into the receiver. Any rule that writes 2 regardless of the current value ends a garbage counter:
  - the slugger's first idle after its draw (I7.15);
  - a stance transition caused by leaning in the idle choice of pistol1, pistol2 and the assault rifle;
  - a stance transition commanded with `+weaponExtra1` / `+weaponExtra2` on pistol1, pistol2 and the assault rifle.

  All other variations need the counter to be 0 first. The observation is right. The spec's "start 0" holds only for
  fresh memory. The exact value cannot be reproduced without the original heap. OpenMerc's choice is an acceptable
  model of the typical cases: 0 for single-player and listen-server inventories, a large positive value on a predicting
  client and on a dedicated server. The stance disagreement the clean team saw is real in the original. Its second
  cause is the select-now path of I7.5.
- Also a correction: the inventory object is **668 bytes** (0x29C = 28 header bytes + the 640-byte block), not 676.
  This affects SPEC-003 export 9 ("676 bytes") and SPEC-601 Constants ("state block / object size 640 / 676"). No
  interface depends on the size beyond the block.
- Verify: with the I7 tracing proxy, the first inventory of a fresh single-player process has block 0x270 = 0. A
  dedicated server shows a large value that never reaches 0 while only the pistol is used and nobody leans or uses
  `+weaponExtra1/2`. After a lean transition the counter is 2 and idle variations start to appear.

#### I7.3 SPEC-602 Behaviour 2/3 and SPEC-600 Behaviour 11 — where the server clears the pending fire times (Refinement)

- Old: SPEC-602 §2: the pending time is cleared after use and when the attack could not start for lack of ammunition.
  The clean team also observed clears at reload attempts and at the draw after a switch.
- New: the server's player module clears **both** pending fire times (server imports 14 and 16) at two more points.
  1. **At the start of every server reload attempt.** This comes after the reload request has passed its own gate and
     before the full-clip test. Every path into the reload start clears:
     - `reload` while reloading is enabled (C on). With C off the request is dropped before the reload start, and
       nothing is cleared.
     - A dry `reload` with a full clip. The clear comes first, then the attempt ends.
     - Every automatic reload: a shot that cannot be paid with D on (attack start, refire, an automatic weapon's fire
       note); the end of a fire sequence with an empty clip.
  2. **At the start of every draw.** This covers selection with no current weapon, a switch to a weapon without a
     put-away sequence, the end of a normal or predicted switch, the return from holstering, and the weapon drawn
     after a hand pseudo-weapon. The hand pseudo-weapon's own `hurthand` / `throwhand` start is not a draw and does
     not clear. The select-now methods 6, 7, 8 and export 11 do not draw and do not clear.

  Both clears are unconditional; they do not test "owner predicts". Without prediction the stored times are normally
  0, so the effect is visible only for predicting owners. The rules already specified stay: a time is cleared after
  use, and a failed attack start clears only the time of its own mode.

  On the client, the start of every draw clears both "fired this frame" exports (SPEC-003 entries 5/6) and the
  synchronised flag (block 0x008). The observation is right. OpenMerc matches at the points it lists. Check that it
  does **not** clear on `reload` with C off or on the select-now methods.
- Verify: dedicated server with `-ServerTraceVerbose`, predicting client. `reload` with a full pistol clip logs imports
  14 and 16. With `dmflags 64` (no reload), `reload` logs neither. Firing a weapon dry until the automatic reload
  starts logs both.

#### I7.4 SPEC-602 Behaviour 4 — order of a predicted switch, and the client follower (Refinement)

- Old: SPEC-602 Behaviour 4 lists the predicted-switch steps without the idle choice. It describes the follower as
  "the idle sequence is restarted".
- New, server with a predicting owner, for a switch that is not a drop or lose and whose current weapon has a put-away
  sequence. The steps run in this order within one think:
  1. The current weapon's deselect rule runs, then its put-away name is chosen. The put-away choice has its own side
     effects: knife blood cleared, flamethrower looping sound reset.
  2. The target's weapon id, animated id and slot are remembered (blocks 0x01C, 0x020, 0x024).
  3. The "next" instance is made from the target weapon's cached model object. If none is cached, an object is created
     and registered as in I7.16, without precaching. Parts and the turn transform are set as for any view instance,
     and the instance goes into the player state.
  4. **One idle choice for the target weapon**: its idle rule runs with the time-seeded random. It uses the variation
     counter and may change the stance words. The chosen idle starts on the next instance at "now". This also writes
     the sequence-name word (block 0x214).
  5. The player state gets the target's weapon id, clip rounds and reserve.
  6. ready := 0, state := 4, and the put-away starts on the **old** instance. This overwrites the sequence-name word
     with the put-away's name, which is what the block shows.

  At the end of the put-away (next think after switch-now):
  - The old instance is destroyed and the next instance becomes current.
  - The current weapon id, animated id and slot come from the remembered values, and the last-selected slot is set.
  - Only the remembered **weapon id** (block 0x01C) is cleared. Blocks 0x020 and 0x024 keep their values.
  - The draw starts with no second idle choice. It applies the draw stance (I7.5) and clears the pending fire times
    (I7.3).

  The visible-rounds rule (assault rifle and machinegun magazine parts) is **not** applied anywhere on this path. On
  the non-predicted path it runs at selection.

  Turning the holster on runs the same preparation (steps 2–5) for a predicting owner. It uses the target slot then
  pending, normally the current weapon's own slot. The weapon therefore comes back after unholstering on a fresh
  instance of the same model, again with one idle choice at the start and none at the end.
- Side finding (UNVERIFIED in game): in step 3 the `arms` → `arms_sleeve` skin is looked up in the **current**
  weapon's model and applied to the **current (old)** instance, not to the next instance. The next instance keeps the
  default skin of material `arms`. The part switches (`_R_FOREARM` …) do go to the next instance. OpenMerc applies
  the skin to the new instance, which differs from the original. The difference is visible only if the default
  `arms` skin of a model is not `arms_sleeve`.
- Client follower:
  - **Dictation**, once per change: the slot request, clip and reserve are set from the player state, and the dictated
    and synchronised flags are set. Then comes a full **idle choice**, which uses the counter and may change stances.
    It is not a replay of the same idle. Processing the dictated request then plays the predictor's own put-away.
  - **Adoption**, when switch-now is set and the server instance is not adopted:
    1. The target is selected.
    2. The predictor's previous instance is destroyed and the server instance is cloned with its children.
    3. An **idle choice** is made, unless the weapon is holstered.
    4. The visible-rounds rule runs with the player-state clip.
    5. The draw starts with its stance; switch-now and any drop mark are cleared.

  The observation is right on every point. OpenMerc matches apart from the skin finding and the visible-rounds
  remark.
- Verify: verbose server trace of a predicting client switching pistol1 → assault rifle. The idle-choice random draw
  and the next instance's idle appear in the same think as the `done_*` start. At the end of `done_*`, `ready` starts
  with no idle sequence between. Block 0x020/0x024 keep the target's values afterwards. For the skin: compare the
  material skins of the view instance in the player state after a predicted switch and after a local one.

#### I7.5 SPEC-600 Behaviour 8 and 10 — every draw applies the draw stance; select-now does not draw (Refinement)

- Old: SPEC-600 gives the draw stances per weapon ("draw `ready_a` (sets stance a, clears c)") but says only for
  ordinary selection that the draw runs.
- New: every draw start runs the weapon's draw rule. These are the draw paths of I7.3: selection with no current
  weapon, a switch without a put-away, the end of a normal or predicted switch, the return from holstering, the weapon
  after a hand pseudo-weapon, and the client follower's adoption. The draw rules write:

  | Weapon | Block words written by every draw |
  |---|---|
  | pistol1 | 0x200 := 1 (stance a), 0x204 := 0 (pose c cleared) |
  | pistol2 | 0x210 := 0 (stance a), 0x204 := 0 (sub-pose d cleared) |
  | sniper | 0x260 := 1 ("drawn"), 0x208 := 1, 0x274 := 0 (saved zoom cleared) |
  | all others | none |

  The sniper's "drawn" meaning lives in 0x260. The draw sets 0x208 as well, which is why the clean team saw it change.
  Two consequences follow.
  - Inventory methods 6, 7, 8 ("select now") and export 11 select **without a draw**. They make an idle choice and
    leave the stance words as they are. The game's `elbow` gives weapons and then selects the 9 mm with method 6, so
    the server's pistol1 stays in the constructor's stance b (block 0x200 = 2) and fires `fire1_b`. The predictor
    follows with a real draw and is in stance a. This explains the I7.2 example.
  - The assault rifle's draw sets no stance, and its stance b shares block 0x260 with the sniper's "drawn". The assault
    rifle therefore comes up in stance b after the sniper rifle has been drawn or reloaded. UNVERIFIED in game; this is
    derived.

  The observation is right. OpenMerc matches if it applies the table on all listed paths and not on select-now.
- Verify: `give sniper`, draw it, then `weaponselect` the assault rifle. Its first idle and fire sequences are the `_b`
  variants. On a dedicated server, `elbow` then holding fire shows `fire1_b` in the server trace and `fire1_a` on the
  predicting client.

#### I7.6 SPEC-600 test vector 4 and Behaviour 10 shotgun — N shells take N + 1 loops (Correction)

- Old: test vector 4: "2 of 8 shells loaded and 10 in reserve: `reload_1`, 6 × `reload_loop` …, `reload_2`; total
  0.2 + 6 × 0.2 + 0.45 s". Behaviour 10: "`reload_loop` once per shell loaded".
- New: the shotgun's reload proceeds as follows.
  - The reload start loads nothing. It only checks that one shell could be loaded: the clip is not full and the
    reserve is not empty. If not, the attempt behaves as "nothing to load" (dry-fire or idle).
  - `reload_1` ends by starting `reload_loop`.
  - At every end of `reload_loop`, one shell is moved if the clip is not full and the reserve is not empty. Then
    `reload_loop` starts again and ready := 1, so a shot can interrupt the reload while the state stays 1. Otherwise
    `reload_2` starts.

  N shells therefore take **N + 1** `reload_loop` sequences: the last one finds the clip full or the reserve empty.
  Each loop plays its `/weapons/shotgun/putin` note, so N + 1 putin sounds are heard. With reloading disabled (C off)
  or a forced instant reload (method 5), the normal instant rule applies instead. The observation is right and the
  test vector is wrong. New test vector 4: 2 of 8 loaded, 10 in reserve → `reload_1`, **7** × `reload_loop` (7 putin
  sounds, 6 shells moved), `reload_2`; play-once total 0.2 + 7 × 0.2 + 0.45 = 2.05 s before server-frame rounding.
  OpenMerc matches.
- Verify: the test above, counting `putin` sounds and the clip counter in the player state (it rises at the end of
  loops 1–6, not at loop 7).

#### I7.7 SPEC-600 Behaviour 10, machinegun and flamethrower — release handling (Correction for the machinegun, Refinement for the flamethrower)

- Old: machinegun: "When `fire1_1` ends with the button released → `fire1_2` (spin-down), then idle." Flamethrower:
  "Releasing the button goes idle, plays `fire1_2` (looping mode) and resets the looping sound."
- New:
  - **Machinegun.** Its per-think rule is the machine pistol's release rule, which the assault rifle's per-think
    rule also contains: in state 2 with the attack button released, the weapon goes idle at once. This is a normal idle choice with ready := 1 and
    state 0, and the spread counter is kept. The per-think rule runs before the sequence events of the same think,
    so a `fire1_1` end can never see the button released. Its end rule therefore always refires, and `fire1_2` is
    unreachable in 1.07f. The end rule still marks the next sequence's first note as pre-paid. The spec text is wrong
    in effect; the observation is right.
  - **Flamethrower**, per think in state 2 with the attack button released, in this order:
    1. An idle choice. On the server this first resets the looping sound; it also uses the variation counter and the
       time-seeded random, and starts the chosen idle with ready := 1 and state 0.
    2. `fire1_2` starts in looping mode, replacing that idle as the playing sequence and in the sequence-name word.
    3. Server only: a second looping-sound reset.

    Every looping-sound reset writes **both** fields: sound `player/flaglp.wav` with data 248 when the owner carries
    the CTF flag (server import 12), otherwise sound 0 **and** data 0. At the first end of `fire1_2` (0.2 s), the
    generic end handling in state 0 makes a second idle choice and replaces `fire1_2`.

  OpenMerc matches.
- Verify: machinegun, tap fire for 0.1 s: the server trace shows `fire1_1` then an idle sequence, never `fire1_2`.
  Flamethrower, hold 1 s and release: `idle`/`idlefinger_*`, then `fire1_2`, then 0.2 s later another idle choice. The
  entity's sound and sound-data fields are both 0 after the release.

#### I7.8 SPEC-600 Behaviour 2 and 7 — notes of a sequence started from an event (Refinement)

- Old: Behaviour 2: "A note lying exactly at a sequence's start is delivered in the same think (UNVERIFIED …)". The
  clean team observed that a refired sequence's 0 ms note arrives one think later.
- New: both statements hold, for different starts. The deciding rule is SPEC-012 §5:
  - the Ghoul event pass first collects all due events and calls the callbacks after the traversal;
  - a Play sets the cursor to the new start (phase 0).

  This gives two cases.
  - A sequence started **before** the think's event delivery has its 0 ms note delivered in the same think, because
    the delivery limit is now + 0.001. This covers an attack started from the buttons in step 6, an idle choice in
    the per-think rules, a draw at switch completion, and a reload started by a request.
  - A sequence started **from inside an event callback** has no note delivered by the pass that started it. All its
    notes from 0 ms on start with the next think's delivery. This covers a refire at the end of a sequence, a chained
    end rule (knife `fire1_1a` → `fire1_1b`, shotgun loops, sniper `scopein` → idle, MPG `fire1` → `charge`), and a
    reload or idle started by a fire note.

  Pistol1 held at 10 Hz: with `fire1_b` (0 ms note) the shots are at 0, 0.4, 0.7, 1.0 s …; with `fire1_a` (50 ms) at
  0.1, 0.4, 0.7 s …. The same rule shifts the derived rates of Constants 2 for every weapon whose fire note is at
  0 ms. Example: the machinegun's start-of-sequence note of a refired `fire1_1` arrives 0.1 s late and is then not
  suppressed by the 0.195 s gate, giving shots at 0, 0.2, 0.4, 0.7, 1.0 s …. The observation is right. OpenMerc
  matches as long as its Ghoul layer queues callbacks as SPEC-012 says.
- Verify: count the server's fire imports while holding pistol1 (stance b) for 2 s on a 10 Hz listen server:
  timestamps 0, 0.4, 0.7, 1.0, 1.3, 1.6, 1.9.

#### I7.9 SPEC-600 Behaviour 6 and 7 — a refire that cannot be paid (Refinement)

- Old: Behaviour 7: "refire: spread counter + 1, take one shot's ammunition, start the primary sequence again … If
  the ammunition could not be taken, go idle."
- New: "take one shot's ammunition" is the full payment rule of Behaviour 6, with its side effects, and the idle
  comes **after** them:
  - The spread counter is incremented.
  - Payment fails with the clip empty or below the alternate requirement (for example an MPG burst with 5 of 10
    units). With D on, the server's automatic reload starts: rounds are moved into the clip, server import 4 is
    called, state 1 and the reload sequence start, the weapon-state byte of the player state is incremented, and both
    pending fire times are cleared (I7.3). The spread counter becomes 0 there. With D off, the dry-fire sequence
    starts instead. The flamethrower runs its out-of-fuel rule.
  - Then the weapon goes idle: idle choice, ready := 1, state 0. This replaces the reload or dry-fire animation and the
    sequence-name word.

  On the server the visible result is an **instant reload**: the clip is full, the game's third-person reload
  animation is triggered, and no reload sequence plays. A predicting client still sees the weapon-state byte change
  and starts its predicted reload (SPEC-602 Behaviour 3). The client and the server then disagree visibly until the
  next sequence (UNVERIFIED in game). The observation is right. OpenMerc matches.
- Verify: MPG with 15 units, hold the alternate button. The first burst costs 10. At the end of `charge` the refire
  finds 5: the server trace shows import 4, the clip refilled from the reserve, and an idle sequence instead of
  `reload`.

#### I7.10 SPEC-600 Behaviour 10 — shot gates record the time before paying (Refinement)

- Old: assault rifle and machine pistol: "one shot per distinct 'now'"; every shot after the first "takes one round".
  Rocket alternate: "one per distinct now".
- New: the time of the last automatic-weapon shot (block 0x27C) is written as soon as a fire note passes the time
  test, **before** the payment.
  - **Assault rifle, machine pistol** (every `fire` note): the note is refused if "now" equals block 0x27C.
    Otherwise 0x27C := now. If the first-shot pre-paid flag (block 0x264) is set, it is cleared and the shot happens.
    Otherwise the spread counter + 1, one round is paid, and the shot happens only if the payment succeeded.
  - **Rocket launcher** (every `fire` and `altfire` note): the same time test and write. Payment (one rocket) is taken
    only in state 3 (alternate) when the pre-paid flag is clear. A single primary rocket's note therefore records the
    time without paying, since its round was paid at the attack start.
  - **Machinegun** (`fire` and `altfire` notes): the note is refused while now − 0.195 < block 0x27C, compared in
    single precision. Otherwise 0x27C := now. Payment with spread + 1 happens only in state 2 when the pre-paid flag is
    clear. The phosphorus grenade's note records the time and never pays.

  A failed payment means the clip is empty or below the requirement. With D on it starts the automatic reload **at
  once**, from inside the note; with D off it starts the dry-fire. That note fires no shot, and a second note at the
  same "now" is refused because the time was already recorded. The observation is right. OpenMerc matches.
- Verify: assault rifle with 3 rounds, hold fire. The third shot empties the clip and the next note starts `reload_a`
  in the same think. Rocket launcher primary, then the alternate within the same server frame: the alternate's first
  note is refused.

#### I7.11 SPEC-601 Behaviour 4 and Constants — goggles battery steps (Correction)

- Old: "While on, every time now passes the next time the battery drops by 1 and the next time becomes now + 0.3 …
  off … now + 0.5". Constants: "−1 per 0.3 s on, +1 per 0.5 s off". Test vector 4: battery 10 switches off "after
  10 × 0.3 = 3.0 s".
- New: a step happens only when the next time is **strictly less** than "now". Both values are single-precision. The
  new next time is computed as now + 0.3 (or + 0.5) in double precision and then stored in single precision. At most
  one step happens per think. The server's level time is the frame number × 0.1 rounded to single precision.
  - Off: a step takes **6 frames (0.6 s)**.
  - On: a step takes **4 frames (0.4 s)** for most start times. For about one start time in nine, now + 0.3 rounds
    below the later frame's time and the step takes 3 frames.

  Use, on or off, sets the next time from the current time with the same rule. When the battery steps from 1 to 0 the
  goggles switch off, with battery 0 and the game notified. The observation is right; the spec's rates hold only for
  a non-strict comparison. New test vector 4: battery 10, switched on → off after 10 steps ≈ 3.6–4.0 s; off for 5 s
  → +8 units. OpenMerc matches the strict comparison. It must also compute the next time in double precision before
  rounding (`now + 0.3f` in single precision can round differently), and keep "one step per think".
- Verify: goggles with battery 10 on a 10 Hz server. Log the battery each frame: decrements 4 frames apart (rarely
  3), increments 6 frames apart.

#### I7.12 SPEC-600 Behaviour 10, sniper — idle variation gate and the saved zoom at `scopein` (Correction)

- Old: "Idle: r < 20 → `idlefinger` (only after a draw or reload, which is always the case in practice), else
  `idle_a`". `scopein` end: "the field of view becomes 60 … idle follows" (saved zoom not mentioned). Clean team:
  the gate is word 0x208.
- New:
  - The sniper's idle rule reads block **0x260** ("drawn", SPEC-601 Interface 3), not 0x208. The draw (I7.5) and
    the reload start set both words to 1, which is why they look alike in traces.
  - With 0x260 = 0, `idle_a` is chosen and the variation counter is **not touched**. With 0x260 ≠ 0, the common
    rule applies: counter 0 and r < 20 → `idlefinger`, counter := 2; otherwise counter − 1 (not below 0) and `idle_a`.
  - The two words differ when the sniper's idle choice happens before its draw. This happens in the predicted-switch
    idle choice (I7.4 step 4), in the follower's dictation and adoption, and with the select-now methods. The value
    then comes from the previous weapon: the assault rifle's stance b uses 0x260, while pistol1 transitions write
    0x208.
  - At the end of `scopein` the field of view becomes 60, "zoom while held" and "scope active" are set, and the
    **saved zoom (block 0x274) := trunc(field of view) = 60**. The model is hidden and an idle choice follows.
    Consequences: a reload while scoped without any zooming restores field of view 60 at the end of `reload`. Only
    `scopeout`, the deselect rule and the draw clear the saved zoom.

  The observation is right about the behaviour, but the word it names is wrong. OpenMerc **differs**: it tests 0x208,
  and must test 0x260.
- Verify: on a predicting client, switch assault rifle (stance a, so 0x260 = 0) → sniper with a predicted switch. The
  server's idle choice for the sniper leaves the variation counter unchanged. After a pistol1 idle transition
  (0x208 = 1, 0x260 = 0) the same switch also leaves it unchanged. `scopein` then `reload`: field of view 60 after the
  reload.

#### I7.13 SPEC-003 interface details (Resolution)

- Old: SPEC-003 method table uses "→ bool"; fire imports `(?, edict_t *owner, inventory *inv)`; method 64 "load
  inventory from persistence cvars"; method 8 "switch to the best weapon immediately"; SPEC-601 table 4 item messages.
- New:
  1. **Results.** Every method result is a 32-bit value in EAX. The boolean methods return 0 or 1 in all 32 bits,
     except method 22, which returns the owned-weapon flag bit itself (1 << id). The game keeps and tests the full
     32 bits; for example, the `elbow` cheat stores method 21's result as a 32-bit word and tests it whole. A
     reimplementation must return full 32-bit values. The module's Ghoul note callbacks are not inventory methods;
     they return their (ignored) result in the low byte only.
  2. **Method 64** ORs the bit of every persistence weapon with a non-zero value into `*weaponMaskOut` (when the
     pointer is not null). It never clears bits, so the caller's pre-filled mask is kept. It returns the knives
     count.
  3. **Fire imports.** Server imports 2/3 and client imports 2/3 are always called with three arguments: (owner
     context pointer, owner entity, inventory). The first argument is the owner context passed to method 61.
  4. **Method 8** finds the best slot and then calls **method 7 through the method table** with it. A replaced
     method 7 is used. When nothing is usable the slot is −1 and the original reads unrelated words (undefined); a
     reimplementation should do nothing in that case (UNVERIFIED in game, needs a player with no usable weapon).
  5. **Item messages** (server, SPEC-003 server import 19, id = package 3 × 256 + entry). Whenever an item is
     consumed and some remain, by `itemuse` or by an `itemdrop` of one item:
     `0x30E` (ITEMS 0x0E AMMO_COUNT) with (new count, display name). When the last one is consumed: `0x30D` (ITEMS
     0x0D AMMO_GONE) with (display name). Goggles are never consumed. Whole-slot drops (goggles, CTF flag) print
     nothing (I7.14). This is what SPEC-601 table 4 says; the observation shows the non-last case.

  The observations are right. OpenMerc matches (check point 5 for the last item).
- Verify: a stub player module whose method 21 returns 1 in AL with garbage in the upper bytes changes the `elbow`
  result. With grenades 2 → 1 → 0 the messages are "Ammo is now 1 for your Grenade..." and then "No more ammo for
  your Grenade".

#### I7.14 SPEC-600 Behaviour 8 and SPEC-601 §6 — end of a hand pseudo-weapon; whole-slot drops (Refinement)

- Old: Behaviour 8: "At its end the hand requests, as forced, `weapnext` if the weapon tag is empty, else the best
  weapon; commands are swallowed until then; the new weapon is drawn immediately". Behaviour 3 step 5: "only when
  ready: a pending request is processed".
- New, at the end of `hurthand` / `throwhand` (the name in the sequence-name word must be exactly that; otherwise the
  end is an ordinary idle choice):
  1. Server only, the follow-up request:
     - Tag empty: the command `weapnext` is executed through method 4 with force 1 (request code 2), while ready is
       still 0.
     - Otherwise: method 11, which resolves the best weapon now and stores the request **code 8 + slot**. With no
       usable weapon the slot is −1 and the code becomes 7, the drop code. With no current weapon, that request
       clears the current weapon and leaves no view model.
  2. Both sides: the hand's idle choice. The hand has none, so nothing is played.
  3. **ready := −1**; the state stays 5.

  Every "ready" test is non-zero, so −1 counts as ready. On the next think the pending request is processed, and
  because the current weapon id is 0 the target is selected and **drawn at once** (no put-away). Weapon and item
  commands that arrive between the hand's end and that next think are swallowed because ready is negative, so they
  cannot replace the follow-up request. `itemdrop` of a whole-slot item (goggles, CTF flag):
  - the item's switch-off rule runs with the selected slot;
  - the game spawns the pickup with the whole count;
  - the slot is emptied and the selection advances;
  - **no message** is printed.

  The observation is right. OpenMerc matches; check the empty-tag `weapnext` branch and the no-usable-weapon case.
- Verify: `weapondrop` with the 9 mm in single player: `throwhand` 0.55 s, then on the next think `ready` of the best
  weapon with no `done`. `itemdrop` of goggles prints nothing.

#### I7.15 SPEC-600 Behaviour 10, slugger — `ready_idle` is a variation (Refinement)

- Old: "Draw `ready`, then the first idle is always `ready_idle`; idle: r ≤ 9 → `idlefinger_a`; r ≤ 24 →
  `idlefinger_b`; else `idle` (r from random(0, 130))".
- New: whenever the slugger's idle rule runs while the sequence-name word is exactly `ready`, it chooses `ready_idle`
  and sets the variation counter to **2**, regardless of the counter's value. The random number is still drawn. In
  practice this happens at the end of every draw, because the draw is the last sequence started. Otherwise:
  - counter 0 and r < 10 → `idlefinger_a`; counter 0 and r < 25 → `idlefinger_b`, each with counter := 2;
  - else counter − 1 (not below 0) and `idle`.

  The observation is right. It is one of the unconditional writers listed in I7.2 that end a heap-word counter on a
  predicting client. The others are the lean and `+weaponExtra` transitions of pistol1, pistol2 and the assault
  rifle. OpenMerc matches.
- Verify: on a predicting client in a fresh process, use the pistol for a minute (no idle variations), then draw the
  slugger and switch back: pistol idle variations now appear.

#### I7.16 SPEC-003 export 11 — rebuild after a full restore (Resolution)

- Old: "after a full state restore … rebuild the owner's view-weapon model and replicated weapon fields (server side
  only)". The clean team observed that a new model object is registered.
- New, on the server only (in client context export 11 does nothing):
  1. The cached model object of the **current weapon only** is forgotten. The cache entry is set to empty and the old
     object is not destroyed; it stays in the Ghoul system with any instances made from it until the system releases
     its objects.
  2. The ordinary selection runs:
     - the player state gets the weapon id, clip rounds and reserve;
     - a view instance is made, and since the cache is empty a **new model object** is created, its sequence list
       registered with common import 17, and registration finished with all skins;
     - the new object goes into the cache, so later precache calls and selections use it;
     - the previous view instance is destroyed and the new instance set up;
     - an idle choice is made unless the weapon is holstered;
     - the visible-rounds rule runs.

     The new object gets **no** specular setting, no note enumeration (no sound or effect precache) and no attack
     precache; only export 7 does those. On the server object the missing specular setting is not expected to be
     visible, because clients prepare their own copies (UNVERIFIED).
  3. The model is hidden when the sniper scope is active (block 0x26C) and shown otherwise.

  No draw happens, so the draw stance is not applied. The Ghoul system tells every client about the new object; this
  is the extra object and the one-frame delay the clean team saw. Answer to "does the cache keep the old object?":
  **no**. The cache then holds the new object, and the old one is abandoned, not freed. Whether this was intended
  cannot be read from the binary: it is consistent with a cache that may hold an object of a Ghoul state that a load
  replaced. OpenMerc matches (06:44 build).
- Verify: load a savegame twice in one level and count object-creation records for the current weapon's in-view model
  in the server's Ghoul stream: one new object per load, while other weapons keep theirs.

#### I7.17 SPEC-600 Behaviour 10, machine pistol — the first shot and the pre-paid flag (Resolution)

- Old: "The first shot after pressing the button also takes a round although starting the attack already took one
  (2 rounds for the first shot; UNVERIFIED in game, derived)." The clean team observed that the first burst after
  `bigelbow` costs 1 round for its first shot and later presses cost 2.
- New: the flag in block 0x264 ("first shot of the fire sequence pre-paid") decides the cost; its value is not tied
  to the press.
  - Starting a machine-pistol attack takes one round and does **not** write the flag. This differs from the assault
    rifle, the machinegun and the rocket alternate, whose attack start sets it to 1.
  - The first `fire` note of `fire2_1` arrives in the same think (0 ms, I7.8). It goes through the automatic-weapon
    gate of I7.10. With the flag set, the flag is cleared and the shot is free; with the flag 0, one more round is
    paid.
  - The flag is 1 in a new inventory (constructor). It is set by the machine pistol's own refire ("after refire"
    rule), and by the start or refire of an assault-rifle, machinegun or rocket-alternate attack: the word is shared
    by all weapons. The first automatic-weapon note that finds it clears it.

  The first press after a new inventory (`bigelbow`, level start, respawn) therefore costs 1 round for its first
  shot. Every later press costs 2, unless the flag was left at 1. That can happen in two ways:
  - a refire whose first note was never delivered, because the button was released in the think that would have
    delivered it (I7.8);
  - an attack of one of the weapons above that set the flag and then could not pay.

  The observation is right. The spec's "2 rounds" holds only while the flag is 0. OpenMerc matches if it keeps the
  flag in the shared block word with these writers.
- Verify: in a fresh single-player game, `give mpistol`, tap fire once: the clip loses 1 for the first shot (2 for a
  two-shot tap). Tap again: the first shot costs 2. Fire the assault rifle with an empty clip, then tap the machine
  pistol: its first shot costs 1 (UNVERIFIED in game; derived).

### C. G18 game module (items 29–35)

#### G18.29 SPEC-780 §1 (ClientUserinfoChanged), §5 — `skin`/`teamname` replacement: userinfo plain, configstring starred (Correction)

- Old: §1: "When the requested model/skin/team cannot be used, the userinfo keys `skin` and `teamname` are rewritten
  to `*<actual value>` in deathmatch, or to the actual values outside it"; "before the client has entered a
  deathmatch game, stops here". Observation (`openmerc-data/g18/ui_orig*`): the userinfo gets the plain actual values;
  the player configstring gets `*` before each replaced field; usable fields are kept as written (compared
  case-insensitively); bots keep the `*`; the early stop is ClientConnect's own call.
- New: the **observation is right; the spec text is wrong** about where the `*` goes. Exact rules of the model part of
  ClientUserinfoChanged (after the name, `spectator`, `bestweap` steps and the team-mode step of §1):
  1. **Early stop.** In deathmatch, when the client is not marked connected, the function stops after `bestweap`
     (only the userinfo is stored, step 7). ClientConnect clears that mark, calls ClientUserinfoChanged (which
     therefore stops here in deathmatch), and sets the mark after the call; the engine's own ClientUserinfoChanged
     call that follows the connect (SPEC-001) runs the full path below and writes the full entry.
  2. Before the model set-up the requested values are copied: R_skin = userinfo `skin`, R_team = userinfo `teamname`
     (as typed, possibly empty).
  3. The player model is set up (§16). The set-up reports "unchanged" when, in deathmatch, the player already had a
     body model before this call **and** userinfo `skin` and `teamname` equal (ignoring case) the values used at the
     previous set-up **and** the player name equals (exactly) the name of the previous set-up. In that case the body
     is still rebuilt, but nothing of steps 4–6 happens: neither the userinfo nor the configstring is rewritten
     (only the team-play suicide check of §1 runs, comparing the model's team with R_team). Every set-up remembers
     the name, `skin` and `teamname` it was called with (the values *before* any rewrite of step 4).
  4. Otherwise let A_skin = the model in use (file name without extension) and A_team = its team (§5).
     - **Outside deathmatch**: userinfo `skin` := A_skin and `teamname` := A_team (plain), and R_skin := A_skin,
       R_team := A_team.
     - **Deathmatch**: if A_skin differs from R_skin (ignoring case), R_skin := `*` + A_skin and userinfo `skin` :=
       A_skin (plain); otherwise both stay as typed (so `Slick` stays `Slick`). The same for the team: if A_team
       differs from R_team (ignoring case; an empty R_team always differs), R_team := `*` + A_team and userinfo
       `teamname` := A_team (plain); otherwise unchanged (`SLICK` with model `slick` stays `SLICK`).
  5. The team-play suicide check of §1 (team play, not CTF/Control/forced Conquer, not a spectator, the call is a
     later change rather than the connect call) compares the model team before the set-up with A_team.
  6. The full player information configstring is written as `name\R_team\R_skin\N` (N = team number − 1). So only
     the configstring carries the `*` marks.
  7. The (possibly rewritten) userinfo is stored and `bestweap` is passed to the player module.
  Bots run the same function on a copy of their `.bpf` userinfo, so their entries carry the same `*` marks
  (`Kirt\*slick\slick\-1`). Consequence of step 3: a client whose next userinfo message repeats the unusable values
  (the client's own cvars are not changed by the server) gets an "unchanged" set-up — its userinfo then keeps the
  unusable value it sent and the earlier starred configstring entry stays. Replace the §1 sentence by steps 3–6 and
  add to §5: "`team` and `model` are the values the player asked for; a value that had to be replaced is written as
  `*` + the value in use (deathmatch only)". OpenMerc (following the observation) **matches** steps 1, 2, 4, 6;
  check step 3 (no rewrite when unchanged).
- Verify: done (`dumpuser` and configstring timelines). Extra check for step 3: connect with `skin nosuchmodel`,
  then change only `bestweap`: `dumpuser` shows `skin nosuchmodel` again and the configstring keeps
  `\*dekker\`.

#### G18.30 SPEC-001 "How the engine drives the exports" / SPEC-760 B9.3 — the game's frame time (Correction)

- Old: SPEC-001: "the returned float is the game time (the original returns counter × 0.1)"; SPEC-760 B9.3: "time =
  framenum·0.1". Observation (`rf_orig/rf.log`): call n of RunFrame returns n × 0.1 whatever the argument; the engine
  calls RunFrame(0), RunFrame(1) while loading and then RunFrame(1), RunFrame(2) …, so the game time during play is
  (argument + 2) × 0.1 (also `net-verification.md` item 2: svc_frame game time = (frame + 2) × 0.1).
- New: the **observation is right**; SPEC-001's wording is wrong, SPEC-760's is right if "framenum" is read as the
  game's own counter:
  1. The game keeps its own frame counter. It is cleared (with the rest of the level state) at the start of every
     `SpawnEntities`.
  2. Every `RunFrame` call increments it by one and sets level time = counter × 0.1 **before** anything else of the
     frame runs (bots excepted, which run just before); the engine's frame-number argument is not used for the time.
  3. While `freezeworld` ≠ 0 the frame does nothing: the counter is not incremented and the unchanged time is returned.
  4. RunFrame returns the level time.
  Because the engine calls `RunFrame` twice during the level load (SPEC-001 "Level start") and then once per server
  frame, the game counter is the engine's frame number + 2, hence svc_frame game time = (frame + 2) × 0.1. Every
  think scheduled at spawn time (spawn functions see time 0) therefore runs in the first settle frame or the first
  frames after it, as in the original. Replace SPEC-001's "(the original returns counter × 0.1)" by "(the original
  returns its own frame count × 0.1; that count includes the two calls at level start)". OpenMerc **differs**.
- Verify: done (net-verification item 2: 1 495 of 1 495 frames). The first svc_frame after a map load carries game
  time 0.3 (frame 1); with `freezeworld 1` set for 1 s the game time stops while the frame number goes on, and the
  offset grows by 10 frames.

#### G18.31 SPEC-782 §7 — the weapon-impact temp entity of projectiles (Refinement)

- Old: SPEC-782 §7 (rocket) names no impact temp entity; SPEC-771 C.2 lists "rockets, split rockets, slug" among the
  senders of TE 6/7 without the values. Observation: a player rocket exploding on a surface sends TE 6 at the
  explosion point: impact kind 0 with the plane normal, direction = −normal, power 20, style 0, debris 0.
- New: confirmed, and exactly four projectile touches send it, each once per explosion, before the explosion damage:
  | Projectile | Power | Mark style | Debris |
  |---|---|---|---|
  | rocket, touching a surface | 20 | 0 | 0 |
  | rocket, touching a character | 20 | 0 | 0 |
  | split rocket (each piece) | 10 | 0 | 0 |
  | slug | 10 | 0 | 0 |
  The trace given is the touch's own (impact descriptor of the touched surface, SPEC-771 C.2), the direction is the
  reversed plane normal (× 1000 on the wire), and all the TE 6/7 rules of SPEC-771 C.2 apply (TE 7 when the surface
  changes damage stage, nothing on sky, nothing on characters/players under `lock_blood`, the 0.2 s limit for the
  listed materials). Grenades (all kinds), C4, mines and the other projectiles send no weapon impact of their own (thrown
  knives and throwing stars send theirs on their own touch, SPEC-782). Add the table to SPEC-782 §7.
  OpenMerc **differs** if it sends none.
- Verify: done for the rocket (self-rocket captures). Slug: one TE 6 with power byte 10 at the impact; grenade: none.

#### G18.32 SPEC-761 §8 — alert decay: the lower limit exists only while spawning is enabled (Correction)

- Old: "A −= (A·0.015 + 0.003)·0.7 (min 0) … when spawning is disabled A decays as above without sounds."
  Observation: on tsr1 the meter (trunc(A × 100)) goes negative towards −20; on arm1 it stays 0.
- New: the observation is right and the two cases differ only in the limit. Each frame, while a player exists:
  1. **Spawning enabled** (single player, not during a cinematic, `ai_spawnfrequency` > 0.01 and the skill alert gain
     > 0.01 — both strictly greater): the phases of §8 apply; in the decay phase A −= (A·0.015 + 0.003)·0.7 and then
     A := 0 when negative. A level that starts at 0 therefore stays at 0 (arm1).
  2. **Spawning disabled** (any of those conditions false, also deathmatch): only A −= (A·0.015 + 0.003)·0.7, with
     **no** lower limit and no phases or sounds. From 0 this tends to −0.2 (A' = 0.9895·A − 0.0021), so the meter
     shows −1 … −19 (tsr1).
  The decay never stops; what changes is the clamp. Replace "(min 0)" by "(then raised to 0 if negative)" and the last
  line by "when spawning is disabled A decays by the same formula every frame without the lower limit (towards −0.2)
  and without sounds; the thresholds 0.01 are exclusive". OpenMerc (negative values kept) **matches** for tsr1;
  it must clamp at 0 while spawning is enabled.
- Verify: done (tsr1, arm1). Which of the conditions disables spawning on tsr1 is not decided here (UNVERIFIED:
  print `ai_spawnfrequency`, the skill's alert gain and the cinematic state on both maps).

#### G18.33 SPEC-780 §21 / SPEC-760 B1.4 — what `noweapons`/`setweapons` do at a single-player start (Resolution)

- Old: SPEC-760 B1.4: "`noweapons` ≠ 0 → weapon mode 0; else `setweapons` > 0 → mode setweapons; in mode 0 or 1 the
  world gets a weapon-restriction helper with value −1.0 (mode 0) or −2.0 (mode 1) (UNVERIFIED effect)"; SPEC-780 §21
  "Level-script override (UNVERIFIED trigger): value −1 … value −2 …". Observation: ger1/tsr1 begin with three
  prediction restarts, arm1 (`noweapons 1`) with two and no weapon.
- New: the trigger is the worldspawn keys; the "level-script override" is exactly this world value:
  1. worldspawn: `noweapons` ≠ 0 → value −1; else `setweapons` = 1 → value −2; `setweapons` ≥ 2 creates the world's
     record but leaves the value 0 (no effect); `setweapons` ≤ 0 and `noweapons` 0 → nothing.
  2. At every single-player spawn the mode's loadout first looks at that value:
     - **−1** (`noweapons`, e.g. arm1, tut1): the normal loadout is skipped entirely — no player-module reset, no
       knife/hands, no load from the persistence cvars; the player keeps the empty inventory he entered with. Only on a
       map whose name begins with `tut1` the in-view models of the sniper rifle and the .44 pistol are precached and
       the level's `weaponsAvailable` mask is passed to the player module.
     - **−2** (`setweapons 1`, e.g. tsr1): the player module is reset (one prediction restart) and the fixed kit of
       §21 is given (knife, 9 mm pistol, shotgun and the hands with their in-view precaches, 6 knives, 128 rounds
       9 mm, 32 shells, 2 C4, 5 flash packs, armour 100, best weapon) instead of the persistence load.
     - otherwise: the normal loadout of §21 (reset with one prediction restart, knife and hands, load from the
       persistence cvars, remembered or best weapon).
  The loadout itself restarts prediction once in the normal and the −2 path and never in the −1 path; the other two
  restarts of the begin packet come from outside the loadout — hence three on ger1/tsr1 and two on arm1. Replace
  "(UNVERIFIED trigger)" in §21 by "worldspawn `noweapons` (−1) / `setweapons 1` (−2), SPEC-760 B1.4" and add the
  `setweapons` ≥ 2 rule to B1.4. OpenMerc's reading **matches** except that −1 must skip the reset as well.
- Verify: done (ger1, tsr1, arm1 begin packets). tut1: view-weapon precache of the sniper rifle and .44 at spawn, no
  weapon owned. A test map with `setweapons 2`: normal loadout.

#### G18.34 SPEC-AMEND-005 G18.27 / SPEC-751 2.1 — which remote cameras set position 24 = `p` (Refinement)

- Old: SPEC-AMEND-005 G18.27: "`func_remote_camera` → position 24 = `p`"; SPEC-751 2.1: "a camera with type 4 also
  makes the level precache an extra resource group". Observation: ger1 (two cameras of type 2) keeps `n`; tsr1 and
  dm/raven (a type-4 camera) have `p`. OpenMerc sets it only for type-4 cameras.
- New: confirmed: only a `func_remote_camera` whose type (key `volume`) is exactly 4 sets position 24 of the
  material-flag string to `p`, at spawn. A camera without `target` is removed at spawn before that point and sets
  nothing; types 0–3 and any other value set nothing. Amend the G18.27 table row to "`func_remote_camera` with type
  (`volume`) 4 → position 24 = `p`". OpenMerc **matches**.
- Verify: done (ger1 `n`; tsr1, dm/raven `p`).

#### G18.35 SPEC-751 2.1 flag 2 — what "non-solid" means for the watchers (Correction)

- Old: "watchers become non-solid and invulnerable". Observation: while such a camera is active the original keeps
  sending the player's packed box; OpenMerc clears the clip mask and keeps the solid type.
- New: when a flag-2 camera starts, each watcher (every player in use without spawnflag 1, else only the activator)
  gets **solid type 0 (not solid)** and the god-mode entity flag 0x10 (invulnerable); when the camera ends, the same
  watchers get solid type 2 (box) and the flag is cleared. Nothing else changes: not the clip mask, not the contents,
  not the box. Effect: from that moment the server's traces and area queries skip the player (shots, projectiles,
  pushers and characters pass through him); his own movement still uses his clip mask. The packed box in the entity
  state is computed by the engine only when the entity is linked (SPEC-202: packed only for solid type 2, else 0), and
  the camera code does not relink the player; the observation that the old packed box keeps being sent shows that the
  player is not relinked with the new solid type while watching (UNVERIFIED which per-frame step skips the link). So
  "non-solid" means the solid type, and the wire box is not a reliable sign of it. OpenMerc **differs**: it must set
  the solid type to 0 (and back to 2), not clear the clip mask.
- Verify (black box): tsr1 or ger2 with a flag-2 camera running: a character's shot (or a thrown grenade) passes the
  player's position without hitting him and without damage; after the camera ends shots hit again.

### D. G2 character AI (items 10–16)

#### G2.10 SPEC-700 §15 and SPEC-722 — what the stored floor height is (Resolution)

- Old: SPEC-AMEND-005 G2.3: the stored floor height is "the z of the `quake_ground` bolt in the character's entity
  space". The clean team asks whether it is something other than the raw bolt height (offset, scale, only some
  families), because kos1 `introjohn` (`m_x_mcharacter`) stays at origin z 96 in the original while OpenMerc lifts
  him to 102.9.
- New: it is the raw bolt height, the same for every family, with no offset. In detail:
  - The value is the translation z of the `quake_ground` bolt matrix in matrix mode 5 (SPEC-011): bolt × instance
    transform. For human characters the instance transform is the −90° turn about z times the uniform size scale
    (1.0 for `m_x_mcharacter`), with no translation. So the value is the model-space bolt z × size scale. The
    Mullins types have no rule of their own; they use the common human set-up, presets and body.
  - It is written in two places. First, by the per-update bolt event (mode 5), but only while the character has a
    ground entity or is a corpse (solid 5). Second, when the body starts a sequence **different from the previous
    record's sequence** (or force-restarts one). In that case the bolt is evaluated right after the Play call, at the
    current level time, with no ground condition. The new sequence then shows its first frame, also when it was
    started 0.1 s in the future.
  - The smoothing (SPEC-700 §15) compares the stored value with the entity's **mins z** (the box bottom relative to
    the origin). It runs only when the requested preset equals the current preset, which never happens on the first
    request: the preset index starts at "none". It returns before moving anything when the character has no ground
    entity and is not a corpse.

  So there is no hidden factor: if the original keeps `introjohn` at 96, its stored value at that moment lies within 1
  unit of mins z (−32), or the smoothing returned early (no ground entity, or a preset change instead of a repeat).
  OpenMerc's 102.9 means its stored value was about −39, which points at the sampled pose: the frame or sequence
  evaluated at a sequence start, or a per-update event delivered while grounded.
- Verify: this needs a black-box run. Save the game in the original right after `introjohn` wakes, and read his
  `AIAI` record field 264 (stored floor height) and field 260 (current preset). Compare with OpenMerc at the same
  moment. A value near −32 means OpenMerc's bolt sampling is off; a value near −39 means the original's smoothing did
  not run (check his ground entity in the same save).

#### G2.11 SPEC-722 "Firing" and SPEC-711 — shots with a target-bolt selector or the lethal flag (Correction and Resolution)

- Old: SPEC-722 Firing: "goes towards the target point (optionally a bolt of the target: head, shoulders, chest,
  groin, gut)"; aim spread per attack type. OpenMerc (interim): straight at an estimated bolt point, no spread.
- New: when the body fires with the lethal flag or a non-zero selector, and the AI's target is in use, the shot takes
  a separate path:
  1. **Start**: the shooter's origin + (0, 0, 20), not the weapon's `flash` bolt.
  2. **Aim point**: when the selector is non-zero and the target has a Ghoul instance, the selected bolt's world
     position at the current pose. This is the bolt matrix in mode 5 at the level time, times the target entity's
     matrix from origin and angles. Otherwise, or when the bolt is not found, the target's origin.

     | Selector | Bolt |
     |---|---|
     | 1 | `sbolt_mouth` (head) |
     | 2 | none defined (the original reads an unrelated id; use the target origin) |
     | 3 | `gbolt_shoulder_lf` |
     | 4 | `gbolt_shoulder_rf` |
     | 5 | `gbolt_chest_f` |
     | 6 | `gbolt_gut_f` |
     | 7 | `gbolt_groin` |
     | 8 | `wbolt_hand_r`; the target also drops the weapons in both hands (the body's drop-hand-weapons rule) |
     | 9 | `sbolt_mouth`, with kill variant 2 (below) |
     | other | none (as 2) |

  3. **Direction** = aim point − start. There is **no facing test** and **no per-type aim spread** on this path; the
     per-type spread belongs to the character's ordinary aim direction. Two rules still apply when the target is in
     use and the AI does not fire blind:
     - attack types 9, 14, 16, 17 and 18 replace the direction with (target origin, or a leaning player's lean
       hitbox, 16 units lower when the target player is crouched) − start;
     - type 11 mixes 60 % of that direction with 40 % towards the target origin.

     The AI's blind-fire flag adds U(−0.1, 0.1) per component, as on every path.
  4. **Lethal flag**: before the shot the target's body gets the scripted kill setting of SPEC-720 §10 rule 1:
     variant 1, or variant 2 (adds SPREAD) for selector 9. The target's **next hit from any source** then deals 9999,
     and the setting is cleared. The action clears its lethal flag after this shot (SPEC-711), so only the first shot
     carries it. If that shot misses, the setting waits for the next hit.

  This explains the clean team's evidence. The tsr1 `i200=1` shot (60 units, type 3) goes exactly to the head bolt
  and kills with the first round. tsr2 `hallraider1` (`i200=5`, machine gun type 12) aims at the **chest** front
  bolt, not the gut, with no spread, and kills at 300 units. The SPEC-722 bolt list order is wrong. OpenMerc differs:
  start point, missing selectors 3–9 meanings, the kill setting, the per-type overrides.
- Verify: tsr1 intro in the original with `developer 1`: the console prints `attacking, targetpoint` with the
  victim's mouth-bolt position. Give a scripted attack `i200=8` against a soldier: he drops his weapon. A selector-5
  machine-gun shot hits zone chest-front at long range in 10 of 10 runs.

#### G2.12 SPEC-710 C12 — path and point waits after G2.9 (Resolution)

- Old: C12: "`waitaction1` … `waitaction6` animations play in order (each as a play-in-place action facing the
  corner's angle)". OpenMerc: re-requests the same waitaction while the body plays it and advances when its animation
  has ended.
- New: OpenMerc's rule is the original's. Precisely, for `path_corner` (kind 7) and `point_combat` (kind 8), which
  share the wait planning:
  - On arrival the wait starts: the wait end time is now + `wait`, the index is 1, and a "first wait plan" mark is
    set.
  - Every think during the wait the decision plans again. The waitaction action is made directly, not through D3,
    with duration 0 and "ends with the animation" set. It therefore completes in its first think, exactly like a
    G2.9 class-0 idle.
  - At each wait plan, the index advances by one when the mark is clear **and** the body's finished flag is set. The
    finished flag is set by the playing sequence's `EOS` note and cleared by every new playback. It is not "animation
    done", which counts loops as done. The mark is then cleared, so the first plan after arrival never advances.
  - The waitaction at the index is requested. Re-requesting the record that is playing does not restart it, so a
    play-once waitaction plays for its length and a looping one for one cycle. Then the next one starts.
  - When the index points past the last set waitaction:
    - without corner spawnflag 2, the index returns to 1;
    - with spawnflag 2, the index stays, `waitaction1` is requested for that one plan, and when that action completes
      the wait end time is set to now − 1, so the decision moves on to the next corner. The spawnflag-2 list is
      therefore played once, followed by a one-think request of the first waitaction (derived; UNVERIFIED whether
      that restart is visible).
  - Independently, the wait ends when the wait end time is reached, whatever the index.

  A waitaction that is the same record as the previous one (or a list with one entry) does not restart. Its finished
  flag stays set, so the index then advances every think until a different record is requested. OpenMerc matches;
  make sure it uses the finished flag (`EOS`), not "animation done".
- Verify: tsr2 `cch_escantalk_lt/rt` alternate at their animation lengths (~1.6 s). nyc1 point wait:
  `std_itouchnuts_n_a_n` once (3 s), then `std_ilooking_n_a_a`.

#### G2.13 SPEC-750 §12.4 and SPEC-710 D3 — records of script kinds 5 and 6 (Correction)

- Old: SPEC-750 §12.4: "Kinds: 0, 5, 6 play the animation in place". OpenMerc now maps a death-class record of a
  play-in-place command to a class-3 death action.
- New: for every script kind, the kind only decides **how the record is looked up**. The action is then always made by
  D3 (SPEC-710) from the record's animation class:

  | Script kind | Record lookup |
  |---|---|
  | 0, 3, 4, 5, 6 | the body's stand choice with the command's animation (by number or name) |
  | 1 | the body's movement choice |
  | 2 | the body's attack choice |
  | 8 | `std_istand_n_a_n` |
  | other | `Invalid action code (%d)!` printed, then as kind 0 |

  The action is made with the command's destination and facing, and with the target entity, lethal flag and selector
  when the lethal flag or the selector is set. It gets the 0x40 duration only when 0x40 was given, else D3's default.
  So `ai(blowback1, 72)` gives a class-6 record and D3 makes a class-3 death action (9999 s), as observed. A kind-5
  command with a class-5 record gives a 4 s hit-reaction action ending with the animation. With a locomotion record
  it gives a move action, and so on. "Play in place" holds only for records of class 0 or 5. OpenMerc matches for
  death records; extend the rule to all classes and all kinds.
- Verify: a script `ai(x, <jump record>)` with kind 5 makes the character jump (class-9 action), not play the jump in
  place.

#### G2.14 SPEC-722 cow idle choice and SPEC-AMEND-005 G2.9 — why the sud2 cows keep `std_i_n` (Resolution)

- Old: SPEC-722 cow: "keep the current stationary animation while it is not done; otherwise with probability 1/60
  `std_imoo_n` …". Clean team: with a stand choice every think, cows would switch about every 3 s, but the three sud2
  cows near the start stay in `std_i_n` for 11 s.
- New: neither proposed explanation holds. The binary shows two things:
  - Cows (`m_x_bull`, type 6) re-plan every think like humans. They use the generic decide and plan, and their only
    decision is the combat decision (kind 2).
  - A looping record without 0x00400000 **is** "done". `std_i_n` has flags 0x08000000, so every call of the cow's
    idle choice rolls the two 1/60 chances.

  The difference is in **when the idle choice is asked at all**:
  - The combat plan asks the body for a stand animation only when it stands: no target position, the owner airborne,
    the stand/crouch branches of SPEC-710 C4/C5 (reaction delay, stuck, blocked).
  - With a target it asks for attack or movement records. The cow body has neither: its attack choice records the
    attack time and returns no record, and its movement choice returns none. D3 then queues nothing, there is no
    current action, and the looping `std_i_n` simply continues.
  - Cows are on team 100, so every other team, the player's included, is an enemy (SPEC-700 §13). As soon as a cow
    perceives the player it has a target and stops rolling idles. The profile of type 6 is not copied from the table,
    so its reaction value is 0 and the attack branch is taken at once.

  So the original's 11 s of `std_i_n` are cows that hold the player as target. This is derived; it needs a black-box
  check. OpenMerc probably either does not make the player a cow's target, or falls back to a stand or idle when the
  body returns no record. SPEC-710 D3 says "No record → nothing is queued".
- Verify: needs a black-box run. On sud2, `notarget` before the cows can perceive the player (or `gimme m_x_bull` far
  out of sight): the cows switch idles about every 3 s. With the player visible they stay in `std_i_n` and do not
  turn.

#### G2.15 SPEC-700 rule 1.1 and rule 10 step 6 — "gravity 0.01" and corpse "gravity 0.5" (Correction)

- Old: rule 1.1: "mass 200, gravity 0.01"; rule 10 step 6: "movement step, gravity 0.5"; Constants "corpse gravity /
  solid / clip mask 0.5 / 5 / 0x4010003". The clean team observed full gravity for a spawned soldier.
- New: both values go to the entity's **ground friction**, not its gravity. Gravity keeps the default 1.0 set when
  the entity is created. This matches SPEC-760's field table: "ground friction 1.0 (characters: 0.01)", gravity 1.0.
  - Common character set-up: ground friction := 0.01.
  - Death: ground friction := 0.5.

  Under SPEC-760 B4.6 step physics, a living character on the ground therefore loses 60 units/s of horizontal speed
  per frame (100 × 0.01 × 60), so a knockback slides it noticeably. A corpse loses 150 units/s per frame
  (0.5 × 300). Vertical friction while flying uses the same factor. The observation is right and SPEC-700 is wrong.
  OpenMerc's gravity 1.0 is right; it must also set ground friction 0.01 (and 0.5 at death) to match sliding after
  hits.
- Verify: `gimme m_irq_msoldier1` 50 units up: lands in 0.3 s (z −8, −16, −24 per frame). Shoot a standing soldier
  with the shotgun at point blank (SPEC-782 knockback): he slides and stops at 60 units/s per frame, not at once.

#### G2.16 SPEC-710 C4/C5 and SPEC-722 — where "ready to fire" is consulted (Resolution)

- Old: C4/C5 request an attack whenever the reaction age allows; SPEC-722 "Ready to fire" is an "AI query" without a
  named caller; the attack-choice lists name fire records without a readiness condition. OpenMerc consults it in the
  decision and also aims instead while the target is more than ~44° off the facing.
- New: the decisions never consult it. The **body's attack choice** consults it once per choice, after the reaction
  override and the special cases (prone, lying on the back, corner peeks):
  1. ready := "ready to fire" (SPEC-722: time since the last shot > d × F × h × p);
  2. ready is cleared when the character's facing is more than about 44° from the target direction (dot < 0.717, the
     same limit as Firing);
  3. **Crouched**, current record of the crouched posture: with ready, the fire list `cch_f_fwd_m_2`, `_r_2`, `_s_2`,
     `_p_2`, `cch_a_fwd_p_2` (MP), `cch_f_fwd_l_2` is tried; without ready, or when none exists, the crouched aim list
     `cch_a_fwd_r_2`, `cch_a_fwd_ms_2` (S/M), `cch_a_fwd_l_2`, `cch_a_fwd_p_2`;
  4. **Standing**: the melee slash `std_mslash_n_kp_n`, the 1-in-6 `std_fattitude_n_p_a` and every `std_f_*` fire record
     (`std_fautosweep_n_m_2`, `std_f_fwd_m_2`, `_r_2`, `_s_2`, `_l_2`, `std_f_n_p_2`, `std_f_fwd_p_2`, `std_f_fwd_p_cb`)
     require ready; the aim records `std_a_fwd_*` (and `std_a_up_*` / `std_a_dwn_*` by target height) are the
     fallback;
  5. the moving, throwing and knife-run branches do not test readiness.

  So between shots an attack request yields an aim record, which also turns the body towards the target. A fire record
  is chosen only once the refire time has passed and the character faces the target, then its `fire` note shoots
  without pacing (SPEC-711 A4). The kos1 cadence follows: 4.0 × T-d × 0.75 (designated) = 3.0 s at skill 2, plus the
  next think and the note time. OpenMerc's placement in the decision gives the same records when its readiness and
  facing tests equal the body's. It differs when the body's other branches (reaction override, prone, moving) would
  have been taken first.
- Verify: kos1 shotgunner, skill 2: `std_a_fwd_ms_2` / `cch_a_fwd_ms_2` between shots, fire records exactly 3.2 s
  apart, and no fire record while the player stands more than 45° off his facing.

### E. G3 damage model, gore, bodies (items 16–22)

#### G3.16 SPEC-721 rule 3.4 — cap bolts switched off at every hand-over (Refinement)

- Old: rule 3.4: the zone and every zone reached through "next zone of piece" hand over to the piece their wound mesh,
  body cap mesh, the bolt-ons on the body-side cap bolt and those on the gore bolt; "all these are hidden/removed on the
  body". The clean team observed that after a gib both cap **bolt parts** of every handed-over zone are switched off on
  the body and stay on on the piece.
- New: the hand-over is one procedure, used by every severing:
  - rule 3 severing of human bodies;
  - every limb of a gib (rule 8);
  - the Dekker split;
  - the dog and cow limb severing.

  For each zone of the chain (start zone, then "next zone of piece" until the chain returns to the start or ends):
  1. Wound mesh: handed over as in rule 3.4 when a piece exists; then hidden on the body.
  2. Body cap mesh: shown on the piece if it was shown on the body (when a piece exists); then hidden on the body.
  3. Body-side cap bolt: its bolt-ons move to the piece (when a piece exists) and are removed from the body. The
     `g_half_head` special case keeps its head, face and `gz_half_head` skins. Then **the bolt's part is switched off
     on the body** (instance part flag off).
  4. Piece-side cap bolt: a stuck knife on it becomes a knife pickup (rule 10); every bolt-on on it is removed from
     the body. Then **the bolt's part is switched off on the body**.
  5. Gore bolt: its bolt-ons move to the piece (when a piece exists) and are removed from the body. The gore bolt's
     part is **not** switched off.

  The piece is a clone made before the hand-over, so it keeps both cap bolt parts on. The observation is right and
  holds for every hand-over, rule-3 severing included. OpenMerc matches. Add the "part off" of steps 3 and 4 to
  rule 3.4 and note that step 5 does not switch off.
- Verify: sever a corpse's left forearm with a close shotgun blast (rule 3). The body's instance shows both
  `…_CAP_…` bolt parts of zone 12 switched off, the piece's on.

#### G3.17 SPEC-721 rule 8.6 — a gib limb that gets no piece (Correction)

- Old: rule 8.6: "caps and hand-over as in rule 3 (no blood clouds)". The clean team observed for a limb without a
  piece: body cap shown and cap bolts off, but the owner part and the rest of the sever group stay shown.
- New: for each gib limb chosen by rule 8.5:
  1. A piece is spawned only when the limb is not the head, G < 5 and fewer than 2 pieces were spawned by this gib
     (unless lock_sever, which stops everything).
  2. Only when the zone has a body cap mesh or a body-side cap bolt:
     - With a piece: the cap bolt-on (`g_cap1` or the zone's cap model, attached by its own bolt when it names one)
       goes to the piece at the piece-side cap bolt, and the piece cap mesh is shown on the piece with skin `c_blood`.
     - The zone hand-over of G3.16 runs **with or without a piece**. Without one, nothing is shown anywhere, but the
       wound meshes and body cap meshes are hidden on the body, the cap bolt parts are switched off and the bolt-ons
       on the cap and gore bolts are removed (knives become pickups).
     - The **sever group walk runs only with a piece**. Without one the group is not walked, except a head group,
       whose head entries are still processed. So the owner part, the other meshes of the group and their bolt-ons
       stay on the body.
     - Body side, always: the body cap mesh is shown with skin `c_blood`. The cap bolt-on (`g_cap1` or the zone's cap
       model; `g_half_head` with the character's head, face and `gz_half_head` skins) is attached at the body-side
       cap bolt.
     - There is **no** separate "hide the owner part and the wound meshes" step (rule 3.7) in a gib: the owner part is
       hidden only through the sever group walk.
  3. A spawned piece is freed again at once when more than 1 piece was already spawned by this gib or G > 2. The walk
     has already moved the limb's meshes to it, so that limb vanishes from the body.

  The observation is right and the spec's "as in rule 3" is wrong for the no-piece case. OpenMerc matches.
- Verify: a grenade gibbing four soldiers at once (G ≥ 5 for the last). The last body shows bloody caps at the limb
  joints with the limbs still attached.

#### G3.18 SPEC-721 §6 — no limbfall sound for gib pieces (Refinement)

- Old: §6 table: "touch | first touch plays `impact/gore/limbfall.wav`"; the paragraph on gib pieces gives them the
  `gore/danglyblood` touch. The clean team observed no limbfall sound from gib pieces.
- New: a gib piece's touch **replaces** the piece's ordinary touch, so `impact/gore/limbfall.wav` is never played for
  gib pieces; the limbfall touch exists only for rule-3 pieces. The gib touch does three things:
  - sends `gore/danglyblood` (direction −surface normal) when the toucher is the world, lock_blood is off and the
    piece moves faster than 2 units/s;
  - stops the piece's spin (angular velocity 0) on **every** touch, whatever the toucher;
  - stays installed, so every later world touch can send the effect again.

  The ordinary limbfall touch plays the sound once (channel 4, volume 0.6, attenuation 1) and then removes itself.
  The observation is right; OpenMerc matches.
- Verify: gib a soldier: the pieces send `gore/danglyblood` on each bounce and no `limbfall` sound. Sever a corpse's
  forearm by shotgun: one `limbfall` sound on its first touch.

#### G3.19 SPEC-720 §13 and SPEC-722 "Hit reactions" — pain reaction of a deathmatch player body (Resolution)

- Old: SPEC-720 §13: "if the damage is not lethal the body plays a pain reaction (SPEC-722)". The clean team observed
  `std_sarmunhurt_lt_a_n` for 0.2 s, then the locomotion pose.
- New: the reaction is chosen and ended as follows.
  - **Choice.** After the body's hit handling, when the returned damage is below the player's health, the game asks
    the body's ordinary hit-reaction choice (SPEC-722 "Hit reactions"). It passes a damage value of **1.0**, so the
    knock-down rule (damage > 75) never applies to players. The rules are the human ones; a player has no AI, so the
    "no-disarm" AI state is never true. It applies only while the current record's posture is standing. In order:
    1. Arm hit pending: `std_sgunfromhand_n_n_n` (disarm) when armour ≤ 0.5 % or health ≤ half, else
       `std_sarm_rt_n_n`.
    2. The limb-injury state of SPEC-720 rule 9, which is set for players too: per limb, the hurt record if the model
       has it, else the unhurt one (`std_sarmunhurt_lt_a_n` for the left arm).
    3. Front or back reactions.

    Every record is used only if the model has it. The stock player model (`MESO_PLAYER`) lacks
    `std_sgunfromhand_n_n_n`, `std_sarm_*`, `std_sleg_*` and the hop/clutch `std_parm_*`/`std_phop_*`. So a player
    gets only the "unhurt" limb records and `std_sunhurt_*`. With a record chosen, the player's animation speed is set
    as for locomotion and the record is **forced** (restarted). The choice first resets the instance speed index to
    normal (4).
  - **End.** The player's per-frame body update (SPEC-AMEND-005 G18.26) keeps the current record while it is "not
    done" and carries flag 0x00400000. All hit-reaction records do (`std_sarmunhurt_lt_a_n`: class 5, flags
    0x00404000, play once). The record therefore stays until its `EOS` sets the finished flag, then the next frame's
    movement or standing choice replaces it. The 0.2 s is the length of `std_sarmunhurt_lt_a_n` in the player model
    plus at most one server frame. Nothing else times it.

  The observation is right; OpenMerc lacks this. Implement the call with damage 1.0 and the keep rule.
- Verify: from the model data, the play-once length of `STD_SARMUNHURT_LT_A_N` in `MESO_PLAYER.ghb` equals the
  observed 0.2 s (± 0.1 s). A hit from the front with 100 damage on a player never plays `std_sblown_2bk_n_n`.

#### G3.20 SPEC-720 §10–§13 — 21 instead of 22 health for a pistol hit on a player's upper arm (open; needs a black-box run)

- Old: SPEC-720 §11 rule 6: one hit zone gives damage = trunc(damage × scale). The clean team observed each pistol1
  hit (attack 3, damage 45, 61 units) on zone 10 (scale 0.5) taking 21 health, not 22.
- New: the binary's player path gives **22** for that input. The original's arithmetic was checked step by step:
  1. Shot (SPEC-782): the damage is 45. Falloff starts only beyond half of the 3 × range trace (768 units for
     range 512), so there is none at 61 units. T_Damage gets the integer 45 with flags 0x200 and gore values 0 / 0.
  2. T_Damage (deathmatch): no single-player skill scaling. The player's hit handler calls the body's hit handling and
     subtracts **exactly** its result from health (female bodies alone add half again).
  3. Body hit handling, player part:
     - the inventory armour method (SPEC-003 method 31) returns the damage unchanged at armour 0;
     - the zone multiplier computes trunc(45 × 0.5), with the scale stored as exactly 0.5, divides by the zone count
       and truncates;
     - the overkill cap (health + 150) does not apply.
  4. Realistic damage (dmflags 16) changes only the armour split and caps limb zones at trunc(health × 0.33). That
     does not give 21 at health 100 either.

  21 therefore needs a different input. The arithmetic admits these causes:
  - a damage of 42 or 43 reaching the body, for example from a second shot path or a modified attack value;
  - a hit list with more than one zone whose truncated average is 21 (no pair of stock scales gives this with 45);
  - armour on the player.

  None is visible in the code for this case, so the spec is not shown wrong, and OpenMerc's 22 follows it. This item
  stays open.
- Verify: needs a black-box run. Repeat the test once with the attacker at 61 units and once at 400 units, against
  the chest (scale 1) and the arm (0.5), with `armor 0` confirmed in the player state. Expected by the code: 45 / 22.
  If the chest shows 44 or 43, the loss is in the shot, not the zone rule. Also log T_Damage's damage argument with
  the G3 shim to see whether 45 arrives.

#### G3.21 SPEC-720 10.5 — when the second estimator is used and what it draws (Resolution and Correction)

- Old: "A second estimator (used when the character is in certain postures, UNVERIFIED which) has no leg case and uses
  right side 7, 3, 16, 11, 4 / left side 7, 5, 18, 12 (up to 5 drawn) and the same front/behind sets." OpenMerc
  (interim): crouched (1) or sitting (4); exits as in the first estimator; 5 zones on the right, 4 on the left.
- New:
  - **Selection.** With no hint zone, the posture field of the body's current record decides:
    - 1 (crouched), 2 (prone), 5, 6 and 7 (lying face down, on the back, on the side) → the **second** estimator;
    - 0 (standing), 3, 4 (sitting), any other value, or no current record → the first.

    OpenMerc's "sitting" is wrong, and prone and lying are missing.
  - **Direction.** The second estimator uses the horizontal direction from the damage origin to the hit point. The
    first uses the direction to the target's origin. c = that direction · the target's forward; "right" means the
    direction · right ≤ 0.
  - **Sets.** Each set is shuffled and its first k entries are taken:

    | Situation | Exit zone | Candidates | k |
    |---|---|---|---|
    | from behind (c > 0.707) | 7 | 8, 6, 4, 14 | 1–4 |
    | side, right | 8 | 7, 3, 16, 11, 4 | 1–5 |
    | side, left | 8 | 7, 5, 18, 12, **6** | 1–5 |
    | from the front (c < −0.707) | 8 | 7, 5, 3, 13, **16** | 1–5 |

    There is no leg case. The exits equal the first estimator's torso exits. The spec's left set lacks 6. The front
    set is **not** the first estimator's: it has five candidates, 16 included.
  - **Head chance.** With probability 1/(n + 1) (a uniform draw 0…n hitting 0) a head zone is added:
    - zone 1 from behind;
    - zone 0 otherwise. When 5 zones were already drawn, zone 0 replaces the fifth instead of being appended.

    n = 6 when the damage origin is above the target's origin. Otherwise n = 4 when the hit point is above the damage
    origin and less than 10 units from the top of the target's box. Otherwise n = 10.

    The first estimator computes n the same way, so its wording "when the point is above the target's origin"
    should read "when the damage origin is above the target's origin" (UNVERIFIED in game).
- Verify: spread damage (a shotgun blast flagged 0x20000) on a crouched soldier from his left: zones are drawn only
  from 7, 5, 18, 12, 6 (+0), the exit is 8, and never a leg zone below the thigh. The same blast on a sitting
  character uses the first estimator's sets.

#### G3.22 SPEC-722 "Savegames" — private body image (acknowledged, no change)

- OpenMerc's own layout inside the original chunk ids and sizes is acceptable: no other module reads the body image,
  and loading original savegames is out of scope.

### F. G4 roster, helicopters, vehicles, special characters (items 1–10)

#### G4.1 SPEC-731 A §4 / open question 2 — which rotor gets which scale (Resolution)

- Old: "scaled uniformly by 0.25 and 1.25 (choppers) or 0.375 and 1.875 (Hind) — UNVERIFIED which factor belongs to
  which rotor (expected: the small one to the tail rotor)"; open question 2. Observation: `ROTOR_TAIL` 0.25 (Hind
  0.375), `ROTOR_MAIN` 1.25 (Hind 1.875).
- New: confirmed. The rotor bolted at `rotor_tail` gets the small factor (0.25; Hind 0.375), the rotor bolted at
  `rotor_main` the large factor (1.25; Hind 1.875). The factor scales the bolt-on instance's own transform uniformly
  (the instance transform is read, multiplied by a uniform scale matrix and written back) right after the bolt-on is
  created, before its sequence is set. Delete the UNVERIFIED note and open question 2. OpenMerc **matches**.
- Verify: done (trn1, jpn3 captures). Data check: the tail rotor is attached first, so its instance id is the lower
  of the two.

#### G4.2 SPEC-731 A §9.3 — target yaw and the turn step (Correction)

- Old: "Target yaw = yaw of (point − origin). If the error is ≤ 9° the yaw snaps to the target: facing. Otherwise the
  yaw changes by 9° in the shorter direction (anglemod) and the absolute error is reported." Observation: the target
  is the exact atan2 direction in (−180°, 180°], not an integer yaw in [0, 360); a snap can leave a negative yaw;
  the error is reported before the step; facing comes one frame after the last step.
- New: the observation is right; exact rule per call:
  1. Exactly above or below the point (same x and y): facing, error 0, nothing changes.
  2. Target T = the continuous yaw of (point − origin) (SPEC-760 B3.6 `vectoangles`: atan2 in degrees, range
     (−180, 180], not rounded).
  3. If the current yaw Y is above 180, 360 is subtracted from it first (Y is stored back).
  4. d = T − Y, **without** wrapping. If |d| ≤ 9: Y := T exactly (so Y can stay negative), the reported error is 0,
     and the result is "facing"; nothing else happens in that call (no bank change).
  5. Otherwise the reported error is |d| (the value before the step, unwrapped). Let a = anglemod(d) (SPEC-760:
     16-bit truncation, range [0, 360)). If a < 180, Y := Y + 9, else Y := Y − 9; then Y := anglemod(Y). The result
     is "not facing"; the bank rule of §9.3 follows when requested.
  Consequences: a turn that ends inside 9° reports "facing" only on the following call (the call after the last 9°
  step); the stored yaw after a snap is the raw atan2 value, after a step it is in [0, 360) and quantised by the
  16-bit anglemod. Replace the first two sentences of §9.3 by these steps. OpenMerc (following the observation)
  **matches**.
- Verify: done (trn1 face turn). Unit test: Y = 350, point at yaw −3.8: step 3 gives Y = −10, d = 6.2 ≤ 9, so
  Y = −3.8 and facing at once. Y = 10, target −170: |d| = 180 is reported, a = anglemod(−180) = 180, Y := 1.

#### G4.3 SPEC-731 A §9.5 — strafe arrival does not snap (Correction)

- Old: "Arrival as in §9.2 (snap, v = 0, barrel stops, arrive routine)." Observation (trn1, id 22, 2 092 units): the
  strafe stops 52 units short, at the first frame where |origin − P1| ≤ v on every axis; the origin is not set to P1
  (flight legs do end exactly on P1).
- New: the **observation is right; the spec is wrong for the strafe.** Each strafe frame, before any speed change or
  movement, the arrival test compares |origin − P1| on every axis with the current speed v (the speed reached in the
  previous frame). When it holds: v := 0, the barrel plays `barrel_still` (always, not only with fire-while-flying),
  and the *arrive* routine (§9.8) is installed for the next frame. The origin is **not** moved to P1; the strafe ends
  up to v units short of the destination on each axis. (The flight leg of §9.2 does set origin := P1.) Replace
  "Arrival as in §9.2 (snap, v = 0, barrel stops, arrive routine)" by "Arrival test as in §9.2, but without the snap:
  v = 0, barrel stops, arrive routine; the origin stays where it is." OpenMerc (following the observation)
  **matches**.
- Verify: done (trn1 strafe ends 52 units short, legs end on P1).

#### G4.4 SPEC-731 B §10–§11, A §8, §13 — "along the bolt's direction" is the bolt's z axis (Resolution)

- Old: B §10 "from the `flash_canon` bolt along its direction"; B §11 "position and direction", "along that muzzle's
  direction"; A §8/§13.2 "barrel forward". Observation: all stock muzzle bolts have z along the barrel; the irq2b
  cannon shell hit point is reproduced only with the z axis.
- New: confirmed. Every muzzle direction of the helicopter and vehicle code is the **third axis (z) of the bolt's
  world matrix**. That matrix is the bolt matrix of the bolt-on that carries the bolt (method 50 at the current level
  time, mode 5, through parents) followed by the entity's origin and angles; its translation is the muzzle position.
  This applies to the tank cannon shell trace (`flash_canon`, ×4096 for the trace end), the tank machine gun
  (`flash_machine_gun`), the snowcat guns (`_flash1`), the helicopter gun (`flash_barrel`, direction × 100 before
  the jitter) and the helicopter rocket pods (`FLASH_ROCKET1`/`FLASH_ROCKET2`). Replace "direction"/"forward" of a
  bolt by "the bolt's z axis in world space" in those sections. OpenMerc (z axis) **matches**.
- Verify: done (irq2b cannon shell hits (856.0, 33.8, −54.8)).

#### G4.5 SPEC-731 B §1/§2 — snowcat hull model and "sequence set `snowcat`" (Correction)

- Old: B §1 table: "model directory / sequence set: `enemy/snowcat` / `snowcat`"; §2: hull `enemy/snowcat` / `body`.
  Observation: the original registers `BODY.GHL` (skins `LIGHTS`, `SNOWCAT`, `TREADS`) on sib1/sib2; the stock data
  has no `snowcat` model file.
- New: the observation is right and §2 is correct. The pair `enemy/snowcat` / `snowcat` in §1 is only what the
  snowcat passes when its character AI is created (SPEC-700 rule 2); the snowcat's own set-up ignores it and never
  loads a model by that name. The hull is the model-table entry `enemy/snowcat` / `body` (default surface 28,
  debris 9, health 100, solid 2), made through the standard prop model set-up of SPEC-770 §3 steps 1–3. `snowcat` is
  the name of the **sequence** the hull plays once at set-up and again from the turret end-of-sequence callback
  (§16). The same holds for the tank (`enemy/tank` / `tank` is both the creation pair and the hull entry; `tank` is
  also the hull sequence). Reword the §1 row to "AI creation names (unused for models): `enemy/tank`/`tank`,
  `enemy/snowcat`/`snowcat`; hull model: §2". OpenMerc **matches** (it uses `body`).
- Verify: done (sib1/sib2 register the `body` model).

#### G4.6 SPEC-731 A §8/§9.4 — the go-to frames (Resolution)

- Old: OpenMerc interpretation of §8/§9.4: the go-to command flies its first leg from the frame after the command
  starts, and the command completes in the frame after the arrive routine. Observation: the original needs one frame
  more at the start (command → first leg move: 4 frames vs 3) and one more at the end (leg arrival → next command's
  first step: 4 vs 3); other commands match frame by frame.
- New: the extra frames are the **go-to routine's own frames**. Rules (one helicopter think = one frame):
  1. A routine installed by another routine ("pending") or by the autonomous selector never runs in the frame it is
     installed; it runs from the next frame on. An action taken from the queue at the start of a think (no current
     action) runs its command routine in that think; an action that starts because the previous one completed runs
     its command routine from the next think.
  2. The go-to commands (go to point, entity, relative, return home and the flee to the exit point) only store the
     goal (moved to a clear spot, §9.11), set the go-to flag and install the **go-to routine** as pending. Frame 1.
  3. The go-to routine (§9.4) is one frame that does not move: it tests "at G", traces, chooses the next leg
     (destination, leg start = origin, start yaw) and installs the **leg** as pending. Frame 2.
  4. The leg (§9.2) moves from its first frame on. Frame 3 = first movement.
  5. At the arrival frame the leg snaps to P1 and installs the arrive routine as pending.
  6. Arrive (§9.8) runs from the next frame; when level it sets "routine done". In that same frame the selector sees
     the go-to flag (still set) and installs the go-to routine directly; it does not run in this frame.
  7. Next frame: the go-to routine finds origin = G (all components equal, guaranteed by the snap), sets "routine
     done" and clears the go-to flag; the selector then finds nothing, the action completes, and the next queued
     action starts in the same think (its first step runs in the next frame). If G is not reached (multi-leg path),
     this frame chooses the next leg instead (step 3).
  So with an already level helicopter: command start frame N → first movement N + 2; arrival frame A → go-to
  complete A + 2 → next command's first step A + 3. Face, strafe and other commands install their working routine
  directly from the command frame and have no such extra frame. OpenMerc **differs** by one frame at each end; add
  the go-to routine frames.
- Verify: trn1 go-to with frame-numbered captures: the command's first frame shows no movement, the second none, the
  third the first 2.67-unit step; after the snap to P1, two frames without movement before the next command acts.

#### G4.7 SPEC-730 §4 — order of skin registration per type (Resolution)

- Old: §4 lists every type's registration entries but the roster gives the variants in choice order; "Every human
  type first registers the gore skins … Armoured types also register `gz` `gz_armor`." Observation: sets match, but
  the order differs for some types; `gz_armor` comes after `c_blood`/`c_black`.
- New: the registration order of a human type is fixed and has no conditions:
  1. the gore block: `gz` `gz_sing1`, `gz_sing2`, `gz_sing3`, `gz_sing_k`, `gz_multi1`, `gz_multi2`, `gz_multi3`,
     `gz_multi_k`, `gz_mass1`, `gz_mass2`, `gz_mass3`, `gz_mass_k`, then `c` `c_blood`, `c_black`;
  2. then the type's own entries in the order of the table below. An entry whose level test fails (§4) is skipped
     and does not change the order of the others. A face entry P registers material `f` skins in the order `Pn`,
     `Pd`, `Pp`, `Pm`, `Pf`, `Pb` (each only when its expression test holds); a talk entry Q registers `Qt1`, `Qt2`,
     `Qt3` (same tests as §4).
  Several classnames share one list (shown together). Levels are given in brackets when not 3. Add the table to §4
  (the roster keeps choice order). OpenMerc **differs** for the types whose list order differs from the roster order.

| Classname (type) | Registration order after the gore block |
|---|---|
| `m_nyc_eskinhead3` (15) | `b=b_nycskinhleader`, `a=a_nycskinhleader`, `b=b_nycskinhleader2`[1], `a=a_nycskinhleader2`[1], `h=h_w_12_s`, face `f_w_12_`, `h=h_w_1_s`[1], face `f_w_1_`[1] |
| `m_nyc_estockbroker` (16) | `b=b_nycmalestockbroker`, `a=a_nycmalestockbroker`, `a=a_nycmalestockbrokerb`[1], `h=h_b_1_s`[1], face `f_b_1_`[1], `h=h_w_2_c`, face `f_w_2_` |
| `m_nyc_etourist` (17) | `b=b_dcmaletourist`, `a=a_dcmaletourist`, `h=h_w_4_s`, face `f_w_4_` |
| `m_nyc_ebum` (18) | `b=b_nycbum`, `a=a_nycbum`, `hb=hb_h_1_b`, face `f_hb_1_` |
| (type 19, no classname) | `b=b_colmaleprisoner1`, `a=a_colmaleprisoner1`, `hb=hb_h_1_b`, face `f_hb_1_` |
| (type 20, no classname) | `b=b_colmaleprisoner2`, `a=a_colmaleprisoner2`, `h=h_h_1_c`, face `f_h_1_` |
| `m_irq_eman1` (21) | `b=b_irqmalecitizen`, `a=a_irqmalecitizen`, `h=h_m_2_s`, face `f_m_2_` |
| `m_afr_ecommander` (22) | `b=b_ugncommander`, `a=a_ugncommander`, `h=h_b_2_s`, face `f_b_2_`, `h=h_b_1_s`[0], face `f_b_1_`[0] |
| `m_kos_eofficer` (23) | `b=b_kosofficer`, `a=a_kosofficer`, `h=h_w_5_c`[1], face `f_w_5_`[1], `h=h_w_2_c`[0], face `f_w_2_`[0], `h=h_h_6_s`, face `f_h_6_` |
| `m_kos_erefugee` (24) | `b=b_kosrefugee`, `a=a_kosrefugee`, `h=h_w_4_s`, face `f_w_4_` |
| `m_irq_eofficer` (25) | `b=b_irqofficer`, `a=a_irqofficer`, `h=h_m_1_s`, face `f_m_1_` |
| `m_afr_eworker` (26) | `b=b_ugnfactoryworker`, `a=a_ugnfactoryworker`, `h=h_b_4_c`[1], face `f_b_4_`[1], `h=h_b_3_s`, face `f_b_3_` |
| (type 27, no classname) | `b=b_colchemist`, `a=a_colchemist`, `h=h_h_1_c`, face `f_h_1_`, `h=h_h_2_s`, face `f_h_2_` |
| `m_sib_eofficer` (28) | `b=b_sibsuit`, `a=a_sibsuit`, face `f_w_5_`, `h=h_w_5_c`, face `f_w_3_`[2], `h=h_w_3_s`[2] |
| `m_sib_escientist1` (29) | `b=b_sibscientist`, `a=a_sibscientist`, face `f_w_2_`, `h=h_w_2_c`, face `f_w_1_`[0], `h=h_w_1_s`[0] |
| `m_nyc_epunk` (30) | face `f_w_12_`, `b=b_nycpunk2`, `a=a_nycpunk2`, `h=h_w_12_s` |
| `m_x_mcharacter` (31) | face `f_john_`, `b=b_xjohn`, `a=a_xjohn`, `h=h_john`, talk `f_john_n`, talk `f_john_m` |
| `m_x_mcharacter_snow` (32) | face `f_john_`, `b=b_xjohn_snow`, `a=a_xjohn_snow`, `h=h_john`, talk `f_john_n`, talk `f_john_m` |
| `m_x_mcharacter_desert` (33) | face `f_john_`, `b=b_xjohn_desert`, `a=a_xjohn_desert`, `h=h_john`, talk `f_john_n`, talk `f_john_m` |
| `m_x_mmerc` (34), `m_x_mhurtmerc` (35) | face `f_b_6_`, `b=b_mercdemo1`, `a=a_mercdemo1`, `h=h_b_6_s`, talk `f_b_6_n`, talk `f_b_6_f` |
| `m_x_msam` (36) | face `f_sam_`, `b=b_xsam`, `a=a_xsam`, `h=h_sam`, talk `f_sam_n` |
| `m_irq_moilworker` (37) | `b=b_irqworker`, `a=a_irqworker`, `h=h_m_2_s`, face `f_m_2_`, `h=h_m_1_s`, face `f_m_1_` |
| `m_nyc_mpunk` (38) | `b=b_nycpunk1`, `a=a_nycpunk1`, `h=h_w_3_s`, face `f_w_3_` |
| `m_x_miraqboss` (39) | `b=b_xamu`, `a=a_xamu`, `h=h_amu`, face `f_amu_`, talk `f_amu_f`, talk `f_amu_n` |
| `m_x_mraiderboss1` (40) | `b=b_xdekker`, `a=a_xdekker`, `h=h_dekker`, `f=f_dekker` |
| `m_x_mraiderboss2` (41) | `gz=gz_armor`, `b=b_xdekker`, `a=a_xdekker`, `h=h_dekker`, `f=f_dekker` |
| `m_irq_msoldier1` (42) | `b=b_irqsoldier1`, `a=a_irqsoldier1`, `h=h_m_3_s`, face `f_m_3_`, `h=h_m_1_s`, face `f_m_1_` |
| `m_irq_msoldier2a` (43), `m_irq_msoldier2b` (44) | `b=b_irqsoldier2`, `a=a_irqsoldier2`, `h=h_m_3_s`, face `f_m_3_`, `h=h_m_4_c`[2], face `f_m_4_`[2] |
| `m_irq_mrepguard1a` (45), `m_irq_mrepguard1b` (46) | `b=b_irqrepguard`, `a=a_irqrepguard`, `h=h_m_5_s`, face `f_m_5_`, `h=h_m_1_s`, face `f_m_1_` |
| `m_irq_mpolice` (47) | `b=b_irqpolice`, `a=a_irqpolice`, `h=h_m_5_s`, face `f_m_5_`, `h=h_m_4_c`[1], face `f_m_4_`[1] |
| `m_irq_mcommander` (48) | `b=b_irqcommander`, `a=a_irqcommander`, `h=h_m_5_s`, face `f_m_5_`, `h=hb_h_2_b`[2], face `f_hb_1_`[2] |
| `m_irq_mbrute1a` (49), `m_irq_mbrute1b` (50) | `b=b_irqbrute`, `a=a_irqbrute`, `h=h_m_5_s`, face `f_m_5_`, `h=h_m_3_s`, face `f_m_3_` |
| `m_irq_mbodyguard` (51) | `gz=gz_armor`, `b=b_irqbodyguard`, `a=a_irqbodyguard`, `h=h_m_1_s`, face `f_m_1_`, `h=h_m_4_c`[0], face `f_m_4_`[0] |
| `m_irq_mrocket` (52) | `gz=gz_armor`, `b=b_irqrocket`, `a=a_irqrocket`, `h=h_irqrocket`, `f=f_irqrocket` |
| `m_irq_msaddam` (53) | `b=b_irqsaddam`, `a=a_irqsaddam`, `h=h_saddam`, face `f_saddam_`, talk `f_saddam_n` |
| `m_irq_mman2` (54) | `b=b_irqman2a`[1], `a=a_irqman2a`[1], `b=b_irqman2b`, `a=a_irqman2b`, `h=h_m_1_s`, face `f_m_1_`, `h=h_m_2_s`, face `f_m_2_` |
| `m_x_romulan` (55) | `b=b_ugnsniper`, `a=a_ugnsniper`, `h=h_b_5_s`, face `f_b_5_`, `h=h_b_2_s`[1], face `f_b_2_`[1] |
| `m_afr_msoldier1a` (56), `m_afr_msoldier1b` (57) | `b=b_ugnsoldier1`, `a=a_ugnsoldier1`, `h=h_b_5_s`, face `f_b_5_`, `h=h_b_4_c`[1], face `f_b_4_`[1] |
| `m_afr_msoldier2` (58) | `b=b_ugnsoldier2`, `a=a_ugnsoldier2`, `h=h_b_2_s`[2], face `f_b_2_`[2], `h=h_b_3_s`, face `f_b_3_` |
| `m_afr_msoldier3` (59) | `gz=gz_armor`, `b=b_ugnsoldier3`, `a=a_ugnsoldier3`, `h=h_b_3_s`, face `f_b_3_` |
| `m_nyc_mswat` (60) | `gz=gz_armor`, `b=b_nycswat`, `a=a_nycswat2`, `h=h_w_3_s`, face `f_w_3_` |
| `m_nyc_mswatleader` (61) | `gz=gz_armor`, `b=b_nycswat`, `a=a_nycswat`, `h=h_b_4_c`, face `f_b_4_`, talk `f_b_4_n` |
| `m_cas_mraider1` (62) | `b=b_dcraider1`, `a=a_dcraider1`, `h=h_raider1`, `f=f_raider2` |
| `m_cas_mraider2a` (63), `m_cas_mraider2b` (64) | `gz=gz_armor`, `b=b_dcraider2`, `a=a_dcraider2`, `h=h_raider2`, `f=f_raider2` |
| `m_cas_mbrute` (65) | `b=b_dcbrute`, `a=a_dcbrute`, `h=h_raider4`, `f=f_raider4` |
| `m_cas_mrocket` (66) | `gz=gz_armor`, `b=b_casrocket`, `a=a_casrocket`, `h=h_raider3`, `f=f_raider3` |
| `m_sib_mtrooper2` (67) | `b=b_sibtrooper2`, `a=a_sibtrooper2`, `hd=hd_sibtroop`, `f=f_sibtroop2` |
| `m_sib_mcleansuit` (68) | `b=b_sibclean`, `a=a_sibclean`, `md=md_clean` |
| `m_kos_mgrunt1` (69) | `b=b_kosgrunt1`, `a=a_kosgrunt1`, `md=md_serbgrunt1` |
| `m_kos_mgrunt2` (70) | `b=b_kosgrunt2`, `a=a_kosgrunt2`, `md=md_serbgrunt2` |
| `m_kos_mgrunt3` (71) | `b=b_kosgrunt3`, `a=a_kosgrunt3`, `h=h_h_6_s`, face `f_h_6_`, `h=h_h_3_s`[0], face `f_h_3_`[0], `h=h_w_5_c`[2], face `f_w_5_`[2] |
| `m_kos_msniper1a` (72), `m_kos_msniper1b` (73) | `gz=gz_armor`, `b=b_kossnipe`, `a=a_kossnipe`, `h=h_h_6_s`, face `f_h_6_`, `h=h_w_10_s`[1], face `f_w_10_`[1], `h=h_w_2_c`[0], face `f_w_2_`[0] |
| `m_kos_mcomtroop` (74) | `b=b_koscomtroop`, `a=a_koscomtroop`, `h=h_w_19_s`[1], face `f_w_19_`[1], `h=h_w_4_s`, face `f_w_4_` |
| `m_kos_mbrute1a` (75), `m_kos_mbrute1b` (76) | `b=b_kosbrute`, `a=a_kosbrute`, `h=h_h_6_s`, face `f_h_6_`, `h=h_w_3_s`[2], face `f_w_3_`[2], `h=h_w_5_c`[1], face `f_w_5_`[1] |
| `m_kos_mmechanic` (77) | `b=b_kosmechanic`, `a=a_kosmechanic`, `h=h_w_4_s`, face `f_w_4_`, `h=hb_h_2_b`[2], face `f_hb_1_`[2] |
| `m_kos_mrebel` (78) | `b=b_kosrebel1`[2], `a=a_kosrebel1`[2], `b=b_kosrebel2`, `a=a_kosrebel2`, `h=h_w_19_s`, face `f_w_19_` |
| `m_kos_mklaleader` (79) | `b=b_kosrebel2`, `a=a_kosrebel2`, `h=h_w_20_c`, face `f_w_20_`, talk `f_w_20_n` |
| `m_nyc_mskinhead1` (80) | `b=b_nycskinhead1`, `a=a_nycskinhead1`, `b=b_nycskinhead1b`[1], `a=a_nycskinhead1b`[1], `h=h_w_1_s`[0], face `f_w_1_`[0], `h=h_w_13_s`[1], face `f_w_13_`[1], `h=h_w_3_s`, face `f_w_3_` |
| `m_nyc_mskinhead2a` (81) | `b=b_nycskinhead2`, `a=a_nycskinhead2`, `b=b_nycskinhead2b`[1], `a=a_nycskinhead2b`[1], `h=h_w_11_c`, face `f_w_11_`, `h=h_w_6_s`[1], face `f_w_6_`[1], `h=h_w_9_s`[2], face `f_w_9_`[2] |
| `m_nyc_mskinhead2b` (82) | `b=b_nycskinhead2`, `a=a_nycskinhead2`, `b=b_nycskinhead2b`, `a=a_nycskinhead2b`, `h=h_w_11_c`, face `f_w_11_`, `h=h_w_6_s`, face `f_w_6_`, `h=h_w_9_s`, face `f_w_9_` |
| `m_x_mskinboss` (83) | `b=b_nycskinboss`, `a=a_nycskinboss`, `h=h_w_8_s`, face `f_w_8_`, talk `f_w_8_m` |
| `m_nyc_mpolitician` (84) | `b=b_dcmalepol`, `a=a_dcmalepol`, `h=h_b_2_s`, face `f_b_2_` |
| `m_tok_mman2` (85) | `b=b_tokman1`, `a=a_tokman1`, `h=h_j_1_c`, face `f_j_1_`, `h=h_j_2_s`[0], face `f_j_2_`[0] |
| `m_tok_mhench1` (86) | `b=b_tokhench1`, `a=a_tokhench1`, `h=h_j_5_c`, face `f_j_5_`, `h=h_j_2_s`[0], face `f_j_2_`[0], `h=h_j_6_s`[1], face `f_j_6_`[1] |
| `m_tok_mhench2` (87) | `b=b_tokhench2`, `a=a_tokhench2`, `h=h_j_3_s`[2], face `f_j_3_`[2], `h=h_j_4_s`, face `f_j_4_`, `h=h_j_6_s`[1], face `f_j_6_`[1] |
| `m_tok_mkiller` (88) | `gz=gz_armor`, `b=b_tokkiller`, `a=a_tokkiller`, `h=h_j_5_c`, face `f_j_5_`, `h=h_j_2_s`[0], face `f_j_2_`[0] |
| `m_tok_mninja` (89) | `b=b_tokninja`, `a=a_tokninja`, `h=h_ninja`, `f=f_ninja` |
| `m_tok_mbrute` (90) | `b=b_tokbrute`, `a=a_tokbrute`, `h=h_j_3_s`[2], face `f_j_3_`[2], `h=h_j_4_s`, face `f_j_4_` |
| `m_tok_mman1` (91) | `b=b_tokmhostage`, `a=a_tokmhostage`, `h=h_j_5_c`, face `f_j_5_`, `h=h_j_6_s`[1], face `f_j_6_`[1] |
| `m_afr_mbrute` (92) | `b=b_ugbutcher`, `a=a_ugbutcher`, `h=h_b_7_s`[2], face `f_b_7_`[2], `h=h_b_2_s`, face `f_b_2_` |
| `m_afr_mrocket` (93) | `gz=gz_armor`, `b=b_ugnrocket`, `a=a_ugnrocket`, `h=h_afrrocket`, `f=f_afrrocket` |
| `m_sib_mtrooper1a` (94), `m_sib_mtrooper1b` (95) | `b=b_sibtrooper2`, `a=a_sibtrooper2`, `hd=hd_sibtroop`, `f=f_sibtroop` |
| `m_sib_mguard1` (96) | `b=b_sibguard1`, `a=a_sibguard1`, `h=h_w_16_s`, face `f_w_16_`, `h=h_w_10_s`[1], face `f_w_10_`[1] |
| `m_sib_mguard3` (97) | `b=b_sibguard1`, `a=a_sibguard1`, `h=h_w_16_s`, face `f_w_16_`, talk `f_w_16_n` |
| `m_sib_mguard4` (98) | `b=b_sibguard4`, `a=a_sibguard4`, `h=h_w_21_s`, face `f_w_21_` |
| `m_sib_mmechanic` (99) | `b=b_sibmechanic`, `a=a_sibmechanic`, `h=h_w_19_s`[2], face `f_w_19_`[2], `h=hb_h_2_b`, face `f_hb_1_` |
| `m_nyc_fskinchick` (100) | `b=b_nycskinchick`, `a=a_nycskinchick`, `h=h_w_15_c`, face `f_w_15_` |
| `m_nyc_fwoman` (101) | `b=b_nycwomanw`, `a=a_nycwomanw`, `h=h_w_18_m`, face `f_w_18_`, `b=b_nycwomanb`[1], `a=a_nycwomanb`[1], `h=h_b_8_c`[1], face `f_b_8_`[1] |
| `m_sib_fguard2` (102) | `gz=gz_armor`, `b=b_sibguard2`, `a=a_sibguard2`, `h=h_w_18_m`[1], face `f_w_18_`[1], `h=h_w_17_c`, face `f_w_17_` |
| `m_sib_fscientist2` (103) | `b=b_sibscientist2`, `a=a_sibscientist2`, `h=h_w_18_m`[1], face `f_w_18_`[1], `h=h_w_17_c`, face `f_w_17_` |
| `m_irq_fwoman1` (104) | `b=b_irqwoman1`, `a=a_irqwoman1`, `h=h_irqwoman1`, `f=f_irqwoman1` |
| `m_irq_fwoman2` (105) | `b=b_irqwoman2`, `a=a_irqwoman2`, `h=h_irqwoman2`, `f=f_irqwoman2` |
| `m_tok_fwoman1` (106) | `b=b_tokwoman1`, `a=a_tokwoman1`, `h=h_j_8_m`[1], face `f_j_8_`[1], `h=h_j_7_c`, face `f_j_7_` |
| `m_tok_fwoman2` (107) | `b=b_tokwoman2`, `a=a_tokwoman2`, `h=h_j_8_m`[1], face `f_j_8_`[1], `h=h_j_7_c`, face `f_j_7_` |
| `m_tok_fassassin` (108) | `b=b_tokassassin`, `a=a_tokassassin`, `h=h_j_8_m`[1], face `f_j_8_`[1], `h=h_j_7_c`, face `f_j_7_` |
| `m_cas_ffemale` (109) | `gz=gz_armor`, `b=b_dcfemale`, `a=a_dcfemale`, `h=h_raider5`, `f=f_raider5` |
| `m_x_ftaylor` (110) | `b=b_xtaylor`, `a=a_xtaylor`, `h=h_taylor`, face `f_taylor_`, talk `f_taylor_n` |

- Verify: data check against the original's Ghoul skin registration on any SP map: e.g. kos2 `m_kos_mbrute1a`
  registers `h_h_6_s`, `h_w_3_s`, `h_w_5_c` (with `mskins_variety_limit 0`); `m_x_mmerc` registers the `f_b_6_*`
  face skins before `b_mercdemo1`.

#### G4.8 SPEC-731 A §9.2 — pitch on a vertical leg (Correction)

- Old: §9.2 "Pitch target = 45 × (1 − q) … q = horizontal distance covered from P0 towards M / horizontal |M − P0|";
  test vector A5 ("nose-down pitch building up and easing off"). Observation (kos2 take-off): the 224-unit climb
  follows the §9.2 speed profile but the pitch stays 0 throughout; the script continues on the frame after arrival.
- New: the observation is right. In both halves of a leg the pitch step is done **only when both horizontal lengths
  used by q (or r) are greater than 0**: first half |M − P0| and |M − origin| measured in x and y only; second half
  |P1 − M| and |P1 − origin| in x and y. Otherwise the pitch is left unchanged in that frame (it is not driven towards
  0). On a purely vertical leg (take-off, or any leg with equal x and y at both ends) the pitch therefore keeps the
  value it had at the leg start (0 after spawn), while the speed profile and the move are as in §9.2. At arrival the
  arrive routine runs the next frame and, with roll and pitch already 0, is done in that frame (no level-out
  frames). Amend §9.2 and test vector A5 accordingly. OpenMerc (following the observation) **matches** if it skips the
  pitch step rather than targeting 0.
- Verify: done (kos2 `helitakeoff`). A leg that is vertical except for a 1-unit horizontal offset pitches normally.

#### G4.9 SPEC-731 A §9.9 / B §9.1 — frame of the aim step (Correction of the proposed reading)

- Old: §9.9 / SPEC-791 §11: "the same desired angles are computed in the frame of the part's bolt (bolt matrix of the
  parent including the entity transform, mode 5, through parents per flag)". Observation (trn1): gun, gunner head and
  pilot head stay at rest while the helicopter flies legs at up to 45° pitch and rolls ±50°; the pilot head yaws only
  during face turns and leg starts. Proposed reading: the frame is turned by the entity's yaw only.
- New: the frame is the **full** entity transform (origin and all three angles), followed by the parent's bolt matrix
  as SPEC-791 §11 says; it is not yaw-only. The observation follows from the target points instead:
  1. "500 units ahead" and "100 units ahead" are computed from the forward vector of the full entity angles (pitch
     included), so in the full frame they lie straight ahead: the desired yaw and pitch are 0 and the parts do not
     move while pitching or rolling.
  2. The pilot's look point during a leg is P1 + normalise(P1 − P0) × |size|, which is straight ahead once the
     helicopter faces the leg (§9.3), so the head turns only while the helicopter still turns towards it (leg starts,
     face turns).
  3. The pilot head (and the gunner head turned with the gun) has its pitch axis disabled (G4.10), so a look point
     below or above never pitches it.
  Keep §9.9 / SPEC-791 §11 as written. OpenMerc must use the full entity angles; with yaw-only frames a pitched
  helicopter would aim its gun 500 units ahead in the horizontal plane, i.e. the barrel would move in dives.
- Verify: trn1 dive (pitch 45°) with fire-while-flying off: the barrel stays at rest (full frame); a yaw-only frame
  would pitch it by 45° (clamped to its range).

#### G4.10 SPEC-731 A §9.9 — pilot head and gun limits; pitch axis (Correction)

- Old: §9.9 "pilot head yaw −0.3927 … 0.7854, pitch −1 … 1, rate 0.314159; gun base yaw −1 … 1 (pitch fixed), gun
  barrel pitch −0.3927 … 0.7854 (yaw fixed)". Observation (trn1): the pilot head yaws in 18° steps to about −79° when
  the look point is behind and never pitches, also on a 540-unit descent.
- New: the aim records used by the helicopter are (SPEC-791 §11 order: pitch range, yaw range, maximum step, flags
  pitch enabled / yaw enabled / through parents):
  | Part | Pitch range | Yaw range | Step | Pitch | Yaw | Through parents |
  |---|---|---|---|---|---|---|
  | pilot head (and the gunner head, turned the same way before the gun) | −0.7854 … 0.7854 (unused) | −1 … 1 | 0.314159 | off | on | on |
  | gun base (invisible null) | 0 … 0 (unused) | −1 … 1 | 0.314159 | off | on | on |
  | gun barrel | −0.7854 … 0.7854 | 0 … 0 (unused) | 0.314159 | on | off | on |
  So the pilot's pitch axis is disabled (never pitches), its yaw range is ±1 rad (±57.3°), and the barrel's pitch range
  is ±0.7854; the values −0.3927 … 0.7854 in §9.9 are wrong for all three. The 18° steps are the 0.314159 rad maximum
  step. The limits are compared with the desired angles measured in the parent-bolt frame from the part's x axis,
  while the step turns the part about its own z axis (SPEC-791 §11). For the crew heads, whose attach bolt `TO_BODY`
  is turned by 90°, these two axes are not the measured yaw axis of the head model, so the ±1 rad limit does not bound
  the visible head turn; the observed −79° is consistent with that, but the exact mapping of the measured angle to
  the visible turn for such parts is UNVERIFIED. OpenMerc **differs** if it pitches the pilot head or uses the old
  ranges.
- Verify (black box): trn1 face turn of 180°: the pilot head stops turning after the measured yaw reaches the ±1 rad
  limit; record the head's yaw per update and compare with the controller's limit measured from the part's x axis
  in the parent-bolt frame.

### G. G7 props and server effects (items 1–11)

#### G7.1 SPEC-770 §3 step 5 — combined right angles read the class box (Correction)

- Old: "When more than one axis rotates, the later axes read the extents of the original box except for the x
  minimum, which is read after the earlier rotation (original quirk; only matters for combined right angles,
  UNVERIFIED in shipped maps)." Observation (clean team, `g7rot`, 28 combinations, and 45 shipped props): every
  rotation reads the class box; the x minimum is not carried over.
- New: the **observation is right and the spec's quirk is wrong.** Re-checked for this amendment: all six extents of
  the box (as set by the class before the setup) are read once, before any rotation, and every rotation (pitch, then
  yaw, then roll) computes its two new ranges from those original six values with the formulas of step 5. Each
  rotation writes only the two axes it names; an axis written by an earlier rotation keeps the earlier result unless
  a later rotation writes it again. So the final range of each axis comes from the **last** rotation that names that
  axis, computed from the class box; an axis no rotation names keeps the class box range. Replace the last
  paragraph of step 5 by: "When more than one axis rotates, every rotation reads the extents of the box as it was
  before step 5; each rotation overwrites only its two axes, so a later rotation replaces an earlier rotation's result
  on a shared axis."
  - Example (nyc2 caged light, class box (−3, −3, −10)/(3, 3, 5), angles `90 270 0`): pitch 90 gives x-range
    [−10, 5] and z-range [−3, 3]; yaw 270 then gives x-range [−3, 3] and y-range [−3, 3] (from the class box);
    final box (−3, −3, −3)/(3, 3, 3).
  OpenMerc (after its fix) **matches**.
- Verify: the clean team's `g7rot` map (all 28 combinations identical) and the shipped props listed in the
  observation; data check: nyc2 caged light at `90 270 0` sends a packed box with half-widths 3/3 and z −3…3.

#### G7.2 SPEC-770 §5.8 / SPEC-791 open question 1 — the "can be pushed over" table does not fall (Resolution)

- Old: SPEC-770 §5.8 (fall animation) as amended by SPEC-AMEND-005 GK.1 ("in 1.07f the prop never moves; after the
  first unblocked attempt the use key no longer reacts"); SPEC-791 open question 1 ("verify in game before
  implementing SPEC-770 §5.8 as written"). Observation: `g7fall`, `misc_generic_table1` spawnflag 4, used three times
  from 62 units and once while touching it: no change of origin, angles or box.
- New: the observation **confirms** GK.1; no further change to the rule. Re-checked: the use arms the fall only after
  both clearance traces are free, removes the prop's use action, and the fall step then finds no prop to rotate and
  ends at once with angles, origin and box unchanged. The SPEC-791 open question can be closed with "confirmed in
  game (no fall)". The observation cannot tell the first use from the later ones (none moves anything); the removal of
  the use action after the first unblocked use stays as stated in GK.1. OpenMerc's default (no fall, use removed)
  **matches**; the specified animation behind `g7_fallover 1` is not original behaviour.
- Verify: done by the clean team (`g7fall`). Optional extra check that separates "use removed" from "use ignored":
  give the table a `target` that fires a `target_speaker`; it is not fired by any use (the table's use action is the
  fall, not the target firing), so nothing distinguishes them black box — no further test needed.

#### G7.3 SPEC-771 B.2 — mover sounds of a one-wave set (Resolution)

- Old: "Stock case: the `func_door` `new_tok_lever` in `tsr1` uses the one-entry set `ButtonElevator1`, so its moving
  and stop sounds would be `AirBy1.wav` (UNVERIFIED in game; listen when that lever-door moves)." Observation
  (`g7door`): start `Ambient/Buttons/HiTech/Elevator1.wav`, moving loop field and stop sound
  `Ambient/Gen/AirPlane/AirBy1.wav`, loop-sound data 248.
- New: confirmed; delete "(UNVERIFIED in game …)" and keep the rule: entry 0 is the set's listed wave, entries 1 and 2
  of a one-wave set read wave 0 of the sound-set file. The loop-sound data value 248 is the mover's usual full-volume,
  normal-attenuation loop data (SPEC-760 §B5.1). OpenMerc's ambient lookup **matches**; G18's movers must use it.
- Verify: done (`g7door`); stock: tsr1 `new_tok_lever` door plays `AirBy1.wav` while moving and when it stops.

#### G7.4 SPEC-721 §13 (lock table) and SPEC-771 A.1/A.2 — `environ_bloodrain` is never suppressed (Correction)

- Old: SPEC-721 §13 `lock_blood` row: "… `environ_bloodrain` and other blood effects of map entities are suppressed
  (SPEC-006)"; SPEC-771 A.1 table: "blood lock: SPEC-721". Observation: the original sends the `environ/bloodrain`
  slot for all four sud2 entities with `lock_blood` on.
- New: the **observation is right; the spec text is wrong.** Re-checked for this amendment:
  1. `environ_bloodrain` always puts the named effect `environ/bloodrain` (scale 0) in its first free effect slot at
     spawn. No lock is read. The same holds for `misc_uganda_gutted_cow` with spawnflag 8, which adds the same slot.
  2. The client does not suppress it either: in the engine the lock cvars are read only by the lock store, by the
     computation of `sv_violence`/`cl_violence`, by the server's connection check and by the server-browser
     filter; named effects and effect slots are
     played without any lock test. So under `lock_blood` the blood rain is still shown.
  3. SPEC-006 (cited as the source) only names the class; it states no suppression.
  Delete "`environ_bloodrain` and other blood effects of map entities are suppressed (SPEC-006)" from the SPEC-721 §13
  table and "blood lock: SPEC-721" from the SPEC-771 A.1 row (replace by "not affected by the parental locks"). The
  map-entity blood that *is* locked is only what SPEC-721/SPEC-720 list for characters and bodies. OpenMerc (always
  sends the slot) **matches**.
- Verify: done (sud2 with `lock_blood` on: four `environ/bloodrain` slots in the baselines). Client check: on the
  same PC the blood rain is visible on sud2 while character hits show only `gore/noblood*`.

#### G7.5 SPEC-771 A.2 `environ_fire` — registration order (Refinement)

- Old: "The three fire sounds and the four fire effects are always registered at spawn." Observation: the order is
  the size's effect, then (when the fire starts with sound) its loop sound, then `FireBig`, `FireMed`, `FireSmall`,
  then `environ/firesmall2`, `gbfire`, `environ/firelarge2`, `environ/firelarge3`.
- New: confirmed exactly. Spawn of `environ_fire` (after its box, link and defaults `health` 12, `count` 64) registers,
  in this order:
  1. the effect of its size N (N < 4 `environ/firesmall2`; 4 ≤ N < 20 `gbfire`; 20 ≤ N < 30 `environ/firelarge2`;
     N ≥ 30 `environ/firelarge3`) — also when spawnflag 1 (start off) is set; without spawnflag 1 that effect is put
     in an effect slot at this moment;
  2. only when the fire starts on (no spawnflag 1) and is not silent (no spawnflag 2): its loop sound by size
     (N < 11 `Ambient/Gen/FireFX/FireSmall.wav`; 11 ≤ N < 21 `FireMed.wav`; N ≥ 21 `FireBig.wav`), loop data 248;
  3. the sounds `Ambient/Gen/FireFX/FireBig.wav`, `FireMed.wav`, `FireSmall.wav`;
  4. the effects `environ/firesmall2`, `gbfire`, `environ/firelarge2`, `environ/firelarge3`.
  A name that is already registered keeps its index (ordinary configstring rule). Add this order to A.2. OpenMerc
  (after its fix) **matches**.
- Verify: done (`g7ord1..4`). Data check: a level whose first fire is `health 35` registers `environ/firelarge3`
  first among the four fire effects.

#### G7.6 SPEC-770 6B.5 security cameras — sound precache order (Refinement)

- Old: "precaches `Ambient/Models/Camera/Alarm.wav`, `Ambient/Models/Camera/MoveLP.wav`" (both camera classes).
  Observation: a panning camera (spawnflag 4) registers `MoveLP.wav` before `Alarm.wav`.
- New: confirmed. Order of a camera's spawn: (1) the camera setup runs first — with spawnflag 4 (panning) it registers
  `Ambient/Models/Camera/MoveLP.wav` as its loop sound (loop data 248) and starts the pan think; (2) the model (and for
  `misc_generic_security_camera` the bolt-on camera) is registered; (3) the class precaches `Alarm.wav` and then
  `MoveLP.wav`. So a non-panning first camera gives `Alarm`, `MoveLP`; a panning first camera gives `MoveLP`,
  `Alarm`. Same for `misc_generic_security_camera2`. OpenMerc (after its fix) **matches**.
- Verify: done (ger2 first camera spawnflags 13 → `MoveLP`, `Alarm`; dm/raven spawnflags 1 → `Alarm`, `MoveLP`).

#### G7.7 SPEC-770 §5.7 — space of the shootable-part bounds (Correction)

- Old: "its model bounds at the current pose, placed at its bolt position and grown by 2 units on every side, must
  contain p." OpenMerc first used the part's bounds in its own axes; the clean team found that the bounds must be
  turned by the bolt's (and the entity's) rotation.
- New: the observation is right; the precise rule is:
  1. R = the parent's bolt matrix for the part's bolt (instance method 50 at the current level time, mode 5, through
     parents), with its translation removed, followed by the prop's rotation from its angles (translation removed);
     that is, a point in the part's model space is first turned by the bolt rotation, then by the entity rotation.
  2. The part's box is instance method 51 of the **bolt-on** instance at the current level time with root R, all parts,
     not exact — an axis-aligned box in world-aligned axes around the bolt.
  3. P = the bolt's position in the world (the prop's origin and angles applied to the bolt position on the main
     model).
  4. The test box is (P + mins − 2, P + maxs + 2); the point p is inside when, on each axis, min ≤ p ≤ max
     (both ends inclusive).
  Only the first matching part (table order) is removed. The method-51 root is therefore "the bolt matrix without
  translation, then the entity rotation", not the bolt matrix alone. OpenMerc's `g7brk2` fix (bounds turned by the
  bolt rotation) **matches** for props with zero angles; it must also apply the entity rotation for turned props.
- Verify: `g7brk2` (bench top: own-axes box (−17.4, −1.9, −56.8)/(0, 2.4, 3.2) becomes (−1.9, −3.2, 0)/(2.4, 56.8,
  17.4) and contains the damage point; the top is shot off). Repeat with the bench at `angle 90`: the original still
  shoots the top off at the same relative point (the box turns with the entity).

#### G7.8 SPEC-770 6A.2 bench — "completely free" means fraction 1 (Resolution)

- Old: "if a trace (the entity's clip mask) from the origin 100 units along f is completely free …". Observation: a
  bench whose origin lies inside another prop's box still falls.
- New: confirmed. The bench tests only the trace fraction: the fall happens when the fraction is 1 (not < 1). The
  start-solid and all-solid flags of the trace are not looked at. A trace that starts inside another box and leaves
  it reports fraction 1 (SPEC-313 trace rules), so it counts as free. Replace "is completely free" by "has fraction 1
  (start-solid is ignored)" for both bench directions. OpenMerc (after its fix) **matches**.
- Verify: done (`g7shot`, bench inside a `misc_generic_table1` falls when shot from the front).

#### G7.9 SPEC-770 7A `misc_iraq_tree_palm` — not linked again (Refinement)

- Old: "forces +2; made non-solid after the setup". Observation: the original keeps sending the packed box of the
  setup's link.
- New: confirmed. The standard setup links the tree with solid 2 (box), which sets the packed box sent to clients;
  the class then sets solid 0 **without linking again**. Consequences: clients keep receiving the setup's packed box
  (irq1a `angle 270`: (23, 30, 67, 67)); the server's collision and area queries ignore the tree from the next
  query on (solid 0). The tree is still marked damageable by the setup, but nothing can hit a non-solid entity.
  Add "the entity is not linked again" to the row. OpenMerc (after its fix) **matches**.
- Verify: done (irq1a baselines).

#### G7.10 SPEC-770 6A.5 big fan — use while running down (Correction)

- Old: "Use toggles: when off it accelerates by `volume` every 0.1 s until it reaches `speed`; when on (or
  accelerating) it first writes the full-speed record once, then decelerates by `volume` every 0.1 s to 0."
  Observation: the deceleration starts from the current speed, and a use while decelerating behaves like a use while
  on.
- New: confirmed; exact state machine. The fan keeps a state (off, on, accelerating, decelerating) and a current speed
  c (0 at spawn; `speed` when it starts on).
  - Use when **off**: state := accelerating; think in 0.1 s; no record is written at the use.
  - Use in **any other state** (on, accelerating, decelerating): state := decelerating; think in 0.1 s; the spin record
    is written once with the **full** `speed` value (the client sees full speed for that frame); c is **not**
    changed. A decelerating fan therefore cannot be restarted until it has stopped.
  - Think while accelerating: if `speed` − c < `volume`: c := `speed`, state := on, no further think; else
    c := c + `volume` and think again in 0.1 s.
  - Think while decelerating: if c < `volume`: c := 0, state := off, no further think; else c := c − `volume` and
    think again in 0.1 s.
  - Every think writes the spin record with the new c.
  OpenMerc (after its fix) **matches**.
- Verify: dm/raven test map, `speed 3`, `volume` default 0.05: use at 0 s (spins up 6 s), use at 4 s (c = 2.0 → one
  record 3.0, then 1.95, 1.90, …), use at 6 s while decelerating (one record 3.0, deceleration continues from 1.0),
  stops at about 8 s.

#### G7.11 SPEC-760 §B4.7 / §B4.11 — why the nycctb2 radar dish does not fall (Resolution)

- Old: SPEC-760 §B4.7: "think first; then, unless attached: recheck ground when moving or when the ground entity is
  not the world; … gravity (when not on ground and gravity ≠ 0 …)"; §B4.11 (ground check: "started all-solid: take
  the hit entity as ground"). Observation: dm/nycctb2's `misc_generic_radar_dish` (origin z 699, box z −15…70) stays
  at 699 in the original; OpenMerc's movetype-10 physics drops it 24 units.
- New: the dish is held by the **all-solid ground rule**, not by anything dish-specific:
  1. The dish's box top is 699 + 70 = 769; the world brush above it (a slab from z 768 to 776 covering the dish's
     x/y extent) overlaps the box by 1 unit. Data check: the dish's box at its spawn origin intersects that solid
     brush.
  2. On its first physics frame the dish has no ground entity, so the ground is rechecked (state in §B4.7: the
     recheck also runs when there is **no** ground entity, besides "moving" and "ground is not the world"). The
     §B4.11 check traces the box 0.25 units down; the trace starts and ends inside the slab (all solid), so the hit
     entity — the world — becomes the ground entity.
  3. Gravity is applied only when the entity has no ground entity (and gravity ≠ 0), so the dish gets no velocity;
     with zero velocity and the world as ground the ground is never rechecked again. It hangs until something gives it
     velocity (e.g. the player push of §5.1 or knockback).
  The bolt-on dish and its rotation record play no part. A `misc_generic_radio` 26 units above a floor is not in solid,
  finds no ground and falls. Amend §B4.7 to "recheck ground (§B4.11, which movetypes 10 and 11 use too) when moving,
  when there is no ground entity, or when the ground entity is not the world". OpenMerc **differs** (G18 physics):
  its movetype-10 ground check must take the hit entity as ground for an all-solid start.
- Verify: data check above. Black box: move the dish 2 units down in a copy of nycctb2 (origin z 697, box no longer
  in the slab): the original then drops it 22 units with a landing event. Unchanged map: the dish never moves on its own.

### H. G9 game modes and Ghoul glue (items 12–16)

#### G9.12 SPEC-790 Behaviour 1 step 2 — only Conquer registers `trigger_conquer` zones (Correction)

- Old: "each `trigger_conquer` (an ordinary brush trigger in every mode) registers with Conquer, which moves the
  trigger's origin to the centre of its box". Observation: on dm/gerctb1 the brush keeps origin (0, 0, 0) in
  deathmatch 1 and is at the box centre (4, −256, 252) in deathmatch 7.
- New: the **observation is right; the spec is wrong.** The trigger's spawn always ends with a call to the current
  mode's zone registration, but only the Conquer mode (`deathmatch` 7) implements it: it adds the zone to its list and
  sets the origin to (absmin + absmax) / 2. In every other mode (single player included) the registration does
  nothing: the trigger keeps its brush-model origin (normally (0, 0, 0)), is not listed anywhere, and its touch
  (forwarded to the mode's zone-touch rule after the touch delay) does nothing either. Replace the sentence by "each
  `trigger_conquer` is an ordinary brush trigger; in Conquer it registers as a zone and its origin is moved to the
  centre of its box (the position of its loop sound, Behaviour 10); in other modes it does nothing". OpenMerc (since
  2026-10-25) **matches**.
- Verify: done (gerctb1 baselines in deathmatch 1 and 7).

#### G9.13 SPEC-791 test vector 6 — `ghoul_model` on a dedicated server (Resolution, needs a black-box run)

- Old: test vector 6 ("the officer plays looking, then alternates stretch/stand seven times with
  `my sequence has ended! hooha!` before each change …"). Observation: on the original dedicated server
  (`developer 1`, `gimme ghoul_model`, 30 s, `logfile 2`) neither print appears; OpenMerc's module behaves the same in
  the original engine.
- New: the game-module side of SPEC-791 §14 is confirmed as written; nothing in the module depends on a client:
  1. At spawn the record `enemy/ecto` / `officer` is found or created with all skins (so its registration is finished
     and the `EOS` note exists), the sequence `std_ilooking_n_a_a` is played once and hold, and — when the model has an
     `EOS` note — the callback is registered on it (instance method 23, which also switches note processing on), with
     the shared flag and count cleared.
  2. The game advances every non-player entity's instance to the level time each frame (instance method 45; SPEC-760
     B9.3, `ghl_no_server_update` "0" by default), so the server's own instance should deliver `EOS` once the 1-cycle
     duration has passed; the callback sets the flag and the next think prints.
  So the vector does not need a client, and a listen server cannot help (callbacks run only on the server's
  instances). Because the original module and OpenMerc's module both stay silent in the same original engine, the
  cause is on the engine side (the server Ghoul system does not deliver the `EOS` for this instance — e.g. the
  instance is not switched on, or the sequence has no usable length on the dedicated server); the module cannot
  decide which. Keep test vector 6 but mark it "UNVERIFIED: the original dedicated server prints nothing (engine
  behaviour)". OpenMerc's module **matches** the original module.
- Verify (black box, decides the engine side): repeat the run as a listen server (`+map dm/jpndm2`, `developer 1`,
  `cheats 1`, `gimme ghoul_model`, 30 s). If `my sequence has ended! hooha!` appears there, the dedicated engine's
  server Ghoul skips the note events; if it does not appear either, the `EOS` of a once-and-hold sequence is not
  delivered on the server at all, and the vector should be dropped.

#### G9.14 SPEC-790 Behaviour 6 Assassin scoring — "A's pending target := V's target" (Correction)

- Old: "In the first and the last case V's pending target := A (the victim will hunt his killer) and A's pending
  target := V's target (A takes over V's contract; possibly none)." Observation: two players, each the other's target,
  Alpha kills Bravo: Alpha +4, then no status layout and no icon change (his target stays Bravo).
- New: the observation is right and the precise rule is a **new choice when V's target is A**. In the first case
  (A's target is V, +4) and the last case (bystander, −1):
  1. V's pending target := A.
  2. If V's target is A (only possible in the first case): A's pending target := a newly chosen target, by the usual
     *choosing a target* rule for A (fewer than 2 players in the game → none; otherwise from client slot
     (r mod `maxclients`) + 1 cyclically, the first slot in use, with a client, not a spectator and not A). V is
     still in use when this runs, so V itself can be chosen.
  3. Otherwise A's pending target := V's target (possibly none).
  With two players the only candidate is V, so A's pending target equals his current target and nothing changes at
  the end of the frame (no layout, no icon update) — the observed result. With three or more players A may get a new
  target (possibly V again). Replace the sentence by steps 1–3. OpenMerc (pending target unchanged when V's target is
  A) **differs** with three or more players: it must make the new choice.
- Verify: done for two players (`orig_assassin_kill`). Three players A, B, C with A→B, B→A, C→A: A kills B: A +4,
  A's next target is chosen among B and C (random), shown in A's status layout when it differs from B.

#### G9.15 SPEC-790 Behaviour 13 — no team block in the CTF game log (Correction)

- Old: "CTF: per team `L` + `Team <name>`, `L` + `Score <n>`, `L` + `Flag Captures <n>` and an empty line, then per
  player Client/Score/Ping/Time." Observation: `orig_ctf_fraglimit_team` (`dmflags` 32) writes no team block, only
  the client block; `ctf_log_tp` (`dmflags` 160) checks whether team play decides.
- New: the **observation is right**, and team play does not matter: in 1.07f the CTF log **never** writes the team
  lines. The CTF scoreboard builds one entry per team (team number, summed score, flag captures), but the field it
  tests before writing `Team`/`Score`/`Flag Captures` (the team name) is never filled and is always empty, so the
  three lines are skipped for both teams in every CTF game. What is written after the header: for team 1 and then
  team 2, every player of that team in descending score order (a later player with an equal score comes after the
  earlier one) as `L` + `Client <name>`, `L` + `Score <n>`, `L` + `Ping <ms>`, `L` + `Time <minutes>` (no empty line
  between teams); then the spectators as in the default scoreboard. Change the CTF sentence accordingly. OpenMerc
  (client block only) **matches**.
- Verify: `ctf_log_tp` (`dmflags` 160): no `Team` line in the log either.

#### G9.16 SPEC-791 Behaviour 1/2 and SPEC-722 — skins of body bolt-on records (Correction)

- Old: SPEC-791 Behaviour 1: "Callers pass all-skins = 1 whenever they do not name a skin." Observation: hand weapons
  `Enemy/Bolt/w_*` (no skin named) are registered with no skins; a bolt-on with a skin registers it once, on the
  material named like the model (`acc_headset` → `ACC_HEADSET`; `w_briefcase` → `W_BRIEFCASE=RUBBISH`), or on the
  material that takes it when no material has the model's name (`acc_ecto_glasses` → `ACC_GLASSES`).
- New: the observation is right; the SPEC-791 sentence does not hold for the character body's bolt-on requests
  (SPEC-722 weapons in hand and accessories, all four bolt-on requests of the body). They always find or create the
  record with **all-skins = 0** and the given skin name as part of the record key (empty when none):
  1. no skin named → the record is finished with no skins at all (object method 1 with 0);
  2. skin named → before finishing, while the record is not yet prepared, the skin is registered with the "register
     skin by name on any material" rule of Behaviour 2: materials in id order 1, 2, … (ids are alphabetical by
     material name, SPEC-012), the skin registered on each and the pair remembered, stopping at the first material that
     provides the skin. In stock data that is the material named like the model when it has the skin, otherwise the
     first material that has it. Because the skin is part of the record key, each (model, skin) pair is its own record
     and holds exactly that one skin;
  3. a record that is already prepared registers nothing new (the earlier request's skins stay).
  Replace the "callers pass all-skins = 1" sentence by "props, items and other glue users pass all-skins = 1 when they
  name no skin; the character body's bolt-on requests always pass 0 (steps 1–3)". OpenMerc (since G9's registry hook)
  **matches**.
- Verify: done (arm1, jpn1 registration suites). Data check (not done here): list the materials of `acc_headset`;
  `ACC_HEADSET` must come first in id order or be the only one that provides the skin.

### I. I2d GameSpy lobby chat (items 1–8)

#### I2d.1 SPEC-204 §5.1 — ERROR before the welcome (Resolution and Refinement)

- Old: "An `ERROR` line before the welcome ends the registration as failed. The original still prints the two
  'Connected' lines and clears the room list, then sends nothing more (UNVERIFIED)." Black-box: confirmed, and no
  LIST and no PONG was seen.
- New: confirmed. The UNVERIFIED mark can go. Exact rules:
  1. An `ERROR` received while registering marks the session "not registering" and calls the connect result with
     *failure*. The lobby ignores the result flag (as for the welcome). So it prints the console line and the public
     log line `^4Connected to <server>:<port>!` and clears the room list.
  2. Its automatic channel-list request is **refused**: every request that sends a command and waits for an answer
     (LIST, JOIN, PART, NAMES, TOPIC, WHOIS/ban, KICK, MODE, PRIVMSG/NOTICE) is sent only while the session is
     *registered*, i.e. after a welcome (001). After an early ERROR the session stays unregistered, so none of the
     `gsc_*` commands sends anything until the next `gsc_connect`.
  3. The session and its socket are **kept**. Incoming lines are still read and handled, and a server `PING` is still
     answered with `PONG` (the PING handler has no registration test). The clean team's "no PONG" carries no
     information: the fake server sends nothing after its ERROR line.
  4. When the server then closes the socket: public log `^4Disconnected Disconnected`, user list cleared.
  5. `gsc_connect` and `gsc_disconnect` first end an existing session. This always writes the public log line
     `^4Disconnected ` (the reason is empty) and clears the user list. `QUIT :Later!` is sent **only** when the
     session was registered. The capture shows the same: each refused or ERROR test is followed by a plain
     "Disconnected" line when the next `gsc_connect` runs.
  OpenMerc **matches** points 1, 2 and 4 by observation. Check point 3 (keep answering PING) and point 5.
- Verify: fake server that answers NICK/USER with `ERROR :x`, keeps the socket open and sends `PING :abc` 2 s later:
  the original sends `PONG abc` and no LIST. Then `gsc_connect` again: public log `Disconnected ` and no `QUIT` line
  on the wire.

#### I2d.2 SPEC-204 §5.1 — CTCP requests (Correction)

- Old: "A CTCP `PING` inside a PRIVMSG is answered `NOTICE <sender> :^1PING <arg>^1`. Other CTCP requests except
  `ACTION` are ignored." Black-box: a CTCP `VERSION` is shown as plain text `VERSION`.
- New: the spec was wrong. The observation is right but incomplete. A PRIVMSG text is a CTCP request only when **all**
  of these hold:
  - it starts with byte 1;
  - its second character is a letter A–Z or a–z;
  - it ends with byte 1;
  - it contains a space after the command word.

  Then the trailing byte 1 is removed, the command is the word between the leading byte 1 and the first space, and
  the argument is the rest after that space. For a recognised request:
  - `PING` (exact, case-sensitive): answered `NOTICE <sender> :^1PING <argument>^1` and not shown;
  - `ACTION` (exact, case-sensitive): shown as an action with the argument as text;
  - any other command: ignored (not shown, no answer).

  A text that fails any of the tests is an **ordinary message**, shown with control characters removed. Hence:
  - `\x01VERSION\x01` (no argument): the space test fails, so it is shown as the plain message `VERSION` (the
    observed case);
  - `\x01VERSION x\x01`: ignored;
  - `\x01PING\x01` without an argument: shown as the text `PING`, not answered;
  - `\x01ACTION\x01` alone: shown as the text `ACTION`;
  - `\x01ping x\x01` (lower case): a recognised request that is neither PING nor ACTION, so ignored.

  The same rules hold for channel messages and private messages. A recognised private CTCP whose target is the own
  nick (case-insensitive) goes to the private-message path; otherwise it goes to the channel named as the target,
  only if the client is in it. OpenMerc **differs** if it treats every `\x01…\x01` text as CTCP.
- Verify: from a peer send `PRIVMSG <nick> :\x01VERSION\x01` → shown as `VERSION`; `:\x01VERSION 1\x01` → nothing
  shown; `:\x01PING\x01` → shown as `PING`, no NOTICE sent; `:\x01PING 7\x01` → NOTICE with `PING 7`.

#### I2d.3 SPEC-204 §5.4 `gsc_ban` — the ban is never sent (Correction)

- Old: "`gsc_ban <channel> <user>` sends `WHOIS <user>`; on its 311 reply `MODE #<channel> +b *!*@<host from the
  reply>`." Black-box: only the WHOIS was ever seen.
- New: the spec was wrong. In 1.07f the ban is **never** sent, under any condition:
  - `gsc_ban` (when the session is registered) sends `WHOIS <user>` and starts a pending ban request for that user.
  - The handler of the 311 reply looks for a pending ban request **that has no user name**. Every request created by
    `gsc_ban` has a user name, so it never matches.
  - The request expires silently after the 30-second limit (no message, no callback).
  - Other WHOIS replies (319, 312, 317, 318) do not use it either.

  Whatever the server answers, the only line on the wire is the WHOIS. The intended `MODE #<channel> +b *!*@<host>`
  is dead code. OpenMerc **matches** (WHOIS only). Strike the 311 clause from the table.
- Verify: as in the clean team's run (`origban_fake.txt`): a complete WHOIS answer with a host, within and after 30 s,
  never produces a MODE line.

#### I2d.4 SPEC-204 §5.2 — every LIST is a request of its own (Refinement)

- Old: "`gsc_getchannels` clears the room list and sends `LIST` … At the end (323) the console shows
  `<n> channels found.`" Black-box: two LISTs answered give two count lines and every room twice; unanswered LISTs
  report `0 channels found.` after about 30 s each.
- New: confirmed. Add these rules:
  1. Each LIST (automatic after the welcome, or `gsc_getchannels`) creates its own pending list request, appended to
     a queue in sending order. `gsc_getchannels` clears the room widget **before** it sends. It clears even when no
     session exists. Then it sends only when registered.
  2. A reply (321 start, 322 entry, 323 end) is given to the **oldest** pending list request.
     - A 322 counts only when it has exactly four parameters (own nick, channel, users, topic). It adds a room at once
       (channels not starting with `*`; SPEC-803 §3 keeps duplicates, so the same room can appear twice). The
       developer line `Channel <i>: <name> (<topic>) with <users> users` numbers the entries of that request from 0.
     - The 323 completes the oldest request: `<n> channels found.` with n = the number of 322 entries that request
       received, including those starting with `*`. The request is then removed, so the next replies go to the next
       request.
  3. **Time limit**: a request expires 30 s after it was sent **or after the last reply (321 or 322) given to it**,
     whichever is later. Every reply that reaches a request restarts its 30 s.
     - On expiry it reports `0 channels found.`, **even if 322 entries had arrived**: the count is not carried into
       the expiry report. Rooms already added stay in the widget.
     - Several unanswered LISTs expire separately, each 30 s after its own sending.
  4. These request and expiry rules (oldest-first matching, the 30 s limit restarted by each matching reply) apply
     to every pending request kind: list, join, names, topic and ban.
  OpenMerc's queue **matches** if it restarts the limit on every matching reply.
- Verify: LIST answered with 322 entries at 0 s and 25 s and no 323: `0 channels found.` at about 55 s, not 30 s.

#### I2d.5 SPEC-204 §5.4 — what each request prints when it fails or times out (Correction)

- Old: "Every request that waits for a server answer … gives up after 30 seconds and then reports failure";
  `gsc_getusers` failure: `Failed to list users for #<channel>`. Black-box: NAMES prints the line without the channel;
  TOPIC and WHOIS print nothing.
- New: the observation is right. Per request kind:
  - **LIST**: `0 channels found.` (I2d.4).
  - **NAMES** (`gsc_getusers`): the console line is `Failed to list users for ` followed by an undefined string. The
    original passes no argument for its `%s`; observed empty. Print it without the channel name. Only the end-of-names
    reply (366 with exactly three parameters) or the time limit ends a pending NAMES. **No error numeric** (403, 401,
    …) ends it early; a 403 only ends a pending join. Servers normally send 366 after 403, which then completes the
    request with the users received (often 0 → `0 users found in #<channel>`).
  - **TOPIC** (`gsc_gettopic`): the failure result is delivered only to a channel the client is in (I2d.6). A TOPIC
    request is only ever sent for a channel the client is *not* in, so a timeout prints nothing. If it were delivered,
    it would print only the developer line `Failed to retrieve topic for <channel>`.
  - **ban / WHOIS**: nothing (I2d.3).
  - **JOIN**: as specified (`^2Failed to enter/create channel #<channel>`).
- Verify: `gsc_getusers slownames` (server never answers): after 30 s `Failed to list users for ` and nothing else.
  `gsc_getusers nosuch` with a 403 and no 366: the same line after 30 s, not at once.

#### I2d.6 SPEC-204 §5.4/§5.5 — results and events for channels the client is not in (Correction)

- Old: "`gsc_gettopic <channel>` … `menu_topic := #<channel>: <topic>`"; §5.5 "Topic received or changed →
  `menu_topic`". Black-box: a topic reply or TOPIC change for a channel other than the current one does not change
  `menu_topic`.
- New: the observation is right. The general rule: **every result or event that names a channel is delivered only
  while the client is in that channel.**
  - If the client has joined but the join is not complete yet, it waits in the queue until the join completes.
  - If the client is not in the channel, it is discarded.
  - Results without a channel (room list, user list) are always delivered.

  Consequences for topics:
  - `gsc_gettopic` for a channel the client **is in** uses the topic already known from that channel, possibly empty.
    It sends nothing and sets `menu_topic := #<channel>: <topic>` at once.
  - `gsc_gettopic` for a channel the client is **not in** sends `TOPIC #<channel>`. The 332/331 answer completes the
    request, but the result is discarded, so `menu_topic` does not change. The command therefore never changes the
    topic of another channel.
  - A `TOPIC` change or a 332 reply without a pending request changes `menu_topic` only for a channel the client is
    in.
  - With one channel at a time this means "the current channel". `gsc_part` takes the client out of the channel at
    once, when the PART is sent. The lobby's remembered current channel is not cleared, though (SPEC-204 §5.4), so
    after `gsc_part` nothing more is delivered for that channel.

  The same rule applies to channel messages, joins/parts of other users, kicks, mode changes and user-list updates of
  a channel. OpenMerc **matches** for topics. Apply the rule to the other channel events too.
- Verify: while in `#opchan` (topic A), `gsc_gettopic sof` with the server answering `332 … #sof :B`: `menu_topic`
  stays `#opchan: A`. A `PRIVMSG #sof :hi` from a peer while the client is not in `#sof`: nothing in the public log.

#### I2d.7 SPEC-803 §3 — lifetime of the chat logs, rooms and users (Refinement)

- Old: "The engine keeps, while menus exist: a public chat log and a private chat log …, a sorted room list and a
  sorted user list". Black-box: lines that arrive while the menus are closed are lost; the logs are empty after
  reopening.
- New: confirmed. State it explicitly:
  - All lobby data belongs to the menu system: both logs (100 lines each), the room list, the user list and the
    selected user. The menu system is created when a menu is opened while none is open. It is destroyed when the last
    menu closes (`menuoff`, `killmenu` or `popmenu` of the last screen, or a failed `default` menu open), and all lobby
    data goes with it.
  - While no menu is open, the five lobby operations are **ignored**: add a log line, clear rooms, add a room, clear
    users, add a user.
  - The chat session itself (GameSpy) is not affected: it stays connected and keeps its channel. But the room list,
    user list and topic shown after reopening are empty until new events arrive.
  - `menu_topic` is a cvar and survives.

  OpenMerc **matches** (`MC_Add` drops lines, `MC_ClearLogs` on close). Clear the room and user lists on close too,
  and drop room and user additions while closed.
- Verify: join a channel, `menuoff`, let a peer join and talk, `menu chat`: empty log, empty user list, empty room
  list; `menu_topic` unchanged.

#### I2d.8 SPEC-803 §3 — chat drawing with a long log (Correction)

- Old: "When the total number of lines changes, the page's content height becomes lines·L and the page scrolls to the
  bottom." OpenMerc reads it as: the page's content grows to lines·L while the widget keeps its own frame height, and
  drawing starts at the widget's screen y + its frame height − 4.
- New: the **widget's own height** follows lines·L. Drawing uses the widget's screen position, which includes the page
  scroll. The rule:
  1. Let H0 be the widget's initial height (from its frame, set when the page is laid out), L the font height, and N
     the total number of wrapped lines of all messages in the log. N is counted every draw, over all messages, also
     those above the drawn area.
  2. When N differs from the count of the previous draw and N·L ≥ H0:
     - the widget's height becomes N·L;
     - the page's content height is recomputed from its widgets;
     - when the content is taller than the visible page area (page height − 2 × frame padding) and the page has a
       vertical bar, the page scrolls to the bottom (to content − visible area).
     When N·L < H0 nothing changes: the widget keeps H0.
  3. Drawing starts at the widget's on-screen y (page y + widget y − page scroll + padding) + its **current** height
     − 4, and goes up message by message as in SPEC-803 §3.

  So when the log is longer than the widget, the widget is as tall as its text and the scrolled page shows its bottom.
  The newest line sits 4 pixels above the visible bottom, with the scroll bar at the bottom. That is the original's
  picture. OpenMerc **differs**: grow the widget to N·L instead of only the page content.
- Verify: fill the public log with 40 one-line messages in a chat widget 10 lines high: the newest line is at the
  bottom edge of the visible widget, and the scroll thumb is at the bottom. Scroll up 5 lines: older lines appear and
  the newest leaves the view at the bottom.

### J. I4c renderer: terrain, liquids, flowing, fog mode 1 (items 1–4)

#### I4c.1 SPEC-410 "Liquids" — the environment-map second pass (Correction)

- Old: "If the base image's second-pass texture is an environment map (type 8), the polygons are drawn again with blend
  (destination colour, source colour), sphere-mapped texture coordinates (OpenGL sphere-map generation), the face plane
  normal, colour 1 and the same wave height." Black-box: the environment map never appears; the base texture appears at
  sphere-map coordinates, with constant colour, and with an opaque base texture it hides the warped first pass. OpenMerc
  draws the base texture with blend (source alpha, one minus source alpha). That is exact for opaque probes, but the
  stock `iraq/water` stays lighter.
- New: the spec was wrong on two points and the observation is right. The exact second pass:
  1. **Condition**: the first pass's image (the current animation frame of the texinfo chain) has a second-pass image of
     type 8, and `gl_drawflat` = 0. The environment map only switches the pass on.
  2. **Texture**: nothing is bound. The pass draws with the texture still bound from the first pass, i.e. the **base**
     image, on texture unit 0 (multitexture off). The environment map is never drawn on a liquid.
  3. **State**:
     - texture environment MODULATE;
     - colour (1, 1, 1, 1);
     - sphere-map texture generation on S and T, with the normal set once per surface to the surface plane's
       stored normal (not flipped for faces on the back side of their plane);
     - no per-vertex colour, no lighting, no fog or `gl_modulate` term;
     - vertices are the same subdivided polygon vertices, with the same wave height when the texinfo has 0x2000;
     - depth state unchanged (the pass lies exactly on the first).
  4. **Blending**: the pass sets the blend factors to (destination colour, source colour) but does **not** switch
     blending on or off. It inherits the caller's blend enable:
     - **liquids drawn in the opaque pass** (no flag 0x800: world texture chains, opaque inline models): blending is
       off. The pass **replaces** the first pass with the sphere-mapped base texture (texture RGB × 1). The texture's
       alpha has no effect. This is the opaque-probe case. It also covers, e.g., sud2 `uganda/2_blood1`, whose
       texinfo flags have 0x8 but not 0x800.
     - **liquids drawn in the sorted alpha pass** (flag 0x800, blending on): result = 2 × texture × framebuffer,
       clamped to 1 per channel. The framebuffer already holds the first pass blended (source alpha, one minus source
       alpha) over the background. The texture's alpha is not used. The stock irq2a `iraq/water` has texinfo flags
       0x808/0x809, so it is in this case. Hence it is darker than OpenMerc's alpha blend wherever the base texture is
       below 0.5.
  5. Afterwards the blend factors are reset to (source alpha, one minus source alpha) and sphere-map generation is
     switched off.
  6. The **first pass is not changed** for such liquids. It is the usual warped, lit pass.
  OpenMerc **differs** for liquids with flag 0x800. Use (destination colour, source colour) with blending on there, and
  no blending in the opaque pass.
- Verify:
  - irq2a `iraq/water` (0x800) with an opaque uniform mid-grey (128) probe: the final pixels equal the first-pass
    result (2 × 0.5 × framebuffer).
  - The same with a black probe: the water turns black. With a white probe: the first-pass result doubles in
    brightness (clamped).
  - sud2 `uganda/2_blood1` with an opaque probe: the sphere-mapped probe replaces the warped pass completely.

#### I4c.2 SPEC-410 "Far fog wall" — region of the wall when the view leaf has no region (Resolution and Refinement)

- Old: "otherwise when any overlay vertex of a region with offset 36 ≠ 0 was processed (the region whose vertex had the
  smallest D gives the colour)". Black-box: with the eye in a solid leaf of sud3 (1640 272 −552), the original's wall
  has region 5's colour (30, 20, 45). OpenMerc (smallest D) picks region 4, 6 or 15's (128, 55, 16).
- New: the rule is "smallest D", but over a precisely defined set and order. Two facts settle the solid-leaf case:
  - the leaf containing (1640, 272, −552) in sud3 is solid with region −1;
  - by raw distance, region 6 (158 units), region 15 and region 4 are nearest and region 5 is far (1187 units).
  So the original's choice comes from the plane reduction and the processing order, not from raw distance. Exact
  rules (first overlay pass of the frame only):
  1. **View region**: the region of the leaf containing the eye, whatever its contents. A solid leaf has a region when
     its record says so; in sud3 it has none. If that region has offset 36 ≠ 0, it gives the colour and nothing below
     changes it.
  2. **Set of vertices**: the vertices of the region polygons of the queued faces of this pass, in queue order, faces
     one after another, vertices in loop order.
     - The queue holds at most 1024 faces; faces beyond the 1024th are not drawn and not considered. sud3 has 2699
       region faces, so with all leaves visible the cap is reached.
     - Queue order is the order of the world drawing. First come the fog-boundary faces (0x10000), queued during the
       BSP walk (front to back). Then the other region faces, queued as the texture chains are drawn (the chain order
       of SPEC-410 "World part of a frame"). Faces of inline models are queued in their own later passes.
     - In `gl_fogmode` 3 no vertex is considered.
  3. **D per vertex**: D0 = |p − eye|². Then, because the view leaf's region is not this region, the boundary-plane
     reduction of "Fog thickness" step 2, then the shimmer term.
     - A vertex lying exactly on an **unmarked** boundary plane of its region (b = 0) and the eye off it give
       D = D0·0/(a²) = **0**. Region polygons of boundary faces lie on boundary planes, so D = 0 is common when the eye
       is outside every region.
     - The marks are order-dependent. A plane is marked when a boundary face on that plane is queued during the walk
       (also when the queue is full). It is also marked when the first vertex on the eye's side of it is processed.
  4. **Choice**: a running minimum starts at 1.0e9 with no region. A vertex of a region with offset 36 ≠ 0 replaces it
     only when its D is **strictly smaller**. So among equal minima (typically several D = 0 vertices) the **first one
     processed** wins.
  5. The wall colour is the chosen region's colour (or `gl_fogcolor` when set).

  So in the sud3 case the wall belongs to the far-flag region whose vertex first reaches the minimum (usually D = 0 on an
  unmarked boundary plane) in queue order. Region 5 has 76 boundary faces, which are walked and queued early. OpenMerc
  differs if it does any of the following: evaluates all region faces instead of the first 1024 queued; uses a
  different queue order; lets a later equal D win; or skips the reduction when the view leaf has no region.
  Needs a black-box run to confirm: the binary fixes the rule but not, without a full renderer trace, which vertex of
  sud3 comes first.
- Verify: in OpenMerc, log for the first overlay pass the queued face order and each far-flag vertex's D. At sud3
  (1640, 272, −552) with the original's view angles, the first vertex achieving the minimum must belong to region 5. A
  synthetic test: two far-flag regions A (near) and B (far), eye in a solid leaf, with a B boundary face queued before
  any A face and having a vertex exactly on an unmarked B boundary plane: the wall takes B's colour.

#### I4c.3 SPEC-410 "World diagnostics" — `gl_showtris` draws only in the two-pass world path (Resolution)

- Old: SPEC-410 World diagnostics: "`gl_showtris` 1 | triangle outlines over the world without depth test; 2 with depth
  test; ≥ 3 also the lightmap sample grid …; 4 grid only". Black-box: no outlines at all for values 1–4 with
  multitexture available.
- New: the observation is right. The outline pass draws the surfaces of the per-lightmap **surface chains** that the
  world walk builds for the separate lightmap blend. Those chains are filled only when world surfaces are drawn by the
  two-pass path (base texture, then lightmaps blended). They are emptied at the start of every world drawing. The
  two-pass path is used only when one of these holds:
  - multitexture is not in use (the driver lacks it or it is switched off);
  - `r_fullbright` ≠ 0;
  - the frame is a night-vision view (rdflags 0x4).
  With multitexture in use (the normal case on any current PC) the world is drawn by the single-pass multitexture
  path, the chains stay empty, and `gl_showtris` shows nothing. This is the same limitation as Quake II's triangle
  outlines.

  When the chains are filled, the pass is as SPEC-410 says. It runs after the sky box and the terrain and covers only
  chained world surfaces. Liquids, sky, alpha surfaces, fog-boundary faces, inline brush models and terrain are never
  outlined. OpenMerc **matches** the observation. Add the condition to the table.
- Verify: `r_fullbright 1`, `gl_showtris 1` on ger1: outlines appear on the world surfaces (not on water or sky).
  `r_fullbright 0`: they disappear again.

#### I4c.4 SPEC-413 §7 — what `wp`, `fp` and `qp` of `r_speeds 1` count (Resolution)

- Old: "world/brush polygons drawn, fog-volume polygons, …, alpha quads drawn. Field meanings of tx, lm, tp, qp
  UNVERIFIED". Black-box: irq3b original `352:wp 659:fp` vs OpenMerc `346:wp 619:fp`. tsr1 original `548:wp … 0:qp`
  vs `530:wp … 15:qp`.
- New: the exact counters, all for the frame of the print (one RenderFrame call):
  - **wp**: set to 0 at frame set-up, +1 for every surface handed to a surface-drawing routine. It counts one per
    surface, never per polygon or per pass:
    - world and inline-model surfaces drawn by the two-pass path, by the multitexture path or by `gl_drawflat`;
    - liquid surfaces (both passes of I4c.1 count once);
    - world alpha surfaces (flag 0x800) drawn in the sorted alpha pass;
    - inline-model alpha surfaces.

    Not counted: fog-boundary faces (0x10000, never drawn), sky surfaces (they go to the sky box), terrain, the fog
    overlay, the far wall, detail/environment passes and the lightmap blend. Surfaces rejected by culling (frustum,
    back face, PVS) are not counted.
  - **fp**: set to 0 at frame set-up and again at the start of the first fog overlay pass of the frame. +1 for every
    **region polygon** (lump 19 facepoly) of every face processed by an overlay pass of the frame: world, inline
    models and alpha surfaces, each pass adding. The polygon counts before its vertices are evaluated, so polygons
    whose vertices were already handled still count. In `gl_fogmode` 3 the diagnostic polygons count the same way.
    Not counted: faces beyond the 1024 queued per pass (I4c.2), interior faces in `gl_fogmode` 1 (they use OpenGL fog
    and are not queued), and the far wall.
  - **qp**: set to 0 at the start of the sorted alpha pass, +1 for every alpha quad that passes the collection test
    of SPEC-412 §"Collection" item 1, whether it is drawn at once (flag 0x0040) or sorted. Quads failing the leaf test
    are neither drawn nor counted. Quad chains and alpha world surfaces are not counted. The leaf test reads the visit
    mark of the **first surface listed for that leaf**. For a leaf with no surfaces this is the first surface listed
    after it in the shared leaf-surface list, which answers the SPEC-412 open question. So a quad counts exactly when
    the original would draw it. tsr1's 0 means that none of the 15 submitted quads was drawn in that view.
  OpenMerc's differences point at drawing differences, not at counting:
  - `qp` 15 vs 0: OpenMerc draws quads the original culls; check the leaf test;
  - `fp` 619 vs 659: region polygons of overlay passes; compare the per-pass face queues, including inline models and
    the alpha pass;
  - `wp`: check that sky and boundary faces are excluded and that inline-model surfaces are included.
- Verify: tsr1 paused spawn view, `r_speeds 1`: `qp` equals the number of alpha quads visible on screen. Disable one
  submitted quad's leaf visibility (stand so that its leaf leaves the frustum): it disappears and `qp` drops by one.

### K. I5s-2 sound backend behaviour (items 1–6)

#### I5s-2.1 SPEC-040 §1.3 — distance on `.adp` sounds (Correction)

- Old: "when an `.adp` sound starts, its distance multiplier is set to 0 after the first spatialization. From the next
  frame on it plays at full master volume … The first mixed block may still be attenuated and panned." Black-box: full
  volume from the first 2 ms at 500, 2500 and ~5700 units, and the sound plays to its last sample (2n − 38).
- New: the observation is right. The last sentence of the spec is wrong. Defsnd re-spatializes every channel that is
  not an automatic (loop) channel **immediately before each painted piece** of the mixing loop, not only once per
  frame in S_Update. This SoF addition is absent from Quake II. Hence:
  1. When a pending sound is issued (inside the mixing loop), it is spatialized once with its distance multiplier.
     For an `.adp` sound the multiplier is then set to 0.
  2. The channel is painted only while its left or right volume is non-zero:
     - If the first spatialization gave a non-zero volume, the channel is re-spatialized (now with multiplier 0)
       before its first samples are painted. So the very first painted sample is at full master volume in both ears.
     - If the first spatialization gave 0/0 (the source is beyond the audible range of its attenuation), the channel
       is not painted in that mixing call. Its position stays at 0 and nothing is lost. At the next frame's
       re-spatialization in S_Update (multiplier 0) it gets full volume and is not freed, and it plays from its first
       sample.
  3. The channel's end time for an `.adp` sound is the paint time + 1000 × length, so a delayed start cuts nothing.
     The decoded position advances only while samples are painted, so the sound plays to its last sample.
  The only effect of distance on an `.adp` start is therefore a delay of at most one client frame for sources beyond
  the audible range. No sample is ever attenuated or panned. Replace the last sentence of the §1.3 item by these rules.
  The per-piece re-spatialization holds for all non-loop channels (PCM too). Add it to §1 step 5.3.
- Verify: `.adp` with attenuation 1 started at 2500 units: its first non-zero output sample appears within one client
  frame and has full amplitude. At 500 units: from the first sample. The total played length is 2n − 38 samples in
  every case.

#### I5s-2.2 SPEC-040 §3.2 — change to general set 0, or to a set without a loop (Correction)

- Old: "For 2000 ms of client time after a change, both loops play: the previous set's loop at trunc(Vloop_prev · (1 −
  f)) and the current set's at trunc(Vloop_cur · f) …". Black-box: 14 → 0 stops the ElevatorShaft loop at once.
- New: the observation is right. The crossfade and the steady loop are both handled only when the **current** set is
  non-zero and has a loop. When the current index is 0, or the current set has no loop (loop index 255), no general
  loop is added at all, so the previous set's loop stops at once: loop sounds not re-added in a frame are stopped. A
  change from a set to 0 is never faded out. Within the 2000 ms window the previous set's loop is played only when the
  previous index is non-zero. So 0 → n fades in, n → m crossfades, and n → 0 cuts. The random one-shot timer is still
  rescheduled on every change (§3.3). OpenMerc **matches** (it follows the observation).
- Verify: player-state ambient field 14 → 0: the loop ends within one frame. 0 → 14: 2 s fade-in. 14 → 7: 2 s
  crossfade.

#### I5s-2.3 SPEC-040 §1.1 — order of the spatialization rules (Correction)

- Old: §1.1 lists "If the client is not in the active state, left = right = 255" before "Sounds on the listener
  entity get left = right = V". Black-box: a local sound in the menus plays at 153.
- New: the observation is right. The listener rule is tested **first**: a sound on the listener's own entity (the
  client's entity number + 1, which is also the entity of local sounds) gets left = right = V at every spatialization,
  in any connection state. Only for other entities does the "not active → 255/255" rule follow, then the distance
  rules. Local sounds (S_StartLocalSound) are started with volume 0.6, so V = trunc(0.6 × 255) = 153. Reorder the two
  bullets (Quake II order). OpenMerc **matches**.
- Verify: `play misc/menu1.wav` at the main menu (not connected): channel volumes 153/153.

#### I5s-2.4 SPEC-AMEND-001 A — Cvar_Get takes four arguments (Correction)

- Old: "Cvar_Get: name, default value, flags; returns the cvar". Black-box: called with three arguments, 1.07f crashes
  right after `------- sound initialization -------`; with a fourth argument NULL it works.
- New: the engine's cvar-get import takes **four** arguments: (name, default value, flags, change callback). The
  fourth is the change callback stored in the cvar record at offset 0x10 (SPEC-AMEND-005 G18.22), or null. The engine
  calls it with the record when the cvar is created with a callback, and in the other cases of SPEC-AMEND-005. A
  missing fourth argument passes whatever lies on the stack. That is taken as a callback and called at once, hence
  the crash. Defsnd, EAXSnd and A3Dsnd always pass null. The import is the same function the renderer and the
  client use (SPEC-002 slot "Cvar_Get" with `cvar_changed_fn`). Amend the SPEC-AMEND-001 A row:
  "Cvar_Get: name, default value, flags, change callback (null in every stock sound module)".
- Verify: OMsnd passing a non-null test callback for `s_volume`: the callback fires once at creation and again on
  `s_volume 0.5`.

#### I5s-2.5 SPEC-040 — what S_StopAllSounds does (Resolution)

- Old: not specified for music and ambient state. Black-box: `stopsound` during music gives a ~0.2 s gap, then the music
  continues on its old timeline.
- New: S_StopAllSounds (when the module is started):
  - empties the pending start requests (all 128 back to the free list);
  - clears all 32 mixing channels (no sound, no position);
  - fills the whole DMA output buffer with silence (0 for 16-bit, 0x80 for 8-bit output).
  It does **not** change the paint time, the music state (tracks, positions, levels, fades, timers), the ambient state
  (current and previous general set, crossfade start, random one-shot timers) or the sound-processing gain.
  Consequences are as follows.
  - the part of the output that was already mixed ahead (`s_mixahead`, typically ~0.2 s) is lost, which is the gap;
  - the music continues from its own position, which had already advanced over the lost part, so it stays on its
    timeline;
  - the general and point ambient loops are re-added by the next frame's ambient update at their time-derived
    positions, i.e. paint time modulo length;
  - one-shots already playing, including `.adp` speech, are gone for good.
  OpenMerc **matches** (it follows the observation).
- Verify: `stopsound` 10 s into a music track: silence for about `s_mixahead` seconds, then the track continues at
  10.2 s, not at 10.0 s. An ambient loop resumes in the same frame as the music.

#### I5s-2.6 SPEC-301 command line — more than 50 words (Resolution, for I1a)

- Old: "At most 50 words including the program name; more is a fatal error `argc > MAX_NUM_ARGVS`." Black-box: 51 words
  crash with an access violation after 0.2 s, nothing in `sof.log`; 48 words run.
- New: the limit is exactly **50 words including the program name**; 50 words run. With 51 or more, the very first
  step of the engine's start-up raises the fatal error `argc > MAX_NUM_ARGVS`. At that point nothing is initialised:
  no memory zones, cvars, filesystem, console or log file. The fatal-error path then prints to the console, wipes
  the `current` savegame directory, closes the menus and shuts down the server and the client, all on uninitialised
  state. One of these faults (UNVERIFIED which), so the visible result is an access violation with no message and
  nothing logged. A reimplementation should show the error text (e.g. a message box) and exit. Matching the crash is
  not required. The words are counted by the C runtime's argument splitting (quotes group words).
- Verify: a command line of exactly 50 words (program name + 49) starts normally; 51 words never reach
  `sof.log`. Use `+exec` for long test configurations.

### L. F5 SPEC-AMEND-005 follow-ups for G5 (items 1–4)

#### F5.1 SPEC-741 §14.4 and SPEC-AMEND-005 G5.7 — juncture duplicates and the `bot_wp_verifypaths` error line (Correction)

- Old: SPEC-AMEND-005 G5.7: "each new juncture is added to its own path and then to the other path … A juncture with
  the same other path at the same waypoint counts as a duplicate and is not added." OpenMerc builds two entries at
  waypoint 1 on jpndm2 path 1 (the mirror of path 6's end juncture 129 → 1 and the mirror of its own loop juncture
  52 → 1) and prints `ERROR! Path 1 has two junctures coming off of waypoint 1`; the original prints
  `Paths verified. 0 errors reported.`
- New: the original never creates the second entry; the duplicate rule is more precise than G5.7 says.
  1. A juncture record holds: path A (the path whose end created it), path B, waypoint a (on A), waypoint b (on B).
     Seen from a path P, its own-side waypoint is a when P = A, else b; the other side is the remaining one.
  2. **Adding to a path P is refused** when P already holds a record whose stored (a, b) pair equals (own-side,
     other-side) of the new record as seen from P. The other path number plays no part in this test.
  3. A loop juncture (A = B, the end of a path links back into the same path) is added to P twice by the "own path,
     then other path" rule; both times P = A, so the own side is a both times, and the second addition is refused. It
     therefore appears **once**, at waypoint a (52 on jpndm2 path 1), never mirrored at waypoint b. Path 1 then holds
     only one record at waypoint 1 (path 6's), and verification finds no error.
  4. **Verification** (`bot_wp_verifypaths`, and automatically after every path build while `bot_wp_edit` ≠ 0):
     for every path with two or more records, for every ordered pair of different records whose own-side waypoints are
     equal, the line `ERROR! Path <path> has two junctures coming off of waypoint <waypoint>. That's bad.` is printed
     unless that waypoint id was already reported earlier in the same verification (the list of reported ids is
     shared by all paths, so a waypoint id is reported at most once per run, even for another path). At the end:
     `Paths verified. <n> error<s>reported.` with n = number of reported waypoint ids (the format has no space
     before "reported").
  Amend G5.7's duplicate sentence by steps 2–3. OpenMerc **differs** (loop junctures mirrored).
- Verify: jpndm2, `cheats 1`, `bot_wp_edit 1`: `Paths verified. 0 errors reported.`; `bot_wp_pathinfo 1` lists the
  loop juncture once.

#### F5.2 SPEC-741 editor commands — why `bot_wp_pathinfo` and `bot_wp_verifypaths` printed nothing (Resolution, needs a black-box run)

- Old: SPEC-741 editor command list. Observation: both commands printed nothing although `bot_wp_whichpath`
  answered.
- New: all editor commands go through the same gate (a client command; `bot_wp_edit` ≠ 0 and the per-frame "alone"
  rule of SPEC-AMEND-005 G5.12) and print through the developer-independent server console print, like
  `bot_wp_whichpath`.
  - `bot_wp_pathinfo` and `bot_wp_whichpath` read their first argument as an integer; **without an argument, with a
    non-number or with 0 nothing is done and nothing is printed**. With a number: `bot_wp_pathinfo` prints
    `PATH <n>: start node = <s>, end node = <e>`, a line `---- <k> Junctures ----` and one
    `...with path <q> between nodes <a> and <b>` line per record, or
    `<n> is not a valid path.` for a number outside 1 … (paths − 1).
  - `bot_wp_verifypaths` takes no argument and always prints at least its summary line (F5.1) when it reaches the
    game module.
  So the silent `bot_wp_pathinfo` is explained by a missing argument; a silent `bot_wp_verifypaths` means the command
  did not reach the game (it has no other condition than `bot_wp_whichpath`). The binary cannot say more.
- Verify (black box): type `bot_wp_pathinfo 1`: the PATH line appears. Type `bot_wp_verifypaths` right after a
  `bot_wp_whichpath 5` that answers: the summary line must appear; if it does not, capture the client's outgoing
  command (it must reach the server as a client command).

#### F5.3 SPEC-741 §14.5 — the spawn-point-to-waypoint trace (Resolution)

- Old: §14.5 (spawn tables: the nearest waypoint with a clear trace). OpenMerc now ignores client 1.
- New: confirmed. For each spawn point, every waypoint is tested by a point trace (zero box) **from the waypoint to
  the spawn point**, ignoring **entity 1** (the first client slot, whether or not a player is there), with content
  mask 0x02010003 (solid, window, player clip and bodies). The waypoint is accepted only when the fraction is exactly 1;
  among accepted ones the one with the smallest squared distance wins (first found on ties). Any other entity with a
  body (a second player, a bot, a character) standing on the line can still hide the waypoint. OpenMerc (ignore client
  1) **matches**; it must also use that mask and direction.
- Verify: done on jpndm2 (path numbers equal). Second player standing on a spawn point during the build: that point
  gets a different waypoint than without him.

#### F5.4 SPEC-741 editor `bot_wp_botroute` — print order and the "don't know" line (Correction)

- Old: the spec lists the origin before the node; observation: `DESTINATION: node 72 (-64 -960 56)`, and nothing more
  for a bot whose destination is a spawn waypoint.
- New:
  1. The bot shown is the one nearest to player 1 (smallest squared distance among all bots); with no bot nothing is
     printed.
  2. Header: `BOT: <name> near node <last waypoint>, path <its path>  DESTINATION: <dest> <origin>` (two spaces
     before `DESTINATION`), where dest is `node <id>` or `NONE`, and origin is the
     destination's position as `(x y z)` or empty when there is none. So the node comes before the origin.
  3. Steps: first the bot's current juncture (if it has one) as step 0, then every juncture of its stored route as
     steps 1, 2, …: `Step <i>: node <waypoint> on path <path>`. A walk keeps a current path (start: the bot's path) and
     the previously left path (start: none); for each record the side that touches the current path gives the node
     and the other side becomes the "previous" path; a record touching only the previous path switches back to it.
  4. `Man, I don't know. Try again.` is printed, and the listing stops, when a route record touches neither the
     current nor the previous path (an inconsistent stored route, e.g. right after the route was replaced). With no
     route at all (route search failed, as for a spawn-waypoint destination) only the header is printed.
  Correct the spec's order. OpenMerc **differs** in the order if it prints the origin first.
- Verify: done for the header. The "don't know" line needs a route whose records do not chain (black box: hard to
  provoke; not needed for parity of normal play).

## Constants

### D. G2 character AI

| Item | Value |
|---|---|
| Selector shot start | shooter origin + (0, 0, 20) |
| Selector bolts | 1 `sbolt_mouth`, 3 `gbolt_shoulder_lf`, 4 `gbolt_shoulder_rf`, 5 `gbolt_chest_f`, 6 `gbolt_gut_f`, 7 `gbolt_groin`, 8 `wbolt_hand_r` (+ drop weapons), 9 `sbolt_mouth` (kill variant 2) |
| Lethal shot | target body kill setting variant 1 (selector 9: variant 2); next hit 9999 |
| Crouched-target drop for type 9/14/16/17/18 shots | 16 units |
| Character ground friction | 0.01 alive, 0.5 corpse; gravity 1.0 |
| Attack-choice facing limit | dot ≥ 0.717 |

### E. G3 damage model, gore, bodies

| Item | Value |
|---|---|
| Cap bolt parts switched off at a hand-over | body-side and piece-side cap bolts of every handed-over zone (not the gore bolt) |
| Gib piece limits | not for the head; G < 5; < 2 pieces per gib; freed again when > 1 piece already spawned or G > 2 |
| Player hit-reaction damage argument | 1.0 |
| Second-estimator postures | 1, 2, 5, 6, 7 |

### I. I2d GameSpy lobby chat

| Item | Value |
|---|---|
| Lobby request time limit | 30 000 ms after sending, restarted by every reply matched to the request |
| CTCP recognition | byte 1 + letter … byte 1 with a space after the command; `PING` and `ACTION` case-sensitive |
| Disconnect log lines | `^4Disconnected ` (local end, empty reason); `^4Disconnected Disconnected` (socket closed by the server) |
| `QUIT :Later!` | only when the session is registered (after 001) |

## Test vectors / how to verify

Each item ends with its own "Verify" line; those are the test vectors of this amendment. Items whose answer still
needs a run of the original game are listed per section under "Open questions", with the black-box test to run.

## Open questions

### A. I1c engine client: the Ghoul client system

1. Needs a black-box run: I1c.2, the claim that auto-rotating entities keep pitch and roll un-negated (the clean
   team's trace showed only a zero roll). The test is in I1c.2.
2. Needs a black-box run: I1c.3, root-first lighting in the main menu (the vertex colours of the first-drawn model).
   The binary is unambiguous; the run only confirms the trace reading.

### B. I7 player module

- I7.4: whether the default skin of material `arms` in the stock in-view models differs from `arms_sleeve`, which
  decides if the predicted-switch skin finding is visible on predicting clients.
- I7.9: what a predicting client shows when the server turns an unpaid refire into an instant reload (its own reload
  animation from the weapon-state byte, against an idle server).
- I7.13 item 4: behaviour of methods 8/11 when no weapon at all is usable (slot −1).

### C. G18 game module

1. Needs a black-box run: G18.32 — which condition disables spawning on tsr1 (the decay itself is decided).
2. Needs a black-box run: G18.35 — shots passing through a player who watches a flag-2 camera, and why the old packed
   box keeps being sent (test in the item).

### D. G2 character AI

1. G2.10: the actual stored floor height of kos1 `introjohn` in the original (save-game probe above).
2. G2.14: black-box confirmation that the sud2 cows hold the player as target.
3. G2.12: whether the one-think request of `waitaction1` at the end of a spawnflag-2 list is visible.

### E. G3 damage model, gore, bodies

1. G3.20: where the original loses one point of a 45-damage pistol hit on a player's arm (black-box run above).
2. G3.19: the play-once length of `STD_SARMUNHURT_LT_A_N` in `MESO_PLAYER.ghb` (data check; expected 0.2 s).

### F. G4 roster, helicopters, vehicles, special characters

1. Needs a black-box run: G4.10 — how the ±1 rad yaw limit of the crew heads maps to the visible turn of a part with
   a 90°-turned attach bolt (test in the item).

### G. G7 props and server effects

1. Optional black-box check: G7.11 — the moved-dish test in the item (the data check already decides it).

### H. G9 game modes and Ghoul glue

1. Needs a black-box run: G9.13 — listen-server repeat of test vector 6 decides whether the engine's server Ghoul
   system delivers `EOS` for a once-and-hold sequence (test in the item).

### I. I2d GameSpy lobby chat

1. Needs a black-box run: I2d.1 point 3 (PONG after an early ERROR on a socket that stays open) and I2d.4 rule 3
   (limit restarted by a late 322). Both follow from the binary; the tests are in the items.

### J. I4c renderer: terrain, liquids, flowing, fog mode 1

1. Needs a black-box run: I4c.2 — which sud3 vertex is first to reach the minimum (queue order of the original's world
   walk and texture chains from that viewpoint).
2. Needs a black-box run: I4c.3 — outlines with `r_fullbright 1` (confirms the path condition).

### L. F5 SPEC-AMEND-005 follow-ups for G5

1. Needs a black-box run: F5.2 — whether `bot_wp_verifypaths` typed in the original reaches the game module (test in the item).
