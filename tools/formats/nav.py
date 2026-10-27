#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Implements: SPEC-143
"""Readers for AI navigation data: maps/<map>.nvb (single player) and
bot/wpfiles/<map>.wrs (multiplayer bot waypoints), SPEC-143.

.nvb (binary, little endian): int32 version (=40), int32 N, N node records,
then a 2-D grid of 48-unit cells (columns -> cells -> layers).
.wrs (text, CRLF): "Version 5", begin/end sections of (index, waypoint id),
then one line per waypoint: id flags (x y z) [tag] next_d2 {target cost}*.

usage:
  nav.py nvb-info <file.nvb>                 counts and a few records
  nav.py nvb-validate <dir>                  parse + check every .nvb under <dir>
  nav.py wrs-validate <dir>                  parse + check every .wrs under <dir>
  nav.py plot-nvb <file.nvb> <map.bsp> <out.png> [grid]
  nav.py plot-wrs <file.wrs> <map.bsp> <out.png>
  nav.py json-nvb <file.nvb> <out.json>      nodes + links as JSON (grid omitted)
"""
import heapq
import json
import math
import os
import re
import struct
import sys
from collections import Counter, namedtuple

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

NVB_VERSION = 40
UNREACHABLE = 16000          # route-rank value for "no path"
CELL = 48.0                  # grid spacing in world units (observed)
NODE_TAIL = struct.Struct("<ifiiiii")
LAYER = struct.Struct("<3fBh12h4hf")   # 51 bytes

Link = namedtuple("Link", "node dist extra")
Node = namedtuple("Node", "flags index origin links next_link rank tail")
Layer = namedtuple("Layer", "pos flags id nodes nbr top")


class NavError(ValueError):
    pass


