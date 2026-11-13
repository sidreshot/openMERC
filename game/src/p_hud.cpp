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
 * OpenMerc: rewritten for the SoF 1.07f game module (G18) on the structure of Quake II's p_hud.c:
 * intermission, the string-package scoreboard layout and the SoF HUD stats.
 * Implements: SPEC-780 3, 19, 24
 */

#include "g_local.h"
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */
#include "ai/ai_hooks.h" /* omaudit */

int SP_Id(const char *package, const char *entry);

void MoveClientToIntermission(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (deathmatch->value)
		c->showscores = true;
	VectorCopy(level.intermission_origin, ent->s.origin);
	for (int i = 0; i < 3; i++)
		c->ps.pmove.origin[i] = (short)(level.intermission_origin[i] * 8);
	VectorCopy(level.intermission_angle, c->ps.viewangles);
	c->ps.pmove.pm_type = c->resp.spectator ? PM_INTERMISSION_SPEC : PM_FREEZE;
	c->ps.blend[3] = 0;
	ent->flags &= ~FL_INWATER;
	ent->viewheight = 0;
	ent->s.modelindex = 0;
	ent->s.effects = 0;
	ent->s.sound = 0;
	ent->solid = SOLID_NOT;
	ent->s.renderfx &= ~RF_GHOUL;
	c->ps.weaponinst = NULL;
	if (deathmatch->value)
		DeathmatchScoreboardMessage(ent, NULL);
}

void BeginIntermission(edict_t *targ)
{
	if (om_ai::AuditActive()) /* test aid (omaudit): a level does not end while its enemies are being checked */
		return;
	if (level.intermissiontime)
		return;

	for (int i = 0; i < maxclients->value; i++) {
		edict_t *client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		if (client->health <= 0)
			respawn(client);
	}

	level.intermissiontime = level.time;
	level.changemap = targ->map;

	if (!deathmatch->value && !strstr(level.changemap, "*")) {
		level.exitintermission = 1;
		if (game.maxclients >= 1 && g_edicts[1].client)
			g_edicts[1].client->ps.weaponinst = NULL;
		return;
	}
	level.exitintermission = 0;

	edict_t *ent = NULL;
	int count = 0;
	while ((ent = G_Find(ent, FOFS(classname), "info_player_intermission")) != NULL)
		count++;
	ent = NULL;
	if (count) {
		int skip = rand() & 3;
		if (count > 4)
			count = 4;
		skip %= count;
		ent = G_Find(NULL, FOFS(classname), "info_player_intermission");
		while (skip-- && ent)
			ent = G_Find(ent, FOFS(classname), "info_player_intermission");
	}
	if (!ent)
		ent = G_Find(NULL, FOFS(classname), "info_player_start");
	if (!ent)
		ent = G_Find(NULL, FOFS(classname), "info_player_deathmatch");
	if (ent) {
		VectorCopy(ent->s.origin, level.intermission_origin);
		VectorCopy(ent->s.angles, level.intermission_angle);
	}
	for (int i = 0; i < maxclients->value; i++) {
		edict_t *client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		MoveClientToIntermission(client);
	}
}

// SPEC-780 19: the scoreboard as string-package layout prints
void DeathmatchScoreboardMessage(edict_t *ent, edict_t *killer)
{
	GM_Scoreboard(ent, killer); /* G9: SPEC-790 B12, B13 */
	return;
	int reset = SP_Id("dm_generic", "LAYOUT_RESET");
	int row = SP_Id("dm_generic", "LAYOUT_SCOREBAORD_CLIENT");
	int spect = SP_Id("dm_generic", "LAYOUT_SCOREBAORD_SPECTATOR");
	if (reset < 0 || row < 0)
		return;
	gix.SP_Print(ent, reset);

	int sorted[MAX_CLIENTS], sortedscores[MAX_CLIENTS], total = 0;
	for (int i = 0; i < game.maxclients && i < MAX_CLIENTS; i++) {
		edict_t *cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || game.clients[i].resp.spectator)
			continue;
		int score = game.clients[i].resp.score;
		int j;
		for (j = 0; j < total; j++)
			if (score > sortedscores[j])
				break;
		for (int k = total; k > j; k--) {
			sorted[k] = sorted[k - 1];
			sortedscores[k] = sortedscores[k - 1];
		}
		sorted[j] = i;
		sortedscores[j] = score;
		total++;
	}
	int rows = 0;
	for (int i = 0; i < total && rows < 12; i++, rows++) {
		gclient_t *cl = &game.clients[sorted[i]];
		int x = rows < 6 ? 0 : 160;
		int y = 32 + 32 * (rows % 6);
		gix.SP_Print(ent, row, x, y, sorted[i], cl->resp.score, cl->ping,
		             (level.framenum - cl->resp.enterframe) / 600);
	}
	if (spect >= 0)
		for (int i = 0; i < game.maxclients && rows < 12; i++) {
			edict_t *cl_ent = g_edicts + 1 + i;
			if (!cl_ent->inuse || !game.clients[i].resp.spectator)
				continue;
			int x = rows < 6 ? 0 : 160;
			int y = 32 + 32 * (rows % 6);
			gix.SP_Print(ent, spect, x, y, i, game.clients[i].ping,
			             (level.framenum - game.clients[i].resp.enterframe) / 600);
			rows++;
		}
}

