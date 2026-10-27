// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - Ghoul glue savegame chunks (G9): AI-table entries of the three Ghoul kinds, GGOB/GGON/
// STRG model records, GGOI/GHLI object instances, GGBI/GHLI bolt-ons, EDOI/EDBN/EDBL attached-object records,
// the instance id remap of SPEC-752 5, and the bolt-on registry used by ids and statistics.
// Implements: SPEC-791 Interface 5, Behaviour 13; SPEC-752 5; SPEC-700 "Savegame chunks of the AI" (Ghoul part)
#include "gg_internal.h"

#define GHLI_BUFFER 16384

//-----------------------------------------------------------------------------------------------------------
// bolt-on registry (creation order)

static GGBolt **gg_bolts = NULL;
static int gg_nbolts = 0, gg_capbolts = 0;

void gg_rememberbolt(GGBolt *b)
{
	if (gg_nbolts >= gg_capbolts) {
		int ncap = gg_capbolts ? gg_capbolts * 2 : 64;
		GGBolt **n = (GGBolt **)realloc(gg_bolts, sizeof(GGBolt *) * ncap);
		if (!n)
			gi.error("Ghoul glue: out of memory");
		gg_bolts = n;
		gg_capbolts = ncap;
	}
	gg_bolts[gg_nbolts++] = b;
}

void gg_forgetbolt(GGBolt *b)
{
	for (int i = 0; i < gg_nbolts; i++) {
		if (gg_bolts[i] == b) {
			memmove(gg_bolts + i, gg_bolts + i + 1, sizeof(GGBolt *) * (gg_nbolts - i - 1));
			gg_nbolts--;
			break;
		}
	}
	gg_attachedforget(b);
}

void gg_resetboltregistry(void) { gg_nbolts = 0; }

int gg_boltcount(void) { return gg_nbolts; }

//-----------------------------------------------------------------------------------------------------------
// ids

static uint32_t gg_maxid = 0;

void GG_AssignIds(uint32_t *counter)
{
	if (!counter)
		return;
	// models, object instances and bolt-ons, each kind in creation order
	for (GGModel *m = gg_models; m; m = m->next)
		if (!m->id)
			m->id = (*counter)++;
	for (GGModel *m = gg_models; m; m = m->next)
		for (GGInst *oi = m->insts; oi; oi = oi->next)
			if (!oi->id)
				oi->id = (*counter)++;
	for (int i = 0; i < gg_nbolts; i++)
		if (!gg_bolts[i]->id)
			gg_bolts[i]->id = (*counter)++;
}

uint32_t GG_MaxId(void) { return gg_maxid; }

static void gg_seenid(uint32_t id)
{
	if (id > gg_maxid)
		gg_maxid = id;
}

static GGModel *gg_modelbyid(uint32_t id)
{
	if (!id)
		return NULL;
	for (GGModel *m = gg_models; m; m = m->next)
		if (m->id == id)
			return m;
	return NULL;
}

static GGInst *gg_instbyid(uint32_t id)
{
	if (!id)
		return NULL;
	for (GGModel *m = gg_models; m; m = m->next)
		for (GGInst *oi = m->insts; oi; oi = oi->next)
			if (oi->id == id)
				return oi;
	return NULL;
}

static GGBolt *gg_boltbyid(uint32_t id)
{
	if (!id)
		return NULL;
	for (int i = 0; i < gg_nbolts; i++)
		if (gg_bolts[i]->id == id)
			return gg_bolts[i];
	return NULL;
}

//-----------------------------------------------------------------------------------------------------------
// chunk helpers

static void wchunk(int id, const void *data, int len)
{
	if (gix.WriteChunk)
		gix.WriteChunk(id, data, len);
}

static void rchunk(int id, void *data, int len)
{
	if (gix.ReadChunk)
		gix.ReadChunk(id, data, len, NULL);
}

static int rchunkany(int id, void **out)
{
	*out = NULL;
	if (!gix.ReadChunk)
		return 0;
	return gix.ReadChunk(id, NULL, 0, out);
}

static void wint(int id, int v) { wchunk(id, &v, 4); }

static int rint(int id)
{
	int v = 0;
	rchunk(id, &v, 4);
	return v;
}

static void wstrg(const char *s)
{
	if (!s)
		s = "";
	wchunk('STRG', s, (int)strlen(s) + 1);
}

