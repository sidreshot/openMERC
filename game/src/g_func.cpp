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
 * OpenMerc: changed for the SoF 1.07f game module (G18): sound sets instead of mover sounds, SoF
 * buttons, doors (activation sources, locked, trigger field, clearance check before closing, area-portal
 * toggling, blocked rules), crushing, portcullis and smart rotating door (SPEC-761 1).
 * Implements: SPEC-760 B5, SPEC-761 1
 */

#include "g_local.h"
#include "effects/ambient.h" /* G7: mover sound sets */

#define PLAT_LOW_TRIGGER 1

#define STATE_TOP 0
#define STATE_BOTTOM 1
#define STATE_UP 2
#define STATE_DOWN 3

#define DOOR_START_OPEN 1
#define DOOR_REVERSE 2
#define DOOR_CRUSHER 4
#define DOOR_NOMONSTER 8
#define DOOR_LOCKED 16
#define DOOR_TOGGLE 32
#define DOOR_X_AXIS 64
#define DOOR_Y_AXIS 128
#define DOOR_TRIGGER_FIELD 128
#define DOOR_PLAYER_USABLE 0x2000
#define DOOR_NO_AXIS_FLAG 0x4000
#define DOOR_FIRE_ON_CLOSE 0x8000

#define MOVER_CHAN (CHAN_NO_PHS_ADD | CHAN_VOICE)

//======================================================================
// sound sets (SPEC-760 B5.1, SPEC-771 B.2): G7's table (effects/ambient.h)

static edict_t *g_soundlookups[256];
static int g_numsoundlookups;

static void Mover_DeferSounds(edict_t *ent)
{
	if (ent->soundname && ent->soundname[0] && g_numsoundlookups < 256)
		g_soundlookups[g_numsoundlookups++] = ent;
}

static int MoverSnd(edict_t *ent, int *slot, int k)
{
	if (*slot == -1) {
		int i = ent->soundname && ent->soundname[0] ? ambient::MoverSound(ent->soundname, k) : -1;
		*slot = i > 0 ? i : 0;
	}
	return *slot;
}

// The set's entries are resolved through G7's table 0.1 s after spawn (SPEC-760 B5.1; -1 = not resolved yet): a
// missing entry of a set with fewer than three waves reads as wave 0 of the file (`AirBy1.wav`, SPEC-771 B.2,
// SPEC-AMEND-006 G7.3). Doors, plats, trains and rotators resolve entries 0-2, a button only its entry 0 (its only
// sound, B5.5). Black box (spsuite): tsr1's lever door `new_tok_lever` (one-wave `ButtonElevator1`) registers
// `AirBy1.wav` in the first frame of the oracle; ger2-4, irq2b, jpn1, whose one-wave sets are on buttons only
// (`ButtonBleep1`, `ButtonElevator1`), never register it.
void G_SoundSetLookup(edict_t *ent)
{
	ent->moveinfo.sound_start = ent->moveinfo.sound_middle = ent->moveinfo.sound_end = -1;
	ent->moversounds = 1;
	MoverSnd(ent, &ent->moveinfo.sound_start, 0);
	if (ent->classname && !Q_stricmp(ent->classname, "func_button"))
		return;
	MoverSnd(ent, &ent->moveinfo.sound_middle, 1);
	MoverSnd(ent, &ent->moveinfo.sound_end, 2);
}

// run once at 0.1 s into the level (called from G_WorldSpawnerFrame's caller order: RunFrame)
void G_MoverSoundsFrame(void)
{
	if (level.time < 0.1f - 0.001f || !g_numsoundlookups)
		return;
	for (int i = 0; i < g_numsoundlookups; i++)
		if (g_soundlookups[i]->inuse)
			G_SoundSetLookup(g_soundlookups[i]);
	g_numsoundlookups = 0;
}

void G_MoverSoundsReset(void)
{
	g_numsoundlookups = 0;
}

static void Mover_StartSound(edict_t *ent)
{
	if (ent->flags & FL_TEAMSLAVE)
		return;
	if (MoverSnd(ent, &ent->moveinfo.sound_start, 0))
		gi.sound(ent, MOVER_CHAN, ent->moveinfo.sound_start, 1, ATTN_NORM, 0);
	ent->s.sound = MoverSnd(ent, &ent->moveinfo.sound_middle, 1);
	ent->s.sounddata = ent->moveinfo.sound_middle ? 248 : 0;
}

static void Mover_EndSound(edict_t *ent)
{
	if (ent->flags & FL_TEAMSLAVE)
		return;
	if (MoverSnd(ent, &ent->moveinfo.sound_end, 2))
		gi.sound(ent, MOVER_CHAN, ent->moveinfo.sound_end, 1, ATTN_NORM, 0);
	ent->s.sound = 0;
	ent->s.sounddata = 0;
}

// SPEC-760 B5.2: crushing non-player, non-character obstacles
static qboolean Mover_Crush(edict_t *self, edict_t *other)
{
	if (!(other->svflags & SVF_MONSTER) && !other->client) {
		T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, 100000, 1, 0, MOD_CRUSH);
		if (other->inuse)
			G_FreeEdict(other);
		return true;
	}
	return false;
}

//======================================================================
// Support routines for movement (changes in origin using velocity)

void Move_Done(edict_t *ent)
{
	VectorClear(ent->velocity);
	ent->moveinfo.endfunc(ent);
}

void Move_Final(edict_t *ent)
{
	if (ent->moveinfo.remaining_distance == 0) {
		Move_Done(ent);
		return;
	}
	VectorScale(ent->moveinfo.dir, ent->moveinfo.remaining_distance / FRAMETIME, ent->velocity);
	ent->think = Move_Done;
	ent->nextthink = level.time + FRAMETIME;
}

void Move_Begin(edict_t *ent)
{
	if ((ent->moveinfo.speed * FRAMETIME) >= ent->moveinfo.remaining_distance) {
		Move_Final(ent);
		return;
	}
	VectorScale(ent->moveinfo.dir, ent->moveinfo.speed, ent->velocity);
	float frames = floor((ent->moveinfo.remaining_distance / ent->moveinfo.speed) / FRAMETIME);
	ent->moveinfo.remaining_distance -= frames * ent->moveinfo.speed * FRAMETIME;
	ent->nextthink = level.time + (frames * FRAMETIME);
	ent->think = Move_Final;
}

void Think_AccelMove(edict_t *ent);

void Move_Calc(edict_t *ent, vec3_t dest, void (*func)(edict_t *))
{
	VectorClear(ent->velocity);
	VectorSubtract(dest, ent->s.origin, ent->moveinfo.dir);
	ent->moveinfo.remaining_distance = VectorNormalize(ent->moveinfo.dir);
	ent->moveinfo.endfunc = func;

	if (ent->moveinfo.speed == ent->moveinfo.accel && ent->moveinfo.speed == ent->moveinfo.decel) {
		if (level.current_entity == ((ent->flags & FL_TEAMSLAVE) ? ent->teammaster : ent))
			Move_Begin(ent);
		else {
			ent->nextthink = level.time + FRAMETIME;
			ent->think = Move_Begin;
		}
	} else {
		ent->moveinfo.current_speed = 0;
		ent->think = Think_AccelMove;
		ent->nextthink = level.time + FRAMETIME;
	}
}

// Support routines for angular movement (changes in angle using avelocity)

void AngleMove_Done(edict_t *ent)
{
	VectorClear(ent->avelocity);
	ent->moveinfo.endfunc(ent);
}

