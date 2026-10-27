// SPDX-License-Identifier: GPL-2.0-or-later
// OpenMerc game module - the new world entities of Soldier of Fortune that Quake II does not have:
// breakable/trigger/push-kill brushes, point helpers, the remote camera, level spawners, the world spawner
// and alert meter, the KOTH zone and test_model, plus shared world helpers (camera shake, explosion
// stimulus, noise reports). Characters spawned by spawners are placeholders until G2 exists.
// Implements: SPEC-761 2, 3, 4, 6, 7.4, 8, 9

#include "g_local.h"
#include "bots/bot_api.h" /* G5 */
#include "ai/ai_hooks.h" /* G2 */

void ED_CallSpawn(edict_t *ent);

static void PointHelper(edict_t *ent)
{
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	VectorSet(ent->mins, -4, -4, -4);
	VectorSet(ent->maxs, 4, 4, 4);
	ent->svflags |= SVF_NOCLIENT;
}

//======================================================================
// 2.1 func_breakable_brush

static void breakable_debris(edict_t *self, edict_t *attacker)
{
	if (self->material <= 0)
		return;
	vec3_t c, sz;
	for (int i = 0; i < 3; i++) {
		c[i] = (self->absmin[i] + self->absmax[i]) * 0.5f;
		sz[i] = self->absmax[i] - self->absmin[i];
	}
	float volume = sz[0] * sz[1] * sz[2];
	int count = (int)(volume / 9000 + 2);
	if (count < 10)
		count = 10;
	if (count > 20)
		count = 20;
	int size = volume > 250000 ? 3 : volume > 40000 ? 2 : 1;
	if (self->count) {
		count = self->count;
		size = self->mass;
	}
	vec3_t dir = {0, 0, 1};
	if (attacker && attacker != world) {
		VectorSubtract(attacker->s.origin, c, dir);
		VectorNormalize(dir);
	}
	// temp entity 18: debris chunks + break sound (SPEC-201 1)
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(18);
	gi.WritePosition(c);
	gi.WriteDir(dir);
	gi.WriteByte(count > 255 ? 255 : count);
	gi.WriteByte(size < 1 ? 1 : size > 3 ? 3 : size);
	gi.WriteByte(self->material > 34 ? 34 : self->material);
	gi.WriteByte((int)(sz[0] > 255 ? 255 : sz[0]));
	gi.WriteByte((int)(sz[1] > 255 ? 255 : sz[1]));
	gi.WriteByte((int)(sz[2] * 0.5f > 255 ? 255 : sz[2] * 0.5f));
	gi.WriteByte(self->surfacetype);
	gi.multicast(c, MULTICAST_PVS);
}

static void breakable_break(edict_t *self, edict_t *attacker)
{
	if (self->dmg) {
		float radius = self->volume ? self->volume : (float)self->dmg;
		vec3_t c;
		for (int i = 0; i < 3; i++)
			c[i] = (self->absmin[i] + self->absmax[i]) * 0.5f;
		VectorCopy(c, self->s.origin);
		T_RadiusDamage(self, attacker, self->dmg, self, radius, MOD_EXPLOSIVE);
		G_ExplosionStimulus(c, self->dmg, self->dmg);
	}
	breakable_debris(self, attacker);
	if (self->message && attacker && attacker->client) {
		gi.centerprintf(attacker, "%s", self->message);
		gi.sound(attacker, CHAN_AUTO, gi.soundindex("Misc/Talk.wav"), 0.6f, ATTN_NORM, 0);
	}
	if (self->target)
		RevealHiddenItems(self->target);
	G_UseTargets(self, self);
	G_FreeEdict(self);
}

static void breakable_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	self->takedamage = DAMAGE_NO;
	breakable_break(self, attacker);
}

static void breakable_pain(edict_t *self, edict_t *other, float kick, int damage, vec3_t point)
{
	if ((self->spawnflags & 2) && self->material > 0)
		breakable_debris(self, other);
}

