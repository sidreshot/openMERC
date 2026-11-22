/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Drives OM_CorpsePart and OM_DeathSettle. Exit code is the failure count.
 */

#include "header/om_corpse.h"

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

	if (failures)
		fprintf(stderr, "corpse_hit_settle_test failures=%d\n", failures);
	else
		printf("corpse_hit_settle_test failures=0\n");
	return failures;
}
