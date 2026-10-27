# I2d — GameSpy lobby chat (work log)

Package: SPEC-204 §5 (lobby chat protocol, the 13 `gsc_*` commands, cvars `gsc_server`/`gsc_port`/`gsc_nick`/
`gsc_user`, messages), SPEC-302 (commands/cvars), SPEC-803 §3 (lobby widgets `chat`, `users`, `rooms`).
Directory: `engine/src/common/gamespy_chat.c` + `engine/src/common/header/gamespy_chat.h`; minimal marked hooks in
`common/frame.c` and the menu (`client/menu/m_data.c`, `menu.c`).

## State (2026-10-25, resumed)
The first session on this package was interrupted on 2026-10-22 ~14:45 without a journal. This
file reconstructs its state from the uncommitted tree and its outputs in `openmerc-data/I2d/`.

### Exists
- `engine/src/common/gamespy_chat.c` (complete draft): non-blocking TCP client on the I2b socket layer
  (`GS_Resolve`, `GS_TCPConnect/Connected/Send/Recv`, `GS_Close`), IRC line parser, registration (NICK/USER,
  invalid nick → `NICK 1`), welcome → `LIST`, 432/433, ERROR before/after welcome, PING/PONG, CTCP PING/ACTION,
  rooms (322/323), join (JOIN echo → `MODE #ch` → 324 completes, failure numerics, 30 s limit), names requests
  (353/366, up to 8 pending), topics (331/332/TOPIC, `menu_topic`), all 13 commands with the SPEC-204 usage texts,
  incoming PRIVMSG/NOTICE/UTM/JOIN/PART/QUIT/KICK/NICK/MODE/TOPIC → public/private log, user list, `menu_topic`.
  Default server `peerchat.openspy.net`; a stored `peerchat.gamespy.com` is replaced at start.
- `engine/src/common/header/gamespy_chat.h`: `GSC_Init/Frame/Shutdown`, the menu entry points (`MC_*`),
  `GSC_ORIGINAL_SERVER` / `GSC_DEFAULT_SERVER`.
- Hooks (all marked `OpenMerc (I2d)`):
  - `engine/CMakeLists.txt`: `gamespy_chat.c/.h` in Client- and Server-Source/Header;
  - `common/frame.c`: `GSC_Init()` after `SG_InitCommands()`, `GSC_Frame()` in the client frame (after
    `CL_ServerList_Frame`) and in the dedicated frame (after `SV_Frame`), `GSC_Shutdown()` after
    `SV_GameSpy_Shutdown()`;
  - `client/menu/m_data.c`: `MC_Add` drops lines while the menus are closed, `MC_SelectedUser()`, `MC_ClearLogs()`;
  - `client/menu/menu.c`: `MC_ClearLogs()` in `M_CloseSystem`.
- `tools/net/chattest.py`: `proxy` (logging relay to a real chat server), `fake` (scripted IRC server with error
  paths and event triggers `ev_msgs`, `ev_people`, `ev_kickme`, `ev_error`, `ev_close`, `ev_ping`), `peer` (second
  user on a real server).
- `tests/I2d/`: `run_orig.ps1` (original SoF.exe in `D:\Games\SoF-test-I2d` on a hidden desktop with I4a's capture
  shim), console scripts `calib.cfg`, `orig_fake.cfg`, `orig_fake2.cfg`, `orig_fake3.cfg`, `orig_ban.cfg`,
  `orig_openspy.cfg` (original client only: `wait` = one frame at ≈1000 frames/s).
- Black-box results of the ORIGINAL client (`openmerc-data/I2d/`): server-side traces `*_fake.txt`,
  `origspy_proxy.txt` + `origspy_peer.txt`, console logs and captures in `shots/`.

