// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game-side Ghoul glue (G9): model records, object instances, bolt-ons, skins,
// sequences, availability bytes, simple models, name helpers, attached-object records, level lifecycle.
// Savegame chunks are in gg_save.cpp, placement helpers and the aim controller in gg_aim.cpp, the
// `ghoul_model` test entity in gg_testent.cpp.
// Implements: SPEC-791 Interface 1-4, Behaviour 1-9
#include "gg_internal.h"

GGModel *gg_models = NULL;          // creation order
static GGModel *gg_models_tail = NULL;
GGAttached *gg_attached[MAX_EDICTS];
uint32_t gg_serial = 0;

// Human sequence names in the order of the animation records (one row list, shared with g3_body.cpp).
#define G3A(n, ...) n,
static const char *const gg_humananims[] = {
#include "../damage/g3_humananims.inc"
};
#undef G3A
#define GG_NUM_HUMANANIMS ((int)(sizeof(gg_humananims) / sizeof(gg_humananims[0])))

//-----------------------------------------------------------------------------------------------------------
// small helpers

ghoul::IGhoulSystem *GG_System(void) { return gix.GetGhoul ? gix.GetGhoul() : NULL; }

void gg_strcpy(char *dst, const char *src, int size)
{
	if (!src)
		src = "";
	int i = 0;
	for (; i < size - 1 && src[i]; i++)
		dst[i] = src[i];
	dst[i] = 0;
}

static bool gg_streq(const char *a, const char *b)
{
	if (!a)
		a = "";
	if (!b)
		b = "";
	return Q_stricmp(a, b) == 0;
}

static bool gg_empty(const char *s) { return !s || !*s; }

void gg_conprint(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	gix.cprintf(NULL, PRINT_HIGH, "%s", buf);
}

void gg_devprint(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	gix.dprintf("%s", buf);
}

static void gg_seqpath(char *out, int size, const char *dir, const char *name)
{
	Com_sprintf(out, size, "ghoul/%s/%s.ghl", dir, name);
}

static void gg_bump(void *pp, int *cap, int count, int elemsize)
{
	void **p = (void **)pp;
	if (count < *cap)
		return;
	int ncap = *cap ? *cap * 2 : 8;
	void *n = realloc(*p, (size_t)ncap * elemsize);
	if (!n)
		gi.error("Ghoul glue: out of memory");
	*p = n;
	*cap = ncap;
}

//-----------------------------------------------------------------------------------------------------------
// 1. model records

static void gg_availability(GGModel *m)
{
	// B4: case-sensitive prefix test on the directory
	bool human = !strncmp(m->dir, "enemy/meso", 10) || !strncmp(m->dir, "enemy/ecto", 10) ||
	             !strncmp(m->dir, "enemy/female", 12);
	if (!human || !m->obj) {
		memset(m->avail, 1, sizeof(m->avail));
		return;
	}
	memset(m->avail, 0, sizeof(m->avail));
	char path[256];
	for (int i = 0; i < GG_NUM_HUMANANIMS && i < (int)sizeof(m->avail); i++) {
		gg_seqpath(path, sizeof(path), m->dir, gg_humananims[i]);
		m->avail[i] = m->obj->FindSequence(path) ? 1 : 0;
	}
}

GGModel *gg_newrecord(const char *dir, const char *list, const char *skin, const char *base)
{
	GGModel *m = (GGModel *)calloc(1, sizeof(GGModel));
	if (!m)
		gi.error("Ghoul glue: out of memory");
	gg_strcpy(m->dir, dir, sizeof(m->dir));
	gg_strcpy(m->list, list, sizeof(m->list));
	gg_strcpy(m->skin, skin, sizeof(m->skin));
	gg_strcpy(m->base, base, sizeof(m->base));
	m->serial = ++gg_serial;
	return m;
}

void gg_linkrecord(GGModel *m)
{
	m->next = NULL;
	if (gg_models_tail)
		gg_models_tail->next = m;
	else
		gg_models = m;
	gg_models_tail = m;
}

// B1 steps 2-3: engine object with the base sequence and the sequence list
bool gg_registerobject(GGModel *m)
{
	ghoul::IGhoulSystem *gs = GG_System();
	if (!gs)
		return false;
	m->obj = gs->CreateObject();
	if (!m->obj)
		return false;
	if (m->base[0]) {
		char path[256];
		gg_seqpath(path, sizeof(path), m->dir, m->base);
		m->obj->RegisterSequence(path, false);
	}
	if (gix.RegisterGSQSequences)
		gix.RegisterGSQSequences(m->dir, m->list, m->obj);
	return true;
}

