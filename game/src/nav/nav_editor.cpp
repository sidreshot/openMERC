/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-740 rules 12 (node editor commands, node drawers, link display, region recording),
 *             13 (.nav source file), 14 (export pipeline: wall clearing, links with the floor walk, collinear
 *             pruning, routing tables, statistics, clearance, floors/grid, .nvb writer); SPEC-AMEND-005 G5.11
 * Owner: package G5 (game/src/nav).
 *
 * Development tool: it reads and writes files with C stdio under cvar ai_editdir (default "p:"), like the
 * original. Not used in normal play.
 */
#include <stdarg.h>
#include <stdio.h>

#include <algorithm>
#include <array>
#include <vector>

#include "nav_api.h"
#include "nav_data.h"
#include "nav_internal.h"

using namespace om_nav;

namespace {

enum { NAV_FILE_VERSION = 11, MAX_REGIONS = 15999 };

struct EdNode {
	vec3_t origin;
	float clearance;
	int flags;
	int ignore;
	std::vector<NavLink> links;
	std::vector<NavLink> links2;
	edict_t *drawer;
};

struct EdRegion {
	vec3_t corner; /* x, y, floor z */
	uint8_t flags;
};

struct Editor {
	bool exists;
	std::vector<EdNode> nodes;  /* main set, node 0 = dummy */
	std::vector<EdNode> nodes2; /* second set ("combat points") */
	std::vector<EdRegion> regions;
	edict_t *linkthink;
	edict_t *regionthink;
	bool boxes;
	char pending[16];
	float pending_time;
} ed;

const vec3_t kZero = {0, 0, 0};

cvar_t *EditDir(void) { return gi.cvar("ai_editdir", "p:", 1); }

bool Allowed(void)
{
	if (!game.cheats)
		return false;
	return !deathmatch->value || gix.cvar_value("cheats") != 0;
}

void Msg(edict_t *ent, const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	gi.cprintf(ent, PRINT_HIGH, "%s", buf);
}

float Dist2(const vec3_t a, const vec3_t b)
{
	vec3_t d;
	VectorSubtract(a, b, d);
	return DotProduct(d, d);
}

trace_t Trace(const vec3_t a, const vec3_t mn, const vec3_t mx, const vec3_t b, int mask)
{
	return gi.trace(a, mn, mx, b, NULL, mask);
}

//------------------------------------------------------------------------------------------
// node drawers and the link display

const char *DrawerEffect(int flags)
{
	if (flags & NF_FLEE)
		return "edit/editflee";
	if (flags & NF_FLEE_FRIENDLY)
		return "edit/editfleegood";
	if (flags & NF_FLEE_ENEMY)
		return "edit/editfleebad";
	return "edit/editpoint";
}

void UpdateDrawer(EdNode &n)
{
	if (!n.drawer) {
		n.drawer = G_Spawn();
		n.drawer->classname = (char *)"nodedrawer";
		n.drawer->solid = SOLID_NOT;
		n.drawer->movetype = MOVETYPE_NONE;
	}
	VectorCopy(n.origin, n.drawer->s.origin);
	FX_ClearSlots(n.drawer);
	FX_SetSlot(n.drawer, 0, DrawerEffect(n.flags));
	if (n.ignore)
		FX_SetSlot(n.drawer, 1, "edit/ignore");
	gi.linkentity(n.drawer);
}

void FreeDrawers(std::vector<EdNode> &v)
{
	for (auto &n : v)
		if (n.drawer) {
			G_FreeEdict(n.drawer);
			n.drawer = NULL;
		}
}

void LinkThink(edict_t *self)
{
	int t = (int)(level.time * 10.0f + 0.5f) % 30;
	for (size_t i = 0; i < ed.nodes.size(); i++) {
		if ((int)(i % 30) != t)
			continue;
		const EdNode &n = ed.nodes[i];
		for (auto &l : n.links)
			if (l.target >= 0 && l.target < (int)ed.nodes.size())
				Nav_DebugLine(n.origin, ed.nodes[l.target].origin, 0xfa0000fa, 3);
		for (auto &l : n.links2)
			if (l.target >= 0 && l.target < (int)ed.nodes2.size())
				Nav_DebugLine(n.origin, ed.nodes2[l.target].origin, 0xfa00fa00, 3);
	}
	self->nextthink = level.time + 0.1f;
}

void StartLinkDisplay(void)
{
	if (ed.linkthink)
		return;
	ed.linkthink = G_Spawn();
	ed.linkthink->classname = (char *)"nodelinks";
	ed.linkthink->think = LinkThink;
	ed.linkthink->nextthink = level.time + 0.1f;
}

//------------------------------------------------------------------------------------------
// regions

float Snap(float v) { return floorf((v + 4096.0f) / 48.0f) * 48.0f - 4096.0f; }

bool CellVolumeBlocked(const EdRegion &r)
{
	vec3_t mn = {8, 8, 16}, mx = {40, 40, 64};
	trace_t tr = Trace(r.corner, mn, mx, r.corner, MASK_SOLID);
	return tr.startsolid || tr.allsolid;
}

bool ProbeMissesFloor(const EdRegion &r, float off, float from)
{
	for (int k = 0; k < 4; k++) {
		vec3_t a = {r.corner[0] + 24 + ((k & 1) ? off : -off), r.corner[1] + 24 + ((k & 2) ? off : -off),
		            r.corner[2] + from};
		vec3_t b = {a[0], a[1], a[2] - 41};
		trace_t tr = Trace(a, kZero, kZero, b, MASK_SOLID);
		if (tr.fraction == 1.0f)
			return true;
	}
	return false;
}

void DebugBox(const EdRegion &r)
{
	vec3_t mn = {r.corner[0], r.corner[1], r.corner[2]}, mx = {r.corner[0] + 48, r.corner[1] + 48, r.corner[2] + 4};
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(64);
	gi.WritePosition(mn);
	gi.WritePosition(mx);
	gi.WriteByte((r.flags & 1) ? 250 : 0);
	gi.WriteByte((r.flags & 0x20) ? 250 : 0);
	gi.WriteByte(!(r.flags & 0x21) ? 250 : 0);
	gi.multicast(mn, MULTICAST_PVS);
}

void RegionThink(edict_t *self)
{
	self->nextthink = level.time + 0.1f;
	edict_t *p = level.sight_client;
	if (!p)
		return;
	for (int ix = -3; ix <= 3; ix++)
		for (int iy = -3; iy <= 3; iy++) {
			vec3_t target = {p->s.origin[0] + 48.0f * ix, p->s.origin[1] + 48.0f * iy, p->s.origin[2]};
			trace_t tr = Trace(p->s.origin, kZero, kZero, target, MASK_SOLID);
			if (fabsf(tr.endpos[0] - target[0]) > 48 || fabsf(tr.endpos[1] - target[1]) > 48)
				continue;
			vec3_t c = {Snap(tr.endpos[0]), Snap(tr.endpos[1]), tr.endpos[2]};
			vec3_t down = {c[0], c[1], c[2] - 256};
			trace_t fl = Trace(c, kZero, kZero, down, MASK_SOLID);
			c[2] = fl.endpos[2];
			bool have = false;
			for (auto &r : ed.regions)
				if (fabsf(r.corner[2] - c[2]) <= 32 && fabsf(r.corner[0] - c[0]) < 3 && fabsf(r.corner[1] - c[1]) < 3) {
					have = true;
					if (ed.boxes && ix == 0 && iy == 0)
						DebugBox(r);
					break;
				}
			if (have)
				continue;
			if ((int)ed.regions.size() >= MAX_REGIONS) {
				gi.dprintf("Out of regions!!!  Huge problem!\n");
				continue;
			}
			EdRegion r;
			VectorCopy(c, r.corner);
			r.flags = 0;
			if (CellVolumeBlocked(r))
				r.flags |= 1;
			if (ProbeMissesFloor(r, 16, 16))
				r.flags |= 0x20;
			ed.regions.push_back(r);
			gi.dprintf("%d regions in use now\n", (int)ed.regions.size());
			if (ed.boxes)
				DebugBox(r);
		}
}

void StartRegions(void)
{
	if (ed.regionthink)
		return;
	ed.regionthink = G_Spawn();
	ed.regionthink->classname = (char *)"noderegions";
	ed.regionthink->think = RegionThink;
	ed.regionthink->nextthink = level.time + 0.1f;
}

//------------------------------------------------------------------------------------------
// nodes

void EnsureDummy(void)
{
	if (!ed.nodes.empty())
		return;
	EdNode d = {};
	d.flags = NF_POINT;
	ed.nodes.push_back(d);
	StartLinkDisplay();
}

void AddNode(edict_t *ent, int flags)
{
	EnsureDummy();
	EdNode n = {};
	VectorCopy(ent->s.origin, n.origin);
	n.flags = flags;
	ed.nodes.push_back(n);
	UpdateDrawer(ed.nodes.back());
}

/* nearest node within 128: main set preferred when nearer or equal; node 0 never chosen */
bool Nearest(const vec3_t p, bool main_only, int *set, int *index)
{
	float best = 128.0f * 128.0f;
	int bs = -1, bi = -1;
	for (size_t i = 1; i < ed.nodes.size(); i++) {
		float d = Dist2(p, ed.nodes[i].origin);
		if (d <= best) {
			best = d;
			bs = 0;
			bi = (int)i;
		}
	}
	if (!main_only)
		for (size_t i = 0; i < ed.nodes2.size(); i++) {
			float d = Dist2(p, ed.nodes2[i].origin);
			if (d < best) {
				best = d;
				bs = 1;
				bi = (int)i;
			}
		}
	if (bs < 0)
		return false;
	*set = bs;
	*index = bi;
	return true;
}

void RemoveLinksTo(std::vector<EdNode> &v, int target, bool second)
{
	for (auto &n : v) {
		auto &l = second ? n.links2 : n.links;
		for (size_t k = 0; k < l.size();)
			if (l[k].target == target)
				l.erase(l.begin() + k);
			else {
				if (l[k].target > target)
					l[k].target--;
				k++;
			}
	}
}

//------------------------------------------------------------------------------------------
// .nav file (13)

void NavPath(char *out, size_t cap, const char *ext)
{
	Com_sprintf(out, (int)cap, "%s/base/maps/%s.%s", EditDir()->string, level.mapname, ext);
}

bool SaveNav(void)
{
	char path[MAX_OSPATH];
	NavPath(path, sizeof(path), "nav");
	FILE *f = fopen(path, "wb");
	if (!f) {
		gi.dprintf("ERROR: couldn't open %s.\n", path);
		return false;
	}
	auto wi = [f](int v) { fwrite(&v, 4, 1, f); };
	auto wset = [&](std::vector<EdNode> &v) {
		wi((int)v.size());
		for (auto &n : v) {
			fwrite(n.origin, 4, 3, f);
			fwrite(&n.clearance, 4, 1, f);
			wi(n.flags);
			wi(0);
			wi(n.ignore);
		}
	};
	wi(NAV_FILE_VERSION);
	wset(ed.nodes);
	wset(ed.nodes2);
	wi((int)ed.regions.size());
	for (auto &r : ed.regions) {
		fwrite(r.corner, 4, 3, f);
		fwrite(&r.flags, 1, 1, f);
	}
	fclose(f);
	return true;
}

bool LoadNav(edict_t *ent)
{
	char path[MAX_OSPATH];
	NavPath(path, sizeof(path), "nav");
	FILE *f = fopen(path, "rb");
	if (!f) {
		Msg(ent, "ERROR: couldn't open %s.\n", path);
		return false;
	}
	auto ri = [f]() {
		int v = 0;
		fread(&v, 4, 1, f);
		return v;
	};
	int version = ri();
	if (version < 8) {
		Msg(ent, "ERROR: wrong version for NavFile - was %d, expected %d.\n", version, NAV_FILE_VERSION);
		fclose(f);
		return false;
	}
	FreeDrawers(ed.nodes);
	FreeDrawers(ed.nodes2);
	ed.nodes.clear();
	ed.nodes2.clear();
	ed.regions.clear();
	/* SPEC-AMEND-005 G5.11: records of the second set are read like the first, but their ignore value is discarded */
	auto rset = [&](std::vector<EdNode> &v, bool keep_ignore) {
		int n = ri();
		for (int i = 0; i < n && !feof(f); i++) {
			EdNode e = {};
			fread(e.origin, 4, 3, f);
			fread(&e.clearance, 4, 1, f); /* skipped by the reader */
			e.clearance = 0;
			e.flags = ri();
			ri();
			e.ignore = version >= 9 ? ri() : 0;
			if (!keep_ignore)
				e.ignore = 0;
			v.push_back(e);
		}
	};
	rset(ed.nodes, true);
	rset(ed.nodes2, false);
	if (version == 10) { /* legacy block, unused (SPEC-740 OQ 3) */
		int n = ri();
		for (int i = 0; i < n && !feof(f); i++) {
			uint8_t skip[36];
			fread(skip, 1, 36, f);
		}
	}
	if (version >= 11) {
		int n = ri();
		for (int i = 0; i < n && !feof(f); i++) {
			EdRegion r;
			fread(r.corner, 4, 3, f);
			fread(&r.flags, 1, 1, f);
			ed.regions.push_back(r);
		}
	}
	fclose(f);
	for (size_t i = 1; i < ed.nodes.size(); i++)
		UpdateDrawer(ed.nodes[i]);
	return true;
}

//------------------------------------------------------------------------------------------
// destroyents

void DestroyEnts(void)
{
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = g_edicts + i;
		if (!e->inuse || !e->classname)
			continue;
		if (!strcmp(e->classname, "func_areaportal") && e->style)
			gi.SetAreaPortalState(e->style, true);
	}
	for (int i = game.maxclients + 1; i < globals.num_edicts; i++) {
		edict_t *e = g_edicts + i;
		if (!e->inuse || !e->classname)
			continue;
		bool character = (e->svflags & SVF_MONSTER) != 0;
		if (character || !strncmp(e->classname, "func", 4) || !strncmp(e->classname, "trigger", 7) ||
		    !strncmp(e->classname, "spawner", 7))
			G_FreeEdict(e);
	}
}

