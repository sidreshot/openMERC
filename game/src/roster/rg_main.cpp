// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4: entry points of the roster (spawn dispatch for every m_* classname, level
// start, per-type data, the type set-up for the AI framework, the script and savegame interfaces) and small
// helpers shared by the roster files.
//
// Implements: SPEC-730 §1, SPEC-731 Part A §1-§3 / Part B §1 / §3 (dispatch), SPEC-732 §1, SPEC-750 §12.8 (host
// side of HELI/VEHICLE), SPEC-AMEND-003 §6
#include "rg_local.h"

// ---------------------------------------------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------------------------------------------
float rg_cvar(const char *name, const char *def)
{
	cvar_t *c = gi.cvar(name, def, 0);
	return c ? c->value : 0.0f;
}

void rg_Trace(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	gi.cprintf(NULL, PRINT_HIGH, "[f%d] %s", level.framenum, buf);
}

// toughness level T 0-4 (SPEC-760 B9.2)
int rg_Toughness(void)
{
	int t = game.skill_toughness;
	return t < 0 ? 0 : (t > 4 ? 4 : t);
}

edict_t *rg_Player1(void) { return game.maxclients >= 1 ? &g_edicts[1] : NULL; }

void rg_UseTargets(edict_t *ent, const char *targetname, edict_t *other, edict_t *activator)
{
	if (!targetname || !targetname[0])
		return;
	edict_t *t = NULL;
	while ((t = G_Find(t, FOFS(targetname), targetname)) != NULL)
		if (t->use)
			t->use(t, other, activator);
	(void)ent;
}

// SPEC-782 attacks fired by vehicles: the game's server-side attack definition when G18 has connected it,
// otherwise a plain bullet (Behaviour 3 without the hit filter and effects)
void rg_FireAttack(int attack, edict_t *owner, const vec3_t start, const vec3_t dir)
{
	if (g3hooks.fireAttack) {
		g3hooks.fireAttack(owner, attack, start, dir, NULL);
		return;
	}
	int dmg, flags, mod;
	float range;
	switch (attack) {
	case 12: dmg = 100; flags = 0x20000; mod = MOD_MACHINEGUN; range = 1024; break;
	case 28: dmg = 180; flags = 0x20000; mod = 0; range = 512; break;
	default: return;
	}
	vec3_t end;
	VectorMA(start, 3 * range, dir, end);
	trace_t tr = gi.trace(start, vec3_origin, vec3_origin, end, owner, 0x6000081);
	if (tr.fraction >= 1.0f || !tr.ent || !tr.ent->takedamage)
		return;
	float d = (float)dmg;
	if (tr.fraction > 0.5f)
		d = d / (2 * tr.fraction);
	T_Damage(tr.ent, owner, owner, dir, tr.endpos, tr.plane.normal, (int)d, (int)d, flags | 0x200, mod);
}

// ---------------------------------------------------------------------------------------------------------------
// classnames
// ---------------------------------------------------------------------------------------------------------------
enum { RK_HUMAN, RK_DOG, RK_BULL, RK_HELI, RK_VEHICLE };

static int rg_Kind(const char *cn, const RgHumanType **ht)
{
	*ht = NULL;
	if (!cn || Q_strncasecmp(cn, "m_", 2))
		return -1;
	if ((*ht = rg_FindHumanType(cn)) != NULL)
		return RK_HUMAN;
	if (!Q_stricmp(cn, "m_x_husky") || !Q_stricmp(cn, "m_x_germanshep") || !Q_stricmp(cn, "m_x_raiderdog") ||
	    !Q_stricmp(cn, "m_x_rottweiler"))
		return RK_DOG;
	if (!Q_stricmp(cn, "m_x_bull"))
		return RK_BULL;
	if (!Q_stricmp(cn, "m_x_chopper_black") || !Q_stricmp(cn, "m_x_chopper_green") ||
	    !Q_stricmp(cn, "m_x_chopper_white") || !Q_stricmp(cn, "m_x_hind"))
		return RK_HELI;
	if (!Q_stricmp(cn, "m_x_tank") || !Q_stricmp(cn, "m_x_snowcat"))
		return RK_VEHICLE;
	return -1;
}

bool Roster_IsClassname(const char *classname)
{
	const RgHumanType *ht;
	return rg_Kind(classname, &ht) >= 0;
}

int Roster_TypeCodeForClassname(const char *cn)
{
	const RgHumanType *ht;
	switch (rg_Kind(cn, &ht)) {
	case RK_HUMAN: return ht->type;
	case RK_DOG: return !Q_stricmp(cn, "m_x_rottweiler") ? 12 : 11;
	case RK_BULL: return 6;
	case RK_HELI: return 8;
	case RK_VEHICLE: return !Q_stricmp(cn, "m_x_tank") ? 9 : 10;
	}
	return 0;
}

bool Roster_CallSpawn(edict_t *ent)
{
	const RgHumanType *ht;
	int k = ent ? rg_Kind(ent->classname, &ht) : -1;
	if (k < 0)
		return false;
	rg_RegisterFactories();
	switch (k) {
	case RK_HUMAN: rg_HumanSpawn(ent, ht); break;
	case RK_DOG: rg_SpawnDog(ent); break;
	case RK_BULL: rg_SpawnBull(ent); break;
	case RK_HELI: rg_SpawnHeli(ent); break;
	case RK_VEHICLE: rg_SpawnVehicle(ent); break;
	}
	return true;
}

// the severed piece's owner is the character (or player) it was cut from (SPEC-721 §6, G3's piece set-up)
static void rg_LimbHook(edict_t *piece) { Roster_SetupLimb(piece, piece ? piece->owner : NULL); }

