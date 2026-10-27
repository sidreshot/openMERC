# SPEC-201: Network messages — primitive encodings, server→client (svc) and client→server (clc) messages
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules; byte-level test vectors still to be confirmed by packet capture
Describes: SoF.exe (client parser and server writers), gamex86.dll (game-written messages), version 1.07f
Depends on: SPEC-200 (transport), SPEC-202 (frames and deltas), SPEC-203 (connection sequence, configstrings)

## Purpose
Defines every message that can appear inside a netchan payload: the primitive field encodings, the numeric id
of every server→client and client→server message, and each message's payload and observable effect.

A netchan payload (SPEC-200) is a concatenation of messages. Each message is one id byte followed by its payload.
The receiver reads messages until the payload is exhausted. There is no per-message length (except where a
message carries its own length field). An unknown id is fatal for the receiver.

## Primitive encodings
All multi-byte values are little-endian. Reading past the end of the payload yields -1 for integer reads (and
-1 × scale for scaled reads); the parsers treat that as end of message.

| Primitive | Wire size | Encoding | Decoding |
|---|---|---|---|
| char | 1 | signed 8-bit | sign-extended |
| byte | 1 | unsigned 8-bit | 0..255 |
| short | 2 | signed 16-bit | sign-extended |
| ushort | 2 | unsigned 16-bit (new in SoF; used e.g. for string-package ids) | 0..65535 |
| long | 4 | 32-bit two's complement | |
| float | 4 | IEEE-754 single | |
| string | n+1 | bytes followed by one NUL | the reader keeps at most 2047 characters; a longer string is not fully consumed, so senders must keep strings below 2048 bytes |
| stringline | n+1 | as string | reading additionally stops at a newline (used only for out-of-band text) |
| coord | 2 | short = value × 8, truncated toward zero | short / 8 (1/8-unit precision, range ±4096) |
| pos | 6 | three coords x, y, z | |
| angle | 1 | byte = low 8 bits of (degrees × 256/360, truncated toward zero) | signed byte × 360/256 (−180..+178.59375) |
| angle16 | 2 | short = low 16 bits of (degrees × 65536/360, truncated toward zero) | signed short × 360/65536 |
| dir | 1 | index (0..249) of the entry of the direction table (Appendix A) with the largest dot product with the vector; ties keep the lower index; a zero vector, a vector with no positive dot product, or "no vector" gives 0 | table entry; an index above 249 is fatal ("MSF_ReadDir: out of range") |
| data(n) | n | raw bytes | |

The scale constants are single-precision floats: 8.0, 0.71111113 (256/360), 182.04445 (65536/360) for encoding;
0.125, 1.40625, 0.0054931640625 for decoding.

Differences from Quake II: the direction table has 250 entries with different vectors (Quake II: 162); the
ushort primitive is new.

## Server → client message ids
| Id | Name | Payload (summary) | Sent by 1.07f servers |
|---|---|---|---|
| 0 | svc_bad | — (fatal for the client) | no |
| 1 | svc_temp_entity | byte type + per-type payload (§1) | yes (game) |
| 2 | svc_layout | string | no (layout arrives via string-package prints) |
| 3 | svc_UNUSED | — (fatal: "Illegible server message") | no |
| 4 | svc_sound_info | byte mask + up to 4 bytes | no |
| 5 | svc_effect | effect record (§5) | yes (game) |
| 6 | svc_equip | byte kind [+ lists] (parsed and discarded) | no |
| 7 | svc_nop | — | no |
| 8 | svc_disconnect | ushort string-package id | yes |
| 9 | svc_reconnect | — | yes |
| 10 | svc_sound | sound record (§10) | yes |
| 11 | svc_print | byte level, string | yes |
| 12 | svc_nameprint | byte sender slot, byte team flag, string | yes |
| 13 | svc_stufftext | string | yes |
| 14 | svc_serverdata | see SPEC-203 | yes |
| 15 | svc_configstring | see SPEC-203 | yes |
| 16 | svc_spawnbaseline | see SPEC-203 / SPEC-202 | yes |
| 17 | svc_centerprint | string | yes |
| 18 | svc_captionprint | short string-package id | engine can send it; the game never does |
| 19 | svc_download | see SPEC-203 | yes |
| 20 | svc_playerinfo | only inside a frame, SPEC-202 | yes |
| 21 | svc_packetentities | only inside a frame, SPEC-202 | yes |
| 22 | svc_deltapacketentities | only inside a frame; never sent | no |
| 23 | svc_frame | SPEC-202 | yes |
| 24 | svc_culledEvent | byte event, byte parameter, byte distance/4, dir | yes |
| 25 | svc_damagetexture | short surface, byte stage | yes |
| 26 | svc_ghoulreliable | short length + ghoul bit stream (§26) | yes |
| 27 | svc_ghoulunreliable | short length + ghoul bit stream (§26) | yes |
| 28 | svc_ric | byte count + inventory events | yes (player module) |
| 29 | svc_restart_predn | byte | yes (game) |
| 30 | svc_rebuild_pred_inv | 640 bytes | yes (game + player module) |
| 31 | svc_countdown | long seconds | yes (game) |
| 32 | svc_cinprintf | short x, short y, byte speed, string | engine can send it; the game never does |
| 33 | svc_playernamecols | byte count + runs | yes (game) |
| 34 | svc_sp_print | short string id | yes |
| 35 | svc_removeconfigstring | short index (SPEC-203) | yes |
| 36 | svc_sp_print_data_1 | short id, byte length, data | yes |
| 37 | svc_sp_print_data_2 | short id, short length, data | yes |
| 38 | svc_welcomeprint | — | yes |
| 39 | svc_sp_print_obit | short id, byte length, data | yes |
| 40 | svc_force_con_notify | — | yes |

