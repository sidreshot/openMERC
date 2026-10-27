/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-710
 * Owner: package G2. Internal: the combat-type decision base shared by ai_decisions.cpp, ai_path.cpp, ai_script.cpp.
 */
#ifndef OM_AI_DECISIONS_H
#define OM_AI_DECISIONS_H

#include "ai_local.h"

namespace om_ai {

int PFlags(AI *ai);
bool Armed(AI *ai);
bool BodyHasSeq(AI *ai, const char *name);
void Relax(Decision *d);
void FillInput(G3MoveInput &in, edict_t *ent, AI *ai, const vec3_t dest, const vec3_t face, edict_t *target,
               int posture, bool running, bool aiming, bool strafing, const char *forced);
void D3Rule(edict_t *ent, AI *ai, const G3Anim *rec, edict_t *target, int *cls, float *dur, bool *ends,
            edict_t **tgt);
Action *QueueRecord(edict_t *ent, AI *ai, Decision *d, const G3Anim *rec, const vec3_t dest, const vec3_t face,
                    edict_t *target);

/* SPEC-710 "Combat prefix" state (AIPD) with the C1-C9 rules */
class CombatDecision : public Decision {
public:
	int kind;
	/* combat prefix */
	float sidestep_time = -100;
	vec3_t sidestep_dir = {0, 0, 0};
	vec3_t pending = {0, 0, 0};
	vec3_t pending_normal = {0, 0, 0};
	edict_t *pending_door = nullptr;
	int pending_set = 0;
	int pending_kind = 0;
	float pending_time = -100;
	edict_t *target = nullptr;
	vec3_t target_pos = {0, 0, 0};
	float target_time = -100000.0f;
	vec3_t steer_dir = {0, 0, 0};
	float steer_time = -100;
	int range_code = 0;
	int blocker = -1;
	float S = -1000.0f, C = -1000.0f;
	/* search tail (kind 4) */
	float heard_time = -100;
	vec3_t heard_pos = {0, 0, 0};
	vec3_t trail[3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
	/* Dekker tail (kinds 13, 14) */
	int landmark_reached = 0;

	explicit CombatDecision(int k);
	int Kind() override { return kind; }
	bool Evaluate(edict_t *ent, AI *ai, int *priority) override;
	void Plan(edict_t *ent, AI *ai) override;
	void ActionDone(edict_t *ent, AI *ai, Action *a, float m) override;
	void ShotAt(edict_t *ent, AI *ai, const vec3_t origin, const vec3_t end, edict_t *shooter) override;
	void AlertSoundFrom(edict_t *ent, AI *ai, const vec3_t pos, edict_t *emitter) override;
	void SaveTail(SaveRec &r) override; /* SPEC-710 combat prefix + search / Dekker tails (ai_save.cpp layout) */

	/* D4 */
	bool Enemy(edict_t *ent, AI *ai, edict_t *e);
	float Memory();
	void Accept(edict_t *e, const Percept &p);
	void UpdateTarget(edict_t *ent, AI *ai);
	bool Valid(edict_t *ent, AI *ai);
	/* C1-C9 */
	void CombatPlan(edict_t *ent, AI *ai);
	int RangeTest(edict_t *ent, AI *ai);
	bool ClearShotTo(edict_t *ent, AI *ai, const vec3_t from, edict_t *t, const vec3_t pos);
	bool CrouchAllowed(AI *ai);
	void IdealPlan(edict_t *ent, AI *ai);
	void NonIdealPlan(edict_t *ent, AI *ai, int code);
	void SearchPlan(edict_t *ent, AI *ai);
	void PlanStand(edict_t *ent, AI *ai, const vec3_t face, edict_t *t, bool crouch, const char *seq);
	void PlanStandForward(edict_t *ent, AI *ai);
	void PlanStandAny(edict_t *ent, AI *ai, const vec3_t face);
	void PlanAttack(edict_t *ent, AI *ai, edict_t *t, const vec3_t at, bool crouch, const vec3_t moveto);
	void PlanMove(edict_t *ent, AI *ai, const vec3_t dest, const vec3_t face, bool running, bool aiming,
	              bool strafing);
	bool NavPoint(edict_t *ent, AI *ai, const vec3_t goal, int mode, vec3_t out);
	bool Steer(edict_t *ent, AI *ai, vec3_t goal);
	bool PendingValid(edict_t *ent);
	bool ExecutePending(edict_t *ent, AI *ai);
	void SetPending(const vec3_t p, int kind, edict_t *door, const vec3_t normal);
	void Sidestep(edict_t *ent, AI *ai, const vec3_t v);
	void Unstick(edict_t *ent, AI *ai);
	void DekkerShock(edict_t *ent, AI *ai);
	int DekkerRange(edict_t *ent, AI *ai);
};

} /* namespace om_ai */

#endif