//------------------------------------------------------------------------------------------
// export pipeline (14)

void ClearFromWalls(void)
{
	static const float off[8][2] = {{-8, 0}, {8, 0}, {0, -8}, {0, 8}, {-16, 0}, {16, 0}, {0, -16}, {0, 16}};
	vec3_t mn = {-24, -24, -24}, mx = {24, 24, 24};
	auto clear = [&](const vec3_t p) {
		trace_t tr = Trace(p, mn, mx, p, MASK_SOLID);
		return !tr.startsolid && !tr.allsolid && tr.fraction > 0.98f;
	};
	for (size_t i = 1; i < ed.nodes.size(); i++) {
		EdNode &n = ed.nodes[i];
		if (clear(n.origin))
			continue;
		for (auto &o : off) {
			vec3_t p = {n.origin[0] + o[0], n.origin[1] + o[1], n.origin[2]};
			if (clear(p)) {
				VectorCopy(p, n.origin);
				break;
			}
		}
	}
}

bool FloorUnder(const vec3_t p, vec3_t out)
{
	vec3_t mn = {-13, -13, 0}, mx = {13, 13, 24};
	vec3_t e = {p[0], p[1], p[2] - 256};
	trace_t tr = Trace(p, mn, mx, e, MASK_SOLID);
	VectorCopy(tr.endpos, out);
	return tr.fraction < 1.0f;
}

