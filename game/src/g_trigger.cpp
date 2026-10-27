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
 * OpenMerc: changed for the SoF 1.07f game module (G18): SoF rules of the Quake II triggers (lean
 * proxies, use-key triggers, string-package counter messages, toggled push, hurt rules, inert key
 * trigger) and the new triggers of SPEC-761 5.
 * Implements: SPEC-760 B6, SPEC-761 5
 */

#include "g_local.h"
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */

void InitTrigger(edict_t *self)
{
	if (!VectorCompare(self->s.angles, vec3_origin))
		G_SetMovedir(self->s.angles, self->movedir);
	self->solid = SOLID_TRIGGER;
	self->movetype = MOVETYPE_NONE;
	if (self->model)
		gi.setmodel(self, self->model);
	self->svflags = SVF_NOCLIENT;
}

void multi_wait(edict_t *ent) { ent->nextthink = 0; }

void multi_trigger(edict_t *ent)
{
	if (ent->nextthink)
		return;
	ent->lastusetime = level.time;
	G_UseTargets(ent, ent->activator);
	if (ent->wait > 0) {
		ent->think = multi_wait;
		ent->nextthink = level.time + ent->wait;
	} else {
		ent->touch = NULL;
		ent->plUse = NULL;
		ent->nextthink = level.time + FRAMETIME;
		ent->think = G_FreeEdict;
	}
}

void Use_Multi(edict_t *ent, edict_t *other, edict_t *activator)
{
	ent->activator = activator;
	multi_trigger(ent);
}

void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other->client || (other->flags & FL_LEANPROXY)) {
		if (self->spawnflags & 2)
			return;
	} else if (other->svflags & SVF_MONSTER) {
		if (!(self->spawnflags & 1))
			return;
	} else
		return;

	if (!VectorCompare(self->movedir, vec3_origin)) {
		vec3_t forward;
		AngleVectors(other->s.angles, forward, NULL, NULL);
		if (_DotProduct(forward, self->movedir) < 0)
			return;
	}

	self->activator = (other->flags & FL_LEANPROXY) && other->owner ? other->owner : other;
	multi_trigger(self);
}

static void UseKey_Multi(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->solid == SOLID_NOT)
		return;
	self->activator = activator;
	multi_trigger(self);
}

void trigger_enable(edict_t *self, edict_t *other, edict_t *activator)
{
	self->solid = SOLID_TRIGGER;
	self->use = Use_Multi;
	gi.linkentity(self);
}

void SP_trigger_multiple(edict_t *ent)
{
	if (ent->sounds >= 1 && ent->sounds <= 3)
		ent->noise_index = gi.soundindex("misc/talk.wav");

	if (!ent->wait)
		ent->wait = 0.2;
	if (ent->spawnflags & 8)
		ent->plUse = UseKey_Multi;
	else
		ent->touch = Touch_Multi;
	ent->movetype = MOVETYPE_NONE;
	ent->svflags |= SVF_NOCLIENT;

	if (ent->spawnflags & 4) {
		ent->solid = SOLID_NOT;
		ent->use = trigger_enable;
	} else {
		ent->solid = SOLID_TRIGGER;
		ent->use = Use_Multi;
	}

	if (!VectorCompare(ent->s.angles, vec3_origin))
		G_SetMovedir(ent->s.angles, ent->movedir);

	gi.setmodel(ent, ent->model);
	gi.linkentity(ent);
}

void SP_trigger_once(edict_t *ent)
{
	ent->wait = -1;
	SP_trigger_multiple(ent);
}

void trigger_relay_use(edict_t *self, edict_t *other, edict_t *activator) { G_UseTargets(self, activator); }

void SP_trigger_relay(edict_t *self) { self->use = trigger_relay_use; }

// trigger_key: validated and precached, using it does nothing (SPEC-760 B6)
void SP_trigger_key(edict_t *self)
{
	if (!st.item) {
		gi.dprintf("no key item for trigger_key at %s\n", vtos(self->s.origin));
		return;
	}
	if (!self->target) {
		gi.dprintf("%s at %s has no target\n", self->classname, vtos(self->s.origin));
		return;
	}
	gi.soundindex("misc/keytry.wav");
	gi.soundindex("misc/keyuse.wav");
}

