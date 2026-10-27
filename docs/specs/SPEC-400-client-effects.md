# SPEC-400: Client effects system — effect definitions (.eft), effect runtime, particles, decals, debris, lights
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (client), version 1.07f
Depends on: SPEC-001 (shared random generator), SPEC-002 (alpha quads, quad chains, dynamic lights, light styles),
SPEC-003 (player-module client hooks), SPEC-142 (.eft data layout, clean-team census), SPEC-201 (svc_effect,
svc_temp_entity), SPEC-202 (entity effect slots, entity effects bits), SPEC-203 (CS_EFFECTS, downloads),
SPEC-302 (cvars), SPEC-401 (temp entities and entity events)

## Purpose
Everything the client needs to reproduce Soldier of Fortune's visual and audio effects: the meaning of every
field of an `.eft` effect definition, how definitions are found and started (network messages, entity effect
slots, the view weapon, particle trails/collisions), how a running effect is updated, how its particles are
created and simulated, and how particles, decals, debris, dynamic lights and light styles reach the renderer.
Temp-entity types and entity events, which mostly use hard-coded effects instead of `.eft` files, are in SPEC-401.

Terms used below:
- **rand(a, b)** — the shared random generator of SPEC-001 (`flrand`): a + (b − a)·r/32768. A *range* is a stored
  pair (min, max); "rand(range)" returns min without drawing a random number when min equals max, otherwise
  rand(min, max). **irand(a, b)** is the integer variant (inclusive). Draw order matters only for exact replay and
  is not normative.
- **fx time** — the effects clock in seconds (see *Timebase*). "now" below always means fx time.
- **axes** — every running effect has a forward, right and up vector (unit vectors) and an origin.
- **particle** — one renderer alpha quad (SPEC-002) driven by the effects system; it may also be a Ghoul model.

## Interface / layout

### Cvars and commands
| Name | Default | Flags | Meaning |
|---|---|---|---|
| `fx_sim_time` | 60 | — | Simulation rate of the effects system in steps per second (step = 1/value s). |
| `cl_max_quads` | 256 | archive | Number of decal slots, min(value, 1024). It does **not** limit particles (see *Particle pool*). |
| `cl_max_debris` | 128 | archive | Number of debris slots, min(value, 1024) (see *Debris*). |
| `fx_maxdebrisonscreen` | 16 | — | Live-debris cap: at most value + 1 debris pieces survive each frame (see *Rendering hand-off*). |
| `cl_freezequads` | 0 | — | Non-zero: particles, decals and debris are frozen — none are created, none move or expire, all are still drawn. |
| `freezeworld` | 0 | — | Same freeze as `cl_freezequads` (the effects system tests both). |
| `fx_noflash` | 0 | — | Non-zero: the predicted primary/alternate-fire client effects of the local player are skipped (shell-casing sounds, zoomed sniper effect). |
| `fx_edit` | 0 | — | Effect editor on (see *Editor*). |
| `fx_editsegment` | 0 | — | Editor substitution filter, wraps within 0..4. |
| `fx_editdir` | "p:" | archive | Root directory that `fx_save` writes below. |
| `fx_run` | 0 | — | Editor: non-zero starts the edited effect once, then is reset to 0. |
| `fx_partadd`, `fx_partsub`, `fx_partadjust` | 0 | — | Editor: adjust the selected editor row by +value / −value / value; reset to 0 after use. |
| `fx_graphic`, `fx_sound`, `fx_collide`, `fx_trail` | 0 / "null" / 0 / 0 | — | Editor: text inputs for the particle graphic, sound name, collision effect and trail effect of the edited segment. |
| `fx_save <name>` | command | — | Writes the edited effect to `<fx_editdir>/base/effects/<name>.eft` (format version 3). Without argument prints `Usage: fx_save <EffectFileName>`. |
| `fx_load <name>` | command | — | Loads `effects/<name>.eft` into the editor. Without argument prints `Usage: fx_load <EffectFileName>`. |

A missing effect file prints `FX_System: Unable to find file effects/<name>.eft` and leaves the definition empty
(an empty definition plays nothing).

### .eft layout and field meanings
SPEC-142 gives the byte layout found from data. This section gives the meaning of every field. Offsets in the
primitive tables are **relative to the start of the primitive, i.e. to its type float**, exactly as in SPEC-142,
and are given for the current versions (file version 3, emitter version 3, particle version 7). Version rules
follow each table. A "range" is two float32 (min, max). All names are 64-byte fields holding a NUL-terminated
string; bytes after the NUL are garbage and must be ignored.

#### File header and trailer
| Offset | Type | Meaning |
|---|---|---|
| 0 | float32 | File version. Values above 3.0 are rejected (the file is ignored). |
| 4 | int32 | Repeat mode: 0 = "Instant", 1 = "Continual". Used only by the editor's preview (see *Editor*). |
| 8 | range | Repeat delay in seconds. Used as the re-trigger interval of entity effect slots and by the editor preview of Continual effects. |
| 16 | float32 | Number of primitives n. At most 10 (the editor refuses more and a running effect has 10 slots; 1.07f does not check the file — a file with more than 10 must be treated as invalid). |
| 20 | primitive × n | See below. In file version 3 every primitive is followed by an 8-byte **trigger delay** range. |
| end − 8 | range | Version ≥ 2 only: repeat-delay limit. When loading, repeat delay min := min(min, limit min) and repeat delay max := min(max, limit max). Version 1 files take the limit equal to the repeat delay. No other use. |

Primitive type codes (float32 at primitive offset 0): 0 emitter (editor label "Effect"), 1 sound ("Sound"),
2 dynamic light ("Light"), 3 decal ("Decal"), 4 view feedback ("Feedback"). Any other value makes the rest of the
file unreadable (1.07f stops reading; treat the file as invalid).

**Trigger delay** (version 3 files; zero for older files): when the effect starts, each primitive gets
trigger time = start + rand(trigger delay). A sound, light, decal or feedback primitive fires once at its
trigger time; an emitter starts emitting at its trigger time.

#### Type 1 — sound (version 3: 92 bytes)
| Offset | Type | Meaning |
|---|---|---|
| 4 | name | Sound file (with `.wav`), registered when the effect is loaded and again after a sound-system restart. |
| 0x44 | range | Volume (editor default 0.6). |
| 0x4C | range | Attenuation (editor default 2.0). |
| 0x54 | range | Trigger delay (version 3). |
Fire: one positional sound at the effect origin, no entity, channel 0, volume = rand(volume), attenuation =
rand(attenuation), no time offset.

#### Type 2 — dynamic light (version 3: 52 bytes)
| Offset | Type | Meaning |
|---|---|---|
| 4 | range | Radius (intensity in SPEC-002 terms; editor default 30). |
| 0x0C | range | Red 0..255 (default 255). |
| 0x14 | range | Green 0..255. |
| 0x1C | range | Blue 0..255. |
| 0x24 | range | Lifetime in seconds (default 1.0). |
| 0x2C | range | Trigger delay (version 3). |
Fire: one dynamic light at the effect origin with colour (trunc(rand(red)), trunc(rand(green)), trunc(rand(blue)))
as 0..255 values, radius rand(radius), lifetime rand(lifetime) seconds, no key (never replaces another light).
Its fading and conversion to the renderer's 0..1 colour are described under *Dynamic lights*.

