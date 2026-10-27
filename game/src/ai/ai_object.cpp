/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700 (rules 1-10, 14, 15: creation, spawn-time decisions and senses, spawnflags, reactions,
 * waking, alert, per-frame think, scheduling of decisions and actions, damage/pain/death of characters, perception
 * summary, bounding-box presets), SPEC-701 sections 1 and 8, SPEC-751 1.3 (cinematic freeze)
 * Owner: package G2.
 */
#include "ai_local.h"

using namespace om_ai;

/* ------------------------------------------------------------------------------------------------------------------
 * the AI object
 * ---------------------------------------------------------------------------------------------------------------- */
OmAI::OmAI()
{
	for (int i = 0; i < NUM_BBOX; i++) {
		VectorClear(bbox_mins[i]);
		VectorClear(bbox_maxs[i]);
	}
	static const float def[9] = {100, 300, 0.8f, 1.0f, 0, 0, 0, 0, 1.0f};
	for (int i = 0; i < NUM_PROFILE; i++)
		profile[i] = i < 9 ? def[i] : 0;
	memset(&nav, 0, sizeof(nav));
	Nav_InitCache(&nav);
}

OmAI::~OmAI()
{
	for (Sense *s : senses)
		delete s;
	for (Decision *d : decisions)
		delete d;
	for (Action *a : queue)
		delete a;
	if (current)
		delete current;
	if (previous && previous != current)
		delete previous;
	senses.clear();
	decisions.clear();
	queue.clear();
	current = previous = nullptr;
}

void OmAI::SetBBoxPreset(int i, const vec3_t mins, const vec3_t maxs)
{
	if (i < 0 || i >= NUM_BBOX)
		return;
	VectorCopy(mins, bbox_mins[i]);
	VectorCopy(maxs, bbox_maxs[i]);
}

/* SPEC-700 rule 15 */
int OmAI::ChangeBBox(edict_t *ent, int i, bool allow_fallback)
{
	if (i < 0 || i > 8)
		return 0;
	vec3_t mins, maxs, org;
	VectorCopy(ent->s.origin, org);
	if (i == bbox_current) {
		/* same preset: only the floor offset is smoothed */
		VectorCopy(ent->mins, mins);
		VectorCopy(ent->maxs, maxs);
		if (floor_height < mins[2] - 15)
			floor_height = mins[2] - 15;
		if (floor_height > mins[2] + 15)
			floor_height = mins[2] + 15;
		bool airborne = !ent->groundentity && ent->solid != SOLID_CORPSE;
		if (airborne || fabsf(mins[2] - floor_height) <= 1.0f)
			return 1;
		float delta = floor_height - mins[2];
		if (CvarValue(cv.ai_trace) >= 2)
			Dbg("ai %d floor: bottom %.1f stored %.1f ground offset %.1f -> origin z %+.1f\n", G_EdictNum(ent),
			    mins[2], floor_height, ground_offset, -delta);
		mins[2] += delta;
		maxs[2] += delta;
		org[2] -= delta;
	} else {
		float bottom = ent->mins[2];
		float height = scale * (bbox_maxs[i][2] - bbox_mins[i][2]);
		for (int k = 0; k < 2; k++) {
			mins[k] = bbox_mins[i][k] * scale;
			maxs[k] = bbox_maxs[i][k] * scale;
		}
		mins[2] = bottom;
		maxs[2] = bottom + height;
	}
	if (maxs[2] < ent->viewheight - 4)
		maxs[2] = (float)ent->viewheight;
	if (ent->movetype != MOVETYPE_NOCLIP) {
		trace_t tr = Trace(org, mins, maxs, org, ent, 0x2020003);
		if (tr.startsolid || tr.allsolid) {
			if (!allow_fallback)
				return 0;
			mins[0] = ent->mins[0];
			mins[1] = ent->mins[1];
			maxs[0] = ent->maxs[0];
			maxs[1] = ent->maxs[1];
			tr = Trace(org, mins, maxs, org, ent, 0x2020003);
			if ((tr.startsolid || tr.allsolid) && ent->health > 0)
				return 0;
		}
	}
	VectorCopy(mins, ent->mins);
	VectorCopy(maxs, ent->maxs);
	VectorCopy(org, ent->s.origin);
	gi.linkentity(ent);
	bbox_current = i;
	return 1;
}

/* ------------------------------------------------------------------------------------------------------------------
 * lists and scheduling (SPEC-700 rule 8)
 * ---------------------------------------------------------------------------------------------------------------- */
Decision *OmAI::FindDecision(int kind)
{
	for (Decision *d : decisions)
		if (d->Kind() == kind)
			return d;
	return nullptr;
}

void OmAI::DestroyAction(Action *a)
{
	if (!a)
		return;
	for (size_t i = 0; i < queue.size();) {
		if (queue[i] == a)
			queue.erase(queue.begin() + i);
		else {
			if (queue[i]->linked == a)
				queue[i]->linked = nullptr;
			i++;
		}
	}
	if (current == a)
		current = nullptr;
	else if (current && current->linked == a)
		current->linked = nullptr;
	if (previous == a)
		previous = nullptr;
	else if (previous && previous->linked == a)
		previous->linked = nullptr;
	delete a;
}

