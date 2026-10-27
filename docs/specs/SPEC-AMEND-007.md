# SPEC-AMEND-007: Answers to the clean team's open questions of 2026-10-25 (round 3)
Status: approved (G, 2026-10-25) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: SoF.exe, gamex86.dll, ref_gl.dll, version 1.07f
Depends on: SPEC-001, SPEC-002, SPEC-003, SPEC-010–014, SPEC-202, SPEC-203, SPEC-204, SPEC-301, SPEC-302, SPEC-311, SPEC-313, SPEC-411, SPEC-700, SPEC-701, SPEC-710, SPEC-711, SPEC-720–722, SPEC-730–732, SPEC-741, SPEC-760, SPEC-770, SPEC-771, SPEC-780, SPEC-782, SPEC-791, SPEC-803, SPEC-303, SPEC-752, SPEC-AMEND-005, SPEC-AMEND-006
Amends: SPEC-313 §7 / SPEC-001 import #53, SPEC-202 §3.1, SPEC-010 lifetimes, SPEC-302 §3 / SPEC-203 command buffer, SPEC-311 serverrecord, SPEC-001 RunFrame (I1b items); SPEC-731 A §9.3/§9.5/§9.9/§11/§13, SPEC-791 §11, SPEC-720 §12, SPEC-760 B3.6a, SPEC-AMEND-006 G4.2/G4.6/G4.10 (G4 items); SPEC-722 cow idle choice, SPEC-700 §15, SPEC-710 C5, SPEC-AMEND-006 G2.14 (G2 items 17–18); SPEC-782 §3, SPEC-AMEND-006 G3.20 (G3 item 23); SPEC-780 §15/§16/§21, SPEC-791 §1, SPEC-760 B4.5, SPEC-752 §3.1, SPEC-AMEND-005 G18.25/G18.26/G18.28, SPEC-AMEND-006 G18.33 (G18 items 36–42); SPEC-770 6A.7 and spawn/skin tables, SPEC-AMEND-006 G7.7 (G7 items 12–15); SPEC-741 §14.4/§14.5, SPEC-AMEND-006 F5.2, SPEC-411 §6.7 (F5 items 5–6); SPEC-002 import 0x68 and alpha-quad flags, SPEC-400 Editor 1, cvar table and decal labels (I5 items 21–23); SPEC-204 §5.3/§5.4, SPEC-803 §3, SPEC-AMEND-006 I2d.6/I2d.8 (I2d items 9–11); SPEC-752 §1.2/§3.1/§3.2/§3.4, SPEC-760 B9.2, SPEC-001 exports 0x40/0x44, SPEC-303 (G10 items)

## Purpose

Answers the items of `docs/specs/OPEN_QUESTIONS.md` that SPEC-AMEND-005 and SPEC-AMEND-006 did not answer, including the items added while this amendment was written. It covers the sections "I1b module host and protocol-33 server" (items 1–6), "G4 roster, helicopters, vehicles, special characters" items 11–17 (with one supplement on crew kills), "G2 character AI" items 17–18, "G3 damage model, gore, bodies" item 23, "G18 game module" items 36–42, "G7 props and server effects" items 12–15, "F5 SPEC-AMEND-005 follow-ups" items 5–6, "I5 client effects and temp entities" items 21–23, "I2d GameSpy lobby chat" items 9–11 and "G10 game-module savegames" (items 1–7). Each answer was
decided from the 1.07f binaries. Several items report a black-box observation of the original that contradicts an
approved spec or an approved amendment. For each of them the item says plainly which one is right.

Items are numbered by section prefix and by the item number used in OPEN_QUESTIONS (`I1b.n`, `G4.n`, `G2.n`,
`G3.n`, `G18.n`, `G7.n`, `F5.n`, `I5.n`, `I2d.n`, `G10.n`), so that the gatekeeper can mark each one. Item kinds are
as in SPEC-AMEND-002:
- **Correction**: the old text (or OpenMerc's interpretation, or the black-box observation, or an earlier amendment)
  is wrong. An implementation that follows it differs visibly from the original.
- **Resolution**: the old text was UNVERIFIED, silent or an open question, and the answer is now known.
- **Refinement**: the old text is true but incomplete in a way that matters for matching the original.

Every item names its target spec and section. It quotes or paraphrases the old text ("OpenMerc interpretation: …"
where the spec was silent), gives the new text, and says whether OpenMerc's current choice matches the original. It
ends with a test (in-game, black-box probe or data check). Where only running the game can decide, the item says so
and gives the black-box test. Observations that need no change are acknowledged in one line.

## Behaviour

### A. I1b module host and protocol-33 server (items 1–6)

#### I1b.1 SPEC-313 §7 / SPEC-001 #53 — the filter callback has four arguments (Correction)

- Old: SPEC-001 #53 "same 7 as #52, then callback pointer"; "every entity the sweep touches is collected, sorted by
  fraction, and offered to the callback nearest-first; the first hit the callback accepts (nonzero return) becomes the
  result". The callback's arguments are not stated. Observation (I1b, tracing proxy under the original engine): the
  callback receives (candidate trace, start point, sweep end point); OpenMerc now passes these three.
- New: the callback is a plain C (caller-cleans) function with **four** arguments and an integer result:
  1. pointer to the candidate's 56-byte trace record (layout SPEC-313 "Trace result"). The record is the result of
     sweeping the box against that one entity, with the hit-entity field (offset 48) already set to the candidate
     entity;
  2. pointer to the start point: the very pointer the game passed as the first argument of #53;
  3. pointer to the end point of the entity sweep (see below);
  4. the content mask, by value: the sixth argument of #53, unchanged.
  The original game module reads all four. Its hit filter tests the candidate's brush contents (offset 44) against
  the fourth argument and rejects some candidates when they share no bit with it, so a stray value there makes shots
  hit or pass characters at random. Add to SPEC-313 §7 and to the SPEC-001 import table row 53: "callback(candidate
  trace, start, sweep end, content mask) → nonzero to accept".

  The third argument, exactly:
  - world trace fraction exactly 1.0 (the world was not hit): the end pointer the game passed (fourth argument of
    #53), not a copy;
  - world trace fraction strictly between 0 and 1: a pointer to the end position of the engine's own world-trace
    result, i.e. the world hit point as the world trace computed it (start + fraction × (end − start) with the
    world trace's own fraction, before that fraction is replaced by 0.99). The value is stable for the whole call;
  - world fraction 0: the callback is never called (the world result is returned at once).
  So at fraction 1 the third argument has the same value as "the world trace's end position" (the world trace
  reports the end point unchanged when nothing is hit) but is the game's own vector.

  Two further rules the spec leaves implicit:
  - The candidate sweeps run from the start point to that sweep end point. A candidate's fraction, end position and
    plane are therefore measured along the shortened segment (start → world hit point), not along the game's full
    segment.
  - The callback may change the candidate record. When it accepts, the engine copies the record **after** the call
    returns, so the callback's changes (the original game rewrites fraction, end position and plane normal for hits
    on Ghoul models) become the result. Rejected records are discarded; their changes have no effect. The return
    value of #53 is then true exactly when the copied fraction is 1.0.
  OpenMerc (start and end passed, mask not passed) **does not match**: it must pass the content mask as the fourth
  argument.
- Verify: tracing proxy under the original engine: log the fourth argument in every callback call; it equals the
  mask the game passed to #53 (0x06000081 for shots). OpenMerc: with the fourth argument passed, kos1 shots that
  pass a character no longer fault and hit/miss decisions repeat exactly between runs.

#### I1b.2 SPEC-202 §3.1 — which sound entities the 1200-unit limit drops (Correction)

- Old: "An entity that fails the visibility test but has a sound is still sent, with renderfx bit 0x80 added; such
  sound-only entities farther than 1200 units are dropped." Observation: on kos1 the original sends 39 looping-sound
  entities with renderfx 0x80 at up to 4678 units, all with attenuation class 0 (sound data 248). OpenMerc
  interpretation: the cut applies only to attenuation classes 1…7.
- New: the limit has nothing to do with the attenuation class (or with single player). It depends on **why** the
  entity is a candidate. The full rule for one viewer, per entity (number 1 upward), is:
  1. Candidate: not flagged "no client" (server flag 0x01) and at least one of: model index ≠ 0, effects ≠ 0, looping
     sound ≠ 0, an occupied effect slot, first event ≠ 0, renderfx Ghoul bit 0x400000.
  2. The viewer's own entity and entities with server flag 0x100 ("always send") are sent, nothing else is tested.
  3. Visible = the area test (the entity's area or second area is connected to the viewer's area) **and** the PVS
     test (any of the entity's clusters is in the viewer's fat PVS). For an entity whose cluster list overflowed
     (it touches too many clusters), the PVS test is the head-node test as in Quake II, and in addition it fails when
     cvar `sv_no_big_ents` > 0.5, or when cvar `sv_flash_big_ents` > 0.5 and the server frame number is odd.
  4. **Sound-only entity** = a candidate whose looping sound ≠ 0 and whose model index, effects, effect slots, first
     event and Ghoul bit are all zero. Such an entity is sent only when its distance from the viewer's eye point
     (player-movement origin / 8 + view offset; the remote-view origin when a remote view is active) is **≤ 1200**;
     beyond that it is never sent, **whether or not it is visible**. Within 1200 it is sent; it gets 0x80 when it is
     not visible.
  5. Any other candidate is sent when visible, or when it has a looping sound (then with 0x80 if not visible), at any
     distance.
  6. A candidate that is not sent and has a first event within 1000 units produces svc_culledEvent (unchanged).
  Bit 0x80 is added to the copy sent to that viewer only.

  The kos1 entities are `environ_soundgen`s: SPEC-771 B.4 gives every active generator effects bit 0x100, so they are
  not sound-only and are sent at any distance with 0x80 — the observation is right and is explained by rule 5, not by
  the attenuation class. Rule 4 only drops entities whose sole network content is a looping sound (no model, no
  effects bit, no Ghoul bit); movers and props always have a model or the Ghoul bit and are not affected. Replace the
  sentence of §3.1 by rules 4 and 5. OpenMerc (cut by attenuation class) **does not match**: a class 1…7 sound entity with a model or
  effects is never cut by the original, and a class-0 sound-only entity is.
- Verify: test map with two point entities playing the same looping sound 2000 units from the player in open view:
  one with effects 0 and no model, one with effects bit 0x100. The original's client demo contains only the second
  (with renderfx 0x80 only when it is outside the PVS); move within 1200 units and both are present.

#### I1b.3 SPEC-010 "Lifetimes and resets" — order at server shutdown (Resolution)

- Old: "Server shutdown | server methods 15 (and 8 unless the server is only reconnecting)". Order not stated.
  OpenMerc: drop-all (15), game `Shutdown`, reset (8), unload.
- New: the original's server shutdown (`killserver`, `quit`, an error that ends the server, and the restart at the
  start of a new game) runs these steps in this order:
  1. if the client array exists: the final message to every client (`disconnect` or `reconnect`, SPEC-201), sent
     twice;
  2. game export `Shutdown`;
  3. the game module is unloaded (the export table is no longer used after this point);
  4. the server's string packages are released; a demo that the server is playing back (`demomap`) is closed;
  5. the level state is cleared; the client array and entity-state arrays are freed; a running `serverrecord` file
     is closed; the static server state is cleared;
  6. Ghoul server-system method 15;
  7. Ghoul server-system method 8, **only when not reconnecting**. The restart that `map` (a new game) or a savegame
     load performs while a server is running passes "reconnect" (`Server restarted`), so it skips method 8;
     `killserver`, `quit` and fatal errors pass "disconnect" and do call it.
  So both Ghoul calls come **after** the game has shut down and has been unloaded; nothing of the game module runs
  after step 2, and methods 15 and 8 never call back into it. Add this list to SPEC-010 "Lifetimes and resets".
  OpenMerc (15 before `Shutdown`, unload last) **does not match**; the safe order is the original's (`Shutdown`,
  unload, 15, then 8).
- Verify: tracing proxy game module: log `Shutdown` and wrap the Ghoul server interface the game receives; at
  `killserver` the log shows `Shutdown` and no later Ghoul call through the game, and the engine's calls 15 and 8
  follow. `map` while a game is running (restart) shows 15 without 8.

#### I1b.4 SPEC-302 §3 / SPEC-203 — commands after `map` wait for the client's `begin` (Resolution)

- Old: not in a spec. Observation: the original holds back the commands that follow `map` in the same buffer
  (`+map kos1 +exec x`) until the local client's `begin`; `record` in x prints "You must be in a level to record.".
  OpenMerc restored the Quake II deferred buffer.
- New: confirmed; this is Quake II's deferred command buffer. Add to SPEC-302 §3:
  - When `map` (or `gamemap`, or the map change of `nextserver`) has spawned a level that is not a `.dm2` demo, all
    text still waiting in the command buffer (the commands that followed the map command: the rest of the command
    line, of an exec'd file, or of the same console line) is moved into a single deferred buffer, and the command
    buffer is emptied. A second map change before any `begin` replaces the deferred text (it is not appended). For a
    `.dm2` demo nothing is deferred.
  - When a client's `begin` is accepted for the current level (spawn count matches, the server is not playing a
    demo), the game's `ClientBegin` runs first, then the deferred text is inserted **in front of** whatever is in the
    command buffer, and the deferred buffer is emptied. If the deferred text plus the waiting text exceeds 8192 bytes,
    `Cbuf_AddText: overflow` is printed and the deferred text is dropped (the waiting text stays).
  - Any client's `begin` releases it, so on a dedicated server started with `+map x +exec y`, `y` runs when the first
    client enters the level. In single player the local client's `begin` arrives before the client is active (the
    first `svc_frame` has not been parsed yet), hence "You must be in a level to record." for a `record` in the
    deferred text.
  Add a reference in SPEC-203's `begin` row ("then the deferred commands are released, SPEC-302 §3"). OpenMerc
  **matches**.
- Verify: done (observation). Dedicated server `+map dm/jpndm2 +exec y` with `y` printing a line: the line appears
  when the first client enters, not at start-up.

#### I1b.5 SPEC-311 — `serverrecord` never writes a frame block in 1.07f (Correction)

- Old: SPEC-311 "Server demo contents" 2: "Then, every server frame, one block …"; 3: "`serverstop` writes −1 and
  closes." Observation: in single player (kos1, 40 s) the original's file held only the configstring block (2594
  bytes), no frame blocks; the clean team assumed a killed process and a lost buffered tail.
- New: the file is complete, the process was not killed by the tester. The run's own log (`sof.log` of that session)
  shows `ERROR: Unset entity number` right after `recording to …dm2.`, then the configuration write and the
  renderer shutdown. Explanation:
  1. The per-frame block is attempted once per server frame, right after the frames are sent to the clients, in
     every mode while a recording is open. There is no single-player condition.
  2. To build it, the original walks the entities from number 1. It locates the first through the game's real
     entity size, but then advances by a **fixed 412 bytes** per entity instead of the game's entity size (1124 bytes
     for the shipped game module, export `edict_size`). So from the second entity on it reads misplaced data inside
     other entities.
  3. As soon as such a misread record passes the content test (model index, effects, sound, effect slot or first
     event non-zero, "no client" flag clear), its "entity number" is 0 (or 1024 or more) and the delta writer stops
     the server with the fatal error `Unset entity number` (or `Entity number >= MAX_EDICTS`).
  4. The fatal error shuts the server down (I1b.3) and then ends the program (configuration written, renderer shut
     down). The server shutdown closes the recording, which therefore holds the configstring block and nothing
     else.
  On stock levels this happens at the first frame after `serverrecord`, in single player and multiplayer alike. A
  1.07f server demo with frame blocks cannot be produced with the shipped game module. SPEC-311 item 2 describes
  the intended layout, which the original never reaches.

  `serverstop` (correction of item 3): it does **not** write a −1 block. It closes the file, prints `Recording
  completed.` and marks the recording with an invalid handle (−1) instead of "none". In 1.07f this is only reachable
  when `serverrecord` and `serverstop` run in the same frame. The next frame then fails as in step 3, and the server
  shutdown tries to close the invalid handle (UNVERIFIED: expected to fault, because that handle reads the file-table
  slot before the first entry, which is not a file).

  Recommendation for OpenMerc: do not reproduce the fatal error. Write the frame blocks of SPEC-311 item 2, write no
  −1 block, and treat `serverstop` as close-and-forget (Quake II). Note that original 1.07f never produces such
  files, so nothing has to stay compatible with them. OpenMerc **differs** from the original by design if it
  follows this recommendation; the gatekeeper may instead require the original's error.
- Verify: done (black box): the kos1 session log of the observation contains `ERROR: Unset entity number`
  immediately after the `recording to` line. Second check: `serverrecord x` on a deathmatch map with only the
  listen player gives the same error at the first frame.

#### I1b.6 SPEC-001 "How the engine drives the exports" — frame counter, returned time, fast-forward (Refinement)

