# SPEC-AMEND-011: How the local player's own weapon fire sound is produced (end to end)

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Evidence: static analysis of the 1.07f binaries (SoF.exe, gamex86.dll, player.dll) plus inspection of the shipped
effect files (`effects/weapons/playermz/*.eft`, `effects/weapons/othermz/*.eft`) and in-view sequence notes
(SPEC-600 Constants). Not yet black-box verified. Every item ends with a test.
Describes: SoF.exe (client view-weapon notes, effects system), gamex86.dll, player.dll, version 1.07f
Depends on: SPEC-003 (client imports 2/4/6), SPEC-011 (note callbacks), SPEC-142/SPEC-400 (effect files, effect
start, attachment refresh, one-shot primitives), SPEC-401 (temp entities 60–63), SPEC-600 (in-view sequences and
notes), SPEC-602 (when prediction is active), SPEC-782 (muzzle effects P/D/S), SPEC-AMEND-008 (I1d.1, I1d.2)
Amends: SPEC-400 "View weapon" item 1 (Refinement: attachment, gates), SPEC-600 Open question "how first-person
`sound`/`effect` notes reach the player in a local game" (Resolution), SPEC-003 client import 6 (Refinement)

Item kinds as in SPEC-AMEND-002 (Correction / Resolution / Refinement / Confirmation).

## Summary (answer in one paragraph)

The fire sound of the player's own weapon is **not a sound message from the server and not a sound call of
player.dll or of the game**. It is a **sound primitive inside the first-person muzzle-flash effect file**
(`effects/weapons/playermz/<weapon>.eft`). That effect is started **by the client engine** when the replicated
first-person (in-view) weapon model plays its fire sequence and the Ghoul runtime delivers that sequence's
`effect` note (`effect weapons/playermz/pistol1 flash`, `effect weapons/playermz/shotgun flash`). The client's
note handler looks up the `flash` bolt on the view-weapon model and starts the named effect attached to that bolt;
on the next effects step the effect's sound primitive fires and calls the sound-start service with the file's
sound, volume 1.0, attenuation 0. In a local / single-player game (mode A) player.dll's client side is **not
involved at all** (prediction is never active for a local server); the engine itself must register its own note
callbacks on the replicated view-weapon instance, dispatch the `effect` note, and run the effect including its
sound primitive. If any link of that chain is missing, the fire sound is silent while the server and game are fine.

## R11.1 — Which module decides, and by which mechanism (Resolution)

1. **Server / game (gamex86.dll):** sends nothing audible to the shooter for his own shot.
   - Single player: a player's bullet produces only the weapon-impact temp entity at the hit point (SPEC-782 §3.6);
     no sound message, no muzzle temp entity, no looping sound (except the flamethrower's looping fire sound,
     SPEC-600). The first-person effect names (`weapons/playermz/...`) appear in the game only to **precache** them
     as effect configstrings (server import 9 of SPEC-003 via the player module, and the game's own attack
     precache). The game's only use of `weapons/shotgun/fire.wav` is the shotgun attack's precache.
   - Deathmatch: the multiplayer bullet / projectile temp entities (SPEC-401 types 60, 61, 62, 63) carry the
     **D** muzzle effect (`weapons/othermz/<weapon>_p`), which also contains the fire sound, but the receiving
     client skips that effect when the shooter is the local player. So in every mode the shooter hears only the
     first-person path below.
2. **player.dll:** in a game where prediction is active (remote server, `cl_predict 1`, SPEC-602) its client-side
   copy of the inventory registers note callbacks on its own clone of the view model and forwards `effect` notes to
   client import 6 and `sound` notes to client import 4. Those imports are the **same engine routines** that the
   engine uses itself when not predicting (R11.2). In a local game player.dll's client side runs nothing for this.
   player.dll never plays a fire sound by itself; its server side ignores `effect`/`sound` notes (SPEC-600 §1).
3. **Client engine (SoF.exe):** owns the sound. When prediction is not active (local server, or `cl_predict 0`, or
   demo playback) the engine, once per rendered frame, registers on the replicated in-view weapon instance (player
   state view-weapon field, SPEC-602 Interface 2) — once per instance — note callbacks for `sound`, `fire`,
   `altfire` and `effect` plus a do-nothing default handler, and advances that instance's events to client game
   time + 0.001 s, before the effects simulation of the frame (SPEC-AMEND-008 I1d.1).
4. **The fire sound itself** is data, in the effect file: each first-person muzzle effect has exactly one sound
   primitive (shipped data):