static void breakable_use(edict_t *self, edict_t *other, edict_t *activator)
{
	breakable_break(self, activator);
}

void SP_func_breakable_brush(edict_t *ent)
{
	ent->solid = SOLID_BSP;
	ent->movetype = MOVETYPE_NONE;
	gi.setmodel(ent, ent->model);
	ent->material = (st.material >= 0 && st.material <= 34) ? st.material : 0;
	ent->surfacetype = (st.surfacetype >= 1 && st.surfacetype <= 42) ? st.surfacetype : 0;
	if (!ent->health)
		ent->health = (ent->material == 7 || ent->surfacetype == 37) ? 50 : 100;
	if (!ent->volume)
		ent->volume = (float)ent->dmg;
	if (!(ent->spawnflags & 1)) {
		ent->takedamage = DAMAGE_YES;
		ent->die = breakable_die;
		ent->pain = breakable_pain;
	}
	if (ent->targetname)
		ent->use = breakable_use;
	gi.linkentity(ent);
}

// 2.2 func_trigger_brush
static void triggerbrush_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	G_UseTargets(self, self);
	G_FreeEdict(self);
}

static void triggerbrush_use(edict_t *self, edict_t *other, edict_t *activator)
{
	self->takedamage = self->takedamage ? DAMAGE_NO : DAMAGE_YES;
}

void SP_func_trigger_brush(edict_t *ent)
{
	ent->solid = SOLID_BSP;
	ent->movetype = MOVETYPE_NONE;
	gi.setmodel(ent, ent->model);
	if (!ent->health)
		ent->health = 100;
	ent->die = triggerbrush_die;
	if (!(ent->spawnflags & 2))
		ent->takedamage = DAMAGE_YES;
	if (ent->spawnflags & 1)
		ent->svflags |= SVF_NOCLIENT;
	if (ent->targetname)
		ent->use = triggerbrush_use;
	gi.linkentity(ent);
}

// 2.3 func_pushkillbox
static void pushkillbox_touch(edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	if (other == world)
		return;
	vec3_t up = {0, 0, 1}, feet;
	VectorCopy(other->s.origin, feet);
	feet[2] += other->mins[2];
	T_Damage(other, self, self, up, feet, vec3_origin, 100000, 0, 0, MOD_CRUSH);
	if (!other->inuse)
		return;
	other->movetype = other->client ? MOVETYPE_DAN : MOVETYPE_NOCLIP;
	float a = self->count * (float)M_PI / 180.0f;
	VectorSet(other->velocity, cos(a) * self->volume, sin(a) * self->volume, 0);
	other->elasticity = -1;
}

void SP_func_pushkillbox(edict_t *ent)
{
	ent->solid = SOLID_BSP;
	ent->movetype = MOVETYPE_NONE;
	gi.setmodel(ent, ent->model);
	ent->clipmask = MASK_PLAYERSOLID | CONTENTS_MONSTERCLIP;
	ent->svflags |= SVF_NOCLIENT;
	ent->touch = pushkillbox_touch;
	gi.linkentity(ent);
}

//======================================================================
// 3. point helpers

static void alarm_use(edict_t *self, edict_t *other, edict_t *activator) { level.alarm = 1; }
void SP_func_alarm(edict_t *ent)
{
	PointHelper(ent);
	ent->use = alarm_use;
}

static void ignite_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->health <= 0)
		return;
	edict_t *list[1024];
	int n = G_RadiusList(self->s.origin, (float)self->health, list, 3);
	for (int i = 0; i < n; i++) {
		trace_t tr = gi.trace(self->s.origin, NULL, NULL, list[i]->s.origin, self, MASK_SOLID);
		if (tr.fraction == 1.0f || tr.ent == list[i])
			SetOnFire(list[i], self);
	}
}
void SP_func_ignite(edict_t *ent)
{
	PointHelper(ent);
	ent->use = ignite_use;
}

void SP_func_Dekker_marker(edict_t *ent) { PointHelper(ent); }

