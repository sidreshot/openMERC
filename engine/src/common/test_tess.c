/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Drives the tessellation class, the weapon scale, and the view-weapon slot.
 * Exit code is the failure count.
 */

#include "header/om_tess.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

static void
expect_int(const char *name, int got, int want)
{
	if (got == want)
	{
		return;
	}

	fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
	failures++;
}

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

/* Eight side directions, each repeated twice: a faceted barrel. */
static void
BarrelNormals(float *xyz)
{
	int i;

	for (i = 0; i < 8; i++)
	{
		float a = (float)i * (3.14159265f / 4.0f);
		float x = cosf(a);
		float y = sinf(a);
		int k;

		for (k = 0; k < 2; k++)
		{
			int v = i * 2 + k;

			xyz[v * 3] = x;
			xyz[v * 3 + 1] = y;
			xyz[v * 3 + 2] = 0.0f;
		}
	}
}

/* Four axis sides: a crate. */
static void
BoxNormals(float *xyz)
{
	static const float dir[4][3] = {{1, 0, 0}, {0, 1, 0}, {-1, 0, 0}, {0, -1, 0}};
	int i;

	for (i = 0; i < 16; i++)
	{
		xyz[i * 3] = dir[i & 3][0];
		xyz[i * 3 + 1] = dir[i & 3][1];
		xyz[i * 3 + 2] = dir[i & 3][2];
	}
}

static void
Class(void)
{
	static const char *list[] = {
		"objects/generic/barrel_chemical/",
		"objects/generic/barrel_burning/",
		"objects/generic/trashcan_barrel/"
	};
	float a[16 * 3], b[16 * 3], box[16 * 3];
	const char *name = "Objects/Generic/Barrel_Chemical/skin.tga";
	const char *crate = "objects/generic/crate_wood/skin.tga";

	BarrelNormals(a);
	memcpy(b, a, sizeof(a));
	BoxNormals(box);

	expect_int("barrel name", OM_TessNameRound(name, list, 3), 1);
	expect_int("same barrel name again", OM_TessNameRound(name, list, 3), 1);
	expect_int("crate name", OM_TessNameRound(crate, list, 3), 0);
	expect_int("barrel normals", OM_TessNormalRound(a, 16), 1);
	expect_int("copy of the barrel normals", OM_TessNormalRound(b, 16), 1);
	expect_int("box normals", OM_TessNormalRound(box, 16), 0);

	/* The sib2 pair: same shape class, same on/off, no per-instance bit. */
	expect_int("pad barrel A", OM_TessClass(name, list, 3, a, 16), 1);
	expect_int("pad barrel B", OM_TessClass(name, list, 3, b, 16), 1);
	expect_int("unlisted barrel still round", OM_TessClass(crate, list, 3, a, 16), 1);
	expect_int("crate stays faceted", OM_TessClass(crate, list, 3, box, 16), 0);
	expect_int("two class calls agree", OM_TessClass(name, list, 3, a, 16), OM_TessClass(name, list, 3, b, 16));
}

