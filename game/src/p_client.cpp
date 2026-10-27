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
 * OpenMerc: rewritten for the SoF 1.07f game module (G18) on the structure of Quake II's p_client.c:
 * SoF connect checks and userinfo, player-module inventory objects and prediction restarts, spawn and
 * deathmatch loadout, spawn points (team points), body queue, death, respawn, spectators, ClientThink with
 * the engine's player movement (measured block layout), command splitting, move scale, lean, use key,
 * target identification, fire times and the per-frame owner context.
 * Implements: SPEC-780 1, 2, 3 (stats inputs), 4-10, 18 (scoring, obituaries), 21
 */

#include "g_local.h"
#include "damage/damage_api.h" /* G3: player bodies, corpses */
#include "effects/ambient.h" /* G7: music value rules */
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */
#include "bots/bot_api.h" /* G5 */
#include "scriptglue/g_scriptglue.h" /* G6b */
#include "roster/roster_api.h" /* G4: player corpse character */

int SP_Id(const char *package, const char *entry);
void G_AddEvent(edict_t *ent, int ev, int parm);

static const char *PlayerModelName(gclient_t *c) { return c->modelname[0] ? c->modelname : "mullins"; }

// SPEC-780 Interface 5. A `*` marks a team or model that replaced the requested one (measured, OPEN_QUESTIONS G18 29).
static void PlayerInfoConfigstring(edict_t *ent, bool shortform)
{
	gclient_t *c = ent->client;
	int slot = G_EdictNum(ent) - 1;
	char buf[256];
	const char *team = c->teamname[0] ? c->teamname : PlayerModelName(c);
	if (shortform) {
		Com_sprintf(buf, sizeof(buf), "%s\\%s\\%s\\0", c->pers.netname, team, PlayerModelName(c));
	} else {
		int teamnum = c->resp.team ? c->resp.team - 1 : -1;
		Com_sprintf(buf, sizeof(buf), "%s\\%s%s\\%s%s\\%d", c->pers.netname, c->team_star ? "*" : "", team,
		            c->skin_star ? "*" : "", PlayerModelName(c), teamnum);
	}
	gi.configstring(CS_PLAYERSKINS + slot, buf);
}

// the game-mode code rewrites the entry when it changes a team (G9, modes/g_modes.h)
extern "C" void G_SetPlayerInfo(edict_t *ent)
{
	if (ent && ent->client)
		PlayerInfoConfigstring(ent, false);
}

//=======================================================================
// spawn point entities (SPEC-780 9)

static int g_dmspawnnum;

void SP_info_player_start(edict_t *self) {}

void SP_info_player_deathmatch(edict_t *self)
{
	if (!deathmatch->value) {
		G_FreeEdict(self);
		return;
	}
	self->count = g_dmspawnnum++;
	self->solid = SOLID_NOT;
	self->svflags |= SVF_NOCLIENT;
}

void SP_info_player_intermission(edict_t *self) {}

//=======================================================================

void InitClientPersistant(gclient_t *client)
{
	char userinfo[MAX_INFO_STRING];
	char netname[16];
	memcpy(userinfo, client->pers.userinfo, sizeof(userinfo));
	memcpy(netname, client->pers.netname, sizeof(netname));
	qboolean connected = client->pers.connected;
	qboolean predicting = client->pers.predicting;
	char bestweap[8];
	memcpy(bestweap, client->pers.bestweap, sizeof(bestweap));
	memset(&client->pers, 0, sizeof(client->pers));
	memcpy(client->pers.userinfo, userinfo, sizeof(userinfo));
	memcpy(client->pers.netname, netname, sizeof(netname));
	memcpy(client->pers.bestweap, bestweap, sizeof(bestweap));
	client->pers.connected = connected;
	client->pers.predicting = predicting;
	client->pers.health = 100;
	client->pers.max_health = 100;
	client->pers.weapon = 0;
}

void InitClientResp(gclient_t *client)
{
	int team = client->resp.team;
	memset(&client->resp, 0, sizeof(client->resp));
	client->resp.team = team;
	client->resp.enterframe = level.framenum;
}

// SPEC-780 24: before a level change
void SaveClientData(void)
{
	for (int i = 0; i < game.maxclients; i++) {
		edict_t *ent = &g_edicts[1 + i];
		if (!ent->inuse)
			continue;
		game.clients[i].pers.health = ent->health;
		game.clients[i].pers.max_health = ent->max_health;
		game.clients[i].pers.savedFlags = ent->flags & FL_GODMODE;
		if (game.clients[i].inv)
			game.clients[i].pers.weapon = InvCurWeapon(game.clients[i].inv);
		game.clients[i].pers.fov = game.clients[i].ps.fov;
	}
}

void FetchClientEntData(edict_t *ent)
{
	ent->health = ent->client->pers.health;
	ent->max_health = ent->client->pers.max_health;
	ent->flags |= ent->client->pers.savedFlags;
}

//=======================================================================
// spawn point selection (SPEC-780 9)

static float PlayersRangeFromSpot(edict_t *spot)
{
	float best = 9999999;
	for (int n = 1; n <= maxclients->value; n++) {
		edict_t *player = &g_edicts[n];
		if (!player->inuse || player->health <= 0 || !player->client || player->client->resp.spectator)
			continue;
		vec3_t v;
		VectorSubtract(spot->s.origin, player->s.origin, v);
		float d = VectorLength(v);
		if (d < best)
			best = d;
	}
	return best;
}

static edict_t *SelectRandomPoint(const char *cls)
{
	int count = 0;
	edict_t *spot = NULL;
	while ((spot = G_Find(spot, FOFS(classname), cls)) != NULL)
		count++;
	if (!count)
		return NULL;
	int sel = rand() % count;
	spot = NULL;
	do
		spot = G_Find(spot, FOFS(classname), cls);
	while (sel--);
	return spot;
}

static edict_t *SelectFarthestDeathmatchSpawnPoint(void)
{
	edict_t *best = NULL, *spot = NULL;
	float bestdistance = -1;
	while ((spot = G_Find(spot, FOFS(classname), "info_player_deathmatch")) != NULL) {
		float d = PlayersRangeFromSpot(spot);
		if (d > bestdistance) {
			best = spot;
			bestdistance = d;
		}
	}
	return best;
}

static edict_t *SelectTeamSpawnPoint(edict_t *ent)
{
	gclient_t *c = ent->client;
	const char *cls = NULL;
	if (c->resp.team == 1 || !Q_stricmp(c->teamname, "blue"))
		cls = "info_player_team1";
	else if (c->resp.team == 2 || !Q_stricmp(c->teamname, "red"))
		cls = "info_player_team2";
	if (!cls)
		return SelectRandomPoint("info_player_deathmatch");
	edict_t *spots[64];
	int n = 0;
	edict_t *spot = NULL;
	while ((spot = G_Find(spot, FOFS(classname), cls)) != NULL && n < 64)
		spots[n++] = spot;
	if (!n)
		return SelectRandomPoint("info_player_deathmatch");
	if (n >= 3) {
		for (int k = 0; k < 2; k++) {
			int closest = 0;
			for (int i = 1; i < n; i++)
				if (PlayersRangeFromSpot(spots[i]) < PlayersRangeFromSpot(spots[closest]))
					closest = i;
			spots[closest] = spots[--n];
		}
	}
	return spots[rand() % n];
}

