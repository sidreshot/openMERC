/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Implements: SPEC-741 1 (level start and readiness), 2 (adding and removing bots), 3 (a bot client),
 *             6 (noise records, damage), 10 (explosive lists), hooks of bot_api.h; SPEC-313 (bot_name slots);
 *             SPEC-AMEND-005 G5.1 (server flag 0x200), G5.2 (add/fill order, R), G5.5 (noise window),
 *             G5.12 (waypoint edit-mode conditions)
 * Owner: package G5 (game/src/bots).
 */
#include "bot_local.h"

#include "../modes/g_modes.h"

#include <stdarg.h>
#include <stdio.h>

Bot g_bots[MAX_CLIENTS + 1];
BotNoise g_noise[MAX_CLIENTS + 1];
std::vector<BotDanger> g_danger;
bool g_bot_active;
float g_bot_wp_init_time;

cvar_t *bot_waypoints, *bot_wp_edit, *bot_wp_info, *bot_listfile, *bot_random, *bot_fillserv, *bot_configexec,
	*bot_chat, *bot_wppref, *bot_hunt, *bot_notarget, *bot_runtact;

/* slot table (cvars bot_name0..31, bot_team0..31) */
struct BotSlot {
	cvar_t *name;
	cvar_t *team;
	char lastteam[16];
	bool request;
	float request_time;
	int bot;  /* edict number of the slot's bot, 0 = none */
};
static BotSlot g_slots[MAX_CLIENTS];
static bool g_avail[MAX_CLIENTS + 1]; /* client number available to bots (not taken by a human) */
static float g_last_add = -1000;

//==========================================================================================
// helpers

Bot *BotFor(const edict_t *ent)
{
	if (!ent)
		return NULL;
	int n = (int)(ent - g_edicts);
	if (n < 1 || n > MAX_CLIENTS || n > game.maxclients)
		return NULL;
	return g_bots[n].inuse ? &g_bots[n] : NULL;
}

const BotPersonality &BotP(const Bot *b)
{
	static BotPersonality none;
	if (b && b->pers >= 0 && b->pers < (int)g_bot_pers.size())
		return g_bot_pers[b->pers];
	return none;
}

float BotMax(void) { return sv_maxvelocity ? sv_maxvelocity->value : 2000.0f; }

PlayerInventory *BotInv(edict_t *ent) { return ent && ent->client ? ent->client->inv : NULL; }

int BotCurWeapon(edict_t *ent)
{
	PlayerInventory *inv = BotInv(ent);
	return inv ? inv->CurrentWeapon() : 0;
}

bool BotCarriesFlag(edict_t *ent)
{
	return ent && ent->client && GM_CarriedFlag(ent) != 0;
}

void BotDevMsg(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	if (gix.cvar_value("developer"))
		gi.dprintf("%s", buf);
}

/* message of string package 12 formatted with printf rules (the BOT texts use only %s and %d), to the console */
void BotConsoleId(int id, ...)
{
	const char *fmt = gix.SP_GetStringText ? gix.SP_GetStringText(id) : NULL;
	if (!fmt)
		return;
	char buf[1024];
	va_list ap;
	va_start(ap, id);
	_vsnprintf(buf, sizeof(buf) - 1, fmt, ap);
	va_end(ap);
	buf[sizeof(buf) - 1] = 0;
	gi.dprintf("%s", buf);
}

static bool IsHumanNumber(int n)
{
	if (n < 1 || n > game.maxclients)
		return false;
	if (g_bots[n].inuse)
		return false;
	if (!g_avail[n])
		return true;
	edict_t *e = g_edicts + n;
	return e->inuse && e->client && e->client->pers.connected;
}

//==========================================================================================
// level start (1.1), readiness (1.2), exit (1.4)