### Black-box findings (original 1.07f, 2026-10-22)
- OpenSpy's peerchat (`peerchat.openspy.net:6667`, 167.99.14.87) accepts the original client's plain-IRC session
  (no CRYPT): welcome, LIST (≈80 channels of many games, mostly `#gsp!…`), JOIN → MODE → 324, messages both ways,
  WHOIS answered (host `*`), TOPIC/MODE/KICK refused with 482 for a non-operator. The original's chat page shows the
  peer's messages (`shots/origspy_o3_peer.png`).
- Things SPEC-204 does not say or says differently (all observed against `chattest.py fake`):
  - ERROR before the welcome: "Connected to …" on the console and in the log, room list cleared, then nothing is
    sent (confirms the UNVERIFIED line);
  - the chat logs exist only while the menus are open: lines that arrive while the menus are closed are lost, and
    the logs are empty after reopening (`origfake3_m3_after_menuoff.png`);
  - CTCP requests other than ACTION/PING are shown as text (`[bob: VERSION]`), not ignored;
  - `gsc_ban`: only `WHOIS <user>` is ever sent; no `MODE +b` follows the 311 reply (short and complete WHOIS
    replies, `origban_fake.txt`);
  - kick of the own client: `<kicker> was kicked from #<channel> because <reason>`; the channel name in the line
    comes from the server's KICK (with `#`);
  - NAMES timeout prints `Failed to list users for ` without the channel name; LIST timeout prints
    `0 channels found.`; TOPIC and WHOIS timeouts print nothing;
  - a topic for a channel the client is not in does not change `menu_topic`;
  - `gsc_nick` follows every user's NICK change (SPEC-204 says so too; OpenMerc follows only its own — deviation).

### Added or changed after the resume
`gamespy_chat.c` (LIST request queue), `tools/net/chattest.py` (exclusive listener port, relay read timeout,
`partlist` nick, log encoding), `tests/I2d/run_om.ps1`, `orig_list.cfg`, `om_default.cfg`, `om_private.cfg`,
`gs_amend005.py`, `gs_violence.py`; for SPEC-AMEND-005: `server/sv_gamespy.c` (port search),
`tools/net/masterquery.py` (GS-V1 vector); `docs/DEVIATIONS.md`, `docs/specs/OPEN_QUESTIONS.md` (section I2d).

### Remains (at resume; all five done on 2026-10-25, see Log and "State (end)")
1. Reconfigure and build `build-I2d-x86` (all targets).
2. OpenMerc against `chattest.py fake` with the same scenarios as the original → compare the C> lines.
3. OpenMerc against OpenSpy's peerchat through `chattest.py proxy`, with `chattest.py peer`: protocol trace of
   connect, getchannels, join, talk, private, part, disconnect.
4. Menu chat pages filled (captures with `om_menushot`).
5. DEVIATIONS rows, OPEN_QUESTIONS (I2d), report.

## Log
- 2026-10-25: journal reconstructed (this file).
- 2026-10-25: step 1 done — `build-I2d-x86` reconfigured, all targets build, `gamespy_chat.c` without warnings.
  OpenMerc test wrapper `tests/I2d/run_om.ps1` (adapts the original's scripts: OpenMerc runs ≈200 `wait`s/s,
  measured with `calib.cfg`: 100 waits ≈ 0.5 s; needs `snd_dll OMsnd.dll` — with the original defsnd.dll the
  client hangs on the hidden desktop).
- 2026-10-25: `orig_fake.cfg` with OpenMerc (`om/omfake_fake.txt`): the whole wire trace, both directions, equals
  the original's (`origfake_fake.txt`) line for line (only the client ports differ). Console differences: the
  order of the developer user listing (the original prints its internal order, OpenMerc the list order) and
  `gsc_nick` after another user's NICK (deviation). Chat log content of the captures matches.