#### Type 3 — decal (version 3: 100 bytes)
| Offset | Type | Meaning |
|---|---|---|
| 4 | int32 | Decal record version, 0 or 1; values above 1 make the file invalid. No other meaning. |
| 8 | name | Decal image (sprite path; a trailing `.m32` is removed at load). |
| 0x48 | float32 | Start size. |
| 0x4C | float32 | End size. |
| 0x50 | float32 | Editor label "Growth Duration", but used as the growth **acceleration** (units/s²; stock values ≤ 0, e.g. −3.1 for puddles). |
| 0x54 | float32 | Editor label "Growth Acceleration", but used as the growth **time** in seconds (e.g. 3.0 for puddles; 0 = no growth). |
| 0x58 | int32 | Orientation: 0 "Forward" (trace along the effect's forward vector), 1 "Downward" (trace along (0, 0, −1)), 2 "Backward" (along −forward). |
| 0x5C | range | Trigger delay (version 3). |
Fire: trace a line from the effect origin 512 units in the chosen direction. If it hits the world (not an
entity), add a decal at the hit point on the hit surface, colour white, alpha 250, with the decal parameters above
(see *Decals*). Nothing happens if nothing or an entity is hit.

#### Type 4 — view feedback (44 bytes)
| Offset | Type | Meaning |
|---|---|---|
| 4 | range | "Blinding light" — no effect in 1.07f. |
| 0x0C | range | "Deafening" — no effect in 1.07f. |
| 0x14 | range | Camera shake strength. |
| 0x1C | int32 | Force-feedback effect id (0 = none; 1..32, list in *Constants*). |
| 0x20 | int32 | Copy of the force-feedback id (written by the editor, ignored). |
| 0x24 | range | Trigger delay (version 3). |
Fire: s = rand(shake); if s > 0.05 the local view shake is increased by rand(shake) × 200 × (600 − d)/600, where d is
the distance from the effect origin to the view origin (a second, independent draw), the result capped at 200, and
the shake decay rate is set to 80 per second (see *Camera shake*; note that beyond 600 units the term is negative and
reduces an existing shake). Then, if the force-feedback id is non-zero, that force-feedback effect is played on a
force-feedback device (ids 10, 20, 22, 28 only when the device options allow continuous effects; UNVERIFIED).

#### Type 0 — emitter (file version 3, emitter version 3, particle version 7: 572 bytes incl. trigger delay)
| Offset | Type | Editor label / meaning |
|---|---|---|
| 4 | float32 | Emitter record version (≤ 3; above 3 the file is invalid). |
| 8 | range | "Delay Per Burst": seconds between bursts (default 0.05). If a drawn delay is below 0.01 the emitter emits exactly one burst. |
| 0x10 | range | "Duration": emitting time in seconds, clamped to 0.005..99999 at load. |
| 0x18 | int32 | "Style": where elements of a burst are placed, 0..8, see *Emitters*. |
| 0x1C | int32 | "Elements": particles per burst (default 1). |
| 0x20 | int32 | Emitter flags: 0x01 "Attached To Source", 0x02 "Scale By Element", 0x04 "Scale By Time", 0x10 distance level-of-detail (no editor label), 0x20 "Get Dir From Offset". |
| 0x24 | int32 | Scale targets (what the scale factor multiplies): 0x01 "Scale Velocity", 0x02 "Scale Acceleration", 0x04 "Scale Height", 0x08 "Scale Width", 0x10 "Scale Alpha", 0x20 "Scale Duration", 0x40 "Scale Radius" (editor only, no runtime effect). |
| 0x28 | float32[3] | Region "Minimum" (forward, right, up components; default −20 each). |
| 0x34 | float32[3] | Region "Maximum" (default +20 each). |
| 0x40 | float32[3] | No runtime meaning (editor default 128, 0, 20). |
| 0x4C | float32 | Ring "Radius" (default 64). |
| 0x50 | float32 | "Element Scale" first value (default 1). |
| 0x54 | float32 | "Element Scale" second value (default 0). |
| 0x58 | float32 | "Time Scale" first value (default 1). |
| 0x5C | float32 | "Time Scale" second value (default 0). |
| 0x60 | — | Particle description, see next table (particle offsets there are relative to this primitive too). |
| 0x220 | range | Delay limit (emitter version ≥ 2): delay-per-burst min := min(min, limit min), max := min(max, limit max). |
| 0x228 | int32 | No runtime meaning. (Stored as an integer by the editor, read back as a float and truncated, so it is 0 after loading.) |
| 0x22C | float32 | "Dist per Unit": spacing for line styles (emitter version 3). |
| 0x230 | float32 | No runtime meaning (default 0.5). |
| 0x234 | range | Trigger delay (file version 3). |
Emitter version < 2: no delay limit and no 0x228 field. Emitter version < 3: no 0x22C/0x230 fields (both 0); when
the style is not 1 (region), the region minimum and maximum are reset to 0.

#### Particle description (inside an emitter, particle version 7)
| Offset | Type | Editor label / meaning |
|---|---|---|
| 0x60 | float32 | Particle record version (≤ 7; above 7 the file is invalid). |
| 0x64 | range | "Forward Vel" — initial speed along forward (default 100,100). |
| 0x6C | range | "Right Vel". |
| 0x74 | range | "Up Vel". |
| 0x7C | range | "Forward Accel". |
| 0x84 | range | "Right Accel". |
| 0x8C | range | "Up Accel" (gravity is a negative value here, e.g. −800). |
| 0x94 | int32 | "Particle Type": 0 Sprite, 1 Line, 2 Oriented Sprite, 3 Ghoul Object, 4 Ghoul Persistent Debris. |
| 0x98 | range | Sprite/oriented: "Height Start" (first billboard dimension). Line: start width. Ghoul: start scale. |
| 0xA0 | range | Sprite/oriented: "Height End". Line: end width. Ghoul: end scale. |
| 0xA8 | range | Sprite/oriented: "Width Start" (second billboard dimension). Line: start width of the far end. |
| 0xB0 | range | Sprite/oriented: "Width End". Line: end width of the far end. |
| 0xB8 | int32 | Line: "Point2 Source" 0 "Dir of Velocity", 1 "From Server", 2 "End of Prev", 3 "End2 is vel". Oriented sprite: "Orientation" 0 "Forward", 1 "Right", 2 "Up", 3 "Backward". Unused otherwise. |
| 0xBC | range | Line: "PointEndScale" — line length for Point2 sources 0 and 3 (default 40). |
| 0xC4 | name | Graphic: sprite image (types 0–2) or Ghoul model (types 3–4). A trailing `.m32` is removed. |
| 0x104 | range | "Duration": particle lifetime in seconds (default 0.5). |
| 0x10C | range | "Red" 0..255. |
| 0x114 | range | "Green". |
| 0x11C | range | "Blue". |
| 0x124 | range | "Alpha Peak" 0..255. |
| 0x12C | float32 | "Alpha Min Ratio" (default 0). |
| 0x130 | float32 | "Trail Life Time" (default 0.5). |
| 0x134 | range | "Start Angle" — billboard rotation at birth, radians. |
| 0x13C | range | "Angle Vel" — billboard rotation speed, radians per second. |
| 0x144 | int32 | Particle flags, see table below. |
| 0x148 | float32[3] | "Bounding" minimum (x, y, z) for collisions (particle version ≥ 2). |
| 0x154 | float32[3] | "Bounding" maximum. |
| 0x160 | float32 | "Elasticity" (default 0.7). |
| 0x164 | name | "fx_collide": effect started when the particle collides (empty = none). |
| 0x1A4 | float32 | "Trail Frequency" (default 60; particle version ≥ 3). |
| 0x1A8 | name | "fx_trail": effect left behind as a trail (empty = none; version ≥ 3). |
| 0x1E8 | range | Ghoul: "Forward Ang" (degrees added to pitch at birth; version ≥ 4). |
| 0x1F0 | range | Ghoul: "Right Ang" (added to yaw). |
| 0x1F8 | range | Ghoul: "Up Ang" (added to roll). |
| 0x200 | range | Ghoul: "Forward Avel" (angular velocity added to pitch, degrees per second). |
| 0x208 | range | Ghoul: "Right Avel". |
| 0x210 | range | Ghoul: "Up Avel". |
| 0x218 | int32 | "Alpha Style": 0 "Linear Fall", 1 "Linear Rise", 2 "L Rise,Fall", 3 "TrigRiseFall" (version ≥ 7). |
| 0x21C | int32 | "Color Style": 0 "Random Linear", 1 "Random Any", 2 "Linear Slide", 3 "Trig Slide" (version ≥ 7). |

Older particle versions: version < 5 has 244 extra bytes (no meaning) between 0xBC and the graphic name;
version < 2 has no bounding box, elasticity or collide name (box 0, elasticity 0.7, no effect); version < 3 has no
trail frequency or trail name (frequency 60, none); version < 4 has no angle fields (all 0) and only flag bits
below 0x4000 are kept; version < 6 clears flags 0x8000, 0x10000, 0x20000; version < 7 has no styles: alpha style 0,
colour style 0 if flag 0x20 is set, otherwise 1, and flags 0x40000 and 0x80000 are cleared.
Ghoul types (3, 4) load the model when the effect is loaded; other types register the graphic as a sprite image.

Particle flags (offset 0x144):
| Bit | Editor label | Effect |
|---|---|---|
| 0x00001 | Width Equals Height | Second billboard dimension (and its end value) = first; for lines the far-end width = near-end width. |
| 0x00002 | Lit By World | Renderer alpha-quad flag 0x0008 (lit). |
| 0x00004 | Additive Transluceny | Renderer flag 0x0200 (additive). |
| 0x00008 | Subtractive Transluceny | Renderer flag 0x0400 (darkening). |
| 0x00010 | Apply Physics | Collision and bounce (see *Particle simulation*). |
| 0x00020 | — | Old files only: selects colour style 0 when the file has no style fields. |
| 0x00040 | Leave Fade Trails | see *Particle simulation*. |
| 0x00080 | Leave Shrinking Trails | see *Particle simulation*. |
| 0x00100 | Absolute Acceleration | Acceleration ranges are world x, y, z instead of forward/right/up. |
| 0x00200 | — | The particle gets a new random rotation in [0, 6.28) every step (flicker). |
| 0x00400 | — | 50 % chance per particle of additive blending. |
| 0x00800 | Special Cheap Type (USE CAUTION) | Renderer flag 0x0080 (chain drawn as one batch). |
| 0x01000 | Special CurveToPoint line | Particle flies to the effect's second position in exactly its lifetime, on a curve (see *Particle creation*). |
| 0x02000 | One Frame | see *Particle simulation*. |
| 0x04000 | Collide with water | see *Particle simulation*. |
| 0x08000 | Remove on collision | see *Particle simulation*. |
| 0x10000 | No scaling | Sizes (line widths, Ghoul scale) stay at their start values. |
| 0x20000 | Collide with Entities | see *Particle simulation*. |
| 0x40000 | Decelerate to a stop | see *Particle simulation*. |
| 0x80000 | No ZBuffer | Renderer flag 0x0800 (no depth test). |

### Effect parameters
Every effect start carries a parameter set. Unless a source sets them: origin (0, 0, 0), forward (1, 0, 0),
right (0, 1, 0), up (0, 0, 1), scale 0 (treated as 1), no count override, no second position, placement mode
"position". Sources:

| Source | How the parameters are filled |
|---|---|
| svc_effect (SPEC-201 §5) | Effect = configstring slot. flags 0x01: origin from the message. 0x02: follow the entity (origin = its interpolated origin, axes from its angles; for the local player: the view origin and view angles). 0x04: attach to a bolt of the entity's model (or of the ghoul instance given with 0x08); bolt 0 = a random part of the model that is currently switched on (6 random tries, then a scan from a random start); origin and axes from the bolt's world matrix: forward = bolt local Z axis, right = bolt Y, up = bolt X; with flag 0x40 forward = bolt X, right = bolt Z, up = bolt Y (UNVERIFIED which is "reversed" visually). 0x20: second position = end point of a trace from the origin along forward for 1024 units (recomputed whenever the attachment is refreshed). Extension: ext 0x01 scale = short/128; ext 0x02 count override (elements per burst); ext 0x04 second position; ext 0x08 direction: forward = the decoded unit vector, right/up derived from its pitch and yaw with roll 0; ext 0x10 / 0x20 replace the region minimum / maximum of every emitter of the effect; ext 0x40/0x80 values are stored and unused. |
| Entity effect slot (SPEC-202) | see *Entity effect slots*. |
| View weapon note `effect` | see *View weapon*. |
| Particle trail / collision | see *Particle simulation*. |

## Behaviour

### Timebase
1. The effects clock (fx time) is separate from the client clock. Once per rendered frame the client first hands the
   current particles, quad chains, dynamic lights and light styles to the renderer, then advances the effects
   system in fixed steps of 1/`fx_sim_time` seconds.
2. If fx time is more than 0.2 s behind the client clock it jumps to client time − 0.2 (at most 0.2 s of catch-up
   per frame). While fx time < client time: run one effects step (effect instances, then particles), add the step,
   and clamp fx time to the client time. Every step integrates a full step length dt = 1/`fx_sim_time`, even the
   last one of a frame whose clock advance was clamped; at most 0.2 s worth of steps (12 at 60 Hz) run per frame.
3. All lifetimes, trigger times and emission times are compared against fx time.

### Effect tables and lookup
1. **Configstring effects.** When an effect configstring (SPEC-203, CS_EFFECTS slot i) is set, or when the level is
   prepared, the client loads `effects/<name>.eft` into slot i of a 256-entry table. Starting the precache clears
   every definition and every running effect first. svc_effect and entity effect slots use this table; an
   unloaded slot is an empty effect (plays nothing).
2. **Named effects.** Effects started by name (view weapon, material impacts, particle trails/collisions, client
   code) are looked up first among the loaded configstring effects (by name), then in a second 256-entry table of
   by-name effects. If the name is in neither, the file is loaded into the next free entry of the second table.
   When that file is missing, the entry is used up and stays empty and the name is not remembered, so every
   later request for the same missing name uses another entry; when all 256 entries are used, by-name requests
   that need loading are ignored.
3. After a sound-system restart all sound primitives re-register their sounds.
4. Downloads (SPEC-203 step 6): after downloading an effect file the 1.07f client tries to list the sound files and
   decal images it names, but it reads the primitive count as the first byte of the float32 count (always 0 for
   counts 1..10), so it never requests them. A reimplementation that wants compatible behaviour requests only the
   `.eft` itself; one that wants useful behaviour may also request the referenced sounds and images.
   (Observable: a client missing a custom effect's sound stays without it after downloading; UNVERIFIED by test.)

### Starting an effect
1. At most **256** effects run at the same time. A start request when all are in use is ignored (an svc_effect is
   still consumed).
2. The start time is now. Each primitive is armed: emitters get start = now + rand(trigger delay), end = start +
   rand(duration), next burst at start; other primitives get trigger time = now + rand(trigger delay).
3. Editor substitution: when `fx_edit` is non-zero and (`fx_editsegment` is 0 or equals the start's source class:
   1 editor preview, 2 game effects (svc_effect, entity slots, view weapon, material impacts), 3 particle trails,
   4 particle collisions), the effect being edited is started instead of the requested one.

### Updating a running effect (each step)
1. **Attachment refresh.** At the first step, and at every step after one in which the effect still had a pending
   one-shot primitive, an emitter inside its emitting period, or an "Attached To Source" emitter with living
   particles, the origin and axes are recomputed from the
   attachment (entity, bolt or particle; position mode never changes). The effect becomes **detached** — it keeps
   its last origin, fires no more pending one-shot primitives and its emitters stop emitting — when the attached
   entity is not in the current server frame, when that entity's effects field has bit 0x4000 set, or when the
   particle it follows has died. With a random bolt (bolt 0 in svc_effect) a new random part is chosen at every
   refresh, so the effect jumps between parts.
2. **One-shot primitives** (sound, light, decal, feedback) fire once at the first step with now > their trigger time
   (strictly greater), unless the effect
   is detached (then they never fire) or it waits for a random bolt that could not be found.
3. **Emitters**: see *Emitters*.
4. An effect ends (its slot is freed) when every one-shot primitive has fired or was skipped and every emitter has
   finished emitting and all its particles have died.

### Emitters
1. When now ≥ next burst time: if next burst time ≤ end time and the effect is not detached, emit one burst
   with progress t = 1 − (now − start)/(end − start) (1 at the start, 0 at the end); then draw d = rand(delay per
   burst); if d < 0.01 the emitter ends after this burst; the next burst time is now + rand(delay per burst) (a
   new draw). Only one burst is emitted per step.
2. An emitter is finished when now > end time and none of its particles are alive. While its particles live after
   the end, the effect keeps refreshing its attachment only if the emitter has "Attached To Source".
3. **Scale.** base = effect scale (1 if 0). With "Scale By Time": base ×= t × time-scale-first + (1 − t) ×
   time-scale-second, and the ring radius is multiplied by base. With "Scale By Element", element k (0-based) of
   a burst of N gets es = (g × element-scale-first + (1 − g) × element-scale-second) × base with g = 1 − (k+1)/N;
   otherwise es = base. es multiplies the quantities selected by the scale targets.
4. **Count.** N = count override from the parameters if non-zero, else Elements. With the distance level-of-detail
   flag: let D = squared distance from the origin to the view origin; if D > 512²: N = 1 when D − 512² > 512², else
   N = trunc(N × (1 − (D − 512²)/512²) + 1). For style 2 (line) with Dist per Unit p ≠ 0: N = trunc(|second
   position − origin| / p). N is at least 1.
5. **Element positions** (k = 0..N−1, f = (k+1)/N; "region offset" = a·forward + b·right + c·up with a, b, c drawn
   from [min, max] of each region component; region min/max come from the parameters' extension if present):

| Style | Label | Position |
|---|---|---|
| 0 | From a Spot | origin |
| 1 | From a Region | origin + region offset |
| 2 | In a Line | origin + f·(second position − origin) + region offset |
| 3 | In a Ring | (origin.x + R·cos θ, origin.y + R·sin θ, origin.z), θ = 2π·(f + φ), φ = rand(0, 6.28) once per burst; R = radius (×base when scaling by time). The ring is always horizontal. |
| 4 | In a Directed Line | origin + k·p·forward + region offset (p = Dist per Unit) |
| 5 | From Position2 | second position |
| 6 | From the floor beneath the effect | end point of a trace from origin to origin − 512·z |
| 7 | From the ceiling above the effect | end point of a trace to origin + 512·z |
| 8 | From surface in front of effect | end point of a trace to origin + 512·forward; if a surface was hit, the burst uses axes derived from the surface normal (forward = normal, right/up from its pitch and yaw) |

   With "Attached To Source", styles 0, 1 and 4 produce positions in the effect's local frame (spot: (0, 0, 0);
   region: (a, b, c); directed line: (k·p + a, b, c)) and the particle stays attached (see *Particle simulation*).
   With "Get Dir From Offset", each element uses axes derived from the direction origin → element position.
6. Each element creates one particle from the particle description (next section).

### Particle creation (from an emitter)
Inputs: position P, the effect's axes and parameters, element scale es, scale targets, attached flag.
1. **Lifetime** L = rand(Duration) (× es when "Scale Duration"). If L ≤ 0 no particle is made. Type 4 particles use
   the debris pool, all others the particle pool; if no slot is free nothing is made.
2. **Velocity v and acceleration a.**
   - "Special CurveToPoint line": v = (second position − P)/L + b·up + c·right and a = −1.9·b/L·up − 1.9·c/L·right,
     where c = rand(Right Vel) and b = rand(Up Vel).
   - otherwise v = rand(Forward Vel)·forward + rand(Right Vel)·right + rand(Up Vel)·up (attached: the three values
     as local x, y, z); a = rand(Forward Accel)·forward + rand(Right Accel)·right + rand(Up Accel)·up, or with
     "Absolute Acceleration" the vector (rand(Forward Accel), rand(Right Accel), rand(Up Accel)) in world axes.
     "Scale Velocity" / "Scale Acceleration" multiply v / a by es.
3. **Alpha.** A = rand(Alpha Peak) (× es with "Scale Alpha"). Alpha style 0 (Linear Fall): starts at trunc(A) and
   changes linearly to trunc(A) × Alpha Min Ratio at the end of life. Style 1 (Linear Rise): starts at
   trunc(A × Alpha Min Ratio) and rises linearly to trunc(A). Styles 2 and 3: start at 1 with peak A (curve under
   *Particle simulation*).
4. **Colour.** Colour style 0 (Random Linear): one t = rand(0, 1), each channel = trunc(min + t·(max − min)), so
   colours lie on the line between the min colour and the max colour. Style 1 (Random Any): each channel
   trunc(rand(range)) independently. Styles 2 and 3: start at the min colour and slide towards the max colour over
   the lifetime (see *Particle simulation*).
5. **Render flags** from the particle flags as in the flag table; the sprite image is the graphic name (types 0–2).
6. **Shape by type** (sizes: start = rand(start range), end = rand(end range); "Scale Height" multiplies the first
   dimension and "Scale Width" the second, both start and end):
   - Sprite / oriented sprite: billboard of first dimension = Height, second = Width (equal with Width Equals
     Height); size change per second = (end − start)/L for each dimension (0 with "No scaling"); rotation =
     rand(Start Angle); rotation speed = rand(Angle Vel). Oriented sprites face along forward / right / up /
     −forward for Orientation 0 / 1 / 2 / 3 instead of the camera.
   - Line: a beam from P to an end point. Point2 Source 0 "Dir of Velocity": end = P + n·rand(PointEndScale),
     n = direction of v (of a if |v| < 0.1); in addition T = rand(PointEndScale)/|v| (a second draw) is fixed at
     birth and every step the end is re-placed at the particle's predicted position T seconds ahead
     (start + v·T + a·T²/2). Source 1 "From Server": end = the effect's second position. Source 2 "End of Prev":
     not implemented — the end point is the world origin (0, 0, 0) (no stock file uses it). Source 3 "End2 is vel":
     end as for source 0 but both ends get zero velocity (only acceleration moves them) and no re-placing.
     Half-width at P = start of the first width range (×es with "Scale Height"), the second width (stored for the
     far end, ×es with "Scale Width"; no renderer use is known) = start of the second range; each changes by
     (end − start)/L per second. Both ends get the same v and a.
   - Ghoul object / persistent debris: the graphic is a Ghoul model; scale starts at rand(start), changes by
     (end − start)/L per second (not for type 4); initial angles = pitch/yaw of forward (degrees) +
     (rand(Forward Ang), rand(Right Ang), rand(Up Ang)); angular velocity (rand(Forward Avel), rand(Right Avel),
     rand(Up Avel)). Type 4 also never fades.

### Particle pool
1. Three fixed stores of 1024 records each: the **particle pool** (all free particles, emitter particles, beams,
   trail particles, Ghoul particles of type 3, invisible timers used by temp entities), the **decal ring** and the
   **debris ring**. The particle pool always has 1024 records; `cl_max_quads` does **not** limit it — it sets the
   decal ring size to min(`cl_max_quads`, 1024). The debris ring size is min(`cl_max_debris`, 1024). Both rings
   overwrite their oldest record when full.
2. Creating a particle fails (the particle is silently not created) when the pool is empty or when `cl_freezequads`
   or `freezeworld` is non-zero. While either cvar is set, particles, decals and debris also stop thinking and are
   never freed, but are still drawn ("frozen").
3. A new particle starts with colour/alpha 255, no image, no motion, spawn time = now.
4. Lifetime setting: a particle gets die time = now + lifetime. A requested sprite outside the client sprite list
   (SPEC-401) gives no image and dies at once. A particle created with the "one frame" marker gets an effectively
   infinite lifetime (5000 s) and is instead killed right after it has been submitted for drawing once.
5. Freeing happens once per rendered frame, before submission: a pool particle is freed when its die time ≤ now,
   when its alpha has dropped below 2, or when it has no image and is neither a timer nor a Ghoul particle. A freed
   Ghoul particle's model instance is destroyed.
6. On map change, disconnect or video restart all three stores are emptied (and the sprite name cache cleared).

### Particle simulation
Each effects step runs, in this order: the decal ring (growth, entity cull), the debris ring, then every pool
particle's behaviour, followed by the attachment update of attached particles. Every behaviour integrates with
the full step length dt = 1/`fx_sim_time`. "Collision" below applies only to emitter particles with "Apply Physics"
(or debris with a collision class).

1. **Billboards (sprite, oriented sprite, also temp-entity puffs)**, per step:
   1. Move (if it has a velocity): attached particles move their local offset by dt·v; with collision see
      *Collision*; otherwise position += dt·v.
   2. Accelerate (if it has an acceleration): v += dt·a. With "Decelerate to a stop": if the new speed is greater
      than the old speed, the particle stops moving and accelerating (it freezes in place).
   3. Rotate: angle += dt·rotation speed, wrapped to [0, 2π); the cosine/sine come from a 512-entry quarter-sine
      table (sin(k·π/1024), k = 0..511, index truncated) — any accurate sine is acceptable.
   4. Size: first dimension += dt·rate1, second += dt·rate2; if either becomes negative it is set to 0 and the
      particle dies.
   5. Alpha and colour (item 5 below).
   6. Particles with particle flag 0x200 get a new random rotation in [0, 6.28) every step.
   7. Trails (item 6 below).
2. **Lines (beams)**: accelerate both ends first (v1 += dt·a1, v2 += dt·a2; decelerate rule on the start), then
   move the start (with collision: any hit kills the line, after the collision effect) and then the end; widths
   change by their rates per second (a negative start width kills the line); alpha/colour; then a line with Point2
   source "Dir of Velocity" places its end at start + v·T + a·T²/2 with T = PointEndScale/|v₀| fixed at birth;
   then trails. Note that beams accelerate before moving while billboards move before accelerating.
3. **Ghoul particles (types 3 and 4) and debris**: accelerate (decelerate rule), move (collision with the particle
   box/elasticity when "Apply Physics", else with the debris collision class, else free), then angles += dt·angular
   velocity (degrees), scale += dt·scale rate (when non-zero), alpha (when its rate is non-zero) clamped 0..255 and
   applied as the model tint (1, 1, 1, alpha/255); trail effects as below (no trail particles). After every move
   that was collision-checked and did not end in liquid, the angular velocity is multiplied by 0.9; when the
   velocity has come to rest (see *Collision*) the object stops completely (no acceleration, no spin, no collision
   class).
4. **Collision** (box trace from the position to position + dt·v with the particle's bounding box, against the world
   and brush entities, or against all solid entities with "Collide with Entities"):
   - zero velocity: nothing moves;
   - no hit (fraction > 0.99), or the end point is in liquid: the particle moves to the end point;
   - hit: the particle does **not** move this step; v := e·(v − 1.8·(v·n)·n) with n the surface normal and e the
     elasticity; if every component of the new v truncated to an integer is within −29..29, v := 0 (at rest).
   - A hit on a sky or no-draw surface kills the particle silently. Otherwise, if the particle has a collision
     effect (`fx_collide`), that effect is started (source class 4) at the particle's position, axes from the
     reversed surface normal (UNVERIFIED sign); with "Remove on collision" (billboards) the particle then dies.
   - With "Collide with water": a particle whose trace ends in water, slime or lava starts its collision effect
     and dies. (UNVERIFIED: the traces used do not ask for liquid contents, so this case and the liquid
     pass-through above may never happen in 1.07f.)
5. **Alpha and colour.** Let t = (now − spawn)/(die − spawn) + 0.01, A0 = Alpha Min Ratio × 255 and R the value
   stored at birth (rate for styles 0/1, peak for styles 2/3).
   - Alpha style 0 or 1 (also every particle not made by an emitter): alpha += dt·R.
   - Style 2 "L Rise,Fall": alpha = A0 + (R − A0)·2t for t < 0.5, else A0 + (R − A0)·2(1 − t) (t not clamped).
   - Style 3 "TrigRiseFall": alpha = A0 + (R − A0)·sin(π·min(max(t, 0), 1)).
   - The result is truncated and clamped to 0..255.
   - Colour style 2 "Linear Slide": f = clamp(t, 0, 1); style 3 "Trig Slide": f = sin(clamp(t, 0, 1)·π/2); each
     channel = trunc(min·(1 − f) + max·f) using the particle description's colour ranges. Styles 0/1 keep the birth
     colour.
6. **Trails.** A particle with a trail effect, or with "Leave Fade Trails"/"Leave Shrinking Trails", has a trail
   timer (initially its spawn time). When now − last trail time > 1/Trail Frequency:
   - if it has a trail effect (`fx_trail`): start that effect (source class 3) at the particle position with
     forward = direction of motion (right/up perpendicular to it); the effect **follows the particle** and detaches
     when the particle dies; the timer is reset;
   - otherwise (same timer, so a particle with both only spawns the effect), for billboards: a copy of the particle
     (position, size, rotation, colour, image, render flags without the trail bits) that does not move and lives
     Trail Life Time seconds. Fade trail: the copy's alpha falls by 2 × its alpha per second (gone after 0.5 s,
     regardless of Trail Life Time). Shrinking trail only: both dimensions shrink by (parent width / Trail Life
     Time) per second. For lines the copy is a line with the same ends and width; fade: alpha rate = −alpha /
     Trail Life Time; shrink: width rate = −width / Trail Life Time.
7. **Attached particles** (emitter with "Attached To Source"): after each step the world position is recomputed
   as origin + x·forward + y·right + z·up of the effect's current origin and axes, where (x, y, z) is the
   particle's local offset, which its velocity moves. For an attached line the end point is recomputed the same
   way from the previous end − start difference (which is a world vector re-read as local coordinates every step,
   so attached lines spin unless the axes are the identity; UNVERIFIED in game, an implementation may keep the
   end offset in local coordinates instead).

### Rendering hand-off
Submission happens once per rendered frame **before** the simulation steps of that frame, so the picture shows
the state after the previous frame's steps. Order (relevant for the renderer limits of 1024 alpha quads and
256 quad chains, SPEC-002):
1. Decal ring records that are alive, in slot order, as alpha quads.
2. Debris ring: starting at the next slot to be reused and going from newest to oldest, alive debris are added as
   renderer entities until `fx_maxdebrisonscreen` + 1 have been added; every further alive debris record is
   destroyed. (This is a global cap on live debris, not a visibility test.)
