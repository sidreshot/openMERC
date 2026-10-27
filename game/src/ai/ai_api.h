/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700, SPEC-701, SPEC-710, SPEC-711
 * Owner: package G2 (game/src/ai). Read by G3 (bodies), G4 (roster, vehicles), G5 (navigation), G6b (script host),
 * G18 (frame hooks). Do not edit from other packages: ask in docs/impl/G2.md ("Requests to G2").
 *
 * Public interface of the character AI framework of the game module.
 *
 * Objects (SPEC-700 "Parts of a character AI object"):
 *   om_ai::AI  (= ::OmAI) one per character entity (edict_t::ai). Its public members are the fields of the saved
 *              record of SPEC-700 (AIAI); it owns the sense / decision / action lists and the combat profile. It is
 *              the G3BodyOwner of the character's body (game/src/damage/damage_api.h, edict_t::g3body, G3).
 *   Sense      perception component (SPEC-701), class codes 0-6.
 *   Decision   goal component (SPEC-710), kinds 0-14; the best one plans by queueing actions.
 *   Action     animation-driven behaviour unit (SPEC-711), classes 0-21. Packages may derive their own.
 * Bodies are G3's (G3Body, animation records G3Anim); navigation is G5's (nav_api.h, NavCache in the record).
 *
 * Roster usage (G4, SPEC-730 section 1; replaces the rgs_* fallbacks of roster/rg_local.h):
 *   om_ai::CharacterSetup(ent, doa)          rgs_CommonSetup        SPEC-700 rule 1.1
 *   AI *ai = om_ai::CreateAI(ent, type)      (new)                  rule 1.2.1, right after the common set-up
 *   ... type set-up: Body_Create (G3), model, presets ai->SetBBoxPreset(), ai->scale, ai->step_height, ai->rank,
 *       ai->buddy, ai->hostage; the body is attached by CreateAI/StartCharacter (body->owner = ai)
 *   om_ai::InitProfile(ai) or copy ai->profile[0..8] then om_ai::AdjustProfile(ai)   rule 1.2.3-1.2.6
 *   ai->first_think = <start-up set function>; ent->think = om_ai::CharacterThink  (rgs_CharacterThink)
 *   start-up set function:  om_ai::StartCharacter(ent); om_ai::AddDecisionSet(ai, set)  (rgs_StartupDecisions)
 *   om_ai::ReportNoise(center, loudness, radius)   rgs_NoiseBroadcast
 *   om_ai::RemoveAllComponents(ent)                rgs_RemoveAllComponents
 *   om_ai::CountKill(ent)                          rgs_CountKill
 *
 * Engine / G18 hooks (one call each, see docs/impl/G2.md):
 *   om_ai::LevelStart()   at level spawn, before entities spawn (clears the character manager)
 *   om_ai::FrameBegin()   every server frame before entities run (sight client/character, character manager)
 * Stimuli from weapons and effects (G18/G3/G7): ReportNoise, AlertSound, DangerNotice, ShotAtNotice, FlashMute,
 * MuteCharacter.
 *
 * Script host (G6b, SPEC-750 section 12): HasAI, ScriptCommand, ScriptReset, ClearCallForHelp, CinematicEndActors;
 * G6b sets om_ai::g_report_ai_done to ScriptSystem::ai_action_completed.
 */
#ifndef OM_AI_API_H
#define OM_AI_API_H

#include "../g_local.h"
#include "../damage/damage_api.h"
#include "../nav/nav_api.h"

#include <stdint.h>
#include <vector>

namespace om_script {
struct AiCommand;
}

class OmAI;

