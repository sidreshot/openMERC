/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-750 12.2, 12.4, 12.5, 12.6 (AI side of the AI command), SPEC-710 C13 (scripted decision, kind 9),
 * SPEC-700 rule 18 (script interface), SPEC-751 1.3 (cinematic actors are marked by the script host)
 * Owner: package G2.
 */
#include "ai_decisions.h"
#include "../script/script_host.h"

namespace om_ai {

/* one queued script action (SPEC-710 "SDSO") */
struct ScriptAction {
	const G3Anim *anim = nullptr;
	float duration = 50.0f;
	vec3_t facing = {0, 0, 0};
	vec3_t destination = {0, 0, 0};
	int absolute = 0;
	int lethal = 0;
	int kind = 0;
	int flags = 0;
	int done_token = 0;
	float speed = 0;
	edict_t *target = nullptr;
	int bolt = 0;
	int expression = 0;
	/* a move command stays at the front of the queue until it arrives (SPEC-750 12.4); set when it first starts
	 * (SPEC-710 kind-9 record: start time, start position, current destination) */
	int started = 0;
	float start_time = 0;
	vec3_t start_pos = {0, 0, 0};
	vec3_t dest = {0, 0, 0};
};

class ScriptedDecision : public CombatDecision {
public:
	int finished = 0;
	std::vector<ScriptAction> items;
	const G3Anim *last_anim = nullptr;
	int widened = 0;

	ScriptedDecision() : CombatDecision(DK_SCRIPTED)
	{
		base_priority = 25;
		bonus = 15;
	}
	bool Evaluate(edict_t *ent, AI *ai, int *priority) override
	{
		UpdateTarget(ent, ai);
		if (finished)
			return false;
		bonus = 15;
		Relax(this);
		*priority = base_priority + bonus;
		return true;
	}
	bool MatchesReset(edict_t *e) override
	{
		(void)e;
		return true;
	}
	void ActionDone(edict_t *ent, AI *ai, Action *a, float m) override
	{
		if (a->Kind() != AK_ATTACK)
			success = 0.75f * success + 0.25f * m;
		if (a->Kind() == AK_MOVE && m < 0.5f)
			widened = 1;
		if (m <= 0.25f && a->Kind() != AK_MOVE)
			Unstick(ent, ai);
		/* the move command behind a scripted move action completes as soon as it has arrived (its AI-done signal
		 * belongs to the frame of arrival, SPEC-750 12.6) */
		if (a->Kind() == AK_MOVE && (a->script_flags & 0x80000000) && !items.empty() && items.front().started &&
		    MoveComplete(ent, items.front(), a->ends_with_anim && ai->body && ai->body->AnimDone()))
			CompleteFront();
	}

	float ArrivalRadius(edict_t *ent)
	{
		if (!widened)
			return 11.0f;
		float dx = ent->maxs[0] - ent->mins[0], dy = ent->maxs[1] - ent->mins[1];
		return 0.5f * sqrtf(dx * dx + dy * dy);
	}
	bool MoveComplete(edict_t *ent, const ScriptAction &f, bool anim_ended)
	{
		/* with flag 0x100 (lethal) duration and animation end are ignored; arrival still ends a move
		 * (SPEC-AMEND-005 G2.1) */
		bool lethal = (f.flags & 0x100) != 0;
		if (!lethal && Now() - f.start_time >= f.duration)
			return true;
		if (!lethal && anim_ended)
			return true;
		return ScriptMoveArrived(ent, f.dest, f.start_pos, ArrivalRadius(ent));
	}
	void CompleteFront()
	{
		int token = items.front().done_token;
		items.erase(items.begin());
		widened = 0;
		if (token && g_report_ai_done)
			g_report_ai_done(token);
	}

	void StandIdle(edict_t *ent, AI *ai);
	void Plan(edict_t *ent, AI *ai) override;