The names are the ones the client prints with `cl_shownet 2`. Ids 20–22 outside an svc_frame are fatal for the
client ("Out of place frame data"); any id above 40 is fatal ("CL_ParseServerMessage: Illegible server message").
Messages marked "no" are still accepted by 1.07f clients with the payload shown.

### Delivery (multicast) rules
Messages built by the game are sent with one of these recipient modes:
| Value | Recipients | Stream |
|---|---|---|
| 0 ALL | every spawned client | unreliable |
| 1 PHS | spawned clients whose potentially-hearable set contains the origin | unreliable |
| 2 PVS | spawned clients whose potentially-visible set contains the origin | unreliable |
| 3 ALL_R | every connected or spawned client | reliable |
| 4 PHS_R | connected/spawned clients in the PHS | reliable |
| 5 PVS_R | connected/spawned clients in the PVS | reliable |
Any other value aborts the server ("SV_Multicast: bad to:%i"). A variant excludes one client (used for effects
the shooting player predicts locally). The listening point of a client is its player origin, or its remote-camera
origin while a remote view is active (UNVERIFIED). A message can also be sent to a single client, reliable or
unreliable. Unreliable data that does not fit a packet is dropped; a client whose reliable stream overflows is
disconnected with string-package message 4/12 ("You overflowed").

Order inside one server→client datagram: reliable part (including at most one svc_ghoulreliable), then
svc_culledEvent records, then svc_frame with its svc_playerinfo and svc_packetentities, then at most one
svc_ghoulunreliable, then the client's queued unreliable messages (temp entities, effects, sounds, …)
(UNVERIFIED that queued messages come last — check with a capture).

### 1. svc_temp_entity
| Field | Type |
|---|---|
| type | byte, 0..64 |
| payload | per type, below |

The 1.07f client accepts types 0..64 and does no range check; a type above 64 crashes it, so a
server must never send one. Sub-structures used below:
- pos = pos-short (SPEC-201 primitives); dir = direction byte; "unit×1000" = a pos whose components were
  multiplied by 1000 before encoding (decode × 0.001; only values within ±4.095 survive); "unit×2048" = a pos of a
  unit vector multiplied by 2048 (decode × 1/2048); "unit×32" = a pos of a unit vector × 32 (decode × 1/32).