bool FloorWalk(const vec3_t from, const vec3_t to)
{
	vec3_t a, b;
	FloorUnder(from, a);
	FloorUnder(to, b);
	vec3_t d;
	VectorSubtract(b, a, d);
	float len = VectorLength(d);
	int n = (int)(len / 4.0f);
	float prev = a[2];
	for (int i = 1; i < n; i++) {
		float f = (4.0f * i) / len;
		vec3_t p = {a[0] + d[0] * f, a[1] + d[1] * f, a[2] + d[2] * f + 32};
		vec3_t fl;
		FloorUnder(p, fl);
		if (fabsf(fl[2] - prev) > 16)
			return false;
		prev = fl[2];
	}
	return fabsf(b[2] - prev) <= 16;
}

void AddLink(std::vector<NavLink> &l, int target, float len)
{
	for (auto &x : l)
		if (x.target == target)
			return;
	l.push_back(NavLink{target, len, 0});
}

void BuildLinks(void)
{
	vec3_t mn = {-16, -16, 0}, mx = {16, 16, 24};
	int n = (int)ed.nodes.size();
	for (int a = 0; a < n; a++) {
		EdNode &A = ed.nodes[a];
		A.links.clear();
		A.links2.clear();
		if (A.origin[0] > 9000 || !(A.flags & NF_BODY_MASK))
			continue;
		for (int b = 0; b < n; b++) {
			if (b == a)
				continue;
			EdNode &B = ed.nodes[b];
			if (B.origin[0] > 9000)
				continue;
			vec3_t d;
			VectorSubtract(A.origin, B.origin, d);
			float len = VectorLength(d);
			if (len >= 512 || len <= 0 || fabsf(d[2] / len) > 0.5f)
				continue;
			trace_t tr = Trace(A.origin, mn, mx, B.origin, 0x6000083);
			if (tr.startsolid || tr.fraction < 0.99f)
				continue;
			if (!FloorWalk(B.origin, A.origin))
				continue;
			AddLink(A.links, b, len);
			AddLink(B.links, a, len);
		}
		vec3_t m2 = {-2, -2, -2}, x2 = {2, 2, 2};
		for (size_t p = 0; p < ed.nodes2.size(); p++) {
			float len = sqrtf(Dist2(A.origin, ed.nodes2[p].origin));
			if (len >= 384)
				continue;
			trace_t tr = Trace(A.origin, m2, x2, ed.nodes2[p].origin, MASK_SOLID);
			if (tr.startsolid || tr.fraction < 0.99f)
				continue;
			AddLink(A.links2, (int)p, len);
		}
	}
}

