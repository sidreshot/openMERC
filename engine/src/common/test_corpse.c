/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Drives OM_CorpsePart and OM_DeathSettle. Exit code is the failure count.
 */

#include "header/om_corpse.h"

#include <math.h>
#include <stdio.h>

static int failures;

static void expect_int(const char *name, int got, int want)
{
	if (got == want)
		return;
	fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
	failures++;
}

static void expect_near(const char *name, float got, float want)
{
	float d = got - want;
	if (d < 0.0f)
		d = -d;
	if (d <= 0.05f)
		return;
	fprintf(stderr, "FAIL %s: got %.4f want %.4f\n", name, got, want);
	failures++;
}

static void ray(const float origin[3], const float angles[3], const om_part_t *parts, int n, const float a[3],
                const float b[3], int want, const char *name)
{
	float hit[3];
	int idx = -2;
	int id = OM_CorpsePart(origin, angles, parts, n, a, b, &idx, hit);
	float rev_hit[3];
	int rev = OM_CorpsePart(origin, angles, parts, n, b, a, NULL, rev_hit);

	expect_int(name, id, want);
	expect_int(name, rev, want);
	if (want != OM_PART_NONE && idx < 0) {
		fprintf(stderr, "FAIL %s: no part index\n", name);
		failures++;
	}
}