static void dekker_split_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *b = G_Find(NULL, FOFS(classname), "m_x_mraiderboss2");
	if (b && b->takedamage)
		T_Damage(b, self, self, vec3_origin, b->s.origin, vec3_origin, 1000, 0, 0, MOD_CRUSH);
}
void SP_func_Dekker_split(edict_t *ent)
{
	PointHelper(ent);
	ent->use = dekker_split_use;
}

// 3.5 holster toggle of the first player's player-module inventory (SPEC-003 slots 35/36)
static void hidegun_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *p = G_FirstPlayer();
	if (!p || !p->client->inv)
		return;
	if (self->wait == -1) {
		self->count = p->client->inv->Holstered() ? 1 : 0;
		self->wait = 0;
		return;
	}
	p->client->inv->ToggleHolster();
	self->health = p->client->inv->Holstered() ? 1 : 0;
}
void SP_func_hide_the_players_gun(edict_t *ent)
{
	PointHelper(ent);
	edict_t *p = G_FirstPlayer();
	ent->health = (p && p->client->inv && p->client->inv->Holstered()) ? 1 : 0;
	ent->use = hidegun_use;
}

static void sniperzoom_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *p = G_FirstPlayer();
	if (!p)
		return;
	float fov = self->wait == -1 ? 95 : self->wait < 6 ? 6 : self->wait > 60 ? 60 : self->wait;
	p->client->ps.fov = fov;
	if (ei_show->value)
		gi.dprintf("new fov = %d\n", (int)fov);
}
void SP_func_snipercam_zoom(edict_t *ent)
{
	PointHelper(ent);
	ent->use = sniperzoom_use;
}

static void missionstatus_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (other && other->targetname && !strcmp(other->targetname, "exitwarning_on"))
		level.missionstatus = 2;
	else if (other && other->targetname && !strcmp(other->targetname, "exitwarning_off"))
		level.missionstatus = 0;
	else
		level.missionstatus = self->count;
}
void SP_func_mission_status(edict_t *ent)
{
	PointHelper(ent);
	ent->use = missionstatus_use;
}

static void playerhealth_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *p = G_FirstPlayer();
	if (!p)
		return;
	p->health += self->count;
	if (p->health < 0)
		p->health = 0;
	if (p->health > 100)
		p->health = 100;
}
void SP_func_player_health(edict_t *ent)
{
	PointHelper(ent);
	ent->use = playerhealth_use;
}

static void playerarmor_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *p = G_FirstPlayer();
	if (p && p->client->inv)
		p->client->inv->AddArmor(self->count);
}
void SP_func_player_armor(edict_t *ent)
{
	PointHelper(ent);
	ent->use = playerarmor_use;
}

static void playeritem_use(edict_t *self, edict_t *other, edict_t *activator)
{
	edict_t *p = G_FirstPlayer();
	if (!self->count) {
		gi.dprintf("hey! you can't add zero items to the player's inventory!\n");
		return;
	}
	if (!p || !p->client->inv) {
		gi.dprintf("hey! no inventory!\n");
		return;
	}
	if (self->health < 1 || self->health > 7) {
		gi.dprintf("hey! adding an invalid item type to the player's inventory!\n");
		return;
	}
	p->client->inv->AddItems(self->health, self->count, -1);
}
void SP_func_player_item(edict_t *ent)
{
	PointHelper(ent);
	ent->use = playeritem_use;
}

static void score_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (!activator || !activator->client)
		return;
	if (self->team) {
		const char *tn = Info_ValueForKey(activator->client->pers.userinfo, "teamname");
		if (strcmp(tn, self->team))
			return;
	}
	activator->client->resp.score += self->count;
}
void SP_func_score(edict_t *ent)
{
	PointHelper(ent);
	ent->takedamage = DAMAGE_NO;
	ent->use = score_use;
}

// 3.12 camera shake: entity events 16 + level, parameter 30
void G_CameraShake(edict_t *player, float intensity)
{
	int lvl = intensity > 80 ? 4 : intensity > 55 ? 3 : intensity > 35 ? 2 : intensity > 17 ? 1 : 0;
	G_AddEvent(player, 16 + lvl, 30);
}

