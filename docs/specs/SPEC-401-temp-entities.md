# SPEC-401: Temp entities, surface impact effects and entity events — client side
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (client), version 1.07f
Depends on: SPEC-001 (random generator), SPEC-002 (alpha quads, dynamic lights), SPEC-201 (svc_temp_entity payloads,
impact descriptor, svc_culledEvent), SPEC-202 (entity events, effects bits), SPEC-400 (particles, decals, debris,
dynamic lights, light style 13 flash, `.eft` effects, camera shake)

## Purpose
What the client draws and plays for each of the 65 temp-entity types (svc_temp_entity, SPEC-201 §1), for the 23
entity ("attached") events, and for bullet/knife/explosion impacts on the 43 surface materials. The payloads are in
SPEC-201; this spec gives the resulting particles, decals, debris, lights and sounds with their constants, so a
reimplementation looks and sounds the same.

Notation (in addition to SPEC-400):
- rand(a, b) / irand(a, b): the shared generator of SPEC-001 (float in [a, b), integer in [a, b]).
  crand: the C runtime generator (separate state, 0..32767), written as "c%N" for crand() mod N.
- A **puff** is a particle (SPEC-400 particle pool) defined by: sprite (client sprite list below), size w×h, RGBA,
  lifetime (ms), blend (normal, additive = alpha-quad flag 0x0200, darkening = 0x0400), lit (0x0008), velocity v,
  acceleration a, rotation r and rotation speed ω (radians, radians/s), growth g (units/s added to both
  dimensions; a negative size kills the particle), alpha rate α (per second; alpha clamped 0..255; a particle whose
  alpha drops below 2 is freed). Unless stated: rotation 0, no motion, no growth, no alpha change, normal blend, not
  lit. "fade over its life" means α = −alpha/lifetime.
- A **spark** is a line particle (beam, SPEC-002) from p to p + d·len with half-width hw, velocity and gravity
  applied to both ends.
- Sounds: S(index, vol, atten, entity, channel) = positional sound of the client sound list entry at the given point;
  entity −1 = no entity. Unless stated volume 1.0, attenuation 1.0, entity 0, channel 0, no time offset.
- Decal: SPEC-400 *Decals* (sprite, colour, alpha, size, end size, growth).
- Light: SPEC-400 *Dynamic lights* (colour bytes, radius, lifetime ms, decay per second, key 0).

## Interface / layout

### Client sound list (index → sound file name as registered, `.wav` appended)
| Index | File |
|---|---|
| 0–1 | Ambient/Gen/Electric/spark1, spark2 |
| 2–5 | player/foot/metal/metal1..4 |
| 6–9 | player/foot/snow/snow1..4 |
| 10–13 | player/foot/water/water1..4 |
| 14–17 | player/foot/grass/grass1..4 |
| 18–21 | player/foot/gravel/gravel1..4 |
| 22–25 | player/foot/sand/sand1..4 |
| 26–29 | player/foot/wood/wood1..4 |
| 30–33 | player/foot/stone/stone1..4 |
| 34–36 | Weapons/FX/Rics/ric1..3 |
| 37–43 | Impact/Surfs/stone, water, wood, grass, gravel, sand, snow |
| 44 | placeholder (never loaded; metal impacts use 79–81) |
| 45–47 | Impact/Gore/impact1..3 |
| 48–51 | Weapons/FX/FlyBy/FlyBy1..4 |
| 52 | Weapons/Glock/fire |
| 53 | ambient/Nature/weather/thnder1 |
| 54 | weapons/FlshGren/FlshExp |
| 55 | Weapons/Autoshot/fire |
| 56–57 | Enemy/MGun/trnslow, trnstop |
| 58 | weapons/C4/C4Exp |
| 59–61 | Impact/Chair/Squeak1..3 |
| 62–65 | Ambient/Locs/Subway/Train/Brake, Horn2, Impact, Move |
| 66–68 | Weapons/Knife/Swing1..3 |
| 69 | Weapons/Knife/Impact |
| 70 | Weapons/Knife/Throw |
| 71–78 | Player/Land/Grass, Gravel, Metal, Sand, Snow, Stone, Water, Wood |
| 79–81 | impact/surfs/metal1..3 |
| 82–84 | Impact/GlassBreak/GBBig, GBMed, GBSmall |
| 85–87 | Impact/Break/Metal, Stone, Wood |
| 88 | Ambient/Gen/Electric/buzz |
| 89–91 | Impact/Explosion/Big, Med, Small |
| 92 | Ambient/Models/Phone/Hit |
| 93 | Ambient/Models/TrashCan/LidSpin |
| 94–96 | weapons/FX/Tink/Metal1..3 |
| 97–98 | weapons/FX/Tink/Gen1..2 |
| 99–101 | Impact/Gore/Fall1..3 |
| 102–104 | weapons/FX/Crack/Crack1..3 |
| 105–107 | enemy/Helicopter/Fire1..3 |
| 108–110 | weapons/FX/Tink/shot1..3 |
| 111–113 | weapons/FX/Tink/hvy1..3 |
Sounds are registered on first use; the list is forgotten at every level start.

### Client sprite list (index → image `textures/sprites/<name>`)
0 smoke2, 1 bldglob4, 2 bldglob5, 3 bldglob6, 4 blood_pool2, 5 footstep, 6 footstp2, 7 lit, 8 ring1, 9 dirt,
10 sparkpart, 11 water, 12 powder, 13 bhole2, 14 lightning, 15 sparkpartblue, 16 flare, 17 waterdrop, 18 dent, 19 rain,
20 mflash, 21 shock2, 22 shock3, 23 shock4, 24 boom3, 25 boom5, 26 scorch, 27 toxic_ooze, 28 explode1, 29 scorchwht,
30 snow, 31 bubble, 32 pipeleft, 33 bhole_glass3, 34 slash, 35 ring2, 36 beam, 37 flash1, 38 mflash3, 39 mflash4,
40 flare2, 41 smoke3, 42 bhole_mtl, 43 bhole_wd, 44–46 minimiflash1..3, 47–50 expfire1..4, 51 smkgrn, 52 smkgry,
53 smkwht, 54 firestreak, 55 whitestreak, 56 puddle. All 57 are registered at level start.

