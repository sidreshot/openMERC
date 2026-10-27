# SPEC-AMEND-013: Player module across level change (MODEB-GAPS Q2)

Status: approved (G, 2026-10-26) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
level transitions crash inside player.dll on a call through a stale pointer).
Evidence: static analysis of the 1.07f binaries; cross-check SPEC-003, SPEC-760 B8 target_changelevel / B9.5,
SPEC-303. Not yet black-box verified on the three failing OpenMerc pairs.
Describes: gamex86.dll, player.dll, SoF.exe, version 1.07f
Depends on: SPEC-001, SPEC-003, SPEC-303, SPEC-760
Amends: SPEC-003 (Init / Shutdown / API table fill on every game init), SPEC-760 (changelevel carry)

## Summary

Across a level change the player module does **not** keep function-pointer tables from the previous
`InitGame`. `Shutdown` on the player module is a no-op; `Init` returns success and does nothing else. On every
game initialisation the engine (and the game module on the server side) calls `GetPlayerClientAPI` /
`GetPlayerServerAPI`, which copy the host import tables and **re-fill** the player module's export method
tables with fresh pointers into the newly loaded module image. Inventory and health cross the boundary through
persistence cvars written by the change-level path before shutdown, then read back after the new map's spawn —
not through dangling pointers inside player.dll.

## R13.1 — Shutdown and Init of the player module (Resolution)

1. Player export `Shutdown`: empty function (returns immediately). No teardown of callbacks, no clearing of
   tables, no free of the inventory objects (those are owned/freed by the game/engine side that created them).
2. Player export `Init`: returns success (1); no other work.
3. Consequence: any callback or method pointer that a reimplementation caches from a *previous* load of the
   player module and keeps across `ShutdownGame`/`InitGame` is wrong. The original always re-queries the API
   after reload.

Test: instrument OpenMerc (or a harness) so that after every map change the player export table pointers equal
the addresses returned by a fresh `GetPlayer*API` call; the three crashing transitions must not call through
any pointer that still equals a value observed before `ShutdownGame`.

## R13.2 — API tables are re-filled on every GetPlayer*API (Resolution)

1. `GetPlayerClientAPI(common, client)` and `GetPlayerServerAPI(common, server)` both:
   - copy the host "common" import table into the module's common-imports slot;
   - copy the side-specific import table (client or server) into the side-imports slot;
   - write a fresh export block whose Init/Shutdown/inventory/weapon methods point at the **current** module
     image, then return that block.
2. The game module's `InitGame` loads the player module (server side, null client table) and expects those
   exports to be valid for the lifetime of this game instance only.
3. The client engine likewise calls `GetPlayerClientAPI` when the client side of the player module is
   initialised (prediction path; local SP may still load the client side for other reasons — UNVERIFIED whether
   local SP ever calls client-side player methods for movement).

Test: after `gamemap`, log the first bytes of the player export block; they must differ from a stash taken
before shutdown if the module was unloaded/reloaded, and must match a new `GetPlayer*API` result always.

## R13.3 — What actually crosses a level change (Confirmation of SPEC-760 / SPEC-003)

On `target_changelevel` use in single player (player 1 alive; ignored while dead; ignored during intermission):

1. The player's inventory is saved to the persistence cvars (SPEC-003 inventory method: save with "add clips to
   reserve") together with the current health.
2. The level's mission statistics (kills, friendly kills, special shots, gibs, saves, money, played time, …)
   are added into the campaign totals that `getgamestats` reads.
3. The player module is notified (game → player hook used by the change-level path; exact method index is in
   SPEC-003's server import list — describe by role: "player notified of level exit").
4. The engine then runs the normal map-change sequence (`gamemap` / reconnect): `ShutdownGame` → load map →
   `InitGame` → spawn → auto.sof write (SPEC-AMEND-012 R12.3).
5. After spawn, inventory is loaded back from the persistence cvars (SPEC-003 load-from-persistence method)
   and health is applied. Weapon state that is not part of those cvars is rebuilt by the post-restore
   view-weapon rebuild.

Deathmatch changelevel is different (damage / no exit for players in practice) and is out of scope for mode B SP.

Test: exit tsr1→tsr2 with known health/ammo; after the new map's spawn the same health and weapons are present;
no call through a player method pointer that was captured before the change.

## Open questions

1. Whether the client-side player module is unloaded at all during a local `gamemap` (or only the server side
   cycles). If the client side stays resident, its export block is still re-filled when `GetPlayerClientAPI` is
   next called — a reimplementation that skips that call and reuses an old block still breaks.
2. Exact identity of the "player notified of level exit" method (role known; ordinal already in SPEC-003 —
   confirm with a black-box that inventory cvars change before `ShutdownGame` prints).

## Gatekeeper notes (G, 2026-10-26)

- No text removed. Lint clean; manual review found no tool-generated symbol names, code addresses, code or copied tables.
  `GetPlayerClientAPI` / `GetPlayerServerAPI` are the module's exported interface names, already in SPEC-003.
- For C (mode B): the rule is "never keep a pointer into the player module, or into objects it hands out, across
  `ShutdownGame` / `InitGame`; query the API again on every game init." This applies to the game module's cached
  player exports, to the engine's client-side copy, and to any inventory or weapon object pointers kept in
  `edict_t` / client records across the change. The three crashing transitions from `docs/impl/MODEB-GAPS.md` are
  the acceptance test (R13.1 and R13.3 tests).
- R13.3 item 3 names the "player notified of level exit" hook by role only. C should find it in SPEC-003 by role,
  not guess an ordinal.
