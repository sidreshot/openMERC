/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
/*
 * OpenMerc: rewritten for the SoF 1.07f game module (G18) on the structure of Quake II's g_cmds.c:
 * SoF dispatch order (player-module commands before the game's), say/say_team with the sender-tagged
 * print, renamed cheats (heretic/ninja/phantom), gimme, killallmonsters, entlist, listpickups, kill,
 * wave, debug inventory kits. Bot waypoint and navigation commands belong to G5.
 * Implements: SPEC-006 Behaviour 4, SPEC-780 20
 */

#include "g_local.h"
#include "nav/nav_api.h" /* G5 */
#include "bots/bot_api.h" /* G5 */
#include "modes/g_modes.h" /* G9 */

int SP_Id(const char *package, const char *entry);
void SP_PrintTo(edict_t *ent, const char *package, const char *entry);
void ED_CallSpawn(edict_t *ent);

static bool CheatsOK(edict_t *ent)
{
	if (!gix.cvar_value("cheats")) {
		gi.cprintf(ent, PRINT_HIGH, "Cheats not available.\n");
		return false;
	}
	return true;
}

static void Cmd_God_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	ent->flags ^= FL_GODMODE;
	gi.cprintf(ent, PRINT_HIGH, (ent->flags & FL_GODMODE) ? "godmode ON\n" : "godmode OFF\n");
}

static void Cmd_Notarget_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	ent->flags ^= FL_NOTARGET;
	gi.cprintf(ent, PRINT_HIGH, (ent->flags & FL_NOTARGET) ? "notarget ON\n" : "notarget OFF\n");
}

static void Cmd_Noclip_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	if (ent->movetype == MOVETYPE_NOCLIP) {
		ent->movetype = MOVETYPE_WALK;
		gi.cprintf(ent, PRINT_HIGH, "noclip OFF\n");
	} else {
		ent->movetype = MOVETYPE_NOCLIP;
		gi.cprintf(ent, PRINT_HIGH, "noclip ON\n");
	}
}

static void Cmd_OmSetPos_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	if (gi.argc() < 4) {
		gi.cprintf(ent, PRINT_HIGH, "usage: omsetpos <x> <y> <z> [yaw] [pitch]\n");
		return;
	}
	vec3_t o;
	o[0] = (float)atof(gi.argv(1));
	o[1] = (float)atof(gi.argv(2));
	o[2] = (float)atof(gi.argv(3));
	/* revive if dead so glass_tv / booth tests are not stuck in death cam */
	if (ent->client && ent->health <= 0) {
		ent->health = ent->max_health > 0 ? ent->max_health : 100;
		ent->solid = SOLID_BBOX;
		ent->svflags &= ~SVF_DEADMONSTER;
		ent->client->ps.pmove.pm_type = PM_NORMAL;
		ent->deadflag = DEAD_NO;
		ent->movetype = MOVETYPE_WALK;
		gi.cprintf(ent, PRINT_HIGH, "omsetpos: revived\n");
	}
	VectorCopy(o, ent->s.origin);
	VectorClear(ent->velocity);
	VectorClear(ent->s.angles);
	if (ent->client) {
		float yaw = gi.argc() >= 5 ? (float)atof(gi.argv(4)) : 0.0f;
		float pitch = gi.argc() >= 6 ? (float)atof(gi.argv(5)) : 0.0f;
		ent->s.angles[YAW] = yaw;
		ent->s.angles[PITCH] = pitch;
		ent->client->ps.pmove.delta_angles[YAW] =
			(short)ANGLE2SHORT(yaw - ent->client->resp.cmd_angles[YAW]);
		ent->client->ps.pmove.delta_angles[PITCH] =
			(short)ANGLE2SHORT(pitch - ent->client->resp.cmd_angles[PITCH]);
		ent->client->ps.pmove.delta_angles[ROLL] =
			(short)ANGLE2SHORT(0.0f - ent->client->resp.cmd_angles[ROLL]);
		ent->client->ps.viewangles[YAW] = yaw;
		ent->client->ps.viewangles[PITCH] = pitch;
		ent->client->ps.viewangles[ROLL] = 0;
		VectorCopy(ent->client->ps.viewangles, ent->client->v_angle);
		VectorClear(ent->client->kick_angles);
	}
	gi.linkentity(ent);
	gi.cprintf(ent, PRINT_HIGH, "omsetpos %.1f %.1f %.1f yaw=%s pitch=%s\n",
		o[0], o[1], o[2],
		gi.argc() >= 5 ? gi.argv(4) : "-",
		gi.argc() >= 6 ? gi.argv(5) : "-");
}

