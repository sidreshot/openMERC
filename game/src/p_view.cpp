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
 * OpenMerc: rewritten for the SoF 1.07f game module (G18) on the structure of Quake II's p_view.c:
 * SoF bob, roll, view offset clamp, lean, kick handling, falling damage, world effects, screen blends,
 * footstep events and the end-of-frame player-state fields.
 * Implements: SPEC-780 11-14
 */

#include "g_local.h"
#include "damage/damage_api.h" /* G3: player bodies */
#include "modes/g_modes.h" /* G9 */
#include "ghoulglue/gg_ghoul.h" /* G9 */
#include "bots/bot_api.h" /* G5 */
#include "scriptglue/g_scriptglue.h" /* G6b: remote-camera end-of-frame rule */

void G_AddEvent(edict_t *ent, int ev, int parm);

static edict_t *current_player;
static gclient_t *current_client;
static vec3_t forward, right, up;

// SPEC-780 11.4
static float SV_CalcRoll(vec3_t angles, vec3_t velocity)
{
	float side = DotProduct(velocity, right);
	float sign = side < 0 ? -1.0f : 1.0f;
	side = fabs(side);
	float value = sv_rollangle->value;
	if (side < sv_rollspeed->value)
		side = side * value / sv_rollspeed->value;
	else
		side = value;
	return side * sign;
}

// SPEC-780 12
static void SV_CalcViewOffset(edict_t *ent)
{
	gclient_t *c = ent->client;
	float *angles = c->ps.kick_angles;
	VectorClear(angles);

	if (ent->deadflag) {
		VectorClear(c->ps.weaponkick);
		c->ps.viewangles[ROLL] = 40;
		c->ps.viewangles[PITCH] = -15;
		c->ps.viewangles[YAW] = c->killer_yaw;
	} else {
		VectorCopy(c->kick_angles, c->ps.weaponkick);

		float ratio = (c->kick_endtime - level.time) * 2;
		if (ratio < 0) {
			ratio = 0;
			c->kick_pitch = c->kick_roll = 0;
		}
		angles[PITCH] += ratio * c->kick_pitch;
		angles[ROLL] += ratio * c->kick_roll;

		ratio = (c->v_dmg_time - level.time) / DAMAGE_TIME;
		if (ratio < 0) {
			ratio = 0;
			c->v_dmg_pitch = c->v_dmg_roll = 0;
		}
		angles[PITCH] += ratio * c->v_dmg_pitch;
		angles[ROLL] += ratio * c->v_dmg_roll;

		ratio = (c->fall_time - level.time) / FALL_TIME;
		if (ratio < 0)
			ratio = 0;
		angles[PITCH] += ratio * c->fall_value;

		float delta = DotProduct(ent->velocity, forward);
		angles[PITCH] += delta * run_pitch->value;
		delta = DotProduct(ent->velocity, right);
		angles[ROLL] += delta * run_roll->value;

		float bp = c->xyspeed * bob_pitch->value * c->bobfracsin;
		float br = c->xyspeed * bob_roll->value * c->bobfracsin;
		if ((c->ps.pmove.pm_flags & PMF_DUCKED) && (c->ps.pmove.pm_flags & PMF_ON_GROUND)) {
			bp *= 6;
			br *= 6;
		}
		angles[PITCH] += bp;
		if (c->bobcycle & 1)
			br = -br;
		angles[ROLL] += br;
	}

	vec3_t v = {0, 0, 0};
	float ratio = (c->fall_time - level.time) / FALL_TIME;
	if (ratio < 0)
		ratio = 0;
	v[2] += ent->viewheight - ratio * c->fall_value * 0.4f;
	float bob = c->bobfracsin * c->xyspeed * bob_up->value;
	if (bob > 6)
		bob = 6;
	v[2] += bob;
	VectorAdd(v, c->kick_origin, v);

	// lean (SPEC-780 12)
	c->lean_on = false;
	if (c->lean && ent->groundentity && DotProduct(ent->velocity, ent->velocity) < 30 && ent->health > 0) {
		vec3_t eye, end, bmins = {-8, -8, -8}, bmaxs = {8, 8, 8};
		VectorCopy(ent->s.origin, eye);
		eye[2] += ent->viewheight;
		VectorMA(eye, -30.0f * c->lean, right, end);
		VectorMA(end, -5, up, end);
		trace_t tr = gi.trace(eye, bmins, bmaxs, end, ent, MASK_PLAYERSOLID);
		VectorSubtract(tr.endpos, ent->s.origin, v);
		VectorCopy(tr.endpos, c->lean_end);
		c->lean_on = true;
		angles[ROLL] -= c->lean * tr.fraction * 8;
	}

	if (v[0] < -14) v[0] = -14;
	else if (v[0] > 14) v[0] = 14;
	if (v[1] < -14) v[1] = -14;
	else if (v[1] > 14) v[1] = 14;
	if (v[2] < -11) v[2] = -11;
	else if (v[2] > 40) v[2] = 40;
	VectorCopy(v, c->ps.viewoffset);
}