void AngleMove_Final(edict_t *ent)
{
	vec3_t move;
	if (ent->moveinfo.state == STATE_UP)
		VectorSubtract(ent->moveinfo.end_angles, ent->s.angles, move);
	else
		VectorSubtract(ent->moveinfo.start_angles, ent->s.angles, move);
	if (VectorCompare(move, vec3_origin)) {
		AngleMove_Done(ent);
		return;
	}
	VectorScale(move, 1.0 / FRAMETIME, ent->avelocity);
	ent->think = AngleMove_Done;
	ent->nextthink = level.time + FRAMETIME;
}

void AngleMove_Begin(edict_t *ent)
{
	vec3_t destdelta;
	if (ent->moveinfo.state == STATE_UP)
		VectorSubtract(ent->moveinfo.end_angles, ent->s.angles, destdelta);
	else
		VectorSubtract(ent->moveinfo.start_angles, ent->s.angles, destdelta);
	float len = VectorLength(destdelta);
	float traveltime = len / ent->moveinfo.speed;
	if (traveltime < FRAMETIME) {
		AngleMove_Final(ent);
		return;
	}
	float frames = floor(traveltime / FRAMETIME);
	VectorScale(destdelta, 1.0 / traveltime, ent->avelocity);
	ent->nextthink = level.time + frames * FRAMETIME;
	ent->think = AngleMove_Final;
}

void AngleMove_Calc(edict_t *ent, void (*func)(edict_t *))
{
	VectorClear(ent->avelocity);
	ent->moveinfo.endfunc = func;
	if (level.current_entity == ((ent->flags & FL_TEAMSLAVE) ? ent->teammaster : ent))
		AngleMove_Begin(ent);
	else {
		ent->nextthink = level.time + FRAMETIME;
		ent->think = AngleMove_Begin;
	}
}

#define AccelerationDistance(target, rate) (target * ((target / rate) + 1) / 2)

void plat_CalcAcceleratedMove(moveinfo_t *moveinfo)
{
	moveinfo->move_speed = moveinfo->speed;
	if (moveinfo->remaining_distance < moveinfo->accel) {
		moveinfo->current_speed = moveinfo->remaining_distance;
		return;
	}
	float accel_dist = AccelerationDistance(moveinfo->speed, moveinfo->accel);
	float decel_dist = AccelerationDistance(moveinfo->speed, moveinfo->decel);
	if ((moveinfo->remaining_distance - accel_dist - decel_dist) < 0) {
		float f = (moveinfo->accel + moveinfo->decel) / (moveinfo->accel * moveinfo->decel);
		moveinfo->move_speed = (-2 + sqrt(4 - 4 * f * (-2 * moveinfo->remaining_distance))) / (2 * f);
		decel_dist = AccelerationDistance(moveinfo->move_speed, moveinfo->decel);
	}
	moveinfo->decel_distance = decel_dist;
}

void plat_Accelerate(moveinfo_t *moveinfo)
{
	if (moveinfo->remaining_distance <= moveinfo->decel_distance) {
		if (moveinfo->remaining_distance < moveinfo->decel_distance) {
			if (moveinfo->next_speed) {
				moveinfo->current_speed = moveinfo->next_speed;
				moveinfo->next_speed = 0;
				return;
			}
			if (moveinfo->current_speed > moveinfo->decel)
				moveinfo->current_speed -= moveinfo->decel;
		}
		return;
	}
	if (moveinfo->current_speed == moveinfo->move_speed)
		if ((moveinfo->remaining_distance - moveinfo->current_speed) < moveinfo->decel_distance) {
			float p1_distance = moveinfo->remaining_distance - moveinfo->decel_distance;
			float p2_distance = moveinfo->move_speed * (1.0 - (p1_distance / moveinfo->move_speed));
			float distance = p1_distance + p2_distance;
			moveinfo->current_speed = moveinfo->move_speed;
			moveinfo->next_speed = moveinfo->move_speed - moveinfo->decel * (p2_distance / distance);
			return;
		}
	if (moveinfo->current_speed < moveinfo->speed) {
		float old_speed = moveinfo->current_speed;
		moveinfo->current_speed += moveinfo->accel;
		if (moveinfo->current_speed > moveinfo->speed)
			moveinfo->current_speed = moveinfo->speed;
		if ((moveinfo->remaining_distance - moveinfo->current_speed) >= moveinfo->decel_distance)
			return;
		float p1_distance = moveinfo->remaining_distance - moveinfo->decel_distance;
		float p1_speed = (old_speed + moveinfo->move_speed) / 2.0;
		float p2_distance = moveinfo->move_speed * (1.0 - (p1_distance / p1_speed));
		float distance = p1_distance + p2_distance;
		moveinfo->current_speed =
			(p1_speed * (p1_distance / distance)) + (moveinfo->move_speed * (p2_distance / distance));
		moveinfo->next_speed = moveinfo->move_speed - moveinfo->decel * (p2_distance / distance);
		return;
	}
}

void Think_AccelMove(edict_t *ent)
{
	ent->moveinfo.remaining_distance -= ent->moveinfo.current_speed;
	if (ent->moveinfo.current_speed == 0)
		plat_CalcAcceleratedMove(&ent->moveinfo);
	plat_Accelerate(&ent->moveinfo);
	if (ent->moveinfo.remaining_distance <= ent->moveinfo.current_speed) {
		Move_Final(ent);
		return;
	}
	VectorScale(ent->moveinfo.dir, ent->moveinfo.current_speed * 10, ent->velocity);
	ent->nextthink = level.time + FRAMETIME;
	ent->think = Think_AccelMove;
}

//======================================================================
// PLATS (SPEC-760 B5.3)

void plat_go_down(edict_t *ent);

void plat_hit_top(edict_t *ent)
{
	Mover_EndSound(ent);
	ent->moveinfo.state = STATE_TOP;
	ent->think = plat_go_down;
	ent->nextthink = level.time + 3;
}

void plat_hit_bottom(edict_t *ent)
{
	Mover_EndSound(ent);
	ent->moveinfo.state = STATE_BOTTOM;
}

void plat_go_down(edict_t *ent)
{
	Mover_StartSound(ent);
	ent->moveinfo.state = STATE_DOWN;
	Move_Calc(ent, ent->moveinfo.end_origin, plat_hit_bottom);
}

void plat_go_up(edict_t *ent)
{
	Mover_StartSound(ent);
	ent->moveinfo.state = STATE_UP;
	Move_Calc(ent, ent->moveinfo.start_origin, plat_hit_top);
}

void plat_blocked(edict_t *self, edict_t *other)
{
	if (Mover_Crush(self, other))
		return;
	T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
	if (self->moveinfo.state == STATE_UP)
		plat_go_down(self);
	else if (self->moveinfo.state == STATE_DOWN)
		plat_go_up(self);
}

void Use_Plat(edict_t *ent, edict_t *other, edict_t *activator)
{
	if (ent->think)
		return;
	plat_go_down(ent);
}

void Touch_Plat_Center(edict_t *ent, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!other->client || other->health <= 0)
		return;
	ent = ent->enemy;
	if (ent->moveinfo.state == STATE_BOTTOM)
		plat_go_up(ent);
	else if (ent->moveinfo.state == STATE_TOP)
		ent->nextthink = level.time + 1;
}