void Cmd_Score_f(edict_t *ent)
{
	ent->client->showhelp = false;
	if (!deathmatch->value)
		return;
	if (ent->client->showscores) {
		ent->client->showscores = false;
		return;
	}
	ent->client->showscores = true;
	DeathmatchScoreboardMessage(ent, ent->enemy);
}

void Cmd_Help_f(edict_t *ent)
{
	GM_Help(ent); /* G9: SPEC-790 B3 help / status layouts */
	return;
	if (level.intermissiontime)
		return;
	ent->client->showscores = false;
	if (ent->client->showhelp) {
		ent->client->showhelp = false;
		return;
	}
	ent->client->showhelp = true;
	ent->client->showhelp_time = level.time + 30;
}

/* OpenMerc: what the touch crosshair shows (engine cl_touch.c), in STAT_FLAGS bits SoF does not use:
   0x1000 a hostile living character under the crosshair (it turns red; the touch aim slows down over it),
   0x2000 the player's damage reached a character in the last 0.2 s (hit marker), 0x4000 it killed one in the last
   0.4 s (kill marker) */
namespace om_ai {
bool SameTeam(edict_t *a, edict_t *b);
}

static float om_hit[MAX_CLIENTS], om_kill[MAX_CLIENTS];

void OM_HitMark(edict_t *attacker, bool killed)
{
	int n = (int)(attacker - g_edicts) - 1;
	if (n < 0 || n >= MAX_CLIENTS)
		return;
	om_hit[n] = level.time;
	if (killed)
		om_kill[n] = level.time;
}

static int OM_TouchFlags(edict_t *ent)
{
	int flags = 0, n = (int)(ent - g_edicts) - 1;
	if (n >= 0 && n < MAX_CLIENTS) {
		if (om_hit[n] > 0 && level.time >= om_hit[n] && level.time - om_hit[n] < 0.2f)
			flags |= 0x2000;
		if (om_kill[n] > 0 && level.time >= om_kill[n] && level.time - om_kill[n] < 0.4f)
			flags |= 0x4000;
	}
	if (ent->health <= 0 || level.intermissiontime)
		return flags;
	vec3_t eye, fwd, end;
	VectorCopy(ent->s.origin, eye);
	eye[2] += ent->viewheight;
	AngleVectors(ent->client->v_angle, fwd, NULL, NULL);
	VectorMA(eye, 4096, fwd, end);
	trace_t tr = gi.trace(eye, NULL, NULL, end, ent, MASK_SHOT);
	edict_t *t = tr.ent;
	if (t && t != world && t->inuse && t->health > 0 && t->takedamage &&
	    (((t->svflags & SVF_MONSTER) && !om_ai::SameTeam(ent, t)) ||
	     (t->client && deathmatch->value && !OnSameTeam(ent, t))))
		flags |= 0x1000;
	return flags;
}

/* OpenMerc: the target of the touch auto aim (engine cl_touch.c touch_autoaim) in stats 14 - the hostile character
   nearest to the crosshair within om_autoaim_angle degrees (12; 0 off), in sight, alive, up to 3000 units, single
   player only. Bits 0-10: its entity number + 1, bits 11-15: the height of its chest above its origin, (z + 32) / 2.
   The client turns the view towards its interpolated position itself (a fresh angle each frame, no lag of the
   10 Hz server frames). */