void RemovePair(int a, int b)
{
	auto rm = [](std::vector<NavLink> &l, int t) {
		for (size_t k = 0; k < l.size(); k++)
			if (l[k].target == t) {
				l.erase(l.begin() + k);
				return true;
			}
		return false;
	};
	rm(ed.nodes[a].links, b);
	rm(ed.nodes[b].links, a);
}

bool HasLink(int a, int b)
{
	for (auto &l : ed.nodes[a].links)
		if (l.target == b)
			return true;
	return false;
}

/* rule 14.5 (iteration order UNVERIFIED, SPEC-740 OQ 1) */
void PruneCollinear(void)
{
	int n = (int)ed.nodes.size();
	for (int a = 0; a < n; a++) {
		bool changed = true;
		while (changed) {
			changed = false;
			std::vector<NavLink> &L = ed.nodes[a].links;
			for (size_t i = 0; i < L.size() && !changed; i++)
				for (size_t j = 0; j < L.size() && !changed; j++) {
					if (i == j)
						continue;
					int B = L[i].target, C = L[j].target;
					const float *pa = ed.nodes[a].origin, *pb = ed.nodes[B].origin, *pc = ed.nodes[C].origin;
					float u[2] = {pa[0] - pc[0], pa[1] - pc[1]}, w[2] = {pb[0] - pc[0], pb[1] - pc[1]};
					float lu = sqrtf(u[0] * u[0] + u[1] * u[1]), lw = sqrtf(w[0] * w[0] + w[1] * w[1]);
					if (lu <= 0 || lw <= 0)
						continue;
					float dot = (u[0] * w[0] + u[1] * w[1]) / (lu * lw);
					if (dot < -0.97f) {
						RemovePair(a, B);
						changed = true;
					} else if (dot > 0.97f) {
						if (lu >= lw) {
							if (HasLink(C, a) || HasLink(a, C)) {
								RemovePair(C, a);
								changed = true;
							}
						} else if (HasLink(C, B) || HasLink(B, C)) {
							RemovePair(C, B);
							changed = true;
						}
					}
				}
		}
	}
}

