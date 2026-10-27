/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-711 (action classes 0-14, life cycle A1, per-think animation step A2, animation start A3, event
 * flags A4, facing and turning A5, movement A6, aiming A7, class rules A8); vehicle classes 15-21 are G4's (inert
 * here)
 * Owner: package G2.
 */
#include "ai_local.h"

namespace om_ai {

bool IsDyingClass(int cls) { return cls == 3 || cls == 4 || cls == 5; }

/* ------------------------------------------------------------------------------------------------------------------
 * common record and life cycle (A1)
 * ---------------------------------------------------------------------------------------------------------------- */
void Action::Init(Decision *own, Action *link, const G3Anim *rec, const vec3_t dest, const vec3_t face, float dur,
                  bool ends)
{
	owner = own;
	linked = link;
	record = rec ? rec : Anim("std_istand_n_a_n");
	VectorCopy(dest ? dest : vec3_origin, destination);
	VectorCopy(face ? face : vec3_origin, facing);
	duration = dur;
	ends_with_anim = ends ? 1 : 0;
	created = Now();
	interrupted = 0;
	stopped = 0;
	move_scale = record ? record->k : 1.0f;
	turn_allowance = record ? 2.0f * record->yawLeft : 30.0f;
	move_flags = MF_FULL_STEP;
	start_time = created;
	end_time = created + dur;
	VectorClear(jump_impulse);
	landing_check = created + 999.0f;
	done_token = 0;
	lethal = 0;
	target_bolt = 0;
	target = nullptr;
	script_flags = 0;
}

static void PlayRecord(edict_t *ent, AI *ai, Action *a, bool attack)
{
	G3Body *b = ai->body;
	if (!b || !a->record)
		return;
	bool loops = (a->record->flags & ANIMF_LOOP) != 0;
	if (attack)
		b->SetAnim(a->record, !loops);
	bool playing = !b->AnimDone();
	b->SetAnim(a->record, !(playing || loops));
	(void)ent;
}

void Action::Start(edict_t *ent, AI *ai)
{
	if (stopped)
		return;
	PlayRecord(ent, ai, this, false);
	start_time = Now();
	end_time = start_time + duration;
}

void ActionCompleted(edict_t *ent, AI *ai, Action *a, float m, float r)
{
	if (CvarValue(cv.ai_trace))
		Dbg("ai %d: action %d (%s) done m %.2f r %.2f\n", G_EdictNum(ent), a->ClassCode(),
		    a->record ? a->record->name : "-", m, r);
	if (a->owner)
		a->owner->ActionDone(ent, ai, a, m);
	if (a->linked)
		a->linked->LinkedDone(r);
	int token = a->done_token;
	a->done_token = 0;
	if (token && g_report_ai_done)
		g_report_ai_done(token);
	if (ent->inuse && ent->ai == ai && ai->current == a)
		ai->NextAction(ent);
}

/* ------------------------------------------------------------------------------------------------------------------
 * the ground-class base: A2 .. A7
 * ---------------------------------------------------------------------------------------------------------------- */
class GroundAction : public Action {
public:
	int cls;
	int kind;
	bool step_ok = true;       /* this think's step move succeeded */
	bool moved = false;
	vec3_t step = {0, 0, 0};

	GroundAction(int c, int k) : cls(c), kind(k) {}
	int ClassCode() override { return cls; }
	int Kind() override { return kind; }

	bool OwnerScriptLike()
	{
		if (!owner)
			return false;
		int k = owner->Kind();
		return k == DK_PATH || k == DK_POINT || k == DK_SCRIPTED;
	}