static void camerashake_think(edict_t *self)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client)
			continue;
		vec3_t c, d;
		for (int j = 0; j < 3; j++)
			c[j] = p->s.origin[j] + (p->mins[j] + p->maxs[j]) * 0.5f;
		VectorSubtract(c, self->s.origin, d);
		float dist = VectorLength(d);
		if (dist > self->health)
			continue;
		G_CameraShake(p, (1.0f - dist / self->health) * self->count);
	}
	if (level.time + 0.2f <= self->timestamp + 0.001f)
		self->nextthink = level.time + 0.2f;
}

static void camerashake_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->style == 1)
		gi.sound(self, CHAN_VOICE, gi.soundindex("Ambient/Gen/Battle/ExpRvb.wav"), 0.8f, ATTN_NONE, 0);
	self->timestamp = level.time + self->delay;
	camerashake_think(self);
}

void SP_func_camerashake(edict_t *ent)
{
	PointHelper(ent);
	if (!ent->count)
		ent->count = 30;
	if (!ent->health)
		ent->health = 300;
	if (!ent->delay)
		ent->delay = 3;
	if (ent->style == 1)
		gi.soundindex("Ambient/Gen/Battle/ExpRvb.wav");
	ent->use = camerashake_use;
	ent->think = camerashake_think;
}

// 3.13 func_fade
static void fade_think(edict_t *self)
{
	bool more = false;
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client)
			continue;
		gclient_t *c = p->client;
		float step = 0.1f / self->delay;
		if (self->spawnflags & 1) {
			c->fade_alpha -= step;
			if (c->fade_alpha < 0)
				c->fade_alpha = 0;
			else
				more = true;
		} else {
			c->fade_alpha += step;
			if (c->fade_alpha > 1)
				c->fade_alpha = 1;
			else
				more = true;
		}
	}
	if (more)
		self->nextthink = level.time + FRAMETIME;
}

static void fade_use(edict_t *self, edict_t *other, edict_t *activator)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client || strcmp(p->classname, "player"))
			continue;
		VectorCopy(self->move_origin, p->client->fade_color);
		p->client->fade_alpha = (self->spawnflags & 1) ? 1.0f + 0.1f / self->delay : 0;
	}
	self->nextthink = level.time + FRAMETIME;
}

void SP_func_fade(edict_t *ent)
{
	PointHelper(ent);
	VectorCopy(st.color, ent->move_origin);
	if (!ent->delay)
		ent->delay = 5;
	ent->use = fade_use;
	ent->think = fade_think;
}

//======================================================================
// 4. func_remote_camera

static void camera_aim(edict_t *self)
{
	edict_t *t = self->enemy;
	if (!t)
		return;
	vec3_t d;
	VectorSubtract(t->s.origin, self->s.origin, d);
	vectoangles(d, self->s.angles);
}

static void camera_apply(edict_t *self, edict_t *p)
{
	gclient_t *c = p->client;
	const float *org = self->s.origin;
	const float *ang = self->s.angles;
	if (self->ghoulflags & 16 && self->enemy) {
		org = self->enemy->s.origin;
		ang = self->enemy->s.angles;
	}
	for (int i = 0; i < 3; i++) {
		c->ps.remote_vieworigin[i] = org[i] * 8;
		c->ps.remote_viewangles[i] = ang[i];
	}
	c->ps.remote_id = G_EdictNum(self);
	c->ps.remote_type = (int)self->volume;
	c->remote_type = (int)self->volume;
}

// SPEC-751 2.5: watchers still looking through this camera (their remote view entity is this camera)
static void camera_think(edict_t *self)
{
	if (!self->count)
		return;
	if (!(self->spawnflags & 8))
		camera_aim(self);
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (p->inuse && p->client && p->client->ps.remote_id == G_EdictNum(self) && p->health > 0)
			camera_apply(self, p);
	}
	self->nextthink = level.time + FRAMETIME;
}

