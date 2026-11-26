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
#include "../../engine/src/common/header/om_corpse.h"
#include "nav/nav_api.h" /* G5 */
#include "bots/bot_api.h" /* G5 */
#include "modes/g_modes.h" /* G9 */
#include "damage/damage_api.h"
#include "damage/g3_local.h" /* G3: bolt positions for omaim head */
#include "ai/ai_hooks.h" /* omaudit */

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

/* omaim: the view turns to the chest of the nearest living character (a test aid for weapons) */
static void Cmd_OmAim_f(edict_t *ent)
{
	if (!CheatsOK(ent) || !ent->client)
		return;
	/* "omaim [targetname]": the nearest living character, or the one with that targetname; "omaim head": the nearest
	   one's mouth bolt (point-blank head shot tests) */
	const char *want = gi.argc() > 1 ? gi.argv(1) : NULL;
	bool head = want && !Q_stricmp(want, "head");
	if (head)
		want = NULL;
	edict_t *best = NULL;
	float bd = 1e9f;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e->client || !(e->svflags & SVF_MONSTER) || e->health <= 0 || (head && e->solid == SOLID_NOT))
			continue;
		if (want && (!e->targetname || Q_stricmp(e->targetname, want)))
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, ent->s.origin, d);
		if (VectorLength(d) < bd) {
			bd = VectorLength(d);
			best = e;
		}
	}
	if (!best) {
		gi.dprintf("omaim: no living character%s%s\n", want ? " named " : "", want ? want : "");
		return;
	}
	vec3_t eye, to, ang;
	VectorCopy(ent->s.origin, eye);
	eye[2] += ent->viewheight;
	VectorCopy(best->s.origin, to);
	to[2] += (best->mins[2] + best->maxs[2]) * 0.5f + 8;
	if (head && !G3G_BoltWorldByName(best, "sbolt_mouth", to)) {
		VectorCopy(best->s.origin, to);
		to[2] += best->maxs[2] - 8;
	}
	VectorSubtract(to, eye, to);
	vectoangles(to, ang);
	for (int k = 0; k < 2; k++) {
		ent->client->ps.pmove.delta_angles[k] = (short)ANGLE2SHORT(ang[k] - ent->client->resp.cmd_angles[k]);
		ent->client->ps.viewangles[k] = ang[k];
	}
	VectorCopy(ent->client->ps.viewangles, ent->client->v_angle);
	vec3_t end;
	VectorNormalize(to);
	VectorMA(eye, 1024, to, end);
	trace_t tr = gi.trace(eye, NULL, NULL, end, ent, 0x06000081);
	gi.cprintf(ent, PRINT_HIGH, "omaim %s at %s dist %.0f solid %d svf 0x%x mins %s maxs %s linked %d area %d trace %s %.2f\n",
		best->classname, vtos(best->s.origin), bd, best->solid, best->svflags, vtos(best->mins), vtos(best->maxs),
		best->linkcount, best->areanum, tr.ent ? tr.ent->classname : "-", tr.fraction);
}

/* omnear [dist] (test aid, AI combat checks): the player is put dist units (default 300) from the nearest living
   character not visited by an earlier omnear, on the line towards the player as far as the room allows, facing it */
static void Cmd_OmNear_f(edict_t *ent)
{
	static int visited[64], nvisited, level_frame;
	if (!CheatsOK(ent))
		return;
	if (level.framenum < level_frame)
		nvisited = 0;
	level_frame = level.framenum;
	float dist = gi.argc() > 1 ? (float)atof(gi.argv(1)) : 300.0f;
	edict_t *best = NULL;
	float bd = 1e9f;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e->client || !(e->svflags & SVF_MONSTER) || e->health <= 0 || e->solid == SOLID_NOT)
			continue;
		bool seen = false;
		for (int k = 0; k < nvisited; k++)
			if (visited[k] == i)
				seen = true;
		if (seen)
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, ent->s.origin, d);
		if (VectorLength(d) < bd) {
			bd = VectorLength(d);
			best = e;
		}
	}
	if (!best) {
		gi.dprintf("omnear: no character left\n");
		return;
	}
	if (nvisited < 64)
		visited[nvisited++] = (int)(best - g_edicts);
	vec3_t dir, end, mins = {-16, -16, -24}, maxs = {16, 16, 32};
	VectorSubtract(ent->s.origin, best->s.origin, dir);
	dir[2] = 0;
	if (VectorNormalize(dir) < 1)
		VectorSet(dir, 1, 0, 0);
	vec3_t from;
	VectorCopy(best->s.origin, from);
	from[2] += 8;
	VectorMA(from, dist, dir, end);
	trace_t tr = gi.trace(from, mins, maxs, end, best, MASK_PLAYERSOLID);
	vec3_t o, to;
	VectorCopy(tr.endpos, o);
	VectorSubtract(best->s.origin, o, to);
	float yaw = vectoyaw(to);
	gi.dprintf("omnear: %s #%d at %s, player to %s (%.0f units)\n", best->classname, (int)(best - g_edicts),
	           vtos(best->s.origin), vtos(o), tr.fraction * dist);
	gi.AddCommandString(va("cmd omsetpos %.0f %.0f %.0f %.0f 0\n", o[0], o[1], o[2], yaw));
}

/* ombot (test aid): one line per connected player. Feet against the floor, Ghoul tint, mesh box and the
   weapon flash bolt against the view. "ombot look" then stands the caller in front of the nearest living bot. */
