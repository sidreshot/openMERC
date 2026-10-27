/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700 ("Character helper rules": range class, visible, bottom check, ground check, water level,
 * world effects, drop to floor), SPEC-711 A6.3 (step move), SPEC-710 C9 (door-like entities)
 * Owner: package G2. The Quake II survivors (M_CheckBottom, M_CheckGround, M_CatagorizePosition, M_WorldEffects,
 * M_droptofloor, SV_movestep) are written from the rules of the specs, which state their differences from Quake II.
 */
#include "ai_local.h"

#include <stdarg.h>
#include <stdio.h>

namespace om_ai {

AICvars cv;

void RegisterCvars(void)
{
	cv.ai_freeze = gi.cvar("ai_freeze", "0", 0);
	cv.ai_dumb = gi.cvar("ai_dumb", "0", 0);
	cv.ai_goretest = gi.cvar("ai_goretest", "0", 0);
	cv.ai_pathtest = gi.cvar("ai_pathtest", "0", 0);
	cv.ai_highpriority = gi.cvar("ai_highpriority", "0", 0);
	cv.ai_nonodes = gi.cvar("ai_nonodes", "0", 0);
	cv.ai_maxcorpses = gi.cvar("ai_maxcorpses", "5", CVAR_ARCHIVE);
	cv.ai_corpselife = gi.cvar("ai_corpselife", "5", CVAR_ARCHIVE);
	cv.ai_editdir = gi.cvar("ai_editdir", "p:", 0);
	cv.aidec_show = gi.cvar("aidec_show", "0", 0);
	cv.aipoints_show = gi.cvar("aipoints_show", "0", 0);
	cv.sv_gravity = gi.cvar("sv_gravity", "800", 0);
	cv.ai_trace = gi.cvar("ai_trace", "0", 0); /* OpenMerc debug: per-think trace of characters to the console */
	cv.ai_savetest = gi.cvar("ai_savetest", "0", 0); /* OpenMerc test: memory round trip of the AI block at 20 s */
}

void Dbg(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	/* the server console directly (import 14 with no entity): dprintf is developer-only, and the original SoF.exe
	 * used as the black-box host must run with developer 0 */
	gi.cprintf(nullptr, PRINT_HIGH, "%6.1f %s", level.time, buf);
}

float Now(void) { return level.time; }

float Frand(void) { return (rand() & 0x7fff) / 32768.0f; }
float Crand(void) { return 2.0f * Frand() - 1.0f; }

float FlRand(float lo, float hi)
{
	if (gix.flrand)
		return gix.flrand(lo, hi);
	return lo + (hi - lo) * Frand();
}

int IRand(int lo, int hi)
{
	if (gix.irand)
		return gix.irand(lo, hi);
	if (hi <= lo)
		return lo;
	return lo + rand() % (hi - lo + 1);
}

float Dist2(const vec3_t a, const vec3_t b)
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + z * z;
}

float HDist2(const vec3_t a, const vec3_t b)
{
	float x = a[0] - b[0], y = a[1] - b[1];
	return x * x + y * y;
}

float HDist(const vec3_t a, const vec3_t b) { return sqrtf(HDist2(a, b)); }

void BoxCenter(edict_t *e, vec3_t out)
{
	for (int i = 0; i < 3; i++)
		out[i] = e->s.origin[i] + 0.5f * (e->mins[i] + e->maxs[i]);
}

float AngleWrap(float a)
{
	while (a > 180.0f)
		a -= 360.0f;
	while (a < -180.0f)
		a += 360.0f;
	return a;
}

float QuantiseAngle(float a) { return (360.0f / 65536.0f) * (float)((int)(a * (65536.0f / 360.0f)) & 65535); }

float YawTo(const vec3_t from, const vec3_t to)
{
	vec3_t d;
	VectorSubtract(to, from, d);
	return vectoyaw(d);
}

void CopyVec(vec3_t dst, const vec3_t src) { VectorCopy(src, dst); }
bool IsZero(const vec3_t v) { return v[0] == 0 && v[1] == 0 && v[2] == 0; }