// trigger_counter
void trigger_counter_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->count == 0)
		return;
	self->count--;
	if (self->count) {
		if (!(self->spawnflags & 1) && activator) {
			gix.SP_Print(activator, 0x060B, self->count);
			gi.sound(activator, CHAN_AUTO, gi.soundindex("Misc/Talk.wav"), 0.6f, ATTN_NORM, 0);
		}
		return;
	}
	if (!(self->spawnflags & 1) && activator) {
		gix.SP_Print(activator, 0x060C);
		gi.sound(activator, CHAN_AUTO, gi.soundindex("Misc/Talk.wav"), 0.6f, ATTN_NORM, 0);
	}
	self->activator = activator;
	multi_trigger(self);
}

void SP_trigger_counter(edict_t *self)
{
	self->wait = -1;
	if (!self->count)
		self->count = 2;
	self->use = trigger_counter_use;
}

void SP_trigger_always(edict_t *ent)
{
	if (ent->delay < 0.2)
		ent->delay = 0.2;
	G_UseTargets(ent, ent);
}

// trigger_push
#define PUSH_ONCE 1
#define PUSH_TOGGLE 2

void trigger_push_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other->targetname && !strcmp(other->targetname, "sib2_heli"))
		return;
	if (strcmp(other->classname, "grenade") == 0) {
		VectorScale(self->movedir, self->speed * 10, other->velocity);
	} else if (other->health > 0) {
		VectorScale(self->movedir, self->speed * 10, other->velocity);
		if (other->client)
			VectorCopy(other->velocity, other->client->oldvelocity);
	}
	if (self->spawnflags & PUSH_ONCE)
		G_FreeEdict(self);
}

static void trigger_push_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->solid = self->solid == SOLID_NOT ? SOLID_TRIGGER : SOLID_NOT;
	gi.linkentity(self);
}

void SP_trigger_push(edict_t *self)
{
	InitTrigger(self);
	self->touch = trigger_push_touch;
	if (!self->speed)
		self->speed = 1000;
	if (self->spawnflags & PUSH_TOGGLE) {
		self->solid = SOLID_NOT;
		self->use = trigger_push_use;
	}
	gi.linkentity(self);
}

// trigger_hurt
void hurt_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->solid = self->solid == SOLID_NOT ? SOLID_TRIGGER : SOLID_NOT;
	gi.linkentity(self);
}

void hurt_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!other->takedamage)
		return;
	if ((self->spawnflags & 32) && !other->client)
		return;

	if (self->spawnflags & 16) {
		if (self->timestamp > level.time && self->count != level.framenum)
			return;
		if (self->count != level.framenum) {
			self->count = level.framenum;
			self->timestamp = level.time + 1;
		}
	}

	int dflags = 0;
	if (self->spawnflags & 8)
		dflags |= DAMAGE_NO_PROTECTION;
	if (self->spawnflags & 64)
		dflags |= DAMAGE_NO_ARMOR;
	T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, self->dmg, self->dmg, dflags,
	         MOD_TRIGGER_HURT);
}

void SP_trigger_hurt(edict_t *self)
{
	InitTrigger(self);
	self->noise_index = 0;
	self->touch = hurt_touch;
	if (!self->dmg)
		self->dmg = 5;
	self->solid = (self->spawnflags & 1) ? SOLID_NOT : SOLID_TRIGGER;
	self->use = hurt_use;
	self->count = -1;
	gi.linkentity(self);
}

// trigger_gravity
void trigger_gravity_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	other->gravity = self->gravity;
}

void SP_trigger_gravity(edict_t *self)
{
	if (st.gravity == 0) {
		gi.dprintf("trigger_gravity without gravity set at %s\n", vtos(self->s.origin));
		G_FreeEdict(self);
		return;
	}
	InitTrigger(self);
	self->gravity = atoi(st.gravity);
	self->touch = trigger_gravity_touch;
}

// trigger_monsterjump
void trigger_monsterjump_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other->flags & (FL_FLY | FL_SWIM))
		return;
	if (other->svflags & SVF_DEADMONSTER)
		return;
	if (!(other->svflags & SVF_MONSTER))
		return;
	other->velocity[0] = self->movedir[0] * self->speed;
	other->velocity[1] = self->movedir[1] * self->speed;
	if (!other->groundentity)
		return;
	other->groundentity = NULL;
	other->velocity[2] = self->movedir[2];
}

void SP_trigger_monsterjump(edict_t *self)
{
	if (!self->speed)
		self->speed = 200;
	if (!st.height)
		st.height = 200;
	if (self->s.angles[YAW] == 0)
		self->s.angles[YAW] = 360;
	InitTrigger(self);
	self->touch = trigger_monsterjump_touch;
	self->movedir[2] = st.height;
}