	/* A4 */
	void HandleEvents(edict_t *ent, AI *ai, uint32_t ev);
	/* A5 */
	void Turn(edict_t *ent, AI *ai);
	/* A7 */
	void Aim(edict_t *ent, AI *ai);
	void JitterStep(edict_t *ent, AI *ai, const vec3_t desired, float d, bool player, float Tc, float E);
	/* A6: returns false when the step move failed */
	bool Move(edict_t *ent, AI *ai);
	/* A2: false when the action must complete with (0, 0) */
	bool Step(edict_t *ent, AI *ai, bool attack, bool dying);
	void Fire(edict_t *ent, AI *ai);
};

static void AddEntityEvent(edict_t *ent, int ev)
{
	for (int i = 0; i < 3; i++)
		if (!ent->s.event[i]) {
			ent->s.event[i] = ev;
			return;
		}
}

void GroundAction::Fire(edict_t *ent, AI *ai)
{
	G3Body *b = ai->body;
	if (!b)
		return;
	if ((lethal || target_bolt) && target)
		ai->target = target;
	edict_t *t = ai->Target();
	vec3_t eye, f, point;
	EyePoint(ent, eye);
	AngleVectors(ai->aim_angles, f, nullptr, nullptr);
	float dist = 512;
	if (t)
		dist = sqrtf(Dist2(t->s.origin, ent->s.origin));
	/* at least 128 units out along the aim: the weapon's flash bolt sits ~30 units in front of the eye, so with a
	 * near target (tsr1 `tokenguy2`, player 25 units away) and a steep aim a 64-unit point lay behind the muzzle and
	 * the body refused the shot as "facing away" (15 of 32 refused); the original fires those */
	if (dist < 128)
		dist = 128;
	VectorMA(eye, dist, f, point);
	/* a shot with the lethal flag or a target-bolt selector takes the separate path of SPEC-AMEND-006 G2.11 (no facing
	 * test, straight at the bolt; ai_fire.cpp ScriptedShot redirects it); until the body takes that path itself, the
	 * point handed to it lies straight ahead so that its facing test passes */
	if ((lethal || target_bolt) && target && target->inuse && t == target) {
		vec3_t fwd;
		AngleVectors(ent->s.angles, fwd, nullptr, nullptr);
		VectorMA(eye, dist, fwd, point);
	}
	if (CvarValue(cv.ai_trace) && t)
		Dbg("ai %d: aim (%.0f %.0f) yaw %.0f eye (%.0f %.0f %.0f) point (%.0f %.0f %.0f) target (%.0f %.0f %.0f)\n",
		    G_EdictNum(ent), ai->aim_angles[PITCH], ai->aim_angles[YAW], ent->s.angles[YAW], eye[0], eye[1], eye[2],
		    point[0], point[1], point[2], t->s.origin[0], t->s.origin[1], t->s.origin[2]);
	bool fired = b->Fire(t, point, false, target_bolt);
	/* a character that has shot at a player has reacted to it: no startle afterwards (SPEC-722 reaction override 3
	 * is for an enemy it has just noticed). tsr1 booth shooters: they fire crouched from the start, the flag stayed
	 * set and the first standing idle after their script played `std_xstartled_n_a_a`; the original's keep firing */
	if (t && t->client)
		ai->just_noticed = 0;
	if (CvarValue(cv.ai_trace))
		Dbg("ai %d: fire at %d (%s) rht %d -> %d\n", G_EdictNum(ent), t ? G_EdictNum(t) : -1,
		    record ? record->name : "-", b->RightHandType(), fired ? 1 : 0);
	lethal = 0;
}

void GroundAction::HandleEvents(edict_t *ent, AI *ai, uint32_t ev)
{
	float now = Now();
	if (ev & BODYEV_LSTEP)
		AddEntityEvent(ent, 3);
	if (ev & BODYEV_FIRE)
		Fire(ent, ai);
	if ((ev & BODYEV_JUMP) && (move_flags & MF_JUMP_IMPULSE) && ent->groundentity && !(ent->flags & FL_FLY)) {
		float g = CvarValue(cv.sv_gravity);
		if (g <= 0)
			g = 800;
		float vz = jump_impulse[2];
		float T = 2.0f * vz / (0.1f * g);
		vec3_t h = {jump_impulse[0], jump_impulse[1], 0};
		float hl = VectorLength(h);
		if (T > 0 && T * hl > ai->jump_reach && hl > 0)
			VectorScale(h, (ai->jump_reach / T) / hl, h);
		ent->velocity[0] = h[0];
		ent->velocity[1] = h[1];
		ent->velocity[2] = vz;
		ent->groundentity = nullptr;
		landing_check = now + 0.1f * T;
	}
	if (ev & BODYEV_THUD) {
		static const char *falls[3] = {"impact/gore/fall1.wav", "impact/gore/fall2.wav", "impact/gore/fall3.wav"};
		gi.sound(ent, CHAN_BODY, gi.soundindex(falls[IRand(0, 2)]), 1, ATTN_NORM, 0);
	}
	if (ev & BODYEV_THROW)
		ai->body->Throw(ai->Target());
	if (ev & BODYEV_INAIR)
		ai->body->Pause(1, now);
}

/* A5 facing and turning */
void GroundAction::Turn(edict_t *ent, AI *ai)
{
	G3Body *b = ai->body;
	edict_t *sc = SightClient();
	if (ai->face_player && ent->health > 0 && sc) {
		ent->s.angles[YAW] = QuantiseAngle(YawTo(ent->s.origin, sc->s.origin));
		return;
	}
	vec3_t goal;
	bool have = false;
	if (!IsZero(facing)) {
		VectorCopy(facing, goal);
		goal[2] -= 20;
		have = true;
	} else if (!IsZero(destination) && HDist2(destination, ent->s.origin) > 0.01f) {
		VectorCopy(destination, goal);
		have = true;
	}
	float desired = ent->s.angles[YAW];
	float face_dist = 0;
	if (have) {
		desired = YawTo(ent->s.origin, goal);
		face_dist = sqrtf(Dist2(goal, ent->s.origin));
	}
	if (OwnerScriptLike() && record) {
		if (record->flags & ANIMF_BACKWARDS)
			desired += 180;
		else if (record->flags & ANIMF_STRAFE_LEFT)
			desired += 90;
		else if (record->flags & ANIMF_STRAFE_RIGHT)
			desired -= 90;
	}
	ai->death_angles[PITCH] = 0;
	ai->death_angles[YAW] = desired;
	ai->death_angles[ROLL] = 0;
	float left = turn_allowance, right = -turn_allowance;
	float rate = b ? b->rootYawRate : 0;
	if (b) {
		edict_t *t = ai->Target();
		b->YawLimits(turn_allowance, face_dist, t && t->client, &left, &right);
	}
	if (CvarValue(cv.ai_trace) >= 2)
		Dbg("ai %d turn: rec %s rate %f limits %.2f %.2f desired %.1f yaw %.1f\n", G_EdictNum(ent),
		    record ? record->name : "-", rate, left, right, desired, ent->s.angles[YAW]);
	float delta = AngleWrap(desired - ent->s.angles[YAW]);
	if (delta > left)
		delta = left;
	if (delta < right)
		delta = right;
	if (ent->health <= 0 && CvarValue(cv.ai_pathtest) && (delta > left || delta < right))
		Dbg("dead monster forcing turn at %3.3f degrees.\n", delta);
	ent->s.angles[YAW] = QuantiseAngle(ent->s.angles[YAW] + delta);
}

/* SPEC-AMEND-005 G2.4: one think of the jitter aim model (replaces A7.3's error scaling): the aim turns from its
 * previous angles towards a clamped, noisy goal; the 1/8 and 1/4 factors scale the turn */
static float YawLimitY(GroundAction *a, AI *ai)
{
	if (!ai->body || !a->record)
		return 1.0f;
	return (a->record->posture == POSTURE_CROUCH || a->record->posture == POSTURE_PRONE) ? 45.0f : 20.0f;
}

static void JitterTurn(float *angle, float goal, float k, float small, float mid, float noise)
{
	float d = AngleWrap(goal - *angle);
	if (d <= -180.0f)
		d += 360.0f;
	float f = fabsf(d) < small ? k / 8.0f : fabsf(d) < mid ? k / 4.0f : k;
	if (f < 1.0f)
		d *= f;
	*angle = *angle + d + noise;
}

void GroundAction::JitterStep(edict_t *ent, AI *ai, const vec3_t desired, float d, bool player, float Tc, float E)
{
	/* 1. turn factor k (L = 512 T-c) */
	float L = 512.0f * Tc, k;
	if (d < 92)
		k = player ? 2.0f : 10.0f;
	else if (d <= L && L > 92)
		k = player ? 2.0f - (d - 92) / (L - 92) : 10.0f - 10.0f * (d - 92) / (L - 92);
	else
		k = 1.0f;
	/* 2. yaw goal: the desired yaw clamped to +-Y around the owner's yaw, plus noise */
	float Y = YawLimitY(this, ai);
	float rel = AngleWrap(desired[YAW] - ent->s.angles[YAW]);
	if (rel > Y)
		rel = Y;
	if (rel < -Y)
		rel = -Y;
	float G = ent->s.angles[YAW] + rel + Crand() * 0.75f * E;
	/* 3. yaw step */
	JitterTurn(&ai->aim_angles[YAW], G, k, 30.0f, 60.0f, Crand() * 0.1f * E);
	/* 4. pitch */
	float P = desired[PITCH] + Crand() * 0.15f * E;
	JitterTurn(&ai->aim_angles[PITCH], P, k, 15.0f, 30.0f, Crand() * 0.05f * E);
	ai->aim_angles[ROLL] = 0;
	/* 5. normalised through the 16-bit angle quantisation */
	ai->aim_angles[YAW] = anglemod(ai->aim_angles[YAW]);
	ai->aim_angles[PITCH] = anglemod(ai->aim_angles[PITCH]);
}

/* A7 aiming */
void GroundAction::Aim(edict_t *ent, AI *ai)
{
	float now = Now();
	edict_t *t = ai->Target();
	if (!ai->alerted) {
		VectorCopy(ent->s.angles, ai->aim_angles);
		return;
	}
	vec3_t eye;
	EyePoint(ent, eye);
	if (ai->AlertedRecently(now) && t && t->client && t->inuse) {
		/* precise model */
		vec3_t P;
		edict_t *lp = PlayerLeaning(t) ? LeanProxy(t) : nullptr;
		VectorCopy(lp ? lp->s.origin : t->s.origin, P);
		int pmf = t->client->ps.pmove.pm_flags;
		if (pmf & PMF_DUCKED)
			P[2] -= 20;
		vec3_t u, f;
		VectorSubtract(P, ent->s.origin, u);
		float d = VectorNormalize(u);
		AngleVectors(ent->s.angles, f, nullptr, nullptr);
		float Td = Skill('d');
		float age = ReactionAge(ai) - Td * ai->profile[4];
		float p = 1.0f;
		vec3_t vh;
		VectorCopy(t->velocity, vh);
		float s = VectorNormalize(vh);
		float L = 1.0f - fabsf(DotProduct(f, vh));
		bool airborne = !(pmf & PMF_ON_GROUND);
		if (d >= 64) {
			float D = d >= ai->profile[1] && d > 0 ? (ai->profile[1] / d) * (ai->profile[1] / d) : 1.0f;
			float A = airborne ? 0.6f : 1.0f;
			float M = A * (1.0f - 0.6f * L * s / 225.0f);
			p = DotProduct(f, u) * ai->profile[3] * Skill('b') * D * M;
			float settle = Td * ai->profile[5];
			if (age < settle) {
				float k = settle > 0 ? age / settle : 1.0f;
				if (k < 0)
					k = 0;
				if (k > 1)
					k = 1;
				p *= k;
			}
			if (d < 128) {
				float w = (d - 64) / 64.0f;
				p = (1 - w) + w * p;
			}
		}
		vec3_t point;
		if (Frand() <= p) {
			point[0] = P[0] + FlRand(-16, 16);
			point[1] = P[1] + FlRand(-16, 16);
			point[2] = P[2] + FlRand(-24, 40);
			ent->flags &= ~FL_DELIBERATE_MISS;
		} else {
			ent->flags |= FL_DELIBERATE_MISS;
			vec3_t dir, ang, r, q;
			VectorSubtract(P, eye, dir);
			vectoangles(dir, ang);
			AngleVectors(ang, nullptr, r, q);
			if (airborne) {
				VectorMA(P, -FlRand(16, 32), q, point);
				VectorMA(point, FlRand(-8, 8), r, point);
			} else if (L * s / 225.0f > 0.1f) {
				float across = DotProduct(t->velocity, r) >= 0 ? 1.0f : -1.0f;
				VectorMA(P, -across * FlRand(16, 32), r, point);
				VectorMA(point, FlRand(-8, 8), q, point);
			} else {
				float sign = Frand() < 0.5f ? -1.0f : 1.0f;
				VectorMA(P, sign * FlRand(16, 32) + FlRand(-8, 8), r, point);
				VectorMA(point, FlRand(-8, 8), q, point);
			}
		}
		vec3_t dir;
		VectorSubtract(point, eye, dir);
		vectoangles(dir, ai->aim_angles);
	} else {
		/* jitter model towards the last known position */
		vec3_t pos;
		if (!ai->TargetPosition(pos)) {
			if (!t) {
				VectorCopy(ent->s.angles, ai->aim_angles);
				return;
			}
			/* a target the senses do not hold (scripted attacks): its body centre */
			BoxCenter(t, pos);
		}
		vec3_t dir, desired;
		VectorSubtract(pos, eye, dir);
		float d = VectorLength(dir);
		vectoangles(dir, desired);
		if (OwnerScriptLike()) {
			VectorCopy(desired, ai->aim_angles);
		} else {
			int near_count = 0;
			edict_t *e = nullptr;
			while ((e = findradius(e, ent->s.origin, 200)) != nullptr)
				if (e != ent && e->ai)
					near_count++;
			float c = 5.0f / (near_count ? near_count : 20);
			float Tc = Skill('c');
			float ed;
			if (d < 92)
				ed = (d / 92.0f) * (d / 92.0f);
			else if (d > 512 * Tc)
				ed = 1 + (d - 512 * Tc) / 256.0f;
			else
				ed = 1;
			if (!(t && t->client))
				ed *= 0.4f;
			float E = (1.0f - ai->profile[3]) * Skill('b') * c * ed;
			JitterStep(ent, ai, desired, d, t && t->client, Tc, E);
			return;
		}
	}
	/* yaw clamp around the owner's yaw */
	float Y = 20;
	if (record && (record->posture == POSTURE_CROUCH || record->posture == POSTURE_PRONE))
		Y = 45;
	float dy = AngleWrap(ai->aim_angles[YAW] - ent->s.angles[YAW]);
	if (dy > Y)
		dy = Y;
	if (dy < -Y)
		dy = -Y;
	ai->aim_angles[YAW] = ent->s.angles[YAW] + dy;
}

/* A6 movement */
bool GroundAction::Move(edict_t *ent, AI *ai)
{
	G3Body *b = ai->body;
	VectorClear(step);
	if (b)
		b->MoveDelta(move_scale, step);
	VectorScale(ai->velocity, 0.9f, ai->velocity);
	float s2 = DotProduct(step, step);
	if (s2 < 1.0f) {
		VectorClear(ai->velocity);
		return true;
	}
	if (!StepMove(ent, ai, step, move_flags)) {
		VectorClear(ai->velocity);
		return false;
	}
	VectorMA(ai->velocity, 0.1f, step, ai->velocity);
	return true;
}

bool GroundAction::Step(edict_t *ent, AI *ai, bool attack, bool dying)
{
	G3Body *b = ai->body;
	if (stopped || !b)
		return false;
	/* 1. face/voice, weapon charge */
	b->Think();
	/* 2. events */
	uint32_t ev = b->TakeEventFlags();
	if (attack && record && (record->flags & ANIMF_AIM) && b->ReadyToFire())
		ev |= BODYEV_FIRE;
	HandleEvents(ent, ai, ev);
	if (!ent->inuse || ent->ai != ai)
		return true;
	/* 3. bounding box */
	if (record)
		ai->ChangeBBox(ent, record->posture, dying);
	/* 4-5. facing and turning */
	Turn(ent, ai);
	/* 6. aim */
	Aim(ent, ai);
	/* 7. movement */
	step_ok = Move(ent, ai);
	return true;
}

/* ------------------------------------------------------------------------------------------------------------------
 * class rules (A8)
 * ---------------------------------------------------------------------------------------------------------------- */
class PlayAction : public GroundAction { /* classes 1, 11, 12 */
public:
	explicit PlayAction(int c) : GroundAction(c, c == 1 ? AK_PLAY : c == 11 ? AK_TIMED : AK_ATTACK) {}
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		if (!Step(ent, ai, false, false)) {
			*m = *r = 0;
			return true;
		}
		float now = Now();
		*m = *r = 1;
		if (cls == 1 && ends_with_anim && ai->body && (ai->body->AnimDone() || ai->body->pauseKind))
			return true;
		return now >= end_time;
	}
};

