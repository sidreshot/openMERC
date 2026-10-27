# SPEC-001: Game module interface (engine ↔ game library)
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe and gamex86.dll, version 1.07f
Depends on: — (forward references: entity layout, trace result layout, player-movement block, string packages,
savegame chunks, Ghoul model interface, player module interface — each to get its own spec)

## Purpose

The server half of the engine and the game rules live in different modules. The engine loads the game library
`gamex86.dll`, hands it one table of engine services (the *import table*) and receives one table of game entry
points (the *export table*). This spec gives the exact layout of both tables, what each entry does as seen from
the other side, and the C++ objects / secondary tables that are passed across the boundary. It is the contract an
engine reimplementation must honour to run the original `gamex86.dll`, and that a game reimplementation must honour
to run inside the original `SoF.exe`.

The design descends from Quake II's `game_import_t` / `game_export_t`; where an entry has the same meaning as a
Quake II member, the Quake II member name is given. Order and contents differ substantially from Quake II, so the
Quake II header can NOT be reused as is.

## Interface / layout

### Conventions
- 32-bit x86, all pointers 4 bytes, every table entry 4 bytes, no padding.
- All functions use the C calling convention (`__cdecl`, caller pops arguments). Entries marked "variadic" take a
  printf-style format string followed by its arguments.
- "entity" = pointer to a game entity record (layout: separate spec). "vec3" = pointer to 3 consecutive 32-bit floats.
  "string" = pointer to a NUL-terminated 8-bit string. "bool" = 32-bit integer, 0 = false.
- "client number" = 0-based client slot. "entity number" = index of the entity in the entity array; for clients
  entity number = client number + 1.

### Loading and handshake
1. The engine looks for the library in this order: `<current directory>/final/gamex86.dll`,
   `<current directory>/gamex86.dll`, then `<search path>/gamex86.dll` for each game search path (UNVERIFIED for the
   third step: the iterator is believed to walk the file-system search paths).
