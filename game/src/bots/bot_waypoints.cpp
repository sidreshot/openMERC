/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 14 (waypoint records, .wrs load and write, automatic links, paths and junctures, ladders,
 *             spawn table, item tagging, hot spots, routes, editing and drawing), 1.2 (initialisation),
 *             waypoint editor commands; SPEC-143 (.wrs grammar) with the SPEC-741 amendments;
 *             SPEC-AMEND-005 G5.7 (one-waypoint paths, juncture lists and order, route start, signed path numbers);
 *             SPEC-AMEND-006 F5.1-F5.4 (juncture duplicates, verification, editor prints, spawn-table trace)
 * Owner: package G5 (game/src/bots).
 */
#include "bot_local.h"
#include "bot_modes.h"

#include "../modes/g_modes.h"

#include <stdio.h>

BotWaypointSystem g_wps;

int BotWp_LinkTarget(int link) { return link & 0x7FFF; }

static float Dist2(const vec3_t a, const vec3_t b)
{
	vec3_t d;
	VectorSubtract(a, b, d);
	return DotProduct(d, d);
}

bool BotWp_Links(int a, int b)
{
	if (a < 1 || a > g_wps.count || b < 1 || b > g_wps.count)
		return false;
	const BotWaypoint &w = g_wps.wp[a];
	for (int i = 0; i < w.nlinks; i++)
		if (BotWp_LinkTarget(w.link[i]) == b)
			return true;
	return false;
}

bool BotWp_Visible(const vec3_t a, const vec3_t b, edict_t *ignore)
{
	trace_t tr = gi.trace(a, vec3_origin, vec3_origin, b, ignore, BOT_TRACE_MASK);
	return tr.fraction == 1.0f && !tr.startsolid;
}

void BotWp_Reset(void)
{
	g_wps.initialised = g_wps.ready = g_wps.roam = g_wps.editmode = g_wps.written = false;
	g_wps.version = 0;
	g_wps.count = 0;
	g_wps.npaths = 0;
	for (auto &v : g_wps.pj)
		v.clear();
	g_wps.spawns.clear();
	g_wps.blue_base = g_wps.red_base = 0;
	g_wps.control_points.clear();
	g_wps.control_flags.clear();
	g_wps.conquer_goal = 0;
	g_wps.edit_cursor = 0;
	g_wps.draw_from = g_wps.draw_to = 0;
	g_wps.draw_rr = 0;
	memset(g_wps.wp, 0, sizeof(g_wps.wp));
}

//==========================================================================================
// loading (14.2)

namespace {
struct Cursor {
	const char *p;
	const char *end;
	void skip_space()
	{
		while (p < end && (*p == ' ' || *p == '\t'))
			p++;
	}
	bool eol() const { return p >= end || *p == '\r' || *p == '\n'; }
	void next_line()
	{
		while (p < end && *p != '\n')
			p++;
		if (p < end)
			p++;
	}
	bool token(char *out, int cap)
	{
		skip_space();
		int n = 0;
		while (p < end && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
			if (n < cap - 1)
				out[n++] = *p;
			p++;
		}
		out[n] = 0;
		return n > 0;
	}
};
} // namespace

bool BotWp_Load(const char *mapbase)
{
	char path[MAX_QPATH * 2];
	Com_sprintf(path, sizeof(path), "bot/wpfiles/%s.wrs", mapbase);
	void *buf = NULL;
	int len = gix.FS_LoadFile(path, &buf, 0);
	if (len <= 0 || !buf) {
		if (buf)
			gix.FS_FreeFile(buf);
		return false;
	}
	Cursor c{(const char *)buf, (const char *)buf + len};
	char tok[128];
	int version = 0;
	if (c.p >= c.end || *c.p != 'V')
		BotConsoleId(BOTMSG(BM_HEADER), path);
	else {
		c.token(tok, sizeof(tok));
		if (strcmp(tok, "Version"))
			BotConsoleId(BOTMSG(BM_VERSION), path, 0, 5);
		else {
			c.token(tok, sizeof(tok));
			version = atoi(tok);
			if (version != 5)
				BotConsoleId(BOTMSG(BM_VERSION), path, version, 5);
		}
	}
	c.next_line();
	g_wps.version = version;
	/* dm/team1/team2 sections: read and discarded (the spawn table is rebuilt from the map) */
	if (version >= 4) {
		for (;;) {
			const char *save = c.p;
			c.skip_space();
			if (c.p + 5 <= c.end && !strncmp(c.p, "begin", 5)) {
				c.next_line();
				for (;;) {
					c.skip_space();
					if (c.p >= c.end || (c.p + 3 <= c.end && !strncmp(c.p, "end", 3))) {
						c.next_line();
						break;
					}
					c.next_line();
				}
				continue;
			}
			c.p = save;
			break;
		}
	}
	g_wps.count = 0;
	while (c.p < c.end) {
		c.skip_space();
		if (c.p < c.end && *c.p == '$')
			break;
		if (c.eol()) {
			c.next_line();
			continue;
		}
		const char *line_start = c.p;
		(void)line_start;
		BotWaypoint w;
		memset(&w, 0, sizeof(w));
		w.path = -1;
		bool ok = c.token(tok, sizeof(tok)); /* id (positional, not checked) */
		ok = ok && c.token(tok, sizeof(tok));
		w.kind = atoi(tok);
		/* (x y z) */
		float v[3] = {0, 0, 0};
		for (int k = 0; k < 3 && ok; k++) {
			ok = c.token(tok, sizeof(tok));
			const char *s = tok;
			if (*s == '(')
				s++;
			v[k] = (float)atof(s);
		}
		if (!ok) {
			c.next_line();
			continue;
		}
		VectorCopy(v, w.origin);
		bool add = true;
		if (version > 4) {
			c.skip_space();
			if (c.p < c.end && *c.p == 'c') {
				c.token(tok, sizeof(tok));
				w.tag = atoi(tok + 1);
			}
		}
		if (version >= 2) {
			c.skip_space();
			if (c.eol())
				add = false; /* a line that ends before next_d2 is not added */
			else {
				c.token(tok, sizeof(tok));
				w.next_d2 = (float)atof(tok);
			}
		}
		if (version >= 3 && add) {
			while (!c.eol()) {
				char t2[64];
				if (!c.token(tok, sizeof(tok)) || !c.token(t2, sizeof(t2)))
					break;
				if (w.nlinks < BOT_LINKS) {
					w.link[w.nlinks] = atoi(tok);
					w.cost[w.nlinks] = (float)atof(t2);
					w.nlinks++;
				}
			}
		}
		c.next_line();
		if (add && g_wps.count < BOT_WP_MAX)
			g_wps.wp[++g_wps.count] = w;
	}
	gix.FS_FreeFile(buf);
	BotWp_BuildSpawnTable();
	BotWp_BuildPaths();
	BotWp_MarkLadders();
	return true;
}

