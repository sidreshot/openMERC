# SPEC-782: Server-side weapons — attack table, hitscan attacks, projectiles and equipment
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll (with the player.dll weapon descriptions it reads), version 1.07f
Depends on: SPEC-001 (game interface), SPEC-003 (player module interface: fire/equipment callbacks, weapon ids,
item types), SPEC-006 (game module map), SPEC-201 (temp-entity and effect messages), SPEC-202 (entity events, effect
slots, user-command buttons). Forward references: damage spec (damage-flag bits, gore parameters, armour, burning),
physics spec (movers), game-rules spec (deathmatch flags), player/client spec SPEC-780 (view kick, screen white-out).

## Purpose

player.dll decides *when* a weapon fires or an item is used (ammunition, rate of fire, animation). The game module
decides *what happens in the world*: it traces bullets, spawns and runs projectiles, places explosives, applies
damage, starts effects and sounds, and wakes up characters. This spec describes that server-side half for every
player weapon (ids 1–12 of SPEC-003), for the equipment items (grenade, C4, claymore, flash pack, medkit, goggles)
and for the extra attacks used only by characters. It gives every speed, damage, radius, timing and resource name a
clean reimplementation needs to reproduce what a player observes.

## Interface / layout

### 1. Entry points used by the player module (SPEC-003 server import table)

| Callback | Arguments | Behaviour (this spec section) |
|---|---|---|
| 2 fire primary | (unused, owner entity, inventory) | look up the owner's current weapon description, take its primary attack id, aim (Behaviour 1) and run the attack |
| 3 fire alternate | same | same with the alternate attack id |
| 4 restricted fire | (owner) | Behaviour 13 |
| 6 item used | (owner, item type) | Behaviour 13 |
| 8 precache attack | (attack id) | register the attack's resources (Behaviour 12); id 0 does nothing |
| 12 entity flag test | (entity) | returns true when one per-entity game flag is set (UNVERIFIED meaning; player.dll uses it only as a boolean) |
| 21 flash pack | (owner, item slot) → true | Behaviour 9.4 |
| 22 (unused) | (entity) | an "emitter" blast: radius damage 400 within 256 units, means of death 19, flags 0x200808, character startle 400/300, effect `weapons/world/emitter`. player.dll never calls it |
| 23 C4 | → true | Behaviour 9.2 |
| 24 claymore | → true | Behaviour 9.3 |
| 25 medkit | → bool | Behaviour 9.5 |
| 26 grenade | → true | Behaviour 9.1 |
| 27 goggles on/off | (owner, item slot) → true | Behaviour 9.6 |

The game reads these members of player.dll objects (completing SPEC-003):

| Object | Byte offset | Type | Meaning |
|---|---|---|---|
| inventory | 0x2C | int | current weapon id |
| inventory | 0x14C | pointer | the server copy of the in-view weapon model instance (its `flash` bolt gives the muzzle position) |
| inventory | 0x270 | int | spread counter maintained by player.dll (believed: consecutive shots; UNVERIFIED) |
| weapon description | 0x1C / 0x20 | float | spread added per counted shot, primary / alternate (degrees) |
| weapon description | 0x24 / 0x28 | float | maximum spread, primary / alternate (degrees) |
| weapon description | 0xB8 / 0xBC | int | primary / alternate attack id (table 2) |
| weapon description | virtual slot at 0x70 | bool () | "fire from the muzzle": start the attack at the view model's muzzle |
| weapon description | virtual slot at 0x74 | bool () | "converge": when firing from the muzzle, re-aim at what the crosshair points at |

### 2. Weapon → attack ids (values in player.dll's weapon descriptions)

| Weapon id | Weapon | Primary attack | Alternate attack | Muzzle start | Converge | Spread/shot, max (primary) |
|---|---|---|---|---|---|---|
| 1 | knife | 1 slash | 2 throw | no | — | 0, 0 |
| 2 | pistol2 (.44) | 4 | — | no | — | 0.2, 0.8 |
| 3 | pistol1 (9 mm) | 3 | — | no | — | 0.3, 1.5 |
| 4 | mpistol | 5 | — | no | — | 0.6, 3.5 |
| 5 | assault | 6 | — | no | — | 0.4, 1.8 |
| 6 | sniper | 7 | 8 (no server action; zoom) | no | — | 0, 0 |
| 7 | slugger (autoshotgun) | 9 slug | 10 concussion grenade | yes | yes | 1.5, 1.5 (alternate 0, 0) |
| 8 | shotgun | 11 | — | no | — | 0, 0 |
| 9 | machinegun | 12 | 13 phosphorus grenade | no | — | 0.1, 0.4 (alternate 0, 0) |
| 10 | rocket | 14 rocket | 15 splitting rocket | yes | yes | 0, 0 |
| 11 | mpg (microwave pulse) | 16 beam | 17 burst | yes | no | 0, 0 |
| 12 | flamegun | 18 flame stream | 19 fireball | yes | no | 0, 0 |
| 13, 14 | hurt hand, throw hand | — | — | | | |

Attack ids 20–30 are used only by characters and level entities (table 2, Behaviour 11).

### 3. Attack descriptor table (31 entries, built when the module loads)

Columns: **dmg** base damage; **imp** impact-effect size passed to the weapon-impact temp entity; **flags** damage
flags (bit meanings: damage spec); **alertP / alertC** alert amount when fired by a player / by a character
(Behaviour 2); **MOD** means of death written by the generic bullet path (projectile attacks use their own codes,
table 5); **gA / gB** two gore parameters passed with every damage call (damage spec); **range** bullet reach
parameter (Behaviour 3); **tracer** world tracer effect; **T** tracer style (1 = large); **unused** a value stored
but never used by the 1.07f server. Muzzle-flash effects: **P** first-person (player), **D** what other players see
in deathmatch, **S** what the player sees on characters in single player.

