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
 * OpenMerc: rewritten for the SoF 1.07f game module (G18, first slice): the parts of the damage model
 * the world core, players, items and projectiles need — who can be damaged, team damage, player
 * toughness/protection/armour through the player module, knockback, death/pain callbacks, area damage
 * variants, CanDamage. Character bodies, gore zones and AI damage rules belong to G3 (SPEC-720..722).
 * Implements: SPEC-720 1-5, 8, 13 (players without bodies), 14, 15
 */

#include "g_local.h"
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */
#include "bots/bot_api.h" /* G5 */
#include "damage/damage_api.h" /* G3 */

void G_AddEvent(edict_t *ent, int ev, int parm);

qboolean CanDamage(edict_t *targ, edict_t *inflictor)
{
	vec3_t dest;
	trace_t trace;

	if (targ->movetype == MOVETYPE_PUSH || targ->solid == SOLID_BSP) {
		VectorAdd(targ->absmin, targ->absmax, dest);
		VectorScale(dest, 0.5, dest);
		trace = gi.trace(inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_OPAQUE);
		if (trace.fraction == 1.0)
			return true;
		if (trace.ent == targ)
			return true;
		return false;
	}

	trace = gi.trace(inflictor->s.origin, vec3_origin, vec3_origin, targ->s.origin, inflictor, MASK_OPAQUE);
	if (trace.fraction == 1.0)
		return true;
	static const float ofs[4][2] = {{15, 15}, {15, -15}, {-15, 15}, {-15, -15}};
	for (int i = 0; i < 4; i++) {
		VectorCopy(targ->s.origin, dest);
		dest[0] += ofs[i][0];
		dest[1] += ofs[i][1];
		trace = gi.trace(inflictor->s.origin, vec3_origin, vec3_origin, dest, inflictor, MASK_OPAQUE);
		if (trace.fraction == 1.0)
			return true;
	}
	return false;
}

void Killed(edict_t *targ, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	if (targ->health < -999)
		targ->health = -999;
	targ->enemy = attacker;
	if (targ->die)
		targ->die(targ, inflictor, attacker, damage, point);
}

qboolean OnSameTeam(edict_t *ent1, edict_t *ent2)
{
	return GM_SameTeam(ent1, ent2); /* G9: SPEC-790 4 */
	if (!ent1->client || !ent2->client)
		return false;
	int dm = (int)deathmatch->value;
	bool teamgame = dm == 4 || dm == 6 || ((int)dmflags->value & 0x80);
	if (!teamgame)
		return false;
	if (dm == 4 || dm == 6 || dm == 7)
		return ent1->client->resp.team && ent1->client->resp.team == ent2->client->resp.team;
	return !Q_stricmp(ent1->client->teamname, ent2->client->teamname);
}

// SPEC-780 3: damage direction of the last hit
static int DamageDirection(edict_t *targ, edict_t *attacker, int dflags)
{
	if (dflags & (DAMAGE_FIRE | DAMAGE_FALL | DAMAGE_DROWN))
		return (dflags & DAMAGE_FALL) ? 0x2 : 0x10;
	if (!attacker || attacker == targ)
		return 0;
	vec3_t d;
	VectorSubtract(attacker->s.origin, targ->s.origin, d);
	float yaw = vectoyaw(d);
	float py = targ->client ? targ->client->v_angle[YAW] : targ->s.angles[YAW];
	float a = anglemod(yaw - py);
	if (a < 30 || a > 340 || (a > 150 && a < 210))
		return 0x10;
	if (a >= 210)
		return 0x4;
	return 0x8;
}