| Effect (`effects/weapons/playermz/…`) | Sound primitive | Volume | Attenuation | Trigger delay |
|---|---|---|---|---|
| `pistol1` | `weapons/gpistol/fire.wav` | 1.0 | 0 | 0 |
| `pistol2` | `weapons/dpistol/fire.wav` | 1.0 | 0 | 0 |
| `shotgun` | `weapons/shotgun/fire.wav` | 1.0 | 0 | 0 |
| `machinepistol` | `weapons/ipistol/fire.wav` | 0.8 | 0 | 0 |
| `assaultrifle` | `weapons/assault/fire.wav` | 1.0 | 0 | 0 |
| `sniper` | `Weapons/Sniper/fire.wav` | 1.0 | 0 | 0 |
| `autoshotgun` (file version 2) | `weapons/autoshot/fire.wav` | 0.7 | 0 | — |
| `machinegun` | `weapons/mgun/fire.wav` | 1.0 | 0 | 0 |
| `rocket` | `weapons/rocket/fire.wav` | 1.0 | 0 | 0 |
| `mpg` | `weapons/mpg/fire.wav` (1.0) and `weapons/mpg/charge.wav` (0.3, delay 0.2 s) | | 0 | |

   (Other first-person effects with sounds: `autoalt`, `phos`, `rocket_alt`, `rocket_alt2`, `flamegun`,
   `fthrow3`, `fthrowmain`, `flamerelease`, `sniper_alt`; the shell effects `shell`, `shell_s`, `shell_l`,
   `shellbig` contain no sound.) Attenuation 0 means "no distance attenuation": the sound is heard at full volume
   regardless of where the muzzle bolt is, so its position does not matter for audibility.

Test: local game, pistol: log every sound-start call — expected exactly one `weapons/gpistol/fire.wav` per shot,
entity "none", channel 0, volume 1.0, attenuation 0, about 50 ms after the fire sequence starts (`fire1_a`) or
immediately (`fire1_b`); no server sound message for it in a network capture of a listen game.

## R11.2 — The `effect` note handler: exact behaviour and gates (Refinement of SPEC-400 View weapon 1)

On an `effect` note with argument text A (from the engine's own callback, or from client import 6):
1. **Gate 1 — freeze flag:** nothing happens while the client's input-freeze flag is set (the same flag that makes
   the client send empty movement and skip movement prediction; believed to be the cinematic / remote-view freeze
   of the player state; it is clear in normal play). UNVERIFIED meaning.
2. **Gate 2 — field of view exactly 95:** nothing happens unless the field of view the client renders with, as
   taken from the player state, is exactly 95.0 (not zoomed). The original has no aspect-ratio correction, so in
   normal play the value is exactly 95. **An engine that widens the horizontal field of view for wide screens
   (or otherwise rescales it) before this comparison silences every first-person muzzle effect and therefore every
   own fire sound.** The comparison must use the unadjusted player-state value (95 unless zoomed).
3. A is split on whitespace: first word = effect name, second word = bolt name (default `flash`). If the name
   contains `exhaust`, the bolt becomes `exhause_l` (sic). No first word: nothing.
4. **Gate 3 — instance:** the view-weapon instance is the predictor's clone while predicting, otherwise the
   replicated in-view instance of the player state. No instance: nothing.
5. **Gate 4 — bolt:** the instance's model must have a bolt of that name (`flash` for the pistol and shotgun fire
   notes). Not found: nothing (no fallback position).
