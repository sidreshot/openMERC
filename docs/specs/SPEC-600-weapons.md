# SPEC-600: Weapons — player-side weapon behaviour (fire modes, timing, ammunition use, reload, switching, zoom, view model)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: player.dll (and the in-view weapon model files it plays), version 1.07f
Depends on: SPEC-003 (player module interface: weapon ids, export/import tables, inventory methods), SPEC-001 (game
module interface), SPEC-010/SPEC-011 (Ghoul model objects, instances, sequence playback, note events), SPEC-201/
SPEC-202 (player-state weapon fields, user-command fire times), SPEC-400/SPEC-401 (client effects), SPEC-780 (player
client: weapon kick, view), SPEC-781 (pickups), SPEC-782 (server-side attacks: damage, range, projectiles, spread
formula). Companion specs: SPEC-601 (inventory and items), SPEC-602 (weapon prediction).

## Purpose

The player module decides *when* a weapon fires, how much ammunition each shot costs, how reloading, switching,
dropping, holstering and zooming behave, which first-person ("in-view") animation is shown and which first-person
sounds and effects are triggered. Shots themselves are executed by the game module (SPEC-782) through the server
import callbacks of SPEC-003. Almost all timing is driven by the animation data of the in-view models: a shot
happens when the playing sequence reaches a `fire` note, the next shot can start when the sequence ends. This spec
gives every rule and table needed to reproduce the observable behaviour of the twelve weapons and the two "hand"
pseudo-weapons. Numbers that depend on the model files are given as they are in the shipped 1.07f data.

## Interface / layout

### 1. Weapon table

Weapon ids are those of SPEC-003. "Clip" = rounds per magazine (0 = no magazine). "Weight" feeds the encumbrance rule
(SPEC-601). "Ammo" = ammunition type (SPEC-003 table) for primary / alternate fire. "Alt" = the weapon has an
alternate attack. "Muzzle" / "Converge" = the two booleans the game module asks before a shot (SPEC-782 Interface 1).
"Display name" is a string the module carries for each weapon (no reader found in 1.07f).

| Id | Cvar name | View model dir | Display name | Clip | Weight | Ammo p / a | Alt | Muzzle | Converge |
|---|---|---|---|---|---|---|---|---|---|
| 1 | knife | Knife | Knife | 0 | 0 | 1 / 1 | yes (throw) | no | yes |
| 2 | pistol2 | Pistol2 | Desert Eagle | 9 | 1 | 2 / — | no | no | yes |
| 3 | pistol1 | Pistol1 | Glock 9mm | 18 | 1 | 3 / — | no | no | yes |
| 4 | mpistol | MPistol | Ingram m11 | 32 | 2 | 3 / — | no | no | yes |
| 5 | assault | Assaultrifle | HK53 | 40 | 2 | 5 / — | no | no | yes |
| 6 | sniper | SniperRifle | Sig2000G | 6 | 2 | 5 / none (zoom) | yes (zoom) | no | yes |
| 7 | slugger | Autoshotgun | Jackhammer | 10 | 2 | 9 / 9 | yes | yes | yes |
| 8 | shotgun | Shotgun | Spas15 | 8 | 2 | 4 / — | no | no | yes |
| 9 | machinegun | Machinegun | Minimi | 40 | 2 | 5 / 5 | yes | no | yes |
| 10 | rocket | Rocket | M202A2 | 4 | 3 | 6 / 6 | yes | yes | yes |
| 11 | mpg | MPG | Microwave Pulse | 30 | 3 | 7 / 7 | yes | yes | no |
| 12 | flamegun | Flamegun | Flamethrower | 60 | 3 | 8 / 8 | yes | yes | no |
| 13 | — | Hurthand | Hurthand | 0 | 0 | — | no | no | yes |
| 14 | — | Throwhand | Throwhand | 0 | 0 | — | no | no | yes |

Attack ids, spread and kick (per fire mode; spread in degrees, used by the game's aiming rule SPEC-782 Behaviour 1;
"kick" = the view-kick pitch written on every shot, Behaviour 5):

| Id | Primary attack | Spread/shot | Max spread | Kick | Alternate attack | Alt spread/shot, max | Alt kick |
|---|---|---|---|---|---|---|---|
| 1 | 1 knife slash | 0 | 0 | 0 | 2 knife throw | 0, 0 | 0 |
| 2 | 4 | 0.2 | 0.8 | 5 | — | — | — |
| 3 | 3 | 0.3 | 1.5 | 1 | — | — | — |
| 4 | 5 | 0.6 | 3.5 | 0 | — | — | — |
| 5 | 6 | 0.4 | 1.8 | 0 | — | — | — |
| 6 | 7 | 0 | 0 | 2 | 8 (zoom; no server action) | 0, 0 | 0 |
| 7 | 9 slug | 1.5 | 1.5 | 4 | 10 concussion grenade | 0, 0 | 4 |
| 8 | 11 | 0 | 0 | 5 | — | — | — |
| 9 | 12 | 0.1 | 0.4 | 0 | 13 phosphorus grenade | 0, 0 | 0 |
| 10 | 14 rocket | 0 | 0 | 0 | 15 splitting rocket | 0, 0 | 0 |
| 11 | 16 beam | 0 | 0 | 0 | 17 burst | 0, 0 | 0 |
| 12 | 18 flame stream | 0 | 0 | 0 | 19 fireball | 0, 0 | 0 |

### 2. Weapon-description object (export entry 4 of SPEC-003)

The internal field offsets of this object were moved on 2026-10-26 to the private record
`docs/private/SPEC-600-descriptor-offsets.md` (not for publication). Publicly: the object carries, per weapon, the
clip size, weight, ammunition type per mode, spread per counted shot and maximum spread per mode, the view-model
directory name, view kick and attack id per mode, and two values with no known reader; their values are those of
table 1.

### 3. In-view model data used

Each weapon's in-view model is `Ghoul/Weapon/Inview/<dir>/<dir>.ghl` with its sequence list `<dir>.gsq` in the same
directory (SPEC-003). The animation timing comes from the compiled model file (`.ghb`). The part of that file the
weapon logic depends on is its sequence header, laid out as follows (little-endian; verified on all 14 shipped
in-view files):

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | 4 bytes | file magic `FE 37 82 19` |
| 0x04 | 20 bytes | other header data (not needed here) |
| 0x18 | uint32 | L = length of the source-path string that follows |
| 0x1C | L bytes | source path (NUL-padded) |
| 0x1C+L | uint32 | total frame count of all sequences |
| 0x20+L | uint32 | N = number of sequences |
| 0x24+L | 10 bytes | zero in all shipped files |
| 0x2E+L | — | N sequence records, back to back |

Sequence record:

| Field | Type | Meaning |
|---|---|---|
| frame time | float | milliseconds per frame |
| name length, name | uint32, bytes | sequence name, upper case, no NUL |
| note count | uint32 | |
| notes | repeated | per note: float time (ms from the sequence start), int32 (−1 in all shipped files), uint32 key length, key bytes, uint32 argument length, argument bytes |
| first frame | uint32 | index of the first frame in the file's frame data |
| frame count | uint32 | frames in this sequence |
| reserved | 12 bytes | zero |