3. Pool particles not owned by an emitter, alive, not timers: Ghoul particles as renderer entities, others as
   alpha quads; "one frame" particles die after this submission.
4. Dynamic lights, then light styles.
5. One quad chain per running emitter: particles whose die time has passed are removed from the chain; particles
   carrying the renderer's level-of-detail "dropped" mark (alpha-quad flag 0x0001, set by the renderer when
   `gl_effectfill` is exceeded) are killed unless the particle description has "Apply Physics"; the chain's
   bounding box is computed from particle origins (both ends for the first line; UNVERIFIED for later lines); the
   coverage weight is Σ over particles that live at least 0.05 s more, are not marked and are not Ghoul particles
   of: billboard w·h, oriented billboard w·h/2, line 5 × half-width; Ghoul particles are added as renderer
   entities; the chain itself is added unless its first particle is a Ghoul particle.

### Entity effect slots
Each packet entity has four effect slots (SPEC-202: effect index, bolt, scale × 16). Every frame, for an entity
whose effects field has none of the bits 0x8001: for each slot with a non-zero effect whose next-start time has
been reached, start configstring effect `index` with origin = the entity's interpolated origin, axes from the
entity's angles, following the entity (bolt 0) or attached to that bolt of the entity's ghoul instance, scale =
value/16 when non-zero; then next-start = now + rand(repeat delay of that effect). Next-start times start at 0, so a
slot fires as soon as it is filled (UNVERIFIED whether they are reset when an entity number is reused). Effects with
repeat delay (0, 0) therefore restart every frame.

