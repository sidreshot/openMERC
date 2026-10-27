# Network spec verification by packet capture (feedback to the reverse team)
Status: clean-team report (C, 2026-10-21) — black-box captures of the original 1.07f executables; no code read
Covers: SPEC-200, SPEC-201, SPEC-202, SPEC-203

## Method
- Original dedicated server: `SoF.exe +set dedicated 1 +set user ./usersrv +set ip 127.0.0.1 +set port 27910
  +set deathmatch 1 +set maxclients 8 +map dm/jpndm2` (D:\Games\SoF-oracle, 1.07f).
- Original client connected through `tools/net/udpproxy.py` (UDP relay that logs every datagram, both ways).
  The client was scripted black-box through the out-of-band `cmd` packet (forward/strafe/turn/jump/lean/fire/
  use/say/weaponselect/disconnect/quit); capture 3 also changed the map with `rcon map dm/jpndm2`.
- Every captured datagram was decoded by `tools/net/sofnet.py`, written only from the specs (tables are read
  from the spec files). Independent cross-check: the original client's own `cl_shownet 2` console log of
  capture 3 (`tools/net/shownet_compare.py`).
- Captures and decoder dumps are game-derived data and stay outside the repository
  (`openmerc-data/net/cap1..3.jsonl`, `*.dump.txt`, `*.summary.txt`).

| Capture | Datagrams c2s / s2c | Sequenced bytes decoded | Content |
|---|---|---|---|
| cap1 | 2157 / 796 | 143,755 of 143,755 | join, 70 s idle in game (relay stopped before the client) |
| cap2 | 1188 / 445 | 101,177 of 101,177 | join, scripted movement/fire/lean/say, disconnect, quit |
| cap3 | 1206 / 492 | 120,428 of 120,428 | join, `rcon map` (reconnect), rejoin, disconnect, quit |

Every sequenced datagram parsed to its last byte with no unknown message id, no underflow and no leftover
bytes (0 errors in 6,284 datagrams). Totals: 4,313 clc_move, 1,495 svc_frame, 112 svc_ghoulreliable,
902 svc_ghoulunreliable, 5,038 svc_configstring. Shownet cross-check: 3,170 of 3,170 messages of the
client's log match the decoder by name and, outside frames, byte length; every frame's
frame+playerinfo+packetentities split differs from the decoder by the same constant 1 byte (374/374).

## Results per spec claim

