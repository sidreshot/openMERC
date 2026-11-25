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
 * OpenMerc: changed for the SoF 1.07f game module (G18): vector gravity, SoF think/impact/clip rules,
 * pusher without 1/8 rounding, toss landing as STEP, SoF step friction, the new movetypes 10/11 with
 * elastic reflection and linear/rotational friction, player-proxy deflection.
 * Implements: SPEC-760 B4
 */
// g_phys.c

#include "g_local.h"
#include "../../engine/src/common/header/om_corpse.h"

void G_AddEvent(edict_t *ent, int ev, int parm);

edict_t *SV_TestEntityPosition(edict_t *ent)
{
	int mask = ent->clipmask ? ent->clipmask : MASK_SOLID;
	trace_t trace = gi.trace(ent->s.origin, ent->mins, ent->maxs, ent->s.origin, ent, mask);
	if (trace.startsolid)
		return g_edicts;
	return NULL;
}

void SV_CheckVelocity(edict_t *ent)
{
	for (int i = 0; i < 3; i++) {
		if (ent->velocity[i] > sv_maxvelocity->value)
			ent->velocity[i] = sv_maxvelocity->value;
		else if (ent->velocity[i] < -sv_maxvelocity->value)
			ent->velocity[i] = -sv_maxvelocity->value;
	}
}

// SPEC-760 B4.2
qboolean SV_RunThink(edict_t *ent)
{
	float thinktime = ent->nextthink;
	if (thinktime <= 0 || thinktime > level.time + 0.001) {
		if ((ent->flags & FL_LEANPROXY) && ent->owner && ent->owner->client) {
			edict_t *o = ent->owner;
			vec3_t right;
			AngleVectors(o->client->v_angle, NULL, right, NULL);
			VectorMA(o->s.origin, -30.0f * o->client->lean, right, ent->s.origin);
			gi.linkentity(ent);
			G_TouchTriggers(ent);
		}
		return true;
	}
	ent->nextthink = 0;
	if (ent->think)
		ent->think(ent);
	return false;
}

// SPEC-760 B4.8: returns whether movement continues
qboolean SV_Impact(edict_t *e1, trace_t *trace)
{
	edict_t *e2 = trace->ent;
	if (!e2)
		return true;

	if (e2->svflags & SVF_PLAYERPROXY) {
		if (e2->owner == e1->owner && e1->owner)
			return false;
		if ((e1->flags & FL_THROWNEQUIP) && e1->health != 31337) {
			if (trace->plane.normal[2] > 0.8f) {
				e1->velocity[2] = -e1->velocity[2];
				VectorScale(e1->velocity, 0.4f, e1->velocity);
			} else
				VectorScale(e1->velocity, -0.2f, e1->velocity);
			if (e1->touch && (e1->solid != SOLID_NOT || e1->movetype == MOVETYPE_DANTOUCH))
				e1->touch(e1, e2->owner ? e2->owner : e2, &trace->plane, trace->surface);
			return false;
		}
	}

	if (e1->touch && (e1->solid != SOLID_NOT || e1->movetype == MOVETYPE_DANTOUCH))
		e1->touch(e1, e2, &trace->plane, trace->surface);
	if (!e1->inuse)
		return false;
	if (e2->inuse && e2->touch && e2->solid != SOLID_NOT)
		e2->touch(e2, e1, NULL, NULL);
	return true;
}

// SPEC-760 B4.3
#define STOP_EPSILON 1.0f

int ClipVelocity(vec3_t in, vec3_t normal, vec3_t out, float overbounce)
{
	int blocked = 0;
	if (normal[2] > 0)
		blocked |= 1;
	if (!normal[2])
		blocked |= 2;

	float d = DotProduct(in, normal);
	float backoff;
	if (d == 0) {
		VectorScale(in, 0.5f, in);
		backoff = VectorLength(in) * overbounce;
	} else
		backoff = d * overbounce;

	for (int i = 0; i < 3; i++) {
		out[i] = in[i] - normal[i] * backoff;
		if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
			out[i] = 0;
	}
	return blocked;
}

static void CorpseBox(edict_t *ent, vec3_t mins, vec3_t maxs)
{
	if (ent->solid == SOLID_CORPSE) {
		/* The shot hull grows to the posed limbs. Movement stays a narrow column.
		   The bottom is the posture feet, clamped so a low head bolt cannot sink the body. */
		float z0 = ent->mins[2];
		if (z0 < -40.0f)
			z0 = -40.0f;
		if (z0 > 0.0f)
			z0 = 0.0f;
		VectorSet(mins, -8, -8, z0);
		VectorSet(maxs, 8, 8, 12);
	} else {
		VectorCopy(ent->mins, mins);
		VectorCopy(ent->maxs, maxs);
	}
}

/* 0 world, 1 BSP, 2 ground, 3 a prop box, 4 a character, 5 anything else. */
static int OM_SolidKind(edict_t *hit)
{
	if (!hit || hit == world)
		return 0;
	if (hit->client || hit->ai || hit->solid == SOLID_CORPSE)
		return 4;
	if (hit->solid == SOLID_BSP)
		return 1;
	if (hit->solid == SOLID_GROUND)
		return 2;
	if (hit->solid == SOLID_BBOX)
		return 3;
	return 5;
}

static int OM_SupportBrush(edict_t *hit, float normal_z)
{
	return OM_CorpseSupport(OM_SolidKind(hit), normal_z);
}

/* Lay an NPC on a face that can hold it. The stored pitch and roll are what
   G3_EntityAxes negates, so the mesh up matches the plane. The player camera
   has no ai and stays level. A steeper face leaves the angles alone. */