### View weapon (local player)
1. Animation notes of the in-view weapon model call the client hooks of SPEC-003. Note `effect <name> [<bolt>]`:
   only while the weapon model is shown and the field of view is exactly 95 (not zoomed); the bolt defaults to
   `flash`; a name containing `exhaust` uses bolt `exhause_l` (sic). If the bolt exists on the view model, effect
   `<name>` (by name) starts attached to that bolt of the view model, following it.
2. Predicted fire effects (hooks 2 and 3 of SPEC-003), skipped entirely when `fx_noflash` ≠ 0. They receive
   code = weapon id − 1 (primary) or weapon id + 11 (alternate); alternate codes do nothing.
   - Code 5 (sniper primary) while zoomed (fov ≠ 95): start effect `weapons/playermz/sniper_alt`.
   - Shell-casing sounds: trace from the view origin 256 units down; if it hits a surface whose material (SPEC-401
     table) has impact sound 37 (stone) or 39 (wood), or the metal impact sound, play a casing sound at the hit point
     (no entity, channel 0, volume 1.0, attenuation 2.0) delayed by a random time offset: codes 1–4 (pistols, machine
     pistol, assault rifle): 50 % chance, delay rand(0.4, 0.6), client sound 97 or 98 (Tink/Gen) on stone/wood or
     94..96 (Tink/Metal) on metal; codes 5 and 8 (sniper rifle, machine gun): always, delay rand(0.4, 0.7), sounds
     111..113 (Tink/hvy); code 7 (shotgun): 75 % chance, delay rand(0.4, 0.8), sounds 108..110 (Tink/shot). Other
     surfaces: no casing sound. Client sound numbers refer to the list in SPEC-401.