class PainAction : public GroundAction { /* class 2 */
public:
	PainAction() : GroundAction(2, AK_PAIN) {}
	void Start(edict_t *ent, AI *ai) override
	{
		if (stopped)
			return;
		VectorCopy(ent->s.origin, destination);
		if (ai->body && record)
			ai->body->SetAnim(record, !interrupted);
		start_time = Now();
		end_time = start_time + duration;
	}
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		*m = *r = 0;
		if (!Step(ent, ai, false, false))
			return true;
		if (ai->body && (ai->body->AnimDone() || ai->body->pauseKind))
			return true;
		return Now() >= end_time;
	}
};

class DyingAction : public GroundAction { /* classes 3, 4, 5 */
public:
	float death_start = 0;
	int notified = 0;
	/* SPEC-711 tail of classes 3-5: 112 death start, 116 notified, 120 requested death record */
	void SaveTail(SaveRec &r) override
	{
		r.F(112, death_start);
		r.B(116, notified);
		r.Anim(120, record);
	}
	explicit DyingAction(int c) : GroundAction(c, AK_DEATH) {}

	void MakeDead(edict_t *ent, AI *ai)
	{
		if (ent->health > 0) {
			ent->health = -1;
			ent->deadflag = DEAD_DEAD;
			ent->solid = SOLID_BBOX;
			ent->clipmask = 0x4000000;
			ent->svflags |= SVF_DEADMONSTER;
			ent->touch = nullptr;
			ai->dead = 1;
		}
	}

