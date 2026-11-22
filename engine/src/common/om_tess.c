/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * See header/om_tess.h. No engine services.
 */

#include "header/om_tess.h"

#include <math.h>
#include <string.h>

static int
Lower(int c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return c + ('a' - 'A');
	}

	return c;
}

static int
Contains(const char *hay, const char *needle)
{
	if (!hay || !needle || !needle[0])
	{
		return 0;
	}

	for (; *hay; hay++)
	{
		const char *h = hay;
		const char *n = needle;

		while (*n && *h && (Lower((unsigned char)*h) == Lower((unsigned char)*n)))
		{
			h++;
			n++;
		}

		if (!*n)
		{
			return 1;
		}
	}

	return 0;
}

int
OM_TessNameRound(const char *name, const char *const *list, int nlist)
{
	int i;

	if (!name || !list || (nlist <= 0))
	{
		return 0;
	}

	for (i = 0; i < nlist; i++)
	{
		if (Contains(name, list[i]))
		{
			return 1;
		}
	}

	return 0;
}

int
OM_TessNormalRound(const float *xyz, int nverts)
{
	int bins[16];
	int i, horiz, occ, maxb;

	/* A character is hundreds of vertices. A barrel, a bin, a can is a short shell. */
	if (!xyz || (nverts < 12) || (nverts > 768))
	{
		return 0;
	}

	memset(bins, 0, sizeof(bins));
	horiz = 0;

	for (i = 0; i < nverts; i++)
	{
		float nx = xyz[i * 3];
		float ny = xyz[i * 3 + 1];
		float nz = xyz[i * 3 + 2];
		float h2 = nx * nx + ny * ny;
		float l2 = h2 + nz * nz;
		float ang;
		int b;

		if (l2 < 1.0e-8f)
		{
			continue;
		}

		/* Side of a barrel, not a lid and not a diagonal of a limb. */
		if (h2 < 0.55f * l2)
		{
			continue;
		}

		horiz++;
		ang = atan2f(ny, nx); /* -pi .. pi */
		b = (int)floorf((ang + 3.14159265f) * (16.0f / (2.0f * 3.14159265f)));

		if (b < 0)
		{
			b = 0;
		}

		if (b > 15)
		{
			b = 15;
		}

		bins[b]++;
	}

	/* Most of the shell faces outward, not up. */
	if (horiz * 2 < nverts)
	{
		return 0;
	}

	occ = 0;
	maxb = 0;

	for (i = 0; i < 16; i++)
	{
		if (bins[i])
		{
			occ++;
		}

		if (bins[i] > maxb)
		{
			maxb = bins[i];
		}
	}

	/* A box has four sides. A barrel has six or more. */
	if (occ < 6)
	{
		return 0;
	}

	/* One face holding a third of the ring is a wall or a chest, not a barrel. */
	if (maxb * 3 > horiz)
	{
		return 0;
	}

	return 1;
}

int
OM_TessClass(const char *name, const char *const *list, int nlist, const float *xyz, int nverts)
{
	if (OM_TessNameRound(name, list, nlist))
	{
		return 1;
	}

	return OM_TessNormalRound(xyz, nverts);
}

int
OM_WeaponTessLevel(float requested)
{
	int i = (int)floorf(requested + 0.5f);

	if (i < 1)
	{
		return 1;
	}

	if (i > 10)
	{
		return 10;
	}

	return i;
}

int
OM_WeaponSubdiv(int level)
{
	level = OM_WeaponTessLevel((float)level);

	if (level <= 3)
	{
		return 2;
	}

	if (level <= 7)
	{
		return 3;
	}

	return 4;
}

float
OM_WeaponPnK(int level)
{
	level = OM_WeaponTessLevel((float)level);

	/* 0.04 at 1, 0.18 at 10. 1.0 is the full PN surface that inflates the gun. */
	return 0.04f + (float)(level - 1) * (0.14f / 9.0f);
}

void
OM_WeaponOffset(int level, float edge, const float delta[3], float out[3])
{
	float k, lim, len;
	int i;

	if (!out)
	{
		return;
	}

	out[0] = out[1] = out[2] = 0.0f;

	if (!delta || !(edge > 0.0f))
	{
		return;
	}

	k = OM_WeaponPnK(level);

	for (i = 0; i < 3; i++)
	{
		out[i] = k * delta[i];
	}

	len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
	lim = OM_WEAPON_PN_EDGE * edge;

	if ((len > lim) && (len > 0.0f))
	{
		float s = lim / len;

		for (i = 0; i < 3; i++)
		{
			out[i] *= s;
		}
	}
}

int
OM_ViewWeaponSlot(int count, int limit, float yaw)
{
	(void)yaw;

	if (limit < 1)
	{
		return -1;
	}

	if (count < 0)
	{
		count = 0;
	}

	if (count < limit)
	{
		return count;
	}

	return limit - 1;
}
