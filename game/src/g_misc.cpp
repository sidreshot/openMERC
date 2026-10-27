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
 * OpenMerc: changed for the SoF 1.07f game module (G18): only the Quake II misc entities SoF keeps
 * (area portal, path_corner/point_combat with the SoF path keys, info_*, light, func_wall, func_object,
 * target_character/string, func_clock); Quake II gibs/debris/models removed except an internal gib.
 * Implements: SPEC-760 B8
 */

#include "g_local.h"
#include "ai/ai_hooks.h" /* G2 */

void Use_Areaportal(edict_t *ent, edict_t *other, edict_t *activator)
{
	ent->count ^= 1;
	gi.SetAreaPortalState(ent->style, ent->count);
}

void SP_func_areaportal(edict_t *ent)
{
	ent->use = Use_Areaportal;
	ent->count = 0;
}

//=====================================================
// internal gibs (SPEC-760 B8.4): no Quake II models in SoF; a gib is an invisible tossed point

static void gib_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!self->groundentity)
		return;
	self->touch = NULL;
	if (plane) {
		gi.sound(self, CHAN_VOICE, gi.soundindex("misc/fhit3.wav"), 0.6f, ATTN_NORM, 0);
		vec3_t normal_angles, right;
		vectoangles(plane->normal, normal_angles);
		AngleVectors(normal_angles, NULL, right, NULL);
		vectoangles(right, self->s.angles);
		self->think = G_FreeEdict;
		self->nextthink = level.time + 8 + random() * 10;
	}
}

void ThrowGib(edict_t *self, const char *gibname, int damage, int type)
{
	edict_t *gib = G_Spawn();
	VectorCopy(self->s.origin, gib->s.origin);
	gib->solid = SOLID_NOT;
	gib->flags |= FL_NO_KNOCKBACK;
	gib->takedamage = DAMAGE_NO;
	gib->movetype = MOVETYPE_TOSS;
	gib->touch = gib_touch;
	gib->velocity[0] = 100.0f * crandom();
	gib->velocity[1] = 100.0f * crandom();
	gib->velocity[2] = 200.0f + 100.0f * random();
	gib->think = G_FreeEdict;
	gib->nextthink = level.time + 10 + random() * 10;
	gi.linkentity(gib);
}

void ThrowHead(edict_t *self, const char *gibname, int damage, int type) { ThrowGib(self, gibname, damage, type); }
void ThrowClientHead(edict_t *self, int damage) {}
void BecomeExplosion1(edict_t *self) { G_FreeEdict(self); }

//=====================================================
// path_corner, point_combat (SPEC-760 B8.1)

static void path_corner_think(edict_t *self) { self->nextthink = level.time + 0.5f; }

void path_corner_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other->movetarget != self)
		return;
	if (other->enemy)
		return;

	if (self->pathtarget) {
		char *savetarget = self->target;
		self->target = self->pathtarget;
		G_UseTargets(self, other);
		self->target = savetarget;
	}

	edict_t *next = self->target ? G_PickTarget(self->target) : NULL;
	if (next && (next->spawnflags & 1)) {
		vec3_t v;
		VectorCopy(next->s.origin, v);
		v[2] += next->mins[2];
		v[2] -= other->mins[2];
		VectorCopy(v, other->s.origin);
		next = G_PickTarget(next->target);
	}
	if (!self->target)
		next = NULL;
	other->goalentity = other->movetarget = next;
}

void SP_path_corner(edict_t *self)
{
	if (!self->targetname) {
		gi.dprintf("path_corner with no targetname at %s\n", vtos(self->s.origin));
		G_FreeEdict(self);
		return;
	}
	self->solid = SOLID_TRIGGER;
	self->touch = path_corner_touch;
	om_ai::PathCornerSpawn(self); /* G2: waitaction1..6 / moveaction (SPEC-760 B8.1) */
	VectorSet(self->mins, -8, -8, -8);
	VectorSet(self->maxs, 8, 8, 8);
	self->svflags |= SVF_NOCLIENT;
	self->think = path_corner_think;
	self->nextthink = level.time + 0.5f;
	gi.linkentity(self);
}

void point_combat_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	edict_t *activator;

	if (other->movetarget != self)
		return;

	if (self->target) {
		other->target = self->target;
		other->goalentity = other->movetarget = G_PickTarget(other->target);
		if (!other->goalentity) {
			gi.dprintf("%s at %s target %s does not exist\n", self->classname, vtos(self->s.origin), self->target);
			other->movetarget = self;
		}
		self->target = NULL;
	}

	if (other->movetarget == self) {
		other->target = NULL;
		other->movetarget = NULL;
		other->goalentity = other->enemy;
	}

	if (self->pathtarget) {
		char *savetarget = self->target;
		self->target = self->pathtarget;
		if (other->enemy && other->enemy->client)
			activator = other->enemy;
		else if (other->oldenemy && other->oldenemy->client)
			activator = other->oldenemy;
		else if (other->activator && other->activator->client)
			activator = other->activator;
		else
			activator = other;
		G_UseTargets(self, activator);
		self->target = savetarget;
	}
}