Note keys used by weapons: `fire` (primary shot), `altfire` (alternate shot), `sound` (argument `name [n|*]`,
SPEC-003 client import 4), `effect` (argument `effect-name [bolt]`, client import 6), `shellbullets`
(make magazine rounds visible), `cleanblood` (clean the knife). The engine adds `EOS` (end of sequence, SPEC-011).

The sequence tables in Constants list the shipped data.

## Behaviour

### 1. Time and randomness

- "Now" is the level time (server context) or the client's game-time estimate (client context, SPEC-003 context
  flag). The server runs the weapon logic once per server frame (0.1 s); the client once per client frame.
- Every random choice made by the weapon logic uses one deterministic function of "now" so that the server and a
  predicting client make the same choice at the same time: with n = trunc(now × 10) (32-bit integer), x = n × n
  (32-bit, wrapping), r = ((((x ≫ 5) XOR x) ≫ 3 XOR x) ≫ 4) AND x (≫ = arithmetic right shift),
  `random(lo, hi)` = lo + (r mod (hi − lo + 1)) using a remainder with the sign of r. If hi ≤ lo the result is lo.
  The steps are: a = (x ≫ 5) XOR x; b = (a ≫ 3) XOR x; r = (b ≫ 4) AND x. "now × 10" is computed in single
  precision before truncation. Test vectors: now = 1.0 → n = 10, x = 100, a = 103, b = 104, r = 4 → random(0, 100)
  = 4; now = 12.34 → n = 123, x = 15129, r = 768 → random(0, 100) = 61.

### 2. Sequence playback rules

- Every sequence a weapon starts is played "once" (Ghoul playback mode 1, SPEC-011): its duration is
  (frames − 1) × frame time (at the instance's normal speed), and the end-of-sequence event fires once at that time.
  Exceptions, played in looping
  mode 0 (duration frames × frame time): the machinegun `land` sequence and the flamethrower `fire1_2` sequence.
- A sequence starts at "now" unless stated otherwise (SPEC-602: a server running for a predicting owner may start a
  fire sequence at the time the client reported).
- The name of the last started sequence is remembered (64 characters); several weapon rules test it.
- Each weapon think ends by delivering all note events of the in-view instance up to now + 0.001 s, notes before
  the end-of-sequence event (SPEC-011). A note lying exactly at a sequence's start is delivered in the same think
  (UNVERIFIED that the engine's boundary is inclusive; all instant-fire weapons depend on it, e.g. pistol2).
- If a requested sequence name does not exist in the model, nothing is played and the previous sequence keeps
  running (this happens, harmlessly, for sequences a weapon never really needs, Constants 3).

### 3. Weapon state

The current weapon has a state and a "ready" flag:

| State | Meaning |
|---|---|
| 0 | idle (also used while the draw animation plays; then "ready" is false) |
| 1 | reloading |
| 2 | firing primary |
| 3 | firing alternate |
| 4 | putting the weapon away for a switch |
| 5 | drawing after a drop/lose (hand pseudo-weapons) |

"Ready" is true while an idle sequence plays. While it is false the weapon accepts no new fire or switch input,
except a client that is applying a switch dictated by the server (SPEC-602).

Per think, in this order:
1. Switch completion (Behaviour 8): if a put-away has finished, select the new weapon and start its draw.
2. Holster handling (Behaviour 9).
3. If the owner is in a cinematic freeze (owner-context flag, SPEC-003), all four button flags are treated as
   released for this think.
4. The weapon's own per-think rule (Behaviour 10: release handling of automatic weapons, stances, zoom).
5. During a cinematic freeze nothing more happens. Otherwise, only when "ready": a pending request (reload, select,
   next, previous, best, drop, lose — SPEC-601) is processed; if it did something, stop.
6. If no in-view model instance exists, stop. Otherwise start a primary attack if the attack button is held
   (server for a predicting owner: if a primary fire time was reported, SPEC-602); if that did not start, start an
   alternate attack if the alternate button is held (or an alternate fire time was reported).
7. Deliver due note and end-of-sequence events (Behaviour 2).
8. Server: write the current clip and reserve counts into the owner's player state (not during a predicted switch,
   SPEC-602).
9. Items think (SPEC-601).

### 4. Starting an attack

- Primary: the weapon's primary sequence for its current stance (Behaviour 10) is chosen; one shot's ammunition is
  taken (Behaviour 6). If that fails the attack does not start (an automatic reload or a dry-fire has been started
  instead, Behaviour 6). Otherwise ready := false, state := 2 and the sequence starts.
- Alternate: only weapons with an alternate attack; same with the alternate sequence and alternate ammunition,
  state := 3.
- In client context the module also sets its "primary/alternate fired this frame" export (SPEC-003 entries 5/6),
  even when the ammunition check failed (SPEC-602).

### 5. Shots (fire notes)

- When a `fire` note is delivered while the state is 2 and the weapon's shot gate (Behaviour 10) allows it, one
  primary shot happens: server context → server import 2 (the game fires the primary attack, SPEC-782); client
  context → client import 2 (predicted muzzle flash and effect sounds). Then the owner's weapon-kick pitch (the
  owner-context pointer, SPEC-003 offset 0x28) is set to −kick of the primary mode (table 1). The game decays it
  (SPEC-780).
- `altfire` notes do the same with state 3, import 3 and the alternate kick.
- A `fire` note with argument `1` additionally takes one shot's ammunition (flamethrower fuel); if none is left the
  firing ends as if the sequence had ended (Behaviour 7).
- `effect` and `sound` notes are played only in client context (imports 6 and 4). In server context they do nothing.
- The **spread counter** (read by the game, SPEC-782) counts shots of the current burst: it is incremented by every
  refire at a sequence end (Behaviour 7) and by every additional paid shot inside an automatic sequence
  (Behaviour 10); it is reset to 0 when a fire sequence ends without refiring, when a reload starts, when the knife
  finishes its wind-up (`fire1_2a`), and when the weapon is cleared. It is **not** reset when an automatic weapon
  stops because the button was released (Behaviour 10), so spread carries over to the next burst until a reload
  or a non-refiring sequence end (UNVERIFIED in game; derived from the rules).

### 6. Ammunition per shot

"Ammunition is used" (player variable A of SPEC-601) is true unless infinite ammunition is on. When it is false no
check or decrement below happens and every shot is allowed.

| Weapon | Primary cost | Alternate cost / requirement |
|---|---|---|
| knife | none | 1 knife; refused unless at least 2 knives are carried (the last knife is never thrown) |
| pistol2, pistol1, mpistol, assault, sniper, shotgun, machinegun, rocket, mpg | 1 round from the clip | sniper: none (zoom); machinegun, mpg: 10 rounds from the clip, refused below 10; rocket: 1 |
| slugger | 1 | 2 slugs from the clip, refused below 2 |
| flamegun | 1 fuel from the clip | 6 fuel, refused below 6 |

When the cost cannot be paid:
- clip empty (or below the alternate requirement) and auto-reload on (player variable D, default on): a reload
  starts (Behaviour 11); otherwise the weapon's dry-fire sequence plays (ready := false, state 0; the sequence's
  `sound` note gives the click).