static void rstrg(char *out, int size)
{
	void *p = NULL;
	int n = rchunkany('STRG', &p);
	if (p) {
		int k = n < size - 1 ? n : size - 1;
		memcpy(out, p, k > 0 ? k : 0);
		out[k > 0 ? k : 0] = 0;
		gix.TagFree(p);
	} else
		out[0] = 0;
}

static int gg_entref(edict_t *e) { return e && g_edicts ? G_EdictNum(e) : -1; }

static edict_t *gg_entfromref(int n)
{
	if (n < 0 || !g_edicts || n >= globals.max_edicts)
		return NULL;
	return &g_edicts[n];
}

// note-callback table of the instance serializer
static ghoul::IGhoulCallback *gg_cbtable[64];
static int gg_ncb = 0;

void GG_RegisterCallback(ghoul::IGhoulCallback *cb)
{
	if (!cb)
		return;
	for (int i = 0; i < gg_ncb; i++)
		if (gg_cbtable[i] == cb)
			return;
	if (gg_ncb < 64)
		gg_cbtable[gg_ncb++] = cb;
}

static int (*gg_fnencode)(void (*fn)(edict_t *)) = NULL;
static void (*(*gg_fndecode)(int))(edict_t *) = NULL;

void GG_SetFunctionCodec(int (*encode)(void (*fn)(edict_t *)), void (*(*decode)(int))(edict_t *))
{
	gg_fnencode = encode;
	gg_fndecode = decode;
}

static void gg_writeghli(ghoul::IGhoulInst *inst, GGModel *m)
{
	static unsigned char buf[GHLI_BUFFER];
	char name[256];
	Com_sprintf(name, sizeof(name), "ghoul/%s/%s", m ? m->dir : "", m ? m->list : "");
	int n = inst->SaveState(buf, GHLI_BUFFER, gg_cbtable, gg_ncb, name);
	if (n < 0)
		n = 0;
	wchunk('GHLI', buf, n);
}

// id map of SPEC-752 5 (old instance id -> new)
static GhoulID gg_idmap_old[4096], gg_idmap_new[4096];
static int gg_nidmap = 0;

void gg_resetidmap(void) { gg_nidmap = 0; }

static void gg_mapid(GhoulID oldid, GhoulID newid)
{
	if (!oldid)
		return;
	for (int i = 0; i < gg_nidmap; i++) {
		if (gg_idmap_old[i] == oldid) {
			gg_idmap_new[i] = newid;
			return;
		}
	}
	if (gg_nidmap < 4096) {
		gg_idmap_old[gg_nidmap] = oldid;
		gg_idmap_new[gg_nidmap++] = newid;
	}
}

static ghoul::IGhoulInst *gg_readghli(GGModel *m)
{
	void *p = NULL;
	int n = rchunkany('GHLI', &p);
	if (!m || !m->obj) {
		if (p)
			gix.TagFree(p);
		return NULL;
	}
	ghoul::IGhoulInst *inst = m->obj->CreateInstance();
	if (inst && p) {
		char name[256];
		Com_sprintf(name, sizeof(name), "ghoul/%s/%s", m->dir, m->list);
		GhoulID oldid = inst->LoadState(p, n, gg_cbtable, gg_ncb, name);
		gg_mapid(oldid, inst->GetInstanceId());
	}
	if (p)
		gix.TagFree(p);
	return inst;
}

void GG_RemapEntityIds(void)
{
	if (!g_edicts)
		return;
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || !(e->s.effects & 1))
			continue;
		unsigned char *base = (unsigned char *)e;
		for (int k = 0; k < 4; k++) {
			GhoulID *id = (GhoulID *)(base + 100 + 2 * k);
			if (!*id)
				continue;
			GhoulID nid = 0;
			for (int j = 0; j < gg_nidmap; j++)
				if (gg_idmap_old[j] == *id)
					nid = gg_idmap_new[j];
			*id = nid;
		}
	}
}

//-----------------------------------------------------------------------------------------------------------
// AI tables (part 1)

struct gg_aicc {
	int id;
	int classcode;
};

static int gg_cmpids(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y ? 1 : 0;
}

static int gg_collectids(int kind, uint32_t *ids, int max)
{
	int n = 0;
	if (kind == GG_KIND_MODEL) {
		for (GGModel *m = gg_models; m && n < max; m = m->next)
			ids[n++] = m->id;
	} else if (kind == GG_KIND_INST) {
		for (GGModel *m = gg_models; m; m = m->next)
			for (GGInst *oi = m->insts; oi && n < max; oi = oi->next)
				ids[n++] = oi->id;
	} else {
		for (int i = 0; i < gg_nbolts && n < max; i++)
			ids[n++] = gg_bolts[i]->id;
	}
	qsort(ids, n, sizeof(uint32_t), gg_cmpids);
	return n;
}

