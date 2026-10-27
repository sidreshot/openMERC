# SPEC-602: Weapon prediction — client-side weapon simulation and its reconciliation with the server
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: player.dll (client and server context), with the engine (SoF.exe) and game (gamex86.dll) parts it relies
on, version 1.07f
Depends on: SPEC-003 (context flag, exports 5/6, client import table, inventory events, svc 28–30, client prediction
loop), SPEC-600 (weapon state machine), SPEC-601 (inventory, state block, variables), SPEC-201 (svc 28 inventory
events, svc 29 restart prediction, svc 30 full inventory), SPEC-202 (player-state weapon fields, user-command fire
times, button inhibit), SPEC-780 (server storage of fire times), SPEC-010/SPEC-011 (Ghoul instances, cloning).

## Purpose

A client connected to a remote server runs its own copy of the player module's inventory so that first-person weapon
animations, muzzle effects, view kick and fire timing respond immediately to the player's buttons. The server stays
authoritative for everything that matters (shots, damage, ammunition, weapon changes, reloads); the predicted copy
follows it through the player state, inventory events and full-state messages, and reports to the server *when* it
fired so that the server's animation — and therefore its shot timing — matches what the player saw. This spec
describes what runs on each side, the data exchanged and the rules that keep the two copies in step.

## Interface / layout

### 1. When prediction is active

- The engine keeps a prediction inventory only while the userinfo/cvar `predicting` is `1` (engine rule, SPEC-003:
  `cl_predict` on and the server is not `localhost`). Local and listen-server players are never predicted.
- The game copies the userinfo value `predicting` into the server inventory's "owner predicts" flag (inventory method
  73) every frame: any value other than `0` means the owner predicts. All server-side differences below apply only to
  predicting owners.
- The client predicts only while the prediction resync number in its player state (SPEC-202 field 5) equals the value
  of the last svc 29; otherwise it waits ("Waiting for prediction resynch", SPEC-003).

### 2. Data from the server used by the predictor