// the watchers of a camera: flag 1 the stored activator, otherwise every in-use client (SPEC-751 2.3 step 4, 2.4)
static bool camera_watcher(edict_t *self, edict_t *p)
{
	if (!p->inuse || !p->client)
		return false;
	return !(self->spawnflags & 1) || p == self->oldenemy;
}

// SPEC-751 2.4
static void camera_deactivate(edict_t *self)
{
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!camera_watcher(self, p))
			continue;
		gclient_t *c = p->client;
		if (self->style)
			c->ps.fov = c->camera_savedfov;
		c->camera_nest--; // effects bit 0x4000 is cleared by the per-frame stats step (p_view.cpp, AMEND-005 G6b.2)
		if (c->camera == self)
			c->camera = NULL;
		c->ps.remote_id = -1;
		c->ps.remote_type = 0;
		c->remote_type = 0;
		if (self->spawnflags & 2) {
			p->solid = SOLID_BBOX; // SPEC-AMEND-006 G18.35: solid type 2 again, not relinked here
			p->flags &= ~FL_GODMODE;
		}
	}
	self->count = 0;
	if (!(self->spawnflags & 4))
		G_FreeEdict(self);
}

// SPEC-751 2.3
static void camera_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (activator && activator->client && activator->health <= 0)
		return;
	// step 2: the camera the activator views now is found by its remote view entity number
	if (activator && activator->client) {
		int rv = activator->client->ps.remote_id;
		if (rv > 0 && rv != G_EdictNum(self)) {
			edict_t *old = rv < globals.num_edicts ? &g_edicts[rv] : NULL;
			if (old && old->inuse && old->classname && !strcmp(old->classname, "func_remote_camera"))
				camera_deactivate(old);
			else
				gi.dprintf("Error in Camera Scripting!  Check your 'use' and 'remove' commands!/n");
		}
	}
	if (self->count) {
		camera_deactivate(self);
		return;
	}
	if ((self->spawnflags & 1) && (!activator || !activator->client)) {
		gi.dprintf("Error in Camera Scripting!  Activator (%s) is not a client!\n",
		           activator && activator->classname ? activator->classname : "none");
		return;
	}
	self->count = 1;
	self->oldenemy = activator;
	self->enemy = G_Find(NULL, FOFS(targetname), self->target);
	if (self->enemy && self->enemy->classname &&
	    (!strcmp(self->enemy->classname, "misc_generic_security_camera") ||
	     !strcmp(self->enemy->classname, "misc_generic_security_camera2")))
		self->ghoulflags |= 16;
	camera_aim(self);
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!camera_watcher(self, p))
			continue;
		gclient_t *c = p->client;
		c->camera_savedfov = c->ps.fov;
		if (self->style)
			c->ps.fov = self->style;
		c->camera_nest++;
		c->camera = self;
		camera_apply(self, p);
		// flag 2 (SPEC-AMEND-006 G18.35): the watcher gets solid type 0 and the god-mode flag; clip mask, contents
		// and box stay. The player is not relinked while watching (ClientThink skips its link), so the packed box of
		// the last link keeps being sent, as in the oracle (tsr1, ger2 … spsuite)
		if (self->spawnflags & 2) {
			p->solid = SOLID_NOT;
			p->flags |= FL_GODMODE;
		}
	}
	self->think = camera_think;
	self->nextthink = level.time + FRAMETIME;
}

void SP_func_remote_camera(edict_t *ent)
{
	// material-flag position 24 = `p` (AMEND-005 G18.27) — only for a camera of type (`volume`) 4, the "extra
	// resource group" of SPEC-751 2.1: ger1's type-2 cameras leave it `n` in the oracle, tsr1's and raven's type-4
	// cameras set it (captures spsuite_orig_ger1, sp_orig_tsr1, suite1_orig_dm_raven; OPEN_QUESTIONS G18 34)
	if ((int)ent->volume == 4)
		G_SetMaterialFlag(24, 'p');
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	VectorSet(ent->mins, -4, -4, -4);
	VectorSet(ent->maxs, 4, 4, 4);
	ent->count = 0;
	if (!ent->target) {
		gi.dprintf("Object 'func_remote_camera' without a target.\n");
		G_FreeEdict(ent);
		return;
	}
	ent->use = camera_use;
	ent->svflags |= SVF_NOCLIENT;
	gi.linkentity(ent);
}