### Debris chunk types (temp entity 18 "chunk type")
| Type | Material group / skin | Model (SPEC-400 debris model index) |
|---|---|---|
| 0 | treated as 8 | |
| 1–6 | Brick: brwn_lt, brwn_dk, brwn_lt, grey_lt, grey_dk, red | 0 (size ignored) |
| 7 | Glass / glass | 1 + size − 1 |
| 8–11 | Metal: light, dark, rusty, shiny | 4 + size − 1 |
| 12–16 | Rock: brwn_lt, brwn_lt, brwn_dk, grey_dk, grey_dk | 7 + size − 1 |
| 17–20 | Wood: brwn_lt, brwn_dk, grey_dk, grey_lt | 15 + size − 1 |
| 21–31 | Wall: black, brown, dbrown, lbrown, grey, dgrey, lgrey, green, orange_cone, red, white | 11 + size − 1 |
| 32 | Rock / flesh | 7 + size − 1 |
| 33 | Wall / straw | 11 + size − 1 |
| 34 | Rock / snow | 7 + size − 1 |
Each chunk shows one part chosen by irand(0, n) among the model's n part names (CHUNK_1..3; GLASS01..03; METAL1..4;
ROCK1..4; _LITTLECHUNK, _SMALLCHUNK, _MEDIUMCHUNK, _LARGECHUNK; WOOD1..4). Because the upper bound is inclusive the
value n selects a part that does not exist on the model (an invisible chunk) — original quirk.

### Surface materials (impact table; material id = SPEC-201 impact material 0..42)
Columns: mark colour RGBA (bullet-hole decal colour, dust colour); mark mode (0 none, 1 hard, 2 soft: footprints);
footstep sound group (4 variants, sound list); impact sound; landing sound; bullet-hole sprite; impact effect class
(see *Material impact effects*). Names are inferred from textures and effects (UNVERIFIED).
| Id | RGBA | Mode | Footsteps | Impact sound | Landing | Hole | Effect | Likely surface |
|---|---|---|---|---|---|---|---|---|
| 0 | 96 96 96 250 | 1 | stone | 37 stone | Stone | bhole2 | concrete | default stone |
| 1 | 250 250 250 250 | 1 | metal | 79–81 metal | Metal | bhole_mtl | metal | metal |
| 2 | 180 140 100 250 | 2 | sand | 42 sand | Sand | bhole2 | dust | sand |
| 3 | 200 200 200 250 | 2 | sand | 42 | Sand | bhole2 | dust | dirt |
| 4 | 180 130 70 250 | 2 | sand | 42 | Sand | bhole2 | dust | dirt |
| 5 | 120 90 20 250 | 2 | sand | 42 | Sand | bhole2 | dust | mud |
| 6 | 160 160 160 250 | 1 | gravel | 41 gravel | Gravel | bhole2 | dust | gravel |
| 7 | 120 90 20 250 | 1 | gravel | 41 | Gravel | bhole2 | dust | gravel |
| 8 | 180 140 30 250 | 1 | gravel | 41 | Gravel | bhole2 | dust | gravel |
| 9 | 230 230 230 150 | 2 | snow | 43 snow | Snow | bhole2 | dust | snow |
| 10 | 0 200 200 250 | 0 | water | 38 water | Water | bhole2 | water | water |
| 11 | 0 100 50 250 | 0 | water | 38 | Water | bhole2 | splat green | slime |
| 12 | 190 100 40 250 | 0 | water | 38 | Water | bhole2 | splat orange | lava |
| 13 | 180 140 20 250 | 0 | water | 38 | Water | bhole2 | splat brown | mud |
| 14 | 180 140 60 250 | 1 | wood | 39 wood | Wood | bhole_wd | concrete | wood |
| 15 | 120 90 30 250 | 1 | wood | 39 | Wood | bhole_wd | concrete | wood |
| 16 | 80 80 80 250 | 1 | wood | 39 | Wood | bhole_wd | concrete | wood |
| 17–24 | 180 180 180 / 90 90 90 / 180 140 50 / 130 90 30 / 240 240 240 / 80 140 80 / 120 20 20 / 10 10 10 (alpha 250) | 1 | stone | 37 | Stone | bhole2 | concrete | stone/brick/tile colours |
| 25 | 20 120 20 250 | 0 | grass | 40 grass | Grass | bhole2 | concrete | grass |
| 26 | 120 120 20 250 | 0 | grass | 40 | Grass | bhole2 | concrete | hay |
| 27 | 100 25 25 250 | 0 | water | 38 | Water | bhole2 | splat red | blood/sewage |
| 28 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_mtl | metal | metal |
| 29 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_mtl | pipe water | water pipe |
| 30 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_mtl | pipe oil | oil barrel/pipe |
| 31 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_mtl | pipe chemical | chemical tank |
| 32 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_mtl | computer | computer |
| 33 | 180 140 50 250 | 2 | stone | 37 | Stone | bhole2 | dust | soft stone |
| 34 | 180 180 180 250 | 2 | stone | 37 | Stone | bhole2 | dust | soft stone |
| 35 | 180 180 180 250 | 0 | 37 (impact sound ×4) | 37 | Stone | bhole2 | flesh | flesh |
| 36 | 23 25 25 250 | 0 | water | 38 | Water | bhole2 | splat black | tar |
| 37 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_glass3 | glass | glass |
| 38 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_glass3 | lit panel | lit glass panel |
| 39 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_glass3 | vending | vending machine |
| 40 | 240 240 240 250 | 1 | wood | 39 | Wood | bhole_wd | paper | paper/painting |
| 41 | 23 25 25 250 | 1 | metal | metal | Metal | bhole2 | paper burst | paper pile |
| 42 | 250 250 250 250 | 1 | metal | metal | Metal | bhole_mtl | vent | radiator/vent |
A material id above 42 prints `Surface out of range!!!` and the impact is dropped.

## Behaviour

### Dispatch
1. svc_temp_entity: the type byte selects one of 65 handlers (0..64); a type above 64 must be rejected (the 1.07f
   client has no check and crashes). Each handler reads exactly the payload of SPEC-201 §1 and then produces the
   effect below at once (temp entities have no state beyond the particles, decals, debris, lights, sounds and effects
   they create).
2. Entity events: for each packet entity of a new frame, its up to three event ids (low 7 bits) are dispatched with
   (entity, event parameter). svc_culledEvent dispatches its event without an entity, at view origin + direction ×
   distance × 4. Ids 0 and 23 have no handler (must not be sent); ids 1, 2 and 15 do nothing; every handler below
   needs an entity, so culled events produce nothing in 1.07f.

