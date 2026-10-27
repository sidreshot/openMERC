# SPEC-740: Navigation runtime and node editor (single-player AI)
Status: approved (G, 2026-10-22) — produced by the reverse team, reviewed against CLEANROOM.md rules; in-game test vectors not yet verified
Describes: gamex86.dll, version 1.07f
Depends on: SPEC-143 (`.nvb` layout), SPEC-700 (character record, character manager, bottom check, sight client),
SPEC-710 (decisions that issue the queries below), SPEC-761 (world spawner), SPEC-760 (level spawn order,
difficulty record), SPEC-401 (temp entities, debug lines), SPEC-101 (traces)

## Purpose

How the game module uses the `.nvb` data of SPEC-143 while a level runs: loading, locating a point on the
walkability grid, the per-character location cache, the path queries the AI decisions make ("approach", "flee",
flee spot, look-around node, hidden spawn spot), the "seen by the player" flood, and the step test that accepts or
rejects a move point. A second part describes the developer node editor that produced the `.nvb` files
(console commands, the `.nav` source file, and the export pipeline including the exact routing-table rule, which
reproduces every stock file bit for bit).

Multiplayer bots do not use any of this; they use `.wrs` waypoints (SPEC-741).

## Interface / layout

### Files and cvars

| Item | Value |
|---|---|
| run-time file | `maps/<mapname>.nvb`, `<mapname>` = the level's map name as given to `map` (so `dm/jpndm2` → `maps/dm/jpndm2.nvb`) |
| editor source file | `<ai_editdir>/base/maps/<mapname>.nav` (read and written with C stdio, not the game filesystem) |
| editor export | `<ai_editdir>/base/maps/<mapname>.nvb` |
| cvar `ai_nonodes` (default 0) | ≠ 0: every query below behaves as if no `.nvb` were loaded, and no cache is refreshed |
| cvar `aipoints_show` (default 0) | ≠ 0: each successful move-point query draws a debug line from the character to the move point and a shock ring (effect size 6) at the move point |
| cvar `ai_editdir` (default `p:`) | editor directory prefix (development drive) |

### Amendments to SPEC-143 (`.nvb` layout as the loader reads it)

- A node record is *flags, index, origin* (20 bytes) followed by the body only when flags has at least one of the
  bits 1, 4, 0x8000, 0x10000. All stock nodes have one of them, so all stock records have a body.
- The body is: link count k, k links, next_link[N], rank[N], **a second link count k2 and k2 links (12 bytes
  each, same layout as links)**, then tail1…tail6. SPEC-143's "tail0, always 0" is k2 (0 in every stock file).