static void SV_AddBlend(float r, float g, float b, float a, float *v_blend)
{
	if (a <= 0)
		return;
	float a2 = v_blend[3] + (1 - v_blend[3]) * a;
	float a3 = v_blend[3] / a2;
	v_blend[0] = v_blend[0] * a3 + r * (1 - a3);
	v_blend[1] = v_blend[1] * a3 + g * (1 - a3);
	v_blend[2] = v_blend[2] * a3 + b * (1 - a3);
	v_blend[3] = a2;
}

// SPEC-780 14
static void SV_CalcBlend(edict_t *ent)
{
	gclient_t *c = ent->client;
	float *bl = c->ps.blend;
	bl[0] = bl[1] = bl[2] = bl[3] = 0;

	vec3_t vieworg;
	VectorAdd(ent->s.origin, c->ps.viewoffset, vieworg);
	int contents = c->ps.remote_type == 1 ? 0 : gi.pointcontents(c->ps.remote_type ? ent->s.origin : vieworg);
	if (contents & (CONTENTS_LAVA | (c->resp.spectator ? 0 : CONTENTS_SOLID)))
		SV_AddBlend(1.0f, 0.3f, 0.0f, 0.6f, bl);
	else if (contents & CONTENTS_SLIME)
		SV_AddBlend(0.0f, 0.1f, 0.05f, 0.6f, bl);
	else if (contents & CONTENTS_WATER)
		SV_AddBlend(0.03f, 0.08f, 0.03f, 0.2f, bl);
	if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER))
		c->ps.rdflags |= RDF_UNDERWATER;
	else
		c->ps.rdflags &= ~RDF_UNDERWATER;
	if (c->goggles)
		c->ps.rdflags |= RDF_GOGGLES;
	else
		c->ps.rdflags &= ~RDF_GOGGLES;

	SV_AddBlend(c->damage_blend[0], c->damage_blend[1], c->damage_blend[2], c->damage_alpha, bl);
	SV_AddBlend(0.85f, 0.7f, 0.3f, c->bonus_alpha, bl);
	if (c->goggles)
		SV_AddBlend(0.5f, 1, 0.5f, c->flash_alpha, bl);
	else
		SV_AddBlend(1, 1, 1, c->flash_alpha, bl);
	SV_AddBlend(c->script_blend[0], c->script_blend[1], c->script_blend[2], c->script_alpha, bl);
	SV_AddBlend(198 / 255.0f, 171 / 255.0f, 20 / 255.0f, c->gold_alpha, bl);
	// func_fade and the single-player death fade use the same blend
	float fade = c->fade_alpha;
	if (!deathmatch->value && level.fadealpha > fade)
		fade = level.fadealpha;
	if (fade > 0)
		SV_AddBlend(c->fade_color[0], c->fade_color[1], c->fade_color[2], fade > 1 ? 1 : fade, bl);

	c->damage_alpha -= 0.06f;
	if (c->damage_alpha < 0)
		c->damage_alpha = 0;
	c->bonus_alpha -= 0.1f;
	if (c->bonus_alpha < 0)
		c->bonus_alpha = 0;
	if (c->flash_alpha > 0.995f)
		c->flash_alpha = 0.995f;
	c->flash_alpha -= (1 - c->flash_alpha) * c->flash_fade;
	if (c->flash_alpha < 0)
		c->flash_alpha = 0;
	c->gold_alpha *= 0.88f;
	if (c->gold_alpha < 0.3f)
		c->gold_alpha -= 0.06f;
	if (c->gold_alpha < 0)
		c->gold_alpha = 0;
}