### Impact handling (shared by temp entities 6, 7, 24, 33, 34, 61, 62 and footsteps)
An **impact** is the decoded SPEC-201 impact descriptor plus: a mark mode (4 bullet, 5 explosion, other =
footprint/dent), a power (float), a hole sprite (default: the material's bullet-hole sprite), and for footprints an
alpha factor and a yaw. Kind 0 is re-traced from point + 5·normal to point − 5·normal against the world to find the
surface (material, flags); if that trace hits an entity instead the impact becomes kind 3. Kind 1 has normal
(0, 0, 1), no material, no flags. Kind 2 converts the local offset and normal with the entity's interpolated yaw
(pitch and roll ignored) and origin. Kind 3 takes the material from the payload and uses the raw horizontal offset ×
0.001 as "normal".

**Mark and impact sound** — applied first, for any impact whose material is 0..42 and whose surface flags have
neither 0x04 (sky) nor 0x80 (no-draw):
- mode 4 (bullet): v = min(1, 0.4 + power × 0.053333) (v = 0.6 when power is 0). If power > 1.5 or power = 0, and the
  material is not 35 (flesh): play the material's impact sound at the point (material impact sound 44 = random of
  79–81), volume v. Then power is capped at 3; if a hole sprite is set: decal (hole sprite, material colour, alpha
  irand(200, 240), size power × 0.6 + rand(0, 0.1), no growth); if the impact is on an entity (number 0..1023) the
  decal belongs to that entity (disappears with it).
- mode 5 (explosion): decal (scorch, colour (230, 230, 230), alpha 230, size rand(32, 36)).
- other modes: only if the material's mark mode is 2 (soft): decal (impact sprite, material colour, alpha =
  alpha factor × 255, size 8), rotated to −π/2 − yaw (footprints, dents).
Decals obey SPEC-400 (world or glass brush entities only).

**Material impact effects** (after the mark; arguments: impact, direction d, integer power p; "n" = impact normal,
"modes 4/5" = only for bullet or explosion impacts). Effects started by name use SPEC-400 by-name lookup with
origin = impact point, forward = n, right = a vector perpendicular to n, up = n × right, unless stated.
| Class (materials) | Effect |
|---|---|
| dust (2–9, 33, 34) | **dust puffs** style 0 (below). Runs for any mode (also footsteps/landings). |
| concrete (0, 14–26) | modes 4/5: 3 puffs: sprite smoke2, lit, life 600, size p × 1.5, colour (200, 200, 200, 100), v[k] = (n[k] + rand(−0.1, 0.1)) × (c%32 + 16) + c%16 − 7, a = (0, 0, 40), r = (c%628)/100, ω = (c%628)/100, g = p × 1.6667, fade over life; then dust puffs style 2 with count 1 (p ≤ 2) or 2. |
| metal (1, 28) | modes 4/5: effect `environ/wallspark`. |
| water (10) | any mode: effect `environ/bulletsplash` with forward (0, 0, 1), right (1, 0, 0), up (0, 1, 0). |
| splats (11 green, 12 orange, 13 brown, 27 red, 36 black) | modes 4/5: effect `environ/splat<colour>` (green, orange, brown, red, black). |
| pipe (29 water, 30 oil, 31 chemical) | modes 4/5: `environ/wallspark`; then, at most once per 300 ms (client time; a backwards clock jump of ≥ 3 s also resets it) and only on a decal-capable surface: `environ/waterspurt` / `environ/oilspurt` / `environ/chemspurt` with forward = n, right = perpendicular of n if |n.z| > 0.9 else (0, 0, 1), up = forward × right. |
| vent (42) | modes 4/5: `environ/wallspark`; then (own 300 ms limit, decal-capable surface) `environ/metal_steam`. |
| computer (32) | modes 4/5, own 300 ms limit: `environ/metal_computer`; a **spark emitter** at point + 5n living irand(4000, 8000) ms; 6 **impact chunks** of material 18 with scale 50. |
| flesh (35) | any mode: 5 drops i = 0..4: sprite smoke2, lit, 3 × 3, colour (105, 0, 0, 100), life 600, v = (15i·n.x, 15i·n.y, 10i), a = (0, 0, −40i), r and ω = (c%628)/100, g = 1.6667i, fade over life. |
| glass (37) | any mode, own 300 ms limit: dust puffs style 2 (count 1 or 2), **glass shards** (scale 10, count irand(4, 6)), sound 84 (GBSmall) volume 0.6, entity −1, channel 5. |
| lit panel (38) | any mode, no limit: `environ/glass_computer`; glass shards (10, 4..6); 6 impact chunks material 18 scale 50; sound 84 volume 0.6, channel 5. |
| vending (39) | modes 4/5, own 300 ms limit, and only when no can burst happened in the last 3 s: spark emitter (4–8 s); `environ/wallspark`; with 25 % chance (c%20 < 5) irand(2, 4) soda cans from point + 5n along n with speed 50 (as temp entity 50) and a 3 s lock; independently 25 %: spark shower (as temp entity 49) at point + 5n along n. |
| paper (40) | sound 39 (Impact/Surfs/wood) volume 0.6, entity −1, channel 5 only. |
| paper burst (41) | blowing paper (as temp entity 48) with count irand(4, 8) at point + (0, 0, 5), shared 300 ms limit. |

**Dust puffs** (style s with parameters {A, B, C, life L ms, G, sprite}: style 0 {30, 10, −150, 600, 3, dirt},
style 1 {30, 10, −120, 600, 2, powder} (unused), style 2 {20, 10, −100, 500, 2, powder}): count N = p if p ≤ 4 else
trunc((p − 4)/2 + 4); f1, f2 = rand(0.8, 1.2) drawn once; puff i = 1..N: sprite, lit, light-probe direction n, at the
impact point, size N × N, colour = material RGBA, r = rand(0, 2π), v = d × B·i·f2 + n × A·i·f1 + (0, 0, 8i),
a = (0, 0, C·i), g = G·i/(L/1000), fade over life.

**Impact chunks** (temp entity 6 debris flag, computer, lit panel): only materials with a chunk entry: 14–24 (wall
model "tiny" with skins lbrown, dbrown, lgrey, lgrey, dgrey, lbrown, dbrown, white, green, red, black), 37 (glass,
small glass model), 40 (wall, white, small wall model). Rate limit: dropped when the previous chunk burst was less
than 200 ms ago with the same material. Each chunk: position = point + n × rand(20, 40); velocity = n × rand(0.1q, 2q)
+ right × rand(−q, q) + up × rand(−q, q) + (0, 0, rand(100, 300)) with q = scale; angular velocity rand(−200, 200) per
axis; gravity −800; collision class 6; settle behaviour as SPEC-400 debris. **Glass shards**: the same with the glass
model, count irand(lo, hi).

