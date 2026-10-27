# SPEC-204: GameSpy — master server list, heartbeats, query responder, server browser, lobby chat
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe, version 1.07f. All GameSpy code is in the engine; gamex86.dll is not involved.
Depends on: SPEC-200 (UDP ports, transport), SPEC-201 (out-of-band `info`), SPEC-001 (game export 0x50, game-type
name), SPEC-302 (cvars and commands), SPEC-803 (server-browser list record, lobby widgets), SPEC-002 (colour codes
used in chat text)
Supersedes: the GameSpy subsections of SPEC-200. This spec agrees with them and adds the missing detail; see
"Amendments" at the end.

## Purpose

SoF 1.07f carries three GameSpy clients and one GameSpy server component:
1. the Internet server list ("master list") used by the multiplayer browser, including the challenge/response
   "validate" algorithm;
2. the UDP heartbeat that a public server sends to the master;
3. the UDP query responder that every running SoF process provides;
4. an IRC-style lobby chat.

GameSpy's own hosts are gone, but community masters speak the same protocol. This spec gives everything a
reimplementation needs to:
- list servers from a GameSpy-compatible master;
- report to one;
- answer masters and browsers exactly like an original server;
- talk to an IRC lobby the way the original does.

All of it runs inside the engine's main loop, once per engine frame, without threads. Some steps block the frame;
they are marked below.

## Interface / layout

### Fixed hosts and ports (no cvar in the original)
| Use | Host | Port | Transport | When resolved |
|---|---|---|---|---|
| Heartbeats from a server | `master.gamespy.com` | 27900 | UDP, sent from the query socket | once, when the query socket is opened at engine start |
| Server list download | `master.gamespy.com` | 28900 | TCP | at every list refresh |
| Query responder | (local) | cvar `gamespyport` (default 28911), see section 3 | UDP | — |
| Lobby chat | cvar `gsc_server` (default `peerchat.gamespy.com`) | cvar `gsc_port` (default `6667`) | TCP | at every `gsc_connect` |

The executable contains no other GameSpy host name: no message-of-the-day, update or statistics host, and no
Quake II `setmaster` command. The master host is a literal in the executable in both places. A dotted IPv4 address
would also be accepted there.

### Identity strings
| Field | Value |
|---|---|
| Game name (all clients) | `sofretail` |
| Secret key (validation) | `iVn3a3` (6 bytes: 69 56 6E 33 61 33) |
| Game version sent by the server in `\basic\` | `1.07f` |
| Version sent by the browser to the master | `1.6` (an SDK constant, not the game version) |
| Location sent by the browser / by the server | `0` / `1` |
| Game mode sent by the server | `openplaying`; `exiting` once engine shutdown has begun |
| Chat "real name" | `SoldierofFortune` |

### Cvars and commands used here (all already listed in SPEC-302)
| Name | Default | Flags | Meaning |
|---|---|---|---|
| `gamespyport` | 28911 | read-only after start (flag 8) | first UDP port tried for the query socket |
| `public` | 0 | — | heartbeats are sent only when ≠ 0 |
| `noudp` | 0 | — | when ≠ 0 the master refresh is skipped |
| `gsc_server` | peerchat.gamespy.com | archive | chat server host |
| `gsc_port` | 6667 | archive | chat server TCP port |
| `gsc_nick` | JohnMullins | archive | chat nickname |
| `gsc_user` | JohnMullins | archive | chat user name (IRC USER field) |
| `menu_servername` | — | — | browser status text (SPEC-803) |
| `menu_topic` | — | — | `#<channel>: <topic>`, at most 63 characters |

Commands used here:
- `heartbeat`: forces a heartbeat at the next server frame.
- `pingservers`: refreshes the browser.
- `gsc_connect`, `gsc_disconnect`, `gsc_getchannels`, `gsc_join`, `gsc_part`, `gsc_talk`, `gsc_private`,
  `gsc_getusers`, `gsc_settopic`, `gsc_gettopic`, `gsc_kick`, `gsc_ban`, `gsc_setusermode` (section 5).

### Key/value text messages
Every GameSpy text message is a list of fields separated by backslashes. It starts with a backslash, and keys
and values alternate: `\key1\value1\key2\value2…`. No NUL byte is sent. Values cannot contain a backslash. A key
with an empty value appears as `\key\\`. The words `final` and `queryid` are protocol markers, not data. Matching
rules differ per side:
- The query responder matches request words case-sensitively (section 3).
- The browser stores reply keys case-insensitively (section 1.3).

### Validation response ("secure key")
Input: a challenge string C (bytes c₀…cₙ₋₁) and the secret key K = `iVn3a3` (k₀…k₅). Output: 4·⌈n/3⌉ printable
characters.