static void OM_LayOnFace(edict_t *ent, const float normal[3])
{
	float pitch, roll;

	if (!ent || !ent->ai || !normal)
		return;
	if (!OM_CorpseAlign(ent->s.angles[YAW], normal, &pitch, &roll))
		return;
	ent->s.angles[PITCH] = pitch;
	ent->s.angles[ROLL] = roll;
	VectorClear(ent->avelocity);
}

/* A steep face with room downhill keeps a body moving. A corner that is
   already blocked stays put, so the nudge does not buzz in a crack. */
static void OM_SlideIfOpen(edict_t *ent, const float normal[3])
{
	float dir[3], speed;
	vec3_t mins, maxs, dest;
	trace_t tr;

	if (!ent || !OM_CorpseDownhill(normal, dir))
		return;
	speed = VectorLength(ent->velocity);
	if (speed >= 40.0f)
		return;
	CorpseBox(ent, mins, maxs);
	VectorMA(ent->s.origin, 48.0f, dir, dest);
	tr = gi.trace(ent->s.origin, mins, maxs, dest, ent, MASK_SOLID);
	if (tr.startsolid || tr.allsolid || tr.fraction < 0.5f)
		return;
	VectorScale(dir, 100.0f, ent->velocity);
}

/* One unit out of a steep face, along its normal, so the box can slide.
   A holding face and a free box stay put. An origin buried in the snow
   (a body wedged in a corner) comes out to the face in one step. A slide
   that is only clipping the box stays at one unit, so it does not hop. */
static void OM_StepOffSteep(edict_t *ent, const float normal[3])
{
	float step;
	vec3_t n;

	if (!ent || !normal)
		return;
	step = OM_CorpseUnstick(normal[2], 1);
	if (step <= 0.0f)
		return;
	VectorCopy(normal, n);
	VectorNormalize(n);
	if (VectorLength(ent->velocity) < 20.0f) {
		vec3_t a, b, hn;
		trace_t tr;
		float dist, along;

		VectorMA(ent->s.origin, 96.0f, n, a);
		VectorMA(ent->s.origin, -24.0f, n, b);
		tr = gi.trace(a, vec3_origin, vec3_origin, b, ent, MASK_SOLID);
		if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f) {
			VectorCopy(tr.plane.normal, hn);
			along = VectorNormalize(hn);
			if (along > 0.0f && hn[2] > 0.0f && hn[2] < OM_CORPSE_SLOPE_MIN && DotProduct(hn, n) > 0.5f) {
				dist = DotProduct(ent->s.origin, hn) - DotProduct(tr.endpos, hn);
				if (dist < -8.0f) {
					float push = -dist + 8.0f;
					cvar_t *dbg;
					if (push > 48.0f)
						push = 48.0f;
					VectorMA(ent->s.origin, push, hn, ent->s.origin);
					gi.linkentity(ent);
					dbg = gi.cvar("om_corpsedebug", "0", 0);
					if (dbg->value) {
						static int shown;
						if (shown < 12) {
							shown++;
							gi.dprintf("OMEJECT at %.0f %.0f %.0f push %.0f\n", ent->s.origin[0],
							           ent->s.origin[1], ent->s.origin[2], push);
						}
					}
					return;
				}
			}
		}
	}
	VectorMA(ent->s.origin, step, n, ent->s.origin);
	gi.linkentity(ent);
}

static float OM_CorpseFeet(edict_t *ent)
{
	/* The shot hull is deeper than the posed body and is rewritten every
	   settle. The ground bolt is the contact the animation is standing on.
	   Using the hull here yanks a lying body back up to the standing height. */
	float z = OM_CorpseBolt(ent);
	if (z >= -1.0f || z < -80.0f)
		z = ent->mins[2];
	if (z < -40.0f)
		z = -40.0f;
	if (z > 0.0f)
		z = 0.0f;
	return z;
}

/* Drop a corpse that has no floor under its feet onto the first supporting brush.
   A kill in the air (the sabre chase on the next train, and every other map) ends
   on that surface instead of staying at the kill height. */
