/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: no specification. A test aid that is not in the original (docs/DEVIATIONS.md "Test aids"): the script
 * audit. It reads the state of the script system (SPEC-750) and of the AI (SPEC-700 / 710 / 711) and changes none of
 * their rules.
 *
 * Do the characters do what the levels' scripts tell them? With cvar om_scriptaudit 1 every AI command of a script
 * that the script waits on (a done signal, SPEC-750 12.6) is followed from the command to its done report, and what
 * goes wrong is one console line that starts with OMSCRIPT:
 *   drop      the command went to an entity that is missing, has no AI or is dead (a script waiting on it hangs);
 *   done      the command completed: its time, and for a move how far from its destination the character stopped;
 *   stuck     no done report after the command's own duration + 10 s (20 s, 30 s for a move): the character's state;
 *   gone      the character died or was removed with the command pending;
 *   reset     the script took the character back (ai_reset) with commands pending;
 *   animsub   the command's animation is missing on the character's body and another one plays (ai_script.cpp).
 * The `omscriptrun` cheat command then runs every script of the level that has not run yet, one after the other, as
 * its trigger would (the runner is used by the player), and waits for each to end (40 s at most): OMSCRIPT run / ran
 * lines. The player is invulnerable, not a target and in noclip; level ends are held (AuditActive). It prints
 * OMAUDIT start / done lines like omaudit, so tests/AIAUDIT/audit_run.py --command omscriptrun runs it on any number of
 * levels.
 */
#include "ai_decisions.h"
#include "../scriptglue/g_scriptglue.h"
#include "../script/script_host.h"
#include "../script/script_vm.h"

#include <string>
#include <vector>

namespace om_ai {

int ScriptQueueLength(edict_t *ent); /* ai_script.cpp */

namespace {

const float RUN_LIMIT = 40.0f;  /* game seconds a script may take in omscriptrun */
const float RUN_GAP = 1.0f;     /* game seconds between two scripts */
const float RUN_SETTLE = 1.5f;  /* game seconds at the script's trigger before it runs (characters near it wake) */

struct Track {
	int done = 0;
	int ent = 0;
	std::string who, script, anim;
	int cmd = 0, kind = 0, count = 0, flags = 0;
	float t0 = 0, limit = 20;
	vec3_t dest = {0, 0, 0};
	bool hasdest = false, stuck = false, reset = false;
};

std::vector<Track> g_tracks;
std::vector<std::string> g_last_script; /* per entity: the script of its last command */
cvar_t *g_cv = nullptr;

/* omscriptrun */
bool g_run = false;
std::vector<int> g_runners;
size_t g_next = 0;
int g_cur = -1;            /* runner entity being waited on */
int g_pending = -1;        /* runner to use once the player has stood at its trigger for RUN_SETTLE */
float g_t0 = 0, g_wait_until = 0;
int g_ran = 0, g_finished = 0, g_timeout = 0, g_stuck = 0, g_drops = 0, g_subs = 0;

bool On(void)
{
	if (!g_cv)
		g_cv = gi.cvar("om_scriptaudit", "0", 0);
	return g_cv->value != 0 || g_run;
}

om_script::ScriptSystem *Sys(void) { return ScriptGlue_System(); }

std::string ScriptName(int handle)
{
	om_script::ScriptSystem *s = Sys();
	std::string p = (s && handle >= 0) ? s->script_path(handle) : "";
	if (p.compare(0, 3, "ds/") == 0)
		p = p.substr(3);
	if (p.size() > 3 && p.compare(p.size() - 3, 3, ".os") == 0)
		p.resize(p.size() - 3);
	return p.empty() ? "-" : p;
}

std::string Who(const edict_t *e)
{
	if (!e)
		return "-";
	return e->targetname ? e->targetname : e->classname ? e->classname : "-";
}

std::string &LastScript(int ent)
{
	if (ent >= (int)g_last_script.size())
		g_last_script.resize(ent + 1, "-");
	return g_last_script[ent];
}

edict_t *Player(void)
{
	edict_t *p = &g_edicts[1];
	return (p->inuse && p->client) ? p : nullptr;
}

/* the character's state for a stuck line: queue lengths and the current action */
void StateLine(edict_t *e, char *out, size_t size)
{
	AI *ai = e ? e->ai : nullptr;
	if (!e || !e->inuse || !ai) {
		Com_sprintf(out, (int)size, "no ai");
		return;
	}
	Action *a = ai->current;
	Com_sprintf(out, (int)size, "health=%d awake=%d scripted=%d queue=%d action=%d anim=%s pos=%.0f,%.0f,%.0f "
	            "vel=%.0f,%.0f,%.0f ground=%d", e->health, ai->awake, ScriptQueueLength(e), (int)ai->queue.size(),
	            a ? a->ClassCode() : -1, a && a->record ? a->record->name : "-", e->s.origin[0], e->s.origin[1],
	            e->s.origin[2], e->velocity[0], e->velocity[1], e->velocity[2],
	            e->groundentity ? G_EdictNum(e->groundentity) : -1);
}

float Dist2D(const vec3_t a, const vec3_t b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1];
	return sqrtf(dx * dx + dy * dy);
}

void RunNext(void)
{
	cvar_t *next = gi.cvar("om_audit_next", "", 0);
	if (next && next->string[0]) {
		char cmd[128];
		Com_sprintf(cmd, sizeof(cmd), "%s\n", next->string);
		gi.AddCommandString(cmd);
	}
}

/* a runner whose script has never run: loaded (state ready) at its first instruction, nothing queued */
bool NeverRun(edict_t *e)
{
	om_script::ScriptSystem *s = Sys();
	if (!s || !e->inuse || e->sg_script <= 0)
		return false;
	int h = e->sg_script - 1;
	return s->script_state(h) == 0 && s->script_ip(h) == 4 && s->queued_events(h) == 0;
}

} /* namespace */