6. The effect is started **by name** (lookup order of SPEC-400 "Effect tables": configstring effects first, then the
   by-name table, loading `effects/<name>.eft` if needed) with source class 2 ("game effects", relevant only to
   `fx_edit`/`fx_editsegment`), and with these attachment parameters: attached to the **local player's own entity**
   (client player number + 1) in entity-bolt mode, on that bolt of the **view-weapon instance** (identified as the
   view model, not as the entity's own Ghoul model), identity axes, no scale. The effect therefore follows the
   view model's muzzle while it runs. If the effect file cannot be found the request does nothing.
7. **Gate 5 — effect slots:** at most 256 effects run at once; a request when all are in use is ignored.
8. **Gate 6 — attachment refresh / detach:** at each step the attachment is refreshed (SPEC-400 "Updating a running
   effect" 1). The effect becomes detached — and then its **one-shot primitives, including the sound, never fire**
   — when the attached entity (the local player's own entity) is **not present in the current server frame**, or
   when that entity's effects field has bit 0x4000 set, or when the view-weapon instance or its bolt cannot be
   resolved at refresh time. So the local player's own entity must be part of the frames the server sends to him
   (it is in the original), even though it is not drawn in first person.
9. **Gate 7 — firing the sound primitive:** at the first effects step with effects time > trigger time (trigger
   delay 0 for these files, so the first step after the start), the sound primitive calls the sound-start service
   with: origin = the effect's current origin, entity = none (−1), channel 0, the sound handle registered when the
   effect was loaded, volume = rand(volume range) (1.0 here), attenuation = rand(attenuation range) (0 here), no
   time offset. The sound handle comes from registering the primitive's file when the effect definition was loaded
   (and again after a sound-system restart, SPEC-400). A null handle (file not found / not registered) means no
   sound.
10. `fx_noflash` does **not** gate this path (it gates only the predicted-fire extras of R11.4).

Test: for each gate, a local pistol shot with instrumentation: note delivered (1 per shot), fov value seen (95.0),
bolt found, effect started, attachment not detached, sound primitive fired. In 1.07f: set `fov 94` (if the game lets
the player-state value follow) or zoom the sniper rifle — the first-person muzzle flash and the fire sound of the
in-view weapon disappear together; at fov 95 both are present.

## R11.3 — Pistol and shotgun specifically (Resolution)

- **Pistol (pistol1, weapon id 2):** fire sequences `fire1_a` (notes at 50 ms) and `fire1_b` (notes at 0 ms), each
  with: `fire` (shot; client side: casing extras), `effect weapons/playermz/pistol1 flash` (muzzle flash **and fire
  sound** `weapons/gpistol/fire.wav`), `effect weapons/playermz/shell_s shell` (ejected shell, no sound).
- **Shotgun (weapon id 8):** `fire1` (notes at 0 ms): `fire`, `effect weapons/playermz/shotgun flash` (flash, sparks,
  smoke **and fire sound** `weapons/shotgun/fire.wav`), `effect weapons/playermz/shellbig shell`.
- Both fire sounds therefore depend on R11.2 entirely. Because the shotgun's and `fire1_b`'s notes lie exactly at the
  sequence start (0 ms), the Ghoul note-boundary rule matters: a note at the sequence start time must be delivered
  when the sequence is started and events are advanced past it (SPEC-011; SPEC-600 open question on the boundary).
  An implementation that only delivers notes strictly after the start would lose the shotgun fire sound and the
  pistol's `fire1_b` sound but keep `fire1_a`'s — a partial symptom that distinguishes this cause.
- The fire sound of the shooter is the same file the other players hear (via the D effect in deathmatch), but
  played from the first-person effect, attenuation 0.

## R11.4 — Other weapon sounds and whether they share the path (Resolution)

| Sound | Path | Gates |
|---|---|---|
| Own fire sound (all weapons except knife swing and flamethrower loop) | `effect` note → first-person muzzle effect → sound primitive (R11.2) | freeze flag, fov = 95, instance, bolt, effect loaded, own entity in frame |
| Reload, cock, ready, done, dry-fire (`weapons/gpistol/empty`), idle fidgets (knuckle, slap, rattle), shotgun `putin`/`cock` per shell, MPG `alt`/`idle` | `sound` note → engine view-weapon sound routine (client import 4 / engine callback): plays `<name>.wav` (or numbered / random volume variants, SPEC-AMEND-008 I1d.2) on the **local player's own entity**, channel 1, volume 0.6, attenuation 0 | freeze flag only (no fov gate, no bolt, no effects system) |
| Shell-casing tinks on the floor | `fire`/`altfire` note → predicted-fire extras (client import 2/3 when predicting, engine callback otherwise): trace 256 units down from the view origin, chance 50 % / 75 % / 100 % by weapon, delayed casing sound at the hit point (SPEC-400 View weapon 2) | `fx_noflash` = 0, surface material stone/wood/metal |
| Sniper zoom | local sound on zoom change (SPEC-AMEND-009 D1) | at most once per 0.18 s |
| Sniper zoomed shot | `fire` note while fov ≠ 95 starts `weapons/playermz/sniper_alt`, which contains `weapons/sniper/fire.wav` (the zoomed replacement of the normal muzzle effect, which is gated off by fov ≠ 95) | `fx_noflash` = 0 |
| Flamethrower firing loop | server: the owner's looping entity sound `weapons/flamegun/fire.wav` (SPEC-600) | none on the client |
| Knife | no fire effect; swing sounds from `sound` notes | as `sound` notes |

So reload and other handling sounds use a **different and simpler** path (`sound` note, direct entity sound),
while the fire sound needs the effects system. Diagnostic: if reload sounds are audible in OpenMerc but fire sounds
are not, note dispatch works and the fault is in R11.2 gates 2–7; if reload sounds are also missing, the engine is
not registering / dispatching note callbacks on the replicated view-weapon instance at all.

## R11.5 — Conditions (Resolution)

- **Prediction:** irrelevant to audibility. Predicting (remote server, `cl_predict 1`): notes come from
  player.dll's clone via client imports 6/4/2/3. Not predicting (local or listen server, `cl_predict 0`, demos): the
  engine's own callbacks on the replicated instance. Both end in the same handler (R11.2).
- **View type:** only first person with the in-view model present. Third-person / remote camera views: the
  freeze flag (believed) or the missing instance suppresses it. Zoomed: fov ≠ 95 suppresses it (sniper uses its own
  replacement effect).
- **Cvars:** there is no "local weapon sounds" cvar in 1.07f. `fx_noflash` affects only casing tinks and the
  zoomed sniper effect. `fx_edit`/`fx_editsegment` can substitute the effect being edited (editor only). Sound
  volume / `s_*` cvars apply as usual.
- **Registration timing:** the first-person muzzle effects are precached by the game as effect configstrings (the
  player module's view-model construction, SPEC-600 §12), so the client loads them during the level precache and
  their sound primitives register their wav files then. A by-name request for an effect not in the configstrings
  loads it on demand, registering the sound at that moment; the sound-registration service must therefore accept
  registrations outside the level-load registration window, and handles held by effect definitions must stay
  valid until the definitions are cleared at the next precache (they are cleared at every precache start).

## R11.6 — What the engine must do for the sound to play (checklist for C)

1. Each rendered frame, when not predicting: find the replicated view-weapon Ghoul instance named by the player
   state; once per instance register note callbacks `effect`, `sound`, `fire`, `altfire` (and a no-op default);
   advance its events to client game time + 0.001 s; do this before the frame's effects step.
2. Deliver notes at the sequence start time (0 ms notes) — required for the shotgun and pistol `fire1_b`.
3. `effect` callback: implement R11.2 exactly; compare the **unadjusted** player-state fov with 95.0 (do not use a
   widescreen-corrected value); split name/bolt; find bolt `flash` on the view-weapon model.
4. Effects system: support start-by-name with an attachment to (own entity, view-weapon instance, bolt); keep the
   local player's own entity in the client's current frame (or treat the view-weapon attachment as always present);
   run one-shot sound primitives at the first step after their trigger time; register primitive sounds at effect
   load; call the sound-start service with entity none, channel 0, the file's volume and attenuation (0 = none).
5. The effect files must be found under `effects/weapons/playermz/` (case-insensitive lookup of names such as
   `Weapons/Sniper/fire.wav`).
6. `sound` callback: own entity, channel 1, volume 0.6, attenuation 0 (already specified, SPEC-AMEND-008 I1d.2).
7. No server-side change is needed; the retail gamex86.dll and player.dll behave correctly in mode A.

Test (acceptance): OpenMerc mode A, kos1: fire the pistol 5 times and the shotgun 5 times; the sound log shows
5 × `weapons/gpistol/fire.wav` and 5 × `weapons/shotgun/fire.wav`, each volume 1.0, attenuation 0, channel 0, no
entity, within one effects step of the corresponding `effect` note; reload of each weapon logs the `sound`-note
wavs on the player's entity, channel 1, volume 0.6.

## Open

- Meaning of the freeze flag (gate 1): believed the player-state cinematic / remote-view freeze; confirm in game.
- Whether the effect's attachment refresh, for the view-weapon instance, uses the view model's render placement
  (it does not affect audibility because attenuation is 0).
- Ghoul note-boundary rule at exactly the sequence start (shared with SPEC-600 open question).

## Gatekeeper notes (G, 2026-10-26)

- Implementation status: gate 2 (R11.2 item 2, R11.6 item 3) is already fixed by the clean team in commit `d06e62b`
  on branch `engine-services`: the fov == 95 test, the zoomed sniper effect and the zoom mouse factor now use the
  player-state fov before the widescreen (`horplus`) widening. Measured at 1280x720: 0 of 4 to 4 of 4 own fire
  sounds (pistol, shotgun).
- Still to be confirmed by tests (not yet measured in OpenMerc): notes at exactly 0 ms are delivered (shotgun `fire1`,
  pistol `fire1_b`; R11.3, R11.6 item 2); the local player's own entity is present in the client frame and its
  effects bit 0x4000 is clear, so the effect is not detached (gate 6); `sound` notes (reload and handling) play on the
  own entity, channel 1, volume 0.6, attenuation 0 / ATTN_NONE (R11.4, R11.6 item 6).
- Gatekeeper edit: one sentence removed from R11.4 (it described a resource-name list inside SoF.exe that has no
  behaviour). The fire-sound table in R11.1 is taken from the shipped effect files (game data), not from the
  executable; C may re-read those values from the user's data files instead of relying on the table.
- The bolt name `exhause_l` (R11.2 item 3, spelling as in the original) is kept because it must match the shipped
  model data; C should confirm it against the models.