void BotWp_MarkLadders(void)
{
	for (int i = 1; i < g_wps.count; i++) {
		const float *a = g_wps.wp[i].origin, *b = g_wps.wp[i + 1].origin;
		float h = sqrtf((b[0] - a[0]) * (b[0] - a[0]) + (b[1] - a[1]) * (b[1] - a[1]));
		if (b[2] - a[2] > 64 && h < 64) {
			g_wps.wp[i].kind = WPK_LADDER_BOTTOM;
			g_wps.wp[i + 1].kind = WPK_LADDER_TOP;
		}
	}
}

//==========================================================================================
// spawn table (14.5)

static const char *g_spawn_classes[3] = {"info_player_deathmatch", "info_player_team1", "info_player_team2"};

void BotWp_BuildSpawnTable(void)
{
	g_wps.spawns.clear();
	for (int i = 1; i <= g_wps.count; i++)
		g_wps.wp[i].spawn = 0;
	for (int cls = 0; cls < 3; cls++) {
		int ordinal = 0;
		for (int e = 1; e < globals.num_edicts; e++) {
			edict_t *ent = g_edicts + e;
			if (!ent->inuse || !ent->classname || strcmp(ent->classname, g_spawn_classes[cls]))
				continue;
			BotSpawnRef r;
			r.cls = cls;
			r.ordinal = ordinal++;
			r.ent = ent;
			/* SPEC-AMEND-006 F5.3: point trace from each waypoint to the spawn point, ignoring entity 1, mask
			 * 0x02010003, accepted only with fraction exactly 1; the smallest squared distance wins (first on ties) */
			r.wp = 0;
			float bd = 0;
			for (int i = 1; i <= g_wps.count; i++) {
				trace_t tr = gi.trace(g_wps.wp[i].origin, vec3_origin, vec3_origin, ent->s.origin, g_edicts + 1,
				                      BOT_TRACE_MASK);
				if (tr.fraction != 1.0f)
					continue;
				float d = Dist2(ent->s.origin, g_wps.wp[i].origin);
				if (!r.wp || d < bd) {
					r.wp = i;
					bd = d;
				}
			}
			if (r.wp)
				g_wps.wp[r.wp].spawn = 1;
			g_wps.spawns.push_back(r);
		}
	}
}

int BotWp_SpawnWaypoint(edict_t *spot)
{
	for (auto &r : g_wps.spawns)
		if (r.ent == spot)
			return r.wp;
	return 0;
}

int BotWp_Nearest(const vec3_t p, float maxdist, bool needvisible, edict_t *ignore, bool skip_spawn)
{
	int best = 0;
	float bd = maxdist * maxdist;
	for (int i = 1; i <= g_wps.count; i++) {
		if (skip_spawn && g_wps.wp[i].spawn)
			continue;
		float d = Dist2(p, g_wps.wp[i].origin);
		if (d > bd || (best && d >= bd))
			continue;
		if (needvisible && !BotWp_Visible(p, g_wps.wp[i].origin, ignore))
			continue;
		best = i;
		bd = d;
	}
	return best;
}

//==========================================================================================
// paths and junctures (14.4)

void BotWp_VerifyPaths(void);

static int ConsecutiveNeighbour(int e, int t) { return (t == e - 1 && BotWp_Links(e, t)) || (t == e + 1 && BotWp_Links(e, t)); }