static void RegisterCvars(void)
{
	bot_waypoints = gi.cvar("bot_waypoints", "1", 0);
	bot_wp_edit = gi.cvar("bot_wp_edit", "0", 0);
	bot_wp_info = gi.cvar("bot_wp_info", "0", 0);
	bot_listfile = gi.cvar("bot_listfile", "botlist.txt", 0);
	bot_random = gi.cvar("bot_random", "1", 0);
	bot_fillserv = gi.cvar("bot_fillserv", "0", 0);
	bot_configexec = gi.cvar("bot_configexec", "1", 0);
	bot_chat = gi.cvar("bot_chat", "0", 0);
	bot_wppref = gi.cvar("bot_wppref", "1", 0);
	bot_hunt = gi.cvar("bot_hunt", "1", 0);
	bot_notarget = gi.cvar("bot_notarget", "0", 0);
	bot_runtact = gi.cvar("bot_runtact", "1", 0);
	for (int i = 0; i < MAX_CLIENTS; i++) {
		g_slots[i].name = gi.cvar(va("bot_name%d", i), BOT_UNSET, 0);
		g_slots[i].team = gi.cvar(va("bot_team%d", i), "0", 0);
	}
}

void Bot_WorldSpawn(void)
{
	if (!deathmatch || !deathmatch->value)
		return;
	RegisterCvars();
	g_bot_active = true;
	memset(g_bots, 0, sizeof(g_bots));
	memset(g_noise, 0, sizeof(g_noise));
	for (int i = 0; i <= MAX_CLIENTS; i++)
		g_avail[i] = true;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		g_slots[i].request = false;
		g_slots[i].bot = 0;
		BotStrCpy(g_slots[i].lastteam, g_slots[i].team->string, sizeof(g_slots[i].lastteam));
	}
	g_last_add = -1000;
	BotPers_Load();
	BotPers_NewLevel();
	g_danger.clear();
	BotWp_Reset();
	if (bot_configexec->value)
		gi.AddCommandString("exec hidebot.cfg\n");
	char base[MAX_QPATH], path[MAX_QPATH * 2];
	BotMapBase(base, sizeof(base));
	Com_sprintf(path, sizeof(path), "bot/mapcfgs/%s.cfg", base);
	if (gix.FS_FileExists && gix.FS_FileExists(path))
		gi.AddCommandString(va("exec %s\n", path));
	else
		gi.AddCommandString("exec bot/mapcfgs/default.cfg\n");
	g_bot_wp_init_time = level.time + 10.0f;
}

void Bot_WaypointLoaderThink(edict_t *self)
{
	if (g_bot_active && deathmatch && deathmatch->value && !g_wps.initialised)
		BotWp_Init();
	G_FreeEdict(self);
}

void Bot_LevelExit(void)
{
	if (!g_bot_active)
		return;
	for (int i = 1; i <= MAX_CLIENTS; i++)
		g_bots[i].inuse = false;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		g_slots[i].bot = 0;
		g_slots[i].request = false;
	}
	BotPers_Free();
	BotWp_Reset();
	g_danger.clear();
	g_bot_active = false;
}

//==========================================================================================
// a bot client (3)

static int FreeClientNumber(void)
{
	for (int n = game.maxclients; n >= 1; n--)
		if (g_avail[n] && !g_edicts[n].inuse && !g_bots[n].inuse)
			return n;
	return 0;
}