void GG_WriteTable(int kind)
{
	static uint32_t ids[8192];
	int n = gg_collectids(kind, ids, 8192);
	wint('AICT', n);
	for (int i = 0; i < n; i++) {
		gg_aicc c = {(int)ids[i], 0};
		wchunk('AICC', &c, sizeof(c));
	}
}

// ids read from the tables, waiting for their data chunks
static uint32_t gg_pending[3][8192];
static int gg_npending[3];

void GG_ReadTable(int kind)
{
	if (kind < 0 || kind > 2)
		return;
	if (kind == GG_KIND_MODEL)
		GG_FreeAll();
	int n = rint('AICT');
	gg_npending[kind] = 0;
	for (int i = 0; i < n; i++) {
		gg_aicc c = {0, 0};
		rchunk('AICC', &c, sizeof(c));
		if (gg_npending[kind] < 8192)
			gg_pending[kind][gg_npending[kind]++] = (uint32_t)c.id;
		gg_seenid((uint32_t)c.id);
	}
}

//-----------------------------------------------------------------------------------------------------------
// part 2

#pragma pack(push, 1)
struct gg_ggob {
	int zero;                 // 0
	uint8_t prepared;         // 4
	uint8_t allskins;         // 5
	uint8_t avail[1024];      // 6
	char dir[100];            // 1030
	char list[100];           // 1130
	char skin[100];           // 1230
	char base[100];           // 1330
	uint8_t undefined[2];     // 1430
};
struct gg_ggoi {
	int zero;                 // 0
	int entity;               // 4
	int model;                // 8
	uint16_t seq;             // 12
	uint16_t undefined;       // 14
};
struct gg_ggbi {
	uint8_t direct;           // 0
	uint8_t undefined1[3];    // 1
	int parentbolton;         // 4
	int ownerinst;            // 8
	uint16_t pbolt;           // 12
	uint16_t undefined2;      // 14
	int zero;                 // 16
	uint16_t cbolt;           // 20
	uint16_t seq;             // 22
	int childmodel;           // 24
	float scale;              // 28
};
struct gg_edoi {
	int owner;                // 0
	int undefined1;           // 4
	int partkind;             // 8 = 1
	int link;                 // 12
	unsigned shownmask;       // 16
	int partcount;            // 20
	int undefined2;           // 24
	int fallkind;             // 28 = 2
	int fallsubject;          // 32
	int falldir;              // 36
	float fallstart;          // 40
	float pivotheight;        // 44
	float radius;             // 48
	float angleofs;           // 52
	float startangles[3];     // 56
	float pivot[3];           // 68
	float falldirection[3];   // 80
	int restorethink;         // 92
};
#pragma pack(pop)
static_assert(sizeof(gg_ggob) == 1432, "SPEC-791 GGOB is 1432 bytes");
static_assert(sizeof(gg_ggoi) == 16, "SPEC-791 GGOI is 16 bytes");
static_assert(sizeof(gg_ggbi) == 32, "SPEC-791 GGBI is 32 bytes");
static_assert(sizeof(gg_edoi) == 96, "SPEC-791 EDOI is 96 bytes");

static void gg_writemodel(GGModel *m)
{
	gg_ggob r;
	memset(&r, 0, sizeof(r));
	r.prepared = m->prepared;
	r.allskins = m->allskins;
	memcpy(r.avail, m->avail, sizeof(r.avail));
	gg_strcpy(r.dir, m->dir, sizeof(r.dir));
	gg_strcpy(r.list, m->list, sizeof(r.list));
	gg_strcpy(r.skin, m->skin, sizeof(r.skin));
	gg_strcpy(r.base, m->base, sizeof(r.base));
	wchunk('GGOB', &r, sizeof(r));
	if (!m->allskins) {
		wint('GGON', m->npairs * 2);
		for (int i = 0; i < m->npairs; i++) {
			wstrg(m->pairs[i].material);
			wstrg(m->pairs[i].skin);
		}
	}
}

