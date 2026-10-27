# SPEC-601: Inventory and items — weapon slots, ammunition, armor, usable items, player variables, commands, persistence
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: player.dll, version 1.07f (the inventory object of SPEC-003 as used by gamex86.dll and SoF.exe)
Depends on: SPEC-003 (player module interface: the 79-method inventory object, owner context, events, persistence,
savegame chunk), SPEC-600 (weapons), SPEC-602 (prediction), SPEC-201 (svc 28–30), SPEC-202 (player state, user
command), SPEC-303 (savegames), SPEC-302 (cvar flag letters), SPEC-780 (player client, game-mode loadouts), SPEC-781
(pickups), SPEC-782 (item use actions), SPEC-131 (string packages).

## Purpose

Defines the contents and rules of the per-player inventory object: which weapons can be carried and in which slots,
ammunition and its limits, armor, the six usable-item slots and the behaviour of each item, the player variables the
game module sets to select game-mode rules, the weapon and item console commands, weapon selection order, and the
state that is sent to clients and stored in savegames. This spec completes and corrects the method meanings that
SPEC-003 left open.

## Interface / layout

### 1. Inventory methods — complete meanings (refines SPEC-003)

Argument and result conventions are those of SPEC-003. "Server only" methods do nothing in client context.

| Slot | Meaning |
|---|---|
| 0 | Clear (bool alsoFlag): every weapon slot emptied (clip 0, no weapon), all 11 ammunition counts 0, every item slot emptied except CTF-flag slots unless alsoFlag, armor 0, pending weapon request 0. The owned-weapon flags (method 22) are **not** cleared, and the current weapon id is not changed |
| 1 | Remove all item slots of a type |
| 2 | Death drop (server only): items switched off (method 59); the current weapon's idle and deselect rules run; if weapon dropping is enabled (B) or a drop is pending, the current weapon (or the slot marked for dropping) is dropped as a pickup unless kept (mask I); then, if F is on, method 62 |
| 3 | Think (SPEC-600 Behaviour 3) |
| 4 | Command (Behaviour 6) |
| 5 | Reload every carried weapon instantly (no animation, ignores single-shell loading; normal or E rule) |
| 6 | Select weapon by id now (searches slots 0–11) → bool |
| 7 | Select weapon slot now |
| 8 | Select the best weapon now (tag `safe` → safe list, else unsafe list; Behaviour 5) |
| 9 | Request weapon by id (slots 0–11; ignored if not carried) |
| 10 | Request weapon slot |
| 11 | Request the best weapon (as 8) |
| 12 | Add ammunition (type, amount) → bool (Behaviour 2) |
| 13 | (weapon id) → per-weapon value + 20000 (SPEC-600 table 2, offset 0xC0; meaning UNVERIFIED) |
| 14, 15 | Add ammunition for a weapon (id, amount); amount 0 → default pickup amount → result of method 12 |
| 16 | Give weapon (id, clip) → bool (Behaviour 1) |
| 17 | Total weight of the weapons in slots 0–11 |
| 18 | Weight of a weapon id |
| 19 | Add items (type, count, max) (Behaviour 4) |
| 20 | Add armor (delta) → int (Behaviour 3) |
| 21 | Has an item of a type → bool |
| 22 | Owns weapon id → non-zero (tests the owned-weapon flags, not the slots) |
| 23 | Current weapon id |
| 24 | Best weapon id (Behaviour 5; tag `safe` → "keep explosive" variant) |
| 25, 26 | Selected item type, selected item count |
| 27 | Reserve ammunition of the animated weapon's primary ammo type |
| 28 | Rounds in the current slot's clip |
| 29, 30 | Clip size of the current weapon / of a weapon id |
| 31 | Armor absorption (damage, mode, fraction) → int (Behaviour 3) |
| 32 | Armor |
| 33 | Sniper scope active (SPEC-600) — the game shows it as HUD status bit 0x0001 |
| 34 | Knife blood (argument): if the knife is not bloody, it becomes bloody; server sends event 4 with the argument |
| 35, 36 | Holstered? / toggle holster (server sends event 5) (SPEC-600 Behaviour 9) |
| 37, 38 | Player variable A get / set (set sends event 0) |
| 39, 40 | B get / set |
| 41, 42 | I get / set (set ORs in 0x6001) |
| 43, 44 | C get / set (event 1) |
| 45, 46 | D get / set (event 2) |
| 47, 48 | E get / set (event 3) |
| 49, 50 | Weapon tag get / set (at most 4 characters kept) |
| 51, 52 | F get / set |
| 53, 54 | G get / set |
| 55, 56 | H get / set |
| 57 | Request a drop of the current weapon (server only) → true; false in client context |
| 58 | Run the current weapon's deselect rule (sniper unzoom, flamethrower sound off) |
| 59 | Items switch-off: every carried item's switch-off rule is called with the **selected** item slot (goggles: off) |
| 60 | Number of carried weapons (slots 0–11) |
| 61 | Set owner context (SPEC-003) |
| 62 | Drop all weapons (server only): every weapon in slots 0–11 not in mask I becomes a pickup; all are removed |
| 63 | Landing notification (machinegun `land`, SPEC-600) |
| 64–67 | Persistence (Behaviour 8) |
| 68–72 | Events (SPEC-602) |
| 73, 74 | "Owner predicts" flag set / get (SPEC-602) |
| 75, 76 | Savegame chunk write / read (Behaviour 9) |
| 77, 78 | Full state read / send (Behaviour 9) |