GGModel *GG_FindModel(const char *dir, const char *list, bool allskins, const char *skin, const char *base,
                      bool findonly)
{
	if (gg_empty(dir) || gg_empty(list))
		return NULL;
	for (GGModel *m = gg_models; m; m = m->next) {
		if (gg_streq(m->dir, dir) && gg_streq(m->list, list) && gg_streq(m->skin, skin) &&
		    gg_streq(m->base, base))
			return m;
	}
	if (findonly)
		return NULL;

	GGModel *m = gg_newrecord(dir, list, skin, base);
	m->allskins = allskins ? 1 : 0;
	if (!gg_registerobject(m)) {
		free(m);
		return NULL;
	}
	m->obj->FinishRegistration(allskins);
	if (allskins)
		GG_Prepare(m);
	gg_availability(m);
	gg_linkrecord(m);
	return m;
}

GGModel *GG_ModelOfObject(ghoul::IGhoulObject *obj)
{
	if (!obj)
		return NULL;
	for (GGModel *m = gg_models; m; m = m->next)
		if (m->obj == obj)
			return m;
	return NULL;
}

GGModel *GG_ModelOfInst(ghoul::IGhoulInst *inst)
{
	if (!inst)
		return NULL;
	return GG_ModelOfObject(inst->GetGhoulObject());
}

GGInst *GG_ObjInstOf(ghoul::IGhoulInst *inst)
{
	GGModel *m = GG_ModelOfInst(inst);
	if (!m)
		return NULL;
	for (GGInst *oi = m->insts; oi; oi = oi->next)
		if (oi->inst == inst)
			return oi;
	return NULL;
}

GGInst *GG_ObjInstOfEntity(edict_t *ent) { return ent ? GG_ObjInstOf(ent->ghoulInst) : NULL; }

ghoul::IGhoulObject *GG_Object(GGModel *m) { return m ? m->obj : NULL; }
const char *GG_ModelDir(GGModel *m) { return m ? m->dir : ""; }
const char *GG_ModelList(GGModel *m) { return m ? m->list : ""; }
bool GG_Prepared(GGModel *m) { return m && m->prepared; }

//-----------------------------------------------------------------------------------------------------------
// 2. skins

void gg_rememberpair(GGModel *m, const char *material, const char *skin)
{
	gg_bump(&m->pairs, &m->cappairs, m->npairs, sizeof(GGSkinPair));
	gg_strcpy(m->pairs[m->npairs].material, material, sizeof(m->pairs[0].material));
	gg_strcpy(m->pairs[m->npairs].skin, skin, sizeof(m->pairs[0].skin));
	m->npairs++;
}

GhoulID GG_RegisterSkin(GGModel *m, const char *material, const char *skin)
{
	if (!m || !m->obj || !material || !skin)
		return 0;
	GhoulID mat = m->obj->FindMaterial(material);
	if (!mat)
		return 0;
	if (m->prepared)
		return m->obj->FindSkin(mat, skin, 0);
	gg_rememberpair(m, material, skin);
	return m->obj->RegisterSkin(mat, skin, 0);
}

GhoulID GG_RegisterSkinAnyMaterial(GGModel *m, const char *skin)
{
	if (!m || !m->obj || !skin || m->prepared)
		return 0;
	// materials in id order (alphabetical by name), stopping at the first that provides the skin (SPEC-AMEND-006
	// G9.16: `acc_headset` → ACC_HEADSET only; its second material `MATERIAL #24` comes later)
	int n = m->obj->NumMaterials();
	for (int i = 1; i <= n; i++) {
		char name[256];
		name[0] = 0;
		m->obj->GetMaterialName((GhoulID)i, name);
		GhoulID id = m->obj->RegisterSkin((GhoulID)i, skin, 0);
		gg_rememberpair(m, name, skin);
		if (id)
			return id;
	}
	return 0;
}

void GG_SetSkinInst(ghoul::IGhoulInst *inst, const char *dir, const char *file, const char *material,
                    const char *skin)
{
	if (!inst) {
		gg_conprint("ERROR: trying to set skin %s on NULL ghoulInst!\n", skin ? skin : "");
		return;
	}
	ghoul::IGhoulObject *obj = inst->GetGhoulObject();
	GhoulID mat = (obj && material) ? obj->FindMaterial(material) : 0;
	GhoulID sk = (obj && mat && skin) ? obj->FindSkin(mat, skin, 0) : 0;
	if (mat && sk) {
		inst->SetSkin(mat, sk, 0);
		return;
	}
	if (!mat)
		gg_conprint("WARNING: Can't find material:%s\n", material ? material : "(null)"); // as C printf of NULL
	else if (!sk)
		gg_conprint("WARNING: Can't find skin:%s\n", skin ? skin : "(null)");
	else
		gg_conprint("WARNING: unknown error in SetSkin2 on object %s/%s.\n", dir ? dir : "", file ? file : "");
}

void GG_SetSkin(edict_t *ent, const char *dir, const char *file, const char *material, const char *skin,
                bool alsoparts)
{
	if (!ent) {
		gg_conprint("WARNING: Can't set skin for NULL edict\n");
		return;
	}
	GG_SetSkinInst(ent->ghoulInst, dir, file, material, skin);
	if (!alsoparts)
		return;
	GGAttached *a = GG_Attached(ent);
	if (!a)
		return;
	for (int i = 1; i <= a->partcount; i++)
		GG_SetSkinInst(GG_TrackedPart(ent, i), dir, file, material, skin);
}