static void SelectSpawnPoint(edict_t *ent, vec3_t origin, vec3_t angles)
{
	edict_t *spot = NULL;
	if (deathmatch->value) {
		if (deathmatch->value == 7)
			spot = SelectTeamSpawnPoint(ent);
		else
			spot = SelectFarthestDeathmatchSpawnPoint();
		if (!spot)
			gi.error("Couldn't find deathmatch spawn point");
	} else {
		while ((spot = G_Find(spot, FOFS(classname), "info_player_start")) != NULL) {
			if (!game.spawnpoint[0] && !spot->targetname)
				break;
			if (!game.spawnpoint[0] || !spot->targetname)
				continue;
			if (Q_stricmp(game.spawnpoint, spot->targetname) == 0)
				break;
		}
		if (!spot) {
			if (!game.spawnpoint[0])
				spot = G_Find(spot, FOFS(classname), "info_player_start");
			if (!spot)
				gi.error("Couldn't find spawn point %s", game.spawnpoint);
		}
	}
	VectorCopy(spot->s.origin, origin);
	origin[2] += 9;
	VectorCopy(spot->s.angles, angles);
	Bot_SpawnSpot(ent, spot); /* G5: a bot starts from the spawn point's waypoint (SPEC-741 3) */
}

//======================================================================
// body queue (SPEC-780 10)

void InitBodyQue(void)
{
	level.body_que = 0;
	for (int i = 0; i < BODY_QUEUE_SIZE; i++) {
		edict_t *ent = G_Spawn();
		ent->classname = (char *)"bodyque";
	}
}

// thinks every 0.1 s and disappears once its death animation has finished (SPEC-780 10)
static void body_think(edict_t *self)
{
	G3Body *b = Body_Of(self);
	if (self->ghoulInst && (!b || !b->finished) && level.time < self->delay) {
		if (b)
			b->Think();
		self->nextthink = level.time + 0.1f;
		return;
	}
	GhoulRemove(self);
	Body_Destroy(self);
	self->ai = nullptr; // G2's manager reaps the corpse character (its body is gone)
	self->svflags |= SVF_NOCLIENT;
	self->solid = SOLID_NOT;
	gi.unlinkentity(self);
}

static void CopyToBodyQue(edict_t *ent)
{
	edict_t *body = &g_edicts[game.maxclients + level.body_que + 1];
	level.body_que = (level.body_que + 1) % BODY_QUEUE_SIZE;
	gi.unlinkentity(ent);
	gi.unlinkentity(body);
	body->s = ent->s;
	body->s.number = G_EdictNum(body);
	body->s.angles[PITCH] = 0;
	body->s.event[0] = body->s.event[1] = body->s.event[2] = 0;
	body->svflags = ent->svflags;
	VectorCopy(ent->mins, body->mins);
	VectorCopy(ent->maxs, body->maxs);
	VectorCopy(ent->absmin, body->absmin);
	VectorCopy(ent->absmax, body->absmax);
	VectorCopy(ent->size, body->size);
	body->solid = ent->solid;
	body->clipmask = ent->clipmask;
	body->owner = ent->owner;
	body->movetype = ent->movetype;
	body->flags = ent->flags;
	body->inuse = true;
	body->classname = (char *)"bodyque";
	// the corpse keeps the player's Ghoul model and gets a body of the dead player's family (SPEC-780 10,
	// SPEC-722 corpses); the player gets a new model at respawn
	GhoulRemove(body);
	Body_Destroy(body);
	GGInst *oi = GG_ObjInstOfEntity(ent);
	if (oi) {
		Body_CopyToCorpse(ent, body);
		GG_MoveInst(oi, body);
		body->ghoulInst = GG_EngineInst(oi);
		body->s.renderfx |= RF_GHOUL;
		ent->ghoulInst = NULL;
		ent->s.renderfx &= ~RF_GHOUL;
		Roster_SetupPlayerCorpse(body, ent); /* G4: the type-14 corpse character (SPEC-732 8), after the hand-over */
	}
	Body_Destroy(ent);
	body->think = body_think; // the corpse's lifetime stays G18's (SPEC-780 10: gone when the death animation ends)
	body->nextthink = level.time + 0.1f;
	body->delay = level.time + 10; // at the latest
	gi.linkentity(body);
}

//======================================================================
// obituaries and scoring (SPEC-780 18)

static void Obituary(edict_t *self, edict_t *inflictor, edict_t *attacker)
{
	GM_Obituary(self, inflictor, attacker); /* G9: SPEC-780 7 obituary, death-drop and scoring hooks */
	GM_DeathDrop(self);
	GM_Score(self, inflictor, attacker);
	return;
	if (!deathmatch->value)
		return;
	int m = meansOfDeath & ~MOD_FRIENDLY_FIRE;
	gclient_t *c = self->client;
	const char *msg = NULL;
	bool female = c->female != 0;
	if (attacker == self) {
		switch (m) {
		case MOD_PHOS_GRENADE: msg = "TEXT_OBT_MOD_PHOS_GRENADE"; break;
		case MOD_ROCKET_SPLASH: msg = female ? "TEXT_OBT_MOD_ROCKET_SPLASH_F" : "TEXT_OBT_MOD_ROCKET_SPLASH_M"; break;
		case MOD_C4: msg = female ? "TEXT_OBT_MOD_C4_F" : "TEXT_OBT_MOD_C4_M"; break;
		case MOD_CLAYMORE: msg = "TEXT_OBT_MOD_CLAYMORE"; break;
		case MOD_NEURAL_GRENADE: msg = female ? "TEXT_OBT_MOD_NEURAL_GRENADE_F" : "TEXT_OBT_MOD_NEURAL_GRENADE_M"; break;
		case MOD_GRENADE: msg = female ? "TEXT_OBT_MOD_GRENADE_F" : "TEXT_OBT_MOD_GRENADE_M"; break;
		}
		if (!msg) {
			switch (m) {
			case MOD_WATER: case MOD_SLIME: case MOD_CRUSH: case MOD_FALLING: case MOD_SUICIDE: case MOD_EXPLOSIVE:
			case MOD_FIRE: case MOD_TRIGGER_HURT: case MOD_LAVA: case MOD_EXIT: case MOD_BARBWIRE: case MOD_DOGBITE:
				break;
			default:
				msg = female ? "TEXT_OBT_F" : "TEXT_OBT_M";
			}
		}
	}
	if (!msg) {
		switch (m) {
		case MOD_WATER: msg = "TEXT_OBT_MOD_WATER"; break;
		case MOD_SLIME: msg = "TEXT_OBT_MOD_SLIME"; break;
		case MOD_CRUSH: msg = "TEXT_OBT_MOD_CRUSH"; break;
		case MOD_FALLING: msg = "TEXT_OBT_MOD_FALLING"; break;
		case MOD_SUICIDE: msg = "TEXT_OBT_MOD_SUICIDE"; break;
		case MOD_EXPLOSIVE: msg = "TEXT_OBT_MOD_EXPLOSIVE"; break;
		case MOD_FIRE: case MOD_TRIGGER_HURT: msg = "TEXT_OBT_MOD_FIRE"; break;
		case MOD_LAVA: msg = "TEXT_OBT_MOD_LAVA"; break;
		case MOD_EXIT: msg = "TEXT_OBT_MOD_EXIT"; break;
		case MOD_BARBWIRE: msg = "TEXT_OBT_MOD_BARBWIRE"; break;
		case MOD_DOGBITE: msg = "TEXT_OBT_MOD_DOGBITE"; break;
		}
	}
	bool byclient = false;
	if (!msg) {
		self->enemy = attacker;
		if (!attacker || !attacker->client)
			msg = "TEXT_OBT_DIED";
		else {
			byclient = true;
			static const char *names[38] = {NULL, "KNIFE_SLASH", "KNIFE_THROWN", "PISTOL1", "PISTOL2", "MPISTOL",
			                                "ASSAULTRIFLE", "SNIPERRIFLE", "AUTOSHOTGUN", "SHOTGUN", "MACHINEGUN",
			                                "PHOS_GRENADE_2", "ROCKET", "ROCKET_SPLASH", "MPG", "FLAMEGUN",
			                                "FLAMEGUN_NAPALM", "C4", "CLAYMORE_2", "NEURAL_GRENADE", NULL, NULL,
			                                NULL, "TELEFRAG", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
			                                NULL, NULL, NULL, "GRENADE_2", "CONC_GRENADE"};
			if (m >= 0 && m < 38 && names[m])
				msg = va("TEXT_OBT_MOD_%s", names[m]);
			else
				msg = "TEXT_OBT_DIED";
		}
	}
	int id = SP_Id("dm_generic", msg);
	if (id >= 0) {
		if (byclient)
			gix.SP_Print_Obit(NULL, id, G_EdictNum(self), G_EdictNum(attacker)); // %p = entity number
		else
			gix.SP_Print_Obit(NULL, id, G_EdictNum(self));
	}

	// scoring (deathmatch default rules)
	if (attacker == self)
		c->resp.score = (int)(c->resp.score - sv_suicidepenalty->value);
	else if (attacker && attacker->client)
		attacker->client->resp.score += (meansOfDeath & MOD_FRIENDLY_FIRE) ? -1 : 1;
	else
		c->resp.score--;
}