- 2026-10-25: `orig_fake2.cfg`, `orig_fake3.cfg`, `orig_ban.cfg` with OpenMerc: wire traces identical to the
  original's (`om/omfake2_fake.txt`, `om/omfake3_fake.txt`, `om/omban_fake.txt`). Side-by-side captures in
  `openmerc-data/I2d/pairs/`: same log lines, colours, user lists and topics. Differences found:
  1. the original prints `0 channels found.` once per unanswered LIST (two pending requests → two lines);
     OpenMerc keeps one LIST state, a second request restarts its timer → one line (fix below);
  2. menu (I6m, not changed here): once the log is longer than the chat widget, OpenMerc's chat page shows the
     newest line in the middle of the widget instead of at its bottom (the page scrolls to the bottom but the
     widget keeps its frame height, so the bottom-up drawing starts at y + h − 4 − scroll);
  3. the original fills the chat area black and draws its Ghoul frame; OpenMerc shows the backdrop (menu models,
     I4b/I1c).
  4. `Failed to list users for ` once in OpenMerc's `orig_fake2` run: the slower OpenMerc script let the 30 s
     limit of `gsc_getusers nosuch` (fake replies 403 only) run out before the disconnect that cancels it in the
     original's faster run — timing of the test, not behaviour.
- 2026-10-25: new black-box run of the original, `tests/I2d/orig_list.cfg` (`origlist_fake.txt`,
  `shots/origlist.log`, `shots/origlist_l1_rooms.png`): two LISTs sent at once and both answered → two lines
  `5 channels found.` (each request counts its own replies, the developer lines restart at `Channel 0`) and every
  room twice in the rooms widget; a LIST answered with 321 + two 322 but no 323 → after 30 s `0 channels found.`;
  LISTs at T0 and T10 that are never answered → one `0 channels found.` before T33 and one after → each request
  has its own 30 s limit. `gamespy_chat.c` now keeps a queue of LIST requests (oldest answered first, own count
  and start time; a timeout reports 0). OpenMerc with `orig_list.cfg` (`om/omlist_fake.txt`): wire trace equal,
  console equal (the T33 marker sits differently only because OpenMerc's script second is 1.5 s), rooms page equal
  (`pairs/pair_omlist_l1_rooms.png`); `orig_fake3.cfg` rerun: now both `0 channels found.` lines.
- 2026-10-25: OpenSpy session with OpenMerc through the relay (`orig_openspy.cfg`, `om/omspy_proxy.txt`,
  `om/omspy_peer.txt`): every client line equals the original's run (`origspy_proxy.txt`): NICK/USER, LIST (auto +
  joinchat page), `JOIN #openmerc-i2d ` → MODE → 324, PRIVMSG/ACTION/NOTICE to the channel, PRIVMSG to the peer,
  TOPIC/WHOIS/MODE +o/KICK (482 from OpenSpy for a non-operator), PART, `QUIT :Later!`. The peer received
  OpenMerc's lines; the peer's channel/private lines, PART/JOIN and NICK reached OpenMerc's chat page
  (`pairs/pair_omspy_o3_peer.png` equals the original's page line for line). A first run lost the connection at
  51 s: the relay's upstream socket kept the 15 s connect timeout as a read timeout (fixed in `chattest.py`;
  OpenSpy keeps silent clients ≥ 90 s, checked with a plain socket).
- 2026-10-25: default server, no relay (`om_default.cfg`, home `openmerc-data/I2d/omhome-default` with an
  original-style `config.cfg` storing `gsc_server "peerchat.gamespy.com"`): `gsc_server` is
  `peerchat.openspy.net` after start; `gsc_connect` without arguments → `Connected to peerchat.openspy.net:6667!`,
  `67 channels found.`, join, talk, private, part, disconnect; the peer (`om/omdef_peer.txt`) saw every line;
  capture `om/omdef_d1_chat.png`; the saved config holds the new host.
