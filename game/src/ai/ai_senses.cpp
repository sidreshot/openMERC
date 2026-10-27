/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-701 (senses 0-6, records, muting, sight, hearing, proximity, omniscience)
 * Owner: package G2.
 */
#include "ai_local.h"

#include <stdio.h>

namespace om_ai {

static float PerceivedD2(float d2, float mute) { return d2 * (1.0f + mute) * (1.0f + mute); }

/* ------------------------------------------------------------------------------------------------------------------
 * common sense state: records and muting (SPEC-701 section 4)
 * ---------------------------------------------------------------------------------------------------------------- */
class SenseBase : public Sense {
public:
	int code;
	int mute_mode = 1;
	float mute_start = 0, mute_end = 0, mute_level = 0;
	bool mutable_ = false;     /* sight and hearing answer mute requests */
	float mute_delay = 0;      /* hearing: 0.3 s */

	explicit SenseBase(int c) : code(c) { created = Now(); }
	int ClassCode() override { return code; }
	/* SPEC-701 savegame chunk: mute state (68-83); the class parts follow in the subclasses */
	void SaveTail(SaveRec &r) override
	{
		r.I(68, mute_mode);
		r.F(72, mute_start);
		r.F(76, mute_end);
		r.F(80, mute_level);
	}
	void Mute(float now, float level, int mode, float duration) override
	{
		if (!mutable_)
			return;
		mute_mode = mode;
		mute_level = level;
		mute_start = now + mute_delay;
		mute_end = now + duration + mute_delay;
	}
	float MuteLevel(float now) override
	{
		if (!mutable_)
			return 0;
		int m = mute_mode;
		if (m == 0)
			return mute_level;
		if (!(mute_start < now && now < mute_end))
			return 0;
		float span = mute_end - mute_start;
		switch (m) {
		case 1: return mute_level;
		case 2: return span > 0 ? mute_level * (mute_end - now) / span : 0;
		case 3: {
			float f = span > 0 ? (mute_end - now) / span : 0;
			return mute_level * f * f;
		}
		default:
			gi.dprintf("unknown sense muting recovery code: %d!\n", m);
			return mute_level;
		}
	}
};

/* inert senses: codes 0 and 1, removed at their first update */
class InertSense : public SenseBase {
public:
	explicit InertSense(int c) : SenseBase(c) {}
	int Mask() override { return code == 1 ? 1 : 7; }
	bool Update(edict_t *, AI *, int mask) override { return !(mask & Mask()); }
};

/* ------------------------------------------------------------------------------------------------------------------
 * the filter of SPEC-701 section 5 rule 1 (without the range)
 * ---------------------------------------------------------------------------------------------------------------- */
bool PerceiveFilter(edict_t *ent, AI *ai, edict_t *t)
{
	if (!t || !t->inuse || t == ent)
		return false;
	if (t->client) {
		if (t->flags & FL_NOTARGET)
			return false;
		if (SameTeam(ent, t) && !ai->perceive_teammates)
			return false;
	} else if (!t->ai)
		return false;
	if ((t->spawnflags & SF_HOSTAGE) && !t->client && (ai->spawner_made || (ent->flags & FL_SPAWNER_MADE)))
		return false;
	if (ClassnameIs(t, "m_x_mmerc"))
		return false;
	return true;
}

/* section 5 rule 2: the view cone */
bool ConeTest(edict_t *ent, AI *ai, edict_t *t, bool no_ai)
{
	float threshold = 0.75f;
	if (ai && !no_ai) {
		switch (ai->view_mode) {
		case 0: threshold = 0.85f; break;
		case 2: threshold = -0.2f; break;
		default: threshold = 0.75f; break;
		}
	}
	float s2;
	if (t->client)
		s2 = DotProduct(t->velocity, t->velocity);
	else if (t->ai)
		s2 = DotProduct(t->ai->velocity, t->ai->velocity);
	else
		s2 = DotProduct(t->velocity, t->velocity);
	if (ent->spawnflags & SF_INATTENTIVE) {
		s2 *= 0.01f;
		threshold += 0.225f;
	}
	if (s2 > 10000.0f)
		threshold -= 0.25f;
	if (s2 > 40000.0f)
		threshold -= 0.25f;
	vec3_t f, d;
	AngleVectors(ai ? ai->look_angles : ent->s.angles, f, nullptr, nullptr);
	VectorSubtract(t->s.origin, ent->s.origin, d);
	if (VectorNormalize(d) <= 0)
		return true;
	return DotProduct(f, d) > threshold;
}

/* ------------------------------------------------------------------------------------------------------------------
 * sight (codes 2, 3)
 * ---------------------------------------------------------------------------------------------------------------- */
class SightSense : public SenseBase {
public:
	float roll_time = 0;
	explicit SightSense(int c) : SenseBase(c) { mutable_ = true; }
	void SaveTail(SaveRec &r) override
	{
		SenseBase::SaveTail(r);
		r.F(84, roll_time);
	}
	int Mask() override { return 2; }