float Clearance(const vec3_t o)
{
	float best = 1024;
	for (int k = 0; k < 20; k++) {
		float th = 18.0f * k * 3.14159265f / 180.0f;
		vec3_t dir = {cosf(th), sinf(th), 0}, end;
		VectorMA(o, 1024, dir, end);
		trace_t tr = Trace(o, kZero, kZero, end, CONTENTS_SOLID);
		float hit = 1024 * tr.fraction, freed = hit;
		for (float d = hit; d > 0; d -= 32) {
			vec3_t p, q;
			VectorMA(o, d, dir, p);
			VectorCopy(p, q);
			q[2] -= 56;
			trace_t ft = Trace(p, kZero, kZero, q, CONTENTS_SOLID);
			if (ft.fraction == 1.0f)
				freed = d; /* the smallest probed distance without floor */
		}
		if (freed < best)
			best = freed;
	}
	return best;
}

struct Grid {
	std::vector<EdRegion> r; /* sorted */
	std::vector<std::array<int, 4>> nbr;
	std::vector<std::vector<int>> nodes;
	std::vector<float> top;
};

int FindRegion(const Grid &g, const vec3_t p)
{
	int best = -1;
	for (size_t i = 0; i < g.r.size(); i++) {
		const float *c = g.r[i].corner;
		if (c[0] <= p[0] && p[0] <= c[0] + 48 && c[1] <= p[1] && p[1] <= c[1] + 48 && c[2] <= p[2] &&
		    (best < 0 || c[2] > g.r[best].corner[2]))
			best = (int)i;
	}
	return best;
}

/* region walk: neighbour steps keeping closest to the straight line */
bool RegionWalk(const Grid &g, int from, int to, bool flagstop, std::vector<int> *passed)
{
	int cur = from;
	vec3_t a = {g.r[from].corner[0] + 24, g.r[from].corner[1] + 24, 0};
	vec3_t b = {g.r[to].corner[0] + 24, g.r[to].corner[1] + 24, 0};
	for (int steps = 0; steps < 4096; steps++) {
		if (passed)
			passed->push_back(cur);
		if (cur == to)
			return true;
		const float *c = g.r[cur].corner;
		bool samecol = fabsf(c[0] - g.r[to].corner[0]) < 1, samerow = fabsf(c[1] - g.r[to].corner[1]) < 1;
		if (samecol && samerow)
			return false; /* right cell, wrong height */
		int cand[2] = {-1, -1};
		if (!samecol)
			cand[0] = g.r[to].corner[0] > c[0] ? 1 : 3;
		if (!samerow)
			cand[1] = g.r[to].corner[1] > c[1] ? 2 : 0;
		int best = -1;
		float bd = 0;
		for (int s : cand) {
			if (s < 0)
				continue;
			int nb = g.nbr[cur][s];
			if (nb < 0)
				continue;
			float px = g.r[nb].corner[0] + 24 - a[0], py = g.r[nb].corner[1] + 24 - a[1];
			float dx = b[0] - a[0], dy = b[1] - a[1], L = sqrtf(dx * dx + dy * dy);
			float off = L > 0 ? fabsf(px * dy - py * dx) / L : 0;
			if (best < 0 || off < bd) {
				best = nb;
				bd = off;
			}
		}
		if (best < 0)
			return false; /* missing neighbour */
		if (flagstop && (g.r[best].flags & 1))
			return false;
		cur = best;
	}
	return false;
}

bool AddNodeTo(std::vector<int> &l, int node)
{
	for (int x : l)
		if (x == node)
			return false;
	if (l.size() >= NAV_LAYER_NODES)
		return false;
	l.push_back(node);
	return true;
}

