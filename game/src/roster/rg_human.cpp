// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: spawn sequence and type set-up of the human roster (SPEC-730 §1-§5): common
// character set-up and AI object (G2), level model (§3), skin registration with the two skin cvars (§4), model
// object and instance (G9 glue), the body and its set-up requests in SPEC-722 order (G3), bounding-box presets
// (§2), size scale, combat profile and the start-up decision set (§1 step 3, table 3).
//
// Implements: SPEC-730 §1-§5, §10; SPEC-722 "Character set-up" (order of the requests)
#include "rg_local.h"

using namespace ghoul;

// ---------------------------------------------------------------------------------------------------------------
// per-entity record (edict_t::roster)
// ---------------------------------------------------------------------------------------------------------------
RgChar *RG(edict_t *ent)
{
	if (!ent || !ent->inuse || !ent->roster || ent->roster->ent != ent)
		return NULL;
	return ent->roster;
}

RgChar *rg_NewChar(edict_t *ent, int type, int family)
{
	RgChar *c = (RgChar *)gi.TagMalloc(sizeof(RgChar), TAG_LEVEL);
	memset(c, 0, sizeof(*c));
	c->ent = ent;
	c->type = type;
	c->family = family;
	c->sizescale = 1.0f;
	c->startset = RG_SET_NONE;
	ent->roster = c;
	return c;
}

// ---------------------------------------------------------------------------------------------------------------
// SPEC-730 §3 level models (level numbers: SPEC-700 §20, om_ai::LevelNumber); NULL = no level model
// ---------------------------------------------------------------------------------------------------------------
static const char *rg_ecto_levels[33] = {
	NULL, "ecto_tut1", "ecto_tsr1", NULL, NULL, "ecto_kos1", NULL, NULL, NULL, "ecto_sib2", NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, "ecto_sud", "ecto_sud", "ecto_sud", NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "ecto_arm", "ecto_arm", "ecto_arm"};
static const char *rg_female_levels[33] = {
	NULL, NULL, "fem_pistol", NULL, NULL, NULL, NULL, NULL, "fem_pistol", "fem_pistol", "fem_pisnpc",
	"fem_npc", "fem_npc", "fem_npc", "fem_pistol", NULL, "fem_pisnpc", NULL, NULL, NULL, "fem_pisnpc", "fem_pisnpc",
	NULL, "fem_npc", "fem_npc", "fem_npc", "fem_shot", NULL, "fem_shot", "fem_shot", "fem_arm", "fem_arm",
	"fem_arm"};
static const char *rg_meso_levels[33] = {
	"meso", "meso_tut1", "meso_nyc", "meso_tsr2", "meso_train", "meso_kos1", "meso_kos2", "meso_kos3",
	"meso_sib1", "meso_sib2", "meso_sib3", "meso_irq1a", "meso_irq2a", "meso_irq3a", "meso_nyc1", "meso_nyc2",
	"meso_nyc3", "meso_sud1", "meso_sud2", "meso_sud3", "meso_tok1", "meso_tok2", "meso_tok3", "meso_irq1b",
	"meso_irq2b", "meso_irq3b", "meso_cas1", "meso_cas2", "meso_cas3", "meso_cas4", "meso_arm", "meso_arm",
	"meso_arm"};

int rg_LevelNumber(void) { return om_ai::LevelNumber(); }

const char *rg_LevelModel(int family, int lvl)
{
	if (lvl < 0 || lvl > 32)
		lvl = 0;
	const char *m;
	switch (family) {
	case RG_FAM_ECTO:
		m = rg_ecto_levels[lvl];
		if (!m) {
			gi.dprintf("WARNING: no level .ghb for ecto--using ecto.ghb!\n");
			m = "ecto";
		}
		return m;
	case RG_FAM_FEMALE:
		m = rg_female_levels[lvl];
		if (!m) {
			gi.dprintf("WARNING: no level .ghb for female--using female.ghb!\n");
			m = "female";
		}
		return m;
	default:
		return rg_meso_levels[lvl];
	}
}