// SPEC-780 13
static void P_FallingDamage(edict_t *ent)
{
	gclient_t *c = ent->client;
	if (ent->s.modelindex != 255)
		return;
	if (ent->movetype == MOVETYPE_NOCLIP)
		return;

	float delta;
	if ((c->oldvelocity[2] < 0) && (ent->velocity[2] > c->oldvelocity[2]) && (!ent->groundentity))
		delta = c->oldvelocity[2];
	else {
		if (!ent->groundentity)
			return;
		delta = ent->velocity[2] - c->oldvelocity[2];
	}
	delta = delta * delta * 0.0001f;

	if (ent->waterlevel == 3)
		return;
	if (ent->waterlevel == 2)
		delta *= 0.25f;
	if (ent->waterlevel == 1)
		delta *= 0.5f;
	if (delta < 1)
		return;

	FX_EffectEnt(gi.effectindex("land"), ent, MULTICAST_PVS);
	if (delta < 15) {
		G_AddEvent(ent, 7, 0);
		return;
	}
	c->fall_value = delta * 0.5f;
	if (c->fall_value > 40)
		c->fall_value = 40;
	c->fall_time = level.time + FALL_TIME;
	if (ent->health > 0)
		G_AddEvent(ent, delta < 55 ? 8 : 9, 0);
	ent->pain_debounce_time = level.time;
	int damage = (int)((delta - 24) * 2);
	if (damage < 1)
		return;
	int df = (int)dmflags->value;
	if (deathmatch->value && (df & 0x8) && !(df & 0x10))
		return;
	vec3_t dir = {0, 0, 1};
	T_Damage(ent, world, world, dir, ent->s.origin, vec3_origin, damage, 0, 0x2002, MOD_FALLING);
}

static void P_WorldEffects(void)
{
	edict_t *ent = current_player;
	gclient_t *c = current_client;
	if (ent->movetype == MOVETYPE_NOCLIP) {
		ent->air_finished = level.time + 12;
		return;
	}
	int waterlevel = ent->waterlevel;
	int old_waterlevel = c->old_waterlevel;
	c->old_waterlevel = waterlevel;

	if (!old_waterlevel && waterlevel) {
		gi.sound(ent, CHAN_BODY, gi.soundindex("player/land/water.wav"), 0.6f, ATTN_NORM, 0);
		ent->flags |= FL_INWATER;
		ent->damage_debounce_time = level.time - 1;
	}
	if (old_waterlevel && !waterlevel)
		ent->flags &= ~FL_INWATER;

	if (waterlevel == 3) {
		if (!ent->s.sound) {
			ent->s.sound = gi.soundindex("player/underwtr/underwtr.wav");
			ent->s.sounddata = 248;
			c->breather_sound = ent->s.sound;
		}
	} else if (c->breather_sound && ent->s.sound == c->breather_sound) {
		ent->s.sound = 0;
		ent->s.sounddata = 0;
		c->breather_sound = 0;
	}

	if (waterlevel != 3) {
		ent->air_finished = level.time + 12;
		ent->dmg = 2;
	} else if (ent->air_finished < level.time && c->next_drown_time < level.time && ent->health > 0) {
		c->next_drown_time = level.time + 1;
		ent->dmg += 2;
		if (ent->dmg > 15)
			ent->dmg = 15;
		if (ent->health <= ent->dmg)
			gi.sound(ent, CHAN_VOICE, gi.soundindex("player/drown1.wav"), 0.6f, ATTN_NORM, 0);
		T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, ent->dmg, 0, 0x4002, MOD_WATER);
	}

	if (waterlevel && (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))) {
		if (ent->watertype & CONTENTS_LAVA) {
			if (ent->health > 0 && ent->pain_debounce_time <= level.time) {
				gi.sound(ent, CHAN_VOICE, gi.soundindex(va("impact/player/hit%d.wav", 1 + rand() % 4)), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, 3 * waterlevel, 0, 0x1000, MOD_LAVA);
		}
		if (ent->watertype & CONTENTS_SLIME)
			T_Damage(ent, world, world, vec3_origin, ent->s.origin, vec3_origin, waterlevel, 0, 0x4000, MOD_SLIME);
	}
}