void OmAI::DestroyDecision(Decision *d)
{
	if (!d)
		return;
	for (Action *a : queue)
		if (a->owner == d)
			a->owner = nullptr;
	if (current && current->owner == d)
		current->owner = nullptr;
	if (previous && previous->owner == d)
		previous->owner = nullptr;
	for (size_t i = 0; i < decisions.size(); i++)
		if (decisions[i] == d) {
			decisions.erase(decisions.begin() + i);
			break;
		}
	delete d;
}

void OmAI::RemoveDecision(edict_t *ent, int kind, edict_t *match)
{
	Decision *found = nullptr;
	for (Decision *d : decisions)
		if (match ? d->MatchesReset(match) : d->Kind() == kind) {
			found = d;
			break;
		}
	if (!found)
		return;
	for (size_t i = 0; i < queue.size();) {
		if (queue[i]->owner == found) {
			Action *a = queue[i];
			DestroyAction(a);
		} else
			i++;
	}
	bool restart = current && current->owner == found;
	if (restart) {
		Action *c = current;
		current = nullptr;
		DestroyAction(c);
	}
	DestroyDecision(found);
	if (restart)
		NextAction(ent);
}

void OmAI::QueueBack(Action *a, bool wake)
{
	queue.push_back(a);
	if (wake && owner && !awake)
		Wake(owner);
}

void OmAI::QueueFront(Action *a) { queue.insert(queue.begin(), a); }

void OmAI::Interrupt(edict_t *ent, Action *a)
{
	if (current) {
		current->Interrupted(ent, this);
		queue.insert(queue.begin(), current);
	}
	current = a;
	a->Start(ent, this);
}

void OmAI::NextAction(edict_t *ent)
{
	if (current) {
		if (previous && previous != current)
			DestroyAction(previous);
		previous = current;
		current = nullptr;
	}
	if (!queue.empty()) {
		current = queue.front();
		queue.erase(queue.begin());
		current->Start(ent, this);
	}
}

void OmAI::ClearActions(bool keep_current)
{
	while (!queue.empty())
		DestroyAction(queue.front());
	if (!keep_current) {
		if (current)
			DestroyAction(current);
		if (previous)
			DestroyAction(previous);
	}
}

void OmAI::ClearAll(bool keep_current)
{
	for (Sense *s : senses)
		delete s;
	senses.clear();
	while (!decisions.empty())
		DestroyDecision(decisions.front());
	ClearActions(keep_current);
}

/* ------------------------------------------------------------------------------------------------------------------
 * perception summary (SPEC-701 section 8)
 * ---------------------------------------------------------------------------------------------------------------- */
Percept OmAI::NewestPlayer(int mask, Sense **from)
{
	Percept best;
	best.time = -1e8f;
	if (from)
		*from = nullptr;
	for (Sense *s : senses)
		if (mask == -1 || (s->Mask() & mask))
			if (s->player.time >= best.time) {
				best = s->player;
				if (from)
					*from = s;
			}
	return best;
}

Percept OmAI::NewestCharacter(int mask, Sense **from)
{
	Percept best;
	best.time = -1e8f;
	if (from)
		*from = nullptr;
	for (Sense *s : senses)
		if (mask == -1 || (s->Mask() & mask))
			if (s->character.time >= best.time) {
				best = s->character;
				if (from)
					*from = s;
			}
	return best;
}

bool OmAI::TargetPosition(vec3_t out)
{
	if (target) {
		Percept p = target->client ? NewestPlayer() : NewestCharacter();
		if (p.ent == target) {
			VectorCopy(p.pos, out);
			return true;
		}
	}
	if (owner)
		VectorCopy(owner->s.origin, out);
	return false;
}

float OmAI::MostMuted(float now, int mask)
{
	float best = 0;
	for (Sense *s : senses)
		if (mask == -1 || (s->Mask() & mask)) {
			float l = s->MuteLevel(now);
			if (l > best)
				best = l;
		}
	return best;
}

void OmAI::MuteSenses(float now, int mask, float level, int mode, float duration)
{
	for (Sense *s : senses)
		if (s->Mask() & mask)
			s->Mute(now, level, mode, duration);
}

void OmAI::HearEvent(const vec3_t pos, edict_t *emitter, const vec3_t emitter_origin, int kind)
{
	for (Sense *s : senses)
		if (s->ClassCode() == 4)
			s->Hear(pos, emitter, emitter_origin, kind, Now());
}