//===========================================================================
// SPEC-761 5: new triggers

// 5.1
static void trigger_activate_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!self->target)
		return;
	edict_t *t = NULL;
	while ((t = G_Find(t, FOFS(targetname), self->target))) {
		if (t == self) {
			gi.dprintf("WARNING: Entity used itself.\n");
			continue;
		}
		t->solid = t->solid == SOLID_NOT ? SOLID_TRIGGER : SOLID_NOT;
		gi.linkentity(t);
		if (!self->inuse) {
			gi.dprintf("entity was removed while using targets\n");
			return;
		}
	}
}

void SP_trigger_activate(edict_t *self)
{
	InitTrigger(self);
	self->solid = SOLID_NOT;
	self->use = trigger_activate_use;
	gi.linkentity(self);
}

// 5.2: the zone handler belongs to the game modes (G9)
static void trigger_conquer_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf) {}

void SP_trigger_conquer(edict_t *self)
{
	InitTrigger(self);
	self->touch = trigger_conquer_touch;
	gi.linkentity(self);
	GM_ConquerZone(self); /* G9: SPEC-790 B1.2, B10 */
}

// 5.3
static void countdown_send(int seconds)
{
	gi.WriteByte(svc_countdown);
	gi.WriteLong(seconds);
	gi.multicast(vec3_origin, MULTICAST_ALL_R);
}

static void countdown_use(edict_t *self, edict_t *other, edict_t *activator);

static void countdown_think(edict_t *self)
{
	int left = (int)(self->count + self->timestamp - level.time);
	if (left <= 0) {
		countdown_send(0);
		self->armed = 0;
		if (!(self->spawnflags & 4)) {
			level.countdown_expired = 1;
			G_UseTargets(self, self->activator);
			G_FreeEdict(self);
		} else {
			G_UseTargets(self, self->activator);
			if (self->spawnflags & 2)
				countdown_use(self, self, self);
		}
		return;
	}
	countdown_send(left);
	self->nextthink = level.time + 1.0f;
}

static void countdown_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *who = activator ? activator : other;
	if (!(self->spawnflags & 2) && (!who || !who->client))
		return;
	if (!self->armed) {
		self->armed = 1;
		self->timestamp = level.time;
		self->activator = who;
		self->solid = SOLID_NOT;
		gi.linkentity(self);
		countdown_send(self->count);
		self->think = countdown_think;
		self->nextthink = level.time + 1.0f;
	} else {
		countdown_send(0);
		self->armed = 0;
		self->nextthink = 0;
		if (!(self->spawnflags & 4))
			G_FreeEdict(self);
		else {
			self->solid = SOLID_TRIGGER;
			gi.linkentity(self);
		}
	}
}

static void countdown_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	countdown_use(self, other, other);
}

void SP_trigger_countdown(edict_t *self)
{
	InitTrigger(self);
	if (!self->count)
		self->count = 10;
	self->touch = countdown_touch;
	self->use = countdown_use;
	if (self->spawnflags & 1)
		self->solid = SOLID_NOT;
	gi.linkentity(self);
}

// 5.4
static void useable_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->lastusetime = level.time;
	G_UseTargets(self, activator);
	if (self->targetname && strstr(self->targetname, "wav"))
		gi.sound(self, CHAN_AUTO, gi.soundindex(self->targetname), 1, ATTN_NORM, 0);
}

void SP_trigger_useable(edict_t *self)
{
	InitTrigger(self);
	self->plUse = useable_use;
	if (self->targetname && strstr(self->targetname, "wav"))
		gi.soundindex(self->targetname);
	gi.linkentity(self);
}

// 5.5
static void heal_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *p = activator;
	if (!p || !p->client)
		return;
	if (!self->count) {
		gi.sound(self, CHAN_AUTO, gi.soundindex("misc/hlthstation/empty.wav"), 1, ATTN_NONE, 0);
		return;
	}
	if (p->health < p->max_health) {
		p->health += self->health;
		if (p->health > p->max_health)
			p->health = p->max_health;
		gi.sound(p, CHAN_ITEM, gi.soundindex("dm/healthpu.wav"), 1, ATTN_IDLE, 0);
		gi.sound(self, CHAN_AUTO, gi.soundindex("misc/hlthstation/health.wav"), 1, ATTN_NONE, 0);
		G_UseTargets(self, p);
		self->count = 0;
	}
	gi.sound(self, CHAN_AUTO, gi.soundindex("misc/hlthstation/full.wav"), 1, ATTN_NONE, 0);
}

