# SPEC-AMEND-010 — client-side movement presentation (timing, prediction, smoothing, interpolation); follow-ups C3 and A1 of AMEND-009

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Evidence: static analysis of the original client/host engine and game module (per item); spec cross-reference;
existing G6b/I1b measurements where named
Describes: SoF.exe (client and host engine), gamex86.dll (game module), version 1.07f; behaviour only
Depends on: SPEC-202 §3/§5, SPEC-301, SPEC-302, SPEC-310 §12/§13, SPEC-312, SPEC-314 §19–§21, SPEC-780 (bob),
SPEC-AMEND-005 G6b.1, SPEC-AMEND-007 (server frame rules), SPEC-AMEND-009
Amends: SPEC-310 §12 (adds explicit rules), SPEC-202 §3 (one refinement), SPEC-751 / AMEND-009 A1, C3
Purpose: give C one place that lists every client-side rule that affects how smooth player movement looks, mark
which parts are already specified, and add the parts that were only "as Quake II" or missing. Priority: the user
reports jittery movement on OpenMerc.

Note: These rules affect feel, not the wire. Wire formats follow SPEC-201/202 as amended by the legal-clean deviation;
network compatibility with original 1.07f is not a goal (user decision 2026-10-26).

---

## Part 1 — Coverage map

| Topic | Where it is already specified | This amendment |
|---|---|---|
| Player movement routine (server and prediction) | SPEC-314 §1–§19, constants | — |
| Prediction loop, step detection, prediction error | SPEC-314 §20–§21 | M4 (command range, cadence), M5 (error decay) |
| User-command generation (msec, pitch clamp, key fractions) | SPEC-312 | M2 (cadence) |
| Client frame scheduling / frame rate cap | not specified (only the `cl_maxfps` cvar) | **M1 (new)** |
| Client time, interpolation fraction, time clamps | SPEC-310 §12 "as Quake II"; SPEC-202 §5 (game-time estimate) | **M3 (explicit)** |
| View origin, stair smoothing, error decay, angles | SPEC-310 §12 "as Quake II" | **M5–M7 (explicit)** |
| Entity interpolation | SPEC-202 §3 | M8 (refinement) |
| View bob, run pitch/roll, view-offset clamp | SPEC-780 (server side, sent as view offset / kick) | M9 (confirmation) |
| View-weapon sway and landing dip | SPEC-310 §13 | M9 (confirmation) |
| Server frame cadence | SPEC-AMEND-007 (frame rules) | M10 (catch-up rule) |

---

## Part 2 — Items

### M1. Resolution — host and client frame scheduling (NEW)
Evidence: static.
1. Host loop: the host waits (busy) until at least 1 ms of real time has passed since the previous host frame, then
   runs one host frame with that elapsed time. It sleeps 1 ms per iteration only when the application is inactive
   (minimised / not focused) or when running dedicated. There is no other host-level frame cap.
2. Each host frame runs, in this order: console input and command buffer, **server frame**, **client frame**.
3. The client accumulates elapsed time. A client frame runs when **any** of:
   - `timedemo` ≠ 0; or
   - (the client is not in the "connected, not yet active" state, or ≥ 100 ms accumulated) **and** (a server is
     running in this process, **or** accumulated time ≥ 1000 / `cl_maxfps` ms).
   So with a local server (single player, listen server) `cl_maxfps` does **not** limit the client: the client
   (input, command, prediction, rendering) runs every host frame, limited only by the renderer (e.g. vsync).
   `cl_maxfps` (default 30) caps the client only when connected to a remote server. While connected but not yet
   active, the client runs at most every 100 ms.
4. Order inside a client frame: input frame; frame-time update (M3); read packets (frames are parsed here); client
   weapon prediction; key events, command buffer; send command (M2); connection resend; **movement prediction**
   (M4); refresh preparation; **screen update** (view computation M5–M8, rendering); sound update with the view
   origin and axes; light updates.
Test: single player with `cl_maxfps 30` and vsync off: the rendered frame rate and the user-command rate are both
far above 30 (count clc_move messages or commands per second in a loopback log). Against a remote server the
command rate equals the `cl_maxfps` rate.

### M2. Refinement — one user command per client frame
Evidence: static (SPEC-312 already has the field rules).
Every client frame that runs builds exactly one user command from the input of that frame, stores it in the
64-entry command ring and (when active) sends it. The command's msec = trunc(frame time × 1000) where the frame time
is already clamped to at most 1/`cl_minfps` (M3); msec > 250 becomes 100. There is **no** separate packet rate, no
"in-progress" command and no re-sampling of a partially built command between client frames: input, command,
prediction and view all advance together once per client frame.
Consequence for an engine that renders faster than it builds commands (asynchronous client): the original has no
such mode; to look like the original, either build and predict one command per rendered frame, or predict the
in-progress partial command every rendered frame.
Test: SP, 125 fps: consecutive commands carry msec 8 (or 7/8 alternating), never a fixed 16/33.

