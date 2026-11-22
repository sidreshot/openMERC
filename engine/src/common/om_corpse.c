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