static void OmBotDump(edict_t *e)
{
	gclient_t *c = e->client;
	float tr, tg, tb, ta;
	tr = tg = tb = ta = -1;
	if (e->ghoulInst)
		e->ghoulInst->GetTint(&tr, &tg, &tb, &ta);

	vec3_t down;
	VectorCopy(e->s.origin, down);
	down[2] -= 256;
	trace_t fl = gi.trace(e->s.origin, NULL, NULL, down, e, MASK_SOLID);
	float feet = e->s.origin[2] + e->mins[2];
	float floorz = fl.fraction < 1 ? fl.endpos[2] : -9999.0f;

	float bmin[3], bmax[3];
	VectorClear(bmin);
	VectorClear(bmax);
	if (e->ghoulInst) {
		ghoul::Matrix4 root;
		memset(&root, 0, sizeof(root));
		root.m[0][0] = root.m[1][1] = root.m[2][2] = root.m[3][3] = 1;
		root.flags = 1;
		e->ghoulInst->GetBounds(level.time, &root, bmin, bmax, 0, false);
	}

	vec3_t hand, heel, flash;
	VectorClear(hand);
	VectorClear(heel);
	VectorClear(flash);
	int handok = GhoulBoltPos(e, "wbolt_hand_r", hand);
	int heelok = GhoulBoltPos(e, "sbolt_heel_r", heel);
	int flashok = 0;
	int flashbolt = -1;
	if (e->ghoulInst) {
		int nc = e->ghoulInst->NumChildren();
		for (int i = 0; i < nc; i++) {
			ghoul::GhoulID bid = 0;
			ghoul::IGhoulInst *ch = e->ghoulInst->GetChild(i, &bid);
			vec3_t f;
			if (ch && GhoulInstBoltPos(ch, e->s.origin, e->s.angles, "flash", f)) {
				VectorCopy(f, flash);
				flashok = 1;
				flashbolt = (int)bid;
				break;
			}
		}
	}

	vec3_t fwd, barrel;
	AngleVectors(c->v_angle, fwd, NULL, NULL);
	float bardot = -2;
	float baryaw = 0;
	if (handok && flashok) {
		VectorSubtract(flash, hand, barrel);
		if (VectorNormalize(barrel) > 0.01f) {
			bardot = DotProduct(fwd, barrel);
			baryaw = vectoyaw(barrel);
		}
	}

	float protect = (c->spawnprotect_time > level.time) ? c->spawnprotect_time - level.time : 0;
	gi.dprintf("OMBOT %s #%d bot %d org %.0f %.0f %.0f feet %.0f floor %.0f gap %.0f ground %d "
	           "tint %.2f protect %.1f view %.0f %.0f sang %.0f %.0f meshz %.0f..%.0f "
	           "hand %d %.0f %.0f %.0f heel %d %.0f flash %d bolt %d %.0f %.0f %.0f bardot %.2f baryaw %.0f\n",
	           c->pers.netname, (int)(e - g_edicts), c->pers.isbot ? 1 : 0,
	           e->s.origin[0], e->s.origin[1], e->s.origin[2], feet, floorz, floorz > -9000 ? feet - floorz : -9999.0f,
	           e->groundentity ? 1 : 0, ta, protect, c->v_angle[PITCH], c->v_angle[YAW], e->s.angles[PITCH],
	           e->s.angles[YAW], bmin[2], bmax[2], handok, hand[0], hand[1], hand[2], heelok, heelok ? heel[2] : 0.0f,
	           flashok, flashbolt, flash[0], flash[1], flash[2], bardot, baryaw);
}

static void Cmd_OmBot_f(edict_t *ent)
{
	if (!CheatsOK(ent) || !ent->client)
		return;
	edict_t *best = NULL;
	float bd = 1e9f;
	int n = 0;
	int limit = game.maxclients;
	if (limit > globals.num_edicts - 1)
		limit = globals.num_edicts - 1;
	for (int i = 1; i <= limit; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || !e->client || !e->client->pers.connected)
			continue;
		OmBotDump(e);
		n++;
		if (e == ent || !e->client->pers.isbot || e->health <= 0)
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, ent->s.origin, d);
		float dist = VectorLength(d);
		if (dist < bd) {
			bd = dist;
			best = e;
		}
	}
	if (!n)
		gi.dprintf("OMBOT none\n");
	if (best && gi.argc() >= 3 && !Q_stricmp(gi.argv(1), "face")) {
		float yaw = (float)atof(gi.argv(2));
		best->s.angles[YAW] = yaw;
		best->s.angles[PITCH] = best->s.angles[ROLL] = 0;
		best->client->v_angle[YAW] = yaw;
		best->client->v_angle[PITCH] = 0;
		best->client->v_angle[ROLL] = 0;
		VectorCopy(best->client->v_angle, best->client->ps.viewangles);
		best->client->ps.pmove.delta_angles[YAW] = (short)ANGLE2SHORT(yaw);
		best->client->ps.pmove.delta_angles[PITCH] = 0;
		best->client->ps.pmove.delta_angles[ROLL] = 0;
		gi.cvar_set("om_botstill", "1");
		gi.dprintf("OMBOT face #%d yaw %.0f\n", (int)(best - g_edicts), yaw);
		return;
	}
	if (!best || gi.argc() < 2 || Q_stricmp(gi.argv(1), "look"))
		return;

	/* Park the bot on the floor in front of the caller and hold still. The caller is already
	   standing where the picture should be taken from. */
	vec3_t fwd, spot, to;
	AngleVectors(ent->client->v_angle, fwd, NULL, NULL);
	fwd[2] = 0;
	if (VectorNormalize(fwd) < 0.1f)
		VectorSet(fwd, 1, 0, 0);
	VectorMA(ent->s.origin, 96, fwd, spot);
	vec3_t drop = {spot[0], spot[1], spot[2] - 160};
	vec3_t top = {spot[0], spot[1], ent->s.origin[2] + 64};
	trace_t fl = gi.trace(top, NULL, NULL, drop, best, MASK_SOLID);
	if (fl.fraction < 1)
		spot[2] = fl.endpos[2] + 24;
	VectorCopy(spot, best->s.origin);
	VectorClear(best->velocity);
	VectorSubtract(ent->s.origin, spot, to);
	vec3_t ang;
	vectoangles(to, ang);
	best->s.angles[YAW] = ang[YAW];
	best->s.angles[PITCH] = best->s.angles[ROLL] = 0;
	best->client->v_angle[YAW] = ang[YAW];
	best->client->v_angle[PITCH] = 0;
	best->client->v_angle[ROLL] = 0;
	VectorCopy(best->client->v_angle, best->client->ps.viewangles);
	best->client->ps.pmove.delta_angles[YAW] = (short)ANGLE2SHORT(ang[YAW]);
	best->client->ps.pmove.delta_angles[PITCH] = 0;
	best->client->ps.pmove.delta_angles[ROLL] = 0;
	gi.linkentity(best);
	gi.cvar_set("om_botstill", "1");
	gi.dprintf("OMBOT park #%d at %.0f %.0f %.0f face %.0f\n", (int)(best - g_edicts), spot[0], spot[1], spot[2],
	           ang[YAW]);
}

