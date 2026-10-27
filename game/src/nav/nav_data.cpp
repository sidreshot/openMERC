/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-143, SPEC-740 (Amendments to SPEC-143, rule 2 grid lookup, rule 5 node choice,
 *             rule 14.6 routing tables, rule 14.10 writer)
 * Owner: package G5 (game/src/nav). Engine-independent; see nav_data.h.
 */
#include "nav_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace om_nav {

namespace {

struct Reader {
	const uint8_t *p;
	const uint8_t *end;
	bool bad;

	bool need(size_t n)
	{
		if (bad || (size_t)(end - p) < n) {
			bad = true;
			return false;
		}
		return true;
	}
	int32_t i32()
	{
		int32_t v = 0;
		if (need(4)) {
			memcpy(&v, p, 4);
			p += 4;
		}
		return v;
	}
	float f32()
	{
		float v = 0;
		if (need(4)) {
			memcpy(&v, p, 4);
			p += 4;
		}
		return v;
	}
	void bytes(void *dst, size_t n)
	{
		if (need(n)) {
			memcpy(dst, p, n);
			p += n;
		} else
			memset(dst, 0, n);
	}
};

void *zalloc(size_t n) { return n ? calloc(1, n) : NULL; }

bool ReadLinks(Reader &r, int32_t *count, NavLink **out)
{
	int32_t k = r.i32();
	if (r.bad || k < 0 || (size_t)k > (size_t)(r.end - r.p) / 12) {
		r.bad = true;
		return false;
	}
	*count = k;
	*out = (NavLink *)zalloc(sizeof(NavLink) * k);
	for (int i = 0; i < k; i++) {
		(*out)[i].target = r.i32();
		(*out)[i].length = r.f32();
		(*out)[i].type = r.i32();
	}
	return !r.bad;
}

/* one node record; `tables` = number of routing-table entries (the size of the node's set) */
bool ReadNode(Reader &r, NavNode *nd, int tables)
{
	memset(nd, 0, sizeof(*nd));
	nd->flags = r.i32();
	nd->index = r.i32();
	nd->origin[0] = r.f32();
	nd->origin[1] = r.f32();
	nd->origin[2] = r.f32();
	nd->body = (nd->flags & NF_BODY_MASK) != 0;
	if (!nd->body || r.bad)
		return !r.bad;
	if (!ReadLinks(r, &nd->nlinks, &nd->links))
		return false;
	nd->next_link = (uint8_t *)zalloc(tables);
	nd->rank = (int16_t *)zalloc(2 * (size_t)tables);
	r.bytes(nd->next_link, tables);
	r.bytes(nd->rank, 2 * (size_t)tables);
	if (!ReadLinks(r, &nd->nlinks2, &nd->links2))
		return false;
	nd->clearance = r.f32();
	nd->tail2 = r.i32();
	nd->tail3 = r.i32();
	nd->ignore = r.i32();
	nd->tail5 = r.i32();
	nd->tail6 = r.i32();
	return !r.bad;
}

void FreeNodes(NavNode *nodes, int n)
{
	if (!nodes)
		return;
	for (int i = 0; i < n; i++) {
		free(nodes[i].links);
		free(nodes[i].links2);
		free(nodes[i].next_link);
		free(nodes[i].rank);
	}
	free(nodes);
}

struct Writer {
	uint8_t *out;
	size_t cap;
	size_t n;
	void put(const void *src, size_t len)
	{
		if (out && n + len <= cap)
			memcpy(out + n, src, len);
		n += len;
	}
	void i32(int32_t v) { put(&v, 4); }
	void f32(float v) { put(&v, 4); }
};

void WriteNode(Writer &w, const NavNode *nd, int tables)
{
	w.i32(nd->flags);
	w.i32(nd->index);
	w.put(nd->origin, 12);
	if (!(nd->flags & NF_BODY_MASK))
		return;
	w.i32(nd->nlinks);
	for (int i = 0; i < nd->nlinks; i++) {
		w.i32(nd->links[i].target);
		w.f32(nd->links[i].length);
		w.i32(nd->links[i].type);
	}
	for (int j = 0; j < tables; j++) {
		uint8_t b = nd->next_link ? nd->next_link[j] : 0;
		w.put(&b, 1);
	}
	for (int j = 0; j < tables; j++) {
		int16_t v = nd->rank ? nd->rank[j] : (int16_t)NAV_UNREACHABLE;
		w.put(&v, 2);
	}
	w.i32(nd->nlinks2);
	for (int i = 0; i < nd->nlinks2; i++) {
		w.i32(nd->links2[i].target);
		w.f32(nd->links2[i].length);
		w.i32(nd->links2[i].type);
	}
	w.f32(nd->clearance);
	w.i32(nd->tail2);
	w.i32(nd->tail3);
	w.i32(nd->ignore);
	w.i32(nd->tail5);
	w.i32(nd->tail6);
}

/* SPEC-740 rule 2 step 1: the probing search over columns or cells. `coord(i)` is the low edge of entry i. */
template <typename F> int ProbeSearch(int n, float v, F coord)
{
	if (n <= 0)
		return -1;
	int b = 0;
	for (int t = n; t; t >>= 1)
		b++;
	int s = b >= 2 ? 1 << (b - 2) : 0;
	int i = n / 2;
	for (;;) {
		float lo = coord(i);
		if (lo <= v && v <= lo + (float)NAV_CELL)
			return i;
		if (s == 0)
			return -1;
		i += v < lo ? -s : s;
		if (i < 0)
			i = 0;
		if (i > n - 1)
			i = n - 1;
		s >>= 1;
	}
}

} // namespace

