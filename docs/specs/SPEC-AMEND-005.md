# SPEC-AMEND-005: Answers to the clean team's open questions of 2026-10-22
Status: approved (G, 2026-10-25) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: SoF.exe, gamex86.dll, player.dll, ref_gl.dll, version 1.07f
Depends on: SPEC-001, SPEC-002, SPEC-003, SPEC-004, SPEC-010–014, SPEC-142, SPEC-143, SPEC-200–204, SPEC-300–303, SPEC-310–313, SPEC-400, SPEC-401, SPEC-410–413, SPEC-602, SPEC-700, SPEC-701, SPEC-710, SPEC-711, SPEC-740, SPEC-741, SPEC-720, SPEC-722, SPEC-721, SPEC-750–752, SPEC-760, SPEC-761, SPEC-770, SPEC-771, SPEC-780, SPEC-781, SPEC-782, SPEC-790, SPEC-791, SPEC-800–803, SPEC-601, SPEC-732
Amends: SPEC-204 (§2, §3, §4, test vectors, open questions), SPEC-302 (`sv_violence`, `allow_download`, `gamedir`, Behaviour 8, command list), SPEC-313 (parental-lock table and §2, trace-result table, Behaviour 1, Behaviour 4, §8, Appendix A, test vector 2), SPEC-202/SPEC-780/SPEC-760 (in-memory layouts), SPEC-752 §5, SPEC-780 §5, SPEC-003 (cvar record), SPEC-001 import #81, SPEC-770 §5.8, SPEC-010–014 and SPEC-121 (I3 items), SPEC-400/SPEC-401/SPEC-142 (I5 items), SPEC-002/SPEC-410/SPEC-412/SPEC-413 (I4a items), SPEC-130/SPEC-800–803/SPEC-312 (I6m items), SPEC-300, SPEC-303, SPEC-004 (I1a items), SPEC-750/SPEC-751/SPEC-760 (G6b items), SPEC-310/SPEC-311/SPEC-312 (I6h items), SPEC-790/SPEC-791/SPEC-781 (G9 items), SPEC-700/SPEC-701/SPEC-710/SPEC-711/SPEC-722/SPEC-750 §12 (G2 items), SPEC-143/SPEC-740/SPEC-741/SPEC-760 (G5 items), SPEC-411/SPEC-002/SPEC-011/SPEC-012 (I4b items), SPEC-760 B1.3/B1.4/B1.5/B2, SPEC-780 §2/§4/§10/§16 and SPEC-781 (G18 items 24–28), SPEC-720/SPEC-721/SPEC-722/SPEC-003/SPEC-601 (G3 items), SPEC-001/SPEC-200/SPEC-740/SPEC-741/SPEC-760 §3 (G5 items)

## Purpose

Answers every item of `docs/specs/OPEN_QUESTIONS.md` that was not marked answered on 2026-10-22, in the sections
"SPEC-204 GameSpy", "G18 game module", "Gatekeeper notes on approved specs", "I3 Ghoul model system", "I5 client
effects and temp entities", and the sections added while this amendment was written: "I4a renderer", "I1a engine
core", "G6b script wiring", "I6m menu runtime", "I6h client view, HUD, input, demos", "G9 game modes and Ghoul
glue", "G2 character AI", "G5 navigation and bots" (including its item 12, which is filed inside the I4b section),
"I4b Ghoul model drawing in the 3D view" and "G3 damage model, gore, bodies". The G18 section includes its items 24–28. Each answer was re-checked against the 1.07f binaries for this amendment.