1. Key schedule (identical to RC4): S starts as the permutation 0, 1, …, 255 and j as 0. For i = 0…255 in turn,
   j becomes (j + S[i] + k[i mod 6]) mod 256, and then S[i] and S[j] are exchanged.
2. Scramble (differs from RC4): a and b start at 0. For each challenge byte cₘ in order:
   - a becomes (a + cₘ + 1) mod 256;
   - b becomes (b + S[a]) mod 256;
   - S[a] and S[b] are exchanged;
   - the output byte is dₘ = cₘ XOR S[(S[a] + S[b]) mod 256].

   Challenge bytes are unsigned.
3. Encoding: the bytes d are taken in groups of three; a missing second or third byte counts as 0. Each group
   (x, y, z) yields four characters with the 6-bit values x>>2, ((x&3)<<4)|(y>>4), ((y&15)<<2)|(z>>6), z&63. These
   are mapped through the alphabet `A`–`Z` (0–25), `a`–`z` (26–51), `0`–`9` (52–61), `+` (62), `/` (63).
   There is no `=` padding, so a 6-byte challenge gives 8 characters and a 4-byte challenge also gives 8.

Because step 2 is a stream scramble, the response to a prefix of a challenge is a prefix of the full response,
up to the last complete group.

The browser always validates exactly the 6 bytes that follow `\secure\` in the master's greeting. The server
validates the whole `\secure\` value as received, at most 128 characters (section 3).

## Behaviour

### 1. Internet server list (browser side)
`pingservers` starts it, after the LAN broadcast and the favourites (SPEC-200/SPEC-201), unless cvar `noudp` is
non-zero. The developer console prints `.... finding GameSpy servers ....`. If a refresh is already in progress,
`pingservers` does nothing at all, not even the LAN part, and prints `GameSpy is busy .....`. Otherwise the menu
list and the GameSpy list of the previous refresh are discarded and the following runs.

**1.1 Master handshake (blocks the engine frame).**
1. Resolve `master.gamespy.com` and open a TCP connection to port 28900. The connect and the first receive are
   blocking, without an application timeout: while the master is unreachable the game freezes for the operating
   system's connect timeout. A failed resolve, socket, connect or receive ends the refresh silently. No message is
   printed and the browser status text does not change.
2. Receive once, at most 255 bytes. The text must contain `\secure\`; otherwise the refresh ends. The challenge is
   the 6 bytes following `\secure\`, whatever they are. GameSpy-compatible masters greet with
   `\basic\\secure\<6 characters>`.
3. Send, as one write and without a terminating NUL:
   `\gamename\sofretail\gamever\1.6\location\0\validate\<8 characters>\final\\queryid\1.1\`
4. Send, as a second write: `\list\cmp\gamename\sofretail\final\`. The browser does not wait for any reply to
   step 3.
5. The browser status text becomes `Transferring Server List` (developer print `GameSpy list transferring`), and the
   engine continues. From here on nothing blocks.

**1.2 List transfer (polled every frame, non-blocking).**
- The reply is binary: a sequence of 6-byte records, terminated by the 7 ASCII characters `\final\`. Each record is
  an IPv4 address (4 bytes, network order, first byte = first number of the dotted form) followed by the server's
  **query** port (2 bytes, big-endian).
- Received bytes are appended to a 2047-byte buffer and parsed whenever at least 6 bytes are buffered. At each
  record boundary, if the next 7 bytes are `\final\` the list is complete; otherwise 6 bytes are taken as a record.
  Robustness note for masters: send `\final\` together with or after the last record in the same write. A TCP
  segment boundary inside `\final\` makes the original misread its first 6 bytes as a server record.
- Complete (`\final\` seen): the connection is closed and the query phase starts.
- If the master closes the connection, or a receive fails, **before** `\final\`, the refresh is aborted. The
  connection is closed, **no** server of this transfer is queried and nothing from this refresh is shown. The
  status text goes through `Querying GameSpy Master` to `Query Complete` immediately.
- There is no timeout in this phase. A master that keeps the connection open without sending `\final\` leaves the
  browser in the "transferring" state, and every later `pingservers` prints `GameSpy is busy .....`.
- Duplicate records are not removed. Each is queried, and the menu keeps one record per address (SPEC-803).
- The number of servers is limited only by memory.
- The browser only ever requests `cmp`, so a master need not implement the `info2` or `groups` list types.

**1.3 Server queries (polled every frame).**
- The status text is `Querying GameSpy Master` while this phase runs (developer print `GameSpy master query`).
- Up to **10** servers are queried at a time, each with its own UDP socket. Servers are taken in list order.
- Query: the 8 bytes `\status\` (no NUL), sent to the record's address and query port. The send time is noted
  (millisecond tick counter).
- Each frame, every active socket is checked once with a zero-timeout poll, and at most one datagram (≤ 1499 bytes)
  is read per socket. A datagram is accepted only if its source address and port equal the queried ones. A receive
  error frees the slot, which drops the server.
- The first accepted datagram sets the server's ping to current time − send time in milliseconds. The value is
  16-bit and is quantised by the client frame time and the tick resolution. It is 9999 until a reply arrives.
- Every accepted datagram is parsed as `\key\value` pairs:
  - keys named exactly `queryid` and `final` are not stored;
  - a later value for the same key replaces the earlier one;
  - keys compare case-insensitively.

  All key and value strings of one refresh are kept in one shared table that ignores case. The first spelling
  seen of any string, whether key or value, is the one stored. For example, if some server's key or value was
  `dm`, another server's value `DM` is later read back as `dm` (UNVERIFIED in game).
- When an accepted datagram contains `\final\` anywhere, the server is complete. It is handed to the browser
  (1.4) and its slot is freed.
- A slot whose server has not completed 3000 ms after its query was sent is freed. That server is dropped and not
  shown, even if some of its datagrams arrived. There is no retry.
- Freed slots are refilled with the next servers. When every server has been queried and no slot is busy, all query
  sockets are closed and the status text becomes `Query Complete` (developer print `GameSpy query complete`).

**1.4 Hand-off to the browser record (SPEC-803 "Server list record").**
For each completed server the browser receives:
- address: the server's IP address with port = the value of key `hostport` (default 28910 when absent). This is the
  game port, not the query port.
- the record text `<hostname>+<mapname>+<numplayers>/<numbots>/<maxplayers>++<gametype>+<violence>+<ping>+`, with
  these defaults when a key is absent: hostname `localhost`, mapname empty, the three numbers `0`, gametype `DM`,
  violence `0`. The ping is the measured ping of 1.3, in decimal. An SoF 1.07f server never sends `numbots`, so the
  middle number is always 0 for original servers.
- the developer console (level 2) prints `... add <record text>`.

All other keys, including rules and players, are ignored by the browser. The only filtering is the menu's violence
lock: a server whose `violence` value does not contain all bits of `cl_violence` is counted as locked and not
listed (SPEC-803). There is no filtering by version, game type, emptiness or ping. Sorting is done by the menu.

### 2. Heartbeat (server → master, UDP 27900)
The message is `\heartbeat\<Q>\gamename\sofretail\statechanged\` (no NUL), where Q is the decimal local port of the
query socket, normally 28911. It is sent **from the query socket**, so the master sees the query port as the
source port.

When it is sent:
- Each server frame checks the conditions. Nothing is sent unless cvar `public` ≠ 0 and `maxclients` > 1.
- Let n be the number of client slots that are fully in the game (spawned). A heartbeat is sent when either:
  - n differs from n at the last heartbeat. It starts as "unknown", so the first check after a server starts
    sends one.
  - at least 150 s (150000 ms of the engine's real time) have passed since the last heartbeat.
- Console command `heartbeat` makes the next check send one. So does every game initialisation: the first map after
  the server starts, or the `map` command, which restarts the game. A level change within a game does not reset
  the timer, but clients reconnecting during it change n, which usually triggers a heartbeat anyway.
- The developer console prints `Sending heartbeat to GameSpy...` before each periodic or triggered send.
- Every heartbeat carries `\statechanged\`. There is no plain heartbeat form.
- At engine shutdown every instance sets its game mode text to `exiting` and sends one heartbeat. This happens
  regardless of `public` and of maxclients, so client-only processes send one too. The query socket is then closed
  immediately, so a master's follow-up query gets no answer.
- The server never waits for a master reply. A master that wants to check the server sends ordinary queries
  (section 3) to the heartbeat's source address. Masters usually send `\basic\\secure\<challenge>` or
  `\status\\secure\<challenge>` and check `\validate\` with the secret key (master-side, UNVERIFIED for a
  particular master).

Address resolution: `master.gamespy.com` is converted once, when the query socket is opened at engine start. If
it is not a dotted address and DNS lookup fails, the heartbeat address stays at 255.255.255.255:27900 for the
whole run. Windows refuses such a send without broadcast permission, so no master is reached (UNVERIFIED whether
anything leaves the host). Every unpatched 1.07f install is in this state today.

### 3. Query responder (every SoF process, UDP)

**Socket.**
- The socket is opened at engine start by every instance: client, listen server and dedicated server alike. This
  happens before any game socket exists.
- Port search: for base b = `gamespyport` + 0 … + 4, the ports b, b+1, …, b+99 are tried in order, and the first
  that binds is used. In practice this is the first free port in 28911–29110.
- It binds to all interfaces (UNVERIFIED: confirm with `netstat -an` that the bound address is 0.0.0.0).
- It is polled once per engine frame, and all pending datagrams are processed in that frame.

**Reading a query.**
- Each datagram is read into a 255-byte buffer. On Windows a longer datagram is reported as an error and is
  **ignored completely**.
- A query counter Qid, starting at 1, is incremented for every datagram before it is processed. The first query
  after start is therefore answered with Qid 2.
- The datagram text is searched for each of these eight words, enclosed in backslashes, in this fixed order:
  `basic`, `info`, `rules`, `players`, `status`, `packets`, `echo`, `secure`. The search is case-sensitive.
- The order in the request does not matter; every word that occurs is handled once.
- The value of a word is the text after its closing backslash, up to the next backslash or the end. Only `echo`
  and `secure` use their value.

**Reply assembly.** Fragments are appended to an output buffer:

| Word | Fragments appended |
|---|---|
| `basic` | basic block |
| `info` | info block |
| `rules` | rules block |
| `players` | players block |
| `status` | basic, info, rules, players blocks in that order (one stream) |
| `packets` | basic block, *flush*, info block, *flush*, rules block, *flush*, players block |
| `echo` | `\echo\<value>` if the value is at most 1350 characters, else nothing |
| `secure` | nothing yet; the value is remembered |

After all words have been handled:
- If a `secure` value was given and it is non-empty:
  - up to 128 characters: `\validate\<validation of the value>` is appended;
  - longer: processing of this datagram **stops here**, and the final flush below does not happen. Datagrams
    already flushed earlier (by `packets` or by splitting) were sent; the rest of the reply, including `\final\`
    and its `\queryid\`, is never sent.
- Then `\final\` is appended, and the buffer is flushed.

**Flush.** Nothing happens if the buffer is empty. Otherwise its contents, followed by `\queryid\<Qid>.<P>`, are
sent to the query's source address as one datagram, where P counts the datagrams of this reply from 1. The buffer
is then emptied.

**Packet splitting.**
- If the current buffer plus the new fragment is at most 1349 bytes, the fragment is appended.
- Otherwise, if the new fragment is at most 1350 bytes, the current buffer is flushed and the fragment starts the
  new buffer.
- Otherwise the fragment is cut at its last backslash that begins a key and lies within its first 1350
  characters. These are the backslashes counted 0, 2, 4, … from the fragment's start. The first piece is handled
  by these same rules, and the rest is handled again. If the only such backslash is at position 0, the fragment is
  dropped.

Consequences of these rules:
- A datagram carries at most 1349 bytes of data plus the queryid suffix.
- Every complete reply ends with a datagram that contains `\final\`.
- `\packets\` always gives at least four datagrams. The last is `\final\\queryid\N.4` when nobody is playing.

**Blocks.** Each block is empty when no server is running. A client-only process therefore answers with just
`\final\\queryid\N.1`, plus the echo and validate parts. In the formats below each `%…` is replaced as in C
`printf`. Each fragment is first formatted into 256 bytes, so longer values are truncated.

| Block | Fragments, in order |
|---|---|
| basic | `\gamename\sofretail\gamever\1.07f\location\1` |
| info | `\hostname\` + cvar `hostname`; `\hostport\` + the game socket's port (unsigned decimal); `\mapname\` + cvar `mapname`; `\gametype\` + the game module's game-type name (SPEC-001 export 0x50), or the integer value of `deathmatch` when no game module is loaded; `\numplayers\` + number of spawned clients; `\maxplayers\` + integer `maxclients`; `\gamemode\` + `openplaying` (`exiting` after shutdown began); `\violence\` + cvar `sv_violence` text |
| rules | `\timelimit\` + integer `timelimit`; `\fraglimit\` + integer `fraglimit`; `\dmflags\` + integer `dmflags`; `\movescale\` + (integer part of `g_movescale`) × 100 followed by a literal `%`; `\cheats\` + `enabled` when `cheats` ≠ 0 else `disabled`; `\ctf_loops\` + integer `ctf_loops`; `\suicide_penalty\` + integer `sv_suicidepenalty` |
| players | for every spawned client slot i (0 … maxclients−1, slot order): `\player_<i>\<name>\frags_<i>\<frags>\ping_<i>\<ping>\skin_<i>\<skin>\team_<i>\<teamname>` (details below the table) |

Players block details:
- i is the **slot number**, not a running count, so gaps are possible.
- The name is the raw player name, including SoF colour bytes.
- Frags are the player's score as a signed 16-bit value.
- Ping is the server's ping estimate for that client.
- Skin and teamname come from the player's userinfo keys `skin` and `teamname`.

Integer values are the cvar's floating-point value, truncated. The movescale value is truncated before the
multiplication, so 1.5 gives `100%`.

### 4. Today's masters (informative; black-box observations of 2026-10-21)
The original reaches only the host name `master.gamespy.com`. Players redirect that name with a hosts-file entry or
a patched executable. These community masters greet with `\basic\\secure\<6 characters>` and accept the 1.07f
browser's validation:

| Master | `\list\cmp\gamename\sofretail\final\` result |
|---|---|
| `sof1master.sofvault.org` (144.217.72.30; used by SoFplus) | 29 records + `\final\` |
| `master.333networks.com` | 42 records + `\final\` |
| `master.openspy.net` | empty list (only `\final\`) |

A replacement master serving original clients must:
- send `\secure\` + 6 characters in its first TCP segment;
- accept the validate message, which the client sends without waiting for a reply;
- answer the `cmp` request with records carrying **query** ports;
- send `\final\` before closing.

To list original servers, it must take their heartbeats on UDP 27900 and query the heartbeat's source address.

Recommendation for OpenMerc (not in the original):
- cvars for the master host and ports, e.g. `gs_master` (default `master.gamespy.com`), with heartbeat port 27900
  and list port 28900;
- resolution at every heartbeat and refresh;
- a connect timeout, so a dead master does not freeze the game.

The browser may query several masters and merge the results. The wire format must stay exactly as above, so
original servers, original clients and existing masters interoperate.

### 5. Lobby chat
**Transport.**
- TCP to `gsc_server`:`gsc_port`. The connect is blocking. TCP keep-alive is enabled on the socket.
- Plain IRC framing (RFC 1459 lines): outgoing lines end with CR LF; incoming lines may end with CR, LF or both.
  Lines have an optional `:prefix`, a command, parameters and a trailing `:` parameter.
- No encryption and no `CRYPT` handshake; no game name or key is sent.
- Sent text is buffered and written as the socket accepts it, up to 1024 bytes per write.
- Every request that waits for a server answer (channel list, join, names, topic, ban look-up) gives up after
  30 seconds and then reports failure.

**Colours.** Chat-log lines begin with SPEC-002 colour codes:

| Code | Used for |
|---|---|
| 1 | operator nick |
| 2 | errors, notices |
| 3 | brackets around foreign private messages |
| 4 | status |
| 6 | actions |
| 7 | highlight |
| 28 | normal messages |

Below, `^n` means the single byte n.

**5.1 `gsc_connect [server [port [nick [user]]]]`**
- An existing chat connection is disconnected first, as by `gsc_disconnect`. Each given argument is stored into
  `gsc_server`, `gsc_port`, `gsc_nick`, `gsc_user`, in that order.
- Console: `Connecting to ^7<server>:<port>^1 with nickname ^7<nick>^1 and username ^7<user>^1`. Public chat log:
  `^4Connecting to: <server>`.
- On connect failure: console and public log `^2Failed to connect to <server>:<port>`.
- Registration, sent right after the TCP connect:
  - `NICK <nick>`. If the nick is empty, starts with a digit, or contains any character other than the letters A–Z
    and a–z, the digits and the nine characters ``[ ] \ ` _ ^ { | } -``, the literal nick `1` is sent instead.
    Servers reject it (see below).
  - then `USER <gsc_user> 127.0.0.1 <gsc_server as typed> :SoldierofFortune`.
