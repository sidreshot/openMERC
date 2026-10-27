/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 15 (Assassin, Arsenal, CTF, Realistic, Control, Conquer), 6 (Assassin rule),
 *             11 (Realistic walking)
 * Owner: package G5 (game/src/bots). The game-mode facts come from G9 through BotModeHooks (bot_api.h).
 */
#include "bot_modes.h"

#include "../modes/g_modes.h"

static BotModeHooks g_hooks;

void Bot_SetModeHooks(const BotModeHooks *h)
{
	if (h)
		g_hooks = *h;
	else
		memset(&g_hooks, 0, sizeof(g_hooks));
}

static int Team(edict_t *p)
{
	if (!p || !p->client)
		return 0;
	if (g_hooks.client_team)
		return g_hooks.client_team(p);
	return GM_Team(p);
}

int BotMode_FlagTeam(edict_t *flag) { return g_hooks.flag_team ? g_hooks.flag_team(flag) : 0; }
static bool FlagAtBase(edict_t *flag) { return g_hooks.flag_at_base ? g_hooks.flag_at_base(flag) != 0 : true; }

static edict_t *FlagCarrier(int team)
{
	if (g_hooks.flag_carrier)
		return g_hooks.flag_carrier(team);
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *e = g_edicts + i;
		if (e->inuse && e->client && GM_CarriedFlag(e) == team)
			return e;
	}
	return NULL;
}

bool BotMode_AssassinAllows(edict_t *bot, edict_t *other)
{
	if (deathmatch->value != 2)
		return true;
	if (!g_hooks.assassin_target)
		return true;
	return g_hooks.assassin_target(bot) == other ||
	       (g_hooks.assassin_hunts && g_hooks.assassin_hunts(other, bot));
}

bool BotMode_IsEnemyFlag(edict_t *bot, edict_t *flag)
{
	int ft = BotMode_FlagTeam(flag);
	return ft && ft != Team(bot);
}

bool BotMode_FlagWanted(edict_t *bot, edict_t *flag)
{
	if (deathmatch->value != 4)
		return false;
	int ft = BotMode_FlagTeam(flag);
	if (!ft)
		return false;
	if (ft == Team(bot))
		return !FlagAtBase(flag); /* return it */
	return true;
}

bool BotMode_Tired(Bot *b)
{
	if (deathmatch->value != 5)
		return false;
	edict_t *ent = g_edicts + b->clientnum;
	if (g_hooks.tired && g_hooks.tired(ent))
		b->shared.tired_seen = level.time;
	if (b->shared.fatigue > 50 && !b->enemy)
		b->shared.fatigue_over = level.time;
	return (b->shared.tired_seen > 0 && level.time - b->shared.tired_seen < 5.0f) ||
	       (b->shared.fatigue_over > 0 && level.time - b->shared.fatigue_over < 3.0f);
}

static void SetDest(Bot *b, int w)
{
	if (w < 1 || w > g_wps.count)
		return;
	b->dest_wp = w;
	VectorCopy(g_wps.wp[w].origin, b->dest_origin);
}

static float DistTo(edict_t *ent, int w)
{
	vec3_t d;
	VectorSubtract(g_wps.wp[w].origin, ent->s.origin, d);
	return VectorLength(d);
}

static edict_t *FindFlag(int team)
{
	for (int i = game.maxclients + 1; i < globals.num_edicts; i++) {
		edict_t *e = g_edicts + i;
		if (e->inuse && e->item && ITEM_INDEX(e->item) == 36 && BotMode_FlagTeam(e) == team)
			return e;
	}
	return NULL;
}

static int CtfRole(Bot *b, edict_t *ent, bool enemy_carrier)
{
	if (enemy_carrier)
		return 0;
	int me = Team(ent), att = 0, def = 0;
	for (int i = 1; i <= game.maxclients; i++) {
		Bot *o = &g_bots[i];
		if (!o->inuse || o == b || Team(g_edicts + i) != me)
			continue;
		if (o->ctf_role)
			def++;
		else
			att++;
	}
	return att > def ? 1 : 0;
}

static void Announce(Bot *b, edict_t *ent, int objective, const char *text)
{
	if (b->ctf_objective == objective)
		return;
	b->ctf_objective = objective;
	const char *name = ent->client ? ent->client->pers.netname : "";
	/* the bracketed field receives no value in 1.07f (SPEC-741 OQ 2); team 2's return message has none */
	if (objective == 2 && Team(ent) == 2)
		gi.AddCommandString(va("say_team %s: %s\n", name, text));
	else
		gi.AddCommandString(va("say_team %s[]: %s\n", name, text));
}