	void Start(edict_t *ent, AI *ai) override
	{
		death_start = Now();
		if (cls == 4)
			return;
		if (cls == 5) {
			MakeDead(ent, ai);
			const G3Anim *h = Anim("std_shead_2frnt_n_n");
			if (ai->body && h) {
				ai->body->SetAnim(h, true);
				/* SPEC-AMEND-005 G2.2: no speed change; the sequence starts 1.5 s into its first cycle, played
				 * once and held (instance method 17: offset 1.5, restart, mode 1) */
				ghoul::IGhoulInst *inst = ent->ghoulInst;
				ghoul::IGhoulObject *obj = inst ? inst->GetGhoulObject() : nullptr;
				if (obj && ai->body->modelDir) {
					char path[160];
					Com_sprintf(path, sizeof(path), "ghoul/%s/%s.ghl", ai->body->modelDir, h->name);
					ghoul::GhoulID seq = obj->FindSequence(path);
					if (seq)
						inst->Play(seq, Now(), 1.5f, true, 1, false, false);
				}
				ai->body->muted = true;
			}
			return;
		}
		if (stopped)
			return;
		MakeDead(ent, ai);
		if (record && !strcmp(record->name, "std_sblown_2bk_n_n") && ent->velocity[2] < 10) {
			vec3_t f;
			AngleVectors(ent->s.angles, f, nullptr, nullptr);
			ent->velocity[0] = -200 * f[0];
			ent->velocity[1] = -200 * f[1];
			ent->velocity[2] = 100;
			ent->maxs[2] -= 26;
			ent->s.origin[2] += 26;
			ent->groundentity = nullptr;
			gi.linkentity(ent);
		}
		PlayRecord(ent, ai, this, false);
		start_time = Now();
		end_time = start_time + duration;
	}

	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		*m = *r = 0;
		G3Body *b = ai->body;
		if (!b)
			return false;
		if (cls == 5) {
			b->Think();
			if (record)
				ai->ChangeBBox(ent, b->cur ? b->cur->posture : record->posture, true);
			return false;
		}
		if (ent->groundentity && b->pauseKind) {
			b->eventFlags |= BODYEV_THUD;
			b->Resume();
		}
		Step(ent, ai, false, true);
		if (!ent->inuse || ent->ai != ai)
			return false;
		if (b->AnimDone() && !notified)
			notified = 1;
		const G3Anim *d = b->ChooseDeath(ent->enemy, nullptr, 0, 0, nullptr);
		if (d) {
			if (d->cls != ANIMCLS_DEATH)
				gi.dprintf("Death action %s not really a death!\n", d->name);
			if (d != b->cur)
				b->SetAnim(d, false);
			record = d;
		}
		return false;
	}

	bool ReadyToRemove(edict_t *ent, AI *ai) override
	{
		(void)ai;
		bool armour = false;
		if (cls == 5 && ent->targetname) {
			edict_t *e = nullptr;
			while ((e = G_Find(e, FOFS(classname), "item_equip_armor")) != nullptr)
				if (e->target && !Q_stricmp(e->target, ent->targetname)) {
					armour = true;
					break;
				}
		}
		return Body_CorpseCheck(ent, armour);
	}
};