- Welcome (numeric 001): console `Connected to <server>:<port>!`, public log `^4Connected to <server>:<port>!`. The
  room list is cleared and `LIST` is sent.
- Nick refused during registration:
  - numeric 433: public log `^2Nick <nick> is already in use - please change`;
  - numeric 432: `^2Nick <nick> is invalid - please change`.

  Both name the nick as configured, even when `1` was sent. The client then closes the connection; no `QUIT` is
  sent before the welcome.
- An `ERROR` line before the welcome ends the registration as failed. The original still prints the two
  "Connected" lines and clears the room list, then sends nothing more (UNVERIFIED: check against a server that
  refuses the client).
- `PING <x>` from the server is answered `PONG <x>`. A CTCP `PING` inside a PRIVMSG is answered
  `NOTICE <sender> :^1PING <arg>^1`. Other CTCP requests except `ACTION` are ignored.
- Disconnection (socket closed by the server, or `ERROR` after registration): public log `^4Disconnected <reason>`,
  where the reason is `Disconnected` or the ERROR text. The user list is cleared.

**5.2 Rooms.**
- `gsc_getchannels` clears the room list and sends `LIST`, which is also sent automatically after the welcome.
- For each 322 reply whose channel name does not start with `*`, the room `<channel without its first character>`
  is added (SPEC-803 §3).
