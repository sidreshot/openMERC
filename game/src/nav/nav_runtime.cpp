/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-740 rules 1-11 (loading, grid lookup, player update and seen flood, cache refresh, node choice,
 *             move-point query, flee spot, look-around node, step test, hidden spawn spot, cone helper), Constants
 * Owner: package G5 (game/src/nav). Interface: nav_api.h.
 */
#include "nav_api.h"
#include "nav_data.h"
#include "nav_internal.h"

using namespace om_nav;

NavLevel g_nav;

static cvar_t *ai_nonodes_cv, *aipoints_show_cv;

static int DefaultDoorPredicate(edict_t *door, edict_t *user);
static nav_door_predicate_t g_door_pred = DefaultDoorPredicate;

#define NAV_MASK 0x2020003 /* SPEC-740: solid | window | monster clip | monster */
#define LIQUID_CONTENTS 0x38

static bool NoNodes(void)
{
	if (!ai_nonodes_cv)
		ai_nonodes_cv = gi.cvar("ai_nonodes", "0", 0);
	return ai_nonodes_cv->value != 0;
}

static bool PointsShow(void)
{
	if (!aipoints_show_cv)
		aipoints_show_cv = gi.cvar("aipoints_show", "0", 0);
	return aipoints_show_cv->value != 0;
}

static inline bool ValidNode(int i) { return i >= 0 && i < g_nav.data.n; }

const NavLayer *Nav_Lookup(const float p[3]) { return g_nav.loaded ? LookupLayer(&g_nav.data, p) : NULL; }

static int LayerFirstNode(const NavLayer *l) { return l ? l->nodes[0] : -1; }

//==========================================================================================
// rule 1: loading and freeing

void Nav_FreeData(void)
{
	if (g_nav.loaded)
		FreeNvb(&g_nav.data);
	free(g_nav.stamp);
	free(g_nav.marks);
	g_nav.stamp = NULL;
	g_nav.marks = NULL;
	g_nav.loaded = false;
	memset(&g_nav.data, 0, sizeof(g_nav.data));
}

void Nav_LevelReset(void)
{
	Nav_FreeData();
	Nav_EditorShutdown(); /* the editor's entities went with the previous level */
	g_nav.player_node = 0;
	g_nav.player_layer = -1;
	g_nav.search_counter = 0;
	ai_nonodes_cv = gi.cvar("ai_nonodes", "0", 0);
	aipoints_show_cv = gi.cvar("aipoints_show", "0", 0);

	char path[MAX_QPATH * 2];
	Com_sprintf(path, sizeof(path), "maps/%s.nvb", level.mapname);
	void *buf = NULL;
	int len = gix.FS_LoadFile(path, &buf, 0);
	if (len <= 0 || !buf) {
		if (buf)
			gix.FS_FreeFile(buf);
		return; /* missing: no navigation, silently */
	}
	int version = len >= 4 ? (int)(signed char)((uint8_t *)buf)[0] : 0;
	if (len < 4 || (((uint8_t *)buf)[0] != NVB_VERSION)) {
		gi.dprintf("Invalid version (%d) for Info Point File (Should be %d) - please re-export the path data\n",
		           version, NVB_VERSION);
		gix.FS_FreeFile(buf);
		return;
	}
	char err[128];
	NavData d;
	bool ok = ParseNvb((const uint8_t *)buf, (size_t)len, &d, err, sizeof(err));
	gix.FS_FreeFile(buf);
	if (!ok) {
		/* the original trusts the file; a truncated one is refused here instead of reading past its end */
		gi.dprintf("%s: %s\n", path, err);
		return;
	}
	RaiseOrigins(&d);
	g_nav.data = d;
	g_nav.loaded = true;
	g_nav.stamp = (float *)calloc(d.n > 0 ? d.n : 1, sizeof(float));
	g_nav.marks = (uint8_t *)calloc(d.nlayers > 0 ? d.nlayers : 1, 1);
}

void Nav_WorldSpawn(void) { memset(g_nav.seen, 0, sizeof(g_nav.seen)); }

void Nav_Shutdown(void)
{
	Nav_FreeData();
	Nav_EditorShutdown();
}

int Nav_NodeCount(void) { return g_nav.loaded ? g_nav.data.n : 0; }
int Nav_Active(void) { return Nav_NodeCount() > 0 && !NoNodes(); }

const float *Nav_NodeOrigin(int node)
{
	if (!g_nav.loaded || !ValidNode(node))
		return NULL;
	return g_nav.data.nodes[node].origin;
}