- flamegun: when out of fuel the owner's looping sound is reset (Behaviour 10) and, if auto-reload is on, the
  command `weaponbestsafe` is executed as forced (switch to the best non-explosive weapon); otherwise dry-fire. For
  the alternate attack with less than 6 fuel: when auto-reload is on and the clip is completely empty, the same
  forced `weaponbestsafe` (server only); then dry-fire.

### 7. End of a sequence

When the end-of-sequence event of the current sequence arrives:
1. The weapon's own end rule (Behaviour 10) may take over (for example chain to the next sequence); then stop.
2. Automatic reload at the end: if the state is 2 or 3, ammunition is used, the clip is empty and auto-reload is on
   (all weapons except knife and flamegun), a reload starts; stop.
3. By state:
   - 0, 1, 5 → idle (ready := true, state 0, an idle sequence is chosen, Behaviour 10).
   - 2 → if no request is pending and the attack button is still held (server for a predicting owner: if a primary
     fire time is pending, except for flamegun, knife and mpg which use the button), **refire**: spread counter + 1,
     take one shot's ammunition, start the primary sequence again (for the predicting-owner case see SPEC-602), then
     the weapon's "after refire" rule (Behaviour 10). If the ammunition could not be taken, go idle. If not
     refiring: spread counter := 0, go idle.
   - 3 → the same with the alternate button / alternate fire time and the "after alternate refire" rule.
   - 4 → the put-away has finished; the switch completes on the next think (Behaviour 8).

Consequently a held button repeats any weapon at the rate given by its fire sequence (Constants 2).

### 8. Switching weapons

Requests come from the commands of SPEC-601 (`weaponselect`, `weapnext`, `weapprev`, `weaponbestsafe`,
`weaponbestunsafe`, `weapondrop`, `weaponlose`) and from the game. A request is resolved into a target slot
(SPEC-601 gives the slot order and the selection rules). If the target is the current weapon nothing happens.

- No current weapon (id 0): the target is selected at once and its draw sequence starts (no put-away).
- Drop / lose request: if the current weapon is in the kept-weapon mask (SPEC-601) the request is ignored.
  Otherwise the current weapon's deselect rule runs; server: the game is asked to spawn a pickup of that weapon with
  the rounds of its clip (server import 5); the weapon is removed from the inventory; the hand pseudo-weapon is
  selected at once — slot 12 (hurt hand, id 13) for `weaponlose`, slot 13 (throw hand, id 14) for `weapondrop` —
  its sequence (`hurthand` / `throwhand`) starts with ready := false and state 5, and the current weapon id is set to
  0 while it plays. At its end the hand requests, as forced, `weapnext` if the weapon tag (SPEC-601) is empty, else
  the best weapon; commands are swallowed until then; the new weapon is drawn immediately (no put-away).
- Any other switch: the current weapon's deselect rule runs (sniper: zoom off; flamegun: looping sound off), then
  its put-away sequence (`done…`, by stance) starts with ready := false and state 4; at its end the target is
  selected and its draw sequence (`ready…`) starts with ready := false. A weapon without a put-away sequence is
  switched at once.
- After selection the module writes the new weapon id, clip rounds and reserve ammunition into the owner's player
  state (SPEC-602) and applies the weapon's "visible rounds" rule (assault rifle, machinegun, Behaviour 10). If the
  weapon is not holstered its idle sequence is chosen first.
- Typical switch time = put-away duration + draw duration of the new weapon (Constants 2), rounded up to whole
  server frames on the server.

### 9. Holstering

The weapon can be put away without a target ("holstered"; SPEC-003 inventory methods 35/36, replicated as event 5).
Toggling it on runs the deselect rule and plays the put-away sequence (state 4); toggling it off only clears the
flag. While holstered: if neither attack button is held, selection requests (codes 2–19 of SPEC-601, except drop
and lose) only change the target slot; pressing either attack button clears the flag. The weapon comes back with its
draw sequence on the next think (the switch completes as in Behaviour 8). Using any item other than the medkit while
holstered unholsters first (SPEC-601). The game uses the holstered flag (SPEC-780: no-target).

### 10. Weapon-specific rules

Common idle rule: when an idle sequence must be chosen, r = random(0, 100) (slugger: random(0, 130)); an "idle
variation" counter (start 0) blocks variations: when it is non-zero the plain idle is chosen and the counter
decreases by one (not below 0); choosing a variation (`idlefinger…`, `idleclean`, a stance transition) sets it to 2.
"Thresholds" below apply only when the counter is 0.

**Knife (1).**
- Primary sequence: r = random(0, 100); r ≥ 75 → `fire1_1a` (thrust, hit at 100 ms), else `fire1_2a` (wind-up, no
  hit). End rules: `fire1_1a` → `fire1_1b`; `fire1_2a` → spread counter 0, then r < 33 → `fire1_2sr_to_sl`,
  r < 67 → `fire1_2dr_to_ul`, else `fire1_2ur_to_dl`; `fire2_1` → the knife becomes clean (skin `knife` on material
  `knife`) and `fire2_2` plays; `fire2_2` → if ammunition is used and fewer than 2 knives remain, idle;
  `fire1_2l_to_idle` → `fire1_2a` (never reached in 1.07f).
- After refire (button held at the end of a swing): a swing is chosen by the parity of the spread counter: odd →
  r < 33 `fire1_2sl_to_sr`, r < 67 `fire1_2dl_to_ur`, else `fire1_2ul_to_dr`; even → r < 33 `fire1_2sr_to_sl`,
  r ≤ 66 `fire1_2dr_to_ul`, else `fire1_2ur_to_dl`. So a held button alternates slashes, one per 0.2 s.
- Alternate: `fire2_1` (throw at 300 ms, attack 2), then `fire2_2` (new knife drawn). After an alternate refire:
  repeats `fire2_1` if the last throw check succeeded, else idle.
- Blood: the game reports a hit on a creature (inventory method 34, SPEC-601) → if not already bloody: skin
  `knifeblood` on material `knife`, event 4 to the client. `cleanblood` note or a throw cleans it.
- Idle: thresholds r < 20 → `idlefinger_1`; r < 40 → `idlefinger_2`; 40 ≤ r < 60 and bloody → `idleclean`; else
  `idle`. While ready: +weaponExtra1 → `idlefinger_1`, +weaponExtra2 → `idleclean` (each unless already playing it
  or `idleclean`).
- Never reloads; selectable with no knives; choosing the put-away `done` clears the blood flag (the next draw
  creates a clean instance).

**Pistol1 — 9 mm Glock (3).** Two stances: a (one hand, the stance after drawing) and b (two hands), plus a pose c
that is entered from stance b. Sequences by stance: fire `fire1_a` (a) / `fire1_b` (b and c); reload `reload_a` /
`reload_b`; put-away `done_a` / `done_b`; dry-fire `dryfire_1hand` / `dryfire_2hands`; draw `ready_a` (sets stance
a, clears c). Firing and dry-fire clear c. A new inventory starts in stance b until the first draw.
- While ready and not in a transition: +weaponExtra1 in stance a (or c) → `idle_a_to_idle_b` (stance b);
  +weaponExtra2 in stance b → `idle_b_to_idle_a` (stance a).