- At the end (323) the console shows `<n> channels found.`, where n counts every listed channel.

**5.3 `gsc_join <channel> [password]`** (the channel is given without `#`)
- The client remembers one current channel. If `<channel>` equals it (case-insensitive): public log
  `^4Already in channel: #<channel>!`, and nothing is sent.
- If another channel is current: public log `^4Leaving channel: #<old>`, then `PART #<old>`.
- Public log `^4Joining channel: #<channel>`, then `JOIN #<channel> <password>`. Without a password the line ends
  with a space after the channel name.
- When the server echoes the join, the client sends `MODE #<channel>`. The join is complete when the channel-mode
  reply (324) arrives.
- Success: console `Successfully entered/created channel #<channel>!`, public log
  `^4Successfully entered/created channel #<channel>!`. The current channel becomes `<channel>`, and the user list is
  cleared and refilled as for `gsc_getusers`.
- Failure (numerics 403, 405, 471, 473, 474, 475, 476, or the 30 s limit): console and public log
  `^2Failed to enter/create channel #<channel>`. The current channel becomes empty.

**5.4 Other commands.** Channel arguments are given without `#`. A text argument is a single console argument, so
it must be quoted.

| Command | Too few arguments → console | Sent | Local effect |
|---|---|---|---|
| `gsc_part <channel>` | `Usage: gsc_part <channelname>` | `PART #<channel>` | user list cleared; public log `^4Left channel: #<channel>`; the current channel is **not** forgotten (a later join of it reports "Already in channel") |
| `gsc_talk <channel> <text>` | (none; empty text sends nothing) | text starting with `/me ` (any case): `PRIVMSG #<channel> :^1ACTION <rest>^1`; starting with `/notice `: `NOTICE #<channel> :<rest>`; otherwise `PRIVMSG #<channel> :<text>` | the message is shown locally as an incoming channel message from the own nick (5.5) |
| `gsc_private <nick> <text>` | (none) | same three forms addressed to `<nick>` | private log: `^6* <own nick> <text>`, `^2-<own nick>: <text>-`, or `^28<own nick>: <text>` (prefix removed from text) |
| `gsc_getusers <channel>` | `Usage: gsc_getusers <channel>` | `NAMES #<channel>` (nothing when the client is already in that channel; the known list is used) | user list cleared, then one entry per user: `^1@<nick>` operator, `^4+<nick>` voice, `^28 <nick>` normal; console `<n> users found in #<channel>`; failure: `Failed to list users for #<channel>` |
| `gsc_settopic <channel> <topic>` | `Usage: gsc_settopic <channel> <topic>` | `TOPIC #<channel> :<topic>` | — |
| `gsc_gettopic <channel>` | `Usage: gsc_gettopic <channel>` | `TOPIC #<channel>` (nothing when the topic is already known from being in the channel) | `menu_topic` := `#<channel>: <topic>` (control characters removed, cut to 63 characters) |
| `gsc_kick <channel> <user> <reason>` | `Usage: gsc_kick <channel> <user> [reason]` (the reason is in fact required) | `KICK #<channel> <user> :<reason>` | — |
| `gsc_ban <channel> <user>` | `Usage: gsc_ban <channel> <user>` | `WHOIS <user>`; on its 311 reply `MODE #<channel> +b *!*@<host from the reply>` | — |
| `gsc_setusermode <channel> <user> <mode>` | `Usage: gsc_setusermode <channel> <user> <mode>` | mode `op`: `MODE #<channel> +o <user>`; `voice`: `MODE #<channel> +v <user>`; anything else: `MODE #<channel> -o <user>` then `MODE #<channel> -v <user>` | — |
| `gsc_disconnect` | — | `QUIT :Later!`, then close | — |