- 2026-10-25: private page (`om_private.cfg` + `dbg` variant, OpenMerc only): a click on `bob` in the users
  widget (`om_menukey mouse1 60 190`) sets `menu_nick` = bob; with bob selected his private messages go to the
  private log unbracketed and the private page shows `Started private chat with bob` + them
  (`om/dbgpriv_q2.png`); the public log no longer gets them. Menu finding (I6m): the click does not run the
  widget's key command `menu private` (stock `users.rmf`); `menu private` from the console opens the page.
- Tools: `chattest.py` listeners take their port exclusively on Windows (SO_REUSEADDR let a stale helper and a
  new one share 16668; one run's trace landed in the other's log and was split by hand); `run_om.ps1` refuses
  to start when 16667/16668 are taken and ends the helpers' whole process tree (`python` = py.exe launcher).
- 2026-10-25: dedicated server (`q2ded`, `openmerc-data/I2d/ded/`): the 13 commands exist there too (as in the
  original, one executable); connect, LIST, join (JOIN echo → MODE), talk, CTCP PING answer, private, part, QUIT
  run through the dedicated frame loop (`ded/ded_fake.txt`).
- 2026-10-25: DEVIATIONS rows (chat server default, non-blocking connect, `gsc_nick`) and OPEN_QUESTIONS section
  "I2d" (8 items) written.

## SPEC-AMEND-005 GS.9–GS.17 (coordinator request 2026-10-25: I2b is not staffed)
Test: `tests/I2d/gs_amend005.py --engine om|orig --case search100|search104|osport` (dedicated, hidden; base
gamespyport 41911 so other GameSpy processes on the PC are not disturbed — the rules are relative to it) and
`tests/I2d/gs_violence.py [--modified]` (original only, test copy). The original runs from `D:\Games\SoF-test-I2d`.