/* ominv (test aid): the weapons owned and the current one's clip and reserve (not WriteInfo: writing the info
   moves the clips into the reserve, each call added them again) */
static void Cmd_OmInv_f(edict_t *ent)
{
	if (!ent->client || !ent->client->inv)
		return;
	PlayerInventory *inv = ent->client->inv;
	char owned[128] = "";
	for (int w = 1; w <= 14; w++)
		if (inv->OwnsWeapon(w))
			{ size_t n = strlen(owned); snprintf(owned + n, sizeof(owned) - n, " %d", w); }
	gi.dprintf("ominv: health %d weapon %d clip %d/%d reserve %d owned%s damage_dir 0x%x\n", ent->health,
	           inv->CurrentWeapon(), inv->ClipRounds(), inv->ClipSize(), inv->ReserveAmmo(), owned,
	           ent->client->damage_dir);
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

/* ------------------------------------------------------------------------------------------------------------------
 * omfreecam [0|1]: the game half of om_freecam (the client hides the interface, cl_main.c). While it is on, the player
 * flies through walls (noclip), is not seen by the enemies (notarget) and is not hurt (god); off again, the player is
 * back where the flight began, with the movement and flags of before. Single player needs no cheats (a camera for
 * pictures and videos), multiplayer the server's. A refusal turns the client's half off again (stufftext).
 * The record names its level: another level (a load, a level change) ends it. A save written during the flight holds
 * the player as before it (Freecam_SaveSwap), and so does the god flag carried to the next level (Freecam_RealFlags).
 * ---------------------------------------------------------------------------------------------------------------- */
static struct
{
	bool on;
	int flags, movetype;
	vec3_t origin, angles;
	char map[MAX_QPATH];
	/* the camera's place while a save is written */
	vec3_t camorigin, camangles;
} freecam[MAX_CLIENTS];

static int FreecamIndex(edict_t *ent)
{
	int i = ent ? (int)(ent - g_edicts) - 1 : -1;
	return (ent && ent->client && i >= 0 && i < MAX_CLIENTS) ? i : -1;
}

static bool FreecamActive(int i)
{
	if (i < 0 || !freecam[i].on)
		return false;
	if (strcmp(freecam[i].map, level.mapname)) {
		freecam[i].on = false; /* a record of another level */
		return false;
	}
	return true;
}

/* the player at o, looking along a (view angles; roll 0), standing still */
static void FreecamPlace(edict_t *ent, const vec3_t o, const vec3_t a)
{
	gclient_t *c = ent->client;
	VectorCopy(o, ent->s.origin);
	VectorClear(ent->velocity);
	for (int k = 0; k < 3; k++)
		c->ps.pmove.origin[k] = (short)(o[k] * 8.0f);
	c->ps.pmove.velocity[0] = c->ps.pmove.velocity[1] = c->ps.pmove.velocity[2] = 0;
	ent->s.angles[YAW] = a[YAW];
	ent->s.angles[PITCH] = 0;
	ent->s.angles[ROLL] = 0;
	for (int k = 0; k < 3; k++) {
		float v = k == ROLL ? 0.0f : a[k];
		c->ps.pmove.delta_angles[k] = (short)ANGLE2SHORT(v - c->resp.cmd_angles[k]);
		c->ps.viewangles[k] = v;
		c->v_angle[k] = v;
	}
	VectorClear(c->kick_angles);
}

static void FreecamRefuse(edict_t *ent)
{
	gi.WriteByte(svc_stufftext);
	gi.WriteString("om_freecam 0\n");
	gi.unicast(ent, true);
}

static void Cmd_OmFreecam_f(edict_t *ent)
{
	int i = FreecamIndex(ent);
	if (i < 0)
		return;
	bool on = FreecamActive(i);
	bool want = gi.argc() > 1 ? atoi(gi.argv(1)) != 0 : !on;
	if (want == on)
		return;
	if (want) {
		if ((deathmatch->value || maxclients->value > 1) && !CheatsOK(ent)) {
			FreecamRefuse(ent);
			return;
		}
		if (ent->health <= 0 || ent->client->resp.spectator) {
			FreecamRefuse(ent);
			return;
		}
		freecam[i].on = true;
		freecam[i].flags = ent->flags & (FL_GODMODE | FL_NOTARGET);
		freecam[i].movetype = ent->movetype;
		VectorCopy(ent->s.origin, freecam[i].origin);
		VectorCopy(ent->client->v_angle, freecam[i].angles);
		Com_sprintf(freecam[i].map, sizeof(freecam[i].map), "%s", level.mapname);
		ent->flags |= FL_GODMODE | FL_NOTARGET;
		ent->movetype = MOVETYPE_NOCLIP;
	} else {
		freecam[i].on = false;
		ent->flags = (ent->flags & ~(FL_GODMODE | FL_NOTARGET)) | freecam[i].flags;
		ent->movetype = freecam[i].movetype;
		FreecamPlace(ent, freecam[i].origin, freecam[i].angles);
		gi.linkentity(ent);
	}
}

int Freecam_RealFlags(edict_t *ent)
{
	int i = FreecamIndex(ent);
	if (!FreecamActive(i))
		return ent->flags;
	return (ent->flags & ~(FL_GODMODE | FL_NOTARGET)) | freecam[i].flags;
}

void Freecam_SaveSwap(bool before)
{
	for (int i = 0; i < game.maxclients && i < MAX_CLIENTS; i++) {
		edict_t *ent = &g_edicts[1 + i];
		if (!ent->inuse || !ent->client || !FreecamActive(i))
			continue;
		if (before) {
			VectorCopy(ent->s.origin, freecam[i].camorigin);
			VectorCopy(ent->client->v_angle, freecam[i].camangles);
			ent->flags = Freecam_RealFlags(ent);
			ent->movetype = freecam[i].movetype;
			FreecamPlace(ent, freecam[i].origin, freecam[i].angles);
		} else {
			ent->flags |= FL_GODMODE | FL_NOTARGET;
			ent->movetype = MOVETYPE_NOCLIP;
			FreecamPlace(ent, freecam[i].camorigin, freecam[i].camangles);
		}
	}
}

/* test aid: uses every entity with the given targetname (as a trigger would, the player activating) */
static void Cmd_OmFire_f(edict_t *ent)
{
	if (!CheatsOK(ent) || gi.argc() < 2)
		return;
	int n = 0;
	edict_t *t = NULL;
	while ((t = G_Find(t, FOFS(targetname), gi.argv(1)))) {
		if (t->use) {
			t->use(t, ent, ent);
			n++;
		}
	}
	gi.cprintf(ent, PRINT_HIGH, "omfire %s: %d used\n", gi.argv(1), n);
}

/* test aid: omboxents x0 y0 z0 x1 y1 z1 - the solid entities touching that box */
static void Cmd_OmBoxEnts_f(edict_t *ent)
{
	if (!CheatsOK(ent) || gi.argc() < 7)
		return;
	vec3_t mn, mx;
	for (int i = 0; i < 3; i++) {
		mn[i] = (float)atof(gi.argv(1 + i));
		mx[i] = (float)atof(gi.argv(4 + i));
	}
	edict_t *list[64];
	int n = gi.BoxEdicts(mn, mx, list, 64, AREA_SOLID);
	for (int i = 0; i < n; i++)
		gi.cprintf(ent, PRINT_HIGH, "omboxents %d %s tn %s solid %d absmin %s absmax %s state %d\n",
		           G_EdictNum(list[i]), list[i]->classname, list[i]->targetname ? list[i]->targetname : "-",
		           (int)list[i]->solid, vtos(list[i]->absmin), vtos(list[i]->absmax), list[i]->moveinfo.state);
}

static void UseTargetDetails(edict_t *ent);

/* test aid: omuse <entity number> - its use-key reaction (else its use) with the player as other and activator, as a
   press of the use key on it would; separates "the key does not reach it" from "using it does nothing" */
static void Cmd_OmUse_f(edict_t *ent)
{
	if (!CheatsOK(ent) || gi.argc() < 2)
		return;
	int n = atoi(gi.argv(1));
	edict_t *e = (n > 0 && n < globals.num_edicts) ? &g_edicts[n] : NULL;
	if (!e || !e->inuse || !(e->plUse || e->use)) {
		gi.cprintf(ent, PRINT_HIGH, "omuse: %d has no use reaction\n", n);
		return;
	}
	gi.cprintf(ent, PRINT_HIGH, "omuse %d %s (%s)\n", n, e->classname, e->plUse ? "use key" : "use");
	if (e->plUse)
		e->plUse(e, ent, ent);
	else
		e->use(e, ent, ent);
}

/* test aid: what the use key would act on now (G_UseTarget), for "the button does nothing" reports */
static void Cmd_OmUseTarget_f(edict_t *ent)
{
	if (!CheatsOK(ent) || !ent->client)
		return;
	UseTargetDetails(ent);
	edict_t *e = G_UseTarget(ent);
	if (!e) {
		gi.cprintf(ent, PRINT_HIGH, "omusetarget: nothing\n");
		return;
	}
	gi.cprintf(ent, PRINT_HIGH, "omusetarget %d %s tn %s target %s solid %d state %d absmin %s absmax %s\n",
	           G_EdictNum(e), e->classname, e->targetname ? e->targetname : "-", e->target ? e->target : "-",
	           (int)e->solid, e->moveinfo.state, vtos(e->absmin), vtos(e->absmax));
}

/* the details: what the view trace hits and every entity with a use-key reaction within 80 units, with its facing,
   distance and the trace to its centre */
static void UseTargetDetails(edict_t *ent)
{
	vec3_t eye, fwd, end, mins, maxs;
	VectorCopy(ent->s.origin, eye);
	eye[2] += ent->viewheight;
	AngleVectors(ent->client->v_angle, fwd, NULL, NULL);
	VectorMA(eye, 75, fwd, end);
	trace_t vt = gi.trace(eye, NULL, NULL, end, ent, MASK_PLAYERSOLID);
	gi.cprintf(ent, PRINT_HIGH, "omusetarget eye %s angles %s view trace %.2f hits %d %s at %s\n", vtos(eye),
	           vtos(ent->client->v_angle), vt.fraction, vt.ent ? G_EdictNum(vt.ent) : -1,
	           vt.ent && vt.ent->classname ? vt.ent->classname : "-", vtos(vt.endpos));
	for (int i = 0; i < 3; i++) {
		mins[i] = eye[i] - 80;
		maxs[i] = eye[i] + 80;
	}
	edict_t *list[256];
	int n = gi.BoxEdicts(mins, maxs, list, 256, AREA_SOLID);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (!e->plUse || e == ent)
			continue;
		vec3_t c, d;
		for (int j = 0; j < 3; j++)
			c[j] = e->solid == SOLID_BSP ? (e->absmin[j] + e->absmax[j]) * 0.5f
			                             : e->s.origin[j] + (e->mins[j] + e->maxs[j]) * 0.5f;
		VectorSubtract(c, eye, d);
		float dist = VectorLength(d);
		d[2] = 0;
		VectorNormalize(d);
		vec3_t flat = {fwd[0], fwd[1], 0};
		VectorNormalize(flat);
		trace_t tr = gi.trace(eye, NULL, NULL, c, ent, MASK_PLAYERSOLID);
		gi.cprintf(ent, PRINT_HIGH, "omusetarget   cand %d %s health %d dot %.3f dist %.0f trace %.2f hits %d %s\n",
		           G_EdictNum(e), e->classname, e->health, DotProduct(d, flat), dist, tr.fraction,
		           tr.ent ? G_EdictNum(tr.ent) : -1, tr.ent && tr.ent->classname ? tr.ent->classname : "-");
	}
}