void BuildGrid(NavData &out)
{
	Grid g;
	g.r = ed.regions;
	std::sort(g.r.begin(), g.r.end(), [](const EdRegion &x, const EdRegion &y) {
		if (x.corner[0] != y.corner[0])
			return x.corner[0] < y.corner[0];
		if (x.corner[1] != y.corner[1])
			return x.corner[1] < y.corner[1];
		return x.corner[2] < y.corner[2];
	});
	int R = (int)g.r.size();
	g.nbr.assign(R, {-1, -1, -1, -1});
	g.nodes.assign(R, {});
	g.top.assign(R, 0);
	for (int i = 0; i < R; i++)
		for (int j = 0; j < R; j++) {
			if (i == j)
				continue;
			const float *a = g.r[i].corner, *b = g.r[j].corner;
			if (fabsf(a[2] - b[2]) > 32)
				continue;
			float dx = b[0] - a[0], dy = b[1] - a[1];
			if (fabsf(dx) < 1 && fabsf(dy) > 1 && fabsf(dy) <= 50)
				g.nbr[i][dy < 0 ? 0 : 2] = j;
			else if (fabsf(dy) < 1 && fabsf(dx) > 1 && fabsf(dx) <= 50)
				g.nbr[i][dx > 0 ? 1 : 3] = j;
		}
	for (int i = 0; i < R; i++) {
		EdRegion &r = g.r[i];
		r.flags = 0;
		if (CellVolumeBlocked(r))
			r.flags |= 1;
		if (ProbeMissesFloor(r, 1, 0))
			r.flags |= 0x20;
		vec3_t mn = {-24, -24, 0}, mx = {24, 24, 1};
		vec3_t s = {r.corner[0] + 24, r.corner[1] + 24, r.corner[2] + 24}, e = {s[0], s[1], s[2] + 2048};
		trace_t tr = Trace(s, mn, mx, e, MASK_SOLID);
		g.top[i] = tr.startsolid ? r.corner[2] : tr.endpos[2];
	}
	/* node lists */
	int N = (int)ed.nodes.size();
	std::vector<int> noderegion(N, -1);
	for (int k = 0; k < N; k++)
		noderegion[k] = FindRegion(g, ed.nodes[k].origin);
	vec3_t b16 = {-16, -16, -16}, B16 = {16, 16, 16};
	for (int i = 0; i < R; i++) {
		vec3_t ci = {g.r[i].corner[0] + 24, g.r[i].corner[1] + 24, g.r[i].corner[2] + 16};
		std::vector<int> walkers;
		for (int k = 1; k < N; k++)
			if (noderegion[k] >= 0 && RegionWalk(g, i, noderegion[k], true, NULL))
				walkers.push_back(k);
		for (int pass = 0; pass < 3 && g.nodes[i].empty(); pass++)
			for (int k : walkers) {
				int q = noderegion[k];
				vec3_t cq = {g.r[q].corner[0] + 24, g.r[q].corner[1] + 24, g.r[q].corner[2] + 16};
				bool ok = pass == 2;
				if (pass == 0)
					ok = Trace(ci, b16, B16, cq, 0x2020003).fraction == 1.0f;
				else if (pass == 1)
					ok = Trace(ci, kZero, kZero, cq, 0x2020003).fraction == 1.0f;
				if (ok)
					AddNodeTo(g.nodes[i], k);
			}
		auto &l = g.nodes[i];
		if (l.size() > 1) {
			size_t best = 0;
			for (size_t k = 1; k < l.size(); k++)
				if (Dist2(ed.nodes[l[k]].origin, g.r[i].corner) < Dist2(ed.nodes[l[best]].origin, g.r[i].corner))
					best = k;
			std::swap(l[0], l[best]);
		}
	}
	static const float offs[5][2] = {{0, 0}, {-16, -16}, {16, -16}, {-16, 16}, {16, 16}};
	for (int a = 0; a < N; a++)
		for (auto &lk : ed.nodes[a].links) {
			int b = lk.target;
			for (auto &o : offs) {
				vec3_t pa = {ed.nodes[a].origin[0] + o[0], ed.nodes[a].origin[1] + o[1], ed.nodes[a].origin[2]};
				vec3_t pb = {ed.nodes[b].origin[0] + o[0], ed.nodes[b].origin[1] + o[1], ed.nodes[b].origin[2]};
				int ra = FindRegion(g, pa), rb = FindRegion(g, pb);
				if (ra < 0 || rb < 0)
					continue;
				std::vector<int> passed;
				RegionWalk(g, ra, rb, false, &passed);
				for (int r : passed) {
					AddNodeTo(g.nodes[r], a);
					AddNodeTo(g.nodes[r], b);
				}
			}
		}
	for (bool changed = true; changed;) {
		changed = false;
		static const int order[4] = {1, 3, 2, 0}; /* +x, -x, +y, -y */
		for (int i = 0; i < R; i++) {
			if (!g.nodes[i].empty())
				continue;
			for (int s : order) {
				int nb = g.nbr[i][s];
				if (nb >= 0 && !g.nodes[nb].empty()) {
					g.nodes[i].push_back(g.nodes[nb][0]);
					changed = true;
					break;
				}
			}
		}
	}
	/* edge bits */
	for (int i = 0; i < R; i++) {
		if (!(g.r[i].flags & 1))
			continue;
		for (int s = 0; s < 4; s++) {
			int nb = g.nbr[i][s];
			if (nb < 0 || !(g.r[nb].flags & 1))
				continue;
			static const float sh[4][2] = {{0, -24}, {24, 0}, {0, 24}, {-24, 0}};
			vec3_t c = {g.r[i].corner[0] + 24 + sh[s][0], g.r[i].corner[1] + 24 + sh[s][1], g.r[i].corner[2] + 1};
			vec3_t mn = {-16, -16, 0}, mx = {16, 16, 1};
			trace_t tr = Trace(c, mn, mx, c, MASK_SOLID);
			if (!tr.startsolid && !tr.allsolid) {
				g.r[i].flags |= (uint8_t)(1 << (s + 1));
				g.r[nb].flags |= (uint8_t)(1 << (((s + 2) & 3) + 1));
			}
		}
	}
	/* to the .nvb grid: columns by x, cells by y, layers by floor z */
	std::vector<NavColumn> cols;
	for (int i = 0; i < R;) {
		NavColumn col;
		col.x = g.r[i].corner[0];
		std::vector<NavCell> cells;
		while (i < R && g.r[i].corner[0] == col.x) {
			NavCell cell;
			cell.y = g.r[i].corner[1];
			std::vector<NavLayer> layers;
			while (i < R && g.r[i].corner[0] == col.x && g.r[i].corner[1] == cell.y) {
				NavLayer ly;
				memset(&ly, 0, sizeof(ly));
				VectorCopy(g.r[i].corner, ly.pos);
				ly.flags = g.r[i].flags;
				ly.id = (int16_t)i;
				for (int k = 0; k < NAV_LAYER_NODES; k++)
					ly.nodes[k] = k < (int)g.nodes[i].size() ? (int16_t)g.nodes[i][k] : -1;
				for (int k = 0; k < 4; k++)
					ly.nbr[k] = (int16_t)g.nbr[i][k];
				ly.top = g.top[i];
				layers.push_back(ly);
				i++;
			}
			cell.nlayers = (int)layers.size();
			cell.layers = (NavLayer *)calloc(layers.size(), sizeof(NavLayer));
			memcpy(cell.layers, layers.data(), layers.size() * sizeof(NavLayer));
			cells.push_back(cell);
		}
		col.ncells = (int)cells.size();
		col.cells = (NavCell *)calloc(cells.size(), sizeof(NavCell));
		memcpy(col.cells, cells.data(), cells.size() * sizeof(NavCell));
		cols.push_back(col);
	}
	out.nlayers = R;
	out.ncolumns = (int)cols.size();
	out.columns = (NavColumn *)calloc(cols.size() ? cols.size() : 1, sizeof(NavColumn));
	memcpy(out.columns, cols.data(), cols.size() * sizeof(NavColumn));
}

