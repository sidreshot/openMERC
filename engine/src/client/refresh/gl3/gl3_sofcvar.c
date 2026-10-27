/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2026 OpenMerc contributors
 *
 * OpenMerc GL3 renderer: the console variables of the Soldier of Fortune
 * renderer with their defaults and flags, and the Ghoul rendering cvars the
 * renderer creates at start-up. Cvars of the Yamagi renderer that SoF does
 * not have are kept private (not registered with the engine) when the
 * renderer runs as the SoF module, so the engine's cvar list matches the
 * original's.
 *
 * Implements: SPEC-413 (cvar table), SPEC-411 (ghl_* cvars), SPEC-AMEND-002 1.1
 */

#include "header/local.h"

cvar_t *gl_picmip, *gl_pictip, *gl_skymip, *r_isf, *r_iaf, *vid_brightness, *vid_contrast;
cvar_t *gl_modulate, *gl_monolightmap, *gl_textureclamp, *fs_createpak;
cvar_t *gl_ext_anisotropic, *gl_scissor_broken, *gl_gentextures_broken;
cvar_t *r_nearclipdist, *r_farclipdist, *gl_clear, *gl_ztrick, *gl_drawmode, *gl_drawflat;
cvar_t *gl_dynamic, *gl_saturatelighting, *gl_dlightintensity, *gl_detailtexturing;
cvar_t *gl_detailtexturingdistance, *gl_offsetfactor, *gl_offsetunits, *gl_effectfill;
cvar_t *gl_showeffectfill, *gl_fogmode, *gl_fogverts, *gl_fogcolor, *gl_fogdensity, *gl_fogstart;
cvar_t *gl_fogend, *gl_fogtime, *gl_fogdensityrange, *r_warnings, *gl_swapinterval;
cvar_t *r_nocull, *gl_lockpvs, *gl_driver, *gl_mode, *gl_bitdepth, *gl_displayrefresh;
cvar_t *gl_allow_cds, *gl_allow_software, *vid_card, *cpu_memory_level, *cpu_speed_level;
cvar_t *cpu_memory_using, *gl_ext_multitexture, *gl_s3_s3tc, *gl_ext_swapinterval;
cvar_t *gl_log, *gl_rj, *sc_framerate, *s3tc_avail;
cvar_t *ghl_mip, *ghl_poly_fact, *ghl_poly_unit, *ghl_specular, *ghl_light_method;

/* gl_modulate as latched at renderer start (SPEC-413 §9) */
float gl3_modulate_latched = 1.0f;

#ifdef OM_SOFREF
/* SPEC-002 import 0x40 with the change callback; set by gl3_sof.c */
extern cvar_t *(*gl3_CvarGetCB)(const char *name, const char *value, int flags, void (*cb)(cvar_t *));
#endif

static cvar_t *
Get(const char *name, const char *value, int flags)
{
	return ri.Cvar_Get(name, (char *)value, flags);
}

/* SPEC-AMEND-002 1.1: r_isf / r_iaf validate themselves when they change */
static void
FormatChanged(cvar_t *cvar)
{
	if (!cvar || !cvar->name)
	{
		return;
	}

	GL3_SoF_CheckFormatCvar(cvar, !Q_stricmp(cvar->name, "r_iaf") ? "GL_ALPHA_FORMAT" : "GL_SOLID_FORMAT");
}

static cvar_t *
GetCB(const char *name, const char *value, int flags)
{
#ifdef OM_SOFREF
	if (gl3_CvarGetCB)
	{
		return gl3_CvarGetCB(name, value, flags, FormatChanged);
	}
#endif
	return Get(name, value, flags);
}

