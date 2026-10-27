// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G3 - savegames of the bodies: the body table and the per-body chunks of the AI
// block (SPEC-700 "Savegame chunks of the AI", kind "bodies"), with the chunk ids and sizes of SPEC-722
// "Savegames" and the gore chunk of SPEC-721 "Savegame data".
//
// The image layout inside the chunk is OpenMerc's own: the original's is a raw copy of its private body object and
// is not portable (SPEC-722: reading original savegames is out of scope). The chunk keeps the original's id and size
// (zero padded), so the container sequence of a save is the same. Image = "OMB1", family, the common body state,
// the gore state of organic bodies, then the family's fields (SaveFields). Pointers are stored as table positions
// (animation records) or entity numbers and looked up again; Ghoul instance pointers are found again on the
// restored instance tree (guts, stuck knives, grenade). The Ghoul instance with its bolt-ons, parts, skins and note
// callbacks is restored by the glue (G9) before the bodies read their chunks.
//
// Implements: SPEC-700 (savegame chunks, kind "bodies"), SPEC-722 (Savegames), SPEC-721 (Savegame data: ZGIA)
#include "g3_local.h"

// ---------------------------------------------------------------------------------------------------------------
// buffer
// ---------------------------------------------------------------------------------------------------------------
void G3SaveBuf::Raw(void *p, int n)
{
	if (pos + n > size) {
		overflow = true;
		if (reading)
			memset(p, 0, n);
		return;
	}
	if (reading)
		memcpy(p, data + pos, n);
	else
		memcpy(data + pos, p, n);
	pos += n;
}

void G3SaveBuf::I(int &v, int bytes)
{
	if (bytes == 1) {
		signed char c = (signed char)v;
		Raw(&c, 1);
		v = c;
	} else if (bytes == 2) {
		short s = (short)v;
		Raw(&s, 2);
		v = s;
	} else {
		int32_t t = v;
		Raw(&t, 4);
		v = t;
	}
}

void G3SaveBuf::F(float &v) { Raw(&v, 4); }

void G3SaveBuf::B(bool &v)
{
	unsigned char c = v ? 1 : 0;
	Raw(&c, 1);
	v = c != 0;
}

void G3SaveBuf::V(vec3_t v)
{
	for (int i = 0; i < 3; i++)
		F(v[i]);
}

void G3SaveBuf::Str(char *s, int cap)
{
	unsigned char n = 0;
	if (!reading) {
		size_t l = strlen(s);
		n = (unsigned char)(l < (size_t)cap - 1 ? l : (size_t)cap - 1);
	}
	Raw(&n, 1);
	if (reading && n > cap - 1)
		n = (unsigned char)(cap - 1);
	Raw(s, n);
	if (reading)
		s[n] = 0;
}

// animation records: table (0 none, 1 human, 2 dog, 3 cow) and position
static const struct {
	const G3Anim *table;
	int count;
} anims[3] = {{g3_humanAnims, g3_numHumanAnims}, {g3_dogAnims, g3_numDogAnims}, {g3_cowAnims, g3_numCowAnims}};

