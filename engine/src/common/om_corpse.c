/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Corpse shot volumes and the airborne death settle.
 */

#include "header/om_corpse.h"

#include <math.h>

/* AngleVectors in shared.c: yaw around Z, then pitch, then roll.
   forward = (cp*cy, cp*sy, -sp), right and up as written there. */
static void om_axes(const float angles[3], float fwd[3], float right[3], float up[3])
{
	const float deg = 0.017453292519943295f;
	float sy, cy, sp, cp, sr, cr;

	sy = sinf(angles[1] * deg);
	cy = cosf(angles[1] * deg);
	sp = sinf(angles[0] * deg);
	cp = cosf(angles[0] * deg);
	sr = sinf(angles[2] * deg);
	cr = cosf(angles[2] * deg);

	fwd[0] = cp * cy;
	fwd[1] = cp * sy;
	fwd[2] = -sp;

	right[0] = (-1.0f * sr * sp * cy + -1.0f * cr * -sy);
	right[1] = (-1.0f * sr * sp * sy + -1.0f * cr * cy);
	right[2] = -1.0f * sr * cp;

	up[0] = (cr * sp * cy + -sr * -sy);
	up[1] = (cr * sp * sy + -sr * cy);
	up[2] = cr * cp;
}

static void om_to_world(const float origin[3], const float fwd[3], const float right[3], const float up[3],
                        const float local[3], float world[3])
{
	int i;

	for (i = 0; i < 3; i++)
		world[i] = origin[i] + local[0] * fwd[i] + local[1] * right[i] + local[2] * up[i];
}

int OM_CorpsePart(const float origin[3], const float angles[3], const om_part_t *parts, int nparts,
                  const float start[3], const float end[3], int *hit_index, float hit_point[3])
{
	float fwd[3], right[3], up[3], dir[3];
	float best_t, best_d;
	int best, i;

	if (hit_index)
		*hit_index = -1;
	if (!origin || !angles || !parts || nparts <= 0 || !start || !end)
		return OM_PART_NONE;

	om_axes(angles, fwd, right, up);
	dir[0] = end[0] - start[0];
	dir[1] = end[1] - start[1];
	dir[2] = end[2] - start[2];

	best = -1;
	best_t = 2.0f;
	best_d = 0.0f;
	for (i = 0; i < nparts; i++) {
		float world[3], rel[3], len2, t, dist2, rad;

		if (parts[i].id == OM_PART_NONE || parts[i].radius <= 0.0f)
			continue;
		om_to_world(origin, fwd, right, up, parts[i].center, world);
		rel[0] = world[0] - start[0];
		rel[1] = world[1] - start[1];
		rel[2] = world[2] - start[2];
		len2 = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
		if (len2 < 1.0e-8f)
			t = 0.0f;
		else {
			t = (rel[0] * dir[0] + rel[1] * dir[1] + rel[2] * dir[2]) / len2;
			if (t < 0.0f)
				t = 0.0f;
			else if (t > 1.0f)
				t = 1.0f;
		}
		{
			float closest[3];
			closest[0] = start[0] + t * dir[0] - world[0];
			closest[1] = start[1] + t * dir[1] - world[1];
			closest[2] = start[2] + t * dir[2] - world[2];
			dist2 = closest[0] * closest[0] + closest[1] * closest[1] + closest[2] * closest[2];
		}
		rad = parts[i].radius;
		if (dist2 > rad * rad)
			continue;
		/* The first volume along the segment wins. A tie keeps the closer centre. */
		if (best < 0 || t < best_t - 1.0e-5f || (t <= best_t + 1.0e-5f && dist2 < best_d)) {
			best = i;
			best_t = t;
			best_d = dist2;
			if (hit_point) {
				hit_point[0] = start[0] + t * dir[0];
				hit_point[1] = start[1] + t * dir[1];
				hit_point[2] = start[2] + t * dir[2];
			}
		}
	}
	if (best < 0)
		return OM_PART_NONE;
	if (hit_index)
		*hit_index = best;
	return parts[best].id;
}

int OM_DeathSettle(float origin[3], float velocity[3], float feet_offset, int contact, int have_floor,
                   float floor_z)
{
	if (!origin || !velocity)
		return 0;
	if (contact) {
		velocity[2] = 0.0f;
		return 1;
	}
	if (have_floor) {
		origin[2] = floor_z - feet_offset;
		velocity[0] = 0.0f;
		velocity[1] = 0.0f;
		velocity[2] = 0.0f;
		return 1;
	}
	return 0;
}

int OM_CorpseSupport(int kind, float normal_z)
{
	if (normal_z < OM_CORPSE_SLOPE_MIN)
		return 0;
	return kind >= 0 && kind <= 3;
}

static int om_unit(const float in[3], float out[3])
{
	float len;

	if (!in)
		return 0;
	len = sqrtf(in[0] * in[0] + in[1] * in[1] + in[2] * in[2]);
	if (len < 1.0e-8f)
		return 0;
	out[0] = in[0] / len;
	out[1] = in[1] / len;
	out[2] = in[2] / len;
	return 1;
}

int OM_CorpseAlign(float yaw, const float normal[3], float *pitch, float *roll)
{
	float n[3], sy, cy, dot_f, sr, cr, sp, cp;
	const float deg = 57.2957795f;
	const float rad = 0.01745329252f;

	if (!pitch || !roll || !om_unit(normal, n))
		return 0;
	if (n[2] < OM_CORPSE_SLOPE_MIN)
		return 0;
	sy = sinf(yaw * rad);
	cy = cosf(yaw * rad);
	/* Horizontal forward (cos yaw, sin yaw) and Quake right (sin yaw, -cos yaw). */
	dot_f = n[0] * cy + n[1] * sy;
	sr = n[0] * sy - n[1] * cy;
	cr = sqrtf(dot_f * dot_f + n[2] * n[2]);
	if (cr < 1.0e-6f)
		return 0;
	sp = dot_f / cr;
	cp = n[2] / cr;
	/* AngleVectors up of (P, yaw, R) is the normal. s.angles stores -P and -R. */
	*pitch = -atan2f(sp, cp) * deg;
	*roll = -atan2f(sr, cr) * deg;
	return 1;
}