namespace om_ai {

typedef ::OmAI AI;
class Sense;
class Decision;
class Action;
class SaveRec; /* savegame record buffer (ai_save.cpp, SPEC-700 "Savegame chunks of the AI") */

/* ------------------------------------------------------------------------------------------------------------------
 * Character type codes (SPEC-700 "Character type codes")
 * ---------------------------------------------------------------------------------------------------------------- */
enum TypeCode {
	TYPE_BULL = 6,
	TYPE_PLAIN = 7,
	TYPE_HELICOPTER = 8,
	TYPE_TANK = 9,
	TYPE_SNOWCAT = 10,
	TYPE_HUSKY = 11,
	TYPE_ROTTWEILER = 12,
	TYPE_BODYPART = 13,
	TYPE_PLAYERCORPSE = 14,
	TYPE_FIRST_HUMAN = 15,
	TYPE_RAIDERBOSS2 = 41, /* m_x_mraiderboss2, Dekker */
	TYPE_SKINBOSS = 83,
	TYPE_LAST = 110
};

enum Family { FAMILY_NONE = 0, FAMILY_SPECIAL, FAMILY_HELICOPTER, FAMILY_VEHICLE, FAMILY_DOG, FAMILY_CIVILIAN,
	FAMILY_MALE, FAMILY_FEMALE };

struct TypeInfo {
	int team;      /* team rules value (SPEC-700 section 13); 9/10 report 1 (ERRATA: SPEC-731 amendment 4) */
	bool innocent; /* counts as an innocent bystander (SPEC-700 rule 11) */
	Family family;
};

/* false for codes outside 6..110 */
bool GetTypeInfo(int code, TypeInfo *out);
/* type code of an m_* classname by the SPEC-700 table, 0 when not listed */
int TypeCodeForClassname(const char *classname);

/* character spawnflags (SPEC-700 rule 3, SPEC-710) */
enum {
	SF_BLIND = 0x2,
	SF_DEAF = 0x4,
	SF_NO_SIZE_VARIATION = 0x8,
	SF_DORMANT = 0x10,
	SF_INATTENTIVE = 0x20,
	SF_WAKE_AT_ONCE = 0x40,
	SF_STATIONARY = 0x80, /* no knockback; never moves (SPEC-710 C2) */
	SF_NO_CALL_FOR_HELP = 0x2000,
	SF_HOSTAGE = 0x4000,
	SF_PROTECTION0 = 0x8000,
	SF_DIE_IN_PLACE = 0x10000,
	SF_KEEP_WEAPON = 0x20000,
	SF_KILLTARGET_ON_RETREAT = 0x40000,
	SF_FACE_PLAYER1 = 0x100000
};

/* entity flags used by the AI (SPEC-700) beyond g_local.h */
enum {
	FL_DELIBERATE_MISS = 0x00001000, /* owner's bullets pass through the sight client (SPEC-711 A7) */
	FL_SPAWNER_MADE = 0x00010000,    /* made by a spawner; also the dormant-use clearance flag (SPEC-700 rule 4) */
	FL_CINEMATIC_FROZEN = 0x00020000,
	FL_AI_HELPER = 0x00100000        /* the "Nug Buddy" helper entity (SPEC-700 rule 16) */
};

/* ------------------------------------------------------------------------------------------------------------------
 * Perception records (SPEC-701 "Records")
 * ---------------------------------------------------------------------------------------------------------------- */
enum PerceptKind { PK_NONE = 0, PK_NOISE = 2, PK_DIRECTED = 4, PK_TOLD = 5, PK_DEATH = 6, PK_ALERT_CALL = 7,
	PK_SEEN = 8, PK_TRACKED = 9, PK_OMNISCIENT = 10 };

struct Percept {
	edict_t *ent = nullptr;
	vec3_t pos = {0, 0, 0};
	float time = -10000.0f;
	int kind = 0;
};

class Sense {
public:
	virtual ~Sense() {}
	virtual int ClassCode() = 0;                      /* SPEC-701 codes 0-6 */
	virtual int Mask() = 0;                           /* 1, 2 or 4 */
	/* returns false when the sense is finished and must be removed */
	virtual bool Update(edict_t *ent, AI *ai, int mask) = 0;
	virtual void Mute(float now, float level, int mode, float duration) {}
	virtual float MuteLevel(float now) { return 0.0f; }
	virtual void Hear(const vec3_t pos, edict_t *emitter, const vec3_t emitter_origin, int kind, float now) {}
	virtual bool ReadyToRemove() { return true; }
	/* savegame: the class-specific part of the sense chunk (ai_save.cpp writes the common part) */
	virtual void SaveTail(SaveRec &r) {}