Without a chat connection, the commands that send do nothing; usage messages still appear.

**5.5 Incoming events → lobby widgets (SPEC-803 §3).** Message text has control characters removed before display.

| Event | Chat log line (log) | Other effect |
|---|---|---|
| Channel message from `<nick>` | normal `^28<nick>: <text>`; action `^6* <nick> <text>`; notice `^2-<nick>: #<channel>- <text>` (public) | — |
| Private message from the user selected in the `users` widget | same three forms as `gsc_private` but with the sender's nick (private) | — |
| Private message from anyone else | the same forms wrapped as `^3[` … `^3]` (public) | — |
| A user joins / leaves the channel | `^4<nick> joined channel #<channel>` / `^4<nick> left channel #<channel>` (public) | — |
| The own client is kicked | `^4<kicker's nick> was kicked from #<channel> because <reason>` (public; the line names the kicker) | the channel is dropped |
| Another user is kicked | as "leaves" | — |
| A user changes nick | `^4<old> is now known as <new>` (public) | `gsc_nick` := `<new>` — for **any** user's change (original behaviour); user list refreshed |
| A user's channel mode changes | `^4<nick> opped|voiced|peoned in #<channel>` (public) | user list refreshed |
| Topic received or changed | — | `menu_topic` as for `gsc_gettopic` |
| Channel mode changed | developer console only | — |