void OM_SettleDeadBody(edict_t *ent)
{
	float feet, origin[3], vel[3], floor_z, floor_nz;
	int contact, have_floor;
	edict_t *floorent;
	vec3_t p, q, floor_n;

	if (!ent || !ent->inuse || ent->solid != SOLID_CORPSE)
		return;
	feet = OM_CorpseFeet(ent);
	contact = 0;
	have_floor = 0;
	floorent = NULL;
	floor_z = 0;
	floor_nz = 0;
	VectorClear(floor_n);
	VectorCopy(ent->s.origin, p);
	p[2] = ent->s.origin[2] + feet + 2.0f;
	/* A monster volume around the feet makes the monster trace start solid.
	   The long probe then climbs out and plants the body on a ceiling.
	   A world face within a step is the ground the body is already standing on,
	   and it wins before that climb. A train is not in MASK_SOLID, so this
	   probe does not steal the roof from the monster trace below. */
	{
		trace_t tr;
		VectorCopy(p, q);
		q[2] -= 24.0f;
		tr = gi.trace(p, vec3_origin, vec3_origin, q, ent, MASK_SOLID);
		if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f &&
		    OM_SupportBrush(tr.ent, tr.plane.normal[2])) {
			float gap = (ent->s.origin[2] + feet) - tr.endpos[2];
			if (gap <= 12.0f && gap >= -16.0f) {
				contact = 1;
				floorent = tr.ent;
				floor_z = tr.endpos[2];
				VectorCopy(tr.plane.normal, floor_n);
			}
		}
	}
	for (int n = 0; n < 3 && !contact; n++) {
		trace_t tr;
		VectorCopy(p, q);
		q[2] -= 24.0f;
		/* A train is a bounding box, so the trace leaf is CONTENTS_MONSTER.
		   MASK_SOLID walks through the car and the body lands on the track under it. */
		tr = gi.trace(p, vec3_origin, vec3_origin, q, ent, MASK_MONSTERSOLID);
		if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f &&
		    OM_SupportBrush(tr.ent, tr.plane.normal[2])) {
			float gap = (ent->s.origin[2] + feet) - tr.endpos[2];
			if (gap <= 12.0f && gap >= -16.0f) {
				contact = 1;
				floorent = tr.ent;
				floor_z = tr.endpos[2];
				VectorCopy(tr.plane.normal, floor_n);
			}
			break;
		}
		if (!(tr.startsolid || tr.allsolid))
			break;
		p[2] += 16.0f;
	}
	if (!contact) {
		VectorCopy(ent->s.origin, p);
		p[2] += 8.0f;
		/* A character or a steep face used to end the probe, so the train box under that hit was never a floor
		   and the body stayed at the kill height. Step past a hit that cannot hold the corpse. */
		for (int n = 0; n < 8 && !have_floor; n++) {
			trace_t tr;
			VectorCopy(p, q);
			q[2] = ent->s.origin[2] - 4096.0f;
			if (q[2] >= p[2])
				break;
			tr = gi.trace(p, vec3_origin, vec3_origin, q, ent, MASK_MONSTERSOLID);
			if (tr.startsolid || tr.allsolid) {
				p[2] += 64.0f;
				continue;
			}
			if (tr.fraction >= 1.0f)
				break;
			if (OM_SupportBrush(tr.ent, tr.plane.normal[2])) {
				/* The climb out of a startsolid volume can exit above a shell
				   and meet that shell on the way down. A train box may sit
				   above the feet and still has to catch the body. A world
				   face that far above the feet is a ceiling. */
				if (OM_SolidKind(tr.ent) != 3 &&
				    tr.endpos[2] > ent->s.origin[2] + feet + 48.0f) {
					p[2] = tr.endpos[2] - 1.0f;
					continue;
				}
				have_floor = 1;
				floorent = tr.ent;
				floor_z = tr.endpos[2];
				floor_nz = tr.plane.normal[2];
				VectorCopy(tr.plane.normal, floor_n);
				break;
			}
			p[2] = tr.endpos[2] - 1.0f;
		}
	}
	VectorCopy(ent->s.origin, origin);
	VectorCopy(ent->velocity, vel);
	/* Already standing on a real floor. The point probe and the movement box
	   disagree on a slope, and snapping the origin each frame made the corpse
	   hop. A far floor still lands an airborne kill. A character under the
	   body is not this floor, so the train roof below one still catches it. */
	if (!contact && have_floor && ent->groundentity) {
		float gap = (ent->s.origin[2] + feet) - floor_z;
		float slope_vz, slope_hop;
		int close = gap <= 24.0f && gap >= -24.0f;
		if (close && OM_CorpseSlope(OM_SolidKind(ent->groundentity), floor_nz, 1, vel[2], 0.0f, &slope_vz,
		                            &slope_hop) == OM_SLOPE_REST)
			contact = 1;
	}
	if (OM_DeathSettle(origin, vel, feet, contact, have_floor, floor_z)) {
		VectorCopy(origin, ent->s.origin);
		VectorCopy(vel, ent->velocity);
		if (floorent) {
			ent->groundentity = floorent;
			ent->groundentity_linkcount = floorent->linkcount;
		}
		/* The snap lands the feet. The mesh still has to follow the plane. */
		OM_LayOnFace(ent, floor_n);
	}
	Dmg_CorpseHull(ent);
	gi.linkentity(ent);
}

static void LandingEvent(edict_t *ent, float vz)
{
	if (ent->client || ent->health <= 0)
		return;
	if (vz <= -400)
		G_AddEvent(ent, 12, 0);
	else if (vz <= -200)
		G_AddEvent(ent, 11, 0);
	else if (vz <= -100)
		G_AddEvent(ent, 10, 0);
}