void plat_spawn_inside_trigger(edict_t *ent)
{
	vec3_t tmin, tmax;
	edict_t *trigger = G_Spawn();
	trigger->touch = Touch_Plat_Center;
	trigger->movetype = MOVETYPE_NONE;
	trigger->solid = SOLID_TRIGGER;
	trigger->enemy = ent;

	tmin[0] = ent->mins[0] + 25;
	tmin[1] = ent->mins[1] + 25;
	tmin[2] = ent->mins[2];
	tmax[0] = ent->maxs[0] - 25;
	tmax[1] = ent->maxs[1] - 25;
	tmax[2] = ent->maxs[2] + 8;
	tmin[2] = tmax[2] - (ent->pos1[2] - ent->pos2[2] + st.lip);
	if (ent->spawnflags & PLAT_LOW_TRIGGER)
		tmax[2] = tmin[2] + 8;
	if (tmax[0] - tmin[0] <= 0) {
		tmin[0] = (ent->mins[0] + ent->maxs[0]) * 0.5;
		tmax[0] = tmin[0] + 1;
	}
	if (tmax[1] - tmin[1] <= 0) {
		tmin[1] = (ent->mins[1] + ent->maxs[1]) * 0.5;
		tmax[1] = tmin[1] + 1;
	}
	VectorCopy(tmin, trigger->mins);
	VectorCopy(tmax, trigger->maxs);
	gi.linkentity(trigger);
}

void SP_func_plat(edict_t *ent)
{
	VectorClear(ent->s.angles);
	ent->solid = SOLID_BSP;
	ent->movetype = MOVETYPE_PUSH;
	gi.setmodel(ent, ent->model);
	ent->blocked = plat_blocked;

	if (!ent->speed)
		ent->speed = 20;
	else
		ent->speed *= 0.1;
	if (!ent->accel)
		ent->accel = 5;
	else
		ent->accel *= 0.1;
	if (!ent->decel)
		ent->decel = 5;
	else
		ent->decel *= 0.1;
	if (!ent->dmg)
		ent->dmg = 2;
	if (!st.lip)
		st.lip = 8;

	VectorCopy(ent->s.origin, ent->pos1);
	VectorCopy(ent->s.origin, ent->pos2);
	if (st.height)
		ent->pos2[2] -= st.height;
	else
		ent->pos2[2] -= (ent->maxs[2] - ent->mins[2]) - st.lip;

	ent->use = Use_Plat;
	plat_spawn_inside_trigger(ent);

	if (ent->targetname)
		ent->moveinfo.state = STATE_UP;
	else {
		VectorCopy(ent->pos2, ent->s.origin);
		gi.linkentity(ent);
		ent->moveinfo.state = STATE_BOTTOM;
	}

	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);
	Mover_DeferSounds(ent);
}

//====================================================================
// func_rotating (SPEC-760 B5.4)

void rotating_blocked(edict_t *self, edict_t *other)
{
	T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void rotating_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (self->avelocity[0] || self->avelocity[1] || self->avelocity[2])
		T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void rotating_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!VectorCompare(self->avelocity, vec3_origin)) {
		self->s.sound = 0;
		self->s.sounddata = 0;
		VectorClear(self->avelocity);
		self->touch = NULL;
	} else {
		self->s.sound = MoverSnd(self, &self->moveinfo.sound_middle, 1);
		self->s.sounddata = self->s.sound ? 248 : 0;
		VectorScale(self->movedir, self->speed, self->avelocity);
		if (self->spawnflags & 16)
			self->touch = rotating_touch;
	}
}

static void rotating_startsound(edict_t *self)
{
	G_SoundSetLookup(self);
	if (!VectorCompare(self->avelocity, vec3_origin)) {
		self->s.sound = MoverSnd(self, &self->moveinfo.sound_middle, 1);
		self->s.sounddata = self->s.sound ? 248 : 0;
	}
}

void SP_func_rotating(edict_t *ent)
{
	ent->solid = SOLID_BSP;
	ent->movetype = (ent->spawnflags & 32) ? MOVETYPE_STOP : MOVETYPE_PUSH;

	VectorClear(ent->movedir);
	if (ent->spawnflags & 4)
		ent->movedir[2] = 1.0;
	else if (ent->spawnflags & 8)
		ent->movedir[0] = 1.0;
	else
		ent->movedir[1] = 1.0;
	if (ent->spawnflags & 2)
		VectorNegate(ent->movedir, ent->movedir);

	if (!ent->speed)
		ent->speed = 100;
	if (!ent->dmg)
		ent->dmg = 2;

	ent->use = rotating_use;
	if (ent->dmg)
		ent->blocked = rotating_blocked;

	if (ent->spawnflags & 1)
		ent->use(ent, NULL, NULL);

	if (ent->spawnflags & 64)
		ent->s.effects |= EF_ANIM_ALL;
	if (ent->spawnflags & 128)
		ent->s.effects |= EF_ANIM_ALLFAST;

	gi.setmodel(ent, ent->model);
	gi.linkentity(ent);
	if (ent->soundname && ent->soundname[0]) {
		ent->think = rotating_startsound;
		ent->nextthink = level.time + FRAMETIME;
	}
}

//======================================================================
// BUTTONS (SPEC-760 B5.5)

void button_done(edict_t *self)
{
	self->moveinfo.state = STATE_BOTTOM;
	self->s.effects &= ~EF_BUTTONPRESSED;
	self->s.effects |= EF_BUTTONIDLE;
}

void button_return(edict_t *self)
{
	self->moveinfo.state = STATE_DOWN;
	Move_Calc(self, self->moveinfo.start_origin, button_done);
	self->s.frame = 0;
	if (self->health)
		self->takedamage = DAMAGE_YES;
}

void button_wait(edict_t *self)
{
	self->moveinfo.state = STATE_TOP;
	self->s.effects &= ~EF_BUTTONIDLE;
	self->s.effects |= EF_BUTTONPRESSED;

	G_UseTargets(self, self->activator);
	self->s.frame = 1;
	if (self->moveinfo.wait >= 0) {
		self->nextthink = level.time + self->moveinfo.wait;
		self->think = button_return;
	}
}

void button_fire(edict_t *self)
{
	self->lastusetime = level.time;
	if (self->moveinfo.state == STATE_UP || self->moveinfo.state == STATE_TOP)
		return;

	self->moveinfo.state = STATE_UP;
	if (!(self->flags & FL_TEAMSLAVE) && MoverSnd(self, &self->moveinfo.sound_start, 0))
		gi.sound(self, MOVER_CHAN, self->moveinfo.sound_start, 1, ATTN_NORM, 0);
	if (!self->soundname || !self->soundname[0])
		gi.sound(self, CHAN_VOICE, gi.soundindex("Ambient/Buttons/HiTech/Elevator1.wav"), 0.6f, ATTN_NORM, 0);
	if (self->sp_message && self->activator && self->activator->client) {
		gix.SP_Print(self->activator, self->sp_message);
		gi.sound(self->activator, CHAN_AUTO, gi.soundindex("Misc/Talk.wav"), 1, ATTN_NORM, 0);
	}
	Move_Calc(self, self->moveinfo.end_origin, button_wait);
}

void button_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->activator = activator;
	button_fire(self);
}

void button_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!other->client || other->health <= 0)
		return;
	self->activator = other;
	button_fire(self);
}

void button_killed(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->activator = attacker;
	self->health = self->max_health;
	self->takedamage = DAMAGE_NO;
	button_fire(self);
}