static const char *rg_FamilyDir(int family)
{
	switch (family) {
	case RG_FAM_ECTO: return "enemy/ecto";
	case RG_FAM_FEMALE: return "enemy/female";
	default: return "enemy/meso";
	}
}

static int rg_BodyFamily(int family)
{
	switch (family) {
	case RG_FAM_ECTO: return BODY_ECTO;
	case RG_FAM_FEMALE: return BODY_FEMALE;
	case RG_FAM_MESO_ARMOURED: return BODY_MESO_ARMOURED;
	default: return BODY_MESO;
	}
}

// ---------------------------------------------------------------------------------------------------------------
// SPEC-730 §2 presets
// ---------------------------------------------------------------------------------------------------------------
static const float rg_human_pmins[9][3] = {{-8, -8, -32}, {-8, -8, -16}, {-32, -32, -8}, {-8, -8, -8},
                                           {-32, -32, -8}, {-41, -41, -38}, {-41, -41, -32}, {-41, -41, -32},
                                           {0, 0, 0}};
static const float rg_human_pmaxs[9][3] = {{8, 8, 41}, {8, 8, 28}, {32, 32, 16}, {8, 8, 0}, {32, 32, 8},
                                           {41, 41, -24}, {41, 41, -24}, {41, 41, -24}, {0, 0, 0}};

// ---------------------------------------------------------------------------------------------------------------
// SPEC-730 §4 skin registration
// ---------------------------------------------------------------------------------------------------------------
static bool rg_VarietyOk(int lvl)
{
	if (lvl == 3)
		return true;
	return rg_cvar("mskins_variety_limit") - 0.1f < (float)lvl;
}

static float rg_ExprTest(void) { return rg_cvar("mskins_expression_limit") - 0.1f; }

void rg_RegisterGore(RgModel *m, bool gzarmor)
{
	static const char *gz[] = {"gz_sing1", "gz_sing2", "gz_sing3", "gz_sing_k", "gz_multi1", "gz_multi2",
	                           "gz_multi3", "gz_multi_k", "gz_mass1", "gz_mass2", "gz_mass3", "gz_mass_k"};
	for (const char *s : gz)
		GG_RegisterSkin(m, "gz", s);
	GG_RegisterSkin(m, "c", "c_blood");
	GG_RegisterSkin(m, "c", "c_black");
	if (gzarmor) // after the c skins (registration order of the original's model objects, kos1 snipers)
		GG_RegisterSkin(m, "gz", "gz_armor");
}

static void rg_RegisterFace(RgModel *m, const char *prefix)
{
	float e = rg_ExprTest();
	GG_RegisterSkin(m, "f", va("%sn", prefix));
	GG_RegisterSkin(m, "f", va("%sd", prefix));
	if (e < 2)
		GG_RegisterSkin(m, "f", va("%sp", prefix));
	if (e < 1) {
		GG_RegisterSkin(m, "f", va("%sm", prefix));
		GG_RegisterSkin(m, "f", va("%sf", prefix));
	}
	if (e < 0)
		GG_RegisterSkin(m, "f", va("%sb", prefix));
}

static void rg_RegisterTalk(RgModel *m, const char *face)
{
	float e = rg_ExprTest();
	if (e < 2)
		GG_RegisterSkin(m, "f", va("%st1", face));
	if (e < 1)
		GG_RegisterSkin(m, "f", va("%st2", face));
	if (e < 0)
		GG_RegisterSkin(m, "f", va("%st3", face));
}

void rg_ApplyRegistration(RgModel *m, const RgReg *reg)
{
	for (; reg && reg->kind != RGR_END; reg++) {
		if (!rg_VarietyOk(reg->level))
			continue;
		switch (reg->kind) {
		case RGR_SKIN: GG_RegisterSkin(m, reg->mat, reg->name); break;
		case RGR_FACE: rg_RegisterFace(m, reg->name); break;
		case RGR_TALK: rg_RegisterTalk(m, reg->name); break;
		}
	}
}

