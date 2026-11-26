/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Character self-shadow and geometric specular AA. Calls the shipped
 * functions in om_shade.c (the same math the Ghoul shader is given).
 * Exit code is the failure count.
 */

#include "header/om_shade.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
expect_true(const char *name, int cond)
{
	if (cond)
	{
		return;
	}

	fprintf(stderr, "FAIL %s\n", name);
	failures++;
}

static void
expect_bits(const char *name, float got, float want)
{
	if (memcmp(&got, &want, sizeof(float)) == 0)
	{
		return;
	}

	fprintf(stderr, "FAIL %s: got %.9g want %.9g\n", name, got, want);
	failures++;
}

/* (a) skin stays lit; a hand in the penumbra is a fraction, not a 0/1 compare. */
static void
test_vis(void)
{
	float receiver = 20.0f;
	float skin[4];
	float fold;
	float hand[4];
	float midb;
	float past;
	float vskin, vfold, vhand, vmix, vmid, vpast, vnone;
	float gap, step;
	int i;

	for (i = 0; i < 4; i++)
	{
		skin[i] = receiver;
	}

	vskin = OM_ShadeVis(skin, 4, receiver);
	expect_bits("skin at receiver", vskin, 1.0f);

	fold = receiver - 0.5f * OM_SHADE_ACNE;
	vfold = OM_ShadeVis(&fold, 1, receiver);
	expect_bits("fold inside acne", vfold, 1.0f);

	vnone = OM_ShadeVis(NULL, 0, receiver);
	expect_bits("no blockers", vnone, 1.0f);

	/* Middle of the penumbra band. smoothstep's Hermite at t = 0.5 is 0.5. */
	midb = receiver - (OM_SHADE_ACNE + 0.5f * OM_SHADE_PENUMBRA);
	vmid = OM_ShadeVis(&midb, 1, receiver);
	gap = receiver - midb;
	step = (gap > OM_SHADE_ACNE) ? 0.0f : 1.0f;
	expect_true("hand visibility is soft", (vmid > 0.0f) && (vmid < 1.0f) && (vmid != step));
	expect_bits("penumbra midpoint", vmid, 0.5f);

	/* Two taps on the skin and two on the hand: a mean, not one compare. */
	hand[0] = receiver;
	hand[1] = receiver;
	hand[2] = midb;
	hand[3] = midb;
	vmix = OM_ShadeVis(hand, 4, receiver);
	vhand = OM_ShadeVis(hand + 2, 2, receiver);
	expect_true("mixed taps sit between skin and hand", (vmix < 1.0f) && (vmix > vhand));

	past = receiver - (OM_SHADE_ACNE + OM_SHADE_PENUMBRA + 2.0f);
	vpast = OM_ShadeVis(&past, 1, receiver);
	expect_bits("past the penumbra", vpast, 0.0f);

	/* A nearer blocker occludes more than one farther down the same band. */
	{
		float near = receiver - (OM_SHADE_ACNE + 0.75f * OM_SHADE_PENUMBRA);
		float far = receiver - (OM_SHADE_ACNE + 0.25f * OM_SHADE_PENUMBRA);
		float vn = OM_ShadeVis(&near, 1, receiver);
		float vf = OM_ShadeVis(&far, 1, receiver);

		expect_true("closer blocker is darker", (vn < vf) && (vn > 0.0f) && (vf < 1.0f));
	}
}

