# SPEC-311: Demos — recording (`record`, `serverrecord`), file layout, playback (`demomap`)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (client and server), version 1.07f
Depends on: SPEC-010 (ghoul reliable stream), SPEC-200 (netchan header, maximum message size), SPEC-201
(message ids), SPEC-202 (svc_frame, entity deltas), SPEC-203 (svc_serverdata, svc_configstring dictionary coding,
connection sequence), SPEC-300 (user and game directories), SPEC-310 (loading screen)

## Purpose
Defines the `.dm2` demo files written and played by Soldier of Fortune 1.07f, so that OpenMerc can play original
demos and write demos the original can play. The design is Quake II's (a demo is the recorded server→client
message stream); the differences are the SoF message set, the deathmatch byte in the header, the ghoul state
blocks, the stuffed command, the file locations and a reduced server-side recording.

## Interface / layout

### Commands and files
| Command | File written / read |
|---|---|
| `record <name>` (client, only while active in a level) | `<user dir>/demos/<name>` lower-cased; `.dm2` is appended unless the lower-cased name already contains `.dm2` anywhere |
| `stop` (client) | ends the client recording |
| `serverrecord <name>` (server, level running) | `<game dir>/demos/<name>.dm2` |
| `serverstop` (server) | ends the server recording |
| `demomap <name>` | plays `demos/<name>` looked up through the normal file search path (SPEC-300); the name is lower-cased and `.dm2` appended unless it already ends in `.dm2` |
| `timedemo` cvar | 1 = play as fast as possible and report the frame rate (Quake II) |

Messages: `record <demoname>` (usage), `Already recording.`, `You must be in a level to record.`,
`Recording to %s.`, `ERROR: couldn't open.`, `Not recording a demo.`, `Stopped demo.`, `serverrecord <demoname>`,
`recording to %s.`, `Not doing a serverrecord.`, `Recording completed.`, `Demo %s` (on `demomap`),
`Couldn't open %s` (fatal for the level when the file is missing), `%i frames, %3.1f seconds: %3.1f fps`
(timedemo result on disconnect).

### File layout
A demo file is a sequence of blocks; all integers little-endian.
| Field | Type | Meaning |
|---|---|---|
| length | int32 | number of message bytes that follow; −1 (FF FF FF FF) = end of demo |
| message | length bytes | server→client message stream exactly as it appears after the netchan header (SPEC-200): any sequence of svc messages (SPEC-201), reliable and unreliable parts concatenated |
A block must not be longer than the engine's current maximum message size (SPEC-200: 1400 in network mode,
16384 in single-player/loopback mode); playback aborts with "SV_SendClientMessages: msglen > MaxMsgLen" otherwise.
End of file without a −1 block also ends the demo.

### Client demo contents (written by `record`)
1. **Header block(s)**, built in a buffer of the current maximum message size:
   - svc_serverdata (14): long 33, long spawn count of the current connection, byte 1 (demo flag), byte = the
     client's `deathmatch` cvar truncated, string game directory, short local player number, string configstring 0;
   - for every non-empty configstring 0..3554 in index order: svc_configstring (15) with the SPEC-203 dictionary
     coding. Before adding one, if (bytes used + text length + 32) exceeds the buffer size, the buffer so far is
     written as a block and emptied;
   - for every entity number 0..1023 whose baseline has a non-zero model index or render flag 0x400000 (ghoul
     model): svc_spawnbaseline (16) followed by that baseline coded against the all-zero state (forced, SPEC-203).
     Before each, if bytes used + 64 exceed the buffer size, the buffer is written as a block and emptied;
   - svc_stufftext (13): `precache <spawn count>; reset_predn\n`;
   - the remaining buffer is written as the last header block.
2. **Wait for an uncompressed frame**: until a frame with delta frame ≤ 0 arrives, received messages are not
   recorded and the client asks the server for uncompressed frames (its move messages carry "last frame" −1,
   SPEC-202 §6).
3. **Ghoul state**: when that first uncompressed frame is parsed, before the message containing it is written,
   the client's stored ghoul reliable stream (SPEC-010 client slots 26/27: records of {32-bit length L, L bytes})
   is written as one block per record: length = L + 3, then byte 26 (svc_ghoulreliable), short L, the L bytes.
4. **Messages**: from then on every server message the client parses is written as one block (after parsing,
   in arrival order), including messages carrying only reliable data.
