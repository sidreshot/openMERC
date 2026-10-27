// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): client hooks. Team number assignment and force join, entering the
// game, spawn kits (deathmatch default, Realistic), disconnect, end of frame, icon tables (svc 33), obituaries,
// death drops and the default scoring.
// Implements: SPEC-790 4 (team assignment), Behaviour 3 (spawn kit, death drop, icons, scoring), 5 (Realistic
//             kit, fatigue, disconnect), 9/10 (bot kits), SPEC-780 18 (default scoring and obituaries)
#include "gm_internal.h"

extern "C" void G_SetPlayerInfo(edict_t *ent);

//-----------------------------------------------------------------------------------------------------------
// team number assignment (SPEC-790 4)

static void gm_setteamkey(char *userinfo, int team)
{
	char v[8];
	Com_sprintf(v, sizeof(v), "%d", team - 1);
	Info_SetValueForKey(userinfo, "team_red_blue", v);
}

void GM_PlayerInfo(edict_t *ent) { G_SetPlayerInfo(ent); }

void GM_UserinfoChanged(edict_t *ent, char *userinfo, bool spawnvariant)
{
	// Called by the core for connected clients: the engine's call right after ClientConnect (not yet in the game),
	// in-game changes and the spawn variant. The original assigns the team already at the connect (captures orig_ctf,
	// orig_ctf_forcejoin: `Bravo\mullins\mullins\1` twice before Bravo has entered).
	if (!GM_AssignsTeams() || !GM_IsClient(ent) || !userinfo)
		return;
	bool entered = ent->inuse;
	gclient_t *cl = ent->client;
	int slot = GM_Slot(ent);
	if (!GM_ForceJoin()) {
		int team = (atoi(Info_ValueForKey(userinfo, "team_red_blue")) & 1) + 1;
		if (!spawnvariant && cl->resp.team && cl->resp.team != team && cl->pers.connected && ent->inuse)
			cl->forcesuicide = 1; // forced team-change suicide, score reset (SPEC-780 B5 step 2)
		cl->resp.team = team;
	} else if (cl->resp.team) {
		// the refusal answers a change by the player; entering and respawning (the spawn variant, team assigned at
		// the connect) print nothing (orig_ctf_forcejoin: only Alpha's own change at 8 s gets NO_CHANGE)
		if (!spawnvariant)
			GM_Print(ent, SP_DM_GENERIC, DMG_NO_CHANGE);
		gm_setteamkey(userinfo, cl->resp.team);
	} else {
		// developer lines as the original prints them (capture orig_ctf_forcejoin): the joiner's entity-number field
		// (0 until his entity is initialised in this level, then slot + 1; SPEC-AMEND-005 G9.6), then every slot
		// 1..maxclients including the joiner, empty names for free slots
		gi.dprintf("player %d %s attempting to join\n", ent->s.number, cl->pers.netname);
		int blue = 0, red = 0;
		for (int i = 0; i < game.maxclients; i++) {
			gclient_t *o = &game.clients[i];
			gi.dprintf("player %d %s val %d\n", i + 1, g_edicts[i + 1].inuse || o->pers.connected ? o->pers.netname : "",
			           o->resp.team);
			if (i == slot)
				continue;
			if (o->resp.team == 1)
				blue++;
			else if (o->resp.team == 2)
				red++;
		}
		int team;
		if (blue < red)
			team = 1;
		else if (red < blue)
			team = 2;
		else
			team = (GM_Rand() & 1) ? 1 : 2; // U{0,1}: 0 -> Red, 1 -> Blue
		gi.dprintf("team balance blue %d red %d player put on team %d\n", blue, red, team);
		cl->resp.team = team;
		gm_setteamkey(userinfo, team);
	}
	GM_Cl(ent)->team = cl->resp.team;
	// Player-info writes of the original (captures orig_ctf, orig_ctf_forcejoin): at the connect the full entry twice
	// (this one and the core's), when entering once (the core's), on a respawn none, on an in-game change once (the
	// core's, also after NO_CHANGE). So the mode writes it only for a client that has not entered yet.
	if (!cl->pers.isbot && !entered)
		GM_PlayerInfo(ent);
}