/* (b) visibility scales only that light's direct term. */
static void
test_lights(void)
{
	float receiver = 20.0f;
	float midb = receiver - (OM_SHADE_ACNE + 0.5f * OM_SHADE_PENUMBRA);
	float past = receiver - (OM_SHADE_ACNE + OM_SHADE_PENUMBRA + 2.0f);
	float amb = 0.25f;
	float vis = OM_ShadeVis(&midb, 1, receiver);
	float v0 = OM_ShadeVis(&past, 1, receiver);
	float aA, dA, aB, dB, aFull, dFull, aShut, dShut;
	float a0, d0, a1, d1;
	float openI, shutI, open2, shut2, darkI, dark2;
	float directB = 0.25f + 0.25f;

	expect_bits("light vis is the penumbra fraction", vis, 0.5f);

	OM_ShadeLight(amb, 1.0f, 1.0f, vis, &aA, &dA);
	OM_ShadeLight(amb, 0.25f, 0.25f, 1.0f, &aB, &dB);
	OM_ShadeLight(amb, 1.0f, 1.0f, 1.0f, &aFull, &dFull);

	expect_bits("blocked light ambient", aA, amb);
	expect_bits("other light ambient", aB, amb);
	expect_bits("open light ambient", aFull, amb);
	expect_bits("other light direct", dB, directB);
	expect_bits("open direct", dFull, 2.0f);
	expect_bits("hand halves the direct term", dA * 2.0f, dFull);

	/* Fully blocked: the direct term is gone, ambient is the same bits. */
	OM_ShadeLight(amb, 1.0f, 1.0f, v0, &aShut, &dShut);
	expect_bits("removed direct", dShut, 0.0f);
	expect_bits("removed ambient", aShut, amb);

	/* Intensity 0: nothing to darken, open or shut. */
	OM_ShadeLight(amb, 0.0f, 0.0f, vis, &a0, &d0);
	OM_ShadeLight(amb, 0.0f, 0.0f, 1.0f, &a1, &d1);
	expect_bits("zero blocked direct", d0, 0.0f);
	expect_bits("zero open direct", d1, 0.0f);
	expect_bits("zero blocked ambient", a0, amb);
	expect_true("zero intensity adds no darkening", d0 == d1);

	/* 2I removes twice what I removes. The fraction comes from OM_ShadeVis. */
	OM_ShadeLight(amb, 1.0f, 1.0f, 1.0f, &a0, &openI);
	OM_ShadeLight(amb, 1.0f, 1.0f, vis, &a0, &shutI);
	OM_ShadeLight(amb, 2.0f, 2.0f, 1.0f, &a1, &open2);
	OM_ShadeLight(amb, 2.0f, 2.0f, vis, &a1, &shut2);
	darkI = openI - shutI;
	dark2 = open2 - shut2;
	expect_true("blocked light actually darkens", darkI > 0.0f);
	expect_bits("2I darkens twice I", dark2, darkI * 2.0f);
	expect_bits("2I ambient still the input", a1, amb);
}

/* (c) Filament normalFiltering. Zero slope keeps the gloss; a corner lowers the GGX peak. */
static void
test_rough(void)
{
	float zero[3] = {0.0f, 0.0f, 0.0f};
	float du[3] = {1.0f, 0.0f, 0.0f};
	float dv[3] = {0.0f, 0.8f, 0.0f};
	float r0 = OM_ShadeRough(0.5f, zero, zero);
	float rnull = OM_ShadeRough(0.5f, NULL, NULL);
	float r1 = OM_ShadeRough(0.5f, du, dv);
	float peak0, peak1;
	const char *glsl = OM_Shade_Glsl();

	expect_bits("flat normal keeps roughness", r0, 0.5f);
	expect_bits("null derivatives keep roughness", rnull, 0.5f);
	expect_true("corner raises roughness", r1 > r0);

	peak0 = OM_ShadeGGX(1.0f, r0 * r0);
	peak1 = OM_ShadeGGX(1.0f, r1 * r1);
	expect_true("corner GGX peak is lower", (peak1 < peak0) && (peak0 > 0.0f));

	expect_true("glsl has omShadeRough", glsl && strstr(glsl, "omShadeRough"));
	expect_true("glsl has omShadeTap", glsl && strstr(glsl, "omShadeTap"));
	expect_true("glsl has omShadeSkin", glsl && strstr(glsl, "omShadeSkin"));
	expect_true("glsl has omShadeVis4", glsl && strstr(glsl, "omShadeVis4"));
}

/* The desktop lamp raw unit is the fog table. Ghoul must read it elsewhere. */
static void
test_units(void)
{
	int shared = OM_ShadeLampRawUnit(32, 0);
	int ghoul = OM_ShadeGhoulRawUnit(32, shared);
	int i;
	static const int taken[] = {0, 7, 8, 11, 16, 17, 18, 19, 20, 24, 25, 26, 27};

	expect_true("desktop lamp compare is 16", OM_ShadeLampCompareUnit(32, 0) == 16);
	expect_true("desktop lamp raw is 17", shared == 17);
	expect_true("ghoul raw is unit 22", ghoul == 22);

	for (i = 0; i < (int)(sizeof(taken) / sizeof(taken[0])); i++)
	{
		expect_true("ghoul raw avoids a used unit", ghoul != taken[i]);
	}

	expect_true("compact raw stays off the soft depth", OM_ShadeGhoulRawUnit(16, OM_ShadeLampRawUnit(16, 1)) != 7);
	expect_true("compact raw is inside the GPU", OM_ShadeGhoulRawUnit(16, 7) < 16);
}

int
main(void)
{
	test_vis();
	test_lights();
	test_rough();
	test_units();
	printf("shade_selfshadow_test failures=%d\n", failures);
	return failures;
}
