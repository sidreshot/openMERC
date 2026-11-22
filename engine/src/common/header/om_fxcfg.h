/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * Named graphics tunables (S.T.A.L.K.E.R. 2007 style) and the flare-light
 * decision. No engine services. A missing key keeps the built-in number.
 *
 * Keys: fog_strength, fog_sib, snow_sib1, snow_cap, tess_scale, tess_weapon,
 * tess_models, tess_round, hdr_gamma, hdr_shoulder.
 * A section header prefixes the next names: [fog] strength -> fog_strength.
 */

#ifndef OM_FXCFG_H
#define OM_FXCFG_H

#ifdef __cplusplus
extern "C" {
#endif

#define OM_FXCFG_MAX 32

typedef struct
{
	char key[32];
	float value;
	int set;
} om_fxkv_t;

/* Fill up to maxn pairs. Returns how many keys were stored. */
int OM_FxCfg_Parse(const char *text, om_fxkv_t *kv, int maxn);

float OM_FxCfg_Get(const om_fxkv_t *kv, int n, const char *key, float fallback);
float OM_FxCfg_Default(const char *key);
float OM_FxCfg_Number(const om_fxkv_t *kv, int n, const char *key);

/* Process table. UseText replaces it. Live reads it, else the built-in default. */
void OM_FxCfg_UseText(const char *text);
float OM_FxCfg_Live(const char *key);

/* file_value / built-in default. 1 when the key is missing or still the default,
   so the current picture does not change. sib1/sib2/sib3 stack fog_sib.
   snow_sib1 applies only on sib1. */
float OM_FxCfg_FogMul(const char *mapname);
float OM_FxCfg_SnowMul(const char *mapname);

/* 1 when the sprite name contains "flare" (any case). */
int OM_FxGlowClass(const char *graphic);

/* A positive file radius is kept. A flare sprite of size >= 4 with no file
   light returns a radius that clears the 64-unit shader cutoff. Else 0. */
float OM_FxLightRadius(float file_radius, int glow, float size);

/* Life in milliseconds. A positive life shorter than one frame is raised so
   the light is still drawn. Zero and negative stay 0. */
float OM_FxLightLifeMs(float life_s);

/* 1 when the particle should stay: alpha still visible, or a style 2/3
   particle younger than one frame. 0 when it should be freed. */
int OM_FxKeepRising(float alpha, int astyle, float age_s, int alive);

#ifdef __cplusplus
}
#endif

#endif