/* test aid: prints the nearest dead character (origin, solid, svflags, box) */
static void Cmd_OmCorpse_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	edict_t *best = NULL;
	float bd = 1e9f;
	if (gi.argc() >= 2) {
		/* omcorpse <targetname>: that entity instead (alive or not) */
		const char *a = gi.argv(1);
		if (a[0] == '#') {
			int n = atoi(a + 1);
			best = (n > 0 && n < globals.num_edicts && g_edicts[n].inuse) ? &g_edicts[n] : NULL;
		} else
			best = G_Find(NULL, FOFS(targetname), a);
		if (best)
			gi.cprintf(ent, PRINT_HIGH, "omcorpse %s at %s health %d solid %d absmin %s absmax %s state %d\n",
			           best->classname, vtos(best->s.origin), best->health, (int)best->solid, vtos(best->absmin),
			           vtos(best->absmax), best->moveinfo.state);
		return;
	}
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e->client || !e->ai || e->health > 0)
			continue;
		gi.dprintf("OMCORPSE #%d %s %.0f %.0f %.0f mins %.0f %.0f %.0f maxs %.0f %.0f %.0f\n", i,
		           e->classname ? e->classname : "-", e->s.origin[0], e->s.origin[1], e->s.origin[2], e->mins[0],
		           e->mins[1], e->mins[2], e->maxs[0], e->maxs[1], e->maxs[2]);
		vec3_t d;
		VectorSubtract(e->s.origin, ent->s.origin, d);
		if (VectorLength(d) < bd) {
			bd = VectorLength(d);
			best = e;
		}
	}
	if (!best) {
		gi.cprintf(ent, PRINT_HIGH, "omcorpse: none\n");
		return;
	}
	gi.cprintf(ent, PRINT_HIGH,
	           "omcorpse %s #%d at %s solid %d svflags 0x%x clipmask 0x%x mins %s maxs %s ground %d\n",
	           best->classname, G_EdictNum(best), vtos(best->s.origin), (int)best->solid, best->svflags,
	           best->clipmask, vtos(best->mins), vtos(best->maxs),
	           best->groundentity ? G_EdictNum(best->groundentity) : -1);
}