//-----------------------------------------------------------------------------------------------------------
// 3. preparing, sequences, availability

void GG_Prepare(GGModel *m)
{
	if (!m || !m->obj || m->prepared)
		return;
	bool specular = ghl_specular && ghl_light_method && ghl_specular->value > 0 && ghl_light_method->value > 0;
	// B3: a temporary object instance without an entity exists while the record is prepared
	ghoul::IGhoulInst *tmp = m->obj->CreateInstance();
	m->obj->Prepare(specular);
	if (tmp)
		tmp->Destroy();
	m->prepared = 1;
}

GhoulID GG_FindSequence(GGModel *m, const char *name)
{
	if (!m || !m->obj || !name)
		return 0;
	char path[256];
	gg_seqpath(path, sizeof(path), m->dir, name);
	GhoulID id = m->obj->FindSequence(path);
	if (!m->prepared) {
		gg_devprint("Tried to find sequence %s before registration finished!\n", name);
		return 0;
	}
	return id;
}

bool GG_HasAnimation(GGModel *m, int i)
{
	if (!m || i < 0 || i >= (int)sizeof(m->avail))
		return false;
	return m->avail[i] != 0;
}

int GG_HumanAnimCount(void) { return GG_NUM_HUMANANIMS; }

const char *GG_HumanAnimName(int i) { return (i >= 0 && i < GG_NUM_HUMANANIMS) ? gg_humananims[i] : NULL; }

//-----------------------------------------------------------------------------------------------------------
// 4. object instances

void gg_linkinst(GGModel *m, GGInst *oi)
{
	oi->model = m;
	oi->next = NULL;
	GGInst **pp = &m->insts;
	while (*pp)
		pp = &(*pp)->next;
	*pp = oi;
}

static void gg_unlinkinst(GGInst *oi)
{
	if (!oi->model)
		return;
	for (GGInst **pp = &oi->model->insts; *pp; pp = &(*pp)->next) {
		if (*pp == oi) {
			*pp = oi->next;
			break;
		}
	}
	oi->next = NULL;
}

GGInst *gg_allocinst(void)
{
	GGInst *oi = (GGInst *)calloc(1, sizeof(GGInst));
	if (!oi)
		gi.error("Ghoul glue: out of memory");
	oi->serial = ++gg_serial;
	return oi;
}

static void gg_bindentity(GGInst *oi, edict_t *ent)
{
	oi->owner = ent;
	if (ent) {
		ent->ghoulInst = oi->inst;
		oi->inst->SetUserValue(ent);
	}
}

GGInst *GG_NewInst(GGModel *m, edict_t *ent)
{
	if (!m || !m->obj)
		return NULL;
	ghoul::IGhoulInst *inst = m->obj->CreateInstance();
	if (!inst)
		return NULL;
	GGInst *oi = gg_allocinst();
	oi->inst = inst;
	gg_bindentity(oi, ent);
	if (gix.TurnOffPartsFromGSQ) {
		char poff[128];
		Com_sprintf(poff, sizeof(poff), "%s_poff", m->list);
		gix.TurnOffPartsFromGSQ(m->dir, poff, m->obj, inst);
	}
	gg_linkinst(m, oi);
	return oi;
}

GGInst *GG_CloneInst(GGModel *m, edict_t *ent, ghoul::IGhoulInst *src)
{
	if (!m || !src)
		return NULL;
	ghoul::IGhoulInst *inst = src->Clone(false);
	if (!inst)
		return NULL;
	GGInst *oi = gg_allocinst();
	oi->inst = inst;
	gg_bindentity(oi, ent);
	gg_linkinst(m, oi);
	return oi;
}

static void gg_setbolttreeuser(GGBolt *b, edict_t *ent)
{
	for (; b; b = b->next) {
		if (b->inst)
			b->inst->SetUserValue(ent);
		gg_setbolttreeuser(b->bolts, ent);
	}
}

void GG_MoveInst(GGInst *oi, edict_t *newent)
{
	if (!oi)
		return;
	gg_setbolttreeuser(oi->bolts, newent);
	if (oi->owner && oi->owner->ghoulInst == oi->inst)
		oi->owner->ghoulInst = NULL;
	oi->owner = newent;
	if (newent) {
		newent->ghoulInst = oi->inst;
		oi->inst->SetUserValue(newent);
	}
}

void GG_Play(GGInst *oi, GhoulID seq, float time, float offset, bool restart, int mode, bool matchphase,
             bool reverse)
{
	if (!oi || !oi->inst)
		return;
	oi->seq = seq;
	if (seq)
		oi->inst->Play(seq, time, offset, restart, mode, matchphase, reverse);
}

static void gg_destroybolt(GGBolt *b, ghoul::IGhoulInst *parentinst);