/* juncture list of path p: ordered by the juncture's waypoint id, entries at the same waypoint in creation order */
static void InsertJuncture(int p, const BotJuncture &j)
{
	std::vector<BotJuncture> &v = g_wps.pj[p];
	size_t at = v.size();
	while (at > 0 && v[at - 1].node > j.node)
		at--;
	v.insert(v.begin() + at, j);
}

/* a real juncture entry: adding is refused when the path already holds a record with the same own-side and other-side
 * waypoints (SPEC-AMEND-006 F5.1; the other path number plays no part); it removes a dead end at the same waypoint of
 * that path */
static void AddJuncture(int p, int node, int opath, int onode)
{
	std::vector<BotJuncture> &v = g_wps.pj[p];
	for (auto &o : v)
		if (o.node == node && o.onode == onode)
			return;
	for (size_t k = 0; k < v.size();)
		if (v[k].node == node && v[k].opath < 0)
			v.erase(v.begin() + k);
		else
			k++;
	InsertJuncture(p, BotJuncture{node, opath, onode});
}

/* a dead end is not added where the path already has a juncture at that waypoint */
static void AddDeadEnd(int p, int node, int onode)
{
	for (auto &o : g_wps.pj[p])
		if (o.node == node)
			return;
	InsertJuncture(p, BotJuncture{node, -1, onode});
}

/* SPEC-741 14.4 with SPEC-AMEND-005 G5.7: every waypoint that is not a spawn waypoint belongs to exactly one path; a
 * path starts at the first waypoint not yet in a path and extends while the current waypoint links to id + 1 and
 * id + 1 is not a spawn waypoint (one-waypoint paths included); waypoints left after the 255th path are in no path.
 * Junctures are created path by path, first end before last end, each added to its own path and then to the other. */
void BotWp_BuildPaths(void)
{
	g_wps.npaths = 0;
	for (auto &v : g_wps.pj)
		v.clear();
	for (int i = 1; i <= g_wps.count; i++)
		g_wps.wp[i].path = -1;
	bool warned = false;
	int i = 1;
	while (i <= g_wps.count) {
		if (g_wps.wp[i].spawn) {
			i++;
			continue;
		}
		int k = i;
		while (k + 1 <= g_wps.count && !g_wps.wp[k + 1].spawn && BotWp_Links(k, k + 1))
			k++;
		if (g_wps.npaths >= BOT_PATH_MAX) {
			if (!warned)
				gi.dprintf("Too many paths...some paths will not used for route-finding.\n");
			warned = true;
		} else {
			int p = g_wps.npaths++;
			g_wps.paths[p].start = i;
			g_wps.paths[p].end = k;
			for (int j = i; j <= k; j++)
				g_wps.wp[j].path = p;
		}
		i = k + 1;
	}
	for (int p = 0; p < g_wps.npaths; p++) {
		int ends[2] = {g_wps.paths[p].start, g_wps.paths[p].end};
		for (int k = 0; k < 2; k++) {
			int e = ends[k];
			const BotWaypoint &w = g_wps.wp[e];
			int best = -1;
			float bc = 0;
			for (int l = 0; l < w.nlinks; l++) {
				int t = BotWp_LinkTarget(w.link[l]);
				if (t < 1 || t > g_wps.count || w.cost[l] >= 16384.0f)
					continue;
				if (ConsecutiveNeighbour(e, t) || g_wps.wp[t].spawn)
					continue;
				if (best < 0 || w.cost[l] < bc) {
					best = t;
					bc = w.cost[l];
				}
			}
			if (best < 0) {
				/* no candidate: a dead-end record with other node 0 (black box, F5: the original's bot_wp_pathinfo
				 * lists "...with path 0 between nodes 53 and 0" for such an end, jpndm2 path 2) */
				AddDeadEnd(p, e, 0);
				continue;
			}
			int q = g_wps.wp[best].path;
			if (q < 0)
				AddDeadEnd(p, e, best);
			else {
				AddJuncture(p, e, q, best);
				/* a loop juncture (q = p) is seen from the same side both times: the second addition is refused,
				 * so it appears once, at the end that created it (SPEC-AMEND-006 F5.1) */
				if (q != p)
					AddJuncture(q, best, p, e);
			}
		}
	}
	/* verified after every path build while bot_wp_edit != 0 (SPEC-AMEND-006 F5.1) */
	if (bot_wp_edit && bot_wp_edit->value != 0)
		BotWp_VerifyPaths();
}

/* 1.07f handles path numbers as signed bytes in the route search (SPEC-AMEND-005 G5.7): routes only start in and cross
 * into paths numbered 1-127 (index 0-126 here) */
static bool RoutablePath(int p) { return p >= 0 && p < g_wps.npaths && (signed char)(p + 1) > 0; }

/* juncture sides: (path, index in that path's juncture list) */
struct Side {
	int p, i;
};
static const BotJuncture &SideJ(Side s) { return g_wps.pj[s.p][s.i]; }
static bool SideEq(Side a, Side b) { return a.p == b.p && a.i == b.i; }

/* the first entry in list order with the smallest |waypoint id - node| */
static Side NearestSide(int path, int node)
{
	Side best{-1, -1};
	int bd = 0;
	const std::vector<BotJuncture> &v = g_wps.pj[path];
	for (int i = 0; i < (int)v.size(); i++) {
		int d = abs(v[i].node - node);
		if (best.p < 0 || d < bd) {
			best = Side{path, i};
			bd = d;
		}
	}
	return best;
}

