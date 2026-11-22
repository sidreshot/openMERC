// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - the shot hit list: the trace filter weapons hand to the engine sweep (import
// #53) runs the Ghoul ray test (instance method 43) on every entity with a model, so shots pass between limbs and
// stop on the model surface; the records of the accepted entity are kept for the body's hit location.
// Implements: SPEC-720 "Shot hit list", SPEC-782 §3 item 2 (hit filter), SPEC-001 #53 (as a client)
#include "g3_local.h"
#include "../../../engine/src/common/header/om_corpse.h"

G3ShotHits dmg_shot;

static vec3_t shotStart, shotDir;
static float shotLen;

void Dmg_ShotClear(void)
{
	dmg_shot.ent = NULL;
	dmg_shot.count = 0;
	dmg_shot.fallbackPart = 0;
}

void Dmg_ShotBegin(const vec3_t start, const vec3_t end)
{
	Dmg_ShotClear();
	VectorCopy(start, shotStart);
	VectorSubtract(end, start, shotDir);
	shotLen = VectorNormalize(shotDir);
}

static int CorpseEntity(edict_t *ent)
{
	if (!ent || !ent->ghoulInst)
		return 0;
	if (ent->health > 0 && ent->solid != SOLID_CORPSE && !(ent->svflags & SVF_DEADMONSTER))
		return 0;
	return 1;
}

static int PartOfSegment(int segment)
{
	switch (segment) {
	case SEG_HEAD:
	case SEG_NECK:
		return OM_PART_HEAD;
	case SEG_RTHIGH:
	case SEG_LTHIGH:
	case SEG_RCALF:
	case SEG_LCALF:
	case SEG_RFOOT:
	case SEG_LFOOT:
		return OM_PART_LEG;
	default:
		return OM_PART_BODY;
	}
}

struct CorpseVol {
	om_part_t part;
	GhoulID owner;
};

/* Posed centres in AngleVectors body space (x forward, y right, z up).
   Ghoul stores local Y as left, so the right axis is the negation.
   Thigh and calf zones have no gore bolt: the joint cap bolt is the centre,
   and the radius reaches the rest of that limb. */
