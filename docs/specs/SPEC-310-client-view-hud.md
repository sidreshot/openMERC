# SPEC-310: Client screen — HUD interface layer, scoreboard layouts, prints, view setup, view weapon
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules
Describes: SoF.exe (client), version 1.07f
Depends on: SPEC-002 (renderer slots, fonts), SPEC-003 (player module, weapon prediction), SPEC-131 (string
packages), SPEC-201 (messages), SPEC-202 (player state, stats block), SPEC-203 (configstrings, serverdata),
SPEC-302 (cvars), SPEC-400 (effects, camera shake, view-weapon effects), SPEC-780 (game-side stats, view offset,
bob, roll, lean), SPEC-800/801 (menus, fonts, menu frame)

## Purpose
Everything the 1.07f client draws on top of (or decides about) the 3D view: the HUD "interface" layer that
replaces Quake II's status bar, the scoreboard layout language, centre/caption/typewriter prints and credit
pictures, crosshair, overhead player labels, remote-camera overlays, pause and loading screens, debug graphs,
the client side of the view computation (interpolation, field of view, near clip, cinematic freeze, view
weapon sway) and a few client commands. Game-side view logic (view height, bob, roll, lean, kick) is in
SPEC-780; effect-driven view changes (camera shake, muzzle flashes) are in SPEC-400.

## Interface / layout

### Conventions
- W, H = current video width and height in pixels. All HUD coordinates below are pixels with (0, 0) at the
  top-left. "scaled x(v)" = trunc(v × W / 640), "scaled y(v)" = trunc(v × H / 480). Where a value is given as
  "640×480: n" it is the result at that resolution.
- Colours are (R, G, B, A) bytes. Named colours used repeatedly:

| Name | RGBA |
|---|---|
| HUD green | 0, 220, 0, 255 |
| HUD green translucent | 0, 220, 0, 160 |
| white | 255, 255, 255, 255 |
| grey | 175, 175, 175, 255 |
| red | 255, 0, 0, 255 |
| green | 0, 255, 0, 255 |
| blue | 0, 0, 255, 255 |
| yellow | 255, 255, 0, 255 |

- "Stretch" = SPEC-002 slot 0x54 (x, y, w, h, colour, pic, rotation 0–3; a negative w or h mirrors);
  "scaled pic" = slot 0x4c with scale 1.0; "pic" = slot 0x48; "pic part" = slot 0x50 (x, y, left, top, right,
  bottom, colour, pic: draws the source rectangle left..right × top..bottom at (x, y), mirrored when
  right < left); "fill" = slot 0x74; "font text" = slot 0xc8 with the named font (`small`, `medium`, `title`,
  SPEC-800); "coloured string" = slot 0x64 (maximum length 1024).
- stats[i] = the 16 player-state stats (SPEC-202 field 26; meanings in SPEC-780 §3). pm_type, fov, rdflags,
  remote view type, cinematic freeze, rank/count, spectated player = SPEC-202 fields 8, 24, 25, 20, 27, 30, 31.
- "Active" = the client is fully in a level (connection state after the first valid frame) and the level's
  graphics are registered.

### Remote view types (player-state field 20, first value)
| Type | Set by | Client presentation |
|---|---|---|
| 0 | — | normal view |
| 1 | third-person / follow camera (SPEC-780 §17) | camera view; HUD elements with a remote mask are hidden |
| 2 | cinematic camera | letterbox bars, typewriter prints allowed, captions below the lower bar, countdown and rank elements stay visible |
| 4 | security-monitor camera | full-screen monitor frame |
| 16 | sniper-scope camera | scope overlay with 6 bullets; field of view interpolated as normal |

Any other non-zero type behaves as a camera without overlay. Which game entities produce types 2, 4 and 16 is
game-defined (UNVERIFIED; see SPEC-780).

### Cvars (defaults in SPEC-302)
| Cvar | Use here |
|---|---|
| `scr_interface` (1, archived) | 0 hides the whole HUD interface layer |
| `crosshair` (0, archived) | crosshair picture number 1–4; values outside 0–4 are forced to 4 |
| `shownames` (0, archived) | draw player names over heads (also forced on by configstring 32, see Player labels) |
| `viewsize` (100) | view rectangle size in percent, clamped to 40–100 |
| `scr_centertime` (2.5), `cinem_centertime` (2.5) | centre-print and typewriter base times |
| `scr_showpause` (1), `paused` | pause screen |
| `scr_menuoverlay` (0) | non-zero: only the menus are drawn (no 3D view, HUD or prints) |
| `netgraph`, `timegraph`, `debuggraph`, `graphheight` (100), `graphscale` (1), `graphshift` (0), `graphwidth` (4), `graphsize` (50), `host_speeds` | debug graphs |
| `cl_gun` (1) | view weapon drawn |
| `cl_subtitles` (1) | captions shown (SPEC-201 §34) |
| `cl_showfps` (0), `cl_showlocation` (0), `cl_stats` (0), `log_stats` (0) | console diagnostics |
| `cl_minfps` (5), `sc_framerate` (30) | frame-time clamp and effect scale |
| `r_nearclipdist` | written by the client from the player state |
| `cl_cinematicfreeze` | written by the client from the player state (created on first write) |
| `menu_loading`, `menu_nostats` | loading-screen menu values |

## Behaviour