### 2. Player variables (set by the game module)

| Var | Methods | Default (new inventory) | Meaning |
|---|---|---|---|
| A | 37/38 | on (−1) | ammunition is used (off = infinite ammunition: no check, no decrement) |
| B | 39/40 | 0 | weapon dropping allowed (`weapondrop`, `weaponlose`, death drop) |
| I | 41/42 | 0x6001 | kept-weapon mask (bit n = weapon id n); never dropped; bits 0, 13, 14 always set |
| C | 43/44 | on (−1) | reloading enabled (off: `reload` ignored, automatic reloads load instantly without animation) |
| D | 45/46 | on (−1) | automatic reload when a shot cannot be paid; also enables the out-of-fuel switch (SPEC-600) |
| E | 47/48 | 0 | reloading discards the rounds left in the clip (takes a full clip from reserve) |
| F | 51/52 | 0 | drop all weapons on death |
| G | 53/54 | 0 | `weapnext`/`weapprev` also stop at weapons with no ammunition at all |
| H | 55/56 | on (−1) | weapon-change commands are accepted without the force flag |
| tag | 49/50 | `safe` | best-weapon preference; the game sets it from userinfo `bestweap` (SPEC-780) |

Values set by the game in 1.07f (for reference; SPEC-780 owns the loadouts): deathmatch default loadout — A off with
infinite ammunition (`dmflags` 0x800), C off with `dmflags` 0x40 ("no reload"), B on unless `dmflags` 0x4, I = knife
(+ 9 mm pistol with `dmflags` 0x10). Realistic — A on, B on, I = knife + 9 mm, C on, D **off**, E **on**, F **on**,
G **on**, tag from `bestweap` (bots `unsafe`). Arsenal — A off, B off, C off, H off, tag `unsafe`. Single player —
A on, I = knife, tag from `bestweap`.

### 3. Inventory state block (640 bytes)

The object's state from byte 0x1C to its end is sent to a client as svc 30 and stored as the savegame chunk `INVN`
(SPEC-003). Offsets below are within the 640-byte block (add 0x1C for the object offset). All fields are 32-bit
little-endian unless noted.

