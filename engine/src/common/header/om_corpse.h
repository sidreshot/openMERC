/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Corpse shot volumes and the airborne death settle.
 * No engine services. Callers pass the body, the ray, and the floor probe.
 *
 * Body space matches AngleVectors: x along forward, y along right, z along up.
 * Yaw 90, pitch 0, roll 0 maps local (10, 0, 0) to world (0, 10, 0).
 */

#ifndef OM_CORPSE_H
#define OM_CORPSE_H

#ifdef __cplusplus
extern "C" {
#endif

#define OM_PART_NONE 0
#define OM_PART_HEAD 1
#define OM_PART_LEG 2
#define OM_PART_BODY 3

typedef struct om_part_s {
	float center[3]; /* body space */
	float radius;
	int id; /* OM_PART_HEAD, OM_PART_LEG, or OM_PART_BODY */
} om_part_t;

/* Which volume the segment hits. 0 is a miss.
   Reversing the segment does not change the part.
   hit_index receives the winning parts[] index when it is not null.
   hit_point receives the closest point on the segment, in world space. */
int OM_CorpsePart(const float origin[3], const float angles[3], const om_part_t *parts, int nparts,
                  const float start[3], const float end[3], int *hit_index, float hit_point[3]);

/* An airborne body lands on the floor this step.
   feet_offset is added to origin[2] to get the feet (usually mins[2]).
   contact nonzero: already supported. Origin stays, vertical velocity is cleared, returns 1.
   have_floor nonzero and contact is 0: the feet are placed on floor_z, velocity is cleared, returns 1.
   Neither: origin and velocity stay, returns 0. */
int OM_DeathSettle(float origin[3], float velocity[3], float feet_offset, int contact, int have_floor,
                   float floor_z);

#ifdef __cplusplus
}
#endif

#endif /* OM_CORPSE_H */
