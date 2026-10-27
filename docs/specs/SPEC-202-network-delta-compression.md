# SPEC-202: Network delta compression — frames, entity states, player state, user commands
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules; byte-level test vectors still to be confirmed by packet capture
Describes: SoF.exe (server writers and client readers), gamex86.dll (meaning of game-set fields), version 1.07f
Depends on: SPEC-200 (netchan), SPEC-201 (message ids, primitive encodings, direction table), SPEC-203 (baselines)

## Purpose
Specifies the snapshot ("frame") messages the server sends every server frame and the movement commands the
client sends every client frame: the transmitted entity-state and player-state structures, every bit flag, field
order and encoding, the delta rules (what is compared, which reference state is used), baselines, area bits, the
bit-packed user command format and the move-message checksum. All three Quake II ideas (frame acknowledgement,
per-field delta bits, baselines) are kept, but almost every layout differs from Quake II.

Conventions: "pos-short" = coord encoding (value × 8 truncated, read back × 1/8); "angle byte" and "angle16" as in
SPEC-201. "Truncated" = rounded toward zero. Bit numbers count from 0 = least significant.

## Interface / layout

### 1. Server frame message (svc_frame, id 23)
| # | Type | Field |
|---|---|---|
| 1 | long | server frame number (10 frames per second; server time = frame × 100 ms) |
| 2 | long | delta frame: the frame number the following data is delta-coded against, or −1 for "no delta" |
| 3 | float | game time in seconds (the game module's level time = frame × 0.1 in practice); new in SoF |
| 4 | byte | area-bits length n (client buffer holds 32 bytes; senders must not exceed 32) |
| 5 | n bytes | area visibility bits (bit a set = area a is visible from the viewer's area) |
| 6 | byte 20 + player state | svc_playerinfo, mandatory (else the client aborts "CL_ParseFrame: not playerinfo") |
| 7 | byte 21 + entity list | svc_packetentities, mandatory (else "CL_ParseFrame: not packetentities") |

Differences from Quake II: field 3 is new; Quake II's "suppress count" byte is absent. The 1.07f server always uses
id 21 (svc_packetentities) and never id 22 (svc_deltapacketentities); the client rejects 20, 21 and 22 when they
are not inside a frame ("Out of place frame data").

### 2. Entity state as transmitted
The client and server keep 116-byte entity states; only the fields below exist on the wire.

| Field | Meaning | Wire encoding |
|---|---|---|
| number | entity number 1..1023 (0 terminates a list) | byte, or short when header bit 24 is set |
| origin x, y, z | position | pos-short each |
| angles pitch, yaw, roll | orientation | angle byte each |
| yaw offset | extra yaw added by the client when the secondary model index is non-zero (UNVERIFIED meaning) | angle byte |
| modelindex | model configstring index (CS_MODELS+n); 255 = player: the client takes model and skin from the player-info configstring of slot `skinnum` | byte |
| modelindex2 | secondary model index; when non-zero and not 255 the client draws this model instead and negates pitch and roll (UNVERIFIED meaning) | byte |
| frame | animation frame; for ghoul-model entities the server replaces it, per client, with the ghoul instance id | byte or short |
| skinnum | skin number; for players the client slot number | byte, short or long (unsigned value) |
| effects | effect bits (see 2.4) | byte, short or long |
| renderfx | render flag bits (see 2.4) | byte, short or long |
| solid | packed bounding box (see 2.3) | long |
| sound | looping sound index 0..511 (CS_SOUNDS+n), or an ambient-set id when effects bit 0x100 is set | byte + header bit 22 for values ≥ 256 |
| sound data | bits 3..7 = volume (value & 0xF8, 0..248), bits 0..2 = attenuation class | byte, sent only together with sound |
| events | up to three (event, parameter) pairs; event bits 0..6 = event id, bit 7 = "another pair follows" | see field order |
| effect slots | four slots of three 16-bit values: effect id (0 = empty slot), ghoul bolt id (0 = entity origin), scale × 16 (0 = default) | 16-bit change mask + changed values |

#### 2.1 Entity header bits
The header is 1 to 4 bytes: byte 0 = bits 0..7; if bit 7 is set, byte 1 = bits 8..15 follows; if bit 15 is set,
byte 2 = bits 16..23 follows; if bit 23 is set, byte 3 = bits 24..31 follows. The writer sets the continuation
bits automatically: any bit in 24..31 → bits 7, 15 and 23 set; else any bit in 16..23 → bits 7 and 15; else any
bit in 8..15 → bit 7.

| Bit | Value | Meaning |
|---|---|---|
| 0 | 0x00000001 | origin x present |
| 1 | 0x00000002 | origin y present |
| 2 | 0x00000004 | yaw present |
| 3 | 0x00000008 | roll present |
| 4 | 0x00000010 | pitch present |
| 5 | 0x00000020 | origin z present |
| 6 | 0x00000040 | events present |
| 7 | 0x00000080 | header continues (byte 1) |
| 8 | 0x00000100 | renderfx 16-bit (with bit 10: 32-bit) |
| 9 | 0x00000200 | modelindex2 present |
| 10 | 0x00000400 | renderfx 8-bit (with bit 8: 32-bit) |
| 11 | 0x00000800 | REMOVE: entity leaves the frame (only with the number) |
| 12 | 0x00001000 | frame 16-bit |
| 13 | 0x00002000 | effect slots present |
| 14 | 0x00004000 | effects 16-bit (with bit 16: 32-bit) |
| 15 | 0x00008000 | header continues (byte 2) |
| 16 | 0x00010000 | effects 8-bit (with bit 14: 32-bit) |
| 17 | 0x00020000 | frame 8-bit |
| 18 | 0x00040000 | solid present |
| 19 | 0x00080000 | skinnum 8-bit (with bit 25: 32-bit) |
| 20 | 0x00100000 | modelindex present |
| 21 | 0x00200000 | sound present (sound byte + sound-data byte) |
| 22 | 0x00400000 | sound index has bit 8 set (add 256 to the sound byte) |
| 23 | 0x00800000 | header continues (byte 3) |
| 24 | 0x01000000 | entity number is 16-bit |
| 25 | 0x02000000 | skinnum 16-bit (with bit 19: 32-bit) |
| 26 | 0x04000000 | yaw offset present |
| 27..31 | | never set |

#### 2.2 Field order after the header
1. entity number (byte, or short if bit 24)
2. modelindex (bit 20) — byte
3. modelindex2 (bit 9) — byte
4. frame — byte (bit 17) or short (bit 12)
5. skinnum — bits 19+25: long; bit 19 only: byte; bit 25 only: short
6. effects — bits 16+14: long; 16 only: byte; 14 only: short
7. renderfx — bits 10+8: long; 10 only: byte; 8 only: short
8. origin x (bit 0), origin y (bit 1), origin z (bit 5) — pos-short each
9. pitch (bit 4), yaw (bit 2), yaw offset (bit 26), roll (bit 3) — angle byte each, in exactly this order
10. sound (bit 21): sound byte (low 8 bits of the index), then sound-data byte; the receiver adds 256 if bit 22 is set
11. events (bit 6): event 1 byte, parameter 1 byte; if event 1 has bit 7 set: event 2 byte, parameter 2 byte; if
    event 2 has bit 7 set: event 3 byte, parameter 3 byte
12. solid (bit 18) — long
13. effect slots (bit 13): 16-bit mask (bit i ↔ value i of the 12 values slot0.id, slot0.bolt, slot0.scale,
    slot1.id, …, slot3.scale), then each value whose bit is set, as a short, in index order

Receiver: fields not present keep the value of the reference state, except the events: when bit 6 is absent all
three events are set to 0 (parameters keep their old values); when only one or two pairs are present the missing
events are set to 0.

#### 2.3 solid encoding
| Value | Meaning |
|---|---|
| 0 | not solid / not clippable on the client |
| 31 | inline brush model: the client clips against the brush model given by modelindex |
| other | packed box: byte 0 = x (half-width in x), byte 1 = y (half-width in y), byte 2 = zd (−mins.z), byte 3 = zu (maxs.z + 128) |

The server packs a bounding-box entity (that is not a "dead monster") as x = clamp(trunc(maxs.x), 1, 255),
y = clamp(trunc(maxs.y), 1, 255), zd = clamp(trunc(−mins.z), 1, 255), zu = clamp(trunc(maxs.z + 128), 1, 255); a
brush-model entity gets 31; anything else 0. The client unpacks mins = (−x, −y, −zd), maxs = (x, y, zu − 128).
Quake II uses a 16-bit 5/5/6-bit packing; SoF uses whole units in 32 bits. The server sends 0 for the viewing
client's own missiles (entities the viewer owns).

#### 2.4 effects and renderfx bits known to matter on the wire
effects: 0x100 = the sound field is an ambient-set id; 0x2000 = auto-rotate (yaw); 0x1078 = auto-animate group;
0x8001 = suppress effect slots. renderfx: 0x2 = viewer's own model, 0x40 = do not interpolate origin, 0x80 = "sound
only, do not draw" (set by the server for entities sent only because they carry a sound), 0x400 = inline brush
model, 0x800 = brush model flag, 0x400000 = ghoul model. Full catalogue: open question.

### 3. Entity list (svc_packetentities, id 21)
A sequence of entity records, each = header + number + present fields, terminated by a record whose header is 0
and whose number byte is 0 (bytes `00 00`). Records are sorted by increasing entity number.

| Record | Meaning |
|---|---|
| header with bit 11 (REMOVE) | entity `number` was in the reference frame and is no longer in this frame |
| header without bit 11 | entity `number` is in this frame; its state = reference state with the present fields replaced |

Reference state for an entity record: if the entity was in the delta frame's list, its state there; otherwise
its baseline (SPEC-203; all-zero if none was received). If the frame has no delta frame (−1), the reference list
is empty and every entity is coded against its baseline. Entities of the reference list that do not appear in
the new list (neither updated nor removed) are carried over unchanged.

Server writer behaviour (observable, must be reproduced only for byte-identical output):
- An entity present in both lists is written only when at least one field changed; entities numbered above 255 are
  written even when unchanged (their header always has bit 24, so it is never empty) — 6 bytes each per frame.
- An entity new to the list is always written (relative to its baseline), even if no field differs.
- A field is marked present when it differs from the reference, with these exceptions: events are present whenever
  event 1 is non-zero (events are never delta-compared); sound data is sent only when the sound index changed;
  the effect slots are compared only when at least one of the four slot ids is non-zero in the reference or in the
  new state.
- Size class for frame, effects, renderfx (signed comparison): value < 256 → 8-bit (so negative values are sent as
  their low byte), < 32768 (effects, renderfx) or any larger value (frame) → 16-bit, else 32-bit. skinnum (unsigned
  comparison): < 256 → 8-bit, < 65536 → 16-bit, else 32-bit. The writer refuses entity number 0 or above 1023.
- The receiver rejects numbers ≥ 1024 ("CL_ParsePacketEntities: bad number:%i") and a message that ends inside the
  list ("CL_ParsePacketEntities: end of message"). A REMOVE record whose number does not match the expected old
  entity prints "U_REMOVE: oldnum != newnum" and is otherwise ignored.

The client resets interpolation for an entity (no lerp) when modelindex or modelindex2 changed, when any origin
component moved more than 512 units, or when the entity was not in the previous frame.

#### 3.1 Which entities the server sends (observable)
An entity is a candidate when it is not flagged "no client" and has a model, effects, a sound, an effect slot, an
event, or the ghoul renderfx bit. The viewer's own entity and entities flagged "always send" are always included.
Others are included when they pass the PVS and area tests (as Quake II). An entity that fails the visibility test
but has a sound is still sent, with renderfx bit 0x80 added; such sound-only entities farther than 1200 units are
dropped. An entity not sent but carrying an event within 1000 units of the viewer produces an svc_culledEvent
(id 24, SPEC-201) placed in the datagram before svc_frame. After every server frame the events of all entities are
cleared.

### 4. Player state (svc_playerinfo, id 20)
Payload: a 32-bit flag word (long), then the present fields in the order of the table, then the stats block
(always present). The reference state is the player state of the delta frame, or all zero when there is no delta
frame. A field is present when it differs from the reference.

| Order | Flag | Field | Wire | Decoding / notes |
|---|---|---|---|---|
| 1 | 0x00010000 | view-weapon model: ghoul instance id, weapon type | short, byte | presence decided by the instance id only; type 6 enables the scope field of view (UNVERIFIED) |
| 2 | 0x00020000 | weapon counter A (UNVERIFIED: clip) | byte | |
| 3 | 0x00040000 | weapon counter B (UNVERIFIED: ammo) | short | |
| 4 | 0x40000000 | weapon-state sequence | byte | the client notifies its weapon predictor when it changes |
| 5 | 0x10000000 | prediction resync number | byte | client predicts weapons only while it equals the value of the last svc_restart_predn |
| 6 | 0x20000000 | button inhibit mask | byte | predicted buttons = buttons AND NOT mask (bits 1, 4, 8, 16) |
| 7 | 0x00100000 | remote-view model ghoul id | short | read by the client; never sent by the 1.07f server |
| 8 | 0x00000001 | pm_type | byte | 0 normal, 1 noclip, 2 spectator, 3 dead, 4 gib, 5 frozen/intermission, 6 normal with remote view, 7 intermission spectator |
| 9 | 0x00000002 | pmove origin | 3 shorts | 1/8 unit, already in pos-short units in the state |
| 10 | 0x00000004 | pmove velocity | 3 shorts | 1/8 unit/s |
| 11 | 0x00000008 | pm_time | byte | |
| 12 | 0x00000010 | pm_flags | byte | |
| 13 | 0x00000020 | gravity | short | |
| 14 | 0x00000040 | delta_angles | 3 shorts | angle16 units |
| 15 | 0x08000000 | extra pmove byte (UNVERIFIED meaning) | byte | |
| 16 | 0x00000080 | viewoffset | 3 chars | x, y: trunc(v × 4), read × 0.25; z: trunc((v − 14.6) × 5) computed in double precision (so 22.0 → 37), read × 0.2 + 14.6 |
| 17 | 0x00000100 | viewangles | 3 angle16 | |
| 18 | 0x00008000 | remote camera angles | 3 angle16 | |
| 19 | 0x00001000 | remote camera origin | 3 shorts | stored and sent in 1/8 units |
| 20 | 0x00002000 | remote view type, remote view entity | byte, byte | type 0 = none; entity: low byte only, server default −1 → 255 |
| 21 | 0x00000200 | kick_angles | 3 chars | trunc(v × 4), read × 0.25 |
| 22 | 0x00080000 | weapon kick (first component only) | 1 char | change test on all three components; trunc(v × 4), read × 0.25 |
| 23 | 0x00000400 | blend RGBA | 4 bytes | trunc(v × 255), read / 255 |
| 24 | 0x00000800 | fov | byte | trunc(fov) |
| 25 | 0x00004000 | rdflags | byte | low 8 bits |
| 26 | (always) | stats | long mask + shorts | 16 stats; mask bit i set when stat i changed; then the changed stats as shorts in index order |
| 27 | 0x00400000 | cinematic freeze | byte | non-zero freezes client input and sets client cvar `cl_cinematicfreeze` |
| 28 | 0x00800000 | music value (UNVERIFIED; written by the game's music-control entity) | byte | |
| 29 | 0x01000000 | ambient-set value (UNVERIFIED; written by the game's ambient-set entity) | short | |
| 30 | 0x02000000 | deathmatch rank, number of ranked players | byte, byte | shown as "rank/count" |
| 31 | 0x04000000 | spectated player index (UNVERIFIED base) | byte | |
| — | 0x80000000 | never set | | |

Differences from Quake II: 32-bit flag word (Quake II: 16-bit); 16 stats with a 32-bit mask (Quake II: 32);
many SoF fields; Quake II's gun index/frame/offset/angles fields do not exist. In demo playback the client forces
pm_type 5.

### 5. Frame acknowledgement and validity
- The client acknowledges the last valid frame in every move message (section 6). The server codes the next
  frame against that frame when it is at most 12 frames old (16-frame history minus 3); otherwise it sends delta
  frame −1.
- Client checks: delta frame ≤ 0 → frame valid with no reference. Otherwise the reference must still be in the
  client's 16-frame history with the same frame number ("Delta frame too old."), its entity list must not have
  been overwritten in the 1024-entry entity ring (more than 896 entities parsed since → "Delta parse_entities
  too old."), and the reference must itself be valid ("Delta from invalid frame (not supposed to happen!)."). An
  invalid frame is still parsed completely but not displayed.
- The client keeps an estimate of the game time: on each frame, if |estimate − received game time| ≤ 0.5 s the
  estimate moves 5 % toward the received value, else it jumps to it; between frames it advances with the client's
  frame time. This estimate is sent back in the fire-time fields of the user commands.

### 6. Client move message (clc_move, id 2)
| # | Type | Field |
|---|---|---|
| 1 | byte | 2 |
| 2 | byte | checksum (6.3) |
| 3 | long | last valid server frame received, or −1 (no valid frame yet, or waiting for a demo to start) |
| 4 | bit stream | three user commands: the command before the previous one coded against an all-zero command, the previous one coded against it, the current one coded against the previous one |

The client sends the move message as the unreliable part of a packet each client frame once the connection is
active (SPEC-203), after any reliable stringcmd/userinfo. The commands are those for the netchan sequences
s−2, s−1 and s, where s is the outgoing sequence of the packet being sent. The server uses the newest command and,
when the netchan reports dropped packets (fewer than 20), replays the older ones (and repeats its last command
for drops beyond 2) as in Quake II. Only the first move message in a packet is processed; a client that is not
yet "spawned" has its commands read and discarded.

#### 6.1 User command fields
| Field | Range / meaning |
|---|---|
| msec | 0..255, milliseconds covered (client: frame time, values above 250 become 100) |
| buttons | 0x01 attack, 0x02 use, 0x04 alt-attack, 0x08 weapon extra 1, 0x10 weapon extra 2, 0x20 run (speed key XOR `cl_run`), 0x40 "use/skip" (restarts a level after death; skips cinematics), 0x80 any key down |
| lean | −1 (right), 0, +1 (left) |
| lightlevel | 0..255, only the top 5 bits are transmitted |
| angles[3] | 16-bit angles (angle16 units, view angles converted with 65536/360) |
| forwardmove, sidemove, upmove | clamped to ±200, ±160, ±200 |
| fire time, alt-fire time | float; client's game-time estimate when the predicted weapon fired (0 otherwise) |

#### 6.2 User command bit stream
The three commands are packed into one bit stream that starts right after the "last frame" field. Bits are
packed least-significant-bit first: stream bit k is bit (k mod 8) of byte (k div 8); an n-bit value is written
starting with its least significant bit. The stream is padded with zero bits to a whole number of bytes. Per
command, in this order (each "changed" bit compares with the reference command):

| # | Field | Bits |
|---|---|---|
| 1 | pitch | 1 changed bit; if 1: 12 bits = (16-bit angle) >> 4. Receiver: value << 4 |
| 2 | yaw | same |
| 3 | roll | same |
| 4 | forwardmove | 1 changed bit; if 1: 10 bits = value + 510. Receiver: bits − 510, then clamp |
| 5 | sidemove | same |
| 6 | upmove | same |
| 7 | buttons | 1 changed bit; if 1: 8 bits |
| 8 | lean | 1 bit (lean ≠ 0); if 1: 1 bit (1 = negative). Always sent, not delta-coded |
| 9 | lightlevel | 1 changed bit (top 5 bits differ); if 1: 5 bits = lightlevel >> 3. Receiver: value << 3 |
| 10 | msec | 8 bits, always |
| 11 | fire time | 1 changed bit; if 1: 32 bits = IEEE single-precision bit pattern |
| 12 | alt-fire time | same |

A command needs 19 to 163 bits. The sender clamps the movement values of the command before comparing. The
receiver, on running out of data, stops decoding and leaves the remaining fields unchanged. Note: the original
client assembles the move message in a 64-byte buffer, while the worst case is 68 bytes; the original client
aborts in that case (both fire times and every field changing in all three commands).

#### 6.3 Move checksum
1. Region = the bytes after the checksum byte up to the end of the bit stream (the "last frame" long plus the
   command bytes), truncated to at most 60 bytes.
2. Let s = the packet's netchan sequence number (the client's outgoing sequence; the server uses the received
   sequence, which is the same number) and p = s mod 2996 (unsigned). Let T be the 3000-byte image of the
   250-entry direction table (SPEC-201 Appendix A: 250 × 3 little-endian floats).
3. Append four salt bytes: T[p] XOR (s & 0xFF), T[p+1], T[p+2] XOR ((s >> 8) & 0xFF), T[p+3].
4. Checksum = low 8 bits of the Quake II block checksum of region+salt (MD4 digest; exclusive-or of its four
   32-bit little-endian words).
The server ignores the rest of the packet when the checksum does not match ("Failed command checksum for
<name>", developer print). Quake II uses a CRC over a different table here.

#### 6.4 Time enforcement
Every 16 server frames each active client's command time budget is reset to 1800 ms; each executed command
subtracts its msec. With cvar `sv_enforcetime` non-zero (default 0) a command that would make the budget negative
is skipped ("commandMsec underflow from <name>").

## Constants
| Name | Value |
|---|---|
| Frame history (UPDATE_BACKUP) | 16 |
| Maximum delta age used by the server | 12 frames |
| Client entity ring (MAX_PARSE_ENTITIES) | 1024; "too old" limit 896 |
| Command history (CMD_BACKUP) | 64 |
| MAX_EDICTS | 1024 |
| Server frame time | 100 ms |
| Area bits buffer | 32 bytes |
| Stats | 16 |
| Move checksum region limit | 60 bytes |
| Move message buffer in the original client | 64 bytes |

## Test vectors / how to verify
Vectors 1–6 were produced by an independent model of the rules above, not yet by capture (UNVERIFIED until
compared with a packet capture of the original game).
1. New entity 5 from an all-zero baseline: origin (100, −50.5, 24), angles (0, 90, 0), modelindex 12, frame 3,
   bounding box x/y ±16, z −24..+32 (solid 0xA0181010). Header bits 0x160027:
   `A7 80 16 05 0C 03 20 03 6C FE C0 00 40 10 10 18 A0`.
2. Entity 300: origin x 100 → 101, events (0x85, 7), (3, 9), sound 300 with data 0xF9, slot 0 values 17, 2, 32:
   `C1 A0 E0 01 2C 01 28 03 2C F9 85 07 03 09 07 00 11 00 02 00 20 00`.
   Entity 300 unchanged: `80 80 80 01 2C 01`. Remove entity 300: `80 88 80 01 2C 01`. Entity 7 new but identical to
   its baseline: `00 07`. End of list: `00 00`.
3. Player state from zero: pmove origin shorts (800, −404, 192), viewangles (0, 90, 0), fov 95, stat 0 = 100:
   `14 02 09 00 00 20 03 6C FE C0 00 00 00 00 40 00 00 5F 01 00 00 00 64 00`.
4. Next player state (delta from vector 3): viewoffset (0, 0, 22), view-weapon id 0x1234 type 6, kick angles
   (−1.5, 0, 0.25), blend (1, 0, 0, 0.5): `14 80 06 01 00 34 12 06 00 00 25 FA 00 01 FF 00 00 7F 00 00 00 00`.
5. User commands: oldest = {msec 16, lightlevel 128, angles (0x0400, 0x4000, 0), forwardmove 200}; previous =
   same with msec 17; current = {msec 16, buttons 0x21, lean +1, angles (0x0400, 0x4010, 0), forwardmove 200,
   sidemove −160, lightlevel 130, fire time 12.3}. Bit stream (58 + 19 + 82 bits, 20 bytes):
   `81 20 00 69 2C 84 10 00 88 C0 00 A2 57 86 04 62 33 33 51 10`.
6. The same commands as a move message for sequence 1234 and last frame 567:
   `02 5B 37 02 00 00 81 20 00 69 2C 84 10 00 88 C0 00 A2 57 86 04 62 33 33 51 10`.
   Checksum spot values: sequence 0 with an empty region → 0xA5; sequence 1 with region `00` → 0x85.
7. Capture check: record a loopback session, split the payload by message ids, and verify that every
   svc_packetentities list ends exactly at `00 00` and that the move checksums recompute.

## Open questions
- Meaning of the yaw-offset field and of modelindex2 (gamex86 writers not traced).
- Player-state fields marked UNVERIFIED (weapon counters, extra pmove byte, music/ambient values, spectated index).
- Full effects / renderfx bit catalogue.
- Position of the culled-event reconstruction (the client appears to mirror it through the viewer).
- Whether any original client ever hits the 64-byte move-buffer limit in practice.