//-----------------------------------------------------------------------------------------------------------
// icon tables (player-name colours, svc 33)

static uint8_t gm_iconvalue(edict_t *viewer, edict_t *other)
{
	gm_client_t *v = GM_Cl(viewer), *o = GM_Cl(other);
	if (!v || !o)
		return 0;
	// 0 when the viewer is a spectator; whether the other one is a spectator is not tested (SPEC-AMEND-005 G9.3).
	// A player's own entry is 1 (capture orig_assassin).
	if (GM_IsSpectator(viewer))
		return 0;
	if (v->target == other)
		return 3;
	if (o->target == viewer)
		return 2;
	return 1;
}

void GM_UpdateIcon(edict_t *viewer, edict_t *other)
{
	gm_client_t *v = GM_Cl(viewer);
	int s = GM_Slot(other);
	if (!v || s < 0)
		return;
	v->icons[s] = gm_iconvalue(viewer, other);
}

void GM_ResetIcons(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	int s = GM_Slot(ent);
	if (!c)
		return;
	// both directions back to 0; the sent copy is cleared too (the original sends nothing for an all-zero table)
	memset(c->icons, 0, sizeof(c->icons));
	memset(c->sent, 0, sizeof(c->sent));
	memset(c->sentvalid, 1, sizeof(c->sentvalid));
	for (int i = 0; i < MAX_CLIENTS; i++)
		gm_cl[i].icons[s] = 0;
}

void GM_SendIcons(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	if (!c || !ent->inuse || ent->client->pers.isbot || !gix.SendPlayernameColors)
		return;
	uint8_t data[3 * MAX_CLIENTS];
	int len = 0, count = 0;
	int n = game.maxclients < MAX_CLIENTS ? game.maxclients : MAX_CLIENTS;
	int i = 0;
	while (i < n) {
		if (c->sentvalid[i] && c->sent[i] == c->icons[i]) {
			i++;
			continue;
		}
		int j = i;
		while (j + 1 < n && !(c->sentvalid[j + 1] && c->sent[j + 1] == c->icons[j + 1]) &&
		       c->icons[j + 1] == c->icons[i])
			j++;
		if (j > i) {
			data[len++] = (uint8_t)(0x80 | (c->icons[i] & 0x7F));
			data[len++] = (uint8_t)i;
			data[len++] = (uint8_t)j;
		} else {
			data[len++] = (uint8_t)(c->icons[i] & 0x7F);
			data[len++] = (uint8_t)i;
		}
		for (int k = i; k <= j; k++) {
			c->sent[k] = c->icons[k];
			c->sentvalid[k] = true;
		}
		count += j - i + 1;
		i = j + 1;
	}
	if (count)
		gix.SendPlayernameColors(ent, len, count, data);
}

//-----------------------------------------------------------------------------------------------------------
// entering the game, spawning

void GM_ClientBegin(edict_t *ent)
{
	if (!GM_IsClient(ent))
		return;
	gm_client_t *c = GM_Cl(ent);
	// A client connecting with `spectator 1` is a spectator from his first frame (capture orig_assassin: Delta gets
	// no target, nobody targets him). G18 copies pers.spectator into resp only in PutClientInServer, right after
	// this hook, so the begin rules take the wanted state now.
	if (GM_Deathmatch() && ent->client->pers.spectator)
		ent->client->resp.spectator = true;
	if (GM_Deathmatch())
		GM_ResetIcons(ent);
	c->spectating = ent->client->resp.spectator != 0;
	c->fatigue = 0;
	c->exhausted = false;
	c->inzone = c->inzone_prev = false;
	if (gm.mode == GM_ASSASSIN)
		GM_AssassinBegin(ent);
	else if (gm.mode == GM_ARSENAL)
		GM_ArsenalKit(ent, true);
}

void GM_NewInventory(edict_t *ent)
{
	gclient_t *cl = ent->client;
	if (cl->inv)
		PlayerDeleteInventory(cl->inv);
	cl->inv = PlayerNewInventory();
	if (!cl->pers.isbot)
		PredictionRestart(ent);
}