void SP_trigger_heal(edict_t *self)
{
	InitTrigger(self);
	if (!self->health)
		self->health = 25;
	self->count = 1;
	self->plUse = heal_use;
	gi.soundindex("misc/hlthstation/health.wav");
	gi.soundindex("misc/hlthstation/full.wav");
	gi.soundindex("misc/hlthstation/empty.wav");
	gi.linkentity(self);
}

// 5.6
static void movedown_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	other->velocity[2] = -200;
}

void SP_trigger_movedown(edict_t *self)
{
	InitTrigger(self);
	self->touch = movedown_touch;
	gi.linkentity(self);
}

// 5.7
static void keepusing_think(edict_t *self)
{
	if (!self->armed)
		return;
	if (level.time - self->lastworked <= 0.2f + 0.001f)
		G_UseTargets(self, self);
	else {
		self->armed = 0;
		return;
	}
	self->nextthink = level.time + FRAMETIME;
}

static void keepusing_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!activator || !activator->client)
		return;
	self->armed = 1;
	self->lastworked = level.time;
	self->think = keepusing_think;
	self->nextthink = level.time + FRAMETIME;
}

static void keepusing_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (self->armed)
		self->lastworked = level.time;
}

void SP_trigger_keep_using(edict_t *self)
{
	InitTrigger(self);
	self->plUse = keepusing_use;
	self->touch = keepusing_touch;
	gi.linkentity(self);
}

// 5.8
static void safezone_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other->client)
		other->svflags |= SVF_SAFEZONE;
}

static void toggle_solid_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->solid = self->solid == SOLID_NOT ? SOLID_TRIGGER : SOLID_NOT;
	gi.linkentity(self);
}

void SP_trigger_safezone(edict_t *self)
{
	InitTrigger(self);
	self->touch = safezone_touch;
	self->use = toggle_solid_use;
	if (self->spawnflags & 1)
		self->solid = SOLID_NOT;
	gi.linkentity(self);
}

// 5.9
static void BoxRightAngles(edict_t *self, const vec3_t mins, const vec3_t maxs)
{
	// rotate the box with right-angle yaw
	int yaw = ((int)self->s.angles[YAW] % 360 + 360) % 360;
	vec3_t mn, mx;
	VectorCopy(mins, mn);
	VectorCopy(maxs, mx);
	if (yaw == 90 || yaw == 270) {
		float t;
		t = mn[0]; mn[0] = mn[1]; mn[1] = t;
		t = mx[0]; mx[0] = mx[1]; mx[1] = t;
	}
	VectorCopy(mn, self->mins);
	VectorCopy(mx, self->maxs);
}

void SP_trigger_sound(edict_t *self)
{
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	BoxRightAngles(self, tv(-5, -5, -7), tv(5, 5, 12));
	if (!self->health)
		self->health = 128;
	if (level.trigger_sounds < 16)
		level.trigger_sound_list[level.trigger_sounds++] = self;
	self->svflags |= SVF_NOCLIENT;
	gi.linkentity(self);
}

// noise reports (gunfire, explosions) reach registered trigger_sound entities
void G_TriggerSoundNoise(edict_t *source, const vec3_t where)
{
	if (deathmatch->value)
		return;
	if (source && (source->flags & FL_NOTARGET))
		return;
	for (int i = 0; i < level.trigger_sounds; i++) {
		edict_t *t = level.trigger_sound_list[i];
		if (!t || !t->inuse)
			continue;
		vec3_t d;
		VectorSubtract(t->s.origin, where, d);
		if (VectorLength(d) > t->health)
			continue;
		level.trigger_sound_list[i] = level.trigger_sound_list[--level.trigger_sounds];
		i--;
		if (t->target)
			G_UseTargets(t, t);
		if (t->inuse)
			G_FreeEdict(t);
	}
}

// 5.10
static void objectives_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!other->client)
		return;
	level.missionstatus = 3;
	level.missionstatus_time = level.time;
	G_FreeEdict(self);
}

void SP_trigger_objectives(edict_t *self)
{
	InitTrigger(self);
	self->touch = objectives_touch;
	self->use = toggle_solid_use;
	if (self->spawnflags & 1)
		self->solid = SOLID_NOT;
	gi.linkentity(self);
}
