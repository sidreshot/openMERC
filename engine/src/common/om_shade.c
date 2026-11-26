/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Numeric half of the Ghoul self-shadow and the specular anti-alias.
 * OM_Shade_Glsl() is the same math, pasted in front of the model shader.
 */

#include "header/om_shade.h"

#include <stdio.h>
#include <math.h>

float
OM_ShadeVis(const float *blocker, int n, float receiver)
{
	int i;
	float sum = 0.0f;
	float edge0 = OM_SHADE_ACNE;
	float edge1 = OM_SHADE_ACNE + OM_SHADE_PENUMBRA;

	if (!blocker || (n <= 0))
	{
		return 1.0f;
	}

	for (i = 0; i < n; i++)
	{
		float gap = receiver - blocker[i];
		float t, s;

		/* GLSL smoothstep(edge0, edge1, gap), then 1 - that.
		   Inside the acne band the tap stays fully lit. Past the penumbra
		   it is fully blocked. Between those it is Hermite, not a step. */
		if (gap <= edge0)
		{
			sum += 1.0f;
			continue;
		}

		if (gap >= edge1)
		{
			continue;
		}

		t = (gap - edge0) / (edge1 - edge0);
		s = t * t * (3.0f - 2.0f * t);
		sum += 1.0f - s;
	}

	return sum / (float)n;
}

void
OM_ShadeLight(float ambient, float direct_diff, float direct_spec, float visibility,
	float *out_ambient, float *out_direct)
{
	if (out_ambient)
	{
		*out_ambient = ambient;
	}

	if (out_direct)
	{
		*out_direct = visibility * (direct_diff + direct_spec);
	}
}

float
OM_ShadeRough(float perceptualRoughness, const float du[3], const float dv[3])
{
	float dnx = 0.0f, dny = 0.0f, dnz = 0.0f;
	float dvx = 0.0f, dvy = 0.0f, dvz = 0.0f;
	float variance, roughness, kernel, square;

	/* Google Filament shaders/src/shading_lit.fs, normalFiltering, v1.51.
	   Kaplanyan et al. 2016, "Stable Specular Highlights".
	   Tokuyoshi and Kaplanyan 2019, "Improved Geometric Specular Antialiasing".
	   This is the normal-derivative form (not the half-vector form): the
	   screen-space variance of the shading normal widens only the specular
	   roughness. Filament's derivativesScale is 1 here. Variance 0.15 and
	   threshold 0.2 are Filament's specularAntiAliasing defaults.
	   perceptualRoughnessToRoughness is the square; the return is
	   roughnessToPerceptualRoughness(sqrt(squareRoughness)). */
	if (du)
	{
		dnx = du[0];
		dny = du[1];
		dnz = du[2];
	}

	if (dv)
	{
		dvx = dv[0];
		dvy = dv[1];
		dvz = dv[2];
	}

	variance = OM_SHADE_AA_VARIANCE * ((dnx * dnx + dny * dny + dnz * dnz) + (dvx * dvx + dvy * dvy + dvz * dvz));
	roughness = perceptualRoughness * perceptualRoughness;
	kernel = 2.0f * variance;

	if (kernel > OM_SHADE_AA_THRESHOLD)
	{
		kernel = OM_SHADE_AA_THRESHOLD;
	}

	square = roughness * roughness + kernel;

	if (square < 0.0f)
	{
		square = 0.0f;
	}

	if (square > 1.0f)
	{
		square = 1.0f;
	}

	return sqrtf(sqrtf(square));
}

float
OM_ShadeGGX(float nh, float alpha)
{
	float a2 = alpha * alpha;
	float d = nh * nh * (a2 - 1.0f) + 1.0f;

	return a2 / (3.14159f * d * d);
}