static int OM_AimTarget(edict_t *ent)
{
	static cvar_t *angle;
	if (!angle)
		angle = gi.cvar("om_autoaim_angle", "12", CVAR_ARCHIVE);
	if (deathmatch->value || angle->value <= 0 || ent->health <= 0 || level.intermissiontime || !ent->client)
		return 0;
	vec3_t eye, fwd;
	VectorCopy(ent->s.origin, eye);
	eye[2] += ent->viewheight;
	AngleVectors(ent->client->v_angle, fwd, NULL, NULL);
	float deg = angle->value < 1.0f ? 1.0f : (angle->value > 45.0f ? 45.0f : angle->value);
	float cosmax = cosf(deg * (float)M_PI / 180.0f), best = cosmax;
	int found = 0;
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = &g_edicts[i];
		if (!e->inuse || e == ent || e->client || !(e->svflags & SVF_MONSTER) || e->health <= 0 || !e->takedamage ||
		    om_ai::SameTeam(ent, e))
			continue;
		float zoff = (e->mins[2] + e->maxs[2]) * 0.5f + 6; /* the chest, as om_aimassist */
		vec3_t pt, d;
		VectorCopy(e->s.origin, pt);
		pt[2] += zoff;
		VectorSubtract(pt, eye, d);
		float dist = VectorNormalize(d);
		if (dist < 32 || dist > 3000)
			continue;
		float c = DotProduct(d, fwd);
		if (c <= best)
			continue;
		trace_t tr = gi.trace(eye, NULL, NULL, pt, ent, MASK_SHOT);
		if (tr.fraction < 1.0f && tr.ent != e)
			continue;
		best = c;
		float zc = zoff < -32.0f ? -32.0f : (zoff > 30.0f ? 30.0f : zoff);
		int zq = (int)((zc + 32.0f) * 0.5f);
		found = (i + 1) | (zq << 11);
	}
	return found;
}

// SPEC-780 3
void G_SetStats(edict_t *ent)
{
	gclient_t *c = ent->client;
	PlayerInventory *inv = c->inv;
	short *st = c->ps.stats;

	st[STAT_HEALTH] = (short)ent->health;
	if (inv) {
		st[STAT_ITEMTYPE] = (short)inv->SelectedItemType();
		st[STAT_CLIP] = (short)inv->ClipRounds();
		st[STAT_AMMO] = (short)inv->ReserveAmmo();
		st[STAT_CLIPSIZE] = (short)inv->ClipSize();
		st[STAT_ARMOR] = (short)inv->Armor();
		st[STAT_WEAPON] = level.intermissiontime ? 0 : (short)inv->CurrentWeapon();
		st[STAT_ITEMCOUNT] = (short)inv->SelectedItemCount();
	}
	int flags = 0;
	if (inv && inv->ScopeActive())
		flags |= 0x0001;
	if (!deathmatch->value && level.maxdeadhostages > 0 && level.deadhostages >= level.maxdeadhostages)
		flags |= 0x0008;
	flags |= c->bot_gunsmoke & 0x30;
	if (level.countdown_expired)
		flags |= 0x0040;
	if (level.missionstatus == 3 && level.time < level.missionstatus_time + 5)
		flags |= 0x0400;
	if (c->flash_alpha > 0.5f)
		flags |= 0x0800;
	if (flags) {
		vec3_t end;
		VectorCopy(ent->s.origin, end);
		end[2] += 10000;
		trace_t tr = gi.trace(ent->s.origin, NULL, NULL, end, ent, MASK_SOLID);
		if (tr.surface && (tr.surface->flags & 4))
			flags |= 0x0004;
	}
	flags |= OM_TouchFlags(ent);
	st[STAT_FLAGS] = (short)flags;
	int layouts = 0;
	if (deathmatch->value)
		layouts |= 0x2;
	if (c->showscores || c->showhelp)
		layouts |= 0x1;
	st[STAT_LAYOUTS] = (short)layouts;
	st[STAT_FRAGS] = (short)c->resp.score;
	st[STAT_FORCEHUD] = level.forcehud ? 1 : 0;
	st[13] = 0;
	st[14] = (short)OM_AimTarget(ent); /* OpenMerc: the touch auto aim target */
	/* OpenMerc: the grenades carried, for the touch grenade button (stats 13 is unused by SoF), and above them the
	   weapon swap offer (g_weapswap.cpp) */
	int grenades = inv ? inv->ItemCount(IT_GRENADE) : 0;
	st[STAT_OM_GRENADES] = (short)((grenades < 0 ? 0 : grenades > 127 ? 127 : grenades) | WeaponSwap_Stat(ent));
	st[STAT_DAMAGEDIR] = (short)c->damage_dir;
}

void G_CheckChaseStats(edict_t *ent)
{
	for (int i = 1; i <= maxclients->value; i++) {
		gclient_t *cl = g_edicts[i].client;
		if (!g_edicts[i].inuse || cl->chase_target != ent)
			continue;
		memcpy(cl->ps.stats, ent->client->ps.stats, sizeof(cl->ps.stats));
		G_SetSpectatorStats(g_edicts + i);
	}
}

void G_SetSpectatorStats(edict_t *ent)
{
	gclient_t *cl = ent->client;
	if (!cl->chase_target)
		G_SetStats(ent);
	cl->ps.stats[STAT_LAYOUTS] = (short)((deathmatch->value ? 0x2 : 0) | (cl->showscores ? 0x1 : 0));
	if (cl->chase_target && cl->chase_target->inuse)
		cl->ps.spectated = (uint8_t)G_EdictNum(cl->chase_target);
}