static bool CreateBot(int slot)
{
	int n = FreeClientNumber();
	if (!n || g_bot_pers.empty())
		return false;
	edict_t *ent = g_edicts + n;
	Bot *b = &g_bots[n];
	memset(b, 0, sizeof(*b));
	b->inuse = true;
	b->clientnum = n;
	b->slot = slot;
	BotStrCpy(b->slotvalue, g_slots[slot].name->string, sizeof(b->slotvalue));
	b->pers = BotPers_Find(b->slotvalue);
	g_bot_pers[b->pers].uses++;
	BotStrCpy(b->userinfo, g_bot_pers[b->pers].userinfo, sizeof(b->userinfo));
	b->team = atoi(g_slots[slot].team->string);
	b->walk_dir = 1;
	b->roam_ds = 1; /* remembered roaming pair starts as (0, increasing) (SPEC-AMEND-005 G5.8) */

	ent->client = game.clients + (n - 1);
	gclient_t *cl = ent->client;
	memset(&cl->resp, 0, sizeof(cl->resp));
	InitClientResp(cl);
	InitClientPersistant(cl);
	cl->pers.isbot = true;
	/* SPEC-AMEND-005 G5.1: server flag 0x200 only (the engine sends nothing to this slot); no entity flag */
	ent->svflags |= BOT_SVFLAG;
	char ui[MAX_INFO_STRING];
	BotStrCpy(ui, b->userinfo, sizeof(ui));
	ClientUserinfoChanged(ent, ui, false);
	cl->pers.connected = true;
	ClientBegin(ent);
	b->idle_chat_time = level.time + gix.flrand(75, 100);
	b->view_yaw = ent->s.angles[YAW];
	b->last_weapon = BotCurWeapon(ent);
	g_slots[slot].bot = n;
	return true;
}

static void BotLeaveQuiet(edict_t *ent)
{
	/* the rules' client-leave hooks, body and hit-volume parts: G18/G9 own them (docs/impl/G5.md "Needs") */
	gclient_t *cl = ent->client;
	if (cl) {
		edict_t **parts[3] = {&cl->helper_lean, &cl->helper_crouch, &cl->helper_gun};
		for (auto p : parts)
			if (*p) {
				G_FreeEdict(*p);
				*p = NULL;
			}
	}
	GhoulRemove(ent);
	gi.unlinkentity(ent);
	ent->s.modelindex = 0;
	ent->solid = SOLID_NOT;
	ent->inuse = false;
	ent->classname = (char *)"disconnected";
	if (cl) {
		cl->pers.connected = false;
		cl->pers.isbot = false;
		if (cl->inv) {
			PlayerDeleteInventory(cl->inv);
			cl->inv = NULL;
		}
	}
	ent->svflags = 0; /* SPEC-AMEND-005 G5.1: bot removal clears the whole server-flags field */
}

void Bot_RemoveClient(Bot *b, bool resetslot)
{
	if (!b || !b->inuse)
		return;
	edict_t *ent = g_edicts + b->clientnum;
	const char *name = ent->client ? ent->client->pers.netname : "";
	if (gix.SP_Print)
		gix.SP_Print(NULL, BOTMSG(BM_DISCONNECTED), name);
	BotLeaveQuiet(ent);
	gi.configstring(CS_PLAYERSKINS + b->clientnum - 1, "");
	if (b->slot >= 0 && b->slot < MAX_CLIENTS) {
		if (g_slots[b->slot].bot == b->clientnum)
			g_slots[b->slot].bot = 0;
		if (resetslot)
			gi.cvar_set(g_slots[b->slot].name->name, BOT_UNSET);
	}
	b->inuse = false;
	g_noise[b->clientnum] = BotNoise{};
}

//==========================================================================================
// adding and removing (2)

static void SlotValue(int s, const char *v) { gi.cvar_set(g_slots[s].name->name, v); }

static int OldestRequest(void)
{
	int best = -1;
	for (int s = 0; s < MAX_CLIENTS; s++)
		if (g_slots[s].request && (best < 0 || g_slots[s].request_time < g_slots[best].request_time))
			best = s;
	return best;
}

/* 5: drop; P = number of pending requests */
static void Drop(int P)
{
	int first = 0;
	for (int n = 1; n <= game.maxclients; n++)
		if (g_bots[n].inuse) {
			first = n;
			break;
		}
	int freecount = 0;
	if (first)
		for (int n = first; n <= game.maxclients; n++)
			if (!g_edicts[n].inuse)
				freecount++;
	if (first && P <= freecount)
		Bot_RemoveClient(&g_bots[first], false);
	else {
		int s = OldestRequest();
		if (s >= 0) {
			g_slots[s].request = false;
			SlotValue(s, BOT_UNSET);
		}
	}
}