### 1. Screen update (once per client frame)
Nothing is drawn while the screen is disabled. While the player state's cinematic-freeze value is 2 or more,
the frame is not redrawn at all (not even menus; the previous image stays) and the centre-print, typewriter and
credit-picture timers are cleared. Otherwise, after the renderer's begin-frame call, in this order:
1. Unless `scr_menuoverlay` ≠ 0: the 3D view (`viewsize` rectangle, tile-cleared border), crosshair (§4), the HUD
   interface layer (§2, only when active), the scoreboard layout when stats[9] bit 0x1 is set (§6; this also
   cancels a running typewriter print), remote-view overlays (§8), the credit picture while its timer runs,
   centre/caption print while its timer runs, the typewriter print (only when active and remote view type is 2;
   with another type its timer is cleared), overhead player labels unless stats[8] bit 0x800 (flash-pack
   blindness) is set, the debug graphs (§11), the pause screen (§7), the console or notify lines (Quake II); when the console is
   exactly half down and the client is not active, the lower half of the screen is filled black; the network
   icon `pics/console/net` at (view x + 16, view y + 16) when more than 62 outgoing packets are unacknowledged
   in the connected or active state.
2. Menus (SPEC-801), then the renderer's end-frame call.

Quake II's status bar (`SCR_DrawStats`), inventory, pic-based loading plaque and cinematic playback do not exist.

### 2. HUD interface layer
The HUD is a fixed list of elements, created once at client start in the order of the table in §2.2; they are
drawn in that order. Each element has a size (w, h), an anchor in percent (px, py), two colours (defaults HUD
green and HUD green translucent) and a remote-view mask (default 0).
- **Position**: x = trunc((W − w) × px / 100), y = trunc((H − h) × py / 100), computed with the video size current
  when the level's graphics are registered (the same moment the element pictures are registered); element
  offsets below are relative to (x, y) unless said otherwise.
- **Drawn** each frame when active and `scr_interface` ≠ 0, for every element whose visibility test passes.
- **Standard visibility test**: pm_type is not 2 or 7, at least one of stats[6] (current weapon) and stats[12]
  (level forces HUD) is non-zero, and either the remote view type is 0 or it shares a bit with the element's mask.
- The pictures `pics/interface2/no_0` … `no_9` and `pics/interface2/no_blank` are registered with the elements.

#### 2.1 Big digits
A number is formatted as decimal (`%d`, or `%<n>d` padded with spaces when a width n is given). Each digit is
the picture `pics/interface2/no_<d>` drawn as a scaled pic; every digit advances 18 pixels. Left-to-right mode
draws from x and advances right (non-digits advance 16 without drawing). Right-aligned mode starts at x and
moves left before each character: digits and spaces move 18 (a space draws `no_blank`), other characters move 16
without drawing.

#### 2.2 Element list
| # | Element | Size | Anchor % | Mask | Visibility |
|---|---|---|---|---|---|
| 1 | Sniper scope | 32×32 | 0, 0 | 16 | standard |
| 2 | Item panel | 130×95 | 0, 100 | 0 | standard |
| 3 | Weapon panel | 130×95 | 100, 100 | 0 | standard |
| 4 | Health / armor / meter | 160×52 | 50, 100 | 0 | standard |
| 5 | Countdown | 32×32 | 0, 0 | 2 | standard |
| 6 | Debug target text | 320×32 | 100, 100 | 0 | standard |
| 7 | Mission status messages | 0×0 | 0, 0 | 0 | always |
| 8 | Deathmatch rank / spectator | 48×32 | 0, 0 | 2 | remote mask only |
| 9 | CTF flags | 48×48 | 0, 0 | 0 | stats[6] or stats[12] non-zero, remote mask, server `deathmatch` value 4 |
| 10 | Control flags | 48×48 | 0, 0 | 0 | as 9 but `deathmatch` value 6 and remote view type ≠ 16 |

The `deathmatch` value is the byte from svc_serverdata (SPEC-203): 1 standard, 2 assassin, 3 arsenal, 4 CTF,
5 realistic, 6 control, 7 conquer. "Team mode" below = value 4 or 6, or value 7 while configstring 10 is a
non-zero number. The local player's team digit is the last field of its player-info configstring
(SPEC-780 §5); team colour = blue when that digit is `0`, red otherwise.

#### 2.3 Sniper scope (element 1)
Shown when the remote view type is 16 (bullet count 6), or when stats[8] bit 0x1 is set (bullet count =
stats[2]); while client weapon prediction is active (SPEC-003) the predicted scope flag and predicted clip count
replace stats[8] bit 0x1 and stats[2], and the predicted field of view replaces the player-state field of view.
Let cx = trunc(W/2), cy = trunc(H/2), z = (fov − 5) / 55 (1 at fov 60, 0 at fov 5), colour = HUD green.
Element-local sizes (640×480 values in brackets): A = trunc(0.625 W) [400], B = trunc(0.6667 H) [320],
C = trunc(0.2 W) [128], D = trunc(0.2667 H) [128], E = trunc(0.1953 W) [125], F = trunc(0.2604 H) [125],
G = trunc(0.003125 W) [2], J = trunc(0.004167 H) [2], K = trunc(0.008333 H) [4]. Drawing order:
| Step | Picture | Position and size | Rotation |
|---|---|---|---|
| 1 | `pics/scope/reticle` | (cx − C, cy − 8), C × 16 | 0 |
| 2 | same | (cx + C, cy − 8), −C × 16 | 0 |
| 3 | same | (cx − 8, cy), 16 × −D | 1 |
| 4 | same | (cx − 8, cy), 16 × D | 1 |
| 5 | fill | (cx, cy), 1 × 1 | — |
| 6–9 | `pics/scope/vinette` | (0,0) cx×cy; (W,0) −cx×cy; (0,H) cx×−cy; (W,H) −cx×−cy | 0 |
| 10 | `pics/scope/bullet`, once per bullet k = 0..n−1 | x = A + scaled x(160), y = B + scaled y((2 − n) × 20 + 20 k), size scaled x(64) × scaled y(16) | 0 |
| 11–14 | `pics/scope/arrows128_<m>`, m = 1 + (trunc(z × 100) mod 4) | (cx − E, cy − F) C×D rot 0; (cx − E, cy − K) C×D rot 1; (cx, cy − J) E×F rot 2; (cx − G, cy − F) C×D rot 3 | as listed |
| 15 | `pics/scope/big_arrow` | (cx − scaled x(7), cy − scaled y(135)), scaled x(16) × scaled y(32) | 0 |
| 16 | same | (cx − scaled x(8), cy + scaled y(102)), same size | 2 |
| 17 | same | (cx − scaled x(134), cy − scaled y(8)), scaled x(32) × scaled y(16) | 1 |
| 18 | same | (cx + scaled x(103), cy − scaled y(7)), same size | 3 |
| 19 | text `x %0.2d` of trunc((1 − z) × 15 + 6), font `small` | (cx − scaled x(300), cy − scaled y(70) − 8) | — |
| 20 | `pics/scope/big_arrow` | (cx − scaled x(290), cy + trunc((110 z − 62) × H / 480)), scaled x(26) × scaled y(12) | 3 |
| 21 | `pics/scope/gauge` | (cx − scaled x(300), cy − scaled y(64)), scaled x(16) × scaled y(128) | 0 |
So the magnification text reads `x 06` at fov 60 and `x 21` at fov 5, and the zoom pointer (step 20) moves from
48 px below the centre (fov 60) to 62 px above it (fov 5) at 640×480.

