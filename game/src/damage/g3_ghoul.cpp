// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - Ghoul helpers used by bodies and gore: name lookups, the registry of bolt-on
// models, attaching/removing bolt-ons, entity/world transforms, bolt positions, and small engine wrappers.
// Model records and bolt-ons go through the SPEC-791 glue of G9 (ghoulglue/gg_ghoul.h) for glue-managed instances;
// unmanaged instances (and the unit tests) use the engine instance directly.
// Implements: SPEC-011 (as a client), SPEC-791 §1, §5, §7 (subset), SPEC-722 (bolt-ons and skins)
#include "g3_local.h"
#include "../ghoulglue/gg_ghoul.h"
#include "../scriptglue/g_scriptglue.h"

#include <stdarg.h>
#include <stdio.h>

using namespace ghoul;

// ---------------------------------------------------------------------------------------------------------------
// engine wrappers
// ---------------------------------------------------------------------------------------------------------------
float G3_flrand(float lo, float hi)
{
	if (gix.flrand)
		return gix.flrand(lo, hi);
	return lo + (hi - lo) * ((rand() & 0x7fff) / 32768.0f);
}

int G3_irand(int lo, int hi)
{
	if (gix.irand)
		return gix.irand(lo, hi);
	if (hi <= lo)
		return lo;
	return lo + rand() % (hi - lo + 1);
}

float G3_Cvar(const char *name)
{
	if (!gix.cvar_value)
		return 0;
	return gix.cvar_value(name);
}

bool G3_Deathmatch(void) { return G3_Cvar("deathmatch") != 0; }
int G3_DeathmatchMode(void) { return (int)G3_Cvar("deathmatch"); }
bool G3_GoreTest(void) { return G3_Cvar("ai_goretest") != 0; }
bool G3_CreatePak(void) { return G3_Cvar("fs_createpak") != 0; }
bool G3_Cinematic(void)
{
	if (g3hooks.cinematic)
		return g3hooks.cinematic();
	return ScriptGlue_CinematicState() != 0; // G6b: the authoritative cinematic state (SPEC-751 1.1)
}
const char *G3_MapName(void) { return level.mapname; }
float G3_Time(void) { return level.time; }

void G3_Printf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	buf[sizeof buf - 1] = 0;
	if (gix.cprintf)
		gix.cprintf(NULL, PRINT_HIGH, "%s", buf); // console print without a client (Quake II convention)
	else if (gix.dprintf)
		gix.dprintf("%s", buf);
}

void G3_DPrintf(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof buf, fmt, ap);
	va_end(ap);
	buf[sizeof buf - 1] = 0;
	if (gix.dprintf)
		gix.dprintf("%s", buf); // developer print (SPEC-001 #13)
}

// vector as text with two decimals (diagnostics; 8 rotating buffers)
const char *G3_vs(const vec3_t v)
{
	static char buf[8][48];
	static int k;
	char *b = buf[k++ & 7];
	Com_sprintf(b, 48, "(%.2f %.2f %.2f)", v[0], v[1], v[2]);
	return b;
}

bool G3_ClassIs(const edict_t *ent, const char *cls)
{
	return ent && ent->classname && !Q_stricmp(ent->classname, cls);
}

edict_t *G3_SightClient(void)
{
	return G_FirstPlayer();
}

int G3_SoundIndex(const char *name) { return gix.soundindex ? gix.soundindex(name) : 0; }
int G3_EffectIndex(const char *name) { return gix.effectindex ? gix.effectindex(name) : 0; }

void G3_Sound(edict_t *ent, int chan, const char *name, float vol, float attn)
{
	int idx = G3_SoundIndex(name);
	if (idx && gix.sound)
		gix.sound(ent, chan, idx, vol, attn, 0, 0);
}

void G3_SoundAt(const vec3_t pos, int chan, const char *name, float vol, float attn)
{
	int idx = G3_SoundIndex(name);
	if (idx && gix.positioned_sound)
		gix.positioned_sound(pos, world, chan, idx, vol, attn, 0, 0);
}