Player-state fields (SPEC-202 table 4; offsets are those of the game's player-state copy):

| Field | Written by the server module | Used by the predictor |
|---|---|---|
| view-weapon model: Ghoul instance id (+ weapon type) | the in-view instance of the server inventory (SPEC-600 Behaviour 12) | a new id signals a weapon change (Behaviour 4) |
| weapon id (the "type" byte of the same field) | current weapon id; during a predicted switch the new weapon's id | target of a dictated switch |
| weapon counter A | rounds in the current clip | overwrites the predicted clip every frame |
| weapon counter B | reserve of the animated weapon's ammo type | overwrites the predicted reserve every frame |
| weapon-state sequence | +1 on every server reload attempt (SPEC-600 Behaviour 11) | a change starts the predicted reload animation |
| prediction resync number | game | Interface 1 |
| button inhibit mask | game (attack not re-armed) | predicted buttons = buttons AND NOT mask |
| field of view | sniper zoom (SPEC-600) | (the client's own zoom writes its predicted value) |

Reliable messages (SPEC-201): svc 28 inventory events (events 0–3 set player variables A, C, D, E; event 4 knife
blood with its argument; event 5 holster toggle; encoding SPEC-003), svc 29 + byte (recreate the prediction inventory,
new resync number), svc 30 + 640 bytes (full state, SPEC-601 Interface 3).

### 3. Data from the client used by the server

- Each user command carries a primary and an alternate **fire time** (SPEC-202 §6.1): when the predictor's "primary
  fired this frame" / "alternate fired this frame" export (SPEC-003 entries 5/6) is set at the end of the client's
  weapon think, the engine puts its current game-time estimate into the corresponding field; otherwise 0.
- The game stores the latest non-zero fire time per client (two values, "pending primary fire time" and "pending
  alternate fire time"; SPEC-780 §5 step 6) and lets the player module read and clear them (server imports 13–16 of
  SPEC-003). The pending primary time is also cleared by the game while the attack button is not re-armed.

### 4. Owner context on each side

| Field (SPEC-003 offset) | Server value | Client value |
|---|---|---|
| attack (0x08) | (buttons OR latched buttons) bit 0x01, and the game's "may fire" flag | command buttons bit 0x01 AND NOT inhibit mask |
| alternate (0x0C) | bit 0x04 | bit 0x04 AND NOT mask |
| extra 1 / extra 2 (0x10 / 0x14) | bits 0x08 / 0x10 (`+weaponExtra1` / `+weaponExtra2`) | same, AND NOT mask |
| lean positive / negative (0x18 / 0x1C) | the player's lean value > 0 / < 0 | a signed lean byte of the client's current input > 0 / < 0 (believed: the command's lean; UNVERIFIED) |
| cinematic freeze (0x20) | player-state cinematic-freeze byte | a client byte believed to be the same field of the predicted state (UNVERIFIED) |
| remote view (0x24) | player-state remote-view type ≠ 0 | a client flag believed to be the same (UNVERIFIED) |
| kick pointer (0x28) | the client's weapon-kick angles (decayed by the game, SPEC-780 §11) | the engine's predicted weapon-kick angles |
| frame length × 10 (0x2C) | 1.0 | client frame time × 10 |
| reload one-shot (0x30) | (not set) | set by the engine when the weapon-state sequence changed; cleared after use |

## Behaviour

### 1. What the predictor runs each client frame

Order (engine loop of SPEC-003, then the inventory think):
1. Owner context set; queued server events applied (SPEC-601: variables, knife blood, holster), queue cleared.
2. Only while the predicted player state's movement type is below 2 (normal or noclip, UNVERIFIED which field), the
   weapon think runs (the command step does nothing in client context).
3. In the think: both "fired this frame" exports are cleared; if the reload one-shot is set it is cleared and the
   **predicted reload start** runs (Behaviour 3); the current slot's clip and the animated weapon's reserve are
   overwritten with the player-state counters A and B.
4. The weapon-change follower (Behaviour 4), then the normal weapon frame of SPEC-600 (holster, per-weapon rules,
   fire starts from the buttons, sequence events at client time + 0.001).

Things the predictor never does on its own: start a reload (its reload start always declines), act on its own
weapon commands (method 4 returns "not handled"), spawn pickups, send events, change cvars, play server imports.
Everything random uses the time-seeded function of SPEC-600 so that both sides choose the same sequences when
"now" is the same.

### 2. Firing

- Client: an attack starts exactly as in SPEC-600 Behaviour 4 from the client's buttons, the sequence starting at
  client time, and the "primary/alternate fired" export is set — also when the ammunition check failed (so the
  server learns about the attempt and performs the dry-fire/auto-reload itself). Fire notes call the client imports
  2/3 (predicted muzzle flash, sounds) and write the predicted kick; `effect`/`sound` notes call client imports 6/4.
  A refire at a sequence end (button held) also sets the export.
- Server, owner predicts:
  - a primary attack starts when a pending primary fire time exists (instead of the attack button); an alternate
    attack when a pending alternate fire time exists;
  - the fire sequence is started at the client's fire time (so its notes up to "now" — usually the first shot — are
    delivered in the same server frame), except: assault rifle and machine pistol primary, and rocket launcher
    alternate, start at "now";
  - the pending time is cleared after use, and also when the attack could not start for lack of ammunition;
  - at a fire sequence's end, refiring requires a pending fire time of that mode instead of the held button —
    except for the flamethrower, knife and MPG primaries, which use the button as for non-predicting owners; a
    refired sequence starts at the pending time except for flamethrower, knife, assault rifle, machine pistol and MPG
    primaries and the rocket alternate, which start at "now";
  - the release rules of automatic weapons (SPEC-600 Behaviour 10) still use the buttons.
- Server, owner does not predict: buttons only, sequences start at "now".

Consequence: the server fires the shots the client predicted, at the client's chosen moments (within the network
delay), and an owner who stops refiring on the client stops refiring on the server.

### 3. Reloading

- Server: as SPEC-600 Behaviour 11; every reload attempt increments the weapon-state sequence byte (except when the
  attempt ended in a forced switch to another weapon).
- Client: when the byte changes the predicted reload start runs: spread counter 0; if the predicted counts say there
  is nothing to load, the dry-fire (or idle) sequence plays; otherwise, if reloading is enabled, state 1 and the
  reload sequence; else idle. Counts are not authoritative (they are overwritten from the player state every frame).
- Shotgun shell-by-shell loading is predicted by the client's own `reload_loop` rule using the server-supplied
  counts.

### 4. Weapon changes

Server, owner predicts — **predicted switch**: when a switch (not drop/lose) starts its put-away (SPEC-600
Behaviour 8), the server immediately:
1. creates the *new* weapon's in-view instance as the "next" instance and puts it into the owner's player state (so
   the client sees the new instance id at once), starting its idle sequence;
2. writes the new weapon id, its clip rounds and its reserve into the player state, and remembers the new weapon id,
   animated id and slot;
3. keeps animating the old weapon's put-away on the old instance (not in the player state any more). While the
   remembered new weapon is set, the end-of-think write of the counters (SPEC-600 Behaviour 3 step 8) is skipped.
When the put-away ends, the old instance is destroyed, the "next" instance becomes the current one, the remembered
weapon becomes current, the remembered values are cleared and the draw sequence starts.

Server, owner does not predict: the new instance and the player-state values change only when the put-away ends.

Client — **switch follower** (runs every think while not holstered):
- When the player state names a view-weapon instance that exists on the client and that the predictor has not
  adopted yet (its user value is empty):
  - if a put-away of the predictor has already finished (switch-now set): the target is selected now — its
    in-view model is made by cloning the server's instance (with children), both instances get the inventory as
    user value (the server instance is now "adopted"), note callbacks are registered on the clone — and its draw
    sequence starts; any pending drop mark is cleared.
  - otherwise, once per change ("synchronised" flag clear): the weapon id from the player state is looked up in the
    slots; that weapon's clip and reserve are set from the player-state counters; a weapon request "select that
    slot" (for ids 13 and 14: the drop/lose request codes, which select the hand slots) is queued as **dictated**,
    the synchronised flag is set and the idle sequence is restarted.
- A dictated request is processed even while the weapon is not ready (the predictor may interrupt its current
  animation); a request that is not dictated is ignored on the client (the idle sequence is replayed instead).
  Processing a dictated request plays the predictor's own put-away (or switches at once when it has none / holds no
  weapon); its end sets switch-now, which the follower completes as above. The synchronised flag is cleared again when
  a draw sequence starts.