// ---------------------------------------------------------------------------------------------------------------
// type tables
// ---------------------------------------------------------------------------------------------------------------
const RgHumanType *rg_FindHumanType(const char *classname)
{
	if (!classname || !classname[0])
		return NULL;
	const RgHumanType *tables[] = {rg_ecto_types, rg_female_types, rg_meso_types};
	for (const RgHumanType *t : tables)
		for (; t->classname; t++)
			if (!Q_stricmp(t->classname, classname))
				return t;
	return NULL;
}

const RgHumanType *rg_FindHumanTypeCode(int type)
{
	const RgHumanType *tables[] = {rg_ecto_types, rg_female_types, rg_meso_types};
	for (const RgHumanType *t : tables)
		for (; t->classname; t++)
			if (t->type == type)
				return t;
	return NULL;
}

// ---------------------------------------------------------------------------------------------------------------
// RgSetup: the per-type set-up requests (SPEC-730 §5 notation) on the G3 body
// ---------------------------------------------------------------------------------------------------------------
static int rg_pendingarmour[2];
static int rg_npendingarmour;

bool RgSetup::unarmed() const { return (ent->spawnflags & RG_SF_UNARMED) != 0; }

bool RgSetup::set(const char *name) const { return !Q_stricmp(ch->set, name); }

void RgSetup::right(int attack, bool honoursUnarmed)
{
	if (honoursUnarmed && unarmed())
		return;
	if (body)
		body->SetRightHand(attack);
}

void RgSetup::left(int attack, bool honoursUnarmed)
{
	if (honoursUnarmed && unarmed())
		return;
	if (body)
		body->SetLeftHand(attack);
}

// the usual bolts of the accessory models when the roster entry names none
static void rg_DefaultBolts(const char *model, const char **bolt, const char **tobolt)
{
	static const struct {
		const char *prefix, *bolt, *tobolt;
	} tab[] = {
		{"acc_backpack", "abolt_back", "to_abolt_back"}, {"acc_tanks", "abolt_back", "to_abolt_back"},
		{"acc_antenna", "abolt_back", "to_abolt_back"},  {"acc_sword", "abolt_back", "to_abolt_back"},
		{"acc_holster", "abolt_hip_r", "to_abolt_hip_r"}, {"acc_box_holster", "abolt_hip_r", "to_abolt_hip_r"},
		{"acc_box_hip", "abolt_hip_l", "to_abolt_hip_l"}, {"acc_pouch_thigh", "abolt_thigh_l", "to_abolt_thigh_l"},
		{"acc_puch_thigh", "abolt_thigh_l", "to_abolt_thigh_l"}, {"acc_machette", "abolt_thigh_r", "to_abolt_thigh_r"},
		{"w_briefcase", "wbolt_hand_l", "to_wbolt_hand_l"},
	};
	const char *b = NULL, *t = NULL;
	for (size_t i = 0; i < sizeof(tab) / sizeof(tab[0]); i++)
		if (!Q_strncasecmp(model, tab[i].prefix, (int)strlen(tab[i].prefix))) {
			b = tab[i].bolt;
			t = tab[i].tobolt;
			break;
		}
	if (!b) {
		b = "abolt_head_t";
		t = "to_abolt_head_t";
	}
	if (!*bolt && *tobolt) {
		*bolt = b;
		return;
	}
	if (*bolt && !*tobolt) {
		static char buf[8][64];
		static int k;
		char *s = buf[k++ & 7];
		Com_sprintf(s, 64, "to_%s", *bolt);
		*tobolt = s;
		return;
	}
	if (!*bolt) {
		*bolt = b;
		*tobolt = t;
	}
}

