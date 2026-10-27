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
 * OpenMerc: changed for the SoF 1.07f game module (G18): SoF target_speaker loop data, sound
 * channels/volumes, target_explosion/target_splash without temp entities, SoF changelevel rules,
 * target_laser render flag and masks; target_blaster removed.
 * Implements: SPEC-760 B7
 */

#include "g_local.h"

void ED_CallSpawn(edict_t *ent);

void Use_Target_Tent(edict_t *ent, edict_t *other, edict_t *activator)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(ent->style);
	gi.WritePosition(ent->s.origin);
	gi.multicast(ent->s.origin, MULTICAST_PVS);
}

void SP_target_temp_entity(edict_t *ent) { ent->use = Use_Target_Tent; }

//==========================================================

void Use_Target_Speaker(edict_t *ent, edict_t *other, edict_t *activator)
{
	if (ent->spawnflags & 3) {
		if (ent->s.sound) {
			ent->s.sound = 0;
			ent->s.sounddata = 0;
		} else {
			ent->s.sound = ent->noise_index;
			ent->s.sounddata = 248;
		}
	} else {
		int chan = CHAN_VOICE;
		if (ent->spawnflags & 4)
			chan |= CHAN_RELIABLE;
		gi.positioned_sound(ent->s.origin, ent, chan, ent->noise_index, ent->volume, ent->attenuation, 0);
	}
}

void SP_target_speaker(edict_t *ent)
{
	char buffer[MAX_QPATH];

	if (!st.noise) {
		gi.dprintf("target_speaker with no noise set at %s\n", vtos(ent->s.origin));
		return;
	}
	if (!strstr(st.noise, ".wav"))
		Com_sprintf(buffer, sizeof(buffer), "%s.wav", st.noise);
	else
		strncpy(buffer, st.noise, sizeof(buffer) - 1), buffer[sizeof(buffer) - 1] = 0;
	ent->noise_index = gi.soundindex(buffer);

	if (!ent->volume)
		ent->volume = 1.0;
	if (!ent->attenuation)
		ent->attenuation = 1.0;
	else if (ent->attenuation == -1)
		ent->attenuation = 0;

	if (ent->spawnflags & 1) {
		ent->s.sound = ent->noise_index;
		ent->s.sounddata = 248;
	}

	ent->use = Use_Target_Speaker;
	gi.linkentity(ent);
}

//==========================================================

void Use_Target_Help(edict_t *ent, edict_t *other, edict_t *activator)
{
	if (ent->spawnflags & 1)
		strncpy(game.helpmessage1, ent->message, sizeof(game.helpmessage2) - 1);
	else
		strncpy(game.helpmessage2, ent->message, sizeof(game.helpmessage1) - 1);
	game.helpchanged++;
	level.objectives_changed++;
}

void SP_target_help(edict_t *ent)
{
	if (deathmatch->value) {
		G_FreeEdict(ent);
		return;
	}
	if (!ent->message) {
		gi.dprintf("%s with no message at %s\n", ent->classname, vtos(ent->s.origin));
		G_FreeEdict(ent);
		return;
	}
	ent->use = Use_Target_Help;
}

//==========================================================

void use_target_secret(edict_t *ent, edict_t *other, edict_t *activator)
{
	gi.sound(ent, CHAN_VOICE, ent->noise_index, 0.6f, ATTN_NORM, 0);
	level.found_secrets++;
	G_UseTargets(ent, activator);
	G_FreeEdict(ent);
}

void SP_target_secret(edict_t *ent)
{
	if (deathmatch->value) {
		G_FreeEdict(ent);
		return;
	}
	ent->use = use_target_secret;
	if (!st.noise)
		st.noise = (char *)"misc/secret.wav";
	ent->noise_index = gi.soundindex(st.noise);
	ent->svflags = SVF_NOCLIENT;
	level.total_secrets++;
}

void use_target_goal(edict_t *ent, edict_t *other, edict_t *activator)
{
	gi.sound(ent, CHAN_VOICE, ent->noise_index, 0.6f, ATTN_NORM, 0);
	level.found_goals++;
	if (level.found_goals == level.total_goals)
		gi.configstring(CS_CDTRACK, "0");
	G_UseTargets(ent, activator);
	G_FreeEdict(ent);
}