/* the matching entry on the other path (none for a dead end or a path the route search cannot enter) */
static Side CrossSide(Side s)
{
	const BotJuncture &j = SideJ(s);
	if (!RoutablePath(j.opath))
		return Side{-1, -1};
	const std::vector<BotJuncture> &v = g_wps.pj[j.opath];
	for (int i = 0; i < (int)v.size(); i++)
		if (v[i].node == j.onode && v[i].opath == s.p)
			return Side{j.opath, i};
	return Side{-1, -1};
}

bool BotWp_Route(int start, int dest, std::vector<BotRouteStep> &out)
{
	out.clear();
	if (start < 1 || start > g_wps.count || dest < 1 || dest > g_wps.count)
		return false;
	int ps = g_wps.wp[start].path, pd = g_wps.wp[dest].path;
	if (!RoutablePath(ps))
		return false; /* a start in no path (or in path 128+) gives no route */
	if (ps == pd)
		return true; /* walk along the path */
	Side from = NearestSide(ps, start);
	/* a destination in no path has no target juncture: only the 100-step cap gives a (truncated) route */
	Side to = pd >= 0 ? NearestSide(pd, dest) : Side{-1, -1};
	if (from.p < 0)
		return false;
	std::vector<std::vector<char>> visited(g_wps.npaths);
	for (int p = 0; p < g_wps.npaths; p++)
		visited[p].assign(g_wps.pj[p].size(), 0);
	struct Frame {
		Side s;
		int move; /* next move to try: 0 next, 1 cross, 2 previous, 3 done */
	};
	std::vector<Frame> st;
	st.push_back(Frame{from, 0});
	visited[from.p][from.i] = 1;
	int steps = 1;
	bool found = to.p >= 0 && SideEq(from, to);
	while (!found && !st.empty() && steps < 100) {
		Frame &f = st.back();
		if (f.move > 2) {
			st.pop_back();
			continue;
		}
		int mv = f.move++;
		Side nb{-1, -1};
		if (mv == 1)
			nb = CrossSide(f.s);
		else if (mv == 0 && f.s.i + 1 < (int)g_wps.pj[f.s.p].size())
			nb = Side{f.s.p, f.s.i + 1};
		else if (mv == 2 && f.s.i > 0)
			nb = Side{f.s.p, f.s.i - 1};
		if (nb.p < 0 || visited[nb.p][nb.i])
			continue;
		visited[nb.p][nb.i] = 1;
		steps++;
		st.push_back(Frame{nb, 0});
		if (to.p >= 0 && SideEq(nb, to))
			found = true;
	}
	/* the partial route is used when the search stops at its cap; an exhausted search has no route */
	for (auto &f : st)
		out.push_back(BotRouteStep{SideJ(f.s).node, f.s.p});
	return !out.empty();
}

//==========================================================================================
// automatic links (14.3)

static void AddSpawnWaypoints(void)
{
	for (int cls = 0; cls < 3; cls++)
		for (int e = 1; e < globals.num_edicts; e++) {
			edict_t *ent = g_edicts + e;
			if (!ent->inuse || !ent->classname || strcmp(ent->classname, g_spawn_classes[cls]))
				continue;
			bool have = false;
			for (int i = 1; i <= g_wps.count && !have; i++)
				if (Dist2(ent->s.origin, g_wps.wp[i].origin) <= 100.0f &&
				    BotWp_Visible(ent->s.origin, g_wps.wp[i].origin, NULL))
					have = true;
			if (have || g_wps.count >= BOT_WP_MAX)
				continue;
			BotWaypoint &w = g_wps.wp[++g_wps.count];
			memset(&w, 0, sizeof(w));
			VectorCopy(ent->s.origin, w.origin);
			w.path = -1;
		}
}

void BotWp_AutoLinks(void)
{
	AddSpawnWaypoints();
	BotWp_BuildSpawnTable();
	edict_t *client1 = g_edicts + 1;
	for (int i = 1; i <= g_wps.count; i++) {
		BotWaypoint &w = g_wps.wp[i];
		int cand[BOT_LINKS];
		float cd[BOT_LINKS];
		int nc = 0;
		for (int j = 1; j <= g_wps.count; j++) {
			if (j == i || g_wps.wp[j].spawn)
				continue;
			float d = Dist2(w.origin, g_wps.wp[j].origin);
			if (VectorCompare(w.origin, g_wps.wp[j].origin))
				continue; /* duplicate position */
			trace_t tr = gi.trace(w.origin, vec3_origin, vec3_origin, g_wps.wp[j].origin, client1, BOT_TRACE_MASK);
			bool ok = tr.fraction == 1.0f;
			if (!ok && tr.ent && tr.ent->classname && strstr(tr.ent->classname, "door"))
				ok = true;
			if (!ok)
				continue;
			/* keep the 8 nearest */
			int pos = nc;
			while (pos > 0 && cd[pos - 1] > d)
				pos--;
			if (pos >= BOT_LINKS)
				continue;
			int last = nc < BOT_LINKS ? nc : BOT_LINKS - 1;
			for (int k = last; k > pos; k--) {
				cand[k] = cand[k - 1];
				cd[k] = cd[k - 1];
			}
			cand[pos] = j;
			cd[pos] = d;
			if (nc < BOT_LINKS)
				nc++;
		}
		/* stored most expensive first */
		w.nlinks = nc;
		for (int k = 0; k < nc; k++) {
			int src = nc - 1 - k;
			w.link[k] = cand[src] | (sqrtf(cd[src]) < 150.0f ? WP_LINKFLAG : 0);
			w.cost[k] = cd[src];
		}
		if (!nc)
			BotConsoleId(BOTMSG(BM_PLACEMENT), i);
	}
	BotWp_BuildPaths();
}