### M3. Resolution — client time, interpolation fraction, clamps (explicit)
Evidence: static.
- Frame time: each client frame, frame time = accumulated ms × 0.001. The client time (integer ms) advances by the
  accumulated ms **before** the frame time is clamped; then the frame time is clamped to at most 1/`cl_minfps` for
  every other user (movement commands, effects, sway). The real-time stamp used for step smoothing is the host's
  millisecond clock at this point.
- Game-time estimate (SPEC-202 §5): advances by the (unclamped) frame time only when `paused` = 0, the client is
  active, and `freezeworld` = 0.
- Server time of a frame = frame number × 100 ms (fixed 10 Hz).
- On every parsed frame: if client time > server time, client time := server time; if client time < server time −
  100, client time := server time − 100 (no message). (This clamp at parse time is in addition to the render-time
  clamp below.)
- On every render, before entities and view are computed: if client time > server time → client time := server
  time, fraction = 1 (`high clamp <n>` printed with `showclamp`); else if client time < server time − 100 → client
  time := server time − 100, fraction = 0 (`low clamp <n>`); else fraction = 1 − (server time − client time) × 0.01.
  With `timedemo` ≠ 0 the fraction is forced to 1.
- The same fraction is used for the player view (M5–M7), all entities (M8) and the view weapon's back-lerp.
Consequence: the client always draws between the previous and the latest frame and never extrapolates; at a
steady 10 Hz the fraction sweeps 0→1 over each 100 ms interval.
Test: with `showclamp 1` in SP at steady frame rate, clamp messages appear only around level start, loads and
hitches, not continuously.