bool ParseNvb(const uint8_t *buf, size_t len, NavData *out, char *err, size_t errsize)
{
	memset(out, 0, sizeof(*out));
	Reader r{buf, buf + len, false};
	out->version = r.i32();
	out->n = r.i32();
	if (r.bad || out->n < 0 || (size_t)out->n > len / 20) {
		snprintf(err, errsize, "bad header");
		FreeNvb(out);
		return false;
	}
	out->nodes = (NavNode *)zalloc(sizeof(NavNode) * out->n);
	for (int i = 0; i < out->n; i++)
		if (!ReadNode(r, &out->nodes[i], out->n)) {
			snprintf(err, errsize, "truncated in node %d", i);
			FreeNvb(out);
			return false;
		}
	out->m = r.i32();
	if (r.bad || out->m < 0 || (size_t)out->m > len / 20) {
		snprintf(err, errsize, "bad second node count");
		FreeNvb(out);
		return false;
	}
	out->nodes2 = (NavNode *)zalloc(sizeof(NavNode) * out->m);
	for (int i = 0; i < out->m; i++)
		if (!ReadNode(r, &out->nodes2[i], out->m)) {
			snprintf(err, errsize, "truncated in second-set node %d", i);
			FreeNvb(out);
			return false;
		}
	out->nlayers = r.i32();
	out->ncolumns = r.i32();
	if (r.bad || out->ncolumns < 0 || out->nlayers < 0 || (size_t)out->ncolumns > len / 8) {
		snprintf(err, errsize, "bad grid header");
		FreeNvb(out);
		return false;
	}
	out->columns = (NavColumn *)zalloc(sizeof(NavColumn) * out->ncolumns);
	for (int c = 0; c < out->ncolumns && !r.bad; c++) {
		NavColumn &col = out->columns[c];
		col.x = r.f32();
		col.ncells = r.i32();
		if (col.ncells < 0 || (size_t)col.ncells > (size_t)(r.end - r.p) / 8) {
			r.bad = true;
			col.ncells = 0;
			break;
		}
		col.cells = (NavCell *)zalloc(sizeof(NavCell) * col.ncells);
		for (int k = 0; k < col.ncells && !r.bad; k++) {
			NavCell &cell = col.cells[k];
			cell.y = r.f32();
			cell.nlayers = r.i32();
			if (cell.nlayers < 0 || (size_t)cell.nlayers > (size_t)(r.end - r.p) / sizeof(NavLayer)) {
				r.bad = true;
				cell.nlayers = 0;
				break;
			}
			cell.layers = (NavLayer *)zalloc(sizeof(NavLayer) * cell.nlayers);
			r.bytes(cell.layers, sizeof(NavLayer) * cell.nlayers);
		}
	}
	if (r.bad) {
		snprintf(err, errsize, "truncated grid");
		FreeNvb(out);
		return false;
	}
	if (out->nlayers > 0) {
		out->layer_by_id = (NavLayer **)zalloc(sizeof(NavLayer *) * out->nlayers);
		for (int c = 0; c < out->ncolumns; c++)
			for (int k = 0; k < out->columns[c].ncells; k++) {
				NavCell &cell = out->columns[c].cells[k];
				for (int l = 0; l < cell.nlayers; l++) {
					int id = cell.layers[l].id;
					if (id >= 0 && id < out->nlayers)
						out->layer_by_id[id] = &cell.layers[l];
				}
			}
	}
	return true;
}