// ---------------------------------------------------------------------------------------------------------------
// name lookups
// ---------------------------------------------------------------------------------------------------------------
IGhoulObject *G3G_Object(IGhoulInst *inst)
{
	return inst ? inst->GetGhoulObject() : NULL;
}

GhoulID G3G_Part(IGhoulInst *inst, const char *name)
{
	IGhoulObject *o = G3G_Object(inst);
	if (!o || !name || !*name)
		return 0;
	return o->FindPart(name);
}

GhoulID G3G_Material(IGhoulInst *inst, const char *name)
{
	IGhoulObject *o = G3G_Object(inst);
	if (!o || !name)
		return 0;
	return o->FindMaterial(name);
}

GhoulID G3G_Skin(IGhoulInst *inst, GhoulID material, const char *name)
{
	IGhoulObject *o = G3G_Object(inst);
	if (!o || !material || !name)
		return 0;
	return o->FindSkin(material, name, 0);
}

GhoulID G3G_Sequence(IGhoulInst *inst, const char *modelDir, const char *seq)
{
	IGhoulObject *o = G3G_Object(inst);
	if (!o || !seq)
		return 0;
	char path[256];
	Com_sprintf(path, sizeof path, "ghoul/%s/%s.ghl", modelDir ? modelDir : "", seq);
	return o->FindSequence(path);
}

bool G3G_PartShown(IGhoulInst *inst, GhoulID part)
{
	return inst && part && inst->IsPartOn(part);
}

void G3G_ShowPart(IGhoulInst *inst, GhoulID part, bool shown)
{
	if (inst && part)
		inst->SetPartOnOff(part, shown);
}

bool G3G_SetSkinByName(IGhoulInst *inst, const char *material, const char *skin, GhoulID part)
{
	GhoulID m = G3G_Material(inst, material);
	if (!m)
		return false;
	GhoulID s = G3G_Skin(inst, m, skin);
	if (!s)
		return false;
	inst->SetSkin(m, s, part);
	return true;
}

// ---------------------------------------------------------------------------------------------------------------
// model records and bolt-ons: through the SPEC-791 glue of G9 (ghoulglue/gg_ghoul.h) whenever the instance is
// managed by it (every character instance made by G4 is); otherwise directly on the engine instance (instances
// made outside the glue, unit tests). A small registry of G3's own is the fallback for model objects.
// ---------------------------------------------------------------------------------------------------------------
struct G3Model {
	char dir[64];
	char model[64];
	IGhoulObject *obj;
};
static G3Model g3models[256];
static int g3nummodels;

void G3G_ResetModels(void)
{
	g3nummodels = 0; // the engine objects were destroyed with the level (SPEC-791 §5)
}

static IGhoulObject *OwnModelObject(const char *dir, const char *model)
{
	for (int i = 0; i < g3nummodels; i++)
		if (!Q_stricmp(g3models[i].dir, dir) && !Q_stricmp(g3models[i].model, model))
			return g3models[i].obj;
	if (g3nummodels >= (int)(sizeof g3models / sizeof g3models[0]))
		return NULL;
	IGhoulSystem *sys = gix.GetGhoul ? gix.GetGhoul() : NULL;
	if (!sys)
		return NULL;
	IGhoulObject *obj = sys->CreateObject();
	if (!obj)
		return NULL;
	if (gix.RegisterGSQSequences)
		gix.RegisterGSQSequences(dir, model, obj);
	obj->FinishRegistration(true);
	bool specular = G3_Cvar("ghl_specular") > 0 && G3_Cvar("ghl_light_method") > 0;
	obj->Prepare(specular);
	G3Model &m = g3models[g3nummodels++];
	strncpy(m.dir, dir, sizeof m.dir - 1);
	m.dir[sizeof m.dir - 1] = 0;
	strncpy(m.model, model, sizeof m.model - 1);
	m.model[sizeof m.model - 1] = 0;
	m.obj = obj;
	return obj;
}