//======================================================================
// death (SPEC-780 7)

static void RemoveHelpers(gclient_t *c)
{
	if (c->helper_lean) { G_FreeEdict(c->helper_lean); c->helper_lean = NULL; }
	if (c->helper_crouch) { G_FreeEdict(c->helper_crouch); c->helper_crouch = NULL; }
	if (c->helper_gun) { G_FreeEdict(c->helper_gun); c->helper_gun = NULL; }
}

void player_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	gclient_t *c = self->client;
	VectorClear(self->avelocity);
	self->takedamage = DAMAGE_YES;
	self->movetype = MOVETYPE_TOSS;
	self->solid = SOLID_CORPSE;
	self->s.angles[PITCH] = 0;
	self->s.angles[ROLL] = 0;
	self->s.sound = 0;
	self->s.sounddata = 0;
	self->maxs[2] = -8;
	self->svflags |= SVF_DEADMONSTER;

	if (!self->deadflag) {
		c->respawn_time = level.time + (deathmatch->value ? 1.0f : 4.0f);
		edict_t *look = attacker && attacker != world && attacker != self ? attacker
		                : inflictor && inflictor != world && inflictor != self ? inflictor : NULL;
		if (look) {
			vec3_t d;
			VectorSubtract(look->s.origin, self->s.origin, d);
			c->killer_yaw = vectoyaw(d);
		} else
			c->killer_yaw = self->s.angles[YAW];
		c->ps.pmove.pm_type = PM_DEAD;
		if (deathmatch->value)
			PModel_Died(self); // weapon models off the body (SPEC-780 7)
		Obituary(self, inflictor, attacker);
		Bot_Killed(self, attacker, meansOfDeath); /* G5: bots are notified of the kill (SPEC-741 12) */
		// the death drop (player module slot 2) is the game mode's death-drop hook, GM_DeathDrop inside Obituary;
		// a second call here dropped twice (goggles switched off twice, G3 report)
		if (deathmatch->value && attacker && attacker->client) {
			if (attacker == self || OnSameTeam(attacker, self))
				gi.sound(attacker, CHAN_VOICE, gi.soundindex("dm/selffrag.wav"), 1, ATTN_STATIC, 0, 1);
			else if (deathmatch->value != 2)
				gi.sound(attacker, CHAN_VOICE, gi.soundindex("dm/frag.wav"), 1, ATTN_STATIC, 0, 1);
		}
		if (deathmatch->value && !c->pers.isbot)
			Cmd_Score_f(self); // human victims get the scoreboard: the `score` toggle, which sends it (SPEC-780 7 step 3)
	}
	if (deathmatch->value && c->inv) {
		c->inv->AddArmor(-100);
	}
	RemoveHelpers(c);
	c->goggles = 0;
	self->deadflag = DEAD_DEAD;
	gi.linkentity(self);
	self->health = -1;
}

//======================================================================
// respawn (SPEC-780 6)

void respawn(edict_t *self)
{
	if (deathmatch->value) {
		CopyToBodyQue(self);
		PutClientInServer(self);
		G_AddEvent(self, 15, 0);
		self->client->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
		self->client->ps.pmove.pm_time = 14;
		self->s.effects |= EF_RESPAWNED;
		self->client->respawn_time = level.time; // the attack stays armed (PutClientInServer; AMEND-005 G18.24)
		return;
	}
	gi.AddCommandString("respawn\n");
}

static void spectator_change(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (!c->pers.spectator) {
		const char *pw = password->string;
		if (*pw && strcmp(pw, "none") && strcmp(pw, Info_ValueForKey(c->pers.userinfo, "password"))) {
			SP_PrintTo(ent, "dm_generic", "TEXT_BAD_PWD");
			c->pers.spectator = true;
			gi.WriteByte(svc_stufftext);
			gi.WriteString("spectator 1\n");
			gi.unicast(ent, true);
			return;
		}
	} else {
		const char *pw = spectator_password->string;
		if (*pw && strcmp(pw, "none") && strcmp(pw, Info_ValueForKey(c->pers.userinfo, "spectator_password"))) {
			SP_PrintTo(ent, "dm_generic", "TEXT_SPECTATOR_BAD_PWD");
			c->pers.spectator = false;
			gi.WriteByte(svc_stufftext);
			gi.WriteString("spectator 0\n");
			gi.unicast(ent, true);
			return;
		}
		int numspec = 0;
		for (int i = 1; i <= maxclients->value; i++)
			if (g_edicts[i].inuse && g_edicts[i].client->pers.spectator && &g_edicts[i] != ent)
				numspec++;
		if (numspec > maxspectators->value) {
			SP_PrintTo(ent, "dm_generic", "TEXT_SPECTATOR_LIMIT_FULL");
			c->pers.spectator = false;
			gi.WriteByte(svc_stufftext);
			gi.WriteString("spectator 0\n");
			gi.unicast(ent, true);
			return;
		}
	}
	GM_DropFlag(ent); /* G9: SPEC-780 8 game-mode drop hook */
	c->resp.score = 0;
	c->resp.spectator = c->pers.spectator;
	PutClientInServer(ent);
	if (!c->pers.spectator) {
		G_AddEvent(ent, 15, 0);
		c->ps.pmove.pm_flags = PMF_TIME_TELEPORT;
		c->ps.pmove.pm_time = 14;
	}
	c->respawn_time = level.time;
	int id = SP_Id("dm_generic", c->pers.spectator ? "TEXT_SPECTATOR_TO_SIDELINES" : "TEXT_SPECTATOR_TO_GAME");
	if (id >= 0)
		gix.SP_Print(NULL, id, G_EdictNum(ent)); // "%p joined the game": %p = entity number (SPEC-201 34)
}

//==============================================================
// loadouts (SPEC-780 4, 21)

