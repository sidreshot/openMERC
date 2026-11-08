/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-710 C12 (path follow, kind 7; combat points, kind 8), SPEC-760 B8.1 (path_corner and
 * point_combat animation keys waitaction1..6 / moveaction resolved against the character animation table)
 * Owner: package G2.
 */
#include "ai_decisions.h"

namespace om_ai {

/* SPEC-760 B8.1: called by SP_path_corner / SP_point_combat with the spawn keys still in `st` */
void PathCornerSpawn(edict_t *self)
{
	for (int i = 0; i < 6; i++) {
		self->ai_waitaction[i] = nullptr;
		const char *n = st.waitaction[i];
		if (!n || !n[0])
			continue;
		const G3Anim *a = Anim_Human(n);
		if (!a) {
			gi.dprintf("couldn't find path_corner waitaction: %s (using stand)\n", n);
			a = Anim_Human("std_istand_n_a_n");
		}
		self->ai_waitaction[i] = a;
	}
	self->ai_moveaction = nullptr;
	if (st.moveaction && st.moveaction[0]) {
		const G3Anim *a = Anim_Human(st.moveaction);
		if (!a) {
			gi.dprintf("couldn't find path_corner moveaction: %s (using walk)\n", st.moveaction);
			a = Anim_Human("std_wnormal_n_pk_a");
		}
		self->ai_moveaction = a;
	}
}

class PathDecision : public CombatDecision {
public:
	edict_t *corner = nullptr;   /* current corner (the one moved to or waited at) */
	edict_t *left = nullptr;     /* openMERC: the corner left for it (its moveaction leads the way on) */
	float wait_end = 0;
	int wait_index = 0;   /* 1-based index of the waitaction requested (SPEC-AMEND-006 G2.12) */
	int first_plan = 0;   /* set on arrival: the first wait plan never advances the index */
	int list_played = 0;  /* spawnflag 2: past the last waitaction, the wait ends after this plan's action */
	int arrived = 0;
	int waiting = 0;
	int ended = 0;

	PathDecision(int k, edict_t *first) : CombatDecision(k), corner(first) {}

	/* SPEC-710 kinds 7/8 tail (AIHD/AIPD 184); the waitaction being played is derived again from the index */
	void SaveTail(SaveRec &r) override
	{
		CombatDecision::SaveTail(r);
		r.I(0, ended); /* OpenMerc: path finished */
		r.F(160, wait_end);
		r.Ent(164, corner);
		r.I(168, wait_index);
		r.I(172, first_plan); /* SPEC-710 "1 after arrival": the first-wait-plan mark (SPEC-AMEND-006 G2.12) */
		r.I(176, waiting);
		const G3Anim *mv = corner ? corner->ai_moveaction : nullptr;
		r.Anim(180, mv);
		if (r.reading) {
			arrived = waiting;
			list_played = 0;
		}
	}

	bool Evaluate(edict_t *ent, AI *ai, int *priority) override
	{
		UpdateTarget(ent, ai);
		if (ended || !corner || !corner->inuse)
			return false;
		if (kind == DK_PATH) {
			if (ai->alerted && Now() - ai->alert_time <= 1.0f)
				return false;
			bonus = 1;
		} else
			bonus = ai->alerted ? 5 : 0;
		Relax(this);
		*priority = base_priority + bonus;
		return true;
	}

	void Arrive(edict_t *ent, AI *ai)
	{
		arrived = 1;
		if (CvarValue(cv.ai_dumb))
			PlayEffect(ent, "edit/ignore");
		if (CvarValue(cv.ai_pathtest))
			gi.dprintf("Finishing pathpoint: time %f\n", Now());
		if (corner->spawnflags & 4) {
			ent->spawnflags |= SF_STATIONARY;
			ent->flags |= FL_NO_KNOCKBACK;
			ai->face_player = 1;
		}
		float w = corner->wait;
		bool unlimited = w == -1 || (corner->spawnflags & 2);
		if (!unlimited && w > -0.1f && w < 0.1f) {
			Leave(ent, ai);
			return;
		}
		waiting = 1;
		wait_index = 1;
		first_plan = 1;
		list_played = 0;
		wait_end = unlimited ? 1e9f : Now() + w;
		if (CvarValue(cv.ai_pathtest))
			gi.dprintf("waiting: time %f\n", Now());
	}