- After the N node records comes **a second node count M and M node records** (same record layout, routing tables
  sized by M), then the grid. SPEC-143's grid header "0, L, C" is therefore M, L, C. M is 0 in every stock file.
  The second node set ("combat points": the editor's lean/duck/cover points) and the second link lists are
  loaded but never used at run time.
- Version: only the low byte of the first int is compared with 40.
- On load every node's origin z is raised by 14 units (all queries below use the raised origin).
- tail4 is the editor's *ignore* flag (command `toggleignore`): a node with tail4 ≠ 0 is never used as a world-spawn
  spot (see "Hidden spawn spot"). tail1 is the node's *clearance radius* (see "Export pipeline").
- At run time only these grid fields are used: layer floor z, layer id and the 12-entry node list. Layer flags,
  `top` and neighbour ids are loaded but unused (their generation rules are under "Export pipeline").

### Per-character navigation cache (96 bytes; SPEC-700 record offset 292)

| Offset | Type | Meaning |
|---|---|---|
| 0 | float[3] | reference position: written by the cache refresh (character origin with z + 16) and by every move-point query (the query's "from" point) |
| 12 | float[3] | move point: result of the last move-point query |
| 24 | int | current node = first node listed by the current layer; 0 = none |
| 28 | int | current layer id; −1 = none |
| 32 | int | node chosen by the last query; 0 = none |
| 36 | int | goal node of the last query (first node of the goal's layer) |
| 40 | int | **direct flag**: 1 = the last query produced no navigation point (move point = "from"); 0 = move point valid |
| 44 | int[3] | node history: the three previous current-node values, most recent first |
| 56 | 8 bytes | unused |
| 64 | int[4] | four copies of the current layer id, written by every refresh (SPEC-700: default −1) |
| 80 | float[3] | last accepted move direction (unit vector) |
| 92 | float | level time at which that direction was accepted |

### Level-wide navigation state (not saved)

- node array (N), second set (M), grid;
- *player node* and *player layer*: written every frame (below); the player node is also the root of the
  hidden-spawn search;
- one *seen* byte per node (1024 entries; nodes ≥ 1024 are not supported), cleared when `worldspawn` spawns, set by
  the seen flood and never cleared during the level;
- a per-node stamp (float) and a level-wide search counter, used by the floods to mark visited nodes;
- 2-bit *occupancy marks* per grid layer and the ids of the four layers around the player: written every frame,
  never read by any 1.07f code.

## Behaviour

### 1. Loading and freeing

- At level spawn ("reset navigation", SPEC-760 B1.3 step 3) the previous data is freed and
  `maps/<mapname>.nvb` is loaded through the game filesystem.
- File missing: no navigation (N = 0), silently.
- Low byte of the version ≠ 40: `Invalid version (%d) for Info Point File (Should be %d) - please re-export the path data`
  (first %d = the low byte as a signed char, second = 40) and no navigation.
- No validation of the content is done (node indices, link targets and table sizes are trusted).
- Data is freed again at game shutdown. "No navigation" makes every query below return its fallback.

### 2. Grid lookup (point → layer)

Columns are sorted by x, cells by y, layers by floor z (all ascending, as written by the exporter).
A point P is looked up in three steps:
1. column: one with x ≤ P.x ≤ x + 48; cell inside it: one with y ≤ P.y ≤ y + 48. Both are found by a binary
   search: start at index ⌊n/2⌋ with step s = 2^(b−2), where b is the bit length of n (n = number of columns or
   cells); while the entry does not contain P, move by s towards P (clamped to 0…n−1), halve s, and give up after a
   move made with s = 0. On an exact boundary (P.x = x_i + 48 = x_{i+1}) the entry visited first wins.
2. layer: the **highest** layer of the cell whose floor z is strictly below P.z.
3. no column, cell or layer → no layer.

### 3. Per-frame player update (every frame while a sight client exists; SPEC-700 character manager)

Skipped when `ai_nonodes` ≠ 0 or N = 0. Otherwise:
1. clear all occupancy marks;
2. look up the sight client's origin (no offset). If a layer is found: player layer = its id; player node = its first
   node, replaced by 0 when < 1 or ≥ N. If none is found both keep their previous values;
3. look up the four points (x ∓ 16, y ∓ 16, z) and mark those layers (no gameplay effect);
4. **seen flood**: increment the search counter, then flood from the player node: a node that is not yet seen and whose
   origin is in the PVS of the sight client's origin becomes seen; the node is stamped; if the node is seen (now or
   earlier), the flood continues to every link target not yet stamped in this flood. The flood therefore spreads
   through chains of seen nodes; nodes that are not in the PVS and were never seen stop it. Seen flags are sticky
   for the rest of the level.

### 4. Per-character cache refresh (each awake character, every frame; SPEC-700 §17)

Skipped when `ai_nonodes` ≠ 0 or N = 0. With O = the character's origin:
1. reference position ← (O.x, O.y, O.z + 16); layer ← lookup of that point (−1 if none); a found layer gets an
   occupancy mark;
2. if no layer was found, the lookup is retried at (O.x − 16, O.y − 16, O.z + 16), then (O.x + 32, O.y, O.z + 16),
   (O.x + 32, O.y + 32, O.z + 16), (O.x, O.y + 32, O.z + 16), stopping at the first hit (these offsets are as the
   original computes them; they are not symmetric);
3. layer found: node ← the layer's first node; if it differs from the stored current node, the history shifts
   (history[2] ← history[1] ← history[0] ← old current node) and the current node is replaced;
   no layer: current node ← 0 (history unchanged);
4. the four layer-id copies ← layer.

### 5. Node choice on a layer

Given a layer L (by id), a target node T and a mode, the candidates are L's listed nodes (up to 12, the list ends
at the first −1). The ranking value of a candidate c is rank_T[c] — the position of c in **T's** rank table (T's
ordering of all nodes by path cost from T, SPEC-143).
- approach mode: the candidate with the **smallest** rank_T ≥ 0 (unreachable candidates, 16000, are allowed but
  lose to any reachable one); on equal values the later candidate in the list wins;
