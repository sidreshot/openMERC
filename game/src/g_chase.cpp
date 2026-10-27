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
 * OpenMerc: changed for the SoF 1.07f game module (G18): SoF chase-camera geometry (35 units behind
 * the eye, +16 when the target is airborne, pitch clamp 56), dead-target view and the "no more chase"
 * string-package message.
 * Implements: SPEC-780 17
 */

#include "g_local.h"

void SP_PrintTo(edict_t *ent, const char *package, const char *entry);

void UpdateChaseCam(edict_t *ent)
{
	edict_t *old = ent->client->chase_target;
	if (!old->inuse || old->client->resp.spectator) {
		ChaseNext(ent);
		if (ent->client->chase_target == old) {
			ent->client->chase_target = NULL;
			ent->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;
			return;
		}
	}
	edict_t *targ = ent->client->chase_target;

	vec3_t ownerv, angles, forward, o, goal;
	VectorCopy(targ->s.origin, ownerv);
	ownerv[2] += targ->viewheight;
	VectorCopy(targ->client->v_angle, angles);
	if (angles[PITCH] > 56)
		angles[PITCH] = 56;
	if (angles[PITCH] < -56)
		angles[PITCH] = -56;
	AngleVectors(angles, forward, NULL, NULL);
	VectorNormalize(forward);
	VectorMA(ownerv, -35, forward, o);
	if (!targ->groundentity)
		o[2] += 16;
	trace_t trace = gi.trace(ownerv, vec3_origin, vec3_origin, o, targ, MASK_SOLID);
	VectorCopy(trace.endpos, goal);
	VectorCopy(goal, ent->s.origin);

	if (targ->deadflag) {
		ent->client->ps.pmove.pm_type = PM_DEAD;
		ent->client->ps.viewangles[ROLL] = 40;
		ent->client->ps.viewangles[PITCH] = -15;
		ent->client->ps.viewangles[YAW] = targ->client->killer_yaw;
	} else {
		ent->client->ps.pmove.pm_type = PM_INTERMISSION_SPEC;
		VectorCopy(targ->client->v_angle, ent->client->ps.viewangles);
		VectorCopy(targ->client->v_angle, ent->client->v_angle);
	}
	for (int i = 0; i < 3; i++)
		ent->client->ps.pmove.delta_angles[i] =
			(short)ANGLE2SHORT(targ->client->v_angle[i] - ent->client->resp.cmd_angles[i]);
	ent->client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
	ent->viewheight = 0;
	ent->client->ps.spectated = (uint8_t)G_EdictNum(targ);
	gi.linkentity(ent);
}

void ChaseNext(edict_t *ent)
{
	if (!ent->client->chase_target)
		return;
	int i = G_EdictNum(ent->client->chase_target);
	edict_t *e;
	do {
		i++;
		if (i > maxclients->value)
			i = 1;
		e = g_edicts + i;
		if (!e->inuse)
			continue;
		if (!e->client->resp.spectator)
			break;
	} while (e != ent->client->chase_target);
	ent->client->chase_target = e;
	ent->client->update_chase = true;
}

void ChasePrev(edict_t *ent)
{
	if (!ent->client->chase_target)
		return;
	int i = G_EdictNum(ent->client->chase_target);
	edict_t *e;
	do {
		i--;
		if (i < 1)
			i = (int)maxclients->value;
		e = g_edicts + i;
		if (!e->inuse)
			continue;
		if (!e->client->resp.spectator)
			break;
	} while (e != ent->client->chase_target);
	ent->client->chase_target = e;
	ent->client->update_chase = true;
}

void GetChaseTarget(edict_t *ent)
{
	for (int i = 1; i <= maxclients->value; i++) {
		edict_t *other = g_edicts + i;
		if (other->inuse && other != ent && !other->client->resp.spectator) {
			ent->client->chase_target = other;
			ent->client->update_chase = true;
			UpdateChaseCam(ent);
			return;
		}
	}
	SP_PrintTo(ent, "dm_generic", "TEXT_NO_MORE_CHASE");
}