static void gg_readmodel(uint32_t id)
{
	gg_ggob r;
	rchunk('GGOB', &r, sizeof(r));
	r.dir[99] = r.list[99] = r.skin[99] = r.base[99] = 0;
	GGModel *m = gg_newrecord(r.dir, r.list, r.skin, r.base);
	m->id = id;
	m->allskins = r.allskins;
	memcpy(m->avail, r.avail, sizeof(m->avail));
	bool ok = gg_registerobject(m);
	if (ok)
		m->obj->FinishRegistration(m->allskins != 0);
	if (!m->allskins) {
		int s = rint('GGON');
		for (int i = 0; i + 1 < s; i += 2) {
			char mat[100], skin[100];
			rstrg(mat, sizeof(mat));
			rstrg(skin, sizeof(skin));
			if (ok)
				GG_RegisterSkin(m, mat, skin);
		}
		if (s & 1) {
			char dummy[100];
			rstrg(dummy, sizeof(dummy));
		}
	}
	m->prepared = r.prepared; // object method 2 is not called (SPEC-791 B13)
	gg_linkrecord(m);
}

static void gg_writeinst(GGInst *oi)
{
	gg_ggoi r;
	memset(&r, 0, sizeof(r));
	r.entity = gg_entref(oi->owner);
	r.model = oi->model ? (int)oi->model->id : 0;
	r.seq = oi->seq;
	wchunk('GGOI', &r, sizeof(r));
	if (oi->inst)
		gg_writeghli(oi->inst, oi->model);
}

static void gg_readinst(uint32_t id)
{
	gg_ggoi r;
	rchunk('GGOI', &r, sizeof(r));
	GGModel *m = gg_modelbyid((uint32_t)r.model);
	edict_t *ent = gg_entfromref(r.entity);
	ghoul::IGhoulInst *inst = gg_readghli(m);
	if (!m || !inst)
		return;
	GGInst *oi = gg_allocinst();
	oi->id = id;
	oi->inst = inst;
	oi->owner = ent;
	oi->seq = r.seq;
	inst->SetUserValue(ent);
	if (ent)
		ent->ghoulInst = inst;
	gg_linkinst(m, oi);
}

static void gg_writebolt(GGBolt *b)
{
	gg_ggbi r;
	memset(&r, 0, sizeof(r));
	r.direct = b->root ? 1 : 0;
	r.parentbolton = b->parent ? (int)b->parent->id : 0;
	r.ownerinst = b->root ? (int)b->root->id : 0;
	r.pbolt = b->pbolt;
	r.cbolt = b->cbolt;
	r.seq = b->seq;
	r.childmodel = b->child ? (int)b->child->id : 0;
	r.scale = b->scale;
	wchunk('GGBI', &r, sizeof(r));
	if (b->inst)
		gg_writeghli(b->inst, b->child);
}

struct gg_boltload {
	GGBolt *b;
	gg_ggbi r;
};

void GG_WriteData(int kind)
{
	static uint32_t ids[8192];
	int n = gg_collectids(kind, ids, 8192);
	for (int i = 0; i < n; i++) {
		if (kind == GG_KIND_MODEL) {
			GGModel *m = gg_modelbyid(ids[i]);
			if (m)
				gg_writemodel(m);
		} else if (kind == GG_KIND_INST) {
			GGInst *oi = gg_instbyid(ids[i]);
			if (oi)
				gg_writeinst(oi);
		} else {
			GGBolt *b = gg_boltbyid(ids[i]);
			if (b)
				gg_writebolt(b);
		}
	}
}

void GG_ReadData(int kind)
{
	if (kind < 0 || kind > 2)
		return;
	int n = gg_npending[kind];
	if (kind == GG_KIND_MODEL) {
		for (int i = 0; i < n; i++)
			gg_readmodel(gg_pending[kind][i]);
		return;
	}
	if (kind == GG_KIND_INST) {
		for (int i = 0; i < n; i++)
			gg_readinst(gg_pending[kind][i]);
		return;
	}
	// bolt-ons: read all, then attach in dependency order
	gg_boltload *load = (gg_boltload *)calloc(n ? n : 1, sizeof(gg_boltload));
	for (int i = 0; i < n; i++) {
		gg_ggbi &r = load[i].r;
		rchunk('GGBI', &r, sizeof(r));
		GGModel *child = gg_modelbyid((uint32_t)r.childmodel);
		ghoul::IGhoulInst *inst = gg_readghli(child);
		if (!child || !inst)
			continue;
		GGBolt *b = gg_allocbolt();
		b->id = gg_pending[kind][i];
		b->pbolt = r.pbolt;
		b->cbolt = r.cbolt;
		b->seq = r.seq;
		b->child = child;
		b->scale = r.scale;
		b->inst = inst;
		load[i].b = b;
		gg_rememberbolt(b);
	}
	int remaining = n, guard = n + 1;
	while (remaining > 0 && guard-- > 0) {
		for (int i = 0; i < n; i++) {
			GGBolt *b = load[i].b;
			if (!b)
				continue;
			const gg_ggbi &r = load[i].r;
			ghoul::IGhoulInst *parentinst = NULL;
			if (r.direct) {
				GGInst *oi = gg_instbyid((uint32_t)r.ownerinst);
				if (oi) {
					b->root = oi;
					parentinst = oi->inst;
					GGBolt **pp = &oi->bolts;
					while (*pp)
						pp = &(*pp)->next;
					*pp = b;
				}
			} else {
				GGBolt *p = gg_boltbyid((uint32_t)r.parentbolton);
				if (p && (p->root || p->parent)) {
					b->parent = p;
					parentinst = p->inst;
					GGBolt **pp = &p->bolts;
					while (*pp)
						pp = &(*pp)->next;
					*pp = b;
				} else if (p)
					continue; // parent not linked yet
			}
			if (parentinst) {
				parentinst->AttachChild(b->pbolt, b->inst, b->cbolt);
				b->inst->SetUserValue(GG_BoltOwner(b));
			}
			load[i].b = NULL;
			remaining--;
		}
	}
	free(load);
}