static void Ctf(Bot *b, edict_t *ent)
{
	int me = Team(ent), them = me == 1 ? 2 : 1;
	int ownbase = me == 1 ? g_wps.blue_base : g_wps.red_base;
	int theirbase = me == 1 ? g_wps.red_base : g_wps.blue_base;
	edict_t *enemy_carrier = FlagCarrier(me); /* carries our flag */
	edict_t *mate_carrier = FlagCarrier(them);
	if (mate_carrier && Team(mate_carrier) != me)
		mate_carrier = NULL;
	if (BotCarriesFlag(ent)) {
		bool all_bot_defenders = true;
		for (int i = 1; i <= game.maxclients; i++) {
			edict_t *e = g_edicts + i;
			if (!e->inuse || e == ent || Team(e) != me)
				continue;
			if (!g_bots[i].inuse || !g_bots[i].ctf_role)
				all_bot_defenders = false;
		}
		if (ownbase && DistTo(ent, ownbase) > 128 && !(enemy_carrier && all_bot_defenders))
			SetDest(b, ownbase);
		return;
	}
	b->ctf_role = CtfRole(b, ent, enemy_carrier != NULL);
	if (b->ctf_role) {
		if (ownbase && DistTo(ent, ownbase) > 512)
			SetDest(b, ownbase);
		return;
	}
	if (level.time < b->ctf_next_eval)
		return;
	b->ctf_next_eval = level.time + 5.0f;
	edict_t *ownflag = FindFlag(me), *theirflag = FindFlag(them);
	if (mate_carrier && !enemy_carrier) {
		int w = BotWp_Nearest(mate_carrier->s.origin, 512, false, NULL, false);
		if (w && DistTo(ent, w) > 256)
			SetDest(b, w);
		b->ctf_objective = 1;
	} else if (ownflag && !FlagAtBase(ownflag) && !enemy_carrier) {
		Announce(b, ent, 2, "Going to return our flag.");
		int w = BotWp_Nearest(ownflag->s.origin, 1e9f, false, NULL, false);
		SetDest(b, w);
	} else if (theirflag && !FlagAtBase(theirflag) && !enemy_carrier) {
		Announce(b, ent, 3, "Their flag's been dropped!");
		int w = BotWp_Nearest(theirflag->s.origin, 1e9f, false, NULL, false);
		SetDest(b, w);
	} else if (mate_carrier) {
		Announce(b, ent, 4, "We have their flag.");
		if (theirbase && DistTo(ent, theirbase) > 1024)
			SetDest(b, theirbase);
	} else {
		Announce(b, ent, 5, "I'm going after their flag.");
		if (theirbase && DistTo(ent, theirbase) > 128)
			SetDest(b, theirbase);
	}
}

static int Owner(size_t k)
{
	if (!g_hooks.control_owner || k >= g_wps.control_flags.size())
		return 0;
	return g_hooks.control_owner(g_wps.control_flags[k]);
}

static void Control(Bot *b, edict_t *ent)
{
	if (g_wps.control_points.empty()) {
		if (!b->control_goal) {
			BotConsoleId(BOTMSG(BM_NOCONTROL), ent->client ? ent->client->pers.netname : "");
			b->control_goal = -1;
		}
		return;
	}
	int me = Team(ent);
	size_t n = g_wps.control_points.size();
	int owned = 0;
	for (size_t k = 0; k < n; k++)
		if (Owner(k) == me)
			owned++;
	if (owned == (int)n)
		b->ctf_role = 1;
	else if (owned == 0)
		b->ctf_role = 0;
	else
		b->ctf_role = CtfRole(b, ent, false);
	/* keep the current point while its ownership still fits the role */
	if (b->control_goal > 0) {
		int k = -1;
		for (size_t i = 0; i < n; i++)
			if (g_wps.control_points[i] == b->control_goal)
				k = (int)i;
		if (k >= 0) {
			bool ours = Owner(k) == me;
			if ((b->ctf_role && ours) || (!b->ctf_role && !ours)) {
				if (b->ctf_role && DistTo(ent, b->control_goal) <= 512)
					return;
				SetDest(b, b->control_goal);
				return;
			}
		}
	}
	int best = 0, fallback = 0;
	float bd = 0, fd = 0;
	for (size_t k = 0; k < n; k++) {
		int w = g_wps.control_points[k];
		bool ours = Owner(k) == me;
		bool fits = b->ctf_role ? ours : !ours;
		float d = DistTo(ent, w);
		if (fits) {
			if (!best || d < bd)
				best = w, bd = d;
		} else if (!fallback || d < fd)
			fallback = w, fd = d;
	}
	b->control_goal = best ? best : fallback;
	SetDest(b, b->control_goal);
}

static void Conquer(Bot *b, edict_t *ent)
{
	int g = g_wps.conquer_goal;
	if (!g || (b->enemy && !b->retreating))
		return;
	if (DistTo(ent, g) < 50) {
		/* hold position, facing the waypoints two ids back and forth */
		if (level.time >= b->hold_switch) {
			b->hold_side = !b->hold_side;
			b->hold_switch = level.time + gix.flrand(1.5f, 3.5f);
		}
		b->dest_wp = 0;
		return;
	}
	SetDest(b, g);
}

void BotMode_Objectives(Bot *b, edict_t *ent)
{
	switch ((int)deathmatch->value) {
	case 4: Ctf(b, ent); break;
	case 6: Control(b, ent); break;
	case 7: Conquer(b, ent); break;
	}
}