void SpawnLoadout(edict_t *ent)
{
	gclient_t *c = ent->client;
	PlayerInventory *inv = c->inv;
	if (!inv)
		return;
	int df = (int)dmflags->value;
	if (deathmatch->value) {
		GM_ClientSpawn(ent); /* G9: SPEC-790 B3-B10 spawn kits */
		return;
		if (deathmatch->value == 3)
			return; // Arsenal has its own loadout (game modes, G9)
		inv->RemoveAll(true);
		inv->SetVarA((df & 0x800) ? 0 : -1);
		if (!c->pers.isbot) {
			inv->SetVarC((df & 0x40) ? 0 : -1);
			inv->SetTag(c->pers.bestweap);
			inv->SetVarB((df & 0x4) ? 0 : 1);
			inv->SetVarI((1 << WP_KNIFE) | ((df & 0x10) ? (1 << WP_PISTOL1) : 0));
		} else
			inv->SetTag("unsafe");
		inv->GiveWeapon(WP_KNIFE, 0);
		inv->GiveWeapon(WP_PISTOL1, 0);
		inv->GiveWeapon(WP_HURTHAND, 0);
		inv->GiveWeapon(WP_THROWHAND, 0);
		if (!(df & 0x800)) {
			inv->AddAmmo(AM_KNIFE, 6);
			inv->AddAmmo(AM_9MM, 150);
		}
		inv->SwitchToWeapon(WP_PISTOL1);
		return;
	}
	// mission statistics: the level counters restart before any other loadout step (SPEC-AMEND-007 G18.41)
	Stats_LevelReset();
	// single player start (SPEC-780 21, SPEC-AMEND-006 G18.33). The world value of worldspawn `noweapons` (−1,
	// weapon mode 0) / `setweapons 1` (−2) decides the loadout. The begin packet of ger1/tsr1 carries three
	// prediction restarts, arm1's two (spsuite through G6b's proxy): the loadout's own reset sends one in the normal
	// and the −2 path, none in the −1 path; two come from outside the loadout — the inventory creation of
	// ClientBegin and one more that G18 sends here (where the original sends it is not known; same packet).
	PredictionRestart(ent);
	if (level.weaponmode == 0) {
		/* noweapons: no kit and no persistence load (arm1/arm3/tut1). Still replace the
		   inventory object: after gamemap the kept one holds view-weapon Ghoul instances
		   freed by GhoulResetLevel (MODEB-GAPS crash B on sud3->arm3 and similar). */
		PlayerDeleteInventory(inv);
		PlayerSpawning(ent);
		inv = c->inv = PlayerNewInventory();
		PlayerSpawning(NULL);
		PlayerSetIdleContext(ent);
		if (pe && !Q_strncasecmp(level.mapname, "tut1", 4)) {
			pe->PrecacheViewModels((1u << WP_SNIPER) | (1u << WP_PISTOL2));
			if (level.weaponsavailable)
				pe->PrecacheViewModels((unsigned)level.weaponsavailable);
		}
		return;
	}
	// the player-module reset is a new inventory object: on a `gamemap` the kept object still holds the view-weapon
	// instance of the previous level, whose Ghoul objects the level spawn released, and its first weapon switch
	// faulted (G10's report; tsr1 → tsr2 crashed at the kit's SwitchToBest). The oracle's tsr2 begin packet after
	// `gamemap tsr2` has two prediction restarts and one svc_ric (no inventory creation at ClientBegin, which keeps
	// the object), the same as OpenMerc with this reset (gm_orig / gm_om, OPEN_QUESTIONS G18 40)
	PlayerDeleteInventory(inv);
	PlayerSpawning(ent);
	inv = c->inv = PlayerNewInventory();
	PlayerSpawning(NULL);
	PlayerSetIdleContext(ent);
	if (!inv)
		return;
	// player variable A (ammunition used) −1, as in deathmatch without infinite ammunition: the oracle's single-player
	// begin packet carries one svc_ric with exactly that event (header 0x30, value −1; tsr1, ger2 — not arm1, whose
	// −1 path skips the loadout); not in SPEC-780 §21 (OPEN_QUESTIONS G18 37)
	inv->SetVarA(-1);
	if (level.weaponmode == 1) {
		// −2 (`setweapons 1`, e.g. tsr1): reset (one restart) and the fixed kit instead of the persistence load
		PredictionRestart(ent);
		inv->GiveWeapon(WP_KNIFE, 0);
		inv->GiveWeapon(WP_PISTOL1, 0);
		inv->GiveWeapon(WP_SHOTGUN, 0);
		inv->GiveWeapon(WP_HURTHAND, 0);
		inv->GiveWeapon(WP_THROWHAND, 0);
		if (pe)
			pe->PrecacheViewModels((1u << WP_KNIFE) | (1u << WP_PISTOL1) | (1u << WP_SHOTGUN) | (1u << WP_HURTHAND) |
			                       (1u << WP_THROWHAND));
		inv->AddAmmo(AM_KNIFE, 6);
		inv->AddAmmo(AM_9MM, 128);
		inv->AddAmmo(AM_SHELLS, 32);
		inv->AddItems(IT_C4, 2, -1);
		inv->AddItems(IT_FLASHPACK, 5, -1);
		inv->AddArmor(100);
		inv->SwitchToBest();
		return;
	}
	inv->GiveWeapon(WP_KNIFE, 0);
	inv->GiveWeapon(WP_HURTHAND, 0);
	inv->GiveWeapon(WP_THROWHAND, 0);
	unsigned mask = 0;
	int health = 100;
	inv->LoadPersist(&mask, &health);
	PredictionRestart(ent);
	if (pe && mask)
		pe->PrecacheViewModels(mask);
	if (c->pers.weapon && inv->OwnsWeapon(c->pers.weapon))
		inv->SwitchToWeapon(c->pers.weapon);
	else
		inv->SwitchToBest();
}

//==============================================================
// PutClientInServer (SPEC-780 4)