**Spark emitter**: an invisible timer (see *Timers*) with interval 1000 ms, jitter 0.7 and the given life; when it
fires (effectively once, 0.3–1.7 s after creation): with c%2 = 0 an electric arc of segment length 6 without branches
between e + p × irand(−15, −10) + n × rand(4, 8) and e + p × irand(10, 15) + n × rand(4, 8) (p perpendicular to the
emitter direction n); otherwise a blue-white flash (as temp entity 10, 20 × 20, shrinking 40/s), a light
(128, 128, 160 colour bytes, i.e. 0.5, 0.5, 0.625; radius 128, 500 ms, decay 128/s), irand(8, 15) sparks as temp
entity 10 but alpha 175, size rand(0.5, 1.0), gravity −300, and sound 0 or 1 (spark) volume 0.6.

### Timers (invisible helper particles)
Explosions and some impacts create invisible timer particles (no image, never drawn) with a callback, interval
(ms), jitter j, life (ms) and a position. The first firing happens at creation + interval/1000 × rand(1 − j, 1 + j)
seconds; after each firing the next one is scheduled at now + interval × rand(1 − j, 1 + j) **seconds** (the
millisecond value is used as seconds — original quirk). Consequences that a faithful clone reproduces: with small
jitter a timer fires once within its life; with jitter 50 the factor is in −49..51, so after each firing there is a
≈ 49 % chance (negative factor) that it fires again at the next step, otherwise never again. A timer passes the
remaining-life fraction f (1 at creation → 0 at the end) to its callback.

Timer callbacks used by temp entities:
| Name here | Behaviour per firing (e = timer position) |
|---|---|
| fireball A | only while f ≥ 0.5: irand(3, 6) times (count re-drawn each loop test) a puff: sprite explode1, normal blend, life 500, at e + (0, 0, rand(12, 16)); d = normalize(rand(−100, 100), rand(−100, 100), rand(−40, 80)); v = d × rand(1000, 1500); a = −2v + (0, 0, 300); size s = (f + 1) × 32; colour grey irand(150, 250) on all three channels, alpha 130; r, ω = (c%628)/100; g = 2s; α = −2 × alpha. |
| fireball B | as A with speed rand(400, 800), colour (250, 250, 250, 150), additive. |
| smoke C | puff at e + random(±20): smoke2, darkening, life 1000, 30 × 30, (250, 250, 250, 150), v = random(±160) per axis, a = (0, 0, 800), ω = (c%628)/100, g = 30, fade over 1 s. |
| smoke ball A' | as fireball A but only while f > 0.5, speed 600, size (f + 1) × 16, sprite smoke2. |
| smoke ball B' | as A' with colour (250, 250, 250, 150), additive. |
| ground smoke | puff at e + (rand(±10), rand(±10), 0): sprite smoke2 or flare (c%2), additive, grey irand(120, 250), alpha trunc(f × 200), life (f + 1) × 500 ms, 30 × 30, v = (0, 0, 50), a = (0, 0, 400), ω = (c%628) × 0.06 − 18.84, g = −30/((f + 1) × 0.5). |
| limb smoke | puff at e: sprite smoke2 or flare, additive, life 800, size rand(4, 6), alpha trunc(f × 170 + 50), v = random(±5), a = (0, 0, 160), r, ω = (c%628)/100, α = −1.25 × alpha. |
| origin smoke | puff 5 × 5, grey irand(200, 250), alpha trunc(f × 100), smoke2, life 1000, a = (rand(±50), rand(±50), 200), ω = (c%628) × 0.06 − 18.84, g = 30, α = −0.8333 × alpha. |