| Offset | Meaning |
|---|---|
| 0x000 | pending weapon request code (Behaviour 6) |
| 0x004 | client: switch dictated by the server pending (SPEC-602) |
| 0x008 | client: synchronised with the server's weapon |
| 0x00C | target slot of a pending switch (−1 none) |
| 0x010 | current weapon id |
| 0x014 | animated weapon id (the one whose in-view model plays) |
| 0x018 | current slot index |
| 0x01C, 0x020, 0x024 | predicted-switch weapon id, animated id, slot (SPEC-602) |
| 0x028 | slot marked for dropping (−1 none) |
| 0x02C | 14 weapon slots × 12 bytes: {rounds in clip, weapon id, unused} |
| 0x0D4 | A |
| 0x0D8 | B |
| 0x0DC | I |
| 0x0E0 | C |
| 0x0E4 | E |
| 0x0E8 | D |
| 0x0EC | F |
| 0x0F0 | G |
| 0x0F4 | tag, 8 bytes (4 characters + NUL + padding) |
| 0x0FC | H |
| 0x100 | 11 ammunition counts, index = ammo type 0–10 (type 8 unused, see Behaviour 2; 10 unused) |
| 0x12C | ready flag (SPEC-600) (−1: commands swallowed) |
| 0x130, 0x134 | in-view instance, next in-view instance (pointers; meaningless after transfer; receivers clear them) |
| 0x138 | last selected slot |
| 0x13C | armor |
| 0x140 | owned-weapon flags (bit = weapon id) |
| 0x144 | switch-now flag (put-away finished) |
| 0x148 | owner predicts (client clears it on receipt) |
| 0x14C | 17 unused words |
| 0x190 | owner-context pointer (meaningless after transfer) |
| 0x194 | selected item slot (0–5) |
| 0x198 | pending item request (Behaviour 6) |
| 0x19C | last item-use time, truncated to whole seconds |
| 0x1A0 | 6 item slots × 16 bytes: {item type, count, state, next time (float)} |
| 0x200 | pistol1 stance (1 one hand, 2 two hands) |
| 0x204 | sub-pose flag (pistol1 c, pistol2 d) |
| 0x208 | a stance transition was the last idle choice |
| 0x20C | knife bloody |
| 0x210 | pistol2 stance (0 a, 1 c) |
| 0x214 | name of the current in-view sequence, 64 characters |
| 0x254 | spread counter |
| 0x258 | weapon state (SPEC-600 Behaviour 3) |
| 0x25C | knife throw check passed |
| 0x260 | stance b (assault rifle), "drawn" (sniper) |
| 0x264 | first shot of the fire sequence pre-paid |
| 0x268 | sniper "zoom while held" |
| 0x26C | sniper scope active |
| 0x270 | idle-variation counter |
| 0x274 | sniper saved zoom (integer degrees) |
| 0x278 | holstered |
| 0x27C | time of the last automatic-weapon shot (float) |

The sender transmits the live object bytes (pointer words included as they are); the savegame writer zeroes the
three pointer words (0x130, 0x134, 0x190) first. Readers zero them after reading; the client also clears 0x148.

### 4. Item types

| Type | Cvar name | Display name | String id (ITEMS package entry) | Use action (SPEC-782) | Drop gives | Consumed on use |
|---|---|---|---|---|---|---|
| 1 | fpak | Flash Packs | 0x301 FLASHPACK | throw flash pack | 1 | yes |
| 2 | c4 | C4 | 0x300 C4 | throw C4 | 1 | yes |
| 3 | goggles | Light Amplification Goggles | 0x303 LIGHT_GOGGLES | toggle (Behaviour 4) | whole slot (count = battery) | no |
| 4 | claymore | Claymore | 0x304 CLAYMORE | place claymore | 1 | yes |
| 5 | medkit | Medkit | 0x335 MEDKIT | heal (refused at full health) | 1 | yes, when it healed |
| 6 | grenade | Grenade | 0x336 GRENADE | throw grenade | 1 | yes |
| 7 | — (`ctf_flag` for `itemuse`) | Flag | 0x339 CTF_FLAG | cannot be used | whole slot | — |

Messages printed to the owner when an item is consumed (server only, SPEC-003 server import 19): ITEMS entry 0x0D
AMMO_GONE "No more ammo for your %s" when the last one is used, otherwise entry 0x0E AMMO_COUNT
"Ammo is now %d for your %s..." with the new count; `%s` is the display name.

## Behaviour

### 1. Weapon slots

- Each weapon has a fixed slot, given by the order 1, 3, 2, 8, 6, 4, 5, 9, 7, 10, 12, 11, 13, 14 (slot 0 = knife,
  1 = 9 mm pistol, 2 = .44 pistol, 3 = shotgun, 4 = sniper rifle, 5 = machine pistol, 6 = assault rifle, 7 =
  machinegun, 8 = slugger, 9 = rocket launcher, 10 = flamethrower, 11 = MPG, 12 = hurt hand, 13 = throw hand).
  `weaponselect n` selects slot n − 1, so keys 1–12 map to these weapons.