/*
============
SV_FlyMove (SPEC-760 B4.10)
============
*/
#define MAX_CLIP_PLANES 5
int SV_FlyMove(edict_t *ent, float time, int mask)
{
	int numbumps = 4, numplanes = 0, blocked = 0;
	vec3_t planes[MAX_CLIP_PLANES];
	vec3_t primal_velocity, original_velocity, new_velocity, dir, end, mins, maxs;
	float time_left = time;

	CorpseBox(ent, mins, maxs);
	VectorCopy(ent->velocity, original_velocity);
	VectorCopy(ent->velocity, primal_velocity);

	ent->groundentity = NULL;
	for (int bumpcount = 0; bumpcount < numbumps; bumpcount++) {
		for (int i = 0; i < 3; i++)
			end[i] = ent->s.origin[i] + time_left * ent->velocity[i];

		trace_t trace = gi.trace(ent->s.origin, mins, maxs, end, ent, mask);

		if (trace.allsolid) {
			VectorCopy(vec3_origin, ent->velocity);
			return 3;
		}
		if (trace.fraction > 0) {
			VectorCopy(trace.endpos, ent->s.origin);
			VectorCopy(ent->velocity, original_velocity);
			numplanes = 0;
		}

		// the end position is tested again (unstick)
		trace_t t2 = gi.trace(ent->s.origin, mins, maxs, ent->s.origin, ent, mask);
		if (t2.startsolid) {
			if (ent->solid == SOLID_CORPSE) {
				/* The stuck unstick reverses velocity and adds a random hop.
				   On a slope that hop is the corpse jumping in place. Rest on a
				   supporting face. On a steeper face, do not add the hop. */
				float nz = 0.0f;
				int kind = 5;
				float slope_vz, slope_hop;
				int decision;
				if (trace.fraction < 1.0f && trace.ent) {
					nz = trace.plane.normal[2];
					kind = OM_SolidKind(trace.ent);
				}
				decision = OM_CorpseSlope(kind, nz, 1, ent->velocity[2], 1.0f, &slope_vz, &slope_hop);
				if (decision == OM_SLOPE_REST) {
					VectorClear(ent->velocity);
					if (trace.fraction < 1.0f && trace.ent) {
						ent->groundentity = trace.ent;
						ent->groundentity_linkcount = trace.ent->linkcount;
						OM_LayOnFace(ent, trace.plane.normal);
					}
					return blocked | 1;
				}
				if (decision == OM_SLOPE_SLIDE && trace.fraction < 1.0f &&
				    OM_CorpseSlideVel(trace.plane.normal, ent->velocity)) {
					OM_StepOffSteep(ent, trace.plane.normal);
					OM_SlideIfOpen(ent, trace.plane.normal);
					return blocked;
				}
				ent->velocity[2] = slope_vz;
				return blocked;
			}
			if (trace.fraction < 1 && trace.plane.normal[2] > 0.7f && trace.ent &&
			    (trace.ent->solid == SOLID_BSP || trace.ent->solid == SOLID_GROUND)) {
				ent->groundentity = trace.ent;
				ent->groundentity_linkcount = trace.ent->linkcount;
			}
			VectorNormalize(ent->velocity);
			for (int i = 0; i < 3; i++)
				ent->velocity[i] = -ent->velocity[i] + crandom();
			return 7;
		}

		if (trace.fraction == 1)
			break;

		edict_t *hit = trace.ent;

		if (trace.plane.normal[2] > 0.7f) {
			blocked |= 1;
			if (hit && (hit->solid == SOLID_BSP || hit->solid == SOLID_GROUND)) {
				ent->groundentity = hit;
				ent->groundentity_linkcount = hit->linkcount;
				LandingEvent(ent, ent->velocity[2]);
			}
		}
		if (!trace.plane.normal[2])
			blocked |= 2;

		if (!SV_Impact(ent, &trace))
			break;
		if (!ent->inuse)
			break;

		time_left -= time_left * trace.fraction;

		if (numplanes >= MAX_CLIP_PLANES) {
			VectorCopy(vec3_origin, ent->velocity);
			return 3;
		}
		VectorCopy(trace.plane.normal, planes[numplanes]);
		numplanes++;

		if (ent->movetype == MOVETYPE_NOCLIP)
			continue;

		float overbounce = trace.fraction == 0 ? 1.1f : 1.0f;
		int i, j;
		for (i = 0; i < numplanes; i++) {
			ClipVelocity(original_velocity, planes[i], new_velocity, overbounce);
			for (j = 0; j < numplanes; j++)
				if ((j != i) && !VectorCompare(planes[i], planes[j])) {
					if (DotProduct(new_velocity, planes[j]) < 0)
						break;
				}
			if (j == numplanes)
				break;
		}

		if (i != numplanes)
			VectorCopy(new_velocity, ent->velocity);
		else {
			if (numplanes != 2) {
				VectorCopy(vec3_origin, ent->velocity);
				return 7;
			}
			CrossProduct(planes[0], planes[1], dir);
			float d = DotProduct(dir, ent->velocity);
			VectorScale(dir, d, ent->velocity);
		}

		// SoF: only the vertical component is stopped when it changed sign
		/* A corpse that was not already rising must not leave a slope upward. */
		if (ent->solid == SOLID_CORPSE && ent->velocity[2] > 0.0f && primal_velocity[2] <= 0.0f) {
			ent->velocity[2] = 0.0f;
			return blocked;
		}
		if ((ent->velocity[2] > 0 && primal_velocity[2] < 0) || (ent->velocity[2] < 0 && primal_velocity[2] > 0)) {
			ent->velocity[2] = 0;
			return blocked;
		}
	}
	return blocked;
}

// SPEC-760 B4.1
void SV_AddGravity(edict_t *ent)
{
	float g = sv_gravity->value * ent->gravity * FRAMETIME;
	ent->velocity[0] += g * sv_gravityx->value;
	ent->velocity[1] += g * sv_gravityy->value;
	ent->velocity[2] -= g * sv_gravityz->value;
}

/*
===============================================================================
PUSHMOVE
===============================================================================
*/

trace_t SV_PushEntity(edict_t *ent, vec3_t push)
{
	trace_t trace;
	vec3_t start, end;

	VectorCopy(ent->s.origin, start);
	VectorAdd(start, push, end);

retry:
	int mask = ent->clipmask ? ent->clipmask : MASK_SOLID;
	trace = gi.trace(start, ent->mins, ent->maxs, end, ent, mask);
	if (trace.allsolid && ent->movetype == MOVETYPE_TOSS) {
		// SPEC-760 B4.5: a TOSS entity that starts all-solid is not moved
		trace.fraction = 0;
		VectorCopy(start, trace.endpos);
		return trace;
	}

	VectorCopy(trace.endpos, ent->s.origin);
	gi.linkentity(ent);

	if (trace.fraction != 1.0) {
		SV_Impact(ent, &trace);
		if (trace.ent && !trace.ent->inuse && ent->inuse) {
			VectorCopy(start, ent->s.origin);
			gi.linkentity(ent);
			goto retry;
		}
	}

	if (ent->inuse)
		G_TouchTriggers(ent);

	return trace;
}

typedef struct {
	edict_t *ent;
	vec3_t origin;
	vec3_t angles;
	float deltayaw;
} pushed_t;
static pushed_t pushed[MAX_EDICTS], *pushed_p;

static edict_t *obstacle;