static void Summarise(edict_t *ent, AI *ai)
{
	float now = Now();
	Sense *from = nullptr;
	Percept p = ai->NewestPlayer(-1, &from);
	if (CvarValue(cv.ai_trace) >= 2 && from && p.ent && p.time > now - 0.05f)
		Dbg("ai %d percept: sense %d kind %d of %d at (%.0f %.0f %.0f)\n", G_EdictNum(ent), from->ClassCode(), p.kind,
		    G_EdictNum(p.ent), p.pos[0], p.pos[1], p.pos[2]);
	if (from && p.kind == PK_ALERT_CALL) {
		ai->recently_called = p.time + 12.0f;
		from->player.kind = PK_DEATH;
	}
	/* SPEC-700 rule 14: a living player record last perceived more than 1 s ago moves the reaction moment to now.
	 * OpenMerc also moves it when the first player record is perceived (SPEC-710: "the moment the current player
	 * record was (re)acquired"), unless that record is a "told" event of a use (the character is alerted by its
	 * trigger). Black box: the arm1 gimme soldier that first sees the player behind it stands still in the original
	 * (with a reaction age of the whole level time OpenMerc acted at once); the kos1 shotgunner, told by its
	 * trigger, runs and fires without a startle in the original (counting the told record made OpenMerc's startle,
	 * SPEC-722 reaction override 3). OPEN_QUESTIONS G2 18. */
	bool first = !ai->had_player_record && p.kind != PK_TOLD;
	if (p.ent && p.ent->inuse && p.ent->health > 0 && (now - p.time > 1.0f || first))
		ai->reaction_time = now + FlRand(-0.2f, 0.2f);
	/* SPEC-722 reaction override 3 "the AI has just noticed an enemy: the AI is notified": a flag set at the first
	 * sighting of a living player and consumed when the body plays the startle (OnStartled). It used to be
	 * "reaction age < 0.2 s", but the rule above moves the reaction moment to now at every think while the record
	 * is stale, so a target seen intermittently made the body startle again every ~4 s (tsr2 `runner01/02`,
	 * `sparkrunner*`: 6-8 `std_xstartled_n_a_a` in 20 s, the original aims and holds `std_ialert`, `fly4_tsr2`). */
	if (first && p.ent && p.ent->inuse && p.ent->health > 0)
		ai->just_noticed = 1;
	ai->had_player_record = p.ent != nullptr;
	(void)ent;
}

/* ------------------------------------------------------------------------------------------------------------------
 * waking and alerts (SPEC-700 rules 5 and 6)
 * ---------------------------------------------------------------------------------------------------------------- */
void OmAI::AddDeferredSenses(edict_t *ent)
{
	if (ent->spawnflags & SF_DEAF) {
		ent->spawnflags &= ~SF_DEAF;
		senses.push_back(NewHearing());
	}
	if (ent->spawnflags & SF_BLIND) {
		ent->spawnflags &= ~SF_BLIND;
		senses.push_back(NewSight(3));
	}
}

void OmAI::Wake(edict_t *ent)
{
	if (CvarValue(cv.ai_trace) && !awake)
		Dbg("ai %d: wakes at (%.0f %.0f %.0f) %s %s\n", G_EdictNum(ent), ent->s.origin[0], ent->s.origin[1], ent->s.origin[2], ent->classname ? ent->classname : "-", ent->targetname ? ent->targetname : "-");
	awake = 1;
	ent->movetype = MOVETYPE_STEP;
	ent->solid = SOLID_BBOX;
	ent->spawnflags &= ~SF_DORMANT;
	view_mode = 2;
	if (type_code == TYPE_RAIDERBOSS2) {
		trace_t tr = Trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, ent, 0x2020003);
		if (tr.startsolid || tr.allsolid) {
			ent->s.origin[2] += 32;
			tr = Trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, ent, 0x2020003);
			if (tr.startsolid || tr.allsolid)
				ent->s.origin[2] -= 64;
		}
	}
	gi.linkentity(ent);
}

void OmAI::Alert(edict_t *ent, float time, edict_t *source, const vec3_t pos)
{
	if (!alerted && Dist2(pos, ent->s.origin) < 1000000.0f && call_for_help) {
		call_for_help = 0;
		if (source && source->client && body) {
			body->Wake(0.9f);
			AlertSound(source, ent->s.origin, PK_ALERT_CALL, nullptr, 500, false, false);
		}
	}
	alerted = 1;
	if (time > alert_time)
		alert_time = time;
	if (type_code != TYPE_RAIDERBOSS2 && type_code != TYPE_SKINBOSS) {
		ent->spawnflags &= ~SF_INATTENTIVE;
		AddDeferredSenses(ent);
	}
	target = source;
}

static bool WakeChecks(edict_t *ent, AI *ai)
{
	float now = Now();
	if (CvarValue(cv.ai_goretest) && !(ent->spawnflags & SF_DORMANT))
		return true;
	if (ent->spawnflags & SF_WAKE_AT_ONCE)
		return true;
	if (!(ent->spawnflags & SF_DORMANT) && ai->corpse_check <= (int)now) {
		ai->corpse_check = (int)now + 1;
		edict_t *e = nullptr;
		while ((e = findradius(e, ent->s.origin, 200)) != nullptr)
			if (e != ent && e->ai && e->deadflag)
				return true;
	}
	edict_t *sc = SightClient();
	if (sc && !(ent->spawnflags & SF_DORMANT) && Dist2(sc->s.origin, ent->s.origin) < 640000.0f)
		return true;
	return false;
}

/* ------------------------------------------------------------------------------------------------------------------
 * per-frame think (SPEC-700 rule 7)
 * ---------------------------------------------------------------------------------------------------------------- */