- Give weapon (method 16): refused (false) when the weapon is already in its slot or the id is not in the order list;
  otherwise the slot gets the weapon with `clip` rounds (−1 = a full clip; when ammunition is not used the clip is 0)
  and the owned flag is set.
- Only slots 0–11 count for weight, weapon count, next/previous and drop-all. The hand pseudo-weapons live in slots 12
  and 13.
- Encumbrance: the game refuses a new weapon when weight(new) + total weight > its limit (SPEC-781: single player
  `sk_maxencum` rows 0, 10, 7, 5, 5; Realistic 4). Weights: knife 0; pistols 1; mpistol, assault, sniper, slugger,
  shotgun, machinegun 2; rocket, MPG, flamethrower 3.

### 2. Ammunition

| Type | Cvar name | Maximum | Default pickup amount |
|---|---|---|---|
| 0 | nothing | 0 | — |
| 1 | aknife | 6 | 1 |
| 2 | a44 | 90 | 4–9 |
| 3 | a9mm | 200 | 5–8 (machine pistol pickup: 8–15) |
| 4 | shells | 50 | 2–4 |
| 5 | a556 | 300 | 15–20 |
| 6 | rockets | 20 | 1–3 |
| 7 | battery | 150 | 5–10 |
| 8 | fuel | 60 | 5–10 |
| 9 | slug | 100 | 5–10 |

- Add ammunition (method 12): refused when the count is already at the maximum; otherwise the amount is added and
  capped at the maximum (a partial add still succeeds).
- Fuel (type 8) is not kept in reserve: it is added directly to the clip of slot 10 (the flamethrower's slot), capped
  at 60, whether or not the flamethrower is carried. The fuel reserve count stays unused (the flamethrower never
  reloads), except that loading from persistence cvars also stores the value there.
- Default pickup amount (method 14/15 with amount 0): `random(min, max)` of SPEC-600 Behaviour 1 (time-seeded).
- The reserve shown to the player is that of the animated weapon's primary ammunition type; the clip is that of the
  current slot.

### 3. Armor

- Add armor (method 20, delta): nothing happens and 0 is returned when armor is 100 and delta ≥ 0, or armor is 0 and
  delta ≤ 0. Otherwise armor := clamp(armor + delta, 0, 100) and the new value is returned.
- Absorption (method 31, damage D (int), mode (float), fraction f (float)): with armor 0 the result is D. Otherwise
  a = trunc(D − D × f) is the part offered to the armor (the caller handles the remaining D × f itself):
  - mode = 0: armor > a → armor −= a, result 0; else result a − armor, armor 0.
  - mode ≠ 0 (armor costs 2 per point): armor > 2a → armor −= 2a, result 0; else result trunc((2a − armor) × 0.5),
    armor 0.
  Test vectors: armor 50, D 30, f 0, mode 0 → result 0, armor 20; armor 20, D 30, f 0.5, mode 1 → a = 15, 2a = 30 >
  20 → result trunc(10 × 0.5) = 5, armor 0.

### 4. Item slots

- Six slots. Add items (method 19, type, count, max; max −1 = no limit): if a slot already holds the type, the count
  is added; with a limit, a total above it is capped at the limit, and the add is refused when the slot already
  holds the limit. Otherwise the first empty slot takes the type (no empty slot → refused) with the same limit rule.
  When the selected slot was empty before a successful add, the selection advances (item request 1) so that the new
  item becomes selected.
- Selection: item request 1 (`itemnext`) moves the selection forward to the next non-empty slot (wrapping; stays if
  none), request 2 (`itemprev`) backwards.
- Use (`itemuse`, request 4 = selected slot, 5 + i = slot i found by name): waits (the request stays pending) until
  now ≥ T + 0.4, where T is the time of the previous use attempt truncated to whole seconds (so the wait is
  between 0 and 0.4 s). Then, for a non-empty slot: if the weapon is holstered and the item is not a medkit, the
  holster is toggled off first; a CTF flag is never used; server: the game is told an item is being used (server
  import 6: third-person throw animation); the item's use action runs (table 4; in client context every use action
  just returns true without doing anything); if it returns true the item is consumed (one less; at 0 the slot is
  emptied and the selection advances; goggles are never consumed). Using a named item that is not the selected one
  does not move the selection. T := trunc(now).