void SP_func_button(edict_t *ent)
{
	vec3_t abs_movedir;

	G_SetMovedir(ent->s.angles, ent->movedir);
	ent->movetype = MOVETYPE_STOP;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	if (!ent->speed)
		ent->speed = 40;
	if (!ent->accel)
		ent->accel = ent->speed;
	if (!ent->decel)
		ent->decel = ent->speed;
	if (!ent->wait)
		ent->wait = 3;
	if (!st.lip)
		st.lip = 4;

	VectorCopy(ent->s.origin, ent->pos1);
	abs_movedir[0] = fabs(ent->movedir[0]);
	abs_movedir[1] = fabs(ent->movedir[1]);
	abs_movedir[2] = fabs(ent->movedir[2]);
	float dist = abs_movedir[0] * ent->size[0] + abs_movedir[1] * ent->size[1] + abs_movedir[2] * ent->size[2] - st.lip;
	VectorMA(ent->pos1, dist, ent->movedir, ent->pos2);

	ent->use = button_use;
	// precached at every button's spawn, with or without a sound set (oracle: nycdm3, nycdm4; ger4, whose four buttons
	// all use `ButtonBuzz`, registers it right at the first button's spawn — spsuite)
	gi.soundindex("Ambient/Buttons/HiTech/Elevator1.wav");
	ent->s.effects |= EF_BUTTONIDLE;
	if (ent->spawnflags & 2)
		ent->s.effects |= EF_BUTTON1000;

	if (ent->health) {
		ent->max_health = ent->health;
		ent->die = button_killed;
		ent->takedamage = DAMAGE_YES;
	}
	if (!ent->targetname)
		ent->plUse = button_use;
	if (ent->spawnflags & 1)
		ent->touch = button_touch;

	ent->moveinfo.state = STATE_BOTTOM;
	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);
	Mover_DeferSounds(ent);
	gi.linkentity(ent);
}

//======================================================================
// DOORS (SPEC-760 B5.7, SPEC-761 1)

static bool IsRotatingDoor(edict_t *e)
{
	return !strcmp(e->classname, "func_door_rotating") || !strcmp(e->classname, "func_door_rotating_smart");
}

static bool IsPortcullis(edict_t *e) { return !strcmp(e->classname, "func_door_portcullis"); }

// doors toggle each targeted func_areaportal (SPEC-760 B5.7 "Area portals")
static void door_toggle_portals(edict_t *self)
{
	edict_t *t = NULL;
	if (!self->target)
		return;
	while ((t = G_Find(t, FOFS(targetname), self->target))) {
		if (Q_stricmp(t->classname, "func_areaportal") == 0 && t->use)
			t->use(t, self, self);
	}
}

static void door_hold_portals(edict_t *self, bool hold)
{
	if (hold && !self->portalsheld) {
		door_toggle_portals(self);
		self->portalsheld = 1;
	} else if (!hold && self->portalsheld) {
		door_toggle_portals(self);
		self->portalsheld = 0;
	}
}

void door_go_down(edict_t *self);
void door_go_up(edict_t *self, edict_t *activator);
static void door_close_check(edict_t *self);

// players and characters with health within 96 units of a team member's origin block the close
static bool door_blocked_by_people(edict_t *master)
{
	for (edict_t *m = master; m; m = m->teamchain) {
		vec3_t org;
		VectorCopy(m->s.origin, org);
		for (int i = 1; i < globals.num_edicts; i++) {
			edict_t *e = &g_edicts[i];
			if (!e->inuse || e->health <= 0)
				continue;
			if (!e->client && !(e->svflags & SVF_MONSTER))
				continue;
			vec3_t c, d;
			for (int j = 0; j < 3; j++)
				c[j] = e->s.origin[j] + (e->mins[j] + e->maxs[j]) * 0.5f;
			VectorSubtract(c, org, d);
			if (VectorLength(d) < 96)
				return true;
		}
	}
	return false;
}

static void door_close_check(edict_t *self)
{
	if (door_blocked_by_people(self)) {
		self->think = door_close_check;
		self->nextthink = level.time + 1.0f;
		return;
	}
	Mover_StartSound(self);
	if (self->spawnflags & DOOR_FIRE_ON_CLOSE)
		G_UseTargets(self, self);
	for (edict_t *e = self; e; e = e->teamchain)
		door_go_down(e);
}

void door_hit_top(edict_t *self)
{
	Mover_EndSound(self);
	self->moveinfo.state = STATE_TOP;
	if (self->spawnflags & DOOR_TOGGLE)
		return;
	if (self->flags & FL_TEAMSLAVE)
		return;
	if (IsPortcullis(self))
		return;
	if (self->moveinfo.wait >= 0) {
		self->think = door_close_check;
		self->nextthink = level.time + self->moveinfo.wait;
	}
}

void door_hit_bottom(edict_t *self)
{
	Mover_EndSound(self);
	self->moveinfo.state = STATE_BOTTOM;
	door_hold_portals(self, false);
}

void door_go_down(edict_t *self)
{
	if (self->max_health) {
		self->takedamage = DAMAGE_YES;
		self->health = self->max_health;
	}
	self->moveinfo.state = STATE_DOWN;
	if (IsRotatingDoor(self)) {
		if (!strcmp(self->classname, "func_door_rotating_smart")) {
			// reset the direction for the next opening (SPEC-761 1.2)
			if (self->doorflip) {
				VectorNegate(self->movedir, self->movedir);
				VectorMA(self->pos1, self->moveinfo.distance, self->movedir, self->moveinfo.end_angles);
				self->doorflip = 0;
			}
		}
		AngleMove_Calc(self, door_hit_bottom);
	} else
		Move_Calc(self, self->moveinfo.start_origin, door_hit_bottom);
}

static void smartdoor_direction(edict_t *self, edict_t *activator)
{
	if (strcmp(self->classname, "func_door_rotating_smart") || !activator)
		return;
	vec3_t o, c, a = {0, 0, 0}, n, d;
	VectorCopy(self->s.origin, o);
	for (int i = 0; i < 3; i++)
		c[i] = (self->absmin[i] + self->absmax[i]) * 0.5f;
	if (self->movedir[1])
		a[2] = 1; // yaw
	else if (self->movedir[0])
		a[1] = 1; // pitch
	else
		a[0] = 1; // roll
	VectorSubtract(c, o, d);
	CrossProduct(d, a, n);
	VectorSubtract(activator->s.origin, c, d);
	if (DotProduct(n, d) < 0 && !self->doorflip) {
		VectorNegate(self->movedir, self->movedir);
		VectorMA(self->pos1, self->moveinfo.distance, self->movedir, self->moveinfo.end_angles);
		self->doorflip = 1;
	}
}

void door_go_up(edict_t *self, edict_t *activator)
{
	if (self->moveinfo.state == STATE_UP)
		return;
	if (self->moveinfo.state == STATE_TOP) {
		if (self->moveinfo.wait >= 0 && !(self->flags & FL_TEAMSLAVE))
			self->nextthink = level.time + self->moveinfo.wait;
		return;
	}
	Mover_StartSound(self);
	self->moveinfo.state = STATE_UP;
	if (IsRotatingDoor(self)) {
		smartdoor_direction(self, activator);
		AngleMove_Calc(self, door_hit_top);
	} else
		Move_Calc(self, self->moveinfo.end_origin, door_hit_top);

	G_UseTargets(self, activator);
	door_hold_portals(self, true);
}

static void door_open_team(edict_t *self, edict_t *activator)
{
	// the activator is passed on unchanged (a script_runner stays the activator)
	for (edict_t *ent = self; ent; ent = ent->teamchain) {
		ent->message = NULL;
		ent->sp_message = 0;
		ent->touch = NULL;
		door_go_up(ent, activator);
	}
}