#### 2.4 Item and weapon panels (elements 2 and 3)
Both panels are hidden while stats[8] bit 0x1 (scope) is set. Panel colours: frame white; frame when closed white
with alpha 175; label and icon HUD green; count HUD green (weapon panel: see blinking). Per-panel constants:
| | Item panel | Weapon panel |
|---|---|---|
| shown value (selection) | stats[0] (item type) | stats[6] (weapon id) |
| count | stats[7] | stats[3] (reserve ammunition) |
| frame source columns | 128 → 0 (mirrored) | 0 → 128 |
| label x | 17 | 49 |
| icon x | 28 | 62 |
| count x | 76 | 31 |
Each frame: when the selection differs from the last one seen (the first frame always counts as a change) and
the tray is closed or closing (phase φ ≤ 0), the tray opens: φ' = 3 rad/s, target π/2, hold time 2.5 s. Each
drawn frame φ += frame time × φ', clamped at the target in the direction of motion (then φ' = 0). The hold time
decreases by the frame time; when it reaches 0 while φ' ≥ 0, the tray closes: φ' = −3 rad/s, target 0.
Drawing (pic parts use the panel's columns, so the item panel is the mirror image of the weapon panel):
- Closed (φ = 0 and φ' = 0): `pics/interface2/frame_top2` rows 0–32 at (0, 27), then `pics/interface2/frame_bottom`
  rows 0–64 at (0, 28), both in the closed colour.
- Otherwise, with o = trunc(cos φ × 27): `pics/interface2/frame_lip` rows 0–32 at (0, 27);
  `pics/interface2/frame_top` rows 0–64 at (0, o); the label (font `small`) at (label x, o + 21);
  `pics/interface2/frame_bottom` rows 0–64 at (0, 28); all frame parts white.
- Then the icon and the count (`%d`, font `small`) at (count x, 63).
Item panel: label = string `items` package (id 3) entry by type: 1 → 1 (flash pack), 2 → 0 (C4), 3 → 3 (goggles),
4 → 4 (claymore), 5 → 53 (medkit), 6 → 54 (grenade), 7 → 57 (flag), other → 5 ("NONE"). Icon = pic part
(0, 0)–(39, 32) at (icon x + 1, 44) of `pics/interface2/item_` + `flash`, `c4`, `nightvision`, `claymore`,
`medkit`, `h_grenade`, `flag` for types 1–7; no icon for other types. (`pics/interface2/item_neuro` is registered but
never drawn.)
Weapon panel: label = `weapons` package (id 2) entry by weapon id: 1 → 0, 2 → 2, 3 → 1, 4 → 7, 5 → 5, 6 → 4, 7 → 8,
8 → 3, 9 → 6, 10 → 10, 11 → 11, 12 → 9, other → 12. Icon = the rounds in the clip, stats[2], as big digits
(§2.1, right-aligned, width 2) starting at (icon x + 39, 42) — i.e. units digit at x 83..101 of the panel.
Count colour: when the reserve is below 2 it blinks: red while (realtime ms mod 1024) ≤ 512, HUD green otherwise.

#### 2.5 Health, armor and meter (element 4; 640×480 position (240, 428))
Hidden while stats[8] bit 0x1 is set. h = clamp(stats[1] / 100, 0, 1), a = clamp(stats[5] / 100, 0, 1),
colours white and white with alpha 150 (the alpha of the second colour is overwritten while blinking).
1. `pics/interface2/frame_health` source (3, 1)–(158, 28) at (0, 10), white.
2. `pics/interface2/health` source (2, 3)–(trunc(124 h + 2), 12) at (26, 21), white.
3. If a > 0: `pics/interface2/armor` source (57, 1)–(trunc(141 a + 57), 13) at (16, 10), white.
4. If h < 0.25: warning light with f = (realtime ms mod 800) × 0.00375: f < 1 → alpha 255; 1 ≤ f < 2 → alpha =
   trunc(sin((2 − f) × π/2) × 255); f ≥ 2 → not drawn. Source (181, 10)–(188, 17) of `frame_health` at (12, 19).
5. If stats[8] bit 0x200: `pics/interface2/frame_stealth` source (3, 0)–(155, 10) at (0, 37); meter with
   m = clamp(stats[11] / 100, 0, 1): source (24, 19)–(trunc(115 m + 24), 23) at (21, 40); if stats[11] > 99 a
   blinking marker (same blink as step 4) source (140, 19)–(152, 24) at (137, 40).

#### 2.6 Countdown (element 5)
While the last svc_countdown value (SPEC-201 §31) is non-zero: text `Countdown : %d` (below 60 s) or
`Countdown : %d:%02d`, font `title`, HUD green, at (trunc(W/2) − 300, trunc(H/2) − 78) (fixed pixel offsets,
not scaled; 640×480: (20, 162)).

#### 2.7 Mission status messages (element 7; always tested)
The first matching row of stats[8] is shown; each text is centred horizontally (x = (W − text width) / 2):
| stats[8] bit | Text (`singleplr` package id 6) | Font, colour | Vertical position (L = font height) |
|---|---|---|---|
| 0x0008 | 6 "Mission Failed!" and 7 "Too many friendly casualties" | `title`, red | (H − L)/2 − 18 and (H − L)/2 + 22 |
| 0x0080 | 8 "Mission Accomplished!" | `title`, red | (H − L)/2 − 8 |
| 0x0100 | 10 "Prepare to exit mission..." | `title`, red | (H − L)/2 − 8 |
| 0x0040 | 6 "Mission Failed!" | `title`, red | (H − L)/2 − 18 |
| 0x0400 | 9 "MISSION OBJECTIVES UPDATED" | `medium`, blinking | 5 L − 8 |
Blinking: red when (realtime mod 1024) / 512 > 1, fully transparent otherwise.

#### 2.8 Deathmatch rank / spectator (element 8)
Drawn only when stats[9] bit 0x2 is set. Anchor (ax, ay) = (scaled x(600), scaled y(20)); items are centred on ax
and stacked downwards from ay, each picture advancing by its height + 6.
- Spectator (pm_type 2 or 7): `pics/interface2/eye` (scaled pic, HUD green). If a player is spectated (field 31
  non-zero, an entity number): when configstring 33 is a non-zero number, that player's team icon (in team modes
  tinted with the local player's team colour, else HUD green); then the spectated player's name, font `small`,
  same colour, at y = (next stack position − 8).
- Playing: when configstring 33 is non-zero, the local player's team icon (team colour in team modes, else HUD
  green); then stats[10] (score) as `%d`, font `title`, colour as before with alpha 215, at stack y − 8; then
  `rank/count` (`%d/%d` of player-state field 30), font `title`, alpha 255, 18 px lower.
Team icon = `ghoul/pmodels/teamicons/<team>`; when a player's team name equals its model name the icon is
`ghoul/pmodels/teamicons/noteam`; a missing picture means no icon.

#### 2.9 CTF flags (element 9)
Positions P1 = (scaled x(20), scaled y(20)), P2 = (scaled x(20), scaled y(65)); each picture 48×48, white.
Configstring 8 = blue flag state, 9 = red flag state. For a blue-team local player (team digit `0`): if CS 8 is
`d` (any case) → `pics/interface2/ctfb_hudflag_d` at P1; else if CS 8 starts with `r` → `ctfb_hudflag_r` at P1;
then if CS 9 starts with `r` and its second byte equals the local player's entity number (slot + 1) →
`ctfr_hudflag` at P2. A red-team player sees the mirror case (CS 9 with `ctfr_hudflag_d` / `ctfr_hudflag_r` at P1,
CS 8 with `ctfb_hudflag` at P2). So P1 shows the own flag when dropped or taken, P2 the enemy flag when the local
player carries it. UNVERIFIED: that `r` means "taken"; observe CS 8/9 while a flag is carried.

#### 2.10 Control flags (element 10)
Positions: blue (scaled x(10), scaled y(328)), red (scaled x(590), scaled y(328)), list origin (scaled x(10),
scaled y(40)). Draws `pics/interface2/ctfb_hudflag` at the blue position with configstring 8 as a coloured string
at (+12, +48), and `ctfr_hudflag` at the red position with configstring 9 likewise (both white, 48×48). Then for
configstrings 3545..3549 in order, each non-empty one occupies a row (rows advance 60 px from the list origin):
first character `G` → `pics/interface2/control_flag`, `R` → `ctfr_hudflag`, `B` → `ctfb_hudflag` (48×48), and the
rest of the string, if any, as a coloured string at (+0, +48).

#### 2.11 Debug target text (element 6)
When stats[8] bit 0x2 is set and bit 0x1 is clear: `Current target:` in font `small`, white, at (x, y − 448)
(640×480: (320, 0)). The game never sets bit 0x2 in 1.07f (SPEC-780), so it is normally invisible.

### 3. Crosshair
Picture `pics/menus/status/ch<n>` for n = `crosshair` (re-read when the cvar changes; a missing picture disables
it; the pictures 1–4 are registered at level load). Drawn centred in the view rectangle when a crosshair is
set, stats[8] bit 0x800 is clear, stats[6] ≠ 0 and the remote view type is not 2, 4 or 16. When stats[8] bit 0x10
(target identification) is set it is tinted red, or green when bit 0x20 (friendly) is also set; otherwise it is
drawn untinted.

### 4. Overhead player labels
Recomputed every frame for each client slot below configstring 31 (maxclients) whose player-info configstring is
not empty, except the local player. A label is visible when all hold:
1. the point P = player origin with z at the middle of its bounding box (SPEC-202 solid encoding:
   z + (zu − zd − 128)/2) is at most 1024 units from the view origin;
2. P lies within the cone of half-angle fov/2 around the view direction (dot product ≥ cos(fov/2));
3. P is in the PVS of the view origin;
4. if the player's last lighting value (largest of its three colour components; UNVERIFIED source: the
   renderer's lighting of that entity) is below 0.2 ("in darkness"): when configstring 33 is non-zero the label
   is shown only for team-mates (`deathmatch` 4 or 6: same team digit; otherwise same team name), else only
   when configstring 34 is non-zero;
5. a box trace (−1…1) from the view origin to P against all entities first hits that player.
Visible labels are drawn with SPEC-002 slot 0xac at the player's interpolated origin raised by (top of its box
+ 2): name text when `shownames` ≠ 0 or configstring 32 is non-zero; picture = the player's team icon when
configstring 33 is non-zero and the player has one, else the deathmatch icon chosen by svc_playernamecols for
that slot (value v ≥ 1 → configstring CS_DMICONS + v, picture `pics/dmicons/<name>`), never while the local player
is a spectator. Colour (only computed when the team icon is used): team modes → that player's team colour;
otherwise, if the local player is a spectator (pm_type 2 or 7) → green, else green when its team name equals the
local player's and red when not. A label without team icon reuses the colour of the previously drawn label (white
for the first one; UNVERIFIED in game — this looks unintended). Labels are not drawn while stats[8] bit 0x800 is set.