- Drop (`itemdrop`, request 3): the selected item's switch-off rule runs; server: the game spawns a dropped item
  (server import 7) with 1 or the whole slot (table 4); one is consumed, or the whole slot is emptied and the
  selection advances.
- Every think: pending item requests are processed, then every slot's item think runs (goggles only).
- Goggles (server only): the slot's count is the battery (0–100). Using toggles: on → next time = now + 0.3, off →
  now + 0.5; the game is notified (server import 27, state 1 = on). While on, every time now passes the next time the
  battery drops by 1 and the next time becomes now + 0.3; reaching 0 switches them off (battery 0, game notified).
  While off, when now passes the next time the battery rises by 1 (up to 100) and the next time becomes now + 0.5
  (at most 1 per think). Switch-off rule: state off, game notified.
- Items switch-off (method 59) calls the switch-off rule of every carried item but always passes the selected slot,
  so with goggles carried it marks the *selected* slot as off and tells the game "goggles off" (quirk; the goggles'
  own slot keeps its state and keeps draining). UNVERIFIED in game.

### 5. Best weapon and cycling

Priority lists (weapon ids, highest first):

| List | Order |
|---|---|
| safe | 11, 9, 5, 4, 8, 2, 3, 6, 1, 10, 7, 12 |
| unsafe | 11, 10, 7, 12, 9, 5, 4, 8, 2, 3, 6, 1 |
| keep-explosive | 10, 7, 12 |

- A slot is usable when it holds a weapon and — server only, unless the caller allows empty weapons — the weapon has
  rounds in its clip, or reserve ammunition, or ammunition is not used, or it is the knife.
- Best weapon (safe or unsafe list): the first weapon of the list held in a usable slot; none → no result.
- Method 24 with tag `safe`: if the current weapon is in the keep-explosive list, the result is the current weapon;
  otherwise the safe list. With any other tag: the unsafe list.
- Methods 8 and 11 use the safe list when the tag is exactly `safe` (case-sensitive), else the unsafe list.
- `weapnext` / `weapprev` start from the last selected slot (slot ≥ 12 counts as 0) and step through slots 0–11 with
  wrap-around until a usable slot (with variable G as "allow empty") is found or the start is reached.
- `weaponselect n`: slot n − 1 must hold a weapon (empty ammunition allowed).
- A request whose result is the already selected slot, or that finds nothing, is dropped. On the server nothing at
  all is selectable when no weapon id 1–12 is owned.

### 6. Commands (method 4, server context only)

Returns "handled" for every command below; returns "not handled" for any other command and for `itemuse` with an
unknown name. While the ready flag is negative (a hand pseudo-weapon is finishing) every command is swallowed.

| Command | Request code (weapon) / item request | Condition |
|---|---|---|
| `reload` | 1 | always (acted on only if reloading is enabled, C) |
| `weapnext` | 2 | ignored unless force or H |
| `weapprev` | 3 | ignored unless force or H |
| `weaponbestsafe` | 4 | ignored unless force or H |
| `weaponbestunsafe` | 5 | ignored unless force or H |
| `weaponlose` | 6 | only when B (dropping) is on |
| `weapondrop` | 7 | only when B is on |
| `weaponselect n` | 7 + n (slot n − 1 → code 8 + slot) | ignored unless force or H; no argument → nothing |
| `itemuse` | item request 4 | |
| `itemuse <name>` | 5 + slot index | names `fpak`, `c4`, `goggles`, `medkit`, `grenade`, `ctf_flag` (case-insensitive); a known name not carried is handled and does nothing; `claymore` is not accepted by name |
| `itemnext` / `itemprev` / `itemdrop` | item requests 1 / 2 / 3 | |

Command names are compared case-insensitively. Weapon requests are processed by the next think when the weapon is
ready (SPEC-600 Behaviour 8); a new command replaces a pending one.

### 7. Context flag side effect

Loading from an info string (method 66) sets the module's context flag to 0 (server) before it gives anything, and
leaves it there.

### 8. Persistence (methods 64–67)