static void Decide(edict_t *ent, AI *ai)
{
	Decision *best = nullptr;
	int bestp = 0;
	for (size_t i = 0; i < ai->decisions.size();) {
		Decision *d = ai->decisions[i];
		int p = 0;
		if (!d->Evaluate(ent, ai, &p)) {
			ai->DestroyDecision(d);
			continue;
		}
		if (p > bestp) {
			best = d;
			bestp = p;
		}
		i++;
	}
	if (best) {
		if (CvarValue(cv.ai_trace))
			Dbg("ai %d: plan kind %d prio %d\n", G_EdictNum(ent), best->Kind(), bestp);
		best->Plan(ent, ai);
		ai->NextAction(ent);
	}
}

static void FacePlayer1(edict_t *ent)
{
	edict_t *p = &g_edicts[1];
	if (!p->inuse)
		return;
	ent->s.angles[YAW] = QuantiseAngle(YawTo(ent->s.origin, p->s.origin));
}

void OmAI::Think(edict_t *ent)
{
	float now = Now();
	/* 2. the previous think's helper */
	RemoveHelper(ent, this);
	/* 3. */
	ent->nextthink = now + FRAMETIME;
	/* 4. spawnflag rules */
	if ((ent->spawnflags & SF_KILLTARGET_ON_RETREAT) && current && current->owner &&
	    current->owner->Kind() == DK_RETREAT && ent->killtarget) {
		edict_t *t = G_Find(nullptr, FOFS(targetname), ent->killtarget);
		if (t && t->use)
			t->use(t, ent, ent);
		ent->spawnflags &= ~SF_KILLTARGET_ON_RETREAT;
	}
	if ((ent->spawnflags & SF_FACE_PLAYER1) && ent->wait == 1)
		FacePlayer1(ent);
	/* 5. cinematic freeze */
	if (Cinematic() && ent->count != 255 && (alerted || (ent->flags & FL_SPAWNER_MADE))) {
		ent->flags |= FL_CINEMATIC_FROZEN;
		ent->solid = SOLID_NOT;
		gi.linkentity(ent);
		ent->nextthink = -(ent->nextthink - now);
		if (ent->ghoulInst)
			ent->ghoulInst->SetOnOff(false, now);
		return;
	}
	/* 6. debug */
	if (CvarValue(cv.ai_dumb)) {
		if (ent->spawnflags & SF_BLIND)
			PlayEffect(ent, "cantsee");
		if (ent->spawnflags & SF_DEAF)
			PlayEffect(ent, "canthear");
	}
	if (CvarValue(cv.ai_freeze))
		return;
	/* 7. waking */
	if (CvarValue(cv.ai_goretest) && !(ent->spawnflags & SF_DORMANT) && !awake)
		Wake(ent);
	if (!awake) {
		if (!WakeChecks(ent, this))
			return;
		Wake(ent);
	}
	/* 8. senses */
	if (!low_priority || sense_stamp <= now - 2) {
		for (size_t i = 0; i < senses.size();) {
			if (!senses[i]->Update(ent, this, sense_mask)) {
				delete senses[i];
				senses.erase(senses.begin() + i);
			} else
				i++;
		}
		sense_stamp = (int)(now + 1);
	}
	Summarise(ent, this);
	/* 9. plan */
	if (!current) {
		if (!queue.empty())
			NextAction(ent);
		else
			Decide(ent, this);
	}
	/* 10. act */
	if (current && ent->inuse && ent->ai == this) {
		if (recently_called > now - 1 && recently_called < now && body)
			recently_called = now - 20;
		float m = 0, r = 0;
		Action *a = current;
		if (a->Think(ent, this, &m, &r) && current == a)
			ActionCompleted(ent, this, a, m, r);
	}
	if (!ent->inuse || ent->ai != this)
		return;
	/* 11. ground, water, world effects */
	if (ent->groundentity && ent->groundentity->linkcount != ground_linkcount)
		CheckGround(ent, this);
	CategorizePosition(ent);
	WorldEffects(ent);
	frame_flag = 0;
	if (!ent->inuse || ent->ai != this)
		return;
	/* 12. silent removal */
	if ((low_priority && (ent->flags & FL_SPAWNER_MADE)) || view_mode == 3)
		SilentRemove(ent, this);
	/* 13. corpse removal */
	if (ent->health <= 0 && PartsReadyToRemove(ent)) {
		FreeCharacter(ent);
		return;
	}
	/* 14. helper */
	if (ent->health > 0)
		MakeHelper(ent, this);
}

bool OmAI::PartsReadyToRemove(edict_t *ent)
{
	for (Sense *s : senses)
		if (!s->ReadyToRemove())
			return false;
	for (Action *a : queue)
		if (!a->ReadyToRemove(ent, this))
			return false;
	for (Decision *d : decisions)
		if (!d->ReadyToRemove())
			return false;
	if (current && !current->ReadyToRemove(ent, this))
		return false;
	return true;
}

