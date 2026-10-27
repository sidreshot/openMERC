// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - Ghoul model glue for world entities: one model object per model path,
// one instance per entity, sequences, skins, scale, bolt positions, ray tests, per-frame events.
// Implements: SPEC-010 (typical model set-up, lifetimes), SPEC-791 (through the G9 glue), SPEC-011 (instances, playback, bolts, traces),
//             SPEC-760 B9.3 (instances advanced each frame unless ghl_no_server_update)

#include "g_local.h"
#include "ghoulglue/gg_ghoul.h" /* G9: SPEC-791 model records and instances */

using namespace ghoul;

static IGhoulSystem *g_gs;

IGhoulSystem *GhoulSys()
{
	if (!g_gs && gix.GetGhoul)
		g_gs = gix.GetGhoul();
	return g_gs;
}

// Model records and object instances go through the game-side Ghoul glue (SPEC-791, package G9): one record per
// (directory, model), registered with all skins when no skin is named (SPEC-791 1, as the original does for items:
// every skin of the ammo box is registered on a map with only some ammo kinds), one object instance per entity.

void GhoulResetLevel(void)
{
	// the glue owns the records; GG_FreeAll (SpawnEntities) destroyed the engine objects already
}

IGhoulObject *GhoulModelObject(const char *dir, const char *model, const char *skin, bool allskins)
{
	(void)allskins;
	if (!GhoulSys() || !dir || !model)
		return NULL;
	GGModel *m = GG_FindModel(dir, model, true);
	if (!m) {
		gi.dprintf("Ghoul: could not register %s/%s\n", dir, model);
		return NULL;
	}
	(void)skin; // all-skins records already hold every skin of the model files
	return GG_Object(m);
}

static void GhoulSetScale(IGhoulInst *inst, float scale)
{
	Matrix4 m;
	memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = scale;
	m.m[3][3] = 1;
	m.flags = scale == 1.0f ? 1 : 0;
	inst->SetTransform(&m);
}

// skin by name on every material that has it (item skins such as `autobox` belong to one material)
static void GhoulSkinAll(IGhoulInst *inst, const char *skin)
{
	IGhoulObject *obj = inst->GetGhoulObject();
	int nm = obj->NumMaterials();
	for (int m = 1; m <= nm; m++) {
		GhoulID s = obj->FindSkin((GhoulID)m, skin, 0);
		if (s)
			inst->SetSkin((GhoulID)m, s, 0);
	}
}

static IGhoulInst *GhoulSetModelRec(edict_t *ent, GGModel *m, const char *dir, const char *model, const char *skin,
                                    float scale, const char *seq, int mode);

IGhoulInst *GhoulSetModel(edict_t *ent, const char *dir, const char *model, const char *skin, float scale,
                          const char *seq, int mode)
{
	GhoulRemove(ent);
	if (!GhoulModelObject(dir, model, skin, true))
		return NULL;
	GGModel *m = GG_FindModel(dir, model, true, NULL, NULL, true);
	return GhoulSetModelRec(ent, m, dir, model, skin, scale, seq, mode);
}

// A record keyed by its one skin, with only that skin registered (on the material named like the model, else on
// the materials that take it) — the single-ammunition pickups: the oracle registers `ammo_shotgun` with
// AMMO_00_SINGLE only on a map without slug pickups (G9's SP registration captures).
static GGModel *SkinnedRecord(const char *dir, const char *model, const char *skin)
{
	if (!GhoulSys() || !dir || !model)
		return NULL;
	GGModel *m = GG_FindModel(dir, model, false, skin);
	if (m && !GG_Prepared(m)) {
		IGhoulObject *o = GG_Object(m);
		if (skin && skin[0]) {
			if (o && o->FindMaterial(model))
				GG_RegisterSkin(m, model, skin);
			else
				GG_RegisterSkinAnyMaterial(m, skin);
		}
		GG_Prepare(m);
	}
	return m;
}

IGhoulObject *GhoulModelObjectSkinned(const char *dir, const char *model, const char *skin)
{
	GGModel *m = SkinnedRecord(dir, model, skin);
	return m ? GG_Object(m) : NULL;
}

IGhoulInst *GhoulSetModelSkinned(edict_t *ent, const char *dir, const char *model, const char *skin, float scale,
                                 const char *seq, int mode)
{
	GhoulRemove(ent);
	GGModel *m = SkinnedRecord(dir, model, skin);
	if (!m)
		return NULL;
	return GhoulSetModelRec(ent, m, dir, model, skin, scale, seq, mode);
}

