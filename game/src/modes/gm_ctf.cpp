// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - game modes (G9): capture the flag (deathmatch 4), Control (6) and Conquer (7).
// Implements: SPEC-790 Behaviour 1 (CTF/Control/Conquer setup), 2 (Control accrual, CTF end, level exit),
//             8 (bases, capture, kill bonuses), 9 (Control), 10 (Conquer zones, scoring, configstrings)
#include "gm_internal.h"


//===========================================================================================================
// CTF

int GM_CTFCaptures(int team) { return (team >= 1 && team <= 2) ? gm.captures[team] : 0; }

void GM_CTFLevelSetup(void)
{
	// the carried-flag model and its two sequences are registered for precaching (SPEC-790 B1.3)
	GG_FindModel("items/ctf_flag", "flag_hold_idle", true);
	GG_FindModel("items/ctf_flag", "flag_run", true);
	static const char *const snd[] = {"dm/ctf/flaggrab.wav", "dm/ctf/flagret.wav", "dm/ctf/flagbase.wav",
	                                  "dm/ctf/triumphant.wav", "player/flaglp.wav"};
	for (const char *s : snd)
		gi.soundindex(s);
	ctf_flag_respawn_time = gi.cvar("ctf_flag_respawn_time", "90", CVAR_ARCHIVE);
	gm.captures[0] = gm.captures[1] = gm.captures[2] = 0;
}

void GM_CTFLevelExit(void)
{
	gm.captures[0] = gm.captures[1] = gm.captures[2] = 0;
	gix.configstring(CS_CTF_BLUE, "d");
	gix.configstring(CS_CTF_RED, "d");
	gix.cvar_set("ctf_loops_count", "0");
	gix.cvar_set("ctf_flag_captured", "0");
}

static void gm_ctfbase_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	(void)surf;
	if (gm.mode != GM_CTF || !GM_IsClient(other))
		return;
	gm_client_t *c = GM_Cl(other);
	int baseteam = self->count == 1 ? 1 : 2;
	if (!c->flagteam || c->flagteam == baseteam || gix.cvar_value("ctf_flag_captured") != 0)
		return;
	if (GM_CTFBaseFlag(baseteam) && !GM_CTFFlagAtBase(baseteam))
		return;
	int carried = c->flagteam;
	gix.sound(other, CHAN_AUTO, gix.soundindex("dm/ctf/flagbase.wav"), 1, ATTN_NONE, 0, 0);
	int team = other->client->resp.team;
	if (team >= 1 && team <= 2)
		gm.captures[team]++;
	GM_Print(NULL, SP_DM_CTF, carried == 1 ? CTF_END_GAME_BONUS_BLUE : CTF_END_GAME_BONUS_RED, GM_EntNum(other), 15);
	GM_AddScore(other, 15);
	int loops = (int)gix.cvar_value("ctf_loops");
	if (loops != 0 && (gm.captures[1] >= loops || gm.captures[2] >= loops)) {
		gix.cvar_set("ctf_flag_captured", "1");
		if (gix.forceconnotify)
			gix.forceconnotify();
		return;
	}
	char v[16];
	Com_sprintf(v, sizeof(v), "%d", (int)gix.cvar_value("ctf_loops_count") + 1);
	gix.cvar_set("ctf_loops_count", v);
	GM_CTFReturnFlag(1);
	GM_CTFReturnFlag(2);
}

void SP_misc_ctf_base(edict_t *ent)
{
	if (!deathmatch || (int)deathmatch->value != GM_CTF) {
		G_FreeEdict(ent);
		return;
	}
	ent->solid = SOLID_TRIGGER;
	ent->movetype = MOVETYPE_NONE;
	ent->svflags |= SVF_NOCLIENT;
	VectorSet(ent->mins, -32, -32, -24);
	VectorSet(ent->maxs, 32, 32, -16);
	ent->touch = gm_ctfbase_touch;
	gi.linkentity(ent);
}

// the team of the flag an entity carries / is: clients from the mode, flag entities from the item code
static int gm_flagteam_of(edict_t *e)
{
	if (GM_IsClient(e))
		return GM_Cl(e)->flagteam;
	return GM_CTFFlagTeam(e);
}

