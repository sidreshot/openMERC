// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module, package G4 (character roster, helicopters, vehicles, special characters).
// Internal declarations shared by the files of game/src/roster/. Not a public interface: other packages use
// roster_api.h.
//
// The roster is built on the AI framework of G2 (ai/ai_api.h: CharacterSetup, CreateAI, StartCharacter,
// AddDecisionSet, profiles, OmAI subclasses for the special AIs), the bodies of G3 (damage/damage_api.h:
// Body_Create and the SPEC-722 set-up requests, helicopter and vehicle bodies) and the Ghoul glue of G9
// (ghoulglue/gg_ghoul.h: model records, object instances, bolt-ons).
//
// Implements: SPEC-730, SPEC-731, SPEC-732 (with SPEC-700, SPEC-722, SPEC-791 where the roster needs them)
#pragma once

#include "../g_local.h"
#include "../ai/ai_api.h"
#include "../damage/damage_api.h"
#include "../ghoulglue/gg_ghoul.h"
#include "../effects/fx.h"
#include "roster_api.h"

#include <math.h>
#include <string.h>

typedef GGModel RgModel;

// ---------------------------------------------------------------------------------------------------------------
// Ghoul helpers on top of G9's glue (rg_ghoul.cpp)
// ---------------------------------------------------------------------------------------------------------------
RgModel *rg_FindModel(const char *dir, const char *list, bool allskins, const char *skin = NULL,
                      const char *base = NULL);
ghoul::IGhoulInst *rg_NewInstance(RgModel *m, edict_t *ent);
// bolt-on of model `child` attached by its bolt `cbolt` to bolt `pbolt` of `parent` (the entity's own instance or
// one of its bolt-ons); the child's skin and scale as SPEC-791 §7. NULL (developer message) when it fails.
ghoul::IGhoulInst *rg_AddBolt(edict_t *owner, ghoul::IGhoulInst *parent, const char *pbolt, RgModel *child,
                              const char *cbolt, const char *skin, float scale);
ghoul::IGhoulInst *rg_ChildOnBolt(ghoul::IGhoulInst *parent, const char *pbolt);
bool rg_SetSkin(ghoul::IGhoulInst *inst, const char *material, const char *skin);   // silent
bool rg_HasSkin(ghoul::IGhoulInst *inst, const char *material, const char *skin);
bool rg_SetPart(ghoul::IGhoulInst *inst, const char *part, bool shown);
// play by name; mode = ghoul::kPlayLoop / kPlayOnce / kPlayStatic. Returns the sequence id (0 = none).
ghoul::GhoulID rg_Play(ghoul::IGhoulInst *inst, const char *seq, int mode);
float rg_SeqDuration(ghoul::IGhoulInst *inst, const char *seq, int mode);
bool rg_HasSequence(ghoul::IGhoulInst *inst, const char *seq);
ghoul::GhoulID rg_Bolt(ghoul::IGhoulInst *inst, const char *bolt);
// bolt of an instance (own model or a bolt-on of ent) in world space: position and axes (forward = bolt X)
bool rg_BoltWorld(edict_t *ent, ghoul::IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t fwd = NULL,
                  vec3_t left = NULL, vec3_t up = NULL);
// muzzle bolt in world space: position and firing direction (the bolt's z axis)
bool rg_MuzzleWorld(edict_t *ent, ghoul::IGhoulInst *inst, const char *bolt, vec3_t pos, vec3_t dir);
void rg_EntityToWorld(edict_t *ent, const vec3_t p, vec3_t out, bool point);
void rg_WorldToEntity(edict_t *ent, const vec3_t p, vec3_t out, bool point);
void rg_SetTransform(ghoul::IGhoulInst *inst, float yawdeg, float scale);
void rg_SetPartRotation(ghoul::IGhoulInst *inst, float yaw, float pitch, float scale);

// Effects through G7's emitter (effects/fx.h); temp entities are sent with fx:: directly
void rg_EffectAt(const char *effect, const vec3_t org, const vec3_t dir = NULL, float scale = 0);
void rg_EffectOnBolt(const char *effect, edict_t *owner, ghoul::IGhoulInst *inst, const char *bolt,
                     float scale = 0);