static void
Weapon(void)
{
	float delta[3] = {8.0f, 0.0f, 0.0f};
	float out[3];
	float edge = 4.0f;
	float len, k;
	int i;

	expect_int("scale 1", OM_WeaponTessLevel(1.0f), 1);
	expect_int("scale 10", OM_WeaponTessLevel(10.0f), 10);
	expect_int("scale 0 clamps up", OM_WeaponTessLevel(0.0f), 1);
	expect_int("scale 11 clamps down", OM_WeaponTessLevel(11.0f), 10);
	expect_int("scale -4 clamps up", OM_WeaponTessLevel(-4.0f), 1);
	expect_int("scale 5.4", OM_WeaponTessLevel(5.4f), 5);
	expect_int("scale 9.6", OM_WeaponTessLevel(9.6f), 10);

	for (i = 1; i <= 10; i++)
	{
		int s = OM_WeaponSubdiv(i);

		expect_true("subdiv in 2..4", (s >= 2) && (s <= 4));
	}

	expect_true("subdiv grows", OM_WeaponSubdiv(10) >= OM_WeaponSubdiv(1));

	k = OM_WeaponPnK(10);
	expect_true("level 10 PN blend stays under 0.25", (k > 0.0f) && (k < 0.25f));
	expect_true("level 10 is stronger than level 1", OM_WeaponPnK(10) > OM_WeaponPnK(1));

	OM_WeaponOffset(10, edge, delta, out);
	len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	expect_true("scale 10 offset is not zero", len > 0.0f);
	expect_true("scale 10 stays inside the edge bound", len <= OM_WEAPON_PN_EDGE * edge + 1.0e-4f);
	/* Uncapped full PN would move 8 units. The bound on a 4-unit edge is 0.48. */
	expect_true("scale 10 does not apply the raw PN step", len < 1.0f);

	delta[0] = 0.05f;
	OM_WeaponOffset(10, 10.0f, delta, out);
	expect_true("a small step is kept", (out[0] > 0.0f) && (out[0] < OM_WEAPON_PN_EDGE * 10.0f));
}

/* Two faces share a long edge and each has its own short edge. The shared edge
   must clamp by that long edge, or the faces split and a dark line shows. */
static void
SharedEdge(void)
{
	float mid = OM_TessLimit(OM_WEAPON_PN_EDGE, 10.0f, 2.0f, 8.0f, 0.5f, 0.5f, 0.0f);
	float other = OM_TessLimit(OM_WEAPON_PN_EDGE, 10.0f, 3.0f, 7.0f, 0.5f, 0.5f, 0.0f);
	float inside = OM_TessLimit(OM_WEAPON_PN_EDGE, 10.0f, 2.0f, 8.0f, 0.3f, 0.3f, 0.4f);
	float corner = OM_TessLimit(OM_WEAPON_PN_EDGE, 10.0f, 2.0f, 8.0f, 1.0f, 0.0f, 0.0f);

	expect_true("shared edge uses its own length", fabsf(mid - 1.0f) < 1.0e-4f);
	expect_true("the face across the edge agrees", fabsf(other - mid) < 1.0e-4f);
	expect_true("an interior point keeps the short edge", fabsf(inside - OM_WEAPON_PN_EDGE * 2.0f) < 1.0e-4f);
	expect_true("a corner keeps the short edge", fabsf(corner - inside) < 1.0e-4f);
	expect_true("no cap does not clamp", OM_TessLimit(0.0f, 10.0f, 2.0f, 8.0f, 0.5f, 0.5f, 0.0f) > 1000.0f);
}

static void
ViewWeapon(void)
{
	int full0 = OM_ViewWeaponSlot(128, 128, 0.0f);
	int full_turn = OM_ViewWeaponSlot(128, 128, 170.0f);
	int full_wrap = OM_ViewWeaponSlot(128, 128, -179.0f);
	int open0 = OM_ViewWeaponSlot(40, 128, 0.0f);
	int open_turn = OM_ViewWeaponSlot(40, 128, 180.0f);

	expect_int("full list keeps a slot", full0, 127);
	expect_int("yaw 170 keeps the same slot", full_turn, full0);
	expect_int("yaw wrap keeps the same slot", full_wrap, full0);
	expect_true("slot is inside the draw set", (full0 >= 0) && (full0 < 128));
	expect_int("open list appends", open0, 40);
	expect_int("turning does not drop an open slot", open_turn, open0);
	expect_int("overfull still keeps the weapon", OM_ViewWeaponSlot(400, 128, 90.0f), 127);
	expect_int("no room", OM_ViewWeaponSlot(0, 0, 45.0f), -1);
}

int
main(void)
{
	Class();
	Weapon();
	SharedEdge();
	ViewWeapon();
	printf("tess_weapon_test failures=%d\n", failures);
	return failures;
}