/* ------------------------------------------------------------------------------------------------------------------
 * the monitor
 * ---------------------------------------------------------------------------------------------------------------- */

void ScriptAuditCommand(int ent, edict_t *e, const om_script::AiCommand &cmd)
{
	if (!On())
		return;
	om_script::ScriptSystem *s = Sys();
	std::string script = ScriptName(s ? s->current_script() : -1);
	LastScript(ent) = script;
	const char *why = !e ? "missing" : !e->ai ? "no ai" : (e->health <= 0 || e->deadflag) ? "dead" : nullptr;
	if (why) {
		g_drops++;
		gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s drop script=%s ent=%d name=%s cmd=%d kind=%d anim=%s done=%d "
		           "why=%s\n", level.mapname, script.c_str(), ent, Who(e).c_str(), cmd.command, cmd.kind,
		           cmd.animation ? cmd.animation : "-", cmd.done_id, why);
		return;
	}
	if (!cmd.done_id)
		return;
	Track t;
	t.done = cmd.done_id;
	t.ent = ent;
	t.who = Who(e);
	t.script = script;
	t.anim = cmd.animation ? cmd.animation : "-";
	t.cmd = cmd.command;
	t.kind = cmd.expression ? -1 : cmd.kind;
	t.count = cmd.count;
	t.flags = cmd.flags;
	t.t0 = level.time;
	t.limit = (cmd.flags & 0x40) ? cmd.duration * (cmd.count > 0 ? cmd.count : 1) + 10.0f : cmd.kind == 1 ? 30.0f : 20.0f;
	if (cmd.kind == 1 && !cmd.expression) {
		if (cmd.target >= 0 && cmd.target < globals.num_edicts && g_edicts[cmd.target].inuse) {
			VectorCopy(g_edicts[cmd.target].s.origin, t.dest);
			t.hasdest = true;
		} else if (cmd.destination[0] || cmd.destination[1] || cmd.destination[2]) {
			if (cmd.flags & 0x20)
				VectorCopy(cmd.destination, t.dest);
			else
				VectorAdd(e->s.origin, cmd.destination, t.dest);
			t.hasdest = true;
		}
	}
	g_tracks.push_back(t);
}

void ScriptAuditDone(int done_id)
{
	if (!On())
		return;
	for (size_t i = 0; i < g_tracks.size(); i++) {
		Track &t = g_tracks[i];
		if (t.done != done_id)
			continue;
		edict_t *e = &g_edicts[t.ent];
		char extra[64] = "";
		if (t.hasdest && e->inuse)
			Com_sprintf(extra, sizeof(extra), " dist=%.0f", Dist2D(e->s.origin, t.dest));
		gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s done script=%s ent=%d name=%s cmd=%d kind=%d anim=%s t=%.1f%s%s\n",
		           level.mapname, t.script.c_str(), t.ent, t.who.c_str(), t.cmd, t.kind, t.anim.c_str(),
		           level.time - t.t0, extra, t.stuck ? " late=1" : "");
		g_tracks.erase(g_tracks.begin() + i);
		return;
	}
}