static void Cmd_Gimme_f(edict_t *ent)
{
	if (!CheatsOK(ent) || gi.argc() < 2)
		return;
	char *name = gi.argv(1);
	vec3_t fwd;
	AngleVectors(ent->client->v_angle, fwd, NULL, NULL);
	edict_t *e = G_Spawn();
	e->classname = G_CopyString(name);
	for (int i = 0; i < 3; i++)
		e->s.origin[i] = (float)(int)(ent->s.origin[i] + fwd[i] * 100);
	e->s.origin[2] = (float)(int)(e->s.origin[2] + 50);
	e->s.angles[YAW] = ent->client->v_angle[YAW];
	gi.cprintf(NULL, PRINT_HIGH, "Spawning : %s\n", name); // shown without developer, as in mode A (SPEC-006)
	memset(&st, 0, sizeof(st));
	ED_CallSpawn(e);
}

static void Cmd_KillAllMonsters_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	int n = 0;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || !(e->svflags & SVF_MONSTER) || e->health <= 0)
			continue;
		T_Damage(e, ent, ent, vec3_origin, e->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION, MOD_SUICIDE);
		n++;
	}
	gi.cprintf(ent, PRINT_HIGH, "killed %d monsters\n", n);
}

static void Cmd_EntList_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	struct cnt {
		const char *name;
		int n;
	} counts[512];
	int nc = 0, monsters = 0, funcs = 0, total = 0;
	for (int i = 0; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		total++;
		if (!strncmp(e->classname, "m_", 2))
			monsters++;
		if (!strncmp(e->classname, "func_", 5))
			funcs++;
		int k;
		for (k = 0; k < nc; k++)
			if (!strcmp(counts[k].name, e->classname))
				break;
		if (k == nc && nc < 512) {
			counts[nc].name = e->classname;
			counts[nc++].n = 0;
		}
		if (k < 512)
			counts[k].n++;
	}
	gi.cprintf(ent, PRINT_HIGH, "Size Type\n");
	for (int k = 0; k < nc; k++)
		gi.cprintf(ent, PRINT_HIGH, "%.4d %s\n", counts[k].n, counts[k].name);
	gi.cprintf(ent, PRINT_HIGH, "%.4d Monsters\n", monsters);
	gi.cprintf(ent, PRINT_HIGH, "%.4d Funcs\n", funcs);
	gi.cprintf(ent, PRINT_HIGH, "%.4d Total\n", total);
}

// SPEC-780 7
static void Cmd_Kill_f(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (c->flood_locktill > level.time && deathmatch->value) {
		int id = SP_Id("general", "TEXT_NOKILL");
		if (id >= 0)
			gix.SP_Print(ent, id, (short)(c->flood_locktill - level.time + 1));
		return;
	}
	if (level.time - c->respawn_time < 5 - 0.001f || c->spawnprotect_time > level.time) // exactly 5 s is accepted (G9: orig_ctf_order)
		return;
	ent->flags &= ~FL_GODMODE;
	ent->health = 0;
	meansOfDeath = MOD_SUICIDE;
	player_die(ent, ent, ent, 100000, vec3_origin);
	respawn(ent);
	c->flood_locktill = level.time + flood_killdelay->value;
}

static void Cmd_PutAway_f(edict_t *ent)
{
	ent->client->showscores = false;
	ent->client->showhelp = false;
}

static void Cmd_Players_f(edict_t *ent)
{
	int index[MAX_CLIENTS], count = 0;
	for (int i = 0; i < maxclients->value && i < MAX_CLIENTS; i++)
		if (game.clients[i].pers.connected)
			index[count++] = i;
	for (int i = 1; i < count; i++)
		for (int j = i; j > 0 && game.clients[index[j]].resp.score > game.clients[index[j - 1]].resp.score; j--) {
			int t = index[j];
			index[j] = index[j - 1];
			index[j - 1] = t;
		}
	char large[1280];
	large[0] = 0;
	for (int i = 0; i < count; i++) {
		char small[128];
		// the address printed is the one of the next player in the sorted list (SPEC-780 20)
		const char *ip = Info_ValueForKey(game.clients[index[(i + 1) % count]].pers.userinfo, "ip");
		Com_sprintf(small, sizeof(small), "%3i %s %s\n", game.clients[index[i]].resp.score,
		            game.clients[index[i]].pers.netname, ip);
		if (strlen(small) + strlen(large) > sizeof(large) - 100) {
			strcat(large, "...\n");
			break;
		}
		strcat(large, small);
	}
	gi.cprintf(ent, PRINT_HIGH, "%s\n%i players\n", large, count);
}