/* The body omkill just shot. Nearest-dead is the wrong corpse once the camera has moved. */
static int om_lastkill = -1;

/* Lower of the two hand bolts, in world space. 0 when neither is posed. */
static int OmLowerHand(edict_t *ent, vec3_t out)
{
	static const char *names[] = {"wbolt_hand_l", "wbolt_hand_r"};
	float bestz = 1.0e9f;
	int ok = 0;

	for (int i = 0; i < 2; i++) {
		vec3_t bp;
		if (!G3G_BoltWorldByName(ent, names[i], bp))
			continue;
		ok = 1;
		if (bp[2] < bestz) {
			bestz = bp[2];
			VectorCopy(bp, out);
		}
	}
	return ok;
}

/* 1 when the eye is in open air and a wall does not stand between it and the aim point.
   The corpse is ignored, so its own mesh does not count as a wall. */
static int OmEyeClear(const vec3_t eye, const vec3_t aim, edict_t *ignore)
{
	vec3_t up;
	trace_t tr;

	if (gi.pointcontents(eye) & MASK_SOLID)
		return 0;
	VectorCopy(eye, up);
	up[2] += 8.0f;
	if (gi.pointcontents(up) & MASK_SOLID)
		return 0;
	tr = gi.trace(eye, vec3_origin, vec3_origin, aim, ignore, MASK_SOLID);
	if (tr.startsolid || tr.allsolid || tr.fraction < 0.90f)
		return 0;
	return 1;
}

/* Pick an eye position that can see the aim point. hand: close and looking down, so a
   finger in the floor is in the frame. wide: far enough to show the whole prone body.
   Noclip would otherwise walk the eye through a wall into the next room. */
static int OmPlaceEye(const vec3_t aim, edict_t *body, int hand, vec3_t eye, float *yaw, float *pitch)
{
	static const float yaws[] = {0, 45, 90, 135, 180, 225, 270, 315};
	static const float wide_d[] = {64.0f, 88.0f, 112.0f};
	static const float wide_u[] = {24.0f, 40.0f, 56.0f};
	static const float hand_d[] = {32.0f, 44.0f, 58.0f};
	static const float hand_u[] = {18.0f, 30.0f, 44.0f};
	const float *dists = hand ? hand_d : wide_d;
	const float *ups = hand ? hand_u : wide_u;
	int found = 0;
	float best = -1.0e9f;

	for (int ui = 0; ui < 3; ui++) {
		for (int di = 0; di < 3; di++) {
			for (int yi = 0; yi < 8; yi++) {
				vec3_t ang, fwd, cam, to, view;
				float score;
				VectorSet(ang, 0, yaws[yi], 0);
				AngleVectors(ang, fwd, NULL, NULL);
				VectorMA(aim, dists[di], fwd, cam);
				cam[2] = aim[2] + ups[ui];
				if (!OmEyeClear(cam, aim, body))
					continue;
				VectorSubtract(aim, cam, to);
				vectoangles(to, view);
				if (hand)
					score = 50.0f - (view[PITCH] > 38.0f ? view[PITCH] - 38.0f : 38.0f - view[PITCH]) - dists[di] * 0.08f;
				else
					score = 50.0f - (view[PITCH] > 20.0f ? view[PITCH] - 20.0f : 20.0f - view[PITCH])
					        - (dists[di] > 88.0f ? dists[di] - 88.0f : 88.0f - dists[di]) * 0.15f;
				if (yaws[yi] == 0.0f)
					score += 2.0f;
				if (score > best) {
					best = score;
					VectorCopy(cam, eye);
					*yaw = view[YAW];
					*pitch = view[PITCH];
					found = 1;
				}
			}
		}
	}
	return found;
}