//-----------------------------------------------------------------------------------------------------------
// attached-object records (OINM, EDOI, EDBN, EDBL)

void GG_WriteAttached(void)
{
	int n = 0;
	for (int i = 0; i < MAX_EDICTS; i++)
		if (gg_attached[i])
			n++;
	wint('OINM', n);
	for (int i = 0; i < MAX_EDICTS; i++) {
		GGAttached *a = gg_attached[i];
		if (!a)
			continue;
		gg_edoi r;
		memset(&r, 0, sizeof(r));
		r.owner = gg_entref(a->owner);
		r.partkind = 1;
		r.link = gg_entref(a->link);
		r.shownmask = a->shownmask;
		r.partcount = a->partcount;
		r.fallkind = 2;
		r.fallsubject = gg_entref(a->fallsubject);
		r.falldir = a->falldir;
		r.fallstart = a->fallstart;
		r.pivotheight = a->pivotheight;
		r.radius = a->radius;
		r.angleofs = a->angleofs;
		for (int k = 0; k < 3; k++) {
			r.startangles[k] = a->startangles[k];
			r.pivot[k] = a->pivot[k];
			r.falldirection[k] = a->falldirection[k];
		}
		r.restorethink = (a->restorethink && gg_fnencode) ? gg_fnencode(a->restorethink) : -1;
		wchunk('EDOI', &r, sizeof(r));
		wint('EDBN', a->listsize);
		if (a->listsize) {
			int *ids = (int *)calloc(a->listsize, sizeof(int));
			for (int k = 0; k < a->listsize; k++)
				ids[k] = a->parts[k] ? (int)a->parts[k]->id : 0;
			wchunk('EDBL', ids, a->listsize * 4);
			free(ids);
		}
	}
}

void GG_ReadAttached(void)
{
	int n = rint('OINM');
	for (int i = 0; i < n; i++) {
		gg_edoi r;
		rchunk('EDOI', &r, sizeof(r));
		edict_t *owner = gg_entfromref(r.owner);
		GGAttached *a = owner ? GG_AttachedCreate(owner) : NULL;
		int nb = rint('EDBN');
		int *ids = NULL;
		if (nb > 0) {
			ids = (int *)calloc(nb, sizeof(int));
			rchunk('EDBL', ids, nb * 4);
		}
		if (a) {
			a->link = gg_entfromref(r.link);
			a->shownmask = r.shownmask;
			a->partcount = r.partcount;
			a->fallsubject = gg_entfromref(r.fallsubject);
			a->falldir = r.falldir;
			a->fallstart = r.fallstart;
			a->pivotheight = r.pivotheight;
			a->radius = r.radius;
			a->angleofs = r.angleofs;
			for (int k = 0; k < 3; k++) {
				a->startangles[k] = r.startangles[k];
				a->pivot[k] = r.pivot[k];
				a->falldirection[k] = r.falldirection[k];
			}
			a->restorethink = (r.restorethink >= 0 && gg_fndecode) ? gg_fndecode(r.restorethink) : NULL;
			a->listsize = 0;
			for (int k = 0; k < nb; k++)
				gg_attachedappend(a, gg_boltbyid((uint32_t)ids[k]));
		}
		free(ids);
	}
}
