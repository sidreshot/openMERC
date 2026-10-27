/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700, SPEC-701, SPEC-710, SPEC-711
 * Owner: package G2. Internal declarations of the character AI (not for other packages; use ai_api.h).
 */
#ifndef OM_AI_LOCAL_H
#define OM_AI_LOCAL_H

#include "ai_api.h"

#include <math.h>
#include <string.h>

namespace om_ai {

/* ---- cvars (SPEC-700 "Cvars", SPEC-701, SPEC-740) ---- */
struct AICvars {
	cvar_t *ai_freeze, *ai_dumb, *ai_goretest, *ai_pathtest, *ai_highpriority, *ai_nonodes;
	cvar_t *ai_maxcorpses, *ai_corpselife, *ai_editdir, *aidec_show, *aipoints_show, *sv_gravity;
	cvar_t *ai_trace;
	cvar_t *ai_savetest;
};
extern AICvars cv;
void RegisterCvars(void);
static inline float CvarValue(cvar_t *c) { return c ? c->value : 0.0f; }

/* ---- level-wide manager state (SPEC-700 "Character manager", not saved) ---- */
enum { MAX_ACTIVE = 64, MAX_TRIGGER_SOUNDS = 16 };
struct Manager {
	edict_t *sight_client = nullptr;
	edict_t *sight_character = nullptr;
	edict_t *active[MAX_ACTIVE];
	int num_active = 0;
	int awake_in_pvs = 0;
	float priority_time = 0;
	edict_t *nearest = nullptr;
	float wake_voice_time = -100;
	float tactical_voice_time = -100;
	int tactical_category = 0;
	edict_t *trigger_sounds[MAX_TRIGGER_SOUNDS];
	int num_trigger_sounds = 0;
	int level_number = 0;
	std::vector<AI *> all;              /* every AI object made this level (lifetime bookkeeping) */
};
extern Manager mgr;

/* ---- helpers (ai_util.cpp) ---- */
float Now(void);
float Frand(void);                          /* [0, 1) */
float Crand(void);                          /* [-1, 1) */
float FlRand(float lo, float hi);           /* engine flrand when available */
int IRand(int lo, int hi);                  /* engine irand when available, inclusive */
float Dist2(const vec3_t a, const vec3_t b);
float HDist2(const vec3_t a, const vec3_t b);
float HDist(const vec3_t a, const vec3_t b);
void BoxCenter(edict_t *e, vec3_t out);
float AngleWrap(float a);                   /* -180 .. 180 */
float QuantiseAngle(float a);               /* 1/65536 turn */
float YawTo(const vec3_t from, const vec3_t to);
void CopyVec(vec3_t dst, const vec3_t src);
bool IsZero(const vec3_t v);
bool IsCharacter(edict_t *e);               /* in use with an AI object */
bool IsPlayer(edict_t *e);
bool IsAlive(edict_t *e);
AI *GetAI(edict_t *e);
trace_t Trace(const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, edict_t *pass, int mask);
bool InPVS(const vec3_t a, const vec3_t b);
void EyePoint(edict_t *e, vec3_t out);
edict_t *LeanProxy(edict_t *player);        /* entity chained to the player with entity flag 32768 */
bool PlayerLeaning(edict_t *player);
bool CheckBottom(edict_t *ent);             /* SPEC-700 helper rule "Bottom check", first form */
bool CheckBottomAt(edict_t *ent, AI *ai, const vec3_t pos, int flags); /* second form */
void CheckGround(edict_t *ent, AI *ai);     /* SPEC-700 helper rule "Ground check" */
void CategorizePosition(edict_t *ent);      /* water level */
void WorldEffects(edict_t *ent);            /* drowning, lava, slime */
void DropToFloor(edict_t *ent, AI *ai);
bool StepMove(edict_t *ent, AI *ai, const vec3_t move, int flags); /* SPEC-711 A6.3 */
bool DoorLike(edict_t *e);                  /* SPEC-710 C9 */
bool DoorUsableNow(edict_t *e);
int NavDoorPredicate(edict_t *door, edict_t *user);
void PlayEffect(edict_t *ent, const char *name);
bool ClassnameIs(edict_t *e, const char *name);
void UseTargetsNamed(const char *targetname, edict_t *activator, edict_t *other);
bool KillTargetFacingOK(edict_t *self);
void Dbg(const char *fmt, ...);
bool Cinematic(void);
const G3Anim *Anim(const char *name);       /* human table record by name */

/* ---- tables (ai_tables.cpp) ---- */
void BuildProfile(int type_code, float profile[AI::NUM_PROFILE], bool *has_entry);
extern const int personality_rows[6][6];

/* ---- manager (ai_manager.cpp) ---- */
void ManagerReset(void);
void ManagerAdd(AI *ai);             /* register an AI object made outside CreateAI (savegame load) */
std::vector<AI *> &ManagerAll(void);
AI *NewAIForCode(int code);          /* the AI object of a type code without an entity (savegame load) */
void SaveSelfTest(void);             /* ai_save.cpp, cvar ai_savetest */
void ManagerFrame(void);
void TriggerSoundCheck(const vec3_t pos);
void RemoveHelper(edict_t *ent, AI *ai);
void MakeHelper(edict_t *ent, AI *ai);
void SilentRemove(edict_t *ent, AI *ai);    /* SPEC-700 rule 9 */
void CasualtyRules(edict_t *victim, AI *ai, edict_t *attacker);

/* ---- senses (ai_senses.cpp) ---- */
Sense *NewSight(int code);
Sense *NewHearing(void);
Sense *NewOmniscience(void);
Sense *NewProximity(float radius);
Sense *NewInertSense(int code);
bool PerceiveFilter(edict_t *ent, AI *ai, edict_t *target); /* SPEC-701 section 5 rule 1 without the range */
bool ConeTest(edict_t *ent, AI *ai, edict_t *target, bool no_ai_threshold = false); /* section 5 rule 2 cone */

/* ---- decisions (ai_decisions.cpp) ---- */
float ReactionAge(AI *ai);
float ReactionK(edict_t *ent);
void SetFace(edict_t *ent, AI *ai, int state, float duration);

/* ---- actions (ai_actions.cpp) ---- */
Action *NewAction(int cls);
void ActionCompleted(edict_t *ent, AI *ai, Action *a, float m, float r);
bool IsDyingClass(int cls);
/* scripted moves (SPEC-750 12.4): arrival test, and marking a move action as part of a script action */
bool ScriptMoveArrived(edict_t *ent, const vec3_t dest, const vec3_t start, float radius);
void SetScriptMove(Action *a, const vec3_t start, float radius);

/* ---- script (ai_script.cpp) ---- */
Decision *NewScriptedDecision(void);
Decision *FindScripted(AI *ai);

void InstallFireFallback(void); /* ai_fire.cpp: character attacks until G18 sets g3hooks.fireAttack */

/* ---- savegames (ai_save.cpp) ---- */
/* One chunk payload: fields are addressed by their byte offset in the record of SPEC-700/701/710/711; when writing
 * they are stored, when reading they are loaded. Entities as entity numbers (`none` = −1, senses 0), components as
 * savegame ids, animations as table × 65536 + index (1 human, 2 dog, 3 cow; −1 none), script events through the
 * script host's encoding (SPEC-752). Offsets beyond the record size are ignored. */
class SaveRec {
public:
	unsigned char data[640]; /* the largest record is AIAI, 588 bytes */
	int size;
	bool reading;
	SaveRec(bool rd, int sz);
	void Raw(int off, void *p, int n);
	void I(int off, int &v);
	void F(int off, float &v);
	void V(int off, float *v);
	void B(int off, int &v); /* byte + 3 padding */
	void Ent(int off, edict_t *&e, int none = -1);
	void Anim(int off, const G3Anim *&a);
	void Event(int off, int &done_token);
	void Act(int off, Action *&a);
	void Dec(int off, Decision *&d);
};
/* one chunk of the AI block (engine container, or memory during the self-test) */
void SaveChunk(const char *id, const void *data, int size);
bool LoadChunk(const char *id, void *data, int size);
/* the scripted decision's extra chunks after its AISD record: AION count + SDSO per queued command */
void ScriptedSaveExtra(Decision *d, bool reading);
/* after loading: re-derive run-time state that is not in the records (scripted moves) */
void ScriptedAfterLoad(edict_t *ent, AI *ai, Decision *d);

} /* namespace om_ai */

#endif