static bool gm_botashuman(void) { return gm.mode == GM_CONTROL || gm.mode == GM_CONQUER; }

void GM_DefaultKit(edict_t *ent)
{
	gclient_t *cl = ent->client;
	PlayerInventory *inv = cl->inv;
	if (!inv)
		return;
	bool infinite = GM_Flag(DF_INFINITE_AMMO);
	inv->RemoveAll(true);
	inv->SetVarA(infinite ? 0 : -1);
	if (!cl->pers.isbot || gm_botashuman()) {
		inv->SetVarC(GM_Flag(DF_NO_RELOAD) ? 0 : -1);
		inv->SetTag(cl->pers.bestweap);
		inv->SetVarB(GM_Flag(DF_WEAPONS_STAY) ? 0 : -1);
		inv->SetVarI((1 << WP_KNIFE) | (GM_Flag(DF_REALISTIC_DAMAGE) ? (1 << WP_PISTOL1) : 0));
	} else {
		inv->SetTag("unsafe");
		if (gm.mode == GM_CTF)
			inv->SetVarI(1 << WP_KNIFE);
	}
	inv->GiveWeapon(WP_KNIFE, 0);
	inv->GiveWeapon(WP_PISTOL1, 0);
	inv->GiveWeapon(WP_HURTHAND, 0);
	inv->GiveWeapon(WP_THROWHAND, 0);
	if (!infinite) {
		inv->AddAmmo(AM_KNIFE, 6);
		inv->AddAmmo(AM_9MM, 150);
	}
	inv->SwitchToWeapon(WP_PISTOL1);
}

void GM_RealisticKit(edict_t *ent)
{
	gclient_t *cl = ent->client;
	PlayerInventory *inv = cl->inv;
	gm_client_t *c = GM_Cl(ent);
	if (!inv)
		return;
	const int kept = (1 << WP_KNIFE) | (1 << WP_PISTOL1);
	inv->RemoveAll(true);
	inv->SetVarA(-1);
	if (!cl->pers.isbot) {
		inv->SetVarB(-1);
		inv->SetVarI(kept);
		inv->SetVarC(-1);
		inv->SetVarD(0);
		inv->SetTag(cl->pers.bestweap);
		inv->SetVarG(-1);
		inv->SetVarE(-1);
		inv->SetVarF(-1);
	} else {
		inv->SetTag("unsafe");
		inv->SetVarI(kept);
		inv->SetVarF(-1);
	}
	inv->GiveWeapon(WP_KNIFE, 0);
	inv->GiveWeapon(WP_PISTOL1, 0);
	inv->GiveWeapon(WP_HURTHAND, 0);
	inv->GiveWeapon(WP_THROWHAND, 0);
	inv->AddAmmo(AM_KNIFE, 6);
	inv->AddAmmo(AM_9MM, 72);
	inv->SwitchToWeapon(WP_PISTOL1);
	if (c) {
		c->fatigue = 0;
		c->exhausted = false;
	}
}

void GM_ClientSpawn(edict_t *ent)
{
	if (!GM_IsClient(ent) || !GM_Deathmatch())
		return;
	gm_client_t *c = GM_Cl(ent);
	if (GM_AssignsTeams() && c->team)
		ent->client->resp.team = c->team;
	bool spectator = ent->client->resp.spectator != 0;
	bool wasspectating = c->spectating;
	c->spectating = spectator;
	switch (gm.mode) {
	case GM_ARSENAL:
		GM_ArsenalKit(ent, spectator);
		if (!spectator)
			GM_StatusLayout(ent);
		break;
	case GM_REALISTIC:
		GM_RealisticKit(ent);
		break;
	case GM_ASSASSIN:
		GM_AssassinSpawn(ent, wasspectating);
		break;
	default:
		GM_DefaultKit(ent);
		break;
	}
}

//-----------------------------------------------------------------------------------------------------------
// disconnect