	Percept player;      /* last player perceived */
	Percept character;   /* last non-player character perceived */
	float created = 0;
};

/* ------------------------------------------------------------------------------------------------------------------
 * Decisions (SPEC-710)
 * ---------------------------------------------------------------------------------------------------------------- */
enum DecisionKind { DK_INVALID0 = 0, DK_IDLE = 1, DK_COMBAT = 2, DK_INERT = 3, DK_SEARCH = 4, DK_DODGE = 5,
	DK_RETREAT = 6, DK_PATH = 7, DK_POINT = 8, DK_SCRIPTED = 9, DK_INVALID10 = 10, DK_DEKKER1 = 11, DK_DEKKER2 = 12,
	DK_DEKKER_CONSOLE = 13, DK_DEKKER_LAST = 14 };

/* start-up decision sets (SPEC-710 "Decision sets given at spawn", SPEC-730 table 3); values = ROSTER_SET_* */
enum DecisionSet { SET_A_BYSTANDER = 0 /* 2, 6 */, SET_B_FIGHTER = 1 /* 2, 3, 4, 6, 5 */,
	SET_C_FIGHTER_RANK3 = 2, SET_F_ANIMAL = 3 /* 2 */, SET_G_DEKKER = 4, SET_H_DEAD_ON_ARRIVAL = 5 /* nothing */,
	SET_VEHICLE = 6 /* 1 */, SET_NONE = 7, SET_UNUSED_RANK2 = 8 /* 2, 3, 4, 6, rank 2 */, SET_UNUSED_236 = 9 };

class Decision {
public:
	virtual ~Decision() {}
	virtual int Kind() = 0;
	/* SPEC-700 rule 8: false = finished (removed); otherwise *priority = base + bonus */
	virtual bool Evaluate(edict_t *ent, AI *ai, int *priority) = 0;
	virtual void Plan(edict_t *ent, AI *ai) = 0;
	/* an action this decision queued completed with movement result m (SPEC-710 D1) */
	virtual void ActionDone(edict_t *ent, AI *ai, Action *a, float m) {}
	/* SPEC-700 rule 8 notices */
	virtual void ShotAt(edict_t *ent, AI *ai, const vec3_t origin, const vec3_t end, edict_t *shooter) {}
	virtual void AlertSoundFrom(edict_t *ent, AI *ai, const vec3_t pos, edict_t *emitter) {}
	virtual void Danger(edict_t *ent, AI *ai, int kind, const vec3_t pos) {}
	virtual bool ReadyToRemove() { return true; }
	/* RESET_AI: a scripted decision matches the entity given (SPEC-700 rule 8) */
	virtual bool MatchesReset(edict_t *e) { return false; }
	/* savegame: the kind-specific part of the decision chunk (ai_save.cpp writes the common prefix) */
	virtual void SaveTail(SaveRec &r) {}

	float created = 0;
	int bonus = 0;
	int base_priority = 0;
	float success = 0.5f;
	float time_limit = 1e9f;
	int has_time_limit = 0;
	float clear_shot_time = -1000.0f;
	int clear_shot_result = 0;
	float last_relax = 0;
};

/* ------------------------------------------------------------------------------------------------------------------
 * Actions (SPEC-711)
 * ---------------------------------------------------------------------------------------------------------------- */
enum ActionKind { AK_PLAY = 0, AK_MOVE = 1, AK_PAIN = 2, AK_DEATH = 3, AK_ATTACK = 4, AK_JUMP = 5, AK_FALL = 6,
	AK_TIMED = 7 };

/* movement flags (SPEC-711 common record offset 60) */
enum { MF_JUMP_IMPULSE = 1, MF_STEP_OFF = 2, MF_FULL_STEP = 4 };

class Action {
public:
	virtual ~Action() {}
	virtual int ClassCode() = 0;                      /* SPEC-711 classes 0-21 */
	virtual int Kind() = 0;                           /* SPEC-711 kind column */
	/* made current (SPEC-711 A1.2) */
	virtual void Start(edict_t *ent, AI *ai);
	/* one think (SPEC-711 A1.3): true when complete; *m and *r = movement result and result */
	virtual bool Think(edict_t *ent, AI *ai, float *m, float *r) = 0;
	virtual void Interrupted(edict_t *ent, AI *ai) { interrupted = 1; }
	/* told r by the action this one is linked from (SPEC-711 A1.3) */
	virtual void LinkedDone(float r) { stopped = r < 0.001f ? 1 : 0; }
	virtual bool ReadyToRemove(edict_t *ent, AI *ai) { return true; }
	/* savegame: the class tail of the action chunk (ai_save.cpp writes the 112-byte common record) */
	virtual void SaveTail(SaveRec &r) {}
	/* (re)initialisation; also used when the previous action is reused (SPEC-700 rule 8 "Reuse") */
	void Init(Decision *owner, Action *linked, const G3Anim *rec, const vec3_t dest, const vec3_t face,
	          float duration, bool ends_with_anim);