static void gg_destroyboltlist(GGBolt *list, ghoul::IGhoulInst *parentinst)
{
	while (list) {
		GGBolt *next = list->next;
		gg_destroybolt(list, parentinst);
		list = next;
	}
}

static void gg_destroybolt(GGBolt *b, ghoul::IGhoulInst *parentinst)
{
	gg_destroyboltlist(b->bolts, b->inst);
	b->bolts = NULL;
	if (b->inst) {
		if (parentinst)
			parentinst->DetachChild(b->inst);
		b->inst->Destroy();
	}
	gg_forgetbolt(b);
	free(b);
}

void GG_DestroyInst(GGInst *oi)
{
	if (!oi)
		return;
	gg_destroyboltlist(oi->bolts, oi->inst);
	oi->bolts = NULL;
	if (oi->owner && oi->owner->ghoulInst == oi->inst)
		oi->owner->ghoulInst = NULL;
	if (oi->inst)
		oi->inst->Destroy();
	gg_unlinkinst(oi);
	free(oi);
}

void GG_RemoveEntity(edict_t *ent)
{
	if (!ent)
		return;
	if (ent->ghoulInst) {
		for (GGModel *m = gg_models; m; m = m->next) {
			GGInst *oi = m->insts;
			while (oi) {
				GGInst *next = oi->next;
				if (oi->owner == ent)
					GG_DestroyInst(oi);
				oi = next;
			}
		}
	}
	ent->ghoulInst = NULL;
}

ghoul::IGhoulInst *GG_EngineInst(GGInst *oi) { return oi ? oi->inst : NULL; }
GGModel *GG_InstModel(GGInst *oi) { return oi ? oi->model : NULL; }
edict_t *GG_InstOwner(GGInst *oi) { return oi ? oi->owner : NULL; }
GhoulID GG_InstSequence(GGInst *oi) { return oi ? oi->seq : 0; }

//-----------------------------------------------------------------------------------------------------------
// 5. bolt-ons

GGBolt *gg_allocbolt(void)
{
	GGBolt *b = (GGBolt *)calloc(1, sizeof(GGBolt));
	if (!b)
		gi.error("Ghoul glue: out of memory");
	b->serial = ++gg_serial;
	b->scale = 1.0f;
	return b;
}

static void gg_appendbolt(GGBolt **list, GGBolt *b)
{
	b->next = NULL;
	while (*list)
		list = &(*list)->next;
	*list = b;
}

static void gg_scaleinst(ghoul::IGhoulInst *inst, float s)
{
	ghoul::Matrix4 m;
	inst->GetTransform(&m);
	for (int r = 0; r < 3; r++)
		for (int c = 0; c < 3; c++)
			m.m[r][c] *= s;
	m.flags &= ~1u;
	inst->SetTransform(&m);
}

edict_t *GG_BoltOwner(GGBolt *b)
{
	while (b && !b->root)
		b = b->parent;
	return (b && b->root) ? b->root->owner : NULL;
}

GGInst *GG_BoltRoot(GGBolt *b)
{
	while (b && !b->root)
		b = b->parent;
	return b ? b->root : NULL;
}

static GGBolt *gg_addbolt(ghoul::IGhoulInst *parentinst, GGBolt **list, GGInst *root, GGBolt *parent,
                          GhoulID parentbolt, GGModel *child, GhoulID childbolt, const char *skin, float scale)
{
	if (!parentinst || !child || !child->obj || !parentbolt || !childbolt)
		return NULL;
	ghoul::IGhoulInst *inst = child->obj->CreateInstance();
	if (!inst)
		return NULL;
	GGBolt *b = gg_allocbolt();
	b->root = root;
	b->parent = parent;
	b->pbolt = parentbolt;
	b->cbolt = childbolt;
	b->child = child;
	b->scale = scale;
	b->inst = inst;
	edict_t *owner = root ? root->owner : GG_BoltOwner(parent);
	inst->SetUserValue(owner);
	parentinst->AttachChild(parentbolt, inst, childbolt);
	if (scale < 0.99999f || scale > 1.00001f)
		gg_scaleinst(inst, scale);
	if (!gg_empty(skin)) {
		GhoulID mat = child->obj->FindMaterial(child->list);
		GhoulID sk = 0;
		if (mat)
			sk = child->obj->FindSkin(mat, skin, 0);
		else {
			int n = child->obj->NumMaterials();
			for (int i = 1; i <= n && !sk; i++) {
				sk = child->obj->FindSkin((GhoulID)i, skin, 0);
				if (sk)
					mat = (GhoulID)i;
			}
		}
		if (mat && sk)
			inst->SetSkin(mat, sk, 0);
	}
	gg_appendbolt(list, b);
	gg_rememberbolt(b);
	if (child->obj && !child->prepared)
		GG_Prepare(child);
	return b;
}