// persistent effect slot (SPEC-202) on a bolt of the entity's own model (bolt NULL = origin)
void rg_SetEffectSlot(edict_t *ent, const char *effect, const char *bolt, float scale);
void rg_ClearEffectSlot(edict_t *ent, const char *effect);
void rg_CameraShake(const vec3_t point, float maxint, float radius);

// ---------------------------------------------------------------------------------------------------------------
// Per-entity roster record (edict_t::roster, the /* G4 */ field)
// ---------------------------------------------------------------------------------------------------------------
enum {
	RG_FAM_NONE = 0,
	RG_FAM_ECTO,
	RG_FAM_FEMALE,
	RG_FAM_MESO,
	RG_FAM_MESO_ARMOURED,
	RG_FAM_DOG,
	RG_FAM_BULL,
	RG_FAM_HELI,
	RG_FAM_VEHICLE,
	RG_FAM_LIMB,
	RG_FAM_CORPSE
};

enum { RG_SET_A, RG_SET_B, RG_SET_C, RG_SET_F, RG_SET_G, RG_SET_H, RG_SET_VEHICLE, RG_SET_NONE };

struct RgHumanType;

struct RgChar {
	edict_t *ent;
	int type;
	int family;
	const RgHumanType *ht;
	int startset;
	char set[32];
	char levelmodel[32];
	float sizescale;
};

RgChar *RG(edict_t *ent);
RgChar *rg_NewChar(edict_t *ent, int type, int family);

// ---------------------------------------------------------------------------------------------------------------
// Human roster (SPEC-730)
// ---------------------------------------------------------------------------------------------------------------
enum { RGR_SKIN = 0, RGR_FACE = 1, RGR_TALK = 2, RGR_END = 3 };
struct RgReg {
	int kind;          // RGR_*
	const char *mat;   // RGR_SKIN: material
	const char *name;  // skin name, face prefix (RGR_FACE) or face skin whose talk variants register (RGR_TALK)
	int level;         // registration level (3 = always)
};

class RgSetup;
struct RgHumanType {
	const char *classname; // "" for the three types without a classname
	int type;
	int family;          // RG_FAM_ECTO / FEMALE / MESO / MESO_ARMOURED
	const char *set;     // sequence-set name
	const char *set2;    // alternative set name chosen 1/2 (odd), or NULL
	int startset;        // RG_SET_*
	float sizescale;     // 1.0 unless stated
	bool gzarmor;        // also registers gz_armor (observed per type, docs/impl/G4.md)
	const RgReg *reg;    // registration list (gore skins are implicit), RGR_END terminated
	void (*setup)(RgSetup &s);
};

extern const RgHumanType rg_ecto_types[];
extern const RgHumanType rg_female_types[];
extern const RgHumanType rg_meso_types[];
const RgHumanType *rg_FindHumanType(const char *classname);
const RgHumanType *rg_FindHumanTypeCode(int type);

// The set-up requests given to the per-type functions (SPEC-730 §5 notation); they go to the G3 body.
class RgSetup {
public:
	edict_t *ent;
	RgChar *ch;
	om_ai::AI *ai;
	G3Body *body;
	ghoul::IGhoulInst *inst;

	bool unarmed() const;                       // spawnflag 131072
	bool set(const char *name) const;           // current sequence-set name equals
	void right(int attack, bool honoursUnarmed = false);
	void left(int attack, bool honoursUnarmed = false);
	// accessories from Enemy/Bolt; bolt/tobolt NULL = the model's usual bolt; scale 0 = family default
	bool acc(const char *model, const char *skin, const char *bolt = NULL, const char *tobolt = NULL,
	         float scale = 0);
	bool racc(float chance, const char *model, const char *skin, const char *bolt = NULL,
	          const char *tobolt = NULL, float scale = 0);
	// skins and parts (succeed only for registered skins)
	bool skin(const char *mat, const char *skin);
	bool has(const char *mat, const char *skin);   // registered on this model object
	bool head(const char *h, const char *face);   // h skin, then the face prefix when it was set
	bool headmat(const char *mat, const char *h, const char *face);
	void off(const char *part);
	void on(const char *part);
	void face(const char *prefix);
	void voice(const char *name, int count, int deathtype);
	void armour(int type);
	void effect(const char *name);
	void sound(const char *name);
	// random draws (SPEC-730 §5): half = equal-chance integer draw, odd = C library parity
	bool half();
	bool odd();
	int pick(int n);            // 0..n-1
	bool chance(float p);       // uniform [0,1) < p
};

