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

/* 1 when this downward hit can hold a corpse.
   kind: 0 world, 1 BSP, 2 ground, 3 a prop box (the subway train), 4 a character, 5 anything else.
   A character is not a floor. A steep face (normal_z below 0.7) is not a floor. */
int OM_CorpseSupport(int kind, float normal_z);

/* 1 when a corpse that is not on a floor must keep the velocity physics gave it.
   The death animation used to clear that velocity every think, so a body with
   nothing under it stayed at the kill height. */
int OM_CorpseKeepFall(int is_corpse, int on_ground);

/* 1 when the ghoul instance may pause on this pose.
   is_death: the current animation is a death. A standing idle (std_istand)
   is not, so the 7 second fallback must not freeze it in the air.
   looping: a dance or a writhe. It is not a rest pose.
   anim_done: the animation has finished.
   paused: an in-air hold is already on. The timeout does not add another
   pause. A finished death still pauses.
   since_death: seconds since the body died.
   A non-looping death pauses when it has finished, or when it is still the
   pose after OM_CORPSE_FREEZE_S seconds. Anything else stays unpaused. */
#define OM_CORPSE_FREEZE_S 7.0f
int OM_CorpseFreeze(int is_death, int looping, int anim_done, int paused, float since_death);

/* Human gore zone indexes for a standing volume that has no owner id.
   Head is the front of the head, leg is the upper right thigh, body is the chest. */
#define OM_ZONE_NONE (-1)
#define OM_ZONE_HEAD 0
#define OM_ZONE_LEG 16
#define OM_ZONE_BODY 7

/* 1 when the bolt matrix is a posed part.
   in_sequence is 0 when the bolt is absent from the current sequence. GetBoltMatrix
   still writes the identity, so the translation is the local origin. That origin is
   not a hit. tx, ty, tz are the translation after the call. A component that is still
   the 1e30 sentinel was never written, which is also not a hit. A bolt that is in the
   sequence is a hit even when it sits on the origin. */
int OM_BoltPoseOk(int in_sequence, float tx, float ty, float tz);

/* Zone index for a volume hit.
   owner_id nonzero: the gore table already names the part, so this returns OM_ZONE_NONE
   and the caller keeps that owner.
   owner_id 0: the standing fallback. OM_PART_HEAD selects OM_ZONE_HEAD, OM_PART_LEG
   selects OM_ZONE_LEG, OM_PART_BODY selects OM_ZONE_BODY. Anything else is OM_ZONE_NONE. */
int OM_CorpseZone(int part_id, int owner_id);

#ifdef __cplusplus
}
#endif

#endif /* OM_CORPSE_H */