GGBolt *GG_AddBolt(GGInst *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt, const char *skin,
                   float scale)
{
	if (!parent)
		return NULL;
	return gg_addbolt(parent->inst, &parent->bolts, parent, NULL, parentbolt, child, childbolt, skin, scale);
}

GGBolt *GG_AddBoltToBolt(GGBolt *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt,
                         const char *skin, float scale)
{
	if (!parent)
		return NULL;
	return gg_addbolt(parent->inst, &parent->bolts, NULL, parent, parentbolt, child, childbolt, skin, scale);
}

GGBolt *GG_AddBoltChecked(GGInst *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt,
                          const char *skin, float scale)
{
	if (!child || !parentbolt || !childbolt) {
		gg_devprint("Unable to add bolt on!\n");
		return NULL;
	}
	return GG_AddBolt(parent, parentbolt, child, childbolt, skin, scale);
}

GGBolt *GG_AddBoltPlaying(GGInst *parent, GhoulID parentbolt, GGModel *child, GhoulID childbolt, GhoulID seq,
                          const char *skin, float scale)
{
	if (!child || !parentbolt || !childbolt || !seq) {
		gg_devprint("Unable to add bolt on!\n");
		return NULL;
	}
	GGBolt *b = GG_AddBolt(parent, parentbolt, child, childbolt, skin, scale);
	if (b)
		GG_PlayBolt(b, seq, level.time, 0, true, ghoul::kPlayLoop);
	return b;
}

void GG_PlayBolt(GGBolt *b, GhoulID seq, float time, float offset, bool restart, int mode, bool matchphase,
                 bool reverse)
{
	if (!b || !b->inst)
		return;
	b->seq = seq;
	if (seq)
		b->inst->Play(seq, time, offset, restart, mode, matchphase, reverse);
}

GGBolt *GG_FindBoltOn(GGInst *oi, GhoulID parentbolt)
{
	if (!oi)
		return NULL;
	for (GGBolt *b = oi->bolts; b; b = b->next)
		if (b->pbolt == parentbolt)
			return b;
	return NULL;
}

GGBolt *GG_FindBoltByInst(GGInst *oi, ghoul::IGhoulInst *inst)
{
	if (!oi || !inst)
		return NULL;
	for (GGBolt *b = oi->bolts; b; b = b->next)
		if (b->inst == inst)
			return b;
	return NULL;
}

static void gg_unlinkfrom(GGBolt **list, GGBolt *b)
{
	for (; *list; list = &(*list)->next) {
		if (*list == b) {
			*list = b->next;
			b->next = NULL;
			return;
		}
	}
}

void GG_RemoveBolt(GGInst *oi, GGBolt *b)
{
	if (!oi || !b)
		return;
	for (GGBolt *x = oi->bolts; x; x = x->next) {
		if (x == b) {
			gg_unlinkfrom(&oi->bolts, b);
			gg_destroybolt(b, oi->inst);
			return;
		}
	}
}

void GG_RemoveBoltsOn(GGInst *oi, GhoulID parentbolt)
{
	if (!oi)
		return;
	GGBolt *b = oi->bolts;
	while (b) {
		GGBolt *next = b->next;
		if (b->pbolt == parentbolt) {
			gg_unlinkfrom(&oi->bolts, b);
			gg_destroybolt(b, oi->inst);
		}
		b = next;
	}
}

void GG_RemoveAllBolts(GGInst *oi)
{
	if (!oi)
		return;
	gg_destroyboltlist(oi->bolts, oi->inst);
	oi->bolts = NULL;
}

void GG_PauseAllBolts(GGInst *oi)
{
	if (!oi)
		return;
	for (GGBolt *b = oi->bolts; b; b = b->next)
		if (b->inst)
			b->inst->Pause(level.time);
}

ghoul::IGhoulInst *GG_BoltEngineInst(GGBolt *b) { return b ? b->inst : NULL; }
GGModel *GG_BoltModel(GGBolt *b) { return b ? b->child : NULL; }
GhoulID GG_BoltSequence(GGBolt *b) { return b ? b->seq : 0; }
GhoulID GG_BoltParentBolt(GGBolt *b) { return b ? b->pbolt : 0; }
GhoulID GG_BoltChildBolt(GGBolt *b) { return b ? b->cbolt : 0; }
float GG_BoltScale(GGBolt *b) { return b ? b->scale : 1.0f; }
GGBolt *GG_FirstBolt(GGInst *oi) { return oi ? oi->bolts : NULL; }
GGBolt *GG_FirstChildBolt(GGBolt *b) { return b ? b->bolts : NULL; }
GGBolt *GG_NextBolt(GGBolt *b) { return b ? b->next : NULL; }

//-----------------------------------------------------------------------------------------------------------
// 6. attached-object records

GGAttached *GG_Attached(edict_t *ent)
{
	if (!ent || !g_edicts)
		return NULL;
	int n = G_EdictNum(ent);
	if (n < 0 || n >= MAX_EDICTS)
		return NULL;
	return gg_attached[n];
}