	/* SPEC-711 common record */
	const G3Anim *record = nullptr;
	vec3_t facing = {0, 0, 0};
	vec3_t destination = {0, 0, 0};
	float created = 0;
	int interrupted = 0;
	int stopped = 0;
	Decision *owner = nullptr;
	Action *linked = nullptr;
	float move_scale = 1.0f;
	float turn_allowance = 30.0f;
	int move_flags = MF_FULL_STEP;
	float start_time = 0;
	float duration = 0;
	float end_time = 0;
	vec3_t jump_impulse = {0, 0, 0};
	float landing_check = 0;
	int ends_with_anim = 0;
	int done_token = 0;        /* AI-done marker of a script action (SPEC-750 12.6), 0 = none */
	int lethal = 0;
	int target_bolt = 0;
	edict_t *target = nullptr;
	int script_flags = 0;      /* SPEC-750 12.1 flag word for scripted actions */
};

/* standard creation request: reuses the AI's previous action of the same class (SPEC-700 rule 8) */
Action *MakeAction(AI *ai, int cls, Decision *owner, Action *linked, const G3Anim *rec, const vec3_t dest,
                   const vec3_t face, float duration, bool ends_with_anim);

} /* namespace om_ai */

/* ------------------------------------------------------------------------------------------------------------------
 * The character AI object (SPEC-700). Public members = the saved record of SPEC-700 (offsets in comments).
 * Global name so that edict_t can hold a pointer to it (edict_t::ai); use it as om_ai::AI.
 * ---------------------------------------------------------------------------------------------------------------- */
class OmAI : public G3BodyOwner {
public:
	enum { NUM_PROFILE = 15, NUM_BBOX = 9, BBOX_NONE = 9 };

	OmAI();
	virtual ~OmAI();

	int type_code = om_ai::TYPE_PLAIN;
	void (*first_think)(edict_t *ent) = nullptr; /* per-type start-up set (SPEC-700 rule 7 step 1) */

	/* SPEC-700 AIAI record */
	vec3_t velocity = {0, 0, 0};         /* 4: own movement velocity estimate (units per think) */
	vec3_t death_angles = {0, 0, 0};     /* 16: desired facing while alive, death angles afterwards */
	float heel_r = 0, heel_l = 0;        /* 28, 32: stored heel heights (footstep hysteresis, SPEC-AMEND-005 G2.5) */
	int first_think_done = 0;            /* 36 */
	vec3_t bbox_mins[NUM_BBOX];          /* 44 */
	vec3_t bbox_maxs[NUM_BBOX];          /* 152 */
	int bbox_current = BBOX_NONE;        /* 260 */
	float floor_height = 0;              /* 264 */
	int alerted = 0;                     /* 268 */
	float alert_time = 0;                /* 272 */
	float reaction_time = 0;             /* 276 */
	int perceive_teammates = 0;          /* 280 */
	float search_value = 0;              /* 284 (written by the search decision, never read) */
	float recently_called = -20.0f;      /* 288 */
	NavCache nav;                        /* 292: SPEC-740 per-character navigation cache */
	int too_close_latch = 0;             /* 348 (in the cache's unused bytes, SPEC-710) */
	int too_far_latch = 0;               /* 352 */
	edict_t *owner = nullptr;            /* 388 */
	edict_t *target = nullptr;           /* 392 */
	om_ai::Action *current = nullptr;    /* 396 */
	om_ai::Action *previous = nullptr;   /* 400 */
	int ground_linkcount = 0;            /* 404 */
	int awake = 0;                       /* 408 */
	int call_for_help = 1;               /* 412 */
	int hostage = 0;                     /* 416 */
	int buddy = 0;                       /* 420 */
	int face_player = 0;                 /* 424 */
	vec3_t retreat_dir = {0, 0, 0};      /* 428 */
	float scale = 1.0f;                  /* 440 */
	int sense_mask = 7;                  /* 444 */
	float jump_reach = 100.0f;           /* 448 */
	float jump_height = 50.0f;           /* 452 */
	float vehicle_456 = 1000000.0f;      /* 456 (vehicle AI) */
	float step_height = 24.0f;           /* 460 */
	int vehicle_464 = 7;                 /* 464 (vehicle AI) */
	int fire_flags = 0;                  /* 468: bit 1 firing at a remembered position */
	int low_priority = 1;                /* 472 */
	int sense_stamp = 0;                 /* 476 */
	int view_mode = 1;                   /* 480: 0 narrow, 1 normal, 2 wide, 3 vanish when unseen */
	int frame_flag = 0;                  /* 484 */
	int corpse_check = 0;                /* 488 */
	int rank = 0;                        /* 492 */
	float profile[NUM_PROFILE];          /* 496 */
	G3Body *body = nullptr;              /* 556 */
	ghoul::IGhoulObject *ghoul_object = nullptr; /* 560 */
	vec3_t aim_angles = {0, 0, 0};       /* 564 */
	vec3_t look_angles = {0, 0, 0};      /* 576: facing (body direction; the body's head bolt writes it) */

