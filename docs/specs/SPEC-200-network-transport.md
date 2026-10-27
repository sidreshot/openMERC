# SPEC-200: Network transport, netchan, out-of-band messages and GameSpy traffic
Status: approved (G, 2026-10-21) — produced by the reverse team, reviewed against CLEANROOM.md rules; byte-level test vectors still to be confirmed by packet capture
Describes: SoF.exe (client and server networking), version 1.07f
Depends on: SPEC-201 (primitive encodings), SPEC-203 (what happens after the handshake)

## Purpose
Defines how Soldier of Fortune 1.07f moves bytes between client and server: UDP ports, packet size limits, the
sequenced "netchan" packet header with its reliable stream, every connectionless (out-of-band) packet, and the
GameSpy query / heartbeat traffic used for server lists. The design is that of Quake II 3.x; every difference
from Quake II is called out. Everything here was confirmed from both the sending and the receiving code unless
marked UNVERIFIED.

## Interface / layout

### UDP ports
| Purpose | Default | Selection |
|---|---|---|
| Server game port | 28910 | cvar `ip_hostport` if non-zero, else `hostport` if non-zero, else `port` (default "28910"); all read-only after start |
| Client game port | 28901 | cvar `ip_clientport` if non-zero, else `clientport` (default "28901"); 0 or a failed bind → any free port |
| Bind address | any | cvar `ip`; "localhost" (any case) or empty = all interfaces |
| GameSpy query port (server status for browsers) | 28911 | cvar `gamespyport`; if the port is busy the next of up to 100 ports is tried; opened by every running instance |
| GameSpy master, heartbeats (UDP) | master.gamespy.com:27900 | fixed |
| GameSpy master, server list (TCP, client browser) | master.gamespy.com:28900 | fixed |

A dedicated server opens no client socket. If a dedicated server cannot bind its game port it aborts with
"Couldn't allocate dedicated server IP port". Sockets are non-blocking with broadcast enabled. Commands that
address a server (`connect`, `ping`, `serverstatus`, `rcon`, `pingservers`) default to port 28910 when the
address has none. An optional SOCKS5 proxy (cvars `net_socksEnabled`, `net_socksServer`, `net_socksPort` 1080,
`net_socksUsername`, `net_socksPassword`) wraps outgoing datagrams with the standard 10-byte SOCKS5 UDP header;
it does not change the game protocol. IPX sockets exist (`ipx_hostport`, `ipx_clientport`, `noipx`) but are
outside the scope of this spec.

### Packet kinds
| First 4 bytes | Kind |
|---|---|
| FF FF FF FF | connectionless (out-of-band) packet: the rest is ASCII text |
| anything else | sequenced netchan packet |

A sequenced packet shorter than 8 bytes is ignored by the client ("<address>: Runt packet"). The client ignores
sequenced packets not coming from its server ("<address>:sequenced packet without connection"). The server
matches a sequenced packet to a client by IP address plus the qport field (UDP port ignored); if the UDP port
changed, it adopts the new port and prints "SV_ReadPackets: fixing up a translated port".

### Netchan packet header
| Offset | Size | Field |
|---|---|---|
| 0 | 4 | bits 0..30: sequence number of this packet; bit 31: set when this packet carries the reliable message |
| 4 | 4 | bits 0..30: last sequence number received from the peer; bit 31: reliable-sequence bit of the last reliable message received from the peer (toggles 0/1 per reliable message) |
| 8 | 2 | qport — client→server packets only; unsigned 16-bit |
| 8 or 10 | n | reliable message bytes (only when bit 31 of the first word is set), immediately followed by the unreliable bytes |

There is no length field separating the reliable and the unreliable part: the receiver processes the whole
remaining payload as one message stream (SPEC-201). There is no fragmentation and no compression at this layer.

### Packet size limit
The engine keeps one global maximum datagram size:
| Mode | Maximum datagram | Reliable / pending message buffers |
|---|---|---|
| Network game (any "connect", "ping", "serverstatus", "rcon", "pingservers" command, or a server with maxclients > 1) | 1400 bytes | 1384 bytes (1400 − 16) |
| Single-player / loopback (server started with maxclients = 1) | 16384 bytes | 16368 bytes |

