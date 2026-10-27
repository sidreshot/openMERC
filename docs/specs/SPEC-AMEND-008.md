# SPEC-AMEND-008: Answers to open questions G2.19, I1d.1/2/5 and the G4/G7 "Needs" (coverage audit 2026-10-26)

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Evidence: static analysis of the 1.07f binaries for every item; no item is black-box verified yet. Each item ends with
the black-box test that would confirm it.
Describes: SoF.exe, gamex86.dll, player.dll, version 1.07f
Depends on: SPEC-003, SPEC-400, SPEC-602, SPEC-700, SPEC-701, SPEC-710, SPEC-760, SPEC-AMEND-007
Amends: SPEC-701 §5 (clarification only), SPEC-700 §5 (clarification only), SPEC-003 client imports 4/6,
SPEC-400 "View weapon", SPEC-602 owner context, SPEC-760 B4.5 and B4.7 (confirmation; no text change)

Item kinds as in SPEC-AMEND-002 (Correction / Resolution / Refinement / Confirmation).

## G2.19 — view cone of an awake, not yet alerted character (Resolution, partial)

Question: does waking switch an un-alerted character to the wide cone, or does sight have a further condition for
still players; and what did kos1 `wallguy` perceive?

Answer (static):
1. **Waking does set the wide view mode** (cos −0.2) in the original, exactly as SPEC-700 §5 says. The original's
   wake conditions are those of SPEC-700 §5; a character 160 units from the sight client with spawnflag 16 clear is
   woken by the 800-unit rule on its next think. The kos1 characters in question (`armorguy2` spawnflags 65536,
   `pistolguy` 0, `wallguy` 0, the two `t22` grunts 4) have neither spawnflag 2 (blind) nor 16 (dormant) nor 32
   (inattentive), so they wake, keep their sight sense and use the wide cone. SPEC-701's "set when the character
   becomes active" means *awake*, not *alerted*. OpenMerc's reading is correct.
2. **Sight has no extra condition for still targets.** The only motion term lowers the threshold for fast targets
   (SPEC-701 §5). Light level and the alert state change only the probability roll, and that roll gates only the
   second (lean-proxy) visibility trace; the plain eye-to-eye trace is always made. So with the player 38° off the
   facing of `armorguy2`, visible, the sight sense records him in the original too.
3. Consequently the difference reported in the black-box runs is **downstream of sight**, in one of:
   a. the current-action rule (SPEC-700 Think order): while a character has a current action, each think runs that
      action; the decisions are consulted only when there is no current action or the action reports that it has
      ended. A fresh sight record does not by itself end an idle or path-wait action; a sense reports a new record
      to the character's decision owner only when the sense marks the record as new (SPEC-701). A character in a
      long or looping idle (`pistolguy` in its `std_igunup` path wait) therefore keeps it although it sees the
      player. Check that OpenMerc does not re-decide every think on a fresh sight record.
   b. the reaction moment (SPEC-AMEND-007 G2.18 remaining question): the reaction moment is set to now ± 0.2 s only
      when the best player record is *older than 1 s* at a sense update; a record refreshed every update never
      becomes older than 1 s, so the jitter path is not the trigger.
   c. the throttled sense update: characters outside the sight client's PVS refresh senses only every 2–3 s
      (SPEC-701 §3), and a sight record that is not refreshed is kept as "tracked" for only 0.25 s.
   Static analysis cannot decide between a and the decision-layer conditions for the non-scripted characters
   (`armorguy2`, `t22`); this needs the black-box test below.
4. **`wallguy`** (167° behind, 161 units): sight cannot apply (cos 167° ≈ −0.97 < −0.2), and proximity needs 75 units.
   The remaining senses are hearing (any alert sound of SPEC-701 §6 within its radius, e.g. a kind-7 call for help
   of another character alerted within 500 units, or a weapon/impact noise) and a "told" event from a trigger use.
   Unresolved statically; see test 2.

Black-box tests:
1. kos1, `ai_dumb 0`, god, notarget off: stand 160 units from `armorguy2` at 38° and record its sense records with a
   proxy module (SPEC-AMEND-007 method) — expected: player sight record refreshed every think; then log which
   decision is consulted. Expected for `pistolguy`: no decision consulted while its path-wait action holds.
2. Same position at `wallguy`: log the hearing queue and told events of `wallguy` during the dwell; the source of the
   reaction is the first non-sight record written.

## I1d.1 — who plays the view weapon's notes in single player (Resolution)

The **engine client** owns it. Once per rendered client frame, when weapon prediction is *not* active (it is active
only when the prediction cvar is on, no demo is playing and the server is not the local host), the client looks up
the replicated in-view weapon instance (player-state field 1), registers on it — once per instance — note
callbacks for `sound`, `fire`, `altfire` and `effect` plus a default handler that does nothing, and then advances
that instance's events to **client game time + 0.001 s**. This happens before the effects simulation of the frame.
OpenMerc's choice (own callbacks on the instance, events to game clock + 0.001 s once per client frame) matches.
When prediction is active the same notes come from the player module's client imports (SPEC-003).