	/* openMERC: the 50-unit arrival is taken only when the way on to the next point is free; otherwise the corner is
	   walked to (16 units). arm3: Sam, pushed aside at the door she had just opened, counted sam16 as reached at the
	   jamb and walked into it on the diagonal to sam17 */
	/* the character's box (above step height) can move straight from a to p */
	static bool StraightOpenFrom(edict_t *ent, const vec3_t a, const vec3_t p)
	{
		vec3_t mn, end;
		VectorCopy(ent->mins, mn);
		mn[2] += 18; /* steps */
		VectorCopy(p, end);
		end[2] = a[2];
		trace_t tr = gi.trace(a, mn, ent->maxs, end, ent, MASK_MONSTERSOLID);
		return tr.fraction >= 1.0f && !tr.startsolid;
	}

	static bool StraightOpen(edict_t *ent, const vec3_t p) { return StraightOpenFrom(ent, ent->s.origin, p); }

	/* openMERC: a straight line into a door jamb or a corner is replaced by a side step from where the goal is in
	   sight (arm3: Sam, pushed back by her door-opening animation, aimed past the jamb at sam17) */
	static void AroundCorner(edict_t *ent, vec3_t pt)
	{
		static const float offs[6] = {16, -16, 32, -32, 48, -48};
		vec3_t d, perp, c;
		if (StraightOpen(ent, pt))
			return;
		VectorSubtract(pt, ent->s.origin, d);
		d[2] = 0;
		if (VectorNormalize(d) == 0)
			return;
		VectorSet(perp, -d[1], d[0], 0);
		for (int i = 0; i < 6; i++) {
			VectorMA(ent->s.origin, offs[i], perp, c);
			if (StraightOpen(ent, c) && StraightOpenFrom(ent, c, pt)) {
				VectorCopy(c, pt);
				return;
			}
		}
	}

	bool NextOpen(edict_t *ent)
	{
		edict_t *next = corner->target ? G_PickTarget(corner->target) : nullptr;
		if (!next || next == corner)
			return true;
		return StraightOpen(ent, next->s.origin);
	}

	void Leave(edict_t *ent, AI *ai)
	{
		waiting = 0;
		arrived = 0;
		if (corner->pathtarget) {
			Percept p = ai->NewestPlayer();
			edict_t *act = p.ent ? p.ent : ent;
			UseTargetsNamed(corner->pathtarget, act, act);
		}
		edict_t *next = corner->target ? G_PickTarget(corner->target) : nullptr;
		if (CvarValue(cv.ai_pathtest))
			gi.dprintf("%d getting next point: old--%s, looking for %s!\n", G_EdictNum(ent),
			           corner->targetname ? corner->targetname : "", corner->target ? corner->target : "");
		if (!next || next == corner) {
			if (corner->spawnflags & 1) {
				ent->spawnflags |= SF_STATIONARY;
				ent->flags |= FL_NO_KNOCKBACK;
			}
			ended = 1;
			return;
		}
		left = corner;
		corner = next;
	}

	const G3Anim *Checked(edict_t *ent, AI *ai, const G3Anim *a, const G3Anim *fallback)
	{
		if (!ai->body) {
			gi.dprintf("path-traversal: no body!\n");
			return nullptr;
		}
		if (a && ai->body->HasAnim(a))
			return a;
		if (a)
			gi.dprintf("path-traversal: %s not available for %s %s, using %s!\n", a->name, ent->classname,
			           ent->targetname ? ent->targetname : "", fallback ? fallback->name : "none");
		return fallback;
	}