- Idle choice: leaning in the positive direction (lean value > 0, SPEC-202) in stance a or c → `idle_a_to_idle_b`
  (b); leaning negative in stance b → `idle_b_to_idle_a` (a). Otherwise: in c: r < 30 (no transition just played,
  not leaning) → `idle_c_to_idle_b` (b); r > 89 → `idlefinger_c`; else `idle_c`. In a: r < 20 (same conditions) →
  `idle_a_to_idle_b` (b); r > 79 → `idlefinger_a`; else `idle_a`. In b: r < 10 (not leaning negative) →
  `idle_b_to_idle_c` (pose c); 10 ≤ r < 30 (not leaning negative) → `idle_b_to_idle_a` (a); r > 94 → `idlefinger_b`;
  else `idle_b`.
- Fire mode: semi-automatic with hold-to-repeat; the pistol carries code for a burst mode (`fire2_b`) that its model
  does not contain and that cannot be triggered (no alternate attack).

**Pistol2 — Desert Eagle (2).** Stances a and c (sideways), with sub-pose d of stance a. Fire `fire1_a` / `fire1_c`;
reload `reload_a` / `reload_c`; put-away `done_a` / `done_c`; dry-fire `dryfire_rhand` (a) / `dryfire_lhand` (c);
draw `ready_a` (stance a). Firing, reloading and dry-fire clear d.
- While ready and not in a transition: +weaponExtra1 in a → `idle_a_to_idle_c`; +weaponExtra2 in c →
  `idle_c_to_idle_a`.
- Idle choice: leaning positive in a → `idle_a_to_idle_c`; leaning negative (not positive) in c → `idle_c_to_idle_a`.
  In c: r < 80 → `idle_c`, else `idlefinger_c`. In d: r < 10 → `idle_d_to_idle_a`, else `idle_d`. In a: r < 10 →
  `idle_a_to_idle_d`; r < 90 → `idle_a`; else `idlefinger_a`.

**MPistol — Ingram (4).** Fire `fire2_1`: shots at 0, 100, …, 700 ms (8 notes), repeated while held → 10 shots/s.
Shot gate: at most one shot per distinct "now"; the first shot of a refired sequence is already paid (the refire
took the round), every other shot takes one round (spread counter + 1). The first shot after pressing the button
also takes a round although starting the attack already took one (2 rounds for the first shot; UNVERIFIED in game,
derived). Releasing the button while firing goes idle at once. End rule: `fire2_1` ended with the button released →
`fire2_2` (one more shot at 50 ms) — not reachable in practice because of the release rule. Idle: r < 20 →
`idlefinger_a`, else `idle`.

**Assault rifle — HK53 (5).** Stances a and b (rifle tilted). Fire `fire1_a` / `fire1_b`: 8 shots at 0…700 ms,
repeated while held → 10 shots/s; the first shot of every fire sequence is pre-paid (no double charge); every other
shot takes one round (spread counter + 1); one shot per distinct "now". Releasing the button goes idle at once.
Reload `reload_a` / `reload_b`; put-away `done_a` / `done_b`; dry-fire `dryfire_up` (a) / `dryfire_side` (b).
- While ready and not in a transition: +weaponExtra1 in a → `idle_a_to_idle_b`; +weaponExtra2 in b →
  `idle_b_to_idle_a`.
- Idle: leaning negative in b → `idle_b_to_idle_a`; leaning positive (not negative) in a → `idle_a_to_idle_b`. In b:
  r < 30 (no transition just played) → `idle_b_to_idle_a`; r < 50 → `idlefinger_b`; else `idle_b`. In a: r < 10 (no
  transition just played) → `idle_a_to_idle_b`; r < 20 → `idlefinger_a`; else `idle_a`.
- Visible rounds: the magazine model has parts named `4` … `16`. When the weapon is selected with c rounds in the
  clip and 3 ≤ c ≤ 15, parts `c+1` … `16` are hidden. After every paid shot, with c the new count in that range, the
  server hides part `c+1` only and the client hides `c+1` … `16`. At the reload's `shellbullets` note parts `4` …
  `min(16, clip)` are shown (`4` … `16` when ammunition is not used).

**Sniper rifle — Sig2000G (6).** Fire `fire1` (shot at 50 ms); semi-automatic with hold-to-repeat. Alternate =
scope (no ammunition):
- Alternate pressed while not scoped: `scopein`; at its end the field of view becomes 60, the scope is on, "zoom
  while held" is on, the in-view model's meshes are hidden, idle follows. Alternate pressed while scoped and
  "zoom while held" is off: `scopeout`, field of view 95, model shown, saved zoom cleared, scope off. (Alternate
  pressed while scoped with "zoom while held" still on: nothing.)
- Every think while scoped and no remote view is active: +weaponExtra1 → zoom in; +weaponExtra2 → zoom out (both
  → nothing); with "zoom while held" on and the alternate button held → zoom in; when the alternate button is
  released "zoom while held" goes off. Zooming changes the field of view by 5 × (frame length × 10) degrees per
  think — 5° per 0.1 s — clamped to [6, 60]; the result (truncated) is remembered as the saved zoom.
- A remote view (camera) while the field of view is below 95 turns the scope off (field of view 95, model shown).
- Reload `reload` (allowed only when reloading is enabled, else idle): if scoped, the scope is turned off first
  (field of view 95, model shown) but the saved zoom is kept; at the end of `reload` the saved zoom (if any) is
  restored, the scope and "zoom while held" are on again and the model hidden. Draw `ready`. Put-away `done_a`
  (`done_b` only before the first draw or reload). Deselect: field of view 95, scope, "zoom while held" and saved
  zoom cleared.
- The scope flag is visible to the game as inventory method 33 (SPEC-601; the HUD scope overlay).
- Idle: r < 20 → `idlefinger` (only after a draw or reload, which is always the case in practice), else `idle_a`.
- The field of view written is the owner's player-state field of view (SPEC-202 field 24).

**Slugger — Jackhammer (7).** Fire `fire1` (shot at 100 ms) → 5 shots/s while held; alternate `fire2`
(concussion grenade at 75 ms, 2 slugs). Draw `ready`, then the first idle is always `ready_idle`; idle: r ≤ 9 →
`idlefinger_a`; r ≤ 24 → `idlefinger_b`; else `idle` (r from random(0, 130)). Reload `reload`; put-away `done`.

**Shotgun — Spas15 (8).** Fire `fire1` (shot at 0 ms) → one shot per 0.6 s while held. Reloads one shell at a time:
`reload_1`, then `reload_loop` once per shell loaded (each end loads one shell if the clip is not full and shells
remain), then `reload_2`. While ready: +weaponExtra1 → `idlefinger_b`, +weaponExtra2 → `idlefinger_c` (unless one of
the two is playing). Idle: r < 10 → `idlefinger_a`, else `idle`.