static int PendingRequests(void)
{
	int P = 0;
	for (int s = 0; s < MAX_CLIENTS; s++)
		if (g_slots[s].request)
			P++;
	return P;
}

/* SPEC-741 2 with SPEC-AMEND-005 G5.2: order shift-up, slot pass (with its removals), add, then fill or drop. R counts
 * every slot whose request time is set (pending requests and bots that joined through a slot), computed once before
 * the slot pass; every request made in this frame is checked against that value. */
static void Manage(void)
{
	int maxc = game.maxclients;
	int H = 0, lastHuman = 0, firstBot = 0;
	for (int n = 1; n <= maxc; n++) {
		if (IsHumanNumber(n)) {
			H++;
			lastHuman = n;
		}
		if (g_bots[n].inuse && !firstBot)
			firstBot = n;
	}

	/* 1: a bot directly above the last human makes room */
	if (firstBot && firstBot == lastHuman + 1) {
		Bot *b = &g_bots[firstBot];
		bool higher = false;
		for (int n = firstBot + 1; n <= maxc; n++)
			if (g_avail[n] && !g_edicts[n].inuse)
				higher = true;
		Bot_RemoveClient(b, !higher);
		firstBot = 0;
		for (int n = 1; n <= maxc; n++)
			if (g_bots[n].inuse) {
				firstBot = n;
				break;
			}
	}

	int R = 0;
	for (int s = 0; s < MAX_CLIENTS; s++)
		if (g_slots[s].request || (g_slots[s].bot && g_bots[g_slots[s].bot].inuse))
			R++;

	/* 2: poll the slots */
	bool perbot_teams = deathmatch->value == 7 && ((int)dmflags->value & 0x8000);
	for (int s = 0; s < MAX_CLIENTS; s++) {
		BotSlot &sl = g_slots[s];
		const char *v = sl.name->string;
		bool unset = !strcmp(v, BOT_UNSET);
		Bot *b = sl.bot ? &g_bots[sl.bot] : NULL;
		if (b && !b->inuse) {
			sl.bot = 0;
			b = NULL;
		}
		if (unset) {
			if (b)
				Bot_RemoveClient(b, false);
			sl.request = false;
			continue;
		}
		if (!b && !sl.request) {
			if (H + R < maxc - 1) {
				sl.request = true;
				sl.request_time = level.time;
			} else {
				BotConsoleId(BOTMSG(BM_NOSLOT), v);
				SlotValue(s, BOT_UNSET);
			}
		} else if (b && strcmp(v, b->slotvalue)) {
			Bot_RemoveClient(b, true); /* renaming a bot just removes it in 1.07f */
		} else if (b && perbot_teams && strcmp(sl.team->string, sl.lastteam)) {
			BotStrCpy(sl.lastteam, sl.team->string, sizeof(sl.lastteam));
			b->team = (int)sl.team->value;
			char ui[MAX_INFO_STRING];
			BotStrCpy(ui, b->userinfo, sizeof(ui));
			ClientUserinfoChanged(g_edicts + b->clientnum, ui, true);
		}
	}

	/* 3: add - the oldest pending request, 1.5 s after the last add (last add + 1.5 <= now; the small tolerance keeps
	 * the observed exact 15-frame spacing despite float level times), with the join gap measured before the add */
	bool gap = !firstBot || firstBot - lastHuman > 2;
	int s = OldestRequest();
	if (s >= 0 && gap && g_last_add + 1.5f <= level.time + 0.001f && CreateBot(s)) {
		g_slots[s].request = false;
		g_last_add = level.time;
	}

	/* 4/5: drop when H + R = maxclients with requests pending (>= guards the slot-table overrun of SPEC-741 OQ 5);
	 * else fill: no 1.5 s spacing, only the join gap and H + R < maxclients - 1 (SPEC-AMEND-005 G5.2) */
	int P = PendingRequests();
	if (H + R >= maxc && P > 0)
		Drop(P);
	else if (bot_fillserv->value && gap && H + R < maxc - 1) {
		for (int k = 0; k < MAX_CLIENTS; k++) {
			BotSlot &sl = g_slots[k];
			if (sl.bot || sl.request || strcmp(sl.name->string, BOT_UNSET))
				continue;
			int p = BotPers_Choose(false);
			if (p < 0)
				break;
			SlotValue(k, g_bot_pers[p].name);
			sl.request = true;
			sl.request_time = level.time;
			break;
		}
	}
}