static float rg_AccScale(RgChar *ch, const char *model, float scale)
{
	if (scale > 0)
		return scale;
	if (ch->family == RG_FAM_ECTO || ch->family == RG_FAM_FEMALE) {
		bool one = !Q_strncasecmp(model, "acc_ecto_glasses", 16) || !Q_stricmp(model, "acc_glasses") ||
		           !Q_stricmp(model, "w_briefcase");
		return one ? 1.0f : 1.125f;
	}
	return 8.0f / 7.0f; // 1.142857: the original's value bit for bit (irq2b capture), the spec rounds it
}

bool RgSetup::acc(const char *model, const char *skinname, const char *bolt, const char *tobolt, float scale)
{
	rg_DefaultBolts(model, &bolt, &tobolt);
	scale = rg_AccScale(ch, model, scale);
	return body && body->AddBoltOn("Enemy/Bolt", model, skinname, bolt, tobolt, scale) != NULL;
}

bool RgSetup::racc(float p, const char *model, const char *skinname, const char *bolt, const char *tobolt,
                   float scale)
{
	rg_DefaultBolts(model, &bolt, &tobolt);
	scale = rg_AccScale(ch, model, scale);
	return body && body->AddRandomBoltOn(p, "Enemy/Bolt", model, skinname, bolt, tobolt, scale) != NULL;
}

bool RgSetup::skin(const char *mat, const char *s)
{
	if (!rg_HasSkin(inst, mat, s))
		return false;
	return body ? body->SetSkin(mat, s) : rg_SetSkin(inst, mat, s);
}

bool RgSetup::has(const char *mat, const char *s) { return rg_HasSkin(inst, mat, s); }

bool RgSetup::head(const char *h, const char *f) { return headmat("h", h, f); }

bool RgSetup::headmat(const char *mat, const char *h, const char *f)
{
	if (!skin(mat, h))
		return false;
	if (f)
		face(f);
	return true;
}

void RgSetup::off(const char *part)
{
	if (body)
		body->ShowPart(part, false);
}
void RgSetup::on(const char *part)
{
	if (body)
		body->ShowPart(part, true);
}
void RgSetup::face(const char *prefix)
{
	if (body)
		body->SetFace(prefix);
}
void RgSetup::voice(const char *name, int count, int deathtype)
{
	if (body)
		body->SetVoice(name, count, deathtype, NULL);
}
// armour is applied after the precache request (SPEC-722 order: ..., voice, precache, armour)
void RgSetup::armour(int type)
{
	if (rg_npendingarmour < 2)
		rg_pendingarmour[rg_npendingarmour++] = type;
}
void RgSetup::effect(const char *name) { gi.effectindex(name); }
void RgSetup::sound(const char *name) { gi.soundindex(name); }
bool RgSetup::half() { return gix.irand(0, 1) == 0; }
bool RgSetup::odd() { return (rand() & 1) != 0; }
int RgSetup::pick(int n) { return n <= 1 ? 0 : gix.irand(0, n - 1); }
bool RgSetup::chance(float p) { return gix.flrand(0.0f, 1.0f) < p; }

