# Effects rewrite: the list

The goal: every effect of Soldier of Fortune drawn with current techniques. The game's own effect data keeps deciding
what happens (`effects/*.eft`, 211 effects in the stock paks, the 65 temp entities, marks, debris): when, where, how
big, what colour and how long. Only the drawing changes.

Rules for every item:

- its own switch (cvar), and a menu row where a player would want it; the original look stays one setting away;
- a path for Low / Medium and the GLES renderer (phones), or the original look there;
- before and after shots of the same moment (`om_fxplay <effect> [dist] [up]`, `om_decal <sprite>`), the frame cost
  measured with the effect on and off;
- a section in [impl/GFX-modern.md](impl/GFX-modern.md); an entry in [DEVIATIONS.md](DEVIATIONS.md) when anything but
  the picture changes (timing, size, sound, gameplay);
- the box here is ticked when the item is in `main`, on by default on High, and checked in the game.

Progress: **18 of 41 done**. The README shows the same list.

## Done

| | Item | Switch | Since |
|---|---|---|---|
| [x] | Dynamic lights of shots, fire and explosions: smooth falloff and highlights on smoothed surfaces instead of a flat colour flood | `r_om_dlight_model`, `r_om_smoothnormals` | 2026-11-10 |
| [x] | Smoke and dust lit by the level's light and by dynamic lights | `r_om_fxlight` | 2026-11-03 |
| [x] | Smoke puffs lit as a volume, no crease across the card | `r_om_fxvolume` | 2026-11-10 |
| [x] | Explosion fire ball without its polygon outline, soft where it cuts into the world | `r_om_fxsoft` | 2026-11-10 |
| [x] | Soft particles: effect cards fade where they meet walls and floors (off only on Minimal) | `r_om_softparticles` | 2026-10-27 |
| [x] | Bullet holes with depth | `r_om_holes` | 2026-11-11 |
| [x] | Wet blood marks, blood pools under bodies | `r_om_wetblood` | 2026-11-10 |
| [x] | Lamp halos (coronas) as bloom instead of painted sprites | `r_om_coronas 0` | 2026-11-02 |
| [x] | Rain and thunderstorm with lightning light instead of the whole-sky flash | `r_om_rain` | 2026-11-01 |
| [x] | Water surfaces: waves, ripples, reflections, refraction, caustics, foam, fog above and under the water | `r_om_water`, `r_om_water_fog` | 2026-11-09 |
| [x] | Splashes in water: a spray of its own (column, drops, foam crown) and rings on the surface, in multiplayer too. Always on, no switch of its own yet | - | 2026-11-09 |
| [x] | Hit tint as a dark red vignette instead of a flat red screen | `r_om_hitfx` | 2026-11-11 |
| [x] | Night vision as an image intensifier tube instead of a green noise multiply | `r_om_nightvision` | 2026-11-11 |
| [x] | Contact shadows of bodies instead of the round blob | `r_om_capsule` | 2026-11-11 |
| [x] | Glass: see-through with refraction, traced reflections, dirt | `r_om_glass` | 2026-11-13 |
| [x] | Thunderstorm as in MGS V: the clouds light up around the channel, glare in the eyes, the flash through windows, thunder after every strike | `r_om_lightning_sky`, `r_om_lightning_glare` | 2026-11-13 |
| [x] | Rain on windows: beads and running drops on the panes that face the rain, each drop a small lens | `r_om_glass_rain` | 2026-11-13 |
| [x] | Map fog per pixel: one fog for the world, models, effects, glass and water, along the view through the fog volumes | `r_om_fogpix` | 2026-11-13 |

New effects without an original (not counted): snow on the sib maps (`r_om_snow`), anti-aircraft fire and the
distant war around kos (`r_om_aa`, `r_om_war`), drifting clouds (`r_om_skyflow`), lens effects (`r_om_lens`).

## To do: shared base

These come first; the effects below are built on them.