namespace om_ai {

void CharacterThink(edict_t *ent)
{
	AI *ai = ent->ai;
	if (!ai) {
		ent->nextthink = level.time + FRAMETIME;
		return;
	}
	/* 1. the per-type start-up callback on the very first think */
	if (!ai->first_think_done) {
		ai->first_think_done = 1;
		if (ai->first_think)
			ai->first_think(ent);
		else
			StartCharacter(ent);
		if (!ent->inuse || ent->ai != ai)
			return;
	}
	ai->Think(ent);
}

/* ------------------------------------------------------------------------------------------------------------------
 * creation (SPEC-700 rules 1-2)
 * ---------------------------------------------------------------------------------------------------------------- */
static AIFactory factories[TYPE_LAST + 1];

void RegisterAIFactory(int code, AIFactory make)
{
	if (code >= 0 && code <= TYPE_LAST)
		factories[code] = make;
}

void CharacterSetup(edict_t *ent, bool doa)
{
	ent->movetype = MOVETYPE_STEP;
	VectorSet(ent->mins, -16, -16, -32);
	VectorSet(ent->maxs, 16, 16, 41);
	ent->mass = 200;
	/* SPEC-AMEND-006 G2.15 (corrects rule 1.1 "gravity 0.01"): the 0.01 is the ground friction; gravity keeps the
	 * default 1.0 (black-box arm1 `gimme m_irq_msoldier1`: z steps 8, 16, 24 per frame) */
	ent->gravity = 1.0f;
	ent->friction = 0.01f;
	ent->svflags |= SVF_MONSTER;
	bool gore = CvarValue(cv.ai_goretest) != 0;
	ent->max_health = gore ? 1000 : 100;
	ent->health = doa && !gore ? 0 : ent->max_health;
	ent->takedamage = DAMAGE_AIM;
	if (!(ent->spawnflags & SF_DORMANT) && level.time < 1.0f)
		DropToFloor(ent, ent->ai);
	ent->solid = SOLID_NOT;
	/* no step physics until the character wakes (rule 5: waking sets "movement type step"). Black box (G18's
	 * report, ger2): the dormant `m_cas_mraider1` at map origin z -344 / -304 stay there in the original until
	 * used, while OpenMerc's fell 7.875 units onto the floor during the first frames (`orig/om_ger2_idle`,
	 * entities 140 and 152) */
	ent->movetype = MOVETYPE_NONE;
	ent->nextthink = level.time + FRAMETIME;
	gi.linkentity(ent);
}

AI *NewAIForCode(int code)
{
	AI *ai = (code >= 0 && code <= TYPE_LAST && factories[code]) ? factories[code](code) : nullptr;
	if (!ai)
		ai = new AI();
	ai->type_code = code;
	return ai;
}

AI *CreateAI(edict_t *ent, int code)
{
	AI *ai = nullptr;
	if (code >= 0 && code <= TYPE_LAST && factories[code])
		ai = factories[code](code);
	if (!ai) {
		TypeInfo ti;
		if (code != TYPE_PLAIN && !GetTypeInfo(code, &ti))
			gi.dprintf("ERROR: invalid ai class code: %d\n", code);
		ai = new AI();
	}
	ai->type_code = code;
	ai->owner = ent;
	VectorCopy(ent->s.angles, ai->look_angles);
	if (ent->ai && ent->ai != ai) {
		AI *old = ent->ai;
		ent->ai = nullptr;
		for (size_t i = 0; i < mgr.all.size(); i++)
			if (mgr.all[i] == old) {
				mgr.all.erase(mgr.all.begin() + i);
				break;
			}
		delete old;
	}
	ent->ai = ai;
	mgr.all.push_back(ai);
	ai->body = ent->g3body;
	if (ai->body)
		ai->body->owner = ai;
	ent->think = CharacterThink;
	return ai;
}

void AdjustProfile(AI *ai)
{
	float *p = ai->profile;
	const char *m = level.mapname;
	if (!strncmp(m, "tsr", 3) || !strncmp(m, "trn", 3) || (!strncmp(m, "irq", 3) && strlen(m) > 4 && m[4] == 'a')) {
		p[4] += 0.25f;
		p[3] *= 0.75f;
		p[5] += 2.0f;
		p[6] *= 0.5f;
	}
	if (gix.regiondistance && ai->owner) {
		float v = gix.regiondistance(ai->owner->s.origin);
		if (v != -1.0f)
			p[3] *= v <= 1200.0f ? 0.33f : 0.66f;
	}
	int key = ai->owner ? ai->owner->personality : 0;
	int row = key == 0 ? IRand(0, 5) : key - 1;
	for (int i = 0; i < 6; i++)
		p[9 + i] = (row >= 0 && row <= 5) ? (float)personality_rows[row][i] : 0.0f;
}

void InitProfile(AI *ai)
{
	if (ai->type_code >= 15 && ai->type_code <= TYPE_LAST) {
		bool has = false;
		float p[AI::NUM_PROFILE];
		BuildProfile(ai->type_code, p, &has);
		if (has)
			for (int i = 0; i < 9; i++)
				ai->profile[i] = p[i];
	}
	AdjustProfile(ai);
}

} /* namespace om_ai */

/* ------------------------------------------------------------------------------------------------------------------
 * reactions installed on the character (SPEC-700 rule 4)
 * ---------------------------------------------------------------------------------------------------------------- */
static bool ReactiveMovetype(edict_t *self)
{
	int m = self->movetype;
	return m != MOVETYPE_NONE && m != MOVETYPE_PUSH && m != MOVETYPE_STOP && m != 9;
}

static void AI_Touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	(void)surf;
	AI *ai = self->ai;
	if (!ai || !other || !other->client || !ReactiveMovetype(self))
		return;
	if (ai->body)
		ai->body->OnTouched(other);
}