/*
============
T_Damage (SPEC-720)
============
*/
void T_Damage(edict_t *targ, edict_t *inflictor, edict_t *attacker, const vec3_t dir_in, const vec3_t point,
              const vec3_t normal, int damage, int knockback, int dflags, int mod)
{
	/* G3: the damage function of SPEC-720 (the 6th value is the shot start / explosion centre) */
	Dmg_Damage(targ, inflictor, attacker, dir_in, point, normal, damage, knockback, dflags, mod);
	return;
	vec3_t dir;

	if (!targ->takedamage)
		return;
	if (!inflictor)
		inflictor = world;
	if (!attacker)
		attacker = world;

	// team damage
	if (targ != attacker && deathmatch->value && targ->client && attacker->client && OnSameTeam(targ, attacker)) {
		if ((int)dmflags->value & 0x400) {
			if (!(dflags & DAMAGE_NO_PROTECTION))
				damage = 0;
			dflags |= 0x40;
		} else
			mod |= MOD_FRIENDLY_FIRE;
	}
	meansOfDeath = mod;
	Bot_Damaged(targ, attacker); /* G5: an attacker becomes a bot's enemy (SPEC-741 6) */
	GM_ClientDamaged(targ, attacker); /* G9: SPEC-790 B8 hurt-carrier record */

	// single player: player damage scaled by toughness
	gclient_t *client = targ->client;
	if (client && !deathmatch->value && damage > 0) {
		float t = game.tparm[0];
		int d = (int)(t * damage);
		damage = d < 1 ? 1 : d;
	}

	VectorCopy(dir_in, dir);
	VectorNormalize(dir);

	if (targ->flags & FL_NO_KNOCKBACK)
		knockback = 0;

	// knockback (SPEC-720 8)
	if (knockback && !(dflags & DAMAGE_NO_KNOCKBACK) && targ->movetype != MOVETYPE_NONE &&
	    targ->movetype != MOVETYPE_PUSH && targ->movetype != MOVETYPE_STOP) {
		if (client) {
			vec3_t v;
			float mass = targ->mass ? (float)targ->mass : 200.0f;
			if (attacker && attacker != world && !(dflags & DAMAGE_EXPLOSIVE_HALF)) {
				VectorSubtract(point, attacker->s.origin, v);
				VectorNormalize(v);
				float k = 250.0f * knockback / mass;
				targ->velocity[0] += v[0] * k;
				targ->velocity[1] += v[1] * k;
				targ->velocity[2] += v[2] * k * 0.25f;
			} else {
				VectorSet(v, dir[0], dir[1], dir[2] * 0.1f);
				VectorNormalize(v);
				float k = 1000.0f * knockback / mass;
				VectorMA(targ->velocity, k, v, targ->velocity);
				if (targ->velocity[2] > 200)
					targ->velocity[2] = 200;
				client->stagger_time = level.time + 0.5f + 0.75f * knockback / mass;
			}
		} else {
			float mass = targ->mass < 50 ? 50.0f : (float)targ->mass;
			vec3_t v;
			VectorSet(v, dir[0], dir[1], dir[2] * ((dflags & DAMAGE_EXPLOSIVE_HALF) ? 0.1f : 0.25f));
			VectorNormalize(v);
			VectorMA(targ->velocity, 250.0f * knockback / mass, v, targ->velocity);
		}
	}

	int take = damage;

	// god mode, spawn protection (SPEC-720 3.3)
	if (!(dflags & DAMAGE_NO_PROTECTION)) {
		if (targ->flags & FL_GODMODE)
			take = 0;
		if (client && client->spawnprotect_time > level.time)
			take = 0;
		if (dflags & 0x40)
			take = 0;
	}

	// player armour through the player module (SPEC-720 13)
	if (client && take > 0 && client->inv && !(dflags & DAMAGE_NO_ARMOR)) {
		int before = client->inv->Armor();
		int left = client->inv->AbsorbArmor(take, 0, 0);
		if (left < 0)
			left = 0;
		if (left != take) {
			if (left == 0)
				gi.sound(targ, CHAN_BODY, gi.soundindex("impact/player/armor.wav"), 1, ATTN_NORM, 0);
			take = left;
		}
		(void)before;
	}

	if (client) {
		if (take > 0) {
			if (mod == MOD_FIRE)
				gi.sound(targ, CHAN_BODY, gi.soundindex("impact/gore/sizzle.wav"), 1, ATTN_NORM, 0);
			else {
				gi.sound(targ, CHAN_BODY, gi.soundindex(va("impact/gore/impact%d.wav", 1 + rand() % 3)), 1,
				         ATTN_NORM, 0);
				gi.sound(targ, CHAN_VOICE, gi.soundindex(va("impact/player/hit%d.wav", 1 + rand() % 4)), 1,
				         ATTN_NORM, 0);
			}
			client->damage_alpha += take * 0.02f;
			if (client->damage_alpha > 0.6f)
				client->damage_alpha = 0.6f;
			VectorSet(client->damage_blend, 1, 0, 0);
		}
		if (targ->health - take > 0 || take == 0)
			client->damage_dir = DamageDirection(targ, attacker, dflags);
		// view kick from damage
		float kick = (float)take * 0.5f;
		if (kick > 50)
			kick = 50;
		client->v_dmg_pitch = -kick;
		client->v_dmg_roll = 0;
		client->v_dmg_time = level.time + DAMAGE_TIME;
	}

	if (take) {
		targ->health -= take;
		if (targ->health <= 0) {
			if (client)
				targ->flags |= FL_NO_KNOCKBACK;
			Killed(targ, inflictor, attacker, take, (float *)point);
			return;
		}
	}

	if (!client && targ->pain)
		targ->pain(targ, attacker, (float)knockback, take, (float *)point);
}