GGAttached *GG_AttachedCreate(edict_t *ent)
{
	if (!ent || !g_edicts)
		return NULL;
	int n = G_EdictNum(ent);
	if (n < 0 || n >= MAX_EDICTS)
		return NULL;
	if (gg_attached[n])
		return gg_attached[n];
	GGAttached *a = (GGAttached *)calloc(1, sizeof(GGAttached));
	if (!a)
		gi.error("Ghoul glue: out of memory");
	a->owner = ent;
	gg_attached[n] = a;
	return a;
}

void GG_AttachedFree(edict_t *ent)
{
	if (!ent || !g_edicts)
		return;
	int n = G_EdictNum(ent);
	if (n < 0 || n >= MAX_EDICTS || !gg_attached[n])
		return;
	free(gg_attached[n]->parts);
	free(gg_attached[n]);
	gg_attached[n] = NULL;
}

void GG_EntityFreed(edict_t *ent)
{
	if (!ent)
		return;
	GG_AttachedFree(ent);
	GG_RemoveEntity(ent);
}

void gg_attachedappend(GGAttached *a, GGBolt *b)
{
	gg_bump(&a->parts, &a->listcap, a->listsize, sizeof(GGBolt *));
	a->parts[a->listsize++] = b;
}

// a destroyed bolt-on leaves an empty slot in every tracked list
void gg_attachedforget(GGBolt *b)
{
	for (int n = 0; n < MAX_EDICTS; n++) {
		GGAttached *a = gg_attached[n];
		if (!a)
			continue;
		for (int i = 0; i < a->listsize; i++)
			if (a->parts[i] == b)
				a->parts[i] = NULL;
	}
}

//-----------------------------------------------------------------------------------------------------------
// 7. simple model (B8)

bool GG_SetSimpleModel(edict_t *ent, const char *dir, const char *file, const char *skin, const char *material)
{
	if (!ent || ent->ghoulInst || (ent->s.renderfx & RF_GHOUL))
		return false;
	GGModel *m;
	if (!gg_empty(skin) && !gg_empty(material)) {
		m = GG_FindModel(dir, file, false, skin, NULL);
		if (m)
			GG_RegisterSkin(m, material, skin);
	} else
		m = GG_FindModel(dir, file, true);
	if (!m) {
		gg_devprint("SetSimpleGhoulModel couldn't create ghoul object %s/%s.\n", dir ? dir : "", file ? file : "");
		return false;
	}
	GG_Prepare(m);
	GhoulID seq = GG_FindSequence(m, file);
	if (!seq) {
		gg_devprint("SetSimpleGhoulModel couldn't find ghoul sequence %s.\n", file ? file : "");
		return false;
	}
	GGInst *oi = GG_NewInst(m, ent);
	if (!oi) {
		gg_devprint("SetSimpleGhoulModel couldn't add instance of ghoul object %s.\n", dir ? dir : "");
		return false;
	}
	ent->s.renderfx = RF_GHOUL;
	ent->ghoulInst = oi->inst;
	GG_Play(oi, seq, level.time, 0, true, ghoul::kPlayLoop);
	return true;
}

//-----------------------------------------------------------------------------------------------------------
// 8. name helpers (B9)

void GG_HidePartInst(ghoul::IGhoulInst *inst, const char *part)
{
	if (!inst || !part)
		return;
	ghoul::IGhoulObject *obj = inst->GetGhoulObject();
	GhoulID id = obj ? obj->FindPart(part) : 0;
	if (id)
		inst->SetPartOnOff(id, false);
	else
		gg_devprint("ERROR: SimpleModelRemoveObject - can find part %s!\n", part);
}

void GG_ShowPartInst(ghoul::IGhoulInst *inst, const char *part)
{
	if (!inst || !part)
		return;
	ghoul::IGhoulObject *obj = inst->GetGhoulObject();
	GhoulID id = obj ? obj->FindPart(part) : 0;
	if (id)
		inst->SetPartOnOff(id, true);
}

void GG_HidePart(edict_t *ent, const char *part)
{
	if (ent)
		GG_HidePartInst(ent->ghoulInst, part);
}

void GG_ShowPart(edict_t *ent, const char *part)
{
	if (ent)
		GG_ShowPartInst(ent->ghoulInst, part);
}

void GG_ShowOnlyPart(ghoul::IGhoulInst *inst, const char *part)
{
	if (!inst || !part)
		return;
	ghoul::IGhoulObject *obj = inst->GetGhoulObject();
	GhoulID id = obj ? obj->FindPart(part) : 0;
	if (!id)
		return;
	inst->SetAllPartsOnOff(false);
	inst->SetPartOnOff(id, true);
}

void GG_SetAllParts(ghoul::IGhoulInst *inst, bool shown)
{
	if (inst)
		inst->SetAllPartsOnOff(shown);
}