static int GatherVolumes(edict_t *ent, CorpseVol *out, int maxn)
{
	int n = 0;
	G3Body *body = ent->g3body;
	IGhoulInst *inst = ent->ghoulInst;
	static const struct {
		const char *name;
		int id;
		float rad;
	} named[] = {
		{"abolt_head_t", OM_PART_HEAD, 12.0f}, {"sbolt_mouth", OM_PART_HEAD, 10.0f},
		{"gbolt_head", OM_PART_HEAD, 12.0f},    {"GBOLT_HEAD", OM_PART_HEAD, 12.0f},
		{"abolt_thigh_l", OM_PART_LEG, 18.0f},  {"abolt_thigh_r", OM_PART_LEG, 18.0f},
		{"abolt_hip_l", OM_PART_LEG, 16.0f},    {"abolt_hip_r", OM_PART_LEG, 16.0f},
		{"sbolt_heel_l", OM_PART_LEG, 12.0f},   {"sbolt_heel_r", OM_PART_LEG, 12.0f},
		{"abolt_chest", OM_PART_BODY, 16.0f},   {"gbolt_chest", OM_PART_BODY, 16.0f},
	};
	int saw_head = 0, saw_leg = 0;

	if (body && body->gore && inst) {
		G3Gore *g = body->gore;
		for (int i = 0; i < g->numZones && n < maxn; i++) {
			ghoul::Matrix4 m;
			G3Zone &z = g->zones[i];
			GhoulID bolt = z.goreBolt ? z.goreBolt : (z.bodyCapBolt ? z.bodyCapBolt : z.pieceCapBolt);
			if (!bolt || !G3G_BoltMatrix(inst, bolt, &m, 5, true))
				continue;
			int id = PartOfSegment(z.segment);
			if (i == 0 && id == OM_PART_BODY)
				id = OM_PART_HEAD;
			out[n].part.center[0] = m.m[3][0];
			out[n].part.center[1] = -m.m[3][1];
			out[n].part.center[2] = m.m[3][2];
			out[n].part.radius = id == OM_PART_HEAD ? 12.0f : id == OM_PART_LEG ? 18.0f : 16.0f;
			out[n].part.id = id;
			out[n].owner = z.owner ? z.owner : bolt;
			if (id == OM_PART_HEAD)
				saw_head = 1;
			if (id == OM_PART_LEG)
				saw_leg = 1;
			n++;
		}
	}
	if (inst && n < maxn && (!saw_head || !saw_leg)) {
		for (unsigned k = 0; k < sizeof named / sizeof named[0] && n < maxn; k++) {
			ghoul::Matrix4 m;
			GhoulID id;
			if (named[k].id == OM_PART_HEAD && saw_head)
				continue;
			if (named[k].id == OM_PART_LEG && saw_leg)
				continue;
			if (named[k].id == OM_PART_BODY)
				continue;
			id = G3G_Part(inst, named[k].name);
			if (!id || !G3G_BoltMatrix(inst, id, &m, 5, true))
				continue;
			out[n].part.center[0] = m.m[3][0];
			out[n].part.center[1] = -m.m[3][1];
			out[n].part.center[2] = m.m[3][2];
			out[n].part.radius = named[k].rad;
			out[n].part.id = named[k].id;
			out[n].owner = id;
			if (body && body->gore) {
				for (int z = 0; z < body->gore->numZones; z++) {
					int zid = PartOfSegment(body->gore->zones[z].segment);
					if (z == 0 && zid == OM_PART_BODY)
						zid = OM_PART_HEAD;
					if (zid == named[k].id && body->gore->zones[z].owner) {
						out[n].owner = body->gore->zones[z].owner;
						break;
					}
				}
			}
			n++;
		}
	}
	/* Nothing posed, or the death sequence has no head or leg bolt: the standing
	   centres still cover those parts. A posed head is not replaced.
	   base is the count before these volumes. Testing n would skip the body
	   after the first standing volume incremented it. */
	{
		int base = n;
		static const float fb[6][5] = {
			{0, 0, 30, 12, (float)OM_PART_HEAD}, {0, 8, -6, 18, (float)OM_PART_LEG},
			{0, -8, -6, 18, (float)OM_PART_LEG}, {0, 8, -22, 14, (float)OM_PART_LEG},
			{0, -8, -22, 14, (float)OM_PART_LEG}, {0, 0, 8, 16, (float)OM_PART_BODY},
		};
		if (base != 0 && saw_head && saw_leg)
			return n;
		for (int i = 0; i < 6 && n < maxn; i++) {
			int id = (int)fb[i][4];
			if (base > 0 && id == OM_PART_HEAD && saw_head)
				continue;
			if (base > 0 && id == OM_PART_LEG && saw_leg)
				continue;
			if (base > 0 && id == OM_PART_BODY)
				continue;
			out[n].part.center[0] = fb[i][0];
			out[n].part.center[1] = fb[i][1];
			out[n].part.center[2] = fb[i][2];
			out[n].part.radius = fb[i][3];
			out[n].part.id = id;
			out[n].owner = 0;
			n++;
		}
	}
	return n;
}

int Dmg_HitZone(const ghoul::HitRecord &r, G3Gore *gore, const float *point, bool knifeDM)
{
	if (!gore)
		return -1;
	/* Owner 0 used to die in RecordZone before any zone was chosen. The part id
	   still names a head, a leg, or the body. */
	if (dmg_shot.fallbackPart && (r.part & 0xF000) == 0xF000) {
		int z = OM_CorpseZone(dmg_shot.fallbackPart, 0);
		if (z >= 0 && z < gore->numZones)
			return z;
		for (int i = 0; i < gore->numZones; i++) {
			int id = PartOfSegment(gore->zones[i].segment);
			if (i == 0 && id == OM_PART_BODY)
				id = OM_PART_HEAD;
			if (id == dmg_shot.fallbackPart)
				return i;
		}
		return -1;
	}
	if (!r.part)
		return -1;
	return gore->ZoneByOwnerPart(r.part, point, knifeDM);
}