void ScriptAuditReset(int ent)
{
	if (!On())
		return;
	int n = 0;
	for (auto &t : g_tracks)
		if (t.ent == ent && !t.reset) {
			t.reset = true;
			n++;
		}
	if (n)
		gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s reset script=%s ent=%d name=%s pending=%d\n", level.mapname,
		           ScriptName(Sys() ? Sys()->current_script() : -1).c_str(), ent, Who(&g_edicts[ent]).c_str(), n);
}

void ScriptAuditAnimSub(edict_t *e, const char *want, const char *got)
{
	if (!On() || !e)
		return;
	g_subs++;
	int n = G_EdictNum(e);
	gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s animsub script=%s ent=%d name=%s want=%s got=%s\n", level.mapname,
	           LastScript(n).c_str(), n, Who(e).c_str(), want ? want : "-", got ? got : "-");
}

/* a character died (G3 damage): who and what killed it, while the audit runs */
void ScriptAuditKilled(edict_t *e, edict_t *inflictor, edict_t *attacker, int damage, int mod)
{
	if (!On() || !e)
		return;
	int n = G_EdictNum(e);
	gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s killed script=%s ent=%d name=%s by=%d %s inflictor=%d %s damage=%d "
	           "mod=%d pos=%.0f,%.0f,%.0f\n", level.mapname, LastScript(n).c_str(), n, Who(e).c_str(),
	           attacker ? G_EdictNum(attacker) : -1, attacker && attacker->classname ? attacker->classname : "-",
	           inflictor ? G_EdictNum(inflictor) : -1, inflictor && inflictor->classname ? inflictor->classname : "-",
	           damage, mod, e->s.origin[0], e->s.origin[1], e->s.origin[2]);
}

static void MonitorFrame(void)
{
	for (size_t i = 0; i < g_tracks.size();) {
		Track &t = g_tracks[i];
		edict_t *e = &g_edicts[t.ent];
		if (!e->inuse || !e->ai || e->health <= 0) {
			gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s gone script=%s ent=%d name=%s cmd=%d kind=%d anim=%s t=%.1f "
			           "why=%s\n", level.mapname, t.script.c_str(), t.ent, t.who.c_str(), t.cmd, t.kind, t.anim.c_str(),
			           level.time - t.t0, !e->inuse ? "removed" : !e->ai ? "no ai" : "dead");
			g_tracks.erase(g_tracks.begin() + i);
			continue;
		}
		if (!t.stuck && level.time - t.t0 > t.limit) {
			t.stuck = true;
			g_stuck++;
			char st[256], extra[64] = "";
			StateLine(e, st, sizeof(st));
			if (t.hasdest)
				Com_sprintf(extra, sizeof(extra), " dest=%.0f,%.0f,%.0f dist=%.0f", t.dest[0], t.dest[1], t.dest[2],
				            Dist2D(e->s.origin, t.dest));
			gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s stuck script=%s ent=%d name=%s cmd=%d kind=%d anim=%s count=%d "
			           "flags=0x%x t=%.1f%s%s %s\n", level.mapname, t.script.c_str(), t.ent, t.who.c_str(), t.cmd, t.kind,
			           t.anim.c_str(), t.count, t.flags, level.time - t.t0, extra, t.reset ? " reset=1" : "", st);
		}
		i++;
	}
}

/* ------------------------------------------------------------------------------------------------------------------
 * omscriptrun
 * ---------------------------------------------------------------------------------------------------------------- */

bool ScriptRunActive(void) { return g_run; }

void ScriptRunStart(edict_t *player)
{
	if (!player || !player->client || deathmatch->value) {
		gi.cprintf(player, PRINT_HIGH, "omscriptrun: single player only\n");
		return;
	}
	g_runners.clear();
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (e->inuse && e->classname && !strcmp(e->classname, "script_runner") && NeverRun(e))
			g_runners.push_back(i);
	}
	g_next = 0;
	g_cur = -1;
	g_pending = -1;
	g_ran = g_finished = g_timeout = g_stuck = g_drops = g_subs = 0;
	g_run = true;
	g_wait_until = level.time + RUN_GAP;
	player->flags |= FL_GODMODE | FL_NOTARGET;
	player->movetype = MOVETYPE_NOCLIP;
	gix.cvar_set("om_audit_running", "1");
	gi.cprintf(nullptr, PRINT_HIGH, "OMAUDIT %s start scripts=%d\n", level.mapname, (int)g_runners.size());
}