float Nav_NodeClearance(int node)
{
	if (!g_nav.loaded || !ValidNode(node))
		return 0;
	return g_nav.data.nodes[node].clearance;
}

int Nav_PlayerNode(void) { return g_nav.player_node; }
int Nav_PlayerLayer(void) { return g_nav.player_layer; }
int Nav_NodeSeen(int node) { return node >= 0 && node < NAV_SEEN_MAX ? g_nav.seen[node] : 0; }

//==========================================================================================
// rule 3: per-frame player update and the seen flood

static void Mark(const NavLayer *l)
{
	if (l && g_nav.marks && l->id >= 0 && l->id < g_nav.data.nlayers)
		g_nav.marks[l->id] = (uint8_t)((g_nav.marks[l->id] + 1) & 3);
}

static void SeenFlood(int node, const float *eye, float stamp)
{
	/* explicit stack: depth-first in link order, like the recursive description */
	struct Frame {
		int node;
		int link;
	};
	Frame stack[2048];
	int sp = 0;
	auto visit = [&](int q) -> bool {
		NavNode &nd = g_nav.data.nodes[q];
		if (q < NAV_SEEN_MAX && !g_nav.seen[q] && gi.inPVS(nd.origin, eye))
			g_nav.seen[q] = 1;
		g_nav.stamp[q] = stamp;
		return q < NAV_SEEN_MAX && g_nav.seen[q];
	};
	if (!ValidNode(node))
		return;
	if (!visit(node))
		return;
	stack[sp++] = Frame{node, 0};
	while (sp > 0) {
		Frame &f = stack[sp - 1];
		NavNode &nd = g_nav.data.nodes[f.node];
		if (f.link >= nd.nlinks) {
			sp--;
			continue;
		}
		int t = nd.links[f.link++].target;
		if (!ValidNode(t) || g_nav.stamp[t] == stamp)
			continue;
		if (visit(t) && sp < 2048)
			stack[sp++] = Frame{t, 0};
	}
}

void Nav_PlayerFrame(edict_t *sight)
{
	if (!sight || NoNodes() || Nav_NodeCount() == 0)
		return;
	if (g_nav.marks)
		memset(g_nav.marks, 0, g_nav.data.nlayers > 0 ? g_nav.data.nlayers : 1);
	const NavLayer *l = Nav_Lookup(sight->s.origin);
	if (l) {
		g_nav.player_layer = l->id;
		int n = l->nodes[0];
		g_nav.player_node = (n < 1 || n >= g_nav.data.n) ? 0 : n;
	}
	static const float corner[4][2] = {{-16, -16}, {16, -16}, {-16, 16}, {16, 16}};
	for (int i = 0; i < 4; i++) {
		vec3_t p = {sight->s.origin[0] + corner[i][0], sight->s.origin[1] + corner[i][1], sight->s.origin[2]};
		const NavLayer *c = Nav_Lookup(p);
		Mark(c);
		g_nav.around[i] = c ? c->id : -1;
	}
	g_nav.search_counter++;
	SeenFlood(g_nav.player_node, sight->s.origin, (float)g_nav.search_counter);
}

//==========================================================================================
// rule 4: per-character cache refresh

void Nav_InitCache(NavCache *c)
{
	memset(c, 0, sizeof(*c));
	c->current_layer = -1;
	for (int i = 0; i < 4; i++)
		c->layer_copy[i] = -1;
}

void Nav_RefreshCache(NavCache *c, edict_t *ent)
{
	if (!c || !ent || NoNodes() || Nav_NodeCount() == 0)
		return;
	const float *o = ent->s.origin;
	VectorSet(c->ref, o[0], o[1], o[2] + 16);
	const NavLayer *l = Nav_Lookup(c->ref);
	if (l)
		Mark(l);
	else {
		/* fallbacks as the original computes them (not symmetric) */
		static const float off[4][2] = {{-16, -16}, {32, 0}, {32, 32}, {0, 32}};
		for (int i = 0; i < 4 && !l; i++) {
			vec3_t p = {o[0] + off[i][0], o[1] + off[i][1], o[2] + 16};
			l = Nav_Lookup(p);
		}
	}
	int layer = l ? l->id : -1;
	c->current_layer = layer;
	if (l) {
		int node = l->nodes[0];
		if (node != c->current_node) {
			c->history[2] = c->history[1];
			c->history[1] = c->history[0];
			c->history[0] = c->current_node;
			c->current_node = node;
		}
	} else
		c->current_node = 0;
	for (int i = 0; i < 4; i++)
		c->layer_copy[i] = layer;
}