static void PrintAnimList(const char *cls, const char *title_start, const char *title_end)
{
	gi.dprintf("%s\n", title_start);
	edict_t *e = nullptr;
	while ((e = G_Find(e, FOFS(classname), cls)) != nullptr) {
		for (int i = 0; i < 6; i++)
			if (e->spawn1 && i == 0) {
			}
		(void)e;
	}
	gi.dprintf("%s\n", title_end);
}

static void AI_PlayerUse(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)activator;
	AI *ai = self->ai;
	if (!ai || !other || !other->client || !ReactiveMovetype(self))
		return;
	if (om_ai::SameTeam(self, other))
		ai->perceive_teammates = 1;
	if (ai->body)
		ai->body->OnUsed(other);
	if (CvarValue(cv.ai_pathtest)) {
		vec3_t d;
		VectorSubtract(self->s.origin, other->s.origin, d);
		VectorNormalize(d);
		float mass = self->mass > 50 ? (float)self->mass : 50.0f;
		VectorMA(self->velocity, 50000.0f / mass, d, self->velocity);
		self->groundentity = nullptr;
		gi.dprintf("starting path_corner anim list:\n");
		PrintAnimList("path_corner", "", "end path_corner anim list, starting point_combat anim list");
		PrintAnimList("point_combat", "", "end point_combat anim list");
	}
}

static void AI_Use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	AI *ai = self->ai;
	if (!ai)
		return;
	if (CvarValue(cv.ai_trace))
		Dbg("ai %d: used, activator %d, awake %d, spawnflags %d\n", G_EdictNum(self),
		    activator ? G_EdictNum(activator) : -1, ai->awake, self->spawnflags);
	if (!ai->awake && (self->spawnflags & SF_DORMANT)) {
		edict_t *e = nullptr;
		while ((e = findradius(e, self->s.origin, 40)) != nullptr)
			if (e != self && e->ai && (e->flags & FL_SPAWNER_MADE))
				SilentRemove(e, e->ai);
		ai->Wake(self);
		return;
	}
	ai->AddDeferredSenses(self);
	if (!ai->awake)
		ai->Wake(self);
	if (activator) {
		ai->Alert(self, Now(), activator, activator->s.origin);
		ai->HearEvent(activator->s.origin, activator, activator->s.origin, PK_TOLD);
	}
}

namespace om_ai {

static edict_t *FindTargetEnt(const char *name)
{
	if (!name)
		return nullptr;
	return G_Find(nullptr, FOFS(targetname), name);
}

Decision *NewPathDecision(int kind, edict_t *first);

void StartCharacter(edict_t *ent)
{
	AI *ai = ent->ai;
	if (!ai)
		return;
	if (!ai->body && ent->g3body) {
		ai->body = ent->g3body;
		ai->body->owner = ai;
	}
	/* spawn-time decisions (rule 2) */
	edict_t *t = FindTargetEnt(ent->target);
	if (t && ClassnameIs(t, "path_corner"))
		ai->decisions.push_back(NewPathDecision(DK_PATH, t));
	else if (t && ClassnameIs(t, "point_combat")) {
		ai->decisions.push_back(NewPathDecision(DK_POINT, t));
		ai->call_for_help = 0;
	}
	edict_t *c = FindTargetEnt(ent->combattarget);
	if (c && ClassnameIs(c, "point_combat")) {
		ai->decisions.push_back(NewPathDecision(DK_POINT, c));
		ai->call_for_help = 0;
	}
	/* spawnflag 32768: the body uses protection class 0 (SPEC-700 rule 2; G3's setter is SetArmour) */
	if ((ent->spawnflags & SF_PROTECTION0) && ai->body)
		ai->body->SetArmour(0);
	if (ent->spawnflags & SF_HOSTAGE)
		ai->hostage = 1;
	if (ent->spawnflags & SF_NO_CALL_FOR_HELP)
		ai->call_for_help = 0;
	/* senses (SPEC-701 section 1) */
	if (!(ent->spawnflags & SF_DEAF))
		ai->senses.push_back(NewHearing());
	if (!(ent->spawnflags & SF_BLIND))
		ai->senses.push_back(NewSight(3));
	float r = ent->alertradius;
	if (r < 0.1f)
		r = 75.0f;
	ai->alertradius = r;
	ai->senses.push_back(NewProximity(r));
	if (CvarValue(cv.ai_pathtest))
		ai->senses.push_back(NewOmniscience());
	if (ent->spawnflags & SF_STATIONARY)
		ent->flags |= FL_NO_KNOCKBACK;
	/* reactions (rule 4) */
	ent->touch = AI_Touch;
	ent->use = AI_Use;
	ent->plUse = AI_PlayerUse;
	ent->think = CharacterThink;
	ai->first_think_done = 1;
}

void AddDecisionSet(AI *ai, int set)
{
	static const int sets[10][6] = {
		{2, 6}, {2, 3, 4, 6, 5}, {2, 3, 4, 6, 5}, {2}, {11, 12, 13, 14, 5}, {0}, {1}, {0}, {2, 3, 4, 6}, {2, 3, 6}};
	if (set < 0 || set > 9)
		return;
	for (int i = 0; i < 6 && sets[set][i]; i++)
		ai->decisions.push_back(MakeDecision(sets[set][i]));
	if (set == SET_C_FIGHTER_RANK3 || set == SET_G_DEKKER)
		ai->rank = 3;
	if (set == SET_UNUSED_RANK2)
		ai->rank = 2;
	if (set == SET_G_DEKKER)
		ai->senses.push_back(NewOmniscience());
}

void RemoveAllComponents(edict_t *ent)
{
	if (ent && ent->ai)
		ent->ai->ClearAll(false);
}

void FreeCharacter(edict_t *ent)
{
	AI *ai = ent->ai;
	RemoveHelper(ent, ai);
	if (ai) {
		ent->ai = nullptr;
		for (size_t i = 0; i < mgr.all.size(); i++)
			if (mgr.all[i] == ai) {
				mgr.all.erase(mgr.all.begin() + i);
				break;
			}
		if (ai->body)
			ai->body->owner = nullptr;
		delete ai;
	}
	Body_Destroy(ent);
	G_FreeEdict(ent);
}

bool HasAI(edict_t *ent) { return ent && ent->inuse && ent->ai != nullptr; }

} /* namespace om_ai */