- flee mode: the candidate with the **largest** rank_T below 15000 (unreachable ones excluded); ties → later
  candidate;
- no candidate → none. A layer id of −1 → none.

### 6. Move-point query

Inputs: *from* F (the character's origin), *goal* G, the character's cache, the character, its entity, the mode
(approach or flee) and an *ignore* entity (the character's target; traces that hit it do not count as blocked).
Output: the cache's move point, chosen node, goal node and direct flag. "Direct" below means: move point ← F,
direct flag ← 1.

1. `ai_nonodes` ≠ 0, N = 0, or cache current node = 0 → direct; stop (nothing else changes).
2. Goal layer = lookup of G. None → chosen node ← 0, direct; stop. Goal node ← first node of the goal layer; direct
   flag ← 0. If the goal layer lists no node (first entry −1) → chosen node ← 0, direct; stop.
3. Chosen node C ← node choice (rule 5) on the **character's current layer** towards the goal node, in the query's
   mode; values < 1 become 0. C is kept only if it can lead to the goal: C has a routing table and either C is the
   goal node or C's next_link entry for the goal node is non-zero and names a node ≥ 1; otherwise C ← 0.
   (If C has no routing table the console prints `Error!  Problem with path!`; with stock data this cannot happen.)
4. C = 0 → direct, then continue with step 7.
5. Target point X:
   - approach mode and the goal layer is the character's current layer → X = G, and continue with step 7
     (no obstacle probe);
   - approach mode and C = the goal node → X = C's origin if (G − E)·(C − E) ≥ 0, else X = G
     (E = entity origin): head for the node unless it lies behind;
   - flee mode and C = the character's current node → X = C's origin if (G − E)·(C − E) ≤ 0, else X = F
     (stay: the only node is on the threat's side);
   - otherwise X = C's origin.
6. Obstacle probe. Box = the entity's box shrunk by 1 unit in x and y, with its bottom raised by 10 units; d = unit
   vector from E to X. Trace from E to E + 64·d, mask 0x2020003.
   - *clear* (fraction > 0.99, or the hit entity is the ignore entity; and neither start- nor all-solid) →
     move point ← X;
   - blocked by a door-like entity (SPEC-710) in approach mode: if the door is usable now → move point ← X and the
     step test of step 7 is skipped; otherwise move point ← trace end + 64 · hit surface normal (back off from the
     door);
   - blocked otherwise: with f and r the forward and right vectors of d's angles, two probes are traced from E with
     the same box to E + 64·f + 64·r (right) and E + 64·f − 64·r (left). A side is *open* when its fraction > 0.1 and
     it does not start or end in solid, or (approach mode) it hit a door-like entity usable now. With a = the right
     fraction and b = the left fraction:
     - neither side open → move point ← F;
     - only the left open, or both open with b ≥ a → move point ← E + 32·b·f − (64 − 32·b)·r;
     - otherwise (right open) → move point ← E + 32·a·f + (64 − 32·a)·r.
7. Finish (also after a direct result):
   - reference position ← F;
   - m = move point − F. If |m|² ≥ 0.1: with u = m/|m|, if u·(last direction) ≥ −0.717 or at least 3.0 s have passed
     since the last direction was accepted, the direction is accepted (last direction ← u, time ← now); otherwise
     the query is treated as direct (move point ← F, direct flag ← 1): a reversal sharper than about 136° within 3 s
     is refused. If |m|² < 0.1 the (tiny) direction is accepted with the current time.
   - Unless the door case of step 6 skipped it, the **step test** (rule 9) is run with the move vector
     25 · unit(move point − F); if it fails → move point ← F, direct flag ← 1.
   - With `aipoints_show` ≠ 0 and C ≠ 0: debug line F → move point and a ring at the move point.

The caller uses the move point when the direct flag is 0, and otherwise falls back as described in SPEC-710
(typically walking straight at its own destination).

The **flee** query is the same query in flee mode, with G = the position to flee from.

### 7. Flee spot (per-node precomputed flee node)

Input: the character's cache and a team value (SPEC-710 decides who uses it).
- `ai_nonodes` ≠ 0 → returns "none" without writing the output.
- current node −1 or 0 → "none".
- team 0 → candidate = the current node's tail2; other teams → tail3, or tail2 when tail3 = 0.
- candidate ≠ 0 → output = that node's origin, "found". Candidate 0 → output = the cache reference position,
  "none".

### 8. Look-around node (search decision, SPEC-710 C6)

Input: a point P and an integer n. Lookup of P; the layer's first node A (must be ≥ 1 and have ≥ 1 link); the
link with index n mod k (k = A's link count, links in file order); its target B (must be ≥ 1) → output B's origin.
Any failure → "none".

### 9. Step test (move feasibility)

Input: character, entity, move vector v (length 25 when called from rule 6). Result: pass/fail. The entity is never
moved by this test.
- **Characters whose movetype is not "step"** (flying/swimming, Quake II style): |v|² < 0.01 → pass. Up to two
  attempts: first to E + v, then to E + v with the z change dropped. Trace with the entity box (mask 0 for
  movetype noclip, else 0x2020003). After each attempt the test fails at once when a flying character (entity flag
  1) that is out of liquid would end with the point (trace end z + box bottom + 1) in liquid (contents 0x38), or a
  swimming character (flag 2) whose water level is below 2 would end with that point out of liquid. An attempt with
  fraction 1 ends the attempts; then (for movetypes other than noclip) a box test at the trace end must not start or
  end in solid, and the test passes. Two attempts without fraction 1 → fail.
- **"Step" characters**:
  1. fail unless the entity has a ground entity and |velocity|² ≤ 100;
  2. |v|² < 0.01 → pass;
  3. with h = the character's step height and E the origin, a box trace (mask 0x2020003) goes from
     (E + v) raised by h down to (E + v) lowered by h. If it starts or ends in solid it is retried from heights
     h/2 and 0 above E + v (same end point). A solid result is accepted at any of the three heights when the hit
     entity is a door-like entity usable now (SPEC-710); if all three fail otherwise → fail;
  4. if the character's water level is 0, the trace end (z + box bottom + 1) must not be in liquid (contents 0x38);
  5. a box test at the trace end must not start or end in solid (a usable-now door is again accepted);
  6. if the first trace hit an entity that is not the ground entity, is a player or a character, and is not dead
     (dead flag value 2 clear): fail when the trace end z is above that entity's top (its origin z + maxs z) plus the
     box bottom − 1 (never step onto someone's head);
  7. otherwise pass. (The original also runs the SPEC-700 bottom check, second form, flag 4, but on the character's
     *current* origin, falling back to the same position; the result can never turn a pass into a fail. A
     re-implementation must not add a real ledge test here, or characters will refuse moves the original makes.)

### 10. Hidden spawn spot (world spawner, SPEC-761)

Input: output point, an *allow-seen* flag from the caller. Requires `ai_nonodes` = 0, N ≥ 1, player node ≥ 1 and a
sight client. With Fw = the sight client's forward vector (from its angles), the search counter is incremented
and a depth-first search starts at the player node:
- a node Q qualifies when (Q is not seen, or allow-seen is set, or the difficulty record's spawning level S > 3) and
  Q's ignore flag is 0, and with D = Q.origin − sight client origin, d = |D|:
  (Q is outside the sight client's PVS, or d > 400 and (D/d)·Fw < 0.5) and d < 600 and no entity is within 100 units
  of Q's origin (entity-radius query with its filters 1, 1, 0; UNVERIFIED which entities it counts);
- a qualifying node ends the search: output = Q's origin (with the +14 load offset; the spawner adds its own 8);
- otherwise the node is stamped and the search recurses into its link targets that are not yet stamped, starting
  at link index r mod k and continuing cyclically (r = a random integer 0…1293 drawn per node, k = link count).
- No qualifying node → "none" (`World can't spawn - no spots available for spawning`, SPEC-761).

### 11. Cone helper (used by one decision, SPEC-710)

Given sphere A (centre a, radius ra), sphere B (centre b, radius rb) and a point p: p is inside when it lies between
the two end planes ((p − a)·(b − a) ≥ 0 and (b − p)·(b − a) ≥ 0) and inside the cone tangent to both spheres:
with axis w = unit(b − a), apex c = a − w · ra·|b − a|/(rb − ra) and a rim point q = b + rb·n (n = the standard
perpendicular of w, Quake II `PerpendicularVector`), p is inside when unit(p − c)·w′ ≥ unit(q − c)·w′, where
w′ = w if rb ≥ ra, else −w. The caller passes the character's current node with radius max(40, clearance) as A and
its goal with radius 30 as B: a character already inside that cone walks straight to the goal, otherwise to the
node first.

### 12. Node editor (development tool)

Commands (client commands, checked before all other client commands):
- The editor exists only after a `loadpoint` or `destroyents` command has created it; before that every other editor
  command is an ordinary unknown command.
- All editor commands are accepted only when cheats are enabled in the difficulty record (SPEC-760) and (the game is
  not deathmatch or cvar `cheats` ≠ 0).
- `savepoints`, `loadpoint`, `exportpoint`, `destroyents` must be given twice: the first time prints
  `Press key again to save` / `load` / `export` / `destroy entities`; a second press no later than one second after the
  whole second of the first press executes (UNVERIFIED exact window).

| Command | Effect | Message |
|---|---|---|
| `setpoint` | add a node at the player origin, flags 1 | `Nav point added` |
| `setflee` | add a node, flags 4 | `Flee point added` |
| `setfleefriendly` | add a node, flags 0x8000 | `Friendly-only flee point added` |
| `setfleeenemy` | add a node, flags 0x10000 | `Enemy-only flee point added` |
| `deletepoint` | delete the nearest node within 128 units (main set preferred when nearer or equal; the dummy node 0 is never chosen); links to it are removed, later nodes shift down by one | `Point deleted` / `Combat Point deleted` |
| `movepoint` | move the nearest node within 128 to the player origin | `Point moved` |
| `toggleignore` | toggle the ignore flag of the nearest main node within 128 (shown by effect `edit/ignore`) | `Status on node toggled` |
| `savepoints` | write the `.nav` file | `Nav points saved off`, `Adding %d nodes` |
| `loadpoint` | read the `.nav` file (replaces all nodes); also starts the link display and the automatic region recording | `Nav points loaded` |
| `exportpoint` | run the export pipeline and write the `.nvb` | `Nav points exported` |
| `pointhelp` | prints four help lines (`a = add point     m = move point     d = delete `, `s = save          l = load           e = export     (Hit these twice to use)`, `1 = flee point    2 = lean point     3 = duck point    4 = cover point`, `5 = friend flee   6 = foe flee       z = destroyents   i = togglespawning`) | |
| `destroyents` | open all `func_areaportal`s and free every in-use entity that is a character or whose classname starts with `func`, `trigger` or `spawner` | `Entities destroyed` |
| `startregions` | start recording grid regions around the player | |
| `toggleboxes` | toggle the region debug boxes | |
| `cleargrid` | forget all recorded regions | |

- Adding the first node of an empty editor first inserts a dummy node 0 at the world origin (flags 1) and starts a
  0.1 s think that draws node links: per tick the nodes whose index ≡ t (mod 30) (t from the level time), links as
  debug lines (main links colour 0xfa0000fa, second-set links 0xfa00fa00).
- Every node is shown by a `nodedrawer` entity with effect `edit/editpoint` (flags 1), `edit/editflee` (4),
  `edit/editfleegood` (0x8000) or `edit/editfleebad` (0x10000), plus `edit/ignore` when ignored.
- Region recording (0.1 s think, while a sight client exists): for each of the 7 × 7 offsets (dx, dy) ∈
  {−144, −96, …, 144}², a point trace (mask 3) from the player origin towards (origin + (dx, dy, 0)); when its end is
  within 48 units of the target in x and y, the end is used, snapped down to the 48-unit lattice (corner =
  ⌊(v + 4096)/48⌋·48 − 4096 in x and y; UNVERIFIED rounding of negative values), and dropped 256 units to the floor
  (point trace, mask 3). An existing region within 32 units in z and less than 3 units horizontally is kept instead.
  Otherwise a region is added (maximum 15 999; `Out of regions!!!  Huge problem!`, else `%d regions in use now`) with
  provisional flags: bit 0 when the cell volume (x + 8 … x + 40, y + 8 … y + 40, floor + 16 … floor + 64) is not
  clear, bit 5 when one of four point probes at (centre ± 16, centre ± 16) traced from floor + 16 down 41 units hits
  nothing. With boxes on, a coloured debug box (red = bit 0, green = bit 5, blue = neither) is sent for some of the
  samples (UNVERIFIED which).

### 13. `.nav` source file (editor, version 11)

Little endian. int version = 11; int N; N × {float[3] origin, float clearance, int flags, int 0, int ignore};
int M; M × the same record (second set); int R; R × {float[3] region corner (x, y, floor z), uint8 flags}.
Reading accepts versions ≥ 8 (else `ERROR: wrong version for NavFile - was %d, expected %d.`): node records are
origin, 4 skipped bytes, flags, 4 skipped bytes, then the ignore int only for versions ≥ 9; version 10 (only)
carries an extra block (int count, count × 36 bytes: int, float[3], float[3], int with values > 400 stored as −1;
UNVERIFIED purpose, unused); regions only for versions ≥ 11. Missing file: `ERROR: couldn't open %s.`

### 14. Export pipeline (`exportpoint`)

In this order:
1. `destroyents` (above); node 0's origin ← (9999, 9999, 9999).
2. *Clear nodes from walls*: a node whose ±24 box around its origin is not clear (start/all solid or fraction
   ≤ 0.98) is moved to the first clear position among the offsets (−8, 0), (8, 0), (0, −8), (0, 8), (−16, 0), (16, 0),
   (0, −16), (0, 16) (z unchanged); none clear → unchanged.
3. Node indices ← list positions.
4. *Links*: for each node A in index order: A's link lists are emptied, then A is connected to every other node B
   (skipped when A or B has x > 9000, or A has none of the flags 1/4/0x8000/0x10000) with |A − B| < 512,
   |unit(A − B).z| ≤ 0.5, a box trace (−16,−16,0)–(16,16,24) A → B with mask 0x6000083 reaching fraction ≥ 0.99
   without starting in solid, and the *floor walk* from B to A succeeding. A passing pair adds A→B and B→A (each only
   if absent), link length = |A − B|, link type 0. (A pair rejected from A's side after B had added A→B leaves a
   one-way link: 2 of 7 550 stock links.) Second-set points are linked one-way (node → point) with radius 384, box
   ±2, mask 3.
   *Floor walk* B → A: both points are dropped 256 units with box (−13,−13,0)–(13,13,24), mask 3; from the floor
   under B towards the floor under A, probes every 4 units (n = ⌊|Δ|/4⌋, probes 1 … n − 1) start 32 units above the
   straight floor-to-floor line and drop 256 units with the same box; the walk fails when the floor height changes
   by more than 16 units between consecutive probes.
5. *Prune near-collinear triangles* (2-D, z ignored): for each node A and each pair of its link targets B, C: with
   u = unit(A − C), w = unit(B − C): if u·w < −0.97 (C lies between A and B) the link A–B is removed in both
   directions; if u·w > 0.97 (A, B on the same side of C) the longer of C–A, C–B is removed in both directions.
   Iteration: A in index order, B and C in A's link order (UNVERIFIED that this order reproduces every stock file;
   links are as stored in the files).
6. *Routing tables* — for each source node s (exact; reproduces all 538 246 next_link/rank entries and every
   tail2/tail3 of the 26 stock files):
   - all entries: next_link 0, rank 16000; tail2 = tail3 = 0. A node without links keeps exactly that.
   - rank[s] = 0, next_link[s] = 0; if s has flag 4, tail2 = tail3 = s.
   - an *open list* receives one entry (node = target, cost = link length, first step = target) per link of s in link
     order; s and all its link targets are marked *discovered*.
   - repeat while the list is not empty: take the entry with the smallest cost (strict comparison, so the earliest
     entry wins ties; removing an entry keeps the others in order); its node u gets next_link[u] = the 1-based
     position of the first link of s whose target is the entry's first step, rank[u] = 1, 2, 3 … in order of
     removal; then u's flags update the tails: flag 4 → tail2 if still 0, else tail3 if still 0; flag 0x8000 (without
     4) → tail2 if still 0; flag 0x10000 (without 4 and 0x8000) → tail3 if still 0; then each link of u, in order,
     whose target v is not discovered appends (v, cost + link length, same first step) and marks v discovered.
   - Costs are 32-bit float sums. A node's cost is fixed when it is first discovered and never lowered, which is why
     about 2 % of entries are not true shortest paths (SPEC-143 open question 2).
   - So tail2 is the nearest node (in this order) that is a general or friendly-only flee point, tail3 the nearest
     enemy-only flee point or the second general flee point.
7. Link statistics are printed: `%d nodes pointing towards each other (out of %d connections)`,
   `%d nodes link up in only one direction`, `%d Nodes were unable to find each other!`,
   `%d Nodes that are completely unconnected`, `%d Invalid looping paths`, `%f Average Nodes from point to point`,
   `%d Regions`.
8. *Clearance* (tail1) per node: for 20 directions θ = 0°, 18°, …, 342° a ray (mask 1) runs 1024 units horizontally
   from the node origin; from the hit distance inwards in 32-unit steps a probe traces 56 units down (mask 1); the
   direction's free distance is the hit distance, or the smallest probed distance whose probe found no floor. The
   clearance is the minimum over the 20 directions (0–1024; stock 0–380).
9. *Floors* (grid): regions sorted by (x, y, z); layer id = sorted position; neighbours: two layers whose floors differ
   by ≤ 32 become neighbours when they are adjacent cells (same x within 1 and y within 50, or the reverse;
   slot 0 = −y, 1 = +x, 2 = +y, 3 = −x; later pairs overwrite earlier ones). Flags are recomputed: bit 0 = the cell
   volume (x + 8 … x + 40, y + 8 … y + 40, floor + 16 … floor + 64) is not clear; bit 5 = one of four point probes at
   (centre ± 1, centre ± 1) from the floor down 41 units hits nothing. `top`: a box (−24,−24,0)–(24,24,1) traced
   from (centre, floor + 24) up 2048 units, mask 3; its end height, or the floor when it starts in solid.
   Node lists: every 12 slots −1; each node is assigned the region containing its origin (x ≤ px ≤ x + 48,
   y ≤ py ≤ y + 48, highest floor ≤ pz). For each region, nodes are added whose region is reached by a *region walk*
   (step to the neighbour that keeps closest to the straight line; fails at a missing neighbour, at a flagged region
   or when it arrives in the right column/row at the wrong height) followed by a line-of-sight test between the two
   region centres raised 16 units: first with a ±16 box (mask 0x2020003); if no node passes, with a point trace; if
   still none, the walk alone. The nearest node (by distance from the region corner) is then swapped into slot 0.
   Then for every link A–B, at five offsets (0,0), (∓16, ∓16) of both endpoints, A and B are appended to every region
   the walk from A's region to B's region passes (without the flag stop). Then, repeated until nothing changes, every
   region with an empty list receives the first node of an adjacent region that has one (order +x, −x, +y, −y).
   Finally, for each flagged region and each flagged neighbour, a flat box (32 × 32 at floor height, shifted 24 units
   across the shared edge) that is clear sets the edge bits: −y → bit 1, +x → bit 2, +y → bit 3, −x → bit 4 on this
   region and the opposite bit on the neighbour. Debug progress strings (not on the console): `Clearing nodes from
   walls`, `Building connections`, `Validating links`, `Checking environment`, `Preparing floor data`,
   `Calculating node partial status and height`, `Connecting floors to nodes`, `Connected %d floors`,
   `Adding floor-node connections from node-node connections`, `Ensuring neighbors have nodes`, `Neighbor pass`.
10. Write the `.nvb` (SPEC-143 + amendments above): version 40, N, nodes, M, second set, grid.

## Constants

| Name | Value |
|---|---|
| file version | 40 (low byte of the first int) |
| node origin offset on load | +14 z |
| grid cell size | 48 × 48 |
| cache refresh probe | origin + 16 z; fallbacks (−16, −16), (+32, 0), (+32, +32), (0, +32) |
| player corner probes | (±16, ±16) |
| flee rank limit | 15000 (unreachable 16000) |
| obstacle probe | 64 units ahead; box shrink 1 (x, y), bottom +10; clear fraction > 0.99; side probes 64 forward ± 64 sideways, open fraction > 0.1 |
| door back-off | 64 units along the hit normal |
| reversal refusal | dot < −0.717 within 3.0 s; minimum move 0.1 (squared) |
| step test move | 25 units; velocity limit 10 (squared 100); retry heights h, h/2, 0 |
| liquid contents | 0x38 |
| world spawn search | distance < 600; hidden: outside PVS, or > 400 and view dot < 0.5; free radius 100; random start 0…1293; S > 3 ignores "seen" |
| seen table | 1024 nodes |
| cone helper radii | node radius max(40, clearance), goal 30 |
| editor pick radius | 128 |
| export: link radius / second set | 512 / 384; slope |dz/d| ≤ 0.5; box (−16,−16,0)–(16,16,24) mask 0x6000083 / ±2 mask 3 |
| export: floor walk | step 4, start 32 above, drop 256, box ±13 × 0…24, max height change 16 |
| export: collinear prune | \|cos\| > 0.97 |
| export: clearance | 20 rays (18°), 1024, inward step 32, floor probe 56 |
| export: wall clearing | box ±24, clear fraction > 0.98, offsets 8 / 16 |
| regions | 7 × 7 samples, spacing 48, ±144; merge < 3 horizontally and ≤ 32 vertically; max 15 999 |
| grid neighbours | height difference ≤ 32 |
| dummy node origin (export) | (9999, 9999, 9999); nodes with x > 9000 are never linked |

## Test vectors / how to verify

1. **Routing rule (data check, done)**: recomputing next_link, rank, tail2, tail3 with rule 14.6 from the stored
   links of each of the 26 stock `.nvb` files reproduces all 538 246 next_link/rank entries and all tail2/tail3
   values exactly.
2. **Load**: start `irq1a` (has a `.nvb`) and `arm1` (none). With `aipoints_show 1`, enemies on `irq1a` draw debug
   lines to node positions when they move around corners; on `arm1` nothing is drawn and enemies walk straight.
3. **Corrupt version**: replace the first byte of a copy of `irq1a.nvb` with 39 (in a mod directory) → console
   prints `Invalid version (39) for Info Point File (Should be 40) - please re-export the path data`.
4. **ai_nonodes**: `ai_nonodes 1` on `irq1a`: no debug lines with `aipoints_show 1`; retreating soldiers back
   straight away from the player instead of along corridors.
5. **Flee spot**: on a map whose nodes carry flag 0x8000/0x10000 (26 + 33 + 52 such nodes in stock data, SPEC-143),
   frighten a civilian and an enemy near the same node: the civilian (player's team) runs to the tail2 node's
   origin, the enemy to tail3's (UNVERIFIED which team value each class reports).
6. **Reversal refusal**: with `aipoints_show 1`, force a retreating soldier to reverse (walk around it): no new debug
   line appears for up to 3 s when the new direction is more than ~136° from the previous one.
7. **Hidden spawn**: on a level with world spawning (`worldspawn` keys `spawn1`…`spawn3`), with `ei_show 1`, spawned
   characters appear at node origins + 22 units (14 + 8) at 400–600 units from the player, out of view.
8. **Editor**: in single player with cheats enabled: `loadpoint` twice (creates the editor; `ERROR: couldn't open …`
   if no `.nav`), `setpoint` → `Nav point added`; `pointhelp` prints the four help lines; `exportpoint` twice writes
   `<ai_editdir>/base/maps/<map>.nvb` that parses with `tools/formats/nav.py nvb-validate`.

## Open questions

1. Exact iteration order of the collinear-link pruning (rule 14.5) — not needed for stock data (links are stored),
   needed only to regenerate links for a new map identically.
2. Which entities the 100-unit "free" test of the hidden spawn search counts (filters 1, 1, 0 of the entity radius
   query).
3. The `.nav` version-10 block and the editor's "lean/duck/cover" second set: legacy, no 1.07f command creates them.
4. The window of the double-press confirmation.