void PutClientInServer(edict_t *ent)
{
	vec3_t mins = {-16, -16, -24};
	vec3_t maxs = {16, 16, 40};
	vec3_t spawn_origin, spawn_angles;
	gclient_t *client = ent->client;
	int index = G_EdictNum(ent) - 1;

	SelectSpawnPoint(ent, spawn_origin, spawn_angles);

	client_respawn_t resp;
	if (deathmatch->value) {
		if (!client->pers.isbot) {
			char userinfo[sizeof(client->pers.userinfo)];
			memcpy(userinfo, client->pers.userinfo, sizeof(userinfo)); // the function copies into pers.userinfo
			InitClientPersistant(client);
			ClientUserinfoChanged(ent, userinfo, 2);
		} else {
			// bots copy their stored userinfo first (a copy: the replaced team/model stay starred), stay flagged as
			// bots and get the same userinfo processing (SPEC-780 4 step 2)
			char userinfo[sizeof(client->pers.userinfo)];
			const char *ui = Bot_IsBot(ent) ? Bot_Userinfo(ent) : NULL; /* G5 */
			Com_sprintf(userinfo, sizeof(userinfo), "%s", ui && *ui ? ui : client->pers.userinfo);
			InitClientPersistant(client);
			client->pers.isbot = true;
			ClientUserinfoChanged(ent, userinfo, 2);
		}
		resp = client->resp; // after the user-info call: a team the game mode assigns there is kept (G9)
	} else
		memset(&resp, 0, sizeof(resp));

	// clear the client record, keeping the listed parts
	PlayerInventory *inv = client->inv;
	client_persistant_t saved = client->pers;
	char modelname[MAX_QPATH], teamname[MAX_QPATH], skinname[MAX_QPATH];
	memcpy(modelname, client->modelname, sizeof(modelname));
	memcpy(teamname, client->teamname, sizeof(teamname));
	memcpy(skinname, client->skinname, sizeof(skinname));
	int resync = client->resync;
	int female = client->female;
	int ping = client->ping;
	memset(client, 0, sizeof(*client));
	client->pers = saved;
	client->inv = inv;
	memcpy(client->modelname, modelname, sizeof(modelname));
	memcpy(client->teamname, teamname, sizeof(teamname));
	memcpy(client->skinname, skinname, sizeof(skinname));
	client->resync = resync;
	client->female = female;
	client->ping = ping;
	if (client->pers.health <= 0)
		InitClientPersistant(client);
	client->resp = resp;

	FetchClientEntData(ent);

	ent->groundentity = NULL;
	ent->client = &game.clients[index];
	ent->takedamage = DAMAGE_AIM;
	ent->movetype = MOVETYPE_WALK;
	ent->viewheight = 35;
	ent->inuse = true;
	ent->classname = (char *)"player";
	ent->mass = 200;
	ent->solid = SOLID_BBOX;
	ent->deadflag = DEAD_NO;
	ent->air_finished = level.time + 12;
	ent->clipmask = MASK_PLAYERSOLID;
	ent->model = (char *)"players/male/tris.md2";
	ent->pain = NULL;
	ent->die = player_die;
	ent->waterlevel = 0;
	ent->watertype = 0;
	ent->flags &= ~FL_NO_KNOCKBACK;
	ent->svflags &= ~SVF_DEADMONSTER;
	ent->s.modelindex2 = -1;
	ent->gravity = 1.0f;
	ent->friction = 1.0f;
	ent->elasticity = 0.5f;

	VectorCopy(mins, ent->mins);
	VectorCopy(maxs, ent->maxs);
	VectorClear(ent->velocity);

	memset(&client->ps, 0, sizeof(client->ps));
	client->ps.fov = 95;
	client->ps.remote_id = -1;
	client->ps.pmove.origin[0] = (short)(spawn_origin[0] * 8);
	client->ps.pmove.origin[1] = (short)(spawn_origin[1] * 8);
	client->ps.pmove.origin[2] = (short)(spawn_origin[2] * 8);

	// game-mode hooks: deathmatch humans get a new inventory object and a prediction restart
	PlayerSpawning(ent);
	if (deathmatch->value && !client->pers.isbot) {
		PlayerDeleteInventory(client->inv);
		client->inv = PlayerNewInventory();
		PredictionRestart(ent);
	}
	if (!client->inv) {
		client->inv = PlayerNewInventory();
		PredictionRestart(ent);
	}
	if (!deathmatch->value)
		ent->health = 100;
	PlayerSetIdleContext(ent);
	SpawnLoadout(ent);
	PlayerSpawning(NULL);

	if (client->inv) {
		PlayerSetIdleContext(ent);
		client->inv->TopUpClips();
	}
	client->ps.musicvalue = 0;

	ent->s.effects = 0;
	ent->s.modelindex = 255;
	ent->s.skinnum = index;
	ent->s.frame = 0;
	VectorCopy(spawn_origin, ent->s.origin);
	ent->s.origin[2] += 1;

	for (int i = 0; i < 3; i++)
		client->ps.pmove.delta_angles[i] = (short)ANGLE2SHORT(spawn_angles[i] - client->resp.cmd_angles[i]);
	ent->s.angles[PITCH] = 0;
	ent->s.angles[YAW] = spawn_angles[YAW];
	ent->s.angles[ROLL] = 0;
	VectorCopy(ent->s.angles, client->ps.viewangles);
	VectorCopy(ent->s.angles, client->v_angle);

	if (deathmatch->value)
		client->resp.spectator = client->pers.spectator; // a client connecting as spectator starts as one (G5, Q2)
	if (deathmatch->value && !client->resp.spectator)
		PModel_Setup(ent); // the player's Ghoul model and body (SPEC-780 16)
	if (!client->resp.spectator) {
		ent->clipmask = MASK_PLAYERSOLID;
		ent->svflags &= ~SVF_NOCLIENT;
		KillBox(ent);
		gi.linkentity(ent);
		if (deathmatch->value) {
			client->spawnprotect_time = level.time + 4;
			GhoulSetTint(ent, 1, 1, 1, 0.5f);
			FX_ClearSlots(ent);
			G_AddEvent(ent, 22, 0);
			FX_Effect(gi.effectindex("environ/prespawn"), ent->s.origin, MULTICAST_PVS);
			gi.sound(ent, CHAN_BODY, gi.soundindex("dm/prespawn.wav"), 0.6f, ATTN_NORM, 0);
		}
	} else {
		client->chase_target = NULL;
		client->resp.spectator = true;
		ent->movetype = MOVETYPE_NOCLIP;
		ent->solid = SOLID_NOT;
		ent->clipmask = 0;
		ent->svflags |= SVF_NOCLIENT;
		gi.linkentity(ent);
	}

	client->attack_rearmed = true; // every spawn re-arms the attack (AMEND-005 G18.24 corrects SPEC-780 4 step 11)
	if (level.startmusic && !client->pers.isbot)
		ambient::MusicSet(ent, level.startmusic + 3); // piece n = value n + 3, with the change time (SPEC-771 B.5, G7)
	client->legslow = 0;
	client->respawn_time = level.time;
}

//==============================================================
// ClientBegin (SPEC-780 2)

static void ClientBeginDeathmatch(edict_t *ent)
{
	G_InitEdict(ent);
	InitClientResp(ent->client);
	ent->client->resp.enterframe = level.framenum;
	ent->client->resp.spectator = ent->client->pers.spectator; // a client connecting as spectator enters as one
	PlayerDeleteInventory(ent->client->inv);
	PlayerSpawning(ent); // the new inventory gets this client's owner context at once
	ent->client->inv = PlayerNewInventory();
	PlayerSpawning(NULL);
	PredictionRestart(ent);
	GM_ClientBegin(ent); /* G9: SPEC-780 2 step 3 game-mode begin hook */
	PutClientInServer(ent);
	PlayerInfoConfigstring(ent, false); // full entry when entering (oracle; the respawns write none)

	int id = SP_Id("dm_generic", "ENTERED");
	if (id >= 0)
		gix.SP_Print(NULL, id, ent->client->pers.netname); // "%s has entered the game!" (SPEC-780 2 step 5)
	if (!ent->client->pers.isbot)
		gix.welcomeprint(ent); // humans only (SPEC-780 2 step 5)
	ClientEndServerFrame(ent);
}