- Old: "Every server frame (100 ms): the frame counter is incremented and `RunFrame(counter)` is called …; the
  returned float is the game time (the original returns counter × 0.1)." SPEC-AMEND-006 G18.30 corrected the game
  side. Observation (proxy module): `RunFrame(0)`, `RunFrame(1)` while loading, then 1, 2, 3 … equal to the wire frame
  number; the game returns 0.1, 0.2, 0.3 …; on the wire game time = (frame + 2) × 0.1. OpenMerc follows this.
- New: confirmed; the engine side, exactly:
  1. At level spawn the server's frame number is reset to 0 and its millisecond time to 1000. After
     `SpawnEntities` the engine calls `RunFrame(0)` and then `RunFrame(1)` — literal arguments, not the frame
     number — and stores each returned value as the game time.
  2. Every server frame: frame number := frame number + 1; server time (ms) := frame number × 100 (so the first
     frame of a level has number 1 and time 100 ms); then `RunFrame(frame number)` and game time := the returned
     float. The wire frame number of svc_frame and of server demos is this frame number; the game time sent is the
     last value returned.
  3. Not called at all (frame number and server time still advance, game time stays): while cvar `paused` ≠ 0 and
     `maxclients` ≤ 1; while a demo plays.
  4. After every call: if the server time is behind the real time, the real time is set back to it (`sv_highclamp`
     printed when `showclamp` ≠ 0), as in Quake II.
  5. Cinematic fast-forward (SPEC-001 "Cinematic fast-forward", exact counts): a counter c starts at 2 and is reset
     to 2 whenever `sv_jumpcinematic` = 0 or the game's cinematic state is 0 after a call. While both are non-zero,
     each server frame: the frame with c = 2 runs one game frame and does the start-up (`s_disable` 1, state 2,
     mirror into the first client); the next frame (c = 1) runs one game frame; from the frame with c = 0 on, the
     engine repeats `RunFrame` with the **same** frame-number argument until 129 calls (first batch) or 128 calls
     (later batches) have run in that server frame, or the state drops to 0.
  Consequence for "game time = (frame + 2) × 0.1": it holds from the level start only until the first pause, game
  `freezeworld` (G18.30) or fast-forward. A pause lowers the offset by one frame per paused frame; each extra
  fast-forward call raises it by one. The game ignores the argument for its time (G18.30); only its "small frame
  numbers" precache decision depends on it. Replace the SPEC-001 bullet by rules 1–5. OpenMerc **matches** for
  rules 1–2 (observed sequence); rules 3–5 to be checked.
- Verify: proxy module under the original engine: in single player open the menu for 2 s (pause) — the logged
  RunFrame arguments jump by about 20 while the returned time continues by 0.1; skip a cinematic (fire button during
  kos1's intro) — many calls with the same argument in one server frame, in batches of 129 then 128.

### B. G4 roster, helicopters, vehicles, special characters (items 11–17 and one supplement)

#### G4.11 SPEC-731 A header, SPEC-AMEND-006 G4.2 and SPEC-760 B3.6a — two different angle normalisations (Resolution)

- Old: SPEC-731 A header: "anglemod(a) is the Quake II angle normalisation to [0, 360) with 1/65536-turn
  resolution"; SPEC-AMEND-006 G4.2 step 5: "a = anglemod(d) (SPEC-760: 16-bit truncation)". SPEC-760 B3.6a: the
  shared `anglemod` reduces into [0, 360) by repeated ±360 without 16-bit quantisation. The clean team asked which
  statement is right.
- New: **both are right, each for its own subject; only the reference "SPEC-760: 16-bit truncation" in G4.2 is
  wrong.** The game module has two normalisations:
  1. The shared function of SPEC-760 B3.6a (used where Quake II calls `anglemod`): repeated ±360 until the value is
     in [0, 360); no quantisation. G18's shared code is right to follow B3.6a.
  2. The helicopter code does not call it. Every normalisation in the helicopter routines uses its own 16-bit form,
     here called **am16**:
     am16(a) = ((trunc(a × 65536/360)) mod 65536) × 360/65536,
     where trunc rounds toward zero and "mod 65536" keeps the low 16 bits of the two's-complement integer (so a
     negative input wraps into [0, 360)). Factors: 182.04444 (65536/360, double) and 0.0054931640625 (360/65536,
     double). Example: am16(−3.8) = 356.2042 (trunc(−691.77) = −691, + 65536 = 64845).
  The helicopter uses am16 in exactly these places (all other angle arithmetic is plain floating point):
  | Place (SPEC-731 A) | What is normalised |
  |---|---|
  | §9.3 turn step | the direction test a = am16(d); the stored yaw after each 9° step; the bank angles A (target yaw − reference yaw) and B (current yaw − reference yaw) before folding |
  | §9.8 level out | the roll at the start of every level-out step (see G4.12 (c)) |
  | §9.2 fly a leg, §9.5 strafe | the pitch (leg) and roll (strafe) before each ease step, followed by "−360 when above 180" |
  | §13.5 rocket dip | the pitch before each dip step, followed by "−360 when above 180" |
  | firing-cone test (§9.7 outside auto-combat, §11.2 step 3) | all three entity angles, **written back** to the helicopter (side effect: after the test, pitch, yaw and roll are stored in [0, 360)) |
  Replace in the SPEC-731 A header "anglemod(a) is the Quake II angle normalisation …" by "am16(a) is the
  helicopter's own 16-bit normalisation (formula above); it is not the shared `anglemod` of SPEC-760 B3.6a", and in
  G4.2 step 5 read "am16" for "anglemod (SPEC-760: 16-bit truncation)". The difference between the two forms is at
  most 0.0055° per call, but the write-back of the firing-cone test and the representation in [0, 360) change the
  angle bytes sent to clients (SPEC-201 angle byte truncates toward zero, so a yaw stored as 255.9 is sent one step
  lower than the same yaw stored as −104.1). OpenMerc **matches** for the turn step; it must use am16 in the other
  places of the table too, including the write-back of the cone test.
- Verify: unit test am16(−3.8) = 356.2042, am16(−100) = 260.0024, am16(359.99999) = 359.9945, am16(720.003) = 0.0.
  Black box: a helicopter in an engaging hover (id 34, outside auto-combat) at yaw −100 with the target in view
  (cone test runs): before the first hover frame its yaw byte is 185 (−99.8°), afterwards 184 (−101.25°), because the
  write-back stores 260.0024.

#### G4.12 SPEC-AMEND-006 G4.2 rule 4 and G4.6 rule 7 — the frame after a snap and the go-to's end (Resolution and Correction of the OpenMerc reading)

- Old: (a) go-to start now identical. (b) Original: final-leg arrival A → next scripted HELI command's first step at
  A + 4; OpenMerc A + 3 "as G4.6 says". (c) Roll after a face turn's snap: original −7.0, −7.0 (snap), −5.6, −1.4, 0;
  OpenMerc −7.0, −7.0, −4.2, 0; after the strafe's arrival test: original −14.1, −14.1, −11.2, −7.0, −2.8, 0. A trial
  that delayed "facing" and the go-to's end by one frame fixed (b) and (c) but moved the script MOVE after trn1's
  first face turn two frames late.
- New: G4.2 rule 4 and G4.6 are right as rules; both differences come from details outside them. The trial variant
  is wrong and must be dropped.
  (a) Acknowledged.
  (b) **Every HELI command routine only installs its working routine as pending; the helicopter does not change in
  the command's own frame.** This holds for the face commands (12–15: the turn routine), look/aim (16–21), attack
  (28–30), hover (33/34), strafe (22/23), take-off, return home and the go-to commands (5–10: the go-to routine,
  which itself spends one more frame, G4.6). So after a go-to whose last leg arrives at A (level helicopter):
  | Frame | What happens |
  |---|---|
  | A | leg arrival: snap to P1, arrive routine pending |
  | A + 1 | arrive: level, routine done; the selector sees the go-to flag and installs the go-to routine (no frame of its own) |
  | A + 2 | go-to routine: origin = G, done, go-to flag cleared; nothing selected; the action completes; the next queued action becomes current |
  | A + 3 | the next command's routine runs (e.g. the face command stores its point and installs the turn routine as pending); no visible change |
  | A + 4 | first visible step (first 9° turn step of the face command) |
  "Next command's first step A + 3" in G4.6 means the command routine's frame, not the first visible change. G4.6's
  sentence "Face, strafe and other commands install their working routine directly from the command frame and have
  no such extra frame" is to be read: they install it in the command frame as pending, so their first visible step
  comes one frame after the command frame; only the go-to commands have one frame more. OpenMerc **does not match**:
  its face command turns in its own frame.
  The same rule applies at a command's start: command frame N; face/strafe/hover/attack first visible step N + 1;
  go-to first movement N + 2.
  Level-out length: the arrive routine is done only in a frame in which both the roll and the pitch snap (roll within
  4°, pitch within 1.5°, see (c)). A leg that arrives with pitch above 1.5° spends one level-out frame per 1.5°
  (jpn3: a leg arriving with pitch about 4.6° needs 4 frames: 4.6 → 3.1 → 1.6 → 0.1 → 0, and only the fourth frame
  reports level), and every following frame in the table moves accordingly.
  (c) The original **does** level out by 4° in the frame after the snap (or after the strafe's arrival-test frame),
  as G4.2 rule 4 and SPEC-731 §9.8 say. The smaller first change seen in the capture comes from where the roll is
  stored, as follows.
  1. The turn routine (§9.3) and the strafe keep the roll in (−180, 180] (a value above 180 has 360 subtracted
     before the 2° or 3.5° step), so a banked roll is stored negative, e.g. −8.3.
  2. Each level-out step first takes R = am16(roll) (G4.11), which turns a negative roll into a value in [180, 360).
     If R = 0, or R + 4 > 360, or R − 4 < 0: roll := 0 and the step reports "roll level". Otherwise roll := R − 4
     when R ≤ 180, else R + 4. The new roll is **stored in [0, 360)** (e.g. 355.7, not −4.3).
  3. The angle byte (SPEC-201) truncates toward zero, so a roll stored as 355.7 goes out as byte 252 (−5.6°) while
     −4.3 goes out as byte 253 (−4.2°).
  Worked examples (both match the captures): face turn, roll −8.3 at the snap: sent −7.0; then stored 355.70 (sent
  −5.6), 359.70 (sent −1.4), 0. Strafe, roll −14.5 at the arrival test: sent −14.1; then 349.50 (−11.2), 353.50
  (−7.0), 357.50 (−2.8), 0. OpenMerc **does not match** while it stores the level-out roll as a signed value. The
  level-out pitch has a related quirk: before its 1.5° step a pitch above 180 is replaced by 360 − pitch (the sign
  flips, e.g. a pitch stored as 355 is treated as +5), a pitch below −180 gets +360; with |pitch| ≤ 1.5 it snaps to 0.
  Pitch is stored above 180 only after the firing-cone write-back (G4.11).
- Verify: trn1 (helicopter 147): arrival 146, go-to complete 148, face command 149 (no change), first turn step 150
  (yaw −2.8 → −14.1, roll 1.4); the face turn's snap 170, level-out 171–174, script MOVE 175. Unit test of the
  level-out with roll −14.5 gives the byte sequence 246 (−14.1), 248, 251, 254, 0.

#### G4.13 SPEC-731 A §9.5 — when the strafe fires (Correction)

- Old: §9.5 "When v ≤ 20 the barrel stops. Every frame the gun fires (if on) and rockets fire at the normal rate (if
  on)." Observation (trn1 strafe, id 22, gun on): 30 rounds from the 4th frame (v = 21.3) to the frame before the
  arrival test; none while v ≤ 20. OpenMerc now fires only when v > 20; rockets not observed.
- New: the observation is right; the two sentences are one rule. Each strafe frame, in this order: arrival test
  (G4.3: when it holds, v := 0, `barrel_still`, arrive pending, nothing else this frame); speed change; roll step;
  move; then, with the speed v reached in this frame: **if v ≤ 20 the barrel stops (when the gun is on and the barrel
  is spinning) and nothing is fired; otherwise one gun round (if the gun is on) and the normal-rate rocket rule
  (§13.4: if rockets are on, rockets are left and the rocket timer has passed).** Rockets therefore follow the same
  rule as the gun. The first round starts the barrel spin (§13.2). Replace the two sentences of §9.5 accordingly.
  OpenMerc **matches** for the gun; it must apply the v > 20 condition to rockets too.
- Verify: trn1 strafe of 2 092 units with gun on: frames with v = 5.3, 10.7, 16.0 fire nothing, the frame with
  v = 21.3 fires the first round; no round in the arrival-test frame (30 rounds). With rockets on (`HELI 24` before
  the strafe) the first rocket leaves in the first frame with v > 20, then one per second.

#### G4.14 SPEC-731 A §9.9, SPEC-791 §11 and SPEC-AMEND-006 G4.10 — how the aim limits act on a bolted part (Resolution)

- Old: G4.10: pilot head yaw range ±1 rad, step 0.314159, pitch off; "the exact mapping of the measured angle to the
  visible turn for such parts is UNVERIFIED". SPEC-791 §11: "the same desired angles are computed in the frame of
  the part's bolt … the part's own transform is rotated by the clamped yaw change about its z axis". Observation
  (trn1, face turn of 180°): visible head yaw −18.3, −36.1, −54.2, −71.7, −77.4, −78.7 (held); OpenMerc with ±1 rad on
  the visible yaw stops at −57.2.
- New: G4.10's records are right; the limits are not applied to the visible turn. The aim controller of SPEC-791 §11
  works with **two frames** when it turns a bolted part. Replace the "Turn a bolted part" paragraph of SPEC-791 §11
  by these steps (one update):
  1. **Part frame F**: the matrix of the part's own attach bolt (mode 5, through parents per flag), which includes
     the part's current rotation, followed by the entity transform (origin and all three angles, G4.9).
     **Parent frame Q**: if the part hangs directly on the entity's main model, the entity transform itself (origin
     at the entity origin, not at the parent bolt); otherwise the parent part's bolt matrix (mode 5, through parents)
     followed by the entity transform.
  2. **Change towards the target** (the normal case): the target point expressed in F gives
     ΔY = −atan2(y, x) and ΔP = atan2(z, √(x² + y²)), i.e. the turn still needed from the part's current x axis.
  3. **Angles for the limit test**, all in Q and normalised to (−π, π]: desired dY, dP = the same formulas for the
     target point expressed in Q; current cY, cP = the same formulas for the point C expressed in Q, where
     C = (F's position x, F's position y, Q's position z) + 100 × (F's x axis). (So both directions are seen from Q's
     origin, and the current point takes its height from Q's origin, not from the part — an original quirk that only
     matters for pitch.)
  4. **Limits** (yaw only if yaw is enabled, pitch only if pitch is enabled): if dY < yaw min − 0.0005 then
     ΔY := yaw min − cY; else if dY > yaw max + 0.0005 then ΔY := yaw max − cY; result "limited". Pitch likewise with
     its range.
  5. **Dead zone**: |ΔY| ≤ 0.01 → 0; |ΔP| ≤ 0.01 → 0.
  6. **Step**: ΔY and ΔP are clamped to ± maximum step (for an enabled axis); result "turning" when clamped.
  7. **Back inside**: if cY > yaw max + 0.0001, ΔY := max(yaw max − cY, −step); else if cY < yaw min − 0.0001,
     ΔY := min(yaw min − cY, step). This replaces any earlier ΔY (also a dead-zone 0). Pitch likewise.
  8. A disabled axis gets change 0. The controller keeps cY + ΔY and cP + ΔP as its output angles.
  9. Result "aligned" when |ΔY| < 0.001 and |ΔP| < 0.001 and the result is not "limited".
  10. The part's transform becomes: rotation by ΔP about its own y axis, then by ΔY about its own z axis, both
      applied in the part's local space before its current transform.
  **Mapping for the crew heads.** A crew head hangs on the main model, so Q is the entity frame and C is seen from
  the entity origin. The head's attach bolt lies ahead of the origin (pilot about 44.2 units, gunner about 84.8 units
  along the entity's x axis), so cY = atan2(100 sin θ, D + 100 cos θ) for a visible head yaw θ and bolt distance D.
  The ±1 rad limit on cY is reached at θ ≈ −79.1° for the pilot (≈ −103° for the gunner). Approach: while the look
  point is outside the range, ΔY = −1 − cY, limited to 18° per update, so the last steps shrink as cY nears −1; once
  ΔY ≤ 0.01 rad the dead zone holds the head. Pilot, from θ = 0: cY 0 → step −18°; θ −18°, cY −0.218 → −18°;
  θ −36°, cY −0.439 → −18°; θ −54°, cY −0.666 → −18°; θ −72°, cY −0.902 → −5.6°; θ −77.6°, cY −0.979 → −1.2°;
  θ −78.8°, cY −0.996 → dead zone, held. This is the captured −18.3 … −78.7. The 90° turn of the `TO_BODY` bolt plays
  no part beyond fixing which way the head's x axis points (forward at rest). OpenMerc **does not match** while it
  limits the visible yaw; it must measure the limit as in steps 3–4.