### 5. Scoreboard layout language
The layout program is the client's layout text (svc_layout, or string-package prints with the layout flag —
SPEC-201 §34). It is executed every frame while stats[9] bit 0x1 is set. Tokens are read with Quake II token
rules; unknown tokens are skipped; every execution starts with x = y = 0, text colour white, alternate colour grey.
| Command | Arguments | Effect |
|---|---|---|
| `xl` / `xr` / `xv` | n | x = n; x = W + n; x = W/2 − 160 + n |
| `yt` / `yb` / `yv` | n | y = n; y = H + n; y = H/2 − 120 + n |
| `picn` | name | pic `name` (as given) at (x, y), white |
| `string` | text | coloured string at (x, y) in the text colour |
| `altstring` | text | each character with bit 7 flipped, drawn with SPEC-002 slot 0x58 at x + 8 i, alternate colour |
| `tc` / `ac` | n | text / alternate colour := colour table[n & 15] |
| `client_sb` | x y client score ping time | player row (below) |
| `client_ctf_sb` | x y client score ping time flag | player row plus flag |
| `spect_sb` | x y client ping time | spectator row |
| `team_sb` | x y client score | team row |
| `client_conq_sb` | x y team score | conquer team row |
| `ctf_team_sb` | x y team score captures | CTF team row |
| `control_team_sb` | x y team score points | control team row |
Row commands take their own position: X = W/2 − 160 + x, Y = H/2 − 120 + y (they also set the current position).
A client number outside 0..31 aborts with "client >= MAX_CLIENTS". Strings come from package `dm_generic` (id 0):
62 "Score: ", 63 "Ping:   ", 64 "Time:   ", 65 "Spectator", 66 "Flag Captures: ", 67 "Control Score:"; labels in grey,
numbers in white unless stated; portrait = the player's `ghoul/pmodels/portraits/<model>` picture (the default
player's portrait when it has none), drawn as a pic at (X, Y), white.
- Player row (`client_sb`, `client_ctf_sb`): name at (X+32, Y) — green when it is the local player or the local
  `spectator` cvar is non-zero; `client_sb` otherwise: in mode 1 with configstring 33 non-zero and the player having a
  team icon → green for same team name, red otherwise; in other modes green for the same team digit when
  configstring 33 is non-zero; red otherwise; `client_ctf_sb`: green for the same team digit, else red. Then "Score: "
  at (X+32, Y+8), ` <score>` at (X+88, Y+8), "Ping:   <ping>" at (X+32, Y+16) and "Time:   <time>" at (X+32, Y+24)
  (label and number concatenated, grey), the portrait. `client_ctf_sb` with flag 1 draws `ctfb_hudflag` 32×32 at
  (X−32, Y), with any other non-zero flag `ctfr_hudflag` at (X−35, Y).