// ---------------------------------------------------------------------------------------------------------------
// SPEC-730 §1: type set-up (inside SPEC-700 rule 2.2)
// ---------------------------------------------------------------------------------------------------------------
bool rg_HumanTypeSetup(edict_t *ent, om_ai::AI *ai, const RgHumanType *ht, const char *forcedset)
{
	RgChar *ch = RG(ent);
	if (!ch)
		ch = rg_NewChar(ent, ht->type, ht->family);
	ch->ht = ht;
	ch->type = ht->type;
	ch->family = ht->family;
	ch->sizescale = ht->sizescale > 0 ? ht->sizescale : 1.0f;
	if (forcedset && forcedset[0])
		strncpy(ch->set, forcedset, sizeof(ch->set) - 1);
	else if (!ch->set[0])
		strncpy(ch->set, (ht->set2 && (rand() & 1)) ? ht->set2 : ht->set, sizeof(ch->set) - 1);
	const char *dir = rg_FamilyDir(ht->family);
	// the body of the family
	G3Body *body = Body_Create(ent, rg_BodyFamily(ht->family));
	if (body && ai) {
		body->owner = ai;
		ai->body = body;
	}
	// 1. bounding-box presets and size scale
	if (ai) {
		for (int i = 0; i < 9; i++)
			ai->SetBBoxPreset(i, rg_human_pmins[i], rg_human_pmaxs[i]);
		if (ht->type == 32 || ht->type == 34) { // m_x_mcharacter_snow, m_x_mmerc
			vec3_t mn = {-16, -16, -32}, mx = {16, 16, 41};
			ai->SetBBoxPreset(0, mn, mx);
		}
		ai->scale = ch->sizescale;
	}
	// 2./3. registration list on the model object, then an instance of it
	int fam = ht->family == RG_FAM_MESO_ARMOURED ? RG_FAM_MESO : ht->family;
	strncpy(ch->levelmodel, rg_LevelModel(fam, rg_LevelNumber()), sizeof(ch->levelmodel) - 1);
	RgModel *m = rg_FindModel(dir, ch->set, false, NULL, ch->levelmodel);
	if (!m || !GG_Object(m)) {
		gi.error("Couldn't find enemy model %s/%s", dir, ch->set);
		return false;
	}
	rg_RegisterGore(m, ht->gzarmor);
	rg_ApplyRegistration(m, ht->reg);
	GG_Prepare(m);
	if (ent->ghoulInst)
		GG_RemoveEntity(ent);
	IGhoulInst *inst = rg_NewInstance(m, ent);
	if (!inst) {
		gi.error("Couldn't find enemy model %s/%s", dir, ch->set);
		return false;
	}
	rg_SetTransform(inst, -90.0f, ch->sizescale);
	ent->s.renderfx |= RF_GHOUL;
	if (ai)
		ai->ghoul_object = GG_Object(m);
	// 4. body requests: weapons, accessories, skins and parts, face, voice (in the set-up function), precache, armour
	RgSetup s;
	s.ent = ent;
	s.ch = ch;
	s.ai = ai;
	s.body = body;
	s.inst = inst;
	rg_npendingarmour = 0;
	if (ht->setup)
		ht->setup(s);
	if (body) {
		body->Precache();
		for (int i = 0; i < rg_npendingarmour; i++)
			body->SetArmour(rg_pendingarmour[i]);
	}
	rg_npendingarmour = 0;
	if (ai) {
		om_ai::InitProfile(ai);
		if (ht->type == 41)
			ai->step_height = 32;
	}
	return true;
}

// SPEC-730 §1 step 3: the classname's start-up set (SPEC-710 decision sets) on the first think
void rg_StartupThink(edict_t *ent)
{
	RgChar *ch = RG(ent);
	om_ai::StartCharacter(ent);
	if (ch && ent->ai)
		om_ai::AddDecisionSet(ent->ai, ch->startset);
}

void rg_HumanSpawn(edict_t *ent, const RgHumanType *ht)
{
	bool doa = (ht->type == 60 || ht->type == 72) && (ent->spawnflags & RG_SF_DOA);
	om_ai::CharacterSetup(ent, doa);
	ent->s.renderfx |= RF_GHOUL;
	if (!doa)
		ent->s.renderfx |= 0x1000; // casts a shadow (SPEC-411 bit 12); dead-on-arrival characters lack it
	RgChar *ch = rg_NewChar(ent, ht->type, ht->family);
	ch->startset = doa ? RG_SET_H : ht->startset;
	om_ai::AI *ai = om_ai::CreateAI(ent, ht->type);
	if (!rg_HumanTypeSetup(ent, ai, ht))
		return;
	if (ai)
		ai->first_think = doa ? rg_DeadOnArrivalThink : rg_StartupThink;
	gi.linkentity(ent);
}
