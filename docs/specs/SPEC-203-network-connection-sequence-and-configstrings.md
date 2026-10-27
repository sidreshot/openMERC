# SPEC-203: Network connection sequence, configstrings, baselines, downloads
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules; byte-level test vectors still to be confirmed by packet capture
Describes: SoF.exe (client and server), gamex86.dll (configstring contents), version 1.07f
Depends on: SPEC-200 (transport, out-of-band messages), SPEC-201 (message ids and primitive encodings), SPEC-202 (entity delta encoding)

## Purpose
Describes, at byte level, everything that happens between a client's first out-of-band packet and its first
in-game frame, the configstring table (index layout, wire encoding including the built-in string dictionary),
entity baselines, the file download protocol, map change / reconnect, and the userinfo / serverinfo keys the
engine itself interprets. A reimplementation that follows this document can join an original 1.07f server and
accept original 1.07f clients.

Conventions: "stringcmd" means a client-to-server message `clc_stringcmd` (id 4) carrying one NUL-terminated
string (SPEC-201). "stufftext" means a server-to-client `svc_stufftext` (id 13) carrying one NUL-terminated string
that the client executes as console text. `<sc>` is the server's spawn count (a 32-bit integer that the server
increments every time it loads a map). All multi-byte integers are little-endian. "Reliable" means written to the
netchan reliable stream (SPEC-200).

## Interface / layout

### Protocol version
| Item | Value |
|---|---|
| Protocol version (in `connect` and `svc_serverdata`) | 33 (0x21) |
| Server cvar `protocol` | "33", flags serverinfo + read-only |
| Server cvar `version` | "1.07fx86F", flags serverinfo + read-only |
| Version text in out-of-band rejection | "Server is version 1.07f." (see SPEC-200) |

### svc_serverdata (id 14) payload
| # | Type | Meaning |
|---|---|---|
| 1 | long | protocol version; the client aborts with "Server returned version %i, not %i" unless it is 33 |
| 2 | long | spawn count `<sc>` |
| 3 | byte | attract-loop / demo flag (non-zero = demo playback, the client will not send input) |
| 4 | byte | value of the server's `deathmatch` cvar, truncated to an integer; the client copies it into its own `deathmatch` cvar |
| 5 | string | game directory (empty = base game); the client copies at most 63 characters and switches its `game` cvar if it differs |
| 6 | short | player number (client slot 0..maxclients-1). A value of -1 makes the client skip printing the level name and keep its refresh state (Quake II cinematic convention); 1.07f servers always send the slot number (UNVERIFIED for cinematic levels) |
| 7 | string | level name, i.e. configstring 0; the client prints it when the player number is not -1 |

Difference from Quake II: field 4 (the deathmatch byte) is new; everything else is in Quake II order.

### svc_configstring (id 15) payload
| # | Type | Meaning |
|---|---|---|
| 1 | short | configstring index, 0..3554; the client aborts with "configstring > MAX_CONFIGSTRINGS" outside that range |
| 2 | short | dictionary code (see below) |
| 3 | string | present only when the dictionary code is negative: the literal configstring text |

Dictionary coding (new in SoF; Quake II sends the string directly). Both sides share the fixed, sorted
dictionary of 1927 strings in Appendix A. The sender looks the configstring text up in the dictionary with an
exact, case-sensitive, byte-wise comparison. If found, it sends the entry's index (0..1926) and no string. If
not found it sends -1 (0xFFFF) followed by the literal string. The receiver treats any negative code as "a
literal string follows" and any non-negative code as a dictionary index. The receiver stores at most 64 bytes
per configstring (63 characters plus NUL).

### svc_removeconfigstring (id 35) payload
| # | Type | Meaning |
|---|---|---|
| 1 | short | configstring index 0..3554 |

The client empties the configstring. If the index is in the sound range (291..646) and the sound was
registered, the client also frees that sound. The server sends it (reliable, to all clients) when the game
removes a sound from the sound table.

### svc_spawnbaseline (id 16) payload
An entity-number header followed by an entity delta from the all-zero entity state, exactly as in a
packet-entities record (SPEC-202): bit-mask header (1..4 bytes), entity number (byte, or short when the 16-bit
number bit is set), then the fields selected by the bit mask. The client stores the result as the baseline of
that entity number.

### Configstring index layout
MAX_CONFIGSTRINGS = 3555; each configstring holds up to 63 characters. Index 0 is "first" in every table below
where a table has a slot 0 that is never used by the index allocator (models, sounds, effects, images, string
packages, ghoul files: slot 0 of each table is reserved and the allocator hands out 1..N-1).