//==========================================================================================
// item tagging (14.6)

static const int g_weapon_pref[16] = {0, 0, 1, 0, 2, 6, 3, 5, 4, 10, 7, 9, 8, 0, 0, 0};

void BotWp_TagItems(void)
{
	g_wps.blue_base = g_wps.red_base = 0;
	g_wps.control_points.clear();
	g_wps.control_flags.clear();
	g_wps.conquer_goal = 0;
	for (int i = 1; i <= g_wps.count; i++)
		g_wps.wp[i].pref = 0;
	for (int e = 1; e < globals.num_edicts; e++) {
		edict_t *ent = g_edicts + e;
		if (!ent->inuse || !ent->item)
			continue;
		int idx = ITEM_INDEX(ent->item);
		bool weapon = ent->item->kind == IK_WEAPON;
		if (!weapon && idx != 36 && idx != 39)
			continue;
		int best = 0;
		float bd = 64.0f * 64.0f;
		for (int i = 1; i <= g_wps.count; i++) {
			const float *w = g_wps.wp[i].origin;
			if (w[2] > ent->s.origin[2] + 41 || w[2] < ent->s.origin[2] - 45)
				continue;
			float d = Dist2(w, ent->s.origin);
			if (d > bd || (best && d >= bd))
				continue;
			if (!BotWp_Visible(ent->s.origin, w, ent))
				continue;
			best = i;
			bd = d;
		}
		if (!best)
			continue;
		if (idx == 36) {
			/* CTF flags: blue (team 1) and red (team 2) bases; the flag's team comes from the game-mode code */
			int team = BotMode_FlagTeam(ent);
			if (team == 2)
				g_wps.red_base = best;
			else if (team == 1)
				g_wps.blue_base = best;
		} else if (idx == 39) {
			g_wps.wp[best].kind = WPK_CONTROL;
			g_wps.control_points.push_back(best);
			g_wps.control_flags.push_back(ent);
		} else {
			int wid = ent->item->value;
			if (wid >= 0 && wid < 16)
				g_wps.wp[best].pref = g_weapon_pref[wid];
		}
	}
	if (deathmatch->value == 7) {
		for (int i = 1; i <= g_wps.count; i++)
			if (GM_InConquerZone(g_wps.wp[i].origin)) {
				g_wps.conquer_goal = i;
				break;
			}
	}
}

//==========================================================================================
// initialisation (1.2)

void BotWp_Init(void)
{
	BotWp_Reset();
	g_wps.initialised = true;
	if (bot_waypoints->value == 0) {
		g_wps.roam = true;
		g_wps.ready = true;
		return;
	}
	char base[MAX_QPATH];
	BotMapBase(base, sizeof(base));
	if (!BotWp_Load(base)) {
		char path[MAX_QPATH * 2];
		Com_sprintf(path, sizeof(path), "bot/wpfiles/%s.wrs", base);
		BotConsoleId(BOTMSG(BM_NOFILE), path, level.mapname);
		g_wps.ready = false;
		g_wps.editmode = bot_wp_edit->value != 0;
		return;
	}
	if (bot_wp_edit->value != 0) {
		g_wps.editmode = true;
		g_wps.ready = false;
		BotDevMsg("In waypoint edit mode. Use writewpfile to save any changes..\n"); /* two dots as 1.07f */
	} else {
		g_wps.ready = true;
		BotDevMsg("Waypoints loaded and placed from file.\n");
	}
	BotWp_TagItems();
}

//==========================================================================================
// writing (14.10)