class EndScriptAction : public GroundAction { /* class 6 */
public:
	EndScriptAction() : GroundAction(6, AK_PLAY) {}
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override;
};

class MoveAction : public GroundAction { /* classes 7, 8 */
public:
	const G3Anim *straight = nullptr, *turn_left = nullptr, *turn_right = nullptr;
	/* SPEC-711 tail of class 8: the straight / turn-left / turn-right records */
	void SaveTail(SaveRec &r) override
	{
		if (cls != 8)
			return;
		r.Anim(112, straight);
		r.Anim(116, turn_left);
		r.Anim(120, turn_right);
	}
	vec3_t start_pos = {0, 0, 0};
	/* a move made for a scripted move command (SPEC-750 12.4): the start position and arrival radius of the script
	 * action, which may span several move actions (SetScriptMove) */
	bool script_move = false;
	vec3_t script_start = {0, 0, 0};
	float script_radius = 11.0f;
	explicit MoveAction(int c) : GroundAction(c, AK_MOVE) {}
	void Start(edict_t *ent, AI *ai) override
	{
		VectorCopy(ent->s.origin, start_pos);
		GroundAction::Start(ent, ai);
	}
	bool Scripted() { return script_move && (script_flags & 0x80000000); }
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		*m = *r = 0;
		if (Scripted() && ScriptMoveArrived(ent, destination, script_start, script_radius)) {
			*m = *r = 1;
			return true;
		}
		G3Body *b = ai->body;
		if (cls == 8 && b && straight) {
			float d = AngleWrap(ai->death_angles[YAW] - ent->s.angles[YAW]);
			const G3Anim *want = d > 1 ? turn_left : d < -1 ? turn_right : straight;
			if (want && want != record) {
				record = want;
				b->SetAnim(want, true);
			}
		}
		if (!Step(ent, ai, false, false))
			return true;
		bool cycle = record && (record->flags & ANIMF_FINISH_CYCLE) && b && !b->AnimDone();
		float now = Now();
		if (!step_ok && CvarValue(cv.ai_trace) >= 2)
			Dbg("ai %d move blocked: step %.1f %.1f %.1f ground %d vel2 %.1f\n", G_EdictNum(ent), step[0], step[1],
			    step[2], ent->groundentity ? G_EdictNum(ent->groundentity) : -1, DotProduct(ent->velocity, ent->velocity));
		if (!step_ok)
			return !cycle;
		if (!ends_with_anim) {
			if (now > end_time)
				return !cycle;
			/* a scripted move steers towards a point at most 0.2 x speed ahead (SPEC-750 12.4), so this "about three
			 * steps" arrival would end every one of its move actions after one think and the scripted decision
			 * would make the next one; the script action's own arrival rule (above) is what ends the move */
			if (Scripted())
				return false;
			float lim = 9.0f * (DotProduct(step, step) + 1.0f);
			if (HDist2(destination, ent->s.origin) <= lim && !cycle) {
				*m = *r = 1;
				return true;
			}
			return false;
		}
		if (b && b->AnimDone() && (b->pauseKind || now > end_time))
			return true;
		return false;
	}
};