void G3SaveBuf::Anim(const G3Anim *&a)
{
	int t = 0, i = 0;
	if (!reading && a)
		for (int k = 0; k < 3; k++)
			if (a >= anims[k].table && a < anims[k].table + anims[k].count) {
				t = k + 1;
				i = (int)(a - anims[k].table);
			}
	I(t, 1);
	I(i, 2);
	if (reading)
		a = (t >= 1 && t <= 3 && i >= 0 && i < anims[t - 1].count) ? &anims[t - 1].table[i] : NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// chunk ids and sizes (SPEC-722 "Savegames", SPEC-700 table "bodies")
// ---------------------------------------------------------------------------------------------------------------
static uint32_t FourCC(const char *s) // stored like the game's other ids: first character in the high byte
{
	return (uint32_t)(unsigned char)s[0] << 24 | (uint32_t)(unsigned char)s[1] << 16 |
	       (uint32_t)(unsigned char)s[2] << 8 | (uint32_t)(unsigned char)s[3];
}

uint32_t Body_SaveChunkId(int family, int *size)
{
	const char *id = NULL;
	int sz = 0;
	switch (family) {
	case BODY_MESO: id = "AIBM"; sz = 492; break;
	case BODY_ECTO: id = "AIBE"; sz = 492; break;
	case BODY_FEMALE: id = "AIBF"; sz = 492; break;
	case BODY_MESO_ARMOURED: id = "AIBK"; sz = 500; break;
	case BODY_HUSKY: id = "AIBH"; sz = 144; break;
	case BODY_ROTTWEILER: id = "AIBR"; sz = 144; break;
	case BODY_COW: id = "AIBC"; sz = 148; break;
	case BODY_HELI: id = "HELI"; sz = 440; break;
	case BODY_TANK: id = "TANK"; sz = 172; break;
	case BODY_SNOWCAT: id = "SCAT"; sz = 192; break;
	}
	if (size)
		*size = sz;
	return id ? FourCC(id) : 0;
}

// ---------------------------------------------------------------------------------------------------------------
// the common part of the image (also writes/reads the gore state of organic bodies)
// ---------------------------------------------------------------------------------------------------------------
static void CommonFields(G3Body *b, G3SaveBuf &s)
{
	s.Anim(b->cur);
	s.B(b->finished);
	s.I(b->pauseKind, 1);
	s.I(b->expr, 1);
	s.F(b->exprExpire);
	s.B(b->exprLock);
	s.B(b->pendingRestart);
	s.F(b->playStart);
	s.F(b->deathTime);
	s.F(b->firstDeathTime);
	s.V(b->deathPoint);
	s.F(b->lastHitTime);
	s.I(b->lastHitFlags, 4);
	s.B(b->readyToRemove);
	s.B(b->gibbed);
	s.B(b->muted);
	s.B(b->voiceEnabled);
	s.F(b->frozenAt);
	s.F(b->gutsStart);
	s.F(b->fadeEnd);
	bool hasGore = b->gore != NULL;
	s.B(hasGore);
	if (hasGore && b->gore) {
		G3Gore *g = b->gore;
		s.I(g->armourType, 1);
		s.B(g->gutsOut);
		s.I(g->killZone, 1);
		s.I(g->lastZone, 1);
		s.B(g->voiceDisabled);
		s.V(g->killBoltPos);
	}
}

// instance pointers kept by the body, found again on the restored instance tree
static void RelinkInstances(G3Body *b)
{
	IGhoulInst *inst = b->Inst();
	b->gutsInst = NULL;
	b->numKnives = 0;
	if (!inst)
		return;
	GhoulID hr = G3G_Part(inst, "wbolt_hand_r"), hl = G3G_Part(inst, "wbolt_hand_l");
	int n = G3G_NumChildren(inst);
	for (int i = 0; i < n; i++) {
		GhoulID bolt;
		IGhoulInst *c = G3G_Child(inst, i, &bolt);
		const char *name = c ? G3G_ModelName(c->GetGhoulObject()) : "";
		if (!Q_stricmp(name, "guts_bolton"))
			b->gutsInst = c;
		else if (!Q_stricmp(name, "w_knife") && bolt != hr && bolt != hl && b->numKnives < 8)
			b->knives[b->numKnives++] = c; // knives stuck in the body (rule 10); a held knife hangs on a hand bolt
	}
}

// ---------------------------------------------------------------------------------------------------------------
// ids and tables
// ---------------------------------------------------------------------------------------------------------------
static uint32_t maxId;
enum { SAVE_MAX = 4096 }; // more than any level has entities

static int CollectBodies(G3Body **out, int max)
{
	int n = 0;
	if (!g_edicts)
		return 0;
	for (int i = 0; i < globals.num_edicts && n < max; i++) {
		edict_t *e = &g_edicts[i];
		if (e->inuse && e->g3body && Body_SaveChunkId(e->g3body->family, NULL))
			out[n++] = e->g3body;
	}
	// ascending id (insertion sort; a few hundred bodies at most)
	for (int i = 1; i < n; i++)
		for (int j = i; j > 0 && out[j - 1]->saveId > out[j]->saveId; j--) {
			G3Body *t = out[j];
			out[j] = out[j - 1];
			out[j - 1] = t;
		}
	return n;
}

void Body_AssignIds(uint32_t *counter)
{
	static G3Body *list[SAVE_MAX];
	int n = CollectBodies(list, SAVE_MAX);
	for (int i = 0; i < n; i++)
		if (!list[i]->saveId && counter) {
			list[i]->saveId = (*counter)++;
			if (list[i]->saveId > maxId)
				maxId = list[i]->saveId;
		}
}

uint32_t Body_MaxId(void) { return maxId; }

G3Body *Body_ById(uint32_t id)
{
	if (!id || !g_edicts)
		return NULL;
	for (int i = 0; i < globals.num_edicts; i++)
		if (g_edicts[i].inuse && g_edicts[i].g3body && g_edicts[i].g3body->saveId == id)
			return g_edicts[i].g3body;
	return NULL;
}

static void WChunk(uint32_t id, const void *data, int len)
{
	if (gix.WriteChunk)
		gix.WriteChunk((int)id, data, len);
}

static bool RChunk(uint32_t id, void *data, int len)
{
	if (!gix.ReadChunk)
		return false;
	return gix.ReadChunk((int)id, data, len, NULL) != 0;
}

#pragma pack(push, 1)
struct BodyAICC {
	int32_t id;
	int32_t cls;
};
#pragma pack(pop)

void Body_WriteTable(void)
{
	static G3Body *list[SAVE_MAX];
	int n = CollectBodies(list, SAVE_MAX);
	int32_t count = 0;
	for (int i = 0; i < n; i++)
		if (list[i]->saveId)
			count++;
	WChunk(FourCC("AICT"), &count, 4);
	for (int i = 0; i < n; i++)
		if (list[i]->saveId) {
			BodyAICC c = {(int32_t)list[i]->saveId, list[i]->family};
			WChunk(FourCC("AICC"), &c, sizeof c);
		}
}

static BodyAICC pending[SAVE_MAX];
static int numPending;

void Body_ReadTable(void)
{
	int32_t count = 0;
	numPending = 0;
	RChunk(FourCC("AICT"), &count, 4);
	for (int i = 0; i < count; i++) {
		BodyAICC c = {0, 0};
		RChunk(FourCC("AICC"), &c, sizeof c);
		if (numPending < SAVE_MAX)
			pending[numPending++] = c;
		if ((uint32_t)c.id > maxId)
			maxId = (uint32_t)c.id;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// per-body data
// ---------------------------------------------------------------------------------------------------------------
static const uint32_t IMAGE_MAGIC = 0x31424d4f; // "OMB1"

static void WriteBody(G3Body *b)
{
	int size = 0;
	uint32_t id = Body_SaveChunkId(b->family, &size);
	G3SaveBuf s(false, size);
	uint32_t magic = IMAGE_MAGIC;
	s.U(magic);
	int fam = b->family, num = b->ent ? G_EdictNum(b->ent) : -1;
	s.I(fam, 1);
	s.I(num, 2);
	CommonFields(b, s);
	b->SaveFields(s);
	if (s.overflow)
		G3_Printf("G3: body image of family %d does not fit in %d bytes\n", b->family, size);
	WChunk(id, s.data, size);
	if (b->gore)
		b->gore->SaveWrite(); // AIGZ: int32 accumulated damage per zone (SPEC-721)
}

void Body_WriteData(void)
{
	static G3Body *list[SAVE_MAX];
	int n = CollectBodies(list, SAVE_MAX);
	for (int i = 0; i < n; i++)
		if (list[i]->saveId)
			WriteBody(list[i]);
}

static void ReadBody(const BodyAICC &c)
{
	int size = 0;
	uint32_t id = Body_SaveChunkId(c.cls, &size);
	if (!id) {
		G3_Printf("ERROR: invalid body class code: %d\n", c.cls);
		return;
	}
	G3SaveBuf s(true, size);
	if (!RChunk(id, s.data, size))
		return;
	uint32_t magic = 0;
	s.U(magic);
	int fam = 0, num = -1;
	s.I(fam, 1);
	s.I(num, 2);
	if (magic != IMAGE_MAGIC || fam != c.cls || num < 0 || num >= globals.num_edicts) {
		G3_Printf("G3: body %d: unusable image (family %d, entity %d)\n", c.id, fam, num);
		return;
	}
	edict_t *ent = &g_edicts[num];
	G3Body *b = Body_Create(ent, fam);
	if (!b)
		return;
	b->saveId = (uint32_t)c.id;
	// zones are rebuilt from the model as it is now (SPEC-721 savegame data); the callbacks come with the instance
	if (b->gore)
		b->gore->Setup();
	CommonFields(b, s);
	b->SaveFields(s);
	RelinkInstances(b);
	if (b->gore) {
		int32_t v[G3_MAX_ZONES];
		memset(v, 0, sizeof v);
		RChunk(FourCC("AIGZ"), v, b->gore->numZones * 4);
		b->gore->SaveRead(v, b->gore->numZones);
	}
}

void Body_ReadData(void)
{
	for (int i = 0; i < numPending; i++)
		ReadBody(pending[i]);
	numPending = 0;
}