- impact = byte kind, then: kind 0 (world surface): pos point, unit×1000 normal; kind 1 (world point, normal
  (0,0,1)): pos point; kind 2 (brush entity): pos offset from the entity origin rotated by −entity yaw,
  unit×1000 normal (also rotated), short entity number, byte material; kind 3 (other entity): pos point,
  unit×1000 horizontal offset (point − entity origin, z = 0, not normalised; values beyond ±4.095 wrap), byte
  material (35 = flesh for players/monsters). Materials above 42 are rejected by the client ("Surface out of
  range!!!") and the effect is dropped.

| Type | Payload | Effect | Sent by 1.07f game |
|---|---|---|---|
| 0 | pos, dir | white sparks + spark sound | yes |
| 1 | pos | nothing (payload read and ignored) | yes |
| 2 | pos, byte dx, byte dy, byte dz (each − 128), byte size | spray of puffs along the direction | no |
| 3 | impact | blood splat decal on a wall | yes |
| 4 | pos, long (ignored) | blood pool decal on the floor below | no |
| 5 | — | nothing | no |
| 6 | impact, unit×1000 shot direction, byte power, byte style (0 no mark, 1 bullet hole, 2 knife slash), byte debris flag | weapon impact (mark, material sound, material effect) | yes |
| 7 | as 6 | weapon impact on a surface that just changed damage stage (throws 6 debris chunks) | yes |
| 8 | pos start, byte length, dir | electric arc | yes |
| 9 | byte distance, unit×1000 direction, unit×1000 (ignored), byte (ignored), byte (ignored) | bullet fly-by sound at view origin + direction × distance (sent to one client, unreliable) | yes |
| 10 | pos, dir | orange/blue sparks | yes |
| 11 | pos, short colour (bits 8–11 R, 4–7 G, 0–3 B, each × 15), byte size | one falling coloured drop | no |
| 12 | pos | electric burst with random arcs | no |
| 13 | impact | explosion ground flash | yes |
| 14 | pos, dir | sparks (as type 0) | yes |
| 15 | pos, long (ignored) | scorch decal on the floor | yes |
| 16 | — | global lightning flash (sent to all, reliable) | yes |
| 17 | pos, long (ignored) | nothing visible | yes |
| 18 | pos, dir, byte count, byte size (1..3), byte chunk type (0..34), byte extent x, byte extent y, byte extent z, byte material | debris chunks + break sound | yes |
| 19 | impact | explosion debris and sparks | yes |
| 20 | pos | flash-grenade explosion | yes |
| 21 | pos | small shock puff | no |
| 22 | — | nothing | no |
| 23 | pos, byte material | body thud sound | no |
| 24 | pos, pos direction (not scaled), byte n, n × (short entity, byte hits) | shotgun pellet marks for the pellets that missed (12 − Σhits) | no |
| 25 | pos start, pos end | bubble trail | no |
| 26 | pos, byte speed | smoke burst | yes |
| 27 | pos start, pos end, long colour (bytes R,G,B,A), long duration (× 500 ms) | debug line | yes (AI debug) |
| 28 | pos, long (ignored) | floor decal "pipeleft" | no |
| 29 | impact | white scorch decal | no |
| 30 | impact | black decal | no |
| 31 | pos, dir | lit sparks | yes |
| 32 | pos, dir, byte (ignored; servers send 3) | knife / throwing-star impact | yes |
| 33 | pos, byte (ignored) | explosion with scattered marks | no |
| 34 | pos muzzle, unit×32 direction | shotgun blast cosmetic pellet marks (10 random pellets) | yes |
| 35 | short ghoul instance, short bolt, short entity, byte delay (× 40 ms) | blood drip from a model bolt | no |
| 36 | short, short, short, short, short | broken effect | no |
| 37 | short ghoul instance, short (ignored) | smoke from limb-cap bolts | no |
| 38 | pos | white-smoke explosion | no |
| 39 | short (ignored) | broken effect at world origin | no |
| 40 | pos, byte (ignored) | C4 explosion | no |
| 41 | as 35 | broken variant of 35 | no |
| 42 | short, short, short | no visible effect | no |
| 43 | pos, pos step | blood stream | no |
| 44 | pos, byte magnitude | large explosion with sound | yes |
| 45 | pos, byte R, byte G, byte B, byte A | coloured smoke puff | yes |
| 46 | pos, byte size (× 0.01), byte sound (0 = silent) | small explosion | yes |
| 47 | pos, short radius | shock ring (PVS) | yes |
| 48 | pos, short count (0 = random 5..10) | blowing paper | yes |
| 49 | pos, dir | spark shower | yes |
| 50 | pos, pos unit direction, short speed (low byte used), short count (low byte used; 0 = random 1..4) | soda cans | yes |
| 51 | pos | nothing | no |
| 52 | pos, short count | dust puffs (PVS) | yes |
| 53 | pos, long (ignored) | puddle decal | yes |
| 54 | pos | ricochet sound | yes |
| 55 | pos, byte size (× 0.01), byte sound | medium explosion | yes |
| 56 | pos, byte size (× 0.01), byte mode (0 particles only, 2 sound only, other both) | large explosion | yes |
| 57 | pos, pos | short debug line | no |
| 58 | pos | phone hit sound | yes |
| 59 | pos | trash-can lid sound | yes |
| 60 | byte effect index, byte second effect index (ignored), byte shooter entity, pos start, pos end | multiplayer bullet: hit an entity or nothing | yes |
| 61 | byte effect index, byte (ignored), byte shooter entity, pos start, unit×2048 direction | multiplayer bullet: hit the world, impact class 4 | yes |
| 62 | as 61 | as 61, impact class 7 | yes |
| 63 | byte effect index, byte shooter entity, pos start, unit×2048 direction | multiplayer projectile weapon fired | yes |
| 64 | pos mins, pos maxs, byte R, byte G, byte B | debug box | yes (AI debug) |

Effect indices in types 60–63 are effect configstring slots (SPEC-203). For types 60–63 the client draws nothing
for its own shots (it predicts them) and replaces the start point with its own view origin where applicable.
A map entity can make the game send any type with a single pos payload; only types whose payload is exactly one pos
(1, 12, 20, 21, 38, 51, 54, 58, 59) parse correctly that way (no shipped map uses it).

### 2. svc_layout
string: replaces the client's layout program (at most 1023 characters). Not sent by 1.07f servers.

### 4. svc_sound_info
| Field | Type | Present when |
|---|---|---|
| mask | byte | always; any bit other than 0x0F is fatal ("Invalid soundinfo type") |
| music intensity | byte | mask & 1 |
| music designer value | byte | mask & 2 |
| general ambient set | byte | mask & 4 |
| sound processing type | byte | mask & 8 |
Not sent by 1.07f servers (the values travel in the player state, SPEC-202).

### 5. svc_effect
| Field | Type | Present when |
|---|---|---|
| effect | byte | always; effect configstring slot (1..255) |
| flags | byte | always |
| origin | pos | flags & 0x01 |
| entity | short | flags & 0x02 and not 0x01: the effect follows that entity's position and orientation |
| owner entity | short | flags & 0x04 and neither 0x01 nor 0x02: the effect is attached to a bolt of that entity's model |
| ghoul instance | short | (bolt case) and flags & 0x08: use this ghoul instance instead of the entity's own |
| bolt | short | bolt case; 0 = the client picks a random bolt of the model |
| extension | block | flags & 0x80 |
flags & 0x40 (bolt case): use the bolt's axes in reversed order (direction reversed). flags 0x10 and 0x20 are never
set and never read. With none of 0x01/0x02/0x04 the effect plays at (0,0,0).

Extension block:
| Field | Type | Present when |
|---|---|---|
| ext flags | byte | always (non-zero) |
| scale | short ÷ 128 | ext & 0x01 |
| byte parameter | byte | ext & 0x02 (never produced) |
| second position | pos | ext & 0x04 (beam end points, emitter positions) |
| direction | unit×2048 pos | ext & 0x08 |
| vector 3 | pos | ext & 0x10 (never produced) |
| vector 4 | pos | ext & 0x20 (never produced) |
| float 2 | short ÷ 128 | ext & 0x40 (never produced) |
| float 3 | byte | ext & 0x80 (never produced) |
The client keeps at most 256 running effects; when full the message is consumed and ignored. Typical delivery:
PHS, unreliable, possibly excluding the player who caused it.

### 6. svc_equip
byte kind; if kind = 1: three lists, each = byte count, then count × (string, long). The 1.07f client reads and
discards it. Not sent by 1.07f servers.

### 8. svc_disconnect
ushort string-package id (§34) whose text explains the reason; the client stores the text in cvar
`menu_dc_reason` and disconnects. Ids used by 1.07f servers (package 4 = "general"): 0x040A "You were kicked!",
0x040B "You timed out", 0x040C "You overflowed", 0x040D "You disconnected", 0x040F "Unknown command character",
0x0410 "Server Quit!" (texts from the shipped string package; UNVERIFIED wording).
When the server shuts down or changes map it sends, outside the reliable mechanism and twice, a packet holding
svc_print (level 2, the message) followed by svc_reconnect (map change) or svc_disconnect with id 0x0410 (quit).

### 9. svc_reconnect
No payload. The client prints "Server disconnected, reconnecting", aborts any download and restarts the
connection handshake (SPEC-203).

### 10. svc_sound
| Field | Type | Present when |
|---|---|---|
| flags | byte | always: 0x01 volume, 0x02 attenuation, 0x04 position, 0x08 entity/channel, 0x10 time offset, 0x20 sound index ≥ 256 |
| sound index | byte | always; low 8 bits of the sound slot (CS_SOUNDS + n); add 256 if flags & 0x20 |
| volume | byte | flags & 0x01; volume × 255 truncated (default 1.0 when absent) |
| attenuation | byte | flags & 0x02; attenuation × 64 truncated (default 1.0) |
| time offset | byte | flags & 0x10; offset in ms (0..255) |
| entity/channel | short | flags & 0x08; entity number × 8 + channel (0..7); the client rejects entity numbers above 1024 |
| position | pos | flags & 0x04 |
Server behaviour: flag 0x08 is always set; volume must be 0..1, attenuation 0..4, offset 0..0.255 s. Delivery is
PHS (unreliable) unless the attenuation is 0 or the channel requests "no PHS" (then ALL); a "reliable" channel bit
selects the reliable variants. For PHS delivery the position is omitted for clients that can see the origin and
included (with flag 0x04 set) for others; once the flag has been set for one client it stays set for all later
clients of the same sound (original quirk). A "local" sound goes only to the entity's own client, without position.
Differences from Quake II: flag 0x20 and the 9-bit index; Quake II's entity/channel packing is the same.

### 11. svc_print
byte level (0 low, 1 medium, 2 high, 3 chat), string. Level 3 plays "misc/talk.wav". The server only sends a print
to clients whose userinfo `msg` level is ≤ the print level.

### 12. svc_nameprint
| Field | Type |
|---|---|
| sender | byte, client slot of the speaker |
| team | byte; 0 = public ("name : text"), non-zero = team chat ("(name) : text") |
| text | string |
The client takes the name from the sender's player info, ignores senders on its `ignore` list, plays
"misc/talk.wav" and prints the line.

### 13. svc_stufftext
string, executed as console text by the client. Used for the connection sequence (SPEC-203), map changes
("changing\n", "reconnect\n") and by the game (e.g. "spectator 0\n").

### 17. svc_centerprint
string, shown centred (up to 10 lines; time = `scr_centertime` (default 2.5 s) + 0.2 s per space + 0.8 s per line).

### 18. svc_captionprint
short string-package id; the client centre-prints the text in caption style (not affected by `cl_subtitles`).
Invalid ids are ignored.

### 24. svc_culledEvent
| Field | Type | Meaning |
|---|---|---|
| event | byte | first event id of an entity that was not sent this frame (1..23) |
| parameter | byte | its parameter |
| distance | byte | trunc(distance × 0.25) (distance < 1000) |
| direction | dir | normalised (viewer − entity) |
Sent in the datagram before svc_frame. The 1.07f client reconstructs a position (view origin + direction ×
distance × 4) and processes the event as an entity event without an entity; no 1.07f event produces a visible or audible
effect in that case. Event ids of 24 and above must not be sent.

### 25. svc_damagetexture
short surface index (map texture-info record), byte new damage stage. The client switches that surface to its
next damage texture. The server never sends it alone: it is placed in front of the weapon-impact temp entity that
caused the damage, in the same PHS multicast.

### 26/27. svc_ghoulreliable / svc_ghoulunreliable
| Field | Type |
|---|---|
| length | short, number of bytes that follow |
| payload | length bytes: a ghoul bit stream (Appendix B) |
The client aborts ("Bad Ghoul reliable packet" / "Bad Ghoul unreliable packet") when the length runs past the
message. 26 travels in the reliable stream (during connection by the `ghoulstrings` loop, SPEC-203; in game
whenever more than 16 bytes of the half-datagram budget are free); 27 travels in the datagram right after
svc_frame and is decoded against that frame's number and game time. At most one of each per packet. In network
games 27 is limited to `sv_max_ghoul_mp` bytes (default 200, minimum 100).

### 28. svc_ric
byte count, then count events. Event = header byte h (event id = h & 0x0F; value size class = (h >> 4) & 3),
then the value: class 0 byte, 1 short, 2 short + byte (value = short | byte << 16), 3 long. Ids 0..4 carry a
value, id 5 carries none, ids ≥ 6 carry none. The events update the client's predicted inventory (meanings belong
to the player-module spec; UNVERIFIED). Reliable, to one client.