AI *GetAI(edict_t *e) { return e ? e->ai : nullptr; }
bool IsCharacter(edict_t *e) { return e && e->inuse && e->ai != nullptr; }
bool IsPlayer(edict_t *e) { return e && e->inuse && e->client != nullptr; }
bool IsAlive(edict_t *e) { return e && e->inuse && e->health > 0; }

trace_t Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, edict_t *pass, int mask)
{
	static vec3_t zero = {0, 0, 0};
	return gi.trace(start, mins ? mins : zero, maxs ? maxs : zero, end, pass, mask);
}

bool InPVS(const vec3_t a, const vec3_t b) { return gi.inPVS(a, b) != 0; }

void EyePoint(edict_t *e, vec3_t out)
{
	VectorCopy(e->s.origin, out);
	out[2] += e->viewheight;
}

edict_t *LeanProxy(edict_t *player)
{
	if (!player || !player->client)
		return nullptr;
	if (player->client->helper_lean && player->client->helper_lean->inuse)
		return player->client->helper_lean;
	for (edict_t *e = player->teamchain; e && e != player; e = e->teamchain)
		if (e->inuse && (e->flags & FL_LEANPROXY))
			return e;
	return nullptr;
}

bool PlayerLeaning(edict_t *player) { return player && player->client && player->client->lean != 0; }

int RangeClass(float d2)
{
	if (d2 < 6400.0f)
		return RANGE_MELEE;
	if (d2 < 250000.0f)
		return RANGE_NEAR;
	if (d2 < 2250000.0f)
		return RANGE_MID;
	return RANGE_FAR;
}

/* SPEC-700 helper rule "Visible" */
bool Visible(edict_t *self, edict_t *other, bool lean)
{
	vec3_t a, b;
	EyePoint(self, a);
	EyePoint(other, b);
	if (!InPVS(a, b) || !InPVS(b, a))
		return false;
	trace_t tr = Trace(a, nullptr, nullptr, b, self, 0x19);
	if (tr.fraction == 1.0f)
		return true;
	if (lean && other->client) {
		edict_t *p = LeanProxy(other);
		if (p) {
			vec3_t c;
			VectorCopy(p->s.origin, c);
			c[2] += 16;
			tr = Trace(a, nullptr, nullptr, c, self, 0x19);
			if (tr.fraction == 1.0f)
				return true;
		}
	}
	return false;
}

/* SPEC-700 helper rule "Bottom check", first form */
bool CheckBottom(edict_t *ent)
{
	vec3_t mins, maxs, start, stop;
	VectorAdd(ent->s.origin, ent->mins, mins);
	VectorAdd(ent->s.origin, ent->maxs, maxs);
	start[2] = mins[2] - 1;
	bool all_solid = true;
	for (int x = 0; x <= 1 && all_solid; x++)
		for (int y = 0; y <= 1; y++) {
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			if (!(gi.pointcontents(start) & CONTENTS_SOLID)) {
				all_solid = false;
				break;
			}
		}
	if (all_solid)
		return true;
	start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5f;
	start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5f;
	start[2] = mins[2];
	stop[2] = start[2] - 36;
	trace_t tr = Trace(start, nullptr, nullptr, stop, ent, 0x2020003);
	float mid = tr.endpos[2];
	int bad = 0;
	for (int x = 0; x <= 1; x++)
		for (int y = 0; y <= 1; y++) {
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];
			tr = Trace(start, nullptr, nullptr, stop, ent, 0x2020003);
			if (tr.fraction == 1.0f || mid - tr.endpos[2] > 18)
				bad++;
		}
	return bad <= 2;
}