void ClientBegin(edict_t *ent)
{
	ent->client = game.clients + (G_EdictNum(ent) - 1);
	ent->client->pers.connected = true;

	if (deathmatch->value) {
		ClientBeginDeathmatch(ent);
		return;
	}

	if (ent->inuse) {
		for (int i = 0; i < 3; i++)
			ent->client->ps.pmove.delta_angles[i] = (short)ANGLE2SHORT(ent->client->ps.viewangles[i]);
		PredictionRestart(ent);
		if (ent->client->inv) {
			gix.ReliableWriteByteToClient(svc_rebuild_pred_inv, G_EdictNum(ent) - 1); /* G10: svc 30 id before the 640 bytes */
			ent->client->inv->WriteFullState(G_EdictNum(ent) - 1);
			if (pe)
				pe->RebuildAfterRestore(ent->client->inv);
		}
		if (ent->client->pers.fov)
			ent->client->ps.fov = ent->client->pers.fov;
	} else {
		G_InitEdict(ent);
		ent->classname = (char *)"player";
		InitClientResp(ent->client);
		if (!ent->client->inv) {
			PlayerSpawning(ent);
			ent->client->inv = PlayerNewInventory();
			PlayerSpawning(NULL);
			PredictionRestart(ent);
		} else
			ent->client->pers.weapon = InvCurWeapon(ent->client->inv);
		PutClientInServer(ent);
	}

	if (level.intermissiontime)
		MoveClientToIntermission(ent);
	else if (game.maxclients > 1)
		gix.welcomeprint(ent);

	ClientEndServerFrame(ent);
	/* G6b (SPEC-AMEND-005 G6b.1): at the very end of a single-player Begin the worldspawn script runner is used
	 * (queued for the next frame's script step); the deathmatch path returned above */
	ScriptGlue_ClientBegin(ent);
}

//==============================================================
// userinfo (SPEC-780 1)

void ClientUserinfoChanged(edict_t *ent, char *userinfo, qboolean ingame)
{
	gclient_t *c = ent->client;
	if (!Info_Validate(userinfo))
		strcpy(userinfo, "\\name\\badinfo\\skin\\mullins");

	char *s = Info_ValueForKey(userinfo, "name");
	char oldname[16];
	strcpy(oldname, c->pers.netname);
	strncpy(c->pers.netname, s, sizeof(c->pers.netname) - 1);
	c->pers.netname[sizeof(c->pers.netname) - 1] = 0;
	bool namechanged = strcmp(oldname, c->pers.netname) != 0;

	if (deathmatch->value) {
		s = Info_ValueForKey(userinfo, "spectator");
		c->pers.spectator = *s && strcmp(s, "0");
	} else
		c->pers.spectator = false;

	s = Info_ValueForKey(userinfo, "bestweap");
	strncpy(c->pers.bestweap, s, sizeof(c->pers.bestweap) - 1);
	s = Info_ValueForKey(userinfo, "predicting");
	c->pers.predicting = strcmp(s, "0") != 0;

	if (namechanged)
		PlayerInfoConfigstring(ent, true);

	// SPEC-780 1: before the client is in a deathmatch game the processing stops here. ClientConnect processes
	// before it marks the client connected; the engine's call right after ClientConnect (SPEC-001) and the spawn
	// variant go on. Oracle: a connecting client's entry is written in the short form, then in the full form with
	// the replaced team/model starred (captures ui_orig..6, OPEN_QUESTIONS G18 29); a bot is added the same way.
	if (deathmatch->value && !c->pers.connected) {
		strncpy(c->pers.userinfo, userinfo, sizeof(c->pers.userinfo) - 1);
		return;
	}

	// player model (SPEC-780 16): a changed model is set up at once for a player in the game.
	// Deathmatch: an unusable `skin` becomes the default model and a `teamname` that is not the model's team
	// (case-insensitive) becomes that team; the userinfo keys get the plain values and the configstring a `*` before
	// each replaced one. Bots process a copy of their stored userinfo, so they keep the `*` (oracle: `Kirt\*slick\
	// slick\-1` from `name\Kirt\skin\slick`). Measured, captures ui_orig..6 (OPEN_QUESTIONS G18 29).
	char oldmodel[MAX_QPATH];
	memcpy(oldmodel, c->modelname, sizeof(oldmodel));
	// SPEC-AMEND-006 G18.29 step 3: the set-up is "unchanged" when the player already has a body model and `skin`,
	// `teamname` (ignoring case) and the name (exactly) equal those of the previous set-up; then neither the userinfo
	// nor the configstring is rewritten (a client repeating an unusable value keeps it in its userinfo, and the
	// earlier starred entry stays). Every set-up remembers the values it was called with, before any rewrite.
	bool unchanged = false;
	if (deathmatch->value) {
		const char *rs = Info_ValueForKey(userinfo, "skin");
		const char *rt = Info_ValueForKey(userinfo, "teamname");
		unchanged = ent->inuse && ent->ghoulInst && c->pm_set_valid && !Q_stricmp(rs, c->pm_set_skin) &&
		            !Q_stricmp(rt, c->pm_set_team) && !strcmp(c->pers.netname, c->pm_set_name);
		Com_sprintf(c->pm_set_skin, sizeof(c->pm_set_skin), "%s", rs);
		Com_sprintf(c->pm_set_team, sizeof(c->pm_set_team), "%s", rt);
		Com_sprintf(c->pm_set_name, sizeof(c->pm_set_name), "%s", c->pers.netname);
		c->pm_set_valid = true;
	}
	if (unchanged) {
		// model, team and their star marks stay as set up before
	} else if (deathmatch->value) {
		char skin[MAX_QPATH], tn[MAX_QPATH], model[MAX_QPATH], team[MAX_QPATH];
		Com_sprintf(skin, sizeof(skin), "%s", Info_ValueForKey(userinfo, "skin"));
		Com_sprintf(tn, sizeof(tn), "%s", Info_ValueForKey(userinfo, "teamname"));
		c->skin_star = !PModel_Resolve(skin, model, sizeof(model), team, sizeof(team));
		if (c->skin_star)
			Info_SetValueForKey(userinfo, "skin", model);
		c->team_star = Q_stricmp(tn, team) != 0;
		if (c->team_star)
			Info_SetValueForKey(userinfo, "teamname", team);
		Com_sprintf(c->modelname, sizeof(c->modelname), "%s", model);
		Com_sprintf(c->teamname, sizeof(c->teamname), "%s", c->team_star ? team : tn);
	} else {
		strcpy(c->modelname, "mullins");
		strcpy(c->teamname, "mullins");
		c->skin_star = c->team_star = false;
	}
	if (deathmatch->value && ent->inuse && ent->ghoulInst && ent->health > 0 && Q_stricmp(oldmodel, c->modelname))
		PModel_Setup(ent);
	GM_UserinfoChanged(ent, userinfo, ingame == 2); /* G9: SPEC-790 4 team number assignment */
	// the spawn variant writes nothing: the oracle writes the entry when a client enters the game (ClientBegin) but
	// not on a respawn (captures orig_walk, respawn_orig; G9 orig_teamdm*)
	if (ingame != 2 && !unchanged)
		PlayerInfoConfigstring(ent, false);

	strncpy(c->pers.userinfo, userinfo, sizeof(c->pers.userinfo) - 1);
	if (c->inv)
		c->inv->SetTag(c->pers.bestweap);
}

//==============================================================
// connect / disconnect (SPEC-780 1, 3)

qboolean ClientPreConnect(edict_t *ent, char *userinfo)
{
	Bot_SlotTaken(ent); /* G5: a bot holding the slot is removed first (SPEC-780 1, SPEC-741 2.6) */
	return true;
}