Items are numbered by section and by the item number used in OPEN_QUESTIONS (`GS.9` = SPEC-204 section item 9,
`G18.18` = G18 item 18, `GK.1` = gatekeeper note 1, `I3.n`, `I5.n`, `I4a.n`, `I6m.n`, `I1a.n`, `G6b.n`, `I6h.n`, `G9.n`, `G2.n`, `G5.n`, `I4b.n`, `G3.n`), so
that the gatekeeper can mark each one. Item kinds as in SPEC-AMEND-002:
- **Correction**: the old text (or OpenMerc's interpretation) is wrong; an implementation that follows it differs
  visibly from the original;
- **Resolution**: the old text was UNVERIFIED, silent or an open question, and the answer is now known;
- **Refinement**: the old text is true but incomplete in a way that matters for matching the original.

Every item names its target spec and section, quotes or paraphrases the old text ("OpenMerc interpretation: …" where
the spec was silent), gives the new text, says whether OpenMerc's current choice matches the original, and ends with
a test (in-game, black-box probe or data check). Where only running the game can decide, the item says so.
Observations that need no change are acknowledged in one line.

## Interface / layout

The tables below are module-interface layouts (structures that cross the boundary between the engine, the game module
and the player module). Offsets are in bytes; all values little-endian; "int16" etc. are signed unless marked "u".

### Entity state (116 bytes, entity record offset 0; G18.18)
| Offset | Type | Field |
|---|---|---|
| 0x00 | int32 | entity number |
| 0x04 | float[3] | origin |
| 0x10 | float[3] | angles (pitch, yaw, roll) |
| 0x1C | float | yaw offset (SPEC-202 header bit 0x04000000) |
| 0x20 | int32 | model index |
| 0x24 | int32 | secondary model index |
| 0x28 | int32 | frame (Ghoul entities: replaced per client by the instance's network id) |
| 0x2C | u32 | skin number |
| 0x30 | int32 | effects |
| 0x34 | int32 | renderfx |
| 0x38 | int32 | solid (packed box, SPEC-202) |
| 0x3C | int32 | looping sound |
| 0x40 | int32 | looping-sound data (volume and attenuation) |
| 0x44 | int32[3] | events (bit 0x80 = another event follows) |
| 0x50 | int32[3] | event parameters |
| 0x5C | int16[12] | v0…v11: four effect slots {effect id, bolt, scale × 16}; with effects bit 0x0001 the rotation block {999, angular velocity ×3, Ghoul instance ids ×4, unused ×4} |

### Player state (200 bytes, client record offset 0; G18.19)
| Offset | Type | Field |
|---|---|---|
| 0x00 | int32 | pm_type |
| 0x04 | int16[3] | origin × 8 |
| 0x0A | int16[3] | velocity × 8 |
| 0x10 | u8 | pm_flags |
| 0x11 | u8 | pm_time |
| 0x12 | int16 | gravity |
| 0x14 | int16[3] | delta_angles |
| 0x1A | u8 | movement-speed byte q (trunc(scale × 255)); read only by client prediction |
| 0x1B | u8 | padding |
| 0x1C | float[3] | view angles |
| 0x28 | float[3] | view offset |
| 0x34 | float[3] | kick angles |
| 0x40 | float[3] | weapon kick (only [0] is sent) |
| 0x4C | float[3] | remote camera origin × 8 |
| 0x58 | float[3] | remote camera angles |
| 0x64 | int32 | remote-view entity (−1 = none; low byte sent) |
| 0x68 | int32 | remote-view type (low byte used) |
| 0x6C | pointer | view-weapon Ghoul instance (player module) |
| 0x70 | int16 | view-weapon instance network id — overwritten by the engine every frame |
| 0x72 | int16 | view-weapon type = current weapon id (low byte sent) |
| 0x74 | int16 | weapon counter A, rounds in the clip (low byte sent) |
| 0x76 | int16 | weapon counter B, reserve |
| 0x78 | u8 | weapon-state sequence |
| 0x79 | u8 | prediction resync number |
| 0x7A | u8 | button inhibit mask |
| 0x7B | u8 | padding |
| 0x7C | pointer | game: the player's own body-model instance; not used by the engine |
| 0x80 | int16 | remote-view model id (never sent by 1.07f; never written by the game) |
| 0x82 | — | padding |
| 0x84 | float[4] | screen blend |
| 0x94 | float | field of view |
| 0x98 | int32 | rdflags |
| 0x9C | int16 | ambient-set value |
| 0x9E | u8 | music value |
| 0x9F | u8 | padding |
| 0xA0 | int16 | unused (cleared by the game every frame; not sent) |
| 0xA2 | int16 | damage-direction latch (copied to stats[15] and cleared every frame; not sent) |
| 0xA4 | int16[16] | stats |
| 0xC4 | u8 | deathmatch rank |
| 0xC5 | u8 | number of ranked players |
| 0xC6 | u8 | spectated player |
| 0xC7 | u8 | cinematic freeze |

### User command (24 bytes; G18.20)
| Offset | Type | Field |
|---|---|---|
| 0 | u8 | msec |
| 1 | u8 | buttons |
| 2 | u8 | light level |
| 3 | int8 | lean (−1, 0, +1) |
| 4 | int16[3] | angles |
| 10 | int16 | forward move |
| 12 | int16 | side move |
| 14 | int16 | up move |
| 16 | float | primary fire time |
| 20 | float | alternate fire time |

### Player-movement block (256 bytes, argument of game import #81; G18.21)
| Offset | Type | Dir | Field |
|---|---|---|---|
| 0x00 | 28 bytes | in/out | movement state = player state bytes 0x00–0x1B (pm_type … movement-speed byte) |
| 0x1C | 24 bytes | in | user command (forward, side, up zeroed by the move for pm_type > 2) |
| 0x34 | int32 | in | snap initial (non-zero: the state was changed outside player movement) |
| 0x38 | int32 | out | number of touched entities |
| 0x3C | pointer[32] | out | touched entities |
| 0xBC | float[3] | out | view angles |
| 0xC8 | float | out | view height |
| 0xCC | float[3] | out | mins |
| 0xD8 | float[3] | out | maxs |
| 0xE4 | pointer | out | ground entity |
| 0xE8 | int32 | out | water type |
| 0xEC | int32 | out | water level |
| 0xF0 | function | in | box trace (start, mins, maxs, end) → 56-byte trace result returned by value (Quake II form) |
| 0xF4 | function | in | point contents (point) → int |
| 0xF8 | function | in | ground-entity-is-client (entity record pointer) → int; non-zero refuses a jump |
| 0xFC | float | in | stagger value v: accelerations × max(0, 1 − v) when v > 0 |

### Cvar record (32 bytes; G18.22)
| Offset | Type | Field |
|---|---|---|
| 0x00 | char* | name |
| 0x04 | char* | value string |
| 0x08 | char* | latched string |
| 0x0C | int32 | flags |
| 0x10 | function | change callback (argument: the record) or null |
| 0x14 | int32 | modified flag |
| 0x18 | float | value |
| 0x1C | pointer | next record (engine-owned) |

### Surface record (72 bytes, pointed to by trace result +40; G18.23)
| Offset | Type | Field |
|---|---|---|
| 0 | float[2][4] | texture vectors (texinfo `vecs`) |
| 32 | int32 | texinfo flags; byte 35 (high byte) = material id |
| 36 | int32 | number of animation frames (1 + `nexttexinfo` chain length) |
| 40 | pointer | next surface record of the animation chain, or null |
| 44 | pointer | current image (renderer-owned; null without a renderer) |
| 48 | pointer | original image (renderer-owned) |
| 52 | pointer | current texture-table record (changes when damaged) |
| 56 | pointer | original texture-table record |
| 60 | int32 | texinfo value |
| 64 | int32 | damage counter |
| 68 | int32 | damage stage |
Game modules read only the material byte (35) and pass the pointer to import #25; everything else is engine state.

## Behaviour

### A. SPEC-204 GameSpy (OPEN_QUESTIONS "SPEC-204 GameSpy", items 9–17)

#### GS.9 SPEC-204 §2 and §4 — how masters react to a heartbeat (Resolution)
- Old (§2, last item): "A master that wants to check the server sends ordinary queries (section 3) to the heartbeat's
  source address. Masters usually send `\basic\\secure\<challenge>` or `\status\\secure\<challenge>` and check
  `\validate\` with the secret key (master-side, UNVERIFIED for a particular master)." Open question "Master-side
  behaviour toward heartbeats".
- New: replace the second sentence by the black-box result of 2026-10-22: the three community masters
  (`sof1master.sofvault.org`, `master.333networks.com`, `master.openspy.net`) answer a heartbeat with **only**
  `\secure\<challenge>`, sent from their UDP port 27900 to the heartbeat's source address and port (the query socket).
  sofvault and 333networks use 6-character challenges, OpenSpy 8 characters. The server's reply follows section 3
  unchanged: a lone `secure` word gives `\validate\<response>\final\\queryid\<Qid>.1`; an 8-character challenge gives
  a 12-character response (the server validates the whole value, section "Validation response"). All three listed a
  new server within 14 s of its first heartbeat. After the shutdown heartbeat only OpenSpy queried again, with a plain
  `\status\`; an original process has closed its query socket by then (§2), so that query is never answered. Master
  behaviour is informative only: the server never depends on it.
- Verify: test vector GS-V1 (below) reproduces the observed OpenSpy exchange with the algorithm of the spec.

#### GS.10 SPEC-204 §4 — delisting and sharing between masters (Resolution, informative)
- Old: open question "delisting timeout".
- New: add to §4: community masters keep an entry for at least 18 minutes after the server's last heartbeat (observed:
  an original server stopped at 23:50 was still listed by sofvault and 333networks at 00:08 and later), and they copy
  entries from each other (a server that sent heartbeats only to sofvault was also listed by 333networks). The original
  server sends nothing that would delist it except the shutdown heartbeat with game mode `exiting`. Nothing in the
  original depends on delisting.

#### GS.11 SPEC-204 §1.2 — duplicate records in a master list (Refinement, informative)
- Old: "Duplicate records are not removed. Each is queried, and the menu keeps one record per address (SPEC-803)."
- New: unchanged rule; add the observation that `master.333networks.com` returns duplicates in practice (44 records,
  31 distinct, 2026-10-22). Merging and de-duplicating before querying (OpenMerc) differs from the original only in the
  number of `\status\` queries sent and in which of the duplicate measurements becomes the shown ping; the listed
  servers are the same. Keep it as a documented difference.

#### GS.12 SPEC-204 §2 — heartbeat addressed to 255.255.255.255 (Resolution)
- Old: "Windows refuses such a send without broadcast permission, so no master is reached (UNVERIFIED whether anything
  leaves the host)."
- New: **nothing leaves the host.** The query socket is never given broadcast permission, so the operating system
  refuses the send; the failure is ignored silently, and the developer print `Sending heartbeat to GameSpy...` still
  appears. Confirmed black-box (pktmon, all components, UDP 27900, Windows 11, `public 1`, no hosts entry). A
  reimplementation that resolves the master later (§4 recommendation) must not fall back to a broadcast. Delete the
  open question.

#### GS.13 SPEC-204 §3 "Socket" — bind address of the query socket (Resolution)
- Old: "It binds to all interfaces (UNVERIFIED: confirm with `netstat -an` that the bound address is 0.0.0.0)."
- New: **always all interfaces (0.0.0.0)** in 1.07f. The socket is opened before the game socket exists, so the
  address the game socket later binds to (cvar `ip`, SPEC-200) never applies to it; in addition a loopback address
  127.0.0.1 would itself be turned into "all interfaces". Confirmed black-box: an original instance with its game
  socket on 127.0.0.1:27910 had its query socket on 0.0.0.0:28911. Delete the UNVERIFIED mark, the open question and
  the corresponding item of "Amendments to approved specs".

#### GS.14 SPEC-204 §3 "Socket" and Constants — port search range (Correction)
- Old: "Port search: for base b = `gamespyport` + 0 … + 4, the ports b, b+1, …, b+99 are tried in order, and the
  first that binds is used. In practice this is the first free port in 28911–29110." Constants: "`gamespyport` +
  0…4, each + 0…99".
- New: the first sentence is right and the second is wrong. Up to five attempts are made; attempt k (k = 0…4) tries
  ports `gamespyport` + k … `gamespyport` + k + 99 in increasing order and stops at the first that binds. The attempts
  overlap, so the ports reachable are **`gamespyport` + 0 … + 103** (28911–29014 with the default): the first free
  port among the first 100 is always used; ports +100 … +103 are used only when all of +0 … +99 are taken (attempt k
  then finds +99 + k). Constants row: "`gamespyport` + 0 … + 103 (five overlapping searches of 100 ports)".
  `gamespyport 0` lets the operating system choose the port; the chosen port is then the one reported in heartbeats.
  OpenMerc's "+0 … +199" gives the same port whenever one of +0 … +103 is free, which is every practical case.
  Behaviour when all 104 ports are taken was not examined (UNVERIFIED; no stock situation).
- Verify: occupy UDP 28911–29010 (100 ports) with a helper and start the original: it binds 29011 (attempt 1
  finds +100). Occupy 28911–29014: no port in 29015–29110 is used.

#### GS.15 SPEC-302 `sv_violence` and SPEC-313 "Parental-lock cvars" — the computation, bit 32 (Correction)
- Old (SPEC-302): "Server violence bitmask, one bit per parental lock (1 deaths, 2 blood, 4 gore zones, 8 dismemberment,
  16 gore textures); computed from the locks at start (polarity UNVERIFIED)." Old (SPEC-313 table): `lock_pure` bit
  0x20 "(`cl_violence` only)"; §2 step 5: "`sv_violence` = the bits of the first five, updated only while no server is
  running."
- New: `sv_violence` has six bits, set by two separate rules:
  1. **Bits 0x01–0x10** are the server machine's own locks, as in SPEC-313: `lock_deaths` 0x01, `lock_blood` 0x02,
     `lock_gorezones` 0x04, `lock_sever` 0x08, `lock_textures` 0x10; a lock value other than 0 sets its bit (bit set =
     that kind of violence is suppressed on this server; the game module applies the locks itself, SPEC-720/721). They
     are recomputed whenever a lock changes or latched cvars are applied, but only while no server is running.
  2. **Bit 0x20 = "pure server"**: at the end of every server spawn (each map start) the engine force-sets
     `sv_violence` to its current integer value with bit 0x20 set when the game module file it loaded has the
     checksum 64C0FC54 (hex) **and** the player module file loaded on the server side has the checksum EACC2EEA (hex) (the
     32-bit file checksum of SPEC-301 §8 / SPEC-003; these are the values of the shipped 1.07f `gamex86.dll` and
     `player.dll`), and with bit 0x20 cleared otherwise. Bits 0x01–0x10 are kept. `lock_pure` (client bit 0x20 of
     `cl_violence`) therefore means "join only servers that run the unmodified game and player modules".
  The SPEC-302 default stays "0" before the first map. Observed values decode as: **63** = all five locks on and stock
  modules; **31** = locks on, modified game or player module; **32** = locks off, stock modules; **0** = locks off,
  modified module.
  Consequences (SPEC-200 connect rule (s AND c) = c, SPEC-803 list rule): a client whose six locks are all on
  (`cl_violence` 63, the state of an install whose parental-lock store is missing or invalid, SPEC-313 §2 step 3)
  lists and joins only servers reporting 63. A server reporting 0 is seen and joined only by clients with every lock
  off. OpenMerc's own game module can never match the checksum, so the original rule makes an OpenMerc server
  non-pure (31 at most); whether OpenMerc deviates (for example reports bit 0x20 for its own trusted modules) is a
  clean-team decision to record in DEVIATIONS.
  UNVERIFIED: whether the oracle's 63 comes from locks stored as "on" or from forced locks (both give 63).
- Verify: (a) start the oracle dedicated server with the G18 probe module (or any modified `gamex86.dll`) in place of
  the stock one: `serverinfo` and the `\status\` reply show `sv_violence` 31 instead of 63. (b) Put the stock DLLs back:
  63 again after the next `map`. (c) Data check: the SPEC-301 §8 checksum of the shipped `gamex86.dll` is 64C0FC54 (hex)
  and of `player.dll` EACC2EEA (hex) (computed by the reverse team on the 1.07f files).

#### GS.16 SPEC-204 test vectors 7 — responder edge cases (Resolution)
- Old: test vector 7 (echo, 300-byte datagram, client-only process); §3 rules for word order, `\basic\\packets\`,
  unknown words, 255-byte limit.
- New: add as confirmed on the original 1.07f dedicated server (black-box, 2026-10-22): keywords are handled in the
  fixed order wherever they appear in the request; `\basic\\packets\` repeats the basic block in datagram 1 (one
  basic block from `basic`, then `packets` starts again with the basic block, so datagram 1 holds it twice when it
  fits); unknown words and plain garbage get `\final\\queryid\N.1`; a 300-byte request gets no answer and does not
  advance Qid; a 255-byte request is answered. No rule changes; the UNVERIFIED-free status of §3 is confirmed.
  Note: "does not advance Qid" refines §3 "Reading a query": a datagram longer than the 255-byte buffer is discarded
  before the query counter is incremented.

#### GS.17 SPEC-204 §2 test vector 8 — shutdown heartbeat from every instance (acknowledged, no change)
- The original sends the shutdown heartbeat from every process, as SPEC-204 states. OpenMerc's restriction to public
  multiplayer servers is a deliberate difference; it belongs in DEVIATIONS, not in the spec. No amendment.

### B. G18 game module (OPEN_QUESTIONS "G18 game module", items 18–28)

#### G18.18 SPEC-202 §2 / SPEC-760 §1 / SPEC-752 §5 — in-memory entity state (Resolution and Correction)
- Old: SPEC-202 gives only the wire format; SPEC-752 §5: "every in-use entity flagged as Ghoul-rendered (bit 0 of the
  int32 at entity offset 48) gets each of its four 16-bit Ghoul ids at entity offsets 100, 102, 104, 106 replaced".
  G18 measurement: number 0, origin 4, angles 0x10, yaw offset 0x1C, modelindex 0x20, modelindex2 0x24, frame 0x28,
  skinnum 0x2C, effects 0x30, renderfx 0x34, solid 0x38, sound 0x3C, sound data 0x40, events int[3] 0x44, parameters
  int[3] 0x50, effect slots at 0x5C.
- New: the G18 layout is **confirmed in full** (table "Entity state" below, 116 bytes, the first field of the entity
  record). The two points of doubt:
  - Events and parameters are int[3] at 0x44 and 0x50. Only event 1 was observable because the second and third pairs
    are sent only when the previous event has bit 0x80 ("another event follows", SPEC-202). The probe sees event 2
    when event 1 = 0x80 | id.
  - The 24 bytes at 0x5C are twelve 16-bit values v0…v11. Normally they are the four effect slots {effect id, bolt,
    scale × 16} in that order (slot k = v(3k), v(3k+1), v(3k+2)), as measured. When effects bit **0x0001** ("rotate",
    SPEC-400 *Entity effects bits*, SPEC-771 *Rotation*) is set, the same twelve values carry the rotation block
    instead: v0 = 999 (marker), v1…v3 = angular velocity, v4…v7 = up to four Ghoul instance ids (0 = none). v4…v7 lie
    at entity offsets 100, 102, 104, 106.
  - So **both are right about the offsets**, and SPEC-752 §5's wording is wrong: bit 0 of offset 48 is the rotate bit,
    not a "Ghoul-rendered" flag (Ghoul rendering is renderfx 0x400000, SPEC-202). Corrected SPEC-752 §5 sentence:
    "After the whole level section is read, every in-use entity whose effects field (offset 48) has bit 0x0001
    (rotate) gets the four 16-bit Ghoul instance ids of its rotation block (offsets 100, 102, 104, 106) replaced
    through that map (0 stays 0; an id missing from the map becomes 0)." In-use is the int at entity offset 120
    (SPEC-760 §1).
- Verify: save and load a level with a rotating Ghoul part (SPEC-771 rotation, v4 ≠ 0): after loading the same part
  keeps rotating. Probe: effects = 1 with v0 = 999 and v1 = 512 makes the client rotate the whole entity (v4 = 0).

#### G18.19 SPEC-202 §4 / SPEC-780 §1 — in-memory player state (Resolution)
- Old: SPEC-780 §1 points to SPEC-202 §4, which gives only the wire order. G18 open points: offsets 0x70, 0x7C, 0x80,
  0xA0; whether 0x70 is the view-weapon type; the type of the remote-camera origin.
- New: the G18 table is confirmed; the open offsets are (full table "Player state" below):
  - **0x6C** pointer to the view-weapon Ghoul instance, written by the player module. **0x70** int16: the engine
    overwrites it **every server frame, in the game's own record**, with the network id of the instance at 0x6C (0
    when 0x6C is null); a game must not use it for anything else. **0x72** int16: the view-weapon type = the current
    weapon id (player module; SPEC-602/SPEC-780 weapon numbering; the client treats 6 as the zooming sniper rifle). Its
    low byte is sent only together with a change of 0x70 (SPEC-202 field 1 compares 0x70 only), which is why 0x70/0x72
    were "not observable alone".
  - **0x74** int16 weapon counter A (rounds in the clip) and **0x76** int16 counter B (reserve): the change test of A
    compares all 16 bits, only the low byte is sent.
  - **0x7C** pointer: the game module stores the player's **own body model instance** there (set when the body model is
    built, cleared when it is removed). The engine never reads it in 1.07f: it is neither compared nor sent nor
    converted. A clean game may keep it or leave it 0.
  - **0x80** int16 (+ 2 bytes padding): never written by the game. It is the slot of SPEC-202 field 7 ("remote-view
    model id"), which the 1.07f server never sends; the client would store the id there and the resolved instance at
    0x7C of its copy.
  - **0xA0** int16: cleared to 0 by the game at the end of every server frame and otherwise unused in 1.07f.
    **0xA2** int16: the damage-direction latch: each damage taken stores its direction value (SPEC-780 §3 table,
    stats[15]); at the end of the frame the game copies it to stats[15] (0xC2) and clears it. Neither 0xA0 nor 0xA2 is
    sent.
  - **Remote camera origin 0x4C**: float[3] holding the position **multiplied by 8** (1/8-unit fixed values kept in
    floats). The wire carries trunc(value) as int16 (SPEC-202 field 19); the server uses value × 0.125 as the
    visibility origin whenever the remote-view type byte at 0x68 is non-zero (otherwise origin × 0.125 + view offset).
    0x64 (remote-view entity) is int32, compared as int32 and sent as its low byte; 0x68 (remote-view type) is int32
    in memory but only its low byte is read and sent.
- Verify: probe — set 0x6C to a valid view-weapon instance and 0x72 to 6: the client's zoom field-of-view rule
  applies; write 0x7C, 0x80, 0xA0: nothing changes on the wire.

#### G18.20 SPEC-202 §6 / SPEC-001 export 0x38 — user command (Resolution)
- New: confirmed exactly as measured: 24 bytes — msec u8 0, buttons u8 1, light level u8 2, lean s8 3 (−1, 0, +1),
  angles int16[3] 4, forward int16 10, side int16 12, up int16 14, primary fire time float 16, alternate fire time
  float 20 (table "User command" below). This is also the command embedded in the player-movement block at 0x1C.

#### G18.21 SPEC-001 import #81 / SPEC-780 §5 — player-movement block (Resolution and Correction)
- Old (SPEC-780 §5): "the pmove block gets … a callback that answers 'is entity number n a client' (1 ≤ n ≤
  `maxclients`), and the remaining knock-back stagger time … when positive"; "lean value (−1, 0, +1 as returned by
  pmove)". G18: block layout as measured; 0xF8 callback with one entity pointer; 0xFC "stops all movement while
  positive"; move scale = extra pmove byte; where is the lean returned; total size?
- New:
  1. **Size and layout.** The block is **256 bytes**; the G18 offsets are confirmed (table "Player-movement block"
     below). Nothing after 0xFC is read or written by the engine; the game clears all 256 bytes before filling it.
     Player movement clears touch count, view angles, view height, ground entity, water type and water level at its
     start, so they are outputs only.
  2. **0xF8, "ground entity is a client" callback.** Argument: one entity record pointer (the current ground entity).
     It is called only when a jump would start (jump pressed: up move ≥ 10, jump not held, not dead, water level < 2,
     on the ground). A non-zero result **refuses the jump**: a player standing on another player cannot jump. The game
     module's callback returns 1 when the entity's number (first int of the record) is 1…`maxclients`, else 0; the
     client's prediction uses an equivalent callback. SPEC-780's "is entity number n a client" is this callback; its
     argument is the entity, not the number.
  3. **0xFC, stagger value (Correction of G18's reading).** It does not stop movement outright. When v > 0 every
     acceleration step of the move (ground, air, ladder and water acceleration) is multiplied by max(0, 1 − v); v ≤ 0
     means factor 1. Friction, gravity, jumping (fixed vertical speed) and the slide/step move are not scaled. The game
     passes the remaining stagger time in seconds, so control returns linearly during the last second of a stagger;
     with v ≥ 1 (G18's 1234.5) no acceleration happens, which looks like "stops all movement" when the player stands
     still.
  4. **Move scale.** Player movement does **not** read the extra pmove byte (state 0x1A). The game scales the
     command itself before calling player movement: forward and side move × q/255, truncated, q = trunc(scale × 255)
     AND 255 (SPEC-780 §5); it stores q in player-state byte 0x1A at the end of each server frame. The client's
     prediction repeats the scaling from the received byte before each predicted move: run button 0x20 (unless pm_flags
     0x40) doubles forward, side and up move, then forward and side × (byte 0x1A)/255, truncated. So a byte of 0
     stops horizontal movement only through these two scalings.
  5. **Lean (Correction of SPEC-780 §5).** Player movement neither reads nor returns the lean. After the move the game
     stores the **command's** lean byte (sign-extended, −1/0/+1) as the entity's lean property (SPEC-760 §1). Replace
     "lean value (−1, 0, +1 as returned by pmove)" by "the command's lean value (−1, 0, +1)". Player movement leaves
     the embedded command unchanged except that it zeroes forward, side and up move for pm_type > 2.
  6. The movement rules themselves differ from Quake II in several places; see G18.21b.
- Verify: probe with a marker callback at 0xF8 that returns 1: jumping is impossible while the callback is reached
  (standing on any entity); with v = 0.5 at 0xFC a player accelerates at half rate (time from 0 to 300 units/s doubles).

#### G18.22 SPEC-003 / SPEC-302 — cvar record (Resolution)
- Old: G18: name 0, string 4, latched string 8, flags 0x0C, value float 0x18 (SPEC-003 names only 0x18); 0x10 and
  0x14 unknown (0 and 1 for a fresh cvar).
- New: the record is **32 bytes**: name 0, string 4, latched string 8, flags 0x0C, **change callback 0x10** (the
  fourth argument of the cvar-get import, SPEC-001 #87 / SPEC-002 import 0x40; null for almost every cvar; when set it
  is called with the record when the cvar is created, when a callback is passed for an existing cvar whose modified
  flag is set, and after every value change), **modified flag 0x14** (int,
  Quake II meaning: 1 on creation and set to 1 on every change; cleared by whoever polls it), value float
  0x18, next record 0x1C (engine list link; read-only for modules). Compared with Quake II the callback is inserted
  at 0x10, so modified and value move by 4.
- Verify: probe: `set` a game cvar from the console and read 0x14 (becomes 1 after the module cleared it).

#### G18.23 SPEC-313 "Trace result" and Behaviour 4 / SPEC-001 import #25 — surface record (Correction and Resolution)
- Old (SPEC-313 trace table, offset 40): "hit surface (texture-information record, see Behaviour 5; a shared empty
  record when nothing was hit)". Behaviour 4 describes the 500-entry texture table. G18 measured a 72-byte texinfo
  record: vecs 0–31, flags 32 (material in the high byte), value 36. Which record does #25 expect, and where are the
  material and damage fields?
- New:
  1. Trace +40 points to a **surface record**: one 72-byte record per texinfo of the world map, kept by the engine in
     texinfo order (table "Surface record" below), or to a shared empty record when nothing was hit or the brush side
     has no texinfo. Correct the SPEC-313 row to "hit surface (surface record, see Behaviour 4; a shared empty record
     when nothing was hit)" ("Behaviour 5" there is a wrong reference).
  2. Offsets 0–35 are as G18 measured. **Offset 36 is not the value**: it is the number of animation frames of the
     texture (1 + the length of its `nexttexinfo` chain, SPEC-101, following the chain until it returns to this record;
     a chain longer than 32 stops the count at 33 with the developer message `> 32 animframes on <texture>`). **The texinfo value is at
     offset 60.**
  3. **Material**: the game reads it from the surface record itself, as the high byte of the flags (byte 35), the same
     byte as SPEC-101/SPEC-100. It never reads the texture table.
  4. **Import #25** takes exactly the pointer found at trace +40. The engine keeps the damage state in the surface
     record: offset 52 points to the surface's **current** record of the 500-entry texture table (Behaviour 4), offset
     56 to its original one, offset 64 is the damage counter and offset 68 the damage stage. The texture-table records
     are engine-internal; game modules do not read them.
  5. **Damage counter start (Refinement of Behaviour 4).** At every map load (also when the same map is loaded again)
     each surface record is reset: current texture := original, current image := original image, damage counter :=
     the **health of the original record**, stage := 0. Because #25 adds the amount to the counter and switches when
     the sum reaches the current record's health, the **first damage of any amount ≥ 0 always switches** a damageable
     surface to its first damaged texture; after a switch the counter restarts at 0, so later stages need their full
     health. The surface index sent in `svc_damagetexture` is the record's position in the surface array.
- Verify: shoot a damageable texture (for example a computer screen, material 32) once with the pistol: it changes to
  its damaged texture at the first hit; a second stage needs ⌈health/amount⌉ further hits (`sv_showdamage 1` prints the
  sums). UNVERIFIED in game.

#### G18.21b SPEC-313 §8 — player movement differences from Quake II (Refinement)

- Old: SPEC-313 §8 ends with "The rest of the player movement differences need their own spec." The engine's player
  movement (game import #81 and client prediction) was otherwise only implied to be Quake II 3.19 `pmove.c`.
- New: the player movement of 1.07f is Quake II 3.19 player movement with the differences below. Everything not
  listed is as Quake II 3.19, including: the order of the steps of one move; the angle clamp (pitch limited to
  89/271, teleport pause keeps only yaw); the drop of the timing counter (msec/8, at least 1, clearing
  PMF_TIME_WATERJUMP, PMF_TIME_LAND and PMF_TIME_TELEPORT); the PMF bits 1, 2, 4, 8, 16, 32 and their meaning; ground
  detection (probe 0.25 units down, normal 0.7, start-solid rule, PMF_TIME_LAND with pm_time 18 below −200 and 25
  below −400, waterjump ended on ground); the touch list (32); ladder detection (1 unit along the flat forward
  vector, CONTENTS_LADDER); ladder climbing (±200 at pitch ±15 with forward input, else by up input, horizontal
  wish clamped to ±25, vertical damping by gravity); water and conveyor currents (400, halved at waterlevel 1 on the
  ground; conveyors 100); friction (stopspeed 100, friction 6 on non-slick ground or ladder, water friction
  1 × waterlevel off ladders, speed < 1 stops x and y); swimming (−60 drift, wish speed clamped to 300 then halved,
  water acceleration 10); the swim "jump" (100 / 80 / 50 by water type, not at vz ≤ −300); the dead slow-down (−20
  per move on the ground); the waterjump fall (gravity, ends when falling); the final position snap (eighths, the
  jitter order 0, 4, 1, 2, 3, 5, 6, 7, fallback to the previous origin; velocity snapped to eighths); step size 18,
  step normal 0.7, 4 bumps, overbounce 1.01, stop epsilon 0.1; the teleport pause; gravity from the player state.

  **Movement types** (SPEC-202 field 8): 0 normal; 1 noclip flight (Quake II PM_SPECTATOR: flight with friction 6 ×
  1.5, acceleration 10, no clipping, moves only when the acceleration step adds speed); 2 spectator (SPEC-313 §8);
  3 dead (forced duck, the dead slow-down); 4 gib; 5 and 7 frozen (no movement at all, as Quake II PM_FREEZE);
  **6: duck test, ground and water classification, the timing counter, the waterjump fall and the final snap run,
  but no jump, friction, acceleration or gravity** — the player stays in place. Types 3–7 clear the forward, side
  and up moves first. Types 1 and 2 always count as a good position when snapping.

  **Differences** (each observable):

  | # | Topic | Quake II 3.19 | 1.07f |
  |---|---|---|---|
  | 1 | Bounding box | standing maxs z 32, ducked 4 | standing **40**, ducked **14** (mins (−16, −16, −24), maxs x/y 16 unchanged; gib box and view height 8 unchanged) |
  | 2 | View height | standing 22, ducked −2, noclip 22 | standing **35**, ducked **0**, noclip **35**. With `gserver SIZEMATTERS s` ≠ 1 (SPEC-012): standing s·59 − 24 clamped to −8…39, ducked s·24 − 24 clamped to −10…13 |
  | 3 | Starting to duck | upmove < 0 **and** PMF_ON_GROUND | upmove < 0, **also in the air**, but not while the lean byte of the command (G18 item 20) is non-zero unless already ducked |
  | 4 | Standing up | when the standing box fits | when the standing (40-high) box fits **and** the lean byte is 0; while leaning a ducked player stays ducked |
  | 5 | Duck speed | 100, whenever PMF_DUCKED | **75**, only when PMF_DUCKED **and** PMF_ON_GROUND (ducking in the air keeps 300) |
  | 6 | Air acceleration | 0 by default (then acceleration 1), configurable by CS_AIRACCEL / `sv_airaccelerate`, with the Quake II air-accelerate variant when non-zero | fixed **1** (same effect as the Quake II default); not configurable, no variant |
  | 7 | Wish direction on ground and in air | forward/right from the view angles with **pitch / 3** | forward and right **flattened** (z = 0) and normalised: full horizontal wish speed at any pitch |
  | 8 | Ground acceleration | vertical velocity set to 0 before and after accelerating; with negative gravity the player lifts off | set to 0 after accelerating only; with negative gravity the player stays on the ground (no difference otherwise) |
  | 9 | Acceleration scale (block float +0xFC, G18 item 21) | — | when > 0, every acceleration step of ground, air, ladder and water movement uses accelerate × max(0, 1 − value); friction, gravity, noclip and spectator movement are not scaled |
  | 10 | Jump | vz += 270, at least 270 | vz := **exactly 270**; refused (nothing changes, PMF_JUMP_HELD not set) when the block's callback +0xF8 returns non-zero for the ground entity. The PMF_TIME_LAND lock-out is as Quake II |
  | 11 | Water jump | horizontal velocity 50 × flat normalised forward, vz 350 | horizontal velocity **200 × the x and y of the full (pitched, unnormalised) forward vector**, vz 350, pm_time 255, same contents tests |
  | 12 | Leaving the ground | vz > 180 counts as airborne | vz > **100** |
  | 13 | Landing velocity | — (block disabled in Quake II) | when the ground probe hits any entity (fraction < 1) and vz < 0, vz := 0, even if the surface is too steep to stand on: sliding down steep slopes does not gain falling speed |
  | 14 | Water levels | sampled with view height 22 / −2 | same rule, with the 1.07f view heights: points origin z − 23; origin z − 24 + trunc(trunc(h + 24)/2); origin z − 24 + trunc(h + 24) (h = view height): standing levels 2 and 3 start at origin z + 5 and + 35, ducked at − 12 and + 0 (Quake II −1/+22, −13/−2) |
  | 15 | Slide along planes | clip against all planes, crease for two, keep the slide speed | older "rub" rule: one plane: clip (1.01), then the whole velocity × rub with rub = 1 + 0.5 × (unit velocity before the clip · plane normal); two planes: crease direction × (crease · velocity) × rub of the first plane; three or more planes in one move: stop. Speed is lost when sliding along walls and up slopes |
  | 16 | Step-up choice | compare horizontal squared distances from the start; when the stepped path wins, keep the unstepped path's vertical velocity | compare the displacements **projected on the start velocity** (unstepped: end − start; stepped: end − raised start); the unstepped path wins when its projection is larger or the final ground normal z < 0.7; when the stepped path wins its own velocity is kept unchanged |
  | 17 | Stuck in solid during a slide | vz := 0, stay | vz := 0, developer message `EmergencySnapPosition()`, then the first free position among offsets of −10…+10 eighths per axis in steps of 2 is taken, searched z from +10 down, then y from +10, then x from +10 (first try +1.25 units on every axis); noclip and spectator types take the first candidate untested; if none is free the move continues from the stuck position |
  | 18 | Initial snap (spawn, teleport) | offsets 0, −1, +1 eighth per axis, unchanged position first | offsets **+1, 0, −1**: the first candidate is (+1, +1, +1)/8, so a free spawn point ends 0.125 units higher and off on x and y. Message `Bad InitialSnapPosition` as Quake II |
  | 19 | Touch list | slides always record touched entities | the normal moves record them; the spectator move does not |

- Verify (test vectors; a probe that calls import #81 with synthetic trace and contents callbacks, as G18 used, can
  check all of them against the original):
  1. Crouch-walk: on flat ground, ducked, forwardmove 400, 0.5 s of 10 ms moves from rest: steady speed **75**
     units/s (Quake II 100). In a demo: duck and run straight; the origin advances 75 units per second.
  2. Duck in the air: jump, then press crouch at the apex: the box shrinks (maxs z 14) and the speed stays up to 300.
  3. View: standing eye 35 above the origin (59 above the box bottom); ducked eye at the origin.
  4. Jump on a flat floor (gravity 800): vz = 270, apex 45.6 units, as Quake II. A jump started with a positive
     vertical velocity while on the ground (for example vz = 50 left by sliding up a ramp) gives 320 in Quake II and
     270 in 1.07f.
  5. Water jump with yaw 0, pitch −30 (looking up): velocity (200·cos 30°, 0, 350) = (173.2, 0, 350); Quake II
     (50, 0, 350).
  6. Stagger value 0.25 on the block: one 10 ms ground move from rest with forwardmove 400 gives 300 × 10 × 0.01 ×
     0.75 = 22.5 units/s (unscaled 30).
  7. Upward speed 150 on a flat floor: 1.07f counts the player as airborne (no friction, air control); Quake II as on
     the ground.
  8. Single wall contact: velocity (300, 0, 0) into a wall with normal (−0.7071, 0.7071, 0) for a full 10 ms move:
     clip gives (150, 150, 0)·1.01-overbounce ≈ (148.5, 151.5, 0); rub = 1 − 0.5 × 0.7071 = 0.6464; result
     ≈ (96.0, 97.9, 0) (Quake II keeps ≈ (148.5, 151.5, 0)).
  9. Snap at spawn: a free spawn origin (0, 0, 24) with snapinitial set ends at (0.125, 0.125, 24.125).
  10. Start a move inside a solid whose free space begins 1 unit above: the player is placed 1.25 units up (first
      free candidate in the search order) and `EmergencySnapPosition()` is printed with `developer 1`.
  11. Water up to origin z + 20 while standing: waterlevel 2 (Quake II 2); up to origin z + 30: 1.07f 2, Quake II 3.

#### G18.24 SPEC-780 §2, §4 step 11, §6, §11 step 13 — the button inhibit byte (Correction)
- Old: SPEC-780 §2 field 6: "bit 0x01 set while the attack button is 'not yet re-armed'"; §4 step 11: "Attack button
  not yet re-armed (the next attack press only re-arms it)"; §6: "Afterwards (humans) the attack button is not
  re-armed". G18 observation: the original sends 0 in every deathmatch frame, also right after the first spawn and
  after a respawn.
- New:
  - The game keeps one "attack armed" flag per client. It is **set** by every spawn (§4: both modes, humans and bots,
    also the forced-suicide respawn of §5 step 2), by every command whose buttons contain a fresh attack press
    (attack in this command, not in the previous one), and by the bot driver when it runs a bot's command (SPEC-741). It is
    **cleared** in exactly one place: the deathmatch respawn of a dead **human** in the per-frame start (§6 "Dead
    players"), after the spawn. At the same moment the latched buttons are cleared and the attack bit is forced into
    both the current and the previous buttons, so an attack held through the respawn is not a fresh press.
  - SPEC-780 §4 step 11 is wrong. Replace it with "The attack button is re-armed." The first spawn, the forced-suicide
    respawn, a single-player start and a bot respawn all leave the attack armed. §6 is right.
  - While the flag is clear: each command clears the stored primary fire time, and the weapon think receives
    attack = 0 (§5 step 6 and §6, unchanged). The first fresh press re-arms the flag. That press is not cleared, so it
    fires in the same frame.
  - The byte: at the end of every client frame (§11 step 13), bit 0x01 of the inhibit byte is set while the flag is
    clear and cleared while it is set. The other seven bits are left unchanged; the game never sets them. During
    intermission the frame ends at step 2, so the byte keeps its last value. The engine sends the byte when it changes
    (SPEC-202 field 6).
  - So the byte is 1 only between a human's deathmatch respawn and his next fresh attack press. Without `dmflags` 0x200
    the respawn itself needs an attack press, so a client that taps attack to respawn sees 1 from the respawn frame
    until its next tap. The G18 observation of 0 "after a respawn" fits only if the harness pressed attack again in
    the command after the respawn: the byte is then 1 for one frame. UNVERIFIED; run the test below.
  OpenMerc sends 0 always and differs after every human deathmatch respawn.
- Verify: deathmatch with `dmflags` 0x200 (force respawn); `kill`; press nothing. After the respawn the inhibit byte is
  1 in every frame. The first attack press fires and the byte is 0 from that frame on. Repeat with a bot (SPEC-741):
  its byte stays 0.

#### G18.25 SPEC-760 B1.3, B1.4, B2 / SPEC-780 §10, §16 — entities before the first map entity (Resolution and Correction)
- Old: SPEC-760 creates only the eight-entry body queue (maxclients + 1 … + 8). G18: the first map entity is
  maxclients + 17 (4 → 21, 8 → 25, 16 → 33), one less with `sv_globalrankings 1`. G18 asks what the other entities
  are.
- New: three level-start rules create records before the first map entity after the world:
  1. **Deathmatch player-model precache** (SPEC-780 §16). It runs before the entity text is parsed (B1.3 step 3, "reset
     bodies and items"). For every model of the `.gpl` lists that loads, the game creates a temporary entity, builds a
     body for that model on it (this precaches the body's resources), and frees the entity again. Freeing a record
     numbered ≤ maxclients + 8 does **not** release it: the record stays in use and only its render flags are zeroed
     (B2). So the first eight temporaries stay allocated for the whole level at maxclients + 1 … + 8: classname
     `noclass`, no model, never linked, never sent. Later temporaries get maxclients + 9, are really freed, and are
     reused at once (level time is 0 at that point). With fewer than eight loadable models, only that many records
     stay. Single player has no such precache.
  2. **worldspawn** (B1.4), in this order:
     - when `sv_globalrankings` is 0 and the game is deathmatch, the bot system's level start creates one entity:
       classname `noclass`, never linked, think at level time + 10 s. The think loads the waypoint file (SPEC-741;
       the server prints `Waypoints loaded and placed from file.`);
     - then the body queue: eight entities with classname `bodyque` (in use, never linked);
     - then `script_runner` when `script` is set.
     The `spawn1` … `spawn3` temporaries are really freed, and their numbers are reused.
  3. **Corpses** (SPEC-780 §10) are copied into record maxclients + 1 + n, n = 0 … 7 cyclic. The copy selects the
     record **by number**, not among the `bodyque` records. In deathmatch these are the records left over from rule 1,
     and the `bodyque` records are never used. In single player the two sets are the same records.
  - Resulting numbers in deathmatch with stock data (more than eight player models):
    - maxclients + 1 … + 8: precache leftovers;
    - with the bot system, + 9: the waypoint loader;
    - then the eight `bodyque` records, then `script_runner`, then the map entities.
    The **first map entity is maxclients + 17 without the bot system and maxclients + 18 with it**. In single player
    the `bodyque` records are maxclients + 1 … + 8, and the first map entity is maxclients + 9 (+ 10 with `script`).
  - Data check against the G18 captures (the first baseline is `func_breakable_brush` `*1`, the first block after
    worldspawn on dm/jpndm2):
    - maxclients 8 with the bot system → 26 = 8 + 18. The server log of these runs shows the `bot` string package
      and `Waypoints loaded and placed from file.`;
    - maxclients 8 with `sv_globalrankings 1` → 25;
    - maxclients 4 and 16 → 21 and 33 = + 17. Their server logs show **no** `bot` string package, so the bot system
      did not start in those runs. The G18 rule "maxclients + 17 … one less without bots" therefore mixes the two
      cases. Why those runs had no bot system is UNVERIFIED; check their `sv_globalrankings`.
  - Correct SPEC-760 B2: "Records numbered ≤ maxclients + 8 are not released by a free: they stay in use; only their
    render flags are zeroed." Add rules 1–3 to B1.3/B1.4 and SPEC-780 §10.
- Verify: dm/jpndm2, maxclients 8, `sv_globalrankings 0`: the `*1` baseline is entity 26; with
  `sv_globalrankings 1` it is 25; single player (G6b proxy) tsr1: the first map entity is maxclients + 10 (`script`
  is set). A corpse after a deathmatch death is entity maxclients + 1, the next one + 2.

#### G18.26 SPEC-780 §16 — locomotion sequences of a deathmatch player (Resolution)
- Old: SPEC-780 §16 names idle, ladder, forward jump, lean, grenade throw and waves only.
- New: the player's body animation is chosen by the **human body's movement, standing and posture choices of
  SPEC-722** ("Movement and standing choice", "Other choices"). The choice runs at the end of every client frame
  (§11 step 9) for a living player model (model index 255). The player module supplies these inputs:
  - **Situation:** off the ground → the airborne rules of §16 (unchanged); on the ground with horizontal speed ≠ 0 →
    the movement choice; on the ground and still → the standing choice. A lean takes precedence as in §16.
  - **Target:** a player always counts as a character with a hostile armed target, so the aimed lists are used.
  - **Weapon class:** from the weapon pose of §16. The pose is the SPEC-722 attack type: P = pistols, MP = machine
    pistol, M = assault rifle, machine gun, flame gun, R = sniper rifle, S = slug thrower, shotgun, microwave gun,
    L = rocket launcher, K = knife.
  - **Posture:** crouched while pm_flags bit 0x01 (ducked) is set, else standing.
  - **Facing and movement:** facing = the entity angles (§11 step 4); movement direction = the horizontal velocity.
    The movement destination lies 100 units ahead while **running** (run button 0x20 held and not exhausted, §5) and
    1 unit ahead otherwise. SPEC-722's walk test (destination distance × (1 + facing · aim direction) < 10 → walking
    lists) therefore gives the running lists exactly while the run button counts, whatever the speed.
  - **Directions:** c = facing · movement direction (unit vectors). Strafing when |c| < 0.8, backwards when c ≤ −0.8,
    forwards when c ≥ 0.8. The strafe side follows r = right vector · movement direction: r ≥ 0 (moving right) picks
    the `_lt_` sequence, r < 0 the `_rt_` sequence. The names are mirrored against the movement, and this is what
    the original does.
  - **Crouched:** while the *current* record is crouched, every movement (any direction, walking or running) uses
    the crouched list. Standing up or down goes first through SPEC-722's posture change.
  All stock player models use `enemy/meso` with sequence set `meso_player`. With the sequences that model has, the
  first available entries are:

  | situation | P, MP | K | R | M, S | L |
  |---|---|---|---|---|---|
  | walk forwards | `std_waim_fwd_p_2` | `std_waim_fwd_p_2` | `std_waim_fwd_r_2` | `std_waim_fwd_ms_2` | `std_waim_fwd_l_2` |
  | walk backwards | `std_wbackaim_n_p_2` | `std_wbackaim_n_p_2` | `std_wbackaim_n_r_2` | `std_wbackaim_n_ms_2` | `std_wbackaim_n_l_2` |
  | walk strafing | `std_waimstraf_lt/rt_p_2` | `std_waimstraf_lt/rt_p_2` | `std_waimstraf_lt/rt_r_2` | `std_waimstraf_lt/rt_ms_2` | `std_waimstraf_lt/rt_l_2` |
  | run forwards | `std_raim_n_p_2` | `std_raim_n_p_n` | `std_raim_n_mrs_2` | `std_raim_n_mrs_2` | `std_raim_n_l_2` |
  | run backwards | `std_rbackaim_n_p_2` | `std_waim_fwd_p_2` (no knife entry; UNVERIFIED) | `std_rbackaim_n_mrs_2` | `std_rbackaim_n_mrs_2` | `std_rbackaim_n_l_2` |
  | run strafing | `std_raimstraf_lt/rt_p_cbn` | `std_raimstraf_lt/rt_p_cbn` | `std_raimstraf_lt/rt_mrs_2` | `std_raimstraf_lt/rt_mrs_2` | `std_raimstraf_lt/rt_l_2` |
  | crouched, still | `cch_a_fwd_p_2` | `cch_a_fwd_p_2` | `cch_a_fwd_r_2` | `cch_a_fwd_ms_2` | `cch_a_fwd_l_2` |
  | crouched, moving | `cch_raim_fwd_p_2` | `cch_r_n_pk_n` | `cch_raim_fwd_mrs_2` | `cch_raim_fwd_mrs_2` | `cch_raim_n_l_2` |
  | going down (crouch pressed) | `std_xcrouch_dwn_pk_a` | `std_xcrouch_dwn_pk_a` | `std_xcrouch_dwn_mrs_2` | `std_xcrouch_dwn_mrs_2` | `std_xcrouch_dwn_l_2` |
  | standing up | `cch_xcrouch_up_pk_a` | `cch_xcrouch_up_pk_a` | `cch_xcrouch_up_mrs_2` | `cch_xcrouch_up_mrs_2` | `cch_xcrouch_up_l_2` |

  When crouch is pressed while moving more than 2 units ahead towards the facing (dot > 0.5, i.e. while running),
  `std_xrolltocch_fwd_a_a` (a forward roll into the crouch) is tried before the going-down entries. Two further
  entity conditions apply; whether a player always meets them is UNVERIFIED. Standing still uses SPEC-722's standing
  choice with the same inputs; §16's idle `std_ialert_n_p_n` is one of its results. The playback speed classes of §16
  are unchanged. With no weapon (pose 0) the unarmed lists apply, and `meso_player` lacks most of them (UNVERIFIED).
  OpenMerc's interim choice from `playmoves.gsq` should be replaced by this table, or better by SPEC-722's choice with
  these inputs.
- Verify: in a capture of a client on the original server, read the Ghoul sequence strings of the player entity's
  instance (svc 26/27). Pistol, walking forwards: `std_waim_fwd_p_2`. With run: `std_raim_n_p_2`. Strafing right with
  run: `std_raimstraf_lt_p_cbn`. Shotgun walking backwards: `std_wbackaim_n_ms_2`. Crouched with the sniper rifle:
  `cch_a_fwd_r_2`.

#### G18.27 SPEC-760 B1.5 — material-flag string and material sounds (Resolution and Correction)
- Old: SPEC-760 B1.5 gives the `material` and surface-type tables and leaves the sound list open. On dm/jpndm2 the
  original's string has position 10 set, which no table entry sets alone.
- New:
  - **Configstring 16** is 25 characters. They are set to `n` at level start and written once, after the last entity
    (B1.3 step 5). Characters are set by:
    - the entity `material` table of B1.5 (confirmed);
    - the surface-type table of B1.5. It is confirmed and complete: 32 → 11; 37 → 1; 38 → 1, 11; 39 → 21; 41 → 22; no
      other surface type sets a position;
    - spawn functions and precaches:

    | writer | positions |
    |---|---|
    | every human character body precache (SPEC-722 *Precache*), including the deathmatch player-model precache (G18.25 rule 1) | 10 |
    | `misc_generic_tv_ceiling`, `misc_generic_tv_wall` | 1, 4 |
    | `misc_newyork_gumball` | 1 |
    | `misc_newyork_news_machine` | 1, 22 |
    | `environ_explosion`, `misc_generic_barrel_chemical` | 19, 20 |
    | `misc_generic_garbage_bag`, `misc_newyork_trashcan` | 21, 22 |
    | `func_remote_camera` | 24, set to **`p`** (not `y`) |

    Positions 18 and 23 are never set. Position 10 is therefore `y` on every deathmatch level whose player list loads
    (the player models are human bodies) and on every single-player level with a human character. That is the
    jpndm2 difference.
  - **Sounds.** The positions precache nothing. The sounds come from a separate list of 115 material sounds with one
    "needed" mark each (constants table "Material sound list"; it is the list that begins with
    `Ambient/Gen/Electric/spark1.wav`). Marks are set as follows:
    1. At level start, before the entities, all marks are cleared. For every material id in the map's material list
       (game import "material list", the distinct materials of the map's surfaces), a group is marked:

       | material ids | sounds marked |
       |---|---|
       | 0, 17–24, 33–35 | `player/foot/stone/stone1…4`, `Impact/Surfs/stone`, `Player/Land/Stone` |
       | 2–5 | `player/foot/sand/sand1…4`, `Impact/Surfs/sand`, `Player/Land/Sand` |
       | 6–8 | `player/foot/gravel/gravel1…4`, `Impact/Surfs/gravel`, `Player/Land/Gravel` |
       | 9 | `player/foot/snow/snow1…4`, `Impact/Surfs/snow`, `Player/Land/Snow` |
       | 10–13, 27, 36 | `player/foot/water/water1…4`, `Impact/Surfs/water`, `Player/Land/Water` |
       | 14–16, 40 | `player/foot/wood/wood1…4`, `Impact/Surfs/wood`, `Player/Land/Wood` |
       | 25, 26 | `player/foot/grass/grass1…4`, `Impact/Surfs/grass`, `Player/Land/Grass` |
       | 28–31, 39, 41, 42 | `player/foot/metal/metal1…4`, `Player/Land/Metal` |
       | 32 | as 28, plus `Impact/Explosion/Small` |
       | 37, 38 | as 28, plus `Impact/GlassBreak/GBSmall` |
       | 1, 43 and above | nothing |

       `Ambient/Gen/Electric/spark1.wav` and `spark2.wav` are then marked unconditionally. They are **always**
       precached. G18's journal attributes them to light-fixture props; that is wrong.
    2. Always marked as well: `player/foot/metal/metal1…4`, `Player/Land/Metal`, `impact/surfs/metal1…3`.
    3. For every spawned entity, including the world, by its surface type:
       - 14–16 → `Impact/Break/Wood`;
       - 28–31, 42 → `Impact/Break/Metal`;
       - 32 → `Impact/Break/Metal`, `Ambient/Gen/Electric/spark1`, `Impact/Explosion/Small`;
       - 37, 38 → `Impact/GlassBreak/GBBig`, `GBMed`, `GBSmall`;
       - 0, 2–13, 17–27, 33–36, 39–41 → `Impact/Break/Stone`;
       - 1, 43 and above → nothing.
       The world's surface type is 0, so `Impact/Break/Stone` is always precached. Entities removed by the
       difficulty/mode filter are not counted.
    4. Spawn functions:
       - `misc_generic_trash_ashtray` → `Weapons/FX/Rics/ric1…3`;
       - `environ_explosion`, `misc_generic_barrel_chemical`, `misc_generic_car_rolls`, `misc_generic_tank_gas`,
         `misc_generic_tank_propane` → `weapons/C4/C4Exp`;
       - `misc_generic_tv_ceiling`, `misc_generic_tv_wall` → `Impact/Explosion/Med`, `Impact/Explosion/Small`;
       - `misc_generic_comp_monitor`, `misc_generic_comp_monitor2`, `misc_generic_speaker` →
         `Impact/Explosion/Small`;
       - `misc_generic_desk_phone` → `Impact/Explosion/Small`, `Ambient/Models/Phone/Hit`;
       - `misc_newyork_trashcan` → `Ambient/Models/TrashCan/LidSpin`;
       - helicopters → `enemy/Helicopter/Fire1…3`.
    5. Just before precaching, `Player/Land/Water` is marked unconditionally.
    After the last entity, every marked sound is precached in list order. The jpndm2 observation (break, glass-break,
    explosion-small, stone/wood surfaces, metal1–3, land and foot sounds) is consistent with these rules.
  OpenMerc's sound rules match on the six measured maps, apart from the spark sounds. Its string lacks the position-10
  rule and the prop positions.
- Verify: dm/jpndm2: configstring 16 = `nyyyyyynnnynnnnyyynnnnnnn`, and the sound list contains both spark sounds. A
  single-player map without human characters and remote cameras has position 10 = `n`. A map with a
  `func_remote_camera` has position 24 = `p`.

#### G18.28 SPEC-781 — item models registered at every level start (Resolution)
- Old: no rule. On dm/jpndm2 the original registers `Items/Pick-ups/Ammo_single/ammo_556`, `ammo_rocket` (with
  their fade sequences), `Items/Pick-ups/healthsm` and `healthbig`, although the map has no such items.
- New: at every level start, in all modes and before the entities are parsed (B1.3 step 3, "reset bodies and
  items"), the item system prepares its 40 item kinds (SPEC-781 item numbers). For the kinds below, it registers a
  Ghoul model record (SPEC-791 §1: directory + list name) and stores the item's fade-sequence name, whether or not
  such an item is on the map:

  | item numbers | directory | list name | fade sequence |
  |---|---|---|---|
  | 2–9 (ammo boxes) | `Items/Pick-ups/Ammo` | `box` | `fade_box` |
  | 13 (`item_ammo_sp_auto`) | `Items/Pick-ups/Ammo_single` | `ammo_556` | `fade_ammo_556` |
  | 16 (`item_ammo_sp_rocket`) | `Items/Pick-ups/Ammo_single` | `ammo_rocket` | `fade_ammo_rocket` |
  | 18–28 (weapons) | `enemy/bolt` | `pistol2`, `pistol1`, `machinepistol`, `assault_rifle`, `sniperrifle`, `autoshotgun`, `shotgun`, `machinegun`, `rocket`, `mpg`, `flamethrower` | `fade_` + the same name |
  | 29 (armour) | `Items/Pick-ups/Armor` | `armor` | `armorfade` |
  | 30 (flash pack) | `Items/Projectiles` | `flashpak` | `fade_flashpak` |
  | 31 (C4) | `Items/Projectiles` | `c4` | `fade_c4` |
  | 32 (goggles) | `enemy/bolt` | `nightvision` | `fade_nightvision` |
  | 33 (claymore) | `Items/Projectiles` | `claymore` | `fade_claymore` |
  | 34 (medkit) | `Items/Pick-ups/MedKit` | `medkit` | `fade_medkit` |
  | 35 (grenade) | `Items/Projectiles` | `grenade` | `fade_grenade` |
  | 37 (`item_health_small`) | `Items/Pick-ups/healthsm` | `health` | `fade_health` |
  | 38 (`item_health_large`) | `Items/Pick-ups/healthbig` | `health` | `fade_health` |

  Items 0, 1, 10–12, 14, 15, 17, 36 and 39 register nothing here. The stock data has no `enemy/bolt/pistol2` …
  `flamethrower` or `enemy/bolt/nightvision` list or sequence, so those registrations load nothing. Whether a record
  is still kept for them is UNVERIFIED: compare the Ghoul file list of a map without weapons. The other lists exist,
  so their models are always registered. On jpndm2 this explains exactly the four extra models: the remaining kinds
  are on the map anyway. Separately, the level start of the standard deathmatch, Assassin, CTF, Control, Conquer and Realistic modes
  spawns and frees one 9 mm pistol item (number 19) to precache it (SPEC-790). This happens after the map entities.
- Verify: any map without health items: the Ghoul file list (configstrings from 1497) contains
  `Items/Pick-ups/healthsm` and `healthbig`. The same holds in single player.

### C. Gatekeeper notes on approved specs (item 1)

#### GK.1 SPEC-770 §5.8 — does a "can be pushed over" prop fall? (Correction)
- Old (SPEC-770 §5.8): "… The prop then rotates about the bottom edge on that side: angular speed starts at 30°/s and
  grows by 30°/s every 0.1 s until it has turned 85° from the start, then it is set to exactly 90° …"; Table 1 text:
  "Table 1 with spawnflag 4 ('can be pushed over'): the player use key makes it fall over (§5.8)." Gatekeeper note:
  WP10 reports that the prop is never told which entity to move.
- New: re-checked for this amendment: **in 1.07f the prop never moves.** The only user of §5.8 is
  `misc_generic_table1` with spawnflag 4 (its player-use action). When the player uses it:
  1. the side and the two clearance traces are evaluated as §5.8 describes; if a trace is blocked nothing happens and
     the prop can be used again;
  2. if both traces are free, the fall is armed (pivot, direction and start angles are recorded), the prop's **use-key
     action is removed** and its think is replaced by the fall think, due 0.1 s later;
  3. at that think the fall state has no entity to rotate (the field that should name the prop is never filled in), so
     the fall counts as finished at once: the previous think is restored (due 0.1 s later if there was one), the fall
     state returns to idle, and the prop's angles, origin and box are unchanged.
  Observable result: nothing moves, no sound, and after the first unblocked attempt the table no longer reacts to the
  use key for the rest of the level (the use action is not given back). The angular-speed rule of §5.8 describes code
  that 1.07f never reaches. Stock data: none of the 17 `misc_generic_table1` in the stock maps has spawnflag 4, so no
  stock level is affected.
  Recommendation: implement steps 1–3 (no fall, use action removed) for parity, and keep the §5.8 animation only
  behind a DEVIATIONS switch. Replace §5.8's first sentence by "Armed by the player use key of `misc_generic_table1`
  with spawnflag 4 (the only user)" and add the paragraph above; change the Table 1 sentence to "the player use key
  arms a fall that never happens in 1.07f (§5.8); the use key is then disabled."
- Verify (black box, needs a test map because no stock map has the flag): a map with one `misc_generic_table1` with
  `spawnflags 4` in an open room. Use it from the front: it does not move, and a second use does nothing. Repeat with
  the table against a wall in the push direction: nothing happens, and a later use from a free side is still accepted
  (then also nothing moves).

### D. I3 Ghoul model system (items 1–15)

#### I3.1 SPEC-013 §4 "Delta bases" — which stored state is the base (Correction)
- Old: "The base for a record is the state stored for the client's acknowledged frame (frame offset = current −
  acknowledged), or the object baseline (offset 0) when none is stored." OpenMerc interpretation: the newest state
  stored at or before the acknowledged frame.
- New: the acknowledged frame is kept **per instance and per client**, not per client. It only moves to frames in
  which that instance's record was actually sent to that client. Rules:
  1. While writing a client frame, the server remembers, for that frame, the set of instance ids whose records were
     written **and fitted the byte budget** (a record dropped by the budget is not in the set). A frame in which no
     record was written stores no set.
  2. When the client acknowledges frame F (system method 19), the server takes the set stored for exactly F, and for
     every instance in that set (still existing) sets that instance's acknowledged frame for this client to F. The
     acknowledged frame of every other instance does not change. Sets of frames older than F are discarded.
  3. Per instance the server then discards stored states whose frame is older than the smallest acknowledged frame
     of all clients of that instance. The state of an acknowledged frame itself is never discarded this way.
  4. When an instance is offered for a client that it does not know yet, its acknowledged frame for that client
     starts as "none". A negative acknowledgement sets "none" for every instance.
  5. The base is the state stored for **exactly** that instance's acknowledged frame A; the frame offset is
     current − A. With "none", or when the state of A is no longer stored (the 20-state limit dropped it), the base
     is the object baseline (offset 0).
  6. Equal to its base: the record is not written and **no state is stored** for the current frame. Otherwise the
     record is written, and the current state is stored under the current frame unless a state is already stored for
     that frame. When storing would exceed 20 states, the oldest are dropped first. A state is stored even when the
     record is then dropped by the byte budget, but such a state never becomes a base (rule 1).
- Consequences: a still instance is not written again after its last written record has been acknowledged, and is
  never resent against the baseline in steady state. OpenMerc's rule gives the same base when no packet is lost and
  no record is dropped by the budget. It differs when (a) the frame that carried the instance's last record was lost
  but a later frame without it was acknowledged: the original keeps the older acknowledged base, OpenMerc would pick a
  base the client never received; (b) a record was dropped by the budget: OpenMerc must not use that state.
- Verify: capture of the original server with a client that drops one svc_frame containing a moving door's record and
  acknowledges the next frame: the door's next record has a frame offset counted from the frame before the dropped
  one. In an undisturbed capture, an instance that stops for N frames and then moves again carries frame offset N + k
  (k = acknowledgement latency in frames), not 0.

#### I3.2 SPEC-012 §5 — raw phase of a paused instance (Correction)
- Old: "In mode 1 it is 1.0 once d > D; paused or D = 0 gives A." OpenMerc interpretation: a paused instance's raw
  phase is A itself (a time), so the first method-45 call after Pause delivers up to two cycles and EOS.
- New: for a paused instance the raw phase is evaluated **at time A**, not equal to A: r = (A − S)/D (mode 1: 1.0
  when A − S > D; other modes as before). Only D = 0 gives r = A. Pause (method 19) sets paused = 1, A := t and the
  pause-end value 1e30, and leaves the event cursor alone. Hence:
  - the first method-45 call after Pause delivers the keys between the cursor phase and the pause point (an ordinary
    catch-up, at most to the phase at the pause time), and later calls while paused deliver nothing;
  - exception: if the frozen phase has a fractional part above 0.9999, `EOS` is queued on every call while paused
    (span − 1 > −0.0001); this follows from the §5 rule and needs no special case;
  - Resume(t) keeps the phase continuous (S moves by the paused span), sets A := t, cursor time := t and cursor
    phase := r(t), so nothing is delivered twice and nothing is skipped.
  OpenMerc's literal reading differs from the original.
- Verify: Play a mode-0 sequence with D = 1.0 at t = 10, method 45 at 10.2, Pause at 10.5, method 45 at 10.6, 11.0 and
  12.0: keys in [0.2 s, 0.5 s) are delivered once (at the 10.6 call), no EOS, nothing at 11.0 and 12.0.

#### I3.3 SPEC-011 method 4 and method 19 — repeated calls (Resolution)
- Old: "Switching on at `time` restarts event processing from `time` for it and all its bolt-ons." OpenMerc
  interpretation: only on the off → on transition; a second Pause keeps the frozen pose.
- New:
  - Method 4 with on ≠ 0 does the restart on **every** call, also when the instance is already on: A := time, cursor
    time := time, cursor phase := r(time) (when it has a sequence), and the same recursively for every bolt-on with
    its own A. Keys between the previous cursor and `time` are therefore skipped, a paused instance's frozen pose
    moves to `time`, and a mode-3 fraction is replaced (SPEC-012 §3 side effects). Method 4 with on = 0 only clears
    the flag. OpenMerc differs.
  - Pause (method 19) on an already paused instance sets A := t again (and pause-end 1e30). The frozen pose becomes
    the pose at the new t, computed from the unchanged S (as if the instance had kept playing until the second Pause).
    A later Resume counts the paused span only from the second Pause. OpenMerc differs.
- Verify: Play mode 0 at t = 10, Pause(10.2), Pause(10.7): the drawn pose is that of 10.7. With a note logger:
  method 45 at 10.1, then method 4(on, 10.6), then method 45 at 10.8: keys in [0.1 s, 0.6 s) are never delivered.

#### I3.4 SPEC-012 §3 — frame pair at f = 1 in mode 0 (Resolution)
- Old: "mode 0: x = N·f, A = ⌊x⌋, B = A + 1 (0 when A + 1 = N)". OpenMerc interpretation: x = N gives frame 0.
- New: the original yields the pair A = N, B = 0, weight of A = 1, so frame N dominates. Frame N is addressed in the
  model file's frame data as "first frame of the sequence + N", i.e. the frame stored right after the sequence's last
  frame: normally the first frame of the next sequence stored in the same file; for the file's last sequence it is
  past the end of the data (undefined). f = 1 occurs in mode 0 when:
  - the reverse flag is set and frac(d/D) = 0, including **d = 0** (evaluation exactly at the start time S, which is
    common on the server: Play at level time t and a bolt or trace query in the same frame);
  - the reverse flag is set and d < 0: "frac" is the C remainder (sign of the dividend), so a negative d gives a
    negative fraction, which is clamped to 0, and reversed to 1 (refines §3: times before S give 0 without and 1
    with the reverse flag, never a wrapped value, and the same holds for mode 2);
  - D = 0 with N > 0 (a 0 ms frame time): f = A clamped, normally 1.
  Implementations should use frame 0 (OpenMerc's choice). The only visible difference is the original's one-frame
  glitch of a reverse mode-0 playback at its start instant. UNVERIFIED whether stock data has a 0 ms frame time.
- Verify: data check: list stock sequences with frame time 0. In game: none expected to be visible.

#### I3.5 SPEC-012 §5 item 3 — bolt-ons of a parent that is not processed (Resolution)
- OpenMerc interpretation: when the parent is off, has no sequence or has note processing off, its method 45 processes
  nothing, bolt-ons included.
- New: confirmed. Method 45 does nothing at all unless the instance it is called on has a sequence, is on and has note
  processing enabled. Inside the traversal, an instance that has no sequence, is off, or whose sequence's model file
  is missing ends the traversal at that level, so **its bolt-ons are not visited** either. A bolt-on is visited only
  when its own note flag is on. OpenMerc matches.
- Verify: a weapon bolted to a character with a logging note callback on the weapon: switch the character off
  (method 4, 0): the weapon's notes stop although the weapon itself is on.

#### I3.6 SPEC-011 method 36 (Clone) — what is copied (Correction of the OpenMerc interpretation)
- OpenMerc interpretation: everything except id, parent and children.
- New: the clone is a new instance of the same object (new id) that receives a copy of the **instance state** (the
  SPEC-013 §3 field set: transform with inverse, determinant sign and scale; attach bolt; on; speed index; S; A;
  pause-end; paused; reverse; tint; sequence; mode; part flags; skin overrides) plus duration D, frame time F and
  frame count N. Everything else has the values of a newly created instance: no parent, no children, user value NULL,
  no note or matrix callbacks, note and matrix processing off, event cursor time and phase as new, render and lighting
  caches reset. With `withChildren` ≠ 0 every child is cloned with `withChildren` = 1 (so the whole subtree) and
  attached to the clone with method 28 at the same parent bolt and the child's own attach bolt, in the original
  attach order. OpenMerc differs in callbacks, user value, cursor and processing flags.
- Verify: clone an instance that has a note callback and user value; method 22 on the clone returns NULL and method
  45 on the clone calls nothing until method 23 is used.

#### I3.7 SPEC-012 §8 "Materials" — how specular is drawn (Refinement)
- Old: "The exponent is 2^(10·glossiness), and slot 1's texture (if any) is the specular map." OpenMerc
  interpretation: an extra additive pass, untextured without a specular map, textured with it.
- New (software lighting, the default path): confirmed. The specular term is **not** added to the vertex colour. After
  the base pass of a mesh, a material with specular on is drawn a second time:
  - texture: slot 1's texture when the material has one, otherwise texturing is disabled;
  - blend (ONE, ONE), depth writes off, polygon offset on with the system's offset values (system method 39);
  - vertex colour = the per-vertex specular term (SPEC-012 §8 lighting);
  - state restored afterwards (blend off, (SRC_ALPHA, ONE_MINUS_SRC_ALPHA), offset off, depth writes on).
  Refinements of the term itself: when slot 1 has a texture, the specular colour used is white (1, 1, 1), the map
  supplying the colour; otherwise it is the material's specular colour. In both cases it is multiplied by the
  render-parameter fog visibility. Specular is on when the system specular flag is on, glossiness > 0, specular level
  ≥ 0.051 (exactly 0.051 counts as on) and self-illumination < 0.99.
- Hardware lighting (`ghl_light_method` > 2.5): the term comes from OpenGL lighting in the base pass: material
  GL_SPECULAR = specular colour × specular level (alpha = opacity), GL_SHININESS = 4 × the exponent (OpenGL 1.1
  rejects values above 128, so glossiness above 0.5 keeps the previous shininess). With a specular map the mesh is
  drawn twice: the specular map lit by the specular term only (ambient and diffuse black), then the base texture added
  with (ONE, ONE) (pass details UNVERIFIED).
- Verify: a stock model with glossiness > 0 and no slot-1 texture drawn over a black background with lights off except
  one omni light: the highlight appears even where the base texture is black (additive), and turns off with
  `ghl_specular 0`.

#### I3.8 SPEC-012 §8 "Shadows" items 5–8 — projected mask placement (Resolution and Correction)
- Old: "Projection basis along origin − light point … The two axes are scaled by 32/size … truncated and clamped to
  −30…29 … drawn three times, offset by (0,0), (−1,−1) and (+1,+1) texels". OpenMerc interpretation: posed vertices
  projected along the light direction onto the ground plane, world X/Y as mask axes, mask drawn as a 2·size square.
- New:
  1. Basis (set up once per drawn top-level instance; bolt-ons stamp into the same mask): â = normalise(origin − light
     point) ((1, 0, 0) when its squared length < 0.01). X̂ = normalise(â × e), where e is the coordinate axis X, Y or Z
     giving the longest cross product (scan order X, Y, Z; a later axis wins only when strictly longer, as in SPEC-014
     §12). Ŷ = normalise(â × X̂). Scale k = 32/size, size as in item 5.
  2. Every posed vertex p of every drawn mesh, in world space, gives mask coordinates u = trunc(k·(p − origin)·X̂),
     v = trunc(k·(p − origin)·Ŷ) (truncation toward zero), clamped to −30…29, texel (u + 32, v + 32). This is an
     orthographic projection onto the plane through the origin perpendicular to â, **not** a projection onto the
     ground. The touched rectangle [umin, umax] × [vmin, vmax] is tracked; with no vertex nothing is drawn.
  3. Before drawing, the rectangle grows by one texel on every side (umin − 1 … umax + 1, same for v; the blur stays
     inside the original rectangle).
  4. The quad has 4 corners with texture coordinates (s, t) = (umin/64 + 0.5, vmin/64 + 0.5), (umin, vmax),
     (umax, vmax), (umax, vmin) in the same /64 + 0.5 form (grown bounds). Their positions are the mask points
     (umin − g, vmin − g), (umin − g, vmax + g), (umax + g, vmax + 1), (umax + g, vmin − 1), with g = 16 × the
     downward trace fraction f (quirk: the last two corners use 1 instead of g for v, so the quad is a trapezoid when
     the model is above the ground). Each mask point (u, v) is mapped back to world space as
     origin + (u·X̂ + v·Ŷ)/k, then x and y are shifted by (origin − light point).x and .y — for shadow-only drawing
     additionally by the x and y of the render-parameter directional vector (offset 0x14) — and z is replaced by the
     ground height.
  5. Mode 2 draws this quad three times: unshifted, shifted by (−1, −1, 0) and by (+1, +1, 0) **world units** (not
     texels), each with colour (1, 1, 1, darkness × 0.3333). Mode 1 draws the same corners once with texture
     coordinates (0, 0), (0, 1), (1, 1), (1, 0) and alpha = darkness.
  6. Darkness (item 2) is further multiplied by the square of the instance's tint alpha (A/255)².
  OpenMerc's square and ground projection differ in shape for slanted lights (the original's footprint does not
  stretch with the light angle; the z clamp keeps the slant below about 41°) and lack the height-dependent growth g.
- Verify: `ghl_shadows 2`, a character on flat ground under a single light offset sideways: the shadow outline matches
  the silhouette seen from the light direction, displaced away from the light by the horizontal light offset, not
  stretched. Lift the character (noclip) 250 units: the shadow grows on one side by 8 texels more than on the other.

#### I3.9 SPEC-012 §8 "Model lights" and SPEC-014 §7 — when a light node is active (Correction)
- Old: "A light is **active** in a frame only when brightness ≥ 0.01 and multiplier ≥ 0.01." OpenMerc uses only that.
- New: a light node contributes (to the model's own light list or to the "lights from models" list) only when all of
  these hold in the current pose: its part is shown (instance part flags, method 8); its **visibility track** (NODE
  bool TRACK, SPEC-014 §7) is true at the dominant frame; brightness ≥ 0.01; multiplier ≥ 0.01. OpenMerc differs in
  the first two conditions.
- Verify: hide the part of a `WORLD_*` light node with method 8 (or use a frame whose visibility key is 0): the
  dynamic light it adds to the world disappears.

#### I3.10 SPEC-014 §7 — LIGHT record of kinds 1–4 (Resolution)
- Old: "kinds 1–4: further tracks …; not used by stock data, not specified (UNVERIFIED)". OpenMerc fails such a file.
- New: the record can be parsed. Every TRACK uses the usual encoding (bool "constant"; constant → one value; else u32
  count + count values). After NODE, u8 world and u32 kind:

  | Kind | Tracks, in file order |
  |---|---|
  | 0 omni | vec3 position, vec3 colour, f32 brightness, f32 multiplier, f32 attenuation start, f32 attenuation end |
  | 1 target spot, 3 free spot | the six tracks of kind 0, then vec3, f32, f32, vec3, vec3 |
  | 2 directional, 4 target directional | the six tracks of kind 0, then vec3, f32, f32, vec3, vec3, f32 |
  | above 4 | nothing more is read; the node has no light |

  The meanings of the extra tracks (cone axis or target, hotspot and falloff angles, aspect) are UNVERIFIED; reading
  them is enough to stay aligned with the file. Kinds above 4: OpenMerc matches.
- Verify: write a synthetic file with a kind-1 light carrying constant tracks and a mesh after it; the mesh loads.

#### I3.11 SPEC-012 §1 "Lookups" — names whose registration returned 0 (Resolution)
- OpenMerc interpretation: unknown names are findable; duplicates map to the lowest id.
- New: the lookup table built on the first call holds **every** registered entry, including names that no loaded
  file has (registration result 0), so such names are found and return their id. OpenMerc matches. For a name
  registered twice the original returns one of its ids, decided by an unstable sort and a binary search (not
  necessarily the lowest). No stock data registers a part, material or skin name twice, so "lowest id" is acceptable.
  The name is upper-cased before the lookup; a name never registered on the system gives 0.
- Verify: register `NOPE` (method 6 returns 0), then method 7 `NOPE` returns the consumed id.

#### I3.12 SPEC-011 method 14 — tint quantisation (Resolution)
- New: per channel: value > 1 → 255; value < 0 → 0; otherwise trunc(value × 255), truncation toward zero (not
  rounded). A not-a-number value gives 0. OpenMerc matches. Method 15 returns byte / 255.
- Verify: method 14 with 0.999 then method 15 returns 254/255.

#### I3.13 Deliberate differences — confirmation (no spec change)
- Method 28: confirmed. The original only appends the child to the new parent and overwrites the child's parent,
  parent bolt and attach bolt; the old parent keeps its entry. Consequences (UNVERIFIED in game): the child is also
  drawn, traced and given events through the old parent, and destroying the old parent destroys the child while the
  new parent still refers to it.
- `gserver RELOAD`: confirmed. Every cached model file is deleted regardless of references and nothing is re-loaded
  for live objects, so they refer to freed data (use is undefined; a crash is likely, UNVERIFIED).
- `.gpm` fields: confirmed. Tokens are copied into the fixed fields of SPEC-012 §2 without a length limit; a longer
  token overwrites the following fields. Truncation is a safe replacement.
- Speed index: confirmed. Method 6 stores any value and the duration computation reads the multiplier table at that
  index without a check; an index outside 0…12 yields an unrelated value (index 13 happens to give 1.0). Clamping is
  acceptable. UNVERIFIED whether a stock module ever passes an index outside 0…12.

#### I3.14 SPEC-011 axes — who turns −Y-facing models to Quake forward (Resolution)
- Observation: stock character and first-person models face −Y in model space; SPEC-011 says Ghoul axes follow Quake.
- New: the rotation is in the **instance transform**, set by the module that owns the instance. Neither the Ghoul
  system, the renderer nor the `quake_origin` bolt rotates models. The rotation R used everywhere is −90° about Z:
  matrix rows (0, 1, 0), (−1, 0, 0), (0, 0, 1), i.e. model −Y → +X (forward), model +X → +Y (left).
  - Game module, player body: transform = R with translation (0, 0, −24), and the instance's own attach bolt (method
    29) set to the part `quake_ground` (so with SPEC-012 §6, XF = Ba⁻¹·T puts that bolt 24 units below the entity
    origin, i.e. on the floor of a standing player).
  - Game module, AI characters (humans, dogs, bulls): transform = R × uniform scale (the character's scale, normally
    1).
  - Player module, view weapon (server copy and the client's predicted copy): transform = R, translation 0.
  The transform reaches clients through group 11 of the instance state (SPEC-013 §3).
- Verify: in a capture, the first state of a player body instance carries a group-11 rotation whose decoded matrix is
  R and a translation z = −24.

#### I3.15 SPEC-201 B.3 — non-zero pad bits after a literal string (Refinement, informative)
- Observation confirmed. The original encodes literal strings into one process-wide scratch block that is never
  cleared and writes exactly the code bits, so the unused bits of the last byte keep whatever an earlier, longer
  encoding (Huffman or raw) left in that byte. Their value depends on the strings encoded before in the same process.
  Receivers ignore them; writing zeros is correct.

### E. I5 client effects and temp entities (items 1–19)

#### I5.1 SPEC-400 §Particle simulation 5 — integer alpha (Resolution)

- Old: "alpha += dt·R … The result is truncated and clamped to 0..255." OpenMerc interpretation: the stored alpha is
  an integer, truncated at every step.
- New: **the stored alpha is the colour byte itself (0..255).** For alpha styles 0 and 1, and for every particle not
  made by an emitter, each step computes byte + dt·R in floating point, truncates toward zero and clamps to 0..255;
  the result is the new byte. There is no hidden fractional accumulator. Consequences that a clone must reproduce:
  - a rising rate with dt·R < 1 (R < `fx_sim_time` per second) never raises the alpha; a rising rate raises it by
    floor(dt·R) per step;
  - a falling rate lowers it by ceil(|dt·R|) per step when dt·R is not an integer (at least 1);
  - example: 155 with R = −155/1.5 at 60 steps/s loses 2 per step and drops below 2 (particle freed) after 77 steps,
    about 1.28 s instead of 1.5 s.
  Alpha styles 2 and 3 are recomputed from scratch every step (no accumulation), so truncation does not build up.
  Spin particles (SPEC-401 *Burning debris*) use the same byte arithmetic, clamped at 0 only.
  OpenMerc's integer alpha **matches the original**.
- Verify: in game, `effects/canthear.eft` (style 3) fades smoothly; a temp-entity puff with α = −alpha/lifetime (for
  example TE 45 with alpha 155, α = −1.6667 × 155 ≈ −258/s, dt·α ≈ −4.3) disappears after ceil(155/5) ≈ 31 steps
  (0.52 s) instead of the nominal 0.6 s — measure with `fx_sim_time 60` and a frame capture.

#### I5.2 SPEC-400 §Timebase 1 and §Rendering hand-off — what the picture shows (Correction)

- Old: "the client first hands the current particles … to the renderer, then advances the effects system";
  "Submission happens once per rendered frame before the simulation steps of that frame, so the picture shows the
  state after the previous frame's steps." OpenMerc steps first, then hands over the lists.
- New: per rendered frame the original does, in this order:
  1. **submission** (SPEC-400 hand-off steps 1–5, including the freeing pass): the renderer lists receive
     *references* to the live particle, decal and debris records and one reference per emitter chain; the chain's
     bounding box, coverage weight and quad count are computed now; dynamic lights and light styles are *copied*;
  2. the **effects steps** of the frame;
  3. **RenderFrame** (SPEC-002), which reads the referenced records.
  So the picture shows every submitted record in its state **after this frame's steps** (position, size, rotation,
  colour, alpha) — the same as OpenMerc for those records. The submission order only decides *which* records are
  drawn and the chain summaries. Differences from "steps first, then hand over":
  - free particles, decals and debris created during this frame's steps are not in the lists; they are first drawn in
    the next frame;
  - emitter particles created during the steps **are** drawn in this frame when their emitter's chain already had a
    particle at submission (new particles are appended to the chain's end); otherwise from the next frame. A newly
    started effect therefore shows its first particles one frame later than in OpenMerc;
  - records that die during the steps are still drawn in this frame with their final state; they are freed at the
    next submission;
  - chain bounding box (culling), weight and count are the pre-step values; particles appended or moved during the
    steps are not in the box;
  - "One Frame" particles: a free one is drawn in exactly one frame (the first submission after its creation, which
    kills it). An emitter one is killed at the first chain build after its creation, but it stays linked and is drawn
    in that frame too, and also in its creation frame if its chain was already non-empty then: it can be visible in
    two consecutive frames;
  - level-of-detail marks set by the renderer in frame F are read at the chain build of frame F + 1 (as OpenMerc);
  - dynamic lights and light styles are not changed by the steps; a light fired by an `.eft` light primitive during
    the steps is first drawn in the next frame.
  Replace both sentences by this rule. OpenMerc's order is visually equivalent except for the one-frame differences
  listed; exact-replay comparisons need the original order.
- Verify: with `fx_sim_time 2` (one step every 0.5 s) start a moving temp-entity puff (TE 26): its first visible
  position is already advanced if a step ran in the arrival frame. A `.eft` with a One Frame emitter particle (stock
  muzzle flashes) appears in up to two consecutive frames in a frame capture.

#### I5.3 SPEC-401 §Notation — order of the two puff dimensions (Correction)

- Old: "A puff is a particle defined by: sprite, size w×h, …". OpenMerc interpretation: the first number is the
  second dimension (alpha-quad +0x10, along right), the second number the first dimension.
- New: in every SPEC-401 size "a × b", **a is the alpha-quad field +0x0c** (SPEC-400's first dimension "Height",
  SPEC-412 §8 half-extent along the camera up axis A) **and b is +0x10** (second dimension, along the right axis B).
  OpenMerc's assignment is **reversed and must be swapped.** Affected non-square entries:

  | Entry | +0x0c (along up) | +0x10 (along right) |
  |---|---|---|
  | type 11 drop | 1.5·size | size (rotation π: still vertical, so the drop is 1.5 times as tall as wide) |
  | type 43 drops | 0.5 + 0.08i | 1.5 + 0.08i |
  | fire puff of the fire+smoke trail | 2k·(parent +0x10) | 2k·(parent +0x0c) |
  | smoke puff of the fire+smoke trail | k·(parent +0x10) | k·(parent +0x0c) |
  | coloured-smoke trail | 2k·(parent +0x10) | 2k·(parent +0x0c) |
  | fire trail (0.05 s) | parent +0x10 | parent +0x0c |

  So in *Burning debris* "w, h = parent's dimensions" means w = parent +0x0c, h = parent +0x10, and every trail puff
  **swaps** the parent's two dimensions. Correction to the "fire (interval 0.05 s): one fire puff (expfire1) as
  above" line: its size is **h × w without the factor 2k** (a factor rand(2.5, 3.0) is drawn and not used); colour,
  life, velocity, ω, g and α are as above. All stock burning bits are square and grow equally in both dimensions, so
  the swap itself is invisible in stock play; the missing factor is not.
- Verify: TE 11 with size 8: the drop is taller than wide (12 vs 8 half-extent units). Burning-bit fire trails of
  TE 19 (fire+smoke) show fire puffs about twice the bit's size, the TE 19 "fire trail" bits (8 × 8) puffs of the
  bit's own size.

#### I5.4 SPEC-401 type 6 — `Surface out of range!!!` (Resolution)

- Old: "style > 42 → `Surface out of range!!!`, nothing." OpenMerc interpretation: the material is tested.
- New: the text is literal: 1.07f compares the **style byte** with 42 (an original quirk; the message was meant for
  the material). The material is tested separately:

  | Case | TE 6 | TE 7 |
  |---|---|---|
  | style byte > 42 | message, nothing else | message, nothing else |
  | style 3..42 | treated as style 0 | treated as style 0 |
  | impact without material (kind 1) | nothing, no message | nothing, no message |
  | material 43..255 (possible only for impact kinds 2 and 3, payload byte) | nothing, **no message** | message, nothing else |
  | material 0..42 | as specified | as specified |

  Kind 0 impacts cannot carry a material above 42 (an out-of-range surface material becomes 0, SPEC-400). Stock
  servers send styles 0..2 only (SPEC-771), so the style check never fires in stock play; OpenMerc's material test
  differs only in printing the message for TE 6 with a material above 42.
- Verify: a hand-made TE 6 with style 50 prints the message; one with style 1 and a kind-3 impact of material 60
  prints nothing and shows nothing.

#### I5.5 SPEC-401 type 6 style 1 with the debris flag — chunk count (Correction)

- Old: "Style 1 with debris flag: one impact chunk burst with scale 25." OpenMerc: 6 chunks.
- New: **exactly one chunk** (scale q = 25 in the *Impact chunks* formula), subject to the impact-chunk rules: only
  materials with a chunk entry, dropped when the previous impact-chunk spawn was less than 200 ms ago with the same
  material, and the settle behaviour. OpenMerc's 6 **differs**. The style-1 debris chunk is thrown before the type
  test, so **TE 7 with style 1 and the debris flag also throws this one chunk**, before its own 6-chunk burst
  (correct type 7 "style and debris ignored" accordingly; see I5.6).
- Verify: with an attack that sets the debris flag (SPEC-782), hit metal (material 1, no chunk entry): no chunk; hit
  a tiled wall (materials 17–24): one small wall chunk per hit, at most one per 0.2 s for the same material.

#### I5.6 SPEC-401 type 7 — mark power and hole sprite (Correction)

- Old: "Impact (style and debris ignored): 6 impact chunks … then mark and impact sound, then the material effect with
  power 12 and zero direction." OpenMerc: the power byte and the material's bullet hole, as type 6 style 1.
- New: the style byte is **not** ignored. TE 7 uses the same impact preparation as TE 6: mark mode 4 (bullet) and
  - style 1: power = power byte, hole = the material's bullet hole (and the one debris chunk of I5.5 when the
    debris flag is set);
  - style 2: power 0, hole sprite 34 (slash);
  - style 0 (and 3..42): power 0, the material's hole for kind-0 impacts, none otherwise.
  Then the 6-chunk burst (scale 300, material-18 chunk model), then mark and impact sound with that power and hole,
  then the material effect with power 12 and zero direction. OpenMerc **matches only for style 1**. Since TE 7 carries
  the attack's mark style (SPEC-771), bullets (style 1) give a normal bullet hole, knife slashes and style-0 attacks
  the ≤ 0.1-unit mark and an impact sound at volume 0.6.
- Verify: knife a breakable damage-stage surface (TE 7, style 2): no visible hole, impact sound, chunks; shoot it
  (style 1): bullet hole.

#### I5.7 SPEC-401 §Electric arc and recursive bolt — sprite, smoothing, branch start (Correction)

- Old: "Each of the n − 1 segments is a line (lit, additive, life 200 ms, white 250 ×4, half-width L × 0.14, α =
  −500)"; "smoothed: each new point is 0.3 × straight position + 0.7 × jittered position and the previous point is
  pulled back by 6 % of the difference; the last point is exactly `end`". OpenMerc: sprite `lightning`, the previous
  point moves 6 % of the way to the new point, recursive branches start at m.
- New:
  - **Sprite and flags.** In both paragraphs "lit" is the **sprite** `textures/sprites/lit` (client sprite 7), not
    the lit flag. Arc segments, arc branches and recursive-bolt lines are additive only (no 0x0008). OpenMerc's
    `lightning` and any lit flag **differ**.
  - **Arc points.** n = trunc(|end − start| / L); nothing is drawn when n < 2. step = (end − start)/n, d = unit
    direction, p = the Quake II perpendicular vector of d, q = d × p, J = trunc(1.6·L), and each jitter value is
    (crand mod J) − trunc(J/2), drawn for p, then for q. With S_k = start + k·step:
    - Q_1 = S_1 + jitter (not smoothed);
    - for k = 2 … n − 1: R_k = Q_(k−1) + step + jitter, Q_k = 0.3·S_(k−1) + 0.7·R_k (the straight position used is
      one step behind, so the arc lags behind the straight line by about one step).
    - Segment 1 runs from `start` to Q_1; segment k (k = 2 … n − 1) runs from Q_(k−1) − 0.06·(R_k − Q_(k−1)) to Q_k:
      the start of each later segment is pushed **back, away from** the new point, by 6 % of (new unsmoothed point −
      previous end), so consecutive segments overlap slightly. OpenMerc's direction **is opposite**.
    - n − 1 segments are drawn in all; the last drawn point is Q_(n−1). The point after it is set to `end` but never
      drawn, so **the arc stops short of `end`** (about two steps). Correct "the last point is exactly `end`".
  - **Arc branches** (only arcs drawn with branches, i.e. type 12; type 8 and the spark emitter's arc have none):
    after a segment, with probability 1/5 (crand mod 5 = 0), one more line from **that segment's start** to (that
    segment's end + rand(−L, L) per axis), life 500 ms, no fade, same sprite, blend, colour and half-width.
  - **Recursive bolt.** Both the branch and the continuation start at **m** (after the ±90 jitter, not at the 1.1×
    overshoot end of the drawn line). OpenMerc **matches**.
- Verify: a TE 8 arc of length 100 that comes out with L = 4 has 24 segments and ends about two segment lengths
  (≈ 8 units) before its end point; the segments look white-hot (`lit` sprite, additive), not lit by the world.

#### I5.8 SPEC-401 type 18 — "glass without a direction" (Resolution)

- Old: "glass (37) with a direction: … glass without direction: as flesh". OpenMerc never takes the second branch.
- New: the branch is taken only when the decoded direction is exactly (0, 0, 0). The direction is a `dir` byte
  (SPEC-201): every one of the 250 table entries is a unit vector and an index above 249 is fatal, so **the branch is
  unreachable in 1.07f**; a zero vector sent by a server is encoded as entry 0. OpenMerc **matches**; implementations
  may keep the branch for a zero vector.
- Verify: data check of the SPEC-201 Appendix A table: no entry has length below 0.99.

#### I5.9 SPEC-401 type 19 — rock chunk model (Resolution)

- Old: "10 rock chunks (chunk type 16 model, random part)". OpenMerc: rock small (debris model 7).
- New: chunk type 16's table entry is used **without a size class**: debris model 7 (`chunks_rock`, file `rock_sm`),
  material group `Rock`, skin `grey_dk`, one part chosen by irand(0, 4) among ROCK1..ROCK4 (the inclusive bound gives
  an invisible chunk one time in five, as for type 18). All ten start at the impact point. OpenMerc **matches** for
  the model; it must also apply skin `grey_dk`.
- Verify: a grenade explosion (TE 19) on any floor throws dark-grey small rocks.

#### I5.10 SPEC-401 type 31 — brightness L (Refinement)

- Old: "L = brightness (length of the world light colour) at origin." OpenMerc: L = 1 until the renderer import exists.
- New: L = √(r² + g² + b²) of the colour returned by the renderer's light-sample export (SPEC-002 slot 0xa4) at the
  temp entity's origin with direction argument 255 (straight down). The spark colour channels are
  trunc(irand(−1, 250) × L) stored as a byte (modulo 256), alpha 250, normal blend. Note that a world without light
  data returns (1, 1, 1), so L = √3 ≈ 1.732 there, not 1. L = 1 is an acceptable placeholder only until the renderer
  call is wired; it is **not** the original value in any lighting.
- Verify: TE 31 in a dark corridor gives dim sparks; where L > 1.024 (bright light, or a map without light data)
  channels above 256/L wrap, so some sparks come out dark or off-colour.

#### I5.11 SPEC-400 `.eft` light primitive — decay (Resolution)

- Old: the light primitive gives radius, colour and lifetime but no decay. OpenMerc: 0.
- New: decay is **0**: the light keeps its full radius for its lifetime and then disappears (SPEC-400 *Dynamic
  lights* items 1–3; the colour's fourth byte is unused). OpenMerc **matches**.
- Verify: `weapons/world/rocketexplode.eft` light (radius 500, 0.105 s) does not shrink before it vanishes.

#### I5.12 SPEC-400 §Rendering hand-off 2 — debris visit order (Correction)

- Old: "starting at the next slot to be reused and going from newest to oldest". OpenMerc starts at the most recently
  written slot and walks backwards.
- New: with N = ring size and W = the slot the next debris will be written to, the visit order is **W, W − 1, W − 2,
  …, W + 1** (indices modulo N): first the slot to be written next — which, once the ring has wrapped, holds the
  **oldest** record — then the newest record and backwards in age. The first `fx_maxdebrisonscreen` + 1 alive records
  visited are drawn and every further alive one is destroyed, so when the oldest record is still alive it survives
  together with the `fx_maxdebrisonscreen` newest. OpenMerc **differs** only in that one record. In stock play the
  oldest record is usually already destroyed; the difference shows when more pieces than `cl_max_debris` are spawned
  close together.
- Verify: `cl_max_debris 20` (ring already in use at this size), `fx_maxdebrisonscreen 4`, one TE 18 with count 20,
  which fills every slot: the first chunk spawned and the last four remain; chunks 2–16 are destroyed before they are
  ever drawn.

#### I5.13 SPEC-400 §Decals 4 — the final fitting scale (Resolution)

- Old: "tries scales s = 1.0, 0.8, 0.6, 0.4, 0.2, 0.1 and finally a very small value". OpenMerc: 0.001 × size.
- New: the tried factors are the single-precision values 1.0, 0.8, 0.6, 0.4, 0.2, 0.1 and then **≈ 3.13 × 10⁻⁸**
  (the residue of 1.2 minus five times 0.2 and twice 0.1 with single-precision storage). If the trace misses at that
  factor too, one more trace is made and the direction's factor becomes **≈ −0.1** (−0.0999999717), which makes the
  decal size negative (size × −0.1 is then the minimum). With the impact point on the surface the 3.13 × 10⁻⁸ trace
  practically always hits, so the result is effectively 0 (invisible), as with OpenMerc's 0.001; the negative case
  occurs only when no surface lies within 2 units behind the impact point in that direction. A negative size is
  expected to draw as a mirrored decal of 0.1 × size (UNVERIFIED; verify with a decal placed just off a surface edge).
  Fitting applies only when the size or the end size is above 3 (bullet holes are never fitted).
- Verify: a scorch (TE 29, size 16–64) on a 2-unit-wide ledge shrinks to nothing visible.

#### I5.14 SPEC-401 type 48 — falling-leaf behaviour (Correction)

- Old: "while falling, vertical speed −18 (acceleration −1), horizontal speed × 0.9 per step, pitch swinging between
  ±70° with a small push along its forward direction; stops when slower than 1." OpenMerc: pitch 70·sin(4·age), push
  2 units/s per step.
- New: the sheet starts with gravity −800 and pitch rate −100°/s. After its normal Ghoul-particle motion of each step
  (SPEC-400 Particle simulation 3) the following rules apply, with F = the sheet's forward vector, U = its up vector
  (Quake II angle vectors of its current angles), F' = (1.4·F.x, 1.4·F.y, 1.3·F.z), k = U.z, and a per-sheet flag
  "swing up" (initially clear):
  1. speed² < 1: the sheet's model is released (it disappears; UNVERIFIED in game). Sheets come to rest when they
     land, so they vanish on landing.
  2. vertical speed > 5 (rising after the throw): if pitch ≤ −70° it is set to −70° and the pitch rate to 0; the
     flag is set. Nothing else.
  3. otherwise (falling): acceleration := (0, 0, −1), vertical speed := −18, horizontal speed × 0.9; then
     a. if the flag is set: when pitch + irand(−15, 15) ≥ 70 the flag is cleared; velocity += 6·F', the horizontal
        part counting only if F'.z > 0;
     b. always: when pitch + irand(−15, 15) ≤ −70 the flag is set and the pitch rate := (200 + irand(−50, 50))·k°/s,
        otherwise the pitch rate := (irand(−50, 50) − 200)·k°/s; velocity −= 6·F', the horizontal part counting only
        if F'.z < 0;
     c. if the vertical speed is now ≥ 5 it becomes 4.5 (cannot happen with these values).
  Because rule 3b always sets the pitch rate last, the "reverse at +70°" of 3a never takes effect: a falling sheet
  turns towards pitch −70° and then jitters around it (±15°) instead of swinging between ±70°. The push is 6 units/s
  per step along F' (vertical parts of 3a and 3b cancel while the flag is set). OpenMerc's sine swing and 2-unit push
  **differ**.
- Verify: shoot a paper pile (material 41): sheets fly up, tilt steeply nose-up while drifting down at about
  18 units/s, and vanish when they land.

#### I5.15 SPEC-401 notation — "random rotation" and "white" (Resolution)

- Old: Flash(x) "random rotation"; "white" puffs and burning bits. OpenMerc: rand(0, 2π) and alpha 255.
- New:
  - **Random rotation** = rand(0, 6.28) (single precision 6.28, one draw of the shared generator). It applies to
    Flash(x) and every "random rotation" of SPEC-401, and also where SPEC-401 states no rotation (so "rotation 0" by
    its default) but the original draws one: the flashes of types 0/10/14, 12 (both sizes), 32, 46/55/56 and the
    type 46/55/56 fireballs, the burning bits and expfire2 sparks of types 19, 38 and 44, the flares of types 20 and
    33, the type 33 fireballs, the type 21 ring, and **all trail puffs** (fire, smoke, coloured smoke). Entries with an explicit formula keep
    it: dust puffs rand(0, 2π) exactly, "(c%628)/100", rand(0, π), rand(±0.175), π; the type 47 ring starts at 0.
    The difference between 6.28 and 2π is not visible.
  - **White** = (255, 255, 255, 255) only for the burning bits of types 19, 38 and 44. Every other "white" puff or
    line is **(250, 250, 250, 250)**: the 15 flares of type 20, the type 21 and type 47 rings, the type 33 flares, the
    flashes of types 20/40/44 (already written as 250), the streak lines of types 38 and 44 and the recursive-bolt
    lines. OpenMerc's 255 **differs** for those.
- Verify: frame capture of a flashbang (TE 20): the flares are drawn at alpha 250.

#### I5.16 SPEC-400 §Particle simulation — attached particles with "Apply Physics" (Refinement)

- Old: (silent). OpenMerc: for particles moving in the effect's local frame the collision test is skipped.
- New:
  - **Billboards (sprites, oriented sprites):** yes — an attached particle moves its local offset by dt·v with no
    collision test, no collision effect and no removal. OpenMerc **matches**.
  - **Lines:** the collision test is **not** skipped: the start point is box-traced in world space from its current
    position along dt·v and a hit kills the line (after the collision effect), before the attachment update recomputes
    both ends from the effect's axes. A line's velocity has no lasting effect on an attached line otherwise.
  - **Ghoul particle types (3, 4)** with "Attached To Source" do not work in 1.07f (the attachment update writes into
    the record's model data); treat them as unattached.
  No stock `.eft` combines "Attached To Source" with "Apply Physics" (data check over all 211 files), so stock effects
  are not affected.
- Verify: data check (no stock emitter has emitter flag 0x01 together with particle flag 0x10).

#### I5.17 SPEC-401 §Impact handling — kind 0 re-trace entities (Correction)

- Old: "if that trace hits an entity instead the impact becomes kind 3". OpenMerc: every solid entity of the frame.
- New: the re-trace is the client's movement trace: the world plus **brush-model packet entities** of the current
  frame (entities whose packed solid value marks a brush model), never bounding-box entities (players, characters,
  items); the local player's own entity is skipped. The impact becomes kind 3 when the first thing hit is such a
  brush entity, **and also when the trace hits nothing at all** (then the trace's end point, a zero normal and the
  engine's empty surface record: material 0, no flags; UNVERIFIED that this record is always present). Kind 3
  impacts get no decal (SPEC-400 *Decals* 1) but keep sound and material effect. OpenMerc **differs** for
  bounding-box entities and for misses.
- Verify: a bullet hitting a wall within 5 units of a standing character still leaves a hole; one hitting the world
  within 5 units of a door's face (brush entity) leaves none.

#### I5.18 SPEC-400 §Emitters 1 — delay draws after the end time (Resolution, Refinement)

- Old: "draw d = rand(delay per burst) … the next burst time is now + rand(delay per burst)". OpenMerc draws only
  after an emitted burst.
- New: both draws happen **only after an emitted burst**; when the next burst time has passed but lies after the end
  time (or the effect is detached or waits for a random bolt) nothing is drawn and the next burst time is unchanged.
  OpenMerc **matches**. Refinement of §Emitters 2: an emitter counts as finished only at the first step at which the
  next burst time has been reached, now > end time and none of its particles are alive. While the next burst time
  still lies ahead the emitter counts as active (the effect stays alive and keeps refreshing its attachment), even
  after its end time; so an effect lives up to one burst delay longer than its last burst.
- Verify: an Instant effect with delay per burst 1.0 s and duration 0.1 s keeps its effect slot for about 1 s after
  its single burst (count running effects with a 256-effect stress test).

#### I5.19 SPEC-142 / SPEC-400 — census of stock `.eft` files (observation)

- Acknowledged, no change: the R-team parser reads the same 211 files exactly with the same version census.

### F. I4a renderer (items 1–8) and I6m menu runtime (items 1–8)

#### I4a.1 SPEC-412 §2 and SPEC-413 §6 step 5 — orientation of the 2D depth range (Resolution)
- Old: "orthographic projection over the whole window: x 0…W left to right, y 0…H top to bottom, depth range
  −9999…9999".
- New: the projection is the OpenGL orthographic matrix with left 0, right W, bottom H, top 0, **near +9999,
  far −9999** (W, H = window size), loaded after an identity, with the viewport set to the whole window. With these
  values eye-space z = −9999 maps to window depth 0 (nearest) and z = +9999 to depth 1 (farthest), i.e. **more
  negative z is nearer the viewer**. OpenMerc's observation (near 9999, far −9999) is right.
- It is the only orthographic projection the renderer ever sets, used identically in every case: at the end of Begin
  frame (SPEC-413 §6 step 5) and when RenderFrame leaves the 3D view (all its exits). Slot 0xb8(0) keeps it (menu
  models are then drawn under it with depth test on). Slot 0xb8(1) saves it and loads its perspective; slot 0xb4(1)
  restores the saved one. Slot 0xb4(0) does not change the projection.
- Verify: main menu with the globe model: identical to the original only with this orientation (OpenMerc's own check).

#### I4a.2 SPEC-413 §6 "Begin frame" — where depth writes are turned on (Refinement)
- Old: steps 3 → 4 (clearing) → 5 (2D state). OpenMerc turns depth writes on for the clear.
- New: between step 3 and the clearing, Begin frame applies the same state as slot 0xb8 with argument 0: depth test on,
  **depth writes on**, flat shading, back faces selected for culling, 2D texturing on (no projection change). Then it
  clears (step 4) and sets the 2D state (step 5). Step 5 turns the depth test off but does **not** change depth writes,
  so depth writes stay on after Begin frame until slot 0xb4 turns them off. The 2D state at the end of RenderFrame does
  not change depth writes either. OpenMerc's choice gives the same result; it should also leave depth writes on after
  the clear.
- Verify: draw a menu model after Begin frame without calling slot 0xb4 first: its depth is written (a second model
  behind it is hidden).

#### I4a.3 SPEC-002 slot 0x54 and SPEC-412 §3 — rotation values 4–7 (Correction)
- Old: SPEC-002: "`rotation` 0–3 turns the texture by quarter turns"; SPEC-412: "rotation values other than 0–3 are
  undefined". OpenMerc uses rotation & 3.
- New: the texture-coordinate table has **8 entries**, exactly the "Picture flips" table of SPEC-802: 4 = unchanged
  (as 0), 5 = mirrored left–right, 6 = mirrored top–bottom, 7 = both (as 2). SPEC-002 and SPEC-412 should refer to it.
  Values ≥ 8 or negative read past the table (undefined). OpenMerc's `& 3` is right for 4 and 7 but wrong for 5 (gives
  a quarter turn) and 6 (gives a half turn instead of a top–bottom mirror; the two look the same only for pictures
  that are symmetric left–right, which explains the matching scroll-bar end caps).
- This also answers SPEC-803 open question 4: the savegame thumbnails are drawn with value 6, i.e. mirrored top–bottom
  (their rows are stored bottom-up, SPEC-412 §12), which shows them upright.
- Verify: draw an asymmetric picture (e.g. a letter) with values 2 and 6: 2 turns it upside down and reverses it, 6
  only turns it upside down.

#### I4a.4 SPEC-002 import 0x68 and slot 0xa4 — size of the direction table (Correction)
- Old: "The engine's table of unit direction vectors, 12 bytes each". OpenMerc assumes 162 entries and samples
  straight down for larger indices.
- New: the table is the **250-entry** direction table of SPEC-201 Appendix A (the same data). Slot 0xa4 uses the low
  8 bits of `dir`: 255 → the sample segment runs from the point 2048 units straight down; 0–249 → from the point to
  point + 256 × entry `dir`. There is no range check: 250–254 read unrelated memory after the table (undefined, no
  caller passes them; directions from the network are 0–249). OpenMerc is wrong for 162–249, which must use their
  table entries; "straight down" for 250–254 is acceptable.
- Verify: data check: entry 249 of SPEC-201 Appendix A is (0.684038, −0.234085, −0.690866); a lit alpha quad with
  direction byte 200 is lit from the sample along that entry, not from below.

#### I4a.5 SPEC-002 slot 0x6c — character argument (Refinement)
- New, add to the slot description: only the low 8 bits of the character argument are used (code = value mod 256),
  so signed values −128…−1 select glyphs 128…255 (Cyrillic −64…−1 → 192…255). Code 32 and y ≤ −8 draw nothing
  (SPEC-412 §3). OpenMerc matches.

#### I4a.6 SPEC-412 §12 — black screenshots on the test PC (acknowledged, no change)
- The original reads whatever the current read buffer holds when the command runs; on a driver that discards the back
  buffer at the swap the file is black. Same behaviour in OpenMerc; nothing to change.

#### I4a.7 SPEC-410 terrain sets, SPEC-412 §13 `avi` (acknowledged, no change)
- Not implemented yet: noted, no spec question.

#### I4a.8 SPEC-412 §8 — drawn mark on quads drawn alone (Refinement)
- Old: §9 rule 7 gives drawn chain members flag 0x1000; §8 says nothing. OpenMerc marks chain members only.
- New, add to §8: drawing a quad alone **never changes its flags**. Neither 0x1000 ("has been drawn") nor 0x0001 is set,
  in every case (any blend, lit or not, billboard, beam or no geometry, with or without fog or decal offset). The only
  fields of a lone quad the renderer writes are its image cache (+0x30) and fog cache (+0x3c), on first use (§8
  "Image", "Fog"). 0x1000 is set only by quad-chain drawing (§9 rule 7) and is never cleared by the renderer, so a quad
  keeps a mark it got as a chain member until the engine rewrites its flags. OpenMerc matches; the traces showing
  0x0202/0x0402 agree.
- Verify: the frame-trace check already done (reused standalone quads keep their submitted flags).

#### I6m.1 SPEC-802 `input` — text colour (Correction)
- Old: "The value (or one `*` per character for `hidden`) in `tint`"; the input section also says "Page font".
  OpenMerc interpretation: `atint` while the field is active.
- New: the value, the `*` characters and the `_` cursor are always drawn in the widget's **`tint`**; `atint` is never
  used by `input`. The widget takes only the page font's **name** from the page font, **not its colours**: its `tint`
  and `atint` start as opaque white and change only through the input tag's own `tint`/`atint` options (and `btint`
  starts as transparent black, so the active-field rectangle is invisible unless `btint` is given). No stock input tag
  sets `tint` or `atint` (one sets `btint black`), so every stock input field shows white text whatever the page font
  colours are. That is why `m_game` (font `tint normaltext`) shows the name white. OpenMerc's "atint while active"
  gives white here only because atint is white too; it differs for an inactive field (white in the original) if
  OpenMerc copies the page font colours into inputs.
- Verify: an input tag after `<font … tint normaltext atint hilitetext>` without its own `tint`: the text is white
  whether hovered or not; with `tint red` on the input it is red, also when active.

#### I6m.2 SPEC-803 `serverdetail` / `players` — nothing selected (Resolution)
- Old: "Drawing: the comma-separated titles on line 0 …".
- New: both widgets draw **only** while the integer value of their cvar is a valid server-list index
  (0 ≤ index < number of listed servers); without a cvar the index is 0. Otherwise nothing at all is drawn: no title
  line, no grey line, no rows (the page's scroll bar is a separate widget and is still drawn). Rows are never drawn
  without titles. With a valid index whose server has no status text yet, the titles and the grey line are drawn with
  no rows. OpenMerc's observation-based rule matches.
- Verify: join page with an empty list: blank widgets; after a refresh fills the list and a server is clicked, titles
  and rows appear.

#### I6m.3 SPEC-803 §5 and SPEC-802 test vector 4 — case of listed file names (Resolution)
- Old: "Names from archives may be upper case (SPEC-300, UNVERIFIED for display)." OpenMerc upper-cases every listed
  name.
- New: listed names are not converted by the list widgets. Their case comes from the file listing (SPEC-300):
  - archive entries: upper case (entry names are normalised when the archive is mounted): `RAVDM`, `DM/AIRPORTDM`;
  - loose files: the **directory part** comes from the requested pattern, which is normalised to upper case, while the
    **file-name part keeps the case reported by the host file system**. Loose `maps/dm/MyMap.bsp` is listed by
    `filebox "maps" "dm/*" "bsp"` as `DM/MyMap`, a loose `maplists/MyList.lst` by the map-list list as `MyList`.
  OpenMerc differs for loose files. SPEC-802 test vector 4 should read `RAVDM` for the stock (archive) file.
- Verify: put `maplists/MixedCase.lst` into the user directory and open the map-list list: it shows `MixedCase` next to
  `RAVDM`.

#### I6m.4 SPEC-801 §9 — when the pointer is drawn (Resolution)
- Old: drawn last on the top screen (as specified). Question: hidden while the mouse is not active?
- New: the pointer is drawn every frame on the top screen, independent of window focus and of whether the mouse is
  active (SPEC-312 §8). While the mouse is not active the pointer only stops moving: `menu_mouse_x/y` keep their values
  (the window centre when the menu system was created). It is hidden only by a page with `<cursor 0>` under the
  pointer (stock: acc, acc2, blankscreen, blankscreen_nostats, credits, disclaimer, endgame, endstats, f_login, intro,
  login, missionfailed, m_loading_top), and the last setting stays while the pointer is over no frame. So the
  missing pointer in the captures is not explained by the menu rules. OpenMerc matches the spec.
- Verify: in the unfocused original, `set menu_mouse_x 100; set menu_mouse_y 100` from the console with a main-menu
  page open: the pointer picture should appear centred on (100, 100); if it does not, the capture path loses it.

#### I6m.5 SPEC-800 §5.2 `<config x>` — synchronous or via the command buffer (Resolution)
- Old: "executes `exec x.cfg` immediately".
- New: **synchronous**. The tag (and every screen refresh, SPEC-801) uses immediate execution (SPEC-302 §3 last rule):
  text already waiting in the command buffer is put aside, `exec x.cfg` runs, then everything it queued (the whole
  file) is executed, then the waiting text is put back. So the file has run completely before the next tag of the page
  is parsed, and widgets that come after `<config>` read the values it set. Stock pages put `<config>` before their
  lists (`m_video`: line 6, the resolution list later), so `set gl_mode 4; menu video` shows 800x600 at once.
  OpenMerc matches. Side effect (derived, UNVERIFIED in game): a `wait` inside the file stops the execution and the
  rest of the file is lost (the put-aside text overwrites it); no stock menu configuration contains `wait`.
- Verify: the test in the question: the resolution row shows 800x600 on the first frame.

#### I6m.6 SPEC-803 §1 "Animation" — whose events are processed (Correction)
- Old: "Events (SPEC-011 instance method 45) are processed while drawing, using the menu clock, except while the current
  menu screen is in its 'waiting for an animation' state". OpenMerc processes every model's events every frame.
- New: the rule is the **opposite**: a model's events (notes `sound`, `EOS` with `control` and `end`) are processed,
  while the model is drawn, **only while the model's own screen is not ready**, i.e. a `<stm waitanim>` screen whose
  waiting state has not ended yet. A ready screen (every screen without `waitanim`, and a `waitanim` screen after a
  `control` model's `EOS` or a page key binding ended the waiting) processes no model events at all: no note sounds, no
  `EOS`, no `end` command. The `EOS` of a `control` model ends the waiting state, and from the next model drawn on that
  screen on, event processing stops for all its models. Bolted models are processed through their parent (SPEC-012
  §5). OpenMerc differs.
- Verify: stock `intro` (waitanim, `end "menu amain" control`) opens the main menu when its animation ends; a model
  with a `sound` note placed on an ordinary page plays no sound.

#### I6m.7 SPEC-803 §1 — `time` with `oneshot` (Resolution)
- New: confirmed, `time` wins. The playback is chosen once, at the first draw: if `time` or `animate` was given, the
  static pose at offset `time` (mode 3); otherwise loop (mode 0) unless `oneshot` was given (mode 1). The order of the
  options in the tag does not matter. Consequence: a `control` model with `time` never ends a waiting state (mode 3
  produces no `EOS`, SPEC-012 §5); only a page key does. OpenMerc matches.

#### I6m.8 Observations (acknowledged)
- Popup and `disclaimer` matches: noted.
- `dm_game_assassin.rmf` cascade: the tokeniser of SPEC-800 §2 has no escape for quotes inside quoted text, so the
  original parses the inner quotes of `MENU_DM_GENERIC_DM2` the same way and should print the same
  `Unrecognised text keyword` cascade with `developer 1` (UNVERIFIED in game).

### G. I1a engine core (items 1–10)

#### I1a.1 SPEC-002 import 0x40 / SPEC-001 #87 / SPEC-302 — when the cvar change callback runs (Correction)
- Old: "called at creation and when an existing cvar is registered again with a callback"; SPEC-313 test vector 2
  "`set sp_language 0` (forces a reload)". OpenMerc calls the callback on every assignment.
- New: the callback (cvar record offset 0x10, G18.22) is called with the record in exactly these cases:
  1. cvar-get creates the cvar and a callback is given (after the record is complete);
  2. cvar-get finds an existing cvar and is given a callback: the callback is stored (replacing an earlier one) and
     called **only if the cvar's modified flag is set**;
  3. a set through any path that changes the value **string** — console `set`/`sset`/`name value`, the set, set-value and
     force-set imports (SPEC-001 #88, #89, #90) — after the new value is stored; **an assignment of the same string
     calls nothing and leaves the modified flag alone** (the comparison is on the exact text, so `1` and `1.0`
     differ);
  4. the full-set path (engine-internal re-registration with new flags, used for example for `cpu_mmx`, `gamedir`,
     `sv_violence` at the end of a server spawn, GS.15) always stores, always sets the modified flag and **always**
     calls the callback, even for an unchanged value.
  Not called: a latched set while a server is running (only the latched string is stored and `<name> will be changed
  for next game.` is printed), and a latched set without a server (the value is stored directly; neither the
  callback nor the modified flag is touched); a refused set (write-protected, internal while not allowed, invalid info value).
  So "on every assignment" (OpenMerc) differs: `set x v` with x already "v" does nothing. SPEC-313 test vector 2 is
  wrong as written: with `sp_language` already 0, `set sp_language 0` does not reload. Corrected vector: `set
  sv_show_strip 1`, `set sp_language 3`, `set sp_language 0`: every menu text starts with `STRIP-`.
- Verify: register a test cvar with a logging callback in a probe module: `set t 1` logs once, a second `set t 1`
  logs nothing, `set t 1.0` logs again.

#### I1a.2 SPEC-313 Appendix A — number of key words (Correction)
- Old: "Fourteen 32-bit words are drawn … for value numbers 0 … 5 a pair (off, on); then the password key K; then the
  pair for value number 6."
- New: the list is right and the count is wrong: **fifteen** words are drawn, in this order: the six pairs (off, on)
  for value numbers 0 … 5 (12 words), K (1 word), the pair for value number 6 (2 words). The pair of value number 0
  (`Server`) is drawn but never used (the password uses K). OpenMerc's 15 is correct. The drive search uses the first
  drive letter A…Z that exists, is a fixed drive and returns volume information.
- Verify: on a PC with a stored lock set, OpenMerc's decoder reads all six locks and the password as valid.

#### I1a.3 SPEC-303 `SPNM`/`SPID` — which string packages are saved (Resolution)
- Old: "count S of string packages in use".
- New: the saved list is every loaded package whose owner set contains the **server owner bit (2)** (SPEC-313
  "Package owners"), in ascending package-ID order. Packages owned only by the engine (8: `str_server`, and the
  client-start copies of `items`, `weapons`, `general`, `dm_generic` until the game registers them) or only by the
  menus (4) are not saved. The game module registers through import #47: `items`, `weapons`, `general` at level
  start, `dm_generic`, `singleplr` or the mode package of the game mode, and the level's own package; each such
  registration adds bit 2 to the already loaded copy (SPEC-313). Packages restored from a savegame's list are
  registered with the server owner too. OpenMerc's rule matches.
- Verify: the reference `auto.sof` of `map kos1` (`dm_generic`, `weapons`, `items`, `general`, `singleplr`).

#### I1a.4 SPEC-300 Behaviour 7 — exact `dir` output (Refinement)
- Old: "`dir [pattern]` … a header line for the mount, then the matching entries … ends with `<bytes> bytes in <files>
  files and <folders> folders from <paths> paths`."
- New (every line starts with a SPEC-002 colour byte):
  1. separator: colour 7, 44 `=` characters, line feed;
  2. per mount, in search order, its header line, then its entries:
     - mount header: colour 3, 11 spaces, the directory path; for an archive mount the name is empty (11 spaces
       only);
     - file: colour 7, the size right-aligned in 8 columns (`%8d`), a space, the name (archive entries in their
       normalised upper-case form, SPEC-300 Behaviour 3);
     - folder (directory mounts only): colour 1, **10 spaces**, the folder name (no `<DIR>` marker);
  3. an empty line, then `<bytes> bytes in <files> files and <folders> folders from <paths> paths` (bytes = sum of the
     listed file sizes; files, folders and mount headers counted separately), line feed;
  4. the separator again.
  The default pattern is `*.*`. OpenMerc's `   <DIR> <name>` differs.
- Verify: `dir maps/*` with a loose sub-folder `user/maps/test/`: the folder line is 10 spaces + `TEST` in colour 1
  (UNVERIFIED in game: the case of a loose folder name, see I1a.5).

#### I1a.5 SPEC-300 "dash switches … normalised (Behaviour 3)" — upper-casing (Resolution)
- New: yes, the full Behaviour 3 normalisation is applied to the values of `-basedir`, `-cddir`, `-game` and `-user`:
  `\` → `/`, ASCII letters upper-cased, trailing spaces removed, everything up to and including `/BASE/` removed, then
  everything before `GHOUL/` removed when `/GHOUL/` occurs. So `-user c:\a\b` → `C:/A/B`, and (quirk) `-user
  d:\sof\base\u` → `U`. The cvar default and `+set` values are not normalised (as the spec says).
  On Windows the upper-casing is harmless; on a case-sensitive host it breaks real paths, so OpenMerc's choice
  (upper-case on Windows only) is a reasonable documented difference. The `/BASE/` stripping is original behaviour
  even on Windows.
- Verify: `SoF.exe -user c:\Temp\U1` then `path`: the top line reads `C:/TEMP/U1`.

#### I1a.6 SPEC-302 Behaviour 8 `cpuid` — `cpu_mmx`, `cpu_amd3d`, MHz (Resolution and Correction)
- Old: `cpu_mmx`, `cpu_amd3d` "Detected MMX / 3DNow! support (read-only)"; the `<model>` table and the MHz method are
  not specified.
- New:
  - At every detection (engine start and every `cpuid`) both cvars are first set to `0` (read-only, full set). They
    are set from the feature bits **only when the CPU reports a base family of 4, 5 or 6** (CPUID leaf 1, bits 8–11):
    `cpu_mmx` = 1 when the MMX bit (leaf 1 EDX bit 23) is set; `cpu_amd3d` = 1 when the 3DNow! bit is set, which
    only the AMD branch reads (extended leaf 0x80000001 EDX bit 31, AMD vendor only). For any other family (every
    AMD CPU from family 0Fh on, i.e. all current AMD CPUs) both stay 0 and the `<model>` text is empty, although the
    `MMX instructions supported` line is still printed (it uses the feature bits directly). Current Intel Core CPUs report
    base family 6 and get the MMX bit (a Pentium 4, base family 0Fh, does not).
  - The `<model>` text comes from a fixed table indexed by vendor (Intel, AMD, Cyrix), family 4–6 and the model
    number; out of that range it is empty. Exact table entries are not specified (only 1990s CPUs are named).
  - MHz (only when the time-stamp counter bit is set, else 0): the time-stamp counter is read, the engine busy-waits
    until its millisecond clock has advanced by at least 101 ms, reads the counter again; MHz = trunc((difference −
    91) / 100000). The result is therefore about 1–2 % above the true clock. Only `cpu_speed_level` uses it (≥ 391 →
    4), so any modern CPU gives level 4.
- Verify: on the AMD test PC `cpu_mmx` and `cpu_amd3d` read 0 (observed); on an Intel CPU `cpu_mmx` reads 1.

#### I1a.7 SPEC-302 commands `ping`, `serverstatus`, `reset_predn`, `adddownload`, `winstart`, `subliminal`, `user*` (Refinement)
- New (texts are exact):
  - `ping <server>` and `serverstatus <server>`: behaviour as SPEC-200 "Client-side commands"; wrong argument count →
    `usage: ping <server>` / `usage: serverstatus <server>`; unresolvable address → `Bad server address`; port 28910
    when none is given. The reply `ack <ms>` prints `Ping time is <n>` (SPEC-200).
  - `reset_predn` (client): frees the client's prediction inventory, shuts down and unloads the client side of the
    player module when loaded, loads it again and re-initialises weapon prediction (SPEC-003/SPEC-602). The server
    sends it after every precache as the stufftext `precache <n>; reset_predn`.
  - `adddownload <file>` (server): wrong argument count → `Usage: adddownload <file>`; otherwise the file is opened
    and closed through the Ghoul server system's method 22 (SPEC-010), which records it as used and therefore
    offered for download.
  - `winstart <cmd>`: without argument `Usage: start <parameter>`; otherwise the command is remembered (a later
    `winstart` replaces it) and `Command '<cmd>' will be started on program exit` is printed; it is run at exit as
    SPEC-301 §11 says.
  - `subliminal`: prints `WARNING: Subliminal messages enabled` and does nothing else.
  - `userweapons`, `useritems`, `userammo`, `usermisc`: as SPEC-801 §4 (header `Weapons:`/`Items:`/`Ammo:`/`Misc:` in
    colour 7, then the info string of the cvars with flag 0x100/0x200/0x400/0x800); the info string is printed one
    pair per line, the key padded with spaces to 20 characters, then the value (`MISSING VALUE` when a key has none),
    as Quake II's info printer; pairs appear newest cvar first because the cvar list is newest first.
  The observations of OPEN_QUESTIONS I1a item 7 are all original behaviour.

#### I1a.8 SPEC-004 sound struct #3 — connection states, built-in sounds (Resolution and Correction)
- Old: "+0 connection state (8 = active, in game)"; "Level registration: S_BeginRegistration; the engine's built-in
  sounds; S_RegisterSound for each sound configstring …".
- New:
  - Connection-state values of 1.07f: **0** before client initialisation; **1** disconnected; **2** a connection was
    requested (`connect`, `reconnect`, or a server-requested reconnect) and nothing has been sent yet; **3** a
    `rejected_auth` with the client's nonce arrived (retry pending, SPEC-200); **4** connecting (`getchallenge` /
    `connect` being sent every 3 s, SPEC-200); **7** connected (`client_connect` or serverdata received, also after
    `changing` and when the level is unloaded while active), loading until the first valid frame; **8** active (first
    valid frame parsed). Values 5 and 6 are tested by the connect code but never set. The sound libraries only compare
    with 8. OpenMerc's 3 (connecting) and 4 (connected) should become 4 and 7 if any module outside the engine is to
    see the original values.
  - **No sound is registered** between S_BeginRegistration and the configstring sounds. At that point the client
    only forgets its 114-entry client sound list (SPEC-401 "Client sound list": registered on first use). Replace
    "the engine's built-in sounds" by "the client sound list of SPEC-401 is cleared". The full order is:
    S_BeginRegistration; clear the client sound list; S_RegisterSound for each non-empty sound configstring (window
    messages are pumped after each); S_RegisterAmbientSet (configstring 12); S_RegisterMusicSet (configstring 13);
    S_EndRegistration.
- Verify: a probe sound library logging its calls sees no S_RegisterSound before the first configstring sound.

#### I1a.9 SPEC-313 §1 — quoted value whose closing quote is followed by a space (Correction)
- Old: "A value that starts with `"` runs to the first `"` that is directly followed by CR, LF or the end of the text
  (so inner quotes are kept)". OpenMerc: the value runs on over the next lines (`zmain/strip/c4.sp`).
- New: the value **never extends beyond its own line**. A line is read up to and including its line feed (at most
  8192 bytes; a longer line is split and the rest is read as the next line). Inside the quoted value the scan stops
  at the first `"` followed by LF, CR or the end of the line buffer; if there is none, the value is **the rest of the
  line including the stray `"`, the following characters and the CR LF**. The next lines are parsed normally. For the
  stock `zmain/strip/c4.sp` entry the English text therefore ends with `."`, a space, CR and LF, and the following
  `TEXT_GERMAN` and `TEXT_FRENCH` lines are separate text keywords (normal language rule). "end of the text" in the
  spec means "end of the line". OpenMerc differs.
- Verify: with `sp_language 0` the C4 description in the weapon/item text shows a trailing quote and an extra line
  break; with `sp_language 2` the French text is shown (it would not be, if the English value had swallowed it).

#### I1a.10 Observations (acknowledged; two spec corrections)
- `path`: confirms SPEC-300 Behaviour 7 (archive mounts print empty lines); remove its UNVERIFIED mark and the
  corresponding SPEC-300 open question for `path` (I1a.4 settles `dir`).
- `gamedir`: correct SPEC-300/SPEC-302: the cvar is **created only when the game directory is set** (a `-game` switch
  or a later mod change); after a plain start it does not exist.
- Dedicated server with `+set logfile` and a missing user directory crashing: original behaviour; OpenMerc's directory
  creation is a fix for DEVIATIONS.
- `allow_download`: correct SPEC-302 to default **"0"**: the server initialisation registers it with "0" before the
  client initialisation registers it with "1", and the second registration does not change an existing value.
- `auto.sof` chunk checksums: acknowledged.

### H. G6b script wiring (items 1–7)

#### G6b.1 SPEC-760 B1.4 / SPEC-750 §1.3 — who uses the worldspawn `script` runner (Resolution)

- Old: SPEC-760 B1.4: "`script` set → a `script_runner` entity is created and spawned". OpenMerc uses it from
  ClientBegin with the client as activator, in every mode, on every Begin.
- New:
  - The worldspawn runner is an ordinary `script_runner`. It uses the worldspawn's `script` key and the worldspawn's
    `parm1`…`parm10` as its parameter strings. It has no `targetname`, so nothing in the map can fire it. The game
    module keeps a reference to it; spawning a level clears the reference first.
  - It is used in exactly one place: at the **very end of the game's ClientBegin** (SPEC-001 export; the client's
    `begin`). This comes after the client is put into the server (or, for a client restored from a savegame,
    after its restore steps), after the intermission check and after the client's end-of-frame update. Self and
    caller are the runner, the activator is the **client's entity**. The use queues an execute event as in SPEC-750
    §1.3, so the script runs in the script step of the next game frame.
  - **Single player only.** In every deathmatch mode (`deathmatch` 1–7) ClientBegin takes the deathmatch path,
    which returns before this point, so the runner is never used there. The runner still exists, but its script never
    runs unless a script uses it.
  - **Every single-player Begin**, including the Begin after loading a savegame. Loading spawns the map first
    (which creates a fresh runner and stores the reference) and then reads the saved level. The saved entity and
    script state replace it, so the use reaches the restored runner. It has an effect only if the restored script
    is READY. A script that has finished (`tsr1pre` after its intro) or that is waiting ignores it (§1.3). After an
    automatic (level-start) save there is no level section: the script is new and READY, so the intro runs again.
  - OpenMerc matches for single player. It **differs** for deathmatch: there it must not use the runner.
- Verify: `deathmatch 1; map tsr1`: the intro (`use(startintro)`) does not start. Single player: save during the
  intro's wait, load: the intro does not restart; load the `auto` save: it restarts.

#### G6b.2 SPEC-751 §2.6 — effects bit 0x4000, not a render flag (Correction)

- Old: "otherwise its player entity gets render flag 0x4000 … (The flag is removed 0.5 s after the counter drops to
  0; UNVERIFIED meaning: hide the player's own body.)"
- New: replace the paragraph:
  - At the end of every server frame, unless third-person view is on, for each client: if its camera counter is < 1
    its remote view entity is −1. Otherwise its player entity gets **entity effects bit 0x4000** (SPEC-202 effects
    field; renderfx is not touched), and the remote view entity and type are refreshed from the active camera.
  - With third-person view on, the remote view entity is set to 0.
  - The bit is cleared by the per-frame stats update when the camera counter is < 1 and the level time is at least
    the client's respawn time + 0.5 s. This is the same timer that clears the respawn copy of this bit (SPEC-780: the
    respawn time is set at spawn, and at death to now + 4 s in single player, now + 1 s in deathmatch). After a camera
    sequence in the middle of a level the bit therefore goes in the first frame after the counter reaches 0, not
    0.5 s later.
  - The client reads this bit as "detach attached effects" (SPEC-400 *Updating a running effect* 1).
  OpenMerc's observation **matches**.
- Verify: capture entity 1's effects field during a `tsr1` intro camera (0x4000 set), then the frame after the camera
  ends (cleared).

#### G6b.3 SPEC-751 §2.1 / §2.3 step 5 — spawnflag 8 and the activation aim (Correction)

- Old: §2.1 "8: do not aim at the target (keep the camera's own angles)"; §2.3 step 5 does not mention the flag.
- New: activation **always** aims: when the look-at target is found, the camera's angles are set to look at it,
  whatever the spawnflags. The follow mode of the two security-camera classnames is chosen as before. Spawnflag 8 is
  tested only in the camera think (§2.5) and only suppresses the re-aim there. So a flag-8 camera starts aimed at its
  target and then keeps that direction plus any ROF angle deltas; its own `angle`/`angles` keys are overwritten
  when the target exists. Reword flag 8 as "do not re-aim while active (the camera is still aimed once on
  activation)". OpenMerc's observation **matches**.
- Verify: `tsr1` `introcam1` (spawnflags 10): the first remote angles are the aim at `introcam1n`, then they change
  only by the ROF deltas.

#### G6b.4 SPEC-750 §3.2 — move-done snap does not relink (Correction)

- Old: "mover velocity := 0, origin := the move's destination (exact snap); signal kind 0". OpenMerc relinks.
- New: the move-done event sets velocity 0, writes the destination into the origin and broadcasts signal kind 0. It
  does **not** relink the entity. The ROF frame event relinks after every frame (SPEC-751 §3.4), the move-done event
  does not. The world link (collision box, area and visibility clusters) stays where the last physics move left
  it, until something relinks the entity (a later MOVE/ROTATE frame, a ROF frame, being pushed, `setmodel`…).
  Normally the last physics frame has already brought the mover to the destination, so nothing is visible. The
  difference matters when the mover did not reach the destination by physics (blocked, or a non-pushing movetype):
  then it is drawn at the destination (the origin is sent) but collides and is culled at its old position. OpenMerc's
  relink **differs** only in that case; strict compatibility does not relink.
- Verify: block a scripted mover with the player until its move-done time: afterwards the mover is drawn at its
  destination, and the player can still stand inside the space it visibly occupies until the next move.

#### G6b.5 Script SOUND outside the hearable set (observation)

- Acknowledged; no spec change (PHS delivery, SPEC-201/SPEC-771).

#### G6b.6 SPEC-751 §3.1 and test vector 6 — `Could not open .ROF file` is a developer print (Correction)

- Old: "A file that cannot be read prints (console) a line of 47 `*`, `Could not open .ROF file '<name>'`, the star
  line"; test vector 6: "console shows … 'trainhemi.rof' between star lines".
- New: the game module prints all its script and camera diagnostics through the engine's developer print
  (SPEC-001 import 13). 1.07f shows these only while cvar `developer` is odd (its integer value has bit 0 set):
  `developer 2` shows nothing. They also reach the log file only then. This covers the three lines of the missing-ROF
  message, the three validation messages of §3.2 and the camera messages of §2.1 and §2.3 (`Error in Camera
  Scripting!` …). The missing-ROF message appears **each time** an instruction that loads the file runs
  (PRECACHE_ROF or ROF, since nothing is cached for a missing file), i.e. when `trn1/trn1_introcam.os` reaches it,
  not at level load. Mark the console lines in §2.1, §2.3, §3.1 and §3.2 as developer prints. Test vector 6 needs
  `developer 1`.
- Verify: `developer 1; map trn1`: the star lines and the message appear when the intro camera script runs; with
  `developer 0` or `2` nothing is printed.

#### G6b.7 SPEC-750 §8.1 — script-spawned brush entities (Resolution)

- Old: the clean team asks whether a `func_wall` spawned by built-in 5 with `model` `*31` and `origin` gets its model
  and is linked (observed: it moves, but is never sent to the client near the `tsr1` player start).
- New: **yes.** Built-in 5 stores the keys, then runs the classname's normal spawn function. For `func_wall` that
  is: movetype push; set model (an inline `*n` model sets the model index, the model's bounds and the brush flag,
  and links); solid BSP; link. With spawnflags 0 or 4 the wall is solid, linked and sent like any brush entity in
  the client's PVS. With spawnflags 1, 2 or 3 (trigger-spawn without start-on) it is non-solid and not sent to
  clients until used (Quake II). `tsr1` model 31 is built around its origin (bounds (−8, 0, 0)–(8, 112, 16)), so the
  wall appears at the `origin` given. Two causes in the script data make such a wall invisible near the player start:
  - **value type**: each value is read with the key's type. A string value read as a vector gives (0, 0, 0), and
    read as an int or float gives 0, so a vector key (`origin`, `angles`) given a string value becomes (0, 0, 0).
    An int value read as a vector is UNVERIFIED, expected the same. So `origin` pushed as a string puts the wall at
    the world origin, far from the `tsr1` player start (−576, 976, 40), and outside its PVS;
  - spawnflags 1–3.
  The classname must match exactly (case-sensitive spawn lookup); an unknown classname prints `<name> doesn't have
  a spawn function` (developer print) and frees the entity. OpenMerc showing the wall is the expected behaviour
  for a vector `origin` and spawnflags 0. Check the test script's value kinds.
- Verify: spawn `func_wall` with `model` `*31`, `origin` as a vector constant at the `tsr1` player start and
  spawnflags 0: the wall is sent and drawn. The same with `origin` pushed as a string: it is not seen there.

### I. I6h client view, HUD, input, demos (items 1–9)

#### I6h.1 SPEC-310 §2.1 / §2.4 — colour of the clip big digits (Resolution)

- Old: §2.1 gives no colour; §2.4 says "label and icon HUD green". OpenMerc draws the clip digits HUD green.
- New: big digits are drawn as scaled pics (SPEC-002 slot 0x4c, scale 1.0) **with the colour the caller passes**.
  The weapon panel passes its icon colour, **HUD green (0, 220, 0, 255)**, for both `no_<d>` and `no_blank`. Add to
  §2.1: "drawn with the caller's colour; the weapon panel uses HUD green". OpenMerc **matches**.
- Verify: a screenshot of the weapon panel with 3 rounds: the `no_3` glyph's lit and unlit segments are tinted
  green; compare pixel colours with the frame (white).

#### I6h.2 SPEC-310 §3 and test vector 6 — when `crosshair` is forced to 4 (Correction)

- Old: "`crosshair` … values outside 0–4 are forced to 4"; test vector 6: "`crosshair 7` → value becomes 4".
- New: the cvar's **string is never changed**; only its numeric value is overwritten with 4. This happens in the
  crosshair drawing step of the screen update, when all of these hold:
  - a level's graphics are registered (the same "refresh prepared" state that gates the HUD);
  - the screen update reaches the 3D-view stage (`scr_menuoverlay` 0, screen not disabled, cinematic freeze < 2);
  - the cvar was modified since the last check (also true once after it is created) and its value is non-zero.
  Then a value above 4 or below 0 becomes 4, the modification mark is cleared, and the picture
  `pics/menus/status/ch<trunc(value)>` is looked up (missing picture: no crosshair). A value of 0 leaves the mark
  set until a non-zero value is seen. Consequently `crosshair 7` followed by `crosshair` prints `"crosshair" is
  "7"` before **and** after the first frame in a level, the archived configuration keeps "7", and the crosshair
  drawn is `ch4`. A fractional value is truncated (2.5 → `ch2`). OpenMerc's console output matches; replace test
  vector 6 by: "`crosshair 7` in a level → `ch4` is drawn, the cvar still reads 7".
- Verify: in a level, `crosshair 7`: `ch4` appears in the next frame; `crosshair` still prints 7; `crosshair -1`
  also draws `ch4`.

#### I6h.3 SPEC-310 §12.2 — which angle the weapon kick adds to (Resolution)

- Old: "Weapon kick: interpolated from the player state". OpenMerc adds it to the pitch.
- New: the weapon kick is an angle triple in the player state (pitch, yaw, roll), interpolated per component between
  the previous and the current frame with the same 180°-wrap rule as Quake II's kick angles, and **added component by
  component to the view angles** after the ordinary kick angles. The network carries only its first component
  (SPEC-202 field 22), so in practice it adds to the **pitch**; the other two stay 0. With client weapon prediction
  active the predicted kick (all three components, §12.2 resampling) is added instead. OpenMerc **matches**.
- Verify: fire the machine gun in a demo: the view climbs (pitch) and does not yaw.

#### I6h.4 SPEC-311 — deathmatch byte of the demo header (Resolution)

- Old: "byte = the client's `deathmatch` cvar truncated".
- New: confirmed, with the clarification proposed by the clean team: the byte is the current value of the client's
  `deathmatch` cvar truncated to an integer, which every svc_serverdata overwrites with the server's byte (SPEC-203
  field 4; the client sets the cvar directly, ignoring its latch). So it is "the last svc_serverdata deathmatch
  value" unless the cvar's value was changed afterwards (a plain `deathmatch` command while connected only latches,
  so it does not change the byte). On a listen server client and server share the one cvar.
- Verify: connect to a deathmatch server with a local `deathmatch 0`, `record x`: header byte 1.

#### I6h.5 SPEC-310 §9 — loading progress per configstring (Correction)

- Old: "each configstring with index < 1415 received while loading: 0.33 + 0.33 × index / 1415" (and a frame drawn).
- New: the rule applies to configstrings received **while the level's graphics are not registered** (not only
  during a loading screen), and only to **8 of every 16** of them. The client keeps one counter for the whole
  process (never reset). It counts every configstring received in that state, whatever its index. A configstring
  advances the progress only when, before it was counted, the counter's value mod 16 was 0…7, and its index is below
  1415. In that case `menu_loading` = 0.33 + 0.33 × index / 1415 and a **complete screen update** is run (unless
  `dedicated` ≠ 0): the renderer's begin and end calls, the loading menu, the console, everything of §1 that is
  allowed while not active. No 3D view is drawn. A join that sends about 1250 configstrings therefore draws about
  625 frames, in 8-frame runs. The phase of the 8-of-16 pattern at a join depends on how many configstrings
  arrived before. OpenMerc's "only at the level-registration steps" **differs**; it is visually close, but the
  loading bar moves during the configstring phase in the original.
- Verify: count renderer end-frame calls (or `timegraph` samples) during a join: about half the number of
  configstrings received.

#### I6h.6 SPEC-311 / SPEC-310 §14 — timedemo with multiplayer demos (Resolution)

- Old: SPEC-311 "`timedemo` 1 = play as fast as possible and report the frame rate". The clean team asks whether this
  works with demos whose configstring 31 is not `1`.
- New: **it does not work in 1.07f.** Configstring 31 is maxclients. The cheat check runs once per client frame
  (after the command buffer, before the move is sent) and compares configstring 31 with exactly `1`. Every
  multiplayer demo sets configstring 31 in its first header block. So `timedemo` is set back to "0" in the client
  frame that parses that block, before the first level frame is rendered. From then on the demo plays at normal
  speed (the server and the client frame limiter see `timedemo` 0). No frames are counted, and the
  `%i frames, %3.1f seconds: %3.1f fps` line is not printed at the end, because it requires `timedemo` ≠ 0 at
  disconnect. Timedemo works only with demos whose configstring 31 is `1` or empty (single-player recordings). Add
  this to SPEC-311 as an original limitation. A reimplementation that wants usable benchmarks may exempt demo
  playback from the check, as a recorded deviation.
- Verify: `timedemo 1`, `demomap <a deathmatch demo>`: `timedemo` reads 0 once the level appears, playback is
  real-time and nothing is printed at the end. Same with a single-player demo: fast playback and the fps line.

#### I6h.7 Command buffer delays stufftexts (observation)

- Acknowledged; no spec change. This is Quake II behaviour: server and demo stufftexts are appended to the same
  command buffer, behind pending `wait`s.

#### I6h.8 SPEC-312 §5.1 / §5.3 (observation)

- Acknowledged; no correction. The spec text is from the original's key handling; the console-key path stays
  UNVERIFIED by test only.

#### I6h.9 SPEC-312 §8 — zoom factor with widened field of view (observation)

- Acknowledged as a deliberate extension. The original uses the horizontal field of view of the last rendered view,
  so using the widened value is the natural continuation.

### J. G9 game modes and Ghoul glue (items 1–11)

#### G9.1 SPEC-790 §2 and Behaviour 10 — configstrings 33 and 10 in Conquer (Correction)

- Old: "10 | Conquer | … the decimal value of `dmflags` AND 0x8000 … i.e. `0` or `3276` (sic, truncated)"; "33 | … in
  Conquer refreshed every frame as `-1` / `0` (sic)".
- New:
  - Both values are booleans written as decimal text: **configstring 33 = `1` when the game is a team game (Conquer:
    team play 0x80) else `0`**, and **configstring 10 = `1` when numbered teams (`dmflags` 0x8000) are on, else `0`**.
    The team-game test returns −1/0 and the Conquer hook negates it; the 0x8000 test is reduced to 0/1 before
    formatting. Neither `-1` nor `3276` can occur.
  - Timing: at level start (before the entities spawn) the mode selection writes configstring 33 = `1`/`0` in every
    mode. Conquer's level setup clears its two cached values (5 bytes each). Each client's end of frame compares
    the two values with the cache and writes a configstring only when it differs, so in practice both are written
    once, by the first client end of frame of the level. They are written again only when team play or 0x8000
    changes during the level. The level exit clears the cache again. Configstring 10 is written only by Conquer.
  OpenMerc's observation **matches**.
- Verify: Conquer with `dmflags` 0x8080: configstrings 33 = `1` and 10 = `1` arrive once after the first frame;
  with 0: `0` and `0`.

#### G9.2 SPEC-790 Behaviour 6 — choosing a target sets it directly (Resolution)

- Old: "Each player has a *target* … and a *pending target*. *Choosing a target* …" (the entering rule says "chooses a
  target"; other rules say "chooses a new pending target").
- New: confirmed. There are two kinds of choice:
  - **On entering the game** (the entering player, and every other player without a target at that moment): the
    chosen player becomes **both target and pending target at once**. Only the icons of that pair are updated. A
    player who chose because of someone else's entry is shown his status layout immediately: scoreboard and
    inventory closed, help time now + 5 s.
  - **After a disconnect or a spectator spawn**, the hunters of that player choose only a new **pending** target. The
    target itself changes at their next end of frame (the transition rule, with icons and status), as do the
    pending targets set by kills.
  OpenMerc's observation **matches**.
- Verify: three players; the third joins: in the same packet as his entry message he receives his own LAYOUT_TARGET,
  and a player who had no target receives LAYOUT_TARGET too; no further status follows at the next end of frame.

#### G9.3 SPEC-790 Behaviour 6 — icon self entry and spectators (Refinement)

- Old: "every viewer V keeps, for every other client X, an icon index: 0 when V is a spectator, 3 …, 2 …, else 1".
- New:
  - Every icon computation uses: 0 when the **viewer** is a spectator, else 3 / 2 / 1 as written. Whether the other
    player X is a spectator is never tested.
  - **Self entry**: when a player enters, his own table is computed for every client slot in use **including his
    own**. So his own entry is 1 (0 if he enters as a spectator), sent with his other changed entries.
  - **Entering as a spectator**: his own table is computed as all 0. That equals the values just reset at his
    Begin, so no entry changes and nothing is sent. The other players' tables are updated for an entering player
    only when he is **not** a spectator. A spectator keeps 0 in every other table, i.e. he appears in nobody's table.
  - A player who becomes a spectator later keeps whatever icons the others had for him, except for the pairs
    recomputed by the resulting target changes (his former hunters show him as 1, his own entries for them become
    0). UNVERIFIED in game.
  OpenMerc's observation **matches**.
- Verify: Alpha enters (svc 33 to Alpha includes his own slot with icon 1); Bravo enters as spectator: no svc 33
  entries for or about Bravo.

#### G9.4 SPEC-790 Behaviour 3/6/7 — 32-frame refresh and `help` (Resolution)

- Old: "`help` … shows the mode's status layout for 30 s, or hides it when shown; while shown it is refreshed every 32
  frames."
- New: confirmed, with these exact rules:
  - The refresh runs at the end of every server frame whose frame number is a multiple of 32 (a global frame count,
    not per client). It resends the status layout while the level time is below the client's help time, and resends
    the scoreboard while the scoreboard is open. **The refresh does not change the help time.** Only `help`
    (now + 30 s, or now to close it) and the mode's own status displays (now + 5 s) set it.
  - `help` also closes the scoreboard and inventory layouts, so the scoreboard refresh stops.
  - In deathmatch modes stats[9] bit 0x1 is set while health < 1, during intermission, while the scoreboard is open
    or while the level time is below the help time. So `help` in a mode without a status layout sends nothing but
    keeps bit 0x1 for 30 s. The client then shows its **last received layout** (SPEC-310 §1). That is empty only if
    no layout was received before; a scoreboard seen earlier would be shown again (derived; UNVERIFIED in game).
  OpenMerc's observation **matches**.
- Verify: Arsenal: `help`; layouts arrive at the 32-frame ticks for 30 s, then stop. DM: open the scoreboard, close
  it, `help`: the old scoreboard is drawn for 30 s without updates.

#### G9.5 SPEC-790 Behaviour 7 — Arsenal status count (observation)

- Acknowledged: the count excludes the hand pseudo-weapon (6 after a fresh kit). Remove the UNVERIFIED note and the
  open question.

#### G9.6 SPEC-790 §4 — force-join developer lines (Correction)

- Old: "Developer messages: `player %d %s attempting to join`, one `player %d %s val %d` per slot, …".
- New: exact lines (developer prints, shown only while `developer` is odd, as all game-module diagnostics):
  - `player %d %s attempting to join`: the number is the joiner's **entity-number field**, not his team. It is 0
    while his entity has not been initialised in this level (the normal case at his first connect, consistent with
    the observed 0 that reads like "team 0"), else slot + 1 (UNVERIFIED in game for a reconnect). The name is his current name. An extra argument (0) is passed and not
    printed.
  - then for every slot 1…`maxclients` **including the joiner**: `player <slot> <name stored for that slot> val
    <team number>`. Free slots show the name kept in their client record: empty if never used, UNVERIFIED after a
    disconnect. The team counts used for balancing skip the joiner;
  - `team balance blue %d red %d player put on team %d` (blue = players with team number 1, red = 2).
  OpenMerc's per-slot lines **match**. For the first line, print the entity-number field rather than the team; they
  agree only while both are 0.
- Verify: with force join, a player reconnects into the same level after a map was running with him: the first line
  shows his slot + 1.

#### G9.7 SPEC-790 Behaviour 12 — order of the team rows (Correction)

- Old: "One DM_CTF/TEAM_SCORE_LAYOUT row per team that has players" (order not given). OpenMerc observed Blue first.
- New: the team rows are emitted in the order in which each team's **first eligible player appears in client-slot
  order** (in use, team number non-zero, not a spectator). The same holds for Control and Conquer. It is not
  team-number order: if a Red player has a lower slot than every Blue player, the Red row comes first. Each row
  sits at x = team × 180 − 180, y = −64, whatever its position in the order. Team rows come before the player rows,
  and spectators continue as specified. Since the rows are placed by team, only the order of the layout commands
  differs (visible only in a capture).
- Verify: Red player in slot 0, Blue in slot 1: the Red TEAM_SCORE_LAYOUT command is sent first.

#### G9.8 SPEC-790 Behaviour 12 — Conquer numbered-team rows (Resolution)

- Old: "as Control with CONQUER_LAYOUT_SCOREBOARD_TEAM team rows". The clean team saw these rows arrive as svc 34
  (no data).
- New:
  - The original passes four values to DM_GENERIC string 72 (`CONQUER_LAYOUT_SCOREBOARD_TEAM`): x = team × 180 −
    180, y = −64, team number − 1, and the team's summed score. The stock text of that string (`pak2`, `COUNT 73`)
    is `client_conq_sb %hd %hd %hu %hd %hu %hu `, i.e. **six** 16-bit conversions. The engine packs arguments by
    the string's conversions, so with the stock package two further values are read from beyond the passed
    arguments (undefined).
  - The oracle and test installations contain `zmain.pak`. This extra archive overrides `pak0`–`pak3` (SPEC-300)
    and its `strip/dm_generic.sp` has `COUNT 67`, i.e. no entries 67–72. For an entry past the package's end the
    engine raises no error (its "not defined" test can never fail) and packs according to whatever lies beyond the
    entry table. In the observed runs that gave no data, hence svc 34; with `zmain` the client has no entry 72
    either and ignores the message.
  - Neither installation therefore draws a usable Conquer team row. A clone with the stock `pak2` text should
    send the four values plus two zeros (12 bytes, svc 36) and accept that the client's `client_conq_sb` command
    reads six values. With `zmain` it should send svc 34. Note also that `zmain` lacks entry 71 (`NO_CHANGE`, the
    force-join message of §4).
- Verify: data check: `pak2/strip/dm_generic.sp` COUNT 73 with entry 72 as above, `zmain.pak/strip/dm_generic.sp`
  COUNT 67. A test install without `zmain.pak` sends svc 36 with 12 data bytes for each team row.

#### G9.9 SPEC-313 §3 / SPEC-790 Behaviour 2 — map list replacement (Resolution)

- Old: SPEC-313: "Whenever the server starts a map with `deathmatch` ≠ 0 and `sv_maplistfile` … not empty, the engine
  reads `maplists/<sv_maplistfile>.lst` … stored in `sv_maplist`." The clean team asks for a note in SPEC-790.
- New: confirmed. The exact condition: on **every server map start** (`map`, `gamemap`, and therefore every
  deathmatch level change, which goes through `gamemap`), after a new game's latched cvars are applied and before
  the game module spawns the level. When the integer value of `deathmatch` is non-zero and `sv_maplistfile` is
  not empty, `sv_maplist` is **overwritten** with the list built from the file. This also happens when the result
  is empty (no valid entries). A missing file is a drop error. The game's rotation (Behaviour 2) then uses this list.
  A server owner's own `sv_maplist` survives only with `sv_maplistfile ""`; alternatively he provides his own `.lst`.
  Add to SPEC-790 Behaviour 2: "`sv_maplist` is rebuilt by the engine from `sv_maplistfile` at every deathmatch map
  start (SPEC-313 §3)."
- Verify: `sv_maplist "dm/jpndm2,dm/nycdm1"`, `deathmatch 1`, `map dm/jpndm2` with default `sv_maplistfile`: after the
  level ends the next map is `dm/raven` (the entry after `dm/jpndm2` in `ravdm.lst`); with `sv_maplistfile ""` it
  is `dm/nycdm1`.

#### G9.10 SPEC-781 Item table / §9 — `item_ctf_flag` without `count` (Correction)

- Old: SPEC-781: default count of #36 is 1; "`count` 0 or absent: default count", so a flag without `count` would be
  Blue.
- New: the default-count and random-count rules (count 0 → default, count < 0 → random) are **skipped for the CTF
  flag (#36) and the control flag (#39)**. A CTF flag without `count` keeps count 0, and the rule "count 1 = Blue,
  anything else = Red" makes it a **Red-looking** flag: classname `ctf_flag_red`, skin red, configstring 9 = `h`.
  Its team number is the count, i.e. **0**, so for the touch rules of §9 it is an enemy flag for both teams (team
  AND 0 = 0) and nobody's own flag (UNVERIFIED gameplay). The default-count column of #36 should read "— (count is
  the team, not defaulted)". Stock data: all 16 `item_ctf_flag` entities in the 8 stock CTF map files (`pak0` dm
  `gerctf1`, `irqctf1`, `jpnctf1`, `nycctf1`, `sibctf1`, `sudctf1`, `sudctf2`, and `pak2` `irqctf1`) set `count` 1
  or 2, one of each per map; their `misc_ctf_base` entities likewise. So no stock map is affected. OpenMerc should
  treat a missing count as 0 (Red slot, team 0), not as Blue.
- Verify: data check over the stock entity lumps (16 flags, counts 1/2); a test map with a count-less flag shows a red
  flag and configstring 9 = `h`.

#### G9.11 SPEC-790 Behaviour 11 / SPEC-761 §8 — the single-player meter value (Refinement)

- Old: "stats[11] = trunc(stealth value × 100) (… the stealth value comes from the world alert system, SPEC-761)".
- New: the value is the **alert level A of SPEC-761 §8** (world spawner, "PADD"). It is one level-wide number in
  [0, 1], reset to 0 when the level's worldspawn is spawned, raised by noise reports and decayed or forced to 1 each
  frame by the world-spawner rules there. Every single-player stats update sets stats[8] bit 0x200 and stats[11] =
  trunc(A × 100), so the meter shows 0 when unnoticed and 100 during the max-alert phase. It is a meter of
  *alertness*, not of stealth. Where the world spawner does not run (no player, `ai_spawnfrequency` or the skill
  gain below 0.01, cinematics), A only keeps its last value or decays as SPEC-761 §8 states. OpenMerc's
  `G_StealthValue()` must return this A.
- Verify: fire a weapon in a single-player level: the meter jumps by max(0.1, G × `ai_spawnfrequency`) × amount × 70
  percent points and then falls back as SPEC-761 §8 describes.

### K. G2 character AI (items 1–9)

#### G2.1 SPEC-750 §12.1 flags 0x100 / 0x200 and SPEC-710 `SDSO` fields 36 / 60 (Correction and Resolution)
- Old: SPEC-750 §12.1: 0x200 "passed to the animation layer with the action (UNVERIFIED meaning)"; 0x100 "the action
  does not end by duration/animation end; it ends when the actor's movement state reaches 'landed' (internal states
  13/14; UNVERIFIED meaning)". SPEC-710 `SDSO`: "UNVERIFIED: which AI command operand … fills fields 36 and 60".
  OpenMerc interpretation: 0x100 = lethal flag, the 0x200 operand = target-bolt selector.
- New: OpenMerc's reading is right.
  - Operand mapping into the `SDSO` record: flag 0x100 → byte 36 "lethal" (1 when the flag is set); the 0x200 int →
    field 60 "target-bolt selector"; the 0x80 entity → field 56 target; 0x02 facing → 8; 0x04 destination → 20;
    0x20 → 32; 0x40 duration → 4; 0x400 → 64.
  - Lethal (0x100) on an attack: the attack action is created with the target, the lethal flag and the selector
    (SPEC-710: only when lethal or selector is non-zero), and a target with health > 1 is set to health 1 at that
    moment (SPEC-711 class 13). The attack completes by the SPEC-711 class-13 rule, i.e. with the lethal flag only
    once the target is dead and the animation allows it.
  - Completion of a script action whose lethal byte is set (replaces the "landed" sentence): duration and animation
    end are **ignored**. The action ends when an action of class 13 (attack) or 14 (dog leap) reports completion to
    the scripted behaviour, or, for kind 1, on arrival (11 units / passed). The "internal states 13/14" are these
    action classes. For other kinds with 0x100 nothing else ends the script action (derived).
  - So `ai(introswatkiller, 42, …, flags 0x380)` on tsr1 sets the victim to health 1, the killer fires, the victim
    dies, the attack completes and the script continues.
- Verify: tsr1 intro continues past the shooting; in a save taken during it the `SDSO` record holds 1 at byte 36 and
  the 0x200 operand at 60.

#### G2.2 SPEC-711 class 5 "1.5 × speed" (Correction)
- Old: "plays `std_shead_2frnt_n_n` at 1.5 × speed directly on the model". OpenMerc uses speed index 8.
- New: the speed is not changed (the instance keeps its speed index, normally 4). The value 1.5 is the **start
  offset**: the sequence is played with instance method 17 as (time = level time, offset 1.5 s, restart, mode 1 =
  play once and hold, no phase match, not reversed). The animation therefore starts 1.5 s into its first cycle and
  holds its last frame. OpenMerc differs.
- Verify: trigger the special death (SPEC-711 "interrupt with animation") on a character: the fall starts
  mid-animation and lasts 1.5 s less than the full `std_shead_2frnt_n_n`.

#### G2.3 SPEC-700 rule 15 — where the stored floor height (record 264) comes from (Resolution)
- Old: "the stored floor height" (no writer named). OpenMerc stores the `quake_ground` bolt height relative to the
  origin (SPEC-722 ground-offset event) and smooths the box bottom towards it.
- New: the value is the z of the `quake_ground` bolt in the character's entity space (bolt matrix mode 5 through
  parents: the height of the bolt relative to the entity origin). It is written:
  1. on every Ghoul update by the `quake_ground` bolt event, but only while the character has a ground entity or is a
     corpse (solid value 5), as in the SPEC-722 table;
  2. **also** whenever the body starts a different animation sequence (or force-restarts one): the bolt is evaluated
     at once at the current level time and stored, without the ground condition;
  3. by the preset-change rule of SPEC-700 §15 (pulled to within 15 units of the box bottom).
  It starts at 0 and is saved and restored as record 264. The same value is also the reference height of the
  footstep rule (G2.5). OpenMerc matches 1 and 3; add 2.
- Verify: in a level save, record 264 of a standing soldier equals the height of his `quake_ground` bolt above his
  origin in the current animation (negative for a standing human).

#### G2.4 SPEC-711 A7.3 — jitter aim step (Resolution)
- Old: "the aim keeps the previous aim angles moved towards the desired direction with random errors". OpenMerc uses
  the desired direction plus the error terms each think.
- New: each think, with current aim angles (pitch₀, yaw₀), the action's desired facing angles (pitch_d, yaw_d) and
  the error amplitude E of A7.3:
  1. Turn factor k (with d = distance from the owner to the target's last known position, L = 512·T-c): player
     target: d < 92 → 2; 92 ≤ d ≤ L → 2 − (d − 92)/(L − 92); d > L → 1. Other targets: d < 92 → 10; 92 ≤ d ≤ L →
     10 − 10·(d − 92)/(L − 92); d > L → 1. (E's distance term e is as in A7.3; the ×0.4 applies to e for non-player
     targets.)
  2. Yaw goal G = yaw_d if it lies within ±Y of the owner's yaw (Y as in A7.3), otherwise owner yaw + Y or − Y
     (the nearer side), plus U(−1, 1)·0.75·E.
  3. Δ = G − yaw₀ wrapped into (−180°, 180°]. f = k/8 when |Δ| < 30°, k/4 when |Δ| < 60°, else k. If f < 1 then
     Δ := f·Δ (otherwise the whole difference is taken in this think). New yaw = yaw₀ + Δ + U(−1, 1)·0.1·E.
  4. Pitch goal P = pitch_d + U(−1, 1)·0.15·E (no clamp). Δp = P − pitch₀ wrapped; f = k/8 when |Δp| < 15°, k/4 when
     |Δp| < 30°, else k; if f < 1 then Δp := f·Δp. New pitch = pitch₀ + Δp + U(−1, 1)·0.05·E. Roll 0.
  5. Angles are normalised to [0°, 360°) through the usual 16-bit angle quantisation.
  For actions owned by path, point or scripted decisions the current aim is first replaced by the desired facing and
  E = 0, so the aim equals the (clamped) desired facing. Without a body (or without a current record) Y = 1°.
  This replaces A7.3's "yaw errors … scaled by 1/8 within 30° and by 1/4 within 60°; pitch errors U·0.375·E": the
  1/8 and 1/4 scale the **turn**, not the error. OpenMerc differs.
- Verify: a soldier shooting at another character 600 units away visibly sweeps onto it over several thinks (k = 1
  far away → 1/8 of the remaining angle per think inside 30°).

#### G2.5 SPEC-722 / SPEC-700 footsteps — the "movement value" (Correction)
- Old: "for a living character whose AI reports a movement value above 9, a heel lower than 7 units above the AI's
  reference height produces a footstep … the heel height is always reported to the AI". OpenMerc reports the length
  of the AI velocity estimate.
- New: the "movement value" is **the same foot's previously stored heel height**. The AI keeps one value per foot
  (right and left, both 0 at creation); the reference height is the stored floor height (G2.3). For each heel bolt
  event, h = heel z (entity space) − floor height and v = that foot's stored value:
  - v ≤ 9: store h; no footstep;
  - v > 9 and h < 7: footstep effect (kind 14 for armour type 3, else 4), store h, limp-voice chance for that leg;
  - v > 9 and h ≥ 7: nothing (the stored value stays above 9).
  So a step is made each time a heel that has been raised more than 9 units comes down below 7 (hysteresis).
  Dead characters and entities without an AI do nothing. OpenMerc differs.
- Verify: a walking soldier makes one footstep per heel strike; a standing soldier swaying in idle makes none.

#### G2.6 SPEC-710 C7 — team-0 civilians without a target (Resolution)
- Old: C7 range test and flee destination; test vector 6 (civilian cowers when shot near). OpenMerc lets kind 6 run its
  own range test without a target when fear > 0.
- New: this is the original path, slightly broader. The retreat decision's "target position" query is its flee
  destination and always succeeds, so whenever kind 6 wins (bonus 4 for an unarmed body with fear > 0, from
  "shot at me" notices, which have no team condition) its plan runs **with or without a target**: flee destination
  (team of the sight client: the node flee spot, else the navigation flee query), then the C7 range test (sight client
  farther than 1000 → vanish; fear > 0.7 → cower; unarmed with fear > 0 → cower; else out of range with code 2/4),
  none of which needs a target. A remembered target is dropped from the decision when it is freed, dead or older than
  90 s. OpenMerc matches.
- Verify: test vector 6 with a civilian on the player's team.

#### G2.7 SPEC-700 rule 10 death step 4 — skill score factor (Resolution)
- Old: "score += profile[6] × the skill score factor (skill spec)". OpenMerc uses the difficulty score of SPEC-760 B9.2.
- New: confirmed: the factor is the SPEC-760 B9.2 difficulty score. The score is an integer:
  score := trunc(score + profile[6] × difficulty score) (truncation toward zero, per kill).
- Verify: SPEC-700 test vector 7 with a known difficulty score, e.g. 2.0 (saves 0) and profile[6] = 10: +20 per
  enemy.

#### G2.8 SPEC-700 rule 16 — how the helper is chained (Resolution)
- Old: "owner = the character, entity flag 1048576, chained to the character". OpenMerc sets only `owner`.
- New: the chain is the helper list of SPEC-780 §15 (the same helper kind as the player's crouch volume). Every
  entity has one helper link. The character's helpers form a circular list: the character's link → helper 1 → … →
  helper n, whose link points back to the character; a new helper is appended at the end. The helper also gets
  `owner` = the character, entity flag 1048576 and server flags 0x82. Where the game resolves a helper to the entity it
  stands for (SPEC-780 §15), it follows the links from the helper to the first entity without server flag 0x80. At the start
  of each think the character's first helper with entity flag 1048576 is freed and the character's link is cleared
  before the new helper is made (rule 14). This is not Quake II `teamchain` or `chain`. OpenMerc should add the link
  (needed for the resolution rule).
- Verify: `entlist` with a living soldier in view shows one `Nug Buddy` whose owner is the soldier; after his death
  none is left.

#### G2.9 Reaction latency while an idle plays (Correction of SPEC-710 D3)
- Old: D3 row "0 / other, otherwise → play in place (class 1), default duration the animation's length". OpenMerc: a
  use/alert during a 3.6 s `std_istand_n_mrs_2` idle is acted on only when it ends.
- New: there is **no** interrupt on use or alert (neither touches the current action), but the idle action is not
  3.6 s long: for animation class 0 records without flag 0x00400000 (and not "0x1000 with a target") the action is
  created with **duration 0 and not ending with the animation**, whatever duration the requester passed. It completes
  at its first execution (the think in which it starts), and the next think plans again. Re-requesting the same
  record does not restart the animation, so the idle keeps playing smoothly while the character re-decides every
  0.1 s. With flag 0x00400000 the rule stays 4.0 s, ending with the animation. OpenMerc differs.
- Verify: nyc1 `extra3`: the point_combat walk starts within about 0.2 s of the script's use (UNVERIFIED in game).

### L. G5 navigation and bots (items 1–12; item 12 is filed in the I4b section)

#### G5.1 SPEC-001 "Messages addressed to a single entity", SPEC-741 §3, SPEC-760 §3 — the "no network connection" flag is server flag 0x200 (Resolution and Correction)

- Old: SPEC-001: "silently not sent when the entity is flagged as having no network connection (bots). UNVERIFIED
  which entity flag". SPEC-741 §3: "The edict gets the bot flag and entity flag 0x200". SPEC-201 open question: "Which
  entities the server flag that suppresses unicasts belongs to (probably bots)". The clean team bisected it to
  server flag 0x200.
- New:
  - The flag is bit **0x200 of the server-flags field** (entity record offset 0x118, SPEC-760 §1), not of the entity
    flags. Meaning: "client entity without a network connection". In 1.07f only bots carry it. The game sets it when
    it creates a bot client. Bot removal clears the whole server-flags field. The game also keeps a separate,
    game-private "is a bot" marker, which the engine never sees: that is SPEC-741's "bot flag".
  - SPEC-741 §3 should read: "The entity is marked as a bot (game-private) and gets **server flag 0x200**". Add a row to
    SPEC-760 §3: server flags | 0x200 | client without a network connection (bot); the engine drops sends addressed to
    it.
  - Engine rule for the flag, by import (SPEC-001 numbering). The pending message is discarded in each case:
    | Import | With an entity that carries server flag 0x200 |
    |---|---|
    | #30 unicast (also the single-entity form of #48 and #49) | nothing is sent |
    | #16 welcome print, #17 center print, #20 caption to one client | nothing is sent |
    | #22, #23 with `local` = 1 | nothing is sent |
    | #48, #49 with NULL entity | every client in the game except those whose entity carries the flag |
  - The flag is **not** tested by #14 (print to one client; only the entity-number range is checked), #18 with an
    entity, #43, #44 and #46. These write straight into that client slot's reliable stream. For a slot that never
    had a network client the stream has no capacity, and the server stops with `SZ_GetSpace: overflow without
    allowoverflow set`. This is the error the clean team saw without the flag. A game module must never call these
    for a bot (UNVERIFIED per import; derived from the shared write path). #15 (chat print) reaches only clients that
    are fully in the game on the engine side, so it never reaches a bot, with or without the flag.
  - The same flag splits the player counts of the `info` reply (SPEC-200, open question "middle number"). Clients
    that are at least connected and whose entity carries server flag 0x200 make up the **middle** number; all other
    connected clients make up the first number. Bots never hold an engine connection, so in 1.07f the middle number
    is always 0. It is not a spectator count.
  OpenMerc's bisection result **matches**.
- Verify: as the clean team's bisection. In addition, `info 33` to a server with bots and spectators always shows
  `…+<humans>/0/<maxclients>+…`.

#### G5.2 SPEC-741 §2 — order of the add and fill steps, and what R counts (Correction)

- Old: §2 lists the add (3) before the fill (4). Step 4 says "else if the add condition holds and H + R < maxclients −
  1", and R is "the number of slots with a pending request". Observed: fill bots exactly 1.5 s apart. OpenMerc
  serves a fill request in the frame that created it.
- New:
  - **R** counts every slot whose request time is set: pending requests **plus** bots that joined through a slot (a
    slot keeps its request time while its bot is in game). H + R is therefore humans + bots + pending requests. It
    is computed once per frame before the slot pass, so several requests made in the same frame are all checked
    against the same value.
  - The order within a frame is as listed: shift-up, slot pass, removals, **add**, then **fill** (or drop).
  - The fill step does **not** apply the 1.5 s spacing. Its condition is only the join gap (no bot in game, or
    firstBot − lastHuman > 2) and H + R < maxclients − 1. The gap is measured before this frame's add. So the fill
    request for the next bot is made in the same frame as the previous add, and it waits until the add step serves
    it when 1.5 s have passed (the test is last add + 1.5 ≤ now). The 1.5 s spacing therefore comes from the add
    step alone.
  - Timing within the server frame: the bot manager and the bot thinks run at the **start** of the game frame. This
    is before the level time advances and before entities think. The waypoint system becomes ready in the think at
    level time 10.0, so the manager sees "ready" one frame later (10.1, first fill request), and the first fill bot
    joins at **10.2**. Further bots join at 11.7, 13.2, and so on. This matches the oracle (10.2, 11.7, 13.2, …).
    OpenMerc's first bot at 10.0 is two frames early. Between two joins the next fill slot's `bot_name<i>` already
    holds the chosen personality name.
  OpenMerc differs in timing only: first bot 0.2 s early, request made 1.5 s later than in the original.
- Verify: `bot_fillserv 1`, maxclients 8, one human, dm/jpndm2: player configstrings of the bots appear at 10.2 +
  1.5k s. Right after the first bot joins, an rcon query of the next free `bot_name<i>` shows a personality name.

#### G5.3 SPEC-741 §5/§11 — what a bot's fire-time fields hold (Resolution)

- Old: "fire and alternate-fire values" (not specified). OpenMerc: the level time while the button is pressed, else 0.
- New:
  - The bot writes its fire request directly into the client's two **stored pending fire times** (SPEC-780 §5 step
    6). It copies the same values into the user command's fire-time fields (offsets 16 and 20, SPEC-202 §6.1). When
    its fire control fires, the chosen mode gets the constant **1.0** and the other mode 0. In all other frames both
    are 0. This includes no enemy, the weapon busy (switching or emptied), the 2 s hold after a weapon change, and
    out of ammunition. The two values are never both non-zero. With the per-weapon delays of §7 the request lasts one
    frame, except for weapons and modes whose delay is 0, which request every frame.
  - The attack button bit (0x01) is pressed only with the flame gun while its primary request is set (§11) and by a
    dead bot (respawn press). Other weapons fire through the fire-time values alone.
  - A bot's userinfo has no `predicting` key. The game passes "owner predicts" ≠ 0 whenever the value is not exactly
    `0`, so bots are predicting owners (SPEC-602 §5 server rules). Their attacks start from the pending fire times,
    and the fire sequences start **at level time 1.0 s**, i.e. long in the past (derived). Per SPEC-602 and SPEC-012
    §5 this delivers the sequence's first notes (the shot) at once and lets the sequence end. Refire is then set by
    the bot's own delay table (§7), not by the animation length (UNVERIFIED in game).
  - The attack re-arm rule of SPEC-780 does not clear a bot's pending primary time: a deathmatch respawn clears the
    re-arm only for humans, and spawning re-arms the attack.
  OpenMerc **differs**: it must write 1.0, not the level time. With the level time, sequences start "now" and play at
    animation speed.
- Verify: capture a bot holding the sniper rifle (delay 2.0 s) and one with pistol1 (0.3 s) while they fight: shot
  sounds every 2.0 s and every 0.3 s, and the user commands (local listen server, `cl_shownet`) show fire time
  1.0. A human with `cl_predict 0` firing the same weapons is slower where the fire animation is longer than the
  delay.

#### G5.4 SPEC-741 §5 and §10 — how bots select and use items (Correction)

- Old: §5: "health 1…49 → the medkit (item type 5) is selected". §10: "with probability 1/2 … it selects the grenade
  (else the flash pack); the throw happens through normal fire control". Run tactic: "selects C4 (type 2) or else a
  grenade and fires it". OpenMerc: `itemnext` until the type is selected, then `itemuse` at the next fire.
- New: there is no console command and no fire involved. The bot sets the player module's **pending item request**
  directly (inventory state block offset 0x198, SPEC-601 §3/§4). "Request item type T" works as follows:
  1. while the bot's item lock is active (2 s after a run-tactic use): nothing ("locked");
  2. T not carried: nothing ("missing");
  3. the selected item's type ≠ T: request 1 (`itemnext`) ("switching");
  4. the selected item's type = T: request 4 (`itemuse`, the selected slot) ("used").
  The player module carries out the request in its next think (SPEC-601, including the 0.4 s use rule). A later
  request in the same frame replaces an earlier one. Uses of this rule:
  - **Medkit**: every think while health is 1…49 the bot requests type 5. The medkit is selected and then **used**
    (healing per SPEC-601), not merely selected.
  - **Grenades in a fight** (§10): when the next-grenade time has passed and the bot carries a grenade (type 6) or a
    flash pack (type 1): with probability 1/2, or always while a previous request is still switching, it requests
    type 6 if it carries a grenade, else type 1. Result "switching" → it keeps trying next frame. Result "used" → next
    try after U(15, 30) s. "Locked" or "missing" → it tries again next frame. In the other half of the cases it
    throws nothing and the next try is after U(15, 30) s. So the 1/2 decides **whether** to throw, not which type.
  - **Run tactic**: it requests C4 (type 2), or type 6 when it carries no C4. Once the result is "used" (or the lock
    is active, or it carries neither) the tactic ends and the item lock is set for 2 s.
  OpenMerc's `itemnext` sequence **matches**. The use **differs**: the original issues `itemuse` as soon as the
  selection matches, independent of fire control, and the medkit is used.
- Verify: a bot at health 40 carrying a medkit: the medkit count drops and health rises within about 0.5 s. A bot
  with grenades in a fight: grenade throws (third-person throw animation, SPEC-601 server import 6) without any fire
  press in its user commands.

#### G5.5 SPEC-741 §6 — noise record kind when a footstep follows a weapon noise (Resolution)

- Old: which kind is kept within the 1 s window. OpenMerc: the latest kind and the maximum loudness.
- New: confirmed. Each noise sets the kind to **its own kind** (the latest wins) and the loudness to the maximum
  within the window. The window restarts at every noise (expiry = now + 1). When the previous record has expired
  (expiry ≤ now), loudness starts from 0. Consequence: a footstep within 1 s after a shot turns the record into kind 2
  with the shot's (larger) loudness, so for that second the shooter is audible only to bots with intelligence ≥ 2.
  OpenMerc **matches**.
- Verify: a bot with intelligence 1 behind a wall: a player who fires while standing still is heard; one who fires and
  then walks is not heard after his first step (until 1 s after the last footstep).

#### G5.6 SPEC-741 §13.5 — "when the waypoint is a jump waypoint" (Correction)

- Old: "… when the waypoint is a jump waypoint, when it is kind 2 and less than 26 s remain …". OpenMerc takes the
  first clause as "the previous waypoint is kind 2".
- New: when the bot reaches a waypoint it records whether that waypoint was kind 2 (and, as §13.4 says, jumps for 1 s).
  The first clause is "**the waypoint just reached (the one the bot is now leaving) was kind 2**". The second clause
  is about the current target waypoint being kind 2. The full rule, with remaining = deadline − now: when 10 <
  remaining < 29 (both strict) and (an obstacle needs a jump, or the waypoint just left was kind 2, or the target is
  kind 2 and remaining < 26, or remaining < 15), the jump request is renewed for 1 s. In practice a bot that leaves a
  jump waypoint keeps jumping (pulsed, §11) until it reaches the next waypoint, at most 20 s. Otherwise, when no jump
  is active and 10 < remaining < 29, it side-steps away from walls.
  OpenMerc's interpretation **matches**.
- Verify: a stock map with a kind-2 waypoint (jump): a bot leaving it hops repeatedly until the next waypoint.

#### G5.7 SPEC-741 §14.4/§14.8 — single-waypoint paths, juncture order, start or destination without a path (Correction)

- Old: "a path is a maximal run of consecutive ids k, k+1, … where each waypoint links to the next". OpenMerc: paths
  have ≥ 2 waypoints, juncture sides ordered by id, no route → roaming.
- New:
  - **Paths**: every waypoint that is not a spawn waypoint belongs to exactly one path. A path starts at the first
    waypoint not yet in a path and extends while the current waypoint links to id + 1 and id + 1 is not a spawn
    waypoint. Only that one direction of the link is tested. A waypoint that does not link to its successor, and whose
    predecessor does not link to it, is a **one-waypoint path**, with junctures like any other (both ends are the same
    waypoint, so the second end adds nothing new). Waypoints left over after the 255th path are in no path. Stock
    data has many one-waypoint paths: `jpndm2` has 10 of its 15 paths (waypoints 127–136), and `gercon1`, `sibctb2`,
    `suddm1` and `plantdm` have 14, 24, 16 and 16. OpenMerc **differs**.
  - **Juncture order within a path**: sorted by the juncture's waypoint id on that path. Junctures at the same waypoint
    keep their creation order. Junctures are created path by path (ascending path number), for each path its first
    waypoint's end before its last waypoint's end, and each new juncture is added to its own path and then to the
    other path. Adding a real juncture removes a dead end at the same waypoint of that path. A dead end is not added
    where the path already has a juncture at that waypoint. A juncture with the same other path at the same waypoint
    counts as a duplicate and is not added. "Next/previous juncture along the path" means the next/previous entry in
    this order. "The juncture nearest to id x" means the first entry in this order with the smallest |waypoint id −
    x|.
  - **Start**: the bot's last waypoint. Every think it moves to the link target of the previous last waypoint that is
    nearest to the bot (or becomes none when that waypoint has no links). Link targets are never spawn waypoints, so
    the start is normally in a path. With no last waypoint, the nearest non-spawn waypoint (straight distance) is
    used; with none at all, there is no route. A start in no path gives no route. **1.07f quirk**: path numbers are
    handled as signed bytes in the route search, so a start in path 128 or higher gives no route, and the search never
    crosses into such a path. No stock file has more than 38 paths.
  - **Destination in no path** (a spawn waypoint): the search can only succeed through its 100-step cap (a truncated
    route), otherwise it fails.
  - **No route**: the original does **not** fall back to roaming. When the bot reaches its current waypoint with a
    destination set and no route, it keeps that waypoint as its current one and tries the route again at every
    think. It therefore hovers at the waypoint until the destination changes or is cleared (enemy, death, mode
    objectives, §15). OpenMerc **differs** (UNVERIFIED in game).
- Verify: data check: count the paths of the stock `.wrs` files with the rule above (jpndm2: 15 paths, 10 of them
  single). In game: `bot_wp_botroute` on jpndm2 prints routes whose steps include paths of single waypoints 127–136.

#### G5.8 SPEC-741 §14.9 — "return along the remembered way" (Correction)

- Old: "with no such link return along the remembered way or reverse direction". OpenMerc: back to the waypoint it came
  from.
- New: roaming (no destination) keeps one **remembered pair** (waypoint id s, walk direction ds), initially (0,
  increasing). On reaching the current waypoint c with walk direction d:
  1. next = c ± 1 (by d) when it is a valid id and c links to it;
  2. else, when c has a tag t: next = t. Direction: for t ≥ 2, decreasing if t links to t − 1, else increasing; for t
     = 1, increasing if it links to 2, else decreasing;
  3. else (path end, no tag): b = the cheapest link of c outside its consecutive chain (§14.9).
     - b exists: remember (c, d). Then next = b with direction decreasing if b links to b − 1, else next = b with
       direction increasing if b links to b + 1. Otherwise the bot stays at c and the direction becomes decreasing.
     - no b: k = s + 1 if ds is increasing, s − 1 if decreasing. If k is a valid id and **c links to k**: next = k, the
       direction becomes ds, and the remembered pair becomes (c, d). Otherwise the bot stays at c and reverses d.
  "Stays" means the reached waypoint remains current, and the rule runs again at the next think with the new
  direction. So "the remembered way" is the waypoint after the remembered branch point in the remembered direction,
  and only when the current waypoint links to it. It is not the waypoint the bot came from. OpenMerc **differs**.
- Verify: in game only (movement fidelity). On a map with a dead-end branch, a roaming bot (no items, no enemies,
  `bot_wppref 0`, `bot_hunt 0`) walking into it turns at its end. Compare its next waypoints with the rule.

#### G5.9 SPEC-740 §6 step 7 — the tiny direction (Resolution)

- Old: "If |m|² < 0.1 the (tiny) direction is accepted with the current time" — normalised or not?
- New: stored **unnormalised** (the raw move vector, often zero), with the time set to now. Consequence: at the next
  query the reversal test (u · last ≥ −0.717) passes for any direction, because the last direction is (nearly) zero.
  OpenMerc **matches**.
- Verify: no separate test; it follows from SPEC-740 test vectors with a character standing still.

#### G5.10 SPEC-740 §11 — cone helper with equal radii (Resolution)

- Old: the original divides by zero for equal radii; OpenMerc uses a cylinder.
- New: equal radii **cannot occur** in 1.07f. The only user passes sphere A with radius max(40, the node's clearance)
  ≥ 40 and sphere B with radius 30. For completeness: with equal radii the original's arithmetic gives undefined
  values, and its final comparison counts that as "inside", so every point between the two end planes would be
  inside. OpenMerc's cylinder is acceptable as a deliberate difference; no stock path reaches it.
- Verify: none needed (unreachable).

#### G5.11 SPEC-740 §13 — position of the version-10 block (Resolution)

- Old: OpenMerc reads the extra block after the second node set.
- New: confirmed. Order: version, first node set, second node set, then **only for version 10 exactly** the extra
  block (int count, count × 36 bytes), then the regions (only for versions ≥ 11, so a version-10 file has none).
  Detail for the editor: records of the second set are read like the first (with the ignore int for versions ≥ 9),
  but their ignore value is discarded. They are only shown as markers and are not added to the editable node list.
  OpenMerc **matches** the order.
- Verify: data check only (no stock `.nav` files ship).

#### G5.12 SPEC-741 Cvars and §14.11 — how waypoint edit mode is entered (Correction)

- Old: "`bot_wp_edit` 1 (before the level loads): waypoint edit mode". In the oracle the cvar always reads back `0`,
  and `makewp` is not a command. OpenMerc follows the spec text.
- New: edit mode needs a **listen server with cheats and the local player alone**. At the start of every server frame
  (skipped while `freezeworld` ≠ 0) the game sets `bot_wp_edit` to `0` when `cheats` is 0, or when `dedicated` ≠ 0, or
  when any human client other than client number 1 is in the game (bots do not count). The editor client commands
  (`makewp`, `remwp`, `writewpfile`, `bot_wp_*`, …) are accepted only while `bot_wp_edit` ≠ 0 **and** those same
  conditions hold. Otherwise they fall through to ordinary client commands (chat). `cheats` is latched (SPEC-302),
  so it must be set before the map starts. On a dedicated server edit mode can never be entered, which explains the
  oracle observation. With the conditions met, the 10 s waypoint load sees `bot_wp_edit` ≠ 0 and the level stays
  "not ready" (message 7, no bots join), as §1 says. SPEC-741's cvar row should read: "1 on a non-dedicated server
  with `cheats` ≠ 0 and no other human player; forced back to 0 every frame otherwise".
  OpenMerc **differs**: it must force the cvar as above.
- Verify: `SoF.exe +set cheats 1 +set deathmatch 1 +set bot_wp_edit 1 +map dm/jpndm2` (listen server): after 10 s
  message 7, `bot_wp_edit` still `1`, and `makewp` places a waypoint. When a second human joins, `bot_wp_edit` becomes
  `0` within one frame.

### M. I4b Ghoul model drawing in the 3D view (items 1–5)

#### I4b.1 SPEC-002 entity 0x34 and SPEC-411 §5 step 1 — the instance handle in `frame` (Resolution)
- Old: "the Ghoul system has no instance for the low 16 bits of the entity's `frame` (system method 9)". OpenMerc
  passes the sign-extended low 16 bits.
- New: only the **low 16 bits** of `frame` are used; the upper 16 bits are ignored. System method 9 treats them as a
  16-bit id h: h = 0 → no instance; the server system looks up h; the client and menu systems look up −h, computed
  and compared in 16-bit arithmetic (signed 16-bit ids, so 0xFFFF and −1 are the same value and give id 1).
  Instances created by the engine's client itself (effect debris and Ghoul-model particles, SPEC-400) have positive
  client ids k and their entities carry `frame` = −k (a 32-bit negative number), so they are found as k. Server
  instances (id n > 0, client copy −n) arrive with `frame` = n. OpenMerc's sign extension gives the same result;
  any implementation that negates in more than 16 bits must truncate to 16 bits before the lookup.
- Verify: tsr1 intro debris (frame 0xFFFFFFFF, client instance 1) is drawn.

#### I4b.2 SPEC-002 "Dynamic-light write-back" — the engine's light count (Refinement)
- Old: "They go after the engine's own entries, up to entry 31, with intensity multiplied by that cvar."
- New, add: the renderer works on its own copy of the frame record. The model lights are written into the engine's
  light array (through the array pointer of the record) after the engine's entries, and only the **copy's** count
  is increased; the engine's `num_dlights` is not changed. After RenderFrame the extra lights lie past the engine's
  count, and the next frame overwrites them. This happens only when `r_norefresh` = 0 and `gl_dlightintensity` ≠ 0.
  A renderer that increases the engine's count makes a re-drawn, unchanged frame (paused game) accumulate copies.
- Verify: OpenMerc's trace observation (count unchanged after the call).

#### I4b.3 SPEC-411 open question 4 / SPEC-AMEND-002 1.11 — who sets 0x1000 and bit 28 (Resolution)
- Old: "Which game entities set entity flag 0x1000 (cast shadow) and flag bit 28 (ambient-only lighting)."
- New: both reach the renderer as entity render flags; for server entities they come from the entity state's
  render-effects field set by the game module, for client effects from the engine.
  - 0x1000 (casts a shadow): the game module sets render effects 0x401000 (Ghoul model + 0x1000) for **every
    character** when it starts (all classes started as characters: soldiers, civilians, animals, bosses), so the flag
    is present from the first frame, also while the entity has no instance handle yet (frame 0). Items: the CTF flags
    get 0x1000 when spawned and when returned to their base (UNVERIFIED exact item condition); picking an item up
    clears Ghoul model and 0x1000 together. Nothing else sets it (props, vehicles, the view weapon do not).
  - Bit 28 (ambient-only lighting): the engine's client sets flags 0x400000 (Ghoul model) and bit 28 together on every
    **client-created Ghoul entity**: effect debris with a Ghoul model and Ghoul-model particles of `.eft` effects
    (SPEC-400), together with `frame` = −(client instance id) (I4b.1). Among server entities only the prop
    `misc_generic_urinal` sets bit 28 (in its spawn function).
  The tsr1/nyc2 traces agree.
- Verify: `misc_generic_urinal` (where present) is lit by the light point at its origin only, without specular.

#### I4b.4 SPEC-411 open question 1 / §6.2 — frame of the probe direction inside the Ghoul system (Resolution)
- Old: SPEC-411 §6.2: the wrap light's direction V is "taken as a world-space direction and turned into the model's
  frame like the other lights". Open question 1: whether the Ghoul drawer rotates it again.
- New: it is **not** turned from world or entity space. The Ghoul system stores the smoothed vector V as a light
  expressed in the frame of the **top-level instance being drawn** (its model frame, the frame its own instance
  transform maps into the entity frame). For the top-level instance it is therefore used exactly as given, as a
  model-space direction (only normalised); bolt-ons receive it converted from the top instance's frame into their own
  frames, as for the other lights. So the renderer's vector, built in the entity's axes (§5 step 8), is applied in the
  model's axes: for a model whose instance transform rotates (characters: −90° about Z, SPEC-AMEND-005 I3.14) the lit
  side follows the model's axes, not the entity's (derived; UNVERIFIED in game). This is I3's side; the renderer
  formula of §5 step 8 stays as written.
- Verify: draw a character with `ghl_light_method 1` through the original renderer once with the original Ghoul
  system and once with OpenMerc's: identical frames require the rule above.

#### I4b.5 SPEC-012 *Lighting* vs SPEC-411 §6.7 — GL linear attenuation of world/dynamic lights (Correction of SPEC-012)
- Old: SPEC-012: "Linear attenuation 1/start for model omni lights and 0.75/radius for world/dynamic lights";
  SPEC-411 §6.7: "LINEAR_ATTENUATION = 1/R". OpenMerc (`ghoul_draw.cpp`) uses 0.75/radius.
- New: SPEC-411 is right: the value is **1/R** (R = the light's radius). The original computes it as 0.75 divided by
  the light's inner radius 0.75·R, which is where SPEC-012's "0.75/radius" came from. SPEC-012 should read "1/radius
  for world and dynamic lights". OpenMerc's 0.75/R is 25 % weaker than the original (only with `ghl_light_method`
  > 2.5 and `ghl_gllight_attenuation 1`).
- Verify: GL trace with hardware lighting: GL_LINEAR_ATTENUATION of a world light of radius 300 is 1/300.

### N. G3 damage model, gore, bodies (items 1–15)

#### G3.1 SPEC-721 §2 rule 7 and §7 — bolt of the killing-blow cloud and of the blood-pool tracking (Correction)

- Old: rule 2.7: "the body remembers the zone and starts tracking the position of its body-side cap bolt (or gore
  bolt) … Every hit with d > H on a zone that has a wound mesh and a body-side cap bolt or gore bolt also attaches a
  `gore/bloodcloud_s` (size 50) there." The clean team observed the cloud on the gore bolt (zone 7 on
  `GBOLT_CHEST_F`, not on `GBOLT_CAP_HIPS_CHEST_TIGHT`).
- New: the two uses take the two bolts in **opposite** order.
  - **Cloud**: for every hit zone of a hit with d > H whose wound mesh is set, the `gore/bloodcloud_s` (size 50) goes
    on the zone's **gore bolt**. If the zone has no gore bolt, it goes on the **body-side cap bolt**. With neither
    there is no cloud. The owner part does not have to be shown. On a corpse (H ≤ 0) every hit with d ≥ 1 qualifies.
    The cloud is sent after that zone's wounding, severing or head explosion.
  - **Blood-pool tracking** (rule 2.7 first sentence, §7): **body-side cap bolt first**, gore bolt only when the zone
    has no body-side cap bolt. This happens once per body, for the first hit zone of the first hit with d > H. It has
    no wound-mesh condition. If the zone has neither bolt, the zone is still remembered (also for the zone death of
    SPEC-722 rule 11) but the tracked offset is zero, so the pool lies under the character origin.
  - Example: male zone 7 has gore bolt `gbolt_chest_f` and body-side cap bolt `gbolt_Cap_hips_chest_tight`. The cloud
    is on the first and the pool follows the second.
  OpenMerc's observation **matches**. Implement the pool with the cap-first order.
- Verify: the DM self-rocket capture shows the four clouds on the gore bolts. Kill a meso soldier with one chest
  shot: the `gore/bloodpool` lies under the hips/chest junction, not under the chest front.

#### G3.2 SPEC-720 §13 — player hit sounds (Correction)

- Old: §13 "Deathmatch with a player body: … the hit sound above". OpenMerc plays them only when the player
  survives the hit.
- New: the hit sounds (`impact/gore/sizzle.wav`, `impact/gore/impact<1–3>.wav` with `impact/player/hit<1–4>.wav`,
  `impact/player/armor.wav`) are played only for a player **without a body model**. Every deathmatch player has a
  body model and single-player players never have one (SPEC-780 §16), so the sounds are played **only in single
  player**.
  - In deathmatch none of them is played, whether the hit kills or not. The clean team's observation is one case of
    this rule. Remove "the hit sound above" from the deathmatch paragraph of §13.
  - In single player "living" means health > 0 **before** the hit is subtracted. The killing hit therefore plays them
    too, like every other hit. A hit on a dead player plays nothing.
  OpenMerc **differs**: a surviving hit in deathmatch must be silent too, and a killing hit in single player must play
  the sounds.
- Verify: DM, non-lethal self-rocket: no `impact/gore/impact*` and no `impact/player/hit*` svc_sound. SP: a soldier
  kills the player: `impact/gore/impact<n>` (channel 4) and `impact/player/hit<n>` (channel 2) are sent in the frame
  of death.

#### G3.3 SPEC-720 §10.2 — body records that map to no zone; wound-mesh records (Resolution)

- Old: "No Ghoul hits at all and no forced zone: the hit does nothing" covers only the empty list. OpenMerc keeps the
  damage unscaled when no record maps to a zone, and maps a record on a wound mesh (`gz_*`) to that mesh's zone.
- New:
  - A record maps to a zone only through the zone's **owner part**: the record's part must equal a zone's owner part
    (§10.1). Records on any other part of the character's own instance (caps, wound meshes, parts that own no zone)
    belong to no zone. The exit scan and the entry scan both skip them and continue with the next record.
  - If no body record maps to a zone and no thrown-knife zone is forced, the hit **continues** with no hit zone and
    no exit zone. Only the empty list and the bolt-on-first case of §10.2 end the hit with nothing.
    - Not applied: zone scaling (§11.6 "no zones": damage unchanged), wounds, limb-hurt marks.
    - No blood point, so no blood splat (§11.5).
    - Still applied: the per-hit debris puff (G3.7), player armour (§11.3), the overkill cap (§11.7), reactions and
      gibbing (§11.8).
  - Wound meshes: the ray test reports them (no stock character mesh is no-block, SPEC-014 §7), but such a record
    belongs to no zone and is skipped. The zone comes from the owner-part record next to it. Zones whose owner part
    was re-pointed (player mask head, brain-shot `_headhelmet`) map through the new owner part.
  OpenMerc's "unscaled" **matches**. Its mapping of wound-mesh records **differs**. It gives the same zone in the
  common case (the wound mesh lies on its own owner part), but it can change which record is the entry or the exit.
  Where a wound mesh overlaps a neighbouring owner part it can also change the zone. Follow the owner-part rule.
- Verify: data check: no mesh of `ghoul/enemy/meso`, `ecto` or `female` has a `NOBLOCK` user property. Unit test: a
  hit list with one record on a shown `gz_*` mesh and nothing else gives no zone and an unchanged damage.

#### G3.4 SPEC-720 §10.2 — a single zone-mapping record (Correction)

- Old: "Entry: the first body record before the exit record that maps to a zone". OpenMerc uses a single record (entry
  = exit) as the entry and has no exit zone.
- New: the entry scan stops **before the exit record**. If the exit record is the only body record that maps to a
  zone, or the nearest one, the hit has **no hit zone**, and that zone stays the exit zone. Consequences:
  - Damage, wounds and effects:
    - the damage is not scaled;
    - no wound, no severing and no limb-hurt mark on that zone;
    - no blood point, so no blood splat;
    - the spread hint of §10.4 becomes "none".
  - Exit: SPEC-721 rule 5.1 needs an entry zone, so a living target gets nothing at the exit. A corpse gets rule 5.2
    (bloodcloud_s with probability 1/2, spray with probability 1/3 with PROJECTILE).
  - Thrown knives: they have no exit scan. Their entry scan covers every record of the list except the **last** one.
  OpenMerc **differs**: it wounds and scales that zone.
- Verify: with `ai_goretest 1` the original prints its hit analysis. A grazing pistol shot through the edge of a
  forearm, which crosses only the far side of the mesh, prints an exit wound position and no entry wound line. The
  damage is then the unscaled pistol damage, with no wound skin.
  UNVERIFIED in game; needs a black-box run. A unit test with one zone-mapping record gives no hit zone.

#### G3.5 SPEC-720 §10.1 — zone of a record: "smallest distance" and the thrown-knife weight (Correction)

- Old: "among the zones whose owner part is the part of the record, a zone without gore bolt wins by smallest
  distance; zones with a gore bolt are compared by the squared distance … For thrown knives in deathmatch the
  distances of later candidates are weighted × 1.3 unless the current best is a head zone (UNVERIFIED)." OpenMerc
  takes the first of two zones without gore bolt.
- New: candidate zones (owner part = the record's part) are visited in **zone-index order**.
  - The "distance" of a zone without gore bolt is the record's own distance along the shot. It is the same value for
    every candidate of that record. So a zone **without** gore bolt is taken only when no candidate has been taken
    yet, and it never replaces a taken candidate.
  - A zone **with** a gore bolt is taken in two cases: no candidate has been taken yet, or its squared bolt distance is
    **smaller** than the smallest squared bolt distance already seen for this record. A taken zone without gore bolt
    does not count, so any later gore-bolt candidate replaces it. Ties keep the earlier zone.
  - Result: if any candidate has a gore bolt, the one with the nearest gore bolt wins. Otherwise the lowest-index
    candidate wins (male thighs: 16 or 18). OpenMerc **matches**.
  - Thrown knife in deathmatch (entry scan only): the candidate's squared distance is multiplied by 1.3 **while the
    current choice is a head zone (0, 1 or 2)**, and the weighted value becomes the stored best. So a head zone, once
    chosen, is harder to replace. The old "unless" is inverted. This is no longer UNVERIFIED.
- Verify: unit test with the male table: a record on `_rtightthigh` gives zone 16. DM knife: for a record on the head
  part, zone 1 replaces zone 0 only when its squared gore-bolt distance is less than the zone-0 distance ÷ 1.3.

#### G3.6 SPEC-720 §4, §10.2 and §11.5 — "blood splat … size 200, heavy variant 1/6" (Correction)

- Old: §4 "sends a blood splat behind the point (SPEC-401, size 200, heavy variant with probability 1/6)"; §11.5
  "a blood splat (size 200) behind it". OpenMerc follows SPEC-771 C.2.
- New:
  - The splat is exactly SPEC-771 C.2 temp entity 3: a trace of 128 units from the point along the shot direction
    (solid and window contents, ignoring the target). It is sent (PHS at the point) only when the trace hits the world
    or a brush entity at fraction ≤ 0.99, and never under `lock_blood`.
  - "200" is an argument the message does not carry. It has no effect.
  - There is no heavy variant. The 1/6 is a **send probability**:
    - **player** target: the splat is attempted only when one uniform draw over six outcomes gives the first one
      (probability 1/6), and never for means of death 27;
    - **human body** (§11.5): it is attempted on every hit that has a blood point.
  - §10.2 "its point (shot start + direction × distance), clamped into the target's bounding box" is also wrong. The
    entry record's distance is not used. The blood point is the **damage point** itself: for traced shots the trace
    end, i.e. the first Ghoul record. It is clamped into the target's absolute bounding box. The body's splat trace
    starts there.
  OpenMerc **matches** SPEC-771 C.2. It must add the 1/6 gate for players and use the damage point as the blood point.
- Verify: DM, 60 pistol hits on a player standing 64 units in front of a wall give about 10 TE 3. The same hits on a
  soldier in front of the wall give one TE 3 per hit that resolves a zone.

#### G3.7 SPEC-720 §11.5 — the "debris/blood puff" of every hit (Resolution)

- Old: "a hit with a non-zero point sends a debris/blood puff (SPEC-401) at the point". OpenMerc sends nothing yet.
- New: on every human-body hit (characters, deathmatch players, the armoured boss) that is not ended earlier, with
  a damage point other than (0, 0, 0), the game sends one **TE 18 debris burst** (SPEC-201, SPEC-771 C.2).
  - When it is sent: after player armour (§11.3) and zone armour (§11.4) have run, before the blood splat, the zone
    multiplier, the gib and the zone wounding. Hits ended by armour or by hit location send none.
  - Payload: position = the damage point; direction (0, 0, 0) in the direction-byte encoding of SPEC-201; count
    irand(0, 5) (0 is possible and is still sent); size class 4; chunk type 32 (flesh); extents 0, 0, 0; surface
    material 35.
  - The TE 18 rules of SPEC-771 C.2 apply. It is not sent under `lock_blood`, and then it does not count for the rate
    limit. It is dropped when the previous TE 18 burst (game-wide) had material 35 and was sent at most 0.1 s earlier.
- Verify: capture of single pistol hits on a soldier, more than 0.1 s apart: one TE 18 per hit (size class 4, chunk
  type 32, material 35, count 0–5) at the hit position. Two pellets in the same frame give one burst.

#### G3.8 SPEC-721 §4 against SPEC-771 C.2 — head-explosion debris and the TE 18 rate limit (Resolution)

- Old: SPEC-721 §4 sends two flesh bursts at the hit position. SPEC-771 C.2 drops a TE 18 burst with the same material
  within 0.1 s of the previous one. The clean team asks which rule wins.
- New: the **rate limit wins**, because every TE 18 of the game module goes through it.
  - Both head-explosion bursts use material 35 and are sent in the same frame. So the second burst (irand(3, 6) chunks
    of size 4) is **always** dropped.
  - The first burst (irand(1, 3) chunks of size 1) belongs to a hit that has already sent the per-hit puff of G3.7
    (also material 35, same frame). It is therefore dropped as well. It is sent only when that hit sent no puff (damage
    point (0, 0, 0)) and no other material-35 burst was sent in the preceding 0.1 s.
  - So in practice a head explosion shows no flying chunks. `gore/headcloud` and `impact/gore/headexp.wav` are not
    affected.
  A reimplementation that keeps one TE 18 sender with the limiter reproduces this without a special case.
- Verify: `ai_goretest 1`, pistol head shot that explodes a meso soldier's head: the capture of that frame holds
  exactly one TE 18 (size class 4, count 0–5) and none of size class 1.

#### G3.9 SPEC-721 variant tables — full sever groups of the abbreviated thigh rows (Resolution)

- Old: civilian `_rthigh` / `_lthigh` and female `_rtightthigh` / `_ltightthigh` rows give "sever group = `_rthigh`…".
  OpenMerc completes them after the male pattern without the thigh bolt.
- New: the complete groups, in order (special entries as in rule 3.5):

  | Family | Shown mesh | Zones | Sever group |
  |---|---|---|---|
  | civilian/officer (`enemy/ecto`) | `_rthigh` | 16, 17 | `_rthigh`, bolt `abolt_thigh_r`, + right lower leg zone, `_rcalf`, `_rcalfcuff`, `_cap_rcalfcuff_rfoot`, bolt `abolt_calf_r`, `_rfoot` |
  | civilian/officer | `_lthigh` | 18, 19 | `_lthigh`, bolt `abolt_thigh_l`, + left lower leg zone, `_lcalf`, `_lcalfcuff`, `_cap_lcalfcuff_lfoot`, bolt `abolt_calf_l`, `_lfoot` |
  | female (`enemy/female`) | `_rtightthigh` | 16, 17 | `_rtightthigh`, bolt `abolt_thigh_r`, + right lower leg zone, `_rtightcalf`, bolt `abolt_calf_r`, `_rheels` |
  | female | `_ltightthigh` | 18, 19 | `_ltightthigh`, bolt `abolt_thigh_l`, + left lower leg zone, `_ltightcalf`, bolt `abolt_calf_l`, `_lheels` |

  These four are the only abbreviated groups in SPEC-721. OpenMerc's lists **match** except for the missing thigh bolt.
  Insert bolt `abolt_thigh_r` / `abolt_thigh_l` after the thigh mesh, so that bolt-ons on it leave with the piece.
- Verify: sever a civilian's thigh (`ai_goretest 1`, shotgun on a corpse): a bolt-on on `abolt_thigh_r` (if the
  character has one) flies off with the leg instead of staying on the hips.

#### G3.10 SPEC-721 test vector 5 — which model shows a brain shot (Correction)

- Old: "kill a meso soldier wearing `_helmethead` … → head changes to `_headhelmet` and `g_brain` appears".
- New: rule 5.1 tests whether the character's **model has a part named `_headhelmet`** (shown or not). The entry
  zone's owner must not be `_headbeard`. The meso head mesh `_helmethead` plays no part.
  - Stock data: `_headhelmet` exists only in the civilian/officer family: 6 of the 7 `ghoul/enemy/ecto` models, all
    except `ECTO_KOS1`. None of the 41 meso models and none of the 9 female models has it.
  - So meso soldiers never show a brain shot. A killing sniper shot from the front of the head to its back gives them
    the normal exit chunk instead (`g_exit_chunky_anim1` with a bloodcloud on the back-of-head gore bolt), unless the
    head explodes.
  - Rewrite the vector: "kill a civilian/officer character (`enemy/ecto`, model other than `ECTO_KOS1`) whose head
    is not the beard head, from the front, with a living-target head shot of more than 80 damage that exits the back
    of the head → head changes to `_headhelmet`, `g_brain` appears at `abolt_head_t`; with the beard head shown it
    never happens."
  The clean team's data check **matches**.
- Verify: data check over `ghoul/enemy/*/*.ghb` for the part name `_headhelmet`.

#### G3.11 SPEC-721 §6 — severed piece: physics values, pose, origin, box, lifetime (Correction and Resolution)

- Old: "physics parameters 0.5, 1.0, 0.1, 0.7, 0.5 (UNVERIFIED meaning)"; "angles: the character's angles with yaw +
  flrand(−30, 30)"; "bounding box: the bounds of the shown parts 0.3 s ahead"; "fallback … box ±8"; lifetime "by the
  limb AI". OpenMerc reads the five values in SPEC-782's column order and asks about the pose, the origin and the
  lifetime.
- New:
  - **Physics values**, with the property names of SPEC-760: ground friction 0.5, gravity 1.0, air friction 0.1,
    buoyancy 0.7, elasticity 0.5. Stop speed keeps its default (100 from the first physics step). OpenMerc's reading
    (gravity 0.5, elasticity 1.0, …) is **wrong**.
  - **Pose**: the piece's instance is a copy of the character's instance. It takes over the character instance's own
    aligned bolt (SPEC-011 method 29 with the value of method 30). Then it is **paused at the current time** (method
    19), so it keeps the character's pose of that moment and does not play on. All its mesh parts are hidden, and the
    handed-over parts are shown by rule 3. Bolt-ons on the piece keep animating until the limb AI freezes them 1 s
    later (SPEC-732 §9 step 1).
  - **Origin and angles**: origin = the world position, at the current time, of the zone's piece-side cap bolt. If
    the zone has no piece-side cap bolt, its body-side cap bolt is used. The entity angles start at (0, 0, 0).
  - **Instance transform**: chosen so that the model stays where it was. It equals the character's instance
    transform followed by the character's entity frame, expressed in the piece's entity frame (that origin, zero
    angles).
  - **Yaw**: only after that the yaw becomes flrand(−30, 30), turning the piece about the cap bolt.
  - Replace "the character's angles with yaw + flrand(−30, 30)" by "(0, flrand(−30, 30), 0); the character's
    orientation is carried by the instance transform". This matters for the −150 °/s pitch spin and for the limb AI's
    "pitch ≤ −85°" rule. OpenMerc's shift of the clone transform **matches** for the origin.
  - **Box**: the exact posed bounds of the zone's **owner part** mesh only, not all shown parts. They are taken 0.3 s
    ahead (the pose is frozen, so this is the current pose) along the axes of the character's entity frame. Only the
    extent is used: the box is centred on the origin.
  - **Fallback** (the zone has no cap bolt at all, or no owner part): if the body cannot play sequences, no piece is
    made. Otherwise the piece plays `std_istand_n_a_n`.
    - Origin = character origin + (0, 0, 24).
    - Box mins (−8, −8, −8), maxs (8, 8, **0**).
    - Velocity = (75 × dx, 75 × dy, irand(30, 70)), d = the direction passed by the caller.
  - **Blood**: the size-400 `gore/bloodcloud` and the `gore/stumpblood` on the cap bolt are added only for pieces of
    rule 3 (severing, also dog/cow severing). Gib pieces (rule 8) get neither.
  - **Lifetime**: the piece entity's own think does nothing but re-arm itself every 0.1 s. Without the type-13 AI a
    piece is never removed. Implement SPEC-732 §9 (approved; type-13 "severed limb character"):
    - settling when speed² < 10 or after 30 s;
    - after settling the piece stays only while a client within 800 units faces it;
    - at most 30 s, then a 1 s fade, removal 1.1 s after the fade starts.
- Verify: sever a soldier's arm and watch it: the arm keeps the pose of the moment of the cut, it is not animated, and
  it is removed within 31.1 s. Capture the piece entity: its first angles have pitch 0 and roll 0.

#### G3.12 SPEC-720 §10.2 b — "armoured above half health" (Correction)

- Old: "for a weapon held in a hand, when not in deathmatch and the character is armoured above half health (or in a
  particular AI state, UNVERIFIED), the hit continues with no zone". OpenMerc uses armour percentage > 50.
- New: the rule applies when the first record lies on a bolt-on with a non-zero weapon type, the game is not
  deathmatch, the target does not have entity flag 0x40000, and **either** of these holds:
  - the character's combat profile has **flag 1** set (SPEC-700 rule 19, profile[7] bit 1; this is the "AI state");
  - its **armour percentage** (SPEC-722, range 0–100) is **above 0.5**, i.e. any armour worth more than half a
    percent remains, **and** its health is above half of its maximum health.
  Then the hit ends with nothing when the damage is below 4, else it continues with no zone and unscaled damage. With
  entity flag 0x40000 neither this rule nor the knock-off rule c) applies. OpenMerc's "> 50 %" **differs**.
- Verify: a single-player soldier with armour type 1 at full health is shot in the rifle with 4 or more damage: no
  wound, health drops by the unscaled damage. Below half health the same shot goes through the knock-off rule c)
  instead (UNVERIFIED in game).

#### G3.13 SPEC-722 death rule 11 — shoulder segments (Resolution)

- Old: "shoulder zones by sever segment 1 or 4 → `std_sshoulder_rt_n_n`, segment 2 or 3 → `std_sshoulder_lt_n_n`"
  without the segment numbers. OpenMerc maps zones 3, 6 → right and 4, 5 → left for every family. It gives the
  civilian zone 6 no shoulder death.
- New: the segment is the one of the first entry of the zone's sever group. No variant changes the shoulder groups.

  | Family | Zone 3 (right front) | Zone 4 (right back) | Zone 5 (left front) | Zone 6 (left back) |
  |---|---|---|---|---|
  | male soldier (also armoured male body) | 1 → right | 2 → left | 3 → left | 4 → right |
  | female | 1 → right | 2 → left | 3 → left | 4 → right |
  | civilian/officer | 1 → right | 2 → left | 3 → left | 0 (torso) → no shoulder death, falls through |

  OpenMerc **matches** for all three families.
- Verify: kill a female soldier with a hit whose first killing zone is GZ_SHLDR_LEFT_BACK: she plays
  `std_sshoulder_rt_n_n`.

#### G3.14 SPEC-003 method 31 / SPEC-601 §3 — type of the third argument, and the bypassed share (Correction)

- Old: SPEC-003 "(damage, float, ?) → int". The game header the clean team uses declares an int third argument.
  OpenMerc passes trunc(value 12). SPEC-601 §3: "(the caller handles the remaining D × f itself)".
- New:
  - The arguments are (int damage, float piercing = value 11, float fraction = value 12). The game passes values 11
    and 12 of the damage call unchanged, as 32-bit floats, and the player module reads both as floats. trunc() turns
    every fraction below 1 into 0, i.e. no bypass, which is **wrong**. Pass the float.
  - No caller adds D × f back. The value the method returns is the damage that remains: the single-player path
    returns it to the damage call, and the deathmatch body path uses it as the hit's damage. So against a player with
    armour > 0 the share D − trunc(D − D × f) is **lost**: neither absorbed nor dealt. With armour 0 the full D is
    returned. The character zone armour of SPEC-720 §11.4 behaves the same way and is already written so.
  - Delete the parenthesis in SPEC-601 §3. SPEC-003 row 31 should read "(int damage, float piercing, float
    fraction) → int, SPEC-601 §3".
- Verify: unit vector of the player module: armour 50, D 40, piercing 0, fraction 0.5 → result 0, armour 30. A
  single-player hit with these values leaves health unchanged.

#### G3.15 Harness note (`lock_blood` stays on)

- Acknowledged, no spec change.

## Constants

### A. SPEC-204 GameSpy

| Item | Value |
|---|---|
| Query-port search | `gamespyport` + 0 … + 103 (five overlapping 100-port searches) |
| Master challenge lengths observed | 6 (sofvault, 333networks), 8 (OpenSpy) |
| `sv_violence` bits | 0x01 deaths, 0x02 blood, 0x04 gore zones, 0x08 dismemberment, 0x10 gore textures (server locks), 0x20 pure server |
| Pure-server file checksums (SPEC-301 §8) | game module 64C0FC54 (hex), player module EACC2EEA (hex) |

### B. G18 game module

| Item | Value |
|---|---|
| Entity state / player state / user command | 116 / 200 / 24 bytes |
| Player-movement block | 256 bytes |
| Cvar record | 32 bytes |
| Surface record | 72 bytes; material = byte 35 |
| Rotation block marker | v0 = 999 |
| Stagger factor | max(0, 1 − v) for v > 0 |

| Name | Value |
|---|---|
| pm_stopspeed / pm_maxspeed / pm_duckspeed | 100 / 300 / **75** |
| pm_accelerate / pm_airaccelerate / pm_wateraccelerate | 10 / **1 (fixed)** / 10 |
| pm_friction / pm_waterfriction / pm_waterspeed | 6 / 1 / 400 |
| Box | (−16, −16, −24) – (16, 16, **40**); ducked maxs z **14**; gib (−16, −16, 0) – (16, 16, 16) |
| View heights | standing **35**, ducked **0**, gib 8, noclip **35**; SIZEMATTERS formulas s·59 − 24 (−8…39) and s·24 − 24 (−10…13) |
| Jump / water jump | vz = 270 exactly / horizontal 200 × forward x, y; vz 350; pm_time 255 |
| Airborne threshold | vz > **100** |
| Land timing | 18 (vz < −200), 25 (vz < −400) |
| Step | 18 units, ground normal 0.7, 4 bumps, overbounce 1.01, stop epsilon 0.1, rub 1 + 0.5 × (v̂ · n) |
| Emergency snap search | ±10 eighths per axis, step 2, z then y then x, each from +10 down |
| Initial snap order | +1, 0, −1 eighth per axis (z outer, x inner) |
| Final snap jitter order | 0, 4, 1, 2, 3, 5, 6, 7 |
| Acceleration scale | max(0, 1 − block float +0xFC) when that float is > 0, else 1 |

| Item | Value |
|---|---|
| Inhibit byte | bit 0x01 = attack not armed; cleared only by a human's deathmatch respawn |
| Records kept by the player-model precache | maxclients + 1 … + min(8, loadable models), deathmatch only |
| Bot waypoint loader | 1 record when `sv_globalrankings` 0 and deathmatch; think at + 10 s |
| First map entity | DM: maxclients + 17 (+ 1 with the bot system, + 1 with `script`); SP: maxclients + 9 (+ 1 with `script`) |
| Corpse records | maxclients + 1 + n, n = 0 … 7 cyclic |
| Strafe / backwards thresholds | \|c\| < 0.8 / c ≤ −0.8; side from the right vector, r ≥ 0 → `_lt_` |
| Run destination | 100 units (run button), else 1 unit; walk test < 10 |
| Material-flag string | 25 characters; extra writers in G18.27; position 24 = `p` |

#### Material sound list (115 entries, in precache order)

| # | sound |
|---|---|
| 0 | `Ambient/Gen/Electric/spark1.wav` |
| 1 | `Ambient/Gen/Electric/spark2.wav` |
| 2–5 | `player/foot/metal/metal1.wav` … `metal4.wav` |
| 6–9 | `player/foot/snow/snow1.wav` … `snow4.wav` |
| 10–13 | `player/foot/water/water1.wav` … `water4.wav` |
| 14–17 | `player/foot/grass/grass1.wav` … `grass4.wav` |
| 18–21 | `player/foot/gravel/gravel1.wav` … `gravel4.wav` |
| 22–25 | `player/foot/sand/sand1.wav` … `sand4.wav` |
| 26–29 | `player/foot/wood/wood1.wav` … `wood4.wav` |
| 30–33 | `player/foot/stone/stone1.wav` … `stone4.wav` |
| 34–36 | `Weapons/FX/Rics/ric1.wav` … `ric3.wav` |
| 37 | `Impact/Surfs/stone.wav` |
| 38 | `Impact/Surfs/water.wav` |
| 39 | `Impact/Surfs/wood.wav` |
| 40 | `Impact/Surfs/grass.wav` |
| 41 | `Impact/Surfs/gravel.wav` |
| 42 | `Impact/Surfs/sand.wav` |
| 43 | `Impact/Surfs/snow.wav` |
| 44 | placeholder text `shouldn't be loaded now` |
| 45–47 | `Impact/Gore/impact1.wav` … `impact3.wav` |
| 48–51 | `Weapons/FX/FlyBy/FlyBy1.wav` … `FlyBy4.wav` |
| 52 | `Weapons/Glock/fire.wav` |
| 53 | `ambient/Nature/weather/thnder1.wav` |
| 54 | `weapons/FlshGren/FlshExp.wav` |
| 55 | `Weapons/Autoshot/fire.wav` |
| 56, 57 | `Enemy/MGun/trnslow.wav`, `Enemy/MGun/trnstop.wav` |
| 58 | `weapons/C4/C4Exp.wav` |
| 59–61 | `Impact/Chair/Squeak1.wav` … `Squeak3.wav` |
| 62–65 | `Ambient/Locs/Subway/Train/Brake.wav`, `Horn2.wav`, `Impact.wav`, `Move.wav` |
| 66–68 | `Weapons/Knife/Swing1.wav` … `Swing3.wav` |
| 69, 70 | `Weapons/Knife/Impact.wav`, `Weapons/Knife/Throw.wav` |
| 71 | `Player/Land/Grass.wav` |
| 72 | `Player/Land/Gravel.wav` |
| 73 | `Player/Land/Metal.wav` |
| 74 | `Player/Land/Sand.wav` |
| 75 | `Player/Land/Snow.wav` |
| 76 | `Player/Land/Stone.wav` |
| 77 | `Player/Land/Water.wav` |
| 78 | `Player/Land/Wood.wav` |
| 79–81 | `impact/surfs/metal1.wav` … `metal3.wav` |
| 82 | `Impact/GlassBreak/GBBig.wav` |
| 83 | `Impact/GlassBreak/GBMed.wav` |
| 84 | `Impact/GlassBreak/GBSmall.wav` |
| 85 | `Impact/Break/Metal.wav` |
| 86 | `Impact/Break/Stone.wav` |
| 87 | `Impact/Break/Wood.wav` |
| 88 | `Ambient/Gen/Electric/buzz.wav` |
| 89 | `Impact/Explosion/Big.wav` |
| 90 | `Impact/Explosion/Med.wav` |
| 91 | `Impact/Explosion/Small.wav` |
| 92 | `Ambient/Models/Phone/Hit.wav` |
| 93 | `Ambient/Models/TrashCan/LidSpin.wav` |
| 94–96 | `weapons/FX/Tink/Metal1.wav` … `Metal3.wav` |
| 97, 98 | `weapons/FX/Tink/Gen1.wav`, `Gen2.wav` |
| 99–101 | `Impact/Gore/Fall1.wav` … `Fall3.wav` |
| 102–104 | `weapons/FX/Crack/Crack1.wav` … `Crack3.wav` |
| 105–107 | `enemy/Helicopter/Fire1.wav` … `Fire3.wav` |
| 108–110 | `weapons/FX/Tink/shot1.wav` … `shot3.wav` |
| 111–113 | `weapons/FX/Tink/hvy1.wav` … `hvy3.wav` |
| 114 | end marker (never marked) |

Entries not named in G18.27 are never marked by the level-start rules (other code may precache the same files
directly).

### D. I3 Ghoul model system

| Item | Value |
|---|---|
| Tint quantisation | trunc(v × 255), v > 1 → 255, v < 0 → 0 |
| Shadow quad growth | g = 16 × trace fraction; second-pair v growth 1 texel; bounds grown 1 texel |
| Shadow pass offsets | (0, 0), (−1, −1), (+1, +1) world units; alpha darkness × 0.3333 |
| Shadow darkness factor | × (tint alpha / 255)² |
| Mask texture coordinate | u/64 + 0.5 |
| Hardware-lighting shininess | 4 × 2^(10·glossiness) |
| Specular on | glossiness > 0, level ≥ 0.051, self-illumination < 0.99 |
| Model rotation | −90° about Z: rows (0, 1, 0), (−1, 0, 0), (0, 0, 1) |
| Player body offset | translation (0, 0, −24), attach bolt `quake_ground` |
| States per instance and client | 20 (unchanged) |

### E. I5 client effects and temp entities

| Item | Value |
|---|---|
| Arc / bolt sprite | client sprite 7 `lit`, additive, not world-lit |
| Arc smoothing | Q_k = 0.3·S_(k−1) + 0.7·R_k; segment start pushed back by 0.06·(R_k − Q_(k−1)); arc ends at Q_(n−1) |
| Arc branch | probability 1/5, from the segment start, life 500 ms, no fade |
| TE 6/7 debris chunk (style 1) | 1 chunk, scale 25 |
| Decal fitting factors | 1.0, 0.8, 0.6, 0.4, 0.2, 0.1, 3.13 × 10⁻⁸, then −0.0999999717 |
| `.eft` light decay | 0 |
| TE 31 brightness | L = √(r² + g² + b²) of the light sample, direction 255 |
| TE 48 sheet | push 6 units/s per step along (1.4·F.x, 1.4·F.y, 1.3·F.z); pitch rate ±(200 ± 50)·U.z °/s; limits ±70° with ±15° jitter; fall speed −18, acceleration −1, horizontal × 0.9 per step |
| Random rotation | rand(0, 6.28); dust puffs rand(0, 2π) |
| "White" | (255, 255, 255, 255) burning bits of types 19/38/44; (250, 250, 250, 250) otherwise |
| Fire trail (0.05 s) puff size | parent +0x10 × parent +0x0c, no factor |

### F. I4a renderer

| Item | Value |
|---|---|
| 2D projection | orthographic, left 0, right W, bottom H, top 0, near +9999, far −9999 |
| Stretch-pic texture table | 8 entries (0–3 quarter turns, 4 unchanged, 5 left–right mirror, 6 top–bottom mirror, 7 half turn) |
| Direction table behind import 0x68 | 250 entries of 12 bytes (SPEC-201 Appendix A) |
| Light sample segment | 256 units along the table entry; 2048 units down for 255 |
| Float char code | low 8 bits of the argument |
| Input widget default colours | tint and atint opaque white, btint transparent black; page font colours not inherited |

### G. I1a engine core

| Item | Value |
|---|---|
| Parental-lock key words | 15 (pairs 0–5, K, pair 6) |
| `dir` colours | separator and files 7, folders 1, mount headers 3; folder indent 10 spaces, mount indent 11 |
| CPU detection | family 4–6 only for `cpu_mmx`/`cpu_amd3d`; MHz = trunc((TSC ticks over ≥ 101 ms − 91) / 100000) |
| Connection states | 0, 1, 2, 3, 4, 7, 8 (5, 6 unused) |
| String-package line limit | 8192 bytes |
| `allow_download` default | "0" |

### H. G6b script wiring

| Item | Value |
|---|---|
| Worldspawn runner use | end of single-player ClientBegin; self = caller = runner, activator = client entity |
| Camera / respawn effects bit | 0x4000 (entity effects); cleared when camera counter < 1 and time ≥ respawn time + 0.5 s |
| Developer print condition | `developer` integer value odd |
| `tsr1` inline model 31 bounds | (−8, 0, 0) – (8, 112, 16) |

### I. I6h client view, HUD, input, demos

| Item | Value |
|---|---|
| Big-digit colour (weapon panel) | HUD green (0, 220, 0, 255) |
| Crosshair clamp | value > 4 or < 0 → 4, numeric value only, at the first drawn level frame after a change |
| Loading progress per configstring | 8 of every 16 configstrings received while not registered (process-wide counter), index < 1415 |
| Cheat-check exemption | configstring 31 exactly `1` or empty |

### J. G9 game modes and Ghoul glue

| Item | Value |
|---|---|
| Conquer configstring 33 / 10 | `1`/`0` (team play) / `1`/`0` (`dmflags` 0x8000); written when changed, cache cleared at level setup and exit |
| Status/scoreboard refresh | server frames whose number is a multiple of 32 |
| Help time | `help`: now + 30 s (toggle); status displays: now + 5 s |
| DM_GENERIC 72 (pak2) | `client_conq_sb %hd %hd %hu %hd %hu %hu `; game passes x, y, team − 1, team score |
| zmain `dm_generic` | COUNT 67 (entries 67–72 absent) |
| Single-player meter | stats[11] = trunc(alert level × 100), stats[8] bit 0x200 |

### K. G2 character AI

| Item | Value |
|---|---|
| Lethal health | target health set to 1 when > 1 |
| Script action end with 0x100 | completion of an action of class 13 or 14, or kind-1 arrival |
| Special death start offset | 1.5 s, mode 1, restart |
| Footstep hysteresis | previous heel height > 9, current < 7 (relative to the floor height) |
| Jitter turn factor k | player 2 → 1, other 10 → 0 over 92…512·T-c; 1 beyond |
| Jitter step scaling | yaw: k/8 under 30°, k/4 under 60°; pitch: k/8 under 15°, k/4 under 30°; applied only when < 1 |
| Jitter noise | yaw goal 0.75·E, yaw step 0.1·E, pitch goal 0.15·E, pitch step 0.05·E |
| Kill score | trunc(score + profile[6] × difficulty score) |
| Idle class-0 action | duration 0, completes at once |

### L. G5 navigation and bots

| Item | Value |
|---|---|
| "No network connection" flag | server flags (entity offset 0x118) bit 0x200; set at bot creation, field cleared at removal |
| Imports that drop sends to it | #16, #17, #20, #30 (and #48/#49 with an entity), #22/#23 with local = 1 |
| Imports that do not test it | #14, #18 (entity), #43, #44, #46 (overflow stop for a never-connected slot) |
| `info` reply middle count | connected clients carrying server flag 0x200 (always 0 in 1.07f) |
| First fill bot / spacing | level time 10.2, then every 1.5 s (fill request made in the same frame as the previous add) |
| Bot fire-time value | 1.0 in the requested mode, 0 otherwise |
| Bot item requests | 1 (`itemnext`) while the selected type differs, 4 (`itemuse`) when it matches |
| Edit-mode conditions | `cheats` ≠ 0, `dedicated` 0, no human other than client 1; checked every frame |
| Signed path index limit | routes only start in and cross into paths 1–127 |

### M. I4b Ghoul model drawing in the 3D view

| Item | Value |
|---|---|
| Instance handle | low 16 bits of `frame`, signed 16-bit; client/menu systems look up the negated id |
| Client effect entity flags | 0x400000 (Ghoul model) plus bit 28 (ambient-only) |
| Character render effects at start | 0x401000 (Ghoul model + casts shadow) |
| GL linear attenuation, world/dynamic light | 1/R |
| Point-light inner radius | 0.75·R (stored with R² and 4/R²) |

### N. G3 damage model, gore, bodies

| Item | Value |
|---|---|
| Killing-blow cloud bolt | gore bolt, else body-side cap bolt |
| Blood-pool tracked bolt | body-side cap bolt, else gore bolt, else character origin |
| Player hit sounds | only players without a body model (single player); health tested before the hit |
| Zone match | record part = zone owner part only |
| Knife zone weight (DM) | squared distance × 1.3 while the current choice is zone 0, 1 or 2 |
| Blood point | damage point clamped into the target's absolute box |
| Player blood splat | send probability 1/6, not for means of death 27; size argument unused |
| Per-hit debris puff | TE 18 at the damage point, direction (0, 0, 0), count irand(0, 5), size class 4, chunk type 32, material 35 |
| TE 18 rate limit | same material within 0.1 s (game-wide) is dropped |
| Severed piece physics | ground friction 0.5, gravity 1.0, air friction 0.1, buoyancy 0.7, elasticity 0.5 |
| Severed piece angles | (0, flrand(−30, 30), 0); instance paused at spawn time |
| Severed piece box | owner-part mesh extent at time + 0.3 s, centred |
| Fallback piece | origin + 24 up, box (−8, −8, −8)…(8, 8, 0), velocity (75·dx, 75·dy, irand(30, 70)) |
| Calm-weapon test | profile flag 1, or armour percentage > 0.5 and health > 0.5 × max health |
| Shoulder death segments | 1, 4 → right; 2, 3 → left |
| Player armour method 31 | (int damage, float piercing, float fraction) → int |

## Test vectors / how to verify

Each item ends with its own "Verify" line; those are the test vectors of this amendment. Additional vectors:

1. **GS-V1 (GS.9), validation of an 8-character challenge**, key `iVn3a3`: `oyMl8D24` → scrambled bytes
   6b 97 5f fa b0 b7 36 2f → response `a5df+rC3Ni8A` (12 characters). This is the exchange observed with OpenSpy and is
   reproduced by the algorithm of SPEC-204 "Validation response".
2. **GS-V2 (GS.15), pure-server checksums**: the SPEC-301 §8 file checksum of the shipped 1.07f `gamex86.dll` is
   64C0FC54 (hex) and of `player.dll` EACC2EEA (hex); with both loaded and all five locks on, `sv_violence` is 63 after
   the first map.
3. **G18-V1 (G18.18–G18.23), probe layouts**: the G18 probe method (one 32-bit word set per frame, read back from the
   wire) reproduces every offset of the "Entity state" and "Player state" tables except the fields marked "not sent";
   those are checked by the rules of G18.19 (engine overwrite of 0x70, low-byte-only fields).
4. **G18-V2 (G18.21b)**: the eleven movement vectors of item G18.21b, run through import #81 with synthetic callbacks.
5. **I1a-V1 (I1a.1)**: `set t 1` twice calls a change callback once; `set t 1.0` calls it again.

## Open questions

### A. SPEC-204 GameSpy

1. Behaviour when all 104 query ports are taken (GS.14).
2. Whether the oracle's `sv_violence` 63 comes from stored or forced locks (GS.15; both give 63).

### B. G18 game module

1. What the game's "may jump" callback (block +0xF8) tests and what sets the stagger float (+0xFC): game-module
   side, see the G18 items.
2. Type 6 keeps the player hovering when it is set in mid-air (no gravity); verify in game with a remote camera
   view entered while falling.

3. G18.24: the G18 harness saw 0 after a respawn; the rule predicts 1 until the next fresh press (test given).
4. G18.25: why the maxclients 4/16 captures ran without the bot system (check their `sv_globalrankings`).
5. G18.26: the run-backwards knife entry, the roll-into-crouch conditions and the unarmed case are derived from the
   choice lists, not yet seen in game.
6. G18.28: whether the missing `enemy/bolt/<weapon>` lists leave model records (Ghoul file list of a map without
   weapons).

### D. I3 Ghoul model system

1. Meanings of the extra light tracks of kinds 1–4 (no stock data).
2. Whether any stock sequence has a 0 ms frame time (I3.4).
3. Whether the game or player module calls method 4 with on = 1 on an instance that is already on during play (I3.3
   would then drop notes).
4. Whether any stock module passes a speed index outside 0…12 (I3.13).

### E. I5 client effects and temp entities

1. Visible result of a decal whose fitted size is negative (I5.13) and of a released paper-sheet model (I5.14): both
   need an in-game look.
2. Related finding for SPEC-401 open question "which ends of the streak lines move" (not an I5 item): neither end
   accelerates. Type 32 streaks: start velocity v, end velocity 1.5·v. Types 38 and 44 streaks: start velocity 20q,
   end velocity 40q. So the streaks stretch outward; "acceleration 1.5v / 40q" in SPEC-401 should read "end velocity".

### F. I4a renderer

1. Why the original's pointer is missing from the unfocused captures (I6m.4); the menu rules do not hide it.
2. In-game confirmation of the `wait` side effect of `<config>` (I6m.5) and of the `dm_game_assassin` cascade (I6m.8).

### H. G6b script wiring

1. None for these items. Re-check of the G6b test script needed for G6b.7 (key value kinds, spawnflags).

### I. I6h client view, HUD, input, demos

1. None for these items. The 8-of-16 pattern (I6h.5) makes the frame count of a join depend on earlier joins in the
   same process; a byte-exact clone of the loading screen needs that counter.

### J. G9 game modes and Ghoul glue

1. Gameplay of a CTF flag with team number 0 (G9.10) and the icons of a player who becomes a spectator mid-game
   (G9.3): both need an in-game check, neither occurs with stock data.
2. Client rendering of `client_conq_sb` with the two undefined trailing values (G9.8) without `zmain.pak`.

### K. G2 character AI

1. The bolt each value of the target-bolt selector picks (SPEC-722 lists head, shoulders, chest, groin, gut; the value
   order was not read).
2. In-game confirmation of the jitter step (G2.4) and of the special death start offset (G2.2).

### L. G5 navigation and bots

1. G5.3: in-game confirmation of the fire cadence of bots (sequences started at level time 1.0).
2. G5.7: in-game confirmation that a bot with an unroutable destination hovers at its waypoint.
3. G5.1: whether #14 to a bot in a slot that a human used earlier in the level (stream already set up) only queues
   unsent data, as derived.

### M. I4b Ghoul model drawing in the 3D view

1. In-game confirmation of the probe-vector frame for rotated models (I4b.4).
2. The exact item condition for 0x1000 on CTF flags (I4b.3).

### N. G3 damage model, gore, bodies

1. G3.4 (exit-only hits) and G3.11 (piece angles, frozen pose) need a black-box run to confirm visually; both follow
   from the binary.
2. Meaning of entity flag 0x40000 in the calm-weapon test (G3.12): not set by any rule known to this amendment.