- Save to cvars (method 65, addClips, health): for each weapon in slots 0–11: when addClips is set its clip rounds are
  added to the reserve of its ammo type (the clip itself is not emptied); `set <weapon> 1 w;`. Then `set <ammo> <n> a;`
  for ammo types 1–9 (fuel from the unused fuel reserve, which addClips filled from the flamethrower's clip), `set
  <item> <count> i;` per non-empty item slot, `set armor <n> i;`, `set health <health> m;`. All through Cbuf_AddText.
  Weapon cvar names: table 1 of SPEC-600; ammo and item names: tables 2 and 4.
- Load from cvars (method 64): parses Cvar_BitInfo(0xF00) (`\name\value` pairs). Weapon names with a non-zero value
  set the weapon's bit in the out-mask (and are queued `set <name> 0 w;`); ammo names add ammunition (method 12;
  `fuel` also sets the fuel reserve) and queue `set <name> 0 a;`; item names with a non-zero count add the items with
  no limit and queue `set <name> 0 i;`; `armor` adds armor; every entry that is not a weapon, ammo or item queues
  `set armor 0 i;`; `health` with a non-zero value is returned through the out-pointer and queues `set health 0 m;`.
  Finally each weapon whose bit was set is given with an empty clip, in the order 1, 3, 2, 8, 6, 4, 5, 9, 7, 10, 11,
  12. The result is the number of knives (ammo type 1). Nothing happens in client context or when the info string is
  empty.
- Info string (method 67, out, weapons, ammo, items, armor): concatenates `<weapon>\1\` per carried weapon (adding its
  clip to the reserve as above — the clip is not emptied, so the live inventory counts those rounds twice until the
  next clip change), `<ammo>\<n>\` for types 1–9, `<item>\<count>\` per item slot, `armor\<n>\`. Method 66 reads the
  same format (a leading `\` is optional) like method 64 but without queuing any command and without health.

### 9. Network and savegame

- Savegame (methods 75/76): chunk `INVN` (bytes `N V N I` in file order, SPEC-003) holding the 640-byte block (pointer
  words zeroed). Reading copies the block into the object; afterwards the game calls export 11 of SPEC-003 to rebuild
  the in-view model, which on the server also re-applies the "scope hides the model" state (model shown unless the
  sniper scope is active).
- Full state (methods 77/78): svc 30 + 640 bytes (SPEC-201), see SPEC-602.

## Constants

| Name | Value |
|---|---|
| weapon slots | 14 (12 selectable + 2 hand slots) |
| item slots | 6 |
| ammunition counts | 11 (types 0–10; 0–9 used) |
| armor range | 0–100 |
| goggles battery | 0–100; −1 per 0.3 s on, +1 per 0.5 s off |
| item use wait | 0.4 s after the whole second of the previous use |
| default kept-weapon mask | 0x6001 |
| state block / object size | 640 / 676 bytes |

## Test vectors / how to verify

1. Give the flamethrower, then pick up 30 fuel with an empty flamethrower: the clip shows 30 (no reserve); dropping the
   flamethrower and picking up fuel still raises slot 10's stored clip (visible when the flamethrower is picked up
   again with its own rounds — check with a `give` + HUD).
2. Armor 50, take 30 damage with armor fraction 0: armor 20, no health loss from the armored part.
3. Use a grenade at time 12.9, then immediately request another at 13.0: the second throw happens at once (12 + 0.4
   < 13.0); use one at 13.05 and request again at 13.1: the second waits until 13.4.
4. Goggles with battery 10: switch on — they switch off by themselves after 10 × 0.3 = 3.0 s; off for 5 s recharges
   10 units.
5. `itemuse claymore` returns "not handled" (the game then prints its unknown-command text); `itemuse medkit` without
   a medkit does nothing.
6. Realistic mode: reload with 5 rounds left in a 40-round assault-rifle clip and 100 in reserve → clip 40, reserve 60
   (the 5 rounds are lost).
7. Level change with 12 rounds in the pistol clip and 50 in reserve: the console receives `set pistol1 1 w;` and
   `set a9mm 62 a;` (clips added back), and on the next map the pistol is given empty with 62 in reserve.

## Open questions

- Meaning of the per-weapon value returned by method 13 (+20000).
- Whether the stale owned-weapon flags after method 0 are observable (the game may create a fresh inventory on
  respawn).
- The item switch-off quirk (method 59 with goggles not selected) — confirm in game.