static void door_activate(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->flags & FL_TEAMSLAVE)
		self = self->teammaster ? self->teammaster : self;

	if (self->spawnflags & DOOR_TOGGLE) {
		if (self->moveinfo.state == STATE_UP || self->moveinfo.state == STATE_TOP) {
			door_close_check(self);
			return;
		}
	}
	door_open_team(self, (other && other->classname && !strcmp(other->classname, "script_runner")) ? other : activator);
}

// use by an event (target firing, scripts, damage)
void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *master = (self->flags & FL_TEAMSLAVE) && self->teammaster ? self->teammaster : self;
	if (master->spawnflags & DOOR_LOCKED) {
		for (edict_t *e = master; e; e = e->teamchain)
			if (e->spawnflags & DOOR_LOCKED)
				e->spawnflags &= ~DOOR_LOCKED;
		return;
	}
	door_activate(master, other, activator);
}

// direct use: use key or trigger field
static void door_direct_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *master = (self->flags & FL_TEAMSLAVE) && self->teammaster ? self->teammaster : self;
	if (master->targetname && !(master->spawnflags & DOOR_PLAYER_USABLE))
		return;
	if (master->spawnflags & DOOR_LOCKED)
		return;
	master->lastusetime = level.time;
	door_activate(master, other, activator);
}

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other->health <= 0)
		return;
	if (!(other->svflags & SVF_MONSTER) && !other->client)
		return;
	if ((self->owner->spawnflags & DOOR_NOMONSTER) && (other->svflags & SVF_MONSTER))
		return;
	if (level.time < self->touch_debounce_time)
		return;
	self->touch_debounce_time = level.time + 1.0;
	door_direct_use(self->owner, other, other);
}

static void DoorTrigger_UseKey(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->owner->targetname)
		return;
	door_direct_use(self->owner, other, activator);
}

void Think_CalcMoveSpeed(edict_t *self)
{
	if (self->flags & FL_TEAMSLAVE)
		return;
	float min = fabs(self->moveinfo.distance);
	for (edict_t *ent = self->teamchain; ent; ent = ent->teamchain) {
		float dist = fabs(ent->moveinfo.distance);
		if (dist < min)
			min = dist;
	}
	float time = min / self->moveinfo.speed;
	for (edict_t *ent = self; ent; ent = ent->teamchain) {
		float newspeed = fabs(ent->moveinfo.distance) / time;
		float ratio = newspeed / ent->moveinfo.speed;
		if (ent->moveinfo.accel == ent->moveinfo.speed)
			ent->moveinfo.accel = newspeed;
		else
			ent->moveinfo.accel *= ratio;
		if (ent->moveinfo.decel == ent->moveinfo.speed)
			ent->moveinfo.decel = newspeed;
		else
			ent->moveinfo.decel *= ratio;
		ent->moveinfo.speed = newspeed;
	}
}

// 0.1 s after spawn: trigger field (func_door 128), START_OPEN portals, team speeds
static void Think_DoorSetup(edict_t *ent)
{
	if (ent->spawnflags & DOOR_START_OPEN)
		door_hold_portals(ent, true);
	if (ent->flags & FL_TEAMSLAVE)
		return;

	if (!strcmp(ent->classname, "func_door") && (ent->spawnflags & DOOR_TRIGGER_FIELD)) {
		vec3_t mins, maxs;
		VectorCopy(ent->absmin, mins);
		VectorCopy(ent->absmax, maxs);
		for (edict_t *o = ent->teamchain; o; o = o->teamchain) {
			AddPointToBounds(o->absmin, mins, maxs);
			AddPointToBounds(o->absmax, mins, maxs);
		}
		mins[0] -= 60;
		mins[1] -= 60;
		maxs[0] += 60;
		maxs[1] += 60;
		edict_t *other = G_Spawn();
		VectorCopy(mins, other->mins);
		VectorCopy(maxs, other->maxs);
		other->owner = ent;
		other->solid = SOLID_TRIGGER;
		other->movetype = MOVETYPE_NONE;
		other->touch = Touch_DoorTrigger;
		other->plUse = DoorTrigger_UseKey;
		other->classname = (char *)"trigger_door";
		gi.linkentity(other);
		if (ent->spawnflags & DOOR_START_OPEN)
			door_toggle_portals(ent); // original quirk: toggled twice at spawn
	}
	Think_CalcMoveSpeed(ent);
}

// SPEC-760 B5.7 "Blocked"
void door_blocked(edict_t *self, edict_t *other)
{
	if (other->s.solid == SOLID_CORPSE)
		other->s.solid = 0;
	if (Mover_Crush(self, other))
		return;

	edict_t *master = self->teammaster ? self->teammaster : self;
	if (self->spawnflags & DOOR_CRUSHER) {
		if (other->client)
			return;
		vec3_t side = {-self->moveinfo.dir[1], self->moveinfo.dir[0], 0};
		VectorNormalize(side);
		VectorMA(other->s.origin, 50, side, other->s.origin);
		gi.linkentity(other);
		T_Damage(other, self, self, side, other->s.origin, vec3_origin, 1000, 100, 0x8000, MOD_CRUSH);
		return;
	}

	if (other->client) {
		if (self->moveinfo.wait >= 0) {
			if (self->moveinfo.state == STATE_DOWN) {
				for (edict_t *ent = master; ent; ent = ent->teamchain)
					door_go_up(ent, ent->activator);
			} else {
				door_close_check(master);
			}
		}
		return;
	}

	// characters: 1 damage pushing them away, the door keeps trying
	vec3_t dir;
	VectorSubtract(other->s.origin, self->s.origin, dir);
	dir[2] = 10;
	VectorNormalize(dir);
	T_Damage(other, self, self, dir, other->s.origin, vec3_origin, 1, 100, 0, MOD_CRUSH);
}

void door_killed(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	for (edict_t *ent = self->teammaster; ent; ent = ent->teamchain) {
		ent->health = ent->max_health;
		ent->takedamage = DAMAGE_NO;
	}
	door_use(self->teammaster, attacker, attacker);
}

void door_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (!other->client)
		return;
	if (level.time < self->touch_debounce_time)
		return;
	self->touch_debounce_time = level.time + 5.0;
	if (self->sp_message)
		gix.SP_Print(other, self->sp_message);
	else
		gi.centerprintf(other, "%s", self->message);
	gi.sound(other, CHAN_AUTO, gi.soundindex("Misc/Talk.wav"), 1, ATTN_NORM, 0);
}

static void door_common(edict_t *ent)
{
	if (ent->health) {
		ent->takedamage = DAMAGE_YES;
		ent->die = door_killed;
		ent->max_health = ent->health;
	}
	if (ent->targetname && (ent->message || ent->sp_message)) {
		gi.soundindex("misc/talk.wav");
		ent->touch = door_touch;
	}
	if (!ent->targetname || (ent->spawnflags & DOOR_PLAYER_USABLE))
		ent->plUse = door_direct_use;

	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = ent->wait;
	ent->moveinfo.state = STATE_BOTTOM;

	if (!ent->team)
		ent->teammaster = ent;

	gi.linkentity(ent);
	ent->nextthink = level.time + FRAMETIME;
	ent->think = Think_DoorSetup;
	Mover_DeferSounds(ent);
}