### Burning debris (spinning particles with trail callbacks)
Spinning particles move (v += a·dt after moving), rotate, grow by g, change alpha by α (never below 0), and every
trail interval call a trail callback that spawns a puff at their position (SPEC-400 spin particle). Trail callbacks
(w, h = parent's dimensions, k = rand(0.85, 1.15)):
- **fire+smoke** (interval 0.075 s): a fire puff (sprite expfire3 or expfire4 = irand(49, 50) of the sprite list,
  additive, life 500, size 2k·h × 2k·w, colour R = irand(180, 250), G = irand(R − 80, R), B = irand(G − 80, G),
  alpha 250, v = 0.35 × parent velocity, ω = rand(−3, 3), g = −8, α = −500) and a smoke puff (smkgry, darkening, life
  1000, size k·h × k·w, (255, 255, 255, 250), a = (0, 0, 50), ω = rand(−3, 3), g = 20, α = −250).
- **fire** (interval 0.05 s): one fire puff (expfire1) as above.
- **coloured smoke** (interval 0.075 s): smoke sprite irand(51, 53) (smkgrn, smkgry, smkwht), additive, life 1000,
  size 2k·h × 2k·w, colour (irand(200, 255), 255, irand(225, 255), 255), ω = rand(−3, 3), g = −4, α = −255.

### Ghoul explosion models
Temp entities 38 and 44 create Ghoul model instances of debris model 19 (`Ghoul/Effects/explosion/explode80`) with
material group "explode" and skin `explosion_tile` (44) or `whitesmoke_tile` (38), a scale a, a second value b, a
life (s) and a spin (rand(−128, 128) per axis) and some a velocity. In 1.07f these records are created but **never
added to the scene and never released before the level ends** (the record carries neither the drawable nor the
Ghoul marker and its die time is computed in milliseconds) — UNVERIFIED in game; verify whether an explode80 model is
visible on a tank destruction (temp entity 44). An implementation that wants the intended look draws the model at the
position for `life` seconds with scale a growing by b per second (UNVERIFIED meaning of b).

### Temp entities
Payload field names refer to SPEC-201 §1. "Flash(x)" = puff sprite flare, additive, life 500 ms, x × x, random rotation.

| Type | Effect |
|---|---|
| 0, 14 | Sound 0 or 1 (c%2), volume 0.6, attenuation 3. Flash 20 × 20 at origin + normal, colour (trunc(rand(200, 250)) ×3, 250), ω = rand(±18.84), g = −40. Light colour (160, 160, 128) [0.625, 0.625, 0.5], radius 128, 500 ms, decay 128/s. 14 sparks: position origin + (c%4 − 1) per axis; v[k] = (normal[k] + rand(−0.1, 0.1)) × irand(32, 160) + irand(−32, 32); a = (0, 0, −300); size s = rand(0.5, 1.5); puff sparkpart, additive, life 1200, colour (irand(200, 250) ×3, 250), g = −s × 0.8333 (shrinks to nothing at the end). |
| 10 | As 0, but flash colour blue-white (irand(120, 170), irand(150, 200), irand(220, 250), 250), light (128, 128, 160), spark sprite sparkpartblue. |
| 1, 51 | Nothing (position read). |
| 2 | dir = normalize(dx − 128, dy − 128, dz − 128); right/up perpendicular. Count = ceil(10k + 1), k = min(1, smoothed frame rate / `sc_framerate` (default 30)) — 11 puffs at ≥ 30 fps. Puff i: smoke2, additive, life 750, size trunc(size/3) square, colour (220, 240, 255, 100), r = rand(0, π/2); v = (dir × (c%50 + 90i) + right × (c%240 − 160) + up × (c%240 − 160)) × size × 0.01; a = (0, 0, 600); ω = rand(0.5, 0.8); g = (rand(1, 2) + 1.5) × 1.3333; α = +133.33. |
| 3 | Decal: sprite irand(1, 3) (bldglob4..6), colour (irand(80, 160), 80, 80), alpha irand(175, 250), size rand(8, 16). |
| 4, 15, 17, 28, 53 | Trace origin → origin − 256 z; if it hits: decal on the floor: 4 blood_pool2 (100, 0, 0) alpha 250 size 2; 15 scorch (255, 250, 250) alpha 255 size 6; 17 none (one irand(200, 240) is drawn); 28 pipeleft (255, 250, 250) alpha 255 size 2; 53 puddle (170, 190, 250) alpha 200 size 2. The long field is ignored. |
| 5, 22 | Nothing (no payload). |
| 6 | Weapon impact: style > 42 → `Surface out of range!!!`, nothing. Impact mode 4; power float = power byte for style 1, 0 for styles 0 and 2; hole sprite: style 1 material hole, style 2 sprite 34 (slash), style 0 material hole (kind 0) or none. Style 1 with debris flag: one impact chunk burst with scale 25. Then, if the material is valid and the surface is not sky/no-draw: mark and impact sound, then the material effect with power = power byte. Visible result: style 0 = sound + material effect (its mark is ≤ 0.1 units: invisible); style 1 = bullet hole of size min(power, 3) × 0.6 + rand(0, 0.1); style 2 = slash decal also ≤ 0.1 units (UNVERIFIED visibility). |
| 7 | Impact (style and debris ignored): 6 impact chunks with scale 300, always using the material-18 chunk model (dark grey wall, quirk) — only for materials with a chunk entry — then mark and impact sound, then the material effect with power 12 and zero direction. |
| 8 | Electric arc from start to start + dir × length with segment length c%3 + 3, no branches (*Electric arc*). |
| 9 | Sound 48 + c%4 (FlyBy1..4) at view origin + direction × distance, volume 1, attenuation 1. |
| 11 | Puff waterdrop, lit, life 2000, size (1.5·size) × size, colour (R, G, B from the 4-bit fields × 15, alpha 250), rotation π, a = (0, 0, −800). |
| 12 | Blue-white flash 20 × 20 at origin (shrinking 40/s), light (128, 128, 160) radius 128, 500 ms, decay 128. 12 traces from origin to origin + (rand(±256), rand(±256), rand(−256, 0)): if fraction ≥ 0.2: electric arc origin → end point with segment length trunc(fraction × 8) and branches; if also fraction < 0.9: a 10 × 10 flash of the same colour at the end point shrinking 20/s. |
| 13 | Light (160, 160, 128) radius 128, 150 ms, decay 50/s at the impact point. One puff boom5, additive, life 165, 10 × 10, colour (irand(120, 170), irand(70, 130), irand(70, 130), 255), random rotation, ω = rand(±18.84), g = +600, α = −133.33. |
| 16 | Lightning: irand(1, 3) bolts; for each, x = view.x ± rand(1000, 4000), y = view.y ± rand(1000, 4000) (signs by c%2), drawn from (x, y, view.z + rand(2800, 4000)) down to (x, y, view.z + rand(−1200, −500)) as a *recursive bolt* with depth 10, branch probability rand(0.6, 1.0) − 0.2 × number of bolts, width 2. Light style 13 flash (SPEC-400) colour (1.0, 1.0, 1.8) for 100 ms. No sound. (An invisible timer is also created; it has no visible effect and in 1.07f is never released — do not copy.) |
| 18 | Debris (SPEC-400 *Debris* item 5) with chunk type table above (type 0 → 8; model by size class 1..3), count chunks; every 5th chunk (0, 5, 10, …) except chunk type 32 also a smoke puff at the chunk position: smoke2, lit, life 1200, 10 × 10, (170, 170, 170, 200), r, ω = (c%628)/100, v = (0, 0, 30), a = (0, 0, 100), g = 0.6, α = −333. Then one sound at origin, entity −1, channel 5, by material: 37/38 glass: GBBig (size 3), GBMed (2), else GBSmall, volume 0.6, attenuation 1; 17–24 Break/Stone 0.9/1; 14–16 Break/Wood 0.9/1; 28, 42, 29–32 Break/Metal 0.9/1; 35 Gore impact1..3 0.9/3; others Break/Stone 0.9/1. |
| 19 | Basis n = impact normal, r ⟂ n, u = n × r. irand(3, 4) **burning bits**: sparkpart, additive, life irand(600, 800), 12 × 12 white, spinning with v = n × rand(0, 450) + r × rand(±300) + u × rand(±300), a = (0, 0, −600), ω = rand(−3, 3), g = rand(−14, −11), α = rand(−255, −175), fire+smoke trail. irand(2, 3) bits: life irand(800, 1000), 8 × 8, v = n × rand(0, 600) + r, u × rand(±300), same a, g = −7.5, α = −255, fire trail. 3 sparks: colour R = irand(230, 250), G = irand(R − 30, R), B = irand(G − 30, G), alpha 250; expfire2, additive, life L = irand(300, 400), 6 × 6, v = n × rand(0, 100) + r, u × rand(±100), ω = rand(−3, 3), g = 30/L(s), fade over life. 10 rock chunks (chunk type 16 model, random part) at the impact point, v = (irand(±300), irand(±300), irand(600, 2000)), angular velocity irand(±900), gravity −1600, class 6, settling. No sound or light. |
| 20 | Trace origin → origin − 16 z; if it hits (fraction < 0.99) the effect is placed at the hit point. Two flashes 360 × 360 (250, 250, 250, 250), ω = rand(±1), g = −640; the second lies flat (oriented to (0, 0, 1)). 15 flares 32 × 32 white, v = random(±200) per axis, ω = rand(±1), g = −64. Light (230, 230, 230) [0.898], radius 192, 500 ms, decay 100. Sound 54 (FlshExp) volume 0.6, attenuation 0.3. (Blinding is done elsewhere.) |
| 21 | Puff shock2, additive, life 200, at origin + (0, 0, 4), 6 × 6 white, flat (oriented to (0, 0, 1)), ω = rand(±1), g = +320, α = −250: a ring growing to ≈ 70 units in 0.2 s. |
| 23 | Material ≥ 43: message only. Else sounds at origin (entity −1): the material's landing sound, then 101 (Gore/Fall3), volume 1. |
| 24 | Pellets not accounted for: m = 12 − Σhits (byte arithmetic). For each: d' = dir + rand(±0.13) per axis (not normalised), trace 1024; on hit: impact mode 4, power rand(1.5, 3.5), no hole sprite (no decal), impact sound, material effect with power trunc(rand(2, 4)). |
| 25 | n = trunc(|end − start|/16) bubbles at start + i × step: puff bubble, life 600, size s × s with s = rand(0.6, 1.2), v = random(±30), a = (0, 0, 75), α = −1.6667 × alpha. |
| 26 | 12 puffs smoke2, darkening blend, life 800, 8 × 8, (250, 250, 250, 160), r = rand(0, π), v = random(±60), a = (0, 0, 100), ω = rand(±1), g = byte × 1.25 (the byte is a growth rate, not a speed). |
| 27 | Line from start to end, sprite lightning, life duration × 500 ms, colour = the long's bytes (R, G, B, A), half-width 1, widening by 20/duration per second, alpha fading over its life. |
| 29 | Decal scorchwht (250, 250, 250), alpha 144, size irand(16, 64). |
| 30 | Decal pipeleft, black, alpha 255, size 4. |
| 31 | L = brightness (length of the world light colour) at origin. n = irand(5, 11); n − 1 sparks as type 0 sparks with speed irand(32, 50), size 1 × 1, colour per channel trunc(irand(−1, 250) × L) taken modulo 256, alpha 250, a = (0, 0, −200), sparkpart, normal blend, life 1200. |
| 32 | Sound 69 (Knife/Impact) volume 0.6. Flash 5 × 5 blue-white (as type 10), life 200, ω = rand(±18.84), g = −25. 8 streaks: v = (normal + rand(±0.25) per axis) × 600; line from point to point + v × 0.025, sprite lightning, additive, life 200, colour (100, 100, 250, 250), half-width 1; the line moves with v and acceleration 1.5v, both widths shrink 3/s, α = −5 × alpha (UNVERIFIED which end moves). |
| 33 | 32 traces from origin + (0, 0, 16) to (origin.x + rand(±128), origin.y + rand(±128), origin.z + rand(−20, 300)); on hit (fraction < 0.99): impact mode 4, material hole, power rand(1.5, 3.5), mark + sound + material effect with power 4. 4 fireballs explode1, additive, life L = irand(100, 200), 6 × 6, orange colour drawn as for the type 19 sparks, at origin + random(±10), v = random(±120), ω = rand(±3), g = 30/L(s), fade. 12 smoke puffs smoke2, life 400, 12 × 12, (120, 120, 120, 160), r = rand(0, π), v = random(±60) with v.z = 0.6|v.z|, a = (0, 0, 100), ω = rand(±1), g = −10. 6 flares 12 × 12 white at origin + random(±8), ω = rand(±1), α = −1250. |
| 34 | dir = decoded/32. 10 pellets: d' = dir + (rand(±0.15), rand(±0.15), rand(±0.08)), range rand(256, 640); on hit: impact mode 4 with hole sprite 13 (bhole2), power 4.0 for the first pellet, rand(0.5, 1.5) for the others; mark + sound + material effect with the unjittered dir and power trunc(rand(2, 4)). Net effect: one impact sound (only the first pellet exceeds 1.5), a hole of 1.8–1.9 units and up to nine of 0.3–1.0 units. |
| 35 | Find the ghoul instance; if missing, nothing. Bolt world position/direction from the entity; trace 320 units down: decal bldglob4 (100, 0, 0), alpha 250, size rand(10, 15) at the floor hit. Then a blood spurt: 5 drops as the flesh material effect but colour (60, 0, 0, 200), size 3. (An invisible timer with no effect is also created.) |
| 41 | Broken: like 35 without the bolt and decal; the spurt uses an undefined position — a clone should draw nothing. |
| 36 | Broken: one 16 × 16 mflash puff for 10 ms at an undefined position — draw nothing. |
| 37 | For each limb-cap/body bolt name (list below) present and active on the ghoul instance: a limb-smoke timer (interval 100 ms, jitter 0.6, life 4000 ms) — placed at the world origin (0, 0, 0) instead of the bolt (quirk), firing once. |
| 38 | Ghoul explosion model with skin whitesmoke_tile (a 0.5, b 2.0, life 0.8 s). irand(3, 4) burning bits (sparkpart 12 × 12 white, life irand(1000, 1200), v = (rand(±200), rand(±200), rand(0, 300)), a = (0, 0, −300), ω = rand(±3), g = rand(−10, −8), α = rand(−255, −175), coloured-smoke trail). irand(4, 5) bits (8 × 8, life irand(1500, 2000), v = (±200, ±200, 0..400), g = −4, α = −500, fire trail). 3 sparks (expfire2 6 × 6 orange as type 19, life irand(500, 1000), v = (±100, ±100, 0..60), g = 6/L, fade). irand(5, 6) streaks: line whitestreak, additive, life L = irand(400, 500), from origin to origin + q with q = (rand(±6), rand(±6), rand(0, 6)), half-width 2, white, moving with 20q and acceleration 40q, fading over life. At the last streak's end point: timers smoke ball A' (interval 20, jitter 0, life 500), smoke ball B' (same), smoke ball A' again; then 4 traces from that point + (0, 0, 64) to point + (rand(±128), rand(±128), −64): on hit a ground-smoke timer (interval 50, life 2000) at 10 % start + 90 % end point. No sound. |
| 39 | One origin-smoke timer (interval 15, jitter 0, life 1500) at the world origin (quirk). |
| 40 | Two flashes 360 × 360 white (second flat), ω = rand(±1), g = −640. 8 fireballs explode1, additive, life 500, 20 × 20, (250, 250, 250, 150), r = (c%628)/100, v = random(±100), ω = (c%628)/100, g = 40, α = −2 × alpha. Timers at origin, life 1500: fireball A (interval 70, jitter 50) twice, fireball B (40, 50), smoke C (30, 0). Sound 58 (C4Exp) volume 0.8, attenuation 2, entity −1. |
| 42 | Nothing visible (an invisible timer with no effect, if the ghoul instance exists). |
| 43 | 30 drops i = 0..29 at origin + 3i × step: bldglob4, life 2500, size (0.5 + 0.08i) × (1.5 + 0.08i), colour (63, 15, 15, a_i), a_0 = 255, a_(i+1) = trunc(0.95 a_i), r = rand(±0.175), v = (0, 0, −10), growth +1/s on both dimensions, α = −1.4286 × alpha. |
| 44 | m = magnitude byte. Ghoul explosion model explosion_tile (a 1.0, b 2.0, life 0.8 s); k = (m >> 6) + 2 more models: direction from pitch rand(−45, 75), yaw rand(0, 360), at origin + 4·dir, velocity 64·dir, a = rand(0.4, 0.6), b = rand(1, 2), life rand(0.6, 0.8). m >> 5 burning bits (sparkpart 24 × 24 white, life irand(1000, 1200), v = (rand(±300), rand(±300), rand(0, 450)), a = (0, 0, −450), ω = rand(±3), g = −20, α = rand(−255, −175), fire+smoke trail). k bits (12 × 12, life irand(1500, 2000), v = (±300, ±300, 0..600), a = (0, 0, −450), g = −6, α = −255, fire trail). k sparks (expfire2 orange 6 × 6, life irand(400, 600), v = random(±100), ω = rand(±3), g = 30/L, fade). (m >> 5) + 4 streaks: line firestreak, additive, life 600, origin → origin + q, q = random(±8), half-width 2, moving 20q, acceleration 40q, α = −416.7. Big flash 360 × 360 (250, 250, 250, 100), ω = rand(±1), g = −640, α = −200. 8 fireballs and the four timers as type 40. Sound 58 volume 1.0, attenuation 0 (heard everywhere), entity −1. |
| 45 | Puff smoke2, lit, life 1200, 10 × 10, colour = payload RGBA, r, ω = (c%628)/100, v = (0, 0, 30), a = (0, 0, 100), g = 0.6, α = −1.6667 × alpha. |
| 46, 55, 56 | s = size byte × 0.01. Flash 30s × 30s (250, 250, 250, 100), life 300, ω = rand(±1), g = −33.33s, α = −333.3. 8 fireballs explode1, additive, life 500, 20s × 20s, (250, 250, 250, 150), v = random(±50s), ω = rand(±1), g = −33.33s, α = −2 × alpha. If the sound byte ≠ 0: sound 91 (Small, type 46), 90 (Med, 55), 89 (Big, 56), volume 1, attenuation 0, entity −1. Type 56 with byte 2: sound only, no particles. |
| 47 | S = trunc(radius × 1.5): puff shock3, additive, life 300, S × S white, flat (oriented to (0, 0, 1)), ω = 7, g = −0.6667·S, α = −833.3. |
| 48 | Ignored if less than 300 client time units since the last one. count 0 → irand(5, 10) sheets of the paper debris model at origin + (rand(±4), rand(±4), rand(1, 5)), angles (rand(40, 60), rand(0, 359), 0), velocity (cos yaw · q, sin yaw · q, rand(180, 240)) with q = rand(0, 70), angular velocity (−100, 0, 0), gravity −800, class 1, and a "falling leaf" behaviour: while falling, vertical speed −18 (acceleration −1), horizontal speed × 0.9 per step, pitch swinging between ±70° with a small push along its forward direction; stops when slower than 1. |
| 49 | n = irand(5, 11); n − 1 sparks as type 0 sparks (speed irand(32, 160)), size 1 × 1, colour (100, 100, 120, 250), a = (0, 0, −800), sparkpart, normal blend, life 1200. |
| 50 | count (low byte; 0 → irand(1, 4)) soda-can debris at origin, angles 0, v[k] = irand(−32, 32) + rand(0.32, 1.6) × (dir[k] + rand(±0.1)) × speed (low byte), angular velocity (0, irand(±900), 0), gravity −300, class 5. |
| 52 | For i < count: puff dirt, lit, life 1000, (5 + 2i) square, colour (250, 250, 250, (100 − 10i) mod 256), r = rand(±π), a = (0, 0, −100 − 40i), g = 5 + 2i, α = −(100 + 10i). |
| 54 | Sound 34 + c%3 (ric1..3), volume 0.6. |
| 57 | Line start → end, sprite lightning, additive, life 5 ms, (250, 250, 250, 125), half-width 1, α = −250. |
| 58, 59 | Sound 92 (Phone/Hit) / 93 (TrashCan/LidSpin), volume 1, attenuation 1. |
| 60 | Muzzle point: for the local player the view origin; otherwise, if the shooter is in the current frame, the world position of bolt `flash` on the model attached to the shooter at bolt `wbolt_hand_r` (if that model has no `flash` bolt the point becomes (0, 0, 0) — quirk); otherwise the payload start. dir = normalize(end − muzzle point). If the shooter is not the local player and the effect index ≠ 0: start configstring effect `index` at the muzzle point with forward = dir, right (0, 1, 0), up (0, 0, 1) (not re-orthogonalised). No impact. |
| 61, 62 | dir = decoded/2048. Muzzle point as 60. Trace muzzle point → + dir × 1024; on hit: impact mode 4, power 4.0, material hole; mark + sound (volume 0.613) + material effect with power 4 (type 61) or 7 (type 62) — for every shooter including the local player. Then the effect as type 60 (non-local shooters only). |
| 63 | Non-local shooter: muzzle point as 60, effect `index` at it along dir (as 60). |
| 64 | 12 box edges as lines (sprite lightning, payload colour, alpha 165, life 400 ms). |

Limb-cap/body bolt names for type 37 (41): gbolt_groin, gbolt_gut_b, gbolt_Cap_hips_chest, gbolt_Cap_hips_lthigh,
gbolt_Cap_hips_rthigh, gbolt_Cap_coathips_coatchest, gbolt_Cap_lcalf_lthigh, gbolt_Cap_lcalf_lfoot,
gbolt_Cap_lfoot_lcalf, gbolt_Cap_lthigh_lcalf, gbolt_Cap_lthigh_hips, gbolt_Cap_rcalf_rthigh, gbolt_Cap_rcalf_rfoot,
gbolt_Cap_rfoot_rcalf, gbolt_Cap_rthigh_rcalf, gbolt_Cap_rthigh_hips, gbolt_Cap_coatchest_coathips, gbolt_gut_f,
gbolt_shoulder_lb, gbolt_shoulder_rb, gbolt_chest_f, gbolt_chest_b, gbolt_shoulder_lf, gbolt_shoulder_rf,
gbolt_Cap_chest_head, gbolt_Cap_chest_hips, gbolt_Cap_chest_lbicep, gbolt_Cap_chest_rbicep, gbolt_cap_head_chest,
gbolt_head_b, gbolt_head_f, gbolt_Cap_lbicep_lforearm, gbolt_Cap_lhand, gbolt_Cap_lforearm_lbicep,
gbolt_Cap_lforearm_lhand, gbolt_Cap_lbicep_chest, gbolt_Cap_rhand, gbolt_Cap_rforearm_rhand,
gbolt_Cap_rforearm_rbicep, gbolt_Cap_rbicep_chest, gbolt_Cap_rbicep_rforearm.

### Electric arc and recursive bolt
**Electric arc** (start, end, segment length L, branches): n = trunc(|end − start|/L) points; step = (end − start)/n;
J = trunc(L × 1.6); point i = start + i·step + right × (c%J − J/2) + up × (c%J − J/2), smoothed: each new point is
0.3 × straight position + 0.7 × jittered position and the previous point is pulled back by 6 % of the difference;
the last point is exactly `end`. Each of the n − 1 segments is a line (lit, additive, life 200 ms, white 250 ×4,
half-width L × 0.14, α = −500). With branches, 1 segment in 5 also gets a line to its end + rand(±L) per axis (life
500, no fade).
**Recursive bolt** (a, b, depth, branch probability P, width w): stop at depth 0; m = b if depth is 1, else
a + (b − a)/depth; m += rand(±90) per axis; draw a line a → a + 1.1 × (m − a) (lit, additive, life 500, white,
half-width 10w, α = −500); with probability P (rand(0, 1) < P) a branch to b + rand(±80) × depth per axis with depth
irand(1, trunc(depth − 1)), probability rand(0, P), width rand(0.1, w); continue with (m, b, depth − 1, P, w).

### Entity events
| Id | Effect |
|---|---|
| 1, 2, 15 | nothing |
| 3–14 | footsteps and landings (below) |
| 16–20 | local player only: view shake S += 7, 17, 35, 75, 130 (ids 16..20), capped at 200; decay rate = parameter × 4 per second (SPEC-400 *Camera shake*) |
| 21 | local player only: sound 102 + irand(0, 2) (bullet crack) on the player's entity, channel 1, volume 1, attenuation 0 |
| 22 | the entity's model tint becomes (1, 1, 1, 1); for the local player also the view-weapon model |

**Footsteps** (ids 3–14) need a loaded map and cvar `cl_footsteps` ≠ 0. In multiplayer (configstring maxclients not
"1") ids 3 and 4 play only for the local player's own entity. Per-id record {power, volume, alpha factor, side,
landing/centred, sprite}:
| Id | Power | Volume | Alpha | Kind | Sprite | Likely meaning |
|---|---|---|---|---|---|---|
| 3 | 0 | 0.7 | 0.3 | left foot | footstep | walk step left |
| 4 | 0 | 0.7 | 0.3 | right foot | footstp2 | walk step right |
| 5 | 2 | 1.0 | 0.8 | left | footstep | run step left |
| 6 | 2 | 1.0 | 0.8 | right | footstp2 | run step right |
| 7 | 3 | 1.0 | 0.8 | landing (both feet) | dent | soft landing |
| 8 | 6 | 1.0 | 1.0 | landing (both feet) | dent | medium landing |
| 9 | 9 | 1.0 | 1.0 | landing (both feet) | dent | hard landing |
| 10 | 4 | 0.8 | 0.8 | landing, centred | dent | light body/object fall |
| 11 | 6 | 1.0 | 1.0 | landing, centred | dent | medium fall |
| 12 | 9 | 1.0 | 1.0 | landing, centred | dent | heavy fall |
| 13 | 2 | 1.0 | 0.8 | left | footstep | step left (variant, UNVERIFIED use) |
| 14 | 2 | 1.0 | 0.8 | right | footstp2 | step right (variant) |
Single-foot ids (3–6, 10–14): trace from P + (0, 0, 10) to P − (0, 0, 64), P = entity origin moved 5 units along the
entity's right vector (left −5, right +5) unless centred; requires a hit (fraction < 0.99) with material ≤ 42. Sound at
the hit point (entity −1, attenuation 1, volume = record volume × min(1, 50/distance to the view origin)): steps play
the material's footstep sound + c%4; landings play the material's landing sound and then 101 (Gore/Fall3, same
volume) except id 7. Mark: footprint (mark mode 2 sprites) or dent, alpha factor and the entity's yaw → mark rule
"other modes" (only soft materials get a size-8 decal), then the material effect with the record's power (dust on
sand/snow, splash on water, breaking glass, paper sounds…).
Landing ids 7–9: two single-foot steps instead: left foot with id 3's volume (0.7), the landing sound (plus Fall3 for
8 and 9), sprite footstep, alpha 0.3, power 0; right foot with id 4's settings, no sound, sprite footstp2, alpha 0.3,
power 0 — so a landing leaves two footprints on soft ground.

