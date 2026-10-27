// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: special characters — dogs and the bull (SPEC-732 §2, §3, §5, §6), the
// dead-on-arrival start of m_nyc_mswat / m_kos_msniper1a (§7), the player-corpse character (type 14, §8) and the
// severed-limb character (type 13, §9). The bosses are ordinary roster entries (rg_roster_meso.cpp, §4).
//
// Implements: SPEC-732 §2, §3, §5-§9
#include "rg_local.h"

using namespace ghoul;

// ---------------------------------------------------------------------------------------------------------------
// §3 animal presets
// ---------------------------------------------------------------------------------------------------------------
static const float dog_pmins[9][3] = {{-24, -24, -20}, {-16, -16, -16}, {-32, -32, -8}, {-24, -24, -8},
                                      {-32, -32, -8},  {-32, -32, -26}, {-24, -24, -32}, {-32, -32, -28},
                                      {0, 0, 0}};
static const float dog_pmaxs[9][3] = {{24, 24, 20}, {16, 16, 8}, {32, 32, 8}, {24, 24, 8}, {32, 32, 8},
                                      {32, 32, 8},  {24, 24, -24}, {32, 32, 16}, {0, 0, 0}};
static const float bull_pmins[9][3] = {{-24, -14, -20}, {-16, -16, -16}, {-32, -32, -8}, {-24, -24, -8},
                                       {-32, -32, -8},  {-32, -32, -26}, {-24, -24, -32}, {-22, -26, -20},
                                       {0, 0, 0}};
static const float bull_pmaxs[9][3] = {{36, 14, 20}, {16, 16, 8}, {36, 32, 8}, {24, 24, 8}, {32, 32, 8},
                                       {32, 32, 8},  {24, 24, -24}, {36, 26, 0}, {0, 0, 0}};

// SPEC-732 §5 type set-up of both dog types, §6 of the bull
bool rg_AnimalTypeSetup(edict_t *ent, om_ai::AI *ai, int type)
{
	bool bull = type == 6;
	bool rott = type == 12;
	const char *set = bull ? "bull" : rott ? "rottweiler" : "husky";
	char dir[32];
	Com_sprintf(dir, sizeof(dir), "enemy/%s", set);
	RgChar *ch = RG(ent);
	if (!ch)
		ch = rg_NewChar(ent, type, bull ? RG_FAM_BULL : RG_FAM_DOG);
	strncpy(ch->set, set, sizeof(ch->set) - 1);
	ch->startset = RG_SET_F;
	// 1. presets
	if (ai)
		for (int i = 0; i < 9; i++)
			ai->SetBBoxPreset(i, bull ? bull_pmins[i] : dog_pmins[i], bull ? bull_pmaxs[i] : dog_pmaxs[i]);
	// 2. the set's own model, no level geometry
	RgModel *m = rg_FindModel(dir, set, true);
	if (!m || !GG_Object(m)) {
		gi.error("Couldn't find enemy model %s/%s", dir, set);
		return false;
	}
	if (ent->ghoulInst)
		GG_RemoveEntity(ent);
	IGhoulInst *inst = rg_NewInstance(m, ent);
	// 3. size scale and the instance transform
	float scale = bull ? 1.5f : ((ent->spawnflags & 8) ? 1.0f : gix.flrand(1.0f, 1.15f));
	ch->sizescale = scale;
	if (inst)
		rg_SetTransform(inst, -90.0f, scale);
	ent->s.renderfx |= RF_GHOUL;
	if (ai) {
		ai->scale = scale;
		ai->ghoul_object = GG_Object(m);
	}
	// 4. body (created once), its precache
	G3Body *body = Body_Create(ent, bull ? BODY_COW : rott ? BODY_ROTTWEILER : BODY_HUSKY);
	if (body && ai) {
		body->owner = ai;
		ai->body = body;
	}
	if (body)
		body->Precache();
	// 5. sounds
	if (bull) {
		gi.soundindex("Enemy/Cow/Moo1.wav");
		gi.soundindex("Enemy/Cow/Moo2.wav");
		gi.soundindex("Enemy/Cow/Die.wav");
	} else {
		static const char *snd[] = {"Bark1", "Bark2", "Bark3", "Bite1", "Bite2", "Die", "Pain"};
		for (const char *s : snd)
			gi.soundindex(va("Enemy/Dog/%s.wav", s));
	}
	// 6. combat profile: dogs set theirs explicitly, the bull keeps the object defaults
	if (ai) {
		om_ai::InitProfile(ai);
		if (!bull) {
			static const float dog[15] = {16, 72, 0, 1.0f, 0, 0, 200, 112, 1.0f, 2, 0, 1, 0, 2, 3};
			memcpy(ai->profile, dog, sizeof(dog));
		}
	}
	return true;
}