IGhoulObject *G3G_ModelObject(const char *dir, const char *model, const char *skin)
{
	if (!dir || !model)
		return NULL;
	if (g3hooks.modelObject)
		return g3hooks.modelObject(dir, model, skin);
	// bolt-on records are made with all skins; the skin is set per instance
	GGModel *gm = GG_FindModel(dir, model, true);
	if (gm && GG_Object(gm))
		return GG_Object(gm);
	return OwnModelObject(dir, model);
}

const char *G3G_ModelName(IGhoulObject *obj)
{
	if (!obj)
		return "";
	GGModel *gm = GG_ModelOfObject(obj);
	if (gm && GG_ModelList(gm))
		return GG_ModelList(gm);
	for (int i = 0; i < g3nummodels; i++)
		if (g3models[i].obj == obj)
			return g3models[i].model;
	return "";
}

static const char *ModelDirOf(IGhoulObject *obj)
{
	GGModel *gm = GG_ModelOfObject(obj);
	if (gm && GG_ModelDir(gm))
		return GG_ModelDir(gm);
	for (int i = 0; i < g3nummodels; i++)
		if (g3models[i].obj == obj)
			return g3models[i].dir;
	return NULL;
}

// glue lookups: the object instance at the root of an engine instance tree, and the bolt-on record of an instance
static GGInst *RootObjInst(IGhoulInst *inst)
{
	if (!inst)
		return NULL;
	while (inst->GetParent())
		inst = inst->GetParent();
	return GG_ObjInstOf(inst);
}

static GGBolt *FindBoltRec(GGBolt *b, IGhoulInst *inst)
{
	for (; b; b = GG_NextBolt(b)) {
		if (GG_BoltEngineInst(b) == inst)
			return b;
		GGBolt *c = FindBoltRec(GG_FirstChildBolt(b), inst);
		if (c)
			return c;
	}
	return NULL;
}

static GGBolt *BoltRecOf(IGhoulInst *inst)
{
	GGInst *oi = RootObjInst(inst);
	return oi ? FindBoltRec(GG_FirstBolt(oi), inst) : NULL;
}

static void ScaleTransform(IGhoulInst *inst, float scale)
{
	if (scale >= 0.99999f && scale <= 1.00001f)
		return;
	Matrix4 m;
	inst->GetTransform(&m);
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			m.m[r][c] *= scale;
	m.flags &= ~1u;
	inst->SetTransform(&m);
}

static void SetChildSkin(IGhoulObject *obj, IGhoulInst *child, const char *skin)
{
	if (!skin || !*skin)
		return;
	// SPEC-791 §7.4: material named like the model, else the first material that has that skin
	GhoulID m = obj->FindMaterial(G3G_ModelName(obj));
	GhoulID s = m ? obj->FindSkin(m, skin, 0) : 0;
	if (!s) {
		int n = obj->NumMaterials();
		for (int i = 1; i <= n && !s; i++) {
			s = obj->FindSkin((GhoulID)i, skin, 0);
			if (s)
				m = (GhoulID)i;
		}
	}
	if (m && s)
		child->SetSkin(m, s, 0);
}