// SPEC-760 B4.4
static qboolean SV_Push(edict_t *pusher, vec3_t move, vec3_t amove)
{
	int i, e;
	edict_t *check, *block;
	vec3_t mins, maxs;
	pushed_t *p;
	vec3_t org, org2, move2, forward, right, up;

	for (i = 0; i < 3; i++) {
		mins[i] = pusher->absmin[i] + move[i];
		maxs[i] = pusher->absmax[i] + move[i];
	}

	VectorSubtract(vec3_origin, amove, org);
	AngleVectors(org, forward, right, up);

	pushed_p->ent = pusher;
	VectorCopy(pusher->s.origin, pushed_p->origin);
	VectorCopy(pusher->s.angles, pushed_p->angles);
	if (pusher->client)
		pushed_p->deltayaw = pusher->client->ps.pmove.delta_angles[YAW];
	pushed_p++;

	VectorAdd(pusher->s.origin, move, pusher->s.origin);
	VectorAdd(pusher->s.angles, amove, pusher->s.angles);
	gi.linkentity(pusher);

	check = g_edicts + 1;
	for (e = 1; e < globals.num_edicts; e++, check++) {
		if (!check->inuse)
			continue;
		if (check->movetype == MOVETYPE_PUSH || check->movetype == MOVETYPE_STOP ||
		    check->movetype == MOVETYPE_NONE || check->movetype == MOVETYPE_NOCLIP)
			continue;
		if (!check->s.solid || check->attachedto)
			continue;
		if (!check->area_prev)
			continue;

		if (check->groundentity != pusher) {
			if (check->absmin[0] >= maxs[0] || check->absmin[1] >= maxs[1] || check->absmin[2] >= maxs[2] ||
			    check->absmax[0] <= mins[0] || check->absmax[1] <= mins[1] || check->absmax[2] <= mins[2])
				continue;
			if (!SV_TestEntityPosition(check))
				continue;
		}

		if ((pusher->movetype == MOVETYPE_PUSH) || (check->groundentity == pusher)) {
			pushed_p->ent = check;
			VectorCopy(check->s.origin, pushed_p->origin);
			VectorCopy(check->s.angles, pushed_p->angles);
			pushed_p++;

			VectorAdd(check->s.origin, move, check->s.origin);
			if (check->client)
				check->client->ps.pmove.delta_angles[YAW] += (short)ANGLE2SHORT(amove[YAW]);

			VectorSubtract(check->s.origin, pusher->s.origin, org);
			org2[0] = DotProduct(org, forward);
			org2[1] = -DotProduct(org, right);
			org2[2] = DotProduct(org, up);
			VectorSubtract(org2, org, move2);
			VectorAdd(check->s.origin, move2, check->s.origin);

			if (check->groundentity != pusher)
				check->groundentity = NULL;

			block = SV_TestEntityPosition(check);
			if (!block) {
				gi.linkentity(check);
				continue;
			}

			VectorSubtract(check->s.origin, move, check->s.origin);
			block = SV_TestEntityPosition(check);
			if (!block) {
				pushed_p--;
				continue;
			}
		}

		obstacle = check;

		for (p = pushed_p - 1; p >= pushed; p--) {
			VectorCopy(p->origin, p->ent->s.origin);
			VectorCopy(p->angles, p->ent->s.angles);
			if (p->ent->client)
				p->ent->client->ps.pmove.delta_angles[YAW] = (short)p->deltayaw;
			gi.linkentity(p->ent);
		}
		return false;
	}

	for (p = pushed_p - 1; p >= pushed; p--)
		G_TouchTriggers(p->ent);

	return true;
}

static void SV_Physics_Pusher(edict_t *ent)
{
	vec3_t move, amove;
	edict_t *part, *mv;

	if (ent->flags & FL_TEAMSLAVE)
		return;

	pushed_p = pushed;
	for (part = ent; part; part = part->teamchain) {
		if (part->velocity[0] || part->velocity[1] || part->velocity[2] || part->avelocity[0] ||
		    part->avelocity[1] || part->avelocity[2]) {
			VectorScale(part->velocity, FRAMETIME, move);
			VectorScale(part->avelocity, FRAMETIME, amove);
			if (!SV_Push(part, move, amove))
				break;
		}
	}
	if (pushed_p > &pushed[MAX_EDICTS])
		gi.error("pushed_p > &pushed[MAX_EDICTS], memory corrupted");

	if (part) {
		for (mv = ent; mv; mv = mv->teamchain) {
			if (mv->nextthink > 0)
				mv->nextthink += FRAMETIME;
		}
		if (part->blocked)
			part->blocked(part, obstacle);
	} else {
		for (part = ent; part; part = part->teamchain)
			SV_RunThink(part);
	}
}

//==================================================================

static void SV_Physics_None(edict_t *ent) { SV_RunThink(ent); }

// SPEC-760 2: move first, then think
static void SV_Physics_Noclip(edict_t *ent)
{
	VectorMA(ent->s.angles, FRAMETIME, ent->avelocity, ent->s.angles);
	VectorMA(ent->s.origin, FRAMETIME, ent->velocity, ent->s.origin);
	gi.linkentity(ent);
	SV_RunThink(ent);
}

static void SV_SetWaterLevel(edict_t *ent)
{
	ent->watertype = gi.pointcontents(ent->s.origin);
	ent->waterlevel = (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER)) ? 1 : 0;
}

static void SV_Physics_Step(edict_t *ent);