| Index | Count | Name used here | Content | Set by |
|---|---|---|---|---|
| 0 | 1 | CS_NAME | level name (worldspawn "message"); also the map name before the game sets it | engine (map name) then game |
| 1 | 1 | CS_CDTRACK | CD track number, decimal | game |
| 2 | 1 | CS_SKY | sky name, "none" when the map has none | game |
| 3 | 1 | CS_SKYAXIS | "%f %f %f" sky rotation axis | game |
| 4 | 1 | CS_SKYROTATE | "%f" sky rotation speed | game |
| 5 | 1 | — | "%f %f %f": the client uses it as the RGB colour of light style 13 (UNVERIFIED meaning; set by a game entity) | game |
| 6 | 1 | CS_MAPCHECKSUM | 44-character map check string, see "Map check string" | engine |
| 7 | 1 | — | not observed in use | — |
| 8, 9 | 2 | — | game-mode state strings for team 1 / team 2 (observed values "0", "d", "h"; capture-the-flag / control modes) UNVERIFIED | game |
| 10 | 1 | — | game-mode string (observed, set by a deathmatch mode; read by the client's player-label drawing) UNVERIFIED | game |
| 11 | 1 | — | not observed in use | — |
| 12 | 1 | CS_AMBIENTSET | ambient sound set file, worldspawn key "ambientSet", default "sound/sound.ams" | game |
| 13 | 1 | CS_MUSICSET | music set name, worldspawn key "musicSet" (client resolves "sound/music/<name>.dms") | game |
| 14 | 1 | CS_TERRAINSET | terrain set name, worldspawn key "terrainset" (client resolves "env/<name>.trn") | game |
| 15 | 1 | CS_SCREENEFFECT | worldspawn key "screeneffect", decimal | game |
| 16 | 1 | — | string set by the game at level spawn; the client passes it to its sound/music system at precache (UNVERIFIED meaning) | game |
| 17..30 | 14 | — | not observed in use | — |
| 31 | 1 | CS_MAXCLIENTS | maxclients, decimal | game |
| 32, 33 | 2 | — | "1"/"0" game-mode flags set by the deathmatch mode object (UNVERIFIED meaning; read by the client's HUD / player labels) | game |
| 34 | 1 | — | "1" while the "assassin" deathmatch mode is active (UNVERIFIED) | game |
| 35 | 256 | CS_MODELS | model names; 36 = "maps/<map>.bsp"; 37.. = "*1", "*2", … inline brush models | engine + game |
| 291 | 356 | CS_SOUNDS | sound names (relative to "sound/") | engine on game request |
| 647 | 256 | CS_EFFECTS | effect names (client resolves "effects/<name>.eft") | engine on game request |
| 903 | 256 | CS_IMAGES | image names (client resolves "<name>.m32") | engine on game request |
| 1159 | 256 | CS_LIGHTS | light style strings ('a'..'z' brightness sequences) | game |
| 1415 | 32 | CS_PLAYERSKINS | per-client info string, one per client slot (see "Player info configstring") | game |
| 1447 | 16 | CS_DMICONS | deathmatch icon names (client resolves "pics/dmicons/<name>") | game |
| 1463 | 30 | CS_STRINGPACKAGES | string-package names (client resolves "strip/<name>.sp") | engine on game request |
| 1493 | 1 | CS_WELCOME | server welcome message (cvar `welcome_mess`) | engine |
| 1494..1496 | 3 | — | not observed in use | — |
| 1497 | 2048 | CS_GHOULFILES | ghoul model file names (paths used as-is) | engine on game request |
| 3545..3554 | 10 | — | not observed in use | — |

Index allocation (engine, when the game asks for an index by name): slot 0 of each range is never used; the
lookup compares names case-insensitively; a leading "/" or "\" is stripped; a new name goes into the first empty
slot (a removed sound frees its slot); when the range is full the server aborts with "*Index: <type> overflow"
where <type> is one of model, sound, effect, image, ghoulfile, sp. When the server is past the loading state,
every configstring change is sent immediately as `svc_configstring` on the reliable stream of every client.

Single-player note: when the server's `deathmatch` cvar is 0 the server sends only configstrings 0..1496 during
the connection sequence (the ghoul-file range is omitted); with deathmatch non-zero it sends 0..3554.

### Map check string (configstring 6)
A 44-character string of uppercase hexadecimal digits, two per BSP lump, for the 22 lumps of the map file in
lump-directory order. For each lump: compute the Quake II "block checksum" of the lump's bytes (MD4 digest of the
bytes, then the exclusive-or of its four 32-bit little-endian words), take the low 8 bits, write them as two
uppercase hex digits. The BSP header is 184 bytes (identifier, version 46, 22 lump entries of offset+length).
The server computes the string when it loads the map. The client computes it from its own copy of the map after
precaching and, if it differs from configstring 6, disconnects with a string-package error (id 0x415, "map
differs"-type message; text UNVERIFIED).

### Player info configstring (1415 + slot)
Format `name\model\skin\…` (backslash separated); the client's default for an empty slot is
`deadmeat\noteam\mullins\0`. The exact field list is game-defined (written by the game when a player's userinfo
changes); UNVERIFIED beyond the first three fields: player name, team/model name, skin name, then a numeric
field.

## Behaviour

### 1. Out-of-band handshake (summary; exact texts in SPEC-200)
1. Client → server (out-of-band): `getchallenge <nonce>
`, repeated every 3 s.
2. Server → client: `challenge <number>`.
3. Client → server: `connect 33 <qport> <challenge> <nonce> 3.14 "<userinfo>"
` (SPEC-200).
4. Server → client: `client_connect` (or a rejection). On `client_connect` the client creates its netchan
   (SPEC-200, client side sends qport) and immediately queues the reliable stringcmd `new`.
   A second `client_connect` received while already connected is ignored ("Dup connect received.  Ignored.").

### 2. Server-driven signon (all server messages on the reliable stream of that client)
Each step is triggered by a stringcmd from the client. The server checks that the client is still in the
"connected, not yet spawned" state (otherwise prints "<cmd> not valid -- already spawned" and ignores it) and that
the `<sc>` argument equals its current spawn count (otherwise prints "SV_<Step>_f from different level" and
restarts the client at step `new`). Batches stop as soon as the client's pending reliable message reaches half
of the maximum datagram size (SPEC-200: 700 bytes in network games, 8192 bytes in single-player); the server then
asks the client to come back for the rest.

| Client stringcmd | Server response |
|---|---|
| `new` | `svc_serverdata`; then, if a level is running (not a demo/cinematic), stufftext `cmd configstrings <sc> 0\n` |
| `configstrings <sc> <start>` | `svc_configstring` for every non-empty index from `<start>` up to the limit (1497 in single-player, 3555 in deathmatch) until the half-datagram mark; then stufftext `cmd configstrings <sc> <next>\n` if not finished, else `cmd ghoulstrings <sc> 0\n` (or `cmd baselines <sc> 0\n` when playing a demo) |
| `ghoulstrings <sc> <n>` | one `svc_ghoulreliable` chunk (short length + bytes, length chosen to fill the remaining half-packet budget plus 8 bytes) carrying the server's ghoul model registry for this client; then stufftext `cmd ghoulstrings <sc> <n+1>\n` if more data remains, else `cmd baselines <sc> 0\n` |
| `baselines <sc> <start>` | `svc_spawnbaseline` for every entity number from `<start>` up to 1023 whose baseline has a model index, a sound, an effects value, or a non-empty effect slot (SPEC-202); then stufftext `cmd baselines <sc> <next>\n` if stopped early, else `precache <sc>\n` |
| (client runs `precache <sc>`, downloads, then sends) `sv_precache <sc>` | stufftext `cmd begin <sc> \n` (note the space before the newline). In demo mode the spawn-count check is skipped. |
| `begin <sc>` | client becomes "spawned"; the game's client-begin entry point runs; from the next server frame on the client receives `svc_frame` messages (SPEC-202) |

`cmd <text>` executed by the client sends `<text>` (everything after "cmd ") as a stringcmd; the client refuses
with "Can't \"cmd\", not connected" when not connected. `sv_precache` is a client console command that is
forwarded to the server the same way.

### 3. Client precache step (`precache <sc>`)
On `precache <sc>` the client remembers `<sc>`. If it is connected to a remote server and its `allow_download`
cvar is non-zero, it runs the download check sequence below; otherwise it goes straight to loading. Loading: it
loads the map named by configstring 36, computes its map check string and compares it with configstring 6
(mismatch → disconnect with string-package message 0x415); for remote servers it also checks that every ghoul
file configstring (1498..) ending in ".gpm" exists locally (missing → disconnect with string-package message
0x414); it registers models, sounds, images, effects, string packages and player infos; then it executes
console text `sv_precache <sc>\n`, which reaches the server as stringcmd `sv_precache <sc>`.

Download check order (each item is requested only if the matching client `allow_download_*` cvar is non-zero
and the file is not already available locally):
1. Map (allow_download_maps): the configstring-36 file; the same name with extension ".eal"; "strip/menu_<map>.sp"; "bot/wpfiles/<map>.wrs".
2. Ambient set (allow_download_sounds): configstring 12 and the files it references.
3. Music set (allow_download_sounds): "sound/music/<cs13>.dms" and the files it references.
4. Terrain (allow_download_maps): "env/<cs14>.trn" and the files it references.
5. Sounds (allow_download_sounds): "sound/<name>" for configstrings 292..646 (names starting with "*" skipped).
6. Effects (allow_download_maps): "effects/<name>.eft" for configstrings 648..902 and the files each references.
7. Images: "<name>.m32" for configstrings 904..1158.
8. Deathmatch icons: "pics/dmicons/<name>.m32" for configstrings 1448..1462.
9. String packages (allow_download_stringpackage): "strip/<name>.sp" for configstrings 1464..1492 and files referenced.
10. Ghoul files: the raw names in configstrings 1498..3544.
11. Map check string comparison (see above).
12. Sky (allow_download_maps): "pics/sky/<cs2>.txt" and the images it lists.
13. Map textures (allow_download_maps): "textures/<name>.m32" for every texture of the map.
Each list stops at the first empty configstring. The order matters only for user-visible progress; the server
serves any permitted file in any order.

### 4. File download protocol
Client side, per file:
- If the name contains "..": prints "Refusing to download a path with ..", skipped.
- If a partial file "<gamedir>/<name without extension>.tmp" exists, the client opens it, seeks to its end and
  sends stringcmd `download <name> <offset>` (resume); otherwise it sends `download <name>`.
- On each `svc_download` it appends the chunk to the temporary file, sets `cl_download_percent` and, while the
  percentage is not 100, sends stringcmd `nextdl`. At 100 it renames the temporary file to the final name and
  continues with the next item. A size of -1 means "file not available": the client prints "File not found"
  and moves on.

`svc_download` (id 19) payload:
| # | Type | Meaning |
|---|---|---|
| 1 | short | chunk size in bytes, 0..1024; -1 = refused / not found |
| 2 | byte | percentage complete (bytes sent so far × 100 / file size, integer division) |
| 3 | bytes | chunk data, exactly "chunk size" bytes (absent when size is -1) |

Server side, `download <name> [<offset>]`: refused (answered with size -1, percent 0) when the name contains
"..", starts with "." or "/", or contains no "/"; names starting with "strip/" need server cvars
`allow_download_stringpackage` and `allow_download`; "sound/" needs `allow_download_sounds` and
`allow_download`; "maps/" and "bot/" need `allow_download_maps` and `allow_download`; any other path containing
"/" is served without consulting `allow_download` (observed behaviour; differs from Quake II). A "maps/" file is
also refused when it comes from inside a pak file. If the file cannot be loaded it is refused. Otherwise the
offset is clamped to the file size and the server sends the first chunk at once; each `nextdl` sends the next
chunk of at most 1024 bytes. Server defaults: `allow_download` 0, all `allow_download_*` 1. Client defaults:
all 1.

### 5. Map change and reconnect
- On a map change the server sends stufftext `changing\n` to every client (reliable), flushes, loads the new
  map (spawn count + 1), clears every client's "last connect" time so the reconnect limit does not apply, then
  sends stufftext `reconnect\n` to every client (reliable).
- Client `changing`: while not downloading, shows the loading plaque and stays connected.
- Client `reconnect` (console command or stufftext) while connected: the client drops back to the "connecting"
  state and immediately restarts the out-of-band handshake (`getchallenge`, `connect`, …) — unlike Quake II,
  which reuses the existing netchan and sends `new`. UNVERIFIED: confirm with a packet capture of a map change.
- `svc_reconnect` (id 9, no payload): the client prints "Server disconnected, reconnecting", closes any
  download and restarts the handshake the same way.
- `svc_disconnect` (id 8): one unsigned short string-package id giving the reason; the client stores the text
  in `menu_dc_reason` and disconnects ("Server disconnected").

### 6. Timeouts
The client declares "Server connection timed out." when no packet has arrived for `cl_timeout` seconds (default
120) on more than 5 consecutive client frames while connected. The server's `timeout` cvar (default 180) and
`zombietime` (default 2) behave as in Quake II (UNVERIFIED).

## Userinfo keys interpreted by the engine
The client builds its userinfo from all cvars flagged "userinfo". Defaults in 1.07f:

| Key | Default | Engine / protocol use |
|---|---|---|
| name | "unnamed" | player name; server keeps up to 31 characters |
| skin | "mullins" | player appearance (game-defined) |
| teamname | "mullins" | player team/model (game-defined) |
| rate | "3000" | server-side bandwidth limit, clamped to 100..15000 bytes/s; missing key → 5000 |
| msg | "1" | message level filter for prints (Quake II semantics) |
| fov | "95" | game-defined |
| bestweap | "safe" | game-defined (weapon auto-switch policy) |
| team_red_blue | "0" | game-defined (team choice) |
| spectator | "0" | spectator request (game-defined) |
| password | "" | server join password (checked during connect, see SPEC-200) |
| spectator_password | "" | spectator join password (see SPEC-200) |
| allow_download_models | "1" | sent to the server (also archived) |
| cl_violence | "0" | client violence setting; the server compares it with its own violence setting during connect (SPEC-200) |

The server adds the key `ip` (client address "a.b.c.d:port") to the userinfo on connect. The game module may add `rejmsg` when it refuses a connection (SPEC-200).

## Serverinfo keys (cvars flagged "serverinfo")
| Key | Default | Notes |
|---|---|---|
| protocol | "33" | read-only |
| version | "1.07fx86F" | read-only |
| hostname | "noname" | |
| maxclients | "1" | latched |
| deathmatch | "0" | latched (not serverinfo itself, but sent in svc_serverdata) |
| dmflags | "0" | latched |
| fraglimit, timelimit | "0" | |
| cheats | "0" | |
| skill | "2" | latched |
| game | "" | latched |
| sv_violence | "0" | |
| sv_suicidepenalty | "2" | |
| g_movescale | "1" | |
| ctf_loops | "3" | |
| control_limit | "300" | |
| control_bonus | "1" | |
| mapname | current map | set by the engine on map load |
| user | "./user" | read-only |

## Constants
| Name | Value |
|---|---|
| Protocol version | 33 |
| MAX_CONFIGSTRINGS | 3555 |
| Configstring text length | 64 bytes including NUL |
| Configstring dictionary entries | 1927 |
| MAX_EDICTS (baselines) | 1024 |
| MAX_CLIENTS (player-info slots) | 32 |
| Signon batch threshold | half the maximum datagram: 700 bytes (network game), 8192 bytes (single-player) |
| Download chunk | 1024 bytes |
| Map check string | 44 characters (22 lumps × 2 hex digits) |
| BSP version checked by the map check string | 46 |

## Test vectors / how to verify
1. Configstring with dictionary hit: setting index 37 to the text "*1" (dictionary entry 0) produces the
   bytes `0F 25 00 00 00` (svc 15, index 37, code 0, no string).
2. Configstring with dictionary miss: index 0 = "Test" → `0F 00 00 FF FF 54 65 73 74 00`.
3. Download refusal: stringcmd `download ../x` → `13 FF FF 00`.
4. serverdata for spawn count 5, deathmatch 1, base game, player 0, level "Arena":
   `0E 21 00 00 00 05 00 00 00 00 01 00 00 00 41 72 65 6E 61 00`.
5. Verify the signon order and the half-datagram (700-byte) batching with a packet capture of an original client joining an
   original dedicated server (cl_shownet 1 also prints the svc names).
6. Verify the map check string by running `developer 1` on an original server (it prints "Map checkstring: …")
   and comparing with the algorithm above applied to the same .bsp.

## Open questions
- Exact meaning of configstrings 5, 8, 9, 10, 16, 32, 33, 34 (game-defined; their indices and value formats
  above are confirmed, their meaning is not).
- Whether anything uses 7, 11, 17..30, 1494..1496, 3545..3554.
- Field list of the player-info configstring after the third field.
- Texts of string-package messages 0x414 / 0x415 (they live in the game's string package data).

## Appendix A: configstring dictionary (index, text)

Removed from the public specification on 2026-10-26 (user decision: the open tree contains no data copied
from the original). The table is kept only in the private record `docs/private/SPEC-203-appendix-A.md`.
OpenMerc does not use it: OpenMerc's server sends every configstring as a literal (code -1); its client refuses dictionary codes. See docs/DEVIATIONS.md, "Fixed tables".