void SP_point_combat(edict_t *self)
{
	if (deathmatch->value) {
		G_FreeEdict(self);
		return;
	}
	self->solid = SOLID_TRIGGER;
	self->touch = point_combat_touch;
	om_ai::PathCornerSpawn(self); /* G2: waitaction1..6 / moveaction (SPEC-760 B8.1) */
	VectorSet(self->mins, -8, -8, -16);
	VectorSet(self->maxs, 8, 8, 16);
	self->svflags = SVF_NOCLIENT;
	gi.linkentity(self);
}

void SP_info_null(edict_t *self) { G_FreeEdict(self); }

void SP_info_notnull(edict_t *self)
{
	VectorCopy(self->s.origin, self->absmin);
	VectorCopy(self->s.origin, self->absmax);
}

// light (SPEC-760 B8.2)
#define START_OFF 1

static void light_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->spawnflags & START_OFF) {
		gi.configstring(CS_LIGHTS + self->style, "m");
		self->spawnflags &= ~START_OFF;
	} else {
		gi.configstring(CS_LIGHTS + self->style, "a");
		self->spawnflags |= START_OFF;
	}
}

void SP_light(edict_t *self)
{
	if (!self->targetname || deathmatch->value) {
		G_FreeEdict(self);
		return;
	}
	if (self->style >= 32) {
		self->use = light_use;
		if (self->spawnflags & START_OFF)
			gi.configstring(CS_LIGHTS + self->style, "a");
		else
			gi.configstring(CS_LIGHTS + self->style, "m");
	}
}

// func_wall (SPEC-760 B8.3: no diagnostic messages)
void func_wall_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->solid == SOLID_NOT) {
		self->solid = SOLID_BSP;
		self->svflags &= ~SVF_NOCLIENT;
		KillBox(self);
	} else {
		self->solid = SOLID_NOT;
		self->svflags |= SVF_NOCLIENT;
	}
	gi.linkentity(self);
	if (!(self->spawnflags & 2))
		self->use = NULL;
}

void SP_func_wall(edict_t *self)
{
	self->movetype = MOVETYPE_PUSH;
	gi.setmodel(self, self->model);

	if (self->spawnflags & 8)
		self->s.effects |= EF_ANIM_ALL;
	if (self->spawnflags & 16)
		self->s.effects |= EF_ANIM_ALLFAST;

	if ((self->spawnflags & 7) == 0) {
		self->solid = SOLID_BSP;
		gi.linkentity(self);
		return;
	}
	if (!(self->spawnflags & 1))
		self->spawnflags |= 1;
	if ((self->spawnflags & 4) && !(self->spawnflags & 2))
		self->spawnflags |= 2;

	self->use = func_wall_use;
	if (self->spawnflags & 4)
		self->solid = SOLID_BSP;
	else {
		self->solid = SOLID_NOT;
		self->svflags |= SVF_NOCLIENT;
	}
	gi.linkentity(self);
}

// func_object
void func_object_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!plane)
		return;
	if (plane->normal[2] < 1.0)
		return;
	if (other->takedamage == DAMAGE_NO)
		return;
	T_Damage(other, self, self, vec3_origin, self->s.origin, vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void func_object_release(edict_t *self)
{
	self->movetype = MOVETYPE_TOSS;
	self->touch = func_object_touch;
}

void func_object_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->solid = SOLID_BSP;
	self->svflags &= ~SVF_NOCLIENT;
	self->use = NULL;
	KillBox(self);
	func_object_release(self);
}

void SP_func_object(edict_t *self)
{
	gi.setmodel(self, self->model);
	self->mins[0] += 1;
	self->mins[1] += 1;
	self->mins[2] += 1;
	self->maxs[0] -= 1;
	self->maxs[1] -= 1;
	self->maxs[2] -= 1;
	if (!self->dmg)
		self->dmg = 100;
	if (self->spawnflags == 0) {
		self->solid = SOLID_BSP;
		self->movetype = MOVETYPE_PUSH;
		self->think = func_object_release;
		self->nextthink = level.time + 2 * FRAMETIME;
	} else {
		self->solid = SOLID_NOT;
		self->movetype = MOVETYPE_PUSH;
		self->use = func_object_use;
		self->svflags |= SVF_NOCLIENT;
	}
	if (self->spawnflags & 2)
		self->s.effects |= EF_ANIM_ALL;
	if (self->spawnflags & 4)
		self->s.effects |= EF_ANIM_ALLFAST;
	self->clipmask = MASK_MONSTERSOLID;
	gi.linkentity(self);
}