static void RunEnd(void)
{
	gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s summary scripts=%d ran=%d finished=%d timeout=%d stuck=%d drops=%d "
	           "animsubs=%d\n", level.mapname, (int)g_runners.size(), g_ran, g_finished, g_timeout, g_stuck, g_drops,
	           g_subs);
	gi.cprintf(nullptr, PRINT_HIGH, "OMAUDIT %s done total=%d reacted=%d silent=%d nospot=0 gone=0\n", level.mapname,
	           (int)g_runners.size(), g_finished, g_timeout);
	g_run = false;
	gix.cvar_set("om_audit_running", "0");
	RunNext();
}

static void RunFrame(void)
{
	edict_t *p = Player();
	if (!p) {
		g_run = false;
		return;
	}
	p->flags |= FL_GODMODE | FL_NOTARGET;
	p->movetype = MOVETYPE_NOCLIP;
	if (p->health <= 0)
		p->health = 100;
	om_script::ScriptSystem *s = Sys();
	if (g_cur >= 0) {
		edict_t *r = &g_edicts[g_cur];
		int h = r->sg_script - 1;
		int st = s ? s->script_state(h) : 1, ip = s ? s->script_ip(h) : -1, q = s ? s->queued_events(h) : 0;
		float t = level.time - g_t0;
		/* finished, or suspended until it is used again (ready past its first instruction, nothing queued); still at
		   its first instruction after a second: it did not start */
		bool idle = st == 0 && q == 0 && t > 0.5f;
		bool ended = st == 1 || (idle && ip != 4);
		bool notstarted = idle && ip == 4 && t > 1.0f;
		if (!ended && !notstarted && t < RUN_LIMIT)
			return;
		if (ended)
			g_finished++;
		else
			g_timeout++;
		gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s ran script=%s runner=%d result=%s state=%d ip=%d t=%.1f\n",
		           level.mapname, ScriptName(h).c_str(), g_cur,
		           ended ? (st == 1 ? "finished" : "suspended") : notstarted ? "notstarted" : "timeout", st, ip, t);
		g_cur = -1;
		g_wait_until = level.time + RUN_GAP;
		return;
	}
	if (level.time < g_wait_until)
		return;
	if (g_pending >= 0) {
		edict_t *r = &g_edicts[g_pending];
		g_pending = -1;
		if (r->inuse && r->use && NeverRun(r)) {
			g_cur = G_EdictNum(r);
			g_t0 = level.time;
			g_ran++;
			r->use(r, p, p);
			return;
		}
	}
	while (g_next < g_runners.size()) {
		edict_t *r = &g_edicts[g_runners[g_next++]];
		if (!r->inuse || !r->use || !NeverRun(r))
			continue; /* removed, or run meanwhile by another script */
		/* the player goes where the level's own trigger of the script is (a trigger brush, a button, a relay): the
		   characters near it wake as in play (SPEC-700: within 800 units of the player), a sleeping one is not solid
		   and a scripted shot at it would pass through. A script used only by other scripts runs where the player
		   stands */
		edict_t *src = nullptr;
		if (r->targetname)
			for (edict_t *e = nullptr; (e = G_Find(e, FOFS(target), r->targetname)) != nullptr;)
				if (e != r) {
					src = e;
					break;
				}
		vec3_t at;
		VectorCopy(p->s.origin, at);
		if (src) {
			bool brush = src->absmax[0] > src->absmin[0];
			for (int k = 0; k < 3; k++)
				at[k] = brush ? (src->absmin[k] + src->absmax[k]) * 0.5f : src->s.origin[k];
			VectorCopy(at, p->s.origin);
			VectorClear(p->velocity);
			gi.linkentity(p);
		}
		gi.cprintf(nullptr, PRINT_HIGH, "OMSCRIPT %s run script=%s runner=%d tn=%s from=%s at=%.0f,%.0f,%.0f\n",
		           level.mapname, ScriptName(r->sg_script - 1).c_str(), G_EdictNum(r),
		           r->targetname ? r->targetname : "-", src && src->classname ? src->classname : "-", at[0], at[1],
		           at[2]);
		g_pending = G_EdictNum(r);
		g_wait_until = level.time + (src ? RUN_SETTLE : 0.0f);
		return;
	}
	RunEnd();
}

void ScriptAuditLevelStart(void)
{
	g_tracks.clear();
	g_last_script.clear();
	g_cv = nullptr;
	if (g_run || gix.cvar_value("om_audit_running")) {
		if (g_run)
			gi.cprintf(nullptr, PRINT_HIGH, "OMNOTE level changed during its script run\n");
		g_run = false;
	}
}

void ScriptAuditFrame(void)
{
	if (!On())
		return;
	MonitorFrame();
	if (g_run)
		RunFrame();
}

} /* namespace om_ai */