- Verify: trn1 face turn of 180°: the pilot head's visible yaw −18, −36, −54, −72, −77.6, −78.8 (held) with the
  steps above; with the look point straight ahead again the head returns by ΔY of step 2 (18° steps, identical in
  both from −43.1 on). Unit test: part bolt 44.2 units ahead of the entity origin, range ±1 rad, step 0.314159, target
  behind the helicopter: six updates give the θ sequence above and the seventh changes nothing.

#### G4.15 SPEC-731 A §9.3, §9.7, §9.8, §11.3, §13.1 — which turns bank (Resolution)

- Old: §9.3: bank "only in the turn routine, §9.8, and when requested"; §9.7 look-around "turns towards it (§9.8
  turn)". Observation (jpn3 auto-combat): the original rolls up to about 11° in every in-place turn; OpenMerc now
  banks them.
- New: the bank is decided by the routine, not by a request flag:
  | Turn | Bank and fire-while-flying handling |
  |---|---|
  | the **turn routine** (§9.8): face commands 12–15, the hover's look-around (§9.7), combat facing (pattern 0 step 1, pattern 1 step 1), and the attack's turn after its first step (§13.1, G4.16) | **always banked** (§9.3 bank rule, reference yaw = the yaw when the turn routine was installed), with the fire-while-flying rule of §9.3; facing → arrive (level-out) |
  | the leg's turn towards P1 (§9.2), the attack routine's own turn step (§13.1), hover-fire's tracking turn (§13.1) | never banked, no firing from the turn itself |
  Replace "and when requested" in §9.3 by this table. OpenMerc (banking them now) **matches**.
- Verify: jpn3 `g4jpn3c`: every in-place turn of more than 9° rolls (up to about ±11° for 180°), legs never roll.

#### G4.16 SPEC-731 A §13.1 and §11 — attack turn and the frames of the combat step (Correction)

- Old: §13.1 "turn towards the attack point (no bank). While not facing, the turn routine runs." (a) The original
  banks the attack turn and levels out at 4°/frame before the dip. (b) After a combat go-to arrives at A the next leg
  moves at A + 4 in the original, A + 7 in OpenMerc (G4.6 frames plus a combat routine installed by the selector, a
  frame for choosing the pattern, a frame for the combat step).
- New:
  (a) Attack routine (rocket branch, and the gun branch when the point is outside the gun's limits): each frame in
  which the attack routine itself runs it makes one **unbanked** turn step (§9.3 without bank). If that step does not
  report facing, it sets the turn routine's destination to the attack point and its reference yaw to the current
  yaw and installs the **turn routine** as pending: from the next frame the turn is banked (G4.15) and, when facing,
  the arrive routine levels out (4°/frame, G4.12 (c)). When level, the selector finds the attack flag still set and
  installs the attack routine again; it now snaps (facing) and continues (dip, vertical leg or hover-fire). jpn3
  `g4jpn3d` (helicopter 217): unbanked step 251 (+9°), banked turn 252–269 (roll to −11.2/+11.2), snap 270, level-out
  271–273, attack routine 274 (facing, dip installed), dip 275. Amend §13.1 accordingly. OpenMerc **matches** now.
  (b) **The selector runs the combat step itself, in the frame in which the current routine reports done; the
  combat step is not installed as a routine** (it is the action's routine only right after id 41). Rules:
  1. When a pattern is active, the combat step runs the interjection test and, if nothing interjects, the pattern's
     next step in that same call; what it starts (go-to, turn, hover, attack) is installed as pending and runs from
     the next frame.
  2. When no pattern is active (or the active one ends in this call), the call only chooses the target and the new
     pattern and reports done. That report is discarded (the selector has "selected" something), so the routine
     that was current runs once more in the next frame; when it reports done again (an arrive or a go-to routine at
     G does so at once) the selector runs the combat step again and the new pattern's first step runs. Choosing a
     pattern therefore costs one frame.
  3. Interjections are decided inside the same call.
  Frames after a combat go-to whose last leg arrives at A, with the pattern still active: A + 1 arrive (done; the go-to
  routine is installed directly), A + 2 go-to routine at G (done) and, in the same frame, the combat step's next step
  installs a new go-to as pending, A + 3 go-to routine chooses the leg, **A + 4 the leg moves**. With a pattern change
  in between: A + 5. OpenMerc **does not match** (A + 7): it must not spend frames on installing the combat step or on
  a separate combat step after the pattern choice.
- Verify: jpn3 `g4jpn3d` original: arrival 133 → leg moves 137; an intermediate cruise point of a multi-leg go-to
  (no combat step involved): arrival 381 → 384 (G4.6 rule 7); a leg arriving with pitch 4.6° (four level-out frames):
  arrival 314 → 320.

#### G4.17 SPEC-731 A §11.2, §13.1, §13.4, §13.5 — weapon choice and rockets in auto-combat (Correction of the observation's reading; Refinement of §13.4)

- Old: §11.2/§13.1/§9.7: with rockets on, every attack against a visible target is a rocket attack (t = −1/−2).
  Observation (jpn3 `g4jpn3d`, ids 24 and 26, spawnflag 4, `volume` 10): original run 1 two single-rocket dips then
  it leaves, 7 gun rounds and 1 rocket explosion in 50 s; run 2 hovers 20 s facing the player, 84 gun rounds, no
  rocket; OpenMerc a dip every 0.8 s with 45–50 rocket explosions. Question: what makes the original use the gun
  (time-mode attacks) and fire almost no rockets?
- New: **the weapon choice of the spec is right and the original does not use time-mode gun attacks here; it fires
  its rockets, but they miss the player and vanish on a sky surface.** Details:
  1. With rockets on and rockets left, every attack is a rocket attack exactly as §9.7/§11.2 say; a time-mode (gun
     burst) attack happens only with rockets off or none left (with spawnflag 4 never). The original run 2 shows the
     same hover-engage loop as OpenMerc: from the hover point (−351, 1040, 186) a dip every 7–9 frames for 20 s
     (hover → attack → dip → level-out → hover).
  2. The gun rounds are the dip's own rounds (§13.5): the gun fires in every dip frame until both dip phases are
     done, i.e. 2 rounds when phase 2 snaps at once and 3 when phase 2 needs a 5° step (dip pitch above 3.33°). This is
     the 2–3-round burst every 0.8 s of run 2 (84 rounds).
  3. Every dip fires its forced volley (1 or 2 rockets, §13.5); in both original runs two rocket entities appear at
     the two pod positions on each dip frame and are gone 7–8 frames later.
  4. **Aim (refines §13.4 "Aim: straight at the combat target's origin")**: the direction of every rocket is the unit
     vector from the **helicopter's origin** to the target's origin (the body's combat target, or the §11.1 choice when
     there is none), computed once per rocket, while the rocket starts at its pod bolt position plus the tube offset.
     The rockets of one volley therefore fly **parallel** to the line origin → target, displaced by the pod offsets
     (about 50–70 units sideways and down on the Cobra), and pass beside a target narrower than that offset. On
     `g4jpn3d` they pass 23 and 34 units to either side of the standing player.
  5. The ground under and around that player position is a sky brush (`Tokyo/sky`, surface flag 4, top face at
     z = −128). A rocket touching a sky surface is removed without explosion, damage or effect (§13.4 "on a sky
     surface it just disappears"). Hence no explosion reaches the client. OpenMerc aims each rocket from its own pod
     at the target, so both rockets converge on the player's box and explode there (its explosions sit on the box
     face, y = 668.1).
  6. Run 1 leaving after two dips is the random combat choice (§11.2: with aggression 10 an interjection happens with
     probability 10/11, and then half of them reposition), not a weapon rule.
  7. Refinement of §13.4: the rocket's value 0.02 is the entity's **gravity factor** (SPEC-760 field "gravity"), not
     an air-friction value: rockets drop slowly (vz falls by sv_gravity × 0.02 × 0.1 = 1.6 units/s per frame at
     `sv_gravity 800`; the captured rocket's vertical step grows by about 0.2 units per frame).
  OpenMerc **does not match**: it must take the rocket direction from the helicopter's origin (item 4) and the 0.02
  gravity factor (item 7); its weapon choice and dip cycle already match.
- Verify: original `crew_orig_g4jpn3d` dip at frame 263: rockets at (−316.8, 968.4, 119.9) and (−415.0, 993.2, 120.8),
  both moving (−12.0, −47.2, −35.4…−36.4) per frame, i.e. 600 × normalise(target − helicopter origin) per second,
  parallel; removed at frame 270 without effect. Black box: the same test with the player on solid ground (no sky
  brush) shows two explosions per dip beside the player, displaced by the pod offsets, and the player outside both
  radius-150 spheres' centres by those offsets.

#### G4.S1 (supplement, requested in docs/impl/G4.md) SPEC-720 §12 "Helicopter" and SPEC-731 A §12.2 — which sub-models count for crew kills (Resolution)

- Old: SPEC-720 §12: "Hits on two marked sub-models (pilot/gunner glass, UNVERIFIED) by a player holding the sniper
  rifle (weapon 6) count down a shot counter …; hits on a third marked sub-model do full damage". SPEC-731 A §3:
  "UNVERIFIED whether pilot and gunner share the counter". Observation: sniper hits aimed at the head bolts and the
  front glass killed no crew member in the original.
- New: the marked sub-models are **bolt-on instances, not glass**:
  | Hit instance | Effect |
  |---|---|
  | the head bolted at `body2` (pilot) | sniper rule → pilot killed; otherwise full damage |
  | the head bolted at `body1` (gunner) | sniper rule → gunner killed; otherwise full damage |
  | the `canopy` bolt-on | full damage (no crew effect) |
  | anything else (main model, rotors, gun) | trunc(damage × 0.1) |
  The shot's Ghoul hit list is scanned in order; the first pilot-head or gunner-head record decides at once; a canopy
  record makes the hit a full-damage hit unless a head record follows; a list with neither is a hull hit. The sniper
  rule applies when the attacker is a client whose current weapon is 6 and crew kills are allowed (SPEC-731 A §3:
  not with spawnflag 16 at toughness ≥ 2): the **shared** counter (starts at 1, common to pilot and gunner) is
  decreased; at ≤ 0 that crew member is marked killed (so the first qualifying head hit kills, and every later one
  kills the other crew member at once). A shot through the canopy counts for the crew only if the trace also hits the
  head instance behind it. On jpn3 the helicopter has spawnflag 16, so at toughness ≥ 2 no crew kill is possible at
  all; at lower toughness the shot must hit a head model. OpenMerc **does not match** while it uses glass regions.
- Verify: kos2 or trn1 (no spawnflag 16), `skill 1`, sniper rifle: one hit on the pilot's head model → canopy blood
  variant 2 and the crash command (§12.2); a hit on the canopy only → health −damage, no crew effect; with spawnflag 16
  and `skill 2` a head hit only takes full damage.

### C. G2 character AI (items 17–18)

#### G2.17 SPEC-722 cow idle choice and SPEC-AMEND-006 G2.14 — why the bulls of the black-box runs never switch idles (Correction of the G2.14 test, Refinement of SPEC-722)

- Old: SPEC-AMEND-006 G2.14: a cow without a target asks its idle choice at every think (two 1/60 chances) and
  switches about every 3 s; the sud2 start cows keep `std_i_n` because they hold the player as target. SPEC-722 cow
  body: "with probability 1/60 `std_imoo_n` …, else with probability 1/60 `std_ilooking_n`, else `std_i_n` (each only
  if the model has it)". Black box (arm1, `gimme m_x_bull` 100 units in front of the player, facing away, with and
  without `ninja`): the bull stays in `std_i_n` for 23 s. OpenMerc keeps the animation length as the duration of cow
  idle actions.
- New: the planning rules of G2.14 are right (a cow without a target plans a stand at every think and its idle choice
  rolls the two chances), but the G2.14 test could not show them, and SPEC-722 leaves out a condition that decides
  the arm1 and sud2 pictures:
  1. **The two alternatives must be "allowed".** `std_imoo_n` or `std_ilooking_n` is returned only when all three
     hold; otherwise the choice returns `std_i_n` (the roll is not repeated):
     - the record's flags share no bit with the caller's mask (the plain stand passes mask 0);
     - the model has the sequence (for families other than meso, ecto and female: the model object knows the
       sequence `ghoul/<model directory>/<record name>.ghl`);
     - the AI can take the record's bounding-box preset (SPEC-700 §15 "changing the preset", without the retry
       that keeps the old horizontal extents). If that preset is already in use the answer is always "yes" (even
       when the floor smoothing of §15 is refused). Otherwise the new box is traced in place with mask 0x2020003; it
       is applied at once when clear, else the alternative is refused.
     All three cow records use preset 0. For `m_x_bull` (size scale 1.5, preset 0 = (−24, −14, −20)–(36, 14, 20))
     that box is (−36, −21)–(54, 21) horizontally, in world axes (presets are never turned with the yaw), with the
     current bottom kept and a height of 60.
  2. **Start state.** A new AI has no preset in use (value 9). Every stand action also asks for its record's preset
     when it starts playing (`std_i_n` is preset 0; a refusal is ignored there). So where the preset-0 box fits, the
     first stand action takes it, and every later check is "yes" without a trace: the cow switches as G2.14 says.
     Where it does not fit, every roll is refused and the cow keeps `std_i_n` indefinitely.
  3. **The arm1 runs.** The gimme bull was spawned 50 units up with the player's yaw −90. The right-angle box rotation
     (SPEC-770 §3 step 5) turns only for 90, 180 and 270 after truncation, so −90 leaves the class box
     (−30, −14, −33)–(56, 14, 26) unturned. At the spawn point (−163.875, −27.875, 138.125) that box overlaps world
     brushes by about 4 units along x, so the bull can never move: the capture shows origin z 138.125 from 3.0 s to
     the end and a packed box from 3.1 s on (it woke, but every physics move starts in solid). The preset-0 box
     overlaps the same brushes by about 2 units, so both alternatives are refused on every roll. The gimme soldier at
     the same spot (box ±16) fits and falls normally.
  4. **The sud2 start cows** (three `m_x_bull` at x −504, y −424/−480/−536, origin z 28, angle 360): their class box
     reaches 5 units into the floor (bottom at −5) and into the pen's clip and fence brushes, so the start drop to the
     floor fails and their preset-0 boxes are refused too. That alone explains their 11 s of `std_i_n`, target or not;
     the "holds the player as target" reading of G2.14 is not needed (it remains true that a cow with a target plans
     no idle at all).
  5. **Durations** (SPEC-710 D3, unchanged): `std_i_n` (looping, no 0x00400000) gives a stand action of 0 s, so the
     cow re-plans at the next think; `std_imoo_n` and `std_ilooking_n` (0x00400000) give a stand action that ends
     with its animation (at most 4 s). Choosing `std_imoo_n` also asks for the idle sound (`Enemy/Cow/Moo1.wav` or
     `Moo2.wav`, 1/2 each, at most once per 30 s). Mean time to a switch where allowed: 1/(1/60 + 59/60 × 1/60) ≈ 30
     thinks ≈ 3.0 s.

  Add rule 1 to the SPEC-722 cow idle choice (replacing "each only if the model has it") and point SPEC-700 §15 to it
  as a user of the preset change. OpenMerc **does not match**: it lacks the preset test (so open-space cows and
  embedded cows behave alike) and gives `std_i_n` the animation length instead of 0 s. With both fixed it reproduces
  the arm1 and sud2 pictures and the 3 s switching in open space.
- Verify: black box, arm1: `gimme m_x_bull` with the player at a spot where the box fits (e.g. facing yaw 0 or 90 so
  the box turns or lies along a free axis, at least 60 units from walls along x and 25 along y); `ninja` on: the bull
  lands and switches to `std_imoo_n` or `std_ilooking_n` about every 3 s (first switch within 10 s in most runs). Data
  check: the sud2 start cows' class boxes overlap the floor by 5 units at origin z 28.

#### G2.18 SPEC-710 C2/C5 — the soldier that glimpses the player behind it stands still facing away (Resolution)

- Old: question: which C4/C5 branch keeps the original's soldier standing and not turning with the player about 100
  units behind it (arm1, `gimme m_irq_msoldier1`, perceived during a look-around, then `std_istand_n_a_n` for 15 s and
  more at yaw −90)? Is the gimme soldier armed? OpenMerc ran to the player with `std_r_n_pk_n`.