### 29. svc_restart_predn
byte: prediction restart number (wraps at 256). The client rebuilds its weapon-prediction state; predicted firing
resumes when the player state's prediction resync number (SPEC-202) equals this value.

### 30. svc_rebuild_pred_inv
640 raw bytes: complete predicted-inventory snapshot (layout belongs to the player-module spec). Sent reliably
after svc_restart_predn when a client begins.

### 31. svc_countdown
long: remaining seconds (0 = hide). The client shows "Countdown : %d" below 60 seconds and "Countdown : %d:%02d"
otherwise; it does not count down by itself. Sent unreliable to all once per second by the game's countdown
entity.

### 32. svc_cinprintf
short x, short y, byte speed, string (≤ 1023 characters): typewriter-style text at (x, y), shown for
strlen × 0.1 s + `cinem_centertime` (default 2.5) seconds.

### 33. svc_playernamecols
byte count = number of player slots described; then runs until count slots are covered: byte b; if b & 0x80: byte
first, byte last, slots first..last get b & 0x7F; else byte slot, that slot gets b & 0x7F. The value selects how
the overhead name of that player is drawn (UNVERIFIED meaning: team colour). The game sends only changed slots,
reliable, to each viewing client. Example: slots 0..3 = 2 and slot 7 = 5 → `21 05 82 00 03 05 07`.