bool BotWp_Write(const char *name)
{
	if (!name && !g_wps.editmode && g_wps.written)
		return true;
	BotWp_AutoLinks();
	char base[MAX_QPATH];
	if (name && name[0])
		BotStrCpy(base, name, sizeof(base));
	else
		BotMapBase(base, sizeof(base));
	char path[MAX_OSPATH];
	Com_sprintf(path, sizeof(path), "%s/bot/wpfiles/%s.wrs", gix.FS_Gamedir(), base);
	gix.FS_CreatePath(path);
	FILE *f = fopen(path, "wt");
	if (!f) {
		BotConsoleId(BOTMSG(BM_CANTCREATE), path);
		return false;
	}
	fprintf(f, "Version 5\n");
	static const char *sect[3] = {"dm", "team1", "team2"};
	for (int cls = 0; cls < 3; cls++) {
		fprintf(f, "begin %s\n", sect[cls]);
		for (auto &r : g_wps.spawns)
			if (r.cls == cls)
				fprintf(f, "%d %d\n", r.ordinal, r.wp);
		fprintf(f, "end %s\n", sect[cls]);
	}
	for (int i = 1; i <= g_wps.count; i++) {
		const BotWaypoint &w = g_wps.wp[i];
		char org[64], tag[24], links[256];
		Com_sprintf(org, sizeof(org), "(%d %d %d)", (int)w.origin[0], (int)w.origin[1], (int)w.origin[2]);
		if (w.tag)
			Com_sprintf(tag, sizeof(tag), " c%d ", w.tag);
		else
			BotStrCpy(tag, " ", sizeof(tag));
		float nd = i < g_wps.count ? Dist2(w.origin, g_wps.wp[i + 1].origin) : 0.0f;
		links[0] = 0;
		for (int l = 0; l < w.nlinks && l < BOT_LINKS; l++) {
			char one[32];
			Com_sprintf(one, sizeof(one), " %d %9.1f", w.link[l], w.cost[l]);
			BotStrCat(links, one, sizeof(links));
		}
		fprintf(f, "%d %d %s%s%9.1f %s\n", i, w.kind, org, tag, nd, links);
	}
	fclose(f);
	g_wps.written = true;
	BotConsoleId(BOTMSG(BM_WRITTEN), name && name[0] ? "backup file" : path);
	BotWp_TagItems();
	return true;
}

//==========================================================================================
// editing (14.11)

static void RenumberAfterInsert(int at)
{
	for (int i = 1; i <= g_wps.count; i++)
		if (g_wps.wp[i].tag >= at)
			g_wps.wp[i].tag++;
}

static bool PlaceWaypoint(edict_t *ent, int kind, int at)
{
	if (!g_wps.editmode)
		return false;
	if (g_wps.count >= BOT_WP_MAX) {
		BotConsoleId(BOTMSG(BM_LIMIT), BOT_WP_MAX);
		gi.cvar_set("bot_wp_info", "0");
		return false;
	}
	BotWaypoint w;
	memset(&w, 0, sizeof(w));
	VectorCopy(ent->s.origin, w.origin);
	w.kind = kind;
	w.path = -1;
	if (at > 0 && at < g_wps.count) {
		RenumberAfterInsert(at);
		memmove(&g_wps.wp[at + 1], &g_wps.wp[at], sizeof(BotWaypoint) * (g_wps.count - at + 1));
		g_wps.wp[at] = w;
		g_wps.count++;
		g_wps.edit_cursor = at;
		BotWp_AutoLinks();
	} else {
		g_wps.wp[++g_wps.count] = w;
		g_wps.edit_cursor = g_wps.count;
	}
	return true;
}

static void RemoveWaypoint(int at)
{
	if (at < 1 || at > g_wps.count)
		return;
	int partner = g_wps.wp[at].tag;
	if (partner >= 1 && partner <= g_wps.count && g_wps.wp[partner].tag == at)
		g_wps.wp[partner].tag = 0;
	memmove(&g_wps.wp[at], &g_wps.wp[at + 1], sizeof(BotWaypoint) * (g_wps.count - at));
	g_wps.count--;
	for (int i = 1; i <= g_wps.count; i++)
		if (g_wps.wp[i].tag > at)
			g_wps.wp[i].tag--;
	if (g_wps.edit_cursor > g_wps.count)
		g_wps.edit_cursor = g_wps.count;
	BotWp_AutoLinks();
}

static int EditCursor(void)
{
	if (bot_wp_info && bot_wp_info->value != 0 && g_edicts[1].inuse)
		return BotWp_Nearest(g_edicts[1].s.origin, 1e9f, false, NULL, false);
	return g_wps.edit_cursor;
}

static const char *KindName(int k)
{
	switch (k) {
	case WPK_STANDARD: return "standard";
	case WPK_CROUCH: return "crouch";
	case WPK_JUMP: return "jump";
	case WPK_CONTROL: return "control";
	case WPK_ELEVATOR: return "elevator";
	case WPK_VISIBLE: return "force visible"; /* UNVERIFIED names for 9, 10, 11 */
	case WPK_LADDER_BOTTOM: return "bottom of ladder";
	case WPK_LADDER_TOP: return "top of ladder";
	}
	return "standard";
}

/* SPEC-AMEND-006 F5.1: every ordered pair of different records of a path with equal own-side waypoints reports that
 * waypoint, unless its id was reported earlier in this run (the list is shared by all paths) */
void BotWp_VerifyPaths(void)
{
	std::vector<int> reported;
	for (int p = 0; p < g_wps.npaths; p++) {
		const std::vector<BotJuncture> &v = g_wps.pj[p];
		for (size_t i = 0; i < v.size(); i++)
			for (size_t k = 0; k < v.size(); k++) {
				if (i == k || v[i].node != v[k].node)
					continue;
				bool seen = false;
				for (int r : reported)
					if (r == v[i].node)
						seen = true;
				if (seen)
					continue;
				gi.dprintf("ERROR! Path %d has two junctures coming off of waypoint %d. That's bad.\n", p + 1, v[i].node);
				reported.push_back(v[i].node);
			}
	}
	int n = (int)reported.size();
	gi.dprintf("Paths verified. %d error%sreported.\n", n, n == 1 ? " " : "s ");
}