void GM_CTFScore(edict_t *self, edict_t *attacker)
{
	gm_client_t *v = GM_Cl(self);
	int carried = v->flagteam;
	if (carried)
		GM_CTFDropCarriedFlag(self);
	if (attacker == self) {
		float pen = sv_suicidepenalty ? sv_suicidepenalty->value : 2.0f;
		GM_SetScore(self, (int)((float)GM_GetScore(self) - pen));
		return;
	}
	if (!GM_IsClient(attacker)) {
		GM_AddScore(self, -1);
		return;
	}
	if (meansOfDeath & MOD_FRIENDLY_FIRE) {
		GM_AddScore(attacker, carried ? -6 : -1);
		return;
	}
	GM_AddScore(attacker, 1);
	int ateam = attacker->client->resp.team;
	if (level.time - v->hurtcarrier < 7.0f) {
		GM_Print(NULL, SP_DM_CTF, self->client->resp.team == 2 ? CTF_FRAG_HURT_CARRIER_BLUE : CTF_FRAG_HURT_CARRIER_RED,
		      GM_EntNum(attacker), GM_EntNum(self), 2);
		GM_AddScore(attacker, 2);
	}
	if (carried && carried == ateam) {
		GM_Print(NULL, SP_DM_CTF, carried == 2 ? CTF_FRAG_CARRIER_BLUE : CTF_FRAG_CARRIER_RED, GM_EntNum(attacker), 3);
		GM_AddScore(attacker, 3);
		return;
	}
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e == self)
			continue;
		if (gm_flagteam_of(e) != ateam || !ateam)
			continue;
		vec3_t d;
		VectorSubtract(self->s.origin, e->s.origin, d);
		if (VectorLength(d) <= 400) {
			GM_Print(NULL, SP_DM_CTF, ateam == 1 ? CTF_PROTECT_FLAG_BLUE : CTF_PROTECT_FLAG_RED, GM_EntNum(attacker), 1);
			GM_AddScore(attacker, 1);
		}
		break;
	}
}

//===========================================================================================================
// Control

void GM_ControlLevelSetup(void)
{
	GG_FindModel("items/ctf_flag", "flag_hold_idle", true);
	gi.soundindex("dm/ctf/flaggrab.wav");
	gi.soundindex("dm/ctf/triumphant.wav");
	gix.configstring(CS_CTF_BLUE, "0");
	gix.configstring(CS_CTF_RED, "0");
	gm.ctrl_score[0] = gm.ctrl_score[1] = gm.ctrl_score[2] = 0;
	gm.ctrl_next = level.time;
}

void GM_ControlRegisterPoint(edict_t *flag)
{
	(void)flag;
	gm.ctrl_points++;
}

void GM_ControlSwitch(edict_t *flag, int oldteam, int newteam)
{
	(void)flag;
	if (oldteam >= 1 && oldteam <= 2 && gm.ctrl_owned[oldteam] > 0)
		gm.ctrl_owned[oldteam]--;
	if (newteam >= 1 && newteam <= 2)
		gm.ctrl_owned[newteam]++;
}

int GM_ControlScore(int team) { return (team >= 1 && team <= 2) ? gm.ctrl_score[team] : 0; }

bool GM_ControlEndRule(void)
{
	if (level.time > gm.ctrl_next) {
		gm.ctrl_next += 3.0f;
		int bonus = (int)gix.cvar_value("control_bonus");
		for (int t = 1; t <= 2; t++) {
			gm.ctrl_score[t] += gm.ctrl_owned[t];
			if (gm.ctrl_points >= 1 && gm.ctrl_owned[t] >= gm.ctrl_points)
				gm.ctrl_score[t] += bonus;
		}
		char v[16];
		Com_sprintf(v, sizeof(v), "%d", gm.ctrl_score[1]);
		gix.configstring(CS_CTF_BLUE, v);
		Com_sprintf(v, sizeof(v), "%d", gm.ctrl_score[2]);
		gix.configstring(CS_CTF_RED, v);
	}
	int limit = (int)gix.cvar_value("control_limit");
	if (limit != 0 && (gm.ctrl_score[1] >= limit || gm.ctrl_score[2] >= limit)) {
		GM_Print(NULL, SP_DM_CONTROL, CTL_END_GAME);
		return true;
	}
	return false;
}

void GM_ControlLevelExit(void)
{
	gm.ctrl_owned[0] = gm.ctrl_owned[1] = gm.ctrl_owned[2] = 0;
	gm.ctrl_score[0] = gm.ctrl_score[1] = gm.ctrl_score[2] = 0;
	gm.ctrl_points = 0;
}

//===========================================================================================================
// Conquer

static bool gm_zonetouched[GM_MAXZONES];

void GM_ConquerLevelInit(void)
{
	gm.nzones = 0;
	memset(gm_zonetouched, 0, sizeof(gm_zonetouched));
}

static int gm_zoneindex(edict_t *ent)
{
	for (int i = 0; i < gm.nzones; i++)
		if (gm.zones[i].ent == ent)
			return i;
	return -1;
}