// right-angle box rotation of point entities (SPEC-761, as for props)
static void rg_RotateBox(edict_t *ent)
{
	int k = ((int)floorf(anglemod(ent->s.angles[YAW]) / 90.0f + 0.5f)) & 3;
	vec3_t mn, mx;
	VectorCopy(ent->mins, mn);
	VectorCopy(ent->maxs, mx);
	switch (k) {
	case 1:
		ent->mins[0] = -mx[1];
		ent->mins[1] = mn[0];
		ent->maxs[0] = -mn[1];
		ent->maxs[1] = mx[0];
		break;
	case 2:
		ent->mins[0] = -mx[0];
		ent->mins[1] = -mx[1];
		ent->maxs[0] = -mn[0];
		ent->maxs[1] = -mn[1];
		break;
	case 3:
		ent->mins[0] = mn[1];
		ent->mins[1] = -mx[0];
		ent->maxs[0] = mx[1];
		ent->maxs[1] = -mn[0];
		break;
	}
}

void rg_SpawnDog(edict_t *ent)
{
	bool rott = !Q_stricmp(ent->classname, "m_x_rottweiler");
	int type = rott ? om_ai::TYPE_ROTTWEILER : om_ai::TYPE_HUSKY;
	om_ai::CharacterSetup(ent, false);
	RgChar *ch = rg_NewChar(ent, type, RG_FAM_DOG);
	ch->startset = RG_SET_F;
	om_ai::AI *ai = om_ai::CreateAI(ent, type);
	if (rott) {
		VectorSet(ent->mins, -26, -26, -20);
		VectorSet(ent->maxs, 26, 26, 11);
	} else {
		VectorSet(ent->mins, -33, -33, -28);
		VectorSet(ent->maxs, 33, 33, 16);
	}
	if (!rg_AnimalTypeSetup(ent, ai, type))
		return;
	if (ai)
		ai->first_think = rg_StartupThink;
	gi.linkentity(ent);
}

void rg_SpawnBull(edict_t *ent)
{
	om_ai::CharacterSetup(ent, false);
	RgChar *ch = rg_NewChar(ent, om_ai::TYPE_BULL, RG_FAM_BULL);
	ch->startset = RG_SET_F;
	om_ai::AI *ai = om_ai::CreateAI(ent, om_ai::TYPE_BULL);
	VectorSet(ent->mins, -30, -14, -33);
	VectorSet(ent->maxs, 56, 14, 26);
	rg_RotateBox(ent);
	if (!rg_AnimalTypeSetup(ent, ai, om_ai::TYPE_BULL))
		return;
	if (ai)
		ai->first_think = rg_StartupThink;
	gi.linkentity(ent);
}

// ---------------------------------------------------------------------------------------------------------------
// §7 dead on arrival: the first think of m_nyc_mswat / m_kos_msniper1a with spawnflag 65536
// ---------------------------------------------------------------------------------------------------------------
void rg_DeadOnArrivalThink(edict_t *ent)
{
	om_ai::AI *ai = ent->ai;
	G3Body *body = Body_Of(ent);
	if (!ai || !body)
		return;
	gitem_t *it = FindItemByClassname("item_equip_armor");
	if (it) {
		edict_t *e = G_Spawn();
		e->classname = (char *)it->classname;
		VectorCopy(ent->s.origin, e->s.origin);
		e->s.origin[2] += ent->maxs[2] + 2;
		VectorCopy(ent->s.angles, e->s.angles);
		e->count = ent->count;
		e->target = ent->targetname;
		SpawnItem(e, it);
		// "its box bottom is lowered by 10": the item comes to rest 10 units higher (kos2 `freearmor`: original
		// at z -314, without this -324)
		e->mins[2] -= 10;
		gi.linkentity(e);
	}
	body->SetArmour(0);
	vec3_t o;
	VectorCopy(ent->s.origin, o);
	om_ai::Action *a = om_ai::MakeAction(ai, 5, NULL, NULL, NULL, o, o, 0, false);
	if (a)
		ai->Interrupt(ent, a);
	ai->first_think_done = 1;
	ent->nextthink = level.time + FRAMETIME;
}

// ---------------------------------------------------------------------------------------------------------------
// §9 severed limb (type 13)
// ---------------------------------------------------------------------------------------------------------------
class RgLimbAI : public OmAI {
public:
	float start = -1;      // S = time of the first think - 0.1 s
	bool frozen = false, settled = false;
	float fadestart = 0;
	void Think(edict_t *ent) override;
};

om_ai::AI *rg_MakeLimbAI(int code)
{
	(void)code;
	return new RgLimbAI();
}

static bool rg_LimbWatched(edict_t *ent)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *c = &g_edicts[i];
		if (!c->inuse || !c->client)
			continue;
		vec3_t d, f;
		VectorSubtract(c->s.origin, ent->s.origin, d);
		if (VectorLength(d) > 800)
			continue;
		VectorNormalize(d);
		AngleVectors(c->client->v_angle, f, NULL, NULL);
		if (DotProduct(f, d) < 0.2f)
			return true;
	}
	return false;
}

