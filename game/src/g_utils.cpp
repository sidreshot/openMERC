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
 * OpenMerc: changed for the SoF 1.07f game module (G18): G_Find with length, area-based findradius and
 * radius lists, sp_message and talk sound in G_UseTargets, SoF vectoyaw/vectoangles, entity
 * allocation/freeing with the SoF defaults, use-key target selection.
 * Implements: SPEC-760 B2, B3
 */
// g_utils.c -- misc utility functions for game module

#include "g_local.h"
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */

void G_ProjectSource(vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	result[0] = point[0] + forward[0] * distance[0] + right[0] * distance[1];
	result[1] = point[1] + forward[1] * distance[0] + right[1] * distance[1];
	result[2] = point[2] + forward[2] * distance[0] + right[2] * distance[1] + distance[2];
}

/*
=============
G_Find (SPEC-760 B3.1: optional compare length)
=============
*/
edict_t *G_Find(edict_t *from, int fieldofs, const char *match, int len)
{
	if (!from)
		from = g_edicts;
	else
		from++;
	for (; from < &g_edicts[globals.num_edicts]; from++) {
		if (!from->inuse)
			continue;
		char *s = *(char **)((byte *)from + fieldofs);
		if (!s)
			continue;
		if (len > 0 ? !Q_strncasecmp(s, (char *)match, len) : !Q_stricmp(s, (char *)match))
			return from;
	}
	return NULL;
}

// SPEC-760 B3.3: entities linked in the given areas (1 solid, 2 triggers, 3 both) whose centre is
// within rad of org; at most 1024
int G_RadiusList(const vec3_t org, float rad, edict_t **list, int areas)
{
	vec3_t mins, maxs;
	edict_t *touch[1024];
	int n = 0;
	for (int i = 0; i < 3; i++) {
		mins[i] = org[i] - rad;
		maxs[i] = org[i] + rad;
	}
	for (int a = 1; a <= 2; a++) {
		if (!(areas & a))
			continue;
		int num = gi.BoxEdicts(mins, maxs, touch, 1024 - n, a);
		for (int i = 0; i < num && n < 1024; i++) {
			edict_t *e = touch[i];
			vec3_t eorg;
			for (int j = 0; j < 3; j++)
				eorg[j] = org[j] - (e->s.origin[j] + (e->mins[j] + e->maxs[j]) * 0.5f);
			if (VectorLength(eorg) > rad)
				continue;
			list[n++] = e;
		}
	}
	return n;
}

/*
=================
findradius (SPEC-760 B3.2)
=================
*/
edict_t *findradius(edict_t *from, const vec3_t org, float rad, int area)
{
	static edict_t *found[1024];
	static int nfound, cursor;
	if (!from) {
		vec3_t mins, maxs;
		edict_t *touch[1024];
		for (int i = 0; i < 3; i++) {
			mins[i] = org[i] - rad;
			maxs[i] = org[i] + rad;
		}
		int num = gi.BoxEdicts(mins, maxs, touch, 1024, area);
		nfound = 0;
		for (int i = 0; i < num; i++) {
			edict_t *e = touch[i];
			if (area == 1) {
				if (e->solid == SOLID_NOT)
					continue;
				vec3_t eorg;
				for (int j = 0; j < 3; j++)
					eorg[j] = org[j] - (e->s.origin[j] + (e->mins[j] + e->maxs[j]) * 0.5f);
				if (VectorLength(eorg) > rad)
					continue;
			}
			found[nfound++] = e;
		}
		// keep entity-number order like an array walk
		for (int i = 1; i < nfound; i++)
			for (int j = i; j > 0 && found[j - 1] > found[j]; j--) {
				edict_t *t = found[j];
				found[j] = found[j - 1];
				found[j - 1] = t;
			}
		cursor = 0;
	}
	while (cursor < nfound) {
		edict_t *e = found[cursor++];
		if (e->inuse)
			return e;
	}
	return NULL;
}

#define MAXCHOICES 8

edict_t *G_PickTarget(const char *targetname)
{
	edict_t *ent = NULL;
	int num_choices = 0;
	edict_t *choice[MAXCHOICES];

	if (!targetname) {
		gi.dprintf("G_PickTarget called with NULL targetname\n");
		return NULL;
	}
	while (1) {
		ent = G_Find(ent, FOFS(targetname), targetname);
		if (!ent)
			break;
		choice[num_choices++] = ent;
		if (num_choices == MAXCHOICES)
			break;
	}
	if (!num_choices) {
		gi.dprintf("G_PickTarget: target %s not found\n", targetname);
		return NULL;
	}
	return choice[rand() % num_choices];
}