class JumpAction : public GroundAction { /* class 9 */
public:
	vec3_t start_velocity = {0, 0, 0};
	void SaveTail(SaveRec &r) override { r.V(112, start_velocity); } /* SPEC-711 class 9 tail */
	JumpAction() : GroundAction(9, AK_JUMP) {}
	void Start(edict_t *ent, AI *ai) override
	{
		move_flags = MF_JUMP_IMPULSE | MF_FULL_STEP;
		VectorCopy(ent->velocity, start_velocity);
		start_velocity[2] = 0;
		vec3_t D;
		VectorCopy(destination, D);
		if (IsZero(D) || HDist2(D, ent->s.origin) < 0.01f) {
			vec3_t f;
			AngleVectors(ent->s.angles, f, nullptr, nullptr);
			VectorMA(ent->s.origin, 64, f, D);
			VectorCopy(D, destination);
		}
		float g = CvarValue(cv.sv_gravity);
		if (g <= 0)
			g = 800;
		float H = ai->jump_height;
		float zt = D[2] - ent->mins[2];
		float x = HDist(D, ent->s.origin);
		if (x > 100)
			x = 100;
		float a = 0.1f * g < H ? 0.1f * g : H;
		float v0 = sqrtf(2 * H * g) + a, T0 = v0 / (0.1f * g);
		float oz = ent->s.origin[2];
		float vz, T;
		if (H + oz - zt <= 0) {
			vz = v0;
			T = T0;
		} else if (zt <= oz) {
			vz = v0;
			T = (sqrtf(2 * (H + oz - zt) * g) + a) / (0.1f * g) + T0;
		} else {
			vz = 2 * a + sqrtf(2 * (zt - oz) * g);
			T = vz / (0.1f * g);
		}
		vec3_t dir;
		VectorSubtract(D, ent->s.origin, dir);
		dir[2] = 0;
		VectorNormalize(dir);
		float hs = T > 0 ? x * 10 / T : 0;
		jump_impulse[0] = dir[0] * hs;
		jump_impulse[1] = dir[1] * hs;
		jump_impulse[2] = vz;
		start_time = Now();
		end_time = start_time + duration;
		landing_check = start_time + 999.0f;
		if (ai->body && record) {
			ai->body->Resume();
			ai->body->SetAnim(record, true);
		}
	}
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		*m = *r = 0;
		/* a scripted jump "completes as soon as started" for the script only (SPEC-750 12.4: its AI-done signal is
		 * reported when it is planned, ai_script.cpp); the jump itself plays out, as in the original */
		if (!Step(ent, ai, false, false))
			return true;
		float now = Now();
		if (!ent->groundentity) {
			if (ent->velocity[2] > 0 && now < landing_check) {
				float left = landing_check - now;
				if (left > 0.05f) {
					vec3_t d;
					VectorSubtract(destination, ent->s.origin, d);
					d[2] = 0;
					VectorScale(d, 1.0f / left, d);
					float s2 = DotProduct(d, d);
					if (s2 > 1.6e7f)
						VectorScale(d, 4000.0f / sqrtf(s2), d);
					ent->velocity[0] = d[0];
					ent->velocity[1] = d[1];
				}
			}
			return false;
		}
		if ((ai->body && ai->body->AnimDone()) || now > landing_check + 3) {
			VectorClear(ent->velocity);
			*m = HDist(destination, ent->s.origin) <= 30 ? 1.0f : 0.0f;
			*r = 1;
			return true;
		}
		return false;
	}
};