| id | attack | P / D / S muzzle effect (`weapons/...`) | dmg | imp | flags | alertP | alertC | MOD | gA | gB | range | tracer | T | unused |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | none | — | 0 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | — | 0 | 0 |
| 1 | knife slash | — / othermz/knife / othermz/knife | 22 | 3 | 0x1000000 | 0 | 0 | 0 | 0 | 0.25 | 30 | — | 0 | 0 |
| 2 | knife throw | — | 30 | 2 | 0x1000000 | 0 | 0 | 0 | 0 | 0 | 0 | — | 0 | 0 |
| 3 | pistol1 bullet | playermz/pistol1 / othermz/pistol1_p / othermz/pistol1 | 45 | 2 | 0 | 0.03 | 0.03 | 3 | 0 | 0 | 512 | world/tracer_s | 0 | 300 |
| 4 | pistol2 bullet | playermz/pistol2 / othermz/pistol2_p / othermz/pistol2 | 120 | 5 | 0x20000 | 0.08 | 0.08 | 4 | 0.5 | 0 | 800 | world/tracer_l | 1 | 700 |
| 5 | mpistol bullet | playermz/machinepistol / othermz/machinepistol (both) | 40 | 2 | 0x1010000 | 0 | 0 | 5 | 0 | 0.5 | 512 | world/tracer_s | 0 | 0 |
| 6 | assault bullet | playermz/assaultrifle / othermz/assaultrifle_p / othermz/assaultrifle | 45 | 4 | 0x10000 | 0.03 | 0.03 | 6 | 0 | 0 | 512 | world/tracer | 0 | 600 |
| 7 | sniper bullet | playermz/sniper / othermz/sniper / othermz/sniperenemy | 80 | 6 | 0x1080000 | 0 | 0 | 7 | 0.5 | 0 | 1600 | world/tracer_n | 0 | 0 |
| 8 | sniper alternate | — | 30 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 512 | world/tracer_n | 0 | 0 |
| 9 | slug | playermz/autoshot / othermz/autoshot_p / othermz/autoshot | 80 | 3 | 0x28000 | 0.04 | 0.04 | 8 | 0 | 0 | 512 | — | 0 | 400 |
| 10 | concussion grenade | playermz/autoalt / othermz/autoalt_p / othermz/autoalt | 200 | 3 | 0x80 | 0.02 | 0.02 | 37 | 0 | 0 | 256 | — | 0 | 800 |
| 11 | shotgun | playermz/shotgun / othermz/shotgun_p / othermz/shotgun | 40 | 1 | 0x20000 | 0.2 | 0.2 | 9 | 0 | 0.5 | 128 | — | 0 | 800 |
| 12 | machinegun bullet | playermz/machinegun / othermz/machinegun_p / othermz/machinegun | 100 | 7 | 0x20000 | 0.08 | 0.04 | 10 | 0.25 | 0 | 1024 | world/tracer_l | 1 | 1000 |
| 13 | phosphorus grenade | playermz/phos / othermz/phos_p / othermz/phos | 30 | 3 | 0 | 0.01 | 0.01 | 0 | 0 | 0 | 512 | — | 0 | 500 |
| 14 | rocket | playermz/rocket / othermz/rocket_p / othermz/rocket | 30 | 5 | 0 | 0.2 | 0.2 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 15 | splitting rocket | playermz/rocket_alt / othermz/rocket_alt_p / othermz/rocket_alt | 15 | 5 | 0 | 0.1 | 0.1 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 16 | mpg beam | playermz/mpgbeam1 / — / — | 10 | 3 | 0 | 0 | 0 | 14 | 1 | 0 | 512 | — | 0 | 0 |
| 17 | mpg burst | playermz/mpg / othermz/mpg / othermz/mpgenemy | 200 | 3 | 0 | 0 | 0 | 14 | 1 | 0 | 512 | — | 0 | 0 |
| 18 | flame stream | playermz/flamegun / — / — | 15 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 19 | fireball | playermz/flamerelease / othermz/flamerelease (both) | 30 | 3 | 0 | 0.3 | 0.3 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 20 | melee strike | — | 30 | 3 | 0 | 0.03 | 0.03 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 21 | light bullet | — | 10 | 3 | 0 | 0.1 | 0.1 | 0 | 0 | 0 | 512 | world/tracer_l | 0 | 0 |
| 22 | thrown grenade (with precache) | — | 30 | 3 | 0 | 0.05 | 0.05 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 23 | molotov | — | 30 | 3 | 0 | 0.1 | 0.1 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 24 | thrown grenade | — | 30 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 25 | nothing | — | 30 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 26 | thrown flash pack | — | 30 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 27 | knife slash (character) | — | 30 | 3 | 0 | 0 | 0 | 0 | 0 | 0 | 512 | — | 0 | 0 |
| 28 | heavy bullet | — | 180 | 15 | 0x20000 | 0.1 | 0.1 | 0 | 0 | 0 | 512 | world/tracer_l | 0 | 1500 |
| 29 | Dekker pulse gun | — / othermz/gb_dekmz / othermz/gb_dekmz | 300 | 3 | 0 | 0.02 | 0.02 | 0 | 0 | 0 | 512 | world/tracer_l | 1 | 0 |
| 30 | throwing star | — | 30 | 2 | 0 | 0 | 0 | 0 | 0 | 0 | 0 | — | 0 | 0 |

Realistic damage (deathmatch flag 16, or always in the Realistic game mode) changes three values at the moment
they are read: attacks 7 and 8 deal 110 instead of their table damage, and attacks 4, 7 and 8 get damage-flag bit
0x2 added. In deathmatch the sniper's range parameter (attacks 7 and 8) is 4800 instead of 1600.

### 4. What an attack receives

