# SPEC-143 — AI navigation data (.nvb) and bot waypoints (.wrs)

Status: draft (clean, from data analysis)
Date: 2026-10-21
Describes: data files `maps/<map>.nvb` (stock: 26, pak0) and `bot/wpfiles/<map>.wrs`
(stock: 44 — 39 in pak2, 5 in pak3).
Code: `tools/formats/nav.py` (readers, validators, overlays); `tools/formats/mapview.py` (backdrop).

Sources: stock data only; BSP geometry and entity lumps read with `tools/formats/bsp.py` (SPEC-101)
as black-box cross-references. SPEC-203 lists `bot/wpfiles/<map>.wrs` among the files a client may
download for a map.

## Purpose

- `.nvb` — navigation for single-player AI: a graph of path nodes with precomputed all-pairs routing
  tables, plus a 48-unit walkability grid that maps any floor position to nearby nodes.
- `.wrs` — waypoint graph for multiplayer bots (added by the multiplayer patches), as plain text.

Each file belongs to the map with the same base name (`maps/irq1a.nvb` ↔ `maps/irq1a.bsp`,
`bot/wpfiles/jpndm2.wrs` ↔ `maps/dm/jpndm2.bsp`). `.nvb` exists for 26 of the 32 single-player maps
(none for arm1–3, trn1, tsr2, tut1). `bot/wpfiles/lastloaded.wrs` has no map (a copy of some map's file).

## .nvb layout

Little endian, no padding, records are byte-packed (the grid layer is 51 bytes).

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | int32 | version, always 40 |
| 4 | 4 | int32 | N, node count (18–243) |
| 8 | … | node[N] | variable-size records, below |
| … | 12 | int32[3] | grid header: 0, L (layer count), C (column count) |
| … | … | column[C] | grid, below; the file ends after the last column |

Node record (size 24 + 12·k + 3·N + 28):

| Offset | Size | Type | Meaning |
|---|---|---|---|
| 0 | 4 | int32 | flags: 1 (3423 nodes), 4 (26), 0x8000 (33), 0x10000 (52) |
| 4 | 4 | int32 | node index (= position in the file) |
| 8 | 12 | float32[3] | origin |
| 20 | 4 | int32 | k, number of links (0–8) |
| 24 | 12·k | link[k] | int32 target node, float32 length, int32 0 (always 0) |
| … | N | uint8[N] | next_link[j]: 1-based index into this node's links of the first step towards node j; 0 = j is this node or unreachable |
| … | 2·N | int16[N] | rank[j]: position of node j when all nodes are sorted by path cost from this node (0 = this node); 16000 = unreachable |
| … | 4 | int32 | tail0, always 0 |
| … | 4 | float32 | tail1, 0–380 (median 55) — UNKNOWN (hypothesis: clearance radius) |
| … | 4 | int32 | tail2: index of a node with flags 0x8000 or 4, 0 = none |
| … | 4 | int32 | tail3: index of a node with flags 0x10000 or 4, 0 = none |
| … | 4 | int32 | tail4: 0 or 1 — UNKNOWN |
| … | 8 | int32[2] | tail5, tail6: always 0 |

Grid:

| Structure | Fields |
|---|---|
| column | float32 x, int32 cell count, cells |
| cell | float32 y, int32 layer count (1–28), layers |
| layer (51 bytes) | float32[3] position (x, y, floor z); uint8 flags; int16 layer id; int16[12] node indices (−1 = unused, filled from the front); int16[4] neighbour layer ids (−1 = none); float32 top |

Layer ids run 0…L−1 in file order. Columns are 48 units apart in x, cells 48 units apart in y; a
layer's position x and y equal its column's x and cell's y. Several layers in one cell are walkable
floors at different heights (bridges, stairs, multi-storey rooms).

## .nvb semantics

- Node 0 is a dummy in every file: origin (9999, 9999, 9999), no links, flags 1.
- Links are undirected in practice (7 548 of 7 550 have a reverse link); `length` equals the Euclidean
  distance between the two origins (all links, to 0.05 units); no link is longer than 511 units.
- 178 nodes have no links (152 besides the dummies); exactly these have `rank[self] = 16000`.
- Routing tables: for 97.8 % of reachable (i, j) pairs, `next_link` is the first step of a shortest
  path under Euclidean link lengths, and 97.6 % of consecutive entries in rank order are in
  non-decreasing path cost; the remainder are near-ties (median 1.9 % longer). The exact cost used by
  the original tool is therefore close to, but not exactly, link length — see open questions.
  `rank` and `next_link` agree on reachability for every pair.
- Grid neighbour slots: 0 = −y, 1 = +x, 2 = +y, 3 = −x (all 288 467 neighbour references point at the
  adjacent cell in that direction). Height difference to a neighbour is at most 32 units
  (step height).
- `nodes[12]` of a layer lists nodes associated with the cell; in a sample of 600 layers of ger1 and
  irq1a, 84 % of listed nodes have a clear line of sight from 24 units above the layer (BSP point test
  every 8 units), versus 29 % of the nearest unlisted nodes — hypothesis: visible nodes, nearest first (67 % of lists are sorted by distance).
- `top` ≥ floor z always; `top` = floor z for 47 % of layers; otherwise it often equals the height of
  the first solid above the cell centre (in a BSP probe of 400 sampled ger1 layers, 205 matched within
  8 units) — UNKNOWN exactly.
- Layer flags (6 bits used): bit k for k = 1…4 set ⇒ neighbour slot k−1 is present (never −1);
  bit 0 set ⇒ `top` equals the floor in 96 % of cases; bit 5 — UNKNOWN.
- The node graph drawn over the map outline follows corridors and doorways
  (`openmerc-data/scripts/nav/{ger4,irq1a,kos2,jpn1}_nvb.png`; yellow = flags 1, magenta = 4,
  cyan = 0x8000, orange = 0x10000; grid layers as blue dots, red where the node list is empty).

## .wrs layout (text)

Latin-1 text, CR LF line ends.

```
Version 5
begin dm
<index> <waypoint id>        index 0,1,2…; id 0 = empty slot
end dm
begin team1
…
end team1
begin team2
…
end team2
<id> <flags> (<x> <y> <z>) [<tag>] <next_d2> {<target> <cost>}*
```

- Waypoint ids are 1…n in file order. `flags`: 0 (9 463), 10 (71), 11 (67), 2 (21), 1 (8) — UNKNOWN.
- Coordinates are printed as integers.
- `tag` (30 waypoints in 3 files: sibctf1, factorydm, labdm) is `c<id>` naming another waypoint; tags come in mutual pairs
  (411 ↔ 415, 120 ↔ 145…) — UNKNOWN meaning (hypothesis: special connection such as a ladder or jump).
- `next_d2`: squared distance to waypoint id+1 in 4 987 of 9 586 cases, 0 for the last waypoint;
  otherwise UNKNOWN (hypothesis: stale value from recording order).
- Links: `target` (bit 0x8000 = flag, low 15 bits = waypoint id) and `cost` = squared distance between
  the two waypoints (68 113 of 68 136 within the rounding of the printed coordinates). Links are listed
  by decreasing cost. Flagged links are all ≤ 150 units long (28 722; 26 784 reciprocated with the
  flag); unflagged links are longer (10th percentile 165, median 235, max 1 770 units) — hypothesis: flag 0x8000 marks the local
  walking neighbourhood, unflagged links longer sight lines.
- Sections: `dm` entries point at waypoints within 64 units of `info_player_deathmatch` entities
  (525 of 540); `team1`/`team2` at `info_player_team1`/`team2` (78/115, 76/116) or deathmatch starts.

## Validation

- `python tools/formats/nav.py nvb-validate <maps>`: 26 / 26 parse exactly to end of file; 3 534 nodes,
  7 550 links, 83 710 grid layers. Checks: version, node index sequence, link targets and lengths,
  next-link range, rank/next-link reachability agreement, tail references, layer id sequence and
  count, neighbour adjacency, `top ≥ floor`.
- `python tools/formats/nav.py wrs-validate <dir>`: 44 / 44 parse; 9 630 waypoints, 68 136 links.
  Checks: ids 1…n, section indices and references, link targets exist, costs ≈ squared distance
  (23 outliers > 1.8 units of distance), link order.
- Overlays: `openmerc-data/scripts/nav/*_nvb.png`, `*_wrs.png` (nycdm4, jpndm2, factorydm; orange =
  flagged link, green = unflagged, red dots = section waypoints).

## Open questions

1. Meaning of node flags 4, 0x8000, 0x10000 and of tail1 (float) and tail4. The flagged nodes do not
   coincide with any entity origin (none within 2 units).
2. The cost function used to build `next_link`/`rank` (2.2–2.4 % of entries disagree with Euclidean
   shortest paths by small margins). A re-implementation can recompute the tables or use them as
   stored; the game's tie-breaking is unknown.
3. Layer flags bits 0 and 5, the exact meaning of `top`, and the ordering of `nodes[12]`.
4. `.wrs` waypoint flags, `c<id>` tags, `next_d2`, and the link flag 0x8000 beyond the length
   statistics.
5. Whether the game regenerates any of this data at run time (e.g. when a `.nvb` is missing for
   arm1–3, trn1, tsr2, tut1).