class FallAction : public GroundAction { /* class 10 */
public:
	int left_ground = 0;
	void SaveTail(SaveRec &r) override /* SPEC-711 class 10 tail: 112 landed state, 116 the fall record */
	{
		r.I(112, left_ground);
		r.Anim(116, record);
	}
	FallAction() : GroundAction(10, AK_FALL) {}
	void Start(edict_t *ent, AI *ai) override
	{
		left_ground = 0; /* also when the previous fall action is reused (SPEC-700 rule 8) */
		GroundAction::Start(ent, ai);
	}
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		*m = *r = 0;
		G3Body *b = ai->body;
		/* a scripted fall: as the jump above (black-box: kos1 `std_jdrop` drops 190 units over 1.8 s in the original
		 * while the script has gone on) */
		if (!Step(ent, ai, false, false))
			return true;
		if (!left_ground) {
			if (ent->groundentity) {
				vec3_t d;
				if (!IsZero(destination)) {
					VectorSubtract(destination, ent->s.origin, d);
					d[2] = 0;
				} else
					AngleVectors(ent->s.angles, d, nullptr, nullptr);
				VectorNormalize(d);
				VectorScale(d, 20, d);
				if (!StepMove(ent, ai, d, move_flags))
					StepMove(ent, ai, d, move_flags | MF_STEP_OFF);
			}
			if (!ent->groundentity) {
				left_ground = 1;
				if (b)
					b->Pause(1, Now());
			}
			return false;
		}
		if (ent->groundentity) {
			if (b && b->pauseKind)
				b->Resume();
			if (!b || b->AnimDone()) {
				VectorClear(ent->velocity);
				float feet = ent->s.origin[2] + ent->mins[2];
				float ok = (!IsZero(destination) && feet - destination[2] > 10) ? 0.0f : 1.0f;
				*m = *r = ok;
				return true;
			}
		}
		return false;
	}
};