void RgLimbAI::Think(edict_t *ent)
{
	float now = level.time;
	ent->nextthink = now + FRAMETIME;
	if (start < 0)
		start = now - FRAMETIME;
	if (fadestart > 0) {
		if (now >= fadestart + 1.1f)
			om_ai::FreeCharacter(ent);
		return;
	}
	if (!frozen && now - start > 1) {
		frozen = true;
		IGhoulInst *inst = ent->ghoulInst;
		int n = inst ? inst->NumChildren() : 0;
		for (int i = 0; i < n; i++) {
			GhoulID b;
			IGhoulInst *c = inst->GetChild(i, &b);
			if (c)
				c->Pause(now);
		}
	}
	if (!settled) {
		if (DotProduct(ent->velocity, ent->velocity) < 10 || now - start > 30) {
			settled = true;
			VectorClear(ent->velocity);
			VectorClear(ent->avelocity);
			ent->nextthink = now + (frozen ? 5.0f : FRAMETIME);
		} else if (ent->s.angles[PITCH] <= -85) {
			ent->s.angles[PITCH] = -90;
			ent->s.angles[ROLL] = 0;
			VectorClear(ent->avelocity);
		}
		return;
	}
	if (now - start >= 30 || !rg_LimbWatched(ent)) {
		fadestart = now;
		fx::FadeOut(ent, 1.0f);
	}
}

static void rg_LimbFirstThink(edict_t *ent) { (void)ent; /* no senses, decisions or reactions */ }

void Roster_SetupLimb(edict_t *piece, edict_t *source)
{
	if (!piece)
		return;
	rg_RegisterFactories();
	om_ai::AI *ai = om_ai::CreateAI(piece, om_ai::TYPE_BODYPART);
	if (!ai)
		return;
	ai->first_think = rg_LimbFirstThink;
	RgChar *ch = rg_NewChar(piece, om_ai::TYPE_BODYPART, RG_FAM_LIMB);
	ch->startset = RG_SET_NONE;
	// the source character's model object (SPEC-730 §1 with its directory, set and level model)
	RgChar *sc = RG(source);
	if (sc && piece->ghoulInst) {
		const char *dir = sc->family == RG_FAM_ECTO ? "enemy/ecto" : sc->family == RG_FAM_FEMALE ? "enemy/female"
		                                                                                          : "enemy/meso";
		RgModel *m = GG_FindModel(dir, sc->set, false, NULL, sc->levelmodel, true);
		IGhoulObject *own = piece->ghoulInst->GetGhoulObject();
		if (m && GG_Object(m) != own) {
			RgModel *pm = GG_ModelOfObject(own);
			gi.dprintf("Warning! Blown-off limb has object%s-%s-%s; parent has %s-%s-%s\n",
			           pm ? GG_ModelDir(pm) : "", pm ? GG_ModelList(pm) : "", "", dir, sc->set, sc->levelmodel);
		}
		strncpy(ch->set, sc->set, sizeof(ch->set) - 1);
		strncpy(ch->levelmodel, sc->levelmodel, sizeof(ch->levelmodel) - 1);
	} else if (source && source->client) {
		strncpy(ch->levelmodel, "meso_player", sizeof(ch->levelmodel) - 1);
	}
	piece->nextthink = level.time + FRAMETIME;
}

// ---------------------------------------------------------------------------------------------------------------
// §8 player corpse (type 14): the corpse AI after G3's Body_CopyToCorpse
// ---------------------------------------------------------------------------------------------------------------
static void rg_CorpseFirstThink(edict_t *ent) { (void)ent; }

void Roster_SetupPlayerCorpse(edict_t *corpse, edict_t *player)
{
	if (!corpse || !player)
		return;
	om_ai::AI *ai = om_ai::CreateAI(corpse, om_ai::TYPE_PLAYERCORPSE);
	if (!ai)
		return;
	ai->first_think = rg_CorpseFirstThink;
	RgChar *ch = rg_NewChar(corpse, om_ai::TYPE_PLAYERCORPSE, RG_FAM_CORPSE);
	ch->startset = RG_SET_NONE;
	G3Body *body = Body_Of(corpse);
	if (!body)
		gi.dprintf("Couldn't find player body for replication!\n");
	else {
		body->owner = ai;
		ai->body = body;
	}
	// all nine presets: the player's box widened by 16 units on each horizontal side
	vec3_t mn, mx;
	VectorCopy(player->mins, mn);
	VectorCopy(player->maxs, mx);
	mn[0] -= 16;
	mn[1] -= 16;
	mx[0] += 16;
	mx[1] += 16;
	for (int i = 0; i < 9; i++)
		ai->SetBBoxPreset(i, mn, mx);
	corpse->s.renderfx = player->s.renderfx;
	if (body && player->g3body && player->g3body->cur) {
		om_ai::Action *a = om_ai::MakeAction(ai, 4, NULL, NULL, player->g3body->cur, corpse->s.origin,
		                                     corpse->s.origin, 0, true);
		if (a)
			ai->QueueBack(a);
		body->SetAnim(player->g3body->cur, false);
		ai->Die(NULL, NULL, 0, corpse->s.origin, 0);
	}
	if (body)
		body->SetExpression(EXPR_DEAD, 999999.9f);
	corpse->owner = NULL;
}