## Constants
| Name | Value |
|---|---|
| Temp-entity types / entity-event ids | 65 (0..64) / 24 slots, 1..22 used |
| Client sound list / sprite list / debris models | 114 / 57 / 24 entries |
| Surface materials | 43 (0..42) |
| Impact rate limits | chunk bursts 200 ms per material; pipe, vent, computer, glass, vending 300 ms; vending can burst lock 3 s; paper 300 client time units |
| Bullet hole size | min(power, 3) × 0.6 + rand(0, 0.1) |
| Impact sound volume | min(1, 0.4 + 0.053333 × power), 0.6 for power 0 |
| View shake increments (events 16..20) | 7, 17, 35, 75, 130; cap 200 |

## Test vectors / how to verify
1. Send TE 46 with size 100, sound 1: a 30 × 30 flash and 8 fireballs 20 × 20 around the origin and
   Impact/Explosion/Small.wav at full volume regardless of distance.
2. TE 6 on concrete (material 0) with power 10, style 1: hole decal of size 1.8 + rand(0, 0.1) in colour
   (96, 96, 96), stone impact sound at volume min(1, 0.4 + 0.533) = 0.933, 3 grey puffs 15 × 15 and powder puffs.
3. TE 4 vs TE 15 under the same point: blood_pool2 decal of size 2 vs scorch decal of size 6 (if the floor is world
   geometry without sky/warp/flowing flags).
4. Footstep id 5 on sand (material 2) 25 units from the viewer: volume 1.0 × min(1, 50/25) = 1.0; on stone 100 units
   away: 0.5; no footprint on stone (mark mode 1), a footprint (size 8, alpha 204) on sand plus 2 dust puffs.
5. TE 48 twice within 0.2 s: the second is ignored.
6. With `cl_stats`, count alpha quads after TE 40: the four timers fire mostly once (interval quirk) — fireball A
   repeats with ≈ 49 % probability per step.

## Open questions
- Whether the Ghoul explosion models (types 38, 44) are visible at all in 1.07f, and the meaning of their second value.
- Knife slash (type 6 style 2) and style 0 marks come out ≤ 0.1 units — verify whether knife marks are visible.
- Which ends of the streak lines move (types 32, 38, 44).
- Material table byte 5 (set for materials 2–5, 9, 10–13, 27, 36, 41; no reader found).
- Which game actions emit footstep ids 13 and 14.
- Names of materials (inferred).