2. It resolves the exported symbol `GetGameAPI` (the library's only required export). If missing, the library is
   unloaded and the server start fails with the error "failed to load game DLL".
3. `GetGameAPI` is called with one argument, a pointer to the import table (layout below), and returns a pointer to
   the export table. The import table may live in temporary storage: the game must copy it (the original copies all
   396 bytes into its own storage before returning).
4. The engine requires export field `apiversion` = **3**; otherwise it raises the error
   "game is version %i, not %i" (with the found value and 3).
5. The engine then calls the export `Init` immediately.
6. If a game library is already loaded when the server (re)initialises the game, the engine first calls `Shutdown`
   and unloads the library. Loading twice without unloading is an engine error ("Sys_GetGameAPI without
   Sys_UnloadingGame").

During `GetGameAPI` (before `Init`) the original game already uses three imports: the Ghoul interface getter
(offset 0x0F0), cvar registration (offset 0x15C, for cvar `ghl_mip`) and one Ghoul interface method. An engine must
therefore have those services ready before calling `GetGameAPI`.

### Import table (engine → game): 99 entries, 396 (0x18C) bytes

Columns: Off = byte offset; Args = arguments in order; Ret = return value; Q2 = matching Quake II member, if any.

| # | Off | Args | Ret | Meaning | Q2 |
|---|---|---|---|---|---|
| 0 | 0x000 | string name | int | Model index: find or allocate a model name slot (see Behaviour: name indexes) | modelindex |
| 1 | 0x004 | string name | int | Sound index | soundindex |
| 2 | 0x008 | string name | int | Effect index (SoF-specific name class) | — |
| 3 | 0x00C | string name | int | Image index | imageindex |
| 4 | 0x010 | string name | void | Release a previously indexed sound name (frees its slot, tells clients) | — |
| 5 | 0x014 | string address | int | Address ban check: nonzero = the given client address must be rejected | — (Q2 had this inside the game) |
| 6 | 0x018 | — | void | In deathmatch only: assign name slots to every Ghoul model file currently loaded by the engine | — |
| 7 | 0x01C | entity, string model | void | Set entity model (inline `*n` models also set bounds and link) | setmodel |
| 8 | 0x020 | entity, string model | void | Set a second, render-only model index on the entity. Not used by the original game | — |
| 9 | 0x024 | — | int | Command argument count | argc |
| 10 | 0x028 | int n | string | Command argument n ("" when out of range) | argv |
| 11 | 0x02C | — | string | All command arguments as one string | args |
| 12 | 0x030 | int printlevel, string fmt, … (variadic) | void | Print to all clients | bprintf |
| 13 | 0x034 | string fmt, … (variadic) | void | Developer print to the server console | dprintf |
| 14 | 0x038 | entity or NULL, int printlevel, string fmt, … (variadic) | void | Print to one client (NULL = server console) | cprintf |
| 15 | 0x03C | entity to, entity from, int printlevel, string fmt, … (variadic) | void | Print to one client, tagged with the sending client's number (chat) | — |
| 16 | 0x040 | entity | void | Tell one client to show the server welcome message | — |
| 17 | 0x044 | entity, string fmt, … (variadic) | void | Center-screen print to one client | centerprintf |
| 18 | 0x048 | entity or NULL, int x, int y, int textspeed, string text | void | Cinematic text print to one client or (NULL) all clients. Not variadic. Not used by the original game | — |
| 19 | 0x04C | int printlevel, int caption id | void | Show a caption (by id) to all clients. Not used by the original game | — |
| 20 | 0x050 | entity, int caption id | void | Show a caption (by id) to one client. Not used by the original game | — |
| 21 | 0x054 | — | void | Tell all clients to force console notify display | — |
| 22 | 0x058 | entity, int channel, int sound index, float volume, float attenuation, float time offset, int local | void | Start a sound on an entity. `local` = 1: only the entity's own player hears it | sound (+1 arg) |
| 23 | 0x05C | vec3 origin or NULL, entity, int channel, int sound index, float volume, float attenuation, float time offset, int local | void | Start a sound at a position | positioned_sound (+1 arg) |
| 24 | 0x060 | — | — | **Unused.** The engine never fills this entry (contents are undefined); the game never reads it | — |
| 25 | 0x064 | surface pointer (from a trace result), int damage | bool | Apply damage to a damageable world texture; true when it switched to its next damage stage | — |
| 26 | 0x068 | byte buffer, int capacity | int | Fill buffer with the distinct material codes used by the current map; returns how many were written (≤ capacity) | — |
| 27 | 0x06C | float fraction, int flag | void | Update the loading screen progress (sets cvar `menu_loading` to fraction and redraws). Ignored on a dedicated server | — |
| 28 | 0x070 | vec3 origin, int destination | void | Send the pending message buffer to a client set | multicast |
| 29 | 0x074 | vec3 origin, int destination, int excluded entity number | void | Same, but skip the client whose entity number is given (0 = skip nobody) | — |
| 30 | 0x078 | entity, bool reliable | void | Send the pending message buffer to one client | unicast |
| 31 | 0x07C | int | void | Append byte to pending message (identical to entry 32 in the original) | WriteChar |
| 32 | 0x080 | int | void | Append byte | WriteByte |
| 33 | 0x084 | int | void | Append 16-bit value | WriteShort |
| 34 | 0x088 | int | void | Append 32-bit value | WriteLong |
| 35 | 0x08C | float | void | Append raw 32-bit float | WriteFloat |
| 36 | 0x090 | string | void | Append string | WriteString |
| 37 | 0x094 | vec3 | void | Append position | WritePosition |
| 38 | 0x098 | vec3 | void | Append direction (normal index) | WriteDir |
| 39 | 0x09C | float | void | Append angle | WriteAngle |
| 40 | 0x0A0 | message buffer, int | void | Append byte to a caller-owned message buffer | — |
| 41 | 0x0A4 | message buffer, int | void | Append 16-bit value to a caller-owned buffer | — |
| 42 | 0x0A8 | message buffer, int | void | Append 32-bit value to a caller-owned buffer | — |
| 43 | 0x0AC | int byte, int client number | void | Append one byte to that client's reliable stream | — |
| 44 | 0x0B0 | pointer data, int length, int client number | void | Append raw bytes to that client's reliable stream | — |
| 45 | 0x0B4 | vec3 direction | int | Quantise a direction to the nearest normal-table index. Not used by the original game | — |
| 46 | 0x0B8 | entity, int length, int count, pointer data | void | Send the player-name-colour table to one client (reliable) | — |
| 47 | 0x0BC | string package name | void | Load and register a string package | — |
| 48 | 0x0C0 | entity or NULL, int string id, … (variadic arguments for the string) | void | Print a string-package string to one client or (NULL) all | — |
| 49 | 0x0C4 | entity or NULL, int string id, … (variadic) | void | Same, as an obituary message | — |
| 50 | 0x0C8 | byte buffer, int capacity, int string id, … (variadic) | int | Format a string-package string locally; returns length | — |
| 51 | 0x0CC | int string id | string | Raw text of a string-package string ("" if empty) | — |
| 52 | 0x0D0 | vec3 start, vec3 mins or NULL, vec3 maxs or NULL, vec3 end, entity passent, int content mask, trace-result pointer | bool | Sweep a box; writes the 56-byte trace result through the last argument; returns true when nothing was hit | trace (result by pointer) |
| 53 | 0x0D4 | same 7 as #52, then callback pointer | bool | Sweep with per-hit filter (see Behaviour) | — |
| 54 | 0x0D8 | vec3 point | int | Content flags at a point | pointcontents |
| 55 | 0x0DC | vec3 point | float | A per-BSP-leaf value for the leaf containing the point, or −1. Meaning UNVERIFIED | — |
| 56 | 0x0E0 | vec3 p1, vec3 p2 | bool | Potentially visible | inPVS |
| 57 | 0x0E4 | vec3 p1, vec3 p2 | bool | Potentially hearable. Not used by the original game | inPHS |
| 58 | 0x0E8 | int portal number, bool open | void | Open/close an area portal | SetAreaPortalState |
| 59 | 0x0EC | int area1, int area2 | bool | Areas connected. Not used by the original game | AreasConnected |
| 60 | 0x0F0 | — | object pointer | Get the engine's server-side Ghoul model system interface (C++ object, see Interfaces) | — |
| 61 | 0x0F4 | string path of a `.gpm` file | object pointer | Create a player-model description object from a `.gpm` file (C++ object, see Interfaces) | — |
| 62 | 0x0F8 | string folder, string file, pointer to receive buffer | int | Load `Ghoul/<folder>/<file>`, falling back to `Ghoul/comskin/<file>` (UNVERIFIED: return value = length) | — |
| 63 | 0x0FC | pointer to remaining-byte count, pointer to text cursor, line buffer | bool | Extract the next newline-terminated line from a text buffer, advancing cursor and count | — |
| 64 | 0x100 | string folder, string sequence-list name | void | Pre-load the `.gsq` list `Ghoul/<folder>/<name>.gsq` (or from `Ghoul/comskin/`) | — |
| 65 | 0x104 | string folder, string sequence-list name, object | bool | For each line of that `.gsq` list register `Ghoul/<folder>/<line>.GHL` with the object; a line starting with `#` includes another list; true if all succeeded | — |
| 66 | 0x108 | string folder, string list name, object A, object B | void | For each line of the list: look the name up in object A and, if found, register the result with object B; `#` lines include another list | — |
| 67 | 0x10C | **data**: pointer to an engine pointer variable | — | Not a function. The game stores into `*entry` a pointer it obtained from the player module (see Behaviour) | — |
| 68 | 0x110 | int index, string value | void | Set a configuration string | configstring |
| 69 | 0x114 | message buffer, pointer storage, int size | void | Initialise a caller-owned message buffer | — |
| 70 | 0x118 | message buffer | void | Clear a caller-owned message buffer | — |
| 71 | 0x11C | message buffer, pointer data, int length | void | Append raw bytes to a caller-owned buffer | — |
| 72 | 0x120 | string fmt, … (variadic) | void (does not return) | Abort the level with "Game Error: <text>" | error |
| 73 | 0x124 | string text | void | Write text to the dedicated-server console output | — |
| 74 | 0x128 | pointer importA, pointer importB, pointer importC, int is-client | pointer | Load `player.dll` and call its `GetPlayerServerAPI(importA, importC)` (is-client = 0) or `GetPlayerClientAPI(importA, importB)` (is-client ≠ 0); returns its result or NULL | — |
| 75 | 0x12C | int is-client | void | Release the player module for that side; the library is unloaded when neither side uses it | — |
| 76 | 0x130 | float min, float max | float | Random float in [min, max) — same function as SoF.exe export `flrand` | — |
| 77 | 0x134 | int min, int max | int | Random integer in [min, max] — SoF.exe export `irand` | — |
| 78 | 0x138 | entity | void | Link entity into the world | linkentity |
| 79 | 0x13C | entity | void | Unlink entity | unlinkentity |
| 80 | 0x140 | vec3 mins, vec3 maxs, entity-pointer array, int max count, int area type | int | Entities touching a box | BoxEdicts |
| 81 | 0x144 | player-movement block pointer | void | Run player movement | Pmove |
| 82 | 0x148 | int size, int tag | pointer | Allocate zero-filled tagged memory | TagMalloc |
| 83 | 0x14C | pointer | void | Free tagged memory — SoF.exe export `Z_Free` | TagFree |
| 84 | 0x150 | int tag | void | Free all memory with that tag | FreeTags |
| 85 | 0x154 | int chunk id, pointer data, int length | bool | Write one savegame chunk (format in Behaviour) | — |
| 86 | 0x158 | int chunk id, pointer buffer or NULL, int expected length or 0, pointer to receive allocated buffer or NULL | int | Read one savegame chunk; returns its length | — |
| 87 | 0x15C | string name, string default, int flags, callback or NULL | cvar pointer | Register / find a cvar; optional change callback (called with the cvar pointer) — SoF.exe export `Cvar_Get` | cvar (+1 arg) |
| 88 | 0x160 | string name, string value | cvar pointer (UNVERIFIED) | Set cvar — export `Cvar_Set` | cvar_set |
| 89 | 0x164 | string name, float value | void | Set cvar from a number — export `Cvar_SetValue` | — |
| 90 | 0x168 | string name, string value | cvar pointer (UNVERIFIED) | Set cvar ignoring write protection | cvar_forceset |
| 91 | 0x16C | int flag mask | string | Info string (`\key\value…`) of all cvars having any of the flag bits | — |
| 92 | 0x170 | string name | float | Numeric value of a cvar (0 if it does not exist) | — |
| 93 | 0x174 | string path, pointer to receive buffer or NULL, int flag | int | Load a whole file — export `FS_LoadFile`; returns length or −1 | — |
| 94 | 0x178 | pointer | void | Free a loaded file — export `FS_FreeFile` | — |
| 95 | 0x17C | — | string | Current game (writable) directory | — |
| 96 | 0x180 | string path | void | Create every directory along a file path | — |
| 97 | 0x184 | string path | bool | File exists in the game file system | — |
| 98 | 0x188 | string text | void | Append text to the command buffer | AddCommandString |

### Export table (game → engine): 28 entries, 112 (0x70) bytes

| Off | Type | Args | Ret | Meaning | Q2 |
|---|---|---|---|---|---|
| 0x00 | int | — | — | API version, must be 3 | apiversion |
| 0x04 | function | — | void | Initialise game (once per library load) | Init |
| 0x08 | function | — | void | Shut down game (before unload) | Shutdown |
| 0x0C | function | string map name, string entity text, string spawn point | void | Spawn the level's entities | SpawnEntities |
| 0x10 | function | bool autosave | void | Write game-wide state to the open savegame (as chunks) | WriteGame (no file name) |
| 0x14 | function | one int argument (ignored by the original game) | bool | Read game-wide state; returns the autosave flag that was saved | ReadGame |
| 0x18 | function | — | void | Write level state | WriteLevel (no file name) |
| 0x1C | function | — | void | Read level state | ReadLevel (no file name) |
| 0x20 | function | entity, string userinfo | bool | Pre-connect check, before the client slot is committed; false = reject | — |
| 0x24 | function | entity, string userinfo | bool | Client connect; false = reject (reason in userinfo key `rejmsg`) | ClientConnect |
| 0x28 | function | entity | void | Client entered the game | ClientBegin |
| 0x2C | function | entity, string userinfo, bool in-game | void | Userinfo changed; third argument 0 during connect, 1 for later changes | ClientUserinfoChanged (+1 arg) |
| 0x30 | function | entity | void | Client left (only for clients that had fully entered) | ClientDisconnect |
| 0x34 | function | entity | void | Client console command (arguments via import #9–#11) | ClientCommand |
| 0x38 | function | entity, user-command pointer | void | Client movement command | ClientThink |
| 0x3C | function | entity | void | Client is being dropped (called for every dropped client, before 0x30) | — |
| 0x40 | function | — | bool | Ask whether a manual save may proceed (consumes one allowed save); false = refuse | — |
| 0x44 | function | — | void | Report remaining saves (console command `savesleft`) | — |
| 0x48 | function | — | void | Publish mission statistics into cvars (console command `getgamestats`) | — |
| 0x4C | function | — | void | Refresh inventory cvars (console command `updateinven`) | — |
| 0x50 | function | — | string | Name of the current game type | — |
| 0x54 | function | — | int (low byte used) | Cinematic state: 0 = none, 1 and 2 = cinematic running (see Behaviour) | — |
| 0x58 | function | int (byte) state | void | Set cinematic state | — |
| 0x5C | function | int server frame number | float | Run one game frame; returns game time in seconds | RunFrame (+arg, +return) |
| 0x60 | pointer | — | — | Entity array | edicts |
| 0x64 | int | — | — | Size of one entity record in bytes (original: 1124 = 0x464) | edict_size |
| 0x68 | int | — | — | Number of entities in use | num_edicts |
| 0x6C | int | — | — | Capacity of the entity array | max_edicts |

The engine reads 0x60–0x68 continuously (entity ↔ number conversion: address = edicts + number × edict_size).
0x6C is written by the game; no engine read of it was found (static analysis).

### Objects and secondary tables crossing the boundary (inventory only)

| Obtained through | What | Shape | Notes |
|---|---|---|---|
| Import #60 | Ghoul model system, server instance | C++ object with **49** virtual methods | Same object for every call. The game calls it from `GetGameAPI` on (texture mip level from cvar `ghl_mip`, clamped 0–2) and twice per game frame. Method-level spec: TODO (separate spec) |
| Import #61 | Player-model description loaded from a `.gpm` file | C++ object with **19** virtual methods, 948 bytes | A new object per call |
| Import #65, #66 (arguments) | Ghoul model objects the game obtained earlier from the Ghoul system (engine-implemented, see SPEC-010) | C++ objects | The engine calls their methods 0, 7 and 8 (0-based). Amended 2026-10-21: these are not game-implemented objects; the only game-implemented Ghoul interface is the one-method event callback (SPEC-010) |
| Import #74 | Player module (`player.dll`) API | C function table; first field = version, the game requires **1** ("player is version %i, not %i") | The game builds the player module's import table itself, mostly from its own copy of the game imports; second field of the returned table is the pointer the game publishes through import #67 |
| Import #53 (argument) | Hit filter | plain C callback | see Behaviour |
| Import #87 (argument) | Cvar change callback | plain C callback taking the cvar pointer | |

## Behaviour

### Name indexes (imports #0–#3, #4, and the engine's own Ghoul-file and string-package indexes)
- Each name class owns a range of configuration strings (Constants). Index 0 of each range is never used; valid results
  are 1 … count−1, and 0 means "no name".
- A NULL or empty name gives 0. One leading `/` or `\` is ignored. Lookup is case-insensitive.
- An existing name returns its old index; otherwise the first empty slot is taken and the name stored there.
- When the range is full the engine prints "Items in Slot:" with the current list and drops the level with
  "*Index: <class> overflow" where class is `model`, `sound`, `effect`, `image`, `ghoulfile` or `sp`.
- A new name assigned while the level is running (not during loading) is sent to all clients at once as a
  configuration-string update (reliable).
- Import #4 clears an existing sound slot and, while the level is running, sends `svc_removeconfigstring` (35) with
  the configuration-string index to all clients.

### Printing and messages
- Message ids used by the import entries (server→client, see the network spec for payloads): print 11, nameprint 12,
  centerprint 17, captionprint 18, damagetexture 25, cinprintf 32, playernamecols 33, sp_print 34,
  removeconfigstring 35, sp_print_data_1 36, sp_print_data_2 37, welcomeprint 38, sp_print_obit 39,
  force_con_notify 40, sound 10, configstring 15.
- Per-client prints (#14, #15, #18) to an entity that is not a connected client drop the level with
  "cprintf to a non-client", "clprintf to a non-client", "cinprintf to a non-client" respectively. NULL entity for
  #14/#15 prints to the server console instead.
- #12 and #14 respect each client's message level (a client receives the text only if its level ≤ printlevel); #12
  reaches only clients that have entered the game and also echoes to the console when cvar `dedicated` is nonzero.
- Messages addressed to a single entity (#16, #17, #20, #48 with an entity, sound with `local` = 1) are silently not
  sent when the entity is flagged as having no network connection (bots). UNVERIFIED which entity flag; verify with a
  bot in a listen server.
- String ids for #48–#51 are 16-bit: high byte = string package, low byte = string within the package. An
  unregistered package drops the level with "String package not registered for ID %04x"; an undefined string with
  "String ID %04x not defined". Variadic arguments are formatted per the string package's own format rules (string
  package spec). With NULL entity, #48/#49 go to all clients that have entered the game.

### Multicast destinations (#28, #29)
0 = all, 1 = hearable set (PHS), 2 = visible set (PVS), 3/4/5 = the same three sent reliably. Any other value is a
fatal error "SV_Multicast: bad to:%i". After sending, the pending message buffer is cleared.

### Sounds (#22, #23)
- volume must be in [0, 1], attenuation in [0, 4], time offset in [0, 0.255]; otherwise fatal error
  ("SV_StartSound: volume = %f" / "attenuation = %f" / "timeofs = %f").
- #22 with a NULL entity does nothing.
- `local` ≠ 0: the sound message goes only to the player that owns the entity (unreliable), not to the hearable set.
- Channel value bit 8 disables hearable-set culling and bit 16 sends the sound reliably, as in Quake II; the low 3 bits
  are the channel number.

### Traces (#52, #53)
- The result is written through the pointer, 56 bytes. Known fields: float fraction at offset 4, hit-surface pointer at
  offset 0x28 (this is what #25 expects), hit-entity pointer at offset 0x30 (set to the world entity when the world
  was hit). The full layout differs from Quake II and belongs to the collision spec (UNVERIFIED beyond these fields).
- Return value: true exactly when fraction = 1.0.
- #53 behaves like #52, but every entity the sweep touches is collected, sorted by fraction, and offered to the
  callback nearest-first; the first hit the callback accepts (nonzero return) becomes the result. If none is accepted
  the world-only result stands. The original game uses it for shots (content mask 0x06000081).

### Savegame chunks (#85, #86)
- On-disk layout of one chunk: id (4 bytes), length L (4), checksum (4), data (L bytes), magic 0x1234ABCD (4) —
  little-endian, total L + 16 bytes. Checksum algorithm: see savegame spec (UNVERIFIED here).
- Chunk ids are four-character codes stored as 32-bit integers (e.g. the game's own state uses 0x47414D45, "GAME").
- #85 returns false if fewer than L + 16 bytes were written.
- #86 errors (level drop) when the next chunk's id differs ("Loaded chunk ID (%s) does not match requested chunk ID
  (%s)"), when an expected length ≠ 0 differs ("Loaded chunk (%s) has different length than requested"), on checksum
  mismatch ("Failed checksum check for chunk"), on wrong magic ("Bad savegame magic for chunk %s") or short read
  ("Error during loading chunk %s"). With a NULL buffer the data is read into new memory with tag 765, returned through
  the fourth argument; if that argument is NULL too, the data is read and discarded.

### Memory, files, cvars
- #82 returns zero-filled memory. The original game uses tag 765 for game-lifetime and 766 for level-lifetime memory
  and frees 766 at every level spawn and both at shutdown.
- #93 with a NULL buffer pointer returns only the length; otherwise the buffer gets the file plus two extra bytes
  `\n` and `\0`. Missing file → −1.
- #87: the callback is called with the cvar pointer when the cvar is created and when an existing cvar is registered
  again with a callback. Cvars with info flags reject names/values containing `\`, `"` or `;`.

### Random numbers (#76, #77)
Both share one 32-bit state s. Each call: s ← s × 214013 + 2531011 (mod 2³²), r = s >> 17 (0…32767).
flrand = min + (max − min) × r / 32768; irand = min + (((max − min + 1) × r) >> 15).

### Player module plumbing (#67, #74, #75)
- The original game loads the player module during `Init` with is-client = 0, checks version 1, calls the module's
  initialisation, and stores the module table's second field into the engine variable designated by #67.
- The engine treats that variable as a pointer to a 32-bit value; while running the console commands
  `getgamestats` and `updateinven` it saves that value, sets it to 0, calls export 0x48 / 0x4C, and restores it.
  Purpose UNVERIFIED (player-module spec).
- On shutdown the game sets the variable to NULL.

### How the engine drives the exports
- **Level start**: `SpawnEntities(map, entities, spawnpoint)`, then `RunFrame(0)` and `RunFrame(1)` while the server is
  still in its loading state (so new names do not generate network updates), then normal frames.
- **Every server frame** (100 ms): the frame counter is incremented and `RunFrame(counter)` is called (not while the
  game is paused with a single client, as in Quake II); the returned float is the game time (the original returns
  counter × 0.1). The original game uses small frame numbers (0, 1) to decide when to precache Ghoul data.
- **Connect**: export 0x20 is asked first; false → rejection (message from userinfo `rejmsg`). Then the slot is
  committed and `ClientConnect` called; false → rejection. Then `ClientUserinfoChanged(ent, userinfo, 0)`.
  Later userinfo changes call it with 1.
- **Enter game**: `ClientBegin` after the client confirmed loading (`begin`).
- **Drop / timeout**: export 0x3C is always called; `ClientDisconnect` only if the client had entered the game.
- **Commands**: `ClientCommand` for client commands the engine does not handle itself, only while a level is running.
- **Movement**: `ClientThink` is called only while the cinematic state (export 0x54) is 0. While a cinematic runs,
  a user command with button bit 0x40 set requests skipping it: the engine sets the state to 1 (unless it is already
  2) and sets cvar `sv_jumpcinematic` to "2". If instead the player's movement type is 3 (believed: dead, UNVERIFIED)
  and button bit 0x40 is set, the engine sets cvar `sv_restartlevel` to "1".
- **Cinematic fast-forward**: while `sv_jumpcinematic` is nonzero and the cinematic state is nonzero, the engine runs
  game frames back-to-back inside one server frame (yielding after a bounded batch); at the start it sets cvar
  `s_disable` to 1 and the cinematic state to 2, and mirrors the state into the first client's player state
  (UNVERIFIED which field). It stops as soon as the game reports state 0.
- **Save** (console `save <name>`): refused with "Cannot save during cinematic." when export 0x54 ≠ 0; otherwise
  export 0x40 must return true, then `WriteGame(autosave)` and — for non-autosaves — `WriteLevel` are called between
  the engine's own chunks.
- **Load**: `ReadGame` is called; if it returns false (not an autosave) the engine restores its own frame/config
  chunks and calls `ReadLevel`.
- Export 0x50's string appears in the console `status` output ("Gametype: %s") and in the server info as key
  `gametype`.
- The original game, in its export 0x40, prints string-package messages about remaining saves and returns false when
  none are left; export 0x44 sets cvar `menu_savesleft` and prints "Saves left : %s".

## Constants

| Name | Value |
|---|---|
| Game API version | 3 |
| Player module API version expected by the game | 1 |
| Import table size | 99 entries, 396 bytes |
| Export table size | 28 fields, 112 bytes |
| Entity record size (original game) | 1124 bytes |
| Memory tags used by the game | 765 (game), 766 (level) |
| Configuration strings | indexes 0 … 3554 (3555 total), 64 bytes each; out of range → "configstring: bad index %i" |
| Model names | configuration strings 35 … 290 (base 35, 256 slots) |
| Sound names | 291 … 646 (base 291, 356 slots) |
| Effect names | 647 … 902 (base 647, 256 slots) |
| Image names | 903 … 1158 (base 903, 256 slots) |
| Per-player info (written by the game) | 1414 + client number |
| String packages | 1463 … 1492 (base 1463, 30 slots) |
| Ghoul model files | 1497 … 3544 (base 1497, 2048 slots) |
| Chunk magic | 0x1234ABCD |
| Sound limits | volume 0–1, attenuation 0–4, time offset 0–0.255 |
| Random generator | multiplier 214013, increment 2531011, output bits 17–31 |
| Exported symbols | `GetGameAPI` (gamex86.dll); `GetPlayerServerAPI`, `GetPlayerClientAPI` (player.dll) |

## Test vectors / how to verify
1. Load the original `gamex86.dll` in a stub host that implements only the 99-entry table with logging functions:
   `GetGameAPI` must return a table whose first int is 3 and whose 0x64 field reads 1124; during the call it must invoke
   only entries at offsets 0x0F0 and 0x15C (cvar `ghl_mip`) plus the returned Ghoul object's method 38.
2. Same stub: after `Init`, field 0x60 is non-NULL, 0x6C equals cvar `maxentities`, 0x68 equals `maxclients` + 1.
3. Logging stub over a full single-player level and a deathmatch round: the original game never calls entries 8, 18,
   19, 20, 24, 31, 35, 39, 45, 57, 59 (static analysis shows no reference to them in the game and they are not
   forwarded to the player module; entry 31 shares its implementation with 32 in the engine anyway).
4. Index behaviour: calling #0 with "models/x.ghl", "/models/x.ghl" and "MODELS/X.GHL" returns the same index; "" returns 0.
5. Ban check: with cvar `filterban` = 1 (default) and an address filter covering 10.0.0.0/8, #5("10.1.2.3:27910") ≠ 0
   and the original game rejects the connection with userinfo `rejmsg` = "rejected_banned" (observe with a client).
   With `filterban` = 0 the result is inverted (only listed addresses may connect), as in Quake II.
6. Sound validation: #22 with volume 1.5 must abort with "SV_StartSound: volume = 1.500000".
7. Random: with a known state, two consecutive irand(0, 9) results follow the formula above (verify by instrumenting
   the original via a debugger, or compare distributions).
8. Save while a cinematic is playing → console shows "Cannot save during cinematic."

## Open questions
- Import #55: what the per-leaf value is (it is precomputed per BSP leaf; the game compares it with 1200 to scale an AI
  parameter). Verify by reading the BSP lump that feeds it.
- Import #62 return value (believed: file length or −1).
- Import #27 second argument: passed through to the screen refresh; the game always passes 1. Meaning UNVERIFIED.
- Import #46 third argument: written as one byte before the data; believed to be an entry count (verify against the
  client's handling of message 33).
- Import #67: what the published 32-bit value represents (player-module spec).
- Export 0x3C: the original game only resets one per-client field (its purpose is unknown); an engine must still call it
  for every drop.
- Export 0x14 argument: the engine passes one value; the original game ignores it. Value UNVERIFIED (believed 0).
- Trace result, user command, player-movement block and entity layouts: separate specs.
- Ghoul interface (49 methods) and player-model description object (19 methods): separate specs.