static IGhoulInst *GhoulSetModelRec(edict_t *ent, GGModel *m, const char *dir, const char *model, const char *skin,
                                    float scale, const char *seq, int mode)
{
	GGInst *oi = GG_NewInst(m, ent);
	IGhoulInst *inst = GG_EngineInst(oi);
	if (!inst)
		return NULL;
	if (scale != 1.0f && scale > 0)
		GhoulSetScale(inst, scale);
	if (skin && skin[0])
		GhoulSkinAll(inst, skin);
	GhoulID sid = GG_FindSequence(m, seq ? seq : model);
	if (!sid && GG_Object(m)->NumSequences() > 0)
		sid = 1;
	if (sid)
		GG_Play(oi, sid, level.time, 0, true, mode);
	ent->ghoulInst = inst;
	ent->s.renderfx |= RF_GHOUL;
	static char pathbuf[MAX_EDICTS][96];
	int n = G_EdictNum(ent);
	if (n >= 0 && n < MAX_EDICTS) {
		Com_sprintf(pathbuf[n], sizeof(pathbuf[n]), "%s/%s", dir, model);
		ent->ghoulmodel = pathbuf[n];
	}
	return inst;
}

void GhoulPlay(edict_t *ent, const char *seq, int mode, float offset, bool restart)
{
	GGInst *oi = GG_ObjInstOfEntity(ent);
	if (!oi)
		return;
	GhoulID sid = GG_FindSequence(GG_InstModel(oi), seq);
	if (sid)
		GG_Play(oi, sid, level.time, offset, restart, mode);
}

float GhoulSeqDuration(edict_t *ent, const char *seq, int mode)
{
	GGInst *oi = GG_ObjInstOfEntity(ent);
	if (!oi || !ent->ghoulInst)
		return 0;
	GhoulID sid = GG_FindSequence(GG_InstModel(oi), seq);
	if (!sid)
		return 0;
	return ent->ghoulInst->SequenceDuration(sid, mode, NULL);
}

void GhoulRemove(edict_t *ent)
{
	if (ent->ghoulInst) {
		if (GG_ObjInstOfEntity(ent))
			GG_RemoveEntity(ent);
		else
			ent->ghoulInst->Destroy();
		ent->ghoulInst = NULL;
	}
	ent->s.renderfx &= ~RF_GHOUL;
	ent->ghoulmodel = NULL;
}

void GhoulSetTint(edict_t *ent, float r, float g, float b, float a)
{
	if (ent->ghoulInst)
		ent->ghoulInst->SetTintAll(r, g, b, a);
}

void GhoulPartsOff(edict_t *ent, const char *part)
{
	if (!ent->ghoulInst)
		return;
	GhoulID p = ent->ghoulInst->GetGhoulObject()->FindPart(part);
	if (p)
		ent->ghoulInst->SetPartOnOff(p, false);
}

// SPEC-700 rule 7.4: an entity's Ghoul events are advanced right before its own physics and think (G_RunFrame)
void GhoulEntityEvents(edict_t *e)
{
	if (ghl_no_server_update && ghl_no_server_update->value >= 0.5f)
		return;
	if (e->inuse && e->ghoulInst)
		e->ghoulInst->ProcessEvents(level.time);
}

void GhoulUpdateTransform(edict_t *ent) { (void)ent; }

// entity space -> world: rows forward, left (= -right), up (SPEC-011 matrix convention)
static void EntityAxes(const vec3_t angles, vec3_t f, vec3_t l, vec3_t u)
{
	vec3_t r;
	AngleVectors(angles, f, r, u);
	VectorNegate(r, l);
}

bool GhoulInstBoltPos(IGhoulInst *inst, const vec3_t origin, const vec3_t angles, const char *bolt, vec3_t out)
{
	if (!inst)
		return false;
	GhoulID b = inst->GetGhoulObject()->FindPart(bolt);
	if (!b)
		return false;
	Matrix4 m;
	memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = m.m[3][3] = 1;
	m.flags = 1;
	inst->GetBoltMatrix(level.time, &m, b, 5, true);
	vec3_t f, l, u;
	EntityAxes(angles, f, l, u);
	for (int i = 0; i < 3; i++)
		out[i] = origin[i] + m.m[3][0] * f[i] + m.m[3][1] * l[i] + m.m[3][2] * u[i];
	return true;
}

bool GhoulBoltPos(edict_t *ent, const char *bolt, vec3_t out)
{
	return GhoulInstBoltPos(ent->ghoulInst, ent->s.origin, ent->s.angles, bolt, out);
}

// ray test in entity space (SPEC-011 traces); returns 1 and the world hit point when hit
int GhoulRayHit(edict_t *ent, const vec3_t start, const vec3_t dir, float maxdist, float *dist, vec3_t point)
{
	if (!ent->ghoulInst)
		return 0;
	vec3_t f, l, u, d, ls, ld;
	EntityAxes(ent->s.angles, f, l, u);
	VectorSubtract(start, ent->s.origin, d);
	ls[0] = DotProduct(d, f);
	ls[1] = DotProduct(d, l);
	ls[2] = DotProduct(d, u);
	ld[0] = DotProduct(dir, f);
	ld[1] = DotProduct(dir, l);
	ld[2] = DotProduct(dir, u);
	HitRecord hits[20];
	int n = ent->ghoulInst->TraceRay(level.time, ls, ld, hits, 20);
	if (n <= 0 || hits[0].distance > maxdist)
		return 0;
	*dist = hits[0].distance;
	VectorMA(start, hits[0].distance, dir, point);
	return 1;
}