//==========================================================================================
// door predicate (SPEC-710 C9 wording; G2 may install the precise one)

static int DefaultDoorPredicate(edict_t *door, edict_t *user)
{
	(void)user;
	if (!door || !door->inuse || !door->classname)
		return 0;
	bool doorlike = !Q_strncasecmp(door->classname, "func_door", 9);
	bool usekey = door->plUse != NULL;
	if (!doorlike && usekey)
		doorlike = !Q_strncasecmp(door->classname, "func_button", 11) ||
		           !Q_strncasecmp(door->classname, "trigger_use", 11);
	if (!doorlike)
		return 0;
	if (!usekey)
		return 1;
	if (door->moveinfo.state != 1 /* closed (STATE_BOTTOM) */)
		return 0;
	if (door->spawnflags & 8)
		return 0;
	return level.time - door->lastusetime > 5.0f;
}

void Nav_SetDoorPredicate(nav_door_predicate_t fn) { g_door_pred = fn ? fn : DefaultDoorPredicate; }
int Nav_DoorUsableNow(edict_t *door, edict_t *user) { return g_door_pred(door, user); }

static bool HitUsableDoor(const trace_t &tr, edict_t *user)
{
	return tr.ent && tr.ent != g_edicts && g_door_pred(tr.ent, user);
}

static bool HitDoorLike(const trace_t &tr)
{
	if (!tr.ent || tr.ent == g_edicts || !tr.ent->classname)
		return false;
	if (!Q_strncasecmp(tr.ent->classname, "func_door", 9))
		return true;
	return tr.ent->plUse != NULL && (!Q_strncasecmp(tr.ent->classname, "func_button", 11) ||
	                                 !Q_strncasecmp(tr.ent->classname, "trigger_use", 11));
}

//==========================================================================================
// debug drawing (aipoints_show): SPEC-401 temp entities 27 (line) and 47 (shock ring)

void Nav_DebugLine(const vec3_t a, const vec3_t b, unsigned color, int duration)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(27);
	gi.WritePosition(a);
	gi.WritePosition(b);
	gi.WriteLong(duration);
	gi.WriteLong((int)color);
	gi.multicast(a, MULTICAST_ALL);
}

static void DebugRing(const vec3_t p, int size)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(47);
	gi.WritePosition(p);
	gi.WriteShort(size);
	gi.multicast(p, MULTICAST_ALL);
}

//==========================================================================================
// rule 9: step test

static bool InLiquid(const vec3_t endpos, float boxbottom)
{
	vec3_t p = {endpos[0], endpos[1], endpos[2] + boxbottom + 1};
	return (gi.pointcontents(p) & LIQUID_CONTENTS) != 0;
}

static bool BoxClearAt(edict_t *ent, const vec3_t p, int mask, bool accept_door)
{
	trace_t tr = gi.trace(p, ent->mins, ent->maxs, p, ent, mask);
	if (!tr.startsolid && !tr.allsolid)
		return true;
	return accept_door && HitUsableDoor(tr, ent);
}