3. Zoom sound: when the zoom changes, `Weapons/Sniper/Zoom.wav` is played locally, at most once per 0.18 s.

### Camera shake
A view shake value S (0..200) and a decay rate R live in the client. Sources: feedback primitives (above, R = 80)
and shake entity events (SPEC-401, R = 4 × parameter). Every rendered frame while S > 0.02: view pitch and yaw each
change by S × 0.04 × u, u = (irand-like integer in −50..49)/100 from the C runtime generator; view origin x and y
each change by S × 0.01 × w, w integer in −2..2; then S decreases by R × frame time.

### Decals (marks)
A decal is added with: an impact description (point, surface normal, hit kind, material, surface flags, BSP leaf;
SPEC-201 impact), a sprite, an RGB colour, an alpha, a start size, an end size (−1 = no growth), a growth
acceleration and a growth time.
1. Refused when: the impact is on the world (kind 0) and the surface is sky, warping or flowing (surface flags 0x04,
   0x08, 0x40); the impact is on a brush entity (kind 2) whose material is not 37; the impact kind is 1 or 3; or
   particles are frozen. Two consumers also skip decals when the impact's surface flags have 0x84 (SPEC-401).
2. The decal takes the next decal-ring slot (oldest overwritten). It never expires by time; it lives until its
   slot is reused, the map changes, or — for decals on a brush entity — that entity is missing from the current
   server frame.