- `spect_sb`: name (green) at (X+32, Y), "Spectator" (white) at (X+32, Y+8), ping and time lines as above, portrait.
- `team_sb`: "Score: " at (X+32, Y+12), `%i` at (X+88, Y+12); picture: the player's team icon tinted green (same
  team name as the local player) or red; without a team icon the portrait.
- `client_conq_sb`, `ctf_team_sb`, `control_team_sb`: flag picture 32×32 at (X, Y), white: team 0 `ctfb_hudflag`,
  otherwise `ctfr_hudflag`. Conquer: "Score: " (X+32, Y+12), value (X+88, Y+12). CTF: same, plus "Flag Captures: " at
  (X+32, Y+22) and captures at (X+150, Y+22). Control: "Score: " at (X+32, Y+22) and score at (X+88, Y+22),
  "Control Score:" at (X+32, Y+12) and points at (X+150, Y+12).
Colour table (index: colour): 0 black, 1 (0,255,0), 2 (0,170,0), 3 (0,102,0), 4 (240,240,0), 5 (255,0,0), 6–15 white
(all alpha 255).
Quake II commands absent: `pic`, `client`, `ctf`, `tag`, `num`, `hnum`, `anum`, `rnum`, `stat_string`, `cstring`,
`cstring2`, `string2`, `if`, `endif`.