void GM_ClientDisconnect(edict_t *ent)
{
	if (!GM_IsClient(ent))
		return;
	gm_client_t *c = GM_Cl(ent);
	switch (gm.mode) {
	case GM_REALISTIC:
		if (ent->client->inv)
			ent->client->inv->DropAllButKept();
		break;
	case GM_ASSASSIN:
		GM_AssassinDisconnect(ent);
		break;
	case GM_CTF:
		ent->client->resp.team = 0;
		c->team = 0;
		if (c->flagteam)
			GM_CTFDropCarriedFlag(ent);
		break;
	case GM_CONTROL:
		ent->client->resp.team = 0;
		c->team = 0;
		break;
	default:
		break;
	}
}

void GM_ClientDisconnected(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	if (!c)
		return;
	// the default rule rewrites the game-log footer into the closed file: no observable effect (SPEC-790 B3)
	c->target = c->pending = NULL;
	c->flagteam = 0;
	c->inzone = c->inzone_prev = false;
	c->arsenal_have = false;
	for (int i = 0; i < MAX_CLIENTS; i++) {
		if (gm_cl[i].target == ent)
			gm_cl[i].target = NULL;
	}
}

//-----------------------------------------------------------------------------------------------------------
// end of a client's frame

void GM_ClientEndFrame(edict_t *ent)
{
	if (!GM_IsClient(ent) || gm.mode < 0)
		return;
	switch (gm.mode) {
	case GM_SP: GM_SetStats(ent); break;
	case GM_REALISTIC: GM_RealisticEndFrame(ent); break;
	case GM_ASSASSIN: GM_AssassinEndFrame(ent); break;
	case GM_CONQUER:
		GM_ConquerEndFrame(ent);
		GM_ConquerConfigstrings();
		break;
	default: break;
	}
	if (GM_Deathmatch()) {
		// the status / help layout lasts until its help time; refreshed every 32 frames while shown
		gclient_t *cl = ent->client;
		if (cl->showhelp && level.time >= cl->showhelp_time) {
			cl->showhelp = false;
			if (!cl->showscores)
				cl->ps.stats[STAT_LAYOUTS] &= ~0x1;
		} else if (cl->showhelp && !(level.framenum & 31))
			GM_StatusRefresh(ent);
		GM_SendIcons(ent);
	}
}

// Realistic fatigue (SPEC-790 B5)
void GM_RealisticEndFrame(edict_t *ent)
{
	gclient_t *cl = ent->client;
	gm_client_t *c = GM_Cl(ent);
	if (cl->pers.isbot)
		return; // bots keep the counter in their bot state (G5)
	if (cl->resp.spectator) {
		c->fatigue = 0;
		c->exhausted = false;
		cl->ps.stats[STAT_FLAGS] &= ~0x200;
		cl->ps.stats[STAT_METER] = 0;
	} else {
		if ((cl->buttons & BUTTON_RUN) && (ent->velocity[0] != 0 || ent->velocity[1] != 0)) {
			c->fatigue += 2;
			if (c->fatigue >= 255) {
				c->fatigue = 255;
				c->exhausted = true;
			}
		} else if (c->fatigue > 0) {
			c->fatigue -= 3;
			if (c->fatigue < 255)
				c->exhausted = false;
			if (c->fatigue < 1)
				c->fatigue = 0;
		}
		cl->ps.stats[STAT_FLAGS] |= 0x200;
		cl->ps.stats[STAT_METER] = (short)(int)((float)c->fatigue / 255.0f * 100.0f);
	}
	if (c->exhausted)
		cl->ps.pmove.pm_flags |= PMF_EXHAUSTED;
	else
		cl->ps.pmove.pm_flags &= ~PMF_EXHAUSTED;
}

//-----------------------------------------------------------------------------------------------------------
// death: obituaries (SPEC-780 18), death drop, scoring