static void gm_zone_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	(void)plane;
	(void)surf;
	if (gm.mode != GM_CONQUER || !GM_InGame(other) || !GM_TouchAllowed())
		return;
	gm_client_t *c = GM_Cl(other);
	if (!c->inzone_prev && !c->inzone)
		c->zoneentered = level.time;
	c->inzone = true;
	int z = gm_zoneindex(self);
	if (z >= 0)
		gm_zonetouched[z] = true;
}

// called by G18's SP_trigger_conquer after InitTrigger (brush model set, trigger solid, linked)
void GM_ConquerZone(edict_t *ent)
{
	// Only Conquer registers its zones: on dm/gerctb1 the original's baseline of the trigger keeps origin (0, 0, 0)
	// in deathmatch 1 (G18 capture) and is at the box centre (4, -256, 252) in deathmatch 7 (orig_conquer_numbered).
	if (!ent || gm.mode != GM_CONQUER)
		return;
	// the origin moves to the centre of the box (the zone's loop-sound position); the box stays in place
	vec3_t c;
	for (int k = 0; k < 3; k++)
		c[k] = 0.5f * (ent->mins[k] + ent->maxs[k]);
	VectorAdd(ent->s.origin, c, ent->s.origin);
	VectorSubtract(ent->mins, c, ent->mins);
	VectorSubtract(ent->maxs, c, ent->maxs);
	ent->touch = gm_zone_touch;
	gi.linkentity(ent);
	if (gm.nzones < GM_MAXZONES) {
		gm.zones[gm.nzones].ent = ent;
		gm.zones[gm.nzones].occupied = false;
		gm.zones[gm.nzones].lastsecond = -1;
		gm.nzones++;
	}
}

bool GM_InConquerZone(const vec3_t p)
{
	for (int i = 0; i < gm.nzones; i++) {
		edict_t *z = gm.zones[i].ent;
		if (!z || !z->inuse)
			continue;
		if (p[0] >= z->absmin[0] && p[0] <= z->absmax[0] && p[1] >= z->absmin[1] && p[1] <= z->absmax[1] &&
		    p[2] >= z->absmin[2] && p[2] <= z->absmax[2])
			return true;
	}
	return false;
}

void GM_ConquerFrame(void)
{
	int sec = (int)level.time;
	int idx = gix.soundindex("player/conquerLP.wav");
	for (int i = 0; i < gm.nzones; i++) {
		gm_zone_t *z = &gm.zones[i];
		if (gm_zonetouched[i]) {
			if (!z->occupied || sec != z->lastsecond) {
				gix.positioned_sound(z->ent->s.origin, z->ent, CHAN_AUTO, idx, 1, ATTN_NORM, 0, 0);
				gi.dprintf("%f: %d %d\n", level.time, sec, sec);
			}
			z->occupied = true;
			z->lastsecond = sec;
		} else
			z->occupied = false;
		gm_zonetouched[i] = false;
	}
}

void GM_ConquerEndFrame(edict_t *ent)
{
	gm_client_t *c = GM_Cl(ent);
	if (c->inzone && level.time - c->zoneentered >= 3.0f) {
		GM_AddScore(ent, 3);
		GM_LocalSound(ent, "dm/conquer/beep.wav", 1, ATTN_STATIC, CHAN_VOICE);
		c->zoneentered = level.time;
	}
	c->inzone_prev = c->inzone;
	c->inzone = false;
}

// Configstrings 33 and 10, written by a client's end of frame when they differ from the cached values (cleared at
// level setup, so the first client frame resends them). Observed values (captures orig_conquer and
// orig_conquer_numbered): 33 = "1" with team play else "0"; 10 = "1" with numbered teams (dmflags 0x8000) else
// "0" (SPEC-790 2 says -1/0 and the truncated 3276; the original sends 1/0).
void GM_ConquerConfigstrings(void)
{
	const char *v33 = GM_TeamGame() ? "1" : "0";
	if (strcmp(v33, gm.cs33)) {
		gm_strncpyz(gm.cs33, v33, sizeof(gm.cs33));
		gix.configstring(CS_GM_TEAMGAME, v33);
	}
	const char *v10 = GM_Flag(DF_NUMBERED_TEAMS) ? "1" : "0";
	if (strcmp(v10, gm.cs10)) {
		gm_strncpyz(gm.cs10, v10, sizeof(gm.cs10));
		gix.configstring(CS_GM_CONQUER_DMFLAGS, v10);
	}
}

void GM_ConquerLevelExit(void) { gm.cs33[0] = gm.cs10[0] = 0; }