### 6. Prints on screen
**Centre print** (svc_centerprint, string-package flag "centered", welcome message) and **caption** (flag
"captioned"): the text is also echoed to the console between separator lines; lines are broken at the last space
before W/8 − 34 characters (W/8 − 4 while the remote view type is 2), at most 10 lines; display time =
`scr_centertime` + 0.2 s per space + 0.8 s per line. Drawn in font `medium`, yellow, each line centred
horizontally, line height L = font height, line i at y0 + i L with y0 = trunc(0.4 H − trunc(n/2) L) for centre
prints, y0 = trunc(0.9 H − n L) for captions, and y0 = H − trunc(48 H / 480) + 4 for captions while the remote view
type is 2. Lines after the first continue the previous colour (SPEC-002 "keep colour"). A null text clears it.

**Typewriter print** (svc_cinprintf, flag "typeamatic"): stores x, y, speed s and up to 1023 characters; display
time = length × 0.1 s + `cinem_centertime`. Drawn only while the remote view type is 2: the speed is clamped to
1..10, one character appears every max(1, 10 − s) × 10 ms of client time from the first drawn frame; characters
8 px wide in colour (0, 254, 0, 220), lines (split at newlines, at most 104 characters) 8 px apart, x always 10,
first line at y = trunc(H − y × H / 640) (the x argument is ignored; UNVERIFIED that this is intended). While
characters are still appearing, `misc/type.wav` plays (local sound) each time a new character appears and an
underscore cursor follows the last character.

**Credit picture** (flag "credit"): the text is `xp;yp;time;fadein;fadeout;r;g;b;name`. The picture
`pics/<name>` is drawn as a pic at (trunc((W − w) × xp/100), trunc((H − h) × yp/100)) in colour (r, g, b, α) for
`time` seconds; α = 255 × elapsed / fadein during the first `fadein` seconds, 255 × remaining / fadeout during
the last `fadeout` seconds, else 255. All credit pictures referenced by loaded string packages are registered
at level load. svc_force_con_notify clears all three timers (SPEC-201 §40).

### 7. Pause screen
When `scr_showpause` ≠ 0 and `paused` ≠ 0: full-screen fade (SPEC-002 slot 0x84, colour (0,0,0,127)), then
string `general` (id 4) entry 8 in font `title`, yellow, centred on the screen.

### 8. Remote-view overlays
Type 2: two black (0,0,0,255) bars, full width, height trunc(48 H / 480), at the top and the bottom.
Type 4: `pics/monitor/monitor` stretched over the four screen quadrants, mirrored horizontally/vertically
so the picture is the top-left quarter of a symmetric frame. Type 16: the scope element (§2.3).

### 9. Loading screen
- Begin (map change `changing`, connect, `loading` command, server map start): menus are closed, all sounds and
  CD audio stop; then — unless the screen is disabled, `dedicated` or `developer` is non-zero, or the console has
  the keyboard — the menu `loading` is opened at once (SPEC-801 `menu loading`) and one frame is drawn.
- Progress: cvar `menu_loading` (0..1) is set and a frame drawn, unless `dedicated` ≠ 0: connect / `changing`
  0.33; each configstring with index < 1415 received while loading: 0.33 + 0.33 × index / 1415; level
  registration: 0.70 (before the world model), 0.75, 0.775, 0.85, 0.875, 0.90, 0.925, 0.95 (player infos done),
  1.0. The game module receives the same progress function as an import (SPEC-001).
- End: menus closed, console notify lines cleared.
- A demo start (`demomap`) sets `menu_nostats` to 1 before loading (SPEC-311).

