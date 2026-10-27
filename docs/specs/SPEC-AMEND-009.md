# SPEC-AMEND-009 — remote cameras (G18), helicopter/ger4 precache (G4), train touch and landing (I1b-2), zoom sound and instance reuse (I1d.3/I1d.4)

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Evidence: static analysis of the original game module and client engine (per item below); spec cross-reference
Describes: SoF.exe (client engine, player-move service), gamex86.dll (game module), version 1.07f; behaviour only
(no implementation guidance beyond tests)
Depends on: SPEC-004, SPEC-040, SPEC-310, SPEC-313, SPEC-314, SPEC-400, SPEC-722, SPEC-730, SPEC-751, SPEC-760,
SPEC-761, SPEC-770, SPEC-780, SPEC-782, SPEC-AMEND-006, SPEC-AMEND-007
Amends: SPEC-400 (zoom sound), SPEC-751 (camera activation/deactivation), SPEC-761 §6.1 (cross-reference only)
Purpose: close the remaining G18 / G4 / I1b-2 / I1d questions of the 2026-10-26 coverage audit.

---

## A. G18 — remote cameras, seats

### A1. Refinement (SPEC-751) — activation ignored for a dead activator
Evidence: static.
When the activator is a client whose player state is "dead" (UNVERIFIED: believed to be the state set when the player dies), the
camera's use is ignored entirely: no view change, no counter change, no targets fired.
Test: kill the player while standing in a trigger that uses a camera after a delay; the camera view never appears.

### A2. Refinement (SPEC-751) — all-clients mode
Evidence: static.
With the "all clients" option, activation writes to **every in-use client**: camera counter +1, camera entity,
remote view type and fov. The view origin is written to every client as well. View **angles** are written only to
clients whose current camera entity already is this camera (i.e. watchers). The camera keeps a **single** saved
"old fov" slot: each watcher overwrites it during activation, so on deactivation the value saved for the last
client processed is restored to all watchers.
Test (MP, two clients with different fov): activate an all-clients camera and release it; both clients end with the
same fov, the one of the higher-numbered client.

### A3. Refinement (SPEC-751) — camera flag 2 (freeze/protect watcher)
Evidence: static.
- On activation: the watcher becomes non-solid and god mode is set.
- On deactivation: the watcher becomes bounding-box solid and god mode is **cleared unconditionally** — a god mode
  the player had before the camera is lost (original quirk). No relink is done in either direction (the new solid
  value takes effect at the next link of the entity).
Test: `god` on, use a flag-2 camera, release it; `god` is off afterwards.

### A4. Refinement (SPEC-751) — deactivation while nested
Evidence: static.
Every deactivation writes camera entity = none (−1) and remote view type 0, even when the camera counter is still
≥ 1 after the decrement. The end-of-frame camera step re-applies the remaining camera's entity and type while the
counter is ≥ 1, so for exactly one server frame the client may receive "no camera".
Test: overlap two cameras, release the inner one; record the player state stream: one frame with type 0, then the
outer camera again.

### A5. Confirmation — shots at a watching player
Evidence: static (both engine trace paths).
Both the plain world trace and the entity-clip trace consider only entities with solid ≠ 0, or entities carrying
the helper server flag (0x80, SPEC-313 §6/§7). A non-solid watcher's own body is therefore never hit. Its helper
volumes (SPEC-780 §15: the lean head volume in every mode; crouch and gun volumes only in deathmatch) are still
traced and resolve to the player (SPEC-760 B4.8). Under god mode the damage produces no feedback and does not end
the camera. The plain trace additionally skips a helper whose owner is the passed entity's owner (SPEC-313 /
AMEND-007 already cover this; no change).
Test: in SP, while a flag-2 camera is active, shoot the player's position from a character: no hit on the body, hits
only on the lean head volume when leaning; health unchanged.

### A6. Status — benches, chairs, "seat usage"
Evidence: spec cross-reference.
Benches and chairs are fully specified by SPEC-770 (props) and AMEND-008 G7. No separate "seat" behaviour exists in
the original game module beyond those props. Item closed as not applicable unless G names a concrete observation.

---

## B. G4 — helicopter items and the ger4 precache

### B1. Status — OPEN_QUESTIONS G4 12 / 14 / 16 / 17
Evidence: spec cross-reference.
Answered by AMEND-007 G4.12, G4.14, G4.16, G4.17 (go-to end frame and roll after a snap; pilot head limit mapping;
combat step frames; weapon choice). The remaining work is C's measurement with AMEND-007's verify vectors; R has
nothing to add. Note for 16/17: the expected gun/rocket ratio in AMEND-007 is the reference against which the
"original uses the gun far more" observation should be checked.

### B2. Resolution — ger4 flamethrower / fireball registrations
Evidence: static (character set-up of both Dekker types) + map entity data + SPEC-730/SPEC-761/SPEC-782.
- The Dekker bosses are **not** the source. Confirmed: `m_x_mraiderboss1` right hand = attack 4 (heavy pistol);
  `m_x_mraiderboss2` right hand = attack 29 and left = 22, only when spawnflag 131072 is clear. OpenMerc's 4 / 29
  are correct; G18 row ger4 is mis-attributed.
- The source is ger4's two `spawner_boosterpack` entities (`spawn1` `m_cas_mbrute`, `spawn2` `m_cas_ffemale`).
  SPEC-761 §6.1: a spawner spawns each non-empty classname once at level start and removes it (precache).
  `m_cas_ffemale` carries attack 18 (flamethrower, SPEC-730 type 109). Its set-up therefore registers the
  flamethrower attack precache (attack 18 with its alternate 19 fireball, SPEC-782 row 12) and caches player weapon
  12 (SPEC-722 "Attack types and weapon models"), which registers the flamegun in-view events
  (`weapons/flamegun/*`, `weapons/playermz/fthrow*`, `flamebuildup`, `flamerelease`) and the fireball effects
  (`weapons/othermz/flamerelease`, `weapons/world/fballimpact`, `fballtrail`).