Net effect for a predicting owner: both sides play the old weapon's put-away starting at about the same moment and
the client draws the new weapon from the server's freshly created instance.

### 5. Variables, holster and blood

- Player variables A, C, D, E arrive as events 0–3 and are set on the predictor (B, F, G, H, I and the tag are not
  replicated: they only influence server-side decisions).
- Holster toggles arrive as event 5 (the predictor toggles its own flag and plays the put-away when turning on);
  pressing an attack button unholsters on both sides by the same rule.
- Knife blood arrives as event 4 (skin `knifeblood`).
- Events are read from svc 28 into a queue (method 69) and applied at the start of the next client frame (method 70);
  when the client is not predicting it applies them immediately on receipt.

### 6. Resynchronisation

- svc 29 (restart prediction): the engine discards the prediction inventory, creates a new one (all defaults, the
  context flag set to client first) and stops predicting until the player state's resync number matches.
- svc 30 (full state): the 640-byte block is copied into the prediction inventory (pointer words and the "owner
  predicts" flag cleared, in-view instances dropped). The server sends svc 29 followed by svc 30 when an existing
  inventory is restored (savegame load; other triggers UNVERIFIED).
- A fresh prediction inventory owns every weapon of the slot order list with empty clips and all owned flags set
  (in client context the constructor fills the 14 slots from the order list), so that any weapon the server switches
  to can be followed.

### 7. Server-only effects of the shared module

Because the module is shared in a listen server, the context flag selects the side: the engine sets it to client at
the start of each client frame and before recreating the prediction inventory, and to server at the start of each
server frame and around the game's client-command and client-think exports (SPEC-003). A reimplementation that keeps
two separate modules does not need the flag but must reproduce the per-side differences listed here.

## Constants

| Name | Value |
|---|---|
| fire-time fields per command | 2 (primary, alternate), IEEE single, 0 = none |
| weapons whose predicted primary start uses "now" | 4 (machine pistol), 5 (assault rifle) |
| weapons whose predicted primary refire uses the button | 1 (knife), 11 (MPG), 12 (flamethrower) |
| weapons whose predicted primary refire starts at "now" | 1, 4, 5, 11, 12 |
| weapon whose predicted alternate start/refire uses "now" | 10 (rocket launcher) |
| event ids | 0–3 variables A, C, D, E; 4 knife blood (argument); 5 holster toggle (no argument) |
| full-state block | 640 bytes (SPEC-601 Interface 3) |
| client think time base | client game-time estimate (SPEC-202 §5) |

## Test vectors / how to verify

1. Remote server, `cl_predict 1`, 150 ms ping: firing the pistol shows the muzzle flash and kick without delay; a
   network capture shows the user command carrying a fire time equal to the client's game-time estimate of that
   frame, and the server's first-shot temp entity arriving one round trip later.
2. Same with `cl_predict 0`: the fire-time fields stay 0 and the first-person animation starts only when the server
   state arrives (no local view kick).
3. Weapon switch on a predicting client: the svc_playerinfo carrying the new view-weapon instance id arrives at the
   start of the put-away (not at its end); with `cl_predict 0` it arrives at the end.
4. Empty clip on a predicting client: pressing fire sends one fire time; the server starts the reload; the weapon-
   state byte increments in the next player state and the client's reload animation starts on receipt.
5. Machine pistol with 150 ms ping: the server's burst starts at "now" (not back-dated), so its first shot is one
   frame later than for the pistol under the same conditions (compare temp-entity timestamps).

## Open questions

- Which instance the engine draws while predicting (the predictor's clone or the replicated server instance).
- The exact movement-type test the engine applies before running the predicted think.
- Other triggers of svc 29/30 besides savegame loading.