/*
============
T_RadiusDamage (SPEC-720 14.1 linear)
============
*/
void T_RadiusDamage(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, int mod,
                    int dflags)
{
	Dmg_RadiusLinear(inflictor, attacker, damage, ignore, radius, mod, dflags); /* G3: SPEC-720 14.1 */
	return;
	edict_t *list[1024];
	vec3_t center;
	VectorCopy(inflictor->s.origin, center);
	int n = G_RadiusList(center, radius, list, 3);
	for (int i = 0; i < n; i++) {
		edict_t *ent = list[i];
		if (ent == ignore || ent == world || !ent->inuse)
			continue;
		if (!ent->takedamage)
			continue;
		if (mod == MOD_FIRE && ent->burn_time > level.time)
			continue;
		vec3_t c, v;
		for (int j = 0; j < 3; j++)
			c[j] = ent->s.origin[j] + (ent->mins[j] + ent->maxs[j]) * 0.5f;
		if (ent->solid == SOLID_BSP && VectorCompare(ent->s.origin, vec3_origin))
			for (int j = 0; j < 3; j++)
				c[j] = (ent->absmin[j] + ent->absmax[j]) * 0.5f;
		VectorSubtract(c, center, v);
		float d = VectorLength(v);
		float p = damage * (1.0f - d / radius);
		if (p <= 0)
			continue;
		if (!CanDamage(ent, inflictor))
			continue;
		trace_t tr = gi.trace(center, NULL, NULL, c, inflictor, 0x6000001);
		vec3_t dir;
		VectorSubtract(tr.endpos, center, dir);
		int dmg = (int)p;
		if (dflags & DAMAGE_EXPLOSIVE_HALF)
			dmg = (int)(dmg * 0.05f);
		T_Damage(ent, inflictor, attacker, dir, tr.endpos, center, dmg, (int)p, dflags | DAMAGE_RADIUS, mod);
	}
}