3. It is an alpha quad with flags billboard + oriented (normal = impact normal) + lit + decal depth offset + drawn
   without sorting (0x0002 | 0x0010 | 0x0008 | 0x0100 | 0x0040), rotation 0, light probe direction = −normal,
   leaf = impact leaf, colour = RGB, alpha = trunc(alpha), both dimensions = size.
4. **Fitting**: if the size (or the end size) is greater than 3, the decal is shrunk so it does not hang over an
   edge: with p a vector perpendicular to the normal and q = normal × p, for each of the four directions ±p, ±q
   it tries scales s = 1.0, 0.8, 0.6, 0.4, 0.2, 0.1 and finally a very small value, tracing from impact point +
   s·size·direction to that point − 2·normal; the first s whose trace hits (fraction ≤ 0.99) is that direction's
   limit; size := the smallest s·size. With growth, the larger of start/end size is fitted and the other clamped to
   it.
5. **Growth** (end size ≠ −1): during growth time T after creation, size changes with an initial rate
   r0 = (end − T²·accel/2 − start)/T and constant acceleration accel, i.e. size(τ) = start + r0·τ + accel·τ²/2,
   which reaches the end size at τ = T. With T ≤ 0 there is no growth. Width and height grow together.
   The decal primitive of an `.eft` passes start size, end size, acceleration = the field at 0x50 and time = the
   field at 0x54 (see the primitive table: the editor labels of these two fields are swapped). Example
   (`environ/puddle.eft`): start 2, end 25, acceleration −3.1, time 3.0 → r0 = (25 + 9 × 3.1/2 − 2)/3 = 12.3167,
   size(1 s) = 2 + 12.3167 − 1.55 = 12.767, size(3 s) = 25.