/* ------------------------------------------------------------------------------------------------------------------
 * damage, pain and death (SPEC-700 rule 10; the damage rule itself runs in G3's Dmg_CharacterDamage, which calls
 * the AI parts below)
 * ---------------------------------------------------------------------------------------------------------------- */
void OmAI::OnBetrayed(edict_t *attacker)
{
	(void)attacker;
	perceive_teammates = 1;
}

void OmAI::OnHitAlert(edict_t *attacker)
{
	if (attacker)
		HearEvent(attacker->s.origin, attacker, attacker->s.origin, PK_NOISE);
}

void OmAI::OnHit(void) { alerted = 1; }

void OmAI::Pain(edict_t *attacker, const vec3_t point, int knockback, int damage)
{
	edict_t *ent = owner;
	if (!ent || ent->health <= 0 || !body)
		return;
	if (attacker && attacker->client && buddy && gix.SP_Print)
		gix.SP_Print(attacker, 0x601);
	const G3Anim *rec = body->ChoosePain(attacker, point, knockback, damage);
	if (!rec)
		return;
	static vec3_t zero = {0, 0, 0};
	Action *a = MakeAction(this, 2, nullptr, current, rec, ent->s.origin, zero, 10.0f, true);
	a->target = ent;
	Interrupt(ent, a);
}

void OmAI::Die(edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, int dflags)
{
	(void)inflictor;
	(void)damage;
	edict_t *ent = owner;
	if (!ent)
		return;
	bool was_dead = dead != 0;
	float now = Now();
	/* 1. */
	if (ent->health < -999)
		ent->health = -999;
	if (ent->health > 0)
		ent->health = 0;
	/* 2. */
	RemoveHelper(ent, this);
	if (attacker)
		AlertSound(attacker, ent->s.origin, PK_DEATH, ent, 600, (dflags & 0x1000000) != 0, true);
	/* 3. */
	edict_t *sc = SightClient();
	if (!was_dead && !(sc && SameTeam(ent, sc)))
		DangerNotice(1, ent->s.origin, 128);
	/* 4. statistics */
	if (!was_dead && !Cinematic()) {
		if (!(sc && SameTeam(ent, sc))) {
			g_stats.enemies_killed++;
			level.killed_monsters++;
			float f = game.difficulty_score > 0 ? game.difficulty_score : 1.0f;
			int cash = (int)(profile[6] * f); /* integer, truncated (SPEC-AMEND-005 G2.7, SPEC-AMEND-007 G18.41) */
			g_stats.score = (float)(int)(g_stats.score + profile[6] * f);
			Stats_Count(MSTAT_ENEMIES, 1);
			Stats_Count(MSTAT_CASH, cash);
		} else {
			g_stats.friendlies_killed++;
			Stats_Count(MSTAT_FRIENDLIES, 1);
		}
	}
	/* 5. kill targets */
	if (!was_dead && ent->killtarget && KillTargetFacingOK(ent)) {
		bool will_kill = false;
		edict_t *t = nullptr;
		bool any = false;
		while ((t = G_Find(t, FOFS(targetname), ent->killtarget)) != nullptr) {
			any = true;
			if (ClassnameIs(t, "script_runner") && (t->spawnflags & 1))
				will_kill = true;
			if (t->use)
				t->use(t, ent, ent);
		}
		if (!any)
			gi.dprintf("Couldn't find killtarget %s\n", ent->killtarget);
		if (will_kill) {
			gi.dprintf("%s is running a WILL_KILL_USER script\n", ent->targetname ? ent->targetname : "");
			ent->health = ent->max_health;
			ClearActions(false);
			VectorClear(ent->velocity);
			return;
		}
	}
	/* 6. corpse */
	ent->solid = SOLID_CORPSE;
	ent->clipmask = 0x4010003;
	ent->svflags |= SVF_DEADMONSTER;
	ent->movetype = MOVETYPE_STEP;
	ent->friction = 0.5f; /* SPEC-AMEND-006 G2.15: the corpse "gravity 0.5" is its ground friction */
	ent->enemy = attacker;
	if (body) {
		if (body->firstDeathTime <= 0)
			body->firstDeathTime = now;
		body->deathTime = now;
		if (point)
			VectorCopy(point, body->deathPoint);
	}
	if (was_dead && !((ent->spawnflags & SF_DIE_IN_PLACE) && current && current->ClassCode() == 5)) {
		gi.linkentity(ent);
		return;
	}
	/* 7. casualties */
	if (!was_dead)
		CasualtyRules(ent, this, attacker);
	/* 8. components */
	bool keep = (ent->spawnflags & SF_DIE_IN_PLACE) != 0;
	for (Sense *s : senses)
		delete s;
	senses.clear();
	while (!decisions.empty())
		DestroyDecision(decisions.front());
	ClearActions(keep);
	/* 9. death animation */
	const G3Anim *rec = body ? body->ChooseDeath(attacker, inflictor, damage, dflags, nullptr) : nullptr;
	if (!rec)
		rec = Anim("std_stoknees_2bk_n_n");
	if (rec && rec->cls != ANIMCLS_DEATH)
		gi.dprintf("Death action %s not really a death\n", rec->name);
	static vec3_t zero = {0, 0, 0};
	Action *a = MakeAction(this, keep ? 4 : 3, nullptr, nullptr, rec, ent->s.origin, zero, 9999.0f, false);
	Interrupt(ent, a);
	/* 10. */
	ent->touch = nullptr;
	ent->deadflag = DEAD_DEAD;
	dead = 1;
	gi.sound(ent, CHAN_VOICE, 0, 0.6f, 1, 0);
	VectorCopy(ent->s.angles, death_angles);
	gi.linkentity(ent);
}