### 34/36/37/39. String-package prints
A string id is a 16-bit value (package id << 8) | string index, both defined by the `.sp` files in `strip/`
(e.g. package 0 = dm_generic, 4 = general, 200 = menu_generic). Packages are announced in configstrings
1464..1492 and loaded by the client from its own files.
| Id | Payload |
|---|---|
| 34 svc_sp_print | short string id |
| 36 svc_sp_print_data_1 | short string id, byte length (1..255), data |
| 37 svc_sp_print_data_2 | short string id, short length (≥ 256; client buffer 1024), data |
| 39 svc_sp_print_obit | short string id, byte length (0..255), data (always this form) |
Data = the format arguments of the package string, packed in format order: `%d` → 4-byte little-endian integer,
`%hd`/`%hu` → 2 bytes, `%p` → 1 byte player entity number (client slot + 1; the client substitutes the player's
name, or "!@#$%^&*()" for an invalid slot), `%s` → NUL-terminated string. Other conversions consume nothing.
Display depends on the string's flags in the package: credit (0x08) → credits picture; captioned (0x04) →
caption, only when cvar `cl_subtitles` ≠ 0 or the always-print flag (0x40) is set; centered (0x01) → centre print;
typeamatic (0x02) → typewriter print at (0, 104), speed 5; layout (0x10) → text starting with "*" replaces the
layout (a lone "*" clears it), other text is appended to the layout with a space; otherwise → console. The
first matching flag in that order wins. For svc 39 the client cvar `cl_obit_filter` (default 0) applies: 0 shows
all, 1 shows only obituaries without a `%p` argument or whose first or second `%p` is the local player, 2 or more
shows none. Unknown ids are ignored by the client. Servers choose 34 when there are no data, 36 for 1..255 bytes,
37 above; obituaries always use 39. Examples: `27 01 00 01 03` (package 0 string 1 with player entity 3);
`24 2C 00 0C 00 00 20 00 01 00 0A 00 32 00 03 00` (package 0 string 44 with six 16-bit arguments).