	/* run-time state that is not part of the record */
	float turn_factor = 1.0f;            /* per-character turn factor (SPEC-722 yaw limits) */
	float ground_offset = 0;             /* quake_ground bolt height (SPEC-722) */
	float heel_prev = 0;                 /* the reported foot's stored heel height before its last report (G2.5) */
	edict_t *helper = nullptr;           /* this think's helper entity */
	int dead = 0;                        /* the death rule ran */
	int spawner_made = 0;                /* created by a spawner (SPEC-701 rule 1: spawnflag 16384 targets) */
	float alertradius = 75.0f;
	int had_player_record = 0;           /* the previous perception summary had a player record (reaction moment) */
	int just_noticed = 0;                /* SPEC-722 startle: set at the first sighting, consumed by OnStartled (not saved) */

	std::vector<om_ai::Sense *> senses;
	std::vector<om_ai::Decision *> decisions;
	std::vector<om_ai::Action *> queue;  /* front = queue[0] */

	/* --- SPEC-700 framework rules (virtual: special AIs of G4 may override) --- */
	virtual void Think(edict_t *ent);                         /* rule 7, character part */
	virtual void Wake(edict_t *ent);                          /* rule 5 */
	virtual bool PartsReadyToRemove(edict_t *ent);            /* rule 10 "Ready to be removed" */

	void Alert(edict_t *ent, float time, edict_t *source, const vec3_t pos); /* rule 6 */
	bool AlertedRecently(float now) { return alerted && now - alert_time <= 1.0f; }
	void AddDeferredSenses(edict_t *ent);                     /* spawnflags 2/4 */
	om_ai::Decision *FindDecision(int kind);
	void RemoveDecision(edict_t *ent, int kind, edict_t *match);
	void QueueBack(om_ai::Action *a, bool wake = false);
	void QueueFront(om_ai::Action *a);
	void Interrupt(edict_t *ent, om_ai::Action *a);           /* pre-empt the current action */
	void NextAction(edict_t *ent);
	void DestroyAction(om_ai::Action *a);
	void DestroyDecision(om_ai::Decision *d);
	void ClearActions(bool keep_current);
	void ClearAll(bool keep_current);                         /* senses, decisions, actions */
	void SetBBoxPreset(int i, const vec3_t mins, const vec3_t maxs);
	int ChangeBBox(edict_t *ent, int preset, bool allow_fallback); /* SPEC-700 rule 15: 1 changed, 0 refused */
	/* SPEC-AMEND-007 G2.17: "the AI can take the record's preset" for a body's idle choice (the cow idle choice of
	 * SPEC-722 rule 1): true at once when the preset is in use, else the preset box is traced in place (mask
	 * 0x2020003, no fallback) and applied when clear. For G3: call it from the cow idle choice before returning
	 * `std_imoo_n` / `std_ilooking_n` (a `G3BodyOwner` virtual would be the clean path). */
	bool CanTakePreset(edict_t *ent, int preset) { return ChangeBBox(ent, preset, false) != 0; }