static int gm_obit_self(int mod, bool female)
{
	switch (mod) {
	case MOD_PHOS_GRENADE: return DMG_TEXT_OBT_MOD_PHOS_GRENADE;
	case MOD_ROCKET_SPLASH: return female ? DMG_TEXT_OBT_MOD_ROCKET_SPLASH_F : DMG_TEXT_OBT_MOD_ROCKET_SPLASH_M;
	case MOD_C4: return female ? DMG_TEXT_OBT_MOD_C4_F : DMG_TEXT_OBT_MOD_C4_M;
	case MOD_CLAYMORE: return DMG_TEXT_OBT_MOD_CLAYMORE;
	case MOD_NEURAL_GRENADE: return female ? DMG_TEXT_OBT_MOD_NEURAL_GRENADE_F : DMG_TEXT_OBT_MOD_NEURAL_GRENADE_M;
	case MOD_GRENADE: return female ? DMG_TEXT_OBT_MOD_GRENADE_F : DMG_TEXT_OBT_MOD_GRENADE_M;
	default: return female ? DMG_TEXT_OBT_F : DMG_TEXT_OBT_M;
	}
}

static int gm_obit_world(int mod)
{
	switch (mod) {
	case MOD_WATER: return DMG_TEXT_OBT_MOD_WATER;
	case MOD_SLIME: return DMG_TEXT_OBT_MOD_SLIME;
	case MOD_CRUSH: return DMG_TEXT_OBT_MOD_CRUSH;
	case MOD_FALLING: return DMG_TEXT_OBT_MOD_FALLING;
	case MOD_SUICIDE: return DMG_TEXT_OBT_MOD_SUICIDE;
	case MOD_EXPLOSIVE: return DMG_TEXT_OBT_MOD_EXPLOSIVE;
	case MOD_FIRE:
	case MOD_TRIGGER_HURT: return DMG_TEXT_OBT_MOD_FIRE;
	case MOD_LAVA: return DMG_TEXT_OBT_MOD_LAVA;
	case MOD_EXIT: return DMG_TEXT_OBT_MOD_EXIT;
	case MOD_BARBWIRE: return DMG_TEXT_OBT_MOD_BARBWIRE;
	case MOD_DOGBITE: return DMG_TEXT_OBT_MOD_DOGBITE;
	default: return -1;
	}
}

static int gm_obit_kill(int mod)
{
	switch (mod) {
	case MOD_KNIFE_SLASH: return DMG_TEXT_OBT_MOD_KNIFE_SLASH;
	case MOD_KNIFE_THROWN: return DMG_TEXT_OBT_MOD_KNIFE_THROWN;
	case MOD_PISTOL1: return DMG_TEXT_OBT_MOD_PISTOL1;
	case MOD_PISTOL2: return DMG_TEXT_OBT_MOD_PISTOL2;
	case MOD_MPISTOL: return DMG_TEXT_OBT_MOD_MPISTOL;
	case MOD_ASSAULTRIFLE: return DMG_TEXT_OBT_MOD_ASSAULTRIFLE;
	case MOD_SNIPERRIFLE: return DMG_TEXT_OBT_MOD_SNIPERRIFLE;
	case MOD_SLUG: return DMG_TEXT_OBT_MOD_AUTOSHOTGUN;
	case MOD_SHOTGUN: return DMG_TEXT_OBT_MOD_SHOTGUN;
	case MOD_MACHINEGUN: return DMG_TEXT_OBT_MOD_MACHINEGUN;
	case MOD_PHOS_GRENADE: return DMG_TEXT_OBT_MOD_PHOS_GRENADE_2;
	case MOD_ROCKET: return DMG_TEXT_OBT_MOD_ROCKET;
	case MOD_ROCKET_SPLASH: return DMG_TEXT_OBT_MOD_ROCKET_SPLASH;
	case MOD_MPG: return DMG_TEXT_OBT_MOD_MPG;
	case MOD_FLAMEGUN: return DMG_TEXT_OBT_MOD_FLAMEGUN;
	case MOD_FLAMEGUN_NAPALM: return DMG_TEXT_OBT_MOD_FLAMEGUN_NAPALM;
	case MOD_C4: return DMG_TEXT_OBT_MOD_C4;
	case MOD_CLAYMORE: return DMG_TEXT_OBT_MOD_CLAYMORE_2;
	case MOD_NEURAL_GRENADE: return DMG_TEXT_OBT_MOD_NEURAL_GRENADE;
	case MOD_TELEFRAG: return DMG_TEXT_OBT_MOD_TELEFRAG;
	case MOD_GRENADE: return DMG_TEXT_OBT_MOD_GRENADE_2;
	case MOD_CONC_GRENADE: return DMG_TEXT_OBT_MOD_CONC_GRENADE;
	default: return DMG_TEXT_OBT_DIED;
	}
}