### 35. svc_removeconfigstring
short index; see SPEC-203.

### 38. svc_welcomeprint
No payload. The client centre-prints configstring 1493 (the welcome message) once, then empties its local copy.

### 40. svc_force_con_notify
No payload. The client clears its centre-print, typewriter-print and credit timers so console notify lines show.

## Client → server message ids
| Id | Name | Payload | Server behaviour |
|---|---|---|---|
| 0 | clc_bad | — | treated as unknown: client dropped with string-package message 4/15 ("Unknown command character") |
| 1 | clc_nop | — | ignored |
| 2 | clc_move | checksum byte, long last frame, user-command bit stream (SPEC-202 §6) | only the first move per packet is used |
| 3 | clc_userinfo | string (≤ 511 characters kept) | replaces the client's userinfo and re-evaluates name, rate, msg (SPEC-203) |
| 4 | clc_stringcmd | string | executed as a client command (SPEC-203 table) or passed to the game; only the first 7 per packet are executed, later ones are read and ignored |
Any other id drops the client (4/15). Reading past the end of a client message is fatal for that client
("SV_ReadClientMessage: bad read", dropped with 4/14). The client sends reliable messages (userinfo, stringcmd)
first and the move message as the unreliable part of the same packet. Ids and payloads are the same as Quake II
except the move message contents.

Server-side client commands (stringcmd) handled by the engine: `new`, `configstrings`, `baselines`, `begin`,
`nextserver`, `disconnect`, `download`, `nextdl`, `ghoulstrings`, `info`, `sv_precache`; anything else goes to the
game module once the level is running.

## Constants
| Name | Value |
|---|---|
| Number of svc ids | 41 (0..40) |
| Direction table size | 250 |
| Max string read length | 2047 characters |
| Temp-entity types | 65 (0..64) |
| Running effects on the client | 256 |
| Ghoul dictionary entries | 3713 |
| Ghoul string maximum | 250 characters |
| Unreliable ghoul budget (`sv_max_ghoul_mp`) | 200 bytes default, 100 minimum |

## Test vectors / how to verify
1. svc_sound for entity 5, channel 2, sound slot 300 (configstring 591), default volume/attenuation, listener
   that can see the origin: `0A 28 2C 2A 00`.