**Machinegun — Minimi (9).** Fire `fire1_1` (notes at 0 and 200 ms) repeated while held. Shot gate: a note fires only
if at least 0.195 s passed since the last shot; the first note of a fire sequence is pre-paid, the others take a
round (spread counter + 1). With the shipped timing this gives shots 0.2 s apart for the first two, then one shot
per 0.3 s sequence (the note at the start of each repeated sequence is suppressed) — about 200 rounds/min
(UNVERIFIED in game). When `fire1_1` ends with the button released → `fire1_2` (spin-down), then idle. Alternate
`fire2` (phosphorus grenade at 133 ms, 10 rounds). Landing from a fall (game, inventory method 63) while ready plays
`land` (looping mode; ends after 0.8 s). Visible belt: parts `BULLET1` … `BULLET8`; on selection with c rounds,
0 ≤ c ≤ 7, parts `BULLET<c+1>` … `BULLET8` are hidden; after every paid shot with c in that range the server hides
`BULLET<c+1>` only and the client hides `BULLET<c+1>` … `BULLET8`; the reload's `shellbullets` note shows `BULLET1` …
`BULLET<min(8, clip)>` (all 8 with unlimited ammunition). Idle: r < 20 → `idlefinger_a`; r < 40 → `idlefinger_b`;
else `idle`.

**Rocket launcher — M202A2 (10).** Fire `fire1` (one rocket at 0 ms). Alternate `fire2`: four `altfire` notes (0,
133, 267, 400 ms), each a splitting rocket; the first is pre-paid, each other takes one rocket (one per distinct
"now"); fewer rockets → fewer shots. Idle: r < 10 → `idlefinger_a`; r < 20 → `idlefinger_b`; else `idle`.

**MPG — microwave pulse gun (11).** Primary = beam: `fire2` (beam notes at 50 and 150 ms), then while held
`fire2_loop` (beam notes every 100 ms) repeated — every beam note is one beam attack; each started sequence costs
one battery unit (≈ 2 units/s). Releasing the button goes idle and plays `fire2_end`. Alternate = burst: `fire1`
(burst at 50 ms, 10 units), then `charge` (0.95 s); at the end of `charge` the burst repeats while the alternate
button is held (one burst per 1.4 s), else idle. Idle: r < 15 → `idlefinger_a`; r < 30 → `idlefinger_b`;
else `idle`.

**Flamethrower (12).** Primary: starting sets the owner's looping sound to `weapons/flamegun/fire.wav` (sound byte
248, server only) and plays `fire1_1`; while held `fire1_idle` repeats: a flame attack at every `fire` note (every
100 ms; the notes at the end and start of consecutive cycles coincide); fuel: 1 at the start, 1 per started
sequence and 1 at each `fire 1` note (400, 800, 1300 ms) — ≈ 2.35 units/s; 60 fuel ≈ 25 s. Releasing the button
goes idle, plays `fire1_2` (looping mode) and resets the looping sound. Alternate: `fire2_1` (build-up effect), at
its end `fire2_2` (fireball at 0 ms, 6 fuel was taken at the start), at its end idle. Looping-sound reset (idle
choice, put-away, out of fuel, release): `player/flaglp.wav` with sound byte 248 when the owner carries the CTF flag
(server import 12), otherwise no sound. Never reloads (fuel goes straight into the clip, SPEC-601). Idle: r < 10 →
`idlefinger_a`; r < 20 → `idlefinger_b`; r < 30 → `idlefinger_c`; else `idle`. Owners whose entity carries server
flag bit 0x200 (believed: bots; UNVERIFIED): whenever any in-view sequence ends while the flamethrower is the current
weapon and the attack button is held, one extra attack is fired directly (the alternate one if the alternate button
is also held).

**Hurt hand (13), throw hand (14).** Only used by drop/lose (Behaviour 8). Draw sequence `hurthand` / `throwhand`;
no idle, no fire.

### 11. Reloading

- Requested by the `reload` command (only when reloading is enabled, player variable C), automatically when a shot
  cannot be paid (auto-reload on), and at the end of a fire sequence with an empty clip.
- Nothing happens when the clip is full (unless player variable E "discard partial clips" is on) or the weapon has
  no reload sequence.
- The spread counter resets. The rounds are moved at once when the reload starts (shotgun excepted):
  - normal: from reserve into the clip until it is full or the reserve is empty;
  - with E on: a full clip is taken from the reserve if available (the rounds left in the old clip are lost);
    otherwise the clip becomes whatever reserve remains;
  - shotgun (when reloading is enabled and E is off): nothing at the start; one shell per `reload_loop` end.
- If there was nothing to load: the dry-fire sequence (or idle) plays; if the clip is empty, a forced
  `weaponbestsafe` is executed, and if that finds another weapon the reload attempt ends there.
- If rounds were loaded and reloading is enabled: server import 4 (reload started — the game's third-person reload
  animation), state 1, ready := false, the reload sequence plays (Constants 2 gives the durations). If reloading is
  disabled (`dmflags` 64), the rounds are loaded instantly with no animation.
- Every reload attempt on the server increments the owner's weapon-state sequence byte in the player state (SPEC-202
  field 4; SPEC-602) unless the forced best-weapon switch above found a weapon.
- The reload ends with the sequence's end (→ idle).

### 12. In-view model construction

- Model object: created once per weapon and cached (SPEC-003 exports 7/8): Ghoul model from the sequence list
  `Weapon/Inview/<dir>` (SPEC-003 common import 17), registration finished with all skins, specular lighting on when
  `ghl_specular` > 0; its `sound` and `effect` notes are enumerated to precache `<name>.wav` (or `<name>1.wav` …
  `<name>n.wav` when the argument has a count n) and the effect files (server imports 10 and 9). Then the weapon's
  attack resources are precached (server import 8 with both attack ids) and, per weapon, its first-person muzzle
  effect: `weapons/playermz/pistol2`, `…/pistol1`, `…/machinepistol`, `…/assaultrifle`, `…/sniper`,
  `…/autoshotgun`, `…/shotgun`, `…/machinegun`, `…/rocket` and `…/rocket_alt`, `…/mpg` (knife and flamethrower:
  none).
- Instance: on selection a new instance replaces the previous one; parts `_R_FOREARM` and `_L_FOREARM` are hidden,
  `_R_SLEEVE_FOREARM` and `_L_SLEEVE_FOREARM` shown, material `arms` uses skin `arms_sleeve`; the instance's user
  value is the inventory; note callbacks for `sound`, `fire`, `altfire`, `EOS`, `cleanblood`, `shellbullets`,
  `effect` are registered; the server stores the instance in the owner's player state (SPEC-602).

## Constants

### 1. Ammunition-related limits used by the weapon logic

| Value | Number |
|---|---|
| knives needed to throw | 2 |
| machinegun / MPG alternate cost | 10 |
| slugger alternate cost | 2 |
| flamethrower alternate cost | 6 |
| machinegun minimum shot interval | 0.195 s |
| sniper field of view: normal / scope-in / limits / step | 95 / 60 / 6…60 / 5° per 0.1 s |
| shotgun clip | 8 |

### 2. Derived timing (shipped data, play-once durations; server values rounded up to 0.1 s frames)