Test: listen game, log note callbacks with a proxy ref/sound DLL — one call per note at the replicated sequence time.

## I1d.2 — `*` random volume in a `sound` note (Resolution)

The `sound` note's argument string is split on whitespace:
- one word `name`: play `name.wav`, volume 0.6;
- second word `*`: play `name.wav` with volume uniform in [0.3, 0.6) and a start-time offset uniform in [0, 0.1) s;
- second word a number N: play `name` followed by a random integer in [1, N] and `.wav`, volume 0.6.
In all cases: own entity (client entity number + 1), channel 1, attenuation 0 (no attenuation). The note is skipped
entirely while a client flag that the prediction code clears is set (UNVERIFIED meaning; never observed set in
single player). OpenMerc's flrand(0.3, 0.6) is right for the volume; add the 0–0.1 s start offset.

## I1d.5 — inventory without an owner context (Resolution)

The owner context is a **per-inventory** value, not module-wide. A newly created, copied or blob-assigned inventory
has no context. The player-state writes and the view-model operations use the context without checking it, so a
1.07f module used without a context would fault, not "use the last one". The original game always sets the context
before it uses an inventory (also in the paths that re-create it). OpenMerc's skip is a safe superset; the game must
set the context as the current source does.

## I1d.3 and I1d.4

Not answered in this audit (I1d.3 is an implementation note; I1d.4 needs the client Ghoul stream code, queued).

## G4/G18 Needs — trn1 `func_pushkillbox` fling (Resolution)

The original's toss physics has **no check for a movetype changed by the touch**. After the push, it stops when the
entity is no longer in use; otherwise, if the push trace fraction is < 1, it clips the velocity and takes the
landing branch (ground = hit entity, movetype STEP, vz = 0, step physics) only when the **trace plane's normal z is
> 0.7** (and vz < 30 or movetype ≠ BOUNCE), as SPEC-760 B4.5 says. The push itself does not retry or move the entity
when the trace starts all-solid only for movetype TOSS; a FLY helicopter is placed at the trace end and the
obstacle's touch runs. The fling survives in the original because a trace that **starts inside** the killbox's brush
reports no plane (normal zero, as the Quake II collision code leaves it for start-solid traces), so the landing
branch is not taken and the NOCLIP set by the touch stays. Check in OpenMerc what plane the f550 push trace returns:
if it carries a plane with z > 0.7 while start-solid, that is the difference (engine trace, not game code).

Test: trn1 end with `rg_trace 2` in OpenMerc: f550 push trace startsolid/allsolid and plane normal; expected for
parity: start-solid with normal (0, 0, 0).

## G7 Needs — knockback of movetype-10 props whose fall trace is blocked (Confirmation of SPEC-760 B4.7)

SPEC-760 B4.7 is right and must not be removed: in each bump the mover traces the move, then tests the trace's end
position with its box shrunk by 1 unit on every side (zero-length trace, the entity's clip mask). If that test is
start-solid or all-solid against anything other than a player proxy, the mover takes ground (only when the move
trace fraction was in (0, 1), the plane normal z > 0.7 and the hit entity is BSP-solid), its **velocity becomes 0 and
the frame's move ends without changing its origin**. So an upright bench whose shrunk box at the end position is in
solid stays exactly where it is, matching `g7bench90` and `g7shot`. OpenMerc's removal of this stop is the cause.

Test: `g7bench90` and `g7shot` after restoring the stop: positions unchanged, as in the original.
## G4 Needs — helicopter ricochet sparks: position and type (Resolution)

When a hit on a helicopter body is passed on (the hit handler's result is non-zero), at most once per 0.3 s per
helicopter, and not in the special case where the attacker is a client whose current weapon reports class 6 while
the helicopter's armour flag for that case is set (that case only counts down armour hits), the original:
1. sends the spark temp entity (TE 1) with a **type chosen uniformly among 0, 10 and 14** (random index 0–2 mapped to
   0 / 10 / 14);
2. at a **position derived from the hit zone's stored reference point**, not from the impact point: the point stored
   with the helicopter's hit-zone record, transformed by the helicopter's current origin and angles; the direction
   is the zone record's stored direction, rotated the same way. For the stock helicopters these stored values are
   zero, which gives the **entity origin and a zero direction (direction byte 0)** seen in the black-box runs;
3. plays `Weapons/FX/Rics/ric<N>.wav`, N uniform in 1…6, on the helicopter's channel 5 at the hit position.
OpenMerc's use of the converted impact point and a fixed type 0 differs on both counts.
Evidence: static; matches the black-box observation (origin, types 10/14/0, byte 0). The single direction byte 99
observed once is not explained (possibly a different zone record with a non-zero direction; UNVERIFIED).
Test: crew_* sniper hits, 20 shots ≥ 0.3 s apart: every TE 1 at the helicopter origin, types only {0, 10, 14} in
roughly equal shares, one ric sound per spark.