static void Think_Delay(edict_t *ent)
{
	G_UseTargets(ent, ent->activator);
	G_FreeEdict(ent);
}

/*
==============================
G_UseTargets (SPEC-760 B3.5)
==============================
*/
void G_UseTargets(edict_t *ent, edict_t *activator)
{
	edict_t *t;

	if (ent->delay) {
		t = G_Spawn();
		t->classname = (char *)"DelayedUse";
		t->nextthink = level.time + ent->delay;
		t->think = Think_Delay;
		t->activator = activator;
		if (!activator)
			gi.dprintf("Think_Delay with no activator\n");
		t->message = ent->message;
		t->target = ent->target;
		t->killtarget = ent->killtarget;
		t->killfacing = ent->killfacing;
		return;
	}

	if ((ent->message || ent->sp_message) && activator && !(activator->svflags & SVF_MONSTER)) {
		if (ent->sp_message)
			gix.SP_Print(activator, ent->sp_message);
		else
			gi.centerprintf(activator, "%s", ent->message);
		if (ent->noise_index)
			gi.sound(activator, CHAN_AUTO, ent->noise_index, 0.6f, ATTN_NORM, 0);
		else
			gi.sound(activator, CHAN_AUTO, gi.soundindex("Misc/Talk.wav"), 0.6f, ATTN_NORM, 0);
	}

	if (ent->killtarget) {
		t = NULL;
		while ((t = G_Find(t, FOFS(targetname), ent->killtarget))) {
			G_FreeEdict(t);
			if (!ent->inuse) {
				gi.dprintf("entity was removed while using killtargets\n");
				return;
			}
		}
	}

	if (ent->target) {
		t = NULL;
		while ((t = G_Find(t, FOFS(targetname), ent->target))) {
			if (!Q_stricmp(t->classname, "func_areaportal") &&
			    (!Q_stricmp(ent->classname, "func_door") || !Q_stricmp(ent->classname, "func_door_rotating")))
				continue;
			if (t == ent) {
				gi.dprintf("WARNING: Entity used itself.\n");
			} else {
				if (t->use)
					t->use(t, ent, activator);
			}
			if (!ent->inuse) {
				gi.dprintf("entity was removed while using targets\n");
				return;
			}
		}
	}
}

float *tv(float x, float y, float z)
{
	static int index;
	static vec3_t vecs[8];
	float *v = vecs[index];
	index = (index + 1) & 7;
	v[0] = x;
	v[1] = y;
	v[2] = z;
	return v;
}

char *vtos(const vec3_t v)
{
	static int index;
	static char str[8][32];
	char *s = str[index];
	index = (index + 1) & 7;
	Com_sprintf(s, 32, "(%i %i %i)", (int)v[0], (int)v[1], (int)v[2]);
	return s;
}

vec3_t VEC_UP = {0, -1, 0};
vec3_t MOVEDIR_UP = {0, 0, 1};
vec3_t VEC_DOWN = {0, -2, 0};
vec3_t MOVEDIR_DOWN = {0, 0, -1};

void G_SetMovedir(vec3_t angles, vec3_t movedir)
{
	if (VectorCompare(angles, VEC_UP))
		VectorCopy(MOVEDIR_UP, movedir);
	else if (VectorCompare(angles, VEC_DOWN))
		VectorCopy(MOVEDIR_DOWN, movedir);
	else
		AngleVectors(angles, movedir, NULL, NULL);
	VectorClear(angles);
}

// SPEC-760 B3.6
float vectoyaw(const vec3_t vec)
{
	if (vec[1] == 0 && vec[0] == 0)
		return 0;
	float yaw = (float)(int)(atan2(vec[1], vec[0]) * 180 / M_PI);
	if (yaw < 0)
		yaw += 360;
	return yaw;
}

void vectoangles(const vec3_t vec, vec3_t angles)
{
	float forward = sqrt(vec[0] * vec[0] + vec[1] * vec[1]);
	angles[YAW] = (float)(atan2(vec[1], vec[0]) * 180 / M_PI);
	angles[PITCH] = (float)(-atan2(vec[2], forward) * 180 / M_PI);
	angles[ROLL] = 0;
}

char *G_CopyString(const char *in)
{
	char *out = (char *)gi.TagMalloc((int)strlen(in) + 1, TAG_LEVEL);
	strcpy(out, in);
	return out;
}