| Item | Code | Verify result |
|---|---|---|
| GS.9 (masters answer heartbeats with `\secure\` only; 8-char OpenSpy challenge) | none needed: the responder validates the whole `\secure\` value | GS-V1 `\basic\secure\oyMl8D24` → `\validate\a5df+rC3Ni8A` from OpenMerc q2ded and from the original (same reply, query id 2); vector added to `tools/net/masterquery.py --selftest` (passes) |
| GS.10 delisting (informative) | none | — |
| GS.11 duplicates (informative) | none; DEVIATIONS "GameSpy masters" row extended | — |
| GS.12 heartbeat to 255.255.255.255 | none: OpenMerc sends only to resolved `sv_masters` entries, never a broadcast fallback; the socket has no broadcast permission (same as the original) | by code reading (no pktmon run) |
| GS.13 query socket on all interfaces | none (`GS_UDPOpen` binds INADDR_ANY); comment added | `ip 127.0.0.1`: query socket `0.0.0.0:<port>`, game socket `127.0.0.1:27979`, OpenMerc and original |
| GS.14 port search +0…+103 | `sv_gamespy.c`: five overlapping 100-port searches instead of +0…+199; `gamespyport 0` = port chosen by the OS | 100 ports taken → both bind base+100 (42011); 104 taken → neither binds in +104…+209; `gamespyport 0` → both get an OS-chosen port |
| GS.15 `sv_violence` bit 0x20 = pure server | **not changed here** (not GameSpy code; `common/sof_locks.c` is I1a's with an I1b workaround, the server spawn is I1b/I2c's and both are being edited now) — hand-off below | original (test copy), dedicated `dm/jpndm2`: stock modules 63; `final\gamex86.dll` = stock + one appended byte → 31; stock again → 63. GS-V2 data check: SPEC-301 §8 checksum of the shipped `gamex86.dll` 64C0FC54, `player.dll` EACC2EEA (test copy and oracle files identical by MD5; oracle only read) |
| GS.16 responder edge cases | none (a datagram longer than 255 bytes is dropped before the query id is incremented) | 300-byte request: no answer, query id not advanced (3 → 4 over it); 255-byte request answered — OpenMerc and original identical |
| GS.17 shutdown heartbeat | none; already in DEVIATIONS ("GameSpy quit heartbeat") | — |

GS.15 hand-off (I1b/I1a): `SoF_RecomputeViolence` must put only the lock bits 0x01–0x10 into `sv_violence` (it now
copies all six lock bits, so `lock_pure` sets 0x20); at the end of every server spawn set bit 0x20 when the game
module file has the SPEC-301 §8 checksum 64C0FC54 and the server-side player module file EACC2EEA (the checksum
routine exists in `server/sv_player.c`), clear it otherwise, keep 0x01–0x10. With OpenMerc's own modules the server
is then never pure (31 at most), so original clients with `lock_pure` (cl_violence 63, the default without a lock
store) would not list or join it — a DEVIATIONS decision for the coordinator.

Note: the first `gs_amend005.py --engine orig` runs passed `developer 1`; the original then waits in its modal
"Developer Mode" box (SPEC-301), which is not on the hidden desktop. Four such processes ran ≤ 20 s each before the
script killed them; the script no longer sets `developer`.

## SPEC-AMEND-006 I2d.1–I2d.8 (coordinator request 2026-10-25, after commit 6a6e77f)
Changes (`gamespy_chat.c`, the marked I2d hooks in `client/menu/m_data.c` and `menu.c`, `gamespy_chat.h`):

| Item | What changed |
|---|---|
| I2d.1 ERROR before the welcome | requests (LIST, JOIN, PART, NAMES, TOPIC, WHOIS, KICK, MODE, messages) only while registered (after 001; was: from the TCP connect on); the refused session keeps its socket and still answers PING (the output was dropped before); `Disconnected ` / no QUIT unchanged |
| I2d.2 CTCP | a PRIVMSG text is a request only with byte 1 + letter … byte 1 and a space after the command; `PING`/`ACTION` exact case; other requests ignored; everything else (also NOTICE/UTM) is plain text |
| I2d.3 ban | comment only (WHOIS only, as before) |
| I2d.4 LIST | `gsc_getchannels` clears the rooms also without a session; 321/322 restart the oldest request's 30 s; a 322 counts only with 4 parameters; rooms go to the menu at once (one `MC_AddRoom` per 322 instead of a pushed list); the same restart for join (JOIN echo, 331/332, 353, 366) and names (353), names matched oldest first |
| I2d.5 timeouts | only a 366 with exactly 3 parameters ends a NAMES (unchanged: no error numeric does) |
| I2d.6 channel delivery | every channel-named event (messages, CTCP actions, JOIN/PART/KICK/MODE/TOPIC, own kick) only while in that channel; held while the join completes and handled after it; messages to a target that is neither the own nick nor the current channel are dropped; the `gsc_talk` echo follows the same rule |
| I2d.7 lifetime | room and user operations ignored while no menu is open; closing the last menu clears logs, rooms, users and the selected user (`MC_ClearLobby`, was `MC_ClearLogs`) |
| I2d.8 chat drawing | `Chat_Update`: once N·L ≥ the laid-out height, the widget's height follows N·L, the page content height is recomputed from its widgets and a page with a vertical bar scrolls to the bottom |

Black-box run of the original for the items marked "needs a black-box run" and the Verify lines
(`tests/I2d/orig_amend006.cfg`, fake server modes `errkeep*`, `latelist*`, `#busy`, `#other`, `ev_ctcp`,
`ev_other` added to `chattest.py`; `origamend_fake.txt`, `shots/origamend.log`, captures `shots/origamend_*`):
- I2d.1: after `ERROR :x` the socket stays open, `PING :abc` 2 s later is answered `PONG :abc`; `gsc_join`,
  `gsc_getchannels`, `gsc_talk` send nothing; the next `gsc_connect` writes `Disconnected ` and sends no QUIT.
  New: `gsc_join` there still writes `Joining channel: #lobby` to the public log (OpenMerc does the same now;
  OPEN_QUESTIONS I2d 10).