- New: the soldier is armed: `m_irq_msoldier1` (type 42) carries the sniper rifle (right-hand attack 7) and a
  grenade (left 22), profile class 7 at tier 2: profile[0] = 512, profile[1] = 1024, flags 2 and 8, profile[4] = 1.0.
  The branch is the "too close, no retreat, may not attack" stand of C5 step 4:
  1. Range test (C2): the clear-shot test passes (nothing between the eyes), so rule 7 applies: 100 ≤ 0.75 × 512 =
     384 → **too close** (code 2). (With a blocked shot the code would be 3, and without navigation that path moves
     the character 10 units towards the target — the original does not move, which rules it out.)
  2. C5 step 4: navigation is asked for a flee point only when the level has navigation data loaded; without data, or
     when no point is found, the "no retreat" sub-case follows.
  3. *May attack* is false: the reaction age stays below profile[4]·k (1.0, or 0.5 for the designated character).
     The soldier perceived the player only during its look-around (sight from the turned head); once the player
     record is older than 1 s, every sense update sets the reaction moment to now ± 0.2 s (SPEC-701 §8, SPEC-700
     "Perception summary"), so the reaction age never grows while the player stays unseen behind it.
  4. So the plan is **stand**, with the target's last known position as the facing point, and the record comes from
     the human stand choice: `std_istand_n_a_n`. That record has turn limits 0 (the per-record left/right turn
     values that SPEC-711 turning adds to the root-motion turn), so the body cannot turn towards the facing point: the
     soldier stands facing away. The plan repeats every think with the same result.
  5. It changes only when the soldier perceives the player again (fresh record: after reaction age ≥ profile[4]·k it
     may attack; with a clear shot it attacks in place, crouched when crouching is allowed) or when the target is
     forgotten (45 s without a fresh record), after which it plans "stand facing forward" again.
  The "cow-like nothing queued" reading does not apply: an attack record would exist (`std_a_fwd_l_2` etc.). Add to
  C5 step 4 "without: stand (S ← now)": "facing the target's position; the stand choice of a human then returns an idle
  record that cannot turn". OpenMerc's run to the player comes from a different range code or a missing flag 8/2
  (class 7 never approaches when too close). The clean team's black-box picture (no flee point, reaction moment at the
  first sighting) is the same rule. OpenMerc **does not match** (it runs to the player); it matches once it follows
  steps 1–4.
- Verify: black box, arm1: same gimme test, then walk around the soldier into its view and stay there: after about
  1 s (0.5 s if it is the nearest awake character to the player) it turns and fires (`cch_f_fwd_l_2` or
  `std_f_fwd_l_2`). A gimme `m_irq_msoldier1` 450 units away in view: it stays in place (in range) and attacks after
  the reaction delay.

### D. G3 damage model, gore, bodies (item 23)

#### G3.23 SPEC-782 §3 and SPEC-AMEND-006 G3.20 — the missing damage is the bullet falloff measured against the wall behind the target (Correction)

- Old: SPEC-782 §3 rule 2: a Ghoul-model entity is hit where the ray meets the model, "the hit point and fraction are
  moved to the model surface, fraction capped at 0.99"; rule 3: falloff `D / (2f)` beyond f = 0.5 "of the trace"
  (start to start + direction × 3 × range). SPEC-AMEND-006 G3.20 step 1 concluded "no falloff at 61 units" and
  predicted 45 / 22. Black box (`g320_orig_front`, dm/jpndm2, 61 units, attacker looking down, armour 0): each pistol
  hit on the left front shoulder (zone scale 1) took 39 health, each upper-arm hit of the first run 21.
- New: the observation is right and G3.20 step 1 is wrong. The damage is reduced by the ordinary falloff of rule 3,
  but for a Ghoul-model target the fraction f is **not** measured along the 3 × range trace:
  1. The engine first clips the shot against the world, then offers the entities it crosses to the game's filter
     (SPEC-313 §7, SPEC-001 #53) in order of their box fractions. The filter's third value is the end of the
     world-clipped sweep: the world hit point, or the trace end when the world was not hit (see I1b.1 of this
     amendment).
  2. For an entity with a Ghoul model the filter runs the per-model ray test and, when the ray meets the model,
     stores the model hit point P in the trace and sets f = |P − S| / |W − S|, where S is the shot start and W that
     sweep end; f is capped at 0.99 (rule 2). The engine returns this candidate unchanged.
  3. Rule 3 then applies to this f: when f > 0.5 the damage becomes D × 1 / (2f), computed in floating point and
     truncated to an integer when it is passed to the damage function (the knockback gets the same value).
     Attacks whose flags contain 0x1000000 or 0x80000 are exempt as before.
  So a shot weakens whenever the target stands more than halfway between the shooter and the first world surface
  behind it along the shot line — for example a shot aimed downwards at a near target, whose line meets the floor
  behind the target at less than twice the target distance. In the G3.23 capture the shooter's view pitch is about
  31° down (view angles 5632 in 1/65536 turns) and the shooter stands 24 units higher than the target, so the line
  meets the floor about 110–115 units out while the shoulder is hit at about 60–65 units: f ≈ 0.53–0.58, which gives
  42–39. Exact matches: 39 needs f in (0.5625, 0.5769]; the arm hits of the first run (trunc(D' × 0.5) = 21, D' = 42
  or 43) need f in (0.5114, 0.5357]. The player damage path after the shot (armour rule at armour 0, zone multiplier
  trunc(D' × scale), overkill cap) is as G3.20 steps 2–4 describe; the d × (1 − d/337.5) fit is a coincidence of two
  points.
  4. A world hit (no entity) and a hit on an entity without a Ghoul model keep the engine's fraction along the full
     trace (start to start + 3 × range), as rule 3 says.
  Replace rule 2's "fraction capped at 0.99" by "fraction = distance from the shot start to the model hit point ÷
  distance from the shot start to the end of the world-clipped sweep (the world hit point, or the trace end), capped
  at 0.99", and add to rule 3: "for a Ghoul-model target f is this fraction, so the falloff depends on the distance of
  the world surface behind the target". Withdraw G3.20 step 1's "there is none at 61 units". OpenMerc (45 / 22)
  **does not match**; it needs the filter fraction of step 2 (the third filter value is already passed, I1b.1).
- Verify: black box, dm/jpndm2: two clients, attacker 61 units from the target with a level view (pitch 0) and
  nothing behind the target within 300 units: shoulder hit 45, arm 22. Same distance, attacker crouched or pitched so
  the shot line meets the floor 100 units out (f ≈ 0.6): shoulder hit trunc(45 / 1.2) = 37, arm 18. Target with its
  back against a wall (wall 20 units behind the hit point, hit at 60 units, f = 0.75): 30 / 15.

### E. G18 game module (items 36–42)

#### G18.36 SPEC-780 §16 / SPEC-791 §1 / SPEC-AMEND-005 G18.25, G18.28 — Ghoul records of the deathmatch player models (Resolution)

- Old: no spec text for the order of the player-model records and their bolt-ons. Observation (dm/jpndm2, records as
  the client receives them): (a) gore models, the weapon precache, then the player models; (b) per model its record,
  its bolt-on records in reverse `.gpm` order, then its body precache; voices before body sounds; (c) one record per
  model; (d) a `.gbm` skin is registered on the bolt-on object's material even when the object has no material of
  that name; (e) a `.gbm` without skins gets a record with the player model's last skin entry; (f) the weapon kinds
  18–28 and the goggles keep (empty) records; (g) one-skin ammo-box records per map pickup; (h) one sequence per
  bolt-on record; (i) in-view records after the mode's 9 mm pickup. OpenMerc implements (a)–(h).
- New: (a)–(d) and (f)–(i) are confirmed; (e) is a leftover value, and the order inside one model is:
  1. **Per model, in this order:** the model record (model directory, sequence list and the model's own name as the
     key — so every `.gpm` gets its own record even when two share directory and base, as ghost and iceman do — with
     the gore-zone skins, `c_blood`/`c_black`, the face skins and then the `.gpm` skin entries registered in list
     order; an entry whose material and skin repeat an earlier entry, or whose skin is `b_dm_armor`, is not
     registered again and prints `Narrowly avoiding re-register on %s %s`); then the bolt-ons, last `.gpm` entry
     first; then the voices of the model's sound directory; then the body's own set-up (gore-cap models such as
     `G_GUTS_LRG` and the body sounds). This is (b) as observed.
  2. **Bolt-on skin (e).** For each bolt-on whose `.gbm` loads and names a base model, the game reads the `.gbm`'s
     first skin entry into the same skin buffer the model's skin list used, and registers the bolt-on with whatever
     that buffer then holds. A `.gbm` without skin entries does not write the buffer, so its bolt-on gets the skin
     read last: the first skin of the bolt-on handled just before it (reverse order) or, when none was handled yet,
     the model's last skin entry (iceman: `f_sibtroop2` on `ACC_ANTENNA` because `menupistol2.gbm`, handled first,
     names no base model and is skipped; ninja: `h_ninja` on `ACC_SWORD` likewise after `menupistol1.gbm`). Bolt-ons
     without a base model (the menu pistols) register nothing and leave the buffer alone. Reproduce it as a rule.
  3. (d) is SPEC-AMEND-006 G9.16 (a bolt-on skin goes to the first material that accepts it, any name).
  4. (f): accepted from the observation — the item system's registrations of SPEC-AMEND-005 G18.28 keep a record
     even when the list loads nothing; replace "UNVERIFIED" there by "a record is kept (empty)".
  OpenMerc **matches** with (e) implemented as the shared-buffer rule above; (i) is G9's mode-setup order.
- Verify: done for jpndm2 (155 of 167 records in place). Data check: a test `.gpl` with only `iceman` gives
  `ACC_ANTENNA` with skin `f_sibtroop2`; swapping the antenna and backpack lines of a copy of `Iceman.gpm` gives the
  antenna the backpack's skin `acc_backpack_lrg_wht`.

#### G18.37 SPEC-780 §21 and §15 — variable A at a single-player start; when the helpers exist (Resolution)

- Old: (a) observation: the single-player begin packet carries one event 0 (player variable A) = −1 (tsr1, ger2),
  none on arm1 (`noweapons`). OpenMerc sets variable A to −1 after the loadout's reset. (b) The original's first rocket
  after a deathmatch respawn has entity number 98 where OpenMerc's has 97; SPEC-780 §15 helpers: when created and
  freed, made once or every frame, and how "not solid" helpers are hit.
- New:
  **(a)** Yes. In the normal single-player loadout and in the `setweapons 1` kit (SPEC-AMEND-006 G18.33, value −2)
  the order is: health 100, statistics and level start time reset; player-module owner context set, current weapon's
  deselect hook, item think; prediction restart; player module "remove all" (method 0 with the CTF-flag argument
  set); **player variable A := −1** (method 38, replicated as event 0); weapon tag from userinfo `bestweap`; method 42
  with 2; then the weapons and items of the respective path. Single player has no infinite-ammo test (always −1). The
  `noweapons` path (−1) skips all of it, so no event (arm1).
  **(b)** The three helpers of §15 are kept, moved and freed by the player's end-of-frame view step (SPEC-780 §11,
  view offset), every frame:
  1. *lean head* (entity flag 0x8000): exists while the lean value is not 0; created when needed, then moved to the
     leaned eye point and relinked each frame; freed when the lean returns to 0;
  2. *crouch* (entity flag 0x100000, classname `Nug Buddy`): deathmatch, client, not dead (dead flag ≠ 2), crouched:
     created when needed, else moved to origin + forward × 8, z + maxs z; freed at once (immediate reuse of the slot)
     when no longer crouched;
  3. *gun* (entity flag 0x200000): deathmatch, client, not dead, and a client state value (spectating or chasing, UNVERIFIED which) is 0: created
     once (box (−12, −12, −4)–(12, 12, 4)) and placed by the §15 rule, then only moved each frame; freed when that
     state becomes non-zero;
  4. at death (dead flag 2) the lean, crouch (immediate) and gun helpers are freed.
  So they are made once and moved (OpenMerc's choice), unlike the characters' helper (SPEC-700 §16), which is freed
  at the start of each AI think and created again at its end. The gun helper of a respawned player is created in the
  end-of-frame step of the respawn frame, i.e. before any entity the player creates later — hence rocket 98.
  **Hits on helpers.** All helpers have solid 0 and server flags 0x82. The engine links an entity into the solid
  area lists when its solid is not 0 **or** its server flags contain 0x80, and its box traces include such entities;
  bit 0x02 makes them invisible to traces whose content mask lacks 0x04000000, so player and character movement
  ignore them while shots (mask 0x06000081) see them. A trace never meets helpers of its own passing entity or of the
  same owner. When a shot meets a helper, the game's hit filter replaces it by the owning player (the first entity
  without flag 0x80 on the helper chain) and runs the per-model ray test on the owner's Ghoul model (SPEC-782 §3
  rule 2): the hit counts only where the ray meets the player's model — the helper only extends the region in which
  the player is offered as a candidate (the gun helper covers the weapon held out in front of the body box).
  OpenMerc **matches** (a); for (b) it needs the end-of-frame creation timing and the hit rule above.
- Verify: (a) done (tsr1, ger2, arm1). (b) jpndm2, two clients: after a respawn the first rocket is entity 98; a shot
  through the gun helper's box that misses the player's model does no damage and continues to the world.

#### G18.38 SPEC-760 §B4.5 — the toss landing is not skipped after a touch that changed the movetype (Correction of OpenMerc's rule)

- Old: B4.5: on impact with normal z > 0.7 and (vz < 30 or movetype ≠ BOUNCE) the entity takes the ground entity,
  becomes STEP, vz = 0 (horizontal velocity kept) and step physics runs at once. trn1: the helicopter (FLY) touching
  `func_pushkillbox` is killed and set to NOCLIP with the fling velocity (−560, 0, 0); OpenMerc's landing then stopped
  it; the original's explosion moves 48 units along the fling one frame later. OpenMerc now skips the landing when
  the movetype changed during the push.
- New: the original has **no** such exception (neither "movetype changed" nor "started inside the toucher"). After
  the push the movetype is read again only for the "≠ BOUNCE" test, so a NOCLIP entity lands like any other: ground
  entity := the hit entity, movetype := STEP, vz := 0, **vx and vy kept**, and step physics runs in the same frame
  (B4.6), so the fling continues as a slide on the step physics with its horizontal friction (dead entity: speed −
  ground friction × 300 per frame) and the STEP movetype from then on. `func_pushkillbox`'s touch (SPEC-761 §2.3)
  does: damage 100000 (means of death 22), then non-clients: movetype NOCLIP (1), clients: movetype 10; velocity =
  (cos a, sin a, 0) × the box's speed, a = the box's `angle`; elasticity −1. If OpenMerc's helicopter "never moved"
  with the landing in place, the loss is in its step move (velocity cleared, or no move for this entity), not in
  B4.5. OpenMerc's skip rule **does not match** (it keeps NOCLIP flight of 56 units per frame instead of a slide with
  friction).
- Verify: black box trn1 end (`kb_orig_g4trn1`): the helicopter's position over the 3 frames after the touch: first
  step about 48 units (as observed), the next steps shorter (friction), then still; with the skip rule each step would
  be 56 units until something blocks it.

#### G18.39 SPEC-780 §16 — the factor of the 13 animation speed classes (Resolution)

- Old: "Animation playback speed follows the movement speed in 13 classes (1 ÷ speed factor above 1.875, 1.75, …,
  0.5 in steps of 0.125; leg injury forces the slow class 5)". Black box: at normal movement scale the speed index
  never changes. OpenMerc: factor = the movement scale of the pmove block.
- New: OpenMerc's reading is right, with two details:
  1. Factor s = the game mode's movement scale (the value written into the pmove block, SPEC-790: default
     `g_movescale` × (1 − leg slowdown), Realistic 0.5625 × (1 − slowdown), single player 1.0 or 0.5625), multiplied
     by 0.75 when the chosen record is a looping crouched record (posture preset 1, flag 0x08000000) and the player is
     not running (the run button is not active or exhausted).
  2. When the player is not moving (horizontal distance from its origin to the movement goal passed by the animation
     code ≤ 0.1) the class is 4. Otherwise q = 1/s: q > 1.875 → 12, > 1.75 → 11, > 1.625 → 10, > 1.5 → 9, > 1.375 → 8,
     > 1.25 → 7, > 1.125 → 6; then a leg-slowdown test (> 1.0 → 5) that never fires in 1.07f because the slowdown is
     capped at 0.5 (the injury acts through s instead); then > 0.875 → 4, > 0.75 → 3, > 0.625 → 2, > 0.5 → 1, else 0.
  3. The class is set on the player's instance each time the third-person animation code (re)chooses a record —
     grounded, stationary, firing, throwing, airborne, weapon actions, waves, death and the pain reaction — which is
     every frame except while a play-once record (flag 0x00400000) is still running. The client sees state field 2
     only when the index changes.
  Replace "leg injury forces the slow class 5" by rule 2's note. OpenMerc **matches** except for the crouch factor
  0.75 and its class-5 rule for leg injuries.
- Verify: black box jpndm2: crouch-walk without run → speed index 7 (q = 1.333); after leg hits that leave a slowdown
  of 0.2 → index 6 while moving (q = 1.25), 4 when standing still.

#### G18.40 SPEC-780 §21 / SPEC-AMEND-006 G18.33 — the loadout resets the kept inventory object (Resolution)

- Old: "the player module is reset": a reset of the kept object or a new object? OpenMerc's reset faulted at the
  kit's first weapon switch after `gamemap` (the kept object still held the previous level's view-weapon instance);
  with a new object the begin packets match the oracle.