- [ ] **B1. Effect gallery.** A script that plays every group of the list below at fixed spots of a few maps (lit and
  dark, indoors and outdoors), takes before and after shots and the GPU time. The gallery of the first pass
  (`fxgal.cfg`) was not kept.
- [ ] **B2. Effects as light in HDR.** Find out how additive effects (fire, sparks, flashes, tracers) enter the HDR
  scene, bloom and exposure today; give them a brightness of their own, so that a fire is a light source and not
  paint over the frame.
- [ ] **B3. Living flame and smoke.** Flow-noise animation of the cards (the texture moves and boils along the
  card's own motion) instead of a sprite that only grows and fades; heat haze above hot effects (the refraction of the
  glass, last frame's colour).
- [ ] **B4. Fast particles stretched along their motion** over the frame time, with the camera's motion blur
  (sparks, tracers, embers, droplets).
- [ ] **B5. Cheap path.** Every item below has a Low / GLES path or keeps the original look there; the cost on the
  phone (Adreno 610) is measured.

## To do: effects

- [ ] **E1. Fire.** Fires of the levels (`environ/firesmall1-2`, `firelarge2-3`, `gbfire`, `onfireburst`,
  `quickfireburst`), burning bodies and burning debris (fx_tent.c), phosphorus (`weapons/world/phosburn`),
  road flares (`roadflare`, `roadflare_blue`). Planned: a flame whose colour goes with temperature from the core to the
  tip, embers, heat haze, smoke from the tip, the light flickering with the flame.
- [ ] **E2. Flamethrower.** `weapons/playermz/fthrow*`, `flamegun`, `flamebuildup`, `flamerelease`,
  `weapons/world/fthrower`, `flamehit`, `flamehitwall`. Planned: the stream as a chain of flame volumes along its path,
  burning where it hits.
- [ ] **E3. Explosions.** `weapons/world/rocketexplode`, `rocketexplode_alt`, `grenadeexplode`, `c4explode`,
  `claymore`, `airexplode`, `airexplodemute`, `slugexplode`, `slughit`, `phosexplode`, `mpgburst`, `fballimpact`,
  `autogundeath`, `heli_expl`. Planned: a flash, a fire ball that boils and cools, smoke that rolls and stays, dust
  thrown off the ground, a shock wave in the air.
- [ ] **E4. Scorch marks** of explosions and fire (`scorch`, `scorchwht`): soot that darkens and dulls the surface,
  embers that fade out.
- [ ] **E5. Sparks.** `spark`, `spark2`, `weakspark`, `environ/wallspark`, the spark emitter of metal hits
  (fx_impact.c), shot machines (`glass_computer`, `metal_computer`, `dekarmor1`). Planned: bright
  streaks with gravity and bounces, a short light.
- [ ] **E6. Impacts by material.** Dust puffs, chunks and splinters of the 43 surface materials, glass shards
  (fx_impact.c), the puff of a hit on armour (`armorhit`). Planned: dust lit as a volume, chunks in the material's
  colour and light, shards that reflect like the glass.
- [ ] **E7. Debris and gibs.** Flying Ghoul pieces (fx_debris.c), `gore/gibbody`, `gibhit`, blowing paper, falling
  leaves. Planned: lit like the other models, wet gibs, motion blur.
- [ ] **E8. Tracers.** `weapons/world/tracer`, `tracer_l`, `tracer_n`, `tracer_s`, `enemyslug`. Planned: bright streaks
  over the frame time, a faint light on the walls they pass.
- [ ] **E9. Projectiles and trails.** `rockettrail2`, `rockettrail_alt1`, `rockettrail_alt2`, `rockettrail_p`,
  `phostrail`, `fballtrail`, `startrail`, `conctrail`, `slugthrow`, `sluggren`, `jz_rocket_flame`, `jz_rocket_smoke`,
  `heli_trail`, `gore/gibtrail`. Planned: an unbroken lit smoke ribbon that spreads and breaks up, the rocket's flame
  as a light.
- [ ] **E10. Muzzle flashes and shell casings.** `weapons/playermz/*` and `weapons/othermz/*`: the flashes of the
  player and of the enemies, `shell`, `shell_l`, `shell_s`, `shellbig`, `exhaust`. Planned: a flash of a new shape every
  shot, bright in HDR; brass casings with metal highlights.
- [ ] **E11. MPG, electricity, beams.** `mpgbeam1`, `mpgbeam2`, `mpgcharge`, `mpgcharge2`, `mpgbuild`, `mpgbuild2`,
  `mpgzap`, `mpgsplatter`, `environ/pulsecook`, the blue streaks of `weapons/world/gb_dek2` and its muzzle
  `othermz/gb_dekmz`, electric arcs and recursive bolts (fx_tent.c), `environ/tripbeam`. Planned: a bright core with
  a noisy glow, flickering branches, light along the bolt.
- [ ] **E12. Blood and gore.** `gore/bloodcloud`, `bloodcloud_s`, `headcloud`, `hugeburst2`, `shotgun`, `danglyblood`,
  `floorblood`, `environ/bloodrain`, `gb_gorefountain`, `splatred`, `splatblack`, `splatbrown`, `splatgreen`,
  `splatorange`. Planned: lit wet droplets stretched by their motion, a fine mist lit by the room.
- [ ] **E13. Spraying liquids.** `chemspurt`, `oilspurt`, `waterspurt`, `takeleak`, `stream`, `stream_red`,
  `toiletspout`, `poolspout`, `k_waterspout3`, `gb_sewerspray01`, `gb_sewerspray02`. Planned: lit streams, water that
  refracts, oil and chemicals that do not.
- [ ] **E14. The rest of the water.** Drips (`waterdrip`, `gb_waterdrip`, `gb_waterdrip2`), the ripple effect
  `gb_waterripple`, `gb_debrisintowater`, leaving the water (`gb_exitw`), `puddle`, `puddle2`, `puddle3`; and the
  original particles of `gb_watersplash` and `bulletsplash`, which still play under the new spray. Planned: drops that
  refract like the rain drops, each of these ringing the surface like a splash, the old splash particles off where the
  new spray runs, a switch for the spray.
- [ ] **E15. Smoke and steam of the levels.** `normalsmoke`, `normalsteam`, `metal_steam`, `sm_steam01`, `sm_steam02`,
  `sm_steam10`, `sm_jet01`, `tv_smoke`, `helismoke`, `machgun_smoke2`, `gb_exhaust01`, `gb_exhaust02`, `kf_exhaust01`,
  `extinguisher1`, `gb_dustcloud01`, `snowspray`. Planned: lit volumes with soft edges that drift with the air.
- [ ] **E16. Lava, molten metal and steam surfaces** of the maps: still the original warp (r_om_water leaves them
  alone). Planned: glowing in HDR, heat haze, a crust that moves.
- [ ] **E17. The rest.** Breath in the cold (`enemy/breathe`), the respawn and teleport effects of multiplayer
  (`irespawn`, `prespawn`, `teleport7`).

## To do: characters

- [ ] **C1. Characters really speak.** Every character who talks - in cut scenes, briefings and in play, men and
  women, every face model - opens the mouth and moves the jaw and lips with the voice line, instead of the swapped
  face texture of the original. Planned: a jaw and lip deformation of the head driven by the voice channel (its
  loudness, later the sounds of the line), blinking and the original expressions kept; the original texture swap one
  setting away. (Owner's request, 2026-11-13.)

Not on the list: `lighthit`, `exphit`, `gotshot` and `land` are empty (72 bytes, no sprite). `edit/*`, `canthear`,
`cantsee`, `confusion` and `scripted` look like markers of the AI and of the editing tools (a snowflake sprite); to be
checked whether any of them shows in normal play.

The order is B1, then B2 to B4 together with E1 to E3 (fire, flamethrower, explosions), then E5 and E8 (sparks,
tracers), then the rest. The plans of each item may change once the gallery shows the effect.