/* second form: an arbitrary position; flags 2 or 4 use the character's step height */
bool CheckBottomAt(edict_t *ent, AI *ai, const vec3_t pos, int flags)
{
	float step = (flags & (MF_STEP_OFF | MF_FULL_STEP)) ? (ai ? ai->step_height : 24.0f) : 2.0f;
	vec3_t mins, maxs, start, stop;
	VectorAdd(pos, ent->mins, mins);
	VectorAdd(pos, ent->maxs, maxs);
	start[2] = mins[2] - 1;
	bool all_solid = true;
	for (int x = 0; x <= 1 && all_solid; x++)
		for (int y = 0; y <= 1; y++) {
			start[0] = x ? maxs[0] : mins[0];
			start[1] = y ? maxs[1] : mins[1];
			if (!(gi.pointcontents(start) & CONTENTS_SOLID)) {
				all_solid = false;
				break;
			}
		}
	if (all_solid)
		return true;
	start[0] = stop[0] = (mins[0] + maxs[0]) * 0.5f;
	start[1] = stop[1] = (mins[1] + maxs[1]) * 0.5f;
	start[2] = mins[2];
	stop[2] = start[2] - 2 * step;
	trace_t tr = Trace(start, nullptr, nullptr, stop, ent, 0x2020003);
	float mid = tr.endpos[2];
	int bad = 0;
	for (int x = 0; x <= 1; x++)
		for (int y = 0; y <= 1; y++) {
			start[0] = stop[0] = x ? maxs[0] : mins[0];
			start[1] = stop[1] = y ? maxs[1] : mins[1];
			tr = Trace(start, nullptr, nullptr, stop, ent, 0x2020003);
			if (tr.fraction == 1.0f || mid - tr.endpos[2] > step)
				bad++;
		}
	return bad <= 2;
}

/* SPEC-700 helper rule "Ground check" */
void CheckGround(edict_t *ent, AI *ai)
{
	if (ent->flags & (FL_SWIM | FL_FLY))
		return;
	if (ent->velocity[2] > 30) {
		ent->groundentity = nullptr;
		return;
	}
	vec3_t point;
	VectorCopy(ent->s.origin, point);
	point[2] -= (ent->groundentity && ent->groundentity->solid == SOLID_GROUND) ? 2.5f : 0.25f;
	trace_t tr = Trace(ent->s.origin, ent->mins, ent->maxs, point, ent, 0x2020003);
	if (tr.plane.normal[2] < 0.7f && !tr.startsolid) {
		ent->groundentity = nullptr;
		return;
	}
	if (!tr.startsolid && !tr.allsolid) {
		edict_t *old = ent->groundentity;
		VectorCopy(tr.endpos, ent->s.origin);
		ent->groundentity = tr.ent;
		ent->groundentity_linkcount = tr.ent ? tr.ent->linkcount : 0;
		if (ai)
			ai->ground_linkcount = ent->groundentity_linkcount;
		ent->velocity[2] = 0;
		if (tr.ent && tr.ent != old && tr.ent->touch)
			tr.ent->touch(tr.ent, ent, nullptr, nullptr);
	}
}

/* SPEC-700 helper rule "Water level" */
void CategorizePosition(edict_t *ent)
{
	vec3_t p;
	VectorCopy(ent->s.origin, p);
	p[2] += ent->mins[2] + 1;
	int cont = gi.pointcontents(p);
	if (!(cont & 0x38)) {
		ent->waterlevel = 0;
		ent->watertype = 0;
		return;
	}
	ent->watertype = cont;
	ent->waterlevel = 1;
	p[2] += 26;
	cont = gi.pointcontents(p);
	if (!(cont & 0x38))
		return;
	ent->waterlevel = 2;
	p[2] += 22;
	cont = gi.pointcontents(p);
	if (cont & 0x38)
		ent->waterlevel = 3;
}