static IGhoulInst *AttachNew(IGhoulInst *parent, GhoulID bolt, IGhoulObject *obj, const char *toBolt, float scale,
                             const char *seq, bool loop, const char *skin)
{
	if (!parent || !bolt || !obj)
		return NULL;
	GhoulID childBolt = 0;
	if (toBolt && *toBolt)
		childBolt = obj->FindPart(toBolt);
	if (!childBolt) {
		G3_DPrintf("Unable to add bolt on!\n");
		return NULL;
	}
	const char *name = seq ? seq : G3G_ModelName(obj);
	GGModel *gm = GG_ModelOfObject(obj);
	GGInst *poi = GG_ObjInstOf(parent);
	GGBolt *pb = poi ? NULL : BoltRecOf(parent);
	if (gm && (poi || pb)) {
		GGBolt *b = poi ? GG_AddBolt(poi, bolt, gm, childBolt, skin, scale)
		                : GG_AddBoltToBolt(pb, bolt, gm, childBolt, skin, scale);
		if (!b)
			return NULL;
		GhoulID s = GG_FindSequence(gm, name);
		if (s)
			GG_PlayBolt(b, s, G3_Time(), 0, true, loop ? kPlayLoop : kPlayOnce);
		return GG_BoltEngineInst(b);
	}
	IGhoulInst *child = obj->CreateInstance();
	if (!child)
		return NULL;
	child->SetUserValue(parent->GetUserValue());
	parent->AttachChild(bolt, child, childBolt);
	ScaleTransform(child, scale);
	SetChildSkin(obj, child, skin);
	const char *dir = ModelDirOf(obj);
	if (dir) {
		char path[256];
		Com_sprintf(path, sizeof path, "ghoul/%s/%s.ghl", dir, name);
		GhoulID s = obj->FindSequence(path);
		if (s)
			child->Play(s, G3_Time(), 0, true, loop ? kPlayLoop : kPlayOnce, false, false);
	}
	return child;
}

IGhoulInst *G3G_AttachNew(IGhoulInst *parent, GhoulID bolt, IGhoulObject *obj, const char *toBolt, float scale,
                          const char *seq, bool loop)
{
	return AttachNew(parent, bolt, obj, toBolt, scale, seq, loop, NULL);
}

IGhoulInst *G3G_AttachByName(IGhoulInst *parent, const char *bolt, const char *dir, const char *model,
                             const char *toBolt, float scale, const char *skin)
{
	GhoulID b = G3G_Part(parent, bolt);
	if (!b)
		return NULL;
	return AttachNew(parent, b, G3G_ModelObject(dir, model, skin), toBolt, scale, NULL, true, skin);
}

int G3G_NumChildren(IGhoulInst *inst) { return inst ? inst->NumChildren() : 0; }

IGhoulInst *G3G_Child(IGhoulInst *inst, int i, GhoulID *bolt)
{
	if (!inst)
		return NULL;
	GhoulID b = 0;
	IGhoulInst *c = inst->GetChild(i, &b);
	if (bolt)
		*bolt = b;
	return c;
}

IGhoulInst *G3G_ChildOnBolt(IGhoulInst *inst, GhoulID bolt, int nth)
{
	int n = G3G_NumChildren(inst);
	for (int i = 0; i < n; i++) {
		GhoulID b;
		IGhoulInst *c = G3G_Child(inst, i, &b);
		if (c && b == bolt && nth-- == 0)
			return c;
	}
	return NULL;
}

static void DestroyTree(IGhoulInst *inst)
{
	while (inst->NumChildren() > 0) {
		GhoulID b;
		IGhoulInst *c = inst->GetChild(0, &b);
		if (!c)
			break;
		DestroyTree(c);
		inst->DetachChild(c);
		c->Destroy();
	}
}

void G3G_RemoveChild(IGhoulInst *parent, IGhoulInst *child)
{
	if (!parent || !child)
		return;
	GGInst *poi = GG_ObjInstOf(parent);
	if (poi) {
		GGBolt *b = GG_FindBoltByInst(poi, child);
		if (b) {
			GG_RemoveBolt(poi, b);
			return;
		}
	} else if (BoltRecOf(parent)) {
		// a bolt-on of a bolt-on in a glue-managed tree: the glue has no removal for it; hide it instead
		child->SetAllPartsOnOff(false);
		return;
	}
	DestroyTree(child);
	parent->DetachChild(child);
	child->Destroy();
}

void G3G_RemoveChildrenOnBolt(IGhoulInst *parent, GhoulID bolt)
{
	if (!parent || !bolt)
		return;
	GGInst *poi = GG_ObjInstOf(parent);
	if (poi) {
		GG_RemoveBoltsOn(poi, bolt);
		return;
	}
	for (int guard = 0; guard < 64; guard++) {
		IGhoulInst *c = G3G_ChildOnBolt(parent, bolt);
		if (!c)
			break;
		G3G_RemoveChild(parent, c);
	}
}