int Nav_StepTest(edict_t *ent, float h, const vec3_t v)
{
	if (!ent)
		return 0;
	const float *E = ent->s.origin;
	if (ent->movetype != MOVETYPE_STEP) {
		if (DotProduct(v, v) < 0.01f)
			return 1;
		int mask = ent->movetype == MOVETYPE_NOCLIP ? 0 : NAV_MASK;
		for (int attempt = 0; attempt < 2; attempt++) {
			vec3_t end = {E[0] + v[0], E[1] + v[1], E[2] + (attempt == 0 ? v[2] : 0)};
			trace_t tr = gi.trace(E, ent->mins, ent->maxs, end, ent, mask);
			if ((ent->flags & FL_FLY) && ent->waterlevel == 0 && InLiquid(tr.endpos, ent->mins[2]))
				return 0;
			if ((ent->flags & FL_SWIM) && ent->waterlevel < 2 && !InLiquid(tr.endpos, ent->mins[2]))
				return 0;
			if (tr.fraction == 1.0f) {
				if (ent->movetype != MOVETYPE_NOCLIP && !BoxClearAt(ent, tr.endpos, mask, false))
					return 0;
				return 1;
			}
		}
		return 0;
	}
	/* "step" characters */
	if (!ent->groundentity || DotProduct(ent->velocity, ent->velocity) > 100.0f)
		return 0;
	if (DotProduct(v, v) < 0.01f)
		return 1;
	vec3_t base = {E[0] + v[0], E[1] + v[1], E[2] + v[2]};
	vec3_t end = {base[0], base[1], base[2] - h};
	const float heights[3] = {h, h * 0.5f, 0};
	trace_t tr;
	bool ok = false, door = false;
	for (int i = 0; i < 3; i++) {
		vec3_t start = {base[0], base[1], base[2] + heights[i]};
		tr = gi.trace(start, ent->mins, ent->maxs, end, ent, NAV_MASK);
		if (!tr.startsolid && !tr.allsolid) {
			ok = true;
			break;
		}
		if (HitUsableDoor(tr, ent)) {
			ok = door = true;
			break;
		}
	}
	if (!ok)
		return 0;
	if (ent->waterlevel == 0 && InLiquid(tr.endpos, ent->mins[2]))
		return 0;
	if (!BoxClearAt(ent, tr.endpos, NAV_MASK, true))
		return 0;
	edict_t *o = tr.ent;
	if (o && o != g_edicts && o != ent->groundentity && (o->client || (o->svflags & SVF_MONSTER)) &&
	    !(o->deadflag & 2)) {
		if (tr.endpos[2] > o->s.origin[2] + o->maxs[2] + ent->mins[2] - 1)
			return 0;
	}
	(void)door;
	/* the original's ledge check here runs on the current origin and never fails a pass (SPEC-740 rule 9.7) */
	return 1;
}

//==========================================================================================
// rule 6: move-point query

static void SetDirect(NavCache *c, const vec3_t F)
{
	VectorCopy(F, c->movepoint);
	c->direct = 1;
}