/* Owner id written onto the hit. 0 from the standing volume becomes the zone's
   own part when that zone has one, otherwise a tagged part the wound path maps. */
static GhoulID VolumeOwner(edict_t *ent, int part_id, GhoulID owner, int *fallback)
{
	*fallback = 0;
	if (owner)
		return owner;
	int z = OM_CorpseZone(part_id, 0);
	G3Body *body = ent ? ent->g3body : NULL;
	if (body && body->gore && z >= 0) {
		G3Gore *g = body->gore;
		if (z < g->numZones && g->zones[z].owner)
			return g->zones[z].owner;
		for (int i = 0; i < g->numZones; i++) {
			int id = PartOfSegment(g->zones[i].segment);
			if (i == 0 && id == OM_PART_BODY)
				id = OM_PART_HEAD;
			if (id == part_id && g->zones[i].owner)
				return g->zones[i].owner;
		}
	}
	*fallback = part_id;
	return (GhoulID)(0xF000u | (unsigned)(part_id & 0xFF));
}

static void AnglesForVolumes(const edict_t *ent, float ang[3])
{
	/* Same rotation as G3_EntityAxes. Local Y there is left; volumes use right. */
	ang[0] = -ent->s.angles[0];
	ang[1] = ent->s.angles[1];
	ang[2] = -ent->s.angles[2];
}

/* The Ghoul mesh trace missed. A volume on the posed part still stops the shot
   and names that zone's owner, so the existing wound path runs. */
static int CorpseVolumeHit(edict_t *ent, const vec3_t start, const vec3_t dir, float length, trace_t *tr)
{
	CorpseVol vols[32];
	om_part_t parts[32];
	float ang[3], hit[3];
	vec3_t endv, delta, nd;
	int n, idx, id;
	float len;
	ghoul::HitRecord rec;

	if (!CorpseEntity(ent))
		return 0;
	n = GatherVolumes(ent, vols, 32);
	if (n <= 0)
		return 0;
	for (int i = 0; i < n; i++)
		parts[i] = vols[i].part;
	AnglesForVolumes(ent, ang);
	len = length < 1.0f ? 8192.0f : length;
	VectorMA(start, len, dir, endv);
	idx = -1;
	id = OM_CorpsePart(ent->s.origin, ang, parts, n, start, endv, &idx, hit);
	if (id == OM_PART_NONE || idx < 0)
		return 0;
	memset(&rec, 0, sizeof rec);
	rec.inst = ent->ghoulInst;
	{
		int fb = 0;
		rec.part = VolumeOwner(ent, vols[idx].part.id, vols[idx].owner, &fb);
		dmg_shot.fallbackPart = fb;
	}
	VectorSubtract(hit, start, delta);
	rec.distance = DotProduct(delta, dir);
	if (rec.distance < 0)
		rec.distance = 0;
	G3_WorldToEntity(ent, hit, rec.point);
	nd[0] = -dir[0];
	nd[1] = -dir[1];
	nd[2] = -dir[2];
	G3_WorldDirToEntity(ent, nd, rec.normal);
	dmg_shot.ent = ent;
	dmg_shot.count = 1;
	dmg_shot.rec[0] = rec;
	VectorCopy(start, dmg_shot.worldStart);
	VectorCopy(dir, dmg_shot.worldDir);
	G3_WorldToEntity(ent, start, dmg_shot.start);
	G3_WorldDirToEntity(ent, dir, dmg_shot.dir);
	if (tr) {
		float f = rec.distance / len;
		if (f < 0)
			f = 0;
		if (f > 0.99f)
			f = 0.99f;
		tr->fraction = f;
		VectorCopy(hit, tr->endpos);
		VectorCopy(nd, tr->plane.normal);
	}
	return 1;
}

/* Shot hull follows the posed parts. Movement does not use this box:
   CorpseBox keeps a narrow column so the body can still settle. */