// SPEC-780 11
// SPEC-780 §15 auxiliary hit volumes: up to three helper entities per player, in the owner's helper list
// (SPEC-AMEND-005 G2.8: "the same helper kind" as a character's `Nug Buddy` — classname, server flags 0x82, not
// solid, clip mask 0x02030003). Kept while their condition holds and moved every frame. Numbering evidence: the
// original's first rocket after a DM respawn has one number more than OpenMerc's had without the gun volume
// (inhib_orig 98 / inhib_om 97). Where in the frame the original creates them is not known (here: after the view
// offset, which computes the lean point).
static edict_t *PlayerHelper(edict_t *ent, edict_t **slot)
{
	if (*slot && (*slot)->inuse && (*slot)->owner == ent)
		return *slot;
	edict_t *h = G_Spawn();
	h->classname = (char *)"Nug Buddy";
	h->owner = ent;
	h->svflags = SVF_PLAYERPROXY | SVF_DEADMONSTER; // 0x82
	h->solid = SOLID_NOT;
	h->clipmask = 0x02030003;
	G_HelperAppend(ent, h);
	*slot = h;
	return h;
}

static void PlayerHelperFree(edict_t **slot)
{
	if (*slot) {
		if ((*slot)->inuse)
			G_FreeEdict(*slot);
		*slot = NULL;
	}
}

static void PlayerHelperPlace(edict_t *h, const vec3_t org, const vec3_t mins, const vec3_t maxs)
{
	VectorCopy(org, h->s.origin);
	VectorCopy(mins, h->mins);
	VectorCopy(maxs, h->maxs);
	gi.linkentity(h);
}