void G3G_RemoveAllChildren(IGhoulInst *parent)
{
	if (!parent)
		return;
	GGInst *poi = GG_ObjInstOf(parent);
	if (poi) {
		GG_RemoveAllBolts(poi);
		return;
	}
	DestroyTree(parent);
}

// true when `inst` is still attached somewhere below `root` (pointers kept by bodies may have been destroyed)
bool G3G_IsDescendant(IGhoulInst *root, IGhoulInst *inst)
{
	if (!root || !inst)
		return false;
	int n = root->NumChildren();
	for (int i = 0; i < n; i++) {
		GhoulID b;
		IGhoulInst *c = root->GetChild(i, &b);
		if (c == inst || G3G_IsDescendant(c, inst))
			return true;
	}
	return false;
}

// model directory of an instance's model record ("enemy/meso"), NULL when unknown
const char *G3G_ModelDirOfInst(IGhoulInst *inst)
{
	if (!inst)
		return NULL;
	GGModel *gm = GG_ModelOfInst(inst);
	if (gm && GG_ModelDir(gm))
		return GG_ModelDir(gm);
	return ModelDirOf(inst->GetGhoulObject());
}

// Move a bolt-on (and its skins and sequence) from one instance to another at the same bolt (severed pieces).
IGhoulInst *G3G_MoveChild(IGhoulInst *from, IGhoulInst *child, IGhoulInst *to, GhoulID bolt, edict_t *newOwner)
{
	if (!from || !child || !to)
		return NULL;
	GGInst *foi = GG_ObjInstOf(from);
	GGInst *toi = GG_ObjInstOf(to);
	GGBolt *b = foi ? GG_FindBoltByInst(foi, child) : NULL;
	if (b && toi) {
		GGBolt *nb = GG_AddBolt(toi, bolt, GG_BoltModel(b), GG_BoltChildBolt(b), NULL, GG_BoltScale(b));
		IGhoulInst *ni = GG_BoltEngineInst(nb);
		if (ni) {
			IGhoulObject *o = child->GetGhoulObject();
			int n = o ? o->NumMaterials() : 0;
			for (int m = 1; m <= n; m++) {
				GhoulID s = child->GetSkin((GhoulID)m, 0, 0);
				if (s)
					ni->SetSkin((GhoulID)m, s, 0);
			}
			if (GG_BoltSequence(b))
				GG_PlayBolt(nb, GG_BoltSequence(b), G3_Time(), 0, true, kPlayLoop);
		}
		GG_RemoveBolt(foi, b);
		return ni;
	}
	if (foi || BoltRecOf(from)) {
		G3G_RemoveChild(from, child);
		return NULL;
	}
	GhoulID cb = child->GetAttachBolt();
	from->DetachChild(child);
	if (newOwner)
		child->SetUserValue(newOwner);
	to->AttachChild(bolt, child, cb);
	return child;
}

// A copy of the instance for another entity (severed pieces): SPEC-791 §6 Clone.
IGhoulInst *G3G_CloneFor(IGhoulInst *src, edict_t *ent)
{
	if (!src || !ent)
		return NULL;
	GGModel *gm = GG_ModelOfInst(src);
	if (gm) {
		GGInst *oi = GG_CloneInst(gm, ent, src);
		if (oi)
			return GG_EngineInst(oi);
	}
	IGhoulInst *c = src->Clone(false);
	if (c) {
		c->SetUserValue(ent);
		ent->ghoulInst = c;
	}
	return c;
}