/*
=============
SV_Physics_Toss (SPEC-760 B4.5)
=============
*/
static void SV_Physics_Toss(edict_t *ent)
{
	trace_t trace;
	vec3_t move;
	edict_t *slave;

	SV_RunThink(ent);
	if (!ent->inuse)
		return;

	if (ent->flags & FL_TEAMSLAVE)
		return;

	if (ent->velocity[2] > 0)
		ent->groundentity = NULL;
	if (ent->groundentity && !ent->groundentity->inuse)
		ent->groundentity = NULL;
	if (ent->groundentity)
		return;

	SV_CheckVelocity(ent);

	if (ent->movetype != MOVETYPE_FLY && ent->movetype != MOVETYPE_FLYMISSILE)
		SV_AddGravity(ent);
	if (ent->waterlevel == 1)
		ent->velocity[2] += sv_gravity->value * ent->buoyancy * FRAMETIME;

	VectorMA(ent->s.angles, FRAMETIME, ent->avelocity, ent->s.angles);

	VectorScale(ent->velocity, FRAMETIME, move);
	float vz = ent->velocity[2];
	int movetype = ent->movetype;
	trace = SV_PushEntity(ent, move);
	if (!ent->inuse)
		return;
	// a touch during the push that changed the movetype owns the entity's motion now: trn1's `func_pushkillbox` sets
	// NOCLIP and the fling velocity on the helicopter, and the original flings it (G4: its explosion lands 48 units
	// along the fling one frame later), so the landing below must not turn it into a stepping entity (UNVERIFIED rule,
	// OPEN_QUESTIONS G18 38)
	if (ent->movetype != movetype)
		return;

	bool tostep = false;
	if (trace.fraction < 1) {
		float backoff = ent->movetype == MOVETYPE_BOUNCE ? 1.5f : 1.0f;
		ClipVelocity(ent->velocity, trace.plane.normal, ent->velocity, backoff);
		if (trace.plane.normal[2] > 0.7f) {
			if (ent->velocity[2] < 30 || ent->movetype != MOVETYPE_BOUNCE) {
				ent->groundentity = trace.ent;
				ent->groundentity_linkcount = trace.ent ? trace.ent->linkcount : 0;
				ent->movetype = MOVETYPE_STEP;
				ent->velocity[2] = 0;
				LandingEvent(ent, vz);
				tostep = true;
			}
		}
	}

	SV_SetWaterLevel(ent);

	for (slave = ent->teamchain; slave; slave = slave->teamchain) {
		VectorCopy(ent->s.origin, slave->s.origin);
		gi.linkentity(slave);
	}

	if (tostep)
		SV_Physics_Step(ent);
}

/*
===============================================================================
STEPPING MOVEMENT (SPEC-760 B4.6, B4.9, B4.11)
===============================================================================
*/

static void SV_RotationalFriction(edict_t *ent)
{
	float adj = ent->groundentity ? ent->friction * 12 : ent->airfriction * 0.01f;
	for (int n = 0; n < 3; n++) {
		if (ent->avelocity[n] > 0) {
			ent->avelocity[n] -= adj;
			if (ent->avelocity[n] < 0)
				ent->avelocity[n] = 0;
		} else {
			ent->avelocity[n] += adj;
			if (ent->avelocity[n] > 0)
				ent->avelocity[n] = 0;
		}
	}
}

static void SV_LinearFriction(edict_t *ent)
{
	float adj = ent->groundentity ? ent->friction * 60 : ent->airfriction * 0.01f;
	for (int n = 0; n < 3; n++) {
		if (ent->velocity[n] > 0) {
			ent->velocity[n] -= adj;
			if (ent->velocity[n] < 0)
				ent->velocity[n] = 0;
		} else {
			ent->velocity[n] += adj;
			if (ent->velocity[n] > 0)
				ent->velocity[n] = 0;
		}
	}
}

/* A stuck corpse box has no plane. Drop a point from above the origin until it
   reads the face under the feet. A ceiling (normal down) is not that face. */
static int OM_SlopeFace(edict_t *ent, trace_t *face)
{
	vec3_t a, b;
	int mask = ent->clipmask ? ent->clipmask : MASK_SOLID;

	for (int n = 0; n < 4; n++) {
		VectorCopy(ent->s.origin, a);
		a[2] = ent->s.origin[2] + 16.0f + 32.0f * (float)n;
		VectorCopy(a, b);
		b[2] = ent->s.origin[2] - 48.0f;
		if (b[2] >= a[2])
			return 0;
		*face = gi.trace(a, vec3_origin, vec3_origin, b, ent, mask);
		if (face->startsolid || face->allsolid)
			continue;
		if (face->fraction < 1.0f && face->plane.normal[2] > 0.0f)
			return 1;
		return 0;
	}
	return 0;
}