void
GL3_SoF_RegisterCvars(void)
{
	/* start-up, mode and driver */
	gl_driver = Get("gl_driver", "opengl32", CVAR_ARCHIVE);
	gl_mode = Get("gl_mode", "3", CVAR_ARCHIVE);
	gl_bitdepth = Get("gl_bitdepth", "0", CVAR_ARCHIVE);
	gl_displayrefresh = Get("gl_displayrefresh", "0", CVAR_ARCHIVE);
	gl_allow_cds = Get("gl_allow_cds", "0", 0x20);
	gl_allow_cds->value = 1; /* no effect in 1.07f: always as if 1 */
	gl_allow_software = Get("gl_allow_software", "0", 0x20);
	vid_card = Get("vid_card", "[not set yet]", CVAR_ARCHIVE);
	cpu_memory_level = Get("cpu_memory_level", "0", 0);
	cpu_speed_level = Get("cpu_speed_level", "0", 0);
	cpu_memory_using = Get("cpu_memory_using", "0", CVAR_ARCHIVE);
	Get("cpu_amd3d", "0", 0);
	Get("use_amd3d", "1", 0);
	Get("cpu_mmx", "0", 0);
	Get("use_mmx", "1", 0);
	gl_ext_multitexture = Get("gl_ext_multitexture", "1", CVAR_ARCHIVE);
	gl_ext_anisotropic = Get("gl_ext_anisotropic", "1", CVAR_ARCHIVE);
	gl_s3_s3tc = Get("gl_s3_s3tc", "1", CVAR_ARCHIVE);
	gl_ext_swapinterval = Get("gl_ext_swapinterval", "1", CVAR_ARCHIVE);
	gl_swapinterval = Get("gl_swapinterval", "1", CVAR_ARCHIVE);
	gl_log = Get("gl_log", "0", 0);
	gl_scissor_broken = Get("gl_scissor_broken", "0", CVAR_ARCHIVE);
	gl_gentextures_broken = Get("gl_gentextures_broken", "0", CVAR_ARCHIVE);
	gl_rj = Get("gl_rj", "0", CVAR_ARCHIVE);
	sc_framerate = Get("sc_framerate", "30", 0);
	fs_createpak = Get("fs_createpak", "0", 0);

	/* images and colour */
	gl_picmip = Get("gl_picmip", "0", CVAR_ARCHIVE);
	gl_pictip = Get("gl_pictip", "0", CVAR_ARCHIVE);
	gl_skymip = Get("gl_skymip", "0", 0);
	r_isf = GetCB("r_isf", "GL_SOLID_FORMAT", CVAR_ARCHIVE);
	r_iaf = GetCB("r_iaf", "GL_ALPHA_FORMAT", CVAR_ARCHIVE);
	GL3_SoF_CheckFormatCvar(r_isf, "GL_SOLID_FORMAT");
	GL3_SoF_CheckFormatCvar(r_iaf, "GL_ALPHA_FORMAT");
	vid_brightness = Get("vid_brightness", "0.5", CVAR_ARCHIVE);
	vid_contrast = Get("vid_contrast", "0.5", CVAR_ARCHIVE);
	gl_modulate = Get("gl_modulate", "1", CVAR_ARCHIVE);
	gl_monolightmap = Get("gl_monolightmap", "0", CVAR_ARCHIVE);
	gl_textureclamp = Get("gl_textureclamp", "1", 0);

	/* frame, view and debugging */
	r_nocull = Get("r_nocull", "0", 0);
	gl_lockpvs = Get("gl_lockpvs", "0", 0);
	r_warnings = Get("r_warnings", "0", 0);
	r_nearclipdist = Get("r_nearclipdist", "0", 0);
	r_farclipdist = Get("r_farclipdist", "65535.0", 0);
	gl_clear = Get("gl_clear", "0", CVAR_ARCHIVE);
	gl_ztrick = Get("gl_ztrick", "0", 0);
	gl_drawmode = Get("gl_drawmode", "0", 0);
	gl_drawflat = Get("gl_drawflat", "0", 0);
	gl_dynamic = Get("gl_dynamic", "1", 0);
	gl_saturatelighting = Get("gl_saturatelighting", "0", 0);
	gl_dlightintensity = Get("gl_dlightintensity", "2.0", CVAR_ARCHIVE);
	gl_detailtexturing = Get("gl_detailtexturing", "1", CVAR_ARCHIVE);
	gl_detailtexturingdistance = Get("gl_detailtexturingdistance", "1000", CVAR_ARCHIVE);
	gl_offsetfactor = Get("gl_offsetfactor", "-2.0", CVAR_ARCHIVE);
	gl_offsetunits = Get("gl_offsetunits", "-2.0", CVAR_ARCHIVE);
	gl_effectfill = Get("gl_effectfill", "100.0", CVAR_ARCHIVE);
	gl_showeffectfill = Get("gl_showeffectfill", "0.0", 0);
	gl_fogmode = Get("gl_fogmode", "0", CVAR_ARCHIVE);
	gl_fogverts = Get("gl_fogverts", "0", CVAR_ARCHIVE);
	gl_fogcolor = Get("gl_fogcolor", "-1", 0x40);
	gl_fogdensity = Get("gl_fogdensity", "-1", 0x40);
	gl_fogstart = Get("gl_fogstart", "-1", 0x40);
	gl_fogend = Get("gl_fogend", "-1", 0x40);
	gl_fogtime = Get("gl_fogtime", "-1", 0x40);
	gl_fogdensityrange = Get("gl_fogdensityrange", "-1", 0x40);

	/* Ghoul rendering (SPEC-411 cvar table); used by package I4b */
	ghl_light_method = Get("ghl_light_method", "3", CVAR_ARCHIVE);
	ghl_specular = Get("ghl_specular", "1", CVAR_ARCHIVE);
	Get("ghl_max_lights", "3", CVAR_ARCHIVE);
	Get("ghl_fast_lights", "0", CVAR_ARCHIVE);
	Get("ghl_light_multiply", "1.3", CVAR_ARCHIVE);
	Get("ghl_gllight_local_viewer", "1", CVAR_ARCHIVE);
	Get("ghl_gllight_directional", "0", CVAR_ARCHIVE);
	Get("ghl_gllight_attenuation", "1", CVAR_ARCHIVE);
	Get("ghl_gllight_max_lights", "6", CVAR_ARCHIVE);
	Get("ghl_shadows", "2", CVAR_ARCHIVE);
	Get("ghl_player_shadow", "1", CVAR_ARCHIVE);
	Get("ghl_shadow_darkness", ".75", CVAR_ARCHIVE);
	Get("ghl_shadow_dist", "25", CVAR_ARCHIVE);
	Get("ghl_shadow_tilt", ".25", CVAR_ARCHIVE);
	Get("ghl_obj_fog", "1", CVAR_ARCHIVE);
	Get("ghl_no_render", "0", 0);
	Get("ghl_wireframe", "0", 0);
	Get("ghl_no_cull", "0", 0);
	Get("ghl_pre_cull", "1", 0);
	Get("ghl_no_texture_stack", "0", CVAR_ARCHIVE);
	ghl_poly_fact = Get("ghl_poly_fact", "-.1", CVAR_ARCHIVE);
	ghl_poly_unit = Get("ghl_poly_unit", "-.1", CVAR_ARCHIVE);
	ghl_mip = Get("ghl_mip", "0", CVAR_ARCHIVE);
	Get("ghl_precache_verts", "0", 0);
	Get("ghl_precache_texture", "1", 0);

	/* OpenMerc modern GL3 postFX (docs/impl/GFX-modern.md). Owner [pt69u]: bloom, volumetric
	   fog and sun shadows default ON at high quality (each has a menu toggle). No visual
	   change until shaders/FBOs land; names reserved so menus/configs can bind early.
	   Existing CVAR_ARCHIVE configs keep their saved values. */
	Get("r_om_bloom", "1", CVAR_ARCHIVE);
	Get("r_om_bloom_threshold", "0", CVAR_ARCHIVE);
	Get("r_om_bloom_intensity", "0.45", CVAR_ARCHIVE);
	Get("r_om_bloom_radius", "1", CVAR_ARCHIVE);
	Get("r_om_bloom_surfscale", "1", CVAR_ARCHIVE); /* AMEND-016 gatekeeper: SURF_LIGHT is a default bloom seed */
	Get("r_om_bloom_ghoulmin", "0.5", CVAR_ARCHIVE);
	Get("r_om_bloom_fullbright", "1", CVAR_ARCHIVE);
	Get("r_om_bloom_fx", "1", CVAR_ARCHIVE);
	Get("r_om_bloom_fromscene", "0", CVAR_ARCHIVE);
	Get("r_om_volfog", "1", CVAR_ARCHIVE);
	Get("r_om_volfog_quality", "2", CVAR_ARCHIVE); /* high */
	Get("r_om_volfog_intensity", "0.4", CVAR_ARCHIVE);
	Get("r_om_volfog_overlaydim", "0.85", CVAR_ARCHIVE);
	Get("r_om_volfog_shafts", "1", CVAR_ARCHIVE);
	Get("r_om_volfog_noise", "0.25", CVAR_ARCHIVE);
	Get("r_om_csm", "1", CVAR_ARCHIVE);
	Get("r_om_csm_cascades", "4", CVAR_ARCHIVE); /* high */
	Get("r_om_csm_size", "2048", CVAR_ARCHIVE); /* high */
	Get("r_om_csm_soft", "2", CVAR_ARCHIVE);
	Get("r_om_csm_bias", "0.0015", CVAR_ARCHIVE);
	Get("r_om_csm_normalbias", "0.02", CVAR_ARCHIVE);
	Get("r_om_csm_split", "0.75", CVAR_ARCHIVE);
	Get("r_om_csm_maxdist", "2048", CVAR_ARCHIVE);
	Get("r_om_csm_replace_ghl", "1", CVAR_ARCHIVE);
	Get("r_om_csm_strength", "0.65", CVAR_ARCHIVE);
	/* owner [pt55u]: per-pixel dynamic lights, soft particles, per-pixel fog regions and
	   overbright are options and default OFF (pdlight until an A/B brightness match) */
	Get("r_om_pdlight", "1", CVAR_ARCHIVE);
	Get("r_om_pdlight_max", "32", CVAR_ARCHIVE);
	Get("r_om_pdlight_scale", "1", CVAR_ARCHIVE);
	Get("r_om_pdlight_normal", "0", CVAR_ARCHIVE);
	Get("r_om_pdlight_soft", "40", CVAR_ARCHIVE);
	Get("r_om_softparticles", "0", CVAR_ARCHIVE);
	Get("r_om_softparticles_dist", "12", CVAR_ARCHIVE);
	Get("r_om_softparticles_add", "1", CVAR_ARCHIVE);
	Get("r_om_ppfog", "0", CVAR_ARCHIVE);
	Get("r_om_overbright", "0", CVAR_ARCHIVE); /* non-original; menu must label it */
	Get("r_om_overbright_scale", "2", CVAR_ARCHIVE);
	Get("r_om_overbright_models", "1", CVAR_ARCHIVE);

	/* SPEC-413 §1 step 2: r_nearclipdist is set to 4 every time the renderer starts */
	ri.Cvar_Set("r_nearclipdist", "4");
}