//======================================================================
// 6. level spawners (characters are G2's; until then the spawned classnames are placeholders)

static void spawner_box(edict_t *ent)
{
	int yaw = ((int)ent->s.angles[YAW] % 360 + 360) % 360;
	if (yaw == 90 || yaw == 270) {
		VectorSet(ent->mins, -5, -5, -7);
		VectorSet(ent->maxs, 5, 5, 12);
	} else {
		VectorSet(ent->mins, -5, -5, -7);
		VectorSet(ent->maxs, 5, 5, 12);
	}
}

static void spawner_think(edict_t *self);

static void spawner_use(edict_t *self, edict_t *other, edict_t *activator)
{
	if (self->spawnflags & 128) {
		self->armed = 1;
		return;
	}
	if (self->nextthink) {
		self->nextthink = 0;
		return;
	}
	self->think = spawner_think;
	self->nextthink = level.time + 1.0f;
}

static void spawner_think(edict_t *self)
{
	const char *classes[3] = {self->spawn1, self->spawn2, self->spawn3};
	int nclasses = 0;
	for (int i = 0; i < 3; i++)
		if (classes[i] && classes[i][0])
			nclasses++;
	if (!nclasses) {
		gi.dprintf("spawner set to spawn nothing!\n");
		self->nextthink = level.time + 99999.9f;
		return;
	}
	if (self->health == 0 && self->itemcount) {
		// exhausted: kill target once the spawned characters are gone (characters are inert placeholders)
		if (self->killtarget) {
			if (ei_show->value)
				gi.dprintf("Level spawner running kill target\n");
			edict_t *t = NULL;
			while ((t = G_Find(t, FOFS(targetname), self->killtarget)))
				if (t->use)
					t->use(t, self, self);
		}
		G_FreeEdict(self);
		return;
	}
	float interval = level.alarm ? self->wait : self->delay;
	if (interval == -1) {
		self->nextthink = level.time + 1.0f;
		return;
	}
	if (!(self->spawnflags & 128) && level.time - self->timestamp <= interval) {
		self->nextthink = level.time + 1.0f;
		return;
	}
	if (game.cinematic) {
		if (ei_show->value)
			gi.dprintf("Level spawner can't spawn - either has too many kids or is in cinematics\n");
		self->nextthink = level.time + 1.0f;
		return;
	}
	for (int k = 0; k < self->count; k++) {
		if (self->itemcount && self->health <= 0)
			break;
		const char *cls = classes[0];
		if (!(self->spawnflags & 2)) {
			int pick = rand() % 3;
			for (int t = 0; t < 3 && !(classes[pick] && classes[pick][0]); t++)
				pick = (pick + 1) % 3;
			cls = classes[pick];
		}
		if (!cls || !cls[0])
			cls = classes[0] ? classes[0] : classes[1] ? classes[1] : classes[2];
		edict_t *e = G_Spawn();
		e->classname = (char *)cls;
		VectorCopy(self->s.origin, e->s.origin);
		e->s.origin[2] += 35;
		VectorCopy(self->s.angles, e->s.angles);
		e->target = self->target;
		VectorSet(e->mins, -16, -16, -32);
		VectorSet(e->maxs, 16, 16, 41);
		trace_t tr = gi.trace(e->s.origin, e->mins, e->maxs, e->s.origin, e, MASK_MONSTERSOLID);
		if (tr.startsolid) {
			G_FreeEdict(e);
			if (ei_show->value)
				gi.dprintf("Level spawner can't spawn - space is occupied\n");
			break;
		}
		ED_CallSpawn(e);
		if (!e->inuse) {
			gi.dprintf("Couldn't spawn entity type %s!\n", cls);
			break;
		}
		e->flags |= FL_SPAWNED;
		if (self->itemcount)
			self->health--;
		self->timestamp = (float)(int)level.time;
		if (ei_show->value)
			gi.dprintf("Level spawner spawned a guy\n");
	}
	self->nextthink = level.time + (interval > 0 ? interval : 1.0f);
}