void SV_CheckGround(edict_t *ent)
{
	vec3_t point, mins, maxs;
	float down = (ent->groundentity && ent->groundentity->solid == SOLID_GROUND) ? 2.5f : 0.25f;
	CorpseBox(ent, mins, maxs);
	VectorCopy(ent->s.origin, point);
	point[2] -= down;
	int mask = ent->clipmask ? ent->clipmask : MASK_SOLID;
	trace_t tr = gi.trace(ent->s.origin, mins, maxs, point, ent, mask);
	if (tr.allsolid) {
		ent->groundentity = tr.ent;
		ent->groundentity_linkcount = tr.ent ? tr.ent->linkcount : 0;
		return;
	}
	/* The corpse box cuts into a slope, so this trace is startsolid and used to
	   clear the floor. The body then fell, landed, and hopped. A close supporting
	   face holds it. A steep face stays a slide, with no upward kick. */
	if (ent->solid == SOLID_CORPSE && tr.startsolid) {
		trace_t face;
		float gap, slope_vz, slope_hop;
		int decision = OM_SLOPE_FALL;
		slope_vz = ent->velocity[2];
		if (OM_SlopeFace(ent, &face)) {
			gap = (ent->s.origin[2] + OM_CorpseFeet(ent)) - face.endpos[2];
			if (gap <= 24.0f && gap >= -24.0f)
				decision = OM_CorpseSlope(OM_SolidKind(face.ent), face.plane.normal[2], 1, ent->velocity[2],
				                          0.0f, &slope_vz, &slope_hop);
		}
		if (decision == OM_SLOPE_REST) {
			ent->groundentity = face.ent ? face.ent : world;
			ent->groundentity_linkcount = ent->groundentity->linkcount;
			VectorClear(ent->velocity);
			OM_LayOnFace(ent, face.plane.normal);
			return;
		}
		if (decision == OM_SLOPE_SLIDE) {
			if (!OM_CorpseSlideVel(face.plane.normal, ent->velocity))
				ent->velocity[2] = slope_vz;
			else
				OM_StepOffSteep(ent, face.plane.normal);
			OM_SlideIfOpen(ent, face.plane.normal);
			ent->groundentity = NULL;
			return;
		}
		if (ent->velocity[2] > 0.0f)
			ent->velocity[2] = 0.0f;
		if (ent->groundentity && OM_CorpseSupport(OM_SolidKind(ent->groundentity), 1.0f))
			return;
		ent->groundentity = NULL;
		return;
	}
	if (!tr.startsolid && tr.fraction == 1) {
		ent->groundentity = NULL;
		return;
	}
	/* A corpse uses 45 degrees. A living body keeps the old 0.7 walkable test. */
	if (!tr.startsolid && tr.fraction < 1.0f && ent->solid == SOLID_CORPSE &&
	    OM_CorpseSupport(OM_SolidKind(tr.ent), tr.plane.normal[2])) {
		VectorCopy(tr.endpos, ent->s.origin);
		ent->groundentity = tr.ent;
		ent->groundentity_linkcount = tr.ent ? tr.ent->linkcount : 0;
		VectorClear(ent->velocity);
		OM_LayOnFace(ent, tr.plane.normal);
		return;
	}
	if (!tr.startsolid && ent->solid != SOLID_CORPSE && tr.plane.normal[2] >= 0.7f) {
		VectorCopy(tr.endpos, ent->s.origin);
		ent->groundentity = tr.ent;
		ent->groundentity_linkcount = tr.ent ? tr.ent->linkcount : 0;
		return;
	}
	if (ent->solid == SOLID_CORPSE && !tr.startsolid && tr.fraction < 1.0f && tr.plane.normal[2] > 0.0f &&
	    tr.plane.normal[2] < OM_CORPSE_SLOPE_MIN) {
		if (!OM_CorpseSlideVel(tr.plane.normal, ent->velocity)) {
			float slope_vz, slope_hop;
			if (OM_CorpseSlope(OM_SolidKind(tr.ent), tr.plane.normal[2], 0, ent->velocity[2], 0.0f, &slope_vz,
			                   &slope_hop) == OM_SLOPE_SLIDE)
				ent->velocity[2] = slope_vz;
		}
		OM_SlideIfOpen(ent, tr.plane.normal);
	}
	ent->groundentity = NULL;
}