- I2d.2: all ten private and four channel variants exactly as the amendment predicts (VERSION shown, `VERSION 1`
  ignored, `PING` shown, `PING 7` answered, `ping x` ignored, `ACTION` shown, `action lower` ignored, NOTICE with
  byte 1 plain, channel `PING 9` answered, channel `VERSION 2` ignored).
- I2d.4 rule 3: LIST answered with 321 + 322 at T0 and a 322 at T25, no 323: `0 channels found.` between the T44 and
  T57 markers (not at T30). I2d.5: `gsc_getusers slownames` and `nosuch` (403, no 366): both
  `Failed to list users for ` between T10 and T34, nothing at once.
- I2d.6: events of `#elsewhere` (message, notice, join, topic, own kick) and a message to another nick: nothing
  shown; `gsc_gettopic other` (answer `Topic B of other`) leaves `menu_topic` = `#lobby: …`. Join of `#busy` with
  a PRIVMSG, a JOIN and a TOPIC between its names and mode reply: user list 6 (the JOIN went into it without a
  line), then the message, then the held topic, then the join's own topic (left in `menu_topic`), then modes —
  OpenMerc now does exactly this (OPEN_QUESTIONS I2d 9).
- I2d.7: `menuoff`, events, reopen with `menu_room slownames`: empty log and user list, `menu_topic` kept.
- Found while comparing captures: `orig_fake2` J — `gsc_talk lobby ev_close` on a new session that never joined
  `#lobby` shows no echo in the original (OpenMerc showed one; fixed with the I2d.6 rule).

Reruns on OpenMerc after the changes (`tests/I2d/compare.py <orig> <om>`: wire both directions, console without
the known order of the developer user listing):

| Scenario | Wire | Console |
|---|---|---|
| `orig_amend006` | same (111 lines) | same (90 lines) |
| `orig_fake` | same (236) | same except `gsc_nick` after another user's NICK (deviation) |
| `orig_fake2` | same (246) | same (62) — the earlier NAMES timing difference is gone with w1s = 200 waits |
| `orig_fake3` | same (77) | same (41) |
| `orig_ban` | same (45) | same (13) |
| `orig_list` | same (76) | same (40) |
| `orig_openspy` (relay to peerchat.openspy.net) | client lines same as the original's | capture `pair_omspy_o3_peer.png` equal |

Captures: in every chat-page pair the chat area differs from the original only inside the 31×31 box of
OpenMerc's mouse pointer (`pairs/pair_om*`), so I2d.8 now matches (newest line at the bottom, scroll bar at the
bottom). Exceptions: `a_errors` (the `Failed to connect` line of the non-blocking connect comes ~1–2 s later than the
original's blocking one, a documented deviation) and the private popup (console background behind it).

Tools: `run_om.ps1` now uses w1s = 200 waits (≈1.0 s, like the original's 1000) and `clientport 29551`;
`run_orig.ps1` also passes `clientport 29551` (port base 29550, `docs/process/testing.md`); chat helpers
stay on TCP 16667/16668 (not game ports).

## Needs from other packages
- I6m (menu runtime): a mouse-1 click on a user in the `users` widget selects him but does not run the widget's
  key command (`menu private`). (The long-log chat drawing is fixed here under SPEC-AMEND-006 I2d.8, marked hook
  in `Chat_Update`.)
- I1b/I1a: GS.15 above.

## State (2026-10-25, end, after SPEC-AMEND-006)
Done: the 13 `gsc_*` commands and cvars, rooms/users/chat/private data for the menus, default server
`peerchat.openspy.net`, SPEC-AMEND-006 I2d.1–I2d.8. Wire behaviour and console equal the original in all seven
scenarios (one documented deviation). Open: the `users` click (I6m); OPEN_QUESTIONS I2d 9–11 (join order, `gsc_join`
on an unregistered connection, I2d.8 on the private page); the DNS lookup of `gsc_server` still blocks the frame (as
the original's whole connect did).
