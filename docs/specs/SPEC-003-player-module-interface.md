# SPEC-003: Player module (player.dll) interface
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: player.dll, SoF.exe, gamex86.dll, version 1.07f
Depends on: SPEC-001 (game module interface: imports #61, #67, #74, #75), SPEC-201/SPEC-203 (network messages, configstrings); Ghoul model interface and weapon tables to get their own specs

## Purpose

player.dll holds the weapon-and-inventory logic for one player. It covers weapons owned and selected, clip
and reserve ammunition, armor, usable items (flash packs, C4, goggles, claymores, medkits, grenades, CTF flag),
the first-person ("in-view") weapon models and their animation events, the weapon console commands, how
inventory state is sent from server to client, and how inventory is kept across level changes and savegames.
It does **not** contain player movement or the HUD.

The same module serves both the server (through the game module) and the client (for weapon prediction).
In a listen server one loaded copy serves both sides at once.

## Interface / layout

### Loading

- player.dll imports only KERNEL32 and exports exactly two functions: `GetPlayerClientAPI` (ordinal 1) and
  `GetPlayerServerAPI` (ordinal 2). Both use the C calling convention and take two pointers:
  (common import table, side-specific import table). Each returns a pointer to the export table.
- Only the engine (SoF.exe) loads the file. It tries, in order: `<current dir>/final/player.dll`,
  `<current dir>/player.dll`, then `<path>/player.dll` for each filesystem search path. It prints the developer
  message `LoadLibrary (<path>)`.
- **Client side:** the engine calls `GetPlayerClientAPI(common, client)` when the client initializes.
  "Client failed to load player DLL" is a fatal error.
- **Server side:** the engine does not load the module directly. It passes a *player loader* function and a
  *player unloader* function to the game module in the game import table (entries 74 and 75, at byte offsets
  0x128 and 0x12C). The game module calls the loader from its InitGame with a null client table, its own
  server table and side flag 0. The loader then calls `GetPlayerServerAPI(common, server)`. On failure the
  game module raises the fatal error "Game failed to load player DLL".
  - Loader signature: `exports *load(common_table*, client_table*, server_table*, int isClient)`. isClient is
    -1 for the client and 0 for the server.
  - Unloader signature: `void unload(int isClient)`.
- After the server-side load, the engine computes a 32-bit checksum of the player.dll file. The seed is
  0xF8349720. For each file byte b (unsigned, in file order), first rotate the 32-bit value left by 1 bit.
  Then add b for the 1st, 3rd, 5th, … byte, or XOR it with b for the 2nd, 4th, … byte. The engine keeps the
  result. Its use is UNVERIFIED (probably sent to
  clients or used as an integrity check). To verify, compute the checksum on the shipped DLL and look for the
  value in network captures.
- Reference counting: the library is freed only when neither side holds it. Unloading the server side also
  forces the client to delete its prediction inventory and drop its own reference. If unloading fails, the
  engine raises the error "FreeLibrary failed for player library".
- Both callers check `exports.version == 1`. A mismatch is fatal: "player is version %i, not %i". Next they
  call `Init`. The game module then prints "***** InitPlayer Complete *****".

### Common import table (19 pointers, 76 bytes)

| # | Offset | Client value | Server (game) value | Used by player.dll | Meaning |
|---|---|---|---|---|---|
| 0 | 0x00 | Cvar_Get | Cvar_Get | no | |
| 1 | 0x04 | Cvar_Set | Cvar_Set | no | |
| 2 | 0x08 | Cvar_SetValue | Cvar_SetValue | no | |
| 3 | 0x0C | Cmd_Argc | Cmd_Argc | no | |
| 4 | 0x10 | Cmd_Argv | Cmd_Argv | no | |
| 5 | 0x14 | Cmd_Args | Cmd_Args | yes | `char *(void)`: the argument string of the current command |
| 6 | 0x18 | Com_Printf | the game's own Com_Printf | no | |
| 7 | 0x1C | Com_sprintf | the game's own Com_sprintf | yes | `void (char *dst, int size, const char *fmt, ...)` |
| 8 | 0x20 | Com_DPrintf | game dprintf | no | |
| 9 | 0x24 | FS_LoadFile | FS_LoadFile | no | |
| 10 | 0x28 | FS_FreeFile | FS_FreeFile | no | |
| 11 | 0x2C | irand | irand | no | |
| 12 | 0x30 | pointer to the `ghl_specular` cvar | pointer to the `ghl_specular` cvar | yes | `cvar_t *`. A value > 0 enables specular lighting on view models. The float value is at byte 0x18 of the cvar. |
| 13 | 0x34 | create a Ghoul player-model-info object | **not filled in by the game (undefined)** | no | |
| 14 | 0x38 | load the file `Ghoul/<dir>/<name>` (falls back to `Ghoul/comskin/<name>`) | same | no | |
| 15 | 0x3C | read one text line from a memory buffer | same | no | |
| 16 | 0x40 | precache `<name>.gsq` | same | no | |
| 17 | 0x44 | register the models listed in `Ghoul/<dir>/<name>.gsq` with a Ghoul object | same | yes | `bool (const char *dir, const char *name, GhoulObject*)`. Lines starting with `#` include another list. With no .gsq, it registers `Ghoul/<dir>/<name>.GHL`. |
| 18 | 0x48 | apply the skins listed in a .gsq | same | no | |

Because the module keeps a single copy of this table, the last caller's values win. An implementation must
not rely on entry 13.

### Client import table (20 pointers, 80 bytes)

| # | Offset | Meaning / signature |
|---|---|---|
| 0 | 0x00 | `GhoulInterface *(void)`: the client-side Ghoul model interface |
| 1 | 0x04 | `float *`: client time in seconds |
| 2 | 0x08 | play predicted primary-fire effects for the current weapon (muzzle flash, effect sounds) |
| 3 | 0x0C | play predicted alternate-fire effects |
| 4 | 0x10 | play a view-weapon sound from an animation note. The note text is `name [n\|*]`: plays `<name>.wav`, or `<name><random 1..n>.wav`, or `<name>.wav` with random volume when `*`. Volume 0.6, on the player's own entity. |
| 5 | 0x14 | stop that view-weapon sound. The engine prints "WeaponKillSoundClient". player.dll never calls it. |
| 6 | 0x18 | spawn a view-weapon effect from an animation note `effect [bolt]` (default bolt `flash`) |
| 7..9 | 0x1C–0x24 | MSG_ReadByte / MSG_ReadShort / MSG_ReadLong on the current incoming network message |
| 10..12 | 0x28–0x30 | MSG_ReadByte / MSG_ReadShort / MSG_ReadLong `(sizebuf_t*)` |
| 13 | 0x34 | MSG_ReadData `(sizebuf_t*, void *dst, int len)` |
| 14..16 | 0x38–0x40 | MSG_WriteByte / MSG_WriteShort / MSG_WriteLong `(sizebuf_t*, int)` |
| 17 | 0x44 | SZ_Init `(sizebuf_t*, byte *data, int maxsize)` |
| 18 | 0x48 | SZ_Clear `(sizebuf_t*)` |
| 19 | 0x4C | SZ_Write `(sizebuf_t*, const void*, int)` (unused) |

sizebuf_t in this interface is 20 bytes: allowoverflow (byte, offset 0), overflowed (byte, 1), data pointer (4),
maxsize (8), cursize (12), readcount (16).

### Server import table (38 pointers, 152 bytes), supplied by the game module

| # | Offset | Meaning / signature |
|---|---|---|
| 0 | 0x00 | `GhoulInterface *(void)`: the server-side Ghoul model interface |
| 1 | 0x04 | `float *`: level time in seconds |
| 2 | 0x08 | fire the current weapon's primary attack `(?, edict_t *owner, inventory *inv)` |
| 3 | 0x0C | fire the current weapon's alternate attack (same arguments) |
| 4 | 0x10 | `(edict_t*)`: called just before a fire animation starts while player variable C (see below) is non-zero. Meaning UNVERIFIED. |
| 5 | 0x14 | `(edict_t*, int weaponId, int rounds)`: spawn a dropped-weapon pickup |
| 6 | 0x18 | `(edict_t*, int itemType)`: tells the game that an item is about to be used. UNVERIFIED. |
| 7 | 0x1C | `(edict_t*, int itemType, int count)`: spawn a dropped-item pickup |
| 8 | 0x20 | `(int attackId)`: precache the resources of a weapon attack |
| 9 | 0x24 | `int (const char*)`: effect index (effect configstrings) |
| 10 | 0x28 | `int (const char*)`: sound index (sound configstrings) |
| 11 | 0x2C | start sound (unused) |
| 12 | 0x30 | `bool (edict_t*)`: an entity flag test. UNVERIFIED. |
| 13 / 14 | 0x34 / 0x38 | read / clear a per-client float counter A |
| 15 / 16 | 0x3C / 0x40 | read / clear a per-client float counter B |
| 17 | 0x44 | Cbuf_AddText |
| 18 | 0x48 | Cvar_BitInfo `(int flagmask)` → info string |
| 19 | 0x4C | print a localized string-package message to one client `(edict_t*, int id, ...)` |
| 20 | 0x50 | look up the text of a string-package id (unused) |
| 21 | 0x54 | item use: flash pack `bool (edict_t*, item_slot*)` |
| 22 | 0x58 | unused |
| 23 | 0x5C | item use: C4 |
| 24 | 0x60 | item use: claymore |
| 25 | 0x64 | item use: medkit |
| 26 | 0x68 | item use: grenade |
| 27 | 0x6C | goggles switched on or off `(edict_t*, item_slot*)` (item_slot.state: 1 = on) |
| 28..30 | 0x70–0x78 | MSG_WriteByte / MSG_WriteShort / MSG_WriteLong `(sizebuf_t*, int)` |
| 31 | 0x7C | append a byte to client N's reliable stream `(int byte, int clientIndex)` |
| 32 | 0x80 | append data to client N's reliable stream `(const void*, int len, int clientIndex)` |
| 33..35 | 0x84–0x8C | SZ_Init, SZ_Clear, SZ_Write |
| 36 | 0x90 | write a savegame chunk `(int fourcc, const void*, int len)` |
| 37 | 0x94 | read a savegame chunk `(int fourcc, void *dst, int len, void **alloc)` |

### Export table (12 entries, 48 bytes; both sides get the same table)

| # | Offset | Contents |
|---|---|---|
| 0 | 0x00 | `int`: API version = 1 |
| 1 | 0x04 | `int *`: the shared **context flag** (see Behaviour) |
| 2 | 0x08 | `int Init(void)`: returns 1, does nothing else |
| 3 | 0x0C | `void Shutdown(void)`: does nothing |
| 4 | 0x10 | pointer to an array of 15 weapon-description object pointers, indexed by weapon id |
| 5 | 0x14 | `int *`: set to 1 on the client when a predicted primary attack started this frame |
| 6 | 0x18 | `int *`: the same for the alternate attack |
| 7 | 0x1C | `void (unsigned weaponMask)`: precache the in-view model of every weapon whose bit is set (bit n = weapon id n) |
| 8 | 0x20 | `void (void)`: forget all cached in-view model objects |
| 9 | 0x24 | `inventory *(void)`: create a new inventory object (676 bytes) |
| 10 | 0x28 | `void (inventory*)`: destroy an inventory object |
| 11 | 0x2C | `void (inventory*)`: after a full state restore (load or full-state message), rebuild the owner's view-weapon model and replicated weapon fields (server side only) |

The engine client uses entries 1, 2, 3, 5, 6, 8, 9 and 10. The game module uses 1, 2, 3, 4, 7, 8, 9, 10 and 11.

### Inventory object (C++ interface)

The object holds a vtable pointer at offset 0. All methods use the MSVC `thiscall` convention (`this` in ECX,
callee pops). The table has 79 slots. Argument counts below come from the module's default (base)
implementations.

| Slot | Offset | Args → result | Meaning |
|---|---|---|---|
| 0 | 0x00 | (bool alsoFlag) | remove all weapons, ammunition, armor and items. The CTF flag (item 7) is removed only when the argument is true. Clears pending requests. |
| 1 | 0x04 | (itemType) | remove all items of a type |
| 2 | 0x08 | () | server: carry out a pending weapon drop or lose (spawns pickups; uses variables B and I) |
| 3 | 0x0C | () | per-frame think. Runs the weapon state machine, animations, reload, item requests and fire, using the owner context (slot 61). |
| 4 | 0x10 | (const char *cmd, int force) → bool | handle a weapon or item console command (list below). Returns 1 if the command was consumed. Always returns 0 in client context. |
| 5 | 0x14 | () | top up every weapon's clip from reserve ammunition |
| 6 | 0x18 | (weaponId) → bool | switch to an owned weapon immediately |
| 7 | 0x1C | (slotIndex) | switch to the weapon in an inventory slot immediately |
| 8 | 0x20 | () | switch to the best weapon immediately (safe/unsafe choice depends on the tag, slot 49) |
| 9 | 0x24 | (weaponId) | request a change to a weapon (done on the next think) |
| 10 | 0x28 | (slotIndex) | request a change to a slot |
| 11 | 0x2C | () | request the best weapon |
| 12 | 0x30 | (ammoType, amount) → bool | add ammunition, capped at the maximum. Returns 0 if already full. |
| 13 | 0x34 | (weaponId) → int | a per-weapon message id (weapon table value + 20000). Meaning UNVERIFIED. |
| 14, 15 | 0x38, 0x3C | (weaponId, amount) → bool | add ammunition of the weapon's ammo type. amount 0 = the default pickup amount (random within the table range). |
| 16 | 0x40 | (weaponId, clipRounds) → bool | give a weapon. Returns 0 if already owned or not a carryable weapon. |
| 17 | 0x44 | () → int | total weight of the carried weapons |
| 18 | 0x48 | (weaponId) → int | weight of one weapon |
| 19 | 0x4C | (itemType, count, max) → bool | add items (6 item slots; max -1 = unlimited) |
| 20 | 0x50 | (delta) → int | add armor, clamped to 0..100. Returns the new armor, or 0 when nothing changed. |
| 21 | 0x54 | (itemType) → bool | has an item of this type |
| 22 | 0x58 | (weaponId) → bool | owns this weapon |
| 23 | 0x5C | () → int | current weapon id |
| 24 | 0x60 | () → int | id of the best available weapon |
| 25, 26 | 0x64, 0x68 | () → int | selected item type, selected item count |
| 27, 28 | 0x6C, 0x70 | () → int | reserve ammo of the current weapon's ammo type; rounds in the current clip |
| 29, 30 | 0x74, 0x78 | () / (weaponId) → int | a weapon-table value of the current or given weapon. UNVERIFIED. |
| 31 | 0x7C | (damage, float, ?) → int | let armor absorb damage and return what remains (when the float is non-zero, armor is used at a 2:1 rate). UNVERIFIED. |
| 32 | 0x80 | () → int | armor |
| 33 | 0x84 | () → int | an internal flag (the client predictor reads it as a boolean). UNVERIFIED. |
| 34 | 0x88 | (?) | one-shot weapon event (replicated as event 4) |
| 35, 36 | 0x8C, 0x90 | () → int / () | read / toggle the weapon alternate mode (toggling is replicated as event 5) |
| 37, 38 | 0x94, 0x98 | get / set | player variable A (set is replicated as event 0) |
| 39, 40 | 0x9C, 0xA0 | get / set | player variable B: weapon dropping enabled (gates `weapondrop`, `weaponlose` and the drop done by slot 2) |
| 41, 42 | 0xA4, 0xA8 | get / set | player variable I: bit mask of weapons kept (never dropped). Set ORs in bits 0, 13 and 14. |
| 43, 44 | 0xAC, 0xB0 | get / set | player variable C (set is replicated as event 1) |
| 45, 46 | 0xB4, 0xB8 | get / set | player variable D (event 2) |
| 47, 48 | 0xBC, 0xC0 | get / set | player variable E (event 3). Changes the reload rule. UNVERIFIED. |
| 49, 50 | 0xC4, 0xC8 | get / set (char*) | a tag string of up to 4 characters |
| 51..56 | 0xCC–0xE0 | get / set pairs | player variables F, G and H (not replicated) |
| 57 | 0xE4 | () → bool | server: request a weapon drop |
| 58 | 0xE8 | () | current weapon hook 1 |
| 59 | 0xEC | () | item think for all item slots (goggles battery, and so on) |
| 60 | 0xF0 | () → int | number of weapons owned |
| 61 | 0xF4 | (owner_context*) | set the owner context used by think and commands |
| 62 | 0xF8 | () | server: drop every weapon not in variable I |
| 63 | 0xFC | () | current weapon hook 2 |
| 64 | 0x100 | (unsigned *weaponMaskOut, int *healthOut) → int | load inventory from persistence cvars, then clear those cvars |
| 65 | 0x104 | (bool addClipsToReserve, int health) | save inventory to persistence cvars |
| 66 | 0x108 | (unsigned *weaponMaskOut, const char *info) → bool | load inventory from an info string |
| 67 | 0x10C | (char *out, bool weapons, bool ammo, bool items, bool armor) | write an info string |
| 68, 69, 70 | 0x110–0x118 | () | client: clear the event queue / read an event block from the current message / apply queued events |
| 71, 72 | 0x11C, 0x120 | () | server: clear the outgoing events / send them to the owner |
| 73, 74 | 0x124, 0x128 | set / get | an int value (client side resets it on full-state load) |
| 75, 76 | 0x12C, 0x130 | () | write / read the savegame chunk `INVN` |
| 77 | 0x134 | (sizebuf_t*) | client: read the full 640-byte state from a message |
| 78 | 0x138 | (int clientIndex) | server: append the full 640-byte state to the client's reliable stream |

### Owner context (52 bytes, passed to slot 61; built every frame)

| Offset | Type | Meaning |
|---|---|---|
| 0x00 | ptr | the inventory object itself |
| 0x04 | edict_t* | owner entity. On the client this is a stand-in entity for the local player. |
| 0x08 | int | attack button held (button bit 0) |
| 0x0C | int | alternate attack held (button bit 2) |
| 0x10 | int | button bit 3 |
| 0x14 | int | button bit 4 |
| 0x18 | int | lean right (lean byte of the command > 0). UNVERIFIED meaning. |
| 0x1C | int | lean left (< 0). UNVERIFIED. |
| 0x20 | int | a client state flag. UNVERIFIED. |
| 0x24 | int | a client state flag. UNVERIFIED. |
| 0x28 | ptr | client-specific data pointer. UNVERIFIED. |
| 0x2C | float | frame length × 10 (seconds × 10) |
| 0x30 | int | one-shot flag, cleared by the caller after use |

### Other C++ objects across the boundary

- **Weapon-description objects** (export entry 4): one per weapon id. The vtable has 32 slots. The game module
  reads data members at byte offsets 0x1C, 0x20, 0x24, 0x28 (spread/range-like floats; UNVERIFIED) and 0xB8, 0xBC
  (the primary and alternate attack ids). It calls virtual slots at offsets 0x70 and 0x74 (booleans asked before
  a fire trace). The full layout belongs to the weapons spec.
- **Item objects**: 8 entries indexed by item type. 8 virtual slots, including think(owner, slot),
  use(owner, slot) → bool, switch off (goggles), name, and string-package id.
  item_slot is 16 bytes: type, count, state, next time (float).
- **Ghoul interfaces** from import entry 0 of either side. The module creates model objects and instances,
  registers sequences, turns parts on and off, and attaches note callbacks named `sound` and `effect`. Those
  callbacks are objects implemented inside player.dll and called by the Ghoul runtime. See the Ghoul spec.

## Behaviour

### Context flag

The export table points to one integer shared by both sides:

- When it is 0, every call into the module behaves as the **server** (authoritative). It uses the server import
  table and level time, sends events, spawns pickups and writes cvars.
- When it is non-zero, the module behaves as the **client predictor**. It uses the client import table and
  client time, plays local effects, and never spawns anything or sends anything.

Who writes the flag:

- The engine client writes -1 at the start of every client frame and before it recreates its prediction
  inventory.
- The engine server writes 0 at the start of every server frame. Around some game-module calls (game export
  offsets 0x48 and 0x4C) it saves the flag, sets it to 0, and restores it afterwards.
- The game module receives the pointer from its loader call. It hands the pointer back to the engine through
  a pointer slot in the game import table (entry 67).

### Client prediction

The client keeps its own inventory object while the cvar `predicting` is "1". The engine sets `predicting` from
cl_predict, and only when the server is not `localhost`. Each frame, while connected and active, the client:

1. Sets the owner context.
2. Applies the queued server events (slot 70), then clears them (slot 68).
3. Only while the predicted player is in a normal state (a player-state type value below 2; UNVERIFIED which
   field), passes the pending weapon command to slot 4 and then runs think (slot 3). Slot 4 returns
   immediately in client context, so in 1.07f the command step has no effect. Weapon changes reach the
   predictor only through server events and full-state messages.

When export entry 5 or 6 reads non-zero, the client copies the current client time into the outgoing user
command at byte offset 0x10 (primary) or 0x14 (alternate). This happens only while predicting. In-view weapon
models of a remote server are not shared: on connect to a non-local server the client calls export 8.

### Network messages (server → client)

| svc | Payload | Client action |
|---|---|---|
| 0x1C (28) | byte count, then `count` events | read and queue (slot 69); when not predicting, also apply immediately (slot 70) |
| 0x1D (29) | byte resync counter | create a fresh prediction inventory; wait until the server's counter matches before predicting again ("Waiting for prediction resynch") |
| 0x1E (30) | 640 raw bytes | full inventory state (slot 77). The server sends it right after 0x1D on the path where a client's existing inventory is restored (observed for savegame load; exact trigger UNVERIFIED). |

Event encoding: header byte = (size class << 4) | event id.

- Size class of the single argument: 0 = byte, 1 = short, 2 = short followed by a byte (24-bit, low 16 bits
  first), 3 = long.
- Event ids: 0..3 set player variables A, C, D and E; 4 = one-shot weapon event; 5 = toggle the alternate mode.
  Events 4 and 5 carry no argument in practice.
- The server sends at most one block per flush. The block goes into the owner's reliable stream as the bytes
  0x1C, count, events.

The layout of the 640-byte state block is UNVERIFIED. It is the object's data from byte 0x1C to the end.
Verify by capturing svc 0x1E after loading a savegame.

### Console commands handled (slot 4, server context only)

`reload`, `weapprev`, `weapnext`, `weaponselect <n>`, `weapondrop`, `weaponlose`, `weaponbestsafe`,
`weaponbestunsafe`, `itemuse [name]`, `itemnext`, `itemprev`, `itemdrop`.

- `weaponselect n` requests inventory slot n-1.
- `itemuse` with no name uses the selected item. Names accepted: `fpak`, `c4`, `goggles`, `medkit`, `grenade`,
  `ctf_flag`. An unknown name returns "not handled" (0). A known name that is not carried is consumed and
  nothing happens.
- `weapprev`, `weapnext`, `weaponselect`, `weaponbestsafe` and `weaponbestunsafe` are consumed but ignored when
  `force` = 0 and player variable H is 0. `weapondrop` and `weaponlose` act only when player variable B is
  non-zero.
- Pending request codes (useful for prediction parity): reload 1, next 2, prev 3, best-safe 4, best-unsafe 5,
  lose 6, drop 7, select slot i = 8 + i.

### Persistence across levels (cvars)

Saving (slot 65) queues console commands through Cbuf_AddText:

- `set <weapon> 1 w;` for each weapon owned (clip rounds go back to reserve when requested)
- `set <ammo> <n> a;` for every ammo type
- `set <item> <n> i;` for each item slot
- `set armor <n> i;`
- `set health <n> m;`

The trailing letter is a cvar flag class.

Loading (slot 64) reads Cvar_BitInfo(0xF00) as `\name\value` pairs and gives the matching weapons, ammo, items
and armor. It reports health through the out-pointer. It clears every cvar it used with
`set <name> 0 <class>;`. Then it gives (with empty clips) every weapon whose bit was set, in the fixed order
1, 3, 2, 8, 6, 4, 5, 9, 7, 10, 11, 12.

The info-string variant (slots 66 and 67) uses the same names with the format `\name\1` (weapons),
`\name\count` (ammo, items) and `armor\n\`.

### Savegame

Slot 75 writes chunk id 0x494E564E (the ASCII bytes "NVNI" in file order, written as the FOURCC 'INVN') with
640 bytes. Slot 76 reads it back.

## Constants

Weapon ids (also the bit numbers in weapon masks):

| Id | Cvar name | In-view model directory |
|---|---|---|
| 0 | nothing | — |
| 1 | knife | Knife |
| 2 | pistol2 | Pistol2 |
| 3 | pistol1 | Pistol1 |
| 4 | mpistol | MPistol |
| 5 | assault | Assaultrifle |
| 6 | sniper | SniperRifle |
| 7 | slugger | Autoshotgun |
| 8 | shotgun | Shotgun |
| 9 | machinegun | Machinegun |
| 10 | rocket | Rocket |
| 11 | mpg | MPG |
| 12 | flamegun | Flamegun |
| 13 | — | Hurthand |
| 14 | — | Throwhand |

In-view model path: `Ghoul/Weapon/Inview/<Dir>/<Dir>.ghl`, with .gsq lists in `Weapon/Inview/<Dir>`.

Ammo types (cvar name; maximum carried): 0 nothing; 1 aknife 6; 2 a44 90; 3 a9mm 200; 4 shells 50; 5 a556 300;
6 rockets 20; 7 battery 150; 8 fuel 60; 9 slug 100.

Default pickup amount when slot 14/15 is called with amount 0: a pseudo-random integer in [min, max] (inclusive)
for the weapon's ammo type — 1: 1..1, 2: 4..9, 3: 5..8, 4: 2..4, 5: 15..20, 6: 1..3, 7: 5..10, 8: 5..10, 9: 5..10.
Special case: weapon 4 (mpistol) gives ammo type 3 in 8..15.

Item types (cvar name, display name):

| Type | Cvar name | Display name |
|---|---|---|
| 1 | fpak | "Flash Packs" |
| 2 | c4 | "C4" |
| 3 | goggles | "Light Amplification Goggles" |
| 4 | claymore | "Claymore" |
| 5 | medkit | "Medkit" |
| 6 | grenade | "Grenade" |
| 7 | — | CTF flag (`ctf_flag`) |

- Goggles battery: drains 1 per 0.3 s while on, recharges 1 per 0.5 s while off, maximum 100. At 0 the goggles
  switch off.
- Armor: 0..100.
- Item slots: 6. Weapon slots: 12 selectable (14 storage).
- Version constant: 1. Savegame chunk: 'INVN'. Inventory state block: 640 bytes. Inventory object: 676 bytes.

## Test vectors / how to verify

- Start a local game with `developer 1`: the console shows `LoadLibrary (…player.dll)` once (one copy shared).
- Replace player.dll with a stub whose version field is 2: the client aborts with "player is version 2, not 1".
- `give` a weapon on map A, change level: the console log shows `set <weapon> 1 w;`-style commands and the
  weapon is present on map B.
- Capture a demo or network stream while picking up ammo on a remote server: svc 28 messages appear with
  header bytes of the form 0x0? / 0x1? (event + size class).
- Load a savegame on a listen server: svc 29 followed by svc 30 with 640 bytes reaches the client.
- Checksum: running the algorithm above over the shipped 1.07f player.dll (110 592 bytes) gives 0xEACC2EEA.
  This value was computed from the description, not read from the running engine (UNVERIFIED). To confirm,
  read the engine's stored value with a debugger after a map load.

## Open questions

- Meaning of player variables A, C, D, E, F, G, H, of the slot 73/74 value, and of the server callbacks 4, 6,
  12, 13–16 (UNVERIFIED).
- Layout of the 640-byte state block. Weapon-description object layout and its 32 virtual slots.
- Exact purpose of the server-side player.dll checksum.
- Owner-context fields 0x18–0x28.