//==========================================================================================
// per frame

/* SPEC-AMEND-005 G5.12: waypoint edit mode needs a listen server with cheats and the local player alone (no human
 * other than client number 1 in the game; bots do not count) */
static bool EditModeAllowed(void)
{
	if (!gix.cvar_value("cheats"))
		return false;
	if (dedicated && dedicated->value)
		return false;
	for (int n = 2; n <= game.maxclients; n++) {
		edict_t *e = g_edicts + n;
		if (e->inuse && e->client && !g_bots[n].inuse && !e->client->pers.isbot)
			return false;
	}
	return true;
}

void Bot_RunFrame(void)
{
	/* start of every server frame, skipped while freezeworld is set: bot_wp_edit is forced back to 0 unless edit mode
	 * is allowed (SPEC-AMEND-005 G5.12) */
	if (bot_wp_edit && bot_wp_edit->value != 0 && !(freezeworld && freezeworld->value) && !EditModeAllowed())
		gi.cvar_set("bot_wp_edit", "0");
	if (!g_bot_active || !deathmatch->value)
		return;
	/* the waypoints are loaded by the loader entity's think at level time 10.0 (Bot_WaypointLoaderThink), after the
	 * bot manager of that frame ran: the manager sees "ready" one frame later (SPEC-AMEND-005 G5.2: first fill
	 * request at 10.1, first fill bot at 10.2) */
	if (!g_wps.initialised)
		return;
	/* an explosive freed without going off (removed by a cinematic, level change) leaves the lists too */
	for (size_t i = 0; i < g_danger.size();)
		if (!g_danger[i].ent || !g_danger[i].ent->inuse || !(g_danger[i].ent->flags & FL_THROWNEQUIP))
			g_danger.erase(g_danger.begin() + i);
		else
			i++;
	if (g_wps.editmode)
		BotWp_EditFrame();
	if (g_wps.ready || g_wps.roam)
		Manage();
	BotChat_Frame();
	for (int n = 1; n <= game.maxclients; n++) {
		Bot *b = &g_bots[n];
		edict_t *ent = g_edicts + n;
		if (!b->inuse || !ent->inuse || !ent->client)
			continue;
		usercmd_t cmd;
		memset(&cmd, 0, sizeof(cmd));
		Bot_Think(b, ent, &cmd);
		ClientThink(ent, &cmd);
	}
}

//==========================================================================================
// hooks

int Bot_ClientCommand(edict_t *ent)
{
	/* SPEC-AMEND-005 G5.12: accepted only while bot_wp_edit != 0 and edit mode is allowed; otherwise the command
	 * falls through to the ordinary client commands (chat). Placement itself still needs edit mode (14.11). */
	if (!g_bot_active || !bot_wp_edit || bot_wp_edit->value == 0 || !g_wps.initialised || !EditModeAllowed())
		return 0;
	return BotWp_EditorCommand(ent, gi.argv(0));
}

void Bot_SlotTaken(edict_t *ent)
{
	if (!g_bot_active)
		return;
	int n = (int)(ent - g_edicts);
	if (n < 1 || n > MAX_CLIENTS)
		return;
	if (g_bots[n].inuse)
		Bot_RemoveClient(&g_bots[n], false);
	g_avail[n] = false;
}

