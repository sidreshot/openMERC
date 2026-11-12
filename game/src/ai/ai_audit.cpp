/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: no specification. A test aid that is not in the original (docs/DEVIATIONS.md "Test aids",
 *             docs/impl/AIAUDIT.md): the `omaudit` cheat command. It reads the state of the SPEC-700 / 701 / 710 /
 *             711 objects and changes none of their rules.
 *
 * Does every enemy of a level react to the player? `omaudit` puts the player in front of each living enemy of the
 * level in turn, wakes it as a trigger would and waits for it to attack or to come for him. Each result is one
 * console line that starts with OMAUDIT; a silent enemy also gets OMWHY lines (the sight tests of SPEC-701 section 5
 * one by one, its ground, its combat state) and the AI trace of its last two seconds between OMTRACE lines.
 * tests/AIAUDIT/audit_run.py runs it on any number of levels and audit_report.py sums the lines up.
 *
 * The player is invulnerable and in noclip for the whole check, so that the visits do not fire walk-in triggers, and
 * each enemy is removed after its turn: the level is used up afterwards. Nothing here runs unless the command was
 * given with cheats on.
 */
#include "ai_decisions.h"
#include "../scriptglue/g_scriptglue.h"

#include <vector>

namespace om_ai {

namespace {

const float REACT_LIMIT = 12.0f;   /* game seconds an enemy has to react in */
const float LEVEL_LIMIT = 1200.0f; /* game seconds for a whole level */
const float HOLD_LIMIT = 15.0f;    /* seconds a cut scene may hold the check before it is ended */

bool g_on = false;
std::vector<int> g_list;
std::vector<int> g_sf0; /* spawnflags at the start, before waking clears 16 and use clears 2 and 4 */
size_t g_next = 0;
edict_t *g_cur = nullptr;
float g_t0 = 0;
vec3_t g_p0;
int g_reacted = 0, g_silent = 0, g_nospot = 0, g_gone = 0;
float g_hold = 0, g_level_t0 = 0;
/* behind: the only place to stand was behind the enemy, so its silence proves nothing */
bool g_behind = false, g_noise = false, g_traced = false;

edict_t *Player(void)
{
	edict_t *p = &g_edicts[1];
	return (p->inuse && p->client) ? p : nullptr;
}

/* the command a finished level hands over to (cvar om_audit_next, e.g. "exec au_01.cfg"; empty: nothing) */
void RunNext(void)
{
	cvar_t *next = gi.cvar("om_audit_next", "", 0);
	if (next && next->string[0]) {
		char cmd[128];
		Com_sprintf(cmd, sizeof(cmd), "%s\n", next->string);
		gi.AddCommandString(cmd);
	}
}

/* a floor spot near the enemy from which it can be seen: in front of it first, then around it */
bool Spot(edict_t *e, vec3_t out, float *yaw)
{
	static const float rels[16] = {0, 22.5f, -22.5f, 45, -45, 67.5f, -67.5f, 90, -90, 112.5f, -112.5f, 135, -135,
	                               157.5f, -157.5f, 180};
	/* 300 and 36 last: they are for wide floors and for closed stalls and small rooms */
	static const float dist[8] = {220, 160, 130, 90, 60, 45, 300, 36};
	vec3_t eye;
	VectorCopy(e->s.origin, eye);
	eye[2] += 20;
	vec3_t pmins = {-16, -16, -24}, pmaxs = {16, 16, 32};
	for (int r = 0; r < 8; r++)
		for (int a = 0; a < 16; a++) {
			float ang = (e->s.angles[YAW] + rels[a]) * (float)M_PI / 180.0f;
			vec3_t p = {e->s.origin[0] + cosf(ang) * dist[r], e->s.origin[1] + sinf(ang) * dist[r], e->s.origin[2] + 16};
			trace_t t = gi.trace(eye, vec3_origin, vec3_origin, p, e, 1);
			if (t.fraction < 1.0f)
				continue;
			vec3_t d;
			VectorCopy(p, d);
			d[2] -= 300;
			t = gi.trace(p, pmins, pmaxs, d, e, 0x02010003);
			if (t.startsolid || t.allsolid || t.fraction >= 1.0f || t.plane.normal[2] < 0.7f ||
			    fabsf(t.endpos[2] - e->s.origin[2]) > 96.0f)
				continue;
			/* the enemy's real eyes to the player's after the drop to the floor: the first test is at a guessed
			 * height and would leave the player below a ledge, out of sight */
			vec3_t eyes, peye;
			EyePoint(e, eyes);
			VectorCopy(t.endpos, peye);
			peye[2] += 22;
			if (gi.trace(eyes, vec3_origin, vec3_origin, peye, e, 0x19).fraction < 1.0f)
				continue;
			VectorCopy(t.endpos, out);
			*yaw = atan2f(e->s.origin[1] - out[1], e->s.origin[0] - out[0]) * 180.0f / (float)M_PI;
			g_behind = a >= 9;
			return true;
		}
	return false;
}

void Place(edict_t *p, const vec3_t o, float yaw)
{
	VectorCopy(o, p->s.origin);
	VectorClear(p->velocity);
	VectorClear(p->s.angles);
	p->s.angles[YAW] = yaw;
	p->client->ps.pmove.delta_angles[YAW] = (short)ANGLE2SHORT(yaw - p->client->resp.cmd_angles[YAW]);
	p->client->ps.pmove.delta_angles[PITCH] = (short)ANGLE2SHORT(0.0f - p->client->resp.cmd_angles[PITCH]);
	p->client->ps.viewangles[YAW] = yaw;
	p->client->ps.viewangles[PITCH] = 0;
	VectorCopy(p->client->ps.viewangles, p->client->v_angle);
	gi.linkentity(p);
}

int StartFlags(edict_t *e)
{
	for (size_t k = 0; k < g_list.size() && k < g_sf0.size(); k++)
		if (g_list[k] == G_EdictNum(e))
			return g_sf0[k];
	return -1;
}

/* why a silent enemy did not see or fight the player: the OMWHY lines */
void ReportWhy(edict_t *e, AI *ai, edict_t *p)
{
	int n = G_EdictNum(e);
	/* the tests of SPEC-701 section 5, one by one */
	vec3_t a, b, f, d;
	EyePoint(e, a);
	EyePoint(p, b);
	trace_t tr = Trace(a, nullptr, nullptr, b, e, 0x19);
	AngleVectors(ai->look_angles, f, nullptr, nullptr);
	VectorSubtract(p->s.origin, e->s.origin, d);
	float dist = VectorNormalize(d);
	gi.cprintf(nullptr, PRINT_HIGH,
	           "OMWHY %s ent=%d dist=%.0f pvs=%d%d trace=%.2f hit=%s cone=%.2f view=%d mask=%d senses=%d sightclient=%d "
	           "target=%d team_same=%d inattentive=%d\n",
	           level.mapname, n, dist, InPVS(a, b) ? 1 : 0, InPVS(b, a) ? 1 : 0, tr.fraction,
	           tr.ent && tr.ent->classname ? tr.ent->classname : "-", DotProduct(f, d), ai->view_mode, ai->sense_mask,
	           (int)ai->senses.size(), SightClient() ? G_EdictNum(SightClient()) : -1,
	           ai->target ? G_EdictNum(ai->target) : -1, SameTeam(e, p) ? 1 : 0,
	           (e->spawnflags & SF_INATTENTIVE) ? 1 : 0);
	Percept pp = ai->NewestPlayer();
	gi.cprintf(nullptr, PRINT_HIGH,
	           "OMWHY2 %s ent=%d seen_ent=%d seen_age=%.1f seen_kind=%d filter=%d conetest=%d yaw=%.0f lookyaw=%.0f "
	           "safe=%d alerted=%d prio=%d\n",
	           level.mapname, n, pp.ent ? G_EdictNum(pp.ent) : -1, pp.ent ? Now() - pp.time : -1.0f, pp.kind,
	           PerceiveFilter(e, ai, p) ? 1 : 0, ConeTest(e, ai, p, false) ? 1 : 0, e->s.angles[YAW],
	           ai->look_angles[YAW], (p->svflags & SVF_SAFEZONE) ? 1 : 0, ai->alerted, ai->low_priority ? 1 : 0);
	CombatDecision *cd = static_cast<CombatDecision *>(ai->FindDecision(DK_COMBAT));
	gi.cprintf(nullptr, PRINT_HIGH,
	           "OMWHY3 %s ent=%d ground=%d combat_target=%d armed=%d z=%.1f mins=%.1f flags=%x vel=%.0f\n",
	           level.mapname, n, e->groundentity ? G_EdictNum(e->groundentity) : -1,
	           cd && cd->target ? G_EdictNum(cd->target) : -1, (!ai->body || Armed(ai)) ? 1 : 0, e->s.origin[2],
	           e->mins[2], e->flags, VectorLength(e->velocity));
	float mute = 0;
	for (Sense *sn : ai->senses)
		if (sn->ClassCode() == 2 || sn->ClassCode() == 3)
			mute = sn->MuteLevel(Now());
	edict_t *bl = cd && cd->blocker > 0 ? &g_edicts[cd->blocker] : nullptr;
	gi.cprintf(nullptr, PRINT_HIGH,
	           "OMWHY4 %s ent=%d sight_mute=%.2f clear=%d blocker=%d(%s) target_age=%.1f reaction_age=%.1f\n",
	           level.mapname, n, mute, cd ? cd->clear_shot_result : -1, cd ? cd->blocker : -2,
	           bl && bl->classname ? bl->classname : "-", cd ? Now() - cd->target_time : -1.0f,
	           Now() - ai->reaction_time);
	/* its ground: the box traced 8 units down (the SPEC-700 ground check uses 0.25) and what it starts in */
	vec3_t down;
	VectorCopy(e->s.origin, down);
	down[2] -= 8;
	trace_t gt = Trace(e->s.origin, e->mins, e->maxs, down, e, 0x2020003);
	/* which fire, aim and move records the body has (SPEC-722 attack and move lists) */
	static const char *const recs[] = {"std_f_fwd_m_2", "std_fautosweep_n_m_2", "std_f_fwd_r_2", "std_f_fwd_s_2",
	                                   "std_f_fwd_p_2", "std_f_n_p_2", "std_f_fwd_l_2", "cch_f_fwd_m_2",
	                                   "cch_f_fwd_r_2", "cch_f_fwd_s_2", "std_a_fwd_ms_2", "std_a_fwd_mrs_2",
	                                   "std_a_fwd_r_2", "cch_raim_fwd_mrs_2", "std_rbackaim_n_mrs_2",
	                                   "std_raimstraf_rt_mrs_2", "std_r_n_mrs_2", "std_raim_n_mrs_2"};
	char have[512] = "";
	for (const char *name : recs) {
		const G3Anim *r = Anim(name);
		if (r && ai->body && ai->body->HasAnim(r)) {
			strncat(have, name, sizeof(have) - strlen(have) - 1);
			strncat(have, ",", sizeof(have) - strlen(have) - 1);
		}
	}
	gi.cprintf(nullptr, PRINT_HIGH,
	           "OMWHY5 %s ent=%d down=%.2f start=%d all=%d hit=%s(%d) normal=%.2f rht=%d pflags=%d recs=%s\n",
	           level.mapname, n, gt.fraction, gt.startsolid ? 1 : 0, gt.allsolid ? 1 : 0,
	           gt.ent && gt.ent->classname ? gt.ent->classname : "-", gt.ent ? G_EdictNum(gt.ent) : -1,
	           gt.plane.normal[2], ai->body ? ai->body->RightHandType() : -1, PFlags(ai), have[0] ? have : "-");
}

void Report(edict_t *e, const char *result, float t)
{
	AI *ai = e->ai;
	char kinds[64] = "";
	if (ai)
		for (Decision *d : ai->decisions) {
			char one[8];
			Com_sprintf(one, sizeof(one), "%d,", d->Kind());
			strncat(kinds, one, sizeof(kinds) - strlen(kinds) - 1);
		}
	Action *c = ai ? ai->current : nullptr;
	edict_t *p = Player();
	if (ai && p && !strcmp(result, "SILENT"))
		ReportWhy(e, ai, p);
	/* plight / clight: the player's light level on the entity and in the client record */
	gi.cprintf(nullptr, PRINT_HIGH,
	           "OMAUDIT %s %s t=%.1f ent=%d class=%s name=%s sf=%d pos=%.0f,%.0f,%.0f awake=%d alerted=%d dec=%s act=%d "
	           "rec=%s health=%d plight=%d clight=%d sf0=%d\n",
	           level.mapname, result, t, G_EdictNum(e), e->classname ? e->classname : "-",
	           e->targetname ? e->targetname : "-", e->spawnflags, e->s.origin[0], e->s.origin[1], e->s.origin[2],
	           ai ? ai->awake : -1, ai ? ai->alerted : -1, kinds[0] ? kinds : "-", c ? c->ClassCode() : -1,
	           c && c->record ? c->record->name : "-", e->health, p ? p->light_level : -1,
	           p && p->client ? p->client->lightlevel : -1, StartFlags(e));
}

void Finish(void)
{
	g_on = false;
	g_cur = nullptr;
	gix.cvar_set("om_audit_running", "0");
	gi.cprintf(nullptr, PRINT_HIGH, "OMAUDIT %s done total=%d reacted=%d silent=%d nospot=%d gone=%d\n", level.mapname,
	           (int)g_list.size(), g_reacted, g_silent, g_nospot, g_gone);
	RunNext();
}

/* the next enemy with a place to stand; false when the list is used up */
bool Begin(edict_t *p)
{
	while (g_next < g_list.size()) {
		edict_t *e = &g_edicts[g_list[g_next++]];
		if (!e->inuse || !e->ai || e->health <= 0) {
			g_gone++;
			continue;
		}
		vec3_t spot;
		float yaw;
		if (!Spot(e, spot, &yaw)) {
			Report(e, "NOSPOT", 0);
			g_nospot++;
			FreeCharacter(e);
			continue;
		}
		Place(p, spot, yaw);
		if (e->use && (!e->ai->awake || (e->spawnflags & (SF_DORMANT | SF_BLIND | SF_DEAF)))) {
			e->use(e, nullptr, nullptr);
			if (e->inuse && e->use && (e->spawnflags & (SF_BLIND | SF_DEAF)))
				e->use(e, nullptr, nullptr);
		}
		if (!e->inuse || !e->ai)
			continue;
		g_cur = e;
		g_t0 = level.time;
		VectorCopy(e->s.origin, g_p0);
		return true;
	}
	return false;
}

} // namespace

bool AuditActive(void) { return g_on; }

/* a level that ended itself during its check (a script or a trigger the guards did not catch): on to the next one.
 * Reads the cvar without creating it, so nothing happens in a session that never gave the command. */
void AuditLevelStart(void)
{
	g_cur = nullptr;
	if (!g_on && !gix.cvar_value("om_audit_running"))
		return;
	gi.cprintf(nullptr, PRINT_HIGH, "OMNOTE level changed during its audit\n");
	g_on = false;
	gix.cvar_set("om_audit_running", "0");
	gix.cvar_set("ai_trace", "0");
	RunNext();
}

void AuditStart(edict_t *player)
{
	if (!player || !player->client || deathmatch->value) {
		gi.cprintf(player, PRINT_HIGH, "omaudit: single player only\n");
		return;
	}
	g_list.clear();
	g_sf0.clear();
	g_next = 0;
	g_cur = nullptr;
	g_reacted = g_silent = g_nospot = g_gone = 0;
	g_behind = g_noise = g_traced = false;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (e->inuse && e->ai && e->health > 0 && e->g3body && !SameTeam(e, player) &&
		    e->ai->type_code != TYPE_HELICOPTER && e->ai->type_code != TYPE_TANK && e->ai->type_code != TYPE_SNOWCAT) {
			g_list.push_back(i);
			g_sf0.push_back(e->spawnflags);
		}
	}
	player->flags |= FL_GODMODE;
	player->flags &= ~FL_NOTARGET;
	player->movetype = MOVETYPE_NOCLIP;
	g_on = true;
	gix.cvar_set("om_audit_running", "1");
	g_hold = 0;
	g_level_t0 = level.time;
	gi.cprintf(nullptr, PRINT_HIGH, "OMAUDIT %s start enemies=%d\n", level.mapname, (int)g_list.size());
}