void FreeNvb(NavData *nav)
{
	FreeNodes(nav->nodes, nav->n);
	FreeNodes(nav->nodes2, nav->m);
	if (nav->columns) {
		for (int c = 0; c < nav->ncolumns; c++) {
			for (int k = 0; k < nav->columns[c].ncells; k++)
				free(nav->columns[c].cells[k].layers);
			free(nav->columns[c].cells);
		}
		free(nav->columns);
	}
	free(nav->layer_by_id);
	memset(nav, 0, sizeof(*nav));
}

size_t WriteNvb(const NavData *nav, uint8_t *out, size_t cap)
{
	Writer w{out, cap, 0};
	w.i32(nav->version);
	w.i32(nav->n);
	for (int i = 0; i < nav->n; i++) {
		NavNode tmp = nav->nodes[i];
		if (nav->origin_raised)
			tmp.origin[2] -= 14.0f;
		WriteNode(w, &tmp, nav->n);
	}
	w.i32(nav->m);
	for (int i = 0; i < nav->m; i++) {
		NavNode tmp = nav->nodes2[i];
		if (nav->origin_raised)
			tmp.origin[2] -= 14.0f;
		WriteNode(w, &tmp, nav->m);
	}
	w.i32(nav->nlayers);
	w.i32(nav->ncolumns);
	for (int c = 0; c < nav->ncolumns; c++) {
		const NavColumn &col = nav->columns[c];
		w.f32(col.x);
		w.i32(col.ncells);
		for (int k = 0; k < col.ncells; k++) {
			w.f32(col.cells[k].y);
			w.i32(col.cells[k].nlayers);
			w.put(col.cells[k].layers, sizeof(NavLayer) * col.cells[k].nlayers);
		}
	}
	return w.n;
}

void RaiseOrigins(NavData *nav)
{
	if (nav->origin_raised)
		return;
	for (int i = 0; i < nav->n; i++)
		nav->nodes[i].origin[2] += 14.0f;
	for (int i = 0; i < nav->m; i++)
		nav->nodes2[i].origin[2] += 14.0f;
	nav->origin_raised = true;
}

const NavLayer *LookupLayer(const NavData *nav, const float p[3])
{
	if (!nav || nav->ncolumns <= 0)
		return NULL;
	int c = ProbeSearch(nav->ncolumns, p[0], [nav](int i) { return nav->columns[i].x; });
	if (c < 0)
		return NULL;
	const NavColumn &col = nav->columns[c];
	int k = ProbeSearch(col.ncells, p[1], [&col](int i) { return col.cells[i].y; });
	if (k < 0)
		return NULL;
	const NavCell &cell = col.cells[k];
	const NavLayer *best = NULL;
	for (int l = 0; l < cell.nlayers; l++)
		if (cell.layers[l].pos[2] < p[2])
			best = &cell.layers[l]; /* layers ascend by floor z: the last one below wins */
	return best;
}