void Dmg_CorpseHull(edict_t *ent)
{
	CorpseVol vols[32];
	vec3_t f, left, u;
	float minx, miny, minz, maxx, maxy, maxz;
	int n;

	if (!ent || ent->solid != SOLID_CORPSE || !ent->ghoulInst)
		return;
	n = GatherVolumes(ent, vols, 32);
	if (n <= 0)
		return;
	minx = ent->mins[0];
	miny = ent->mins[1];
	minz = ent->mins[2];
	maxx = ent->maxs[0];
	maxy = ent->maxs[1];
	maxz = ent->maxs[2];
	if (minx > -16)
		minx = -16;
	if (miny > -16)
		miny = -16;
	if (maxx < 16)
		maxx = 16;
	if (maxy < 16)
		maxy = 16;
	G3_EntityAxes(ent, f, left, u);
	for (int i = 0; i < n; i++) {
		float lx = vols[i].part.center[0];
		float ly = -vols[i].part.center[1];
		float lz = vols[i].part.center[2];
		float rad = vols[i].part.radius;
		vec3_t w;
		for (int a = 0; a < 3; a++)
			w[a] = lx * f[a] + ly * left[a] + lz * u[a];
		if (w[0] - rad < minx)
			minx = w[0] - rad;
		if (w[1] - rad < miny)
			miny = w[1] - rad;
		if (w[2] - rad < minz)
			minz = w[2] - rad;
		if (w[0] + rad > maxx)
			maxx = w[0] + rad;
		if (w[1] + rad > maxy)
			maxy = w[1] + rad;
		if (w[2] + rad > maxz)
			maxz = w[2] + rad;
	}
	if (minx < -96)
		minx = -96;
	if (miny < -96)
		miny = -96;
	if (minz < -72)
		minz = -72;
	if (maxx > 96)
		maxx = 96;
	if (maxy > 96)
		maxy = 96;
	if (maxz > 80)
		maxz = 80;
	ent->mins[0] = minx;
	ent->mins[1] = miny;
	ent->mins[2] = minz;
	ent->maxs[0] = maxx;
	ent->maxs[1] = maxy;
	ent->maxs[2] = maxz;
}

// Ray test of `ent` along a world ray; fills dmg_shot when it hits.
int Dmg_ShotFill(edict_t *ent, const vec3_t start, const vec3_t dir, float length)
{
	IGhoulInst *inst = ent ? ent->ghoulInst : NULL;
	if (!inst)
		return 0;
	ghoul::HitRecord recs[DMG_SHOT_MAX_HITS];
	vec3_t ls, ld;
	G3_WorldToEntity(ent, start, ls);
	G3_WorldDirToEntity(ent, dir, ld);
	int n = inst->TraceRay(G3_Time(), ls, ld, recs, DMG_SHOT_MAX_HITS);
	if (n <= 0)
		return CorpseVolumeHit(ent, start, dir, length, NULL);
	if (n > DMG_SHOT_MAX_HITS)
		n = DMG_SHOT_MAX_HITS;
	dmg_shot.ent = ent;
	dmg_shot.count = n;
	dmg_shot.fallbackPart = 0;
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
	{
		/* om_shotdebug 1: every shot tested against a character's model (ports: hit detection checks) */
		static cvar_t *dbg;
		if (!dbg && gix.cvar) /* the unit tests have no cvars */
			dbg = gi.cvar("om_shotdebug", "0", 0);
		if (dbg && dbg->value)
			gi.dprintf("OMSHOT %s n %d ls %s ld %s t %.3f\n", ent->classname ? ent->classname : "?", n, vtos(ls),
			           vtos(ld), (double)G3_Time());
	}
	if (n <= 0) {
		/* A posed corpse is not invisible. The mesh trace missed; the part volume still hits. */
		if (CorpseVolumeHit(ent, shotStart, shotDir, shotLen, tr))
			return 1;
		return 0; // the shot passes between the limbs
	}
	if (n > DMG_SHOT_MAX_HITS)
		n = DMG_SHOT_MAX_HITS;
	dmg_shot.ent = ent;
	dmg_shot.count = n;
	dmg_shot.fallbackPart = 0;
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
