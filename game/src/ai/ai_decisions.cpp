/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-710 (decision kinds 0-6, 10-14: D1 common state, D2 bonus, D3 records to actions, D4 target
 * memory, C1 combat plan, C2 range test, C3 clear shot, C4 ideal plan, C5 non-ideal plan, C6 search, C7 retreat,
 * C8 pending points and steering, C9 unstick, C10 Dekker, C11 dodge, E1 events); kinds 7-9 are in ai_path.cpp and
 * ai_script.cpp
 * Owner: package G2.
 */
#include "ai_decisions.h"

#include <stdio.h>

namespace om_ai {

/* ------------------------------------------------------------------------------------------------------------------
 * helpers
 * ---------------------------------------------------------------------------------------------------------------- */
float ReactionAge(AI *ai) { return Now() - ai->reaction_time; }
float ReactionK(edict_t *ent) { return ent == DesignatedCharacter() ? 0.5f : 1.0f; }
int PFlags(AI *ai) { return (int)ai->profile[7]; }

void SetFace(edict_t *ent, AI *ai, int state, float duration)
{
	(void)ent;
	if (ai->body)
		ai->body->SetExpression(state, duration, false);
}

bool Armed(AI *ai) { return ai->body ? ai->body->RightHandType() != 0 : false; }

bool BodyHasSeq(AI *ai, const char *name)
{
	const G3Anim *a = Anim(name);
	return a && ai->body && ai->body->HasAnim(a);
}

static float FacingYawTo(edict_t *ent, const vec3_t p) { return AngleWrap(YawTo(ent->s.origin, p) - ent->s.angles[YAW]); }

/* G3MoveInput for a body choice (SPEC-722 inputs) */
void FillInput(G3MoveInput &in, edict_t *ent, AI *ai, const vec3_t dest, const vec3_t face, edict_t *target,
               int posture, bool running, bool aiming, bool strafing, const char *forced)
{
	memset(&in, 0, sizeof(in));
	in.forced = forced ? Anim(forced) : nullptr;
	in.posture = posture;
	vec3_t d = {0, 0, 0};
	if (dest && !IsZero(dest)) {
		VectorSubtract(dest, ent->s.origin, d);
		d[2] = 0;
	}
	float len = VectorNormalize(d);
	VectorCopy(d, in.moveDir);
	in.speed = len > 1 ? (running ? 200.0f : 100.0f) : 0.0f;
	in.running = running;
	in.aiming = aiming;
	in.strafing = strafing;
	in.target = target;
	vec3_t aimp;
	if (target) {
		EyePoint(target, aimp);
	} else if (face && !IsZero(face))
		VectorCopy(face, aimp);
	else
		VectorCopy(ent->s.origin, aimp);
	VectorCopy(aimp, in.aimPoint);
	if (face && !IsZero(face))
		in.turn = FacingYawTo(ent, face);
	else if (target)
		in.turn = FacingYawTo(ent, target->s.origin);
	in.meleeRange = target && Dist2(target->s.origin, ent->s.origin) < 6400.0f;
	(void)ai;
}

/* the length of a record's sequence (D3 "the animation's length" before SPEC-AMEND-005 G2.9) */
static float AnimLength(edict_t *ent, AI *ai, const G3Anim *rec)
{
	ghoul::IGhoulInst *inst = ent->ghoulInst;
	if (!inst || !ai->body || !ai->body->modelDir || !rec)
		return 1.0f;
	ghoul::IGhoulObject *obj = inst->GetGhoulObject();
	if (!obj)
		return 1.0f;
	char path[160];
	Com_sprintf(path, sizeof(path), "ghoul/%s/%s.ghl", ai->body->modelDir, rec->name);
	ghoul::GhoulID seq = obj->FindSequence(path);
	if (!seq)
		return 1.0f;
	float ft = 0;
	float d = inst->SequenceDuration(seq, (rec->flags & ANIMF_LOOP) ? 0 : 1, &ft);
	return d > 0 ? d : 1.0f;
}

/* SPEC-710 D3: the action class, duration, "ends with the animation" and target for a record (also used by the
 * scripted decision, SPEC-AMEND-006 G2.13) */
void D3Rule(edict_t *ent, AI *ai, const G3Anim *rec, edict_t *target, int *cls_out, float *dur_out, bool *ends_out,
            edict_t **tgt_out)
{
	int cls = 1;
	float dur = 0;
	bool ends = false;
	edict_t *tgt = nullptr;
	bool living = target && target->inuse && target->health > 0;
	switch (rec->cls) {
	case ANIMCLS_MOVE:
		if (rec->flags & ANIMF_FINISH_CYCLE) {
			cls = 7;
			dur = 0.2f;
		} else if (!(rec->flags & ANIMF_AIM) || !target) {
			cls = 7;
			dur = 0.5f;
		} else {
			cls = 13;
			dur = 0.25f;
			ends = true;
		}
		break;
	case ANIMCLS_ATTACK:
		cls = 13;
		dur = 0.25f;
		ends = true;
		if (living)
			tgt = target;
		break;
	case ANIMCLS_JUMP: cls = 9; dur = 1.5f; break;
	case ANIMCLS_FALL: cls = 10; dur = 0.4f; break;
	case ANIMCLS_HIT: cls = 1; dur = 4.0f; ends = true; break;
	case ANIMCLS_DEATH: cls = 3; dur = 9999.0f; break;
	default:
		if (rec->flags & ANIMF_FINISH_CYCLE) {
			cls = 1;
			dur = 4.0f;
			ends = true;
		} else if ((rec->flags & ANIMF_AIM) && target) {
			cls = 13;
			dur = 0.25f;
		} else {
			/* SPEC-AMEND-005 G2.9 (corrects D3 "the animation's length"): duration 0, not ending with the
			 * animation — the action completes at its first think and the decisions re-plan every think; the
			 * body keeps a running idle, so the animation is not restarted */
			cls = 1;
			dur = 0;
			ends = false;
			/* (cows too: SPEC-AMEND-007 G2.17 — the bulls that never switched idles in the black-box runs were
			 * embedded in brushes, so their preset-0 box was refused on every roll; the former animation-length
			 * duration for cow idles is gone, the preset rule is in QueueRecord) */
		}
		break;
	}
	*cls_out = cls;
	*dur_out = dur;
	*ends_out = ends;
	*tgt_out = tgt;
}

Action *QueueRecord(edict_t *ent, AI *ai, Decision *d, const G3Anim *rec, const vec3_t dest, const vec3_t face,
                    edict_t *target)
{
	if (!rec)
		return nullptr;
	/* SPEC-AMEND-007 G2.17 (SPEC-722 cow idle choice, rule 1): `std_imoo_n` / `std_ilooking_n` are allowed only
	 * when the AI can take the record's bounding-box preset (SPEC-700 rule 15 without the fallback: "yes" when the
	 * preset is in use, else the box is traced in place and applied when clear); otherwise the choice is `std_i_n`
	 * and the roll is not repeated. The check belongs in G3's choice (Needs from G3: `G3BodyOwner::CanTakePreset`);
	 * meanwhile G2 applies it to the answer, substituting the running `std_i_n` (the cow's idle sound has already
	 * been played by then, a small deviation). */
	if (ai->body && ai->body->family == BODY_COW && rec->name &&
	    (!Q_stricmp(rec->name, "std_imoo_n") || !Q_stricmp(rec->name, "std_ilooking_n")) &&
	    !ai->CanTakePreset(ent, rec->posture)) {
		const G3Anim *cur = ai->body->cur;
		if (cur && cur->name && !Q_stricmp(cur->name, "std_i_n")) {
			if (CvarValue(cv.ai_trace))
				Dbg("ai %d: cow idle %s refused (preset %d), std_i_n\n", G_EdictNum(ent), rec->name, rec->posture);
			rec = cur;
		}
	}
	int cls;
	float dur;
	bool ends;
	edict_t *tgt;
	D3Rule(ent, ai, rec, target, &cls, &dur, &ends, &tgt);
	Action *a = MakeAction(ai, cls, d, nullptr, rec, dest, face, dur, ends);
	a->target = tgt;
	ai->QueueBack(a);
	if (CvarValue(cv.ai_trace))
		Dbg("ai %d: queue class %d %s dur %.2f\n", G_EdictNum(ent), cls, rec->name, dur);
	return a;
}

/* ------------------------------------------------------------------------------------------------------------------
 * D1 / D4: common state, success feedback, target memory
 * ---------------------------------------------------------------------------------------------------------------- */
void Relax(Decision *d)
{
	float now = Now();
	float dt = now - d->last_relax;
	if (dt < 0)
		dt = 0;
	if (dt > 1)
		dt = 1;
	d->success = 0.05f * dt + (1.0f - 0.1f * dt) * d->success;
	d->last_relax = now;
}

bool CombatDecision::Enemy(edict_t *ent, AI *ai, edict_t *e)
{
	if (!SameTeam(ent, e))
		return true;
	return kind == DK_RETREAT && ai->perceive_teammates;
}

float CombatDecision::Memory()
{
	switch (kind) {
	case DK_SEARCH: case DK_RETREAT: return 90.0f;
	case DK_INERT: return 15.0f;
	default: return 45.0f;
	}
}

void CombatDecision::Accept(edict_t *e, const Percept &p)
{
	target = e;
	vec3_t c;
	for (int i = 0; i < 3; i++)
		c[i] = 0.5f * (e->mins[i] + e->maxs[i]);
	VectorAdd(p.pos, c, target_pos);
	target_time = p.time;
}

void CombatDecision::UpdateTarget(edict_t *ent, AI *ai)
{
	Percept pp = ai->NewestPlayer(), pc = ai->NewestCharacter();
	bool keep = target && (target->client || target->ai) && target != pp.ent && target != pc.ent && target->inuse &&
	            target->health > 0;
	if (!keep) {
		auto fresh = [&](const Percept &p) {
			return p.ent && p.ent->inuse && p.ent->health > 0 &&
			       (p.time >= target_time || fabsf(p.time - target_time) < 0.01f || !target) && Enemy(ent, ai, p.ent);
		};
		if (fresh(pp))
			Accept(pp.ent, pp);
		else if (fresh(pc))
			Accept(pc.ent, pc);
	}
	/* trail of the search decision */
	if (kind == DK_SEARCH && target) {
		float d2 = Dist2(trail[0], target_pos);
		if (d2 > 64.0f) {
			VectorCopy(trail[1], trail[2]);
			VectorCopy(trail[0], trail[1]);
			VectorCopy(target_pos, trail[0]);
		}
	}
	if (!Valid(ent, ai))
		target = nullptr;
}

bool CombatDecision::Valid(edict_t *ent, AI *ai)
{
	(void)ent;
	if (!target || !(target->client || target->ai) || !target->inuse || target->health <= 0)
		return false;
	if (Now() - target_time > Memory())
		return false;
	if (kind == DK_INERT && Armed(ai))
		return false;
	return true;
}

void CombatDecision::ActionDone(edict_t *ent, AI *ai, Action *a, float m)
{
	if (a->Kind() != AK_ATTACK)
		success = 0.75f * success + 0.25f * m;
	if (m <= 0.25f)
		Unstick(ent, ai);
	else if (pending_set && !PendingValid(ent))
		pending_set = 0;
}

/* SPEC-710 "Savegame records": combat prefix (36-159) and the kind 4 / 13 / 14 tails */
void CombatDecision::SaveTail(SaveRec &r)
{
	r.F(36, last_relax);
	r.F(40, sidestep_time);
	r.V(44, sidestep_dir);
	r.V(56, pending);
	r.V(68, pending_normal);
	r.Ent(80, pending_door);
	r.B(84, pending_set);
	r.I(88, pending_kind);
	r.F(92, pending_time);
	r.Ent(96, target);
	r.V(100, target_pos);
	r.F(112, target_time);
	r.V(116, steer_dir);
	r.F(128, steer_time);
	r.I(132, range_code);
	r.I(136, blocker);
	r.F(140, S);
	r.F(144, C);
	if (kind == DK_SEARCH) {
		int marks = 1;
		r.I(160, marks);
		r.I(164, marks);
		r.F(180, heard_time);
		r.V(184, heard_pos);
		r.V(196, trail[0]);
		r.V(208, trail[1]);
		r.V(220, trail[2]);
	} else if (kind == DK_DEKKER_CONSOLE || kind == DK_DEKKER_LAST)
		r.I(160, landmark_reached);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C3 clear shot
 * ---------------------------------------------------------------------------------------------------------------- */
bool CombatDecision::ClearShotTo(edict_t *ent, AI *ai, const vec3_t from_given, edict_t *t, const vec3_t pos)
{
	vec3_t from, to;
	if (from_given)
		VectorCopy(from_given, from);
	else
		EyePoint(ent, from);
	if (t)
		EyePoint(t, to);
	else {
		VectorCopy(pos, to);
		to[2] += 20;
	}
	int rht = ai->body ? ai->body->RightHandType() : 0;
	bool box = rht == 14 || rht == 9;
	vec3_t mins = {-10, -10, -6}, maxs = {10, 10, 6};
	int mask = box ? 0x6000081 : 0x2000081;
	trace_t tr = Trace(from, box ? mins : nullptr, box ? maxs : nullptr, to, ent, mask);
	bool clear = (tr.fraction > 0.95f && !tr.startsolid) || (t && tr.ent == t);
	if (!clear && t && t->client && PlayerLeaning(t)) {
		edict_t *lp = LeanProxy(t);
		if (lp) {
			vec3_t l;
			VectorCopy(lp->s.origin, l);
			l[2] += 16;
			vec3_t m6 = {-6, -6, -6}, x6 = {6, 6, 6};
			tr = Trace(from, box ? m6 : nullptr, box ? x6 : nullptr, l, ent, mask);
			clear = tr.fraction > 0.95f || tr.ent == lp;
		}
	}
	blocker = tr.ent ? G_EdictNum(tr.ent) : -1;
	clear_shot_time = Now();
	clear_shot_result = clear ? 1 : 0;
	return clear;
}

/* ------------------------------------------------------------------------------------------------------------------
 * C2 range test
 * ---------------------------------------------------------------------------------------------------------------- */
int CombatDecision::RangeTest(edict_t *ent, AI *ai)
{
	if (ent->spawnflags & SF_STATIONARY)
		return 0;
	if (ai->body && ai->body->LimbInjury()) {
		ai->too_close_latch = 1;
		return 2;
	}
	if (Now() - target_time > 45.0f)
		return 1;
	float d = sqrtf(Dist2(ent->s.origin, target_pos));
	if (d < 10)
		return 0;
	if (ai->body && !ai->body->cur)
		return 1;
	if (!ClearShotTo(ent, ai, nullptr, target, target_pos))
		return 3;
	float p0 = ai->profile[0], p1 = ai->profile[1];
	if (d <= 0.75f * p0 || (ai->too_close_latch && d <= 1.2f * p0)) {
		ai->too_close_latch = 1;
		return 2;
	}
	if (d > 1.2f * p0)
		ai->too_close_latch = 0;
	if (!(PFlags(ai) & 2) && (d >= 1.2f * p1 || (ai->too_far_latch && d >= 0.75f * p1))) {
		ai->too_far_latch = 1;
		return 1;
	}
	ai->too_far_latch = 0;
	return 0;
}

/* ------------------------------------------------------------------------------------------------------------------
 * planning primitives
 * ---------------------------------------------------------------------------------------------------------------- */
bool CombatDecision::CrouchAllowed(AI *ai) { return !(PFlags(ai) & 16) && (S > C + 3 || S < C); }

static const char *CrouchedAimName(AI *ai);

void CombatDecision::PlanStand(edict_t *ent, AI *ai, const vec3_t face, edict_t *t, bool crouch, const char *seq)
{
	G3MoveInput in;
	/* TEMPORARY (as in PlanAttack / PlanStandAny) until G3's stand choice has the crouched list: a crouched stand
	 * request from a character that is already crouched gets the standing `std_a_fwd_*` / idles from G3, so the
	 * character stands up, is asked to crouch again next think, and flickers `std_xcrouch_dwn` <-> `std_a_fwd_ms_2`
	 * every 0.4 s without ever aiming or firing (kos1 flythrough `fly_om_kos1`, entity 52 at 193-207 s; the
	 * original holds `cch_a_fwd_ms_2`). Ask for the crouched aim record by name. */
	if (crouch && !seq && ai->body && ai->body->cur && ai->body->cur->posture == POSTURE_CROUCH)
		seq = CrouchedAimName(ai);
	FillInput(in, ent, ai, nullptr, face, t, crouch ? POSTURE_CROUCH : POSTURE_STAND, false, t != nullptr, false, seq);
	if (crouch)
		C = Now();
	else
		S = Now();
	const G3Anim *rec = ai->body ? ai->body->ChooseStand(in) : nullptr;
	QueueRecord(ent, ai, this, rec, ent->s.origin, face, t);
}

/* the first crouched aim record the body has (SPEC-722 crouched list: `cch_a_fwd_*`); nullptr if none */
static const char *CrouchedAimName(AI *ai)
{
	static const char *const cch_aims[] = {"cch_a_fwd_ms_2", "cch_a_fwd_r_2", "cch_a_fwd_l_2", "cch_a_fwd_p_2"};
	for (const char *n : cch_aims) {
		const G3Anim *r = Anim(n);
		if (r && ai->body && ai->body->HasAnim(r))
			return n;
	}
	return nullptr;
}

/* C1.2 / kind 1 "stand facing forward" and C1.1 "stand": the body's stand choice with no posture request, so a
 * crouched character stays crouched (SPEC-722 pre-emption 4 changes posture only when one is requested). Black box:
 * the kos1 shotgunner, crouched when the player dies, stays in `cch_a_fwd_ms_2` for 35 s in the original
 * (`orig_kos1_walk_long1/2`); OpenMerc asked for standing and it stood up (`cch_xcrouch_up_mrs_2`) and idled. */
void CombatDecision::PlanStandAny(edict_t *ent, AI *ai, const vec3_t face)
{
	G3MoveInput in;
	/* TEMPORARY (as in PlanAttack): G3's stand choice has no crouched list without a target and answers a crouched
	 * character with the standing idles once its record is done; the original keeps the crouched aim record */
	const char *seq = nullptr;
	if (ai->body && ai->body->cur && ai->body->cur->posture == POSTURE_CROUCH)
		seq = CrouchedAimName(ai);
	FillInput(in, ent, ai, nullptr, face, nullptr, -1, false, false, false, seq);
	S = Now();
	const G3Anim *rec = ai->body ? ai->body->ChooseStand(in) : nullptr;
	QueueRecord(ent, ai, this, rec, ent->s.origin, face, nullptr);
}

void CombatDecision::PlanStandForward(edict_t *ent, AI *ai)
{
	vec3_t f, p;
	AngleVectors(ent->s.angles, f, nullptr, nullptr);
	VectorMA(ent->s.origin, 1, f, p);
	PlanStandAny(ent, ai, p);
}

void CombatDecision::PlanAttack(edict_t *ent, AI *ai, edict_t *t, const vec3_t at, bool crouch, const vec3_t moveto)
{
	G3MoveInput in;
	vec3_t face;
	if (t)
		VectorCopy(t->s.origin, face);
	else
		VectorCopy(at, face);
	FillInput(in, ent, ai, moveto, face, t, crouch ? POSTURE_CROUCH : POSTURE_STAND, moveto != nullptr, true,
	          false, nullptr);
	if (!t)
		VectorCopy(at, in.aimPoint);
	/* an armed character whose weapon is not ready to fire again (SPEC-722 "Ready to fire") aims in place
	 * instead of starting another attack. Black-box (kos1 shotgunner, `orig_kos1_sc930a574`): shots exactly every
	 * 3.2 s, between them `std_xcock_n_s_2` and then the aim stances `std_a_fwd_ms_2` / `cch_a_fwd_ms_2` (which may
	 * turn 30°/think), never fire records back to back; OPEN_QUESTIONS G2 16 */
	if (!moveto && ai->body && ai->body->RightHandType() > 1) {
		/* also while the target is more than ~44° off the owner's facing: the shot would be refused by the body
		 * (SPEC-722 Firing, dot < 0.717) and the fire records do not turn; the aim stance turns towards it (the
		 * original's crouched shotgunner turns from −67° to +70° between shots, OpenMerc's kept firing blanks) */
		vec3_t fw, to;
		AngleVectors(ent->s.angles, fw, nullptr, nullptr);
		VectorSubtract(face, ent->s.origin, to);
		fw[2] = to[2] = 0;
		VectorNormalize(fw);
		VectorNormalize(to);
		bool facing = DotProduct(fw, to) >= 0.717f || ai->body->RightHandType() == 18;
		if (!facing || !ai->body->ReadyToFire()) {
			/* aim in the posture last chosen (crouched aiming between crouched shots; the original stays crouched
			 * from its first crouched shot on) */
			bool crouched = (ai->body->cur && ai->body->cur->posture == POSTURE_CROUCH) || C > S;
			/* TEMPORARY until G3's stand choice has the crouched aim list (SPEC-722 "crouched, moving towards ...
			 * cch_a_fwd_*"): G3 answers a crouched aiming request with the standing `std_a_fwd_*`, so the
			 * character stands up and crouches again every think; ask for the crouched aim record by name */
			const char *seq = nullptr;
			if (crouched && ai->body->cur && ai->body->cur->posture == POSTURE_CROUCH)
				seq = CrouchedAimName(ai);
			PlanStand(ent, ai, face, t, crouch || crouched, seq);
			return;
		}
	}
	if (crouch)
		C = Now();
	else
		S = Now();
	const G3Anim *rec = ai->body ? ai->body->ChooseAttack(in) : nullptr;
	QueueRecord(ent, ai, this, rec, moveto ? moveto : ent->s.origin, face, t);
	ai->fire_flags &= ~1;
}

void CombatDecision::PlanMove(edict_t *ent, AI *ai, const vec3_t dest, const vec3_t face, bool running, bool aiming,
                              bool strafing)
{
	G3MoveInput in;
	FillInput(in, ent, ai, dest, face, aiming ? target : nullptr, -1, running, aiming, strafing, nullptr);
	S = Now();
	const G3Anim *rec = ai->body ? ai->body->ChooseMove(in) : nullptr;
	QueueRecord(ent, ai, this, rec, dest, face, aiming ? target : nullptr);
}

/* C4 */
void CombatDecision::IdealPlan(edict_t *ent, AI *ai)
{
	bool crouch_ok = CrouchAllowed(ai);
	if (ReactionAge(ai) < ai->profile[4] * ReactionK(ent)) {
		PlanStand(ent, ai, target->s.origin, target, crouch_ok, nullptr);
		return;
	}
	vec3_t f, d;
	AngleVectors(ent->s.angles, f, nullptr, nullptr);
	f[2] = 0;
	VectorNormalize(f);
	VectorSubtract(target_pos, ent->s.origin, d);
	d[2] = 0;
	VectorNormalize(d);
	bool clear = ClearShotTo(ent, ai, nullptr, target, target_pos);
	if (crouch_ok && DotProduct(f, d) > 0.98f && clear)
		PlanAttack(ent, ai, target, target_pos, true, nullptr);
	else if (clear)
		PlanAttack(ent, ai, target, target_pos, false, nullptr);
	else
		PlanStand(ent, ai, target->s.origin, target, crouch_ok, nullptr);
}

/* navigation query (SPEC-740 rule 6). Without navigation data every query is "direct" (SPEC-740 rule 6 step 1):
 * an approach falls back to walking straight at the destination; a flee query has no point, so C5's back-off finds
 * "no retreat point" (black box: arm1 has no navigation data, and a `gimme m_irq_msoldier1` that notices the player
 * 100 units behind it — too close for its ideal distance 512 — stands still in the original; OpenMerc's former
 * invented flee point 64 units away made it move) */
bool CombatDecision::NavPoint(edict_t *ent, AI *ai, const vec3_t goal, int mode, vec3_t out)
{
	if (CvarValue(cv.ai_nonodes) || !Nav_Active()) {
		if (mode == NAV_APPROACH) {
			VectorCopy(goal, out);
			return true;
		}
		return false;
	}
	if (Nav_MovePoint(&ai->nav, ent, ai->step_height, ent->s.origin, goal, mode, target)) {
		VectorCopy(ai->nav.movepoint, out);
		return true;
	}
	return false;
}

/* steering (C8): probes 60 units ahead and turns by +-40 / +-80 degrees around obstacles */
bool CombatDecision::Steer(edict_t *ent, AI *ai, vec3_t goal)
{
	if (ai->low_priority)
		return true;
	vec3_t d;
	VectorSubtract(goal, ent->s.origin, d);
	d[2] = 0;
	float len = VectorNormalize(d);
	if (len < 1)
		return true;
	vec3_t maxs;
	VectorCopy(ent->maxs, maxs);
	maxs[2] += 16;
	auto clearance = [&](const vec3_t dir) {
		vec3_t end;
		VectorMA(ent->s.origin, 60, dir, end);
		trace_t tr = Trace(ent->s.origin, ent->mins, maxs, end, ent, 0x2020003);
		if (tr.ent && DoorUsableNow(tr.ent))
			return 1.0f;
		if (tr.startsolid)
			return 0.0f;
		return tr.fraction;
	};
	float c0 = clearance(d);
	if (c0 >= 1.0f)
		return true;
	static const float rot[4] = {40, 80, -40, -80};
	float side = Frand() < 0.5f ? 1.0f : -1.0f;
	float best = 0.2f;
	vec3_t bestdir;
	bool found = false;
	for (int i = 0; i < 4; i++) {
		float a = side * rot[i] * (float)M_PI / 180.0f;
		vec3_t r = {d[0] * cosf(a) - d[1] * sinf(a), d[0] * sinf(a) + d[1] * cosf(a), 0};
		float c = clearance(r);
		if (c > best) {
			best = c;
			VectorCopy(r, bestdir);
			found = true;
		}
	}
	if (!found)
		return c0 > 0.2f;
	VectorMA(ent->s.origin, len, bestdir, goal);
	VectorCopy(bestdir, steer_dir);
	steer_time = Now();
	return true;
}

/* C5 */
void CombatDecision::NonIdealPlan(edict_t *ent, AI *ai, int code)
{
	float now = Now();
	bool may_attack = code != 7 && !(code == 1 && (PFlags(ai) & 128)) && ReactionAge(ai) >= ai->profile[4] * ReactionK(ent);
	if (code == 6) {
		const G3Anim *rec = ai->body ? ai->body->ChooseDodge(Frand() < 0.5f ? 0 : 1) : nullptr;
		if (rec) {
			QueueRecord(ent, ai, this, rec, ent->s.origin, nullptr, target);
			return;
		}
		code = 5;
	}
	if (code == 5) {
		G3MoveInput in;
		FillInput(in, ent, ai, nullptr, nullptr, nullptr, POSTURE_CROUCH, false, false, false, "cch_xcchcower_mid_a_a");
		const G3Anim *rec = ai->body ? ai->body->ChooseStand(in) : nullptr;
		if (rec) {
			QueueRecord(ent, ai, this, rec, ent->s.origin, nullptr, nullptr);
			return;
		}
		code = 4;
	}
	edict_t *t = target;
	if ((ent->spawnflags & SF_STATIONARY) || (ai->low_priority && t && t == SightClient())) {
		PlanStand(ent, ai, t ? t->s.origin : nullptr, t, false, nullptr);
		return;
	}
	vec3_t to_t;
	VectorSubtract(target_pos, ent->s.origin, to_t);
	to_t[2] = 0;
	float dist_t = VectorNormalize(to_t);
	if (code == 2 || code == 8) {
		vec3_t P;
		bool retreat = NavPoint(ent, ai, target_pos, NAV_FLEE, P);
		vec3_t r = {0, 0, 0};
		if (retreat) {
			VectorSubtract(P, ent->s.origin, r);
			r[2] = 0;
			if (VectorNormalize(r) <= 0)
				retreat = false;
		}
		if (CvarValue(cv.ai_trace) >= 2)
			Dbg("ai %d retreat: point %d (%.0f %.0f %.0f) dir (%.2f %.2f) nav %d\n", G_EdictNum(ent), retreat ? 1 : 0,
			    P[0], P[1], P[2], r[0], r[1], Nav_Active() ? 1 : 0);
		if (retreat) {
			/* bend away from characters within 96 units in front */
			edict_t *e = nullptr;
			vec3_t bend;
			VectorCopy(r, bend);
			while ((e = findradius(e, ent->s.origin, 96)) != nullptr) {
				if (e == ent || !e->ai || e->health <= 0)
					continue;
				vec3_t o;
				VectorSubtract(e->s.origin, ent->s.origin, o);
				o[2] = 0;
				float od = VectorNormalize(o);
				if (DotProduct(o, r) <= 0)
					continue;
				vec3_t perp = {-r[1], r[0], 0};
				if (DotProduct(perp, o) > 0)
					VectorNegate(perp, perp);
				float w = 1 - (od / 96) * (od / 96);
				VectorMA(bend, w, perp, bend);
			}
			VectorNormalize(bend);
			VectorCopy(bend, r);
			VectorCopy(r, ai->retreat_dir);
			vec3_t test;
			VectorMA(ent->s.origin, 18, r, test);
			test[2] += ent->viewheight;
			if (!(code == 8 || (PFlags(ai) & 8) || ClearShotTo(ent, ai, test, target, target_pos)))
				retreat = false;
		}
		if (retreat) {
			float stepd = 40;
			float dt = DotProduct(r, to_t);
			vec3_t face;
			bool strafe = false;
			if (dt < -0.717f) {
				VectorCopy(target->s.origin, face);
				if (!BodyHasSeq(ai, "std_rbackaim_n_mrs_2") && !BodyHasSeq(ai, "std_rbackaim_n_p_2") &&
				    !BodyHasSeq(ai, "std_rbackaim_n_l_2"))
					stepd = 4;
			} else if (dt > 0.717f) {
				VectorMA(ent->s.origin, 2440, r, face);
			} else {
				VectorCopy(target->s.origin, face);
				strafe = true;
				if (!BodyHasSeq(ai, "std_raimstraf_rt_mrs_2") && !BodyHasSeq(ai, "std_raimstraf_rt_l_2") &&
				    !BodyHasSeq(ai, "std_raimstraf_rt_p_cbn"))
					stepd = 12;
			}
			vec3_t goal;
			VectorMA(ent->s.origin, stepd, r, goal);
			if (may_attack && !(PFlags(ai) & 8) && ClearShotTo(ent, ai, nullptr, target, target_pos)) {
				G3MoveInput in;
				FillInput(in, ent, ai, goal, face, target, -1, true, true, strafe, nullptr);
				S = now;
				const G3Anim *rec = ai->body ? ai->body->ChooseAttack(in) : nullptr;
				QueueRecord(ent, ai, this, rec, goal, face, target);
			} else
				PlanMove(ent, ai, goal, face, true, false, strafe);
			return;
		}
		if (may_attack && ClearShotTo(ent, ai, nullptr, target, target_pos)) {
			bool crouch = CrouchAllowed(ai);
			if (crouch) {
				vec3_t eye;
				VectorCopy(ent->s.origin, eye);
				eye[2] += 12;
				crouch = ClearShotTo(ent, ai, eye, target, target_pos);
			}
			PlanAttack(ent, ai, target, target_pos, crouch, nullptr);
		} else
			PlanStand(ent, ai, target->s.origin, target, false, nullptr);
		return;
	}
	/* too far, out of time, no clear shot, flee (4), search (7) */
	vec3_t pathpt;
	if (!NavPoint(ent, ai, target_pos, NAV_APPROACH, pathpt)) {
		if (may_attack && ClearShotTo(ent, ai, nullptr, target, target_pos))
			PlanAttack(ent, ai, target, target_pos, false, nullptr);
		else
			PlanStand(ent, ai, target ? target->s.origin : nullptr, target, false, nullptr);
		return;
	}
	vec3_t dir;
	VectorSubtract(pathpt, ent->s.origin, dir);
	dir[2] = 0;
	float plen = VectorNormalize(dir);
	float stepd = plen < 10 ? plen : 10;
	vec3_t goal;
	VectorMA(ent->s.origin, stepd, dir, goal);
	vec3_t face;
	bool strafe = false;
	bool can_strafe = BodyHasSeq(ai, "std_raimstraf_rt_mrs_2") || BodyHasSeq(ai, "std_raimstraf_rt_l_2") ||
	                  BodyHasSeq(ai, "std_raimstraf_rt_p_cbn");
	if (code != 4 && can_strafe && target && dist_t < 448) {
		float dt = DotProduct(dir, to_t);
		if (dt < -0.717f) {
			VectorCopy(target->s.origin, face);
		} else if (dt > 0.717f)
			VectorMA(ent->s.origin, 2440, dir, face);
		else {
			VectorCopy(target->s.origin, face);
			strafe = true;
		}
	} else
		VectorMA(ent->s.origin, 500, dir, face);
	bool far_enough = dist_t > 50 || code == 4;
	if (stepd * stepd >= 2 && far_enough && !((PFlags(ai) & 2) && code != 4)) {
		if (Steer(ent, ai, goal))
			PlanMove(ent, ai, goal, face, true, may_attack && !(PFlags(ai) & 128) && code != 4 && code != 7, strafe);
		else
			PlanStand(ent, ai, face, nullptr, false, nullptr);
		return;
	}
	if (code == 4) {
		G3MoveInput in;
		const char *cow = Frand() < 0.5f ? "cch_xcchcower_mid_a_a" : "cch_xcchcower2_mid_a_a";
		FillInput(in, ent, ai, nullptr, nullptr, nullptr, POSTURE_CROUCH, false, false, false, cow);
		const G3Anim *rec = ai->body ? ai->body->ChooseStand(in) : nullptr;
		QueueRecord(ent, ai, this, rec, ent->s.origin, nullptr, nullptr);
		return;
	}
	if (may_attack && target && ClearShotTo(ent, ai, nullptr, target, target_pos))
		PlanAttack(ent, ai, target, target_pos, false, nullptr);
	else
		PlanStand(ent, ai, target ? target->s.origin : nullptr, target, CrouchAllowed(ai), nullptr);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C8 pending points, C9 unstick
 * ---------------------------------------------------------------------------------------------------------------- */
bool CombatDecision::PendingValid(edict_t *ent)
{
	return pending_set && Now() - pending_time < 1.0f && HDist2(pending, ent->s.origin) >= 2500.0f;
}

bool CombatDecision::ExecutePending(edict_t *ent, AI *ai)
{
	if (!pending_set)
		return false;
	if (!PendingValid(ent) && !(pending_kind == 3 && Now() - pending_time < 1.0f)) {
		pending_set = 0;
		return false;
	}
	switch (pending_kind) {
	case 0: {
		vec3_t face;
		VectorCopy(pending, face);
		PlanMove(ent, ai, pending, face, true, false, false);
		break;
	}
	case 1: {
		G3MoveInput in;
		FillInput(in, ent, ai, pending, pending, nullptr, -1, true, false, false, nullptr);
		const G3Anim *rec = ai->body ? ai->body->ChooseJump(in) : nullptr;
		QueueRecord(ent, ai, this, rec, pending, pending, nullptr);
		break;
	}
	case 2: {
		const G3Anim *rec = Anim("std_jdrop_n_a_a");
		QueueRecord(ent, ai, this, rec, pending, pending, nullptr);
		break;
	}
	case 3: {
		edict_t *door = pending_door;
		if (door && door->inuse && door->plUse)
			door->plUse(door, ent, ent);
		vec3_t face;
		VectorMA(ent->s.origin, 20, pending_normal, face);
		const G3Anim *rec = Anim("std_xopendoor_n_a_n");
		if (rec && ai->body && !ai->body->HasAnim(rec))
			rec = nullptr;
		if (rec)
			QueueRecord(ent, ai, this, rec, ent->s.origin, face, nullptr);
		break;
	}
	}
	pending_set = 0;
	return true;
}

void CombatDecision::SetPending(const vec3_t p, int kind_, edict_t *door, const vec3_t normal)
{
	VectorCopy(p, pending);
	pending_kind = kind_;
	pending_door = door;
	if (normal)
		VectorCopy(normal, pending_normal);
	else
		VectorClear(pending_normal);
	pending_time = Now();
	pending_set = 1;
}

void CombatDecision::Sidestep(edict_t *ent, AI *ai, const vec3_t v_in)
{
	float now = Now();
	if (pending_set && now - pending_time < 1.0f)
		return;
	vec3_t v;
	VectorCopy(v_in, v);
	v[2] = 0;
	if (VectorNormalize(v) <= 0)
		return;
	vec3_t perp = {-v[1], v[0], 0};
	vec3_t ref;
	bool recent = now - sidestep_time < 1.0f;
	if (recent && Frand() < 0.25f)
		VectorCopy(sidestep_dir, ref);
	else
		AngleVectors(ent->s.angles, ref, nullptr, nullptr);
	float thr = Crand();
	if (pending_set || recent) {
		float r = Crand();
		thr = r * r * r * r;
	}
	if (DotProduct(ref, perp) < thr)
		VectorNegate(perp, perp);
	float speed = VectorLength(ai->velocity);
	float dist = speed + 30;
	vec3_t p;
	VectorMA(ent->s.origin, dist, perp, p);
	VectorMA(p, dist * 0.5f, v, p);
	SetPending(p, 0, nullptr, nullptr);
	VectorCopy(perp, sidestep_dir);
	sidestep_time = now;
}

void CombatDecision::Unstick(edict_t *ent, AI *ai)
{
	if (!ent->groundentity && !(ent->flags & FL_FLY))
		return;
	vec3_t f, start, end;
	AngleVectors(ent->s.angles, f, nullptr, nullptr);
	f[2] = 0;
	VectorNormalize(f);
	VectorCopy(ent->s.origin, start);
	start[2] += ai->step_height;
	VectorMA(start, 25, f, end);
	trace_t tr = Trace(start, ent->mins, ent->maxs, end, ent, 0x2020003);
	if (tr.fraction >= 1.0f && !tr.startsolid) {
		vec3_t back;
		VectorNegate(f, back);
		Sidestep(ent, ai, back);
		return;
	}
	edict_t *o = tr.ent;
	if (o && o == target)
		return;
	if (o && o->ai) {
		float u = -20.0f * Frand();
		if ((float)(o->ai->rank - ai->rank) <= u)
			return;
		vec3_t of;
		AngleVectors(o->s.angles, of, nullptr, nullptr);
		if (DotProduct(of, f) > Crand())
			return;
		vec3_t away;
		VectorSubtract(ent->s.origin, o->s.origin, away);
		away[0] += FlRand(-15, 15);
		away[1] += FlRand(-15, 15);
		Sidestep(ent, ai, away);
		return;
	}
	if (o && DoorLike(o)) {
		if (DoorUsableNow(o)) {
			SetPending(ent->s.origin, 3, o, tr.plane.normal);
		} else if (!(pending_set && Now() - pending_time < 1.0f)) {
			vec3_t p;
			VectorMA(ent->s.origin, 70, tr.plane.normal, p);
			SetPending(p, 0, nullptr, nullptr);
		}
		return;
	}
	Sidestep(ent, ai, tr.plane.normal);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C1 combat plan and evaluation (kinds 2, 3, 4, 11-14)
 * ---------------------------------------------------------------------------------------------------------------- */
CombatDecision::CombatDecision(int k) : kind(k)
{
	created = Now();
	last_relax = created;
	switch (k) {
	case DK_DEKKER1: base_priority = 10; break;
	case DK_DEKKER2: base_priority = 8; break;
	case DK_DEKKER_CONSOLE: base_priority = 5; break;
	case DK_DEKKER_LAST: base_priority = 2; break;
	default: base_priority = 0; break;
	}
}

bool CombatDecision::Evaluate(edict_t *ent, AI *ai, int *priority)
{
	UpdateTarget(ent, ai);
	if (kind == DK_INERT) {
		bonus = 0;
		*priority = base_priority + bonus;
		return true;
	}
	if (kind == DK_SEARCH)
		success = target ? 0.2f : 0.0f;
	if (target && (!ai->body || Armed(ai)))
		bonus = 2 + (int)floorf(success + 0.5f);
	else
		bonus = 1;
	Relax(this);
	if (kind == DK_DEKKER1 && ent->health < ent->max_health)
		return false;
	if (kind == DK_DEKKER2 && ent->health < 0.75f * ent->max_health)
		return false;
	if (kind == DK_DEKKER_CONSOLE && landmark_reached)
		return false;
	*priority = base_priority + bonus;
	return true;
}

void CombatDecision::Plan(edict_t *ent, AI *ai)
{
	if (kind >= DK_DEKKER1 && kind <= DK_DEKKER_LAST)
		DekkerShock(ent, ai);
	if (kind == DK_SEARCH) {
		SearchPlan(ent, ai);
		return;
	}
	CombatPlan(ent, ai);
}

void CombatDecision::CombatPlan(edict_t *ent, AI *ai)
{
	if (!ent->groundentity && ent->movetype != MOVETYPE_FLY) {
		PlanStandAny(ent, ai, nullptr);
		return;
	}
	if (!target) {
		PlanStandForward(ent, ai);
		return;
	}
	if ((target->client || target->ai) && Enemy(ent, ai, target)) {
		SetFace(ent, ai, (!ai->body || Armed(ai)) ? EXPR_ALT : EXPR_AFRAID, 5.0f);
		ai->Alert(ent, target_time, target, target_pos);
	}
	ai->fire_flags &= ~1;
	int code;
	if (kind == DK_DEKKER_CONSOLE || kind == DK_DEKKER_LAST)
		code = DekkerRange(ent, ai);
	else
		code = RangeTest(ent, ai);
	range_code = code;
	if (CvarValue(cv.ai_trace) >= 2)
		Dbg("ai %d combat: kind %d target %d code %d reaction age %.2f (limit %.2f) dist %.0f target age %.1f "
		    "p0 %.0f p1 %.0f rht %d\n", G_EdictNum(ent), kind, G_EdictNum(target), code, ReactionAge(ai),
		    ai->profile[4] * ReactionK(ent), sqrtf(Dist2(ent->s.origin, target_pos)), Now() - target_time,
		    ai->profile[0], ai->profile[1], ai->body ? ai->body->RightHandType() : -1);
	if (code == 0) {
		IdealPlan(ent, ai);
		return;
	}
	if (PendingValid(ent) && ExecutePending(ent, ai))
		return;
	NonIdealPlan(ent, ai, code);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C6 search (kind 4)
 * ---------------------------------------------------------------------------------------------------------------- */
void CombatDecision::ShotAt(edict_t *ent, AI *ai, const vec3_t origin, const vec3_t end, edict_t *shooter)
{
	(void)ent;
	(void)ai;
	(void)end;
	(void)shooter;
	if (kind == DK_SEARCH) {
		VectorCopy(origin, heard_pos);
		heard_time = Now();
	}
}

void CombatDecision::AlertSoundFrom(edict_t *ent, AI *ai, const vec3_t pos, edict_t *emitter)
{
	(void)ent;
	(void)ai;
	(void)emitter;
	if (kind == DK_SEARCH) {
		VectorCopy(pos, heard_pos);
		heard_time = Now();
	}
}

void CombatDecision::SearchPlan(edict_t *ent, AI *ai)
{
	float now = Now();
	ai->search_value = now;
	bool heard8 = now - heard_time <= 8.0f;
	if (heard8 || (target && now - target_time <= 4.0f && !(PFlags(ai) & 32))) {
		vec3_t aim;
		if (heard8)
			VectorCopy(heard_pos, aim);
		else if (!IsZero(trail[2]))
			VectorCopy(trail[2], aim);
		else
			VectorCopy(target_pos, aim);
		if (ClearShotTo(ent, ai, nullptr, nullptr, aim)) {
			G3MoveInput in;
			FillInput(in, ent, ai, nullptr, aim, nullptr, -1, false, true, false, nullptr);
			VectorCopy(aim, in.aimPoint);
			const G3Anim *rec = ai->body ? ai->body->ChooseAttack(in) : nullptr;
			if (rec) {
				QueueRecord(ent, ai, this, rec, ent->s.origin, aim, nullptr);
				ai->fire_flags |= 1;
				return;
			}
		}
	}
	if ((ent->spawnflags & SF_STATIONARY) && target) {
		IdealPlan(ent, ai);
		return;
	}
	vec3_t dest;
	bool old_alert = now - ai->alert_time > 10.0f;
	if (old_alert || (PFlags(ai) & 64) || !target) {
		if (now - heard_time <= 20.0f)
			VectorCopy(heard_pos, dest);
		else if (target || !IsZero(target_pos))
			VectorCopy(target_pos, dest);
		else {
			PlanStandForward(ent, ai);
			return;
		}
	} else
		VectorCopy(target->s.origin, dest);
	range_code = 7;
	if (Dist2(dest, ent->s.origin) <= 64.0f * 64.0f) {
		vec3_t node;
		if (Nav_LookAroundNode(ent->s.origin, IRand(0, 1293), node))
			PlanStand(ent, ai, node, nullptr, false, nullptr);
		else
			PlanStandForward(ent, ai);
		return;
	}
	vec3_t saved;
	VectorCopy(target_pos, saved);
	VectorCopy(dest, target_pos);
	NonIdealPlan(ent, ai, 7);
	VectorCopy(saved, target_pos);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C10 Dekker
 * ---------------------------------------------------------------------------------------------------------------- */
void CombatDecision::DekkerShock(edict_t *ent, AI *ai)
{
	(void)ai;
	edict_t *sc = SightClient();
	if (sc && Dist2(sc->s.origin, ent->s.origin) < 70.0f * 70.0f) {
		vec3_t dir;
		VectorSubtract(sc->s.origin, ent->s.origin, dir);
		VectorNormalize(dir);
		Dmg_Damage(sc, ent, ent, dir, sc->s.origin, ent->s.origin, 5, 0, DMGF_NO_KNOCKBACK | 0x2000000, 14, 0.5f,
		           0.5f);
		PlayEffect(ent, "environ/dekarmor1");
	}
}

int CombatDecision::DekkerRange(edict_t *ent, AI *ai)
{
	const char *cls = kind == DK_DEKKER_CONSOLE ? "func_Dekker_console" : "func_Dekker_diehere";
	edict_t *mark = G_Find(nullptr, FOFS(classname), cls);
	if (kind == DK_DEKKER_LAST && landmark_reached)
		return RangeTest(ent, ai);
	if (!mark)
		return RangeTest(ent, ai);
	float lim = kind == DK_DEKKER_CONSOLE ? 90.0f : 70.0f;
	if (HDist(mark->s.origin, ent->s.origin) > lim) {
		VectorCopy(mark->s.origin, target_pos);
		return 1;
	}
	if (!landmark_reached) {
		landmark_reached = 1;
		if (kind == DK_DEKKER_CONSOLE) {
			UseTargetsNamed("dekkergun1", ent, ent);
			UseTargetsNamed("dekkergun2", ent, ent);
			UseTargetsNamed("dekkergun3", ent, ent);
		} else {
			ent->spawnflags |= SF_STATIONARY;
			ent->flags |= FL_NO_KNOCKBACK;
		}
	}
	return RangeTest(ent, ai);
}

/* ------------------------------------------------------------------------------------------------------------------
 * C7 retreat / cower (kind 6)
 * ---------------------------------------------------------------------------------------------------------------- */
class RetreatDecision : public CombatDecision {
public:
	int health_snapshot = 0;
	float fear = 0;
	int vanish = 0;
	int danger_kind = 0;
	int danger_time = -100;
	vec3_t danger_pos = {0, 0, 0};
	int evaluated = 0;

	RetreatDecision() : CombatDecision(DK_RETREAT) {}
	void SaveTail(SaveRec &r) override
	{
		CombatDecision::SaveTail(r);
		r.I(0, evaluated); /* OpenMerc: first evaluation done */
		r.I(160, health_snapshot);
		r.F(164, fear);
		r.B(168, vanish);
		r.I(172, danger_kind);
		r.I(176, danger_time);
		r.V(180, danger_pos);
	}

	void ShotAt(edict_t *, AI *, const vec3_t, const vec3_t, edict_t *) override
	{
		fear += 0.4f;
		if (fear > 1)
			fear = 1;
	}
	void Danger(edict_t *, AI *, int k, const vec3_t pos) override
	{
		if (k >= danger_kind) {
			danger_kind = k;
			danger_time = (int)Now();
			VectorCopy(pos, danger_pos);
		}
	}
	bool FreshExplosive() { return (danger_kind == 2 || danger_kind == 3) && Now() - danger_time < 2.0f; }

	bool Evaluate(edict_t *ent, AI *ai, int *priority) override
	{
		UpdateTarget(ent, ai);
		fear -= 0.003f;
		if (fear < 0)
			fear = 0;
		if (ai->low_priority) {
			fear = 0;
		} else {
			bool unarmed = ai->body && !Armed(ai);
			if (unarmed && fear > 0)
				bonus = 4;
			else if (!evaluated || !target || Now() - target_time > 90.0f) {
				bonus = 1;
				health_snapshot = ent->health;
			} else if (FreshExplosive() && !(ent->spawnflags & SF_STATIONARY))
				bonus = 4;
			else if (ai->alerted && ai->body && ai->body->RightHandType() == 0)
				bonus = 4;
			else
				bonus = 1;
		}
		evaluated = 1;
		*priority = base_priority + bonus;
		return true;
	}

	void FleeDestination(edict_t *ent, AI *ai, vec3_t out)
	{
		if (FreshExplosive() && target) {
			VectorCopy(target_pos, out);
			return;
		}
		vanish = 1;
		edict_t *sc = SightClient();
		TypeInfo ti;
		int team = GetTypeInfo(ai->type_code, &ti) ? ti.team : 1;
		if (sc && SameTeam(ent, sc) && !CvarValue(cv.ai_nonodes) && Nav_FleeSpot(&ai->nav, team, out))
			return;
		vec3_t from;
		if (target)
			VectorCopy(target_pos, from);
		else if (sc)
			VectorCopy(sc->s.origin, from);
		else {
			VectorCopy(ent->s.origin, out);
			vanish = 0;
			return;
		}
		if (!NavPoint(ent, ai, from, NAV_FLEE, out)) {
			VectorCopy(ent->s.origin, out);
			vanish = 0;
		}
	}

	void Plan(edict_t *ent, AI *ai) override
	{
		if (!ent->groundentity && ent->movetype != MOVETYPE_FLY) {
			PlanStand(ent, ai, nullptr, nullptr, false, nullptr);
			return;
		}
		bool unarmed = ai->body && !Armed(ai);
		if (!target && !(unarmed && fear > 0) && fear <= 0.7f) {
			PlanStandForward(ent, ai);
			return;
		}
		if (target && (target->client || target->ai) && Enemy(ent, ai, target))
			ai->Alert(ent, target_time, target, target_pos);
		/* own range test */
		SetFace(ent, ai, EXPR_AFRAID, 5.0f);
		edict_t *sc = SightClient();
		bool in_range = false;
		int code = 4;
		if (sc && Dist2(sc->s.origin, ent->s.origin) > 1000.0f * 1000.0f) {
			vanish = 1;
			in_range = true;
		} else if (fear > 0.7f || (unarmed && fear > 0))
			in_range = true;
		else
			code = (danger_kind == 2 || danger_kind == 3) ? 2 : 4;
		range_code = in_range ? 0 : code;
		vec3_t dest;
		if (in_range) {
			if (fear > 0.7f || (unarmed && fear > 0)) {
				G3MoveInput in;
				FillInput(in, ent, ai, nullptr, nullptr, nullptr, POSTURE_CROUCH, false, false, false,
				          "cch_xcchcower_mid_a_a");
				const G3Anim *rec = ai->body ? ai->body->ChooseStand(in) : nullptr;
				if (!rec) {
					FillInput(in, ent, ai, nullptr, nullptr, nullptr, POSTURE_CROUCH, false, false, false,
					          "cch_xcchcower2_mid_a_a");
					rec = ai->body ? ai->body->ChooseStand(in) : nullptr;
				}
				QueueRecord(ent, ai, this, rec, ent->s.origin, nullptr, nullptr);
				return;
			}
			bool v = vanish != 0;
			FleeDestination(ent, ai, dest);
			if (v)
				vanish = 1;
			PlanStand(ent, ai, dest, nullptr, false, nullptr);
			if (vanish)
				ai->view_mode = 3;
			return;
		}
		FleeDestination(ent, ai, dest);
		vec3_t saved;
		VectorCopy(target_pos, saved);
		VectorCopy(dest, target_pos);
		if (code == 2 && target)
			VectorCopy(saved, target_pos);
		edict_t *st = target;
		if (!target)
			target = ent;
		NonIdealPlan(ent, ai, code);
		target = st;
		VectorCopy(saved, target_pos);
	}
};

/* ------------------------------------------------------------------------------------------------------------------
 * C11 dodge (kind 5)
 * ---------------------------------------------------------------------------------------------------------------- */
class DodgeDecision : public CombatDecision {
public:
	float last_dodge = -100;
	float shot_time = -100;
	vec3_t shot_origin = {0, 0, 0}, shot_end = {0, 0, 0};
	int dir = 0;
	DodgeDecision() : CombatDecision(DK_DODGE) {}
	void SaveTail(SaveRec &r) override
	{
		CombatDecision::SaveTail(r);
		r.F(16, success); /* "16 = s" (SPEC-710 kind 5) */
		int zero = 0;
		r.I(160, zero);
		r.F(164, last_dodge);
		r.F(168, shot_time);
		r.I(172, dir);
		r.V(176, shot_origin);
		r.V(188, shot_end);
	}

	void ShotAt(edict_t *, AI *, const vec3_t origin, const vec3_t end, edict_t *) override
	{
		VectorCopy(origin, shot_origin);
		VectorCopy(end, shot_end);
		shot_time = Now();
	}

	bool Evaluate(edict_t *ent, AI *ai, int *priority) override
	{
		UpdateTarget(ent, ai);
		float now = Now();
		bonus = 0;
		bool ok = !(ai->body && ai->body->LimbInjury()) && !(ent->spawnflags & SF_STATIONARY) && target &&
		          (!ai->body || Armed(ai)) && now - last_dodge >= 3.0f && now - shot_time <= 0.3f &&
		          now - target_time <= 0.2f && Frand() <= Skill('a') * ai->profile[2];
		if (ok) {
			vec3_t f, d;
			AngleVectors(ai->aim_angles, f, nullptr, nullptr);
			VectorSubtract(ent->s.origin, shot_origin, d);
			VectorNormalize(d);
			ok = DotProduct(f, d) <= -0.707f;
		}
		if (ok) {
			/* away from the shot line; side trace, else forward dive */
			vec3_t line, rel, side;
			VectorSubtract(shot_end, shot_origin, line);
			line[2] = 0;
			VectorNormalize(line);
			VectorSubtract(ent->s.origin, shot_origin, rel);
			vec3_t perp = {-line[1], line[0], 0};
			float s = DotProduct(rel, perp) >= 0 ? 1.0f : -1.0f;
			VectorScale(perp, s, side);
			vec3_t end, mins = {-16, -16, -16}, maxs = {16, 16, 16};
			VectorMA(ent->s.origin, 64, side, end);
			trace_t tr = Trace(ent->s.origin, mins, maxs, end, ent, 3);
			vec3_t r;
			AngleVectors(ent->s.angles, nullptr, r, nullptr);
			if (tr.fraction >= 1.0f)
				dir = DotProduct(side, r) >= 0 ? 1 : 0;
			else {
				vec3_t fw;
				AngleVectors(ent->s.angles, fw, nullptr, nullptr);
				VectorMA(ent->s.origin, 64, fw, end);
				tr = Trace(ent->s.origin, mins, maxs, end, ent, 3);
				if (tr.fraction >= 1.0f)
					dir = 3;
				else
					ok = false;
			}
		}
		if (ok)
			bonus = 4;
		else
			shot_time = -100;
		*priority = base_priority + bonus;
		return true;
	}

	void Plan(edict_t *ent, AI *ai) override
	{
		if (target)
			ai->Alert(ent, target_time, target, target_pos);
		const G3Anim *rec = ai->body ? ai->body->ChooseDodge(dir) : nullptr;
		vec3_t face = {0, 0, 0};
		if (rec && (rec->flags & ANIMF_BACKWARDS)) {
			vec3_t f;
			AngleVectors(ai->aim_angles, f, nullptr, nullptr);
			VectorMA(ent->s.origin, -200, f, face);
		}
		QueueRecord(ent, ai, this, rec, ent->s.origin, IsZero(face) ? nullptr : face, target);
		last_dodge = Now();
		shot_time = -100;
	}
};

/* ------------------------------------------------------------------------------------------------------------------
 * C13 idle (kind 1) and the invalid kinds
 * ---------------------------------------------------------------------------------------------------------------- */
class IdleDecision : public CombatDecision {
public:
	IdleDecision() : CombatDecision(DK_IDLE) {}
	bool Evaluate(edict_t *, AI *, int *priority) override
	{
		bonus = 1;
		*priority = base_priority + bonus;
		return true;
	}
	void Plan(edict_t *ent, AI *ai) override { PlanStandForward(ent, ai); }
};

class InvalidDecision : public Decision {
public:
	int k;
	explicit InvalidDecision(int kk) : k(kk) {}
	int Kind() override { return k; }
	bool Evaluate(edict_t *, AI *, int *priority) override
	{
		*priority = 0;
		return true;
	}
	void Plan(edict_t *, AI *) override {}
};

Decision *NewScriptedDecision(void);
Decision *NewPathDecision(int kind, edict_t *first);

Decision *MakeDecision(int kind)
{
	Decision *d = nullptr;
	switch (kind) {
	case DK_IDLE: d = new IdleDecision(); break;
	case DK_COMBAT: case DK_INERT: case DK_SEARCH: case DK_DEKKER1: case DK_DEKKER2: case DK_DEKKER_CONSOLE:
	case DK_DEKKER_LAST:
		d = new CombatDecision(kind);
		break;
	case DK_DODGE: d = new DodgeDecision(); break;
	case DK_RETREAT: d = new RetreatDecision(); break;
	case DK_PATH: case DK_POINT: d = NewPathDecision(kind, nullptr); break;
	case DK_SCRIPTED: d = NewScriptedDecision(); break;
	case 0:
		gi.dprintf("ERROR: only allowed to use leaves of the decision class tree\n");
		d = new InvalidDecision(0);
		break;
	default:
		gi.dprintf("ERROR: invalid decision class code: %d\n", kind);
		d = new InvalidDecision(10);
		break;
	}
	d->created = Now();
	return d;
}

} /* namespace om_ai */