static void Cmd_Wave_f(edict_t *ent)
{
	if (!deathmatch->value)
		return;
	int i = atoi(gi.argv(1));
	if (i < 1 || i > 4)
		gi.dprintf("Unrecognized wave value: %d, trying to play 'go'.\n", i);
	ent->client->wave_pending = i;
}

static void Cmd_Say_f(edict_t *ent, qboolean team, qboolean arg0)
{
	char text[2048];
	if (gi.argc() < 2 && !arg0)
		return;
	if (!((int)dmflags->value & 0x80))
		team = false;

	// the message alone goes to the clients: their sender-tagged print adds "<name> : " itself (mode A probes print
	// "John Mullins : notarget", mode B printed the name twice); the full "<name>: message" is the server echo
	char msg[1024];
	if (arg0) {
		Com_sprintf(msg, sizeof(msg), "%s %s", gi.argv(0), gi.args());
	} else {
		char *p = gi.args();
		if (*p == '"') {
			p++;
			p[strlen(p) - 1] = 0;
		}
		Com_sprintf(msg, sizeof(msg), "%s", p);
	}
	Com_sprintf(text, sizeof(text), "%s: %s", ent->client->pers.netname, msg);
	if (strlen(text) > 150 || strlen(msg) > 150) {
		text[150] = 0;
		msg[150] = 0;
		gi.cprintf(ent, PRINT_HIGH, "Chat string too long - truncated to 150 chars\n");
	}
	strcat(text, "\n");
	strcat(msg, "\n");

	gclient_t *cl = ent->client;
	if (flood_msgs->value) {
		if (level.time < cl->flood_locktill) {
			int id = SP_Id("general", "TEXT_SHUT_UP");
			if (id >= 0)
				gix.SP_Print(ent, id, (short)(cl->flood_locktill - level.time));
			return;
		}
		int i = cl->flood_whenhead - (int)flood_msgs->value + 1;
		if (i < 0)
			i = 10 + i;
		if (cl->flood_when[i] && level.time - cl->flood_when[i] < flood_persecond->value) {
			cl->flood_locktill = level.time + flood_waitdelay->value;
			int id = SP_Id("general", "TEXT_SHUT_UP");
			if (id >= 0)
				gix.SP_Print(ent, id, (short)flood_waitdelay->value);
			return;
		}
		cl->flood_whenhead = (cl->flood_whenhead + 1) % 10;
		cl->flood_when[cl->flood_whenhead] = level.time;
	}

	if (dedicated->value)
		gix.cprintf(NULL, PRINT_CHAT, "%s", text);

	for (int j = 1; j <= game.maxclients; j++) {
		edict_t *other = &g_edicts[j];
		if (!other->inuse || !other->client || other->client->pers.isbot)
			continue;
		if (team && !OnSameTeam(ent, other))
			continue;
		gix.clprintf(other, ent, team ? 1 : 0, "%s", msg);
	}
	Bot_HeardChat(ent, text); /* G5: bot replies (SPEC-741 12) */
}

static void Cmd_PlayerList_f(edict_t *ent)
{
	char st[80], text[1400];
	text[0] = 0;
	edict_t *e2 = g_edicts + 1;
	for (int i = 0; i < maxclients->value; i++, e2++) {
		if (!e2->inuse)
			continue;
		Com_sprintf(st, sizeof(st), "%02d:%02d %4d %3d %s%s\n", (level.framenum - e2->client->resp.enterframe) / 600,
		            ((level.framenum - e2->client->resp.enterframe) % 600) / 10, e2->client->ping,
		            e2->client->resp.score, e2->client->pers.netname, e2->client->resp.spectator ? " (spectator)" : "");
		if (strlen(text) + strlen(st) > sizeof(text) - 50) {
			strcat(text, "And more...\n");
			gi.cprintf(ent, PRINT_HIGH, "%s", text);
			return;
		}
		strcat(text, st);
	}
	gi.cprintf(ent, PRINT_HIGH, "%s", text);
}