	/* perception summary (SPEC-701 section 8) */
	om_ai::Percept NewestPlayer(int mask = -1, om_ai::Sense **from = nullptr);
	om_ai::Percept NewestCharacter(int mask = -1, om_ai::Sense **from = nullptr);
	bool TargetPosition(vec3_t out);   /* SPEC-700 rule 14; false = unknown (owner origin returned) */
	float MostMuted(float now, int mask = -1);
	void MuteSenses(float now, int mask, float level, int mode, float duration);
	void HearEvent(const vec3_t pos, edict_t *emitter, const vec3_t emitter_origin, int kind);

	/* --- G3BodyOwner (damage_api.h): the AI parts of the damage rule and the body's queries --- */
	void OnBetrayed(edict_t *attacker) override;
	void OnHitAlert(edict_t *attacker) override;
	void OnHit(void) override;
	bool IsBuddy(void) override { return buddy != 0; }
	bool IsHostage(void) override { return hostage != 0; }
	void Die(edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, int dflags) override;
	void Pain(edict_t *attacker, const vec3_t point, int knockback, int damage) override;
	edict_t *Target(void) override;
	bool TargetHostileArmed(void) override;
	float FlashBlindness(void) override;
	bool JustNoticedEnemy(void) override;
	void OnStartled(void) override;                           /* the body played the startle */
	void Velocity(vec3_t out) override { VectorCopy(velocity, out); }
	float MoveValue(void) override;
	float ReferenceHeight(void) override;
	bool HasKiller(void) override;
	float TurnFactor(void) override { return turn_factor; }
	bool IsDesignated(void) override;
	bool FiresBlind(void) override { return (fire_flags & 1) != 0; }
	bool FeetFollowFloor(void) override;
	float RefireFactor(void) override;
	void OnSequenceEnd(void) override {}
	void OnGroundOffset(float z) override { ground_offset = z; floor_height = z; }
	void OnLook(const vec3_t angles, float eye_height) override;
	void OnHeel(int side, float height) override;
	void OnDropWeaponNote(void) override;
	void OnDogBite(edict_t *victim) override;
	/* combat profile flag 1 (SPEC-700 rule 19, profile[7] bit 1): the gore code's calm-weapon test
	 * (SPEC-AMEND-005 G3.12) */
	bool CombatProfileFlag1(void) override { return (((int)profile[7]) & 1) != 0; }
};