2. svc_disconnect "You timed out": `08 0B 04`.
3. svc_countdown 90 s: `1F 5A 00 00 00` → "Countdown : 1:30".
4. svc_playernamecols: `21 05 82 00 03 05 07` (slots 0..3 = 2, slot 7 = 5).
5. svc_sp_print_obit, package 0 string 1, player entity 3: `27 01 00 01 03`.
6. svc_effect at (8, 16, −4) for effect slot E with no extension: `05 E 01 40 00 80 00 E0 FF`.
7. Direction byte: (0.219625, 0, 0.975584) encodes to 0; index 250 received → client aborts.
8. Angle byte: 90° → 0x40; 270° → 0xC0 (decodes to −90°). Angle16: 90° → 0x4000.
9. Coord: 100.0 → `20 03`; −50.5 → `6C FE`.
10. Capture check: with `cl_shownet 2` an original client prints the message names of every received message;
    compare with the ids above. Temp-entity payloads can be checked by sending each type from a test server and
    confirming that the client keeps parsing the rest of the packet.

## Open questions
- Meaning of the second count in the svc_playernamecols values and of several svc_effect extension fields that
  no 1.07f code produces.
- Exact placement of queued unreliable messages relative to svc_frame inside a datagram (capture).
- Meanings of most ghoul instance-state groups (Appendix B.6) and of the object-definition lists (B.5); the
  instance-state field list is confirmed from the reader only.
- Contents of svc_ric events and of the 640-byte svc_rebuild_pred_inv snapshot (player-module spec).
- Which entities the server flag that suppresses unicasts belongs to (probably bots).

## Appendix A: direction table