// A new instance of a model for an entity (dropped bolt-ons).
IGhoulInst *G3G_NewInstFor(IGhoulObject *obj, edict_t *ent)
{
	if (!obj || !ent)
		return NULL;
	GGModel *gm = GG_ModelOfObject(obj);
	if (gm) {
		GGInst *oi = GG_NewInst(gm, ent);
		if (oi)
			return GG_EngineInst(oi);
	}
	IGhoulInst *c = obj->CreateInstance();
	if (c) {
		c->SetUserValue(ent);
		ent->ghoulInst = c;
	}
	return c;
}

// ---------------------------------------------------------------------------------------------------------------
// transforms
// ---------------------------------------------------------------------------------------------------------------
void G3_EntityAxes(const edict_t *ent, vec3_t fwd, vec3_t left, vec3_t up)
{
	vec3_t a = {-ent->s.angles[0], ent->s.angles[1], -ent->s.angles[2]};
	vec3_t r;
	AngleVectors(a, fwd, r, up);
	VectorNegate(r, left);
}

void G3_EntityToWorld(const edict_t *ent, const vec3_t local, vec3_t world)
{
	vec3_t f, l, u;
	G3_EntityAxes(ent, f, l, u);
	for (int i = 0; i < 3; i++)
		world[i] = ent->s.origin[i] + local[0] * f[i] + local[1] * l[i] + local[2] * u[i];
}

void G3_WorldToEntity(const edict_t *ent, const vec3_t worldp, vec3_t local)
{
	vec3_t f, l, u, d;
	G3_EntityAxes(ent, f, l, u);
	VectorSubtract(worldp, ent->s.origin, d);
	local[0] = DotProduct(d, f);
	local[1] = DotProduct(d, l);
	local[2] = DotProduct(d, u);
}

void G3_WorldDirToEntity(const edict_t *ent, const vec3_t wdir, vec3_t ldir)
{
	vec3_t f, l, u;
	G3_EntityAxes(ent, f, l, u);
	ldir[0] = DotProduct(wdir, f);
	ldir[1] = DotProduct(wdir, l);
	ldir[2] = DotProduct(wdir, u);
}

void G3_EntityDirToWorld(const edict_t *ent, const vec3_t ldir, vec3_t wdir)
{
	vec3_t f, l, u;
	G3_EntityAxes(ent, f, l, u);
	for (int i = 0; i < 3; i++)
		wdir[i] = ldir[0] * f[i] + ldir[1] * l[i] + ldir[2] * u[i];
}

bool G3G_BoltMatrix(IGhoulInst *inst, GhoulID bolt, Matrix4 *m, int mode, bool parents)
{
	if (!inst || !bolt)
		return false;
	memset(m, 0, sizeof *m);
	m->m[0][0] = m->m[1][1] = m->m[2][2] = m->m[3][3] = 1;
	m->flags = 1;
	m->m[3][0] = m->m[3][1] = m->m[3][2] = 1.0e30f; // marker: "bolt not in the current sequence"
	inst->GetBoltMatrix(G3_Time(), m, bolt, mode, parents);
	if (m->m[3][0] == 1.0e30f) {
		m->m[3][0] = m->m[3][1] = m->m[3][2] = 0;
		return false;
	}
	return true;
}

bool G3G_BoltWorld(edict_t *ent, IGhoulInst *inst, GhoulID bolt, vec3_t pos, vec3_t fwd)
{
	Matrix4 m;
	if (!ent || !G3G_BoltMatrix(inst ? inst : ent->ghoulInst, bolt, &m, 5, true))
		return false;
	vec3_t local = {m.m[3][0], m.m[3][1], m.m[3][2]};
	G3_EntityToWorld(ent, local, pos);
	if (fwd) {
		vec3_t ld = {m.m[0][0], m.m[0][1], m.m[0][2]};
		G3_EntityDirToWorld(ent, ld, fwd);
		VectorNormalize(fwd);
	}
	return true;
}

bool G3G_BoltWorldByName(edict_t *ent, const char *bolt, vec3_t pos)
{
	if (!ent || !ent->ghoulInst)
		return false;
	return G3G_BoltWorld(ent, ent->ghoulInst, G3G_Part(ent->ghoulInst, bolt), pos);
}