| Weapon | Draw | Put-away | Primary cycle | Shots per primary cycle | Primary rate (held) | Alternate | Reload |
|---|---|---|---|---|---|---|---|
| knife | 0.4 | 0.3 | 0.2 (swing) | 1 | 5 /s (after the first wind-up) | 0.35 + 0.4 | — |
| pistol2 | 0.45 | 0.5 | 0.4 | 1 | 2.5 /s | — | 1.2 (`reload_c` 1.07) |
| pistol1 | 0.85 | 0.3 | 0.3 | 1 | 3.3 /s | — | 0.87 |
| mpistol | 1.1 | 0.3 | 0.75 (0.8) | 8 | 10 /s | — | 1.5 |
| assault | 0.4 | 0.5 | 0.75 (0.8) | 8 | 10 /s | — | 1.67 |
| sniper | 1.4 | 0.5 | 0.3 | 1 | 3.3 /s | scope-in 0.5 / out 0.4 | 1.6 |
| slugger | 0.47 | 0.3 | 0.2 | 1 | 5 /s | 0.86 (0.9) | 1.85 |
| shotgun | 1.2 | 0.5 | 0.55 (0.6) | 1 | 1.7 /s | — | 0.2 + 0.2 per shell + 0.45 |
| machinegun | 0.9 | 0.6 | 0.3 | 2 (first), then 1 | ≈ 3.3 /s | 0.8 | 2.25 |
| rocket | 0.9 | 0.6 | 0.7 | 1 | 1.4 /s | 0.87 (4 rockets) | 1.67 |
| mpg | 1.6 | 0.67 | 0.2, then 0.45 (0.5) | 2, then 5 | 10 beams/s | 0.45 + charge 0.95 | 1.33 |
| flamegun | 0.73 | 0.53 | 0.2, then 1.7 | 0, then 18 | 10 flames/s | 0.2 + 0.4 | — |

All values in seconds. Dry-fire takes 0.4 s for every weapon. The derived rates assume a non-predicting owner on a
10 Hz server; they are UNVERIFIED in game (measure by counting shots/rounds over 10 s of held fire).

### 3. Sequence data (shipped 1.07f in-view files)

Columns: frames, frame time (ms), play-once duration (ms) = (frames − 1) × frame time, notes (time in ms: key
[argument]). Sequences listed in a model but never started by the 1.07f logic are marked "unused". Sequences the
logic asks for but the model lacks: knife none; flamethrower `reload_a` (never needed).