namespace om_ai {

/* ------------------------------------------------------------------------------------------------------------------
 * Creation and spawn (SPEC-700 rules 1-4)
 * ---------------------------------------------------------------------------------------------------------------- */
/* special AI classes of other packages (G4); default: a plain AI object */
typedef AI *(*AIFactory)(int type_code);
void RegisterAIFactory(int type_code, AIFactory make);

void CharacterSetup(edict_t *ent, bool dead_on_arrival = false); /* rule 1.1 */
AI *CreateAI(edict_t *ent, int type_code);             /* rule 1.2.1; prints the invalid-code error */
void InitProfile(AI *ai);                              /* rule 1.2.3-1.2.6 from the SPEC-700 tables */
void AdjustProfile(AI *ai);                            /* rule 1.2.4-1.2.6 only (profile[0..8] already set) */
void StartCharacter(edict_t *ent);                     /* rule 2 + rule 4 (monster_start_go equivalent) */
void AddDecisionSet(AI *ai, int set);                  /* DecisionSet / ROSTER_SET_* */
Decision *MakeDecision(int kind);                      /* factory by SPEC-710 kind */
Sense *MakeSense(int code, float alertradius = 75.0f); /* factory by SPEC-701 code */
void CharacterThink(edict_t *ent);                     /* entity think of every character (SPEC-700 rule 7) */
void RemoveAllComponents(edict_t *ent);                /* senses, decisions, actions */
void CountKill(edict_t *ent);                          /* rule 11 "total characters killed" */
void FreeCharacter(edict_t *ent);                      /* frees the entity, its AI and its body's remains */
/* SP_path_corner / SP_point_combat (G18 hook): resolves waitaction1..6 / moveaction from `st` (SPEC-760 B8.1) */
void PathCornerSpawn(edict_t *self);

/* mission counters (SPEC-700 rule 11), reset by the end-of-mission statistics command */
struct MissionStats {
	int enemies_killed, friendlies_killed, total_killed;
	float score;
};
extern MissionStats g_stats;

/* ------------------------------------------------------------------------------------------------------------------
 * Frame hooks, stimuli and helpers (SPEC-700 "Character helper rules", SPEC-701)
 * ---------------------------------------------------------------------------------------------------------------- */
void LevelStart(void);
void FrameBegin(void);

edict_t *SightClient(void);
edict_t *SightCharacter(void);
edict_t *DesignatedCharacter(void); /* nearest awake character to the sight client (SPEC-700 manager) */

/* noise broadcast (SPEC-700 "Noise broadcast"): hearing muting of nearby characters (loudness, radius table) */
void ReportNoise(const vec3_t center, float loudness, float radius);
/* alert sound (SPEC-701 section 6); kinds PerceptKind */
void AlertSound(edict_t *emitter, const vec3_t pos, int kind, edict_t *target, float radius, bool needs_sight,
                bool tell_decisions);
/* danger notice (SPEC-700 rule 12): explosives kind 2 radius 160, deaths kind 1 radius 128 */
void DangerNotice(int kind, const vec3_t pos, float radius);
/* a player's bullet hit the world in single player (SPEC-710 E1) */
void ShotAtNotice(edict_t *shooter, const vec3_t start, const vec3_t end);
/* flash blinding (SPEC-701 section 4) */
void FlashMute(const vec3_t origin, float radius);
/* direct muting of one character (burning, electric shock: sight level 25, mode 2) */
void MuteCharacter(edict_t *ent, int mask, float level, int mode, float duration);

bool SameTeam(edict_t *a, edict_t *b);                 /* SPEC-700 section 13 */
int RangeClass(float dist2);                           /* SPEC-700 helper rule "Range class" */
bool Visible(edict_t *self, edict_t *other, bool lean);
int LevelNumber(void);                                 /* SPEC-700 rule 20 */
const char *LevelName(int number);
float Skill(char param);                               /* toughness group T-a .. T-i (SPEC-760 B9.2, SPEC-710) */
/* SPEC-700 voice throttles: category 0 = wake yell, 1..6 tactical (backup, throw, duck, retreat, help, react) */
bool VoiceAllowed(int category);

/* ------------------------------------------------------------------------------------------------------------------
 * Script interface (SPEC-750 section 12, SPEC-751 section 1.3)
 * ---------------------------------------------------------------------------------------------------------------- */
bool HasAI(edict_t *ent);
void ScriptCommand(edict_t *ent, const om_script::AiCommand &cmd);
void ScriptReset(edict_t *ent);                        /* RESET_AI */
void ClearCallForHelp(edict_t *ent);
void CinematicEndActors(void);                         /* SPEC-751 1.4 step 3 */
extern void (*g_report_ai_done)(int done_id);          /* set by G6b: ScriptSystem::ai_action_completed */

/* ------------------------------------------------------------------------------------------------------------------
 * Savegames (SPEC-700 "Savegame chunks of the AI", SPEC-752 1.2 step 6) — ai_save.cpp
 * WriteLevelAI / ReadLevelAI write / read the whole AI block of a level save: part 1 (the eight AICT/AICC tables:
 * Ghoul models, bolt-ons and object instances through G9's glue, actions, decisions, senses, bodies through G3,
 * characters) and part 2 (per-object chunks in the order of SPEC-700, G3's body images, G4's vehicle chunks
 * around AIAI). The savegame package (G10, game/src/save/) calls them in the level section right after the script
 * chunks (ScriptGlue_WriteLevel / ReadLevel) and before the attachment chunks (GG_WriteAttached / GG_ReadAttached,
 * `OINM`). On load every existing
 * AI object is released first, all objects are created from the tables, then each reads its chunks; bodies are
 * re-linked to their AI objects (`owner`) and the entities to theirs (`ai`, think).
 * ---------------------------------------------------------------------------------------------------------------- */
void WriteLevelAI(void);
void ReadLevelAI(void);

} /* namespace om_ai */

#endif
