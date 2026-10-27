/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-700 ("Character helper rules": sight client and sight character, character manager, noise
 * broadcast, danger notice, voice throttles, trigger_sound; rules 9, 11, 12, 13 (teams), 16 (helper entity)),
 * SPEC-701 sections 2, 3, 4 (muting sources), 6 (alert sounds), 6a (alert candidates), SPEC-710 E1 (shot notices),
 * SPEC-751 1.4 step 3 (cinematic actors), SPEC-740 sections 3/4 (calls into G5's navigation)
 * Owner: package G2.
 */
#include "ai_local.h"
#include "../effects/fx.h"      /* G7: fx::g_ai_noise (prop and environ explosions) */
#include "../effects/ambient.h" /* G7: ambient::g_angry_count (automatic music rule) */

void G_TriggerSoundNoise(edict_t *source, const vec3_t where); /* g_trigger.cpp (G18), SPEC-761 5.9 */

namespace om_ai {

Manager mgr;
MissionStats g_stats;
void (*g_report_ai_done)(int done_id) = nullptr;

/* ------------------------------------------------------------------------------------------------------------------
 * level and frame
 * ---------------------------------------------------------------------------------------------------------------- */
void ManagerReset(void)
{
	for (AI *ai : mgr.all)
		delete ai;
	mgr.all.clear();
	mgr.sight_client = nullptr;
	mgr.sight_character = nullptr;
	mgr.num_active = 0;
	mgr.awake_in_pvs = 0;
	mgr.priority_time = 0;
	mgr.nearest = nullptr;
	mgr.wake_voice_time = -100;
	mgr.tactical_voice_time = -100;
	mgr.tactical_category = 0;
	mgr.num_trigger_sounds = 0;
	mgr.level_number = LevelNumber();
}

/* G7's character noise alert (prop explosions, environ_explosion): the noise broadcast of SPEC-700 */
static void PropNoise(edict_t *source, const vec3_t where, float loudness, float radius)
{
	(void)source;
	ReportNoise(where, loudness, radius);
}

/* G7's music rule (SPEC-771 B.5): living characters within 600 units of the player showing the angry face
 * (expression state 3, SPEC-750 12.5) */
static int AngryCount(edict_t *player)
{
	if (!player)
		return 0;
	int n = 0;
	for (AI *ai : mgr.all) {
		edict_t *e = ai->owner;
		if (!e || !e->inuse || e->ai != ai || e->health <= 0 || !ai->body || ai->body->expr != 3)
			continue;
		if (Dist2(e->s.origin, player->s.origin) <= 600.0f * 600.0f)
			n++;
	}
	return n;
}

/* G3's explosion noise (gore and body explosions): the explosion stimulus (hearing muting) */
static void BodyExplosionNoise(const vec3_t at, float radius, float strength) { ReportNoise(at, strength, radius); }

void ManagerAdd(AI *ai) { mgr.all.push_back(ai); }
std::vector<AI *> &ManagerAll(void) { return mgr.all; }

void LevelStart(void)
{
	RegisterCvars();
	/* the entities of the previous level are gone: their AI objects are released here */
	ManagerReset();
	Nav_SetDoorPredicate(NavDoorPredicate);
	InstallFireFallback();
	fx::g_ai_noise = PropNoise;
	ambient::g_angry_count = AngryCount;
	g3hooks.explosionNoise = BodyExplosionNoise;
}

edict_t *SightClient(void) { return mgr.sight_client; }
edict_t *SightCharacter(void) { return mgr.sight_character; }
edict_t *DesignatedCharacter(void) { return mgr.nearest; }

/* AI objects whose entity was freed by other code (killtarget, remove commands, level code) are released */
static void ReapOrphans(void)
{
	for (size_t i = 0; i < mgr.all.size();) {
		AI *ai = mgr.all[i];
		edict_t *e = ai->owner;
		if (!e || !e->inuse || e->ai != ai) {
			mgr.all.erase(mgr.all.begin() + i);
			if (e && e->ai == ai)
				e->ai = nullptr;
			/* the helper made at its last think would otherwise stay in use for the rest of the level */
			edict_t *h = ai->helper;
			if (h && h->inuse && (h->flags & FL_AI_HELPER) && h->owner == e)
				G_FreeEdict(h);
			ai->helper = nullptr;
			delete ai;
		} else
			i++;
	}
}

/* SPEC-700 "Sight client and sight character" / SPEC-701 section 3 */
static void AdvanceSight(void)
{
	int maxc = game.maxclients;
	edict_t *sc = mgr.sight_client;
	int start = sc ? G_EdictNum(sc) : maxc;
	mgr.sight_client = nullptr;
	for (int k = 1; k <= maxc; k++) {
		int n = (start - 1 + k) % maxc + 1;
		edict_t *e = &g_edicts[n];
		if (e->inuse && e->client) {
			mgr.sight_client = e;
			break;
		}
	}
	int first = maxc + 1, count = globals.num_edicts - first;
	if (count <= 0) {
		mgr.sight_character = nullptr;
		return;
	}
	int cur = mgr.sight_character ? G_EdictNum(mgr.sight_character) : globals.num_edicts - 1;
	mgr.sight_character = nullptr;
	for (int k = 1; k <= count; k++) {
		int n = first + ((cur - first + k) % count + count) % count;
		edict_t *e = &g_edicts[n];
		if (e->inuse && e->health > 0 && !(e->flags & FL_NOTARGET) && e->ai && e->ai->awake) {
			mgr.sight_character = e;
			break;
		}
	}
}

static float WeightedDist2(const vec3_t a, const vec3_t b, float zw)
{
	float x = a[0] - b[0], y = a[1] - b[1], z = a[2] - b[2];
	return x * x + y * y + zw * z * z;
}

void ManagerFrame(void)
{
	edict_t *sc = mgr.sight_client;
	mgr.num_active = 0;
	mgr.awake_in_pvs = 0;
	if (!sc)
		return;
	float now = Now();
	if (!CvarValue(cv.ai_nonodes))
		Nav_PlayerFrame(sc);
	/* every frame: the active list (SPEC-701 6a) */
	for (int n = game.maxclients + 1; n < globals.num_edicts; n++) {
		edict_t *e = &g_edicts[n];
		if (!e->inuse || !e->ai || e->health < 1 || (e->spawnflags & SF_DORMANT))
			continue;
		AI *ai = e->ai;
		if (!ai->awake && WeightedDist2(e->s.origin, sc->s.origin, 16) >= 1024.0f * 1024.0f)
			continue;
		if (mgr.num_active < MAX_ACTIVE)
			mgr.active[mgr.num_active++] = e;
		if (ai->awake) {
			if (!CvarValue(cv.ai_nonodes))
				Nav_RefreshCache(&ai->nav, e);
			if (InPVS(e->s.origin, sc->s.origin))
				mgr.awake_in_pvs++;
		}
	}
	/* every 0.5 s: priorities and the designated character */
	if (mgr.priority_time > now + 0.1f)
		mgr.priority_time = now - 0.6f;
	if (now - mgr.priority_time < 0.5f)
		return;
	mgr.priority_time = now;
	bool all_high = CvarValue(cv.ai_highpriority) != 0;
	int high = 0;
	float best = 999999.0f;
	mgr.nearest = nullptr;
	edict_t *hidden[MAX_ACTIVE];
	float hd[MAX_ACTIVE];
	int nh = 0;
	for (int i = 0; i < mgr.num_active; i++) {
		edict_t *e = mgr.active[i];
		AI *ai = e->ai;
		if (!ai->awake)
			continue;
		float d = WeightedDist2(e->s.origin, sc->s.origin, 9);
		if (d < best) {
			best = d;
			mgr.nearest = e;
		}
		if (InPVS(e->s.origin, sc->s.origin)) {
			ai->low_priority = 0;
			high++;
		} else {
			ai->low_priority = all_high ? 0 : 1;
			if (!all_high) {
				hidden[nh] = e;
				hd[nh++] = d;
			} else
				high++;
		}
	}
	while (high < 7 && nh > 0) {
		int bi = 0;
		for (int i = 1; i < nh; i++)
			if (hd[i] < hd[bi])
				bi = i;
		hidden[bi]->ai->low_priority = 0;
		high++;
		hidden[bi] = hidden[nh - 1];
		hd[bi] = hd[nh - 1];
		nh--;
	}
}

void FrameBegin(void)
{
	if (!cv.ai_freeze)
		RegisterCvars();
	ReapOrphans();
	if (CvarValue(cv.ai_savetest) && level.framenum == 200)
		SaveSelfTest();
	if (CvarValue(cv.ai_trace) && level.framenum == 20)
		Dbg("ai: skill %g toughness %d T-a..T-d %g %g %g %g\n", CvarValue(gi.cvar("skill", "2", 0)), game.skill_toughness,
		    Skill('a'), Skill('b'), Skill('c'), Skill('d'));
	Body_CorpseFrameBegin();
	AdvanceSight();
	ManagerFrame();
}

/* ------------------------------------------------------------------------------------------------------------------
 * teams (SPEC-700 section 13)
 * ---------------------------------------------------------------------------------------------------------------- */
static int TeamOfChar(edict_t *e)
{
	TypeInfo ti;
	if (e->ai && GetTypeInfo(e->ai->type_code, &ti))
		return ti.team;
	return 1;
}

bool SameTeam(edict_t *a, edict_t *b)
{
	if (!a || !b)
		return false;
	bool ca = a->ai != nullptr, cb = b->ai != nullptr;
	if ((ca && a->g3body && a->g3body->Ignored()) || (cb && b->g3body && b->g3body->Ignored()))
		return false;
	if (ca && cb)
		return TeamOfChar(a) == TeamOfChar(b);
	if (ca || cb) {
		edict_t *c = ca ? a : b;
		edict_t *p = ca ? b : a;
		if (!p->client)
			return false;
		if (TeamOfChar(c) != 0)
			return false;
		if (deathmatch && deathmatch->value) {
			const char *tn = Info_ValueForKey(p->client->pers.userinfo, "teamname");
			return tn && !Q_stricmp(tn, "mullins");
		}
		return true;
	}
	return OnSameTeam(a, b) != 0;
}

/* ------------------------------------------------------------------------------------------------------------------
 * stimuli
 * ---------------------------------------------------------------------------------------------------------------- */
void ReportNoise(const vec3_t center, float loudness, float radius)
{
	if (radius <= 0)
		return;
	float now = Now();
	for (int n = game.maxclients + 1; n < globals.num_edicts; n++) {
		edict_t *e = &g_edicts[n];
		if (!e->inuse || !e->ai || e->health <= 0)
			continue;
		vec3_t c;
		BoxCenter(e, c);
		float d = sqrtf(Dist2(c, center));
		if (d > radius)
			continue;
		float s = (1.0f - d / radius) * loudness;
		if (s > 100)
			s = 100;
		if (s <= 0)
			continue;
		e->ai->MuteSenses(now, 4, s, 2, s);
	}
}

void MuteCharacter(edict_t *ent, int mask, float level_, int mode, float duration)
{
	if (ent && ent->ai)
		ent->ai->MuteSenses(Now(), mask, level_, mode, duration);
}

void FlashMute(const vec3_t origin, float radius)
{
	float now = Now();
	for (int n = game.maxclients + 1; n < globals.num_edicts; n++) {
		edict_t *e = &g_edicts[n];
		if (!e->inuse || !e->ai || e->health <= 0)
			continue;
		vec3_t eye, d, f;
		EyePoint(e, eye);
		trace_t tr = Trace(origin, nullptr, nullptr, eye, nullptr, 0x6000001);
		if (tr.fraction < 1.0f && tr.ent != e)
			continue;
		VectorSubtract(origin, e->s.origin, d);
		float dist = VectorNormalize(d);
		if (dist < 1)
			dist = 1;
		AngleVectors(e->ai->look_angles, f, nullptr, nullptr);
		float l = (DotProduct(f, d) + 1.25f) * (radius * 0.5f) / dist;
		if (l > 25)
			l = 25;
		if (l <= 0)
			continue;
		e->ai->MuteSenses(now, 2, l, 2, l);
	}
}

/* SPEC-701 section 6 */
void AlertSound(edict_t *emitter, const vec3_t pos, int kind, edict_t *target, float radius, bool needs_sight,
                bool tell_decisions)
{
	if (deathmatch && deathmatch->value)
		return;
	if (!emitter || (emitter->flags & FL_NOTARGET))
		return;
	if (kind == PK_DIRECTED) {
		if (target && target->ai)
			target->ai->HearEvent(pos, emitter, emitter->s.origin, kind);
		return;
	}
	edict_t *seen = target ? target : emitter;
	for (int i = 0; i < mgr.num_active; i++) {
		edict_t *e = mgr.active[i];
		if (!e->inuse || !e->ai || e->health <= 0 || e->deadflag)
			continue;
		if (Dist2(e->s.origin, pos) > radius * radius)
			continue;
		if (needs_sight && (!ConeTest(e, e->ai, seen) || !Visible(e, seen, false)))
			continue;
		e->ai->HearEvent(pos, emitter, emitter->s.origin, kind);
		if (tell_decisions)
			for (Decision *d : e->ai->decisions)
				d->AlertSoundFrom(e, e->ai, emitter->s.origin, emitter);
	}
	if (!needs_sight)
		G_TriggerSoundNoise(emitter, pos);
}

void DangerNotice(int kind, const vec3_t pos, float radius)
{
	for (int i = 0; i < mgr.num_active; i++) {
		edict_t *e = mgr.active[i];
		if (!e->inuse || !e->ai)
			continue;
		if (Dist2(e->s.origin, pos) > radius * radius)
			continue;
		for (Decision *d : e->ai->decisions)
			d->Danger(e, e->ai, kind, pos);
	}
}

/* SPEC-710 E1: a player's bullet hit the world */
void ShotAtNotice(edict_t *shooter, const vec3_t start, const vec3_t end)
{
	if (deathmatch && deathmatch->value)
		return;
	if (!shooter || !shooter->client)
		return;
	vec3_t sdir;
	VectorSubtract(end, start, sdir);
	VectorNormalize(sdir);
	static float lookout_time = -100;
	for (int i = 0; i < mgr.num_active; i++) {
		edict_t *e = mgr.active[i];
		if (!e->inuse || !e->ai || e->health <= 0)
			continue;
		vec3_t to;
		VectorSubtract(e->s.origin, shooter->s.origin, to);
		float dist = VectorNormalize(to);
		float dot = DotProduct(to, sdir);
		if (dist < 800 && dot > 0.9397f) {
			if (ClassnameIs(e, "m_x_mmerc") && Now() - lookout_time > 10.0f) {
				lookout_time = Now();
				gi.sound(e, CHAN_VOICE, gi.soundindex("impact/Hawk/LookOut.adp"), 1, ATTN_NORM, 0);
				continue;
			}
			for (Decision *d : e->ai->decisions)
				d->ShotAt(e, e->ai, start, end, shooter);
			e->ai->HearEvent(shooter->s.origin, shooter, shooter->s.origin, PK_DIRECTED);
		} else if (dot > 0 && dot <= 0.9397f &&
		           (InPVS(shooter->s.origin, e->s.origin) || InPVS(end, e->s.origin))) {
			/* original defect: the direction is passed as the position (UNVERIFIED in play) */
			for (Decision *d : e->ai->decisions)
				d->AlertSoundFrom(e, e->ai, to, shooter);
		}
	}
}

/* SPEC-700 voice throttles */
bool VoiceAllowed(int category)
{
	float now = Now();
	if (category == 0) {
		if (mgr.wake_voice_time > now + 0.1f)
			mgr.wake_voice_time = now - 3;
		if ((int)now - (int)mgr.wake_voice_time <= 2)
			return false;
		mgr.wake_voice_time = now;
		return true;
	}
	if (mgr.tactical_voice_time > now + 0.1f)
		mgr.tactical_voice_time = now - 3;
	if ((int)now - (int)mgr.tactical_voice_time <= 2 || category == mgr.tactical_category)
		return false;
	mgr.tactical_voice_time = now;
	mgr.tactical_category = category;
	return true;
}

/* ------------------------------------------------------------------------------------------------------------------
 * helper entity (SPEC-700 rule 16)
 * ---------------------------------------------------------------------------------------------------------------- */
void RemoveHelper(edict_t *ent, AI *ai)
{
	(void)ent;
	if (ai && ai->helper) {
		if (ai->helper->inuse && (ai->helper->flags & FL_AI_HELPER) && ai->helper->owner == ent)
			G_FreeEdict(ai->helper);
		ai->helper = nullptr;
	}
}

void MakeHelper(edict_t *ent, AI *ai)
{
	edict_t *h = G_Spawn();
	h->classname = (char *)"Nug Buddy";
	h->solid = SOLID_NOT;
	h->clipmask = 0x2030003;
	h->svflags = 0x82;
	h->owner = ent;
	h->flags |= FL_AI_HELPER;
	VectorCopy(ent->s.origin, h->s.origin);
	VectorCopy(ent->mins, h->mins);
	VectorCopy(ent->maxs, h->maxs);
	h->mins[0] -= 8;
	h->mins[1] -= 8;
	h->maxs[0] += 8;
	h->maxs[1] += 8;
	/* not linked: linked, the original engine reports these non-solid boxes to shot traces (seen in G2's runs: a
	 * character's bullet stopped on its target's helper), and the spec does not say they are linked (OPEN_QUESTIONS
	 * G2 8) */
	/* SPEC-AMEND-005 G2.8: appended to the character's circular helper list (G18's helper link, SPEC-780 15); the
	 * free in RemoveHelper unlinks it (G_FreeEdict) */
	G_HelperAppend(ent, h);
	ai->helper = h;
}

/* ------------------------------------------------------------------------------------------------------------------
 * silent removal (rule 9) and casualties (rule 11)
 * ---------------------------------------------------------------------------------------------------------------- */
void SilentRemove(edict_t *ent, AI *ai)
{
	edict_t *sc = SightClient();
	if (sc && InPVS(ent->s.origin, sc->s.origin))
		return;
	ai->view_mode = 0;
	ent->health = 0;
	if (ent->killtarget && KillTargetFacingOK(ent)) {
		edict_t *t = nullptr;
		bool any = false;
		while ((t = G_Find(t, FOFS(targetname), ent->killtarget)) != nullptr) {
			any = true;
			if (t->use)
				t->use(t, ent, ent);
		}
		if (!any)
			gi.dprintf("Couldn't find killtarget %s\n", ent->killtarget);
	}
	ent->solid = SOLID_NOT;
	ent->clipmask = 0;
	ent->svflags |= SVF_NOCLIENT;
	ent->movetype = MOVETYPE_NONE;
	if (ai->body)
		ai->body->deathTime = ai->body->firstDeathTime = Now() - 25;
	gi.linkentity(ent);
	if (!ai->dead) {
		ai->ClearAll(false);
		ent->touch = nullptr;
		ent->deadflag = DEAD_DEAD;
		ai->dead = 1;
	}
}

void CasualtyRules(edict_t *victim, AI *ai, edict_t *attacker)
{
	TypeInfo ti;
	bool innocent = GetTypeInfo(ai->type_code, &ti) && ti.innocent;
	bool by_player = attacker && attacker->client;
	int limit = level.maxdeadhostages;
	if (limit == 0)
		limit = 3;
	if (victim->classname && strstr(victim->classname, "swat") && by_player &&
	    !(victim->spawnflags & SF_DIE_IN_PLACE)) {
		if (limit == -1)
			limit = 0;
		level.deadhostages = limit + 1;
	} else if (ai->hostage || (innocent && by_player) || (ai->buddy && by_player))
		level.deadhostages++;
	g_stats.total_killed++;
}

void CountKill(edict_t *ent)
{
	(void)ent;
	g_stats.total_killed++;
}

/* ------------------------------------------------------------------------------------------------------------------
 * cinematic actors (SPEC-751 1.4 step 3)
 * ---------------------------------------------------------------------------------------------------------------- */
void CinematicEndActors(void)
{
	float now = Now();
	for (int n = 1; n < globals.num_edicts; n++) {
		edict_t *e = &g_edicts[n];
		if (!e->inuse || !e->ai)
			continue;
		if (e->count == 255) {
			e->count = 0;
			continue;
		}
		if (e->flags & FL_CINEMATIC_FROZEN) {
			e->flags &= ~FL_CINEMATIC_FROZEN;
			float delay = e->nextthink < 0 ? -e->nextthink : 0.1f;
			e->nextthink = now + delay;
			e->solid = SOLID_BBOX;
			gi.linkentity(e);
			if (e->ghoulInst)
				e->ghoulInst->SetOnOff(true, now);
		}
	}
}

} /* namespace om_ai */

namespace om_ai {
/* G_RunFrame entity loop (SPEC-760 B9.3): a character without fly/swim flags re-checks its ground */
void EntityGroundLost(edict_t *e)
{
	if (e && e->ai && !(e->flags & (FL_FLY | FL_SWIM)))
		CheckGround(e, e->ai);
}
} /* namespace om_ai */

namespace om_ai {
/* G_ExplosionStimulus: characters within `radius` get hearing muting (SPEC-700 "Noise broadcast") */
void ExplosionStimulus(const vec3_t center, float radius, float strength) { ReportNoise(center, strength, radius); }

/* G_ReportNoise: a player's weapon noise is an alert sound of kind 2 (SPEC-701 6); trigger_sound is G18's */
void NoiseAlert(edict_t *source, const vec3_t where, float radius)
{
	if (!source || !source->client || (source->flags & FL_NOTARGET))
		return;
	if (deathmatch && deathmatch->value)
		return;
	for (int i = 0; i < mgr.num_active; i++) {
		edict_t *e = mgr.active[i];
		if (!e->inuse || !e->ai || e->health <= 0 || e->deadflag)
			continue;
		if (Dist2(e->s.origin, where) > radius * radius)
			continue;
		e->ai->HearEvent(where, source, source->s.origin, PK_NOISE);
	}
}
} /* namespace om_ai */