static void spawner_common(edict_t *ent)
{
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_NOT;
	ent->s.modelindex = 0;
	ent->clipmask = 0x02030003;
	ent->svflags |= SVF_NOCLIENT;
	spawner_box(ent);
	ent->classname = (char *)"spawner";
	ent->timestamp = level.time;
	const char *sp[3] = {ent->spawn1, ent->spawn2, ent->spawn3};
	for (int i = 0; i < 3; i++) {
		if (!sp[i] || !sp[i][0])
			continue;
		edict_t *e = G_Spawn();
		e->classname = (char *)sp[i];
		spawn_temp_t save = st;
		memset(&st, 0, sizeof(st));
		ED_CallSpawn(e);
		st = save;
		if (e->inuse)
			G_FreeEdict(e);
	}
	if (!ent->delay)
		ent->delay = 30;
	if (!ent->wait)
		ent->wait = 10;
	if (!ent->count)
		ent->count = 1;
	if (!ent->mass)
		ent->mass = 1;
	if (!st.lip)
		st.lip = 450;
	ent->itemcount = ent->health ? 1 : 0; // total limited
	ent->use = spawner_use;
	ent->think = spawner_think;
	if (!(ent->spawnflags & 1) && !(ent->spawnflags & 128))
		ent->nextthink = level.time + 1.0f;
	gi.linkentity(ent);
}

void SP_spawner(edict_t *ent) { spawner_common(ent); }

void SP_spawner_monster(edict_t *ent)
{
	ent->spawnflags |= 64;
	spawner_common(ent);
}

static void boosterpack_think(edict_t *self)
{
	if (self->gotsky)
		return;
	edict_t *p = G_FirstPlayer();
	if (p && gi.inPVS(p->s.origin, self->s.origin)) {
		trace_t tr = gi.trace(p->s.origin, NULL, NULL, self->s.origin, p, MASK_SOLID);
		if (tr.fraction == 1.0f) {
			self->gotsky = 1;
			return;
		}
	}
	if (self->armed)
		spawner_think(self);
	self->think = boosterpack_think;
	self->nextthink = level.time + 0.4f;
}

void SP_spawner_boosterpack(edict_t *ent)
{
	if (!ent->health)
		ent->health = 8;
	if (!ent->count)
		ent->count = 1;
	if (!ent->mass)
		ent->mass = 20;
	if (!ent->delay)
		ent->delay = 1;
	if (!ent->wait)
		ent->wait = 1;
	ent->spawnflags |= 192;
	spawner_common(ent);
	ent->think = boosterpack_think;
	ent->nextthink = level.time + 0.4f;
}

//======================================================================
// 7.4 / 8: stimuli and the world spawner alert meter (characters are G2's)

void G_ExplosionStimulus(const vec3_t center, float radius, float strength)
{
	om_ai::ExplosionStimulus(center, radius, strength); /* G2: hearing muting of characters (SPEC-700 noise broadcast) */
}

void G_ReportNoise(edict_t *source, const vec3_t where, float radius, int kind)
{
	if (source && source->client)
		Bot_PlayerNoise(source, 1.0f); /* G5: bots always hear player noises (SPEC-780 22, SPEC-741 6) */
	G_TriggerSoundNoise(source, where);
	om_ai::NoiseAlert(source, where, radius); /* G2: alert sound kind 2 to characters (SPEC-701 6) */
}