void ComputeRoutes(NavNode *nodes, int n)
{
	struct Entry {
		int node;
		float cost;
		int first;
	};
	Entry *open = (Entry *)malloc(sizeof(Entry) * (n > 0 ? n : 1));
	uint8_t *disc = (uint8_t *)malloc(n > 0 ? n : 1);
	for (int s = 0; s < n; s++) {
		NavNode &src = nodes[s];
		if (!src.next_link || !src.rank)
			continue;
		for (int j = 0; j < n; j++) {
			src.next_link[j] = 0;
			src.rank[j] = (int16_t)NAV_UNREACHABLE;
		}
		src.tail2 = src.tail3 = 0;
		if (src.nlinks <= 0)
			continue;
		src.rank[s] = 0;
		src.next_link[s] = 0;
		if (src.flags & NF_FLEE)
			src.tail2 = src.tail3 = s;
		memset(disc, 0, n);
		disc[s] = 1;
		int nopen = 0;
		for (int l = 0; l < src.nlinks; l++) {
			int t = src.links[l].target;
			if (t < 0 || t >= n)
				continue;
			if (nopen < n)
				open[nopen++] = Entry{t, src.links[l].length, t};
			disc[t] = 1;
		}
		int counter = 1;
		while (nopen > 0) {
			int best = 0;
			for (int e = 1; e < nopen; e++)
				if (open[e].cost < open[best].cost)
					best = e;
			Entry cur = open[best];
			memmove(&open[best], &open[best + 1], sizeof(Entry) * (nopen - best - 1));
			nopen--;
			int u = cur.node;
			int pos = 0;
			for (int l = 0; l < src.nlinks; l++)
				if (src.links[l].target == cur.first) {
					pos = l + 1;
					break;
				}
			src.next_link[u] = (uint8_t)pos;
			src.rank[u] = (int16_t)counter++;
			int f = nodes[u].flags;
			if (f & NF_FLEE) {
				if (!src.tail2)
					src.tail2 = u;
				else if (!src.tail3)
					src.tail3 = u;
			} else if (f & NF_FLEE_FRIENDLY) {
				if (!src.tail2)
					src.tail2 = u;
			} else if (f & NF_FLEE_ENEMY) {
				if (!src.tail3)
					src.tail3 = u;
			}
			const NavNode &un = nodes[u];
			for (int l = 0; l < un.nlinks; l++) {
				int v = un.links[l].target;
				if (v < 0 || v >= n || disc[v])
					continue;
				float c = cur.cost + un.links[l].length; /* 32-bit float sum */
				if (nopen < n)
					open[nopen++] = Entry{v, c, cur.first};
				disc[v] = 1;
			}
		}
	}
	free(open);
	free(disc);
}

int ChooseNodeOnLayer(const NavData *nav, const NavLayer *layer, int t, bool flee)
{
	if (!nav || !layer || t < 0 || t >= nav->n)
		return -1;
	const NavNode &tn = nav->nodes[t];
	if (!tn.rank)
		return -1;
	int best = -1;
	int bestv = 0;
	for (int k = 0; k < NAV_LAYER_NODES; k++) {
		int c = layer->nodes[k];
		if (c == -1)
			break;
		if (c < 0 || c >= nav->n)
			continue;
		int v = tn.rank[c];
		if (!flee) {
			if (v < 0)
				continue;
			if (best < 0 || v <= bestv) { /* equal values: the later candidate wins */
				best = c;
				bestv = v;
			}
		} else {
			if (v >= NAV_FLEE_RANK_LIMIT)
				continue;
			if (best < 0 || v >= bestv) {
				best = c;
				bestv = v;
			}
		}
	}
	return best;
}

} // namespace om_nav