void AuditFrame(void)
{
	if (!g_on)
		return;
	edict_t *p = Player();
	if (!p) {
		g_on = false;
		return;
	}
	p->flags |= FL_GODMODE;
	p->movetype = MOVETYPE_NOCLIP;
	if (p->health <= 0)
		p->health = 100;
	/* a cut scene freezes alerted characters on purpose: the clock waits for it, but one that does not end (its
	 * actors may have been removed by earlier turns) is ended here */
	if (Cinematic()) {
		g_t0 += FRAMETIME;
		g_hold += FRAMETIME;
		if (g_hold >= HOLD_LIMIT) {
			gi.cprintf(nullptr, PRINT_HIGH, "OMNOTE %s hold cleared at %.0f\n", level.mapname, level.time);
			ScriptGlue_SetCinematicState(0);
			g_hold = 0;
		}
		return;
	}
	g_hold = 0;
	if (level.time - g_level_t0 > LEVEL_LIMIT) {
		gi.cprintf(nullptr, PRINT_HIGH, "OMNOTE %s level time limit, %d of %d enemies left\n", level.mapname,
		           (int)(g_list.size() - g_next), (int)g_list.size());
		if (g_traced)
			gix.cvar_set("ai_trace", "0");
		Finish();
		return;
	}
	if (!g_cur) {
		if (!Begin(p))
			Finish();
		return;
	}
	edict_t *e = g_cur;
	if (!e->inuse || !e->ai || e->health <= 0) {
		g_gone++;
		g_cur = nullptr;
		if (g_traced) {
			gix.cvar_set("ai_trace", "0");
			gi.cprintf(nullptr, PRINT_HIGH, "OMTRACE %s off\n", level.mapname);
		}
		g_traced = g_noise = false;
		return;
	}
	AI *ai = e->ai;
	float t = level.time - g_t0;
	vec3_t d;
	VectorSubtract(e->s.origin, g_p0, d);
	/* behind it: a noise from the player, as a footstep or a shot would make */
	if (g_behind && t >= 4.0f && !g_noise) {
		ai->HearEvent(p->s.origin, p, p->s.origin, PK_NOISE);
		g_noise = true;
	}
	/* the last two seconds of a silent turn are traced (ai_trace 2: the combat plan's range code) */
	if (t >= REACT_LIMIT - 2.0f && !g_traced) {
		gix.cvar_set("ai_trace", "2");
		gi.cprintf(nullptr, PRINT_HIGH, "OMTRACE %s ent=%d on\n", level.mapname, G_EdictNum(e));
		g_traced = true;
	}
	bool attacking = ai->current && ai->current->Kind() == AK_ATTACK;
	bool came = ai->alerted && VectorLength(d) > 48.0f;
	const char *rec = ai->current && ai->current->record ? ai->current->record->name : nullptr;
	if (attacking || came) {
		Report(e, attacking ? "ATTACK" : "CHASE", t);
		g_reacted++;
	} else if (rec && (strstr(rec, "afraid") || strstr(rec, "cower"))) {
		Report(e, "COWER", t);
		g_reacted++;
	} else if (t >= REACT_LIMIT) {
		bool armed = !ai->body || Armed(ai);
		Report(e, g_behind ? "BEHIND" : armed ? "SILENT" : "UNARMED", t);
		if (!g_behind && armed)
			g_silent++;
	} else
		return;
	if (g_traced) {
		gix.cvar_set("ai_trace", "0");
		gi.cprintf(nullptr, PRINT_HIGH, "OMTRACE %s off\n", level.mapname);
	}
	g_traced = g_noise = false;
	FreeCharacter(e);
	g_cur = nullptr;
}

} /* namespace om_ai */