static void PlayerHelpers(edict_t *ent)
{
	gclient_t *c = ent->client;
	bool alive = ent->health > 0 && !ent->deadflag;
	bool dm = deathmatch->value != 0;
	bool crouched = (c->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	// lean head: while leaning, at the leaned eye point
	if (alive && c->lean_on) {
		static const vec3_t mn = {-12, -12, -24}, mx = {12, 12, 26};
		PlayerHelperPlace(PlayerHelper(ent, &c->helper_lean), c->lean_end, mn, mx);
	} else
		PlayerHelperFree(&c->helper_lean);
	// crouch: deathmatch, alive, while crouched; origin + forward × 8, z + maxs z
	if (dm && alive && crouched && !c->resp.spectator) {
		static const vec3_t mn = {-12, -12, 0}, mx = {12, 12, 8};
		vec3_t f, p;
		AngleVectors(c->v_angle, f, NULL, NULL);
		VectorMA(ent->s.origin, 8, f, p);
		p[2] = ent->s.origin[2] + ent->maxs[2];
		PlayerHelperPlace(PlayerHelper(ent, &c->helper_crouch), p, mn, mx);
	} else
		PlayerHelperFree(&c->helper_crouch);
	// gun: deathmatch, alive, not spectating; the span from the origin to origin + forward × 48 with the pitch
	// clamped (UNVERIFIED reading of §15)
	if (dm && alive && !c->resp.spectator) {
		vec3_t ang, f, p, lo, hi;
		VectorCopy(c->v_angle, ang);
		float pitch = ang[PITCH] > 180 ? ang[PITCH] - 360 : ang[PITCH];
		float pmin = crouched ? -5.0f : -50.0f;
		pitch = pitch < pmin ? pmin : pitch > 28 ? 28 : pitch;
		ang[PITCH] = pitch;
		AngleVectors(ang, f, NULL, NULL);
		VectorMA(ent->s.origin, 48, f, p);
		for (int i = 0; i < 2; i++) {
			lo[i] = p[i] < ent->s.origin[i] ? p[i] : ent->s.origin[i];
			hi[i] = p[i] < ent->s.origin[i] ? ent->s.origin[i] : p[i];
		}
		if (pitch < 0) { // looking up
			lo[2] = ent->s.origin[2] + (crouched ? -1 : 15);
			hi[2] = p[2] + 32;
		} else {
			lo[2] = p[2];
			hi[2] = ent->s.origin[2] + (crouched ? 26 : 42);
		}
		if (lo[2] > hi[2]) {
			float t = lo[2];
			lo[2] = hi[2];
			hi[2] = t;
		}
		vec3_t mn, mx;
		VectorSubtract(lo, ent->s.origin, mn);
		VectorSubtract(hi, ent->s.origin, mx);
		PlayerHelperPlace(PlayerHelper(ent, &c->helper_gun), ent->s.origin, mn, mx);
	} else
		PlayerHelperFree(&c->helper_gun);
}

void ClientEndServerFrame(edict_t *ent)
{
	current_player = ent;
	current_client = ent->client;
	gclient_t *c = ent->client;

	for (int i = 0; i < 3; i++) {
		c->ps.pmove.origin[i] = (short)(ent->s.origin[i] * 8.0f);
		c->ps.pmove.velocity[i] = (short)(ent->velocity[i] * 8.0f);
	}
	float scale = GM_MoveScale(ent); /* G9: SPEC-790 B3/B5/B11 */
	c->ps.pmove.movescale = (uint8_t)(int)(scale * 255);

	if (level.intermissiontime) {
		c->ps.blend[3] = 0;
		c->ps.fov = 95;
		G_SetStats(ent);
		return;
	}

	AngleVectors(c->v_angle, forward, right, up);
	P_WorldEffects();
	// SPEC-780 7: while dead, health decreases by 2 per frame down to -100 (oracle: -3, -5, -7 ... from the death
	// frame on; capture inhib_orig)
	if (ent->deadflag && ent->health > -100)
		ent->health = ent->health - 2 < -100 ? -100 : ent->health - 2;

	if (c->v_angle[PITCH] > 180)
		ent->s.angles[PITCH] = (-360 + c->v_angle[PITCH]) / 3;
	else
		ent->s.angles[PITCH] = c->v_angle[PITCH] / 3;
	ent->s.angles[YAW] = c->v_angle[YAW];
	ent->s.angles[ROLL] = 0;
	ent->s.angles[ROLL] = SV_CalcRoll(ent->s.angles, ent->velocity) * 4;

	c->xyspeed = sqrt(ent->velocity[0] * ent->velocity[0] + ent->velocity[1] * ent->velocity[1]);
	if (c->xyspeed < 5) {
		c->bobmove = 0;
		c->bobtime = 0;
	} else if (ent->groundentity) {
		if (c->xyspeed > 210)
			c->bobmove = 0.3f;
		else if (c->xyspeed > 10)
			c->bobmove = 0.2f;
		else
			c->bobmove = 0.1f;
	}
	float bobtime = (c->bobtime += c->bobmove);
	int bobcycle = (int)bobtime;
	c->bobfracsin = fabs(sin(bobtime * M_PI));
	int oldcycle = c->bobcycle;
	c->bobcycle = bobcycle;

	P_FallingDamage(ent);
	SV_CalcViewOffset(ent);
	PlayerHelpers(ent);
	SV_CalcBlend(ent);

	// SPEC-AMEND-005 G6b.2: the stats step clears effects bit 0x4000 whenever the camera counter is < 1 and the level
	// time is at least the respawn time + 0.5 s — the respawn copy 0.5 s after a spawn, and after a mid-level camera
	// in the first frame after the counter reached 0 (F5's report: a one-shot timer kept the bit set)
	if (c->camera_nest < 1 && level.time >= c->respawn_time + 0.5f - 0.001f)
		ent->s.effects &= ~EF_RESPAWNED;

	if (c->resp.spectator)
		G_SetSpectatorStats(ent);
	else
		G_SetStats(ent);
	G_CheckChaseStats(ent);

	// footsteps
	if (ent->groundentity && bobcycle != oldcycle && ent->health > 0 && !c->resp.spectator) {
		G_AddEvent(ent, (c->xyspeed > 225 ? 5 : 3) + (bobcycle & 1), 0);
		Bot_FootstepNoise(ent); /* G5: bots hear footsteps, radius 250 (SPEC-780 11.7) */
	}

	// objectives-updated jingle
	if (level.objectives_changed != c->mission_msg) {
		c->mission_msg = level.objectives_changed;
		level.objectives_jingle = 3;
	}
	if (level.objectives_jingle > 0 && (level.framenum & 63) == 0) {
		gi.sound(ent, CHAN_VOICE, gi.soundindex("misc/pc_up.wav"), 0.6f, ATTN_STATIC, 0);
		level.objectives_jingle--;
	}

	VectorCopy(ent->velocity, c->oldvelocity);
	VectorCopy(ent->client->ps.viewangles, c->oldviewangles);
	VectorClear(c->kick_origin);
	for (int i = 0; i < 3; i++) {
		if (fabs(c->kick_angles[i]) >= 0.8f)
			c->kick_angles[i] *= 0.7f;
		else
			c->kick_angles[i] = 0;
	}

	if (c->showscores && !(level.framenum & 31))
		DeathmatchScoreboardMessage(ent, ent->enemy);

	// step 12: remote view; unless third-person is on, SPEC-751 2.6: camera counter < 1 → no remote view, else
	// effects bit 0x4000 and the active camera's entity/type (G6b's ScriptGlue_ClientEndFrame)
	if (!c->thirdperson)
		ScriptGlue_ClientEndFrame(ent);
	else
		c->ps.remote_id = 0;

	if (deathmatch->value) {
		PModel_Animate(ent); // third-person animation and hand weapon (SPEC-780 16)
		if (ent->g3body)
			ent->g3body->Think(); // body events, faces (SPEC-722)
	}
	GM_ClientEndFrame(ent); /* G9: SPEC-780 11 step 13 game-mode end-frame hook */
	if (c->inv && !c->pers.isbot) {
		c->inv->ServerSendEvents();
		c->inv->ServerClearEvents();
	}
	c->ps.resync = (uint8_t)c->resync;
	// bit 0x01 while the attack is not armed (only between a human's deathmatch respawn and his next fresh attack
	// press); the other bits are left alone (AMEND-005 G18.24)
	c->ps.inhibit = (uint8_t)((c->ps.inhibit & ~1) | (c->attack_rearmed ? 0 : 1));
	if (c->ps.pmove.pm_type != PM_INTERMISSION_SPEC)
		c->ps.spectated = 0;
	c->ps.cinematicfreeze = (uint8_t)game.cinematic;
	c->damage_dir = 0;
	// the frame's damage counters written by the damage code (G3, SPEC-720 4) start again every frame
	c->g3_blood = c->g3_armorsaved = c->g3_knockback = 0;
	VectorClear(c->g3_damagefrom);
	ent->svflags &= ~SVF_SAFEZONE;
}