void SP_target_goal(edict_t *ent)
{
	if (deathmatch->value) {
		G_FreeEdict(ent);
		return;
	}
	ent->use = use_target_goal;
	if (!st.noise)
		st.noise = (char *)"misc/secret.wav";
	ent->noise_index = gi.soundindex(st.noise);
	ent->svflags = SVF_NOCLIENT;
	level.total_goals++;
}

//==========================================================

void target_explosion_explode(edict_t *self)
{
	T_RadiusDamage(self, self->activator, self->dmg, NULL, self->dmg + 40, MOD_EXPLOSIVE);
	if (self->dmg)
		G_ExplosionStimulus(self->s.origin, self->dmg, self->dmg);

	float save = self->delay;
	self->delay = 0;
	G_UseTargets(self, self->activator);
	self->delay = save;
}

void use_target_explosion(edict_t *self, edict_t *other, edict_t *activator)
{
	self->activator = activator;
	if (!self->delay) {
		target_explosion_explode(self);
		return;
	}
	self->think = target_explosion_explode;
	self->nextthink = level.time + self->delay;
}

void SP_target_explosion(edict_t *ent)
{
	ent->use = use_target_explosion;
	ent->svflags = SVF_NOCLIENT;
}

//==========================================================
// target_changelevel (SPEC-760 B7)

void use_target_changelevel(edict_t *self, edict_t *other, edict_t *activator)
{
	if (level.intermissiontime)
		return;

	if (!deathmatch->value) {
		if (g_edicts[1].health <= 0)
			return;
		edict_t *p = &g_edicts[1];
		if (p->client && p->client->inv)
			p->client->inv->SavePersist(true, p->health);
		Stats_Accumulate(); // mission totals (SPEC-AMEND-007 G18.41); the next level's spawn clears the counters
	} else {
		if (other && other != world) {
			T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, 10 * other->max_health, 1000,
			         DAMAGE_NO_PROTECTION, MOD_EXIT);
			return;
		}
		if (activator && activator->client)
			gi.bprintf(PRINT_HIGH, "%s exited the level.\n", activator->client->pers.netname);
	}

	if (strstr(self->map, "*"))
		game.serverflags &= ~(SFL_CROSS_TRIGGER_MASK);

	BeginIntermission(self);
}

void SP_target_changelevel(edict_t *ent)
{
	if (!ent->map) {
		gi.dprintf("target_changelevel with no map at %s\n", vtos(ent->s.origin));
		G_FreeEdict(ent);
		return;
	}
	ent->use = use_target_changelevel;
	ent->svflags = SVF_NOCLIENT;
}

//==========================================================

void use_target_splash(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->dmg) {
		T_RadiusDamage(self, activator, self->dmg, NULL, 5, MOD_SPLASH);
		G_ExplosionStimulus(self->s.origin, self->dmg, self->dmg);
	}
}

void SP_target_splash(edict_t *self)
{
	self->use = use_target_splash;
	G_SetMovedir(self->s.angles, self->movedir);
	if (!self->count)
		self->count = 32;
	self->svflags = SVF_NOCLIENT;
}

//==========================================================

void use_target_spawner(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *ent = G_Spawn();
	ent->classname = self->target;
	VectorCopy(self->s.origin, ent->s.origin);
	VectorCopy(self->s.angles, ent->s.angles);
	ED_CallSpawn(ent);
	gi.unlinkentity(ent);
	KillBox(ent);
	gi.linkentity(ent);
	if (self->speed)
		VectorCopy(self->movedir, ent->velocity);
}

void SP_target_spawner(edict_t *self)
{
	self->use = use_target_spawner;
	self->svflags = SVF_NOCLIENT;
	if (self->speed) {
		G_SetMovedir(self->s.angles, self->movedir);
		VectorScale(self->movedir, self->speed, self->movedir);
	}
}

//==========================================================

