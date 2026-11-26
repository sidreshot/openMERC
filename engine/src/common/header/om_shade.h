/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Character self-shadow and geometric specular anti-aliasing.
 * No engine services. The renderer copies OM_Shade_Glsl() into the Ghoul
 * fragment shader, so the numbers below are the ones the GPU uses.
 *
 * A blocker within OM_SHADE_ACNE of the receiver is the surface itself and
 * stays fully lit. A hand or a weapon farther off reduces that one light by
 * a filtered fraction (OM_ShadeVis), and OM_ShadeLight applies the fraction
 * only to that light's direct diffuse and specular.
 *
 * Specular roughness uses the normal-derivative filter from Google Filament
 * shading_lit.fs normalFiltering (v1.51): Kaplanyan et al. 2016, "Stable
 * Specular Highlights", and Tokuyoshi and Kaplanyan 2019, "Improved Geometric
 * Specular Antialiasing". OM_SHADE_AA_VARIANCE (0.15) and
 * OM_SHADE_AA_THRESHOLD (0.2) are Filament's specularAntiAliasing defaults.
 */

#ifndef OM_SHADE_H
#define OM_SHADE_H

#ifdef __cplusplus
extern "C" {
#endif

/* World units (inches). The receiver is moved this far along its normal and
   toward the light before the depth test, so the skin does not occlude itself
   and a hand or a weapon that stands off the body still does. */
#define OM_SHADE_SKIN 1.5f

/* A blocker this close to the receiver, in light-space distance, is acne. */
#define OM_SHADE_ACNE 1.0f

/* The occluder fades across this many units instead of a hard depth compare. */
#define OM_SHADE_PENUMBRA 3.5f

/* Filament specularAntiAliasingVariance and specularAntiAliasingThreshold. */
#define OM_SHADE_AA_VARIANCE 0.15f
#define OM_SHADE_AA_THRESHOLD 0.2f

/* Mean of the per-tap smoothstep. n <= 0 or a null list is fully lit.
   Blockers are distances from the light in the same units as receiver;
   a smaller blocker is closer to the light. */
float OM_ShadeVis(const float *blocker, int n, float receiver);

/* out_ambient is the ambient input, bit for bit. out_direct is
   visibility * (direct_diff + direct_spec). Visibility does not scale ambient. */
void OM_ShadeLight(float ambient, float direct_diff, float direct_spec, float visibility,
	float *out_ambient, float *out_direct);

/* Filament normalFiltering. du and dv are the screen-space derivatives of the
   shading normal (dFdx / dFdy). Zero derivatives return perceptualRoughness
   (within float error). The result is specular roughness only. */
float OM_ShadeRough(float perceptualRoughness, const float du[3], const float dv[3]);

/* GGX normal distribution, the same expression as omGGX in the Ghoul shader.
   alpha is roughness squared, matching `a = rough * rough` there. */
float OM_ShadeGGX(float nh, float alpha);

/* GLSL functions omShadeTap, omShadeVis4, omShadeRough and the constant
   omShadeSkin. Built from the macros above. */
const char *OM_Shade_Glsl(void);

/* Lamp shadow texture units. compact is r_om_lshadow_compact.
   Desktop (18 or more units, compact off): compare 16, raw 17.
   Otherwise: compare 11, raw 7. */
int OM_ShadeLampCompareUnit(int max_units, int compact);
int OM_ShadeLampRawUnit(int max_units, int compact);

/* Unit for the Ghoul program's omLsRaw (sampler2DArray).
   shared_raw_unit is OM_ShadeLampRawUnit. That unit is already a sampler2D
   in this program: the region-fog table on 17, or the soft-add depth on 7.
   Two sampler types on one unit make glDrawArrays return GL_INVALID_OPERATION.
   The result is a unit this program does not use for a different sampler. */
int OM_ShadeGhoulRawUnit(int max_units, int shared_raw_unit);

#ifdef __cplusplus
}
#endif

#endif