void Bot_SlotFreed(edict_t *ent)
{
	if (!g_bot_active)
		return;
	int n = (int)(ent - g_edicts);
	if (n < 1 || n > MAX_CLIENTS || g_bots[n].inuse)
		return;
	g_avail[n] = true;
}

int Bot_IsBot(const edict_t *ent) { return BotFor(ent) != NULL; }

const char *Bot_Userinfo(const edict_t *ent)
{
	Bot *b = BotFor(ent);
	return b ? b->userinfo : NULL;
}

BotShared *Bot_State(edict_t *ent)
{
	Bot *b = BotFor(ent);
	return b ? &b->shared : NULL;
}

void Bot_SpawnSpot(edict_t *ent, edict_t *spot)
{
	Bot *b = BotFor(ent);
	if (b && spot)
		b->last_wp = BotWp_SpawnWaypoint(spot);
}

void Bot_Damaged(edict_t *targ, edict_t *attacker)
{
	Bot *b = BotFor(targ);
	if (!b || !attacker || attacker == targ || !attacker->client || b->enemy)
		return;
	if (bot_notarget && bot_notarget->value)
		return;
	b->enemy = attacker;
}

void Bot_Killed(edict_t *victim, edict_t *attacker, int mod)
{
	(void)mod;
	if (!g_bot_active || !deathmatch->value)
		return;
	if (!victim || !attacker || !attacker->client || attacker == victim)
		return;
	Bot *bv = BotFor(victim), *ba = BotFor(attacker);
	if (bv)
		BotChat_Trigger(bv, attacker, 1);
	if (ba)
		BotChat_Trigger(ba, victim, 2);
}

/* SPEC-741 6 with SPEC-AMEND-005 G5.5: each noise sets its own kind (the latest wins), the loudness is the maximum
 * within the window, the window restarts at every noise (expiry = now + 1) and an expired record starts from 0 */
static void RecordNoise(edict_t *who, float loudness, int kind)
{
	int n = (int)(who - g_edicts);
	if (n < 1 || n > MAX_CLIENTS)
		return;
	BotNoise &r = g_noise[n];
	if (r.expiry <= level.time)
		r.loudness = 0;
	if (loudness > r.loudness)
		r.loudness = loudness;
	r.kind = kind;
	r.time = level.time;
	r.expiry = level.time + 1.0f;
}

static edict_t *PlayerOf(edict_t *ent)
{
	if (!ent)
		return NULL;
	if (ent->classname && !strcmp(ent->classname, "player") && ent->client)
		return ent;
	if (ent->owner && ent->owner->client && ent->owner->classname && !strcmp(ent->owner->classname, "player"))
		return ent->owner;
	return NULL;
}

void Bot_SoundNoise(edict_t *ent, float volume, int lastarg)
{
	if (!g_bot_active || lastarg == 1)
		return;
	edict_t *p = PlayerOf(ent);
	if (p)
		RecordNoise(p, volume * 200.0f, 0);
}

int Bot_DropSound(edict_t *ent, int lastarg) { return lastarg == 1 && BotFor(ent) != NULL; }

void Bot_FootstepNoise(edict_t *ent)
{
	if (g_bot_active && ent && ent->client)
		RecordNoise(ent, 250.0f, 2);
}

void Bot_PlayerNoise(edict_t *ent, float loudness)
{
	if (g_bot_active && ent && ent->client)
		RecordNoise(ent, loudness, 0);
}

void Bot_Explosive(edict_t *ent, int kind, int add)
{
	if (!g_bot_active || !ent)
		return;
	for (size_t i = 0; i < g_danger.size(); i++)
		if (g_danger[i].ent == ent) {
			if (!add)
				g_danger.erase(g_danger.begin() + i);
			return;
		}
	if (add)
		g_danger.push_back(BotDanger{ent, kind});
}