**Knife** (`Ghoul/Weapon/Inview/knife/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 4 | 100 | 300 | 200 sound `weapons/knife/done` |
| fire1_1a | 6 | 50 | 250 | 100 fire, sound `weapons/knife/swing 3` |
| fire1_1b | 5 | 50 | 200 | — |
| fire1_1_to_idle | 6 | 50 | 250 | — (unused) |
| fire1_2a | 6 | 50 | 250 | — |
| fire1_2dl_to_ur | 5 | 50 | 200 | 100 fire, sound `weapons/knife/swing 3` |
| fire1_2dr_to_ul | 5 | 50 | 200 | 50 fire, sound `weapons/knife/swing 3` |
| fire1_2l_to_idle, fire1_2r_to_idle | 4 | 50 | 150 | — (unused) |
| fire1_2sl_to_sr, fire1_2sr_to_sl, fire1_2ur_to_dl | 5 | 50 | 200 | 100 fire, sound `weapons/knife/swing 3` |
| fire1_2ul_to_dr | 5 | 50 | 200 | 50 fire, sound `weapons/knife/swing3` |
| fire2_1 | 8 | 50 | 350 | 300 altfire |
| fire2_2 | 9 | 50 | 400 | — |
| idle | 23 | 100 | 2200 | — |
| idlefinger_1 | 23 | 50 | 1100 | 400 sound `weapons/knife/twirl` |
| idlefinger_2 | 19 | 100 | 1800 | — |
| ready | 5 | 100 | 400 | 100 sound `weapons/knife/ready` |
| idleclean | 29 | 100 | 2800 | 1000 cleanblood |

**Pistol1** (`Ghoul/Weapon/Inview/pistol1/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| fire1_a | 7 | 50 | 300 | 50 fire, effect `weapons/playermz/pistol1 flash`, effect `weapons/playermz/shell_s shell` |
| fire1_b | 7 | 50 | 300 | 0 the same three notes |
| reload_a | 14 | 66.7 | 866.7 | 0 sound `weapons/gpistol/reloadout`; 400 sound `weapons/gpistol/reloadin`; 733.3 sound `weapons/gpistol/reloadfin` |
| reload_b | 14 | 66.7 | 866.7 | 66.7 reloadout; 400 reloadin; 666.7 reloadfin (same sound names) |
| done_a, done_b | 4 | 100 | 300 | 200 sound `weapons/gpistol/done` |
| ready_a | 18 | 50 | 850 | 250 sound `weapons/gpistol/cock` |
| idlefinger_a / _b / _c | 19 / 19 / 14 | 100 | 1800 / 1800 / 1300 | _b: 500 sound `weapons/gpistol/knuckle` |
| idle_a, idle_b, idle_c | 23 | 100 | 2200 | — |
| idle_a_to_idle_b, idle_b_to_idle_a | 4 | 100 | 300 | — |
| idle_c_to_idle_b, idle_b_to_idle_c | 6 | 50 | 250 | — |
| dryfire_1hand, dryfire_2hands | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Pistol2** (`Ghoul/Weapon/Inview/pistol2/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done_a / done_c | 11 | 50 | 500 | 100 sound `/weapons/dpistol/spindone` / `/weapons/dpistol/throw` |
| fire1_a, fire1_c | 9 | 50 | 400 | 0 fire, effect `weapons/playermz/pistol2 flash`, effect `weapons/playermz/shell_l shell` |
| idlefinger_a | 18 | 50 | 850 | 100 sound `/weapons/dpistol/spin` |
| idlefinger_c | 22 | 66.7 | 1400 | — |
| idle_a, idle_c | 23 | 100 | 2200 | — |
| idle_a_to_idle_c, idle_c_to_idle_a | 11 | 66.7 | 666.7 | 200 sound `/weapons/dpistol/throw` |
| idle_a_to_idle_d | 6 | 66.7 | 333.3 | — |
| idle_d | 23 | 50 | 1100 | — |
| idle_d_to_idle_a | 4 | 50 | 150 | — |
| ready_a | 10 | 50 | 450 | 50 sound `/weapons/dpistol/spinready` |
| reload_a | 19 | 66.7 | 1200 | 200 sound `/weapons/dpistol/pullout`; 533.3 `/weapons/dpistol/putin`; 866.7 `/weapons/dpistol/cock` |
| reload_c | 17 | 66.7 | 1066.7 | 0 pullout; 333.3 putin; 666.7 cock |
| dryfire_lhand, dryfire_rhand | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**MPistol** (`Ghoul/Weapon/Inview/mpistol/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 4 | 100 | 300 | 200 sound `/weapons/gpistol/done` |
| fire2_1 | 16 | 50 | 750 | every 100 ms from 0 to 700: fire, effect `weapons/playermz/machinepistol flash`, effect `weapons/playermz/shell_s shell` |
| fire2_2 | 4 | 50 | 150 | 50 fire |
| idle | 23 | 100 | 2200 | — |
| idlefinger_a | 15 | 100 | 1400 | — |
| ready | 23 | 50 | 1100 | 50 sound `\weapons\ipistol\throw`; 400 `\weapons\ipistol\catch`; 700 `\weapons\ipistol\cock` |
| reload | 16 | 100 | 1500 | 0 `/weapons/gpistol/reloadout`; 500 `/weapons/gpistol/reloadin`; 1000 `/weapons/gpistol/cock` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Assaultrifle** (`Ghoul/Weapon/Inview/assaultrifle/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done_a, done_b | 11 | 50 | 500 | 200 sound `weapons/dpistol/spindone` |
| fire1_a, fire1_b | 16 | 50 | 750 | every 100 ms from 0 to 700: fire, effect `weapons/playermz/assaultrifle flash`, effect `weapons/playermz/shell shell` |
| idlefinger_a | 19 | 100 | 1800 | — |
| idlefinger_b | 15 | 66.7 | 933.3 | 200 sound `weapons/dpistol/adjust` |
| idlefinger_c | 11 | 66.7 | 666.7 | 533.3 sound `weapons/dpistol/spinready` (unused) |
| idle_a, idle_b | 23 | 100 | 2200 | — |
| idle_a_to_idle_b, idle_b_to_idle_a | 3 | 100 | 200 | — |
| ready | 9 | 50 | 400 | 100 `weapons/dpistol/spindone`; 300 `weapons/ipistol/catch` |
| reload_a, reload_b | 26 | 66.7 | 1666.7 | 200 `/weapons/gpistol/reloadout`; 400 shellbullets; 733.3 `/weapons/gpistol/reloadin`; 1133.3 `/weapons/ipistol/cock` |
| dryfire_up, dryfire_side | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**SniperRifle** (`Ghoul/Weapon/Inview/sniperrifle/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done_a, done_b | 6 | 100 | 500 | 300 sound `/weapons/sniper/done` |
| fire1 | 7 | 50 | 300 | 50 fire, effect `weapons/playermz/sniper flash`, effect `weapons/playermz/shell shell` |
| idlefinger | 16 | 66.7 | 1000 | — |
| idle_a | 23 | 100 | 2200 | — |
| idle_b, idle_a_to_idle_b (4 frames), idle_b_to_idle_a (3 frames) | | | | unused |
| ready | 29 | 50 | 1400 | 200 `/weapons/assault/slap`; 600 `/weapons/sniper/cock` |
| reload | 33 | 50 | 1600 | 300 `/weapons/sniper/reload`; 850 `/weapons/sniper/cock`; 1400 `/weapons/sniper/cock2` |
| scopein | 6 | 100 | 500 | — |
| scopeout | 5 | 100 | 400 | — |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Autoshotgun** (slugger, `Ghoul/Weapon/Inview/autoshotgun/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 4 | 100 | 300 | 100 sound `weapons/autoshot/done` |
| fire1 | 5 | 50 | 200 | 100 fire, effect `weapons/playermz/autoshot flash` |
| fire2 | 24 | 37.5 | 862.5 | 75 altfire, effect `weapons/playermz/autoalt flash` |
| idle | 23 | 100 | 2200 | — |
| idlefinger_a | 60 | 50 | 2950 | 650 sound `weapons/autoshot/spin` |
| idlefinger_b | 15 | 66.7 | 933.3 | — |
| ready | 8 | 66.7 | 466.7 | 66.7 sound `weapons/autoshot/ready` |
| ready_idle | 24 | 66.7 | 1533.3 | — |
| reload | 38 | 50 | 1850 | 700 `weapons/autoshot/pullout`; 1200 `weapons/autoshot/putin` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Shotgun** (`Ghoul/Weapon/Inview/shotgun/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 11 | 50 | 500 | 300 sound `/weapons/shotgun/done` |
| fire1 | 12 | 50 | 550 | 0 fire, effect `weapons/playermz/shotgun flash`, effect `weapons/playermz/shellbig shell` |
| idle | 23 | 100 | 2200 | — |
| idlefinger_a | 19 | 66.7 | 1200 | — |
| idlefinger_b | 21 | 50 | 1000 | 300 `/weapons/assault/slap`; 650 `/weapons/assault/rattle` |
| idlefinger_c | 26 | 50 | 1250 | 500 `/weapons/assault/rattle`; 950 `/weapons/assault/slap` |
| ready | 25 | 50 | 1200 | 50 `/weapons/shotgun/rattle`; 600 `/weapons/assault/slap`; 900 `/weapons/shotgun/cock` |
| reload_1 | 4 | 66.7 | 200 | — |
| reload_loop | 4 | 66.7 | 200 | 66.7 sound `/weapons/shotgun/putin` |
| reload_2 | 10 | 50 | 450 | 150 sound `/weapons/shotgun/cock` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Machinegun** (`Ghoul/Weapon/Inview/machinegun/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 13 | 50 | 600 | 100 sound `weapons/mgun/done` |
| fire1_1 | 4 | 100 | 300 | 0 and 200: fire, effect `weapons/playermz/machinegun flash`, effect `weapons/playermz/shell_l shell` |
| fire1_2 | 6 | 50 | 250 | 0 sound `/weapons/mgun/rattle` |
| fire2 | 13 | 66.7 | 800 | 133.3 altfire, effect `weapons/playermz/phos flash2` |
| idle | 23 | 100 | 2200 | — |
| idlefinger_a | 28 | 50 | 1350 | 850 `/weapons/mgun/rattle` |
| idlefinger_b | 40 | 50 | 1950 | 1000 `/weapons/mgun/rattle` |
| land | 16 | 50 | 800 (looping mode) | 300 `/weapons/mgun/rattle` |
| ready | 19 | 50 | 900 | 0 `/weapons/mgun/ready` |
| reload | 46 | 50 | 2250 | 650 shellbullets; 750 `/weapons/mgun/reload` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Rocket** (`Ghoul/Weapon/Inview/rocket/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 10 | 66.7 | 600 | 400 sound `weapons/rocket/done` |
| fire1 | 8 | 100 | 700 | 0 fire, effect `weapons/playermz/rocket flash`, effect `weapons/playermz/exhaust exhaust_l` |
| fire2 | 14 | 66.7 | 866.7 | 0, 133.3, 266.7, 400: altfire + effect `weapons/playermz/rocket_alt2 flash`; 0 and 266.7 also effect `weapons/playermz/exhaust exhaust_l` |
| idle | 24 | 100 | 2300 | — |
| idlefinger_a | 14 | 100 | 1300 | 300 sound `weapons/flamegun/knob` |
| idlefinger_b | 19 | 100 | 1800 | — |
| ready | 10 | 100 | 900 | 100 sound `weapons/rocket/ready` |
| reload | 26 | 66.7 | 1666.7 | 333.3 `weapons/rocket/pullout`; 1066.7 `weapons/rocket/putin` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**MPG** (`Ghoul/Weapon/Inview/mpg/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| charge | 20 | 50 | 950 | — |
| done | 11 | 66.7 | 666.7 | 266.7 `weapons/mpg/done`; 533.3 `weapons/mpg/takeout` |
| fire1 | 10 | 50 | 450 | 50 altfire, effect `weapons/playermz/mpg flash`; 100 effect `weapons/playermz/mpgbuild2 flash` |
| fire2 | 5 | 50 | 200 | 50 and 150: fire, effect `weapons/playermz/mpgbeam1 flash` (50) / `…/mpgbeam2 flash` (150), sound `weapons/mpg/alt 4` |
| fire2_end | 4 | 50 | 150 | — |
| fire2_loop | 10 | 50 | 450 | every 100 ms from 0 to 400: fire, effect `weapons/playermz/mpgbeam1 flash` / `mpgbeam2` alternating, sound `weapons/mpg/alt 4` |
| idle | 46 | 50 | 2250 | effect `weapons/playermz/mpgcharge flash` every 200 ms (0–2200), `…/mpgcharge2 flash` at 2250; sound `weapons/mpg/idle *` at 0, 750, 1500 |
| idlefinger_a | 24 | 100 | 2300 | mpgcharge effect every 200 ms; `weapons/mpg/idle *` at 0, 800, 1600; `weapons/mpg/claw` at 600, 1600 |
| idlefinger_b | 12 | 66.7 | 733.3 | mpgcharge effect at 0, 200, 400, 600; `weapons/mpg/idle *` at 0 |
| ready | 49 | 33.3 | 1600 | 0 `weapons/mpg/takeout`; 500 `weapons/mpg/out`; 566.7 `weapons/mpg/claw`; 766.7 `weapons/mpg/on`; 966.7 effect `weapons/playermz/mpgbuild flash` |
| reload | 21 | 66.7 | 1333.3 | 533.3 `weapons/mpg/pullout`; 1000 effect `weapons/playermz/mpgbuild2 flash`; 1066.7 `weapons/mpg/putin` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Flamegun** (`Ghoul/Weapon/Inview/flamegun/`)

| Sequence | Frames | ms/frame | Once | Notes |
|---|---|---|---|---|
| done | 9 | 66.7 | 533.3 | 200 sound `weapons/flamegun/done` |
| fire1_1 | 4 | 66.7 | 200 | 0 effect `weapons/playermz/fthrowmain flash` |
| fire1_2 | 3 | 66.7 | 200 (looping mode) | 0 effect `weapons/playermz/fthrow3 flash` |
| fire1_idle | 18 | 100 | 1700 | fire every 100 ms from 0 to 1700, with argument `1` at 400, 800, 1300; effect `weapons/playermz/fthrow1 flash` at 0, 400, 800, 1200, 1600 and `…/fthrow2 flash` at 200, 600, 1000, 1400 |
| fire2_1 | 3 | 100 | 200 | 100 effect `weapons/playermz/flamebuildup` |
| fire2_2 | 5 | 100 | 400 | 0 altfire, effect `weapons/playermz/flamerelease flash` |
| fire2, fire2_charge, fire2_idle | 3 / 19 / 3 | 66.7 / 100 / 100 | | unused (`fire2` has an altfire note at 200) |
| idle | 23 | 100 | 2200 | effect `weapons/playermz/fthrowrand flash` at 0, 600, 1200, 1800 |
| idlefinger_a | 17 | 100 | 1600 | fthrowrand at 0, 600, 1200; 400 sound `/weapons/flamegun/knob` |
| idlefinger_b | 11 | 100 | 1000 | fthrowrand at 0, 600 |
| idlefinger_c | 26 | 50 | 1250 | fthrowrand at 0, 600; 50 sound `/weapons/flamegun/burn` |
| ready | 12 | 66.7 | 733.3 | 66.7 `/weapons/flamegun/pullout`; 333.3 `/weapons/flamegun/tug` |
| dryfire | 3 | 200 | 400 | 0 sound `weapons/gpistol/empty` |

**Hurthand / Throwhand**: `hurthand` 13 frames × 50 ms (600 ms); `throwhand` 12 frames × 50 ms (550 ms); no notes.

### 4. Damage, range and projectile data

Per-shot damage, range, pellets, projectile speeds, splash and the full spread formula are server-side and given in
SPEC-782 (attack table, Behaviours 1–10). Summary for player weapons (base damage before falloff, armour and
locational rules): knife slash 22, thrown knife 30; pistol2 120; pistol1 45; mpistol 40; assault 45; sniper 80 (110
with realistic damage); slug 100 direct + 60 splash; concussion grenade (own rule); shotgun 40 centre + 12 pellets
(14 per pellet against creatures); machinegun 100; phosphorus grenade (150 − d)/2; rocket 250 + 225→32 splash; split
rocket halves 150 + 200→32; MPG beam ramping, burst 240; flame stream 10 (16 in deathmatch); fireball 300 splash +
fire pool. Bullet reach is 3 × the attack's range value (pistol1 1536, pistol2 2400, sniper 4800 units; shotgun 256).

Accuracy: the player module only supplies the spread counter (Behaviour 5) and the per-mode spread values (table 1);
the cone is min(counter × spread/shot, max spread) plus the game-rule movement term (velocity², an airborne-like body
state, and +4.5° for the sniper rifle while its field of view is above 60°; SPEC-782 Behaviour 1). Crouching has no term of its
own in 1.07f. Recoil: the per-shot view kick of table 1 (decayed ×0.7 per server frame by the game, SPEC-780) plus,
for rockets and split rockets, the game's recoil push (SPEC-782 Behaviour 10, k = 12 / 6). Penetration: bullets do not
pass through solid walls; they pass through glass and window surfaces (damaging glass), and beam attacks (MPG, flame
stream) continue through up to 12 / 3 targets (SPEC-782 Behaviours 3 and 8).

## Test vectors / how to verify

1. Pistol1 with 18 rounds, hold attack: shots every 0.3 s on a 10 Hz server; after 18 shots the clip is empty, the
   end of the last fire sequence starts `reload_a` (0.87 s), then firing resumes. Holding fire with no reserve gives
   one `dryfire_1hand` click and a forced switch to the best non-explosive weapon.
2. Assault rifle, tap the trigger for 0.15 s: 2 shots (0 and 0.1 s), the gun goes idle at once; the next burst
   starts with the spread counter left over (spread not reset until a reload) — check with a long-range target.
3. Sniper rifle: alternate press → `scopein` (0.5 s) → field of view 60; keep holding → field of view drops 5° per
   0.1 s to 6; release, press +weaponExtra2 → zooms out to 60 max; press alternate again → `scopeout`, field of view
   95. Reload while scoped → scope off, after `reload` (1.6 s) the previous zoom returns.
4. Shotgun with 2 of 8 shells loaded and 10 in reserve: `reload` → `reload_1`, 6 × `reload_loop` (one
   `/weapons/shotgun/putin` each), `reload_2`; total 0.2 + 6 × 0.2 + 0.45 s.
5. Machinegun alternate with 9 rounds in the clip: nothing fires, an automatic reload starts (auto-reload on).
6. Knife with exactly 1 knife: alternate does nothing; with 2: one knife is thrown and 1 remains.
7. `weapondrop` holding the 9 mm in single player (where dropping is allowed): the pistol pickup appears with the clip
   rounds, `throwhand` plays (0.55 s), then the best/next weapon is drawn without a put-away.
8. Rocket alternate with 2 rockets loaded: 2 split rockets leave (the 3rd and 4th notes find no ammunition and start
   an automatic reload instead).
9. Random function: `random(0, 100)` at now = 1.0 is 4 (formula above); the knife's first primary at that time plays
   `fire1_2a` (4 < 75).

## Open questions

- Ghoul note-boundary rule (note exactly at the start/cycle time) — confirm against the Ghoul runtime (SPEC-011).
- The per-mode float at descriptor offsets 0x14/0x18 and the value at 0xC0 (+20000): no reader found.
- Which instance the engine client draws while not predicting, and how first-person `sound`/`effect` notes reach the
  player in a local game (player.dll plays them only in client context).
- Exact in-game automatic rates (Constants 2 are derived from the rules and the data).