class NVB:
    def __init__(self, data):
        self.data = data
        if len(data) < 8:
            raise NavError("file too short")
        self.version, n = struct.unpack_from("<ii", data, 0)
        if self.version != NVB_VERSION:
            raise NavError("version %d != %d" % (self.version, NVB_VERSION))
        if not 0 < n < 4096:
            raise NavError("node count %d" % n)
        p = 8
        self.nodes = []
        for i in range(n):
            flags, idx = struct.unpack_from("<ii", data, p)
            org = struct.unpack_from("<3f", data, p + 8)
            nl = struct.unpack_from("<i", data, p + 20)[0]
            if not 0 <= nl <= n:
                raise NavError("node %d: link count %d" % (i, nl))
            p += 24
            links = [Link(*struct.unpack_from("<ifi", data, p + 12 * k)) for k in range(nl)]
            p += 12 * nl
            nxt = data[p:p + n]
            p += n
            rank = struct.unpack_from("<%dh" % n, data, p)
            p += 2 * n
            tail = NODE_TAIL.unpack_from(data, p)
            p += NODE_TAIL.size
            self.nodes.append(Node(flags, idx, org, links, nxt, rank, tail))
        self.grid_header = struct.unpack_from("<3i", data, p)   # (0, layer count, column count)
        p += 12
        self.columns = []                                        # [(x, [(y, [Layer])])]
        for c in range(self.grid_header[2]):
            x, ncell = struct.unpack_from("<fi", data, p)
            p += 8
            cells = []
            for k in range(ncell):
                y, nl = struct.unpack_from("<fi", data, p)
                p += 8
                layers = []
                for _ in range(nl):
                    v = LAYER.unpack_from(data, p)
                    p += LAYER.size
                    layers.append(Layer(v[0:3], v[3], v[4], v[5:17], v[17:21], v[21]))
                cells.append((y, layers))
            self.columns.append((x, cells))
        if p != len(data):
            raise NavError("%d trailing bytes" % (len(data) - p))

    def layers(self):
        for x, cells in self.columns:
            for y, layers in cells:
                for l in layers:
                    yield l

    def validate(self):
        probs = []
        n = len(self.nodes)
        for i, nd in enumerate(self.nodes):
            if nd.index != i:
                probs.append("node %d has index %d" % (i, nd.index))
            for l in nd.links:
                if not 0 <= l.node < n:
                    probs.append("node %d links to %d" % (i, l.node))
                elif abs(l.dist - math.dist(nd.origin, self.nodes[l.node].origin)) > 0.05:
                    probs.append("node %d link %d length %g != distance" % (i, l.node, l.dist))
            for j in range(n):
                b = nd.next_link[j]
                if b > len(nd.links):
                    probs.append("node %d next_link[%d]=%d > %d links" % (i, j, b, len(nd.links)))
                r = nd.rank[j]
                if j == i:
                    if b != 0 or r not in (0, UNREACHABLE):
                        probs.append("node %d: self entry next_link %d rank %d" % (i, b, r))
                elif (r == UNREACHABLE) != (b == 0):
                    probs.append("node %d: rank/next-link reachability disagree for %d" % (i, j))
                    break
            for t in (nd.tail[2], nd.tail[3]):
                if not 0 <= t < n:
                    probs.append("node %d tail ref %d" % (i, t))
        ids = [l.id for l in self.layers()]
        if ids != list(range(len(ids))):
            probs.append("layer ids not sequential")
        if len(ids) != self.grid_header[1]:
            probs.append("layer count %d != header %d" % (len(ids), self.grid_header[1]))
        by_id = {l.id: l for l in self.layers()}
        for l in by_id.values():
            for v in l.nodes:
                if v != -1 and not 0 <= v < n:
                    probs.append("layer %d node ref %d" % (l.id, v))
            for s, v in enumerate(l.nbr):
                if v == -1:
                    continue
                if v not in by_id:
                    probs.append("layer %d neighbour %d missing" % (l.id, v))
                    continue
                q = by_id[v].pos
                want = [(0, -CELL), (CELL, 0), (0, CELL), (-CELL, 0)][s]
                if abs(q[0] - l.pos[0] - want[0]) > 0.5 or abs(q[1] - l.pos[1] - want[1]) > 0.5:
                    probs.append("layer %d neighbour slot %d not adjacent" % (l.id, s))
            if l.top < l.pos[2] - 0.01:
                probs.append("layer %d top below floor" % l.id)
        return probs

    def shortest(self, src):
        """Dijkstra over link lengths from node src (used to check the routing tables)."""
        dist = [math.inf] * len(self.nodes)
        dist[src] = 0.0
        pq = [(0.0, src)]
        while pq:
            d, u = heapq.heappop(pq)
            if d > dist[u]:
                continue
            for l in self.nodes[u].links:
                nd = d + l.dist
                if nd < dist[l.node] - 1e-6:
                    dist[l.node] = nd
                    heapq.heappush(pq, (nd, l.node))
        return dist

    def route_agreement(self):
        """(first-hop ok, first-hop total, rank pairs in order, rank pairs total)."""
        D = [self.shortest(i) for i in range(len(self.nodes))]
        ok = tot = rok = rtot = 0
        for i, nd in enumerate(self.nodes):
            for j in range(len(self.nodes)):
                if j == i or D[i][j] == math.inf or nd.next_link[j] == 0:
                    continue
                l = nd.links[nd.next_link[j] - 1]
                tot += 1
                ok += l.dist + D[l.node][j] <= D[i][j] + 1e-3
            order = sorted((j for j in range(len(self.nodes)) if D[i][j] < math.inf), key=lambda j: nd.rank[j])
            for a, b in zip(order, order[1:]):
                rtot += 1
                rok += D[i][a] <= D[i][b] + 1e-3
        return ok, tot, rok, rtot


# ---- .wrs ---------------------------------------------------------------------
WP_LINE = re.compile(r"^(\d+)\s+(-?\d+)\s+\(\s*(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\s*\)\s*(.*)$")
Waypoint = namedtuple("Waypoint", "id flags origin tag next_d2 links")