void trigger_crosslevel_trigger_use(edict_t *self, edict_t *other, edict_t *activator)
{
	game.serverflags |= self->spawnflags;
	G_FreeEdict(self);
}

void SP_target_crosslevel_trigger(edict_t *self)
{
	self->svflags = SVF_NOCLIENT;
	self->use = trigger_crosslevel_trigger_use;
}

void target_crosslevel_target_think(edict_t *self)
{
	if (self->spawnflags == (game.serverflags & SFL_CROSS_TRIGGER_MASK & self->spawnflags)) {
		G_UseTargets(self, self);
		G_FreeEdict(self);
	}
}

void SP_target_crosslevel_target(edict_t *self)
{
	if (!self->delay)
		self->delay = 1;
	self->svflags = SVF_NOCLIENT;
	self->think = target_crosslevel_target_think;
	self->nextthink = level.time + self->delay;
}

//==========================================================
// target_laser: no sparks, beam end point not updated (SPEC-760 B7)

void target_laser_think(edict_t *self)
{
	edict_t *ignore;
	vec3_t start, end, point;
	trace_t tr;

	if (self->enemy) {
		VectorMA(self->enemy->absmin, 0.5, self->enemy->size, point);
		VectorSubtract(point, self->s.origin, self->movedir);
		VectorNormalize(self->movedir);
	}

	ignore = self;
	VectorCopy(self->s.origin, start);
	VectorMA(start, 2048, self->movedir, end);
	while (1) {
		tr = gi.trace(start, NULL, NULL, end, ignore, CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_DEADMONSTER);
		if (!tr.ent)
			break;
		if ((tr.ent->takedamage) && !(tr.ent->flags & FL_IMMUNE_LASER))
			T_Damage(tr.ent, self, self->activator, self->movedir, tr.endpos, vec3_origin, self->dmg, 1,
			         DAMAGE_ENERGY, MOD_TARGET_LASER);
		if (!(tr.ent->svflags & SVF_MONSTER) && (!tr.ent->client))
			break;
		ignore = tr.ent;
		VectorCopy(tr.endpos, start);
	}
	self->nextthink = level.time + FRAMETIME;
}

void target_laser_on(edict_t *self)
{
	if (!self->activator)
		self->activator = self;
	self->spawnflags |= 1;
	self->svflags &= ~SVF_NOCLIENT;
	target_laser_think(self);
}

void target_laser_off(edict_t *self)
{
	self->spawnflags &= ~1;
	self->svflags |= SVF_NOCLIENT;
	self->nextthink = 0;
}

void target_laser_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->activator = activator;
	if (self->spawnflags & 1)
		target_laser_off(self);
	else
		target_laser_on(self);
}

void target_laser_start(edict_t *self)
{
	self->movetype = MOVETYPE_NONE;
	self->solid = SOLID_NOT;
	self->s.renderfx |= RF_LASERBEAM;
	self->s.modelindex = 1;
	self->s.frame = (self->spawnflags & 64) ? 16 : 4;

	if (self->spawnflags & 2)
		self->s.skinnum = 0xf2f2f0f0;
	else if (self->spawnflags & 4)
		self->s.skinnum = 0xd0d1d2d3;
	else if (self->spawnflags & 8)
		self->s.skinnum = 0xf3f3f1f1;
	else if (self->spawnflags & 16)
		self->s.skinnum = 0xdcdddedf;
	else if (self->spawnflags & 32)
		self->s.skinnum = 0xe0e1e2e3;

	if (!self->enemy) {
		if (self->target) {
			edict_t *ent = G_Find(NULL, FOFS(targetname), self->target);
			if (!ent)
				gi.dprintf("%s at %s: %s is a bad target\n", self->classname, vtos(self->s.origin), self->target);
			self->enemy = ent;
		} else
			G_SetMovedir(self->s.angles, self->movedir);
	}
	self->use = target_laser_use;
	self->think = target_laser_think;
	if (!self->dmg)
		self->dmg = 1;
	VectorSet(self->mins, -8, -8, -8);
	VectorSet(self->maxs, 8, 8, 8);
	gi.linkentity(self);
	if (self->spawnflags & 1)
		target_laser_on(self);
	else
		target_laser_off(self);
}