// cheat kits (SPEC-780 20)
static void GiveKit(edict_t *ent, int kit)
{
	if (!CheatsOK(ent))
		return;
	gclient_t *c = ent->client;
	PlayerInventory *old = c->inv;
	int a = old ? old->GetVarA() : -1, b = old ? old->GetVarB() : 1, cc = old ? old->GetVarC() : -1;
	PlayerDeleteInventory(old);
	// the new inventory needs this client's owner context before the kit is given: without it player.dll works on
	// whatever context it had last (another client's) and faults with two clients connected (G3 report, GiveKit)
	PlayerSpawning(ent);
	c->inv = PlayerNewInventory();
	PlayerSpawning(NULL);
	PlayerSetIdleContext(ent);
	PredictionRestart(ent);
	PlayerInventory *inv = c->inv;
	if (!inv)
		return;
	inv->SetVarA(a);
	inv->SetVarB(b);
	inv->SetVarC(cc);
	bool inf = ((int)dmflags->value & 0x800) && deathmatch->value;
	inv->GiveWeapon(WP_HURTHAND, 0);
	inv->GiveWeapon(WP_THROWHAND, 0);
	if (kit == 0) {
		inv->GiveWeapon(WP_KNIFE, 0);
		inv->GiveWeapon(WP_PISTOL1, 0);
		if (!inf) {
			inv->AddAmmo(AM_9MM, 150);
			inv->AddAmmo(AM_KNIFE, 6);
		}
		inv->AddItems(IT_C4, 2, -1);
		inv->SwitchToWeapon(WP_PISTOL1);
		inv->AddArmor(-100);
		ent->flags &= ~FL_GODMODE;
		gix.cvar_set("nodamage", "0");
	} else {
		static const int k1[] = {WP_KNIFE, WP_PISTOL1, WP_PISTOL2, WP_SHOTGUN, WP_SNIPER, WP_ASSAULT};
		static const int k2[] = {WP_KNIFE, WP_SLUGGER, WP_ROCKET, WP_FLAMEGUN, WP_MPG, WP_MPISTOL, WP_MACHINEGUN};
		if (kit == 1)
			for (int w : k1)
				inv->GiveWeapon(w, 0);
		else
			for (int w : k2)
				inv->GiveWeapon(w, 0);
		if (!inf)
			for (int t = 1; t <= 9; t++)
				inv->AddAmmo(t, 2000);
		inv->AddItems(IT_GRENADE, 100, -1);
		inv->AddItems(IT_GOGGLES, 100, -1);
		inv->AddItems(IT_FLASHPACK, 100, -1);
		inv->AddItems(IT_C4, 100, -1);
		inv->SwitchToWeapon(kit == 1 ? WP_PISTOL1 : WP_SLUGGER);
	}
	inv->TopUpClips();
	if (pe)
		pe->PrecacheViewModels(0x7FFE);
}

// SPEC-780 §20 `givesnipertutorial` / `givemoretutorial` (single player, tutorial levels): recreate the inventory
// and give the sniper rifle with 200 rounds 5.56 / the .44 pistol with 90 rounds, and select it. The player variables
// are kept and prediction restarts as for the cheat kits (UNVERIFIED for these two commands; G4's report: they fell
// through to `say`).
static void GiveTutorial(edict_t *ent, bool sniper)
{
	if (deathmatch->value)
		return;
	gclient_t *c = ent->client;
	PlayerInventory *old = c->inv;
	int a = old ? old->GetVarA() : -1, b = old ? old->GetVarB() : 1, cc = old ? old->GetVarC() : -1;
	PlayerDeleteInventory(old);
	PlayerSpawning(ent);
	c->inv = PlayerNewInventory();
	PlayerSpawning(NULL);
	PlayerSetIdleContext(ent);
	PredictionRestart(ent);
	PlayerInventory *inv = c->inv;
	if (!inv)
		return;
	inv->SetVarA(a);
	inv->SetVarB(b);
	inv->SetVarC(cc);
	int w = sniper ? WP_SNIPER : WP_PISTOL2;
	inv->GiveWeapon(w, 0);
	inv->AddAmmo(sniper ? AM_556 : AM_44, sniper ? 200 : 90);
	inv->TopUpClips(); // oracle (tut_orig, arm1): clip 6 / reserve 194, then clip 9 / reserve 81
	if (pe)
		pe->PrecacheViewModels(1u << w);
	inv->SwitchToWeapon(w);
}