class WRS:
    LINK_FLAG = 0x8000

    def __init__(self, data):
        text = data.decode("latin-1")
        lines = text.split("\r\n") if "\r\n" in text else text.split("\n")
        if not lines or lines[0].strip() != "Version 5":
            raise NavError("bad header %r" % (lines[0][:20] if lines else ""))
        i = 1
        self.sections = {}
        while i < len(lines) and lines[i].startswith("begin "):
            name = lines[i][6:].strip()
            i += 1
            items = []
            while i < len(lines) and not lines[i].startswith("end "):
                a, b = lines[i].split()
                items.append((int(a), int(b)))
                i += 1
            if i >= len(lines) or lines[i].strip() != "end " + name:
                raise NavError("section %s not closed" % name)
            self.sections[name] = items
            i += 1
        self.waypoints = {}
        for ln in lines[i:]:
            if not ln.strip():
                continue
            m = WP_LINE.match(ln)
            if not m:
                raise NavError("bad waypoint line %r" % ln[:60])
            toks = m.group(6).split()
            tag = toks.pop(0) if toks and toks[0][0].isalpha() else None
            if not toks or (len(toks) - 1) % 2:
                raise NavError("bad link list in %r" % ln[:60])
            links = [(int(toks[k]), float(toks[k + 1])) for k in range(1, len(toks), 2)]
            wid = int(m.group(1))
            self.waypoints[wid] = Waypoint(wid, int(m.group(2)),
                                           tuple(float(m.group(k)) for k in (3, 4, 5)), tag, float(toks[0]), links)

    def validate(self):
        probs = []
        ids = sorted(self.waypoints)
        if ids != list(range(1, len(ids) + 1)):
            probs.append("waypoint ids are not 1..n")
        for name, items in self.sections.items():
            if [a for a, _ in items] != list(range(len(items))):
                probs.append("section %s index not 0..n-1" % name)
            for _, b in items:
                if b != 0 and b not in self.waypoints:      # 0 = empty slot
                    probs.append("section %s refers to missing waypoint %d" % (name, b))
        far = 0
        for w in self.waypoints.values():
            for t, c in w.links:
                tid = t & 0x7FFF
                if tid not in self.waypoints:
                    probs.append("waypoint %d links to missing %d" % (w.id, tid))
                    continue
                d = math.dist(w.origin, self.waypoints[tid].origin)
                if abs(math.sqrt(max(c, 0)) - d) > 1.8:      # positions are printed rounded to integers
                    far += 1
            cs = [c for _, c in w.links]
            if any(a < b for a, b in zip(cs, cs[1:])):
                probs.append("waypoint %d links not sorted by decreasing cost" % w.id)
        self.cost_outliers = far
        return probs


# ---- plotting -------------------------------------------------------------------
def plot_nvb(nv, bsp_path, out_png, grid=False, upp=4.0):
    import bsp
    from mapview import TopDown
    b = bsp.load(bsp_path)
    real = [nd for nd in nv.nodes if abs(nd.origin[0]) < 9000]
    td = TopDown(b, upp, extra_points=[nd.origin for nd in real])
    dr = td.draw()
    if grid:
        for l in nv.layers():
            x, y = td.xy(l.pos)
            c = (40, 120, 200) if l.nodes[0] != -1 else (200, 60, 60)
            dr.point((x, y), fill=c)
    for i, nd in enumerate(nv.nodes):
        if abs(nd.origin[0]) >= 9000:
            continue
        for l in nd.links:
            if l.node > i:
                dr.line([td.xy(nd.origin), td.xy(nv.nodes[l.node].origin)], fill=(80, 220, 80), width=1)
    colours = {1: (255, 255, 0), 4: (255, 0, 255), 0x8000: (0, 255, 255), 0x10000: (255, 128, 0)}
    for i, nd in enumerate(nv.nodes):
        if abs(nd.origin[0]) >= 9000:
            continue
        x, y = td.xy(nd.origin)
        c = colours.get(nd.flags, (255, 255, 255))
        r = 2 if nd.flags == 1 else 4
        dr.ellipse((x - r, y - r, x + r, y + r), fill=c)
    td.img.save(out_png)


def plot_wrs(w, bsp_path, out_png, upp=4.0):
    import bsp
    from mapview import TopDown
    b = bsp.load(bsp_path)
    td = TopDown(b, upp, extra_points=[p.origin for p in w.waypoints.values()])
    dr = td.draw()
    for p in w.waypoints.values():
        for t, c in p.links:
            q = w.waypoints.get(t & 0x7FFF)
            if q is None:
                continue
            col = (255, 170, 0) if t & WRS.LINK_FLAG else (60, 160, 60)
            dr.line([td.xy(p.origin), td.xy(q.origin)], fill=col, width=1)
    special = {b for items in w.sections.values() for _, b in items}
    for p in w.waypoints.values():
        x, y = td.xy(p.origin)
        c = (255, 60, 60) if p.id in special else ((0, 255, 255) if p.flags else (255, 255, 255))
        r = 4 if p.id in special else 2
        dr.ellipse((x - r, y - r, x + r, y + r), fill=c)
        if p.tag:
            dr.text((x + 4, y), p.tag, fill=(255, 255, 255))
    td.img.save(out_png)