- New: the original never creates an inventory object in the loadout. The object is created at connect (and replaced
  when a savegame's client record is read, SPEC-752 §1.1); across a `gamemap` the client keeps it, and the loadout
  resets it in place (owner context, deselect hook, item think, prediction restart, "remove all" — G18.37 (a)).
  "Remove all" clears weapons, ammunition, armour and items but not the two view-weapon instance fields; in the
  player module those are cleared when the next weapon-change request is processed
  (UNVERIFIED whether before any use of the old instance; the original shows no fault). OpenMerc's new object
  gives the same packets; it is acceptable as long as no observable state of the old object (none in single player: everything else is cleared or reloaded from the
  persistence cvars) is lost. Keeping the object and clearing the view-weapon fields at the reset also matches.
  OpenMerc **matches** observably.
- Verify: done (`gm_orig2` / `gm_om2`). tsr1 → tsr2 by `gamemap`: the kit's first weapon switch plays the select
  animation of the new weapon without a stale model.

#### G18.41 SPEC-752 §3.1 GAME 0x618 and SPEC-700 §11 — the eight mission-statistics counters (Resolution)

- Old: "eight int32 mission-statistics counters accumulated over the levels of the mission"; which, in what order,
  when added, who resets them, who reads them?
- New: the level keeps eight int32 counters and a level start time; the game record accumulates them:

  | index | GAME offset | counter | counted when |
  |---:|---|---|---|
  | 0 | 0x618 | enemies killed | a character dies (not already dead, no cinematic running) and is not on the sight client's team |
  | 1 | 0x61C | friendlies killed | as above, on the sight client's team |
  | 2 | 0x620 | throat shots | the throat death reaction is chosen (`std_sthroat_2side_n_n`), no cinematic |
  | 3 | 0x624 | groin shots | the groin death reaction (`std_sgroin_2side_n_n`), no cinematic |
  | 4 | 0x628 | head shots | the head death reaction (`std_shead_2frnt_n_n`), no cinematic |
  | 5 | 0x62C | gibs | a character is gibbed |
  | 6 | 0x630 | saves used | each save the save limit allows (the "saves left" count drops by 1) |
  | 7 | 0x634 | cash earned | enemies killed add trunc(profile[6] × the skill score factor); at `updateinvfinal` the cvar `stat_moneyadd` is added and set to 0 |
  | — | 0x638 | played time (float s) | level time − level start time |

  - **Reset of the level counters**: at every single-player spawn (the loadout, before any other step: counters 0,
    start time := level time, and the "saves left" count is set to its level-start value) and after each accumulation.
  - **Accumulation** (single player only): (1) when a `target_changelevel` is used (before the intermission):
    mission totals += level counters, played time += level time − start time (the level counters are not cleared
    there; the next level's spawn clears them); (2) by the console command `updateinvfinal`: first `stat_moneyadd` is
    added to cash, then the statistics cvars are written (below), the inventory is saved to the persistence cvars,
    then the totals are accumulated, the level counters and the level start time are set to 0 and the game's level
    time is set to 0.
  - The totals are cleared only with the whole game record (new game) and replaced when a savegame's GAME chunk is
    read; nothing else writes them.
  - **Reading**: the game export "update statistics" (SPEC-001 export at +0x48) and `updateinvfinal` set the cvars
    `stat_guyskilled`, `stat_friendlieskilled`, `stat_throatshots`, `stat_nutshots`, `stat_headshots`, `stat_gibs`,
    `stat_savesused`, `stat_cashearned` to "level ␟(level + total)" (format `%d \x1f(%d)`), `stat_playedtime` to
    `h.mm.ss ␟(h.mm.ss)` for the level and level + total, and `stat_playedtime_base` to the total time `%f`; the
    end-of-mission menu shows these cvars. Nothing is written when level + total time is 0.
  OpenMerc: fill GAME 0x618–0x63B as in the table.
- Verify: data check: the reference `auto.sof` (KOS1) totals 73, 4, 1, 1, 3, 0, 3, 32804, 765.4 read in this order
  give the end-of-mission screen's numbers; black box: kill one enemy with a head shot on tsr1 and type
  `updateinvfinal`: `stat_guyskilled` "1 ␟(1)", `stat_headshots` "1 ␟(1)".

#### G18.42 SPEC-780 §16 / SPEC-AMEND-005 G18.26 — airborne choice of a deathmatch player; `std_l_n_r_2` is a reload (Refinement, partly open)

- Old: §16 names only the forward jump `std_jrun_fwd_a_a` (horizontal speed > 250, moving forward) and "a jump is
  followed by a landing". Observation (`spd_orig`, jpndm2): after the spawn drop `std_l_n_r_2`; walking off a ledge
  `std_jstraitup_up_l_2` while falling, `std_waim_fwd_r_2` after landing; standing with the 9 mm `std_a_fwd_p_cb`
  for 0.2 s, then `std_a_fwd_p_2`.
- New:
  1. `std_l_n_r_2` is not a landing record: it is the standing **reload** choice of SPEC-722 ("reload", standing
     `std_l_n_<w>_2`), started by the player module's reload-start callback (SPEC-003 server callback 4) because the
     spawn kit gives the pistol with an empty clip (SPEC-780 §4 step 4) and the module reloads at once. It is a
     play-once record (flag 0x00400000), so the per-frame choice keeps it until it ends.
  2. **Airborne choice** (deathmatch only; a living player with a body, when the current record is missing, done, or
     not of record kind 5):
     - movement goal: when the horizontal distance from the origin to the goal passed by the movement code is above
       0.1, the goal is moved to 1 unit (not running) or 100 units (running) from the origin in that horizontal
       direction, keeping its height;
     - ladder: a box trace of the player's box 1 unit forward (view yaw, horizontal) with mask 0x02010003; when it
       hits a brush whose contents include the ladder bit (bit 29, Quake II's ladder contents): vertical speed > 2 → `std_xladder_up_a_a`, < −2 →
       `std_xladder_dwn_a_a`, otherwise the standing choice (a ladder record that is done and not looping is forced
       to restart);
     - otherwise, when the goal's height differs from the origin's by less than 1 upwards and less than 5 downwards,
       nothing changes (the current record stays);
     - otherwise the body's **jump choice** (SPEC-722 "Other choices", standing posture, crouched flag from the
       player's crouch state) with the name `std_jrun_fwd_a_a` when the horizontal speed exceeds 250 and the movement
       direction and the velocity direction agree (dot > 0.25), else with no name — so falling off a ledge gives the
       jump choice's own list (`std_jstraitup_up_l_2` …), as observed; then the speed class of G18.39.
  3. After landing the grounded movement choice applies (SPEC-722 movement table; the aimed walk `std_waim_fwd_*` as
     observed). The standing `_cb` entry for 0.2 s before `std_a_fwd_p_2` comes from the standing aim list
     (`std_a_fwd_p_cb` is a 20°-turn aim record); which condition selects it first was not decided here — UNVERIFIED,
     see Open questions.
  OpenMerc **matches** if its airborne rule follows step 2 and treats the spawn record as the reload of step 1.
- Verify: jpndm2: respawn with an empty pistol clip → `std_l_n_*` reload record before any movement record; run off a
  ledge at > 250 units/s → `std_jrun_fwd_a_a`; walk off it → `std_jstraitup_up_l_2`; climb a ladder → ladder records.

### F. G7 props and server effects (items 12–15)

#### G7.12 SPEC-770 6A.7 — `misc_test_gun_auto` sets no effects bit 0x200 (Correction)

- Old: 6A.7: turrets set entity effects bit 0x200. Observation (`g7all3`): the developer test gun is sent with effects
  0 and has no baseline.
- New: confirmed. The test gun's set-up (box (−28, −21, −34)–(21, 21, 19), health 10000, no pain reaction, the gun
  turret's death, its own think 2.5 s after spawn, model and the `DUMMY01` part chain of the automatic gun, effect
  `weapons/othermz/autogun` registered) never sets effects bit 0x200; only the turret classes of 6A.7 do. Add to 6A.7:
  "except `misc_test_gun_auto`, which keeps effects 0". OpenMerc (following the observation) **matches**.
- Verify: done (`g7all3`).

#### G7.13 SPEC-770 open questions on missing models — classes that stop the original (Resolution)

- Old: SPEC-770 left open what happens when a class's model is absent. Observation (`g7one_*`): `misc_generic_car_rolls`
  and `misc_newyork_news_machine` end the server at spawn (`SetSimpleGhoulModel couldn't find ghoul sequence …`),
  `environ_trainarm_spawn` at its first use, a point `environ_raingen` stops the map load with
  `ERROR: PF_setmodel: NULL`, `func_musicControl` used by a non-player as SPEC-771 B.5 says; the other missing-model
  classes spawn invisible and the level runs. OpenMerc skips the missing instance instead of stopping.
- New: the observation is the original's behaviour and closes those open questions: the listed fatal cases are
  missing-data errors (the stock data lacks the model these classes ask for; UNVERIFIED whether a stock map places
  them), and every other missing-model class spawns without a model and otherwise behaves as specified (entity state,
  baseline, box). Reproducing the fatal stops is not required for compatibility; OpenMerc's choice to skip the
  missing instance is acceptable, preferably printing the same console line before continuing. Mark the SPEC-770
  open questions on these classes as answered by this list. OpenMerc **matches** for all non-fatal classes.
- Verify: done (`g7one_*`, dm/raven).

#### G7.14 SPEC-770 skin table — four classes set their skin after the set-up (Correction)

- Old: SPEC-770 lists `misc_generic_cash_register`, `misc_generic_comp_monitor`, `misc_generic_fire_extinguisher`
  and `misc_newyork_gumball` with a skin passed to the set-up. Observation: they register every skin of their model
  directory (`register`, `registerd`; `damaged`, `monitor`; `damaged`, `exting`; `gumball`, `gumball2`).
- New: confirmed. These four classes run the common prop set-up **without** a skin (so the model's record gets all
  skins of its directory, SPEC-791 §1) and then set the chosen skin on the material by name (cash register:
  `register`, or `registerd` for skin number 1). Mark the four rows "applied after the set-up", like the other classes
  so marked; classes such as `misc_generic_chair_office`, `misc_generic_barrel_chemical`, `misc_generic_desk_phone`
  and `misc_generic_bench` pass their skin to the set-up and register only that skin. OpenMerc (following the
  observation) **matches**.
- Verify: done (32 SP maps and dm/raven, `sp_reg_arm1` / `sp_reg_jpn1`).

#### G7.15 SPEC-AMEND-006 G7.7 — verify run with a bench at `angle 90` (acknowledged, no change)

The `g7bench90` result (the angle-90 bench's top is shot off, shown by the same-material rule dropping the angle-0
bench's burst in the same frame) is what G7.7 predicts for the bounds in bolt-then-entity space. No change;
G7.7's Verify is done.

### G. F5 SPEC-AMEND-005 follow-ups (items 5–6)

#### F5.5 SPEC-741 §14.4/§14.5 with SPEC-AMEND-006 F5.1/F5.3 — spawn-waypoint test, best branch, juncture pre-check (Correction)

- Old: SPEC-741 §14.4 "the cheapest link with cost < 16384 … whose target is not one of its consecutive linked
  neighbours and not a spawn waypoint"; §14.5 spawn table; SPEC-AMEND-006 F5.1 (duplicate rule of adding) and F5.3
  (spawn trace). Black box on other maps: original / OpenMerc error waypoints differ on nycdm5, plantdm, labdm,
  factorydm, irqdm1, gerctf1 and sudctf2.
  - (a) irqdm1: the original has 15 spawn waypoints for 20 spawn points: waypoints 1–11, 13, 14, 17, 18 are in no
    path, and 12, 15, 16, 19 are one-waypoint paths 1–4. OpenMerc has 19 (1–19).
  - (b) nycdm5: the original lacks the junctures `75 → 33` (path 6 to 2) and `75 → 77` (path 8 mirrored into 6).
- New: three rules of the original, all missing from the spec. With them a data-only simulation of the stock files
  reproduces **every** reported original result: the error waypoints of all eight maps (nycdm5 {49}, plantdm
  {48, 92, 125}, labdm {10, 120, 144, 248, 261, 266}, factorydm {}, irqdm1 {146, path 8}, gerctf1 {5}, sudctf2 {},
  jpndm2 none), irqdm1's spawn waypoints and path numbers, and nycdm5's `bot_wp_pathinfo 2` and `6` listings.

  **1. Spawn ordinals and the spawn-waypoint test (cause of a).**
  - Every kept `info_player_deathmatch`, `info_player_team1` and `info_player_team2` takes its ordinal from **one
    counter shared by the three classes**, in entity order. The counter starts at 0 when the game module is loaded.
    It is never reset by a level change; it is reset only by a module reload (`map` restarts the server and reloads
    the module; `gamemap`/rotation do not). A spawn point removed by the mode's rules takes no ordinal. On irqdm1 the
    12 deathmatch points get ordinals 1, 2, 5, 6, 9, 10, 11, 13, 15, 16, 17, 19, team 1 0, 3, 7, 14, and team 2 4, 8,
    12, 18 (exactly the keys of the stock file's sections).
  - The spawn table is three sorted maps (deathmatch, team 1, team 2), ordinal → waypoint. They are emptied and
    refilled (F5.3 trace, nearest accepted waypoint; spawn points without one are left out) at every waypoint load
    and every link rebuild.
  - **"Is waypoint w a spawn waypoint"**: the three maps are visited in that order. For one map, let n be its entry
    count when its visit starts. The keys 0, 1, …, n − 1 are looked up in turn. A key that is not present is
    **inserted with waypoint 0**, so the map grows by one. The test answers yes at the first visited entry whose
    waypoint equals w, and stops. If no map has such an entry within the visited keys, the answer is no.
  - So an ordinal ≥ n is not seen until earlier tests have grown that map past it. Such a spawn point's waypoint is
    not a spawn waypoint for the first tests and becomes one later. The test is made, in this order:
    1. while paths are built: for each waypoint id at the top of the path loop, then for each successor id while a
       path is extended;
    2. in the best-branch search: for each candidate that beats the current best (rule 2);
    3. elsewhere: edit-mode drawing, link building, the nearest-waypoint search of a route start, `bot_wp_info`.
    The order of these tests decides the outcome. Waypoints tested early fall through to path membership; on
    irqdm1 these are 12, 15, 16, 19.
  - The inserted zero entries stay in the maps until the next refill. The writer emits them, which is why stock
    files map many keys to waypoint 0 (irqdm1: dm keys 0, 3, 4, 7, 8, 12, 14, 18 → 0).

  **2. Best branch of a path end W (cause of the second part of b).**
  - Candidates are W's links.
  - **Distance-chain exclusion**: for k = 1, 2, 3, … every link whose target id differs from W's id by exactly k (in
    either direction) is excluded, as long as round k excluded at least one link. The first k with no such link ends
    the exclusion.
    - This is not limited to W's own path. nycdm5 waypoint 77 links to 78 (k = 1), 75 and 79 (k = 2), 80 (k = 3),
      81 (k = 4): all are excluded, 75 included.
    - A link at k = 2 is not excluded when there is no link at k = 1.
  - Among the remaining links, the cheapest with cost **strictly below** 16384 whose target is not a spawn waypoint
    (test of rule 1 at that moment) is chosen. Ties keep the first in stored order.
  - No candidate → dead-end record `(path, 0, W, 0)`.
  - The juncture's other path is the highest-numbered path whose id range contains the target; 0 when none.

  **3. Creation pre-check (cause of the first part of b).** Before a new juncture of path P's end is added anywhere,
  P is searched for a record with the **same other path at the same own-side waypoint**. If there is one, the new
  juncture is discarded: it is added neither to P nor to the other path.
  - This is SPEC-AMEND-005 G5.7's duplicate sentence; keep it.
  - F5.1's raw-pair rule is a **second, separate** refusal applied at each of the two additions.
  - Dead-end records skip the pre-check and go to P only (with F5.1's dead-end rules).
  - nycdm5: path 2 (processed first) adds `34 → 75` to paths 2 and 6. Path 6's end 75 then finds 33 (cost 9919.6),
    but path 6 already holds a record with path 2 at waypoint 75, so `75 → 33` is discarded entirely.
  - Order of creation: paths in ascending number, for each path its first waypoint before its last.

  OpenMerc **does not match**: it lacks the map-growth quirk of rule 1, the distance-chain exclusion of rule 2 and
  the pre-check of rule 3. A matching implementation must also keep the inserted zero entries between refills.

  Two printing details seen in the same run:
  - `bot_wp_pathinfo n` accepts 1 … number of paths. F5.2's "(paths − 1)" is wrong. The argument is taken as a
    signed byte: 128 … 255 are negative and invalid, and 256 + k acts as k.
  - The invalid-path line prints a value that is **not** the argument: the original passes no number to its
    `%d is not a valid path.` format. OpenMerc may print the argument.
- Verify: data check (done): apply the three rules to the stock `.wrs` files with the spawn points of the stock
  maps (ordinals from 0, in entity order; the nearest-waypoint result of each spawn point taken from the file's own
  non-zero section entries, which the original writer produced with the F5.3 trace). The eight maps above give exactly the original's verification lines.
  irqdm1 gives waypoints 1–11, 13, 14, 17, 18 outside all paths, and paths 12, 15, 16, 19, 20–50 as 1–5. In game:
  start irqdm1 with `map` (fresh module), then once more after a `gamemap` to another deathmatch map and back. The
  second start continues the ordinals of the earlier levels (not from 0), so its set of spawn waypoints and the
  `bot_wp_whichpath` answers differ from the first start.

#### F5.6 SPEC-411 §6.7 / SPEC-012 *Lighting* — which lights carry the (1 − `LIGHT_AMBIENT`) factor (Correction)

- Old: SPEC-411 §6.7: "A world or dynamic point light sets DIFFUSE = SPECULAR = (C, 1)". Black box: the GL trace
  shows DIFFUSE and SPECULAR of every world light = colour × (1 − `LIGHT_AMBIENT`): 0.4× at the default 0.6, 0.2× at
  0.8, 0.8× at 0.2. OpenMerc now uses colour × (1 − K) for world and dynamic lights. Asked: dynamic lights, model
  (file) omni lights, the fast directional form.
- New: the observation is right for world point lights; the factor applies to nothing else. With K = `LIGHT_AMBIENT`
  (clamped to 0 … 1 by `gserver`):

  | Light in the instance's light list | Colour C used (software vertex lighting §6.3 and GL DIFFUSE/SPECULAR §6.7) |
  |---|---|
  | world-light-set point light, immediate mode | colour × (1 − K) |
  | world-light-set point light, fading in / on (smoothed mode) | colour × fade level × (1 − K) |
  | world-light-set point light, fading out | colour × fade level × (1 − K) |
  | world-light-set light in the fast directional form (§6.3, fast lights) | colour × h only (**no** (1 − K)) |
  | dynamic light, point form (§6.4) | colour (**no** factor) |
  | dynamic light, fast directional form | colour × h (**no** factor) |
  | model (file) lights, omni or otherwise (§6.5) | SPEC-012's colours (**no** factor) |
  | ambient term A | K × block colour (unchanged) |

  - The factor is applied when the light enters the instance's light list. It therefore holds for the software
    vertex lighting of 2 ≤ m ≤ 2.5 and for the OpenGL lights of m > 2.5 alike.
  - The fade levels are applied as in §6.3, and the 0.2 deletion test of fading-out lights uses the unscaled colour.
  - A world point light at the default K = 0.6 thus gives 0.4 × colour, and K = 1 removes world point lights
    entirely while leaving dynamic and model lights.
  - Replace in §6.7 "A world or dynamic point light sets DIFFUSE = SPECULAR = (C, 1)" by "… = (C, 1) with C from the
    table", and add the factor to the world point-light contribution of §6.3.
  OpenMerc **does not match** for dynamic lights (it scales them); it must also not scale fast directional world
  lights.
- Verify: GL trace as in the observation, with a muzzle flash or rocket (dynamic light) near the traced model:
  that light's DIFFUSE equals its full colour while the world lights are at 0.4×. `ghl_light_method 2` (software)
  with `gserver LIGHT_AMBIENT 1`: models lose all world-light shading but still light up from a dynamic light.

### H. I5 client effects and temp entities (items 21–23)

#### I5.21 SPEC-002 import 0x68 / SPEC-AMEND-005 I5.8 — size of the direction table seen by the renderer (Refinement, acknowledgement)

- Old: SPEC-002 import 0x68 "The engine's table of unit direction vectors, 12 bytes each … not Quake II's normal
  table." Observation (no question): the OpenMerc GL3 renderer assumes **162** entries. Loaded by the OpenMerc
  engine it gets no table at all, so lit quads sample their light straight down.
- New: acknowledged. For I4a/I4c, state it in SPEC-002:
  - The table behind import 0x68 has **250** entries: the vectors of SPEC-201 Appendix A, in the same order. The
    first entry is (0.219625, 0, 0.975584). It is the table of the `dir` byte, not Quake II's 162-entry table.
  - The original engine fills the import before the renderer's `Init`, and it is never null while a renderer is
    loaded.
  - The renderer uses it in export 0xa4 (SPEC-410 *Light sampling*). The low byte of the direction argument selects
    the entry: 255 means "straight down, 2048 units"; any other value v reads entry v and samples 256 units along
    it, with no range check against 250.
  - Callers only pass bytes decoded by the engine, which aborts on an index above 249 (SPEC-201), or 255. Entries
    250…254 are therefore never read in 1.07f.
  Consequences for OpenMerc:
  - the engine must hand the renderer the 250-entry table, as the original does;
  - a renderer must size the table at 250 entries, not 162;
  - a renderer that is not given a table cannot reproduce the light samples of lit quads (SPEC-401/SPEC-400
    effects that pass a direction). Sampling straight down is a visible difference, not a fallback of the original.
  OpenMerc **does not match** until the engine passes the table and the renderer sizes it at 250.
- Verify: data check: with the table installed, a lit quad effect whose direction byte is 200 samples the wall it
  points at (above entry 161, so a 162-entry table would read past its end). Original vs OpenMerc capture of the
  same effect against a coloured wall: same tint.

#### I5.22 SPEC-400 Editor 1 — the editor's text pages, rows, steps and text cvars (Resolution; Correction of the `fx_partadjust` row of the cvar table and of two decal labels)

- Old: SPEC-400 *Editor* 1: "With `fx_edit` ≠ 0 the client shows a text page … each step"; main-page labels; "Sub-pages show
  the labels used in the field tables"; cvar table: `fx_partadd`, `fx_partsub`, `fx_partadjust` "adjust the selected
  editor row by +value / −value / value". The clean team asked how the page is drawn, how rows are selected and
  pages left, the sub-page rows, step sizes, and how the text cvars are applied.
- New: replace *Editor* item 1 and the `fx_part*` row of the cvar table by the following.

  **1. Output and timing.** The editor has no 2D drawing and no key handling of its own. While `fx_edit` ≠ 0, every
  effects simulation step (SPEC-400 *Timebase*: `fx_sim_time` steps per second, 60 by default; none in a frame without
  a step) does, before running the effects: wrap `fx_editsegment` (< 0 → 4, > 4 → 0); apply the editor cvars (§3);
  print the current page with console prints (one console line per page line, also shown as notify lines and written
  to the log; nothing is cleared, so the console scrolls continuously); then the preview of item 2. The user drives
  the editor by setting cvars (console, binds or menus).

  **2. Colours.** Every value or action field of a page is preceded by a colour byte (SPEC-313 colour codes) and
  followed by `\x01` (white): the selected row's field gets the byte chosen by trunc(`fx_editsegment`): 0 → `\x02`
  (red), 1 → `\x0B` (high red), 2 → `\x07` (cyan), 3 → `\x04` (yellow), 4 → `\x06` (purple); every other field gets
  `\x01`. There is no timer; the colour changes only when `fx_editsegment` changes (which also selects the
  substitution segment). Labels coloured `\x03` (green) mark text-cvar fields. In this item `\xNN` is one control byte.

  **3. Control cvars.** When any of `fx_partadd`, `fx_partsub`, `fx_partadjust` is non-zero, the page in front gets
  one call with cursor code c = trunc(`fx_partadjust`) and amount a = `fx_partadd` if it is non-zero, else
  −`fx_partsub`; then all three are set to 0. The call first moves the cursor by c, then, if a ≠ 0, applies a to the
  (new) selected row, then (main page) re-validates the cursor with c = 0.
  | c | Main page | Emitter and particle pages | Sound, light, decal, feedback pages |
  |---|---|---|---|
  | 999 / 1000 | previous / next page | previous / next page | moves by c (mod row count) |
  | 1001 | delete the primitive of the selected primitive row | moves by c | moves by c |
  | 1002 | solo toggle of the selected primitive (selected again → none) | moves by c | moves by c |
  | other | move by c, wrapped into 0 … rows − 1 | same | same |
  After any cursor change, rows that are not valid (tables below) are skipped one by one in the direction of c (+1
  for 0 and the special codes), wrapping. Integer fields become trunc(old + a); real fields get old + step × a;
  for a min/max pair the other end follows so that min ≤ max (unless the table says "unordered"); clamps apply after
  that. Toggle rows flip their bit on any non-zero a; action rows act on any non-zero a.

  **4. Main page (44 rows).** Pages by cursor: rows 0–11 show the header lines and primitives 1–2, rows 12–27
  primitives 3–6, rows 28–43 primitives 7–10. Page keys: 1000 goes 0–11 → 12, 12–27 → 28, 28–43 → 1; 999 goes
  0–11 → 28, 12–27 → 1, 28–43 → 12. Lines:
  | Line | Format (fields in order) |
  |---|---|
  | add (row 0) | `%sAdd New Effect\x01\n`; an empty line `\n` when 10 primitives exist |
  | duration, Instant | `Duration      %s%-12s\x01\n` (row 1, "Instant") |
  | duration, Continual | `Duration  %s%-9s\x01 Delay %s%6.2f\x01 / %s%6.2f\x01\n` (row 1 "Continual", rows 2, 3 repeat delay) |
  | primitive n (1–10) | `%sEffect %2d   %s%-9s\x01      Delay    %s%7.2f\x01 / %s%7.2f\x01    %sEdit\x01\n`: solo mark (`\x05` blue when n is the solo primitive, else `\x01`), n, row 4n type name (Effect/Sound/Light/Decal/Feedback), rows 4n+1, 4n+2 trigger delay, row 4n+3 "Edit"; `\n` when n > count |
  | Row | Action on a ≠ 0 | Valid when |
  |---|---|---|
  | 0 | append a new emitter (type "Effect") with the default field values of the emitter and particle tables | fewer than 10 primitives |
  | 1 | mode := trunc(mode + a) wrapped into 0 … 1 | always |
  | 2, 3 | repeat delay min / max, step 0.05, clamp 0 … 100; the repeat-delay limit (file trailer) is raised to stay ≥ min and max | Continual |
  | 4n | type := trunc(type + a) wrapped into 0 … 4; the primitive is replaced by a new default primitive of that type (also when the type is unchanged), keeping its trigger delay | n ≤ count |
  | 4n+1, 4n+2 | trigger delay min / max, step 0.05, clamp 0 … 100 | n ≤ count |
  | 4n+3 | open the primitive's page | n ≤ count |
  **Solo**: while a solo primitive is set, starting this effect (`fx_run`, the Continual preview, or any other start
  of the edited definition) fires only that primitive; the others get no slot.

  **5. Primitive pages.** While a page is open, the main page is not printed and §3 goes to that page. Row 0 of
  every page ("Go back to Main Effect" / "Exit to effect control") closes it on a ≠ 0 (back to the main page, cursor
  unchanged; from the particle page back to the emitter page). Each page keeps its own cursor in the primitive.
  Sound, light, decal and feedback pages have one page of text; emitter and particle pages have page keys.

  **Emitter page (31 rows; pages 0–5, 6–15, 16–23, 24–30; 1000: → 6 → 16 → 24 → 2, 999: 0–5 → 24, 6–15 → 2,
  16–23 → 6, 24–30 → 16).**
  | Rows | Line | Field (SPEC-400 emitter table) | Step / range | Valid when |
  |---|---|---|---|---|
  | 0 | `%sGo back to Main Effect\x01\n` | close | — | — |
  | 1 | `%sEdit the particle\x01\n` | open the particle page | — | — |
  | 2, 3 | `Delay Per Burst   %s%7.2f\x01 / %s%7.2f\x01\n` | Delay Per Burst | 0.05, 0 … 1000; delay limit raised to ≥ | — |
  | 4, 5 | `Duration          %s%7.2f\x01 / %s%7.2f\n` | Duration | 0.1, 0 … 1000 | — |
  | 6 | `Style          %s%s\x01\n` (From a Spot, From a Region, In a Line, In a Ring, In a Directed Line, From Position2, From the floor beneath the effect, From the ceiling above the effect, From surface in front of effect) | Style | integer, wrap 0 … 8 | — |
  | 7 | `Elements       %s%d\x01\n` | Elements | integer, minimum 1 | — |
  | 8–10, 11–13 | `Minimum %s%5.1f\x01 / %s%5.1f\x01 / %s%5.1f\x01  Maximum    %s%5.1f\x01 / %s%5.1f\x01 / %s%5.1f\x01\n` (styles 1, 2, 4; style 0 prints `\n`) | Region min / max (forward, right, up) | 10, min ≤ max per component | style 1, 2 or 4 |
  | 14 | `Radius         %s%.1f\x01\n` (style 3) | ring Radius | 1, minimum 0 | style 3 |
  | 15 | `Dist per Unit  %s%5.1f\x01\n` (styles 2, 4; else `\n`) | Dist per Unit | 5, minimum 0 | style 2 or 4 |
  | 16 | `Attached To Source        %s%s\x01\n` | flag 0x01 | toggle | — |
  | 17, 21 | `Scale By Element          %s%s\x01Get Dir From Offset       %s%s\x01\n` | flags 0x02, 0x20 | toggle | — |
  | 18 | `Scale By Time             %s%s\x01\n` | flag 0x04 | toggle | — |
  | 19, 20, 22, 23 | `Element Scale        %s%5.1f\x01 / %s%5.1f\x01    Time Scale       %s%5.1f\x01 / %s%5.1f\x01\n` | Element Scale, Time Scale (two values each) | 0.1, minimum 0, unordered | — |
  | 24, 28 | `Scale Velocity                    %s%s\x01 Scale Width                %s%s\x01\n` | scale targets 0x01, 0x08 | toggle | — |
  | 25, 29 | `Scale Acceleration                %s%s\x01 Scale Alpha                %s%s\x01\n` | 0x02, 0x10 | toggle | — |
  | 26, 30 | `Scale Height                      %s%s\x01 Scale Duration             %s%s\x01\n` | 0x04, 0x20 | toggle | — |
  | 27 | `Scale Radius                      %s%s\x01\n` (style 3; else `\n`) | 0x40 | toggle | style 3 |
  Yes/no fields print `Yes` or `No ` (with a trailing space).

  **Particle page (83 rows; pages 0–12, 13–24, 25–35, 36–45, 46–53, 54–65, 66–72, 73–78, 79–82).** 1000 goes to the
  next page start: 0–12 → 13 → 25 → 36 → 46 (only with Apply Physics, else 54) → 54 (only for Ghoul types 3/4, else
  66) → 66 → 73 → 79 → 0. 999 goes back likewise (0–12 → 79; pages 46 and 54 skipped under the same conditions).
  | Rows | Line(s) | Field (SPEC-400 particle table) | Step / range | Valid when |
  |---|---|---|---|---|
  | 0 | `%sExit to effect control\x01\n` | close | — | — |
  | 1, 2 / 7, 8 | `Forward Vel     %s%7.1f\x01 / %s%7.1f\x01   Forward Accel   %s%7.1f\x01 / %s%7.1f\x01\n` | Forward Vel / Forward Accel | 10 | — |
  | 3, 4 / 9, 10 | `Right   Vel     …   Right   Accel   …` (same format) | Right Vel / Right Accel | 10 | — |
  | 5, 6 / 11, 12 | `Up      Vel     …   Up      Accel   …` | Up Vel / Up Accel | 10 | — |
  | 13 | `Particle Type     %s%s\x01\n` (Sprite, Line, Oriented Sprite, Ghoul Object, Ghoul Persistent Debris) | type | integer, wrap 0 … 4 | — |
  | 14, 15 / 19, 20 | `Height Start    %s%6.1f\x01 / %s%6.1f\x01       Width Start     %s%6.1f\x01 / %s%6.1f\x01\n` (with Width Equals Height: `Height Start    %s%6.1f\x01 / %s%6.1f\x01\n`) | Height Start / Width Start | 1, 0 … 1000 | width rows: flag 0x01 clear |
  | 16, 17 / 21, 22 | `Height End      …       Width End       …` (same rule) | Height End / Width End | 1, 0 … 1000 | as above |
  | 18 / 23, 24 | Line: `Point2 Source     %s%9s\x01   PointEndScale %s%6.1f\x01 / %s%6.1f\x01\n`; Oriented Sprite: `Orientation     %s%9s\x01\n`; others `\n` | Point2 Source or Orientation / PointEndScale | integer wrap 0 … 3 / 10, 1 … 300 | 18: type 1 or 2; 23, 24: type 1 |
  | 25 / 32 | `Color Style      %s%14s\x01       Alpha Style        %s%12s\x01\n` | Color Style / Alpha Style | integer, clamp 0 … 3 | — |
  | 26, 27 / 33, 34 | `Red             %s%6.1f\x01 / %s%6.1f\x01       Alpha Peak      %s%6.1f\x01 / %s%6.1f\x01\n` | Red / Alpha Peak | 5, 0 … 255 (Red unordered) | — |
  | 28, 29 / 35 | `Green           %s%6.1f\x01 / %s%6.1f\x01       Alpha Min Ratio %s%6.1f\x01\n` | Green / Alpha Min Ratio | 5, 0 … 255 unordered / 0.1, 0 … 1 | — |
  | 30, 31 | `Blue            %s%6.1f\x01 / %s%6.1f\x01\n` | Blue | 5, 0 … 255, unordered | — |
  | 36, 37 / 43 | `Duration        %s%6.1f\x01 / %s%6.1f\x03       fx_trail   \x01%s%s\x01\n` | Duration / trail name | 0.1, 0 … 100 / no action (text cvar) | — |
  | 38, 39 / 44 | `Start Angle     %s%6.1f\x01 / %s%6.1f\x01       Trail Life Time %s%6.1f\x01\n` (types 1, 3, 4: 31 spaces, `\x01`, then the Trail Life Time part) | Start Angle / Trail Life Time | 0.3 / 0.1, minimum 0.1 | 38, 39: type 0 or 2; 44: flag 0x40 or 0x80 |
  | 40, 41 / 45 | `Angle Vel       %s%6.1f\x01 / %s%6.1f\x01       Trail Frequency %s%6.1f\x01\n` (same blanking) | Angle Vel / Trail Frequency | 0.5 / 1, 1 … 60 | 40, 41: type 0 or 2 |
  | 42 | `\x03fx_graphic          \x01%s%s\x01\n` | graphic | action: console command `menu fxgraphic` | — |
  | 46, 47 / 53 | `Bounding X       %s%6.1f\x01 / %s%6.1f\x01       Elasticity      %s%6.1f\x01\n` | Bounding x / Elasticity | 1 / 0.1, 0 … 1 | 46–52: flag 0x10 |
  | 48, 49 | `Bounding Y       %s%6.1f\x01 / %s%6.1f\x01\n` | Bounding y | 1 | flag 0x10 |
  | 50, 51 | `Bounding Z       %s%6.1f\x01 / %s%6.1f\x01\n` | Bounding z | 1 | flag 0x10 |
  | 52 | `\x03fx_collide       \x01%s%s\x01\n` | collide name | no action (text cvar) | flag 0x10 |
  | 54–65 | `Forward Ang     %s%7.1f\x01 / %s%7.1f\x01   Forward Avel    %s%7.1f\x01 / %s%7.1f\x01\n`, `Right   Ang …`, `Up      Ang …` then `\n` (rows 54, 55 / 60, 61 forward; 56, 57 / 62, 63 right; 58, 59 / 64, 65 up) | Ghoul angles / angular velocities | 10 | type 3 or 4 |
  | 66–82 | flag lines (below) | particle flags | toggle | — |
  Flag rows (each label is space-padded fixed text followed by `%s%s\x01`, i.e. colour and `Yes`/`No `; two labels
  per line as listed): 66 Width Equals Height (0x01) with
  70 Apply Physics (0x10); 67 Lit By World (0x02); 68 Additive Transluceny (0x04) with 71 Leave Fade Trails (0x40);
  69 Subtractive Transluceny (0x08) with 72 Leave Shrinking Trails (0x80); 73 Absolute Acceleration (0x100) with
  75 Special CurveToPoint line (0x1000); 76 One Frame (0x2000) and 77 No scaling (0x10000) each after a blank left
  column; 74 Special Cheap Type (USE CAUTION) (0x800) with 78 Remove on collision (0x8000); 79 Collide with water
  (0x4000); 80 Collide with Entities (0x20000); 81 Decelerate to a stop (0x40000); 82 No ZBuffer (0x80000). Style
  names: colour styles Random Linear, Random Any, Linear Slide, Trig Slide; alpha styles Linear Fall, Linear Rise,
  L Rise,Fall, TrigRiseFall; Point2 sources Dir of Velocity, From Server, End of Prev, End2 is vel; orientations
  Forward, Right, Up, Backward.

  **Sound page (5 rows)**: `%sGo back to Main Effect\n` (row 0); `Volume        %s%7.2f\x01 / %s%7.2f\x01   Attenuation   %s%7.2f\x01 / %s%7.2f\x01\n`
  (rows 1, 2 volume: 0.05, 0 … 1; rows 3, 4 attenuation: 0.05, 0 … 3); `\x03fx_sound: \x01%s \n` (sound file name);
  `--set cvar "fx_sound" to soundname to set sound (no "sound" or ".wav")\n`.
  **Light page (11 rows)**: `%sGo back to Main Effect\x01\n`; `Red        %s%7.2f\x01 / %s%7.2f\x01  Radius   %s%7.2f\x01 / %s%7.2f\x01\n`
  (rows 1, 2 red; 7, 8 radius); `Green      … Lifetime …` (rows 3, 4 green; 9, 10 lifetime); `Blue       %s%7.2f\x01 / %s%7.2f\x01\n`
  (rows 5, 6). Colours: step 5, 0 … 255; radius step 10, 0 … 800; lifetime step 0.05, 0 … 5.
  **Decal page (7 rows)**: `%sGo back to Main Effect   \x03fx_graphic       \x01%s%s\n` (row 0, row 4 = image name,
  action `menu fxgraphic`); `Start size      %s%7.2f\x01          Growth Acceleration   %s%7.2f\x01\n` (row 1 start
  size, row 5 the decal field at file offset 0x50); `End size        %s%7.2f\x01          Growth Duration       %s%7.2f\x01\n`
  (row 2 end size, row 6 the field at 0x54); `Orientation   %s%s\x01\n` (row 3: Forward, Downward, Backward; integer
  wrap 0 … 2). Sizes step 1, minimum 0; row 5 step 1 unclamped; row 6 step 1, minimum 0.
  **Feedback page (8 rows)**: `%sGo back to Main Effect\n`; `Blinding Light   %s%6.1f\x01 / %s%6.1f\x01  Deafening      %s%6.1f\x01 / %s%6.1f\x01\n`
  (rows 1, 2 / 5, 6); `Camera Shake     %s%6.1f\x01 / %s%6.1f\x01  ForceFeedback  %s%s\x01\n` (rows 3, 4 / 7); `\n`.
  Ranges step 0.1, 0 … 1; row 7 force-feedback id, integer wrap 0 … 32, shown as "No force feedback",
  "fffx_RandomNoise=0", then `fffx_` + the names of SPEC-400 *Constants* (e.g. `fffx_Jump`).

  **6. Text cvars.** They are read only by the page that uses them, each step while that page is open, and only
  when their string is not exactly "0"; after use the cvar is set back to "0". A value set while another page is
  shown waits until such a page opens and then goes to that page's primitive.
  | Cvar | Page | Effect |
  |---|---|---|
  | `fx_graphic` | particle | Ghoul types 3/4: graphic := value; other types: graphic := `textures/sprites/` + value, a trailing `.m32` (any case) removed |
  | `fx_graphic` | decal | image := `textures/sprites/` + value, a trailing `.m32` removed |
  | `fx_collide` | particle | collide effect name := value |
  | `fx_trail` | particle | trail effect name := value |
  | `fx_sound` | sound | sound := value + `.wav`, registered at once. Its default is "null", so opening a sound page before setting it turns the sound into `null.wav` |
  The images are not registered by the editor (only when the effect is loaded or re-registered).
  **Menu `fxgraphic`** (`menus/fxgraphic.rmf`, page `fxfile`): header "Select a sprite", a file box of
  `textures/sprites/*.m32` sorted by name that writes the chosen file name into `fx_graphic`; a double click closes
  the menus. The next step then applies it as above.

  **7. Corrections to SPEC-400.** Cvar table: `fx_partadjust` is the cursor code of §3 (move / page / delete / solo),
  not an amount. Decal table: the editor labels file offset 0x50 "Growth Acceleration" and 0x54 "Growth Duration",
  matching their runtime use; the table's statement that the labels are swapped is wrong (the runtime meanings stay).
  OpenMerc **does not match** (no text pages, no row editing).
- Verify: `fx_edit 1` with an effect loaded: the console shows the main page (header lines, then one
  `Effect  1   Effect    …  Delay … / …    Edit` line per primitive) about 60 times per second. `set fx_partadjust 4; set fx_partadd 1` → the type field of
  primitive 1 is selected (red with `fx_editsegment 0`) and becomes "Sound" with a fresh volume 0.60/0.60 and
  attenuation 2.00/2.00. `set fx_partadjust 1000` from row 1 shows primitives 3–6. `set fx_editsegment 2` turns the
  selected field cyan. On a particle page with Width Equals Height set, moving with `fx_partadjust 1` from row 17
  lands on row 18 (Line/Oriented) or 23 (Line) or 25, never on 19–22.

#### I5.23 SPEC-002 alpha-quad flags (+0x38) — bit 0x2000 (Resolution)

- Old: SPEC-002 "Alpha-quad flags (+0x38)" lists 0x0001 … 0x1000; SPEC-412 likewise. Observation (renderer traces of
  an original spectator, session beams1): bit 0x2000 is on every quad of the emitter line particles of
  `othermz/mpgbeam1/2` (sprites `lit`, `lita`), 0x2204 before the first draw and 0x3204 after; never on billboards
  (`flareblue`, `gb_orbit10`, `smoke2`: 0x0202/0x1202). Asked: who sets it, for which lines, and does the renderer
  read it?
- New: bit 0x2000 is an **engine-private** marker that shares the flag word. The renderer never reads it: the
  1.07f renderer has no test of the bit, so it has no visual effect.
  - **Who sets it.** The client effects system, at the birth of an emitter-made line particle whose Point2 Source
    is 0 "Dir of Velocity" (SPEC-400 *Particle creation*, Line). It is set right after the line's ends, widths and
    physics are set up, together with the look-ahead time T = rand(PointEndScale) / |v| of SPEC-400.
  - **What it means to the engine.** Every simulation step re-places the line's end point at the particle's
    predicted position T seconds ahead (start + v·T + a·T²/2; SPEC-400 *Particle simulation*, lines). Lines without
    the bit keep their own end-point motion.
  - **Which lines.** Only those. It is never set on:
    - billboards or oriented sprites;
    - lines with Point2 Source 1, 2 or 3;
    - temp-entity lines and beams (SPEC-401);
    - trail copies of lines: a trail copy starts from cleared flags and explicitly clears 0x2000 together with the
      two bits above it.
  - The observed 0x2204 is beam (0x0004) + additive (0x0200) + this bit; 0x1000 is the "has been drawn" mark of
    chain drawing (SPEC-AMEND-002 1.12).
  Add to the SPEC-002 table: "0x2000 — engine-private (line end follows the predicted path, SPEC-400 Point2 Source
  0); not read by the renderer; an implementation may ignore it." OpenMerc (does not set it) **matches** in every
  picture. Setting it is only needed to reproduce the original's flag words in renderer traces.
- Verify: done for the renderer side (beams look the same without the bit). Trace check: a Point2-source-1 line
  effect (`From Server`) shows 0x0204/0x1204, without 0x2000.

### I. I2d GameSpy lobby chat (items 9–11)

#### I2d.9 SPEC-204 §5.3 / SPEC-AMEND-006 I2d.6 — when a join completes, and the order of events around it (Correction)

- Old: SPEC-204 §5.3: "When the server echoes the join, the client sends `MODE #<channel>`. The join is complete when
  the channel-mode reply (324) arrives." SPEC-AMEND-006 I2d.6: results for a channel the client has joined wait in
  the queue until the join completes. Black box: between the join's names and its mode reply, another user's JOIN goes
  into the user list without a `joined` line; a PRIVMSG and a TOPIC change are delivered after `Successfully
  entered/created channel` and the user list, and only then come the join's own `Topic for … is` (from 332, which
  stays in `menu_topic`) and the `modechange` line. OpenMerc: PART/KICK remove the user silently, MODE is held like a
  message.
- New: the join completes at the **end-of-names reply (366)**, not at the mode reply. SPEC-204's last sentence of the
  `MODE` bullet is wrong. The observed order follows from three rules:

  **A. Join states.** For one channel the client is *joining* (JOIN sent), then *in the channel but not ready* (from
  its own JOIN echo on; `MODE #<channel>` is sent at that moment), then *ready*.
  - The 332 topic reply and the 353 names replies of the join are applied at once to the stored channel data (stored
    topic, user list).
  - The 366 reply completes the join: the join's success result is queued. This result is not tied to a channel.
  - The channel becomes ready only when that success result is **delivered**: just before the lobby's success
    handler runs. That handler writes `Successfully entered/created channel #<channel>!`, sets the current channel and
    fills the user list from the stored list at once (`<n> users found in #<channel>`).
  - The 324 mode reply only stores the channel mode. The first 324 for a channel also queues the channel-mode result
    (the developer-console `Channel #<channel> modechange: …` line). Later 324 replies change nothing visible.

  **B. Queue delivery.** Results are queued in arrival order. Once per engine frame the queue is visited front to
  back, each entry once:
  - an entry tied to a channel the client is not in is discarded;
  - an entry tied to a channel that is not ready stays in place;
  - every other entry is delivered and removed.
  Entries queued **after** the success result therefore come out in the same visit, right after it (the channel has
  just become ready). Entries for that channel queued **before** it, such as the join's own 332 topic result, were
  passed over earlier in the visit and come out one engine frame later. Hence the observed order:
  1. `Successfully entered/created channel`, user list;
  2. PRIVMSG / TOPIC change that arrived after 366;
  3. next frame: the join's own `Topic for #<channel> is "<topic>"` (developer console). It sets `menu_topic` to the
     **332** topic, although the stored topic is already the newer one from the TOPIC change. A later
     `gsc_gettopic` of that channel (I2d.6: answered from the stored topic) shows the newer one;
  4. the modechange line, after (2) or (3) depending on when the 324 arrived.

  **C. Events of other users while in the channel but not ready** (own JOIN echo up to the delivery of the success
  result):
  - JOIN of another user: added to the stored user list; no `joined` result and no user-list refresh is queued, so
    no `joined` line. The user appears in the list filled at the success.
  - PART of another user: removed from the stored list; nothing queued (silent).
  - KICK of another user: removed from the stored list; nothing queued (silent).
  - KICK of the own client: the channel is dropped at once and the kicked result is queued (not tied to a channel).
    The success result was queued earlier, so it is still delivered first (success line, current channel set). The
    user listing then finds the client outside the channel and sends `NAMES #<channel>`. Then comes the kicked line;
    channel-tied entries are discarded.
  - MODE with user modes (`+o`/`-o`, `+v`/`-v`): the user's flags in the stored list change at once. The user-mode
    result (`<nick> opped|voiced|peoned in #<channel>` and a user-list refresh) is queued and delivered after the
    success like a message.
  - MODE with channel modes: until the first 324 reply has stored the channel mode, the change is dropped. A key
    change (`+k`/`-k`) still updates the stored key. After the 324, the change updates the stored mode silently. A
    MODE message never produces a modechange line; only the first 324 does.
  - PRIVMSG, NOTICE, UTM, TOPIC change: queued, delivered after the success (rule B). A TOPIC change also replaces
    the stored topic at once.
  Results of other users' JOIN/PART/KICK received **after** the success is delivered produce their lines and a
  user-list refresh as usual.

  OpenMerc: **matches** for JOIN, PART, KICK of others and user-mode MODE. It must complete the join at 366 (not
  324), drop channel-mode MODE before the first 324, and never print modechange for a MODE message.
- Verify: scripted server: JOIN echo, 332 `A`, 353, 366, then in the same packet `PRIVMSG #c :hi`, `TOPIC #c :B`,
  `MODE #c +o peer`, `MODE #c +m`, and 1 s later 324 `+t`. Expected console/log order: success line and user list;
  `hi`; the topic-B line; `peer opped in #c`; next frame `Topic for #c is "A"` with `menu_topic` = `#c: A`; one
  modechange line (from the 324, `+m` not included).

#### I2d.10 SPEC-204 §5.4 — lobby commands while the chat connection is not registered or closed (Resolution)

- Old: SPEC-204 §5.4: "Without a chat connection, the commands that send do nothing; usage messages still appear."
  Observed: after an ERROR before the welcome, `gsc_join` writes `^4Joining channel: #<channel>`, sends nothing and
  does not change the current channel; `gsc_talk` shows no echo. Unknown: `gsc_part`, `gsc_private` in that state, and
  `gsc_join` after the server closed the connection. OpenMerc: nothing at all, no socket.
- New: the lobby distinguishes three states.
  1. **No session**: never connected, after `gsc_disconnect`, or after a nick error (432/433), which disconnects and
     discards the session. Here `gsc_join`, `gsc_part`, `gsc_talk` and the other channel commands do nothing apart
     from their usage messages; only `gsc_getusers` still clears the user list and `gsc_private` still writes its
     echo (see below).
  2. **Session not registered**: an ERROR came before the welcome. The session is kept and the welcome-time lines are
     printed (SPEC-204 §5.1), but the session is marked not connected.
  3. **Session closed by the server**: the socket closed after registration, or an ERROR after it. The `^4Disconnected
     <reason>` line is written and the user list is cleared, but the session object and the remembered current
     channel are kept, and the session is marked not connected.
  In states 2 and 3 the chat library sends nothing (no JOIN, PART, PRIVMSG, NOTICE, TOPIC, NAMES, MODE, KICK, WHOIS)
  and changes none of its channel data. The lobby commands still do their local part:
  - `gsc_join <ch>`: if `<ch>` equals the remembered current channel → `^4Already in channel: #<ch>!`. Otherwise, if a
    current channel is remembered → `^4Leaving channel: #<old>` (no PART); then `^4Joining channel: #<ch>`. No result
    ever arrives, so no success or failure line, and the current channel does not change.
  - `gsc_part <ch>`: nothing sent; the user list is cleared and `^4Left channel: #<ch>` is written.
  - `gsc_talk <ch> <text>`: nothing sent and **no** echo (the echo of a channel message is produced only after it was
    sent).
  - `gsc_private <nick> <text>`: nothing sent, but the echo line is written exactly as when connected (`^6* <own
    nick> <text>`, `^2-<own nick>: <text>-` or the normal form). The echo appears even in state 1, as long as the
    lobby exists.
  - `gsc_getusers`, `gsc_gettopic`, `gsc_settopic`, `gsc_kick`, `gsc_ban`, `gsc_setusermode`: nothing visible except
    that `gsc_getusers` clears the user list.
  So `gsc_join` after the server closed the connection behaves exactly as after an ERROR before the welcome. Replace
  §5.4's last sentence by this list. OpenMerc **does not match** for states 2 and 3 (it prints nothing for
  `gsc_join` and `gsc_part`, and no `gsc_private` echo).
- Verify: server that answers `ERROR :Closing` before 001: `gsc_join a` → `^4Joining channel: #a`; `gsc_part a` →
  `^4Left channel: #a`; `gsc_private bob hi` → private echo; no bytes sent after the registration lines. Second run:
  join `#a`, let the server close the socket, then `gsc_join b` → `^4Leaving channel: #a`, `^4Joining channel: #b`,
  nothing sent; `gsc_join a` → `^4Already in channel: #a!`.

#### I2d.11 SPEC-AMEND-006 I2d.8 on the private page (Resolution)

- Old: I2d.8 rule 2 scrolls the page only when it has a vertical bar. `a_private.rmf` has none, so the page never
  scrolls while the widget grows to N·L, and the newest private lines would be below the visible area. OpenMerc
  follows the rule as written.
- New: confirmed; that is the original's picture. The private chat page (`a_private`, shown in the `chattext` frame
  of `private.rmf`) has a backdrop and the chat widget, but no `<vbar>`. The public page (`m_chat.rmf`) has one.
  - With no bar, the content height is stored but the page scroll stays 0.
  - The widget's height still grows to N·L once N·L reaches its initial height. Drawing still starts at the
    widget's bottom (screen y + N·L − 4) and goes up.
  - The page clip (the frame rectangle, set before the backdrop) hides everything below the frame. The visible part
    shows the widget's top: the oldest kept lines.
  - New lines become invisible once the log no longer fits. Only when the 100-message cap drops the oldest messages
    does the visible part move on, by one message per new message.
  OpenMerc **matches**.