void SP_func_door(edict_t *ent)
{
	vec3_t abs_movedir;

	G_SetMovedir(ent->s.angles, ent->movedir);
	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	ent->blocked = door_blocked;
	ent->use = door_use;

	if (!ent->speed)
		ent->speed = 100;
	if (deathmatch->value)
		ent->speed *= 2;
	if (!ent->accel)
		ent->accel = ent->speed;
	if (!ent->decel)
		ent->decel = ent->speed;
	if (!ent->wait)
		ent->wait = 3;
	if (!st.lip)
		st.lip = 8;
	if (!ent->dmg)
		ent->dmg = 2;

	VectorCopy(ent->s.origin, ent->pos1);
	abs_movedir[0] = fabs(ent->movedir[0]);
	abs_movedir[1] = fabs(ent->movedir[1]);
	abs_movedir[2] = fabs(ent->movedir[2]);
	ent->moveinfo.distance =
		abs_movedir[0] * ent->size[0] + abs_movedir[1] * ent->size[1] + abs_movedir[2] * ent->size[2] - st.lip;
	VectorMA(ent->pos1, ent->moveinfo.distance, ent->movedir, ent->pos2);

	if (ent->spawnflags & DOOR_START_OPEN) {
		VectorCopy(ent->pos2, ent->s.origin);
		VectorCopy(ent->pos1, ent->pos2);
		VectorCopy(ent->s.origin, ent->pos1);
	}

	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);

	door_common(ent);
}

void SP_func_door_rotating(edict_t *ent)
{
	VectorClear(ent->s.angles);

	VectorClear(ent->movedir);
	if (ent->spawnflags & DOOR_X_AXIS)
		ent->movedir[2] = 1.0;
	else if (ent->spawnflags & DOOR_Y_AXIS)
		ent->movedir[0] = 1.0;
	else
		ent->movedir[1] = 1.0;

	// engine collision hint (SPEC-313 6, ERRATA)
	if (!(ent->spawnflags & DOOR_NO_AXIS_FLAG)) {
		if (ent->spawnflags & DOOR_X_AXIS)
			ent->svflags |= SVF_ROTATE_ROLL;
		else if (ent->spawnflags & DOOR_Y_AXIS)
			ent->svflags |= SVF_ROTATE_PITCH;
		else
			ent->svflags |= SVF_ROTATE_YAW;
	}

	bool smart = !strcmp(ent->classname, "func_door_rotating_smart");
	if ((ent->spawnflags & DOOR_REVERSE) && !smart)
		VectorNegate(ent->movedir, ent->movedir);

	if (!st.distance)
		st.distance = 90;

	VectorCopy(ent->s.angles, ent->pos1);
	VectorMA(ent->s.angles, st.distance, ent->movedir, ent->pos2);
	ent->moveinfo.distance = st.distance;

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	ent->blocked = door_blocked;
	ent->use = door_use;

	if (!ent->speed)
		ent->speed = 100;
	if (!ent->accel)
		ent->accel = ent->speed;
	if (!ent->decel)
		ent->decel = ent->speed;
	if (!ent->wait)
		ent->wait = 3;
	if (!ent->dmg)
		ent->dmg = 2;

	if (ent->spawnflags & DOOR_START_OPEN) {
		VectorCopy(ent->pos2, ent->s.angles);
		VectorCopy(ent->pos1, ent->pos2);
		VectorCopy(ent->s.angles, ent->pos1);
		VectorNegate(ent->movedir, ent->movedir);
	}

	VectorCopy(ent->s.origin, ent->moveinfo.start_origin);
	VectorCopy(ent->pos1, ent->moveinfo.start_angles);
	VectorCopy(ent->s.origin, ent->moveinfo.end_origin);
	VectorCopy(ent->pos2, ent->moveinfo.end_angles);

	door_common(ent);
}

// SPEC-761 1.2
void SP_func_door_rotating_smart(edict_t *ent) { SP_func_door_rotating(ent); }

// SPEC-761 1.1: portcullis — rises only while worked
static void portcullis_think(edict_t *self);

static void portcullis_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->count = 1;
	self->lastworked = level.time;
	self->lastusetime = level.time;
}

static void portcullis_hit_top(edict_t *self)
{
	Mover_EndSound(self);
	self->moveinfo.state = STATE_TOP;
	self->think = portcullis_think;
	self->nextthink = level.time + FRAMETIME;
}

static void portcullis_hit_bottom(edict_t *self)
{
	Mover_EndSound(self);
	self->moveinfo.state = STATE_BOTTOM;
	door_hold_portals(self, false);
	self->think = portcullis_think;
	self->nextthink = level.time + FRAMETIME;
}

static void portcullis_think(edict_t *self)
{
	if (level.time - self->lastworked > 0.2f + 0.001f)
		self->count = 0;

	bool moving = self->moveinfo.state == STATE_UP || self->moveinfo.state == STATE_DOWN;
	if (!self->count) {
		self->moveinfo.speed = self->moveinfo.accel = self->moveinfo.decel = self->volume;
		if (self->moveinfo.state == STATE_TOP || self->moveinfo.state == STATE_UP) {
			if (!door_blocked_by_people(self)) {
				Mover_StartSound(self);
				self->moveinfo.state = STATE_DOWN;
				Move_Calc(self, self->moveinfo.start_origin, portcullis_hit_bottom);
				moving = true;
			}
		}
	} else {
		self->moveinfo.speed = self->moveinfo.accel = self->moveinfo.decel = self->speed;
		if (self->moveinfo.state == STATE_DOWN || self->moveinfo.state == STATE_BOTTOM) {
			Mover_StartSound(self);
			self->moveinfo.state = STATE_UP;
			Move_Calc(self, self->moveinfo.end_origin, portcullis_hit_top);
			door_hold_portals(self, true);
			moving = true;
		}
	}

	// every tick: use the targets directly
	if (self->target) {
		edict_t *t = NULL;
		while ((t = G_Find(t, FOFS(targetname), self->target)))
			if (t != self && t->use)
				t->use(t, self, self);
	}

	if (!moving || self->think == portcullis_think) {
		self->think = portcullis_think;
		self->nextthink = level.time + FRAMETIME;
	}
}

void SP_func_door_portcullis(edict_t *ent)
{
	vec3_t abs_movedir;

	G_SetMovedir(ent->s.angles, ent->movedir);
	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);
	ent->blocked = door_blocked;
	ent->use = portcullis_use;

	if (!ent->speed)
		ent->speed = 20;
	if (deathmatch->value)
		ent->speed *= 2;
	if (!ent->volume)
		ent->volume = 10;
	if (!ent->accel)
		ent->accel = ent->speed;
	if (!ent->decel)
		ent->decel = ent->speed;
	if (!st.lip)
		st.lip = 8;
	if (!ent->dmg)
		ent->dmg = 2;
	ent->wait = 0;

	VectorCopy(ent->s.origin, ent->pos1);
	abs_movedir[0] = fabs(ent->movedir[0]);
	abs_movedir[1] = fabs(ent->movedir[1]);
	abs_movedir[2] = fabs(ent->movedir[2]);
	ent->moveinfo.distance =
		abs_movedir[0] * ent->size[0] + abs_movedir[1] * ent->size[1] + abs_movedir[2] * ent->size[2] - st.lip;
	VectorMA(ent->pos1, ent->moveinfo.distance, ent->movedir, ent->pos2);
	if (ent->spawnflags & DOOR_START_OPEN) {
		VectorCopy(ent->pos2, ent->s.origin);
		VectorCopy(ent->pos1, ent->pos2);
		VectorCopy(ent->s.origin, ent->pos1);
	}
	VectorCopy(ent->pos1, ent->moveinfo.start_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.start_angles);
	VectorCopy(ent->pos2, ent->moveinfo.end_origin);
	VectorCopy(ent->s.angles, ent->moveinfo.end_angles);

	if (ent->targetname && (ent->message || ent->sp_message)) {
		gi.soundindex("misc/talk.wav");
		ent->touch = door_touch;
	}
	ent->moveinfo.speed = ent->speed;
	ent->moveinfo.accel = ent->accel;
	ent->moveinfo.decel = ent->decel;
	ent->moveinfo.wait = 0;
	ent->moveinfo.state = STATE_BOTTOM;
	if (!ent->team)
		ent->teammaster = ent;
	gi.linkentity(ent);
	ent->think = portcullis_think;
	ent->nextthink = level.time + FRAMETIME;
	Mover_DeferSounds(ent);
}