### Confirmed
| Spec | Claim | Evidence |
|---|---|---|
| 200 | Netchan header: seq/ack words, bit 31 flags, 16-bit qport only c2s | 6,268 sequenced datagrams |
| 200 | Ack bit = parity of reliable messages received from the peer | 6,268/6,268 |
| 200 | Ack sequence = newest peer sequence | 6,235/6,268; the 33 others are packets already in flight (sent before the peer's newer packet arrived) |
| 200 | Test vector 1 (`new` packet) | captured `01 00 00 80 00 00 00 00 04 71 04 6E 65 77 00` (qport 0x7104); nothing else bundled — confirmed |
| 200 | Test vector 2 (`ping 5` → `ack 5`), `ping` without argument → `ack ` | exact |
| 200 | Test vector 3 (`info 33`) | `info\nSOF+          noname+dm/jpndm2+0/0/8++DM+63\n` (map name longer than 8 is not truncated); `info 34` → no reply |
| 200 | Test vector 4 (`connect 32` → `print\nServer is version 1.07f.\n`) | exact |
| 200 | Test vector 5 (GameSpy `\basic\` first request) | `\gamename\sofretail\gamever\1.07f\location\1\final\\queryid\2.1` exact; `\info\`, `\rules\`, `\players\` (empty server: `\final\\queryid\5.1`), `\echo\hello` match the section tables |
| 200 | status reply ends with `\` + newline (5C 0A) | exact |
| 200 | Bad rcon → `print\nBad rcon_password.\n`; unknown OOB → no reply | exact |
| 200 | challenge bits 15 and 31 are zero | 9 challenges |
| 200 | `getchallenge <nonce>`, nonce = Unix time, repeated in `connect` | 4 handshakes |
| 200 | `connect 33 <qport> <challenge> <nonce> 3.14 "<userinfo>"\n`, reply `client_connect` (no newline) | 4 handshakes |
| 201 | Message ids/payloads of every svc seen (1,5,8,9,10,11,12,13,14,15,16,23,24,26,27,28,29,34,36,38,39) and clc 2/4 | byte-exact parse of all datagrams + shownet |
| 201 | Datagram order: reliable part, culled events, frame, ghoul unreliable, queued unreliable | 1,495/1,495 datagrams (the UNVERIFIED part is confirmed: queued messages come last, 66/66) |
| 201 | At most one svc 26 and one svc 27 per datagram; 27 directly after the frame | 1,495 / 902 |
| 201 §8 | Map change: twice, outside the reliable stream, svc_print(2, "Server restarted\n") + svc_reconnect | cap3 seq 300/301 |
| 201 B.1–B.5 | Ghoul bit order, ids, times, strings (dictionary + Huffman + raw), reliable lists, object definitions | 112/112 reliable payloads end with < 8 zero padding bits; 1,739 strings per join (1,637 dictionary, 102 Huffman) decode to readable ghoul names; the Huffman tree built from Appendix D is complete |
| 201 B.6 | Unreliable instance updates | 902/902 payloads, 1,124 updates, all end within the padding (after the correction below) |
| 202 §1 | svc_frame layout, mandatory 20 and 21, areabits length 1 | 1,495 frames |
| 202 §2 | Entity header bits, field order, sizes; list ends with `00 00`; bits 27..31 never set | 2,533 entity fields, 16 baselines per join |
| 202 §4 | Player-state flag word, field order and sizes; stats mask bits ≥ 16 never set | 1,495 states; stat present ⇒ changed 59/59, absent ⇒ unchanged 23,861/23,861 |
| 202 §5 | Server deltas against the frame the client last acknowledged; first frame delta −1 | 1,491/1,491; all deltas 1 frame old |
| 202 §6 | clc_move: last frame = newest frame received | 4,313/4,313 |
| 202 §6.2 | Bit-packed user commands (field order, widths, LSB-first, zero padding) | 4,313/4,313; commands s−2/s−1 equal the previous packet's s−1/s in 4,309/4,309 |
| 202 §6.3 | Move checksum (MD4 over region + direction-table salt) | 4,313/4,313; test vectors 5, 6 and both spot values agree with the decoder |
| 203 | Signon order `new` → serverdata + `cmd configstrings <sc> 0` → … → `ghoulstrings` 0..26 → `baselines` → `precache <sc>` → client `sv_precache <sc>` → `cmd begin <sc> \n` (with the space) | 4 joins |
| 203 | Batches stop once the pending reliable data reaches 700 bytes | configstring datagrams 736–776 bytes |
| 203 | Configstring dictionary: code ≥ 0 = index, −1 + literal; literals never in the dictionary | 5,038 configstrings, 0 literals found in the dictionary; test vector 1 captured byte-exact (`0F 25 00 00 00`), vector 2 form (`0F 00 00 FF FF "JPNDM2"`) |
| 203 | svc_serverdata layout incl. deathmatch byte (test vector 4 form) | `0E 21 00 00 00 2A 00 00 00 00 01 00 00 00 4A 50 4E 44 4D 32 00` |
| 203 | Configstring index layout (models 36.., sounds 292.., effects 648.., images 904.., lights 1159.., player info 1415+slot, string packages 1464.., welcome 1493, ghoul files 1498..) | 1,257 configstrings of dm/jpndm2 |
| 203 | Map check string (test vector 6) | MD4 per lump of jpndm2.bsp = `933716648CD0377CD043BBB566F479B71C8B52B783B7` = configstring 6 |
| 203 §5 | svc_reconnect → client restarts the out-of-band handshake (getchallenge, connect) | cap3 |

### Refuted or corrected (exact counter-examples)
1. **SPEC-200 challenge slots are keyed by IP address and port**, not by IP alone. Same server, source IP
   127.0.0.1: port A → `challenge 1767006096`, again from port A → `1767006096`; port B → `373911025`;
   port C → `1525760443`.
2. **SPEC-202 §1 field 3: game time = (frame + 2) × 0.1**, not frame × 0.1, in 1,495/1,495 frames of
   three server runs. Example: frame 2545 (`F1 09 00 00`) carries `33 B3 7E 43` = 254.7.
3. **SPEC-201 B.6 field 12 is under-specified: the "per-instance byte array" has as many entries as list 2
   of the object definition** (B.5 field 3). With list 3's count, 13 of 31 reliable payloads of cap2 fail
   (the run-length mask overruns, e.g. 37 entries decoded for a count of 10 on object 20, lists of
   194/263/10/8/19 entries); with list 2's count all 112 reliable and 902 unreliable payloads decode exactly.
   Lists 1, 4 and 5 also fail.
4. **SPEC-203 spawn count**: not "incremented every time a map loads". After `rcon map dm/jpndm2` it went
   from 42 to 15725 (the values are those of a C-library `rand()` + 1 at a fresh game start, as in Quake II;
   only level changes within a game increment it).
5. **SPEC-200 "LAN address" test**: the client's `cmd` packet from 127.0.0.1 was ignored while the client was
   bound to 127.0.0.1; from 172.17.1.102 (same class-B network as the interface) it was executed. So
   "loopback" in the LAN test means the engine's internal loopback only, not 127.0.0.0/8. Please state this.
6. **Presence = "differs from the reference" (SPEC-202 §2, §4) is decided on unquantised values**: 186 of
   2,195 present player-state fields and 357 of 2,533 present entity fields are equal after quantisation —
   all of them float fields (viewoffset, kick angles, view angles, entity pitch/yaw/roll). Integer fields:
   no exception. Receivers must not assume a present field changed on the wire.

### Gaps (behaviour seen but not in the specs)
7. **Client disconnect**: three unreliable datagrams, each only `04 "disconnect"` *without* the NUL
   (e.g. `A0 04 00 00 B9 01 00 80 AC 5B 04 64 69 73 63 6F 6E 6E 65 63 74`); the string ends at the end of the
   message. The server answers reliably with `08 0D 04` (svc_disconnect 0x040D), svc_print(2,
   "John Mullins disconnected\n") and svc_configstring 1416 = literal "" (`0F 88 05 FF FF 00`).
8. **The first reliable message after `client_connect` starts with svc_configstring 1415+slot (the joining
   player's info) before svc_serverdata** (queued by the game at connect). A client must accept
   configstrings before serverdata.
9. **Userinfo of the 1.07f client** carries a key `predicting` ("1") not in the SPEC-203 table, and
   `cl_violence` = 63 on this installation (table default "0"); the dedicated server reports `sv_violence` 63
   (serverinfo table default "0"). Defaults may depend on the installed region/violence setting.
10. Player-info configstring observed: `John Mullins\mullins\mullins\-1` (4th field −1).
11. The server sends an 8-byte header-only datagram about once a second to a client that is still signing on
    (keep-alive), as Quake II does.
12. `cl_shownet 2` prints message lengths, not offsets, and "TOTAL PACKET" = datagram length + 1.
13. Challenge values repeat across server restarts (same `rand()` sequence): 1736723169 was handed out by two
    different server processes.

### Not exercised by these captures
svc ids 2, 4, 6, 7, 17–19, 22, 25, 30–33, 35, 37, 40; temp-entity types other than 34, 60, 61; svc_effect
extension blocks and flags 0x10/0x20 (OPEN_QUESTIONS 8 could not be confirmed: 20 effects seen, flags 01, 02,
04 only); svc_sound with index ≥ 256 (SPEC-201 vector 1 only in form); entity numbers > 255 (max 98, so
"unchanged entities > 255 are still sent" is untested); downloads; `changing`/`reconnect` stufftext path
(`map` uses svc_reconnect); rejections other than the version check; GameSpy `\secure\`, `\status\`,
`\packets\`; packet-loss behaviour (no drops on loopback).

## OpenMerc client against the original server (engine `client/cl_sof.c`)
The same relay recorded the OpenMerc engine (`cl_protocol 33`, default) joining the original dedicated server
four times (om1–om4, 30–100 s each, the last one with four server bots shooting at it); `sofnet.py` checked
our side of every capture.

| Check | Result |
|---|---|
| Out-of-band handshake | same sequence and formats as the original client: `getchallenge <unix time>`, `connect 33 <qport> <challenge> <nonce> 3.14 "<userinfo>"`, `client_connect` |
| Signon stringcmds | identical to the original client: `new`, 25–26 × `configstrings`, 27 × `ghoulstrings`, `baselines`, `sv_precache <sc>`, `begin <sc>` |
| First sequenced packet | `01 00 00 80 01 00 00 80 50 0C 04 6E 65 77 00` (original: same with its qport) |
| clc_move | 15,647 moves, checksum valid 15,647, zero padding 15,647, s−2/s−1 chaining 100 %; the server deltas every frame against the frame we acknowledge |
| Server accepts the commands | W for 2 s moved the player 271 units (x −928 → −657, ~135 units/s walking), jump z 24 → 64 → 24, strafe and back likewise |
| Server messages | 2,671 frames and all signon/game messages parsed without error, including svc_temp_entity 34/60/61, svc_effect, svc_sound, svc_culledEvent, ghoul streams, string-package prints and obituaries |
| Map check string | computed by the engine from its own .bsp, equal to configstring 6 |

Differences from the original client: the userinfo carries Yamagi's extra keys (`gender`, `hand`,
`vid_pauseonfocuslost`, `windowed_mouse`) and no `msg`; `predicting` is sent as 0 (no weapon prediction);
moves go out at the client frame rate (~64/s instead of ~30/s); fire times are always 0.