const char *
OM_Shade_Glsl(void)
{
	static char glsl[4096];
	static int ready;
	int n;

	if (ready)
	{
		return glsl;
	}

	n = snprintf(glsl, sizeof(glsl),
		"const float omShadeSkin = %.9g;\n"
		"float omShadeTap(float receiver, float blocker)\n"
		"{\n"
		"	float gap = receiver - blocker;\n"
		"	return 1.0 - smoothstep(%.9g, %.9g, gap);\n"
		"}\n"
		"float omShadeVis4(float receiver, float b0, float b1, float b2, float b3)\n"
		"{\n"
		"	return (omShadeTap(receiver, b0) + omShadeTap(receiver, b1) + omShadeTap(receiver, b2) + omShadeTap(receiver, b3)) * 0.25;\n"
		"}\n"
		/* Filament shading_lit.fs normalFiltering (v1.51). Kaplanyan et al. 2016,
		   "Stable Specular Highlights"; Tokuyoshi and Kaplanyan 2019, "Improved
		   Geometric Specular Antialiasing". Normal derivatives only. Diffuse is
		   not modified; the caller assigns this to specular roughness. */
		"float omShadeRough(float perceptualRoughness, vec3 du, vec3 dv)\n"
		"{\n"
		"	float variance = %.9g * (dot(du, du) + dot(dv, dv));\n"
		"	float roughness = perceptualRoughness * perceptualRoughness;\n"
		"	float kernelRoughness = min(2.0 * variance, %.9g);\n"
		"	float squareRoughness = clamp(roughness * roughness + kernelRoughness, 0.0, 1.0);\n"
		"	return sqrt(sqrt(squareRoughness));\n"
		"}\n",
		(double)OM_SHADE_SKIN,
		(double)OM_SHADE_ACNE,
		(double)(OM_SHADE_ACNE + OM_SHADE_PENUMBRA),
		(double)OM_SHADE_AA_VARIANCE,
		(double)OM_SHADE_AA_THRESHOLD);

	if ((n < 0) || (n >= (int)sizeof(glsl)))
	{
		glsl[0] = 0;
	}

	ready = 1;
	return glsl;
}

int
OM_ShadeLampCompareUnit(int max_units, int compact)
{
	if ((max_units >= 18) && !compact)
	{
		return 16;
	}

	return 11;
}

int
OM_ShadeLampRawUnit(int max_units, int compact)
{
	if ((max_units >= 18) && !compact)
	{
		return 17;
	}

	return 7;
}

/* sampler2D 0 (tex), 7 (soft depth), 17 (region fog).
   sampler2DArrayShadow 8 (sun), 11 and 16 (lamp compare).
   sampler2DArray 18/19, or 2/5 when the GPU has no unit 19.
   samplerBuffer 20 (tessellation). samplerCube 24..27 (probes).
   21 is the glass history copy, a different program. 22 is free. */
static int
ghoul_unit_taken(int max_units, int unit)
{
	if ((unit == 0) || (unit == 7) || (unit == 17))
	{
		return 1;
	}

	if ((unit == 8) || (unit == 11) || (unit == 16))
	{
		return 1;
	}

	if (max_units > 19)
	{
		if ((unit == 18) || (unit == 19))
		{
			return 1;
		}
	}
	else if ((unit == 2) || (unit == 5))
	{
		return 1;
	}

	if ((max_units > 20) && (unit == 20))
	{
		return 1;
	}

	if ((max_units > 27) && (unit >= 24) && (unit <= 27))
	{
		return 1;
	}

	return 0;
}

int
OM_ShadeGhoulRawUnit(int max_units, int shared_raw_unit)
{
	static const int prefer[] = {22, 23, 28, 29, 30, 31, 15, 14, 13, 12, 10, 9, 6, 4, 3, 1};
	int i;

	if (max_units < 1)
	{
		return shared_raw_unit;
	}

	if ((shared_raw_unit >= 0) && (shared_raw_unit < max_units) &&
		!ghoul_unit_taken(max_units, shared_raw_unit))
	{
		return shared_raw_unit;
	}

	for (i = 0; i < (int)(sizeof(prefer) / sizeof(prefer[0])); i++)
	{
		int u = prefer[i];

		if ((u < max_units) && !ghoul_unit_taken(max_units, u))
		{
			return u;
		}
	}

	for (i = max_units - 1; i >= 0; i--)
	{
		if (!ghoul_unit_taken(max_units, i))
		{
			return i;
		}
	}

	return shared_raw_unit;
}