void ClientCommand(edict_t *ent)
{
	if (!ent->client)
		return;
	char *cmd = gi.argv(0);
	if (Nav_ClientCommand(ent) || Bot_ClientCommand(ent)) /* G5: node editor, waypoint editor (SPEC-740 12, 780 20) */
		return;

	if (Q_stricmp(cmd, "players") == 0) {
		Cmd_Players_f(ent);
		return;
	}
	if (Q_stricmp(cmd, "say") == 0) {
		Cmd_Say_f(ent, false, false);
		return;
	}
	if (Q_stricmp(cmd, "say_team") == 0) {
		Cmd_Say_f(ent, true, false);
		return;
	}
	if (Q_stricmp(cmd, "score") == 0) {
		Cmd_Score_f(ent);
		return;
	}
	if (Q_stricmp(cmd, "help") == 0) {
		Cmd_Help_f(ent);
		return;
	}

	// end-of-mission menus (acc, acc2, blankscreen) run it during the intermission (SPEC-780 20, SPEC-AMEND-007 G18.41)
	if (Q_stricmp(cmd, "updateinvfinal") == 0) {
		if (!deathmatch->value)
			Stats_UpdateInvFinal(ent);
		return;
	}

	if (level.intermissiontime || game.cinematic)
		return;

	if (!ent->client->resp.spectator && ent->client->inv && ent->client->inv->HandleCommand(cmd, 0))
		return;

	if (Q_stricmp(cmd, "heretic") == 0)
		Cmd_God_f(ent);
	else if (Q_stricmp(cmd, "ninja") == 0)
		Cmd_Notarget_f(ent);
	else if (Q_stricmp(cmd, "phantom") == 0)
		Cmd_Noclip_f(ent);
	else if (Q_stricmp(cmd, "omsetpos") == 0)
		Cmd_OmSetPos_f(ent);
	else if (Q_stricmp(cmd, "gimme") == 0)
		Cmd_Gimme_f(ent);
	else if (Q_stricmp(cmd, "defaultweapons") == 0) {
		if (CheatsOK(ent))
			GM_DefaultWeapons(ent); /* G9: SPEC-790 the level's default weapons */
	}
	else if (Q_stricmp(cmd, "killallmonsters") == 0)
		Cmd_KillAllMonsters_f(ent);
	else if (Q_stricmp(cmd, "entlist") == 0)
		Cmd_EntList_f(ent);
	else if (Q_stricmp(cmd, "listpickups") == 0) {
		if (CheatsOK(ent))
			Cmd_ListPickups_f(ent);
	} else if (Q_stricmp(cmd, "kill") == 0)
		Cmd_Kill_f(ent);
	else if (Q_stricmp(cmd, "putaway") == 0)
		Cmd_PutAway_f(ent);
	else if (Q_stricmp(cmd, "wave") == 0)
		Cmd_Wave_f(ent);
	else if (Q_stricmp(cmd, "playerlist") == 0)
		Cmd_PlayerList_f(ent);
	else if (Q_stricmp(cmd, "teamview") == 0)
		ent->client->thirdperson = atoi(gi.argv(1)) != 0;
	else if (Q_stricmp(cmd, "playlevel") == 0)
		GiveKit(ent, 0);
	else if (Q_stricmp(cmd, "elbow") == 0)
		GiveKit(ent, 1);
	else if (Q_stricmp(cmd, "bigelbow") == 0)
		GiveKit(ent, 2);
	else if (Q_stricmp(cmd, "givesnipertutorial") == 0)
		GiveTutorial(ent, true);
	else if (Q_stricmp(cmd, "givemoretutorial") == 0)
		GiveTutorial(ent, false);
	else if (Q_stricmp(cmd, "turnoffmissionmsg") == 0)
		level.missionstatus = 0;
	else if (Q_stricmp(cmd, "turnonmissionmsg") == 0)
		level.missionstatus = 3;
	else
		Cmd_Say_f(ent, false, true);
}