/* Ghoul ray only. 0 is a miss on the drawn mesh, not a part sphere. */
static int OmMeshRay(edict_t *ent, const vec3_t from, const vec3_t to)
{
	vec3_t dir, ls, ld;
	ghoul::HitRecord recs[4];

	if (!ent || !ent->ghoulInst)
		return -1;
	VectorSubtract(to, from, dir);
	if (VectorNormalize(dir) < 0.5f)
		return -1;
	G3_WorldToEntity(ent, from, ls);
	G3_WorldDirToEntity(ent, dir, ld);
	return ent->ghoulInst->TraceRay(G3_Time(), ls, ld, recs, 4);
}

/* omcorpsecam [last|#n] [wide|hand]: measure one corpse and look at it from open air.
   The same body is sampled again later. dz is the origin change since the previous
   sample, so a body that keeps falling is SINK even when a single frame looks seated.
   gapm is the drawn mesh against the floor (negative is in the floor). gaph is the
   hand bolt, which sits in the middle of the hand. */
static void Cmd_OmCorpseCam_f(edict_t *ent)
{
	static float prevz[2048], prevt[2048], firstfl[2048];
	static unsigned char flseen[2048];
	edict_t *best = NULL;
	float bd = 1e9f;
	float handz = 1.0e9f, low, mesh, bolt, flm, fls, gapm, gaph, dz, dt, mloc;
	vec3_t a, b;
	trace_t trm, trs;
	int id, ss, handmode, uselast, wantid;
	char verdict[48];
	const char *mode = "wide";

	if (!CheatsOK(ent) || !ent->client)
		return;
	handmode = 0;
	uselast = 0;
	wantid = 0;
	for (int aarg = 1; aarg < gi.argc(); aarg++) {
		const char *s = gi.argv(aarg);
		if (!s || !s[0])
			continue;
		if (s[0] == '#')
			wantid = atoi(s + 1);
		else if (!Q_stricmp(s, "last"))
			uselast = 1;
		else if (!Q_stricmp(s, "hand"))
			handmode = 1;
		else if (!Q_stricmp(s, "wide"))
			handmode = 0;
	}
	if (wantid > 0 && wantid < globals.num_edicts && g_edicts[wantid].inuse)
		best = &g_edicts[wantid];
	else if (uselast && om_lastkill > 0 && om_lastkill < globals.num_edicts && g_edicts[om_lastkill].inuse)
		best = &g_edicts[om_lastkill];
	if (!best && !uselast && !wantid) {
		for (int i = 1; i < globals.num_edicts; i++) {
			edict_t *e = &g_edicts[i];
			vec3_t d;
			if (!e->inuse || e->client || !e->ai || e->health > 0)
				continue;
			VectorSubtract(e->s.origin, ent->s.origin, d);
			if (VectorLength(d) < bd) {
				bd = VectorLength(d);
				best = e;
			}
		}
	}
	if (!best || !best->ai) {
		gi.dprintf("OMCAM none\n");
		return;
	}
	id = (int)(best - g_edicts);
	low = OM_CorpseLowest(best, &handz);
	mesh = OM_CorpseMeshBottom(best);
	bolt = best->s.origin[2] + OM_CorpseBolt(best);
	VectorCopy(best->s.origin, a);
	a[2] += 24.0f;
	VectorCopy(a, b);
	b[2] -= 256.0f;
	trm = gi.trace(a, vec3_origin, vec3_origin, b, best, MASK_MONSTERSOLID);
	trs = gi.trace(a, vec3_origin, vec3_origin, b, best, MASK_SOLID);
	ss = (trm.startsolid || trm.allsolid) ? 1 : 0;
	flm = (!ss && trm.fraction < 1.0f) ? trm.endpos[2] : -9999.0f;
	fls = (trs.fraction < 1.0f && !trs.startsolid) ? trs.endpos[2] : -9999.0f;
	gapm = (mesh < 1.0e8f && flm > -9000.0f) ? mesh - flm : 9999.0f;
	gaph = (handz < 1.0e8f && flm > -9000.0f) ? handz - flm : 9999.0f;
	dz = 0.0f;
	dt = 0.0f;
	if (id > 0 && id < 2048 && prevt[id] > 0.0f) {
		dt = level.time - prevt[id];
		dz = best->s.origin[2] - prevz[id];
	}
	if (id > 0 && id < 2048) {
		if (!flseen[id] && flm > -9000.0f) {
			firstfl[id] = flm;
			flseen[id] = 1;
		}
		prevz[id] = best->s.origin[2];
		prevt[id] = level.time;
	}
	float cam_sup = 0.0f, cam_limb = 0.0f, cam_plh = 9999.0f, cam_nz = 0.0f;
	int cam_have = 0, ray_t = -1, ray_h = -1, ray_a = -1;
	if (!ss && trm.fraction < 1.0f) {
		cam_nz = trm.plane.normal[2];
		if (cam_nz >= OM_CORPSE_SLOPE_MIN)
			cam_have = OM_CorpseContacts(best, trm.endpos, trm.plane.normal, &cam_sup, &cam_limb);
	}
	{
		vec3_t from, to, handp;
		VectorCopy(best->s.origin, to);
		VectorCopy(to, from);
		from[2] += 72.0f;
		ray_t = OmMeshRay(best, from, to);
		if (OmLowerHand(best, handp)) {
			if (cam_have)
				cam_plh = DotProduct(handp, trm.plane.normal) - DotProduct(trm.endpos, trm.plane.normal);
			VectorCopy(handp, to);
			VectorCopy(handp, from);
			from[2] += 48.0f;
			ray_h = OmMeshRay(best, from, to);
		}
		VectorCopy(best->s.origin, to);
		to[0] += 80.0f;
		VectorCopy(to, from);
		from[2] += 72.0f;
		ray_a = OmMeshRay(best, from, to);
	}
	/* World Z against the floor under the origin lies on a slope. The seat
	   is the posed mesh's distance to that plane. */
	if (cam_have) {
		if (cam_limb < -1.5f)
			snprintf(verdict, sizeof(verdict), "IN_FLOOR");
		else if (cam_limb > 4.0f)
			snprintf(verdict, sizeof(verdict), "HOVER");
		else
			snprintf(verdict, sizeof(verdict), "SEAT");
	} else if (gapm < -1.5f)
		snprintf(verdict, sizeof(verdict), "IN_FLOOR");
	else if (gapm > 4.0f)
		snprintf(verdict, sizeof(verdict), "HOVER");
	else if (gapm > 9000.0f)
		snprintf(verdict, sizeof(verdict), "NOMESH");
	else
		snprintf(verdict, sizeof(verdict), "SEAT");
	if (dt > 0.4f && dz < -4.0f)
		snprintf(verdict + strlen(verdict), sizeof(verdict) - strlen(verdict), "+SINK");
	if (id > 0 && id < 2048 && flseen[id] && flm > -9000.0f && flm < firstfl[id] - 16.0f)
		snprintf(verdict + strlen(verdict), sizeof(verdict) - strlen(verdict), "+FELL");
	mloc = (mesh < 1.0e8f) ? mesh - best->s.origin[2] : 0.0f;
	{
		vec3_t aim, eye, origin;
		vec3_t handp;
		float cyaw = 0, cpitch = 0;
		int placed = 0;
		VectorCopy(best->s.origin, aim);
		aim[2] += 6.0f;
		if (handmode) {
			mode = "hand";
			if (OmLowerHand(best, handp))
				VectorCopy(handp, aim);
			else
				mode = "NOHAND";
		}
		placed = OmPlaceEye(aim, best, handmode, eye, &cyaw, &cpitch);
		if (!placed)
			mode = "BLOCKED";
		gi.dprintf("OMCAM #%d %s x %.1f y %.1f z %.1f vz %.1f flm %.1f fls %.1f bolt %.1f hand %.1f low %.1f mesh %.1f mloc %.1f gapm %.1f gaph %.1f nz %.2f sup %.1f limb %.1f plh %.1f rays %d %d %d gnd %d ss %d dz %.1f dt %.1f eye %.1f %.1f %.1f cyaw %.1f cpitch %.1f %s %s\n",
		           id, best->classname ? best->classname : "-",
		           best->s.origin[0], best->s.origin[1], best->s.origin[2], best->velocity[2],
		           flm, fls, bolt, handz < 1.0e8f ? handz : 0.0f, low < 1.0e8f ? low : 0.0f,
		           mesh < 1.0e8f ? mesh : 0.0f, mloc, gapm, gaph, cam_nz, cam_sup, cam_limb, cam_plh, ray_t, ray_h,
		           ray_a, best->groundentity ? (int)(best->groundentity - g_edicts) : -1, ss, dz, dt,
		           placed ? eye[0] : 0.0f, placed ? eye[1] : 0.0f, placed ? eye[2] : 0.0f,
		           cyaw, cpitch, verdict, mode);
		if (!placed)
			return;
		/* Noclip looks from origin + 35. Put that eye on the clear spot. */
		VectorCopy(eye, origin);
		origin[2] -= 35.0f;
		ent->movetype = MOVETYPE_NOCLIP;
		ent->flags |= FL_NOTARGET | FL_GODMODE;
		ent->viewheight = 35;
		VectorCopy(origin, ent->s.origin);
		VectorClear(ent->velocity);
		ent->s.angles[YAW] = cyaw;
		ent->s.angles[PITCH] = 0;
		ent->s.angles[ROLL] = 0;
		ent->client->ps.pmove.delta_angles[YAW] = (short)ANGLE2SHORT(cyaw - ent->client->resp.cmd_angles[YAW]);
		ent->client->ps.pmove.delta_angles[PITCH] = (short)ANGLE2SHORT(cpitch - ent->client->resp.cmd_angles[PITCH]);
		ent->client->ps.pmove.delta_angles[ROLL] = (short)ANGLE2SHORT(0.0f - ent->client->resp.cmd_angles[ROLL]);
		ent->client->ps.viewangles[YAW] = cyaw;
		ent->client->ps.viewangles[PITCH] = cpitch;
		ent->client->ps.viewangles[ROLL] = 0;
		VectorCopy(ent->client->ps.viewangles, ent->client->v_angle);
		VectorClear(ent->client->kick_angles);
		for (int k = 0; k < 3; k++)
			ent->client->ps.pmove.origin[k] = (short)(origin[k] * 8.0f);
		gi.linkentity(ent);
	}
}