void SP_target_laser(edict_t *self)
{
	self->think = target_laser_start;
	self->nextthink = level.time + 1;
}

//==========================================================

void target_lightramp_think(edict_t *self)
{
	char style[2];
	style[0] = 'a' + self->movedir[0] + (level.time - self->timestamp) / FRAMETIME * self->movedir[2];
	style[1] = 0;
	gi.configstring(CS_LIGHTS + self->enemy->style, style);
	if ((level.time - self->timestamp) < self->speed)
		self->nextthink = level.time + FRAMETIME;
	else if (self->spawnflags & 1) {
		char temp = self->movedir[0];
		self->movedir[0] = self->movedir[1];
		self->movedir[1] = temp;
		self->movedir[2] *= -1;
	}
}

void target_lightramp_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!self->enemy) {
		edict_t *e = NULL;
		while (1) {
			e = G_Find(e, FOFS(targetname), self->target);
			if (!e)
				break;
			if (strcmp(e->classname, "light") != 0) {
				gi.dprintf("%s at %s ", self->classname, vtos(self->s.origin));
				gi.dprintf("target %s (%s at %s) is not a light\n", self->target, e->classname, vtos(e->s.origin));
			} else
				self->enemy = e;
		}
		if (!self->enemy) {
			gi.dprintf("%s target %s not found at %s\n", self->classname, self->target, vtos(self->s.origin));
			G_FreeEdict(self);
			return;
		}
	}
	self->timestamp = level.time;
	target_lightramp_think(self);
}

void SP_target_lightramp(edict_t *self)
{
	if (!self->message || strlen(self->message) != 2 || self->message[0] < 'a' || self->message[0] > 'z' ||
	    self->message[1] < 'a' || self->message[1] > 'z' || self->message[0] == self->message[1]) {
		gi.dprintf("target_lightramp has bad ramp (%s) at %s\n", self->message, vtos(self->s.origin));
		G_FreeEdict(self);
		return;
	}
	if (deathmatch->value) {
		G_FreeEdict(self);
		return;
	}
	if (!self->target) {
		gi.dprintf("%s with no target at %s\n", self->classname, vtos(self->s.origin));
		G_FreeEdict(self);
		return;
	}
	self->svflags |= SVF_NOCLIENT;
	self->use = target_lightramp_use;
	self->think = target_lightramp_think;
	self->movedir[0] = self->message[0] - 'a';
	self->movedir[1] = self->message[1] - 'a';
	self->movedir[2] = (self->movedir[1] - self->movedir[0]) / (self->speed / FRAMETIME);
}

//==========================================================

void target_earthquake_think(edict_t *self)
{
	if (self->last_move_time < level.time) {
		gi.positioned_sound(self->s.origin, self, CHAN_AUTO, self->noise_index, 1.0, ATTN_NONE, 0);
		self->last_move_time = level.time + 0.5;
	}
	for (int i = 1; i < globals.num_edicts; i++) {
		edict_t *e = g_edicts + i;
		if (!e->inuse || !e->client || !e->groundentity)
			continue;
		e->groundentity = NULL;
		e->velocity[0] += crandom() * 150;
		e->velocity[1] += crandom() * 150;
		e->velocity[2] = self->speed * (100.0 / (e->mass ? e->mass : 200));
	}
	if (level.time < self->timestamp)
		self->nextthink = level.time + FRAMETIME;
}

void target_earthquake_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->timestamp = level.time + self->count;
	self->nextthink = level.time + FRAMETIME;
	self->activator = activator;
	self->last_move_time = 0;
}

void SP_target_earthquake(edict_t *self)
{
	if (!self->targetname)
		gi.dprintf("untargeted %s at %s\n", self->classname, vtos(self->s.origin));
	if (!self->count)
		self->count = 5;
	if (!self->speed)
		self->speed = 200;
	self->svflags |= SVF_NOCLIENT;
	self->think = target_earthquake_think;
	self->use = target_earthquake_use;
	self->noise_index = gi.soundindex("world/quake.wav");
}