Messages whose IRC command is `UTM` are shown like normal messages.

## State machines (summary)
**Browser list.** States and transitions:
- idle → transferring: the handshake (1.1) succeeded.
- transferring → querying: `\final\` was received, or the connection closed.
- querying → idle: every server is done, or the transfer was aborted.

A handshake failure leaves the list idle. `pingservers` is refused unless the list is idle.

**Heartbeat timer.** It keeps the last-sent time T and the last player count N. The command `heartbeat` and game
initialisation set T to "long ago". On each server frame, if the server is public and multiplayer: when the current
count ≠ N or now − T ≥ 150 s, a heartbeat is sent and T = now, N = count.

## Constants
| Name | Value |
|---|---|
| Master list TCP port / heartbeat UDP port | 28900 / 27900 |
| Query port default / search | 28911 / `gamespyport` + 0…4, each + 0…99 |
| Chat default host / port | peerchat.gamespy.com / 6667 |
| Game name / secret key | sofretail / iVn3a3 |
| Browser version / location fields | 1.6 / 0 |
| Server version / location fields | 1.07f / 1 |
| List request type | cmp |
| Per-server query | `\status\` (8 bytes) |
| Concurrent server queries | 10 |
| Per-server query timeout | 3000 ms |
| Unanswered ping value | 9999 |
| Master greeting read | one receive, ≤ 255 bytes |
| Challenge length used by the browser | 6 bytes (response 8 characters) |
| List record size | 6 bytes (4 address + 2 port, network order) |
| List receive buffer | 2047 bytes |
| Server reply datagram read size (browser) | ≤ 1499 bytes |
| Query read size (responder) | 255 bytes; longer datagrams ignored |
| Reply datagram data limit | 1349 bytes + `\queryid\…`; single-fragment limit 1350 |
| Responder fragment format buffer | 256 bytes |
| Echo limit / `\secure\` limit | 1350 / 128 characters |
| Heartbeat interval | 150000 ms |
| Chat request timeout | 30 s |
| Chat write chunk | ≤ 1024 bytes |
| `menu_topic` maximum | 63 characters |
| Browser status texts | `Transferring Server List`, `Querying GameSpy Master`, `Query Complete` |

## Test vectors / how to verify
1. Validation, key `iVn3a3`. The responses were computed by running the original 1.07f code under emulation and by
   an independent implementation of the rules above:

   | Challenge | Scrambled bytes (hex) | Response |
   |---|---|---|
   | `ABCDEF` | 5e 6d 6e c9 32 c1 | `Xm1uyTLB` |
   | `abcdef` | 5c 2e 72 b1 9f bc | `XC5ysZ+8` |
   | `123456` | 40 77 36 33 19 7f | `QHc2Mxl/` |
   | `QWERTY` | 77 7b bd c4 74 a2 | `d3u9xHSi` |
   | `000000` | 41 de 77 b1 40 7c | `Qd53sUB8` |
   | `IQPMTF` | e4 72 c9 aa e1 94 | `5HLJquGU` |
   | `IVJYHE` | e4 f7 7c 74 8f 4a | `5Pd8dI9K` |
   | `QZvHrG` | 77 3a 21 18 fb 68 | `dzohGPto` |
   | `abcdefgh` (server side, 8 bytes) | 5c 2e 72 b1 9f bc c9 aa | `XC5ysZ+8yaoA` |
   | `abcdefg` | — | `XC5ysZ+8yQAA` |
   | `abc` | — | `XC5y` |
   | `A` | — | `XgAA` |

2. Black-box confirmations against live third-party hosts, 2026-10-21:
   - A running 1.07f server, sent `\basic\\secure\ABCDEF`, answered in one datagram:
     `\gamename\sofretail\gamever\1.07f\location\1\validate\Xm1uyTLB\final\\queryid\107079.1`
   - The same server, sent `\status\`, answered in one datagram:
     `\gamename\sofretail\gamever\1.07f\location\1\hostname\Mad Onion\hostport\28910\mapname\dm/madrange\gametype\DM\numplayers\0\maxplayers\32\gamemode\openplaying\violence\63\timelimit\20\fraglimit\0\dmflags\4100\movescale\100%\cheats\disabled\ctf_loops\10\suicide_penalty\2\final\\queryid\107078.1`
   - `\packets\` to the same server gave four datagrams with suffixes `.1` to `.4`: the basic block, the info
     block, the rules block, then `\final\\queryid\107080.4` (nobody playing).
   - `\players\` to a populated server:
     `\player_0\<name>\frags_0\57\ping_0\24\skin_0\bear\team_0\Red Guard\player_1\…\final\\queryid\8733.1`.
     Names contain raw colour bytes.
   - `sof1master.sofvault.org` greeted with `\basic\\secure\EHLEXM`. It accepted the two requests of 1.1, sent with
     validate `+UTkXOOR`, and returned 29 records followed by `\final\`. The first record was
     `2F 37 FF A9 71 03` = 47.55.255.169, query port 28931.
3. Fake master on TCP 28900, via a hosts-file redirect. Send `\basic\\secure\ABCDEF`, then expect exactly
   `\gamename\sofretail\gamever\1.6\location\0\validate\Xm1uyTLB\final\\queryid\1.1\` followed by
   `\list\cmp\gamename\sofretail\final\`.
4. Reply with the bytes `C0 A8 01 0A 70 EF 5C 66 69 6E 61 6C 5C`: one server 192.168.1.10 with query port 28911,
   then `\final\`.
   - The client sends `\status\` to 192.168.1.10:28911.
   - A server at that address appears in the browser.
   - Joining it connects to 192.168.1.10 at its `hostport`.
5. Same as test 4, but close the TCP connection without `\final\`: no `\status\` query is sent and no GameSpy entry
   appears.
6. A fake server answers within 3 s with
   `\hostname\X\hostport\28910\mapname\dm/a\numplayers\2\maxplayers\8\gametype\CTF\violence\0\final\\queryid\5.1`.
   The browser record text is `X+dm/a+2/0/8++CTF+0+<ping>+`. If the answer comes after 3 s, the server is not listed.
7. Query responder edge cases:
   - `\echo\hello` → `\echo\hello\final\\queryid\N.1`.
   - A 300-byte datagram gets no answer at all.
   - A client-only process (no server running) answers `\status\` with `\final\\queryid\N.1`.
8. Heartbeats: start a server with `public 1` and `maxclients 8` and capture UDP 27900. Expect
   `\heartbeat\28911\gamename\sofretail\statechanged\` at start, again when a player finishes connecting, and every
   150 s. Then quit the game with `public 0`: one heartbeat in the same form is still sent.
9. Chat with default cvars against a local IRC server:
   - The first lines received are `NICK JohnMullins` and
     `USER JohnMullins 127.0.0.1 peerchat.gamespy.com :SoldierofFortune`.
   - After the welcome the client sends `LIST`.
   - `set gsc_nick 1abc; gsc_connect` sends `NICK 1`.
10. `gsc_join lobby` → `JOIN #lobby ` (trailing space), and after the server's JOIN echo, `MODE #lobby`.
11. `gsc_setusermode lobby bob none` → `MODE #lobby -o bob` and `MODE #lobby -v bob`.