- Verify: open `menu private`, receive 30 one-line private messages from the selected user: the frame keeps showing
  the first lines that fit; the 30th is not visible. The public page under the same load shows the newest line at the
  bottom.

### J. G10 game-module savegames (items 1–7)

#### G10.1 SPEC-752 §3.1 / open question 3 — the last 92 bytes of `GAME` (Resolution)

- Old: SPEC-752 §3.1: "0x63C, 92: skill settings (from 0x640), cheats-enabled flag (0x668), level-spawner state
  (0x670–0x687) and other game-wide values … (UNVERIFIED split)"; open question 3. Clean-team decode from automatic
  saves at skills 0, 1, 2, 4: 0x63C int (always 0); 0x640–0x658 T-a … T-g; 0x65C encumbrance value; 0x660 S-a;
  0x664 T-h; 0x668 cheats; 0x66C T-i; 0x670–0x684 the six spawner parameters; 0x688 saves allowed; 0x68C S;
  0x690 T; 0x694 difficulty score.
- New: confirmed in full. The bytes 0x640–0x697 are the difficulty record of SPEC-760 B9.2 followed by its score:

  | Offset | Type | Content (SPEC-760 B9.2 names) | Source |
  |---|---|---|---|
  | 0x63C | int32 | unused: no code of the game module reads or writes it; 0 from module load, carried through saves unchanged | — |
  | 0x640–0x658 | float × 7 | T-a, T-b, T-c, T-d, T-e, T-f, T-g | toughness row T |
  | 0x65C | int32 | encumbrance value | encumbrance row E |
  | 0x660 | float | S-a | spawning row S |
  | 0x664 | float | T-h | row T |
  | 0x668 | int32 | cheats (cvar `cheats`, truncated) | cvar |
  | 0x66C | int32 | T-i | row T |
  | 0x670–0x684 | 6 × 4 | alert gain, alert fade time, max-alert duration, spawn interval at max alert, max characters near the player, spawn interval | row S |
  | 0x688 | int32 | saves allowed (cvar `sk_saving`, truncated) | cvar |
  | 0x68C | int32 | S (clamped 0 … 4) | cvar `sk_spawning` |
  | 0x690 | int32 | T (clamped 0 … 4) | cvar `sk_toughness` |
  | 0x694 | float | difficulty score of B9.2 | computed |

  - The record is filled once, by the game's initialisation (InitGame), from the skill cvars (B9.2 presets first).
    It is not refreshed at level changes.
  - Loading a save (export 0x14) overwrites the whole `GAME` record. From then on the difficulty values, the saves
    allowed and the score are the saved ones, whatever the current skill cvars say.
  - At module load, before the first InitGame, S and T hold −1 and the score −1.0.
  Replace §3.1's last row by this table and close open question 3. OpenMerc **matches** (it writes this layout).