- Hence the difference means: OpenMerc either does not perform the spawner's precache-spawn of its classnames, or
  its `m_cas_ffemale` precache does not register the full attack 18/19 and player-weapon-12 set.
- The `cin/ger4/s54d1.adp` registration that only OpenMerc makes comes from the level's precache script
  (worldspawn `script` key); it is the cinematic-sound routing item of G6b, not G4.
Test: on ger4 the registered effect/sound list at level start must include the flame/fireball names above before any
spawner fires; also check on another map with a `spawner*` that lists `m_cas_ffemale`.

---

## C. I1b-2 — player movement, tsr1 train, trn1 camera

### C1. Confirmation (SPEC-770 §6B.9) — "Failed fDot test" on tsr1
Evidence: static.
The subway car's touch prints the message when the toucher is beside the moving car rather than in front of it (a
side contact is rejected; the hit sound uses channel 2, volume 1, attenuation 1 as specced). The game module is the
same under both engines, so extra prints in OpenMerc come from the engine side: the player's position or touch list
during movement (OpenMerc's Quake II movement instead of SPEC-314), or different trace/link results.
Test: log every train touch with the toucher's origin and the car's velocity. Expected: with SPEC-314-exact player
movement the side-contact prints disappear (the original prints none on the same route).

### C2. Confirmation (SPEC-314 §4/§13) — landing flag lost on slopes
Evidence: static.
SoF's step-slide keeps the **raised** slide's vertical velocity when the stepped path wins; the choice compares 3D
distances along the start velocity (not horizontal distances); there is no step-down phase. Quake II's version copies
the plain slide's vertical velocity in that case, which clips the fall speed and loses the landing event. Ground test:
SoF uses "trace hit no entity"; Quake II uses "fraction is 1".
Test: existing vectors L1–L4; optional new vector: fall 128 units onto a 30° slope edge — the landing event fires
with the full fall speed.

### C3. Status — trn1 intro camera timing
Evidence: none yet.
Not analysed in this round. Candidate cause: the script/camera start relative to the client-begin hook (SPEC-751).
OPEN.

---

## D. I1d — client items

### D1. Correction (SPEC-400) — sniper zoom sound
Evidence: static (client engine).
Each client frame, while the view is computed:
1. Only when the remote view type is 16, or is 0 in both the current and the previous frame: fov change = current
   frame fov − previous frame fov. While client weapon prediction is active, the type is not 16 and the predicted
   fov is non-zero, the change is the predicted fov's change and the view fov is the predicted fov.
2. Otherwise (any other camera type) the fov is the current value and no sound is considered.
3. The sound plays when the change is non-zero and either more than **0.18 s** (client game-time estimate) passed
   since the last play, or that clock moved back by more than 1 s. The last-play time is then set.
4. Sound: `Weapons/Sniper/Zoom.wav` as a local sound (SPEC-004/040: listener entity, channel 0, volume 0.6,
   attenuation 1).
This replaces SPEC-400's "every 0.2 s".
Test: hold zoom continuously: plays are ≥ 0.18 s apart (not 0.2 s); no zoom sound while a type-2 or type-4 camera is
active.

### D2. Resolution — Ghoul instance id reuse
Evidence: static (client engine).
- Unreliable records: the instance id is looked up by exact match. If the instance exists, the record's object id is
  read but **not compared**; the delta is applied to the existing instance (no destroy, no recreate). If no instance
  exists: object id 0 is fatal (`Null object to create remote instance`), an unknown object is fatal (`No object found
  to create remote instance`), otherwise a new instance with that id is created and the delta read into it. Reading
  past the record is fatal (`Delta underflowed`).
- Reliable chunks are kept and parsed only while the client accepts them, in this order: string table, object
  definitions (an existing object with the same id is destroyed first), object removals (unknown ids ignored),
  instance removals (unknown ids ignored), then the underflow check (`Reliable underflowed`).
- Consequence: OpenMerc's "destroy and recreate on object mismatch" deviates from the original. While the server-side
  avoidance of id reuse is kept, the difference is unobservable. Recommendation for G: either apply the record to the
  existing instance (original behaviour) or keep the avoidance and document it.
Test: force a record with an existing instance id and a different object id: the original keeps the old object and
applies the delta; no error.

**G decision (2026-10-26).** OpenMerc follows the original's wire behaviour on both sides:
- Client: records are matched to an instance by id only; for an existing instance the record's object id is not
  compared and the delta is applied to that instance (the destroy-and-recreate on an object mismatch is dropped);
  the three fatal cases and the reliable-chunk order are as above.
- Server: the avoidance stays (no record is written for an instance id that still has a queued removal for that
  client). An original 1.07f client would apply a new instance's delta to the old object, so an OpenMerc server
  must never send that case; the original module never produces it (it creates the new view instance before it
  destroys the old one), so the stream stays the same as the original server's.
Reason: compatibility with original 1.07f servers and clients in both directions; with the server rule in place the
client rule is unobservable for OpenMerc's own streams.
G note (2026-10-26, later): the decision stands on its own merits (simpler client, identical behaviour); the 1.07f
compatibility rationale is moot after the user decision that drops 1.07f network compatibility (docs/DEVIATIONS.md).

---

## Open after this amendment
- C3 trn1 intro camera timing (not analysed).
- A1 "dead" state meaning (believed; static only).
- B2 needs C to confirm which half applies (no precache-spawn vs. incomplete ffemale precache).
- B1 measurements belong to C (AMEND-007 vectors).