static void gm_obit_print(int entry, edict_t *a, edict_t *b)
{
	int id = SPID(SP_DM_GENERIC, entry);
	int na = GM_EntNum(a), nb = GM_EntNum(b);
	// the original game prints nothing more on a dedicated server (capture orig_dm: no obituary line in the server
	// log); SPEC-780 18's console echo is left to the engine's obituary print
	if (b)
		gix.SP_Print_Obit(NULL, id, na, nb);
	else
		gix.SP_Print_Obit(NULL, id, na);
}

void GM_Obituary(edict_t *self, edict_t *inflictor, edict_t *attacker)
{
	(void)inflictor;
	if (!GM_Deathmatch() || !GM_IsClient(self))
		return;
	int mod = meansOfDeath & ~MOD_FRIENDLY_FIRE;
	if (attacker == self) {
		gm_obit_print(gm_obit_self(mod, self->client->female != 0), self, NULL);
		return;
	}
	int w = gm_obit_world(mod);
	if (w >= 0) {
		gm_obit_print(w, self, NULL);
		return;
	}
	self->enemy = attacker;
	if (!GM_IsClient(attacker)) {
		gm_obit_print(DMG_TEXT_OBT_DIED, self, NULL);
		return;
	}
	gm_obit_print(gm_obit_kill(mod), self, attacker);
}

void GM_DeathDrop(edict_t *self)
{
	if (!GM_Deathmatch() || !GM_IsClient(self) || GM_NoDrops())
		return;
	PlayerInventory *inv = self->client->inv;
	if (!inv)
		return;
	inv->SetVarB(-1);
	inv->DoPendingDrop();
	self->client->ps.weaponinst = NULL;
}

static void gm_defaultscore(edict_t *self, edict_t *attacker)
{
	if (attacker == self) {
		float pen = sv_suicidepenalty ? sv_suicidepenalty->value : 2.0f;
		GM_SetScore(self, (int)((float)GM_GetScore(self) - pen));
	} else if (GM_IsClient(attacker)) {
		GM_AddScore(attacker, (meansOfDeath & MOD_FRIENDLY_FIRE) ? -1 : 1);
	} else
		GM_AddScore(self, -1);
}

void GM_Score(edict_t *self, edict_t *inflictor, edict_t *attacker)
{
	(void)inflictor;
	if (!GM_Deathmatch() || !GM_IsClient(self))
		return;
	switch (gm.mode) {
	case GM_ASSASSIN: GM_AssassinScore(self, attacker); break;
	case GM_ARSENAL: GM_ArsenalScore(self, attacker); break;
	case GM_CTF: GM_CTFScore(self, attacker); break;
	default: gm_defaultscore(self, attacker); break;
	}
}

bool GM_FragSounds(void) { return gm.mode != GM_ASSASSIN; }

void GM_ClientDamaged(edict_t *targ, edict_t *attacker)
{
	if (gm.mode != GM_CTF || !GM_IsClient(targ) || !GM_IsClient(attacker) || targ == attacker)
		return;
	gm_client_t *t = GM_Cl(targ), *a = GM_Cl(attacker);
	if (t->flagteam)
		a->hurtcarrier = level.time;
}

void GM_DropFlag(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	if (gm.mode == GM_CTF && c && c->flagteam)
		GM_CTFDropCarriedFlag(ent);
}

int GM_CarriedFlag(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	return c ? c->flagteam : 0;
}

void GM_SetCarriedFlag(edict_t *ent, int team)
{
	gm_client_t *c = GM_Cl(ent);
	if (c)
		c->flagteam = team;
}