void GG_HideAllRecursive(ghoul::IGhoulInst *inst)
{
	if (!inst)
		return;
	inst->SetAllPartsOnOff(false);
	int n = inst->NumChildren();
	for (int i = 0; i < n; i++) {
		GhoulID bolt = 0;
		GG_HideAllRecursive(inst->GetChild(i, &bolt));
	}
}

static GGModel *gg_entrymodel(const gg_entry_t *child, const char *skin, const char *skinmaterial)
{
	GGModel *m;
	if (!gg_empty(skin)) {
		m = GG_FindModel(child->dir, child->file, false, skin, NULL);
		if (m && skinmaterial)
			GG_RegisterSkin(m, skinmaterial, skin);
	} else
		m = GG_FindModel(child->dir, child->file, true);
	if (m)
		GG_Prepare(m);
	return m;
}

// shared body of the table-entry bolt helpers; complex = bolting onto a bolt-on (message names)
static GGBolt *gg_boltentry(ghoul::IGhoulInst *parentinst, GGInst *rootoi, GGBolt *parentbolton,
                            const gg_entry_t *parent, const char *parentbolt, const gg_entry_t *child,
                            const char *childbolt, const char *skin, bool complex)
{
	if (!parentinst || !parent || !child)
		return NULL;
	const char *who = complex ? "ComplexModelAddBolt" : "SimpleModelAddBolt";
	const char *skinmat = child->skinmaterial ? child->skinmaterial : parent->skinmaterial;
	GGModel *cm = gg_entrymodel(child, skin, skinmat);
	GGModel *pm = GG_ModelOfInst(parentinst);
	if (!pm || !cm) {
		if (!pm)
			gg_devprint("ERROR: %s - can't find object %s!\n", who, parent->file ? parent->file : "");
		if (!cm)
			gg_devprint("ERROR: %s - can't find object %s!\n", who, child->file ? child->file : "");
		return NULL;
	}
	GhoulID pb = parentbolt ? pm->obj->FindPart(parentbolt) : 0;
	GhoulID cb = childbolt ? cm->obj->FindPart(childbolt) : 0;
	GhoulID seq = GG_FindSequence(cm, child->file);
	if (!pb)
		gg_devprint("ERROR: %s - can't find bolt %s!\n", who, parentbolt ? parentbolt : "");
	if (!cb)
		gg_devprint("ERROR: SimpleModelAddBolt - can't find bolt %s!\n", childbolt ? childbolt : "");
	if (!pb || !cb || !seq)
		return NULL;
	GGBolt *b;
	if (parentbolton)
		b = GG_AddBoltToBolt(parentbolton, pb, cm, cb, NULL, 1.0f);
	else
		b = GG_AddBolt(rootoi, pb, cm, cb, NULL, 1.0f);
	if (!b)
		return NULL;
	if (!gg_empty(skin) && skinmat)
		GG_SetSkinInst(b->inst, child->dir, child->file, skinmat, skin);
	GG_PlayBolt(b, seq, level.time, 0, true, ghoul::kPlayStatic);
	return b;
}

GGBolt *GG_BoltEntry(ghoul::IGhoulInst *inst, const gg_entry_t *parent, const char *parentbolt,
                     const gg_entry_t *child, const char *childbolt, const char *skin)
{
	GGInst *oi = GG_ObjInstOf(inst);
	if (!oi)
		return NULL;
	return gg_boltentry(inst, oi, NULL, parent, parentbolt, child, childbolt, skin, false);
}

static void gg_track(GGAttached *a, GGBolt *b)
{
	if (!a || !b)
		return;
	if (a->partcount < GG_MAX_TRACKED_PARTS) {
		a->partcount++;
		a->shownmask |= 1u << (a->partcount - 1);
	}
	gg_attachedappend(a, b);
}

GGBolt *GG_AddTrackedPart(edict_t *ent, const gg_entry_t *parent, const char *parentbolt,
                          const gg_entry_t *child, const char *childbolt, const char *skin)
{
	if (!ent)
		return NULL;
	GGBolt *b = GG_BoltEntry(ent->ghoulInst, parent, parentbolt, child, childbolt, skin);
	if (b)
		gg_track(GG_Attached(ent), b);
	return b;
}

GGBolt *GG_BoltEntryOnBolt(GGBolt *parent, const gg_entry_t *parententry, const char *parentbolt,
                           const gg_entry_t *child, const char *childbolt, const char *skin)
{
	if (!parent)
		return NULL;
	GGBolt *b = gg_boltentry(parent->inst, NULL, parent, parententry, parentbolt, child, childbolt, skin, true);
	if (!b)
		return NULL;
	edict_t *owner = GG_BoltOwner(b);
	GGAttached *a = GG_Attached(owner);
	if (!a)
		gg_devprint("ERROR: ComplexModelAddBolt - can't find root edict for %s!\n", parentbolt ? parentbolt : "");
	else
		gg_track(a, b);
	return b;
}