5. **End**: `stop`, a disconnect, or a map change (`changing`) writes the −1 block and closes the file; a demo
   therefore covers one level.

### Server demo contents (written by `serverrecord`; differs from Quake II)
1. First block: svc_configstring messages for all non-empty server configstrings (dictionary coded). Unlike Quake
   II there is no svc_serverdata and no baselines in this block.
2. Then, every server frame, one block: svc_frame (23) with long frame number and float game time only (no delta
   frame, no area bits, no player state), svc_packetentities (21) with every entity number from 1 upward that is not
   flagged "not sent to clients" and has a non-zero model index, effects, sound or first event, or an occupied effect
   slot, each coded against the all-zero state; terminated by short 0; followed by all multicast messages of that
   frame.
3. `serverstop` writes −1 and closes. Such files cannot be played with `demomap` (no serverdata; the frame layout
   is not the client's), as in Quake II. UNVERIFIED in game.

## Behaviour

### Playback (`demomap`)
1. The server starts a level in demo state: menu cvar `menu_nostats` := 1, the loading screen is shown
   (SPEC-310 §9), connected clients are told `changing` and later `reconnect` as for a map change. As in Quake II the
   server does not load the level's map or run the game module for a demo (UNVERIFIED).
2. When the client sends `new` (SPEC-203), the server opens the demo file (missing → level aborted with
   `Couldn't open demos/<name>`) and sends nothing itself; the demo's own header provides svc_serverdata etc.
3. Every server frame (10 per second) the server reads the next block and sends its bytes as the unreliable part
   of one netchan packet to each connected client. While the server cvar `paused` is non-zero no block is read.
   A −1 block, a short read or end of file closes the demo and the server moves to `nextserver` (Quake II
   behaviour: usually ends the session).
4. The client treats the stream normally, with the demo flag set from svc_serverdata: it sends no movement, forces
   pm_type 5 for display (SPEC-202), any key press acts as the Escape key (no exception for function keys, unlike
   Quake II), `toggleconsole` executes `killserver`, and the `changing` command only closes menus. With `timedemo`
   ≠ 0 the client counts rendered frames from the first one and prints the rate when the demo ends.
5. Because messages are replayed at the server frame rate, a client demo plays back in real time only if the
   original messages arrived once per server frame (the normal case). UNVERIFIED for recordings made with packet
   loss or with more than one message per frame.

## Constants
| Name | Value |
|---|---|
| Extension | `.dm2` |
| Directory | `demos/` (client: under the user directory; server recording: under the game directory) |
| End marker | int32 −1 |
| Demo flag in svc_serverdata | 1 (client recording) |
| Header split margins | configstring: text length + 32 bytes; baseline: 64 bytes |
| Stuffed command | `precache <spawn count>; reset_predn\n` |
| Ghoul state block | length L + 3: byte 26, short L, L bytes |
| Playback rate | one block per server frame (100 ms) |

## Test vectors / how to verify
1. Record `record test` in a single-player level and stop immediately: `<user dir>/demos/test.dm2` starts with an
   int32 length, then bytes `0E 21 00 00 00`, the spawn count (4 bytes), `01`, the deathmatch byte, the game
   directory string (empty → `00`), the player number as 2 bytes, the level name string; the file ends with
   `FF FF FF FF`.
2. `record MyDemo.DM2x` writes `demos/mydemo.dm2x` (".dm2" is contained, nothing appended).
3. `demomap test` replays it; the loading screen shows without statistics (`menu_nostats` 1); pressing any key opens
   the menu.
4. After the header blocks, the first recorded block(s) begin with byte 26 (ghoul state) when the level has ghoul
   models, followed by a block containing svc_frame (23) with delta frame −1.
5. `serverrecord sv1` then `serverstop`: `<game dir>/demos/sv1.dm2` whose first block starts with byte 15
   (configstring), not 14.

## Open questions
- Whether 1.07f plays client demos recorded in a network game whose blocks are ≤ 1400 bytes while the local demo
  server runs in single-player size mode (expected yes; the check is only an upper bound).
- Exact client reaction to `precache <n>` during playback (map check string comparison, local download skip):
  assumed as in SPEC-203 §3 with downloads disabled because the server is local.
- Whether a demo recorded across a level's `reconnect` (single-player level transition) is usable (recording stops
  at `changing`).