void Roster_LevelStart(void)
{
	rg_RegisterFactories();
	rg_HeliLevelReset();
	g3hooks.limbAI = rg_LimbHook;
}

void rg_RegisterFactories(void)
{
	static bool done;
	if (done)
		return;
	done = true;
	om_ai::RegisterAIFactory(om_ai::TYPE_HELICOPTER, rg_MakeHeliAI);
	om_ai::RegisterAIFactory(om_ai::TYPE_TANK, rg_MakeVehicleAI);
	om_ai::RegisterAIFactory(om_ai::TYPE_SNOWCAT, rg_MakeVehicleAI);
	om_ai::RegisterAIFactory(om_ai::TYPE_BODYPART, rg_MakeLimbAI);
}

// ---------------------------------------------------------------------------------------------------------------
// per-type data and the type set-up for the AI framework
// ---------------------------------------------------------------------------------------------------------------
bool Roster_TypeInfo(int type, RosterTypeInfo *out)
{
	memset(out, 0, sizeof(*out));
	out->type = type;
	out->sizescale = 1.0f;
	const RgHumanType *ht = rg_FindHumanTypeCode(type);
	if (ht) {
		static const int fam[] = {ROSTER_FAMILY_NONE, ROSTER_FAMILY_ECTO, ROSTER_FAMILY_FEMALE, ROSTER_FAMILY_MESO,
		                          ROSTER_FAMILY_ARMOURED};
		out->family = fam[ht->family];
		out->startset = ht->startset;
		out->sizescale = ht->sizescale;
		out->dir = ht->family == RG_FAM_ECTO ? "enemy/ecto" : ht->family == RG_FAM_FEMALE ? "enemy/female"
		                                                                                    : "enemy/meso";
		out->set = ht->set;
		return true;
	}
	switch (type) {
	case 6: out->family = ROSTER_FAMILY_BULL; out->startset = ROSTER_SET_F; out->sizescale = 1.5f;
		out->dir = "enemy/bull"; out->set = "bull"; return true;
	case 11: out->family = ROSTER_FAMILY_DOG; out->startset = ROSTER_SET_F; out->dir = "enemy/husky";
		out->set = "husky"; return true;
	case 12: out->family = ROSTER_FAMILY_DOG; out->startset = ROSTER_SET_F; out->dir = "enemy/rottweiler";
		out->set = "rottweiler"; return true;
	case 8: out->family = ROSTER_FAMILY_HELI; out->startset = ROSTER_SET_VEHICLE; out->dir = "enemy/chopper";
		out->set = "cobrass"; return true;
	case 9: out->family = ROSTER_FAMILY_VEHICLE; out->startset = ROSTER_SET_VEHICLE; out->dir = "enemy/tank";
		out->set = "tank"; return true;
	case 10: out->family = ROSTER_FAMILY_VEHICLE; out->startset = ROSTER_SET_VEHICLE; out->dir = "enemy/snowcat";
		out->set = "snowcat"; return true;
	case 13: out->family = ROSTER_FAMILY_LIMB; out->startset = ROSTER_SET_NONE; return true;
	case 14: out->family = ROSTER_FAMILY_CORPSE; out->startset = ROSTER_SET_NONE; return true;
	}
	return false;
}

bool Roster_TypeSetup(edict_t *ent, int type)
{
	if (!ent)
		return false;
	const RgHumanType *ht = rg_FindHumanTypeCode(type);
	if (ht) {
		// a classname of its own wins when the entity has one of the same type (m_x_romulan / m_afr_msniper)
		const RgHumanType *byname = rg_FindHumanType(ent->classname);
		if (byname && byname->type == type)
			ht = byname;
		return rg_HumanTypeSetup(ent, ent->ai, ht);
	}
	if (type == 6 || type == 11 || type == 12)
		return rg_AnimalTypeSetup(ent, ent->ai, type);
	return false;
}

// ---------------------------------------------------------------------------------------------------------------
// scripts and savegames
// ---------------------------------------------------------------------------------------------------------------
bool Roster_HasHeliAI(edict_t *ent) { return rg_IsHeli(ent); }
bool Roster_HasVehicleAI(edict_t *ent) { return rg_IsVehicle(ent); }

int Roster_HeliCommand(edict_t *ent, int command, edict_t *target, const vec3_t vec, float scalar)
{
	return rg_HeliCommand(ent, command, target, vec, scalar);
}

int Roster_VehicleCommand(edict_t *ent, int command, edict_t *target, const vec3_t vec, float scalar)
{
	return rg_VehicleCommand(ent, command, target, vec, scalar);
}

bool Roster_VehicleActionDone(edict_t *ent, int id)
{
	if (rg_IsHeli(ent))
		return rg_HeliActionDone(ent, id);
	if (rg_IsVehicle(ent))
		return rg_VehicleActionDone(ent, id);
	return true; // the entity has lost its AI (SPEC-AMEND-003 §6)
}

void Roster_WriteVehicleChunks(edict_t *ent, int part, roster_chunk_write_t w, void *ctx)
{
	if (rg_IsHeli(ent))
		rg_HeliWrite(ent, part, w, ctx);
	else if (rg_IsVehicle(ent) && part == 0)
		rg_VehicleWrite(ent, w, ctx);
}

bool Roster_ReadVehicleChunks(edict_t *ent, int part, roster_chunk_read_t r, void *ctx)
{
	if (rg_IsHeli(ent))
		return rg_HeliRead(ent, part, r, ctx);
	if (rg_IsVehicle(ent))
		return part != 0 || rg_VehicleRead(ent, r, ctx);
	return true;
}