/* SPEC-700 helper rule "World effects" (silent) */
void WorldEffects(edict_t *ent)
{
	static vec3_t zero = {0, 0, 0};
	float now = Now();
	if (ent->health > 0) {
		bool swim = (ent->flags & FL_SWIM) != 0;
		bool submerged = swim ? ent->waterlevel == 0 : ent->waterlevel >= 3;
		if (!submerged)
			ent->air_finished = now + (swim ? 9.0f : 12.0f);
		else if (ent->air_finished < now && ent->pain_debounce_time < now) {
			int dmg = 2 + 2 * (int)floorf(now - ent->air_finished);
			if (dmg > 15)
				dmg = 15;
			T_Damage(ent, world, world, zero, ent->s.origin, zero, dmg, 0, 0x4002, MOD_WATER);
			ent->pain_debounce_time = now + 1;
		}
	}
	if (ent->waterlevel == 0) {
		ent->flags &= ~FL_INWATER;
		return;
	}
	if ((ent->watertype & CONTENTS_LAVA) && !(ent->flags & 128) && ent->damage_debounce_time < now) {
		ent->damage_debounce_time = now + 0.2f;
		T_Damage(ent, world, world, zero, ent->s.origin, zero, 10 * ent->waterlevel, 0, 0x1002, MOD_LAVA);
	}
	if ((ent->watertype & CONTENTS_SLIME) && !(ent->flags & 64) && ent->damage_debounce_time < now) {
		ent->damage_debounce_time = now + 1;
		T_Damage(ent, world, world, zero, ent->s.origin, zero, 4 * ent->waterlevel, 0, 0x4002, MOD_SLIME);
	}
	if (!(ent->flags & FL_INWATER)) {
		ent->flags |= FL_INWATER;
		ent->damage_debounce_time = 0;
	}
}

/* SPEC-700 helper rule "Drop to floor" */
void DropToFloor(edict_t *ent, AI *ai)
{
	vec3_t end;
	ent->s.origin[2] += 1;
	VectorCopy(ent->s.origin, end);
	end[2] -= 256;
	trace_t tr = Trace(ent->s.origin, ent->mins, ent->maxs, end, ent, 0x2020003);
	if (tr.fraction == 1.0f || tr.allsolid)
		return;
	VectorCopy(tr.endpos, ent->s.origin);
	gi.linkentity(ent);
	CheckGround(ent, ai);
	CategorizePosition(ent);
}

static bool StandsOnSomeone(edict_t *ent, const trace_t &tr)
{
	edict_t *o = tr.ent;
	if (!o || o == world || o == ent->groundentity)
		return false;
	if (!(o->client || o->ai))
		return false;
	if (o->health <= 0 || o->deadflag == DEAD_DEAD)
		return false;
	float newbottom = tr.endpos[2] + ent->mins[2];
	float top = o->s.origin[2] + o->maxs[2];
	return newbottom > top - 1;
}

/* SPEC-711 A6.3 */
bool StepMove(edict_t *ent, AI *ai, const vec3_t move, int flags)
{
	vec3_t oldorg, neworg, end;
	VectorCopy(ent->s.origin, oldorg);
	int mask = ent->movetype == MOVETYPE_NOCLIP ? 0 : 0x2020003;

	if (ent->movetype != MOVETYPE_STEP) {
		for (int attempt = 0; attempt < 2; attempt++) {
			VectorAdd(ent->s.origin, move, neworg);
			if (attempt == 1)
				neworg[2] = ent->s.origin[2];
			trace_t tr = Trace(ent->s.origin, ent->mins, ent->maxs, neworg, ent, mask);
			vec3_t test;
			VectorCopy(tr.endpos, test);
			test[2] += ent->mins[2] + 1;
			if ((ent->flags & FL_FLY) && ent->waterlevel == 0 && (gi.pointcontents(test) & 0x38))
				return false;
			if ((ent->flags & FL_SWIM) && ent->waterlevel < 2 && !(gi.pointcontents(test) & 0x38))
				return false;
			if (tr.fraction == 1.0f) {
				VectorCopy(tr.endpos, ent->s.origin);
				gi.linkentity(ent);
				G_TouchTriggers(ent);
				return true;
			}
		}
		return false;
	}

	if (!ent->groundentity)
		return false;
	if (DotProduct(ent->velocity, ent->velocity) > 121.0f)
		return false;
	float h = (flags & MF_FULL_STEP) ? (ai ? ai->step_height : 24.0f) : 2.0f;
	trace_t tr;
	bool found = false;
	for (int i = 0; i < 3; i++) {
		float hh = i == 0 ? h : i == 1 ? h * 0.5f : 0.0f;
		VectorAdd(ent->s.origin, move, neworg);
		VectorCopy(neworg, end);
		neworg[2] += hh;
		end[2] -= 2 * h;
		tr = Trace(neworg, ent->mins, ent->maxs, end, ent, mask);
		if (!tr.allsolid && !tr.startsolid) {
			found = true;
			break;
		}
	}
	if (!found)
		return false;
	if (!(flags & MF_STEP_OFF) && StandsOnSomeone(ent, tr))
		return false;
	if (ent->waterlevel == 0) {
		vec3_t test;
		VectorCopy(tr.endpos, test);
		test[2] += ent->mins[2] + 1;
		if (gi.pointcontents(test) & 0x38)
			return false;
	}
	if (tr.fraction == 1.0f || !CheckBottomAt(ent, ai, tr.endpos, flags)) {
		if (!(flags & MF_STEP_OFF))
			return false;
		/* stepping off a ledge: partial ground when the floor 20 units below is missing or steep */
		vec3_t below;
		VectorCopy(tr.endpos, below);
		below[2] -= 20;
		trace_t fl = Trace(tr.endpos, ent->mins, ent->maxs, below, ent, mask);
		if (fl.fraction == 1.0f || fl.plane.normal[2] < 0.8f)
			ent->flags |= FL_PARTIALGROUND;
		VectorCopy(tr.endpos, ent->s.origin);
		gi.linkentity(ent);
		G_TouchTriggers(ent);
		return true;
	}
	ent->flags &= ~FL_PARTIALGROUND;
	VectorCopy(tr.endpos, ent->s.origin);
	ent->groundentity = tr.ent;
	ent->groundentity_linkcount = tr.ent ? tr.ent->linkcount : 0;
	if (ai)
		ai->ground_linkcount = ent->groundentity_linkcount;
	gi.linkentity(ent);
	G_TouchTriggers(ent);
	(void)oldorg;
	return true;
}