/* ------------------------------------------------------------------------------------------------------------------
 * queries of the body (G3BodyOwner)
 * ---------------------------------------------------------------------------------------------------------------- */
edict_t *OmAI::Target(void)
{
	if (target && target->inuse)
		return target;
	return nullptr;
}

bool OmAI::TargetHostileArmed(void)
{
	edict_t *t = Target();
	if (!t || t->health <= 0 || !owner)
		return false;
	if (SameTeam(owner, t))
		return false;
	if (t->ai && t->g3body)
		return t->g3body->RightHandType() != 0;
	return t->client != nullptr;
}

float OmAI::FlashBlindness(void) { return MostMuted(Now(), 2); }

bool OmAI::JustNoticedEnemy(void)
{
	edict_t *t = Target();
	if (!t || !owner || SameTeam(owner, t)) {
		just_noticed = 0; /* nothing to be startled by any more */
		return false;
	}
	return just_noticed != 0;
}

void OmAI::OnStartled(void) { just_noticed = 0; }

/* SPEC-AMEND-005 G2.5: the footstep "movement value" is the foot's previously stored heel height; the body asks for it
 * right after reporting the heel (OnHeel below), so this is the value before that report */
float OmAI::MoveValue(void) { return heel_prev; }

/* the reference height of the footstep rule is the stored floor height (entity space, like the heel bolt; G2.5) */
float OmAI::ReferenceHeight(void) { return floor_height; }

bool OmAI::HasKiller(void) { return owner && owner->enemy != nullptr; }

bool OmAI::IsDesignated(void) { return owner && owner == DesignatedCharacter(); }

/* SPEC-AMEND-005 G2.3: the quake_ground event stores the floor height while the character has a ground entity or is
 * a corpse (solid 5) */
bool OmAI::FeetFollowFloor(void) { return owner && (owner->groundentity || owner->solid == SOLID_CORPSE); }

float OmAI::RefireFactor(void) { return Skill('d'); }

void OmAI::OnLook(const vec3_t angles, float eye_height)
{
	VectorCopy(angles, look_angles);
	if (owner)
		owner->viewheight = (int)eye_height;
	if (owner && (CvarValue(cv.ai_trace) >= 4 || (CvarValue(cv.ai_trace) >= 3 && ((int)(Now() * 10 + 0.5f)) % 10 == 0)))
		Dbg("ai %d look: yaw %.0f (entity %.0f) pitch %.0f eye %.0f\n", G_EdictNum(owner), look_angles[YAW],
		    owner->s.angles[YAW], look_angles[PITCH], eye_height);
}

/* SPEC-AMEND-005 G2.5: one stored value per foot (record 28 right, 32 left; 0 at creation). h = heel height above
 * the stored floor height; v = the stored value: v <= 9 -> store h; v > 9 and h < 7 -> store h (the body makes the
 * footstep); v > 9 and h >= 7 -> keep v. A step is made each time a heel raised more than 9 units comes below 7. */
void OmAI::OnHeel(int side, float height)
{
	float &v = side == 0 ? heel_r : heel_l;
	float h = height - floor_height;
	heel_prev = v;
	if (v <= 9 || h < 7)
		v = h;
}

void OmAI::OnDropWeaponNote(void)
{
	if (!owner)
		return;
	owner->spawnflags &= ~SF_KILLTARGET_ON_RETREAT;
	if (!(owner->spawnflags & SF_KEEP_WEAPON) && body)
		body->DropWeapons();
}

void OmAI::OnDogBite(edict_t *victim)
{
	if (victim && victim->ai && owner)
		victim->ai->Alert(victim, Now(), owner, owner->s.origin);
}