int main(void)
{
	/* Yaw 90, pitch 0, roll 0. AngleVectors forward is (0, 1, 0), right is (1, 0, 0).
	   Local (10, 0, 0) is ten units along forward: world y + 10.
	   Local (0, 10, 0) is ten units along right: world x + 10. */
	const float origin[3] = {100.0f, 200.0f, 50.0f};
	const float yaw90[3] = {0.0f, 90.0f, 0.0f};
	om_part_t parts[2];
	float head_from[3] = {100.0f, 190.0f, 50.0f};
	float head_to[3] = {100.0f, 230.0f, 50.0f};
	float leg_from[3] = {90.0f, 200.0f, 50.0f};
	float leg_to[3] = {130.0f, 200.0f, 50.0f};
	float miss_a[3] = {100.0f, 190.0f, 50.0f};
	float miss_b[3] = {100.0f, 200.0f, 50.0f};
	float far_a[3] = {0.0f, 0.0f, 0.0f};
	float far_b[3] = {1.0f, 0.0f, 0.0f};

	/* Pitch 90, yaw 0, roll 0. forward = (0, 0, -1). Local (10, 0, 0) -> z - 10. */
	const float pitch90[3] = {90.0f, 0.0f, 0.0f};
	float ph_from[3] = {0.0f, 0.0f, 120.0f};
	float ph_to[3] = {0.0f, 0.0f, 70.0f};
	float pl_from[3] = {0.0f, -10.0f, 100.0f};
	float pl_to[3] = {0.0f, -70.0f, 100.0f};
	const float porigin[3] = {0.0f, 0.0f, 100.0f};

	/* Roll 90, yaw 0, pitch 0. right = (0, 0, -1). Local (0, 10, 0) -> z - 10. */
	const float roll90[3] = {0.0f, 0.0f, 90.0f};
	float rl_from[3] = {0.0f, 0.0f, 20.0f};
	float rl_to[3] = {0.0f, 0.0f, -30.0f};
	float rh_from[3] = {-10.0f, 0.0f, 0.0f};
	float rh_to[3] = {30.0f, 0.0f, 0.0f};
	const float rorigin[3] = {0.0f, 0.0f, 0.0f};

	/* Yaw 0. forward = (1, 0, 0). A ray through both volumes hits the nearer one. */
	const float yaw0[3] = {0.0f, 0.0f, 0.0f};
	om_part_t order[2];
	float ofrom[3] = {-10.0f, 0.0f, 0.0f};
	float oto[3] = {50.0f, 0.0f, 0.0f};
	const float ozero[3] = {0.0f, 0.0f, 0.0f};

	float settle_o[3], settle_v[3];
	int supported;

	parts[0].center[0] = 10.0f;
	parts[0].center[1] = 0.0f;
	parts[0].center[2] = 0.0f;
	parts[0].radius = 8.0f;
	parts[0].id = OM_PART_HEAD;
	parts[1].center[0] = 0.0f;
	parts[1].center[1] = 10.0f;
	parts[1].center[2] = 0.0f;
	parts[1].radius = 8.0f;
	parts[1].id = OM_PART_LEG;

	ray(origin, yaw90, parts, 2, head_from, head_to, OM_PART_HEAD, "yaw90 head");
	ray(origin, yaw90, parts, 2, leg_from, leg_to, OM_PART_LEG, "yaw90 leg");
	ray(origin, yaw90, parts, 2, miss_a, miss_b, OM_PART_NONE, "yaw90 short of head");
	ray(origin, yaw90, parts, 2, far_a, far_b, OM_PART_NONE, "yaw90 far");

	parts[0].center[0] = 10.0f;
	parts[0].center[1] = 0.0f;
	parts[0].center[2] = 0.0f;
	parts[1].center[0] = 0.0f;
	parts[1].center[1] = 40.0f;
	parts[1].center[2] = 0.0f;
	ray(porigin, pitch90, parts, 2, ph_from, ph_to, OM_PART_HEAD, "pitch90 head");
	ray(porigin, pitch90, parts, 2, pl_from, pl_to, OM_PART_LEG, "pitch90 leg");

	parts[0].center[0] = 10.0f;
	parts[0].center[1] = 0.0f;
	parts[0].center[2] = 0.0f;
	parts[1].center[0] = 0.0f;
	parts[1].center[1] = 10.0f;
	parts[1].center[2] = 0.0f;
	ray(rorigin, roll90, parts, 2, rh_from, rh_to, OM_PART_HEAD, "roll90 head");
	ray(rorigin, roll90, parts, 2, rl_from, rl_to, OM_PART_LEG, "roll90 leg");

	order[0].center[0] = 30.0f;
	order[0].center[1] = 0.0f;
	order[0].center[2] = 0.0f;
	order[0].radius = 6.0f;
	order[0].id = OM_PART_HEAD;
	order[1].center[0] = 10.0f;
	order[1].center[1] = 0.0f;
	order[1].center[2] = 0.0f;
	order[1].radius = 6.0f;
	order[1].id = OM_PART_LEG;
	/* The segment hits the leg first, then the head. The reversed segment hits the head first. */
	expect_int("nearer leg before head", OM_CorpsePart(ozero, yaw0, order, 2, ofrom, oto, NULL, NULL), OM_PART_LEG);
	expect_int("reversed hits head first", OM_CorpsePart(ozero, yaw0, order, 2, oto, ofrom, NULL, NULL), OM_PART_HEAD);

	/* Killed in the air, floor under the feet. Must not stay at the kill height. */
	settle_o[0] = 10.0f;
	settle_o[1] = 20.0f;
	settle_o[2] = 400.0f;
	settle_v[0] = 30.0f;
	settle_v[1] = 0.0f;
	settle_v[2] = -10.0f;
	supported = OM_DeathSettle(settle_o, settle_v, -24.0f, 0, 1, 100.0f);
	expect_int("airborne supported", supported, 1);
	expect_near("airborne x", settle_o[0], 10.0f);
	expect_near("airborne y", settle_o[1], 20.0f);
	expect_near("airborne feet on floor", settle_o[2] + -24.0f, 100.0f);
	expect_near("airborne not kill height", settle_o[2] == 400.0f ? 400.0f : 0.0f, 0.0f);
	expect_near("airborne vel0", settle_v[0], 0.0f);
	expect_near("airborne vel2", settle_v[2], 0.0f);

	/* Already standing on a floor: do not move the body. */
	settle_o[2] = 124.0f;
	settle_v[0] = 15.0f;
	settle_v[2] = -40.0f;
	supported = OM_DeathSettle(settle_o, settle_v, -24.0f, 1, 0, 0.0f);
	expect_int("contact supported", supported, 1);
	expect_near("contact stays", settle_o[2], 124.0f);
	expect_near("contact keeps run", settle_v[0], 15.0f);
	expect_near("contact drops vz", settle_v[2], 0.0f);

	/* No floor in the probe: the caller keeps falling. The function does not invent a floor. */
	settle_o[2] = 400.0f;
	settle_v[2] = -50.0f;
	supported = OM_DeathSettle(settle_o, settle_v, -24.0f, 0, 0, 0.0f);
	expect_int("no floor", supported, 0);
	expect_near("no floor stays", settle_o[2], 400.0f);
	expect_near("no floor keeps vz", settle_v[2], -50.0f);

	/* Feet sunk through a floor come back up onto it. */
	settle_o[2] = 50.0f;
	settle_v[2] = -5.0f;
	supported = OM_DeathSettle(settle_o, settle_v, -24.0f, 0, 1, 100.0f);
	expect_int("embedded supported", supported, 1);
	expect_near("embedded feet", settle_o[2] - 24.0f, 100.0f);

	/* std_istand is not a death. The 7 s fallback must not freeze it. */
	expect_int("stand idle at 8s does not freeze", OM_CorpseFreeze(0, 1, 1, 0, 8.0f), 0);
	expect_int("stand pose finished does not freeze", OM_CorpseFreeze(0, 0, 1, 0, 8.0f), 0);
	expect_int("non-death at exactly 7s does not freeze", OM_CorpseFreeze(0, 0, 0, 0, 7.0f), 0);
	expect_int("finished death freezes", OM_CorpseFreeze(1, 0, 1, 0, 1.0f), 1);
	expect_int("death still playing under 7s does not freeze", OM_CorpseFreeze(1, 0, 0, 0, 6.9f), 0);
	expect_int("death still the pose past 7s freezes", OM_CorpseFreeze(1, 0, 0, 0, 7.1f), 1);
	expect_int("death at exactly 7s does not freeze yet", OM_CorpseFreeze(1, 0, 0, 0, 7.0f), 0);
	expect_int("looping death does not freeze", OM_CorpseFreeze(1, 1, 1, 0, 8.0f), 0);
	expect_int("paused unfinished death past 7s does not freeze", OM_CorpseFreeze(1, 0, 0, 1, 8.0f), 0);
	expect_int("finished death freezes even when already paused", OM_CorpseFreeze(1, 0, 1, 1, 8.0f), 1);

	/* The subway train is a prop box. Its roof holds a corpse. A character does not. A wall does not. */
	expect_int("world roof supports", OM_CorpseSupport(0, 1.0f), 1);
	expect_int("bsp face at 44 degrees supports", OM_CorpseSupport(1, 0.72f), 1);
	expect_int("ground roof supports", OM_CorpseSupport(2, 0.9f), 1);
	expect_int("prop box roof supports", OM_CorpseSupport(3, 1.0f), 1);
	expect_int("steep prop is not a floor", OM_CorpseSupport(3, 0.69f), 0);
	expect_int("character is not a floor", OM_CorpseSupport(4, 1.0f), 0);
	expect_int("other solid is not a floor", OM_CorpseSupport(5, 1.0f), 0);
	expect_int("downward face is not a floor", OM_CorpseSupport(1, -1.0f), 0);

	/* No floor yet: the death think must not wipe the fall. On a floor, the animation may stop it. */
	expect_int("airborne corpse keeps the fall", OM_CorpseKeepFall(1, 0), 1);
	expect_int("supported corpse does not keep the fall", OM_CorpseKeepFall(1, 1), 0);
	expect_int("living body does not keep the fall", OM_CorpseKeepFall(0, 0), 0);

	/* A bolt missing from the sequence comes back as the identity at the local origin.
	   That origin is not stored, so a ray through it is a miss. A bolt that is in the
	   sequence is kept even when its translation is the origin. The 1e30 sentinel is
	   an unwritten matrix, also a miss. */
	{
		float zorigin[3] = {0.0f, 0.0f, 0.0f};
		float zang[3] = {0.0f, 0.0f, 0.0f};
		float z0[3] = {-40.0f, 0.0f, 0.0f};
		float z1[3] = {40.0f, 0.0f, 0.0f};
		om_part_t stored[1];
		int nstored = 0;
		if (OM_BoltPoseOk(0, 0.0f, 0.0f, 0.0f)) {
			stored[0].center[0] = 0.0f;
			stored[0].center[1] = 0.0f;
			stored[0].center[2] = 0.0f;
			stored[0].radius = 12.0f;
			stored[0].id = OM_PART_HEAD;
			nstored = 1;
		}
		expect_int("missing bolt is not an origin hit",
		           OM_CorpsePart(zorigin, zang, stored, nstored, z0, z1, NULL, NULL), OM_PART_NONE);
	}
	expect_int("bolt at the origin in the sequence is kept", OM_BoltPoseOk(1, 0.0f, 0.0f, 0.0f), 1);
	expect_int("unwritten bolt matrix is not a hit", OM_BoltPoseOk(1, 1.0e30f, 1.0e30f, 1.0e30f), 0);
	expect_int("posed bolt away from the origin is kept", OM_BoltPoseOk(1, 4.0f, -2.0f, 30.0f), 1);

	/* The standing fallback has owner 0. The part id still selects a zone. */
	expect_int("fallback head selects a head zone", OM_CorpseZone(OM_PART_HEAD, 0), OM_ZONE_HEAD);
	expect_int("fallback leg selects a leg zone", OM_CorpseZone(OM_PART_LEG, 0), OM_ZONE_LEG);
	expect_int("fallback body selects a body zone", OM_CorpseZone(OM_PART_BODY, 0), OM_ZONE_BODY);
	expect_int("named owner is not a fallback zone", OM_CorpseZone(OM_PART_HEAD, 42), OM_ZONE_NONE);
	expect_int("empty part is not a zone", OM_CorpseZone(OM_PART_NONE, 0), OM_ZONE_NONE);

	/* A corpse on a slope. The box starts inside the face, the ground check used
	   to drop the floor, and the stuck unstick added a hop. The body landed and
	   hopped again. A supporting face keeps the height and clears that hop.
	   A second step must not pull the body back down. A steeper face is still
	   not a floor: the downhill speed stays, the hop is not added. */
	{
		float slope_o[3] = {4.0f, 8.0f, 80.0f};
		float slope_v[3] = {30.0f, -12.0f, -20.0f};
		float again;
		int decision;

		decision = OM_CorpseSlopeStep(slope_o, slope_v, 1, 0.85f, 1, 1.0f);
		expect_int("slope rest", decision, OM_SLOPE_REST);
		expect_near("slope rest keeps x", slope_o[0], 4.0f);
		expect_near("slope rest keeps the height", slope_o[2], 80.0f);
		expect_near("slope rest clears the hop", slope_v[2], 0.0f);
		expect_near("slope rest stops the crawl", slope_v[0], 0.0f);
		expect_near("slope rest stops the side crawl", slope_v[1], 0.0f);
		again = slope_o[2];
		slope_v[2] = 40.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 1, 0.85f, 1, 1.0f);
		expect_int("slope rest again", decision, OM_SLOPE_REST);
		expect_near("slope rest again stays at that height", slope_o[2], again);
		expect_near("slope rest again does not take an upward speed", slope_v[2], 0.0f);

		slope_o[2] = 50.0f;
		slope_v[0] = 10.0f;
		slope_v[1] = 0.0f;
		slope_v[2] = -8.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 1, 0.72f, 0, 1.0f);
		expect_int("a 44 degree slope rests", decision, OM_SLOPE_REST);
		expect_near("a 44 degree slope keeps the height", slope_o[2], 50.0f);
		expect_near("a 44 degree slope does not rise", slope_v[2], 0.0f);
		expect_int("exactly 45 degrees holds", OM_CorpseSupport(1, OM_CORPSE_SLOPE_MIN), 1);
		expect_int("just steeper than 45 does not hold", OM_CorpseSupport(1, 0.70f), 0);

		slope_o[2] = 40.0f;
		slope_v[0] = 12.0f;
		slope_v[1] = 4.0f;
		slope_v[2] = 25.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 1, 0.69f, 1, 5.0f);
		expect_int("steep face is not a rest", decision, OM_SLOPE_SLIDE);
		expect_int("steep face is still not a floor", OM_CorpseSupport(1, 0.69f), 0);
		expect_near("steep slide keeps the height", slope_o[2], 40.0f);
		expect_near("steep slide keeps the downhill speed", slope_v[0], 12.0f);
		expect_near("steep slide keeps the side speed", slope_v[1], 4.0f);
		expect_near("steep slide does not add the hop", slope_v[2], 0.0f);

		slope_o[2] = 70.0f;
		slope_v[0] = 6.0f;
		slope_v[2] = -10.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 3, 0.69f, 0, 5.0f);
		expect_int("steep touch slides", decision, OM_SLOPE_SLIDE);
		expect_near("steep touch keeps the height", slope_o[2], 70.0f);
		expect_near("steep touch keeps the run", slope_v[0], 6.0f);
		expect_near("steep touch does not raise vz", slope_v[2], -10.0f);

		slope_o[2] = 15.0f;
		slope_v[2] = -5.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 4, 1.0f, 1, 1.0f);
		expect_int("character is not a slope rest", decision, OM_SLOPE_SLIDE);
		expect_near("character slide keeps the height", slope_o[2], 15.0f);

		slope_o[2] = 90.0f;
		slope_v[0] = 0.0f;
		slope_v[2] = 40.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 1, 0.9f, 0, 7.0f);
		expect_int("upward off a slope is still a fall", decision, OM_SLOPE_FALL);
		expect_near("upward fall keeps the height", slope_o[2], 90.0f);
		expect_near("upward fall keeps vz", slope_v[2], 40.0f);

		slope_o[2] = 400.0f;
		slope_v[2] = -50.0f;
		decision = OM_CorpseSlopeStep(slope_o, slope_v, 0, -1.0f, 0, 1.0f);
		expect_int("downward face is still a fall", decision, OM_SLOPE_FALL);
		expect_near("downward face keeps the fall height", slope_o[2], 400.0f);
		expect_near("downward face keeps vz", slope_v[2], -50.0f);
	}

	/* The body lies on the plane. G3_EntityAxes negates pitch and roll, and that
	   up is the face normal. Steeper than 45 degrees does not turn the body. */
	{
		float pitch = 9.0f, roll = 9.0f, up[3];
		float flat[3] = {0.0f, 0.0f, 1.0f};
		float slope[3] = {0.70710678f, 0.0f, 0.70710678f};
		float side[3] = {0.70710678f, 0.0f, 0.70710678f};
		float steep[3] = {0.7193f, 0.0f, 0.6947f};
		const float rad = 0.01745329252f;

		expect_int("flat face aligns", OM_CorpseAlign(37.0f, flat, &pitch, &roll), 1);
		expect_near("flat pitch stays level", pitch, 0.0f);
		expect_near("flat roll stays level", roll, 0.0f);

		pitch = roll = 9.0f;
		expect_int("45 degrees facing uphill aligns", OM_CorpseAlign(0.0f, slope, &pitch, &roll), 1);
		expect_near("45 degrees stores the negated pitch", pitch, -45.0f);
		expect_near("45 degrees stores no roll", roll, 0.0f);
		{
			float sy, cy, sp, cp, sr, cr, P = -pitch, R = -roll;
			sy = sinf(0.0f);
			cy = cosf(0.0f);
			sp = sinf(P * rad);
			cp = cosf(P * rad);
			sr = sinf(R * rad);
			cr = cosf(R * rad);
			up[0] = cr * sp * cy + sr * sy;
			up[1] = cr * sp * sy - sr * cy;
			up[2] = cr * cp;
		}
		expect_near("45 degree up x", up[0], slope[0]);
		expect_near("45 degree up z", up[2], slope[2]);

		pitch = roll = 9.0f;
		expect_int("45 degrees facing across the slope aligns", OM_CorpseAlign(90.0f, side, &pitch, &roll), 1);
		expect_near("across-slope pitch is level", pitch, 0.0f);
		expect_near("across-slope roll is the negated tilt", roll, -45.0f);
		{
			float sy, cy, sp, cp, sr, cr, P = -pitch, R = -roll, yaw = 90.0f;
			sy = sinf(yaw * rad);
			cy = cosf(yaw * rad);
			sp = sinf(P * rad);
			cp = cosf(P * rad);
			sr = sinf(R * rad);
			cr = cosf(R * rad);
			up[0] = cr * sp * cy + sr * sy;
			up[1] = cr * sp * sy - sr * cy;
			up[2] = cr * cp;
		}
		expect_near("across-slope up x", up[0], side[0]);
		expect_near("across-slope up y", up[1], side[1]);
		expect_near("across-slope up z", up[2], side[2]);

		pitch = 4.0f;
		roll = 5.0f;
		expect_int("steeper than 45 does not align", OM_CorpseAlign(0.0f, steep, &pitch, &roll), 0);
		expect_near("steep align keeps pitch", pitch, 4.0f);
		expect_near("steep align keeps roll", roll, 5.0f);

		{
			float n22[3] = {0.0f, 0.3846154f, 0.9230769f};
			float lay_p = 0.0f, lay_r = 0.0f;

			expect_int("22 degree face across the body aligns", OM_CorpseAlign(180.0f, n22, &lay_p, &lay_r), 1);
			expect_near("across 22 degrees stores no pitch", lay_p, 0.0f);
			expect_near("across 22 degrees stores the roll", lay_r, -22.62f);
		}
	}

	/* A fall into a steep face keeps the downhill speed and loses the hop. */
	{
		float n[3] = {0.8660254f, 0.0f, 0.5f};
		float v[3] = {0.0f, 0.0f, -100.0f};
		float up[3] = {0.0f, 0.0f, 40.0f};
		float hold[3] = {3.0f, 0.0f, -20.0f};
		float floor_n[3] = {0.0f, 0.0f, 1.0f};

		expect_int("steep fall is a slide", OM_CorpseSlideVel(n, v), 1);
		expect_near("slide keeps a downhill x", v[0], 43.30127f);
		expect_near("slide keeps going down", v[2], -75.0f);
		expect_int("upward speed on a steep face is cleared", OM_CorpseSlideVel(n, up), 1);
		expect_near("hop x is gone", up[0], 0.0f);
		expect_near("hop z is gone", up[2], 0.0f);
		expect_int("a holding face is not a slide", OM_CorpseSlideVel(floor_n, hold), 0);
		expect_near("holding face keeps the run", hold[0], 3.0f);
		expect_near("holding face keeps the fall", hold[2], -20.0f);
		expect_near("embedded steep face steps out", OM_CorpseUnstick(0.5f, 1), 1.0f);
		expect_near("a holding face does not step out", OM_CorpseUnstick(0.9f, 1), 0.0f);
		expect_near("a free steep face does not step out", OM_CorpseUnstick(0.5f, 0), 0.0f);
	}

	/* Plant along the normal. A flat floor is the old straight drop. A point
	   already through the plane is not pulled up. */
	{
		float origin[3] = {10.0f, 20.0f, 80.0f};
		float plane[3] = {10.0f, 20.0f, 40.0f};
		float floor_n[3] = {0.0f, 0.0f, 1.0f};
		float slope_n[3] = {0.70710678f, 0.0f, 0.70710678f};
		float slope_p[3] = {10.0f, 20.0f, 40.0f};
		float under[3] = {10.0f, 20.0f, 10.0f};
		float under_p[3] = {10.0f, 20.0f, 40.0f};

		expect_int("flat plant drops", OM_CorpsePlant(origin, -20.0f, plane, floor_n), 1);
		expect_near("flat plant x stays", origin[0], 10.0f);
		expect_near("flat plant meets the floor", origin[2], 60.0f);

		origin[0] = 10.0f;
		origin[1] = 20.0f;
		origin[2] = 80.0f;
		expect_int("slope plant follows the normal", OM_CorpsePlant(origin, -20.0f, slope_p, slope_n), 1);
		expect_near("slope plant shifts downhill", origin[0], 10.0f - 0.70710678f * 14.142136f);
		expect_near("slope plant lowers along the normal", origin[2], 80.0f - 0.70710678f * 14.142136f);

		origin[2] = 10.0f;
		expect_int("a bolt under the plane is not lifted", OM_CorpsePlant(under, -20.0f, under_p, floor_n), 0);
		expect_near("under plane z stays", under[2], 10.0f);
	}

	/* A heel over the drop is walked back toward the pelvis, along the plane. */
	{
		float origin[3] = {10.0f, 0.0f, 5.0f};
		float pelvis[3] = {0.0f, 0.0f, 10.0f};
		float heel[3] = {40.0f, 0.0f, 10.0f};
		float floor_n[3] = {0.0f, 0.0f, 1.0f};
		float steep[3] = {0.8660254f, 0.0f, 0.5f};
		float near_heel[3] = {4.0f, 0.0f, 10.0f};

		expect_int("heel over the drop is pulled on", OM_CorpsePullOn(origin, heel, pelvis, floor_n, 8.0f), 1);
		expect_near("pull walks back along the plane", origin[0], 2.0f);
		expect_near("pull does not lift", origin[2], 5.0f);
		expect_int("a heel next to the pelvis stays", OM_CorpsePullOn(origin, near_heel, pelvis, floor_n, 8.0f), 0);
		expect_int("a steep face is not a pull", OM_CorpsePullOn(origin, heel, pelvis, steep, 8.0f), 0);
	}

	/* Gravity on a steep face points downhill. A floor has no slide. */
	{
		float n[3] = {0.8660254f, 0.0f, 0.5f};
		float floor_n[3] = {0.0f, 0.0f, 1.0f};
		float dir[3] = {9.0f, 9.0f, 9.0f};

		expect_int("60 degrees points downhill", OM_CorpseDownhill(n, dir), 1);
		expect_near("downhill x", dir[0], 0.5f);
		expect_near("downhill z", dir[2], -0.8660254f);
		dir[0] = 4.0f;
		expect_int("a holding face has no downhill", OM_CorpseDownhill(floor_n, dir), 0);
		expect_near("holding downhill stays", dir[0], 4.0f);
	}

	if (failures)
		fprintf(stderr, "corpse_hit_settle_test failures=%d\n", failures);
	else
		printf("corpse_hit_settle_test failures=0\n");
	return failures;
}