//======================================================================
// func_water (SPEC-760 B5.8)

void SP_func_water(edict_t *self)
{
	vec3_t abs_movedir;

	G_SetMovedir(self->s.angles, self->movedir);
	self->movetype = MOVETYPE_PUSH;
	self->solid = SOLID_BSP;
	gi.setmodel(self, self->model);

	switch (self->sounds) {
	default:
		break;
	case 1:
	case 2:
		self->moveinfo.sound_start = gi.soundindex("world/mov_watr.wav");
		self->moveinfo.sound_end = gi.soundindex("world/stp_watr.wav");
		break;
	}

	VectorCopy(self->s.origin, self->pos1);
	abs_movedir[0] = fabs(self->movedir[0]);
	abs_movedir[1] = fabs(self->movedir[1]);
	abs_movedir[2] = fabs(self->movedir[2]);
	self->moveinfo.distance =
		abs_movedir[0] * self->size[0] + abs_movedir[1] * self->size[1] + abs_movedir[2] * self->size[2] - st.lip;
	VectorMA(self->pos1, self->moveinfo.distance, self->movedir, self->pos2);

	if (self->spawnflags & DOOR_START_OPEN) {
		VectorCopy(self->pos2, self->s.origin);
		VectorCopy(self->pos1, self->pos2);
		VectorCopy(self->s.origin, self->pos1);
	}

	VectorCopy(self->pos1, self->moveinfo.start_origin);
	VectorCopy(self->s.angles, self->moveinfo.start_angles);
	VectorCopy(self->pos2, self->moveinfo.end_origin);
	VectorCopy(self->s.angles, self->moveinfo.end_angles);

	self->moveinfo.state = STATE_BOTTOM;

	if (!self->speed)
		self->speed = 25;
	self->moveinfo.accel = self->moveinfo.decel = self->moveinfo.speed = self->speed;

	if (!self->wait)
		self->wait = -1;
	self->moveinfo.wait = self->wait;

	self->use = door_use;
	if (self->wait == -1)
		self->spawnflags |= DOOR_TOGGLE;

	self->classname = (char *)"func_door";
	if (!self->team)
		self->teammaster = self;
	gi.linkentity(self);
}

//======================================================================
// func_train (SPEC-760 B5.6)

#define TRAIN_START_ON 1
#define TRAIN_TOGGLE 2
#define TRAIN_BLOCK_STOPS 4

void train_next(edict_t *self);

void train_blocked(edict_t *self, edict_t *other)
{
	if (Mover_Crush(self, other))
		return;
	if (level.time < self->touch_debounce_time)
		return;
	if (!self->dmg)
		return;
	self->touch_debounce_time = level.time + 0.5;
	T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, self->dmg, 1, 0x8008, MOD_CRUSH);
}

void train_wait(edict_t *self)
{
	if (self->target_ent->pathtarget) {
		edict_t *ent = self->target_ent;
		char *savetarget = ent->target;
		ent->target = ent->pathtarget;
		G_UseTargets(ent, self->activator);
		ent->target = savetarget;
		if (!self->inuse)
			return;
	}

	if (self->moveinfo.wait) {
		if (self->moveinfo.wait > 0) {
			self->nextthink = level.time + self->moveinfo.wait;
			self->think = train_next;
		} else if (self->spawnflags & TRAIN_TOGGLE) {
			train_next(self);
			self->spawnflags &= ~TRAIN_START_ON;
			VectorClear(self->velocity);
			self->nextthink = 0;
		}
		Mover_EndSound(self);
	} else
		train_next(self);
}

void train_next(edict_t *self)
{
	edict_t *ent;
	vec3_t dest;
	qboolean first = true;
again:
	if (!self->target)
		return;

	ent = G_PickTarget(self->target);
	if (!ent) {
		gi.dprintf("train_next: bad target %s\n", self->target);
		return;
	}

	self->target = ent->target;

	if (ent->spawnflags & 1) {
		if (!first) {
			gi.dprintf("connected teleport path_corners, see %s at %s\n", ent->classname, vtos(ent->s.origin));
			return;
		}
		first = false;
		VectorSubtract(ent->s.origin, self->mins, self->s.origin);
		self->s.renderfx |= RF_NOLERP;
		gi.linkentity(self);
		goto again;
	}

	self->moveinfo.wait = ent->wait;
	self->target_ent = ent;

	Mover_StartSound(self);

	VectorSubtract(ent->s.origin, self->mins, dest);
	self->moveinfo.state = STATE_TOP;
	VectorCopy(self->s.origin, self->moveinfo.start_origin);
	VectorCopy(dest, self->moveinfo.end_origin);
	Move_Calc(self, dest, train_wait);
	self->spawnflags |= TRAIN_START_ON;
}

void train_resume(edict_t *self)
{
	edict_t *ent = self->target_ent;
	vec3_t dest;
	VectorSubtract(ent->s.origin, self->mins, dest);
	self->moveinfo.state = STATE_TOP;
	VectorCopy(self->s.origin, self->moveinfo.start_origin);
	VectorCopy(dest, self->moveinfo.end_origin);
	Move_Calc(self, dest, train_wait);
	self->spawnflags |= TRAIN_START_ON;
}

void func_train_find(edict_t *self)
{
	if (!self->target) {
		gi.dprintf("train_find: no target\n");
		return;
	}
	edict_t *ent = G_PickTarget(self->target);
	if (!ent) {
		gi.dprintf("train_find: target %s not found\n", self->target);
		return;
	}
	self->target = ent->target;
	VectorSubtract(ent->s.origin, self->mins, self->s.origin);
	gi.linkentity(self);
	if (!self->targetname)
		self->spawnflags |= TRAIN_START_ON;
	if (self->spawnflags & TRAIN_START_ON) {
		self->nextthink = level.time + FRAMETIME;
		self->think = train_next;
		self->activator = self;
	}
}

void train_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->activator = activator;
	if (self->spawnflags & TRAIN_START_ON) {
		if (!(self->spawnflags & TRAIN_TOGGLE))
			return;
		self->spawnflags &= ~TRAIN_START_ON;
		VectorClear(self->velocity);
		self->nextthink = 0;
	} else {
		if (self->target_ent)
			train_resume(self);
		else
			train_next(self);
	}
}

void SP_func_train(edict_t *self)
{
	self->movetype = MOVETYPE_PUSH;
	VectorClear(self->s.angles);
	self->blocked = train_blocked;
	if (self->spawnflags & TRAIN_BLOCK_STOPS)
		self->dmg = 0;
	else if (!self->dmg)
		self->dmg = 100;
	self->solid = SOLID_BSP;
	gi.setmodel(self, self->model);

	if (!self->speed)
		self->speed = 100;
	self->moveinfo.speed = self->speed;
	self->moveinfo.accel = self->moveinfo.decel = self->moveinfo.speed;
	self->use = train_use;
	gi.linkentity(self);
	Mover_DeferSounds(self);

	if (self->target) {
		self->nextthink = level.time + FRAMETIME;
		self->think = func_train_find;
	} else
		gi.dprintf("func_train without a target at %s\n", vtos(self->absmin));
}