static void Cmd_Gimme_f(edict_t *ent)
{
	if (!CheatsOK(ent) || gi.argc() < 2)
		return;
	char *name = gi.argv(1);
	/* OpenMerc test aid: gimme <class> [ahead [right]] - that many units ahead (100) and to the right (0), level */
	float ahead = gi.argc() > 2 ? (float)atof(gi.argv(2)) : 100.0f, side = gi.argc() > 3 ? (float)atof(gi.argv(3)) : 0.0f;
	vec3_t fwd, right, flat = {0, ent->client->v_angle[YAW], 0};
	AngleVectors(gi.argc() > 2 ? flat : ent->client->v_angle, fwd, right, NULL);
	edict_t *e = G_Spawn();
	e->classname = G_CopyString(name);
	for (int i = 0; i < 3; i++)
		e->s.origin[i] = (float)(int)(ent->s.origin[i] + fwd[i] * ahead + right[i] * side);
	e->s.origin[2] = (float)(int)(e->s.origin[2] + 50);
	e->s.angles[YAW] = ent->client->v_angle[YAW] + (gi.argc() > 2 ? 180.0f : 0.0f); /* placed ones face the player */
	gi.cprintf(NULL, PRINT_HIGH, "Spawning : %s\n", name); // shown without developer, as in mode A (SPEC-006)
	memset(&st, 0, sizeof(st));
	ED_CallSpawn(e);
}