### M4. Refinement (SPEC-314 §20) — prediction command range and cadence
Evidence: static.
Prediction runs once per client frame, after the command of that frame was sent, and replays **every** command from
the one after the last acknowledged command up to and including the command just sent (oldest first), starting from
the movement state of the latest received frame. The just-built command is therefore always predicted; nothing is
predicted between client frames. Prediction does not run while `paused` ≠ 0, while the player state's
cinematic-freeze byte is non-zero, or while not active. With prediction off (`cl_predict` 0 or the "no prediction"
movement flag), predicted angles = command view angles + delta angles and no origin is predicted.
After the loop: predicted origin = final stored origin ÷ 8; predicted angles = the movement routine's view angles;
step smoothing rule of SPEC-314 §20 (64…159 eighths, ground flag) stores step height = difference ÷ 8 and step time =
real time − frame time × 500 ms.
Test: SPEC-314's vectors plus `cl_showmiss 1`: walking on flat ground, stairs and slopes in SP must print no
"prediction miss" lines (expected under the original; any steady stream of misses on OpenMerc means the client's
movement routine or its inputs differ from the server's — the most likely single cause of visible jitter).

### M5. Resolution — view origin (explicit, matches Quake II)
Evidence: static.
Previous frame = the frame with number (current − 1) if it is valid and stored, else the current frame. If any origin
axis of the player state differs by more than 256 units between the two, the previous frame is replaced by the
current one (snap, no interpolation).
- Prediction off: view origin = prev.origin + prev.view offset + fraction × ((cur.origin + cur.view offset) −
  (prev.origin + prev.view offset)) (origins in 1/8 units converted to units).
- Prediction on: view origin = predicted origin + prev.view offset + fraction × (cur.view offset − prev.view
  offset) − (1 − fraction) × prediction error.
- Stair smoothing (prediction on only): if (real time − step time) < 100 ms, view z −= step height × (100 − (real
  time − step time)) × 0.01. A step is thus smoothed over 100 ms of real time, linearly.
- Then: camera shake (SPEC-400), night-vision jitter and remote camera override (SPEC-310 §12).
Test: walk up a staircase of 16-unit steps at 300 u/s: view z rises in linear ramps of 100 ms per step, without a
jump at the step frame.

### M6. Resolution — prediction error decay
Evidence: static.
On each parsed valid frame (prediction on, flag clear), error = received origin − origin predicted for the
acknowledged command (eighths). |dx| + |dy| + |dz| > 640 eighths (80 units): error := 0 (teleport, snap). Otherwise
the stored prediction for that command is replaced by the received origin and error = delta ÷ 8. The view applies
(1 − fraction) × error (M5), so a correction fades out linearly over the next 100 ms interval of client time; it is
not reapplied in later frames.
Test: force a 4-unit server-side correction (e.g. a small push): the view moves to the corrected position over ~100
ms, not in one frame and not with an overshoot.

### M7. Resolution — view angles and kick
Evidence: static.
- Movement types 0–2 (normal, spectator, dead — the three lowest values): view angles = predicted angles every
  client frame (no interpolation). Other types: interpolated between previous and current player-state view angles
  with the fraction (shortest way, ±180°).
- Kick angles: interpolated between previous and current player state and added.
- Weapon kick: interpolated from the player state, or with client weapon prediction the predicted kick
  (SPEC-310 §12.2) added instead.
Test: mouse look in SP is as smooth as the render rate (angles change every rendered frame, not every 100 ms).

### M8. Refinement (SPEC-202 §3) — entity interpolation
Evidence: static.
- Every entity (including other players and moving brush models) is drawn at prev.origin + fraction × (cur.origin
  − prev.origin), unless its renderfx has "do not interpolate origin" (then the current origin). Angles likewise,
  shortest way; the auto-rotate effect uses a time-based yaw instead.
- Reset (no interpolation for that frame: previous := current, with previous origin := the entity's sent old
  origin) when the model index or the second model index changed, any origin axis moved more than 512 units, or the
  entity was not in the previous frame. **Unlike Quake II there is no reset on a teleport event**: a teleport of at
  most 512 units on each axis is interpolated (drawn sliding over 100 ms).
- The local player's own entity is marked "viewer's own model" (not drawn in first person).
Test: a `misc_teleporter`-style jump of 300 units: the entity slides over one frame interval in the original.

### M9. Confirmation — view bob, run tilt, view-offset clamp, view-weapon sway
Evidence: spec cross-reference (static for the client side).
The view bob, run pitch/roll and view-offset clamping are computed by the game on the server once per server frame
(SPEC-780) and reach the client as player-state view offset and kick angles; the client only interpolates them
(M5/M7). The view weapon's sway and landing dip are client-side and per rendered frame (SPEC-310 §13). There is no
other client-side bob.
Consequence: an engine that also applies Quake II's client-side bob, or that computes the server bob at a different
cadence, will double or alias the bob.
Test: stand still → no bob; run → view z offset changes only at 10 Hz in the received state and is smoothed by M5.

### M10. Refinement (AMEND-007) — server frame catch-up
Evidence: static.
Per host frame the server adds the elapsed ms to its real time and runs **at most one** game frame, when its real
time ≥ server time (with `timedemo` 0). If its real time is behind server time by more than 100 ms it is set to
server time − 100 (`sv_lowclamp`) and no frame runs. After running a frame, if server time is behind real time,
real time := server time (`sv_highclamp`). Hence a long host frame (loading, hitch) is **not** caught up: game time
slows down instead of running several frames at once.
Test: insert a 500 ms stall: the server runs one frame after it, not five.

### M11. Diagnostic checklist for the reported jitter (guidance for C; not behaviour)
In decreasing likelihood, compare OpenMerc against M1–M10:
1. Client prediction uses a movement routine or inputs (run flag, speed scale byte, stagger 0, command splitting)
   different from the server's → steady prediction misses (M4 test).
2. Asynchronous client: commands at a fixed packet rate while rendering faster without predicting the partial
   command (M1/M2).
3. Interpolation fraction or client-time clamp differing from M3 (e.g. extrapolation, or a different 100 ms base).
4. Stair smoothing or error decay over a different time base (M5/M6).
5. Client-side Quake II view bob added on top of the server's (M9).
6. Server catching up several frames after a hitch (M10).

---

## Part 3 — Follow-ups to AMEND-009

### C3. Resolution — trn1 intro camera timing
Evidence: static + level data + existing G6b measurement.
- The trn1 intro is started by the level's precache script (worldspawn `script` key), which uses the
  level's `introscript` entity (map data) at its end. The worldspawn runner is used only at the end of the single-player ClientBegin
  (AMEND-005 G6b.1). So the intro starts at the server frame in which the local client begins, not at level spawn.
- Server frames run from level spawn regardless of clients (M10: at most one per host frame, no catch-up), so the
  frame of ClientBegin depends on how long the engine's connection and loading takes. It is not a fixed frame.
- G6b already compared the intro relative to its first caption and found it identical (757/757 lines). The I1b
  difference used different anchors: server frame number for OpenMerc, wall-clock time from the "Map:" line for the
  original. So it measures the begin delay, not a camera difference.
- Verdict: not a camera or script defect. The only possible engine difference is the number of server frames
  between level spawn and ClientBegin.
Test: log the server frame number at ClientBegin and at the first caption in both engines. The difference between the
two must be equal (expected: equal, per G6b). Compare the begin frame itself only if the intro shows the wrong segment
relative to the player's arrival.

### A1. Confirmation — "dead" state for camera activation
Evidence: static.
The value AMEND-009 A1 tests is the entity's death state. The game sets it to the "dead" value (2) in these cases:
- player death;
- the forced suicide on a team or model change in team deathmatch;
- character death actions (normal and head deaths);
- vehicle deaths;
- silent AI removal.
It is reset to "alive" (0) when a client is put into the server and when a level is read. The camera check applies only
to activators that are clients. So a dead player (health ≤ 0, waiting to respawn) can't start a camera.
Test: in DM, die and let a trigger use a camera while you are dead. Nothing happens. After respawning, the same
trigger works.

## Open after this amendment
- M4 expectation "no prediction misses in SP under the original" is static reasoning; confirm with `cl_showmiss 1`
  on the original.
- M8 teleport slide: confirm in game.