int Nav_MovePoint(NavCache *c, edict_t *ent, float step_height, const vec3_t F, const vec3_t G, int mode,
                  edict_t *ignore)
{
	if (!c || !ent)
		return 0;
	/* 1 */
	if (NoNodes() || Nav_NodeCount() == 0 || c->current_node == 0) {
		SetDirect(c, F);
		return 0;
	}
	NavData &d = g_nav.data;
	const bool approach = mode == NAV_APPROACH;
	/* 2 */
	const NavLayer *gl = Nav_Lookup(G);
	if (!gl) {
		c->chosen_node = 0;
		SetDirect(c, F);
		return 0;
	}
	c->goal_node = gl->nodes[0];
	c->direct = 0;
	if (gl->nodes[0] == -1) {
		c->chosen_node = 0;
		SetDirect(c, F);
		return 0;
	}
	int goal = c->goal_node;
	/* 3 */
	const NavLayer *cl = (c->current_layer >= 0 && c->current_layer < d.nlayers && d.layer_by_id)
	                         ? d.layer_by_id[c->current_layer]
	                         : NULL;
	int C = cl ? ChooseNodeOnLayer(&d, cl, goal, !approach) : -1;
	if (C < 1)
		C = 0;
	if (C) {
		if (!ValidNode(C) || !d.nodes[C].next_link) {
			gi.dprintf("Error!  Problem with path!\n");
			C = 0;
		} else if (C != goal) {
			int nl = ValidNode(goal) ? d.nodes[C].next_link[goal] : 0;
			if (nl == 0 || nl > d.nodes[C].nlinks || d.nodes[C].links[nl - 1].target < 1)
				C = 0;
		}
	}
	c->chosen_node = C;
	const float *E = ent->s.origin;
	bool skip_step = false;
	if (!C) {
		/* 4 */
		SetDirect(c, F);
	} else {
		/* 5 */
		vec3_t X;
		bool probe = true;
		const float *Co = d.nodes[C].origin;
		vec3_t GE, CE;
		VectorSubtract(G, E, GE);
		VectorSubtract(Co, E, CE);
		if (approach && gl->id == c->current_layer) {
			VectorCopy(G, X);
			probe = false;
		} else if (approach && C == goal) {
			if (DotProduct(GE, CE) >= 0)
				VectorCopy(Co, X);
			else
				VectorCopy(G, X);
		} else if (!approach && C == c->current_node) {
			if (DotProduct(GE, CE) <= 0)
				VectorCopy(Co, X);
			else
				VectorCopy(F, X);
		} else
			VectorCopy(Co, X);

		if (!probe)
			VectorCopy(X, c->movepoint);
		else {
			/* 6 obstacle probe */
			vec3_t mins = {ent->mins[0] + 1, ent->mins[1] + 1, ent->mins[2] + 10};
			vec3_t maxs = {ent->maxs[0] - 1, ent->maxs[1] - 1, ent->maxs[2]};
			vec3_t dir, end;
			VectorSubtract(X, E, dir);
			VectorNormalize(dir);
			VectorMA(E, 64, dir, end);
			trace_t tr = gi.trace(E, mins, maxs, end, ent, NAV_MASK);
			bool clear = (tr.fraction > 0.99f || (ignore && tr.ent == ignore)) && !tr.startsolid && !tr.allsolid;
			if (clear)
				VectorCopy(X, c->movepoint);
			else if (approach && HitDoorLike(tr)) {
				if (g_door_pred(tr.ent, ent)) {
					VectorCopy(X, c->movepoint);
					skip_step = true;
				} else
					VectorMA(tr.endpos, 64, tr.plane.normal, c->movepoint);
			} else {
				vec3_t ang, f, r, pr, pl;
				vectoangles(dir, ang);
				AngleVectors(ang, f, r, NULL);
				for (int i = 0; i < 3; i++) {
					pr[i] = E[i] + 64 * f[i] + 64 * r[i];
					pl[i] = E[i] + 64 * f[i] - 64 * r[i];
				}
				trace_t trr = gi.trace(E, mins, maxs, pr, ent, NAV_MASK);
				trace_t trl = gi.trace(E, mins, maxs, pl, ent, NAV_MASK);
				auto open = [&](const trace_t &t) {
					if (t.fraction > 0.1f && !t.startsolid && !t.allsolid)
						return true;
					return approach && HitUsableDoor(t, ent);
				};
				bool ro = open(trr), lo = open(trl);
				float a = trr.fraction, b = trl.fraction;
				if (!ro && !lo)
					VectorCopy(F, c->movepoint);
				else if ((lo && !ro) || (lo && ro && b >= a)) {
					for (int i = 0; i < 3; i++)
						c->movepoint[i] = E[i] + 32 * b * f[i] - (64 - 32 * b) * r[i];
				} else {
					for (int i = 0; i < 3; i++)
						c->movepoint[i] = E[i] + 32 * a * f[i] + (64 - 32 * a) * r[i];
				}
			}
		}
	}
	/* 7 finish */
	VectorCopy(F, c->ref);
	vec3_t m;
	VectorSubtract(c->movepoint, F, m);
	float m2 = DotProduct(m, m);
	if (m2 >= 0.1f) {
		vec3_t u;
		float len = sqrtf(m2);
		VectorScale(m, 1.0f / len, u);
		if (DotProduct(u, c->last_dir) >= -0.717f || level.time - c->last_dir_time >= 3.0f) {
			VectorCopy(u, c->last_dir);
			c->last_dir_time = level.time;
		} else
			SetDirect(c, F);
	} else {
		VectorCopy(m, c->last_dir); /* the tiny direction itself */
		c->last_dir_time = level.time;
	}
	if (!skip_step) {
		vec3_t v;
		VectorSubtract(c->movepoint, F, v);
		VectorNormalize(v);
		VectorScale(v, 25, v);
		if (!Nav_StepTest(ent, step_height, v))
			SetDirect(c, F);
	}
	if (PointsShow() && C) {
		Nav_DebugLine(F, c->movepoint, 0xfa0000fa, 1);
		DebugRing(c->movepoint, 6);
	}
	return !c->direct;
}

//==========================================================================================
// rules 7, 8

int Nav_FleeSpot(NavCache *c, int team, vec3_t out)
{
	if (!c || NoNodes())
		return 0;
	int cur = c->current_node;
	if (cur == -1 || cur == 0 || !ValidNode(cur))
		return 0;
	const NavNode &nd = g_nav.data.nodes[cur];
	int cand = team == 0 ? nd.tail2 : (nd.tail3 ? nd.tail3 : nd.tail2);
	if (cand != 0 && ValidNode(cand)) {
		VectorCopy(g_nav.data.nodes[cand].origin, out);
		return 1;
	}
	VectorCopy(c->ref, out);
	return 0;
}

