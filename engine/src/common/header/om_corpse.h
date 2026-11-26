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

/* cos(45 degrees). A face at least this upright can hold a corpse.
   Steeper than 45 degrees slides. A flat floor is 1. */
#define OM_CORPSE_SLOPE_MIN 0.70710678f

/* 1 when this downward hit can hold a corpse.
   kind: 0 world, 1 BSP, 2 ground, 3 a prop box (the subway train), 4 a character, 5 anything else.
   A character is not a floor. A face steeper than 45 degrees (normal_z below
   OM_CORPSE_SLOPE_MIN) is not a floor. */
int OM_CorpseSupport(int kind, float normal_z);

/* Pitch and roll to store in s.angles so the body lies on the plane.
   G3_EntityAxes negates those two before AngleVectors, and that up matches normal.
   Yaw is kept. Returns 1 when the face can hold the body.
   Returns 0 when the face is steeper than 45 degrees or points down:
   pitch and roll are left unchanged. normal does not have to be unit length. */
int OM_CorpseAlign(float yaw, const float normal[3], float *pitch, float *roll);

/* Take the velocity component that points into a face steeper than 45 degrees.
   The part along the plane stays, so a falling body keeps moving downhill.
   An upward speed is cleared: it is the hop. The origin is not moved.
   Returns 1 when normal is a slide face. A holding face and a downward face
   return 0 and leave velocity alone. */
int OM_CorpseSlideVel(const float normal[3], float velocity[3]);

/* How far to step out of a steep face along its normal, in world units.
   1 when the box is embedded in a slide face. 0 when the face can hold the
   body, points down, or the box is not embedded. A holding face must not be
   pushed back out: that push is the hop. */
float OM_CorpseUnstick(float normal_z, int embedded);

/* Move origin along the normal so the point `feet` below the origin (feet is
   negative) meets the plane through plane_point. Only toward the plane, and
   only when the face can hold the body and the gap is between 2 and 80.
   A flat floor moves straight down. A slope moves along the normal, not
   straight down into the face. A point already under the plane is not lifted.
   Returns 1 when origin changed. */
int OM_CorpsePlant(float origin[3], float feet, const float plane_point[3], const float normal[3]);

/* How far the pelvis may sit above the plane so a drawn part can meet it.
   A deeper part stays under: lifting the torso by a dangling hand is the hop. */
#define OM_CORPSE_LIMB_CAP 16.0f

/* Move origin along a holding face.
   support_above and limb_above are signed distances above the plane, positive
   above it. The lower of the two meets the plane. When that would leave the
   support more than OM_CORPSE_LIMB_CAP above the plane, the lift stops there.
   A move already under 0.5 is left alone, so the same distances do not move
   again. A drop past 80 is another floor. An upward move past 48 is ignored.
   A face steeper than 45 degrees is ignored. Returns 1 when origin changed. */
int OM_CorpseFit(float origin[3], const float normal[3], float support_above, float limb_above);

/* Push origin away from other along the plane until they are `keep` apart.
   At most `step` units this call. No component along the normal: the other
   body is not a floor, and the shift is not a hop. Already `keep` apart, a
   steep face, or a step under 1 returns 0. Two origins on the same point step
   along the plane. Returns 1 when origin changed. */
int OM_CorpseSeparate(float origin[3], const float other[3], const float normal[3], float keep, float step);

/* 1 when the two world boxes cover each other in the horizontal plane by more
   than 4 units on both axes. A prone body is longer than the gap between
   origins, so that gap alone still hides one mesh inside the other. */
int OM_CorpseStacked(const float a0[3], const float a1[3], const float b0[3], const float b1[3]);

/* One step of origin along the plane, out of the other box, on the shorter
   overlap. The boxes stay put: the caller moves them with the origin next
   time. No component along the normal. A clear pair, a steep face, or a step
   under 1 returns 0. */
int OM_CorpseClear(float origin[3], const float mine0[3], const float mine1[3], const float other0[3],
                   const float other1[3], const float normal[3], float step);

/* Shift origin in the plane so `from` moves toward `toward` by `step` units.
   A heel over the drop uses the pelvis as `toward`, and the body comes back
   onto the snow. A holding face only. No shift along the normal, so a hand
   under the plane stays under it. Returns 1 when origin changed. */
int OM_CorpsePullOn(float origin[3], const float from[3], const float toward[3], const float normal[3],
                    float step);

/* Unit direction a body slides on a steep face: gravity projected onto the
   plane, pointing downhill. Returns 1 when the face is a slide. A holding
   face and a downward face return 0 and leave `dir` unchanged. */
int OM_CorpseDownhill(const float normal[3], float dir[3]);

/* 1 when a corpse that is not on a floor must keep the velocity physics gave it.
   The death animation used to clear that velocity every think, so a body with
   nothing under it stayed at the kill height. */
int OM_CorpseKeepFall(int is_corpse, int on_ground);

/* What a corpse does with a face its box is touching.
   OM_SLOPE_FALL: not a slope decision. Origin and velocity stay.
   OM_SLOPE_REST: the face can hold the body (OM_CorpseSupport). Clear velocity,
   keep the origin, keep the ground. Do not add a hop.
   OM_SLOPE_SLIDE: the face is too steep to be a floor. Keep the downhill speed,
   drop any upward speed, and do not add a hop. Do not treat it as ground.
   kind and normal_z match OM_CorpseSupport. embedded is 1 when the movement box
   already intersects the face. vz is the vertical speed (positive is up).
   hop is the upward kick a stuck-box unstick would add. vz_out and hop_out may
   be null. A supporting face rests when the box is embedded or vz is not upward.
   An upward speed on a supporting face the box has already left is a fall. */
#define OM_SLOPE_FALL 0
#define OM_SLOPE_REST 1
#define OM_SLOPE_SLIDE 2
int OM_CorpseSlope(int kind, float normal_z, int embedded, float vz, float hop, float *vz_out, float *hop_out);

/* One slope decision applied to a body. The origin is never moved, so a second
   call on the same face cannot pull the body back to an earlier height.
   REST clears velocity. SLIDE removes an upward vz and does not add hop.
   FALL leaves origin and velocity unchanged. Returns the decision. */
int OM_CorpseSlopeStep(float origin[3], float velocity[3], int kind, float normal_z, int embedded, float hop);

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