static void SV_Physics_Step(edict_t *ent)
{
	if (ent->stopspeed == 0)
		ent->stopspeed = 100;

	if (ent->solid == SOLID_CORPSE)
		OM_SettleDeadBody(ent);

	if (ent->solid == SOLID_CORPSE || !ent->groundentity)
		SV_CheckGround(ent);

	edict_t *groundentity = ent->groundentity;
	SV_CheckVelocity(ent);
	qboolean wasonground = groundentity != NULL;

	if (ent->avelocity[0] || ent->avelocity[1] || ent->avelocity[2]) {
		for (int i = 0; i < 3; i++)
			if (fabs(ent->avelocity[i]) < 1)
				ent->avelocity[i] = 0;
		VectorMA(ent->s.angles, FRAMETIME, ent->avelocity, ent->s.angles);
		SV_RotationalFriction(ent);
	}

	if (!wasonground)
		if (!(ent->flags & FL_FLY))
			if (!((ent->flags & FL_SWIM) && (ent->waterlevel > 2)))
				if (ent->waterlevel == 0)
					SV_AddGravity(ent);

	if ((ent->flags & FL_FLY) && (ent->velocity[2] != 0)) {
		float speed = fabs(ent->velocity[2]);
		float control = speed < ent->stopspeed ? ent->stopspeed : speed;
		float newspeed = speed - ent->friction * 200 * control * FRAMETIME;
		if (newspeed < 0)
			newspeed = 0;
		ent->velocity[2] *= newspeed / speed;
	}
	if ((ent->flags & FL_SWIM) && (ent->velocity[2] != 0)) {
		float speed = fabs(ent->velocity[2]);
		float control = speed < ent->stopspeed ? ent->stopspeed : speed;
		float newspeed = speed - FRAMETIME * control * ent->waterlevel;
		if (newspeed < 0)
			newspeed = 0;
		ent->velocity[2] *= newspeed / speed;
	}

	if (ent->velocity[2] || ent->velocity[1] || ent->velocity[0]) {
		if (wasonground || (ent->flags & (FL_SWIM | FL_FLY))) {
			float *vel = ent->velocity;
			float speed = sqrt(vel[0] * vel[0] + vel[1] * vel[1]);
			if (speed) {
				float newspeed;
				if (ent->health < 0)
					newspeed = speed - ent->friction * 300;
				else
					newspeed = speed - ent->stopspeed * ent->friction * 60;
				if (newspeed < 0)
					newspeed = 0;
				newspeed /= speed;
				vel[0] *= newspeed;
				vel[1] *= newspeed;
			}
		}

		int mask = (ent->svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (ent->clipmask ? ent->clipmask : MASK_SOLID);
		SV_FlyMove(ent, FRAMETIME, mask);

		gi.linkentity(ent);
		if (ent->solid != SOLID_CORPSE)
			G_TouchTriggers(ent);
		if (!ent->inuse)
			return;
	}

	SV_RunThink(ent);
}

/*
=============
SV_Physics_Dan (SPEC-760 B4.7): movetypes 10 and 11
=============
*/
static void SV_Physics_Dan(edict_t *ent)
{
	SV_RunThink(ent);
	if (!ent->inuse)
		return;
	if (ent->attachedto)
		return;

	// SPEC-AMEND-006 G7.11: the ground is rechecked when moving, when there is no ground entity, or when the ground
	// entity is not the world; an all-solid start takes the hit entity as ground (SV_CheckGround), which holds the
	// nycctb2 radar dish whose box top overlaps the slab above it
	qboolean moving = ent->velocity[0] || ent->velocity[1] || ent->velocity[2];
	if (moving || !ent->groundentity || ent->groundentity != world)
		SV_CheckGround(ent);

	SV_CheckVelocity(ent);
	if ((!ent->groundentity && ent->gravity != 0) || sv_gravityx->value || sv_gravityy->value ||
	    sv_gravityz->value < 0)
		SV_AddGravity(ent);
	SV_SetWaterLevel(ent);
	if (ent->waterlevel == 1)
		ent->velocity[2] += sv_gravity->value * ent->buoyancy * FRAMETIME;

	moving = ent->velocity[0] || ent->velocity[1] || ent->velocity[2];
	if (moving) {
		int mask = ent->clipmask ? ent->clipmask : MASK_SOLID;
		float time_left = FRAMETIME;
		int numplanes = 0;
		vec3_t smins, smaxs;
		for (int i = 0; i < 3; i++) {
			smins[i] = ent->mins[i] + 1;
			smaxs[i] = ent->maxs[i] - 1;
			if (smins[i] > smaxs[i])
				smins[i] = smaxs[i] = (ent->mins[i] + ent->maxs[i]) * 0.5f;
		}
		for (int bump = 0; bump < 4 && numplanes < 5; bump++) {
			vec3_t end;
			VectorMA(ent->s.origin, time_left, ent->velocity, end);
			// SPEC-760 B4.7: trace the move, then test its end position with the box shrunk by 1 unit; when that is
			// solid the move is not taken and the velocity becomes 0. There is no all-solid early stop: a box that
			// only touches the floor traces all-solid (fraction 1) and still moves (g7bench0's fallen bench, 0.75 and
			// 0.625 units on two explosions in the original), while a box sunk into a world brush or another prop
			// stays where it is (g7bench90's upright benches, g7shot's bench inside the table: the original does not
			// move them). Measured with G7's three cases (OPEN_QUESTIONS G18 43).
			trace_t tr = gi.trace(ent->s.origin, ent->mins, ent->maxs, end, ent, mask);
			vec3_t from;
			VectorCopy(ent->s.origin, from);
			VectorCopy(tr.endpos, ent->s.origin);

			trace_t t2 = gi.trace(ent->s.origin, smins, smaxs, ent->s.origin, ent, mask);
			if (t2.startsolid && !(t2.ent && (t2.ent->svflags & SVF_PLAYERPROXY))) {
				VectorCopy(from, ent->s.origin);
				VectorClear(ent->velocity);
				if (tr.fraction < 1 && tr.plane.normal[2] > 0.7f && tr.ent && tr.ent->solid == SOLID_BSP) {
					ent->groundentity = tr.ent;
					ent->groundentity_linkcount = tr.ent->linkcount;
				}
				break;
			}
			if (tr.fraction == 1)
				break;

			if (tr.plane.normal[2] > 0.7f && tr.ent && (tr.ent->solid == SOLID_BSP || tr.ent->solid == SOLID_GROUND)) {
				ent->groundentity = tr.ent;
				ent->groundentity_linkcount = tr.ent->linkcount;
				LandingEvent(ent, ent->velocity[2]);
			}
			if (!SV_Impact(ent, &tr) || !ent->inuse)
				break;

			if (ent->elasticity != -1) {
				float vn = DotProduct(ent->velocity, tr.plane.normal);
				for (int i = 0; i < 3; i++)
					ent->velocity[i] = ent->elasticity * (ent->velocity[i] - 1.8f * vn * tr.plane.normal[i]);
				if (fabs(ent->velocity[0]) < 1)
					ent->velocity[0] = 0;
				if (fabs(ent->velocity[1]) < 1)
					ent->velocity[1] = 0;
				if (fabs(ent->velocity[2]) < 100)
					ent->velocity[2] = 0;
			}
			numplanes++;
			time_left -= time_left * tr.fraction;
		}
		if (!ent->inuse)
			return;
	}

	SV_CheckGround(ent);
	for (int i = 0; i < 3; i++)
		if (fabs(ent->avelocity[i]) < 1)
			ent->avelocity[i] = 0;
	VectorMA(ent->s.angles, FRAMETIME, ent->avelocity, ent->s.angles);
	SV_RotationalFriction(ent);
	SV_LinearFriction(ent);
	gi.linkentity(ent);
}

//============================================================================

void G_RunEntity(edict_t *ent)
{
	if (ent->prethink)
		ent->prethink(ent);

	switch ((int)ent->movetype) {
	case MOVETYPE_PUSH:
	case MOVETYPE_STOP:
		SV_Physics_Pusher(ent);
		break;
	case MOVETYPE_NONE:
		SV_Physics_None(ent);
		break;
	case MOVETYPE_NOCLIP:
		SV_Physics_Noclip(ent);
		break;
	case MOVETYPE_STEP:
		SV_Physics_Step(ent);
		break;
	case MOVETYPE_TOSS:
	case MOVETYPE_BOUNCE:
	case MOVETYPE_FLY:
	case MOVETYPE_FLYMISSILE:
		SV_Physics_Toss(ent);
		break;
	case MOVETYPE_DAN:
	case MOVETYPE_DANTOUCH:
		SV_Physics_Dan(ent);
		break;
	default:
		gi.error("SV_Physics: bad movetype %i", (int)ent->movetype);
	}
}

// SPEC-202 2: up to three (event, parameter) pairs, bit 7 = another pair follows
void G_AddEvent(edict_t *ent, int ev, int parm)
{
	for (int i = 0; i < 3; i++) {
		if (ent->s.event[i] & 0x7F)
			continue;
		ent->s.event[i] = ev & 0x7F;
		ent->s.eventparm[i] = parm;
		if (i > 0)
			ent->s.event[i - 1] |= 0x80;
		return;
	}
}