int BotWp_EditorCommand(edict_t *ent, const char *cmd)
{
	int argc = gi.argc();
	int n = argc > 1 ? atoi(gi.argv(1)) : 0;
	if (!Q_stricmp(cmd, "makewp")) {
		PlaceWaypoint(ent, (ent->client && (ent->client->ps.pmove.pm_flags & PMF_DUCKED)) ? WPK_CROUCH : WPK_STANDARD,
		              n);
	} else if (!Q_stricmp(cmd, "makewpj"))
		PlaceWaypoint(ent, WPK_JUMP, 0);
	else if (!Q_stricmp(cmd, "makewp_nv"))
		PlaceWaypoint(ent, WPK_VISIBLE, 0);
	else if (!Q_stricmp(cmd, "makewp_el"))
		PlaceWaypoint(ent, WPK_ELEVATOR, n);
	else if (!Q_stricmp(cmd, "remwp"))
		RemoveWaypoint(EditCursor());
	else if (!Q_stricmp(cmd, "teletowp")) {
		if (g_wps.count >= 1 && bot_waypoints->value != 0) {
			int w = (n <= 0 || n > g_wps.count) ? g_wps.count : n;
			gi.unlinkentity(ent);
			VectorCopy(g_wps.wp[w].origin, ent->s.origin);
			if (ent->client)
				for (int i = 0; i < 3; i++)
					ent->client->ps.pmove.origin[i] = (short)(ent->s.origin[i] * 8);
			gi.linkentity(ent);
		}
	} else if (!Q_stricmp(cmd, "writewpfile"))
		BotWp_Write(argc > 1 ? gi.argv(1) : NULL);
	else if (!Q_stricmp(cmd, "bot_wp_connect")) {
		int a = n, b = argc > 2 ? atoi(gi.argv(2)) : 0;
		if (a >= 1 && a <= g_wps.count) {
			if (b >= 1 && b <= g_wps.count) {
				if (!g_wps.wp[a].tag && !g_wps.wp[b].tag) {
					g_wps.wp[a].tag = b;
					g_wps.wp[b].tag = a;
				}
			} else {
				int p = g_wps.wp[a].tag;
				if (p >= 1 && p <= g_wps.count)
					g_wps.wp[p].tag = 0;
				g_wps.wp[a].tag = 0;
			}
		}
	} else if (!Q_stricmp(cmd, "bot_wp_drawonly")) {
		const char *s = argc > 1 ? gi.argv(1) : "0";
		g_wps.draw_from = atoi(s);
		const char *dot = strchr(s, '.');
		g_wps.draw_to = dot ? atoi(dot + 1) : 0;
		if (!dot)
			g_wps.draw_from = 0;
	} else if (!Q_stricmp(cmd, "bot_wp_verifypaths"))
		BotWp_VerifyPaths();
	else if (!Q_stricmp(cmd, "bot_wp_whichpath")) {
		/* SPEC-AMEND-006 F5.2: no argument, a non-number or 0: nothing */
		if (n == 0)
			;
		else if (n >= 1 && n <= g_wps.count && g_wps.wp[n].path >= 0)
			gi.dprintf("Node %d is in path %d\n", n, g_wps.wp[n].path + 1);
		else
			gi.dprintf("Node %d is not in a path (probably doesn't exist)\n", n);
	} else if (!Q_stricmp(cmd, "bot_wp_pathinfo")) {
		/* SPEC-AMEND-006 F5.2: nothing for 0 / no argument. Valid numbers: 1 ... number of paths (black box, F5: on
		 * jpndm2, 15 paths, `bot_wp_pathinfo 15` lists path 15; F5.2's "paths - 1" counts an extra slot). An invalid
		 * number prints an unrelated large number in 1.07f; OpenMerc prints the number given. */
		if (n == 0)
			;
		else if (n >= 1 && n <= g_wps.npaths) {
			gi.dprintf("PATH %d: start node = %d, end node = %d\n", n, g_wps.paths[n - 1].start, g_wps.paths[n - 1].end);
			gi.dprintf("---- %d Junctures ----\n", (int)g_wps.pj[n - 1].size());
			for (auto &j : g_wps.pj[n - 1])
				gi.dprintf("...with path %d between nodes %d and %d\n", j.opath + 1, j.node, j.onode);
		} else
			gi.dprintf("%d is not a valid path.\n", n);
	} else if (!Q_stricmp(cmd, "bot_wp_botroute")) {
		Bot *best = NULL;
		float bd = 0;
		for (int i = 1; i <= game.maxclients; i++) {
			Bot *b = &g_bots[i];
			if (!b->inuse || !g_edicts[i].inuse)
				continue;
			float d = Dist2(g_edicts[i].s.origin, g_edicts[1].s.origin);
			if (!best || d < bd) {
				best = b;
				bd = d;
			}
		}
		if (best) {
			/* SPEC-AMEND-006 F5.4: header `BOT: <name> near node <last waypoint>, path <its path>  DESTINATION: <dest>
			 * <origin>` (dest `node <id>` or `NONE`, origin empty without a destination); then the bot's STORED
			 * route: its current step as step 0, then every stored step from the first. OpenMerc stores the route as
			 * juncture sides (one per side, two for a crossing), 1.07f as juncture records (one line per record, the
			 * side on the current path), so a crossing lists both sides here; no route: only the header. The
			 * "Man, I don't know. Try again." line of 1.07f (a stored route whose records do not chain) cannot occur
			 * with side lists. */
			int near = best->last_wp;
			int path = near >= 1 && near <= g_wps.count ? g_wps.wp[near].path + 1 : 0;
			char d1[64] = "", d2[32] = "NONE";
			if (best->dest_wp) {
				BotStrCpy(d1, vtos(g_wps.wp[best->dest_wp].origin), sizeof(d1));
				Com_sprintf(d2, sizeof(d2), "node %d", best->dest_wp);
			}
			gi.dprintf("BOT: %s near node %d, path %d  DESTINATION: %s %s\n", best->userinfo[0] ?
			           Info_ValueForKey(best->userinfo, "name") : "", near, path, d2, d1);
			const BotRouteCache &rc = best->route;
			if (best->dest_wp && rc.dest == best->dest_wp && !rc.steps.empty()) {
				int k = 0;
				if (rc.pos >= 0 && rc.pos < (int)rc.steps.size())
					gi.dprintf("Step %d: node %d on path %d\n", k++, rc.steps[rc.pos].node, rc.steps[rc.pos].path + 1);
				else
					k = 1;
				for (size_t i = 0; i < rc.steps.size(); i++)
					gi.dprintf("Step %d: node %d on path %d\n", k++, rc.steps[i].node, rc.steps[i].path + 1);
			}
		}
	} else
		return 0;
	return 1;
}