	void Refresh(Percept &r, edict_t *t, float window, float now)
	{
		(void)now;
		VectorCopy(t->s.origin, r.pos);
		r.kind = PK_TRACKED;
		(void)window;
	}

	void NotSeen(edict_t *t, float window, float now)
	{
		if (player.ent == t && now - player.time < window)
			Refresh(player, t, window, now);
		else if (character.ent == t && now - character.time < window)
			Refresh(character, t, window, now);
	}

	void Evaluate(edict_t *ent, AI *ai, edict_t *t)
	{
		float now = Now();
		if (!PerceiveFilter(ent, ai, t))
			return;
		float mute = MuteLevel(now);
		float d2 = Dist2(ent->s.origin, t->s.origin);
		int rc = RangeClass(PerceivedD2(d2, mute));
		if (rc == RANGE_FAR)
			return;
		/* rule 2 */
		bool in_view = rc == RANGE_MELEE;
		if (!in_view) {
			bool safe = (t->svflags & SVF_SAFEZONE) && !ai->alerted;
			in_view = !safe && ConeTest(ent, ai, t);
		}
		if (!in_view) {
			NotSeen(t, 4.0f, now);
			return;
		}
		/* rule 4: perception roll */
		bool roll;
		if (roll_time > 0 && now - roll_time < 4.0f)
			roll = true;
		else {
			float light = t->client ? (float)t->client->lightlevel : 0.0f;
			float B;
			if (d2 < 192.0f)
				B = 2.5f;
			else if (d2 <= 512.0f)
				B = (1.0f - (d2 - 36864.0f) / 225280.0f) * 0.8f * 2.5f + 0.5f;
			else
				B = 0.5f;
			float p = B * (light / 128.0f * 0.75f + 0.25f);
			if (!ai->alerted)
				p *= 0.1f;
			roll = Frand() < p;
			if (roll)
				roll_time = now;
			if (!ai->alerted && light < 64)
				roll = false;
		}
		/* rule 5: line of sight */
		bool leaning = t->client && PlayerLeaning(t);
		bool vis = Visible(ent, t, false);
		vec3_t leanpt;
		edict_t *lp = leaning ? LeanProxy(t) : nullptr;
		if (!vis && roll && lp) {
			vec3_t a;
			EyePoint(ent, a);
			VectorCopy(lp->s.origin, leanpt);
			leanpt[2] += 16;
			trace_t tr = Trace(a, nullptr, nullptr, leanpt, ent, 0x19);
			vis = tr.fraction == 1.0f;
		}
		if (vis) {
			Percept &r = t->client ? player : character;
			r.ent = t;
			VectorCopy(t->s.origin, r.pos);
			if (t->client && lp) {
				r.pos[0] = lp->s.origin[0];
				r.pos[1] = lp->s.origin[1];
			}
			r.time = now;
			r.kind = PK_SEEN;
		} else
			NotSeen(t, 0.25f, now);
	}

	bool Update(edict_t *ent, AI *ai, int mask) override
	{
		if (!(mask & Mask()))
			return true;
		Evaluate(ent, ai, SightClient());
		Evaluate(ent, ai, SightCharacter());
		return true;
	}
};

/* ------------------------------------------------------------------------------------------------------------------
 * hearing (code 4)
 * ---------------------------------------------------------------------------------------------------------------- */
struct HeardEvent {
	vec3_t pos;
	vec3_t emitter_origin;
	edict_t *emitter;
	int kind;
	float time;
};

class HearingSense : public SenseBase {
public:
	HeardEvent events[10];
	int count = 0;
	HearingSense() : SenseBase(4)
	{
		mutable_ = true;
		mute_delay = 0.3f;
	}
	int Mask() override { return 4; }
	void SaveTail(SaveRec &r) override
	{
		SenseBase::SaveTail(r);
		if (!r.reading)
			for (int i = count; i < 10; i++)
				memset(&events[i], 0, sizeof(events[i])); /* unused slots are written as zeros */
		for (int i = 0; i < 10; i++) {
			int o = 84 + 36 * i;
			r.V(o, events[i].pos);
			r.V(o + 12, events[i].emitter_origin);
			r.Ent(o + 24, events[i].emitter, 0);
			r.I(o + 28, events[i].kind);
			r.F(o + 32, events[i].time);
		}
		r.I(444, count);
		if (r.reading && (count < 0 || count > 10))
			count = 0;
	}
	void Hear(const vec3_t pos, edict_t *emitter, const vec3_t eo, int kind, float now) override
	{
		if (count >= 10)
			return;
		HeardEvent &e = events[count++];
		VectorCopy(pos, e.pos);
		VectorCopy(eo, e.emitter_origin);
		e.emitter = emitter;
		e.kind = kind;
		e.time = now;
	}