### 10. Debug output
- `cl_showfps` ≠ 0: prints `Frames Per Second = %f` with the 10-frame average (every frame when the value is 2).
- `cl_showlocation` ≠ 0: prints `Location: %.3f %.3f %.3f (Dir: %.3f)` (entity 1's origin and yaw) every frame.
- `cl_stats` ≠ 0: prints `ent %i lt %i alphaquads %i quadchains %i (quads %i)` per rendered frame; `log_stats`
  writes the same five numbers to the stats log file.

### 11. Debug graphs
A ring of 1024 samples, each up to 5 stacked (value, colour) segments. Samples are added by: `timegraph` (each
screen update: frame time × 300, colour (255,0,0,127)); the network graph (each server message while
`debuggraph`, `timegraph` are 0 and `host_speeds` ≠ 2: one sample of 30 per dropped packet in colour
(64,0,0,127), one of 30 per suppressed packet in colour (223,0,0,127), then the ping in 30 ms units capped at 30,
colour (208,0,0,127)); `host_speeds` 2 (per frame: server, client, game, renderer milliseconds as 4 segments red,
green, blue, magenta with alpha 159) and 3 (game time averaged over 10 frames, blue; also printed `gm %3i`).
Drawn when `debuggraph`, `timegraph`, `netgraph` is non-zero or `host_speeds` is 2: n = min(`graphsize`, view width)
samples, newest at the right, each `graphwidth` wide, centred horizontally in the view rectangle and standing on its
bottom edge; a segment value v becomes v × `graphscale` + `graphshift`, wrapped into 0..`graphheight` like Quake II,
and is drawn as a vertical line (SPEC-002 slot 0x7c) of width `graphwidth`, segments stacked upwards.

### 12. View setup (client part)
Per rendered frame, after entities are added (Quake II CL_CalcViewValues with these differences):
1. Interpolation fraction, view origin (with Quake II stair smoothing and prediction-error decay; prediction is
   off when `cl_predict` = 0 or pmove flag 0x80 is set), angles (predicted for pm_type 0–2, interpolated otherwise),
   kick angles: as Quake II. During demo playback (`timedemo` ≠ 0 too) the fraction is 1.
2. Weapon kick: interpolated from the player state; with client weapon prediction active the predicted kick is
   added instead. The predicted kick is resampled like the network would deliver it: every 0.1 s of game time the
   accumulated kick (each component truncated to a multiple of 0.25 after ×4 rounding toward zero) becomes the
   new target, and the value used is old + (target − old) × (time since resample) × 10.
3. Camera shake (SPEC-400).
4. Night-vision jitter: when configstring 15 equals 1, fov is exactly 95 and rdflags bit 0x4 is set, the view
   origin z gets a random offset in [−0.5, 0.5] each frame.
5. Remote camera: when the remote view type is non-zero, the camera origin (stored in 1/8 units) and angles are
   interpolated between the previous and current frame; a change of type or entity snaps (no interpolation).
6. Field of view: interpolated between frames only when the remote view type is 16, or 0 in both frames; with
   weapon prediction active (and type ≠ 16) the predicted value is used; the zoom sound rule of SPEC-400 runs on the
   change. Otherwise the current value is used directly. A field of view outside 1..179 aborts ("Bad fov: %f").
7. Screen blend from the player state; `cl_testblend` forces (1, 0.5, 0.25, 0.5); `cl_blend` = 0 clears RGB; a
   white-out amount Wo (0 at start; UNVERIFIED who sets it — nothing in the executable does) moves every component
   toward 1 by Wo while Wo > 0.02 and decreases by 0.2 per second.
8. Near clip: whenever a player state with the pm_type field is parsed, `r_nearclipdist` becomes 1 if the
   pm_type of the frame shown before it was 2 or 7, else 4 (note: the previous frame's value; UNVERIFIED whether
   this one-frame lag is visible).
9. Cinematic freeze: each parsed value is copied to `cl_cinematicfreeze`; values ≥ 2 stop screen updates (§1);
   input effects are in SPEC-312.
10. Frame time: the client frame time is clamped to at most 1/`cl_minfps`; a smoothed rate
   f = 0.9 f + 0.1 × max(1/frame time, `cl_minfps`) is kept (at least `cl_minfps`) and handed to the renderer as the
   first refdef value; the effect detail factor is min(1, f / `sc_framerate`) (used by SPEC-400).
11. Quake II's view-origin nudge (+1/16 on each axis), `cl_testlights`, refdef time = client time / 1000 are kept.

### 13. View weapon
Drawn when `cl_gun` ≠ 0, the field of view is at most 95, the remote view type is 0, and a view-weapon instance
exists (player-state field 1 or the predicted one). Entity: origin = view origin, angles = view angles with pitch
and roll negated, render flags 0x400015 (ghoul model, minimum light, weapon model, depth hack), back-lerp =
1 − interpolation fraction. Before it is added, a client-side sway transform is applied to the model:
1. Each frame: origin p = interpolated player origin; horizontal speed s = |(p.xy − previous p.xy)| / frame time,
   capped at 300; vertical speed vz = (p.z − previous p.z) / frame time.
2. If s < frame time × S × 200 (S = previous smoothed speed) then s = (3 S + s) / 4. S := s.
3. Phase φ += sqrt(s / 300) × 5.1 × frame time; amplitude A = s × 0.0046667, halved when the view-weapon type value
   (field 1, second value) is 3 or 4.
4. Landing dip: when |vz| < 0.0005 and the previous vz < −30, target T = max(−1.3, (|previous vz| − 30) × −0.0216667).
   While T ≠ 0 the dip offset d decreases by 12 × frame time until it reaches T (then d = T and T := 0); while T = 0,
   d < 0 rises by 4 × frame time up to 0.
5. The model's own transform (re-captured whenever something else changed it since the last frame) is multiplied
   by a translation (0, 0.9 A sin φ, |0.4 A sin φ| + d) in model space and stored back on the instance.
All state is reset at level start. View-weapon effects and sounds: SPEC-400.

### 14. Client weapon-prediction hooks (in addition to SPEC-003)
- Every key binding command executed on a key press is also remembered; on the next predicted weapon frame while
  pm_type < 2 its first word is passed to the player module's command hook (SPEC-003 slot 0x10), then forgotten.
  This lets `weaponselect` and similar bound commands act on the predicted inventory immediately.
- Multiplayer cheat protection (Quake II CL_FixCvarCheats, list changed): when configstring 31 is neither `1` nor
  empty, these cvars are forced every frame: `timescale` 1, `timedemo` 0, `freezeworld` 0, `r_drawworld` 1,
  `cl_testlights` 0, `r_fullbright` 0, `gl_drawflat` 0, `paused` 0, `fixedtime` 0, `gl_showtris` 0,
  `gl_drawmode` 0, `gl_fogcolor` −1.

### 15. Client commands
| Command | Behaviour |
|---|---|
| `ignore <name or slot>` / `unignore <…>` | exactly one argument, else prints `ignore <client name> or <client id number>`. A number is a slot 0..31 (`Bad client slot: %i`; an empty slot gives `Client %i is not active`); a name is matched case-insensitively against player names (no match: no change). Ignored senders' svc_nameprint chat is dropped. Prints `Client %i named %s is now ignored` / `... is now free to message you`. The list is cleared on disconnect. |
| `listignore` | prints `Client id %i named %s is ignored.` per entry (entries of empty slots are dropped), or `No one is Ignored` |
| `addfave <address>` | stores the address in the first empty archived cvar `adr0`..`adr19`; `Favourite already in list.`, `Server at %s has been added to list.`, `No free slots to add favourite %s.`; usage `Usage: addfave <ip>` |
| `remfave <address>` | clears the matching `adrN` (case-insensitive): `Favourite %s removed from list.` or `Could not find %s to remove.`; usage `Usage: remfave <ip>` |
| `terrain <name>` | passes the name to the renderer's terrain-set slot (SPEC-002 0x38); usage `Usage: terrain <terrainFileName>` |
| `cloud <type> <density> <angle> <speed> <r> <g> <b>` | passes the 7 numbers to SPEC-002 slot 0x34 (a stub in ref_gl); fewer than 3 arguments prints the usage but still calls it |
| `cl_touch` | re-touches all renderer textures and sounds so they survive the next registration cycle |

### 16. Screenshots and save thumbnails
Screenshots (`screenshot`, `rawshot`, `avi`) are renderer commands (SPEC-002). For single-player saves the client
keeps a 128 × 128 RGBA thumbnail: when a save is made while the game has the keyboard it is captured through
SPEC-002 slot 0xd4; otherwise, if none exists, a black (all-zero) image is used (SPEC-303 thumbnail chunk). Only
when maxclients is 1 and a server is running.

## Constants
| Name | Value |
|---|---|
| HUD reference resolution | 640 × 480 |
| Big digit advance / non-digit advance | 18 / 16 px |
| Panel tray: open/close speed, hold time, travel | 3 rad/s, 2.5 s, 27 px (cos law) |
| Low-ammunition blink period | 1024 ms (red for the first 513 ms) |
| Health warning threshold / blink period | 0.25 / 800 ms (lit 266.7 ms, fading 266.7 ms, dark 266.7 ms) |
| Objectives blink period | 1024 ms |
| Countdown text offset from centre | (−300, −78) px |
| Letterbox bar height | 48/480 of the screen height |
| Overhead label range / darkness threshold / trace box | 1024 units / 0.2 / ±1 |
| Typewriter: char size, line length, colour | 8 px, 104 chars, (0, 254, 0, 220) |
| Centre print: wrap, max lines, time increments | W/8 − 34 chars, 10, +0.2 s/space +0.8 s/line |
| Debug graph ring | 1024 samples × 5 segments |
| View weapon sway | speed cap 300; phase rate 5.1 √(s/300) rad/s; amplitude s × 0.0046667; lateral × 0.9, vertical |× 0.4|; dip −0.0216667 per unit/s over 30, min −1.3, fall 12/s, recover 4/s |
| Near clip | 1 (spectator views), 4 (otherwise) |
| Network icon threshold | 62 unacknowledged packets |

## Test vectors / how to verify
1. 640×480, deathmatch, holding the shotgun with 3 shells loaded and 1 in reserve: weapon panel at (510, 385);
   big digits " 3" with `no_blank` at x 575 and `no_3` at x 593, y 427; reserve count "1" at (541, 448) blinking
   red/green once per second; label `SHOTGUN` slides up and back within ~3 s after switching to it.
2. Health 20: the warning light at (252, 447) pulses every 0.8 s; health bar right edge at source column 26.
3. Scope at fov 60: text `x 06` at (20, 162); at fov 5: `x 21`; the four `arrows128` pieces cycle as the zoom moves.
4. Set `scr_interface 0`: all HUD elements disappear, crosshair and labels stay.
5. A layout `xv 0 yv 0 client_sb 0 0 0 15 50 3` shows the player's portrait at (160, 120) with "Score:" at (192, 128)
   and " 15" at (248, 128) at 640×480.
6. `crosshair 7` → value becomes 4 (`pics/menus/status/ch4`).
7. `addfave 1.2.3.4` twice → second prints `Favourite already in list.`; `adr0` holds `1.2.3.4`.
8. With `developer 1`, map loads show no loading menu (stays on the console/last frame).
9. svc_cinprintf at y 104 at 640×480 is only visible during a letterbox camera; first line baseline y = 480 − 78 = 402.

## Open questions
- Which game entities produce remote view types 2, 4 and 16 (cinematic, monitor, scope camera) — check the game
  module's camera entities.
- Source of the white-out amount (§12.7): possibly a player-module hook; not set by SoF.exe.
- Meaning of configstring 8/9 values starting with `r` (assumed "flag taken"; second byte = carrier).
- The lighting value used for the darkness rule of player labels.
- Whether the HUD positions are recomputed after `vid_restart` without a level change (they are computed at level
  registration).

## Notes for other specs (found while writing this one)
- SPEC-201 §33 (svc_playernamecols): the value is not a colour but a deathmatch-icon index: v ≥ 1 selects
  configstring CS_DMICONS + v (`pics/dmicons/<name>`, up to 15) drawn above that player's head; 0 = none.
- SPEC-203 configstrings: 8/9 = CTF/control flag states read by the HUD (§2.9, §2.10); 10 = team-colour switch for
  mode 7; 15 = screen effect (1 enables night-vision jitter); 32 = force player names; 33 = team icons enabled;
  34 = labels visible in darkness; 3545–3549 = control-point rows (first character G/R/B + text).
- SPEC-203 §3 (download "files it references"): ambient set → `sound/<wave>` for every wave name in the set;
  music set → both file names of every entry; string package → `pics/<name>.m32` for every credit-picture string;
  terrain set → its texture names with `.m32` appended (unless already ending in `.m32`); sky → the six names
  listed in `pics/sky/<name>.txt` with `.m32` appended (`none` = no image). A downloaded `.gpm` player model is
  appended as a line to `<user dir>/<sv_altpmodlistfile>.gpl` (file created if missing).
- SPEC-202 field 1: the second value (weapon "type") 3 or 4 halves the view-weapon sway amplitude.