//==========================================================================================
// drawing (14.11): SPEC-401 temp entities 64 (box), 27 (line), 47 (ring)

static void TeBox(const vec3_t c, float half, int r, int g, int b)
{
	vec3_t mn = {c[0] - half, c[1] - half, c[2] - half}, mx = {c[0] + half, c[1] + half, c[2] + half};
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(64);
	gi.WritePosition(mn);
	gi.WritePosition(mx);
	gi.WriteByte(r);
	gi.WriteByte(g);
	gi.WriteByte(b);
	gi.multicast(c, MULTICAST_PVS);
}

static void TeLine(const vec3_t a, const vec3_t b, unsigned rgba)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(27);
	gi.WritePosition(a);
	gi.WritePosition(b);
	gi.WriteLong(1);
	gi.WriteLong((int)rgba);
	gi.multicast(a, MULTICAST_PVS);
}

static void TeRing(const vec3_t p)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(47);
	gi.WritePosition(p);
	gi.WriteShort(16);
	gi.multicast(p, MULTICAST_PVS);
}

void BotWp_EditFrame(void)
{
	if (!g_wps.editmode || g_wps.count < 1)
		return;
	edict_t *viewer = g_edicts + 1;
	if (!viewer->inuse)
		return;
	int cursor = EditCursor();
	int drawn = 0, tries = 0;
	while (drawn < 2 && tries < g_wps.count) {
		tries++;
		g_wps.draw_rr = g_wps.draw_rr % g_wps.count + 1;
		int i = g_wps.draw_rr;
		if (g_wps.draw_from && (i < g_wps.draw_from || (g_wps.draw_to && i > g_wps.draw_to)))
			continue;
		const BotWaypoint &w = g_wps.wp[i];
		if (!BotWp_Visible(viewer->s.origin, w.origin, viewer))
			continue;
		drawn++;
		int r = 250, g = 0, b = 0;
		if (i == cursor)
			r = gix.irand(0, 250), g = gix.irand(0, 250), b = gix.irand(0, 250); /* multicoloured */
		else if (w.kind == WPK_CROUCH)
			r = 0, g = 250;
		else if (w.kind == WPK_JUMP)
			r = 0, b = 250;
		TeBox(w.origin, 10, r, g, b);
		if (i < g_wps.count && BotWp_Links(i, i + 1) && BotWp_Visible(w.origin, g_wps.wp[i + 1].origin, viewer)) {
			bool ladder = w.kind == WPK_LADDER_BOTTOM && g_wps.wp[i + 1].kind == WPK_LADDER_TOP;
			TeLine(w.origin, g_wps.wp[i + 1].origin, ladder ? 0xff00fafa : 0xff0000fa);
		}
		if (w.tag >= 1 && w.tag <= g_wps.count)
			TeLine(w.origin, g_wps.wp[w.tag].origin, 0xfffa0000);
		if (w.path >= 0)
			for (auto &j : g_wps.pj[w.path])
				if (j.node == i) {
					TeRing(w.origin);
					break;
				}
	}
	if (bot_wp_info && bot_wp_info->value != 0 && cursor >= 1) {
		const BotWaypoint &w = g_wps.wp[cursor];
		gi.centerprintf(viewer, "%s #%d of %d    type: %s    origin: %s", w.spawn ? "**SPAWNPOINT**\nnode" : "node",
		                cursor, g_wps.count, KindName(w.kind), vtos(w.origin));
	}
}