class AttackAction : public GroundAction { /* classes 13, 14 */
public:
	int target_health = 0;
	int leap = 0;
	void SaveTail(SaveRec &r) override /* SPEC-711 classes 13/14: target health, creation time, leap state */
	{
		r.I(112, target_health);
		r.F(116, created);
		if (cls == 14)
			r.I(120, leap);
	}
	explicit AttackAction(int c) : GroundAction(c, AK_ATTACK) {}
	void Start(edict_t *ent, AI *ai) override
	{
		if (stopped)
			return;
		if ((lethal || target_bolt) && target)
			ai->target = target;
		if (lethal && target && target->health > 1)
			target->health = 1;
		target_health = target ? target->health : 0;
		PlayRecord(ent, ai, this, true);
		start_time = Now();
		end_time = start_time + duration;
	}
	bool Think(edict_t *ent, AI *ai, float *m, float *r) override
	{
		/* an attack on a given target turns towards where the target is now (the facing point given at creation
		 * is the target's position when the action was made) */
		if (cls == 13 && target && target->inuse && target->health > 0)
			VectorCopy(target->s.origin, facing);
		if (!Step(ent, ai, true, false)) {
			*m = *r = 0;
			return true;
		}
		G3Body *b = ai->body;
		if (cls == 14 && target && leap == 1) {
			vec3_t c;
			BoxCenter(target, c);
			VectorCopy(c, facing);
		}
		bool alive = target && target->inuse && target->health > 0;
		*m = step_ok ? 1.0f : 0.0f;
		*r = alive ? 1.0f : 0.0f;
		bool anim_done = !b || b->AnimDone() || b->pauseKind;
		bool end_ok = !ends_with_anim || anim_done;
		bool dead_or_freed = target && (!target->inuse || target->health <= 0);
		bool c1 = !lethal || (target && target->health <= 0);
		bool c2 = dead_or_freed || (Now() >= end_time && end_ok);
		bool c3 = !target || alive || anim_done;
		return c1 && c2 && c3;
	}
};

class InertAction : public Action { /* class 0, invalid codes and vehicle classes without G4 */
public:
	int code;
	explicit InertAction(int c) : code(c) {}
	int ClassCode() override { return code; }
	int Kind() override { return 0; }
	bool Think(edict_t *, AI *, float *m, float *r) override
	{
		*m = *r = 1;
		return true;
	}
};

Action *NewAction(int cls)
{
	switch (cls) {
	case 1: case 11: case 12: return new PlayAction(cls);
	case 2: return new PainAction();
	case 3: case 4: case 5: return new DyingAction(cls);
	case 6: return new EndScriptAction();
	case 7: case 8: return new MoveAction(cls);
	case 9: return new JumpAction();
	case 10: return new FallAction();
	case 13: case 14: return new AttackAction(cls);
	case 0:
		gi.dprintf("ERROR: only allowed to use leaves of action class tree: %d\n", cls);
		return new InertAction(0);
	default:
		if (cls < 15 || cls > 21)
			gi.dprintf("ERROR: invalid action class code: %d\n", cls);
		return new InertAction(cls);
	}
}

/* SPEC-750 12.4: a scripted move is complete within the arrival radius (11 units, or the widened radius after a
 * failed step, SPEC-710 C13) horizontally, or once passed (moving away from the destination relative to the
 * direction from the script action's start position) */
bool ScriptMoveArrived(edict_t *ent, const vec3_t dest, const vec3_t start, float radius)
{
	if (HDist2(dest, ent->s.origin) <= radius * radius)
		return true;
	vec3_t a, b;
	VectorSubtract(dest, start, a);
	VectorSubtract(dest, ent->s.origin, b);
	a[2] = b[2] = 0;
	return DotProduct(a, b) < 0;
}

void SetScriptMove(Action *a, const vec3_t start, float radius)
{
	if (!a || (a->ClassCode() != 7 && a->ClassCode() != 8))
		return;
	MoveAction *m = static_cast<MoveAction *>(a);
	m->script_move = true;
	VectorCopy(start, m->script_start);
	m->script_radius = radius;
}

Action *MakeAction(AI *ai, int cls, Decision *owner, Action *linked, const G3Anim *rec, const vec3_t dest,
                   const vec3_t face, float duration, bool ends_with_anim)
{
	Action *a = nullptr;
	if (ai->previous && ai->previous->ClassCode() == cls && ai->previous != ai->current) {
		a = ai->previous;
		ai->previous = nullptr;
	} else
		a = NewAction(cls);
	a->Init(owner, linked, rec, dest, face, duration, ends_with_anim);
	if (cls == 7 || cls == 8)
		static_cast<MoveAction *>(a)->script_move = false;
	return a;
}

/* class 6: end of script control */
void SetScriptedFinished(Decision *d);
bool EndScriptAction::Think(edict_t *ent, AI *ai, float *m, float *r)
{
	ai->AddDeferredSenses(ent);
	if (owner && owner->Kind() == DK_SCRIPTED)
		SetScriptedFinished(owner);
	*m = *r = 1;
	return true;
}

} /* namespace om_ai */
