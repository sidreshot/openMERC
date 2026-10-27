// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - the shot hit list: the trace filter weapons hand to the engine sweep (import
// #53) runs the Ghoul ray test (instance method 43) on every entity with a model, so shots pass between limbs and
// stop on the model surface; the records of the accepted entity are kept for the body's hit location.
// Implements: SPEC-720 "Shot hit list", SPEC-782 §3 item 2 (hit filter), SPEC-001 #53 (as a client)
#include "g3_local.h"

G3ShotHits dmg_shot;

static vec3_t shotStart, shotDir;
static float shotLen;

void Dmg_ShotClear(void)
{
	dmg_shot.ent = NULL;
	dmg_shot.count = 0;
}

void Dmg_ShotBegin(const vec3_t start, const vec3_t end)
{
	Dmg_ShotClear();
	VectorCopy(start, shotStart);
	VectorSubtract(end, start, shotDir);
	shotLen = VectorNormalize(shotDir);
}

// Ray test of `ent` along a world ray; fills dmg_shot when it hits.
int Dmg_ShotFill(edict_t *ent, const vec3_t start, const vec3_t dir, float length)
{
	(void)length;
	IGhoulInst *inst = ent ? ent->ghoulInst : NULL;
	if (!inst)
		return 0;
	ghoul::HitRecord recs[DMG_SHOT_MAX_HITS];
	vec3_t ls, ld;
	G3_WorldToEntity(ent, start, ls);
	G3_WorldDirToEntity(ent, dir, ld);
	int n = inst->TraceRay(G3_Time(), ls, ld, recs, DMG_SHOT_MAX_HITS);
	if (n <= 0)
		return 0;
	if (n > DMG_SHOT_MAX_HITS)
		n = DMG_SHOT_MAX_HITS;
	dmg_shot.ent = ent;
	dmg_shot.count = n;
	memcpy(dmg_shot.rec, recs, sizeof(recs[0]) * n);
	VectorCopy(ls, dmg_shot.start);
	VectorCopy(ld, dmg_shot.dir);
	VectorCopy(start, dmg_shot.worldStart);
	VectorCopy(dir, dmg_shot.worldDir);
	return n;
}

static int SurfMaterial(const trace_t *tr)
{
	if (!tr->surface)
		return 0;
	return ((*(const int *)((const char *)tr->surface + 32)) >> 24) & 0xff;
}

int Dmg_ShotFilter(trace_t *tr)
{
	edict_t *ent = tr->ent;
	if (!ent || ent == world)
		return 1;
	// helper volumes (players' lean/crouch/gun helpers, the characters' "Nug Buddy"; server flag 0x80) resolve
	// to their owner (SPEC-780 §15, SPEC-700 §16): the shot is tested against the owner's model
	if ((ent->svflags & SVF_PLAYERPROXY) && ent->owner && ent->owner->inuse && ent->owner != ent) {
		ent = ent->owner;
		tr->ent = ent;
	}
	if (!ent->ghoulInst) {
		int mat = SurfMaterial(tr);
		if (mat == 37 || mat == 38 || (tr->contents & CONTENTS_WINDOW))
			return 0; // glass and windows are passed through
		return 1;
	}
	ghoul::HitRecord recs[DMG_SHOT_MAX_HITS];
	vec3_t ls, ld;
	G3_WorldToEntity(ent, shotStart, ls);
	G3_WorldDirToEntity(ent, shotDir, ld);
	int n = ent->ghoulInst->TraceRay(G3_Time(), ls, ld, recs, DMG_SHOT_MAX_HITS);
	if (n <= 0)
		return 0; // the shot passes between the limbs
	if (n > DMG_SHOT_MAX_HITS)
		n = DMG_SHOT_MAX_HITS;
	dmg_shot.ent = ent;
	dmg_shot.count = n;
	memcpy(dmg_shot.rec, recs, sizeof(recs[0]) * n);
	VectorCopy(ls, dmg_shot.start);
	VectorCopy(ld, dmg_shot.dir);
	VectorCopy(shotStart, dmg_shot.worldStart);
	VectorCopy(shotDir, dmg_shot.worldDir);
	// move the result to the first record
	if (shotLen > 0.01f) {
		float f = recs[0].distance / shotLen;
		if (f > 0.99f)
			f = 0.99f;
		tr->fraction = f;
	}
	G3_EntityToWorld(ent, recs[0].point, tr->endpos);
	G3_EntityDirToWorld(ent, recs[0].normal, tr->plane.normal);
	return 1;
}

trace_t Dmg_ShotTrace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, edict_t *pass,
                      int mask)
{
	trace_t tr;
	memset(&tr, 0, sizeof tr);
	Dmg_ShotBegin(start, end);
	if (gix.tracefilter)
		gix.tracefilter(start, mins ? mins : vec3_origin, maxs ? maxs : vec3_origin, end, pass, mask, &tr,
		                Dmg_ShotFilter);
	else if (gix.trace)
		gix.trace(start, mins ? mins : vec3_origin, maxs ? maxs : vec3_origin, end, pass, mask, &tr);
	if (tr.ent != dmg_shot.ent)
		Dmg_ShotClear();
	return tr;
}

// A traced hit whose weapon did not run Dmg_ShotFilter: fill the list from the hit's shot start along its direction
// (the same ray the filter would have tested). G3 safety net for callers outside the filter (OPEN_QUESTIONS G3).
void Dmg_EnsureShot(edict_t *ent, const G3Hit &h)
{
	if (!ent || !(h.dflags & (DMGF_TRACED | DMGF_PROJECTILE)))
		return;
	if (dmg_shot.ent == ent && dmg_shot.count > 0)
		return;
	vec3_t d;
	VectorCopy(h.dir, d);
	if (VectorNormalize(d) == 0)
		return;
	Dmg_ShotFill(ent, h.origin, d, 8192);
}