qboolean ClientConnect(edict_t *ent, char *userinfo)
{
	char *value = Info_ValueForKey(userinfo, "ip");
	if (gix.addressbanned(value)) {
		Info_SetValueForKey(userinfo, "rejmsg", "rejected_banned");
		return false;
	}
	value = Info_ValueForKey(userinfo, "spectator");
	if (deathmatch->value && *value && strcmp(value, "0")) {
		const char *pw = spectator_password->string;
		if (*pw && strcmp(pw, "none") && strcmp(pw, Info_ValueForKey(userinfo, "spectator_password"))) {
			Info_SetValueForKey(userinfo, "rejmsg", "rejected_spectator_password");
			return false;
		}
		int numspec = 0;
		for (int i = 0; i < maxclients->value; i++)
			if (g_edicts[i + 1].inuse && g_edicts[i + 1].client->pers.spectator && &g_edicts[i + 1] != ent)
				numspec++;
		if (numspec > maxspectators->value) {
			Info_SetValueForKey(userinfo, "rejmsg", "rejected_spectator_limit_exeeded");
			return false;
		}
	} else {
		const char *pw = password->string;
		if (*pw && strcmp(pw, "none") && strcmp(pw, Info_ValueForKey(userinfo, "password"))) {
			Info_SetValueForKey(userinfo, "rejmsg", "rejected_password");
			return false;
		}
	}

	ent->client = game.clients + (G_EdictNum(ent) - 1);
	if (ent->inuse == false) {
		InitClientResp(ent->client);
		InitClientPersistant(ent->client);
		ent->client->pers.connected = false; // a client that left before entering the game kept the flag
		ent->client->pm_set_valid = false;
	}
	ClientUserinfoChanged(ent, userinfo, 0);

	if (game.maxclients > 1)
		gi.dprintf("%s connected\n", ent->client->pers.netname);

	ent->svflags = 0;
	ent->client->pers.connected = true;
	return true;
}

// the client leaves the game: body model, hit volumes, game-mode leave hooks (SPEC-780 3); ClientLeaveQuiet is the
// same without the `disconnected` broadcast and the bot-slot release (bot removal, G5)
static void ClientLeave(edict_t *ent)
{
	GM_ClientDisconnect(ent); /* G9: SPEC-780 3 first game-mode hook */
	RemoveHelpers(ent->client);
	GhoulRemove(ent);
	Body_Destroy(ent);
	gi.unlinkentity(ent);
	ent->s.modelindex = 0;
	ent->solid = SOLID_NOT;
	ent->inuse = false;
	ent->classname = (char *)"disconnected";
	ent->client->pers.connected = false;
	gi.configstring(CS_PLAYERSKINS + (G_EdictNum(ent) - 1), "");
	GM_ClientDisconnected(ent); /* G9: SPEC-780 3 game-mode disconnect hook */
}

void ClientLeaveQuiet(edict_t *ent)
{
	if (ent && ent->client)
		ClientLeave(ent);
}

void ClientDisconnect(edict_t *ent)
{
	if (!ent->client)
		return;
	Bot_SlotFreed(ent); /* G5: the client number is available to bots again (SPEC-741 2.6) */
	gi.bprintf(PRINT_HIGH, "%s disconnected\n", ent->client->pers.netname);
	ClientLeave(ent);
}

void ClientDropped(edict_t *ent)
{
	if (ent->client)
		ent->client->resp.team = 0;
}

//==============================================================
// ClientThink (SPEC-780 5)

static edict_t *pm_passent;

static trace_t PM_trace(const float *start, const float *mins, const float *maxs, const float *end)
{
	if (pm_passent->health > 0)
		return gi.trace(start, mins, maxs, end, pm_passent, MASK_PLAYERSOLID);
	return gi.trace(start, mins, maxs, end, pm_passent, 0x00010003);
}

static int PM_pointcontents(const float *p) { return gix.pointcontents(p); }

static int PM_isclient(edict_t *e) { return e && G_IsClient(e); }

static float MoveScale(edict_t *ent)
{
	return GM_MoveScale(ent); /* G9: SPEC-790 B3/B5/B11 */
	if (!deathmatch->value)
		return 1.0f;
	return g_movescale->value * (1.0f - ent->client->legslow);
}

static int SelectPmType(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (level.intermissiontime)
		return c->resp.spectator ? PM_INTERMISSION_SPEC : PM_FREEZE;
	if (ent->movetype == MOVETYPE_NOCLIP && !c->resp.spectator)
		return PM_NOCLIP;
	if (c->resp.spectator)
		return PM_SPECTATOR;
	if (ent->deadflag)
		return PM_DEAD;
	if (c->ps.remote_type > 1)
		return PM_REMOTE;
	return PM_NORMAL;
}

static void TargetIdentification(edict_t *ent)
{
	gclient_t *c = ent->client;
	vec3_t start, fwd, right, end;
	AngleVectors(c->v_angle, fwd, right, NULL);
	VectorCopy(ent->s.origin, start);
	start[2] += 26;
	VectorMA(start, 2.5f * c->lean, right, start);
	VectorMA(start, 2000, fwd, end);
	trace_t tr = gi.trace(start, NULL, NULL, end, ent, 0x06000081);
	c->bot_gunsmoke = 0;
	edict_t *t = tr.ent;
	if (t && (t->svflags & SVF_PLAYERPROXY) && t->owner)
		t = t->owner;
	if (!t || t == world || t->health <= 0 || !t->takedamage)
		return;
	if (t->client) {
		c->bot_gunsmoke = OnSameTeam(t, ent) ? 0x30 : 0x10;
	} else if (t->svflags & SVF_MONSTER)
		c->bot_gunsmoke = 0x10;
}