void FillNodes(NavNode *out, std::vector<EdNode> &v, int tables)
{
	for (size_t i = 0; i < v.size(); i++) {
		NavNode &n = out[i];
		memset(&n, 0, sizeof(n));
		n.flags = v[i].flags;
		n.index = (int)i;
		VectorCopy(v[i].origin, n.origin);
		n.body = (n.flags & NF_BODY_MASK) != 0;
		n.nlinks = (int)v[i].links.size();
		n.links = (NavLink *)calloc(n.nlinks ? n.nlinks : 1, sizeof(NavLink));
		memcpy(n.links, v[i].links.data(), n.nlinks * sizeof(NavLink));
		n.nlinks2 = (int)v[i].links2.size();
		n.links2 = (NavLink *)calloc(n.nlinks2 ? n.nlinks2 : 1, sizeof(NavLink));
		memcpy(n.links2, v[i].links2.data(), n.nlinks2 * sizeof(NavLink));
		n.next_link = (uint8_t *)calloc(tables ? tables : 1, 1);
		n.rank = (int16_t *)calloc(tables ? tables : 1, 2);
		for (int j = 0; j < tables; j++)
			n.rank[j] = (int16_t)NAV_UNREACHABLE;
		n.clearance = v[i].clearance;
		n.ignore = v[i].ignore;
	}
}

void PrintStats(int regions)
{
	int N = (int)ed.nodes.size(), mutual = 0, oneway = 0, total = 0, unconnected = 0;
	for (int a = 0; a < N; a++) {
		if (ed.nodes[a].links.empty())
			unconnected++;
		for (auto &l : ed.nodes[a].links) {
			total++;
			if (HasLink(l.target, a))
				mutual++;
			else
				oneway++;
		}
	}
	gi.dprintf("%d nodes pointing towards each other (out of %d connections)\n", mutual / 2, total);
	gi.dprintf("%d nodes link up in only one direction\n", oneway);
	gi.dprintf("%d Nodes were unable to find each other!\n", 0);
	gi.dprintf("%d Nodes that are completely unconnected\n", unconnected);
	gi.dprintf("%d Invalid looping paths\n", 0);
	gi.dprintf("%f Average Nodes from point to point\n", 0.0);
	gi.dprintf("%d Regions\n", regions);
}

bool Export(void)
{
	DestroyEnts();
	EnsureDummy();
	VectorSet(ed.nodes[0].origin, 9999, 9999, 9999);
	ClearFromWalls();
	BuildLinks();
	PruneCollinear();
	NavData d;
	memset(&d, 0, sizeof(d));
	d.version = NVB_VERSION;
	d.n = (int)ed.nodes.size();
	d.nodes = (NavNode *)calloc(d.n, sizeof(NavNode));
	FillNodes(d.nodes, ed.nodes, d.n);
	ComputeRoutes(d.nodes, d.n);
	PrintStats((int)ed.regions.size());
	for (int i = 0; i < d.n; i++)
		d.nodes[i].clearance = i == 0 ? 0 : Clearance(d.nodes[i].origin);
	d.m = (int)ed.nodes2.size();
	d.nodes2 = (NavNode *)calloc(d.m ? d.m : 1, sizeof(NavNode));
	FillNodes(d.nodes2, ed.nodes2, d.m);
	BuildGrid(d);
	size_t len = WriteNvb(&d, NULL, 0);
	std::vector<uint8_t> buf(len);
	WriteNvb(&d, buf.data(), len);
	FreeNvb(&d);
	char path[MAX_OSPATH];
	NavPath(path, sizeof(path), "nvb");
	FILE *f = fopen(path, "wb");
	if (!f) {
		gi.dprintf("ERROR: couldn't open %s.\n", path);
		return false;
	}
	fwrite(buf.data(), 1, len, f);
	fclose(f);
	return true;
}

/* double-press confirmation: the second press no later than one second after the whole second of the first
 * (UNVERIFIED exact window, SPEC-740 OQ 4) */