def iter_ext(root, ext):
    for dp, _, fn in os.walk(root):
        for f in sorted(fn):
            if f.lower().endswith(ext):
                yield os.path.join(dp, f)


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "nvb-info":
        nv = NVB(open(argv[1], "rb").read())
        flags = Counter(nd.flags for nd in nv.nodes)
        nl = sum(len(nd.links) for nd in nv.nodes)
        print("version %d, %d nodes (flags %s), %d directed links, grid %d columns / %d layers"
              % (nv.version, len(nv.nodes), dict(flags), nl, nv.grid_header[2], nv.grid_header[1]))
        for nd in nv.nodes[:4]:
            print("node %d flags 0x%x origin %s links %s tail %s" % (
                nd.index, nd.flags, nd.origin, [(l.node, round(l.dist, 1)) for l in nd.links], nd.tail))
        ok, tot, rok, rtot = nv.route_agreement()
        print("next-link on a Euclidean shortest path: %d/%d (%.2f%%); rank order monotone: %d/%d (%.2f%%)"
              % (ok, tot, 100.0 * ok / max(tot, 1), rok, rtot, 100.0 * rok / max(rtot, 1)))
    elif cmd == "nvb-validate":
        ok = bad = 0
        tot = Counter()
        for p in iter_ext(argv[1], ".nvb"):
            try:
                nv = NVB(open(p, "rb").read())
                probs = nv.validate()
            except (NavError, struct.error) as e:
                probs = [str(e)]
                nv = None
            if probs:
                bad += 1
                print("FAIL %s: %s" % (p, "; ".join(probs[:4])))
            else:
                ok += 1
                a, t, ra, rt = nv.route_agreement()
                tot["nodes"] += len(nv.nodes)
                tot["links"] += sum(len(nd.links) for nd in nv.nodes)
                tot["layers"] += nv.grid_header[1]
                tot["hop_ok"] += a
                tot["hop_total"] += t
                tot["rank_ok"] += ra
                tot["rank_total"] += rt
        print("parsed %d ok, %d failed; totals %s" % (ok, bad, dict(tot)))
        return 0 if bad == 0 else 1
    elif cmd == "wrs-validate":
        ok = bad = 0
        tot = Counter()
        for p in iter_ext(argv[1], ".wrs"):
            try:
                w = WRS(open(p, "rb").read())
                probs = w.validate()
            except (NavError, ValueError) as e:
                probs = [str(e)]
                w = None
            if probs:
                bad += 1
                print("FAIL %s: %s" % (p, "; ".join(probs[:4])))
            else:
                ok += 1
                tot["waypoints"] += len(w.waypoints)
                tot["links"] += sum(len(x.links) for x in w.waypoints.values())
                tot["flagged_links"] += sum(1 for x in w.waypoints.values() for t, _ in x.links if t & WRS.LINK_FLAG)
                tot["cost_outliers"] += w.cost_outliers
                tot["tags"] += sum(1 for x in w.waypoints.values() if x.tag)
                for k, v in w.sections.items():
                    tot["section_" + k] += sum(1 for _, b in v if b)
                    tot["section_" + k + "_empty"] += sum(1 for _, b in v if not b)
        print("parsed %d ok, %d failed; totals %s" % (ok, bad, dict(tot)))
        return 0 if bad == 0 else 1
    elif cmd == "plot-nvb":
        plot_nvb(NVB(open(argv[1], "rb").read()), argv[2], argv[3], grid=len(argv) > 4 and argv[4] == "grid")
        print("wrote", argv[3])
    elif cmd == "plot-wrs":
        plot_wrs(WRS(open(argv[1], "rb").read()), argv[2], argv[3])
        print("wrote", argv[3])
    elif cmd == "json-nvb":
        nv = NVB(open(argv[1], "rb").read())
        out = {"version": nv.version, "grid_header": nv.grid_header,
               "nodes": [{"index": nd.index, "flags": nd.flags, "origin": nd.origin,
                          "links": [[l.node, l.dist] for l in nd.links], "tail": nd.tail} for nd in nv.nodes]}
        with open(argv[2], "w") as f:
            json.dump(out, f, indent=1)
        print("wrote", argv[2])
    else:
        print(__doc__)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