int Nav_LookAroundNode(const vec3_t p, int n, vec3_t out)
{
	const NavLayer *l = Nav_Lookup(p);
	if (!l)
		return 0;
	int A = l->nodes[0];
	if (A < 1 || !ValidNode(A))
		return 0;
	const NavNode &a = g_nav.data.nodes[A];
	if (a.nlinks < 1)
		return 0;
	int k = n % a.nlinks;
	if (k < 0)
		k += a.nlinks;
	int B = a.links[k].target;
	if (B < 1 || !ValidNode(B))
		return 0;
	VectorCopy(g_nav.data.nodes[B].origin, out);
	return 1;
}

//==========================================================================================
// rule 10: hidden spawn spot

struct HiddenSearch {
	edict_t *sight;
	vec3_t fw;
	int allow_seen;
	float stamp;
	vec3_t out;
};

static bool Qualifies(HiddenSearch &s, int q)
{
	const NavNode &nd = g_nav.data.nodes[q];
	bool seen = q < NAV_SEEN_MAX && g_nav.seen[q];
	if (!(!seen || s.allow_seen || game.skill_spawning > 3))
		return false;
	if (nd.ignore)
		return false;
	vec3_t D;
	VectorSubtract(nd.origin, s.sight->s.origin, D);
	float d = VectorLength(D);
	bool hidden = !gi.inPVS(s.sight->s.origin, nd.origin);
	if (!hidden && d > 400) {
		vec3_t u;
		VectorScale(D, 1.0f / d, u);
		hidden = DotProduct(u, s.fw) < 0.5f;
	}
	if (!hidden || !(d < 600))
		return false;
	edict_t *list[1024];
	int k = G_RadiusList(nd.origin, 100, list, 1); /* UNVERIFIED which entities count (SPEC-740 OQ 2) */
	for (int i = 0; i < k; i++)
		if (list[i] && list[i] != g_edicts && list[i]->inuse)
			return false;
	return true;
}

static bool HiddenDFS(HiddenSearch &s, int q, int depth)
{
	if (Qualifies(s, q)) {
		VectorCopy(g_nav.data.nodes[q].origin, s.out);
		return true;
	}
	g_nav.stamp[q] = s.stamp;
	if (depth > 4096)
		return false;
	const NavNode &nd = g_nav.data.nodes[q];
	int k = nd.nlinks;
	if (k <= 0)
		return false;
	int r = gix.irand(0, 1293);
	for (int i = 0; i < k; i++) {
		int t = nd.links[(r + i) % k].target;
		if (!ValidNode(t) || g_nav.stamp[t] == s.stamp)
			continue;
		if (HiddenDFS(s, t, depth + 1))
			return true;
	}
	return false;
}

int Nav_HiddenSpawnSpot(vec3_t out, int allow_seen)
{
	edict_t *sight = level.sight_client;
	if (NoNodes() || Nav_NodeCount() < 1 || g_nav.player_node < 1 || !sight)
		return 0;
	HiddenSearch s;
	s.sight = sight;
	AngleVectors(sight->s.angles, s.fw, NULL, NULL);
	s.allow_seen = allow_seen;
	g_nav.search_counter++;
	s.stamp = (float)g_nav.search_counter;
	if (!HiddenDFS(s, g_nav.player_node, 0))
		return 0;
	VectorCopy(s.out, out);
	return 1;
}

//==========================================================================================
// rule 11: cone helper

int Nav_InCone(const vec3_t a, float ra, const vec3_t b, float rb, const vec3_t p)
{
	vec3_t ba, pa, bp;
	VectorSubtract(b, a, ba);
	VectorSubtract(p, a, pa);
	VectorSubtract(b, p, bp);
	if (DotProduct(pa, ba) < 0 || DotProduct(bp, ba) < 0)
		return 0;
	float L = VectorLength(ba);
	if (L <= 0)
		return 0;
	vec3_t w;
	VectorScale(ba, 1.0f / L, w);
	if (rb == ra) {
		/* the apex is at infinity (the original divides by zero): a cylinder of radius ra */
		float along = DotProduct(pa, w);
		vec3_t radial;
		VectorMA(pa, -along, w, radial);
		return VectorLength(radial) <= ra;
	}
	vec3_t c, n, q, pc, qc, w2;
	VectorMA(a, -(ra * L / (rb - ra)), w, c);
	PerpendicularVector(n, w);
	VectorMA(b, rb, n, q);
	VectorSubtract(p, c, pc);
	VectorSubtract(q, c, qc);
	VectorNormalize(pc);
	VectorNormalize(qc);
	if (rb >= ra)
		VectorCopy(w, w2);
	else
		VectorNegate(w, w2);
	return DotProduct(pc, w2) >= DotProduct(qc, w2);
}