	/* SPEC-710 kind 9 record (AISD 220) after the combat prefix; the front command's start state lives here (the
	 * queued commands follow as SDSO chunks, so on load it is kept until ScriptedAfterLoad) */
	int saved_started = 0;
	float saved_start_time = 0;
	vec3_t saved_start_pos = {0, 0, 0}, saved_dest = {0, 0, 0};
	void SaveTail(SaveRec &r) override
	{
		CombatDecision::SaveTail(r);
		if (!r.reading && !items.empty()) {
			const ScriptAction &f = items.front();
			saved_started = f.started;
			saved_start_time = f.start_time;
			VectorCopy(f.start_pos, saved_start_pos);
			VectorCopy(f.dest, saved_dest);
		} else if (!r.reading)
			saved_started = 0;
		r.I(0, saved_started); /* OpenMerc: the front command has started */
		r.I(160, finished);
		edict_t *none = nullptr;
		r.Ent(164, none);
		r.I(168, widened);
		r.F(172, saved_start_time);
		r.V(176, saved_start_pos);
		r.V(188, saved_dest);
		vec3_t dir;
		VectorSubtract(saved_dest, saved_start_pos, dir);
		VectorNormalize(dir);
		r.V(200, dir);
		r.Anim(212, last_anim);
		r.Ent(216, none);
	}
};

/* the AION / SDSO chunks of SPEC-710 after the kind-9 record */
static void SdsoRecord(ScriptAction &a, SaveRec &r)
{
	r.Anim(0, a.anim);
	r.F(4, a.duration);
	r.V(8, a.facing);
	r.V(20, a.destination);
	r.I(32, a.absolute);
	r.B(36, a.lethal);
	r.I(40, a.kind);
	r.I(44, a.flags);
	r.Event(48, a.done_token);
	r.F(52, a.speed);
	r.Ent(56, a.target);
	r.I(60, a.bolt);
	r.I(64, a.expression);
}



void ScriptedSaveExtra(Decision *dd, bool reading)
{
	ScriptedDecision *d = static_cast<ScriptedDecision *>(dd);
	int32_t n = (int32_t)d->items.size();
	if (!reading) {
		SaveChunk("AION", &n, 4);
		for (ScriptAction &a : d->items) {
			SaveRec r(false, 68);
			SdsoRecord(a, r);
			SaveChunk("SDSO", r.data, 68);
		}
		return;
	}
	d->items.clear();
	if (!LoadChunk("AION", &n, 4) || n < 0 || n > 4096)
		return;
	for (int i = 0; i < n; i++) {
		SaveRec r(true, 68);
		if (!LoadChunk("SDSO", r.data, 68))
			break;
		ScriptAction a;
		SdsoRecord(a, r);
		d->items.push_back(a);
	}
}

/* the front command's start state was read with the record; a move under way gets its move action marked again */
void ScriptedAfterLoad(edict_t *ent, AI *ai, Decision *dd)
{
	ScriptedDecision *d = static_cast<ScriptedDecision *>(dd);
	(void)ent;
	if (d->items.empty())
		return;
	ScriptAction &f = d->items.front();
	f.started = d->saved_started;
	f.start_time = d->saved_start_time;
	VectorCopy(d->saved_start_pos, f.start_pos);
	VectorCopy(d->saved_dest, f.dest);
	Action *c = ai->current;
	if (f.started && f.kind == 1 && c && c->owner == d && (c->script_flags & 0x80000000))
		SetScriptMove(c, f.start_pos, d->ArrivalRadius(ai->owner));
}

void SetScriptedFinished(Decision *d)
{
	if (d && d->Kind() == DK_SCRIPTED)
		static_cast<ScriptedDecision *>(d)->finished = 1;
}

Decision *NewScriptedDecision(void) { return new ScriptedDecision(); }

Decision *FindScripted(AI *ai) { return ai->FindDecision(DK_SCRIPTED); }

static ScriptedDecision *GetScripted(AI *ai)
{
	Decision *d = FindScripted(ai);
	if (!d) {
		d = NewScriptedDecision();
		ai->decisions.push_back(d);
	}
	return static_cast<ScriptedDecision *>(d);
}

/* SPEC-710 C13: the standing idle with an empty script queue */
void ScriptedDecision::StandIdle(edict_t *ent, AI *ai)
{
	const G3Body *b = ai->body;
	const char *want;
	int rht = ai->body ? ai->body->RightHandType() : 0;
	if (b && b->cur && b->cur->group == 9)
		want = "std_ietalkpose_n_a_n";
	else if (last_anim && last_anim->group == 3 && (last_anim->flags & ANIMF_LOOP))
		want = last_anim->name;
	else if (rht == 6 || rht == 7 || rht == 9 || rht == 11 || rht == 12 || rht == 16 || rht == 17 || rht == 18 ||
	         rht == 29)
		want = "std_istand_n_mrs_2";
	else if (rht == 14)
		want = "std_a_fwd_l_2";
	else
		want = "std_istand_n_a_n";
	const G3Anim *a = Anim(want);
	const G3Anim *use = nullptr;
	if (a && ai->body && ai->body->HasAnim(a))
		use = a;
	else {
		G3MoveInput in;
		FillInput(in, ent, ai, nullptr, nullptr, nullptr, -1, false, false, false, nullptr);
		use = ai->body ? ai->body->ChooseStand(in) : nullptr;
		if (use)
			gi.dprintf("Botched idiocy! Couldn't set %s, using %s\n", want, use->name);
	}
	if (CvarValue(cv.ai_dumb))
		PlayEffect(ent, "confusion");
	if (!use)
		return;
	/* a standing idle record is a class-0 request: duration 0, not ending with the animation, so it completes at its
	 * first think and the decision re-plans every think (SPEC-AMEND-005 G2.9); a record with flag 0x00400000 keeps
	 * the 4.0 s / animation-end rule of D3 */
	bool cycle = (use->flags & ANIMF_FINISH_CYCLE) != 0;
	Action *act = MakeAction(ai, 1, this, nullptr, use, ent->s.origin, nullptr, cycle ? 4.0f : 0.0f, cycle);
	ai->QueueBack(act);
}

/* SPEC-750 12.4: turn the front script action into an AI action */
void ScriptedDecision::Plan(edict_t *ent, AI *ai)
{
	if (CvarValue(cv.ai_dumb))
		PlayEffect(ent, "scripted");
	while (!items.empty() && items.front().expression) {
		ScriptAction e = items.front();
		items.erase(items.begin());
		if (ai->body)
			ai->body->SetExpression(e.expression, e.duration, false);
		if (e.done_token && g_report_ai_done)
			g_report_ai_done(e.done_token);
	}
	/* a move command already under way that has arrived (or run out of time) completes here */
	while (!items.empty() && items.front().kind == 1 && items.front().started &&
	       MoveComplete(ent, items.front(), false)) {
		CompleteFront();
		while (!items.empty() && items.front().expression) {
			ScriptAction e = items.front();
			items.erase(items.begin());
			if (ai->body)
				ai->body->SetExpression(e.expression, e.duration, false);
			if (e.done_token && g_report_ai_done)
				g_report_ai_done(e.done_token);
		}
	}
	if (items.empty()) {
		StandIdle(ent, ai);
		return;
	}
	/* a move command stays queued while it moves: each of its move actions steers towards the destination, and
	 * after a failed step the next one is made with the widened arrival radius (SPEC-750 12.4, SPEC-710 C13) */
	bool keep = items.front().kind == 1;
	ScriptAction s = items.front();
	if (!keep) {
		items.erase(items.begin());
		widened = 0;
	}
	vec3_t dest, face = {0, 0, 0};
	if (s.started)
		VectorCopy(s.dest, dest);
	else if (s.absolute)
		VectorCopy(s.destination, dest);
	else
		VectorAdd(ent->s.origin, s.destination, dest);
	if (!IsZero(s.facing))
		VectorMA(ent->s.origin, 1000, s.facing, face);
	edict_t *t = s.target;
	if (t && t->inuse && (t->health > 0 || ClassnameIs(t, "info_notnull"))) {
		VectorCopy(t->s.origin, dest);
		VectorCopy(t->s.origin, face);
	} else
		t = nullptr;
	if (keep) {
		ScriptAction &f = items.front();
		if (!f.started) {
			f.started = 1;
			f.start_time = Now();
			VectorCopy(ent->s.origin, f.start_pos);
			widened = 0;
		}
		VectorCopy(dest, f.dest);
		s = f;
	}
	bool explicit_dur = (s.flags & 0x40) != 0;
	bool lethal = (s.flags & 0x100) != 0; /* duration and animation end ignored (SPEC-AMEND-005 G2.1) */
	/* SPEC-AMEND-006 G2.13: the kind only decides how the record is looked up (0, 3, 4, 5, 6: the command's
	 * animation, else the body's stand choice; 1: movement choice; 2: attack choice; 8: `std_istand_n_a_n`; others
	 * print "Invalid action code" and count as 0); the action is then made by D3 from the record's class (a jump
	 * record of a kind-5 command jumps, a death record dies, a hit record plays the 4 s hit reaction) */
	int kind = s.kind;
	if (kind < 0 || kind == 7 || kind > 8) {
		gi.dprintf("Invalid action code (%d)!\n", kind);
		kind = 0;
	}
	const G3Anim *rec = kind == 8 ? Anim("std_istand_n_a_n") : s.anim;
	if (rec && ai->body && !ai->body->HasAnim(rec)) {
		const G3Anim *sub = nullptr;
		G3MoveInput in;
		FillInput(in, ent, ai, kind == 1 ? dest : nullptr, face, t, -1, s.speed >= 100, false, false, nullptr);
		sub = kind == 1 ? ai->body->ChooseMove(in) : ai->body->ChooseStand(in);
		gi.dprintf("script anim: %s not available for %s, using %s!\n", rec->name,
		           ent->targetname ? ent->targetname : ent->classname, sub ? sub->name : "nothing");
		rec = sub;
	}
	if (!rec && ai->body) {
		G3MoveInput in;
		if (kind == 1) {
			FillInput(in, ent, ai, dest, face, nullptr, -1, s.speed >= 100, false, false, nullptr);
			rec = ai->body->ChooseMove(in);
		} else if (kind == 2) {
			FillInput(in, ent, ai, nullptr, face, t, -1, false, true, false, nullptr);
			rec = ai->body->ChooseAttack(in);
		}
		if (!rec) {
			FillInput(in, ent, ai, nullptr, face, t, -1, false, false, false, nullptr);
			rec = ai->body->ChooseStand(in);
		}
	}
	/* D3 sees the target only with the lethal flag or a selector (SPEC-AMEND-006 G2.13) */
	edict_t *d3_target = (s.lethal || s.bolt) ? s.target : nullptr;
	int cls = 1;
	float d3_dur = 0;
	bool d3_ends = false;
	edict_t *d3_tgt = nullptr;
	if (rec)
		D3Rule(ent, ai, rec, d3_target, &cls, &d3_dur, &d3_ends, &d3_tgt);
	/* SPEC-710 D3 row "8 (script stand)": script kind 8 makes the end-of-script action (class 6), whatever the
	 * class of its `std_istand_n_a_n` record. No record has class 8, so keying the row on the record class left
	 * the scripted decision in control for good after `ai(x, 99)`: kos2 `bosguy2`/`bosguy5` stood in
	 * `std_istand_n_a_n` at priority 40 after the girder script and never fired (the original's bosguy2 fires ~120
	 * times) */
	if (kind == 8) {
		cls = 6;
		d3_dur = 0;
		d3_ends = false;
		d3_tgt = nullptr;
	}
	/* completion (SPEC-750 12.4): the animation end counts unless 0x40 or 0x100 was given; a move command only with
	 * a record that ends on its animation end */
	bool ends = !explicit_dur && !lethal;
	if (kind == 1)
		ends = rec && (rec->flags & ANIMF_FINISH_CYCLE) && !explicit_dur && !lethal;
	last_anim = rec;
	/* the 0x40 duration only when given, else D3's default (SPEC-AMEND-006 G2.13) */
	float dur = lethal ? 999999.0f : explicit_dur ? s.duration : d3_dur;
	if (keep && !lethal && explicit_dur) {
		dur = s.duration - (Now() - s.start_time);
		if (dur < FRAMETIME)
			dur = FRAMETIME;
	}
	Action *a = MakeAction(ai, cls, this, nullptr, rec, dest, IsZero(face) ? nullptr : face, dur, ends);
	a->done_token = keep ? 0 : s.done_token; /* a move command reports its signal itself when it arrives */
	/* jump and fall commands complete for the script as soon as they start (SPEC-750 12.4); the AI action plays on */
	bool done_at_start = (kind == 3 || kind == 4) && !lethal;
	if (done_at_start)
		a->done_token = 0;
	a->script_flags = s.flags | 0x80000000; /* marks a scripted action (SPEC-750 12.4 completion rules) */
	if (keep)
		SetScriptMove(a, s.start_pos, ArrivalRadius(ent));
	if (s.lethal || s.bolt) {
		a->target = s.target;
		a->lethal = s.lethal;
		a->target_bolt = s.bolt;
	} else if (cls == 13)
		a->target = d3_tgt ? d3_tgt : t;
	ai->QueueBack(a);
	if (done_at_start && s.done_token && g_report_ai_done)
		g_report_ai_done(s.done_token);
}

/* ------------------------------------------------------------------------------------------------------------------
 * SPEC-750 12.2 execution of an AI command
 * ---------------------------------------------------------------------------------------------------------------- */
void ScriptCommand(edict_t *ent, const om_script::AiCommand &cmd)
{
	AI *ai = ent ? ent->ai : nullptr;
	if (!ai)
		return;
	ScriptedDecision *d = GetScripted(ai);
	if (cmd.expression) {
		float dur = (cmd.flags & 0x40) ? cmd.duration : 9999.9f;
		if (d->items.empty() && ai->body) {
			ai->body->SetExpression(cmd.expression, dur, false);
			if (cmd.done_id && g_report_ai_done)
				g_report_ai_done(cmd.done_id);
		} else {
			ScriptAction e;
			e.expression = cmd.expression;
			e.duration = dur;
			e.done_token = cmd.done_id;
			d->items.push_back(e);
		}
	} else if (cmd.count > 0) {
		ScriptAction s;
		s.anim = cmd.animation ? Anim(cmd.animation) : nullptr;
		s.duration = cmd.duration;
		VectorCopy(cmd.facing, s.facing);
		VectorCopy(cmd.destination, s.destination);
		s.absolute = (cmd.flags & 0x20) != 0;
		s.kind = cmd.kind;
		s.flags = cmd.flags;
		s.speed = (float)cmd.speed;
		s.target = (cmd.target >= 0 && cmd.target < globals.num_edicts) ? &g_edicts[cmd.target] : nullptr;
		s.bolt = (cmd.flags & 0x200) ? cmd.anim_param : 0;
		if (CvarValue(cv.ai_trace))
			Dbg("ai %d: script cmd %d kind %d flags 0x%x param %d target %d dest (%.0f %.0f %.0f) face (%.2f %.2f %.2f) "
			    "at (%.0f %.0f %.0f)\n", G_EdictNum(ent), cmd.command, cmd.kind, cmd.flags, cmd.anim_param, cmd.target,
			    cmd.destination[0], cmd.destination[1], cmd.destination[2], cmd.facing[0], cmd.facing[1],
			    cmd.facing[2], ent->s.origin[0], ent->s.origin[1], ent->s.origin[2]);
		/* 0x100 = the "lethal" byte of the scripted action (SDSO 36), 0x200 operand = the target-bolt selector
		 * (SDSO 60); a script action with it ends only by an attack's completion or a move's arrival
		 * (SPEC-AMEND-005 G2.1) */
		s.lethal = (cmd.flags & 0x100) ? 1 : 0;
		for (int i = 0; i < cmd.count; i++) {
			ScriptAction c = s;
			c.done_token = i == cmd.count - 1 ? cmd.done_id : 0;
			d->items.push_back(c);
		}
	}
	/* activation */
	if (!ai->awake)
		ai->Wake(ent);
	ai->call_for_help = 0;
}

void ScriptReset(edict_t *ent)
{
	AI *ai = ent ? ent->ai : nullptr;
	if (!ai) {
		gi.dprintf("entity %s had no ai, aborting HandleResetAI\n",
		           ent && ent->targetname ? ent->targetname : (ent && ent->classname ? ent->classname : ""));
		return;
	}
	ai->RemoveDecision(ent, DK_SCRIPTED, nullptr);
}

void ClearCallForHelp(edict_t *ent)
{
	if (ent && ent->ai)
		ent->ai->call_for_help = 0;
}

} /* namespace om_ai */