Every attack runs with: the owner entity, a start point, a unit direction, the owner's model instance (characters
only), the attack descriptor, an optional muzzle position (when the owner's model has a muzzle bolt), and two
character-only extras (a hit callback for the melee strike and a throw speed for thrown grenades and molotovs).

### 5. Bounce mover used by thrown and fired objects

All projectiles below use movement type 11 (a Raven mover; full description belongs to the physics spec). It runs
the entity's think, then once per 0.1 s frame: clamps velocity to `sv_maxvelocity`; adds gravity
`sv_gravity × G × 0.1` along the `sv_gravityx/y/z` direction (normally straight down); sweeps the bounding box with
the entity's clip mask (up to 4 clipping planes); calls the touch behaviour on impact; on impact replaces the
velocity by `E × (v − 1.8 (v·n) n)` (n = plane normal; components |x|, |y| < 1 and |z| < 100 become 0; E = −1 keeps
the velocity unchanged); applies linear friction (on ground: `F_ground × 60` per frame, in the air: `F_air × 0.01`
per frame, toward zero per axis) and angular friction (`F_ground × 12` / `F_air × 0.01`); turns the angles by
`avelocity × 0.1`. A non-player entity with health > 0 that lands on a floor (normal z > 0.7) with downward speed ≥ 100 gets entity event 10
(≥ 200: 11, ≥ 400: 12). Parameters per projectile (G gravity scale, E elasticity, Fg ground friction, Fa air
friction, B a buoyancy-like scale applied while the entity's water level is 1, UNVERIFIED):

| Object | G | E | Fg | Fa | B | Box mins / maxs | Clip mask |
|---|---|---|---|---|---|---|---|
| hand grenade (player) | 1 | 0.5 | 0.7 | 0 | 0 | (−2,−2,−3) / (2,2,3) | 0x6000083 |
| thrown grenade (character) | 1 | 0.15 | 0.7 | 0 | 0 | same | 0x6000083 |
| flash pack | 1 | 0.7 | 0.7 | 0 | 0 | same | 0x6000083 |
| C4 | 1 | 0 | 1 | 0 | 0 | (−2,−2,−2) / (2,2,2) | 0x6000083 |
| claymore | 1 | 0 | 1 | 0 | 0 | (−4,−4,0) / (4,4,6) | 0x2000081 |
| phosphorus grenade | 1 | 0.7 | 0.7 | 0 | 0 | (−2,−2,−3) / (2,2,3) | 0x6000083 |
| concussion grenade | 1 | 0 | 1 | 0 | 0 | same | 0x6000083 |
| molotov | 1 | 0 | 0.7 | 0 | 0 | same | 0x6000083 |
| fireball | 1.625 | 0.3 | 1 | 0 | 0 | same | 3 |
| slug | 0 | 0 | 1 | 0 | 0.7 | (−1,−1,−1) / (1,1,1) | 0x6000083 |
| rocket, split rocket, throwing star | 0 | 0 | 0 | 0 | 0 | (−1,−1,−1) / (1,1,1) | 0x6000083 |
| thrown knife | 0.7 | 0.4 | 1 | 0 | 0.7 | (−1,−1,−1) / (1,1,1) | 0x6000083 |

All projectiles are non-solid, record their owner (they do not collide with it), and are freed when their lifetime
ends unless stated otherwise.

## Behaviour

### 1. Aiming a player's shot

1. Eye = player origin + view offset. Direction = view angles plus, for the sniper rifle only, a per-player aim
   offset (believed: scope sway, owned by SPEC-780) with the pitch clamped to ≥ −90.
2. Spread s (degrees) = min(spread counter × spread-per-shot, maximum spread) from the weapon description. When
   realistic damage is on, or for the sniper rifle always, s is increased by a game-rule term:
   - single player: sniper rifle +4.5 while the player's field of view is above 60 (not zoomed), otherwise +0;
   - deathmatch modes: `|velocity|² × 3.75e-5`, plus 3 when the player's body reports a particular movement state
     (believed: airborne; UNVERIFIED), for every weapon except the sniper rifle; the sniper rifle gets +4.5 when not
     zoomed and +0 when zoomed;
   - Realistic mode: as deathmatch but the velocity term is multiplied by 0.5625.
   Each of pitch, yaw and roll gets an independent uniform random offset in [−s, s]; the shot direction is the
   forward vector of the result.
3. Start = eye. If the weapon fires from the muzzle (table in Interface 2): take the world position of the `flash`
   bolt of the server copy of the in-view model (placed at the eye with the view angles) and move it along the shot
   direction onto the plane through the eye perpendicular to the direction; that point is the start. If the weapon
   also converges, trace from the eye 2048 units along the direction (mask 0x6000081) and aim from the start at the
   trace end point.
4. The attack runs (Behaviour 2) and afterwards characters may hear the gunfire (damage/AI spec).

### 2. Common steps for every attack

- In deathmatch, a player attack first starts that player's third-person "fire" body animation (player-client
  spec).
- The level alert value used by the character spawners is raised by alertP (players) or alertC (characters)
  (spawner spec).
- Characters firing through a model with a `flash` bolt show the muzzle-flash effect on that bolt: the **D** effect
  in deathmatch, the **S** effect in single player. A deathmatch bot that holds the flamegun always fires attack 18
  or 19 (19 when the inventory's alternate mode is on) whatever attack it asked for.
- In deathmatch, the slug, concussion grenade, phosphorus grenade, rocket, splitting rocket, fireball and the
  player's MPG burst send the third-person "projectile fired" effect for other players (SPEC-201) using the **D**
  muzzle effect.
- While a cinematic is running (cinematic state ≠ 0, SPEC-001), every projectile, grenade, C4 and claymore that
  would explode is removed silently instead.
- A projectile that touches a sky surface (surface flag 4) is removed without exploding (rockets, split rockets,
  slugs, thrown knives, throwing stars, fireballs, concussion grenades).
- Projectiles that touch glass (surface material 37 or 38, or an entity of the glass material class) deal 1000
  damage to it (knockback 1, flags 0x8200, means of death 12) before their normal impact (rocket, split rocket,
  slug).

### 3. Bullets (attacks 3, 4, 5, 6, 7, 12, 21, 28; also the centre shot of the shotgun)

1. Trace from the owner's origin to the start (mask 0x6000081). If blocked (fraction ≤ 0.99) the shot hits that
   obstruction. Otherwise trace from the start to `start + direction × 3 × range` (range 128 → 256 units instead),
   mask 0x6000081, with the hit filter below; an owner carrying entity flag 0x1000 ignores the current sight client
   (the single-player player) instead of itself.
2. Hit filter: an entity that has a Ghoul model is hit only where the ray really meets the model (per-model ray
   test; the hit point and fraction are moved to the model surface, fraction capped at 0.99). An entity without a
   model whose surface material is 37 or 38, that is of the glass material class, or whose contents are 2 (window)
   is passed through.
3. Damage falloff: unless the attack's flags contain 0x1000000 or 0x80000, the damage D is kept while the travelled
   fraction f ≤ 0.5 and becomes `D / (2f)` beyond (D/2 at the end of the trace). Attacks whose flags contain one of
   those bits have no falloff, and deal double damage when fired by a character (non-player owner).
4. Damage: unless shot damage is suppressed by an invisible-attack level entity (Behaviour 11), the hit entity
   receives damage = knockback = the computed value, flags = attack flags | 0x200, the attack's MOD, gA, gB; the
   damage point is the trace end and the direction the shot direction. Characters shooting a target on their own
   team hurt it only if a cinematic is running, or with probability 6/11 when the hit point lies within 12 units of
   the line from the muzzle to the single-player player.
5. Deathmatch: the hit surface takes 25 damage-texture points (SPEC-001 import #25); a world hit sends the
   multiplayer world-hit temp entity (large-tracer variant when T = 1), an entity hit the multiplayer entity-hit temp
   entity (SPEC-201), carrying the tracer effect and the **D** muzzle effect.
6. Single player: a world or prop hit (not a creature) sends the weapon-impact temp entity with power = imp, style 1
   (bullet hole; style 0 when the hit entity's classname contains "door") and a random debris flag 0/1. Then:
   - shots by characters near the player (the current sight client): if the player was the target, a fly-by effect
     or a crack sound (`weapons/fx/crack/crack1..3.wav`), 1/2 each; if the shot passes within 128 units of the
     player, a fly-by effect, a crack sound or a ricochet sound at the impact point (1/3 each); otherwise a ricochet
     sound (`Weapons/FX/Rics/ric1..6.wav`) with probability 0.3;
   - shots by characters that hit the world show the tracer effect from the muzzle (or start) to the impact point
     with probability 1/4;
   - shots by the player that hit the world alert characters within 800 units of the start: a character within 20°
     of the shot line (cosine > 0.9397) reacts as "shot at"; one at a wider angle in front of the line and in the PVS
     of the start or impact point reacts as "heard a shot". The ally `m_x_mmerc` instead says
     `impact/Hawk/LookOut.adp` (volume 0.9, attenuation 0.3), at most once per 10 s.
7. Finally the segment start → impact is traced again with mask 0x22 (window | water): a water surface shows
   `environ/bulletsplash`; a window surface of glass material (37/38) or a glass-class entity takes the attack's base
   damage without falloff (knockback 1, flags | 0x200, the attack's MOD), shows a weapon-impact temp entity (single player, non-creature) and raises the alert by
   0.2.

### 4. Shotgun (attack 11)

1. If the trace from the owner's origin to the start is blocked, the start becomes the owner's origin.
2. One centre bullet as in Behaviour 3 (damage 40, range 128 → trace length 256).
3. 12 pellets: direction + right × U(−0.1, 0.1) + up × U(−0.05, 0.05), normalised, length irand(256, 512); trace
   mask 0x6000081. A pellet hitting a non-creature deals 12 (knockback 100, attack flags, MOD 9). Pellets hitting
   creatures (players and characters) are counted per target.
4. Each target hit by n pellets: player shooter → damage 14·n, knockback 100, gA, gB; character shooter → 10·n,
   gB = 0.2, flags | 0x8 against non-teammates; a teammate is hurt only with probability 2/10 and only if the
   teammate is not within 16 units of the line from the muzzle to the single-player player.
5. A temp entity 34 (SPEC-201) with the start and `direction × 32` is multicast to the hearable set.

### 5. Knife slash (attacks 1 and 27)

Trace from the start to `start + direction × 48` with the hit filter; a second trace from `start − direction × 16`
to the same end (mask 0x60000b9) gives the impact. A non-creature hit in single player shows a weapon-impact temp
entity (power 6, style 2 = knife slash). A hit entity that can take damage (and is not excluded by a per-entity exclusion flag, UNVERIFIED) takes damage 22
(attack 27: 30), knockback 1, flags 0x400100, MOD 1, gA, gB. When a player's knife (not a bot's) hits a creature
and the blood lock is off, the knife becomes bloody (one-shot weapon event of the inventory, SPEC-003 slot 34, with
argument 1; UNVERIFIED that this is the visible blood).

### 6. Thrown blades

**Thrown knife (attack 2):** spawned 10 units ahead of the start, velocity `direction × 800`, angles of the owner
with yaw + 90, pitch spin −2160°/s, Ghoul model `Enemy/Bolt` / `w_knife`, model scale 2, looping sound
`weapons/knife/throwloop.wav`, health 31337, lifetime 30 s. If it starts inside a character's hit-part entity that does
not belong to its owner, it touches that character at once. On touch:
- hitting an entity that can take damage: 30 damage (knockback 0, flags 0x1000100, MOD 2) in the flight direction.
  On a creature the knife is normally used up (removed); if the damage system deflects it (it then lies 40 units in
  front of the target as a pickup) it stays;
- hitting a solid brush entity or a damageable non-creature: the knife sticks (moved back 40 units along its flight,
  or 20 units off the hit plane with the knife-impact temp entity), stops, gets gravity 1 and no owner;
- hitting the world: knife-impact temp entity, weapon-impact temp entity (single player, power 6, style 0), the knife
  is pushed 5 units into the surface and stops, its angles follow the surface normal.
A stuck or dropped knife becomes a knife-ammunition pickup that disappears after 30 s (10 s when the game rule
"infinite ammunition" is on — deathmatch flag 2048 or always in Arsenal).

**Throwing star (attack 30):** classname `thrown_star`, velocity `direction × 500`, Ghoul model `Enemy/Bolt` /
`w_throw_star`, looping sound `Weapons/Knife/ThrowLoop.wav`, trail effect `weapons/world/startrail`, health
10000, lifetime 3 s. Touch: stops spinning and the trail; damage 30 (knockback 0, flags 0x1000100, MOD 35) to a
damageable entity (removed at once when it hit a creature); otherwise sticks (knife-impact temp entity) and is
removed after 10 s.

### 7. Explosive projectiles

**Slug (attack 9):** velocity `direction × 1200` (character: × 600), no gravity, lifetime 5 s, launch effect
`weapons/world/slugthrow` (character: `weapons/world/enemyslug`) at the start. Touch: 100 damage to the touched
entity (knockback 100, flags 0x20000, MOD 8), then linear radius damage 60 within 80 units (flags 0x108000, MOD 8,
the directly hit entity excluded), effect `weapons/world/slugexplode` at the slug, removed.

**Rocket (attack 14):** velocity `direction × 800` (character: 550), no gravity, placed where a ±2 box trace from
the owner's origin to the start ends, health 100 decremented every 0.1 s (life 10 s; it explodes at once if its
velocity becomes zero), Ghoul model `Weapon/Projectiles/Rocket` / `Rocket`, entity effect slot
`weapons/world/rockettrail_p` (character: `weapons/world/rockettrail2`), looping sound `Weapons/Rocket/FlyLP.wav`
(looping-sound byte 0xF8: volume 248, attenuation class 0). Player touch: 250 damage to the touched entity (knockback 300, flags 0x8000, MOD 12), then
min/max radius damage 225 → 32 within 175 units (flags 0x108000, MOD 13, the directly hit entity excluded); effect
`weapons/world/rocketexplode`; character startle 150 within 250; white-out 1500 / 0.7 / 0.5; alert 0.4. Character
rocket touch: no direct damage, radius damage 225 → 32 within 175 (flags 0x908000, MOD 13, nobody excluded),
explosion effect and startle, no white-out. Firing pushes the shooter back (Behaviour 10, k = 12).

**Splitting rocket (attack 15):** direction + U(−0.1, 0.1) per component, normalised; velocity × 700; otherwise as a
player rocket, trail `weapons/world/rockettrail_alt1`. Touching something within the first 0.2 s explodes it as a player rocket. Otherwise after 0.2 s it splits: a random
0–180° is added to its roll, two rockets are spawned 6 units either side along its up vector with velocity
`parent velocity ± up × 48`, model scale 0.5, trail `weapons/world/rockettrail_alt2`, life 10 s; the effect
`weapons/world/rocketsplit` is shown and the parent is removed. Each half on touch: 150 damage (knockback 175,
flags 0x8000, MOD 12), radius damage 200 → 32 within 150 (flags 0x108000, MOD 13, hit entity excluded), effect
`weapons/world/rocketexplode_alt`, startle 100 within 150, white-out 1000 / 0.7 / 0.5. Recoil k = 6.

**Concussion grenade (attack 10):** Ghoul model `Items/Projectiles` / `grenade`, velocity `direction × 800 + (0,0,150)`,
pitch spin 1000°/s, health 40, trail `weapons/world/conctrail`, lifetime 6 s (then removed without exploding).
Explodes on any touch: effect `weapons/world/sluggren`, alert 0.1; every player, character or damageable entity
within 210 units of `position + normal × 8` with a clear line (mask 3) is hit: with d the distance,
f is 1 − 0.004·d and K is trunc(400·f) (halved for the thrower). Players take damage trunc(0.15·K), knockback K;
others take trunc(0.05 · trunc(0.6·K)) with knockback trunc(0.6·K) (flags 0x80, MOD 37). Players also get
white-out + (f + 0.2) (capped 0.95), white-out fade 0.15, and entity event `17 + trunc(3.2·f)` with parameter 30
(screen shake, SPEC-202); characters are stunned (strength 25).

**Phosphorus grenade (attack 13):** Ghoul model `Items/Projectiles` / `grenade`, starts at the muzzle bolt (if any)
16 units lower, velocity `direction × 600 + up × U(100, 300)`, trail `weapons/world/phostrail`, explodes 1.5 s
after launch or at once when it touches a player or character; other touches play `Weapons/gren/bounce.wav`
(volume 0.6, attenuation 2) unless the surface material is soft (table 7) and wake characters within 160 units.
Explosion: effect `weapons/world/phosexplode`, alert 0.3; every player or character within 120 units of
`position + (0,0,8)` with a clear line takes `trunc((150 − d) × 0.5)` damage (knockback 1, flags 0x1000, MOD 11)
and burns: a target not already burning shows `weapons/world/phosburn` on its model and burns for 2 s, a burning
target's burn time is extended by 0.5 s (burning itself: damage spec).

**Fireball (attack 19):** velocity `direction × 600 + up × 100 + (0,0,150)`, gravity 1.625, elasticity 0.3, clip mask
3, health 5 (bounce count), trail `weapons/world/fballtrail`, lifetime 10 s. Touching a wall or ceiling (plane
normal z ≤ 0.5, or no plane) uses up one bounce while any remain. Otherwise it ignites: alert 0.3, linear radius
damage 300 within 150 (flags 0x9000, MOD 16), startle 300 within 300, effect `weapons/world/fballimpact`, the
entity stops (movement type 0), clears its effect slots, and becomes a fire pool that every 0.2 s for 15 ticks
deals linear radius damage `2 × remaining ticks` (30, 28, … 2) within 100 units (flags 0x1002, MOD 16).

**Molotov (attack 23, characters):** Ghoul model `Items/Projectiles` / `w_molitov`, velocity `direction × throw
speed`, roll spin 300°/s, trail `weapons/world/molitrail2`, lifetime 4 s. Touch: effect
`weapons/world/molitovburst`, linear radius damage 50 within 150 (flags 0x1000, MOD 27), every entity within 150
units with a clear line is set on fire (Behaviour 14), removed.

**Launch check (slug, concussion grenade, fireball):** a box trace from the owner's origin to the start; if it hits
something the projectile is placed at the trace end and touches the world on the next frame (0.1 s).

### 8. Beams

**MPG beam (attack 16):** a ±0 box trace from the owner's origin to the start; a start inside solid fires nothing.
A world hit there only shows the beam effect from the owner. A damageable entity hit there takes 20 (knockback 0,
flags 0x200000, MOD 14) and is "cooked". Then up to 12 successive traces along `start → start + direction × 1024`,
each starting 8 units further than the previous hit, damage every damageable entity except the owner with a
ramping amount based on a per-target timer T:

| | single player | deathmatch |
|---|---|---|
| T not in the future | damage 2, T = now + 0.2 | damage 4, T = now + 0.4, sound `Ambient/Gen/Electric/spark2.wav` (channel 4, volume 1, attenuation 2) |
| T in the future | damage trunc((T − now) × 15), T += 0.2 | damage trunc((T − now) × 30), T += 0.4 |

(knockback 0, flags 0x200000, MOD 14, gA, gB). Each hit target is "cooked": players get white-out + 0.3 (cap 0.9)
and fade 0.2, entity event 20 with parameter 30; characters are stunned (25); a living target shows
`weapons/world/mpgzap` (at most once per second), a dead one `environ/onfireburst` (once per 2 s); three boss
characters (`m_x_mskinboss`, `m_x_mraiderboss1`, `m_x_mraiderboss2`) are only cooked in one particular state
(UNVERIFIED). A hit player's "zapped" time is set to now + 0.8 (player-client spec). The beam effect alternates
between `weapons/othermz/mpgbeam1` and `weapons/othermz/mpgbeam2` on odd/even server frames, drawn from the muzzle
(deathmatch: the owner's `wbolt_hand_l` bolt) to the end point.

**MPG burst (attack 17):** player: immediately; character: a marker entity fires the same burst from the recorded
start and direction 0.4 s later if the character is still alive. A ±3 box trace from the owner to the start (world
hit: effect only; damageable hit: 240 damage). Then up to 12 traces along 1024 units through every target: 240
damage each (knockback 100, flags 0x800, MOD 14). Effect `weapons/world/mpgburst` from the start towards the end.

**Dekker pulse gun (attack 29):** as the MPG burst along 1024 units, 240 damage (knockback 100, flags 0x2000800,
MOD 14), weapon-impact temp entity on non-creatures in single player, effect `weapons/world/gb_dek2`, sound
`Enemy/Dekker/Fire.wav` on channel 1 (volume 1, attenuation 0).

**Flame stream (attack 18):** effect `weapons/world/fthrower` at the muzzle (deathmatch: the owner's `wbolt_hand_l`
bolt). A ±1 box trace from the owner to the start: start in solid → nothing; world hit → nothing more; damageable
hit → damage. Reach 280 units (deathmatch 325); up to 3 successive traces (each starting 8 units further). Each
damageable entity hit (not the owner) takes 10 (deathmatch 16), knockback 0, flags 0x1008, MOD 15, gA, gB, and on
odd server frames is set on fire (Behaviour 14). On odd server frames a solid brush hit within 175 units shows
`weapons/world/flamehitwall`.

### 9. Equipment

**9.1 Hand grenade (item 6).** Throw velocity = forward × 475 + right × U(−15, 15) + up × U(300, 350) + the thrower's
velocity (view angles); start = thrower origin with height + view height − 20 (when the thrower is linked to a
carrier entity in its attachment chain, that entity's x/y position is used; UNVERIFIED when this happens). Model `Items/Projectiles` / `grenade`, spin U(−300, 300)°/s per axis,
health 21, sound `Weapons/gren/throw.wav` (channel 2, volume 0.6, attenuation 1). The grenade is announced to bots.
Fuse: every 0.1 s, while health > 1, it wakes characters within 160 units and health decreases; it explodes on the
think after health reaches 1 (2.1 s after the throw). Touch: in deathmatch, touching a player makes it explode at
once; otherwise, when moving faster than 10 units/s and the surface material is not soft (table 7), plays
`Weapons/gren/bounce.wav` (volume 0.6, attenuation 2). Explosion at `position + (0,0,10)`: alert 0.4, min/max
radius damage 200 → 40 within 200 (flags 0x108000, MOD 36), effect `weapons/world/grenadeexplode`, white-out
2000 / 0.8 / 0.5, startle 130 within 250, bots notified, removed. Characters' thrown grenades (attacks 22/24) are
the same object with elasticity 0.15, launched from the attack start at `direction × throw speed`.

**9.2 C4 (item 2).** Throw velocity = forward × 410 + right × U(−30, 30) + up × U(150, 180) (no inherited
velocity); start = owner origin at view height. Model `Items/Projectiles` / `C4`, spin U(−300, 300)°/s, health 100,
throw sound as the grenade, announced to bots. The first time it touches the world with a contact plane it sticks:
`Weapons/C4/c4attch.wav` (volume 0.6, attenuation 1), characters within 160 hear it, gravity and all velocities
become 0, angles = direction of the surface normal. Timer: first check 0.5 s after the throw, then every 0.4 s: the
first 4 checks beep (`Weapons/C4/Beep.wav`, volume 0.6, attenuation 1; characters within 160 hear it), the 5th
explodes (2.1 s after the throw). Explosion at `position + forward(angles) × 10`: min/max radius damage 250 → 32
within 200 (flags 0x108000, MOD 17), white-out 2500 / 0.8 / 0.5, startle 300 within 300, effect
`weapons/world/c4explode` with the forward direction, alert 0.6, bots notified, removed. There is no remote
trigger.

**9.3 Claymore (item 4).** Placed at the owner's origin + (0,0,24) with zero velocity (it drops), fixed angles
(270, 90, 90), model `Items/Projectiles` / `claymore`, takes damage with health 1. Touching anything for the first
time plays `Weapons/C4/c4attch.wav`. Arming: first check 3 s after placement; the first check plays
`Weapons/Claymore/Beep.wav` (volume 0.6, attenuation 1). Every 0.2 s it looks for solid entities within 128 units:
it fires when one of them is a player or character with health > 0 — including the one who placed it — or, once 200
checks (about 40 s) have passed, when any solid entity is there. Firing: attacker = the placer, min/max radius
damage 300 → 150 within 100 (flags 0x108000, MOD 18), startle 300 within 300, alert 0.4, effect
`weapons/world/claymore`, removed. Destroying it (damage to 0 health) makes it fire 0.2 s later.

**9.4 Flash pack (item 1).** Velocity = forward × 400 + up × U(200, 400), start = owner origin at view height, model
`Items/Projectiles` / `flashpak`, spin U(−300, 300)°/s, health 100, throw sound as the grenade, announced to bots.
Touch: bounce sound when faster than 10 units/s. 1.5 s after the throw: white-out from `position + (0,0,15)` with
radius 10000, cap 0.99, fade 0.15; flash-bang temp entity; linear radius damage 20 within 100 (flags 0, MOD 27,
the pack excluded; a target whose fire-effect timer is still running is skipped); bots notified; removed. Characters throw
the same object with attack 26.

**9.5 Medkit (item 5).** With realistic damage on: always succeeds; the user's body object runs its wound-healing
routine (damage spec) and one per-client value is reset to 0 (believed: a wound/bleeding indicator, UNVERIFIED),
sound `dm/healthpu.wav` (channel 3, volume 1, attenuation 2). Otherwise: when health < maximum health, health
increases by 25 capped at the maximum, same sound, the same per-client value is reset, returns true; at full health it prints string ITEMS/FULL_HEALTH
("You are uninjured, medical attention not necessary.") to the user and returns false (the medkit is not used up).

**9.6 Goggles (item 3).** On: the client's goggles flag is set, `Weapons/Goggles/on.wav` (channel 2, volume 0.6,
attenuation 2). Off: flag cleared, `Weapons/Goggles/off.wav` (not played during a cinematic). Always returns true.
The battery is managed by player.dll (SPEC-003). The flag changes white-outs (Behaviour 10).

### 10. Shared explosion rules

- **Linear radius damage (damage D, radius R, means of death, flags, excluded entity):** for every entity that can
  take damage, is not excluded by a per-entity exclusion flag (UNVERIFIED), is not the world or a model sub-part, and whose bounding-box centre is within R
  of the inflictor: damage = D × (1 − d/R); a line-of-damage check must pass; knockback = damage, or, when the flags
  contain 0x80, damage is multiplied by 0.05 while knockback keeps the full value; flags | 0x1. Means of death 27
  only hits targets whose fire-effect timer (Behaviour 14) has expired.
- **Min/max radius damage (centre point, max M, min m, radius R):** same selection around the given point; damage =
  m + (M − m)(1 − d/R); the damage point is where a trace (mask 0x6000001) from the centre to the target ends.
- **White-out (point, radius R, cap C, fade Fd):** for every player within R of the point with a clear line from the
  point to its eye, facing the point (dot of view forward and direction to the point = q > 0): amount =
  `R × 0.03 / d × q`. With goggles on: amount + 0.4 if amount < 0.4, else 2 × amount; and Fd × 0.5,
  C + (1 − C) × 0.5. The player's white-out value grows by the amount and is capped at C; the fade value is set to
  Fd when unset or larger (player-client spec draws it). Characters within R with a clear line are stunned for
  `min(25, (q' + 1.25) × 0.5 R / d)` where q' is the dot of their facing with the direction to the point.
- **Startle (strength S, radius R):** each living character within R of the entity's centre gets a startle
  reaction of `min(100, (1 − d/R) × S)`.
- **Recoil push (k):** the shooting player's kick pitch decreases by k (not below −20) and its kick end time becomes
  `max(old end + k/30, now + k/30)` (player-client spec); the shooter is pushed along −direction (vertical part
  scaled by 0.25 before normalising) by a velocity of `12k × 250 / max(50, mass)` unless its movement type is none,
  push, stop or noclip or it carries the no-knockback flag.

### 11. Character-only attacks

- **Melee strike (attack 20):** trace from `start − direction × 16` to `start + direction × 48` (mask 0x60000b9); if
  something is hit the character's hit callback runs; a damageable hit takes 30 (knockback 100, flags 0x100,
  MOD 31).
- **`environ_invisible_attack`:** fires its configured attack id at a fixed rate in a randomly jittered
  direction; with spawnflag 2 its bullets (Behaviour 3) do no damage (effects only).
- Attack 25 does nothing; attacks 21 and 28 are bullets (Behaviour 3); 22/24 grenades, 23 molotov, 26 flash pack,
  27 knife slash, 29 Dekker gun, 30 throwing star.

### 12. Precache

- At every level spawn: models `Items/Projectiles` `flashpak`, `C4`, `claymore`, `grenade`, `shell`;
  `Items/Pick-ups/Armor` `armor`; the effects and sounds of table 8; and per item type: flash pack →
  `Weapons/gren/bounce.wav`; C4 → `Weapons/C4/c4attch.wav`, `Weapons/C4/Beep.wav`; goggles →
  `Weapons/Goggles/on.wav`, `Weapons/Goggles/off.wav`; claymore → `weapons/world/claymore`,
  `Weapons/C4/c4attch.wav`, `Weapons/Claymore/Beep.wav`; grenade → `weapons/world/grenadeexplode`,
  `Weapons/gren/bounce.wav`.
- Per attack (callback 8): the **P** effect, the **D** effect in deathmatch or the **S** effect in single player,
  the tracer effect, and the attack's own list (table 9).

### 13. Fire and item hooks

- **Callback 4 (restricted fire):** for a bot, one bot timer is set 2.3 s ahead and a bot routine is notified
  (UNVERIFIED); then, in deathmatch, the player's third-person body animation for a restricted fire is started
  (player-client spec). UNVERIFIED what "restricted" means (player.dll calls it before a fire animation while its
  variable C is non-zero).
- **Callback 6 (item used):** in deathmatch, for item types 1, 2, 4 and 6 (flash pack, C4, claymore, grenade) and a
  player who is not crouching, the third-person throw animation `std_tgrenade_n_a_n` is started.

### 14. Setting on fire

"Set on fire" (flame stream, molotov) records the attacker on the target and shows a fire effect on its model:
`environ/quickfireburst` normally; for a dead target whose fire-effect timer is not more than 2 s in the future,
`environ/onfireburst` and the timer becomes now + 4 s. Nothing happens to entities that cannot take damage, to
god-mode entities, or to the three boss characters named in Behaviour 8 unless they are in the exempt state, and
nothing at all while the death lock (`lock_deaths`) is on. Burning damage over time is defined by the damage spec.

## Constants

### Table 5: means of death written by this module

| Code | Cause |
|---|---|
| 1 | knife slash |
| 2 | thrown knife |
| 3 | pistol1 |
| 4 | pistol2 |
| 5 | mpistol |
| 6 | assault rifle |
| 7 | sniper rifle |
| 8 | slug (direct and splash) |
| 9 | shotgun |
| 10 | machinegun |
| 11 | phosphorus grenade |
| 12 | rocket direct hit; glass broken by a projectile |
| 13 | rocket splash |
| 14 | MPG beam, MPG burst, Dekker gun |
| 15 | flame stream |
| 16 | fireball and fire pool |
| 17 | C4 |
| 18 | claymore |
| 19 | emitter blast (unused callback 22) |
| 27 | flash pack, molotov |
| 31 | character melee strike |
| 35 | throwing star |
| 36 | hand grenade |
| 37 | concussion grenade |
| 0 | heavy bullet (attack 28) |

### Table 6: equipment and projectile timings

| Object | Value |
|---|---|
| hand grenade fuse | first check 0.1 s, 0.1 s steps, explodes at 2.1 s |
| C4 | first beep 0.5 s, beep every 0.4 s, 4 beeps, explodes at 2.1 s |
| claymore | arms after 3 s, checks every 0.2 s, any-entity mode after 200 checks, fires 0.2 s after being destroyed |
| flash pack | explodes at 1.5 s |
| phosphorus grenade | explodes at 1.5 s |
| concussion grenade | removed after 6 s if it never touched anything |
| rocket / split rocket | life 10 s (100 ticks of 0.1 s); split after 0.2 s |
| slug | life 5 s |
| thrown knife | life 30 s; as a pickup 30 s (10 s with infinite ammunition) |
| throwing star | life 3 s; stuck 10 s |
| fireball | life 10 s; fire pool 15 ticks of 0.2 s |
| molotov | life 4 s |
| MPG burst (character) | 0.4 s delay |

### Table 7: soft surface materials (no grenade bounce sound)

2, 3, 4, 5, 9, 10, 11, 12, 13, 25, 26, 27, 33, 34, 36, 40.

### Table 8: resources precached at every level spawn

Effects: `environ/onfireburst`, `environ/quickfireburst`, `spark`, `environ/wallspark`, `land`, `gotshot`,
`exphit`, `lighthit`, `weapons/world/airexplode`, `weapons/world/grenadeexplode`,
`weapons/world/c4explode`. Sounds: `weapons/fx/tink/gen1..2.wav`, `weapons/fx/tink/metal1..3.wav`, `weapons/fx/tink/shot1..3.wav`,
`weapons/fx/tink/hvy1..3.wav`, `weapons/fx/crack/crack1..3.wav`, `Weapons/gren/throw.wav`,
`Weapons/FX/FlyBy/FlyBy1..4.wav`, `Weapons/FX/Rics/ric1..6.wav`, `dm/armorpu.wav`, `dm/healthpu.wav`,
`weapons/gpistol/cock.wav`, `misc/talk.wav`, `misc/type.wav`, `weapons/FlshGren/FlshExp.wav`,
`impact/gore/impact1..3.wav`, `impact/player/hit1..4.wav`, `impact/player/armor.wav`, `player/jump.wav`,
`misc/padd/warning.wav`, `misc/padd/warning2.wav`, `misc/padd/max.wav`, `weapons/knife/throwloop.wav`.
The file names are case-insensitive in the file system; the spelling above is the one the game registers.

### Table 9: per-attack precache lists

| Attack | Resources |
|---|---|
| 1 knife slash | `weapons/knife/swing2.wav`, `weapons/knife/swing3.wav` |
| 2 knife throw | `weapons/knife/throwloop.wav`, `weapons/knife/impact.wav`, model `Enemy/Bolt` `w_knife` |
| 30 throwing star | the same sounds, effect `weapons/world/startrail`, model `Enemy/Bolt` `w_throw_star` |
| 7 sniper | `weapons/sniper/zoom.wav`, effect `weapons/playermz/sniper_alt` |
| 9 slug | `weapons/world/slugthrow`, `weapons/world/enemyslug`, `weapons/world/slughit`, `weapons/world/slugexplode` |
| 10 concussion | model `Items/Projectiles` `grenade`, `weapons/world/sluggren`, `weapons/world/conctrail`, `Ambient/Gen/Electric/spark2.wav` |
| 11 shotgun | `weapons/shotgun/fire.wav`, effect `gore/shotgun` |
| 13 phosphorus | model `grenade`, `weapons/world/phostrail`, `weapons/mgun/explode.wav`, `Weapons/gren/bounce.wav`, `weapons/world/phosexplode`, `weapons/world/phosburn` |
| 14 rocket | model `Weapon/Projectiles/Rocket` `Rocket`, `weapons/world/rocketexplode`, `weapons/world/rockettrail2`, `weapons/world/rockettrail_p`, `Weapons/Rocket/FlyLP.wav` |
| 15 split rocket | as 14 plus `weapons/world/rocketexplode_alt`, `weapons/world/rocketsplit`, `weapons/world/rockettrail_alt1`, `weapons/world/rockettrail_alt2` |
| 16 mpg beam | `weapons/world/mpgzap`, `weapons/othermz/mpgbeam1`, `weapons/othermz/mpgbeam2`, `environ/onfireburst`, `environ/quickfireburst` |
| 17 mpg burst | `weapons/world/mpgburst`, `weapons/othermz/mpgcharge`, `weapons/othermz/mpgcharge2`, `environ/pulsecook`, `gore/mpgsplatter`, `Weapons/MPG/Expand.wav`, `Weapons/MPG/Burst.wav`, `Weapons/MPG/alt1..4.wav`, `Weapons/MPG/charge.wav` |
| 29 Dekker gun | `weapons/world/gb_dek2`, `weapons/othermz/gb_dekmz`, `environ/pulsecook`, `gore/mpgsplatter`, `Enemy/Dekker/Fire.wav`, and the MPG sounds of attack 17 |
| 18 flame stream | `weapons/world/fthrower`, `weapons/world/flamehit`, `weapons/world/flamehitwall`, `environ/onfireburst`, `environ/quickfireburst`, `weapons/othermz/flamecharge` |
| 19 fireball | `weapons/world/fballtrail`, `weapons/playermz/flamebuildup`, `weapons/playermz/fthrowidle`, `weapons/world/fballimpact` |
| 22 grenade | model `Items/Projectiles` `grenade` |
| others | nothing extra |

### Table 10: explosion summary

| Explosion | Direct hit | Radius damage | Flags / MOD | White-out R / C / Fd | Startle S / R | Alert |
|---|---|---|---|---|---|---|
| hand grenade | — | 200 → 40 within 200 | 0x108000 / 36 | 2000 / 0.8 / 0.5 | 130 / 250 | 0.4 |
| C4 | — | 250 → 32 within 200 | 0x108000 / 17 | 2500 / 0.8 / 0.5 | 300 / 300 | 0.6 |
| claymore | — | 300 → 150 within 100 | 0x108000 / 18 | — | 300 / 300 | 0.4 |
| flash pack | — | 20 × (1 − d/100) | 0 / 27 | 10000 / 0.99 / 0.15 | — | — |
| rocket (player) | 250, kb 300 | 225 → 32 within 175 | 0x108000 / 13 | 1500 / 0.7 / 0.5 | 150 / 250 | 0.4 |
| rocket (character) | — | 225 → 32 within 175 | 0x908000 / 13 | — | 150 / 250 | 0.2 |
| split rocket half | 150, kb 175 | 200 → 32 within 150 | 0x108000 / 13 | 1000 / 0.7 / 0.5 | 100 / 150 | 0.2 |
| slug | 100, kb 100 | 60 × (1 − d/80) | 0x108000 / 8 | — | — | — |
| fireball | — | 300 × (1 − d/150) | 0x9000 / 16 | — | 300 / 300 | 0.3 |
| fire pool tick | — | 2 × ticks left × (1 − d/100) | 0x1002 / 16 | — | — | — |
| molotov | — | 50 × (1 − d/150) | 0x1000 / 27 | — | — | — |
| phosphorus | — | (150 − d)/2 within 120 | 0x1000 / 11 | — | — | 0.3 |
| concussion | — | see Behaviour 7 (within 210) | 0x80 / 37 | own rule | — | 0.1 |

## Test vectors / how to verify

1. Single player, cheats on, `give` the C4 (or start a level with it): throw it at a wall — it sticks with
   `Weapons/C4/c4attch.wav`, beeps 4 times at 0.4 s intervals and explodes 2.1 s after the throw. A test dummy
   character standing 100 units away takes 250 − 218 × 0.5 = 141 damage before armour/gore rules (verify with
   `developer 1` damage prints or by health readouts).
2. Throw a hand grenade straight up on flat ground: it explodes 2.1 s after release; in deathmatch, a grenade
   hitting another player explodes on contact.
3. Place a claymore and step within 128 units 3 s later — it fires (even for its owner). Shoot a claymore — it
   fires 0.2 s later.
4. Shotgun at point-blank range against a single character (single player): damage per shot = 40 (centre) +
   14 × (pellets that hit).
5. Pistol1 at 1000 units: range 512, trace length 1536, f ≈ 0.65 → damage 45 / 1.3 ≈ 34; at 700 units the full 45.
   Sniper rifle damage does not fall off (80; 110 with dmflags 16).
6. Rocket launcher in deathmatch: the shooter is pushed back (≈ 180 units/s for a 200-mass player) and the view
   kicks up 12°; a rocket fired at a wall 2 m away explodes with `weapons/world/rocketexplode`.
7. Splitting rocket: after 0.2 s of free flight two half-size rockets separate with the `weapons/world/rocketsplit`
   effect.
8. MPG beam on a character for 1 s (single player): the per-hit damage grows 2, then trunc(0.2×15)=3, …; watch the
   `weapons/world/mpgzap` effect at most once per second.
9. During a cinematic, a thrown grenade vanishes without exploding.
10. Medkit at full health: message "You are uninjured, medical attention not necessary." and the medkit count does
    not change.

## Open questions

- Meaning of the "unused" attack-table value (300…1500) and of the gore parameters gA/gB (damage spec).
- The game-rule movement-state term (+3 spread) — which body state it tests.
- Server callback 12 (entity flag) and the exact effect of callback 4 ("restricted" fire).
- The buoyancy-like bounce-mover parameter B and the water-level value it tests.
- The "deflected thrown knife" condition decided by the damage system.
- Boss exemption from MPG cooking (the state value tested).
- Exact first-person view-kick handling of `kick pitch`/`kick end time` (player-client spec).
- One short effect name in the common precache list (table 8) to be read from the shipped data.