## Open questions
- Bind address of the query socket (expected all interfaces): check with netstat.
- Whether a heartbeat addressed to 255.255.255.255 leaves the host (section 2).
- Master-side behaviour toward heartbeats (which query is sent back, delisting timeout): observe with a capture
  against each community master.
- Exact behaviour when the chat server sends `ERROR` before the welcome (5.1).
- Case folding of browser key and value strings (1.3): confirm with two fake servers.

## Amendments to approved specs (proposed)
- **SPEC-200, "GameSpy server list (client side, informative)":** the list type is `cmp` (binary 6-byte records
  ending with `\final\`) and the per-server query is `\status\`. This resolves the open question "List type and
  per-server query string". Test vector 1 above answers the validation test-vector open question.
- **SPEC-200, "GameSpy heartbeats":**
  - The shutdown heartbeat is sent by every instance, regardless of `public` and maxclients.
  - The heartbeat address is resolved only once, at start (section 2).
  - Open question "Address the GameSpy query socket binds to": all interfaces (UNVERIFIED, netstat).
- **SPEC-200, "GameSpy query protocol":** add the 255-byte request limit and the 128-character `\secure\` limit of
  section 3.
- **SPEC-803 §4 "List maintenance":**
  - The order of the `menu_servername` texts is `Transferring Server List` (list download), then `Querying GameSpy
    Master` (querying the servers), then `Query Complete`.
  - The GameSpy refresh is skipped when `noudp` is set.
  - The whole `pingservers` is refused with `GameSpy is busy .....` while a refresh runs.
- **SPEC-803 "Server list record", ping text:** for master-list servers the ping is the round trip measured by the
  client's own `\status\` query (1.3), not a value reported by the master. The record address uses the `hostport`
  key.