// SPEC-760 B2 allocation defaults
void G_InitEdict(edict_t *e)
{
	e->inuse = true;
	e->classname = (char *)"noclass";
	e->gravity = 1.0;
	e->s.number = (int)(e - g_edicts);
	e->s.modelindex2 = -1;
	VectorSet(e->engine_a, 1000, 1000, 1000);
	VectorSet(e->engine_b, -1000, -1000, -1000);
	VectorSet(e->engine_c, 10000, 10000, 10000);
	e->engine_d = -1;
	e->count = 0;
	e->stopspeed = 0;
	e->friction = 1.0f;
	e->airfriction = 1.0f;
	e->buoyancy = 0;
	e->elasticity = 0.5f;
	e->lastusetime = level.time - 10;
}

edict_t *G_Spawn(void)
{
	int i;
	edict_t *e = &g_edicts[game.maxclients + 1];
	for (i = game.maxclients + 1; i < globals.num_edicts; i++, e++) {
		if (!e->inuse && (e->freetime < 2 || level.time - e->freetime > 0.5)) {
			G_InitEdict(e);
			return e;
		}
	}
	if (i == game.maxentities)
		gi.error("ED_Alloc: no free edicts");
	globals.num_edicts++;
	G_InitEdict(e);
	return e;
}

// SPEC-780 §15 / SPEC-AMEND-005 G2.8: every entity has one helper link; an owner's helpers form a circular list
// owner → helper 1 → … → helper n → owner, a new helper is appended at the end (not Quake II teamchain/chain)
// SPEC-780 15 / SPEC-AMEND-005 G2.8: every entity has one helper link; an owner's helpers form a circular list
// owner -> helper 1 -> ... -> helper n -> owner, a new helper is appended at the end (not Quake II teamchain/chain)
static bool helper_ptr_ok(const edict_t *p)
{
	if (!p || !g_edicts)
		return false;
	int i = (int)(p - g_edicts);
	return i >= 0 && i < game.maxentities && &g_edicts[i] == p;
}

void G_HelperAppend(edict_t *owner, edict_t *helper)
{
	if (!helper_ptr_ok(owner) || !helper_ptr_ok(helper) || owner == helper)
		return;
	G_HelperUnlink(helper);
	edict_t *last = owner;
	for (int guard = 0; helper_ptr_ok(last->helperlink) && last->helperlink != owner && guard < MAX_EDICTS; guard++)
		last = last->helperlink;
	last->helperlink = helper;
	helper->helperlink = owner;
}

void G_HelperUnlink(edict_t *e)
{
	if (!helper_ptr_ok(e))
		return;
	if (!helper_ptr_ok(e->helperlink)) {
		e->helperlink = NULL;
		return;
	}
	edict_t *p = e->helperlink;
	for (int guard = 0; helper_ptr_ok(p->helperlink) && p->helperlink != e && guard < MAX_EDICTS; guard++)
		p = p->helperlink;
	if (p->helperlink == e)
		p->helperlink = e->helperlink == p ? NULL : e->helperlink;
	e->helperlink = NULL;
}

edict_t *G_HelperResolve(edict_t *e)
{
	edict_t *p = e;
	for (int guard = 0; p && (p->svflags & SVF_PLAYERPROXY) && guard < 64; guard++)
		p = p->helperlink;
	return p && !(p->svflags & SVF_PLAYERPROXY) ? p : e;
}

static void G_FreeCommon(edict_t *ed)
{
	G_HelperUnlink(ed);
	if (ed->flags & FL_ITEMLIST)
		ItemRemoveFromList(ed);
	{ extern void Body_Destroy(edict_t *); Body_Destroy(ed); } /* G3: the entity's body */
	GG_EntityFreed(ed); /* G9: SPEC-791 Interface 4, B6 */
	GhoulRemove(ed);
	gi.unlinkentity(ed);
}

void G_FreeEdict(edict_t *ed)
{
	G_FreeCommon(ed);
	if ((ed - g_edicts) <= (game.maxclients + BODY_QUEUE_SIZE)) {
		ed->s.renderfx = 0;
		return;
	}
	memset(ed, 0, sizeof(*ed));
	ed->classname = (char *)"freed";
	ed->freetime = level.time;
	ed->inuse = false;
}

// SPEC-760 B2: variant whose record is immediately reusable
void G_FreeEdictNow(edict_t *ed)
{
	G_FreeEdict(ed);
	if (!ed->inuse)
		ed->freetime = 0;
}