// SPEC-720 14.2 min/max
void T_RadiusDamageMinMax(edict_t *inflictor, edict_t *attacker, const vec3_t center, float maxdmg, float mindmg,
                          float radius, edict_t *ignore, int mod, int dflags)
{
	Dmg_RadiusMinMax(inflictor, attacker, center, maxdmg, mindmg, radius, ignore, mod, dflags); /* G3: 14.2 */
	return;
	edict_t *list[1024];
	int n = G_RadiusList(center, radius, list, 3);
	for (int i = 0; i < n; i++) {
		edict_t *ent = list[i];
		if (ent == ignore || ent == world || !ent->inuse || !ent->takedamage)
			continue;
		vec3_t c, v;
		for (int j = 0; j < 3; j++)
			c[j] = ent->s.origin[j] + (ent->mins[j] + ent->maxs[j]) * 0.5f;
		if (ent->solid == SOLID_BSP && VectorCompare(ent->s.origin, vec3_origin))
			for (int j = 0; j < 3; j++)
				c[j] = (ent->absmin[j] + ent->absmax[j]) * 0.5f;
		VectorSubtract(c, center, v);
		float d = VectorLength(v);
		if (d > radius)
			continue;
		if (!CanDamage(ent, inflictor))
			continue;
		float p = mindmg + (maxdmg - mindmg) * (1.0f - d / radius);
		trace_t tr = gi.trace(center, NULL, NULL, c, inflictor, 0x6000001);
		vec3_t dir;
		VectorSubtract(tr.endpos, center, dir);
		T_Damage(ent, inflictor, attacker, dir, tr.endpos, center, (int)p, (int)p, dflags | DAMAGE_RADIUS, mod);
	}
}

//======================================================================
// G3 hooks owned by G18 (damage_api.h G3Hooks): ignition, weapon/armour pickups thrown off a body, camera shake
// near an explosion, the player `weaponlose` command. Installed at game init.

static void G3Hook_SetOnFire(edict_t *targ, edict_t *attacker, float seconds)
{
	(void)seconds; // SPEC-782 ignition keeps its own burn timers (SetOnFire)
	SetOnFire(targ, attacker);
}

static edict_t *G3Hook_DropWeapon(edict_t *from, int weapon, const vec3_t org, const vec3_t vel)
{
	edict_t *e = DropWeaponItem(from, weapon, 0);
	if (e) {
		VectorCopy(org, e->s.origin);
		VectorCopy(vel, e->velocity);
		gi.linkentity(e);
	}
	return e;
}

static edict_t *G3Hook_DropArmour(edict_t *corpse, int count)
{
	gitem_t *it = FindItemByClassname("item_equip_armor");
	if (!it || !corpse)
		return NULL;
	edict_t *e = G_Spawn();
	e->classname = (char *)it->classname;
	VectorCopy(corpse->s.origin, e->s.origin);
	e->spawnflags = DROPPED_ITEM;
	SpawnItem(e, it);
	e->itemcount = count;
	return e;
}

// func_camerashake parameters at a point: intensity a within radius b (c: duration, one shake event per call)
static void G3Hook_CameraShake(const vec3_t at, float a, float b, float c)
{
	(void)c;
	if (b <= 0)
		return;
	for (int i = 1; i <= game.maxclients; i++) {
		edict_t *p = &g_edicts[i];
		if (!p->inuse || !p->client)
			continue;
		vec3_t d;
		VectorSubtract(p->s.origin, at, d);
		float dist = VectorLength(d);
		if (dist <= b)
			G_CameraShake(p, (1.0f - dist / b) * a);
	}
}

static void G3Hook_WeaponLose(edict_t *player)
{
	if (player && player->client && player->client->inv)
		player->client->inv->HandleCommand("weaponlose", 1);
}

void G_InstallG3Hooks(void)
{
	g3hooks.setOnFire = G3Hook_SetOnFire;
	g3hooks.dropWeapon = G3Hook_DropWeapon;
	g3hooks.dropArmour = G3Hook_DropArmour;
	g3hooks.cameraShake = G3Hook_CameraShake;
	{ extern void G_InstallAttackHooks(void); G_InstallAttackHooks(); } // character attacks (SPEC-782)
	g3hooks.playerWeaponLose = G3Hook_WeaponLose;
	g3hooks.statistic = Stats_DamageHook; // mission statistics (SPEC-AMEND-007 G18.41)
}