// target_character / target_string
void SP_target_character(edict_t *self)
{
	self->movetype = MOVETYPE_PUSH;
	gi.setmodel(self, self->model);
	self->solid = SOLID_BSP;
	self->s.frame = 12;
	gi.linkentity(self);
}

void target_string_use(edict_t *self, edict_t *other, edict_t *activator)
{
	int l = (int)strlen(self->message);
	for (edict_t *e = self->teammaster; e; e = e->teamchain) {
		if (!e->count)
			continue;
		int n = e->count - 1;
		if (n > l) {
			e->s.frame = 12;
			continue;
		}
		char c = self->message[n];
		if (c >= '0' && c <= '9')
			e->s.frame = c - '0';
		else if (c == '-')
			e->s.frame = 10;
		else if (c == ':')
			e->s.frame = 11;
		else
			e->s.frame = 12;
	}
}

void SP_target_string(edict_t *self)
{
	if (!self->message)
		self->message = (char *)"";
	self->use = target_string_use;
}

// func_clock
#define CLOCK_MESSAGE_SIZE 16

static void func_clock_reset(edict_t *self)
{
	self->activator = NULL;
	if (self->spawnflags & 1) {
		self->health = 0;
		self->wait = self->count;
	} else if (self->spawnflags & 2) {
		self->health = self->count;
		self->wait = 0;
	}
}

static void func_clock_format_countdown(edict_t *self)
{
	if (self->style == 0) {
		Com_sprintf(self->message, CLOCK_MESSAGE_SIZE, "%2i", self->health);
		return;
	}
	if (self->style == 1) {
		Com_sprintf(self->message, CLOCK_MESSAGE_SIZE, "%2i:%2i", self->health / 60, self->health % 60);
		if (self->message[3] == ' ')
			self->message[3] = '0';
		return;
	}
	if (self->style == 2) {
		Com_sprintf(self->message, CLOCK_MESSAGE_SIZE, "%2i:%2i:%2i", self->health / 3600,
		            (self->health - (self->health / 3600) * 3600) / 60, self->health % 60);
		if (self->message[3] == ' ')
			self->message[3] = '0';
		if (self->message[6] == ' ')
			self->message[6] = '0';
	}
}

void func_clock_think(edict_t *self)
{
	if (!self->enemy) {
		self->enemy = G_Find(NULL, FOFS(targetname), self->target);
		if (!self->enemy)
			return;
	}
	if (self->spawnflags & 1) {
		func_clock_format_countdown(self);
		self->health++;
	} else if (self->spawnflags & 2) {
		func_clock_format_countdown(self);
		self->health--;
	} else {
		time_t gmtime;
		time(&gmtime);
		struct tm *ltime = localtime(&gmtime);
		Com_sprintf(self->message, CLOCK_MESSAGE_SIZE, "%2i:%2i:%2i", ltime->tm_hour, ltime->tm_min, ltime->tm_sec);
		if (self->message[3] == ' ')
			self->message[3] = '0';
		if (self->message[6] == ' ')
			self->message[6] = '0';
	}
	self->enemy->message = self->message;
	if (self->enemy->use)
		self->enemy->use(self->enemy, self, self);

	if (((self->spawnflags & 1) && (self->health > self->wait)) ||
	    ((self->spawnflags & 2) && (self->health < self->wait))) {
		if (self->pathtarget) {
			char *savetarget = self->target;
			char *savemessage = self->message;
			self->target = self->pathtarget;
			self->message = NULL;
			G_UseTargets(self, self->activator);
			self->target = savetarget;
			self->message = savemessage;
		}
		if (!(self->spawnflags & 8))
			return;
		func_clock_reset(self);
		if (self->spawnflags & 4)
			return;
	}
	self->nextthink = level.time + 1;
}

void func_clock_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!(self->spawnflags & 8))
		self->use = NULL;
	if (self->activator)
		return;
	self->activator = activator;
	self->think(self);
}

void SP_func_clock(edict_t *self)
{
	if (!self->target) {
		gi.dprintf("%s with no target at %s\n", self->classname, vtos(self->s.origin));
		G_FreeEdict(self);
		return;
	}
	if ((self->spawnflags & 2) && (!self->count)) {
		gi.dprintf("%s with no count at %s\n", self->classname, vtos(self->s.origin));
		G_FreeEdict(self);
		return;
	}
	if ((self->spawnflags & 1) && (!self->count))
		self->count = 60 * 60;
	func_clock_reset(self);
	self->message = (char *)gi.TagMalloc(CLOCK_MESSAGE_SIZE, TAG_LEVEL);
	self->think = func_clock_think;
	if (self->spawnflags & 4)
		self->use = func_clock_use;
	else
		self->nextthink = level.time + 1;
}