6. Decals made by `.eft` decal primitives use colour white, alpha 250.

### Debris
1. Debris are Ghoul model pieces in the debris ring. Models (directory, file; number of selectable parts):
   0 chunks/brick (3), 1–3 chunks/glass small/medium/large (3), 4–6 chunks/metal small/medium/large (4), 7–10
   chunks/rock small/medium/large/tiny (4), 11–14 chunks/wall small/medium/large/tiny (4), 15–17 chunks/wood
   small/medium/large (3), 18 shell casing, 19 explosion (explode80), 20 explosion (explode20), 21 soda can,
   22 blowing paper (1), 23 mushroom explosion; exact file names in SPEC-401. One random part of the model is shown.
2. Models are registered when first used; at level start the client pre-registers rock small, rock tiny and the
   shell casing always, glass small if the map uses materials 37/38, wall small if 32/38/40, wall tiny if any of
   14..24, soda can if 39, paper if 41.
3. Debris never expire by time: they live until their ring slot is reused, they hit a sky/no-draw surface, the
   on-screen cap (above) destroys them, or the map changes.
4. Motion uses the Ghoul-particle behaviour with a **collision class** (box, elasticity): 1 point/0.7, 2 ±2/0.7,
   3 ±10/0.7, 4 point/0.95, 5 (−4, −2, −3)–(4, 2, 3)/0.7, 6 point/0.7, 7 ±5/0.7, 8 ±10/0.7; collision is against the
   world and brush entities only.
5. Temp-entity chunks (SPEC-401 type 18) use: per chunk origin = position + irand(−e, e) per axis with
   e = trunc(0.85 × extent byte); angles 0; angular velocity irand(−900, 900) degrees/s per axis; velocity and
   gravity by material: flesh (35): (irand(−100, 100), irand(−100, 100), irand(−10, 100)), gravity −400; glass (37)
   with a direction: d = direction + rand(−0.05, 0.05) per axis, v = d·rand(50, 100) + (0, 0, rand(100, 300)),
   gravity −400; glass without direction: as flesh; all others: irand(−300, 300) per axis, gravity −800. Collision
   class: rock types 12–16 use 6/7/8 for size 1/2/3, all others 6. While a chunk slows down below 80 units/s its
   pitch and roll are scaled by speed/80; below 0.1 they become 0 and it lies flat.
6. Entity "shatter" (entity effects bit 0x400): every flagged part of the entity's model (including bolted-on
   models) becomes a debris piece at the part's position with velocity 100 × direction from the entity origin to
   the part ((0, 0, 100) if they coincide), angular velocity irand(−900, 900), gravity −800, class 1.

### Entity effects bits handled by the effects system
Per packet entity each frame (SPEC-202 effects field). For entities with bit 0x0001 or 0x8000 the four effect slots
are not started (see *Entity effect slots*); their twelve 16-bit values (slot0 effect, slot0 bolt, slot0 scale,
slot1 effect, …) carry other data, named v0..v11 below.
- 0x0001 rotate: angular velocity w = (v1, v2, v3) as signed integers. If v4 = 0 the entity's drawn angles are its
  network angles + w × (client time in ms)/512 (so w is in 1/512 degree per millisecond). Otherwise v4, v5, v6, v7
  are ghoul instance handles (0 = none) and each existing instance is rotated incrementally every frame by
  w × frame time (s)/512 (UNVERIFIED unit; this branch turns 1000 times slower than the first).
- 0x0400 shatter (see *Debris*).
- 0x0800 pulsing tint: s = sin(5·t) (t = client time, s), model tint (0.6 − 0.3s, 0.8 − 0.3s, 1, 0.6 + 0.2s); for the
  local player's entity the view-weapon and player models are tinted too.
- 0x8000 fade (Ghoul entities only): start = v0 + 65536·v1 (milliseconds, 32-bit), d = v2 (signed, ms). d > 0: alpha
  = 1 − (t − start)/d until start + d, then 0 (fade out); d < 0: alpha = (t − start)/|d| until start + |d|, then 1
  (fade in); d = 0: no change. The model tint becomes (1, 1, 1, alpha).

### Dynamic lights
Quake II behaviour (32 lights, time base = client time in ms) with these differences:
1. A light is created with key, origin, colour as an RGBA byte quadruple (each channel/256, so 255 → 0.996),
   radius, lifetime in ms and a decay per second (all in one call).
2. Allocation: a light with the same non-zero key is reused; else the first expired light; else light 0.
3. Every client frame: radius −= frame time × decay (not below 0). A light whose die time has passed is cleared —
   and, as in Quake II, the remaining lights are not decayed in that frame.
4. Lights with non-zero radius go to the renderer (origin, radius, colour); minimum light is unused.