GGBolt *GG_FindBoltNamed(edict_t *ent, const gg_entry_t *entry, const char *bolt)
{
	if (!ent || !entry)
		return NULL;
	GGModel *m = GG_FindModel(entry->dir, entry->file, true, NULL, NULL, true);
	if (!m) {
		gg_devprint("ERROR: SimpleModelFindBolt - can't find object %s!\n", entry->file ? entry->file : "");
		return NULL;
	}
	GhoulID b = bolt ? m->obj->FindPart(bolt) : 0;
	if (!b) {
		gg_devprint("ERROR: SimpleModelFindBolt - can't find bolt %s!\n", bolt ? bolt : "");
		return NULL;
	}
	GGInst *oi = GG_ObjInstOfEntity(ent);
	if (!oi) {
		gg_devprint("ERROR: SimpleModelFindBolt - can't find instance of %s!\n", entry->file ? entry->file : "");
		return NULL;
	}
	return GG_FindBoltOn(oi, b);
}

GGBolt *GG_TrackedPartBolt(edict_t *ent, int i)
{
	GGAttached *a = GG_Attached(ent);
	if (!a || i < 1 || i > a->listsize)
		return NULL;
	return a->parts[i - 1];
}

ghoul::IGhoulInst *GG_TrackedPart(edict_t *ent, int i)
{
	GGBolt *b = GG_TrackedPartBolt(ent, i);
	return b ? b->inst : NULL;
}

void GG_PlayByName(edict_t *ent, const char *name, bool loop)
{
	if (!ent || !ent->ghoulInst)
		return;
	GhoulID seq = GG_FindSequence(GG_ModelOfInst(ent->ghoulInst), name);
	if (!seq)
		return;
	GGInst *oi = GG_ObjInstOf(ent->ghoulInst);
	if (oi)
		oi->seq = seq;
	ent->ghoulInst->Play(seq, level.time, 0, true, loop ? ghoul::kPlayLoop : ghoul::kPlayOnce, false, false);
}

GhoulID GG_PlayByNameInst(ghoul::IGhoulInst *inst, const char *name, int code)
{
	if (!inst)
		return 0;
	GhoulID seq = GG_FindSequence(GG_ModelOfInst(inst), name);
	if (!seq)
		return 0;
	int mode;
	switch (code) {
	case 0: mode = ghoul::kPlayOnce; break;
	case 1: mode = ghoul::kPlayLoop; break;
	case 2: mode = ghoul::kPlayStatic; break;
	default: return seq;
	}
	inst->Play(seq, level.time, 0, true, mode, false, false);
	return seq;
}

void GG_ScaleInst(ghoul::IGhoulInst *inst, float factor)
{
	if (inst)
		gg_scaleinst(inst, factor);
}

void GG_ScaleModel(edict_t *ent, float factor)
{
	if (ent)
		GG_ScaleInst(ent->ghoulInst, factor);
}

//-----------------------------------------------------------------------------------------------------------
// 9. level lifecycle (B5)

static void gg_destroyrecords(bool callsystem)
{
	for (GGModel *m = gg_models; m; m = m->next) {
		while (m->insts)
			GG_DestroyInst(m->insts);
	}
	GGModel *m = gg_models;
	while (m) {
		GGModel *next = m->next;
		if (m->obj)
			m->obj->Destroy();
		free(m->pairs);
		free(m);
		m = next;
	}
	gg_models = gg_models_tail = NULL;
	gg_resetboltregistry();
	if (callsystem) {
		ghoul::IGhoulSystem *gs = GG_System();
		if (gs)
			gs->DestroyAllObjects();
	}
}

void GG_FreeAll(void)
{
	for (int n = 0; n < MAX_EDICTS; n++) {
		if (gg_attached[n]) {
			free(gg_attached[n]->parts);
			free(gg_attached[n]);
			gg_attached[n] = NULL;
		}
	}
	gg_destroyrecords(true);
	gg_resetidmap();
}

void GG_LevelSpawn(void)
{
	static const char *const gore[] = {"g_arm_torn_anim1", "g_bone_anim1", "g_brain", "g_cap1", "g_cap2",
	                                   "g_cap3", "g_exit_chunky_anim1", "guts_bolton", "g_exit_lrg",
	                                   "g_exit_smll", "g_guts", "g_guts3", "g_neck_torn_anim2"};
	for (const char *g : gore)
		GG_FindModel("Enemy/bolt", g, true);
}

void GG_Shutdown(void)
{
	GG_FreeAll();
}

void GG_Stats(int *models, int *insts, int *bolts, int *attached)
{
	int nm = 0, ni = 0, na = 0;
	for (GGModel *m = gg_models; m; m = m->next) {
		nm++;
		for (GGInst *oi = m->insts; oi; oi = oi->next)
			ni++;
	}
	for (int n = 0; n < MAX_EDICTS; n++)
		if (gg_attached[n])
			na++;
	if (models)
		*models = nm;
	if (insts)
		*insts = ni;
	if (bolts)
		*bolts = gg_boltcount();
	if (attached)
		*attached = na;
}