void G_AlertNoise(float amount)
{
	if (deathmatch->value || game.spawn_alertgain <= 0.01f || !ai_spawnfrequency || ai_spawnfrequency->value <= 0.01f)
		return;
	if (level.time < level.alert_cooldown + game.spawn_alertfade)
		return;
	float g = game.spawn_alertgain * ai_spawnfrequency->value;
	if (g < 0.1f)
		g = 0.1f;
	level.alert += g * amount * 0.7f;
	float cap = (ai_spawnfrequency->value < 0.1f || game.spawn_alertgain < 0.1f) ? 0.4f : 1.0f;
	if (level.alert > cap)
		level.alert = cap;
}

void G_WorldSpawnerFrame(void)
{
	edict_t *p = G_FirstPlayer();
	if (!p)
		return;
	if (level.alert_cooldown == 0)
		level.alert_cooldown = -999;
	// SPEC-AMEND-006 G18.32: spawning is enabled in single player outside a cinematic with ai_spawnfrequency and the
	// skill's alert gain both > 0.01 (exclusive); then the phases run and the decay is raised to 0 when negative.
	// Otherwise (also deathmatch) A only decays, without sounds and without the lower limit (towards −0.2): the
	// oracle's meter goes −1 … −19 on tsr1, ger1, irq1a … and stays 0 on arm1 (spsuite captures).
	bool enabled = !deathmatch->value && !game.cinematic && ai_spawnfrequency && ai_spawnfrequency->value > 0.01f &&
	               game.spawn_alertgain > 0.01f;
	if (!enabled) {
		level.alert -= (level.alert * 0.015f + 0.003f) * 0.7f;
		return;
	}
	float C = level.alert_cooldown, F = game.spawn_alertfade;
	if (enabled && level.time < C) {
		level.alert = 1;
		if (level.time - level.last_alert_sound >= 0.75f) {
			gi.sound(p, CHAN_AUTO, gi.soundindex("misc/padd/max.wav"), 1, ATTN_NONE, 0);
			level.last_alert_sound = level.time;
		}
		return;
	}
	if (enabled && level.time < C + F) {
		level.alert = 1.0f - (level.time - C) / F;
		return;
	}
	if (enabled && level.alert >= 1)
		level.alert_cooldown = level.time + game.spawn_maxalert;
	level.alert -= (level.alert * 0.015f + 0.003f) * 0.7f;
	if (level.alert < 0)
		level.alert = 0;
	float interval = 0;
	if (level.alert > 0.75f)
		interval = 1.0f - (level.alert - 0.75f) * 3.2f;
	else if (level.alert > 0.5f)
		interval = 3.0f - 8.0f * (level.alert - 0.5f);
	if (interval > 0 && level.time - level.last_alert_warning >= interval) {
		gi.sound(p, CHAN_AUTO, gi.soundindex("misc/padd/warning.wav"), 0.4f, ATTN_NONE, 0);
		level.last_alert_warning = level.time;
	}
}

//======================================================================
// 9. other

void SP_dm_KOTH_targetzone(edict_t *ent)
{
	ent->solid = SOLID_NOT;
	ent->movetype = MOVETYPE_NONE;
	gi.setmodel(ent, ent->model);
	for (int i = 0; i < 3; i++)
		ent->s.origin[i] = (ent->absmin[i] + ent->absmax[i]) * 0.5f;
	ent->svflags |= SVF_NOCLIENT;
	gi.linkentity(ent);
}

void SP_test_model(edict_t *ent)
{
	ent->movetype = MOVETYPE_NONE;
	ent->solid = SOLID_BBOX;
	VectorSet(ent->mins, -16, -16, -16);
	VectorSet(ent->maxs, 16, 16, 16);
	if (ent->model)
		ent->s.modelindex = gi.modelindex(ent->model);
	gi.linkentity(ent);
}

// the single-player status meter shows the world spawner's alert level (SPEC-761 8 "PADD", SPEC-790 B11);
// replaces the G9 default (modes/gm_shim.cpp, bound by /alternatename)
extern "C" float G_StealthValue(void)
{
	return level.alert > 1 ? 1 : level.alert; // negative values are shown (stats[11] −14 in the oracle)
}