- Verify: done (clean-team decode at skills 0, 1, 2, 4). Extra check: change `skill` after loading a skill-3 save and
  save again — 0x640–0x697 keep the skill-3 values.

#### G10.2 SPEC-001 export 0x40 / SPEC-760 B9.2 — the per-level save count (Resolution)

- Old: SPEC-760 B9.2 "saves allowed". Observed: the limit is per level. The count left is in the level record (LVLC
  0x4EC) and comes back with a loaded save. The messages are as listed. Asked: does a new level start with the full
  count, and what does `savesleft` print for −1 (OpenMerc: `unlimited`)?
- New: confirmed, with these exact rules:
  1. **Refill.** The count (LVLC 0x4EC) is set to the game record's saves allowed (`GAME` 0x688) whenever the
     single-player rules put the player into the level: at every level start, after every respawn that re-places the
     player, and when the debug commands `playlevel`, `elbow`, `bigelbow` re-run the loadout. The same step zeroes
     the level's eight per-level counters (LVLC 0x578 … 0x594 and 0x59C) and stores the level time at 0x598.
     A savegame load does not refill: the count comes from the loaded level record.
  2. **Allow save (export 0x40).**
     - Count 0: message `SAVES_NO_MORE` (singleplr 2) and the save is refused.
     - Otherwise the count is decremented and a per-level "saves made" counter (LVLC 0x590) is incremented. Then:
       new count 1 → `SAVES_ONE_LEFT`; new count ≥ 2 → `SAVES_N_LEFT` with the count; new count 0 →
       `SAVES_USED_UP`; new count negative → no message. The save is allowed in all four cases.
     - With unlimited saves (−1) the count keeps going down (−2, −3, …). It never reaches 0, so the saves stay
       unlimited, but the stored value changes with every save.
  3. **`savesleft` (export 0x44)** sets cvar `menu_savesleft` and prints `Saves left : <value>` through the game's
     print, with this value:
     - count < 0 (any negative value): the text of `UNLIMITED` (singleplr 13), English `Unlimited` (capital U);
     - count 0: `0`;
     - count > 0: the number (cvar float value).
     It also creates `menu_savesleft` (default empty, no flags) if it did not exist.
  OpenMerc **matches** except the `savesleft` text: it must use the string-package text (`Unlimited`), not
  `unlimited`, and must also show it for −2, −3, ….
