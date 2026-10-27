# SPEC-AMEND-003: Script VM — answers to the clean team's G6a questions
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-750 (script VM), SPEC-140 (`.os` format), SPEC-731 (vehicle commands)
Amends: SPEC-750 §3.2, §5.3, §9.1, §9.2, §11, §12.7, §12.8 and its open question 5; the ERRATA row "SPEC-750 | 12.8"

## Purpose

Answers OPEN_QUESTIONS "G6a script VM" items 1, 3 and 4, corrects item 2, and re-examines the VEHICLE-id amendment
recorded in ERRATA. Item kinds as in SPEC-AMEND-002: **Correction** (old text is wrong), **Resolution** (old text was
UNVERIFIED or open), **Refinement** (true but incomplete).

## Interface / layout

No new layouts. Notation: a pending signal *entry* is the (entity, signal kind, variable) triple of SPEC-750 §9.1;
"due now" means an event whose time is the quantized current level time (SPEC-750 §3.3).

## Behaviour

### 1. §9.1 "Renner Error #1" (Correction) — G6a item 1

- Old: "Registering a variable that already has a pending signal raises the game error `Renner Error #1: …`."
- Corrected: **1.07f never raises this error.** Its check only matches when the very same pushed reference value is
  registered a second time. Every PUSH creates a new value (variable references included), and the registering
  instruction keeps the one it popped for as long as its entry is pending, so no bytecode can reach the error: neither
  "same entity", "same kind" nor "while waiting" plays any role. This matches the clean team's observation that the five
  stock scripts play in the original. Implementations must not raise it. Keeping both entries (OpenMerc's current
  behaviour) is the original behaviour; `strict_signal_reuse` has no counterpart in the original.
- The older entry is **kept**, never replaced. Registration always appends a new entry. An entry leaves the list only
  when it is delivered (§9.2) or when the script state is destroyed (SPEC-750 §1.2). The watch-handler flush (§3.4)
  deletes queued events but **not** pending entries: an entry whose done event was flushed stays pending.

### 2. §9.2 several entries for one variable (Refinement)

Consequences of §9.2 that decide how the five stock scripts behave:

- **2a.** A completion is not tied to the instruction that asked for it. When action kind k of entity E completes, every
  script consumes **all** its pending entries with entity E and kind k, including entries registered after the
  completed action was started, and adds 1 to the variable of each consumed entry (a variable named by two consumed
  entries gains 2).
- **2b. Different entities, same variable** (`irq1a/hawk_leave.os` 0x064a/0x0667, `kos3/garage.os`): each completion
  adds 1 when it happens. A WAIT_SIGNALS on that variable is satisfied by the **first** completion. With
  clear-after-wait the variable is set to 0 at that moment and the later completion sets it to 1 again, so the next
  WAIT_SIGNALS on the same variable may be satisfied at once by that late completion.
- **2c. Same entity, same kind** (`trn1/trn1_heli2.os` 0x1205 then 0x12bd; the movers of `kos3/ending.os`
  0x0e26/0x0eb6 and 0x0e48/0x0ed8): the first done event that fires for that entity and kind consumes both entries
  (+2); the later done event finds nothing left to deliver.
- **2d. An action that never completes** (`irq2a/irq2a_bridge.os`, dead first actor): its entry stays pending for the
  rest of the level. It does nothing unless the same entity later completes an action of the same kind, which then
  consumes it as well.
- **2e. Second timed MOVE/ROTATE of a busy entity** (refines §3.2 kinds 0 and 1). The destination snapped to by a
  move-done event is the destination of the entity's **most recent** timed MOVE. So when an earlier MOVE's done event
  fires while a later MOVE of the same entity is running, it stops the entity (velocity 0) and puts it at the later
  MOVE's destination at once. An earlier rotate-done event likewise stops a later rotation (angular velocity 0, angles
  not snapped). The later done event then only repeats the snap.

### 3. §12.7 id 3 (Correction, editorial) — G6a item 2

The text is right: 341 ids have an entry, and id 3 has none (it gets the default: kind 0, speed 0, no animation). The
table row `| 3 | 0 | — | — (none) | 13 | 440 |` is the error. Delete it. All 13 stock uses of id 3 are expression
commands (flags 0x440, §12.5), which ignore the table, so behaviour does not change.

### 4. §5.3 parameter declaration executed again (Resolution) — G6a item 3

Neither alternative. When a parameter declaration (0x04/0x05) runs and this script already has a bound parameter of
the same name:
- no parameter string is consumed and no error is raised, even if no strings are left;
- the slot gets the declaration's fresh variable: for 0x04 the type default (§4.1); for 0x05 the declaration's inline
  initial value. The value bound earlier can no longer be reached through the slot. References pushed before the
  re-declaration (for example pending signal entries) still refer to the earlier variable.

This is the same "re-creates the variable" rule as §5.2. OpenMerc's "keep the binding" differs, but no stock script is
affected.

### 5. §11 timed MOVE/ROTATE with zero speed or zero distance (Resolution) — G6a item 4 and §11 open question 5

The frame count is computed in floating point with no guard. Division by zero gives infinity or not-a-number; it
does not crash (default floating-point environment with exceptions masked; UNVERIFIED that the game never unmasks
them — check in game with test vector 5). MOVE applies the formula
N = floor(10 × d / s) + 1 only when **d > 0**, otherwise N = 0. ROTATE applies it only when **s > 0**, otherwise
N = 0 and the angular velocity is set to 0. The velocity is always direction × (d / N) × 10, and the done event is due
at level time + N × 0.1.