int OM_CorpseSlideVel(const float normal[3], float velocity[3])
{
	float n[3], vn;
	int k;

	if (!velocity || !om_unit(normal, n))
		return 0;
	if (n[2] <= 0.0f || n[2] >= OM_CORPSE_SLOPE_MIN)
		return 0;
	/* Upward speed on a steep face is the hop. Do not turn it into a climb. */
	if (velocity[2] > 0.0f) {
		velocity[0] = 0.0f;
		velocity[1] = 0.0f;
		velocity[2] = 0.0f;
		return 1;
	}
	vn = velocity[0] * n[0] + velocity[1] * n[1] + velocity[2] * n[2];
	if (vn < 0.0f) {
		for (k = 0; k < 3; k++)
			velocity[k] -= vn * n[k];
	}
	return 1;
}

float OM_CorpseUnstick(float normal_z, int embedded)
{
	if (!embedded || normal_z <= 0.0f || normal_z >= OM_CORPSE_SLOPE_MIN)
		return 0.0f;
	return 1.0f;
}

int OM_CorpsePlant(float origin[3], float feet, const float plane_point[3], const float normal[3])
{
	float n[3], bolt[3], dist;
	int k;

	if (!origin || !plane_point || !om_unit(normal, n))
		return 0;
	if (n[2] < OM_CORPSE_SLOPE_MIN)
		return 0;
	bolt[0] = origin[0];
	bolt[1] = origin[1];
	bolt[2] = origin[2] + feet;
	dist = 0.0f;
	for (k = 0; k < 3; k++)
		dist += (bolt[k] - plane_point[k]) * n[k];
	/* Above the plane by more than a step, and not a different floor. Under the
	   plane is left where it is: pulling it up is how a dipped limb floats the torso. */
	if (dist <= 2.0f || dist > 80.0f)
		return 0;
	for (k = 0; k < 3; k++)
		origin[k] -= n[k] * dist;
	return 1;
}

int OM_CorpseKeepFall(int is_corpse, int on_ground)
{
	return is_corpse && !on_ground;
}

int OM_CorpseSlope(int kind, float normal_z, int embedded, float vz, float hop, float *vz_out, float *hop_out)
{
	if (vz_out)
		*vz_out = vz;
	if (hop_out)
		*hop_out = hop;

	if (OM_CorpseSupport(kind, normal_z)) {
		/* Embedded in a floor, or coming down onto one: stay. A hop here is the
		   stuck box leaving and landing again. An upward speed that has already
		   left the face is still a fall. */
		if (embedded || vz <= 0.0f) {
			if (vz_out)
				*vz_out = 0.0f;
			if (hop_out)
				*hop_out = 0.0f;
			return OM_SLOPE_REST;
		}
		return OM_SLOPE_FALL;
	}

	/* Too steep to stand on, or a character. Slide when the box is in the face,
	   or when a downward move meets an upward face. Do not turn that into a jump.
	   The band between the old 0.7 walkable test and 45 degrees slides too. */
	if (embedded || (normal_z > 0.0f && normal_z < OM_CORPSE_SLOPE_MIN && vz <= 0.0f)) {
		if (hop_out)
			*hop_out = 0.0f;
		if (vz_out && vz > 0.0f)
			*vz_out = 0.0f;
		return OM_SLOPE_SLIDE;
	}

	return OM_SLOPE_FALL;
}

int OM_CorpseSlopeStep(float origin[3], float velocity[3], int kind, float normal_z, int embedded, float hop)
{
	float vz, hop_out;
	int decision;

	if (!origin || !velocity)
		return OM_SLOPE_FALL;
	decision = OM_CorpseSlope(kind, normal_z, embedded, velocity[2], hop, &vz, &hop_out);
	if (decision == OM_SLOPE_REST) {
		velocity[0] = 0.0f;
		velocity[1] = 0.0f;
		velocity[2] = 0.0f;
		return decision;
	}
	if (decision == OM_SLOPE_SLIDE) {
		velocity[2] = vz;
		return decision;
	}
	(void)hop_out;
	return decision;
}

int OM_CorpseFreeze(int is_death, int looping, int anim_done, int paused, float since_death)
{
	if (!is_death || looping)
		return 0;
	if (anim_done)
		return 1;
	if (paused)
		return 0;
	if (since_death > OM_CORPSE_FREEZE_S)
		return 1;
	return 0;
}

int OM_BoltPoseOk(int in_sequence, float tx, float ty, float tz)
{
	/* The caller plants 1e30 before GetBoltMatrix. A miss in the test mock leaves it.
	   The real pose call replaces a missing bolt with the identity at the origin. */
	if (tx == 1.0e30f || ty == 1.0e30f || tz == 1.0e30f)
		return 0;
	if (!in_sequence)
		return 0;
	return 1;
}

int OM_CorpseZone(int part_id, int owner_id)
{
	if (owner_id != 0)
		return OM_ZONE_NONE;
	if (part_id == OM_PART_HEAD)
		return OM_ZONE_HEAD;
	if (part_id == OM_PART_LEG)
		return OM_ZONE_LEG;
	if (part_id == OM_PART_BODY)
		return OM_ZONE_BODY;
	return OM_ZONE_NONE;
}