void ClientThink(edict_t *ent, usercmd_t *ucmd)
{
	gclient_t *client = ent->client;
	level.current_entity = ent;

	if (level.intermissiontime) {
		client->ps.pmove.pm_type = client->resp.spectator ? PM_INTERMISSION_SPEC : PM_FREEZE;
		if (level.time > level.intermissiontime + 10.0 && (ucmd->buttons & BUTTON_ATTACK))
			level.exitintermission = true;
		return;
	}

	if (client->forcesuicide) {
		client->forcesuicide = 0;
		ent->health = 0;
		meansOfDeath = MOD_SUICIDE;
		player_die(ent, ent, ent, 100000, vec3_origin);
		client->resp.score = 0;
		if (deathmatch->value)
			respawn(ent);
		else
			gi.AddCommandString("respawn\n");
		return;
	}

	pm_passent = ent;

	if (client->chase_target) {
		for (int i = 0; i < 3; i++)
			client->resp.cmd_angles[i] = SHORT2ANGLE(ucmd->angles[i]);
	} else {
		int nmoves = 1, msec = ucmd->msec;
		if (ucmd->msec > 33 && ent->velocity[2] != 0) {
			nmoves = ucmd->msec / 10;
			msec = (ucmd->msec % 10) + 10;
		}
		for (int mv = 0; mv < nmoves; mv++) {
			pmove_t pm;
			memset(&pm, 0, sizeof(pm));
			client->ps.pmove.pm_type = SelectPmType(ent);
			client->ps.pmove.gravity = (short)sv_gravity->value;
			pm.s = client->ps.pmove;
			for (int i = 0; i < 3; i++) {
				pm.s.origin[i] = (short)(ent->s.origin[i] * 8);
				pm.s.velocity[i] = (short)(ent->velocity[i] * 8);
			}
			if (memcmp(&client->old_pmove, &pm.s, sizeof(pm.s)))
				pm.snapinitial = true;

			pm.cmd = *ucmd;
			// SPEC-314 §19: the first split move lasts (msec mod 10) + 10 ms, every further one 10 ms, so the moves
			// add up to the command's msec (SPEC-780 §4 had every move at (msec mod 10) + 10)
			pm.cmd.msec = (uint8_t)(mv == 0 ? msec : 10);
			if ((ucmd->buttons & BUTTON_RUN) && !(pm.s.pm_flags & PMF_EXHAUSTED)) {
				pm.cmd.forwardmove *= 2;
				pm.cmd.sidemove *= 2;
				pm.cmd.upmove *= 2;
			}
			// SPEC-314 §19: move := trunc(move × (q × (1/255))) in single precision, q = trunc(scale × 255) AND 255
			int q = (int)(MoveScale(ent) * 255) & 255;
			float qs = (float)q * (1.0f / 255.0f);
			pm.cmd.forwardmove = (short)(int)((float)pm.cmd.forwardmove * qs);
			pm.cmd.sidemove = (short)(int)((float)pm.cmd.sidemove * qs);
			pm.s.movescale = (uint8_t)q;

			pm.trace = PM_trace;
			pm.pointcontents = PM_pointcontents;
			pm.isclient = PM_isclient;
			if (client->stagger_time > level.time)
				pm.staggertime = client->stagger_time - level.time;

			int pre_pm_flags = client->ps.pmove.pm_flags;
			gi.Pmove(&pm);

			client->ps.pmove = pm.s;
			client->old_pmove = pm.s;
			for (int i = 0; i < 3; i++) {
				ent->s.origin[i] = pm.s.origin[i] * 0.125f;
				ent->velocity[i] = pm.s.velocity[i] * 0.125f;
			}
			VectorCopy(pm.mins, ent->mins);
			VectorCopy(pm.maxs, ent->maxs);
			for (int i = 0; i < 3; i++)
				client->resp.cmd_angles[i] = SHORT2ANGLE(ucmd->angles[i]);

			/* Jump sound: SPEC-780 ground->air + upmove, gated on JUMP_HELD rising
			   edge (CheckJump impulse this move). Ungated, stair/lip grazes while
			   holding jump spam jump.wav every split (userbugs stairs_hold). */
			if (ent->groundentity && !pm.groundentity && (pm.cmd.upmove >= 10) && (pm.waterlevel == 0) &&
				(pm.s.pm_flags & PMF_JUMP_HELD) && !(pre_pm_flags & PMF_JUMP_HELD))
				gi.sound(ent, CHAN_VOICE, gi.soundindex("player/jump.wav"), 0.6f, ATTN_NORM, 0);

			client->lean = pm.cmd.lean;
			ent->viewheight = (int)pm.viewheight;
			client->viewheight = pm.viewheight;
			ent->waterlevel = pm.waterlevel;
			ent->watertype = pm.watertype;
			ent->groundentity = pm.groundentity;
			if (pm.groundentity)
				ent->groundentity_linkcount = pm.groundentity->linkcount;

			if (ent->deadflag) {
				client->ps.viewangles[ROLL] = 40;
				client->ps.viewangles[PITCH] = -15;
				client->ps.viewangles[YAW] = client->killer_yaw;
			} else {
				VectorCopy(pm.viewangles, client->v_angle);
				VectorCopy(pm.viewangles, client->ps.viewangles);
			}

			// a watcher of a flag-2 camera (solid type 0) is not relinked: the oracle keeps sending the packed box
			// of its last link (SPEC-AMEND-006 G18.35; which step of the original skips the link is UNVERIFIED)
			if (!(ent->solid == SOLID_NOT && client->camera_nest > 0))
				gi.linkentity(ent);

			if (ent->movetype != MOVETYPE_NOCLIP && !client->resp.spectator)
				G_TouchTriggers(ent);

			for (int i = 0; i < pm.numtouch; i++) {
				edict_t *other = pm.touchents[i];
				int j;
				for (j = 0; j < i; j++)
					if (pm.touchents[j] == other)
						break;
				if (j != i)
					continue;
				if (!other->touch)
					continue;
				other->touch(other, ent, NULL, NULL);
			}
		}
	}

	client->oldbuttons = client->buttons;
	client->buttons = ucmd->buttons;
	client->latched_buttons |= client->buttons & ~client->oldbuttons;

	// a fresh attack press re-arms the attack and fires in the same frame (AMEND-005 G18.24)
	if ((client->buttons & BUTTON_ATTACK) && !(client->oldbuttons & BUTTON_ATTACK))
		client->attack_rearmed = true;
	if (ucmd->firetime)
		client->firetime = ucmd->firetime;
	if (ucmd->altfiretime)
		client->altfiretime = ucmd->altfiretime;
	if (!client->attack_rearmed)
		client->firetime = 0;

	ent->light_level = ucmd->lightlevel;

	if (!client->resp.spectator && ent->health > 0) {
		TargetIdentification(ent);
		if ((client->buttons & BUTTON_USE) && !(client->oldbuttons & BUTTON_USE))
			G_UseKey(ent);
	}

	if (client->resp.spectator) {
		if ((client->latched_buttons & BUTTON_ATTACK)) {
			client->latched_buttons = 0;
			if (client->chase_target) {
				client->chase_target = NULL;
				client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;
			} else
				GetChaseTarget(ent);
		}
		if ((client->buttons & BUTTON_USE) && !(client->oldbuttons & BUTTON_USE)) {
			if (client->chase_target)
				ChaseNext(ent);
			else
				GetChaseTarget(ent);
		}
	}

	for (int i = 1; i <= maxclients->value; i++) {
		edict_t *other = g_edicts + i;
		if (other->inuse && other->client->chase_target == ent)
			UpdateChaseCam(other);
	}
}

//==============================================================
// ClientBeginServerFrame (SPEC-780 6)

void ClientBeginServerFrame(edict_t *ent)
{
	gclient_t *client = ent->client;
	if (level.intermissiontime)
		return;
	ambient::MusicAuto(ent); // the music logic runs first, bots too (SPEC-780 6, SPEC-771 B.5)

	if (deathmatch->value && client->pers.spectator != client->resp.spectator &&
	    (level.time - client->respawn_time) >= 5) {
		spectator_change(ent);
		return;
	}

	if (ent->health > 0 && !client->resp.spectator) {
		int b = client->buttons | client->latched_buttons;
		int attack = (b & BUTTON_ATTACK) && client->attack_rearmed;
		PlayerThinkInventory(ent, attack);
		if (deathmatch->value && (b & (BUTTON_ATTACK | BUTTON_ALTATTACK)) && client->spawnprotect_time > level.time) {
			client->spawnprotect_time = 0;
			GhoulSetTint(ent, 1, 1, 1, 1);
		}
	}

	if (!deathmatch->value && ent->health > 0) {
		edict_t *last = PlayerTrail_LastSpot();
		if (!last || !gi.inPVS(ent->s.origin, last->s.origin))
			PlayerTrail_Add(ent->s.origin);
	}

	if (ent->deadflag) {
		if (level.time > client->respawn_time) {
			int buttonMask = deathmatch->value ? BUTTON_ATTACK : -1;
			if ((client->latched_buttons & buttonMask) || (deathmatch->value && ((int)dmflags->value & 0x200))) {
				respawn(ent);
				client->latched_buttons = 0;
				// the only place the attack is disarmed: a dead human's deathmatch respawn; an attack held through
				// it is not a fresh press (AMEND-005 G18.24)
				if (deathmatch->value && !client->pers.isbot) {
					client->attack_rearmed = false;
					client->buttons |= BUTTON_ATTACK;
					client->oldbuttons |= BUTTON_ATTACK;
				}
			}
		}
		return;
	}

	if (client->spawnprotect_time && level.time >= client->spawnprotect_time) {
		client->spawnprotect_time = 0;
		GhoulSetTint(ent, 1, 1, 1, 1);
	}

	client->latched_buttons = 0;
	ItemsFrame();
}