| Case | MOVE | ROTATE |
|---|---|---|
| speed s = 0, d > 0 | N = +∞: velocity (0, 0, 0); the done event is due at +∞ and **never fires**; the entity does not move; the signal is not delivered (2a/2d) | N = 0: angular velocity 0; done event due now: **completes at once** without rotating, signal kind 1 delivered |
| s < 0, d > 0 (not in stock data) | N ≤ 0: done event due now, snaps the origin to D when it fires; the velocity set in the meantime is meaningless | as s = 0 |
| only a duration, duration 0, d > 0 | s = +∞, N = 1: arrives after one frame | N = 1: one frame |
| d = 0, s > 0 | N = 0: done event due now; velocity components are not-a-number (0/0) until it fires and sets velocity 0, origin D | N = 1: one frame, angular velocity 0 (corrects "N = 0 when d = 0" for ROTATE) |
| d = 0, only a duration (≠ 0) | as the row above (MOVE tests d) | s = 0/duration = 0: as s = 0 |

No finite duration gives s = 0 when d > 0, so "a duration giving speed 0" does not occur. OpenMerc's "one frame" for
s = 0 differs from the original in both MOVE and ROTATE.

A due-now done event fires in the same server frame unless the instruction ran during a signal wake-up caused by a
script later in the list order (§3.1). In that case it fires in the next frame, and one physics frame sees the
meaningless or not-a-number velocity (UNVERIFIED whether this is visible; implementations may use velocity 0).

### 6. §12.8 VEHICLE ids other than 1–5 (Correction of an ERRATA entry)

- ERRATA row (from SPEC-731 "Amendments" 1): "every VEHICLE id creates an action; ids other than 1-5 complete at their
  first execution, so the done event fires".
- Re-checked for this amendment against gamex86.dll: **this does not hold for VEHICLE commands from scripts.** The
  script command creates a vehicle action **only for ids 1–5**. For any other id no action is queued, and the done
  event waits for an action identifier that no action ever gets. So it **never fires** as long as the vehicle entity
  still has its AI; it fires at once only if the entity no longer has an AI. The signal entry stays pending (2d): a
  later completed VEHICLE command of the same vehicle consumes it too (2a).
- SPEC-731 Part B §3 row "other" correctly describes the vehicle's own command handling: an action carrying such an
  id would finish at its first execution. Scripts cannot create such an action.
- Result: SPEC-750 §12.8 stays as written, with "UNVERIFIED" removed: "VEHICLE ids 1–5 create an action; others none
  (the done event never fires)". SPEC-731 Amendments item 1 and its ERRATA row should be withdrawn. Stock scripts use
  only ids 2, 3 and 4, so no stock level is affected.

## Constants

| Item | Value |
|---|---|
| Frame count, MOVE | floor(10 × d / s) + 1 when d > 0, else 0 |
| Frame count, ROTATE | floor(10 × d / s) + 1 when s > 0, else 0 (angular velocity forced to 0) |
| VEHICLE ids that create an action | 1–5 |
| Renner Error #1 | never raised in 1.07f |

## Test vectors / how to verify

1. Different entities (item 2b): movers A and B (`func_wall`, movetype push). `move(A, (0,0,64), speed=64, signal=s)`,
   `move(B, (0,0,128), speed=64, signal=s)`, WAIT_SIGNALS(s) with clear (0x81). No game error. The script continues
   1.1 s later (A: N = 11). B arrives at 2.1 s (N = 21) and sets s = 1. A further `wait(1.5)` and WAIT_SIGNALS(s)
   continues at once.
2. Stock check (2b): in `kos3/garage.os` the second `use(gardoor1)` comes 5.2 s after the four MOVEs start, when
   `gartruck` (N = 52) and `garcrates` arrive. `paljack1` and `movbox1` (N = 57) are still moving for another 0.5 s
   (UNVERIFIED in game).
3. Same mover twice (2c, 2e): `move(A, (0,0,64), speed=64, signal=s)`, `wait(0.5)`, `move(A, (0,0,64), speed=64,
   signal=s)`, WAIT_SIGNALS(s) without clear (0x01). 1.1 s after the first MOVE, A stops and jumps to the second
   destination (about +93.1). s = 2 and the script continues then. The second done event at 1.6 s changes nothing
   (derived values, UNVERIFIED in game).
4. Re-declaration (item 4): a hand-made `.os` whose code jumps back over `param int p` (runner `parm1` = "5", no
   `parm2`). After the second execution p reads 0 and no `Missing Parameter` error occurs.
5. Zero speed (item 5): `move(e, (0,0,64), speed=0, signal=s)` + WAIT_SIGNALS(s) never continues and e stays put.
   `rotate(e, (0,90,0), speed=0, signal=s)` + WAIT_SIGNALS(s) continues in the same frame with angles unchanged.
   `move(e, (0,0,64), time=0, signal=s)` arrives after 0.1 s.
6. VEHICLE (item 6): `vehicle(tank, 6, signal=s)` + WAIT_SIGNALS(s) never continues while the tank has its AI.

## Open questions

1. Whether the one physics frame with a not-a-number or meaningless velocity (item 5, wake-up ordering case) has a
   visible effect.
2. When a vehicle or helicopter entity loses its AI (pending HELI/VEHICLE done events of that entity then fire at
   once).