Removed from the public specification on 2026-10-26 (user decision: the open tree contains no data copied
from the original). The table is kept only in the private record `docs/private/SPEC-201-appendix-A.md`.
OpenMerc does not use it: the 250 directions are generated at run time as the vertices of a frequency-5 geodesic icosphere without its poles, in the order described in docs/impl/LEGAL-tables.md (they agree with the original's table to within 1e-6, but are not bit-identical). See docs/DEVIATIONS.md, "Fixed tables".

## Appendix B: ghoul bit stream (payload of svc 26 / 27)
Status: framing, primitives, string coding, the reliable section layout and the unreliable record layout are
confirmed from both sides; the field list of the instance state (B.6) is confirmed from the reader only and the
meaning of most fields is UNVERIFIED.

### B.1 Bit order
Least-significant-bit first: stream bit i is bit (i mod 8) of byte (i div 8); an n-bit value is written starting
with its least significant bit. The payload length is the number of bits rounded up to bytes, padded with zero
bits. A reader that runs out of data treats the rest as zero and the client aborts ("Ghoul :StringTable
underflowed", "Reliable underflowed", "Delta underflowed"). The sender writes records whole: a record that would
exceed the byte budget is moved to the next message.

### B.2 Variable-length integers
| Name | Encoding |
|---|---|
| id | 1 bit present (0 → value 0; 0 also terminates every list); if present: 2 bits n, then 4n+4 bits value (4, 8, 12 or 16 bits); the sender uses the smallest n that fits |
| time | 1 bit present (0 → 0); 1 bit sign (1 = negative); 2 bits n; 8n+8 bits magnitude |
| frame offset | 1 bit present (0 → 0); 2 bits n; 8n+8 bits value |

### B.3 Ghoul strings (at most 250 characters)
1 bit kind. Kind 1 = dictionary: an id (B.2) giving the index into the ghoul dictionary (Appendix C). Kind 0 =
literal: 1 bit present, 2 bits m, 2m+2 bits L (block length in bytes, 0..255; smallest m with L < 4^(m+1)), then
L bytes (8L bits) of encoded block. Encoded block: if its first byte is 0xFF, the remaining L−1 bytes are the raw
characters. Otherwise the first byte is the number of unused bits at the end of the block and the following bits
(same bit order) are Huffman codes of the characters; number of code bits = 8L − 8 − first byte. The sender uses
the raw form when the Huffman form would not be shorter than the string plus one byte.

Huffman code: built once from the 256 weights of Appendix D (single-precision floats). Start with 256 leaf nodes
in slots 0..255. Repeat 255 times: find, scanning slots 0..255 in order and skipping empty slots, the node with
the smallest weight (on equal weight the earlier slot wins) and the node with the second smallest weight (same
rule); create a node whose branch 0 is the second-smallest node and branch 1 the smallest node, with weight =
their sum computed in single precision; put it into the smallest node's slot and empty the second-smallest node's
slot. A character's code is the sequence of branches from the root to its leaf; the first bit in the stream is the
branch taken at the root.

### B.4 Reliable payload (svc 26)
Four consecutive lists, each terminated by an id of 0:
| Section | Record |
|---|---|
| A string table additions | id (index of the new string; strings are numbered consecutively from 1, index 0 = "" is implicit; the receiver appends in order and ignores the value) + ghoul string |
| B object definitions | object definition (B.5); an existing object with the same id is replaced |
| C removed objects | object id |
| D removed instances | instance id |
When a message fills up inside A or B, the sender closes all remaining lists with terminators and continues in the
next svc 26.

### B.5 Object definition
| # | Field |
|---|---|
| 1 | object id (id) |
| 2..5 | four lists, each: count (id), then count string-table indices coded as differences: first = d0, next = d + previous + 1 (strictly increasing). Tentative meanings: model/sequence files, parts, other names, materials (UNVERIFIED) |
| 6 | fifth list: count (id), then count × {1 bit "has extra"; if 1: id extra; difference-coded index} (tentative: skins with optional material) |
| 7 | baseline instance state (B.6) relative to the default state, time base 0 |
Derived widths used by instance states: w0 = bits(count of list 1), w1 = bits(count of list 2), w2 = bits(count of
list 4), w3 = bits(count of list 5), where bits(c) is the smallest b ≥ 0 with c < 2^b.

### B.6 Unreliable payload (svc 27)
List of instance updates terminated by instance id 0:
| Field | Encoding |
|---|---|
| instance id | id |
| object id | id (the receiver creates the instance from this object if it does not exist; object id 0 or an unknown object is fatal) |
| base | frame offset (B.2): 0 = coded against the object's baseline state; k > 0 = coded against the state this client received for frame (current frame − k) |
| state | instance state, below |
The receiver stores each decoded state under the frame number of the svc_frame in the same packet and discards
states older than the base; more than 1000 unacknowledged stored states is fatal ("More than 1000 unacked
deltas from server"). The server's base choice follows the frame the client last acknowledged in clc_move (a
negative acknowledgement resets every instance to full states); only instances whose state changed are sent.

Instance state (reader field order; "group" = 1 presence bit + data, absent = unchanged):
| # | Bits | Tentative meaning |
|---|---|---|
| 1 | 1 | flag (always sent) |
| 2 | group: 4 bits | small value |
| 3 | 1 | flag A (always sent; enables group 9) |
| 4 | 1 | flag B (always sent) |
| 5 | group (only if w1 > 0): w1 bits | index into list 2 |
| 6 | group (only if w0 > 0): w0 bits | index into list 1 |
| 7 | group: 2 bits | small value |
| 8 | group: time t1, time t2 | T1 = t1/120 + frame game time; T2 = t2/120 + T1 (seconds) |
| 9 | (only if flag A) group: 1 bit "infinite" (value 1e30) else 32-bit float | |
| 10 | group: 4 × (1 presence bit + 8 bits) | four optional bytes |
| 11 | group: transform | see below |
| 12 | group: run-length change mask over a per-instance byte array: until the array end, 1 bit mode; mode 0: next ≤ 4 entries each XOR 1 bit; mode 1: 1 bit v + 5 bits c, next c+6 entries XOR v | |
| 13 | group: list X then list Y, each repeated {id key (0 ends), w1-bit value}; X = add/override, Y = remove | |
| 14 | group: while next bit = 1: {w2 bits, w1 bits, w3 bits} (add); then while next bit = 1: {w2 bits, w1 bits} (remove) | |
Transform: 1 bit + 32-bit packed rotation (else identity); 1 bit + 32-bit float scale (else 1.0); 1 bit
translation present, then per axis x, y, z: 1 bit present, 1 bit kind, kind 0 = 32-bit float, kind 1 = 11 bits v
→ v/16 − 64.
Packed rotation (32 bits): bits 0–7 = index d into the rotation direction table = the six axis vectors
(+x, −x, +y, −y, +z, −z) followed by the 250 entries of Appendix A; bits 8–11, 12–15, 16–19 = corrections
q → q × 0.018666666 − 0.14 added to the x, y, z of that direction, result normalised = first axis (encoder:
q = round((delta + 0.14) × 53.57143) clamped to 0..15); bits 20–30 = roll a → a × 2π/2047 about the first axis;
bit 31 = negate the third axis.

## Appendix C: ghoul string dictionary

Removed from the public specification on 2026-10-26 (user decision: the open tree contains no data copied
from the original). The table is kept only in the private record `docs/private/SPEC-201-appendix-C.md`.
OpenMerc does not use it: OpenMerc sends no dictionary codes; every ghoul string is coded (Appendix B.3). See docs/DEVIATIONS.md, "Fixed tables".

## Appendix D: ghoul Huffman weights

Removed from the public specification on 2026-10-26 (user decision: the open tree contains no data copied
from the original). The table is kept only in the private record `docs/private/SPEC-201-appendix-D.md`.
OpenMerc does not use it: the ghoul string coder uses OpenMerc's own character weights (docs/impl/LEGAL-tables.md). See docs/DEVIATIONS.md, "Fixed tables".