// trigger_elevator (Quake II)
void trigger_elevator_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->movetarget->nextthink)
		return;
	if (!other->pathtarget) {
		gi.dprintf("elevator used with no pathtarget\n");
		return;
	}
	edict_t *target = G_PickTarget(other->pathtarget);
	if (!target) {
		gi.dprintf("elevator used with bad pathtarget: %s\n", other->pathtarget);
		return;
	}
	self->movetarget->target_ent = target;
	train_resume(self->movetarget);
}

void trigger_elevator_init(edict_t *self)
{
	if (!self->target) {
		gi.dprintf("trigger_elevator has no target\n");
		return;
	}
	self->movetarget = G_PickTarget(self->target);
	if (!self->movetarget) {
		gi.dprintf("trigger_elevator unable to find target %s\n", self->target);
		return;
	}
	if (strcmp(self->movetarget->classname, "func_train") != 0) {
		gi.dprintf("trigger_elevator target %s is not a train\n", self->target);
		return;
	}
	self->use = trigger_elevator_use;
	self->svflags = SVF_NOCLIENT;
}

void SP_trigger_elevator(edict_t *self)
{
	self->think = trigger_elevator_init;
	self->nextthink = level.time + FRAMETIME;
}

// func_timer (Quake II)
void func_timer_think(edict_t *self)
{
	G_UseTargets(self, self->activator);
	self->nextthink = level.time + self->wait + crandom() * self->random;
}

void func_timer_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->activator = activator;
	if (self->nextthink) {
		self->nextthink = 0;
		return;
	}
	if (self->delay)
		self->nextthink = level.time + self->delay;
	else
		func_timer_think(self);
}

void SP_func_timer(edict_t *self)
{
	if (!self->wait)
		self->wait = 1.0;
	self->use = func_timer_use;
	self->think = func_timer_think;
	if (self->random >= self->wait) {
		self->random = self->wait - FRAMETIME;
		gi.dprintf("func_timer at %s has random >= wait\n", vtos(self->s.origin));
	}
	if (self->spawnflags & 1) {
		self->nextthink = level.time + 1.0 + st.pausetime + self->delay + self->wait + crandom() * self->random;
		self->activator = self;
	}
	self->svflags = SVF_NOCLIENT;
}

// func_conveyor (Quake II)
void func_conveyor_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->spawnflags & 1) {
		self->speed = 0;
		self->spawnflags &= ~1;
	} else {
		self->speed = self->count;
		self->spawnflags |= 1;
	}
	if (!(self->spawnflags & 2))
		self->count = 0;
}

void SP_func_conveyor(edict_t *self)
{
	if (!self->speed)
		self->speed = 100;
	if (!(self->spawnflags & 1)) {
		self->count = self->speed;
		self->speed = 0;
	}
	self->use = func_conveyor_use;
	gi.setmodel(self, self->model);
	self->solid = SOLID_BSP;
	gi.linkentity(self);
}

//======================================================================
// func_door_secret (SPEC-760 B5.7: use key, no sounds, portals toggled)

#define SECRET_ALWAYS_SHOOT 1
#define SECRET_1ST_LEFT 2
#define SECRET_1ST_DOWN 4

void door_secret_move1(edict_t *self);
void door_secret_move2(edict_t *self);
void door_secret_move3(edict_t *self);
void door_secret_move4(edict_t *self);
void door_secret_move5(edict_t *self);
void door_secret_move6(edict_t *self);
void door_secret_done(edict_t *self);

void door_secret_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!VectorCompare(self->s.origin, vec3_origin))
		return;
	Move_Calc(self, self->pos1, door_secret_move1);
	door_hold_portals(self, true);
}

void door_secret_move1(edict_t *self)
{
	self->nextthink = level.time + 1.0;
	self->think = door_secret_move2;
}

void door_secret_move2(edict_t *self) { Move_Calc(self, self->pos2, door_secret_move3); }

void door_secret_move3(edict_t *self)
{
	if (self->wait == -1)
		return;
	self->nextthink = level.time + self->wait;
	self->think = door_secret_move4;
}

void door_secret_move4(edict_t *self) { Move_Calc(self, self->pos1, door_secret_move5); }

void door_secret_move5(edict_t *self)
{
	self->nextthink = level.time + 1.0;
	self->think = door_secret_move6;
}

void door_secret_move6(edict_t *self) { Move_Calc(self, vec3_origin, door_secret_done); }

void door_secret_done(edict_t *self)
{
	if (!(self->targetname) || (self->spawnflags & SECRET_ALWAYS_SHOOT)) {
		self->health = 0;
		self->takedamage = DAMAGE_YES;
	}
	door_hold_portals(self, false);
}

void door_secret_blocked(edict_t *self, edict_t *other)
{
	if (Mover_Crush(self, other))
		return;
	if (level.time < self->touch_debounce_time)
		return;
	self->touch_debounce_time = level.time + 0.5;
	T_Damage(other, self, self, vec3_origin, other->s.origin, vec3_origin, self->dmg, 1, 0, MOD_CRUSH);
}

void door_secret_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->takedamage = DAMAGE_NO;
	door_secret_use(self, attacker, attacker);
}

void SP_func_door_secret(edict_t *ent)
{
	vec3_t forward, right, up;

	ent->movetype = MOVETYPE_PUSH;
	ent->solid = SOLID_BSP;
	gi.setmodel(ent, ent->model);

	ent->blocked = door_secret_blocked;
	ent->use = door_secret_use;
	ent->plUse = door_secret_use;

	if (!(ent->targetname) || (ent->spawnflags & SECRET_ALWAYS_SHOOT)) {
		ent->health = 0;
		ent->takedamage = DAMAGE_YES;
		ent->die = door_secret_die;
	}
	if (!ent->dmg)
		ent->dmg = 2;
	if (!ent->wait)
		ent->wait = 5;

	ent->moveinfo.accel = ent->moveinfo.decel = ent->moveinfo.speed = 50;

	AngleVectors(ent->s.angles, forward, right, up);
	VectorClear(ent->s.angles);
	float side = 1.0 - (ent->spawnflags & SECRET_1ST_LEFT);
	float width;
	if (ent->spawnflags & SECRET_1ST_DOWN)
		width = fabs(DotProduct(up, ent->size));
	else
		width = fabs(DotProduct(right, ent->size));
	float length = fabs(DotProduct(forward, ent->size));
	if (ent->spawnflags & SECRET_1ST_DOWN)
		VectorMA(ent->s.origin, -1 * width, up, ent->pos1);
	else
		VectorMA(ent->s.origin, side * width, right, ent->pos1);
	VectorMA(ent->pos1, length, forward, ent->pos2);

	if (ent->health) {
		ent->takedamage = DAMAGE_YES;
		ent->die = door_killed;
		ent->max_health = ent->health;
	} else if (ent->targetname && (ent->message || ent->sp_message)) {
		gi.soundindex("misc/talk.wav");
		ent->touch = door_touch;
	}

	ent->classname = (char *)"func_door";
	gi.linkentity(ent);
}

// func_killbox (Quake II)
void use_killbox(edict_t *self, edict_t *other, edict_t *activator) { KillBox(self); }

void SP_func_killbox(edict_t *ent)
{
	gi.setmodel(ent, ent->model);
	ent->use = use_killbox;
	ent->svflags = SVF_NOCLIENT;
}