bool ClassnameIs(edict_t *e, const char *name) { return e && e->classname && !strcmp(e->classname, name); }

bool DoorLike(edict_t *e)
{
	if (!e || !e->classname)
		return false;
	return !strncmp(e->classname, "func_door", 9) || !strcmp(e->classname, "func_button") ||
	       !strcmp(e->classname, "trigger_useable");
}

bool DoorUsableNow(edict_t *e)
{
	if (!DoorLike(e))
		return false;
	if (!e->plUse)
		return false;
	/* Quake II mover states: 0 top (open), 1 bottom (closed) */
	if (e->moveinfo.state != 1)
		return false;
	if (e->spawnflags & 8)
		return false;
	return Now() - e->lastusetime > 5.0f;
}

void PlayEffect(edict_t *ent, const char *name)
{
	int idx = gi.effectindex(name);
	if (idx)
		FX_EffectEnt(idx, ent, MULTICAST_PVS);
}

int NavDoorPredicate(edict_t *door, edict_t *user)
{
	(void)user;
	return DoorUsableNow(door) ? 1 : 0;
}

bool Cinematic(void) { return game.cinematic != 0; }

const G3Anim *Anim(const char *name) { return Anim_Human(name); }

void UseTargetsNamed(const char *targetname, edict_t *activator, edict_t *other)
{
	if (!targetname)
		return;
	edict_t *t = nullptr;
	while ((t = G_Find(t, FOFS(targetname), targetname)) != nullptr)
		if (t->use)
			t->use(t, other, activator);
}

/* killfacing: with `killfacing` set, the character must face that entity within dot > 0.96 horizontally */
bool KillTargetFacingOK(edict_t *self)
{
	if (!self->killfacing)
		return true;
	edict_t *f = G_Find(nullptr, FOFS(targetname), self->killfacing);
	if (!f) {
		gi.dprintf("Couldn't find killfacing ent %s\n", self->killfacing);
		return false;
	}
	vec3_t fwd, d;
	AngleVectors(self->s.angles, fwd, nullptr, nullptr);
	fwd[2] = 0;
	VectorNormalize(fwd);
	VectorSubtract(f->s.origin, self->s.origin, d);
	d[2] = 0;
	VectorNormalize(d);
	return DotProduct(fwd, d) > 0.96f;
}

} /* namespace om_ai */