The receive buffer has the same size; a datagram that fills it completely is dropped ("Oversize packet from
<address>"). Quake II always uses 1400. A reimplementation that interoperates over the network must never send
more than 1400 bytes per datagram.

## Behaviour

### Netchan
State per side: outgoing sequence (starts at 1), incoming sequence (0), incoming acknowledged (0), own reliable
bit (0), peer's reliable bit (0), "sequence that last carried the reliable message", the pending reliable
message, and a growing "next reliable" buffer.

Transmit (once per frame, or whenever the owner flushes):
1. If the "next reliable" buffer overflowed, the channel is marked broken and nothing is sent
   ("<address>:Outgoing message overflow"). The owner then drops the connection.
2. The pending reliable message is (re)sent when the peer has acknowledged a packet newer than the one that last
   carried it, and the peer's echoed reliable bit differs from our own reliable bit.
3. If nothing is pending and the "next reliable" buffer is not empty, its contents become the pending reliable
   message, the buffer is emptied, our reliable bit toggles, and it is sent in this packet.
4. Header word 1 = outgoing sequence | (reliable sent ? 0x80000000 : 0); word 2 = incoming sequence | (peer's
   reliable bit << 31). The outgoing sequence is then incremented.
5. Client only: the 16-bit qport.
6. The pending reliable bytes (if sent), then the unreliable bytes if they still fit in the maximum datagram;
   otherwise the unreliable part is dropped for this packet ("Netchan_Transmit: dumped unreliable").

Receive:
1. Read both words; the server also reads the qport.
2. A packet whose sequence number is not greater than the incoming sequence is a duplicate or arrived out of
   order and is discarded entirely ("<address>:Out of order packet <n> at <m>" with `showdrop 1`).
3. Packets missed = sequence − (incoming sequence + 1) ("<address>:Dropped <n> packets at <m>").
4. If the echoed reliable bit equals our own reliable bit, our pending reliable message was delivered and is
   cleared.
5. The incoming sequence, acknowledged sequence and echoed bit are stored; if bit 31 of word 1 is set, the peer's
   reliable bit toggles. Then the payload is parsed as messages (SPEC-201).

qport: a client value, cvar `qport` (read-only), default = milliseconds since start-up & 0xFFFF, written in
every client→server header and also sent in the `connect` text. Purpose as in Quake II: lets the server keep a
client whose NAT changes its UDP port.

Timeouts: the client gives up after `cl_timeout` seconds (default 120) without packets ("Server connection timed
out."); the server uses `timeout` (default 180) and `zombietime` (default 2) as in Quake II (UNVERIFIED).

Debug and test cvars: `showpackets` (one line per packet), `showdrop`, and two SoF additions `net_sendrate` and
`net_receiverate` (default 1): when strictly between 0 and 1, each outgoing / incoming datagram is kept only with
that probability (packet-loss simulation).

### Out-of-band packets: framing
An out-of-band packet is FF FF FF FF followed by text. Replies written by the engine carry no terminating NUL
(exception: the client's `rcon` packet includes the NUL). The receiver takes the first line (up to newline, NUL
or end of packet) and splits it into whitespace-separated tokens with Quake-style quoting; token 0 selects the
command (exact, case-sensitive match). Some commands then read further text after the first line (as a string,
up to NUL or end). Every received out-of-band command is printed by the client as "<address>: <command>".

### "LAN address" test (SoF-specific)
Several decisions below depend on whether the sender's address counts as "LAN". An address is LAN when it is
loopback or IPX, or an IPv4 address that, compared with any of up to four local interface addresses, is in the
same classful network (first octet below 128: same first octet; 128..191: same first two octets; 192 and above:
same first three octets), or when both addresses are inside 172.16.0.0/12, or both inside 192.168.0.0/16.
Quake II uses "loopback only" at these places.

### Out-of-band commands accepted by the server
| Command (token 0) | Arguments | Reply / effect |
|---|---|---|
| `ping` | optional token 1 | `ack <token1>` (with no argument: `ack ` followed by nothing). Quake II replies `ack`. |
| `ack` | — | no reply; server prints "Ping acknowledge from <address>" |
| `status` | — | one or more packets `print\n<status text>`; the status text is split into 1384-byte pieces |
| `info` | protocol | see "info reply"; no reply unless token 1 is "33", maxclients > 1 and deathmatch ≠ 0 |
| `getchallenge` | token 1 (ignored) | `challenge <n>` |
| `connect` | see "connect" | see "connect" |
| `rcon` | password, command words | see "rcon" |
| anything else | | no reply; server prints "Bad connectionless packet from <address>:" and the text |

Status text = the serverinfo string (all serverinfo cvars as `\key\value` pairs), then a backslash and a newline
(bytes 5C 0A; Quake II has only the newline), then one line per connected or spawned client:
`<frags> <ping> "<name>"` followed by a newline. Lines are added only while the total stays below the maximum
datagram size.

info reply: `info\n` followed by
`SOF+<hostname right-aligned to 16>+<map name right-aligned to 8>+<players>/<spectators>/<maxclients>+<game dir>+<game type>+<sv_violence>\n`
(printf-style "%16s" and "%8s": padded on the left with spaces, never truncated). players and spectators count
clients that are at least connected, split by a per-player game flag (UNVERIFIED that the second number is
spectators). game dir is the `game` cvar (empty for the base game). game type is the game module's mode name —
observed values "single player", "Assassin", "Arsenal", "CTF", "Control", "Realistic", "Team Real", "DM",
"Team DM", "Unknown" — or the deathmatch value as a decimal number when no game module is loaded. Example:
`info\nSOF+          noname+    dm/x+1/0/16++DM+0\n`.

challenge: the server keeps 1024 challenge slots keyed by IP address (port ignored). A request from a known
address returns that address's existing challenge; an unknown address replaces the oldest slot with a new
random value made of two 15-bit random numbers (bits 0..14 and 16..30; bits 15 and 31 are zero).

### connect
Client → server: `connect 33 <qport> <challenge> <nonce> <cd value> "<userinfo>"\n` where 33 is the protocol
version, qport the client's qport (decimal), challenge the number from the `challenge` reply, nonce the
decimal value the client also sent in its `getchallenge` (seconds since 1970 when the connection attempt
started; the server ignores it), cd value always the text `3.14`, and userinfo the client's userinfo string
(SPEC-203) in double quotes (at most 511 characters are kept by the server).
Example: `connect 33 40213 1394829174 1729555200 3.14 "\name\Mullins\skin\mullins\..."`.

Server checks, in this order, with the exact replies (each a separate out-of-band packet, no NUL):
1. Protocol token ≠ 33 → `print\nServer is version 1.07f.\n`.
2. If the client is not a LAN address: no challenge stored for its IP → `rejected_error -1\n` then
   `print\nNo challenge for address.\n`; challenge differs → `rejected_error -1\n` then `print\nBad challenge.\n`.
   LAN clients skip the challenge check (Quake II: loopback only).
3. Violence: let c = integer value of userinfo key `cl_violence` and s = integer value of the server's
   `sv_violence`; if (s AND c) ≠ c → `rejected_violence\n` then `print\nServer violence settings are incompatible.\n`.
4. The server sets userinfo key `ip` to the client's address ("a.b.c.d:port").
5. While playing a demo/attract loop, a non-LAN client gets `print\nConnection refused.\n`.
6. Reconnect: if a slot already belongs to the same IP with the same qport (or same port), and less than
   `sv_reconnect_limit` seconds (default 3) have passed since that slot last connected, the request is silently
   ignored (no reply; unlike Quake II there is no loopback exemption); otherwise that slot is reused.
7. Otherwise the first free slot is used; none free → `rejected_full\n` then `print\nServer is full.\n`.
8. The game module decides (it may check `password`, `spectator`, `spectator_password`, bans). On refusal: if
   the game put a key `rejmsg` into the userinfo, the server sends the rejmsg value verbatim as one packet, then
   `print\n<rejmsg>\nConnection refused.\n`; without rejmsg it sends `print\nConnection refused.\n`. Values of
   rejmsg used by the 1.07f game module: `rejected_banned`, `rejected_spectator_password`,
   `rejected_spectator_limit_exeeded` (sic), `rejected_password`.
9. Success: `client_connect` (no newline). The server creates the client's netchan and the connection continues
   as in SPEC-203.

The cd value: the server stores (cd value / 1.35 × 4) for the slot and, when the client sends `new`, refuses a
client connected over loopback (the local single-player/listen client) whose stored value × 0.4 is below 1.98
(i.e. cd value below about 1.6706) with string-package message 4/17 ("Please insert the SOF CD and try
again."). Remote clients are not checked. Clients must send `3.14`.

### Out-of-band commands accepted by the client
| Command | Effect |
|---|---|
| `challenge <n>` | sends the `connect` packet above |
| `client_connect` | if already connected: ignored ("Dup connect received.  Ignored."); else creates the netchan, queues the reliable stringcmd `new` (SPEC-203) |
| `print` | reads the text after the first line and prints it (also shown in the server browser) |
| `info` | reads the text after the first line; accepted only when it starts with `SOF+` and the sender is a LAN address (otherwise "Local address considered Wide: <address>"); added to the LAN server list |
| `ping` | replies `ack` |
| `ack <ms>` | ping time = now − ms; "Ping time is <n>" |
| `echo <text>` | replies `<text>` |
| `cmd` | only from a LAN address (else "Command packet from remote host.  Ignored."): executes the text after the first line as console commands |
| `rejected_violence` | disconnects and shows rejection menu 1 |
| `rejected_full` | menu 2 |
| `rejected_error <n>` | menu 3 (`menu_won_error` set to "Rejected") |
| `rejected_banned` | menu 4 |
| `rejected_spectator_password` | menu 5 |
| `rejected_spectator_limit_exeeded` | menu 6 |
| `rejected_password` | menu 7 |
| `rejected_auth <n>` | when n equals the client's current nonce: waits 3 s and restarts with a new `getchallenge` (never sent by 1.07f servers) |
| anything else | "Unknown command." |

### Client handshake timing
After `connect <server>` the client sends `getchallenge <nonce>\n` immediately and repeats it every 3 seconds
(printing "Connecting to <server>...") until a `challenge` arrives. If a server is running in the same process,
the client skips `getchallenge` and sends `connect` directly to it.

### rcon
Client packet: FF FF FF FF, then `rcon <rcon_password> <arg1> <arg2> … ` (each argument followed by one space),
then a NUL. Sent to the connected server, or to the address in cvar `rcon_address` when not connected. The
server accepts it when its `rcon_password` is non-empty and equals token 1 exactly. Rejected:
`print\nBad rcon_password.\n`. Accepted: the command is the tokens from 2 on joined with spaces (each followed by a
space), with every apostrophe (') replaced by a double quote ("); its console output is returned in
`print\n<text>` packets of at most 1384 bytes of text each.

### Client query commands
- `ping <server>`: sends `ping <milliseconds>\n`; the reply `ack <milliseconds>` gives the round-trip time.
- `serverstatus <server>`: sends three packets: `info 33`, `status\n`, `ping <milliseconds>\n`.
- `pingservers`: unless `cl_nolangames` is set, broadcasts `info 33` to 255.255.255.255:28910; sends `info 33`
  to each favourite address in cvars `adr0`..`adr19`; then asks GameSpy for the internet list.

### GameSpy query protocol (server side)
The server answers GameSpy "Query & Reporting" requests on its query port. A request is a datagram of at most
255 bytes containing one or more of the keywords `\basic\`, `\info\`, `\rules\`, `\players\`, `\status\`,
`\packets\`, `\echo\<text>`, `\secure\<challenge>`. A counter (query id) starts at 1 and is incremented for every
request datagram received; the packet counter restarts at 0 per request.

Replies are sequences of `\key\value` pairs:
| Section | Keys in order |
|---|---|
| basic | `\gamename\sofretail\gamever\1.07f\location\1` |
| info | `\hostname\<hostname>` `\hostport\<game port>` `\mapname\<mapname>` `\gametype\<mode name or deathmatch number>` `\numplayers\<spawned clients>` `\maxplayers\<maxclients>` `\gamemode\openplaying` `\violence\<sv_violence>` |
| rules | `\timelimit\<n>` `\fraglimit\<n>` `\dmflags\<n>` `\movescale\<g_movescale×100>%` `\cheats\enabled` or `\cheats\disabled` `\ctf_loops\<n>` `\suicide_penalty\<n>` |
| players | for each spawned client slot i (slot number, gaps possible): `\player_<i>\<name>\frags_<i>\<frags>\ping_<i>\<ping>\skin_<i>\<userinfo skin>\team_<i>\<userinfo teamname>` |

Handling of one request: `\basic\`, `\info\`, `\rules\`, `\players\` each produce their section; `\status\`
produces basic+info+rules+players in one stream; `\packets\` produces the four sections each flushed into its own
datagram; `\echo\<text>` (text shorter than 1351 characters) adds `\echo\<text>`; `\secure\<challenge>` adds
`\validate\<response>` where response is the standard GameSpy validation (the public "gsseckey" algorithm:
RC4-style keyed scramble of the challenge with the secret key `iVn3a3`, then GameSpy base64 without padding);
finally `\final\` is added. Every datagram sent ends with `\queryid\<query id>.<packet number>` where packet
number counts 1, 2, … within this request. Text is accumulated up to 1349 bytes per datagram; a section that does
not fit starts a new datagram, and a section longer than 1350 bytes is split at a key boundary. With no server
running, the sections are empty. Example reply to `\basic\` as the first request after start-up:
`\gamename\sofretail\gamever\1.07f\location\1\final\\queryid\2.1`.

### GameSpy heartbeats
Only when cvar `public` is non-zero and maxclients > 1. Sent from the query socket to master.gamespy.com:27900
when the number of spawned clients changes, at least every 150 seconds, immediately after a map start or the
console command `heartbeat`, and once more at engine shutdown (with game mode "exiting"):
`\heartbeat\<query port>\gamename\sofretail\statechanged\` (no NUL). The master then queries the server's query
port as above (using `\secure\`).

### GameSpy server list (client side, informative)
The client browser connects over TCP to master.gamespy.com:28900, answers the master's `\secure\<challenge>`
with `\gamename\sofretail\gamever\1.6\location\0\validate\<response>\final\\queryid\1.1\`, requests
`\list\<type>\gamename\sofretail\final\`, then queries each listed server's query port over UDP. The list type and
per-server query string are UNVERIFIED (expected `cmp` and `\status\`). Chat (`gsc_*` commands) uses
peerchat.gamespy.com:6667 and is out of scope.

## Constants
| Name | Value |
|---|---|
| Out-of-band marker | FF FF FF FF |
| Protocol version | 33 |
| Default server port / client port | 28910 / 28901 |
| GameSpy query port (default) / master heartbeat port / master list port | 28911 / 27900 / 28900 |
| GameSpy game name / secret key | sofretail / iVn3a3 |
| Heartbeat interval | 150 s |
| Max datagram (network game) | 1400 |
| Max datagram (single-player/loopback) | 16384 |
| Sequence mask / reliable flag | 0x7FFFFFFF / 0x80000000 |
| Challenge slots | 1024 |
| Reconnect limit default | 3 s |

## Test vectors / how to verify
1. First client packet after `client_connect`, carrying only the reliable stringcmd `new`, qport 0x1234:
   `01 00 00 80 00 00 00 00 34 12 04 6E 65 77 00` (sequence 1 + reliable flag; ack 0, bit 0; qport; clc_stringcmd;
   "new"). UNVERIFIED that no other message is bundled — check with a capture.
2. `ping` request `FF FF FF FF 70 69 6E 67 20 35 0A` ("ping 5\n") → reply `FF FF FF FF 61 63 6B 20 35` ("ack 5").
3. `info 33` to a deathmatch server named "noname" on map "dm/x" with one player and maxclients 16 → reply text
   `info\nSOF+          noname+    dm/x+1/0/16++DM+0\n` (exact game-type text depends on the mode).
4. `connect 32 ...` → `print\nServer is version 1.07f.\n`.
5. GameSpy: send `\basic\` to UDP 28911 of a fresh server → `\gamename\sofretail\gamever\1.07f\location\1\final\\queryid\2.1`.
6. Packet-loss simulation: `net_sendrate 0.5` on one side, `showdrop 1` on the other.

## Open questions
- Meaning of the middle number of the `info` player counts (spectators expected) — join as spectator and query.
- Which score field the status/players "frags" value comes from (game-defined).
- Address the GameSpy query socket binds to (expected all interfaces) — check with netstat.
- List type and per-server query string used by the client browser — capture while refreshing the list.
- Validation test vector for `\secure\` — capture a master query, or compute with the public algorithm and
  compare with an original server's reply.
- Timeout behaviour of the server (`timeout`, `zombietime`) is assumed identical to Quake II.