bool Confirmed(edict_t *ent, const char *cmd, const char *what)
{
	if (!strcmp(ed.pending, cmd) && (int)level.time - (int)ed.pending_time <= 1) {
		ed.pending[0] = 0;
		return true;
	}
	strncpy(ed.pending, cmd, sizeof(ed.pending) - 1);
	ed.pending_time = level.time;
	Msg(ent, "Press key again to %s\n", what);
	return false;
}

} // namespace

int Nav_ClientCommand(edict_t *ent)
{
	const char *cmd = gi.argv(0);
	bool creator = !Q_stricmp(cmd, "loadpoint") || !Q_stricmp(cmd, "destroyents");
	static const char *cmds[] = {"setpoint", "setflee", "setfleefriendly", "setfleeenemy", "deletepoint",
	                             "movepoint", "toggleignore", "savepoints", "loadpoint", "exportpoint",
	                             "pointhelp", "destroyents", "startregions", "toggleboxes", "cleargrid"};
	bool known = false;
	for (const char *c : cmds)
		if (!Q_stricmp(cmd, c))
			known = true;
	if (!known || (!ed.exists && !creator) || !Allowed())
		return 0;
	ed.exists = true;
	if (!Q_stricmp(cmd, "setpoint")) {
		AddNode(ent, NF_POINT);
		Msg(ent, "Nav point added\n");
	} else if (!Q_stricmp(cmd, "setflee")) {
		AddNode(ent, NF_FLEE);
		Msg(ent, "Flee point added\n");
	} else if (!Q_stricmp(cmd, "setfleefriendly")) {
		AddNode(ent, NF_FLEE_FRIENDLY);
		Msg(ent, "Friendly-only flee point added\n");
	} else if (!Q_stricmp(cmd, "setfleeenemy")) {
		AddNode(ent, NF_FLEE_ENEMY);
		Msg(ent, "Enemy-only flee point added\n");
	} else if (!Q_stricmp(cmd, "deletepoint")) {
		int set, idx;
		if (Nearest(ent->s.origin, false, &set, &idx)) {
			std::vector<EdNode> &v = set ? ed.nodes2 : ed.nodes;
			if (v[idx].drawer)
				G_FreeEdict(v[idx].drawer);
			v.erase(v.begin() + idx);
			if (set)
				RemoveLinksTo(ed.nodes, idx, true);
			else
				RemoveLinksTo(ed.nodes, idx, false);
			Msg(ent, set ? "Combat Point deleted\n" : "Point deleted\n");
		}
	} else if (!Q_stricmp(cmd, "movepoint")) {
		int set, idx;
		if (Nearest(ent->s.origin, false, &set, &idx)) {
			EdNode &n = (set ? ed.nodes2 : ed.nodes)[idx];
			VectorCopy(ent->s.origin, n.origin);
			UpdateDrawer(n);
			Msg(ent, "Point moved\n");
		}
	} else if (!Q_stricmp(cmd, "toggleignore")) {
		int set, idx;
		if (Nearest(ent->s.origin, true, &set, &idx)) {
			ed.nodes[idx].ignore = !ed.nodes[idx].ignore;
			UpdateDrawer(ed.nodes[idx]);
			Msg(ent, "Status on node toggled\n");
		}
	} else if (!Q_stricmp(cmd, "savepoints")) {
		if (Confirmed(ent, "savepoints", "save")) {
			Msg(ent, "Adding %d nodes\n", (int)ed.nodes.size());
			if (SaveNav())
				Msg(ent, "Nav points saved off\n");
		}
	} else if (!Q_stricmp(cmd, "loadpoint")) {
		if (Confirmed(ent, "loadpoint", "load") && LoadNav(ent)) {
			StartLinkDisplay();
			StartRegions();
			Msg(ent, "Nav points loaded\n");
		}
	} else if (!Q_stricmp(cmd, "exportpoint")) {
		if (Confirmed(ent, "exportpoint", "export") && Export())
			Msg(ent, "Nav points exported\n");
	} else if (!Q_stricmp(cmd, "pointhelp")) {
		Msg(ent, "a = add point     m = move point     d = delete \n");
		Msg(ent, "s = save          l = load           e = export     (Hit these twice to use)\n");
		Msg(ent, "1 = flee point    2 = lean point     3 = duck point    4 = cover point\n");
		Msg(ent, "5 = friend flee   6 = foe flee       z = destroyents   i = togglespawning\n");
	} else if (!Q_stricmp(cmd, "destroyents")) {
		if (Confirmed(ent, "destroyents", "destroy entities")) {
			DestroyEnts();
			Msg(ent, "Entities destroyed\n");
		}
	} else if (!Q_stricmp(cmd, "startregions"))
		StartRegions();
	else if (!Q_stricmp(cmd, "toggleboxes"))
		ed.boxes = !ed.boxes;
	else if (!Q_stricmp(cmd, "cleargrid"))
		ed.regions.clear();
	return 1;
}

void Nav_EditorShutdown(void)
{
	ed.nodes.clear();
	ed.nodes2.clear();
	ed.regions.clear();
	ed.exists = false;
	ed.linkthink = ed.regionthink = NULL;
	ed.pending[0] = 0;
}