/*
============
G_TouchTriggers
============
*/
void G_TouchTriggers(edict_t *ent)
{
	edict_t *touch[1024], *hit;

	if ((ent->client || (ent->svflags & SVF_MONSTER)) && (ent->health <= 0))
		return;

	int num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, 1024, 2);
	for (int i = 0; i < num; i++) {
		hit = touch[i];
		if (!hit->inuse)
			continue;
		if (!hit->touch)
			continue;
		hit->touch(hit, ent, NULL, NULL);
	}
}

void G_TouchSolids(edict_t *ent)
{
	edict_t *touch[1024], *hit;
	int num = gi.BoxEdicts(ent->absmin, ent->absmax, touch, 1024, 1);
	for (int i = 0; i < num; i++) {
		hit = touch[i];
		if (!hit->inuse)
			continue;
		if (ent->touch)
			ent->touch(hit, ent, NULL, NULL);
		if (!ent->inuse)
			break;
	}
}

/*
=================
KillBox
=================
*/
qboolean KillBox(edict_t *ent)
{
	trace_t tr;
	while (1) {
		tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, NULL, MASK_PLAYERSOLID);
		if (!tr.ent || tr.ent == world)
			break;
		T_Damage(tr.ent, ent, ent, vec3_origin, ent->s.origin, vec3_origin, 100000, 0, DAMAGE_NO_PROTECTION,
		         MOD_TELEFRAG);
		if (tr.ent->solid)
			return false;
	}
	return true;
}

qboolean G_IsClient(edict_t *e)
{
	int n = (int)(e - g_edicts);
	return e && n >= 1 && n <= game.maxclients;
}

edict_t *G_FirstPlayer(void)
{
	edict_t *e = g_edicts + 1;
	if (game.maxclients >= 1 && e->inuse && e->client)
		return e;
	return NULL;
}

/*
=================
G_UseKey (SPEC-760 B3.8, SPEC-780 5 "use key")
=================
*/
void G_UseKey(edict_t *player)
{
	vec3_t eye, mins, maxs, fwd, flat;
	edict_t *list[1024];

	VectorCopy(player->s.origin, eye);
	eye[2] += player->viewheight;

	// 1: trigger-class entity overlapping a cube of half the player's width around the eye
	for (int i = 0; i < 3; i++) {
		mins[i] = eye[i] - 16;
		maxs[i] = eye[i] + 16;
	}
	int n = gi.BoxEdicts(mins, maxs, list, 1024, 2);
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (e->inuse && e->plUse && e->classname && strstr(e->classname, "trigger")) {
			e->plUse(e, player, player);
			return;
		}
	}

	// 2: solid entity with a use-key handler within 80 units, best horizontal facing
	AngleVectors(player->client ? player->client->v_angle : player->s.angles, fwd, NULL, NULL);
	flat[0] = fwd[0];
	flat[1] = fwd[1];
	flat[2] = 0;
	VectorNormalize(flat);
	for (int i = 0; i < 3; i++) {
		mins[i] = eye[i] - 80;
		maxs[i] = eye[i] + 80;
	}
	n = gi.BoxEdicts(mins, maxs, list, 1024, 1);
	edict_t *best = NULL;
	float bestdot = 0.2f;
	for (int i = 0; i < n; i++) {
		edict_t *e = list[i];
		if (!e->inuse || !e->plUse || e == player)
			continue;
		vec3_t c, d;
		for (int j = 0; j < 3; j++)
			c[j] = e->s.origin[j] + (e->mins[j] + e->maxs[j]) * 0.5f;
		if (e->solid == SOLID_BSP) // brush entities: centre of their absolute box
			for (int j = 0; j < 3; j++)
				c[j] = (e->absmin[j] + e->absmax[j]) * 0.5f;
		VectorSubtract(c, eye, d);
		if (VectorLength(d) > 80)
			continue;
		d[2] = 0;
		VectorNormalize(d);
		float dot = DotProduct(d, flat);
		if (dot < bestdot)
			continue;
		trace_t tr = gi.trace(eye, NULL, NULL, c, player, MASK_PLAYERSOLID);
		if (tr.fraction < 1.0f && tr.ent != e)
			continue;
		best = e;
		bestdot = dot;
	}
	if (best) {
		best->plUse(best, player, player);
		return;
	}

	// 3: 75-unit trace along the view
	vec3_t end;
	VectorMA(eye, 75, fwd, end);
	trace_t tr = gi.trace(eye, NULL, NULL, end, player, MASK_PLAYERSOLID);
	if (tr.ent && tr.ent->inuse && tr.ent->plUse)
		tr.ent->plUse(tr.ent, player, player);
}