void rg_HumanSpawn(edict_t *ent, const RgHumanType *ht);
bool rg_HumanTypeSetup(edict_t *ent, om_ai::AI *ai, const RgHumanType *ht, const char *forcedset = NULL);
const char *rg_LevelModel(int family, int level);
int rg_LevelNumber(void);
void rg_RegisterGore(RgModel *m, bool gzarmor);
void rg_ApplyRegistration(RgModel *m, const RgReg *reg);
void rg_BuildProfile(int type, float *profile);
int rg_TypeTeam(int type);
bool rg_TypeInnocent(int type);
void rg_StartupThink(edict_t *ent);

// ---------------------------------------------------------------------------------------------------------------
// Special characters (SPEC-732), helicopters and vehicles (SPEC-731)
// ---------------------------------------------------------------------------------------------------------------
void rg_SpawnDog(edict_t *ent);
void rg_SpawnBull(edict_t *ent);
bool rg_AnimalTypeSetup(edict_t *ent, om_ai::AI *ai, int type);
void rg_SpawnHeli(edict_t *ent);
void rg_SpawnVehicle(edict_t *ent);
void rg_DeadOnArrivalThink(edict_t *ent);
void rg_HeliLevelReset(void);
void rg_RegisterFactories(void);
om_ai::AI *rg_MakeHeliAI(int code);
om_ai::AI *rg_MakeVehicleAI(int code);
om_ai::AI *rg_MakeLimbAI(int code);

bool rg_IsHeli(edict_t *ent);
int rg_HeliCommand(edict_t *ent, int cmd, edict_t *target, const vec3_t vec, float scalar);
bool rg_HeliActionDone(edict_t *ent, int id);
void rg_HeliWrite(edict_t *ent, int part, roster_chunk_write_t w, void *ctx);
bool rg_HeliRead(edict_t *ent, int part, roster_chunk_read_t r, void *ctx);
bool rg_IsVehicle(edict_t *ent);
int rg_VehicleCommand(edict_t *ent, int cmd, edict_t *target, const vec3_t vec, float scalar);
bool rg_VehicleActionDone(edict_t *ent, int id);
void rg_VehicleWrite(edict_t *ent, roster_chunk_write_t w, void *ctx);
bool rg_VehicleRead(edict_t *ent, roster_chunk_read_t r, void *ctx);

// Aim step shared by helicopters and ground vehicles (SPEC-731 Part B §9.1)
enum { RG_AIM_ALIGNED = 1, RG_AIM_TURNING = 2, RG_AIM_LIMITED = 3 };
struct RgAimPart {
	ghoul::IGhoulInst *inst;     // the rotating part
	ghoul::IGhoulInst *parent;   // the instance it hangs on
	char pbolt[32];              // the parent's bolt
	float yaw, pitch;            // current angles relative to the rest orientation (radians)
	float scale;
};
int rg_AimStep(edict_t *ent, RgAimPart *p, const vec3_t target, bool yawon, bool pitchon, float yawmin,
               float yawmax, float pitchmin, float pitchmax, float rate);
void rg_AimInit(RgAimPart *p, ghoul::IGhoulInst *inst, ghoul::IGhoulInst *parent, const char *pbolt,
                float scale = 1.0f);

// ---------------------------------------------------------------------------------------------------------------
// Small helpers (rg_main.cpp)
// ---------------------------------------------------------------------------------------------------------------
int rg_Toughness(void);                     // T 0-4 (SPEC-760 B9.2)
float rg_cvar(const char *name, const char *def = "0");
// test trace (cvar rg_trace): printed to the server console also with developer 0
void rg_Trace(const char *fmt, ...);
edict_t *rg_Player1(void);
// SPEC-782 attack fired by a vehicle (G3 hook g3hooks.fireAttack when set, else a plain bullet)
void rg_FireAttack(int attack, edict_t *owner, const vec3_t start, const vec3_t dir);
void rg_UseTargets(edict_t *ent, const char *targetname, edict_t *other, edict_t *activator);

#define RG_SF_UNARMED 131072
#define RG_SF_DOA 65536