	void Plan(edict_t *ent, AI *ai) override
	{
		if (kind == DK_POINT && target && target->client && Enemy(ent, ai, target))
			ai->Alert(ent, target_time, target, target_pos);
		if (PendingExecutable(ent) && ExecutePending(ent, ai))
			return;
		if (!waiting && !arrived && HDist2(corner->s.origin, ent->s.origin) <= 50.0f * 50.0f &&
		    (HDist2(corner->s.origin, ent->s.origin) <= 16.0f * 16.0f || NextOpen(ent)))
			Arrive(ent, ai);
		if (ended)
			return;
		if (waiting) {
			float now = Now();
			if (now >= wait_end) {
				Leave(ent, ai);
				if (ended)
					return;
			} else {
				const G3Anim *a = nullptr;
				/* SPEC-AMEND-006 G2.12: every think plans the waitaction at the index again (a play-in-place
				 * action of one think, SPEC-AMEND-005 G2.9; re-requesting the playing record does not restart
				 * it). The index advances when the first-plan mark is clear and the body's finished flag (EOS of
				 * the playing sequence, cleared by every new playback) is set; past the last set waitaction it
				 * returns to 1, or with corner spawnflag 2 it stays, waitaction1 is requested for this one plan
				 * and the wait ends once that action has completed. */
				if (list_played) {
					wait_end = now - 1;
					Leave(ent, ai);
					if (ended)
						return;
					goto move;
				}
				if (!first_plan && ai->body && ai->body->finished)
					wait_index++;
				first_plan = 0;
				if (wait_index < 1 || wait_index > 6 || !corner->ai_waitaction[wait_index - 1]) {
					if (corner->spawnflags & 2) {
						list_played = 1;
						a = corner->ai_waitaction[0];
					} else
						wait_index = 1;
				}
				if (!list_played)
					a = corner->ai_waitaction[wait_index - 1];
				vec3_t f, face;
				vec3_t ang = {0, corner->s.angles[YAW], 0};
				AngleVectors(ang, f, nullptr, nullptr);
				VectorMA(ent->s.origin, 1000, f, face);
				if (a) {
					const G3Anim *u = Checked(ent, ai, a, nullptr);
					if (u) {
						QueueRecord(ent, ai, this, u, ent->s.origin, face, nullptr);
						return;
					}
				}
				/* no waitaction: a combat point's holder with a valid target stands aiming at it (the body's stand
				 * choice with a target: `std_a_fwd_*`), without firing (a stand action is no attack). Black box,
				 * tsr2 flythrough `fly4/fly5_*_tsr2`: `runner01/02` (wait -1, no waitactions) aim `std_a_fwd_ms_2`
				 * from their arrival (32.8 s) for 49 s, then `std_ialert_n_mrs_2`; OpenMerc played the plain idles
				 * `std_istand/istretch/ilooking_n_mrs_2` facing the point's angle */
				if (kind == DK_POINT && target && Valid(ent, ai)) {
					PlanStand(ent, ai, target->s.origin, target, false, nullptr);
					return;
				}
				PlanStand(ent, ai, face, nullptr, false, nullptr);
				return;
			}
		}
	move:
		arrived = 0;
		{
			/* a short step goal towards the corner (through the navigation data when there is any), so that the
			 * 0.5 s move action arrives instead of timing out (a time-out reports m = 0 and runs C9) */
			vec3_t pt, dir, goal, face;
			/* path corners (kind 7) are walked to in a straight line (SPEC-710 C12 "towards the current corner";
			 * black-box: irq1a `introknocker` walks t390 -> t391 -> t392 straight in the original, while the
			 * navigation approach point sent it away east); combat points keep the navigation query */
			/* openMERC: a combat point in plain sight is walked to straight as well; the navigation approach point
			   led arm3's Sam from the door she had opened into its jamb */
			if (kind == DK_PATH || StraightOpen(ent, corner->s.origin) ||
			    !NavPoint(ent, ai, corner->s.origin, NAV_APPROACH, pt))
				VectorCopy(corner->s.origin, pt);
			AroundCorner(ent, pt);
			VectorSubtract(pt, ent->s.origin, dir);
			dir[2] = 0;
			float len = VectorNormalize(dir);
			if (len < 1) {
				VectorSubtract(corner->s.origin, ent->s.origin, dir);
				dir[2] = 0;
				len = VectorNormalize(dir);
			}
			VectorMA(ent->s.origin, len < 10 ? len : 10, dir, goal);
			VectorMA(ent->s.origin, 500, dir, face);
			const G3Anim *a = nullptr;
			/* the moveaction of the corner just left leads to the next one (black box, arm1 `toiletguy`: toilet1's
			   std_wnormal_n_pk_a carries him to toilet2, which has none), else the corner's own */
			const G3Anim *mv = (left && left->inuse && left->ai_moveaction) ? left->ai_moveaction : corner->ai_moveaction;
			if (mv)
				a = Checked(ent, ai, mv, nullptr);
			if (a) {
				Action *act = MakeAction(ai, 7, this, nullptr, a, goal, face, 0.5f, false);
				ai->QueueBack(act);
			} else {
				/* SPEC-710 C12: without the corner's moveaction the body's normal walk - but an alerted fighter runs to
				   its combat point. sv_animlog A/B: tsr1 `camdoorkicker` (skinhead, point_combat t212 without a
				   moveaction) runs at ~210 u/s in the original module and walked at ~50 in B, so the door was kicked
				   7 s late while the other two stood aiming at it; arm1 `toiletguy` (tourist) walks at 52 in both */
				TypeInfo ti;
				bool fighter = !(GetTypeInfo(ai->type_code, &ti) && (ti.innocent || ti.family == FAMILY_CIVILIAN));
				PlanMove(ent, ai, goal, face, kind == DK_POINT && ai->alerted && fighter, false, false);
			}
		}
	}
};

Decision *NewPathDecision(int kind, edict_t *first) { return new PathDecision(kind, first); }

} /* namespace om_ai */