- Verify: skill 3 (2 saves): save twice (`SAVES_ONE_LEFT`, then `SAVES_USED_UP`), third save refused
  (`SAVES_NO_MORE`). Change level: `savesleft` prints `Saves left : 2`. Skill 0: save three times, then `savesleft`
  prints `Saves left : Unlimited`; LVLC 0x4EC in the last save is −4.

#### G10.3 SPEC-752 §3.2 / SPEC-780 §1 — persistent fields of `CLNT` (Resolution)

- Old: SPEC-752 §3.2 "All other bytes are plain image". Clean-team decode: userinfo 204 (512), name 716 (16),
  best-weapon string 736, remembered weapon id 752, field of view 756 (int), connected 760, health 764, maximum
  health 768, old movement state from 0x32C, inventory presence 844. Asked: confirm; list 732, 744–751, 772–811.
- New: confirmed. The missing fields are:

  | Offset | Size | Content |
  |---|---|---|
  | 732 | int32 | spectator flag of the persistent block: 1 when, in deathmatch, the userinfo key `spectator` is set and not `0`; else 0 (set at every userinfo change) |
  | 736 | 16 | best-weapon buffer: userinfo `bestweap`, copied with at most 15 characters. 744–751 are the rest of this buffer (zero unless the value is longer than 8 characters); the copy does not write a terminator, so byte 751 stays 0 |
  | 752 | int32 | remembered weapon id (−1 at connect) |
  | 756 | int32 | field of view (integer) |
  | 760 | int32 | connected flag (SPEC-752 1.2: cleared after a level load) |
  | 764, 768 | int32 | health, maximum health (100, 100 at connect) |
  | 772–811 | 40 | **per-level block**, cleared by every `ClientBegin` except offset 804, then 772 := level frame number: |
  | 772 | int32 | frame number at which the client entered the level |
  | 776 | int32 | score |
  | 780, 784, 788 | float × 3 | view angles of the last user command (written by every client think) |
  | 792 | int32 | copy of the game's help-changed counter (`GAME` 0x400) |
  | 796 | int32 | help-changed flag (set when 792 had to be updated) |
  | 800 | int32 | spectator / chase state used by the scoreboards and the chase camera |
  | 804 | int32 | team (1, 2; 0 none), kept across `ClientBegin` |
  | 808 | int32 | unused (cleared) |
  | 812 … | | old movement state (0x32C), as decoded |
  | 844 | int32 | inventory presence (encoded field) |

  - 764 and 768 are refreshed from the player entity, together with 752 (from the inventory's current weapon) and
    756 (from the view field), before a full (non-automatic) save and at the start of every level spawn.
  - The per-level block is restored by a load, but the next `ClientBegin` of a new level clears it.
  OpenMerc **matches** (same offsets). It should keep 776–808 as listed if original full saves are to load with
  the right score and team.
- Verify: deathmatch with `spectator 1` in userinfo: 732 = 1. Team mode: 804 = 1 or 2. A full save in single player:
  772 = the level frame number at the player's entry.

#### G10.4 SPEC-752 §3.4 — every entity in use is linked after a save (Resolution)

- Old: §3.4: "The record is copied while the entity is unlinked from the world … and the entity is relinked
  immediately." Observation: after an original save every entity in use is linked, also those never linked before.
- New: confirmed. For every entity in use, in number order and starting with entity 0, the writer:
  1. writes `EDNM`;
  2. unlinks the entity;
  3. copies the record;
  4. **links** the entity (the engine's link: bounds from the entity's box, area, clusters);
  5. writes `EDCT` and its strings.
  The link happens whatever the entity's previous state, so a never-linked point entity becomes linked with its
  own box. On load each entity is linked once after decoding (§1.2). OpenMerc **matches**.
- Verify: done (observation). An `info_notnull` that was never linked shows linked bounds (origin ± its box, or ± 1
  with an empty box as observed) right after a save.

#### G10.5 SPEC-752 §1.2 — the player module's two post-load hooks (Resolution)

- Old: "the player module's two post-load hooks are called (SPEC-003)". OpenMerc calls nothing in ReadLevel; its
  ClientBegin sends svc 29, svc 30 (+ slot 78) and calls export 11.
- New: at the very end of the level read, after the Ghoul id remap, the game calls two player-module exports (SPEC-003
  "game → player" table) in this order:
  1. export 8 (`void (void)`): forget all cached in-view model objects;
  2. export 7 (`void (unsigned weaponMask)`): precache the in-view models of the weapons whose bit is set. The mask
     is the level record's in-view precache mask (LVLC 0x564: the weapons registered for the level by
     `worldspawn`, the loadout and the weapon precache of SPEC-782 §12).
  Nothing else of the player module is called by the level read. OpenMerc **does not match**: it must call 8 then 7
  with LVLC 0x564 at the end of ReadLevel. (Its ClientBegin calls stay as they are.)
- Verify: tracing proxy player module under the original: on `load`, the first player-module calls after the
  level read are export 8, then export 7 with a mask equal to LVLC 0x564 of the save.

#### G10.6 Ghoul objects on ReadLevel (Resolution)

- Old: SPEC-752 §1.2 frees level memory and re-creates the model objects from the attached-object chunks. OpenMerc
  destroys every object of the server Ghoul system there, also the player module's cached in-view models, and then
  re-precaches them (exports 8 and 7). Asked: does the original keep the spawn's objects alive?
- New: no, the original destroys them exactly as at a level spawn. Before reading, the level read:
  1. frees every entity in use (removing its instances);
  2. clears the entity array;
  3. clears the game's model registry, destroying every model object it holds;
  4. calls Ghoul server method 6 (destroy all objects of the server system). This removes the player module's
     cached in-view objects as well.
  The entity-attached objects are then rebuilt from `OINM`/`EDOI`, and G10.5's exports 8 and 7 rebuild the in-view
  models. OpenMerc **matches**.
- Verify: wrap the Ghoul server interface in the tracing proxy: one method-6 call per `load`, before the first
  object creation of the load.

#### G10.7 SPEC-303 — `save` on a dedicated server (acknowledgement; cause not found)

- Observation: the original engine's `save` stops a dedicated server before export 0x40 is called (no log line, no
  game-module frame on the stack). After a `load` in a listen game the game is paused (SPEC-303 step 6).
- Acknowledged. The path of `save` up to export 0x40, in order:
  1. checks of the argument count, `deathmatch`, the reserved names and "a game is running";
  2. the "dead" check: it reads the health statistic of the first client slot through that slot's entity and the
     entity's client record, without testing that the record exists;
  3. the name check;
  4. the game's cinematic query;
  5. export 0x40.
  With the shipped module every client-slot entity gets its client record at each level spawn, so step 2 reads a
  valid record (an empty slot reads health 0 and gives `Can't savegame while dead!`). Nothing on this path explains
  a stop. The cause is UNVERIFIED and needs a debugger capture of the stopping run (faulting address and stack).
  OpenMerc should not stop: it should apply the SPEC-303 refusals in this order (so a dedicated server without a
  living player in the first slot prints `Can't savegame while dead!`). No spec change.
- Verify: black box: the same dedicated run under a debugger; record the faulting instruction's module.

## Constants

### B. G4 roster, helicopters, vehicles, special characters

| Name | Value |
|---|---|
| am16 factors | 182.04444 (65536/360) and 0.0054931640625 (360/65536); trunc toward zero, low 16 bits |
| aim controller: range tolerance / dead zone / back-inside threshold / aligned tolerance | 0.0005 / 0.01 / 0.0001 / 0.001 rad |
| aim controller: current-axis point | 100 units along the part's x axis |
| crew head attach bolts ahead of the entity origin (Cobra) | pilot ≈ 44.2, gunner ≈ 84.8 units |
| pilot head visible yaw limit for ±1 rad | ≈ ±79.1° |
| strafe fire threshold | v > 20 units per frame |
| rocket gravity factor | 0.02 |
| crew sniper counter | 1, shared by pilot and gunner |

## Test vectors / how to verify

Each item ends with its own "Verify" line; those are the test vectors of this amendment. Items whose answer still
needs a run of the original game are listed per section under "Open questions", with the black-box test to run.

## Open questions

### A. I1b module host and protocol-33 server

- I1b.5: whether the server shutdown after `serverrecord` + `serverstop` in one frame faults (invalid handle) is
  UNVERIFIED; it does not affect OpenMerc if the recommendation is followed.

### B. G4 roster, helicopters, vehicles, special characters

1. Needs a black-box run: G4.17 item 5 — the explosion of a missed rocket on solid ground beside the player (test in
   the item); the code side (aim from the origin, sky removal) is decided.
2. Needs a black-box run: G4.S1 — a sniper kill of the pilot on a helicopter without spawnflag 16 (test in the item).

### E. G18 game module

- G18.42: which condition makes the standing aim choice of a deathmatch player return `std_a_fwd_p_cb` for 0.2 s
  before `std_a_fwd_p_2` (black box: switch to the 9 mm while standing still and record the first two records; repeat
  after firing once).

### J. G10 game-module savegames

- G10.7: why `save` stops the original dedicated server; the engine path up to export 0x40 shows no fault. Needs a
  debugger capture of the stopping run.