### Light styles
As Quake II (256 styles; the pattern string from configstring CS_LIGHTS+i, 'a' = 0 … 'm' = 1.0 in steps of 1/12;
a string of 64 or more characters is a fatal error `svc_lightstyle length=<n>`; value = pattern[trunc(client
time × 10) mod length], empty pattern = 1.0) with one exception: **style 13** takes its RGB value from configstring
5 parsed as three floats "%f %f %f" (its own pattern is ignored; if configstring 5 is missing or unparsable the
previous value stays — black after a level change, UNVERIFIED in game). A **style-13 flash** (lightning, SPEC-401
type 16) replaces style 13 for a duration with value = flash·f + base·(1 − f), f = (end − now)/(end − start), i.e. it
fades linearly from the flash colour back to the configstring-5 colour.

### Editor
1. With `fx_edit` ≠ 0 the client shows a text page for the effect being edited each step and applies the editor
   cvars. Main page rows: "Add New Effect" (up to 10 primitives), "Duration" Instant/Continual, repeat delay
   min/max (steps of 0.05, clamped 0..100 s), then per primitive: type (cycles Effect/Sound/Light/Decal/Feedback),
   trigger delay min/max (steps of 0.05, clamped 0..100), "Edit" (opens the primitive's page). Sub-pages show the
   labels used in the field tables above. The menu `fxgraphic` lets the user pick the particle graphic.
2. `fx_run` ≠ 0 starts the edited effect once at (0, 0, 32) with default parameters; a Continual effect is
   restarted every rand(repeat delay) seconds while the editor is on.
3. `fx_save` writes file version 3 with emitter version 3 and particle version 7 records, every name field as
   the full 64-byte buffer, and the trailer equal to the in-memory repeat-delay limit.

## Constants
| Name | Value |
|---|---|
| Configstring effect table / named effect table | 256 / 256 entries |
| Running effects | 256 |
| Primitives per effect | 10 |
| Maximum file / emitter / particle / decal record versions | 3 / 3 / 7 / 1 |
| Default simulation rate | 60 steps/s; catch-up limit 0.2 s |
| Emitter single-burst threshold | delay < 0.01 s |
| Duration clamp | 0.005 .. 99999 s |
| Distance LOD radius | 512 units (squared 262144) |
| Trace lengths: emitter styles 6–8, decal primitive / svc_effect flag 0x20 | 512 / 1024 |
| Curve-to-point factor | −1.9 |
| Camera shake cap / feedback decay / feedback range | 200 / 80 per s / 600 units, factor 200 |
| Force-feedback ids | 0 none, 1 RandomNoise, 2 AircraftCarrierTakeOff, 3 BasketballDribble, 4 CarEngineIdle, 5 ChainsawIdle, 6 ChainsawInAction, 7 DieselEngineIdle, 8 Jump, 9 Land, 10 MachineGun, 11 Punched, 12 RocketLaunch, 13 SecretDoor, 14 SwitchClick, 15 WindGust, 16 WindShear, 17 Pistol, 18 Shotgun, 19–24 Laser1–6, 25 OutOfAmmo, 26 LightningGun, 27 Missile, 28 GatlingGun, 29 ShortPlasma, 30 PlasmaCannon1, 31 PlasmaCannon2, 32 Cannon |

## Test vectors / how to verify
1. Data check: with the layout above, all 211 stock `.eft` files are consumed exactly (same result as SPEC-142).
2. `effects/exphit.eft`: version 3, Instant, repeat delay (0, 0), one feedback primitive with no shake and
   force-feedback id 8 (Jump) → starting it produces no visible or audible effect on a machine without a
   force-feedback device.
3. `weapons/world/rocketexplode.eft`: primitive 0 is a light of radius 500, colour (255, 205, 205), lifetime
   0.105 s; primitive 7 is a feedback with shake 0.4; primitive 8 a decal `textures/sprites/scorch` size 32 traced
   forward. In game, a rocket explosion within 600 units shakes the view and leaves a scorch mark on the surface it
   faces (verify visually).
4. `effects/canthear.eft`: one emitter, style 0, 1 element, delay 0.05, duration (0, 0) clamped to 0.005 s →
   exactly one particle: sprite `textures/sprites/snow`, 8 × 8 (Width Equals Height, additive), lifetime 1.5 s,
   velocity (rand(−10, 10), rand(−10, 10), rand(−10, 10)) on the effect axes, alpha style 3 with peak 155 and min
   ratio 0 (alpha = trunc(155·sin(π·(age/1.5 + 0.01)))), colour style 2 sliding linearly from (255, 255, 5) to
   (255, 5, 55).
5. Emitter count with distance LOD: N = 10 at squared distance 393216 (= 512² × 1.5) gives trunc(10 × 0.5 + 1) = 6.
6. Ring style, N = 4, φ = 0: elements at angles 90°, 180°, 270°, 360° around the origin (first element at 90°).
7. Bounce: v = (100, 0, −200) hitting a floor (n = (0, 0, 1)) with elasticity 0.7 → (70, 0, 112); the particle
   stays where it was for that step. v = (20, 0, −25) → (14, 0, 0.7·20) = (14, 0, 14) → all components within
   −29..29 → v = 0 (rest).
8. Alpha style 2, peak 200, min ratio 0, particle at a quarter of its life: t = 0.26 → alpha = trunc(200 × 0.52) = 104;
   at three quarters: t = 0.76 → trunc(200 × 0.48) = 96.
9. Decal growth: see *Decals* item 5 (puddle example).
10. Simulation steps: client time advances from 10.000 to 10.050 s with fx time 10.000 and `fx_sim_time` 60 → three
    steps run (fx time 10.0167, 10.0333, 10.050 after clamping); each integrates 1/60 s.

## Open questions
- Alpha-quad flag 0x0001: the effects system uses it to mean "draw once, then destroy" (One Frame particles and the
  renderer's level-of-detail drop mark); SPEC-002 calls it "hidden". If the renderer skipped such quads, One Frame
  particles (35 stock emitters, mostly muzzle flashes) would never be visible — verify in ref_gl or in game.
- Liquid handling of particle collisions (the traces may never report liquid contents).
- Exact visual result of the attached-line end-point update.
- Visual meaning of bolt axis order (svc_effect flag 0x40) — verify with a bolt-attached effect in game.
- Whether force-feedback ids 10/20/22/28 depend on a device option (needs a force-feedback device).
- Effects bit 0x4000 meaning on the game side (SPEC-202 catalogue).

### Notes for other specs (found while writing this one)
- SPEC-201 §5 says svc_effect flags 0x10 and 0x20 are never read: the client does read 0x20 (second position = trace
  end point along forward, 1024 units; see *Effect parameters*). 0x10 is indeed unused.
- SPEC-201 §1: the client of type 16 does not tint the screen; it flashes light style 13 (SPEC-400 *Light styles*).
- SPEC-302: `cl_max_quads` sizes the decal ring (not particles); `fx_sim_time` is the effects step rate (steps/s);
  `cl_freezequads` freezes particles, decals and debris; `fx_noflash` disables the local predicted fire effects.
- SPEC-002: alpha-quad flag 0x0001 — see the first open question above.