	static void Write(Percept &r, edict_t *em, const vec3_t org, float t, int kind)
	{
		r.ent = em;
		VectorCopy(org, r.pos);
		r.time = t;
		r.kind = kind;
	}

	void Process(edict_t *ent, const HeardEvent &e, float now)
	{
		edict_t *em = e.emitter;
		if (em && (em->flags & FL_NOTARGET))
			return;
		if (e.kind == PK_TOLD) {
			if (!em)
				return;
			Write(em->client ? player : character, em, e.emitter_origin, now, PK_TOLD);
			return;
		}
		float d2 = Dist2(ent->s.origin, e.pos);
		if (RangeClass(PerceivedD2(d2, MuteLevel(now))) == RANGE_FAR)
			return;
		if (!em)
			return;
		if (e.kind == PK_ALERT_CALL && d2 < 400.0f)
			return;
		bool kind12 = e.kind == 1 || e.kind == 2;
		if (em->client) {
			if (player.ent && player.ent != em && now - player.time <= 10.0f)
				return;
			if (kind12)
				Write(player, em, e.emitter_origin, now - 5, e.kind);
			else {
				if (player.ent && now - player.time <= 20.0f)
					return;
				Write(player, em, e.emitter_origin, now - 10, e.kind);
			}
		} else {
			if (character.ent && now - character.time <= 10.0f)
				return;
			if (kind12) {
				int keep = character.kind;
				Write(character, em, e.emitter_origin, now - 5, keep);
				player.kind = e.kind; /* original behaviour (SPEC-701 open question 4) */
			} else {
				if (character.ent && now - character.time <= 20.0f)
					return;
				Write(character, em, e.emitter_origin, now - 10, e.kind);
			}
		}
	}

	bool Update(edict_t *ent, AI *ai, int mask) override
	{
		(void)ai;
		if (!(mask & Mask()))
			return true;
		float now = Now();
		for (int i = 0; i < count; i++)
			Process(ent, events[i], now);
		count = 0;
		return true;
	}
};

/* ------------------------------------------------------------------------------------------------------------------
 * omniscience (code 5) and proximity (code 6)
 * ---------------------------------------------------------------------------------------------------------------- */
class OmniSense : public SenseBase {
public:
	OmniSense() : SenseBase(5) {}
	int Mask() override { return 2; }
	void Eval(edict_t *ent, AI *ai, edict_t *t)
	{
		if (!t || !t->inuse || t == ent)
			return;
		if (t->client) {
			if ((t->flags & FL_NOTARGET) || (SameTeam(ent, t) && !ai->perceive_teammates))
				return;
		} else if (!t->ai)
			return;
		Percept &r = t->client ? player : character;
		r.ent = t;
		VectorCopy(t->s.origin, r.pos);
		r.time = Now();
		r.kind = PK_OMNISCIENT;
	}
	bool Update(edict_t *ent, AI *ai, int mask) override
	{
		if (!(mask & Mask()))
			return true;
		Eval(ent, ai, SightClient());
		Eval(ent, ai, SightCharacter());
		return true;
	}
};

class ProximitySense : public SenseBase {
public:
	float radius2;
	explicit ProximitySense(float r) : SenseBase(6), radius2(r * r) {}
	void SaveTail(SaveRec &r) override
	{
		SenseBase::SaveTail(r);
		float roll = 0;
		r.F(84, roll);
		r.F(88, radius2);
	}
	int Mask() override { return 2; }
	bool Update(edict_t *ent, AI *ai, int mask) override
	{
		if (!(mask & Mask()))
			return true;
		edict_t *t = SightClient();
		if (!t || !t->inuse || t == ent || (t->flags & FL_NOTARGET))
			return true;
		if (SameTeam(ent, t) && !ai->perceive_teammates)
			return true;
		if (Dist2(ent->s.origin, t->s.origin) > radius2)
			return true;
		player.ent = t;
		VectorCopy(t->s.origin, player.pos);
		player.time = Now();
		return true;
	}
};

Sense *NewSight(int code) { return new SightSense(code == 2 ? 2 : 3); }
Sense *NewHearing(void) { return new HearingSense(); }
Sense *NewOmniscience(void) { return new OmniSense(); }
Sense *NewProximity(float radius) { return new ProximitySense(radius < 0.1f ? 75.0f : radius); }
Sense *NewInertSense(int code) { return new InertSense(code); }

Sense *MakeSense(int code, float alertradius)
{
	switch (code) {
	case 0: case 1: return NewInertSense(code);
	case 2: case 3: return NewSight(code);
	case 4: return NewHearing();
	case 5: return NewOmniscience();
	case 6: return NewProximity(alertradius);
	default:
		gi.dprintf("ERROR: invalid sense class code: %d\n", code);
		return NewInertSense(0);
	}
}

} /* namespace om_ai */