/* omhurt [damage] (test aid): the nearest living character hits the player with a pistol bullet through the damage
   function its own shots use (Dmg_Damage), e.g. to check the damage direction marker */
static void Cmd_OmHurt_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	edict_t *best = NULL;
	float bd = 1e9f;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e->client || !(e->svflags & SVF_MONSTER) || e->health <= 0)
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, ent->s.origin, d);
		if (VectorLength(d) < bd) {
			bd = VectorLength(d);
			best = e;
		}
	}
	if (!best)
		return;
	int dmg = gi.argc() > 1 ? atoi(gi.argv(1)) : 5;
	vec3_t dir, pt;
	VectorSubtract(ent->s.origin, best->s.origin, dir);
	VectorNormalize(dir);
	VectorMA(ent->s.origin, -8, dir, pt);
	pt[2] += 16;
	Dmg_Damage(ent, best, best, dir, pt, best->s.origin, dmg, 0, DMGF_PROJECTILE, MOD_PISTOL1);
	gi.dprintf("omhurt: %s #%d hit the player for %d, damage_dir 0x%x, health now %d (takedamage %d flags 0x%x protect %.1f)\n",
	           best->classname, (int)(best - g_edicts), dmg, ent->client ? ent->client->damage_dir : 0, ent->health,
	           (int)ent->takedamage, ent->flags, ent->client ? ent->client->spawnprotect_time - level.time : 0.0f);
}

/* omkill: the nearest living character (not the player) dies of a shot from the player's position (a test aid) */
static void Cmd_OmKill_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	/* omkill <classname prefix>: only that family (irq3a waves are m_irq, Hawk is m_x_mmerc) */
	const char *want = NULL;
	if (gi.argc() >= 2 && Q_stricmp(gi.argv(1), "dance"))
		want = gi.argv(1);
	edict_t *best = NULL;
	float bd = 1e9f;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e->client || !(e->svflags & SVF_MONSTER) || e->health <= 0)
			continue;
		if (want && (!e->classname || Q_strncasecmp(e->classname, want, (int)strlen(want))))
			continue;
		vec3_t d;
		VectorSubtract(e->s.origin, ent->s.origin, d);
		if (VectorLength(d) < bd) {
			bd = VectorLength(d);
			best = e;
		}
	}
	if (!best)
		return;
	vec3_t dir;
	VectorSubtract(best->s.origin, ent->s.origin, dir);
	VectorNormalize(dir);
	om_lastkill = (int)(best - g_edicts);
	gi.dprintf("omkill #%d %s at %.0f %.0f %.0f vel %.0f %.0f %.0f ground %d\n", om_lastkill, best->classname,
	           best->s.origin[0], best->s.origin[1], best->s.origin[2], best->velocity[0], best->velocity[1],
	           best->velocity[2], best->groundentity ? 1 : 0);
	/* omkill dance: as a burst of an automatic weapon (the 0x10000 "dance" death flag, SPEC-722) */
	int dflags = DAMAGE_NO_PROTECTION;
	if (gi.argc() >= 2 && !Q_stricmp(gi.argv(1), "dance"))
		dflags |= 0x10000;
	T_Damage(best, ent, ent, dir, best->s.origin, ent->s.origin, best->health + 20, 0, dflags, MOD_SUICIDE);
}

/* omaudit (test aid): does every enemy of the level react to the player? Puts the player in front of each one in
   turn and prints one OMAUDIT line per enemy (ai/ai_audit.cpp, docs/impl/AIAUDIT.md). The level is used up afterwards */
static void Cmd_OmAudit_f(edict_t *ent)
{
	if (!CheatsOK(ent))
		return;
	om_ai::AuditStart(ent);
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
	else if (Q_stricmp(cmd, "omfreecam") == 0)
		Cmd_OmFreecam_f(ent);
	else if (Q_stricmp(cmd, "omhurt") == 0)
		Cmd_OmHurt_f(ent);
	else if (Q_stricmp(cmd, "ominv") == 0)
		Cmd_OmInv_f(ent);
	else if (Q_stricmp(cmd, "omswap") == 0) {
		if (CheatsOK(ent))
			WeaponSwap_Debug(ent); /* openMERC weapon swap: why a weapon is offered or not */
	}
	else if (Q_stricmp(cmd, "omnear") == 0)
		Cmd_OmNear_f(ent);
	else if (Q_stricmp(cmd, "ombot") == 0)
		Cmd_OmBot_f(ent);
	else if (Q_stricmp(cmd, "omaim") == 0)
		Cmd_OmAim_f(ent);
	else if (Q_stricmp(cmd, "gimme") == 0)
		Cmd_Gimme_f(ent);
	else if (Q_stricmp(cmd, "omcorpse") == 0)
		Cmd_OmCorpse_f(ent);
	else if (Q_stricmp(cmd, "omcorpsecam") == 0)
		Cmd_OmCorpseCam_f(ent);
	else if (Q_stricmp(cmd, "omfire") == 0)
		Cmd_OmFire_f(ent);
	else if (Q_stricmp(cmd, "omboxents") == 0)
		Cmd_OmBoxEnts_f(ent);
	else if (Q_stricmp(cmd, "omusetarget") == 0)
		Cmd_OmUseTarget_f(ent);
	else if (Q_stricmp(cmd, "omuse") == 0)
		Cmd_OmUse_f(ent);
	else if (Q_stricmp(cmd, "defaultweapons") == 0) {
		if (CheatsOK(ent))
			GM_DefaultWeapons(ent); /* G9: SPEC-790 the level's default weapons */
	}
	else if (Q_stricmp(cmd, "omaudit") == 0)
		Cmd_OmAudit_f(ent);
	else if (Q_stricmp(cmd, "omscriptrun") == 0) {
		if (CheatsOK(ent))
			om_ai::ScriptRunStart(ent); /* test aid: run every script of the level that has not run (ai_scriptaudit.cpp) */
	}
	else if (Q_stricmp(cmd, "omkill") == 0)
		Cmd_OmKill_f(ent);
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
